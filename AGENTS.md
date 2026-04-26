# AGENTS.md — keyboard-volume-app

Comprehensive project docs are in `CLAUDE.md`. This file covers only the sharp edges an agent would otherwise guess wrong.

## Build & Run

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build -j$(nproc)
cpp/build/keyboard-volume-app
```

User must be in the `input` group for evdev access (`sudo usermod -aG input $USER`, re-login).

## Environment — Wayland

The app reads `WAYLAND_DISPLAY` / `XDG_SESSION_TYPE` at startup. If a Wayland session is detected and `QT_QPA_PLATFORM` is unset, it forces `xcb` (XWayland). Without this, `QWidget::move()` is ignored by the compositor and the OSD lands at (0,0). Do not remove this logic from `main.cpp`.

## Key codes are evdev, not Qt

Config `hotkeys` and all internal key handling use **Linux evdev key codes** (e.g. `KEY_VOLUMEUP` = 115). Conversion from X11 keycodes: `evdev = X11_keycode − 8`. The `KeyCaptureDialog` in `settingsdialog.cpp` uses two capture paths in parallel: evdev thread for media keys, `QKeyEvent::nativeScanCode()` for regular keys.

## Threading — critical rules

- **`InputHandler` IS a `QThread`** — direct subclass, runs `select()` in `run()`.
- **`PaWorker` uses `moveToThread()`** — not a subclass. All PulseAudio calls go through `QMetaObject::invokeMethod` targeting the PaWorker thread. **Never call libpulse from the main thread** — it will block the Qt event loop and freeze the tray menu.

## VolumeController fallback strategy

4 levels, in hot-path order:
1. Active sink input (libpulse, ~0.5ms)
2. Stream restore DB (libpulse)
3. PipeWire node via `pw-dump` + `pw-cli` subprocess (~30ms) — slow, **never in the hot keypress path**
4. Pending watcher — queues volume for when app reconnects

`pw-dump` is only invoked for idle-app listing and PW-node fallback. The fast path touches only libpulse.

## OSD painting quirk

Qt skips stylesheet background painting for translucent top-level windows (`WA_TranslucentBackground`). The OSD background is drawn manually in `OSDWindow::paintEvent()` via `QPainter::drawRoundedRect()`. Do not try to set OSD background via stylesheet — it won't render.

After `show()`, position is also set via `QWindow::setPosition()` on `windowHandle()` for XWayland compatibility.

## Config

- Reads/writes `$XDG_CONFIG_HOME/keyboard-volume-app/config.json` (uses `QStandardPaths::ConfigLocation`, not hardcoded `~/.config`).
- Deep-merges existing config with built-in defaults so new keys are always present.
- **Every setter calls `save()` immediately` — no explicit save step, no Config::save() to call manually.
- Hotkey values are evdev codes (see above).
- `isFirstRun()` returns `true` when the config file did not exist at load time. Used by `main()` to show the first-run wizard.

## First-run wizard

`cpp/src/firstrunwizard.h/cpp` — `QWizard` with 2 pages. Shown before `App::init()` when `Config::isFirstRun()` is true.
- **WelcomePage** — language picker (saves to config on Next)
- **DevicePage** — evdev device list (reuses scan logic from `deviceselector.cpp`)
- If wizard is cancelled, the app exits immediately (`return 0` in `main()`)

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

## No tests, no lint, no CI

There is no test framework, no linter config, and no CI workflow yet. Build verification is the only gate. The `ROADMAP.md` lists planned improvements.
