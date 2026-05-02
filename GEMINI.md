# GEMINI.md — keyboard-volume-app

This is the concise working guide for Gemini / Antigravity agents in this repository. It summarizes the rules that matter during implementation; the full project reference lives in `CLAUDE.md`, and sharp agent-specific constraints live in `AGENTS.md`.

## Project Overview

- **What:** Linux desktop utility that intercepts keyboard volume/mute keys through evdev and applies them to one selected audio application instead of the system master volume.
- **Stack:** C++20, Qt6 Widgets, Qt6 DBus, CMake 3.20+.
- **Audio:** PulseAudio-compatible IPC via libpulse, with PipeWire support through `pipewire-pulse` plus `pw-dump` / `pw-cli` subprocess fallback.
- **Input:** libevdev + libuinput.
- **Platform:** Linux only. KDE Plasma is the primary target. On Wayland sessions, the app forces `QT_QPA_PLATFORM=xcb` when that variable is unset, so the OSD can be positioned through XWayland.

## Non-Negotiable Rules

- **Do not commit directly to `main`** unless the user explicitly asks. Create a branch from the latest `origin/main`, use `feature/`, `fix/`, `refactor/`, `docs/`, or `chore/`, push it, and open a PR to `main`.
- Never force-push to `main`, delete branches, or rewrite published history without explicit consent.
- Before opening a PR, run the relevant build/tests. For evdev, libpulse, D-Bus, MPRIS, threading, CMake, or config migration changes, include a short risk/rollback note in the PR description.
- Config hotkeys and internal key handling use **Linux evdev key codes**, not Qt key codes. X11 native scan code conversion is `evdev = X11_keycode - 8`.
- `InputHandler` is a direct `QThread` subclass. It runs `epoll()` in `run()` with a 50ms timeout and handles both key press (`ev.value == 1`) and repeat (`ev.value == 2`) with 100ms debounce per key code.
- `PaWorker` uses `moveToThread()`. All libpulse calls must go through `QMetaObject::invokeMethod` targeting the PaWorker thread. Never call libpulse from the main thread.
- `pw-dump` and `pw-cli` are slow fallback tools. Do not put them in the hot keypress path or block the Qt event loop with them.
- Use `std::atomic<bool>` for thread-shared flags such as `m_running` and `m_stopping`; do not use `volatile bool`.
- `EvdevDevice` is the RAII, move-only wrapper for fd, `libevdev*`, grab/ungrab, and `libevdev_uinput*`. Reuse it instead of duplicating evdev cleanup logic.
- Keep the Wayland/XWayland startup logic in `main.cpp`: if Wayland is detected and `QT_QPA_PLATFORM` is unset, force `xcb`.
- The OSD is a translucent top-level window. Draw its background manually in `OSDWindow::paintEvent()` with `QPainter::drawRoundedRect()`; stylesheet backgrounds will not render there.
- After showing the OSD, also set position through `QWindow::setPosition()` on `windowHandle()` for XWayland compatibility.
- In Qt6, `QDBusConnection::sessionBus()` returns by value. Use `auto bus = QDBusConnection::sessionBus();`, not `auto &bus = ...`.
- MPRIS adaptors require `QDBusConnection::ExportAdaptors` when registering the object.
- The tray icon is embedded through `cpp/resources.qrc` as `:/icon.png`. Do not add post-build copy steps for the icon.

## Architecture Map

- `cpp/src/main.cpp`: entry point and `App` coordinator. Uses two-phase init: constructor creates only `Config`, then `init()` creates UI, input, audio, and D-Bus/MPRIS integration after the optional first-run wizard.
- `cpp/src/config.h/cpp`: thread-safe JSON config via `QStandardPaths::ConfigLocation`. Loads with deep-merge defaults. Setters save automatically.
- `cpp/src/i18n.h/cpp`: PL/EN translation tables; `tr(key)` lookup with English fallback.
- `cpp/src/inputhandler.h/cpp`: evdev hotkey capture, device grabbing, uinput mirroring, and `KeyCaptureThread` for hotkey rebinding. Also exposes `getVolumeDevices()` for device enumeration.
- `cpp/src/volumecontroller.h/cpp`: async per-app volume/mute controller. Fast path is libpulse active sink input, then stream restore, then PipeWire node fallback, then pending state in `PaWorker`. Reconnects PA context with backoff after daemon/context loss.
- `cpp/src/pwutils.h/cpp`: shared PipeWire client listing via `pw-dump` subprocess (`listPipeWireClients()`). Exports `PipeWireClient` struct and filter constants (`SYSTEM_BINARIES`, `SKIP_APP_NAMES`). Used by `VolumeController`, `AppListWidget`, and `AppSelectorDialog`.
- `cpp/src/applistwidget.h/cpp`: reusable `QWidget` with a `QListWidget` + Refresh button. `populate(Config*)` lists PW clients, pre-selects current config choice. Shared between `AppPage` (wizard) and `AppSelectorDialog` (tray).
- `cpp/src/appselectordialog.h/cpp`: modal `QDialog` for changing the default audio app. Embeds an `AppListWidget`. Opened from tray via "Change default application..." action.
- `cpp/src/osdwindow.h/cpp`: frameless always-on-top OSD with custom translucent painting and explicit XWayland positioning.
- `cpp/src/trayapp.h/cpp`: tray icon and context menu. Radio-list for audio app selection, "Change default application..." (opens `AppSelectorDialog`), Refresh, Change device, Settings, Quit. Rebuilds never replace the configured selected app just because it temporarily disappeared from the refreshed list.
- `cpp/src/deviceselector.h/cpp`: dialog for picking an evdev input device with volume keys.
- `cpp/src/settingsdialog.h/cpp`: settings dialog for OSD position/colors/timeout, volume step, hotkey rebinding, and language.
- `cpp/src/firstrunwizard.h/cpp`: first-run `QWizard` with 3 pages — `WelcomePage` (language), `DevicePage` (evdev device), `AppPage` (default application via `AppListWidget`).
- `cpp/src/dbusinterface.h/cpp`: custom D-Bus interface `org.keyboardvolumeapp.VolumeControl`.
- `cpp/src/mprisinterface.h/cpp`: MPRIS endpoint `org.mpris.MediaPlayer2.keyboardvolumeapp`.
- `cpp/src/evdevdevice.h/cpp`: shared RAII wrapper for evdev/uinput resources.
- `cpp/src/screenutils.h`: header-only utility `centerDialogOnScreenAt(QWidget*, QPoint)` — centers a parentless dialog on the screen containing the given cursor position. Used before `exec()` in all 4 parentless dialogs (`SettingsDialog`, `AppSelectorDialog`, `DeviceSelectorDialog`, `FirstRunWizard`). No event filters, QTimer, or window-flag changes.

## Development Workflow

Build:

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build -j$(nproc)
```

Run:

```bash
cpp/build/keyboard-volume-app
```

The user must be in the `input` group for evdev access to `/dev/input/event*` devices.

Common edits:

- **Adding a translation key:** add the English string to `_en` in `cpp/src/i18n.cpp`, add the Polish string to `_pl`, then call `tr("your.key")` from UI code.
- **Adding a config field:** add the default to the merge logic in `Config::load()`, add a typed getter/setter pair in `Config`, and rely on the setter's automatic save.
- **Changing D-Bus behavior:** keep property reads served from cached main-thread state and delegate setters/methods asynchronously to `VolumeController` or config.

## Risk Areas

- **evdev/libuinput:** exclusive grabs must not swallow normal keyboard input. Non-hotkey events should still be mirrored through uinput.
- **Threading:** main-thread blocking freezes tray/UI/D-Bus dispatch. Audio operations belong on PaWorker; evdev polling belongs in `InputHandler`.
- **Volume fallback:** keep the hot path fast. `pw-dump` is only for idle-app listing and PipeWire node fallback.
- **PA reconnect/cleanup:** context loss is expected; keep reconnect/backoff and cleanup on the PA worker side, preserve pending volume/mute, and do not clear `selected_app` during transient app-list refreshes.
- **Wayland/OSD:** removing XWayland forcing or `QWindow::setPosition()` can make the OSD land at `(0,0)`.
- **Config migration:** preserve deep-merge behavior so old config files receive new defaults.
- **D-Bus/MPRIS:** unregister both objects and services during cleanup; keep `Qt6::DBus` in CMake when touching these modules.

## Testing

Unit tests are in `cpp/tests/` and run through CTest:

- `test_config` — config merge, load/save, thread-safety
- `test_i18n` — lookup and fallback
- `test_volumecontroller` — 5 smoke tests
- `test_inputhandler` — API and evdev device listing

Run:

```bash
cd cpp/build && ctest --output-on-failure
```

Documentation-only changes do not require a build. For code changes, build first and run the focused or full test set based on risk.

## References

- `AGENTS.md`: mandatory git workflow and high-risk implementation constraints.
- `CLAUDE.md`: full architecture, module behavior, config schema, and signal flow.
- `README.md`: user-facing project overview and basic usage.

Public bus names and object paths:

| Service | Object path | Interface |
|---|---|---|
| `org.keyboardvolumeapp` | `/org/keyboardvolumeapp` | `org.keyboardvolumeapp.VolumeControl` |
| `org.mpris.MediaPlayer2.keyboardvolumeapp` | `/org/mpris/MediaPlayer2` | `org.mpris.MediaPlayer2` + `.Player` |
