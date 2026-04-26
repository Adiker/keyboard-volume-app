# GEMINI.md — keyboard-volume-app

This file contains the core context, architectural constraints, and rules specifically curated for Gemini (Antigravity) when working on the `keyboard-volume-app` project. It synthesizes the most important guidelines from `AGENTS.md`, `CLAUDE.md`, and the current state of the codebase.

## 1. Project Overview
- **What:** A Linux desktop utility that intercepts volume/mute keys via evdev to control a single, user-selected audio application's volume (instead of the system master volume).
- **Stack:** C++20, Qt6 (Widgets, DBus), CMake 3.20+.
- **Backend:** libevdev, libuinput, PulseAudio (libpulse IPC) + PipeWire (`pw-dump`/`pw-cli`).
- **Platform:** Linux only (Targets KDE Plasma primarily; X11 & Wayland).

## 2. Critical Development Rules

### Build & Run
- **Build:** 
  ```bash
  cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release
  cmake --build cpp/build -j$(nproc)
  ```
- **Run:** `cpp/build/keyboard-volume-app`
- **Permissions:** You must be in the `input` group to access `/dev/input/event*` devices.

### Threading & Concurrency (CRITICAL)
- **`InputHandler`:** Extends `QThread`. Runs `epoll()` in `run()` (recently updated from `select()`).
- **`PaWorker`:** Uses `moveToThread()`. All PulseAudio and `pw-dump` operations MUST go through `QMetaObject::invokeMethod` targeting this thread. 
- **Rule:** NEVER call libpulse or PipeWire subprocesses from the main thread. It will block the Qt event loop and freeze the UI/tray.

### Wayland & OSD Positioning
- On Wayland, Qt cannot position pure Wayland windows via `move()`. 
- **Workaround:** The app reads `WAYLAND_DISPLAY` and `XDG_SESSION_TYPE`. If Wayland is detected and `QT_QPA_PLATFORM` is unset, it forces `xcb` (XWayland). Do NOT remove this logic from `main.cpp`.
- After calling `show()` on the OSD, its position is forcibly updated using `QWindow::setPosition()` on `windowHandle()`.
- **Styling Quirk:** The OSD uses `WA_TranslucentBackground`. Qt skips stylesheet background painting for translucent top-level windows. The background MUST be drawn manually in `OSDWindow::paintEvent()` via `QPainter::drawRoundedRect()`. Do not attempt to use CSS/stylesheets for the OSD background.

### Key Handling (evdev vs Qt)
- Internal key routing and config use **Linux evdev key codes** (e.g., `KEY_VOLUMEUP` = 115).
- Conversion from X11 keycodes to evdev: `evdev = X11_keycode - 8`.
- `KeyCaptureDialog` (settings) uses two parallel paths: a background `QThread` for media keys, and Qt's `QKeyEvent::nativeScanCode()` for regular keys.

### Volume Controller Fallback
Volume changes use a 4-level fallback (in hot-path order):
1. **Active sink input** (libpulse, ~0.5ms) — primary fast path.
2. **Stream restore DB** (libpulse).
3. **PipeWire node** (`pw-dump` + `pw-cli`, ~30ms) — slow, NEVER use in the main hotkey path.
4. **Pending watcher** (queues volume for when the app reconnects).

### Configuration (`Config`)
- Uses `QStandardPaths::ConfigLocation` (`~/.config/keyboard-volume-app/config.json`).
- Always deep-merges config read from disk with built-in defaults.
- **Auto-save:** Every setter method calls `save()` automatically. Do not call `save()` manually.
- `isFirstRun()` determines if the setup wizard (`FirstRunWizard`) should be shown before the main `App::init()`.

### D-Bus & MPRIS Integration
- **D-Bus Session Bus:** `QDBusConnection::sessionBus()` returns by value in Qt6 (use `auto bus = ...`, NOT `auto &bus = ...`).
- **MPRIS:** Uses `QDBusAbstractAdaptor`. You MUST use the `ExportAdaptors` flag when registering the object (`bus.registerObject("/...", obj, QDBusConnection::ExportAdaptors)`) so Qt6 exposes the adaptor children.
- Two services are registered: `org.keyboardvolumeapp` and `org.mpris.MediaPlayer2.keyboardvolumeapp`. Unregister both explicitly during cleanup.

### Resources
- The tray icon is embedded via `cpp/resources.qrc` (`:/icon.png`). There is no need to copy icon files post-build. `CMAKE_AUTORCC` is ON and handles it automatically.

## 3. Current State & Roadmap Context
- There are **no tests**, **no linting**, and **no CI** yet. Build verification is the primary gate.
- Input polling uses `epoll()` with a 50ms timeout to reduce latency without hogging CPU.
- **Future plans (from `ROADMAP.md`):** Packaging (PKGBUILD/CPack), Unit testing (GTest/Catch2), native libpipewire support (to replace `pw-dump` sub-processes), and supporting multiple applications via user profiles. Keep these in mind if architectural changes are requested.
