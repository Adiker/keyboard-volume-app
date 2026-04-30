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
│       ├── config.h/cpp         # JSON config r/w (XDG config dir via QStandardPaths)
│       ├── i18n.h/cpp           # PL/EN translations; tr() lookup function
│       ├── volumecontroller.h/cpp  # Per-app volume/mute via libpulse + pw-dump/pw-cli
│       ├── pwutils.h/cpp           # Shared PipeWire client listing (pw-dump subprocess)
│       ├── applistwidget.h/cpp     # Reusable PW app list widget + Refresh button
│       ├── appselectordialog.h/cpp # QDialog for changing the default audio app from tray
│       ├── inputhandler.h/cpp   # evdev QThread — global key capture + uinput re-injection
│       ├── osdwindow.h/cpp      # Frameless always-on-top Qt6 OSD overlay
│       ├── trayapp.h/cpp        # System tray icon and context menu
│       ├── deviceselector.h/cpp # Dialog for picking an evdev input device
│       ├── settingsdialog.h/cpp # Settings dialog (OSD, volume step, hotkeys, colors)
│       ├── firstrunwizard.h/cpp # QWizard — first-run language + device + app setup
│       ├── dbusinterface.h/cpp  # Custom D-Bus interface (org.keyboardvolumeapp.VolumeControl)
│       ├── mprisinterface.h/cpp # MPRIS adaptors (org.mpris.MediaPlayer2 + Player)
│       ├── evdevdevice.h/cpp    # RAII evdev device wrapper (fd, libevdev*, uinput)
│       ├── screenutils.h        # Header-only: centerDialogOnScreenAt() for multi-monitor XWayland
│       └── audioapp.h           # AudioApp struct (display name, PA index, muted, volume)
├── pkg/
│   └── arch/
│       └── PKGBUILD             # Arch Linux package (keyboard-volume-app-git)
├── resources/
│   ├── icon.png
│   └── keyboard-volume-app.desktop  # Desktop entry (distribution copy, no hardcoded paths)
└── LICENSE                      # GPL-2.0-or-later
```

---

## Module Reference

### `cpp/src/main.cpp` — `App`, `main()`

The root coordinator. `App` uses **two-phase initialization**:
- **Constructor** creates only `Config`
- **`init()`** creates all remaining components, connects signals, starts evdev, and registers D-Bus interfaces

On first run (`Config::isFirstRun()`), a `FirstRunWizard` is shown before `App::init()` to guide the user through language, input device, and default application selection. If the wizard is cancelled the app exits immediately.

**Singleton:** Only one instance allowed. On startup, the app checks if `org.keyboardvolumeapp` is already registered on the D-Bus session bus — if so, prints a warning and exits with code 1.

**CLI flags:** `--version` and `--help` via `QCommandLineParser`. `APP_VERSION` is injected from `CMakeLists.txt` at build time.

`Config` is held as `std::unique_ptr<Config>`; `m_osd` is explicitly `delete`d in `~App()`.

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

Thread-safe — uses `std::mutex` (`m_mutex`) guarding `m_data` and `m_firstRun`. All public methods lock.

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

### `cpp/src/pwutils.h/cpp` — PipeWire client listing utility

Shared utility for listing idle PipeWire audio clients via `pw-dump` subprocess. Used by both `VolumeController` (via `PaWorker`) and the first-run wizard (`AppPage`).

- **`PipeWireClient` struct** — `{ QString name; QString binary; }`
- **`SYSTEM_BINARIES` / `SKIP_APP_NAMES`** — `QSet<QString>` filter constants shared between `pwutils` and `VolumeController`
- **`listPipeWireClients()`** — spawns `pw-dump`, waits up to 3s for start + 3s for finish, checks exit code, parses JSON, filters system binaries, renames skipped app names to their binary, returns deduplicated list. `qWarning()` on every failure path. Returns empty list on any error.

### `cpp/src/applistwidget.h/cpp` — `AppListWidget`

Reusable `QWidget` containing a `QListWidget` + Refresh button for PipeWire audio client selection. Shared between `AppPage` (first-run wizard) and `AppSelectorDialog` (tray).

- **`populate(Config *)`** — calls `listPipeWireClients()`, adds "No default application" (enabled, first), adds "No audio applications found" (disabled) if empty, otherwise lists each client; pre-selects the current `Config::selectedApp()`.
- **`selectedAppName()`** — returns the `Qt::UserRole` data of the selected item.
- **`setSelectedApp(name)`** — selects the item with matching data.
- Uses `app_selector.*` translation keys (`app_selector.no_default`, `app_selector.no_apps`, `app_selector.refresh`).

### `cpp/src/appselectordialog.h/cpp` — `AppSelectorDialog`

Modal `QDialog` for changing the default audio application. Opened from the tray menu via "Change default application..." action.

- Embeds an `AppListWidget`, subtitle label, OK/Cancel `QDialogButtonBox`.
- On accept: reads `selectedAppName()` from the widget, saves via `Config::setSelectedApp()`, accepts.
- Window title: `app_selector.title`, subtitle: `app_selector.subtitle`.

### `cpp/src/screenutils.h` — `centerDialogOnScreenAt()`

Header-only utility that centers a dialog on the screen containing the given global position. Used to fix dialog placement on XWayland multi-monitor setups where Qt defaults to the primary screen, which is often wrong.

```cpp
inline void centerDialogOnScreenAt(QWidget *window, const QPoint &globalPos)
```

- `QApplication::screenAt(globalPos)` → fallback to `primaryScreen()`.
- `ensurePolished()` + `adjustSize()` to compute layout before measuring.
- Final size = `sizeHint` expanded to `minimumSizeHint` and `minimumSize`; `resize()` if needed.
- Position centered inside `screen->availableGeometry()`, clamped to screen bounds.
- `window->move(x, y)` — no event filters, QTimer, or window-flag manipulation.

**Call sites:** All 4 parentless dialogs capture `QCursor::pos()` before construction and call `centerDialogOnScreenAt()` before `exec()`:
- `SettingsDialog` and `AppSelectorDialog` in `cpp/src/trayapp.cpp`
- `DeviceSelectorDialog` and `FirstRunWizard` in `cpp/src/main.cpp`

### `cpp/src/volumecontroller.h/cpp` — `VolumeController`, `PaWorker`, `AudioApp`

All PulseAudio/PipeWire operations run on a dedicated `PaWorker` thread (moved via `moveToThread`). The public API is **async**: `changeVolume`/`toggleMute` post work via `QMetaObject::invokeMethod`; results come back as signals.

4-level fallback strategy (in hot-path order):

1. **Active sink input** (libpulse IPC, ~0.5ms) — fastest, for currently playing apps
2. **Stream restore DB** (libpulse) — persists across pause/resume
3. **PipeWire node** (`pw-dump` + `pw-cli`, ~30ms) — for paused apps with an idle node
4. **Pending watcher** — stores desired volume; applies when app reconnects

`listApps()` returns cached data immediately and posts a background refresh that emits `appsReady(list)`. The background watcher listens for new sink-input events and applies pending volumes.

`PaWatcherThread` emits both `sinkInputAppeared` (PA NEW event) and `sinkInputRemoved` (PA REMOVE event). `PaWorker` connects both to a 500ms debounce `QTimer` (`m_refreshTimer`) that calls `doListApps(true)` — so the tray menu rebuilds automatically whenever an audio app starts or stops, without any manual Refresh click. The existing `appsReady → TrayApp::rebuildMenu` connection handles the UI update. `doApplyPending` (100ms one-shot) always fires before the debounce timer, so pending volumes are applied before the refreshed list is emitted.

App/binary filter constants (`SYSTEM_BINARIES`, `SKIP_APP_NAMES`) live in `pwutils.h` and are shared with `AppPage`.

### `cpp/src/inputhandler.h/cpp` — `InputHandler`, `KeyCaptureThread`

**`InputHandler`** (extends `QThread`): reads evdev events from the selected device and all other devices advertising the configured hotkey codes. All such devices are grabbed exclusively and mirrored via uinput so non-hotkey events pass through transparently. Hotkey events are swallowed (never re-injected). Uses 100ms debounce per key code.

Key repeat events (`ev.value == 2`) are handled alongside regular press events (`ev.value == 1`).

`std::atomic<bool>` used for all thread-shared flags (`m_running`) — never `volatile bool`.

**`EvdevDevice`** (RAII, move-only) in `evdevdevice.h/cpp` — manages fd, `libevdev*`, grab/ungrab, and `libevdev_uinput*` with automatic cleanup in destructor. Used by `InputHandler`, `DeviceSelectorDialog`, and `FirstRunWizard`. `getVolumeDevices()` also lives in `inputhandler.h/cpp`.

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

Menu actions: current app list (checkable radio items), Refresh, **Change default application...** (opens `AppSelectorDialog`), Change input device..., Settings..., Quit.

### `cpp/src/deviceselector.h/cpp` — `DeviceSelectorDialog`

Filters `/dev/input/event*` to show only devices exposing `KEY_VOLUMEUP`/`KEY_VOLUMEDOWN`.
`firstRun=true` shows a different window title ("first launch" variant).

### `cpp/src/firstrunwizard.h/cpp` — `FirstRunWizard`, `WelcomePage`, `DevicePage`, `AppPage`

`QWizard`-based first-run dialog shown when `Config::isFirstRun()` returns `true`.
- **WelcomePage** — welcome text + language selection (`QComboBox` with EN/PL)
- **DevicePage** — reuses evdev scan logic (same as `DeviceSelectorDialog`) to list compatible devices
- **AppPage** — embeds an `AppListWidget` (shared with `AppSelectorDialog`) to let the user pick the default audio application at first launch. Includes a "No default application" option and a Refresh button in case the target app wasn't running
- On accept: saves language, device, and selected app to `Config`; on reject: app exits.

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

### `pkg/arch/PKGBUILD` — Arch Linux package

`keyboard-volume-app-git` package for Arch Linux / AUR. Builds from the `main` branch via `git clone`.

- `pkgver()` uses `git describe --tags --long` to generate a version like `r0.1.0.24.gc2cd813`
- `depends`: `qt6-base libevdev libpulse pipewire`
- `makedepends`: `cmake gcc pkg-config git`
- CMake Release build with `BUILD_TESTING=OFF` and `DESTDIR` install
- Installs: binary → `/usr/bin/`, `.desktop` → `/usr/share/applications/`, icon → `/usr/share/pixmaps/` and `/usr/share/keyboard-volume-app/`

To build locally: `cd pkg/arch && makepkg -f --skipchecksums`  
To validate: `namcap PKGBUILD`  
Before AUR submission: `makepkg --printsrcinfo > .SRCINFO`

**Distribution files installed by CMake:**
- `resources/keyboard-volume-app.desktop` — clean `.desktop` without hardcoded paths; tracked in git (the root-level `keyboard-volume-app.desktop` with dev paths remains gitignored)
- `resources/icon.png` — installed to both `share/keyboard-volume-app/` (legacy) and `share/pixmaps/keyboard-volume-app.png` (DE icon lookup)

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

### Git Workflow

- **Never commit directly to `main`** unless explicitly asked.
- Always create a branch from the latest `origin/main`.
- Use branch prefixes: `feature/`, `fix/`, `refactor/`, `docs/`, `chore/`.
- Push the branch and open a PR to `main`.
- Never force-push to `main`.
- Never delete branches without explicit consent.
- Never rewrite published history without explicit consent.
- Before opening a PR, run relevant build/tests if the change warrants it.
- For risky areas (evdev, libpulse, D-Bus, MPRIS, threading, CMake, config migration), add a short risk/rollback note in the PR description.

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

## Branch Layout

| Branch | Purpose |
|---|---|
| `main` | Primary C++/Qt6 implementation |
| `python-legacy` | Archived Python/PyQt6 implementation |
| `python-last` | Tag pointing to last Python `main` commit |
| `cpp-rewrite` | Preserved migration branch, inactive for new changes |

---

## Tests

Unit tests are in `cpp/tests/`, integrated with CTest:
- `test_config` — 14 tests (merge, load/save, thread-safety)
- `test_i18n` — 8 tests (lookup, fallback)
- `test_volumecontroller` — 4 smoke tests
- `test_inputhandler` — 8 tests (API, evdev device listing)

Run: `cd cpp/build && ctest --output-on-failure`. No CI workflow yet.

---

## Key Conventions

- **No global master volume changes** — all volume operations target a specific app's sink input
- **Evdev key codes** in config/hotkeys, not Qt key codes. Conversion: evdev = X11 keycode − 8
- **Config saves on every setter** — no explicit "save all" step needed
- **All PA operations on PaWorker thread** — never block the Qt event loop with libpulse or pw-dump calls
- **All hotkey devices grabbed exclusively** — siblings of the primary device AND every other device advertising the hotkey codes; non-hotkey events re-injected via uinput so typing is unaffected
- **`pw-dump` is slow** (~30ms) — only called for idle-app lookup and PW-node fallback; never in the main hotkey path
- **Wayland position workaround** — after every `widget.show()` that positions the OSD, also call `QWindow::setPosition()` on `windowHandle()`
- **Dialog centering on multi-monitor** — use `centerDialogOnScreenAt(window, QCursor::pos())` from `screenutils.h` before `exec()` for parentless dialogs. Never use event filters, QTimer hacks, or `Qt::Dialog` flag changes for positioning.
- **Icon embedded as Qt resource** — loaded via `:/icon.png` from `resources.qrc`; no external file needed at runtime
- **Two-phase App init** — constructor creates only `Config`; `init()` creates the rest after optional first-run wizard
- **`QDBusConnection::sessionBus()` returns by value** in Qt6 (not by reference as in Qt5) — use `auto bus = ...` not `auto &bus`
- **`ExportAdaptors` flag required** when registering objects that have `QDBusAbstractAdaptor` children (like the MPRIS endpoint). Without it, adaptor interfaces are not exported
