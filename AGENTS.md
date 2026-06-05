# AGENTS.md — keyboard-volume-app

Comprehensive project docs are in `ARCHITECTURE.md`. This file covers only the sharp edges an agent would otherwise guess wrong.

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

## Documentation

- For every user-visible or operational change, check whether docs need updating.
- Update `README.md` for end-user behavior, setup, configuration, CLI/D-Bus examples, and troubleshooting.
- Update `ARCHITECTURE.md` for architecture, build/test recipes, branch layout, sharp implementation details, and maintainer workflows.
- Update `CLAUDE.md` only for Claude-specific quick-start guidance.
- Update `AGENTS.md` only for agent-specific guardrails or mistakes future agents are likely to make.
- If no docs update is needed, mention that explicitly in the PR description.

## Branch Layout

See **Branch Layout** in `ARCHITECTURE.md` for the full table (`main`, `python-legacy`, `python-last`, `cpp-rewrite`).

## Branch hygiene

- Working branches on origin must use `feature/`, `fix/`, `refactor/`, `docs/`, or `chore/` prefixes — never push `claude/*` or other agent-generated names to origin.
- When starting work from an agent branch, rename to the proper prefix before opening a PR.
- GitHub is configured to **automatically delete head branches after merge**; do not rely on stale remote branches as long-lived workspaces.
- Archive branches (`python-legacy`, `cpp-rewrite`) and tags (`python-last`, `main-v0.1.0`, `cpp-rewrite-v0.1.0`) are excluded from cleanup.
- To audit stale branches locally: `.github/scripts/audit-branches.sh` (uses `git cherry` patch equivalence, not commit ancestry — catches squash-merged branches)

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

OSD resizing is handled manually inside `OSDWindow` by detecting mouse drags on the visible edges/corners. Do not replace it with compositor/window-decoration resize APIs: native Wayland layer-shell has no normal decorated resize path, and unsupported Wayland sessions intentionally use the XWayland fallback.

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

Mouse resizing of the OSD is custom too: it updates the fixed widget size proportionally, persists `osd_scale` (and adjusted `screen`/`x`/`y` for left/top drags) on mouse release, and restarts the hide timer. Keep resize hit-testing separate from the progress-bar seek path so center clicks on the progress bar are not swallowed as resize gestures.

## D-Bus / MPRIS

The app registers two D-Bus services on the session bus:

| Service | Object path | Interface |
|---|---|---|
| `org.keyboardvolumeapp` | `/org/keyboardvolumeapp` | `org.keyboardvolumeapp.VolumeControl` |
| `org.mpris.MediaPlayer2.keyboardvolumeapp` *(opt-in, default OFF)* | `/org/mpris/MediaPlayer2` | `org.mpris.MediaPlayer2` + `.Player` |

- **`DbusInterface`** — `QObject` with `Q_CLASSINFO`, registered directly. Caches volume/mute/active-app/apps from `VolumeController`/`TrayApp` signals. D-Bus setters delegate to `VolumeController` async. Holds an optional `MprisClient*` (set via `setMprisClient()` from `App::initDbus()`); `Media{PlayPause,Next,Previous,Stop}` methods relay to `MprisClient` via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` because D-Bus dispatch happens on a worker thread while libdbus state must only be touched from the main thread. When `m_mpris` is null, the methods are silent no-ops (graceful degradation).
- **MPRIS** — separate `QObject` endpoint with `MprisRootAdaptor` and `MprisPlayerAdaptor` (`QDBusAbstractAdaptor` subclasses). **Must include `ExportAdaptors` flag** when registering — Qt6 auto-detects adaptor children.
- **MPRIS registration is conditional** on `OsdConfig::exposeMpris` (default `false`). The adaptors and endpoint `QObject` are always created in `initDbus()`, but the D-Bus service is only registered when the option is enabled. `App::registerMprisEndpoint()` / `App::unregisterMprisEndpoint()` handle the toggle at runtime; `m_mprisRegistered` tracks state. `App::onMprisExposureChanged()` is wired to `TrayApp::settingsChanged` so the change takes effect immediately without a restart.
- **`QDBusConnection::sessionBus()` returns by value** in Qt6 (not `&`). Write `auto bus = QDBusConnection::sessionBus();`, not `auto &bus`.
- `cleanup()` calls `unregisterMprisEndpoint()` (no-op if not registered); `org.keyboardvolumeapp` is always unregistered unconditionally.
- `Qt6::DBus` is a separate CMake component — requires `find_package(Qt6 REQUIRED COMPONENTS ... DBus)`.
- The MPRIS `Volume` property maps to `DbusInterface::volume()`. `PlaybackStatus` is always `"Stopped"`. Play/Pause/Next/Previous are no-ops. `Quit` → `qApp->quit()`.

For script-friendly `dbus-send` recipes (debugging without `kv-ctl` or `qdbus`), see **D-Bus / MPRIS → `dbus-send` recipes** in `ARCHITECTURE.md`. End-user `qdbus` examples are in `README.md`.

## Icon / QRC

The tray icon is embedded as a Qt resource: `cpp/resources.qrc` maps `../resources/icon.png` to `:/icon.png`. `CMAKE_AUTORCC` is ON so the `.qrc` only needs to be listed in `SOURCES`. Do not add separate `POST_BUILD` copy commands — the icon is already in the binary.

## Tests

Test inventory, `ctest` invocation, and clang-tidy local recipes live in **Tests** in `ARCHITECTURE.md`. Sharp edges only:

- All C++ changes in `cpp/src` and `cpp/tests` must be `clang-format` compliant. Before opening a PR that touches those paths, run `clang-format --dry-run --Werror` on the changed files; if it fails, run `clang-format -i` only on the changed files rather than reformatting the whole repo.
- CI is path-filtered: docs-only changes do not run CI; changes under `cpp/`, `pkg/`, `deploy/`, `resources/`, CMake files, or `.github/workflows/ci.yml` do.
- `clang-tidy` runs in CI as a warning-only job (`continue-on-error: true`) — it does **not** block merge.
- `Claude Code Review` is currently disabled via `if: false` in `.github/workflows/claude-code-review.yml`.
