# AGENTS.md — keyboard-volume-app

Comprehensive project docs are in `CLAUDE.md`. This file covers only the sharp edges an agent would otherwise guess wrong.

## Git Workflow (mandatory)

- **Never commit directly to `main`** unless explicitly asked by the user.
- Always create a branch from the latest `origin/main`.
- Use branch prefixes: `feature/`, `fix/`, `refactor/`, `docs/`, `chore/`.
- Push the branch and open a PR to `main`.
- Never force-push to `main`.
- Never delete branches without explicit consent.
- Never rewrite published history without explicit consent.
- Before opening a PR, run relevant build/tests if the change warrants it.
- For risky areas (evdev, libpulse, D-Bus, MPRIS, threading, CMake, config migration), add a short risk/rollback note in the PR description.

## Branch Layout

| Branch | Purpose |
|---|---|
| `main` | Primary C++/Qt6 implementation |
| `python-legacy` | Archived Python/PyQt6 implementation |
| `python-last` | Tag pointing to last Python `main` commit |
| `cpp-rewrite` | Preserved migration branch, inactive for new changes |

## Build & Run

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build -j$(nproc)
cpp/build/keyboard-volume-app
```

User must be in the `input` group for evdev access (`sudo usermod -aG input $USER`, re-login).

## Environment — Wayland

The app reads `WAYLAND_DISPLAY` / `XDG_SESSION_TYPE` at startup. On Wayland, regular `QWidget::move()` is ignored by compositors, so OSD positioning must use one of two guarded paths:

- If built with `wayland-client` + `LayerShellQt >= 6.6` and the compositor advertises `zwlr_layer_shell_v1`, keep Qt on native Wayland and position only the OSD through `LayerShellQt::Window::get()`.
- Otherwise, when `QT_QPA_PLATFORM` is unset, force `xcb` (XWayland) so the existing `move()` / `QWindow::setPosition()` fallback still works.

Do not remove this startup decision logic from `main.cpp`, and do not set `QT_WAYLAND_SHELL_INTEGRATION=layer-shell` globally; that would turn dialogs and other windows into layer-shell surfaces too.

## Hotkey bindings are evdev, not Qt

Config `hotkeys` and all internal key handling use evdev bindings. Legacy integer values still mean **EV_KEY Linux evdev key codes** (e.g. `KEY_VOLUMEUP` = 115). New scroll bindings are stored as objects such as `{ "type": "rel", "code": 8, "direction": 1 }` for `EV_REL / REL_WHEEL`. Conversion from X11 keycodes: `evdev = X11_keycode − 8`. The `KeyCaptureDialog` in `settingsdialog.cpp` uses two capture paths in parallel: evdev thread for media keys and scroll, `QKeyEvent::nativeScanCode()` for regular keys.

Key repeat events (`ev.value == 2`) are handled alongside regular press events (`ev.value == 1`) in `InputHandler::run()`, with 100ms debounce per hotkey binding.

## Threading — critical rules

- **`InputHandler` IS a `QThread`** — direct subclass, runs `epoll()` in `run()` (50ms timeout).
- **`PaWorker` uses `moveToThread()`** — not a subclass. All PulseAudio calls go through `QMetaObject::invokeMethod` targeting the PaWorker thread. **Never call libpulse from the main thread** — it will block the Qt event loop and freeze the tray menu.
- **`std::atomic<bool>`** used for all thread-shared flags (`m_running`, `m_stopping`) — never `volatile bool`.
- **`EvdevDevice`** (RAII, move-only) in `evdevdevice.h/cpp` — manages fd, `libevdev*`, grab/ungrab, and `libevdev_uinput*` with automatic cleanup in destructor. Used by `InputHandler`, `DeviceSelectorDialog`, and `FirstRunWizard`.

## VolumeController fallback strategy

4 levels, in hot-path order:
1. Active sink input (libpulse, ~0.5ms)
2. Stream restore DB (libpulse)
3. PipeWire node via libpipewire — used only after libpulse active-input and stream-restore paths
4. Pending watcher — queues volume/mute in `PaWorker` for when app reconnects

Idle-app listing and PW-node fallback use libpipewire directly, not `pw-dump`/`pw-cli` subprocesses. The fast path touches only libpulse.

If the PulseAudio context fails or terminates, `PaWorker` reconnects with backoff and `PaWatcherThread` rebuilds its sink-input subscription. Do not move reconnect, cleanup, or libpulse calls to the main thread. Transient app-list refreshes during reconnect must not overwrite `Config::selectedApp()`; keep the user's configured app even if it temporarily disappears from the list.

## OSD painting quirk

Qt skips stylesheet background painting for translucent top-level windows (`WA_TranslucentBackground`). The OSD background is drawn manually in `OSDWindow::paintEvent()` via `QPainter::drawRoundedRect()`. Do not try to set OSD background via stylesheet — it won't render.

After `show()`, the X11/XWayland path also sets position via `QWindow::setPosition()` on `windowHandle()`. The native Wayland path updates layer-shell margins instead of calling `move()`.

## D-Bus / MPRIS

The app registers two D-Bus services on the session bus:

| Service | Object path | Interface |
|---|---|---|
| `org.keyboardvolumeapp` | `/org/keyboardvolumeapp` | `org.keyboardvolumeapp.VolumeControl` |
| `org.mpris.MediaPlayer2.keyboardvolumeapp` | `/org/mpris/MediaPlayer2` | `org.mpris.MediaPlayer2` + `.Player` |

- **`DbusInterface`** — `QObject` with `Q_CLASSINFO`, registered directly. Caches volume/mute/active-app/apps from `VolumeController`/`TrayApp` signals. D-Bus setters delegate to `VolumeController` async.
- **MPRIS** — separate `QObject` endpoint with `MprisRootAdaptor` and `MprisPlayerAdaptor` (`QDBusAbstractAdaptor` subclasses). **Must include `ExportAdaptors` flag** when registering — Qt6 auto-detects adaptor children.
- **`QDBusConnection::sessionBus()` returns by value** in Qt6 (not `&`). Write `auto bus = QDBusConnection::sessionBus();`, not `auto &bus`.
- `cleanup()` unregisters both objects and services via `bus.unregisterObject()` / `bus.unregisterService()`.
- `Qt6::DBus` is a separate CMake component — requires `find_package(Qt6 REQUIRED COMPONENTS ... DBus)`.
- The MPRIS `Volume` property maps to `DbusInterface::volume()`. `PlaybackStatus` is always `"Stopped"`. Play/Pause/Next/Previous are no-ops. `Quit` → `qApp->quit()`.

## Icon / QRC

The tray icon is embedded as a Qt resource: `cpp/resources.qrc` maps `../resources/icon.png` to `:/icon.png`. `CMAKE_AUTORCC` is ON so the `.qrc` only needs to be listed in `SOURCES`. Do not add separate `POST_BUILD` copy commands — the icon is already in the binary.

## Tests

Unit tests are in `cpp/tests/`, integrated with CTest:
- `test_config` — 32 tests (merge, load/save, atomic save failure, thread-safety, profile migration / round-trip / mirror / scroll hotkeys / show hotkey / id uniqueification)
- `test_i18n` — 7 tests (lookup, fallback)
- `test_kvctlcommand` — 9 tests (subcommand parser, profile option, get/set fields, show command, invalid input)
- `test_pwutils` — 3 tests (PipeWire client filtering, skipped-name fallback, deduplication)
- `test_volumecontroller` — 5 smoke tests
- `test_inputhandler` — 26 tests (API, evdev device listing, modifier normalize, `resolveProfile` specificity, scroll binding matching, show volume action)

Run locally: `cd cpp/build && ctest --output-on-failure`.

GitHub Actions CI is enabled in `.github/workflows/ci.yml` for PRs and pushes to
`main`. It builds and runs CTest in both Debug and Release, and checks
`clang-format` only for changed C++ files under `cpp/src` and `cpp/tests`.
All C++ changes in `cpp/src` and `cpp/tests` must be `clang-format` compliant.
Before opening a PR that touches those paths, check the changed files with:
`clang-format --dry-run --Werror <changed-cpp-files>`. If the check fails, run
`clang-format -i` only on the changed C++ files rather than formatting the whole
repo unnecessarily.
The CI workflow is path-filtered: docs-only changes such as Markdown updates do
not run CI, while changes under `cpp/`, `pkg/`, `deploy/`, `resources/`, CMake
files, or `.github/workflows/ci.yml` do.
`clang-tidy` is not part of CI yet. `Claude Code Review` is currently
temporarily disabled via `if: false` in `.github/workflows/claude-code-review.yml`.
