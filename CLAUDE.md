# Project Context

**keyboard-volume-app** is a Linux desktop utility that intercepts keyboard volume/mute keys at the evdev level and routes them to a single user-selected audio application, rather than the system master volume. It displays an OSD overlay (Qt6 frameless window) showing the app name, volume bar, and percentage.

**Stack:** C++20, Qt6 (Widgets, DBus)  
**Audio backend:** PipeWire / PulseAudio (via libpulse IPC + `pw-dump`/`pw-cli` subprocesses)  
**Input:** libevdev + libuinput  
**Build system:** CMake 3.20+  
**Platform:** Linux only (KDE Plasma primary target; XWayland required on Wayland sessions)

---

## Required Environment Variables

These are read at startup in `cpp/src/main.cpp` to decide whether to force XWayland:

- `WAYLAND_DISPLAY` — set by compositor when running under Wayland
- `XDG_SESSION_TYPE` — `"wayland"` or `"x11"`
- `QT_QPA_PLATFORM` — if unset and Wayland is detected, the app forces `xcb` (XWayland) so `QWidget::move()` works for OSD positioning

> On Wayland, Qt cannot position windows via `move()` — the compositor ignores it. The app auto-sets `QT_QPA_PLATFORM=xcb` unless the user explicitly overrides it.

---

## Project Structure

```
keyboard-volume-app/
├── cpp/
│   ├── CMakeLists.txt           # CMake build (Qt6, libevdev, libpulse, DBus)
│   ├── resources.qrc            # Qt resource file — embeds icon.png
│   └── src/
│       ├── main.cpp             # Entry point; App class wires all modules
│       ├── config.h/cpp         # JSON config r/w (~/.config/keyboard-volume-app/config.json)
│       ├── i18n.h/cpp           # PL/EN translations; tr() lookup function
│       ├── volumecontroller.h/cpp  # Per-app volume/mute via libpulse + pw-dump/pw-cli
│       ├── inputhandler.h/cpp   # evdev QThread — global key capture + uinput re-injection
│       ├── osdwindow.h/cpp      # Frameless always-on-top Qt6 OSD overlay
│       ├── trayapp.h/cpp        # System tray icon and context menu
│       ├── deviceselector.h/cpp # Dialog for picking an evdev input device
│       ├── settingsdialog.h/cpp # Settings dialog (OSD, volume step, hotkeys, colors)
│       ├── firstrunwizard.h/cpp # QWizard — first-run language + device setup
│       ├── dbusinterface.h/cpp  # Custom D-Bus interface (org.keyboardvolumeapp.VolumeControl)
│       ├── mprisinterface.h/cpp # MPRIS adaptors (org.mpris.MediaPlayer2 + Player)
│       └── audioapp.h           # AudioApp struct (display name, PA index, muted, volume)
└── resources/
    └── icon.png
```

---

## Module Reference

### `cpp/src/main.cpp` — `App`, `main()`

The root coordinator. `App` uses **two-phase initialization**:
- **Constructor** creates only `Config`
- **`init()`** creates all remaining components, connects signals, starts evdev, and registers D-Bus interfaces

On first run (`Config::isFirstRun()`), a `FirstRunWizard` is shown before `App::init()` to guide the user through language and device selection. If the wizard is cancelled the app exits immediately.

Signal wiring:
- `InputHandler` signals (`volume_up`, `volume_down`, `volume_mute`) → `changeVolume()` / `onMute()`
- `VolumeController` signal `volumeChanged(app, vol, muted)` → `OSDWindow::showVolume()`
- `VolumeController` signal `appsReady(list)` → `TrayApp::rebuildMenu()`
- `TrayApp` signals → device/settings changes, OSD preview
- `DbusInterface` sits alongside, caching volume/mute state and forwarding D-Bus calls to `VolumeController`/`TrayApp`/`Config`
- `App::cleanup()` stops evdev, closes PA, unregisters D-Bus objects and services

Build: `cmake -S cpp -B cpp/build && cmake --build cpp/build -j$(nproc)`  
Run: `cpp/build/keyboard-volume-app`

### `cpp/src/config.h/cpp` — `Config`

Reads/writes `$XDG_CONFIG_HOME/keyboard-volume-app/config.json` (via `QStandardPaths`). Uses deep-merge so new default keys are always present when loading old config files. All setters call `save()` immediately.

`isFirstRun()` returns `true` when the config file did not exist at load time — used by `main()` to decide whether to show the first-run wizard.

**Config schema:**
```json
{
  "input_device": "/dev/input/event3",
  "selected_app": "youtube-music",
  "language": "en",
  "osd": {
    "screen": 0, "x": 50, "y": 1150,
    "timeout_ms": 1200, "opacity": 85,
    "color_bg": "#1A1A1A", "color_text": "#FFFFFF", "color_bar": "#0078D7"
  },
  "volume_step": 5,
  "hotkeys": { "volume_up": 115, "volume_down": 114, "mute": 113 }
}
```

Hotkey values are Linux evdev key codes (`KEY_VOLUMEUP`=115, `KEY_VOLUMEDOWN`=114, `KEY_MUTE`=113).

### `cpp/src/volumecontroller.h/cpp` — `VolumeController`, `PaWorker`, `AudioApp`

All PulseAudio/PipeWire operations run on a dedicated `PaWorker` thread (moved via `moveToThread`). The public API is **async**: `changeVolume`/`toggleMute` post work via `QMetaObject::invokeMethod`; results come back as signals.

4-level fallback strategy (in hot-path order):

1. **Active sink input** (libpulse IPC, ~0.5ms) — fastest, for currently playing apps
2. **Stream restore DB** (libpulse) — persists across pause/resume
3. **PipeWire node** (`pw-dump` + `pw-cli`, ~30ms) — for paused apps with an idle node
4. **Pending watcher** — stores desired volume; applies when app reconnects

`listApps()` returns cached data immediately and posts a background refresh that emits `appsReady(list)`. The background watcher listens for new sink-input events and applies pending volumes.

### `cpp/src/inputhandler.h/cpp` — `InputHandler`, `KeyCaptureThread`

**`InputHandler`** (extends `QThread`): reads evdev events from the selected device and all other devices advertising the configured hotkey codes. All such devices are grabbed exclusively and mirrored via uinput so non-hotkey events pass through transparently. Hotkey events are swallowed (never re-injected). Uses 100ms debounce per key code.

**Device selection logic:**
- `findSiblingDevices(path)` — finds all nodes sharing the same `phys` prefix
- `findHotkeyDevices(path, codes)` — siblings + every other device exposing at least one hotkey code; all marked exclusive (grabbed + uinput mirror)

**`KeyCaptureThread`** (extends `QThread`): used in the settings dialog to capture a single keypress for hotkey rebinding. Grabs all candidate devices, emits `key_captured(code)` or `cancelled()`.

### `cpp/src/osdwindow.h/cpp` — `OSDWindow`

Frameless, always-on-top Qt widget (220×70 px). Uses `WA_TranslucentBackground` + custom `paintEvent` for rounded corners (background drawn manually via `QPainter::drawRoundedRect()` because Qt skips stylesheet background painting on translucent top-level windows).

`showVolume(app, volume, muted)` — main display call, starts the auto-hide timer.  
After `show()`, position is also set via `QWindow::setPosition()` for XWayland compatibility.

### `cpp/src/trayapp.h/cpp` — `TrayApp`

System tray icon with context menu. Rebuilds the app list when `VolumeController::appsReady` fires.
The tray icon is loaded from Qt resources (`:/icon.png` via `resources.qrc`) — no external icon file required at runtime.

### `cpp/src/deviceselector.h/cpp` — `DeviceSelectorDialog`

Filters `/dev/input/event*` to show only devices exposing `KEY_VOLUMEUP`/`KEY_VOLUMEDOWN`.
`firstRun=true` shows a different window title ("first launch" variant).

### `cpp/src/firstrunwizard.h/cpp` — `FirstRunWizard`, `WelcomePage`, `DevicePage`

`QWizard`-based first-run dialog shown when `Config::isFirstRun()` returns `true`.
- **WelcomePage** — welcome text + language selection (`QComboBox` with EN/PL)
- **DevicePage** — reuses evdev scan logic (same as `DeviceSelectorDialog`) to list compatible devices
- On accept: saves language and device to `Config`; on reject: app exits.

### `cpp/src/dbusinterface.h/cpp` — `DbusInterface`

Exposes `org.keyboardvolumeapp.VolumeControl` on the D-Bus session bus (bus name `org.keyboardvolumeapp`, object path `/org/keyboardvolumeapp`).

Caches volume/mute/app state by listening to `VolumeController::volumeChanged`, `VolumeController::appsReady`, and `TrayApp::appChanged`. D-Bus properties (`Volume`, `Muted`, `ActiveApp`, `Apps`, `VolumeStep`) are served from the cache; setters delegate to `VolumeController`/`Config` async.

D-Bus methods: `VolumeUp()`, `VolumeDown()`, `ToggleMute()`, `RefreshApps()`.

### `cpp/src/mprisinterface.h/cpp` — `MprisRootAdaptor`, `MprisPlayerAdaptor`

`QDBusAbstractAdaptor` subclasses providing MPRIS v2 compliance (bus name `org.mpris.MediaPlayer2.keyboardvolumeapp`, object path `/org/mpris/MediaPlayer2`).

- **`MprisRootAdaptor`** — `org.mpris.MediaPlayer2`: `Identity`="Keyboard Volume App", `CanQuit`=true, `Quit` → `qApp->quit()`.
- **`MprisPlayerAdaptor`** — `org.mpris.MediaPlayer2.Player`: `Volume` (R/W, delegates to `DbusInterface`), `PlaybackStatus`="Stopped", `CanControl`=true, `Metadata` with `xesam:title`=active app name. Play/Pause/Next/Previous are no-ops.

Enables integration with KDE Connect, Plasma widgets, and other MPRIS-aware tools.

Settings dialog with live OSD position and color preview. `HotkeyCapture` stops `InputHandler` during capture (releases the grabbed device) and restarts it after.

**`KeyCaptureDialog`** has two parallel capture paths:
- evdev thread (`KeyCaptureThread`) — catches media/Consumer-Control keys
- Qt `keyPressEvent` + `nativeScanCode()` — catches regular keyboard keys (evdev code = X11 keycode − 8)

### `cpp/src/i18n.h/cpp`

Static string tables for `en` and `pl`. `tr(key)` falls back to English if a key is missing in the current language.

---

## Signal Flow

```
Keyboard keypress (evdev)
    └─► InputHandler::run() [QThread]
            ├─ hotkey → emit volume_up/volume_down/volume_mute
            └─ other keys → UInput re-injection

volume_up/volume_down
    └─► App::changeVolume(direction)
            └─► VolumeController::changeVolume(app, delta) [async → PaWorker]
                    └─► emit volumeChanged(app, newVol, muted)
                            ├─► OSDWindow::showVolume(app, vol, muted)
                            └─► DbusInterface (cache update → D-Bus signals)

volume_mute
    └─► App::onMute()
            └─► VolumeController::toggleMute(app) [async → PaWorker]
                    └─► emit volumeChanged(app, vol, muted=true/false)

D-Bus external call (e.g. qdbus)
    └─► DbusInterface::VolumeUp/Down/ToggleMute
            └─► VolumeController::changeVolume/toggleMute [async → PaWorker]
                    └─► (same cache update + D-Bus property notification)

MPRIS external call
    └─► MprisPlayerAdaptor::setVolume() / MprisRootAdaptor::Quit()
            └─► DbusInterface / qApp->quit()
```

---

## Threading Model

| Thread | Type | Purpose |
|---|---|---|
| Main | Qt event loop | UI, signals, OSD updates, D-Bus dispatch |
| `InputHandler` | `QThread` | evdev polling with `epoll()` (50ms timeout) |
| `KeyCaptureThread` | `QThread` | One-shot key capture for hotkey rebinding |
| `PaWorker` | `QThread` (via `moveToThread`) | All PulseAudio/pw-dump operations |

D-Bus calls arrive on the main thread and are forwarded to `VolumeController` (which posts to `PaWorker`). `DbusInterface` property reads are served from main-thread caches — no blocking.

---

## Development Workflow

### Build

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build -j$(nproc)
```

### Run

```bash
cpp/build/keyboard-volume-app
```

User must be in the `input` group for evdev access:
```bash
sudo usermod -aG input $USER
# log out and back in
```

### Adding a Translation Key

1. Add the key + English string to the `_en` map in `cpp/src/i18n.cpp`
2. Add the Polish translation to the `_pl` map
3. Use `tr("your.key")` in the UI code

### Adding a Config Field

1. Add the field with its default to the merge logic in `Config::load()` in `cpp/src/config.cpp`
2. Add a typed getter/setter pair in `cpp/src/config.h`; setter calls `save()`

### OSD Styling

OSD background is not set via stylesheet (Qt skips it for translucent top-level windows). Background is drawn in `OSDWindow::paintEvent()` using `QPainter::drawRoundedRect()`.

---

## Key Conventions

- **No global master volume changes** — all volume operations target a specific app's sink input
- **Evdev key codes** in config/hotkeys, not Qt key codes. Conversion: evdev = X11 keycode − 8
- **Config saves on every setter** — no explicit "save all" step needed
- **All PA operations on PaWorker thread** — never block the Qt event loop with libpulse or pw-dump calls
- **All hotkey devices grabbed exclusively** — siblings of the primary device AND every other device advertising the hotkey codes; non-hotkey events re-injected via uinput so typing is unaffected
- **`pw-dump` is slow** (~30ms) — only called for idle-app lookup and PW-node fallback; never in the main hotkey path
- **Wayland position workaround** — after every `widget.show()` that positions the OSD, also call `QWindow::setPosition()` on `windowHandle()`
- **Icon embedded as Qt resource** — loaded via `:/icon.png` from `resources.qrc`; no external file needed at runtime
- **Two-phase App init** — constructor creates only `Config`; `init()` creates the rest after optional first-run wizard
- **`QDBusConnection::sessionBus()` returns by value** in Qt6 (not by reference as in Qt5) — use `auto bus = ...` not `auto &bus`
- **`ExportAdaptors` flag required** when registering objects that have `QDBusAbstractAdaptor` children (like the MPRIS endpoint). Without it, adaptor interfaces are not exported
