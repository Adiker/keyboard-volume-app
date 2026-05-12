# Project Context

**keyboard-volume-app** is a Linux desktop utility that intercepts keyboard volume/mute keys at the evdev level and routes them to a single user-selected audio application, rather than the system master volume. It displays an OSD overlay (Qt6 frameless window) showing the app name, volume bar, and percentage.

**Stack:** C++20, Qt6 (Widgets, DBus)  
**Audio backend:** PipeWire / PulseAudio (via libpulse IPC + libpipewire)
**Input:** libevdev + libuinput  
**Build system:** CMake 3.20+  
**Platform:** Linux only (KDE Plasma primary target; native Wayland OSD on layer-shell compositors, XWayland fallback elsewhere)

---

## Required Environment Variables

These are read at startup in `cpp/src/main.cpp` to decide whether to use native Wayland OSD positioning or force XWayland:

- `WAYLAND_DISPLAY` — set by compositor when running under Wayland
- `XDG_SESSION_TYPE` — `"wayland"` or `"x11"`
- `QT_QPA_PLATFORM` — if unset and Wayland is detected without usable layer-shell support, the app forces `xcb` (XWayland) so `QWidget::move()` works for OSD positioning

> On native Wayland, Qt cannot position regular top-level windows via `move()` — the compositor ignores it. When built with `wayland-client` and `LayerShellQt >= 6.6`, the app probes `zwlr_layer_shell_v1` before `QApplication`; wlroots/KDE sessions use a per-window LayerShellQt OSD surface, while GNOME/unsupported compositors keep the `QT_QPA_PLATFORM=xcb` fallback unless the user explicitly overrides Qt's platform.

---

## Project Structure

```
keyboard-volume-app/
├── cpp/
│   ├── CMakeLists.txt           # CMake build (Qt6, libevdev, libpulse, libpipewire, DBus)
│   ├── resources.qrc            # Qt resource file — embeds icon.png
│   └── src/
│       ├── main.cpp             # Entry point; App class wires all modules
│       ├── config.h/cpp         # JSON config r/w (XDG config dir via QStandardPaths)
│       ├── i18n.h/cpp           # PL/EN translations; tr() lookup function
│       ├── volumecontroller.h/cpp  # Per-app volume/mute via libpulse + libpipewire
│       ├── pwutils.h/cpp           # Shared PipeWire helpers (libpipewire)
│       ├── applistwidget.h/cpp     # Reusable PW app list widget + Refresh button
│       ├── appselectordialog.h/cpp # QDialog for changing the default audio app from tray
│       ├── inputhandler.h/cpp   # evdev QThread — global key capture + uinput re-injection
│       ├── osdwindow.h/cpp      # Frameless always-on-top Qt6 OSD overlay
│       ├── trayapp.h/cpp        # System tray icon and context menu
│       ├── deviceselector.h/cpp # Dialog for picking an evdev input device
│       ├── settingsdialog.h/cpp # Settings dialog (OSD, volume step, profiles, colors)
│       ├── profileeditdialog.h/cpp # Sub-dialog for editing a single audio profile
│       ├── firstrunwizard.h/cpp # QWizard — first-run language + device + app setup
│       ├── dbusinterface.h/cpp  # Custom D-Bus interface (org.keyboardvolumeapp.VolumeControl)
│       ├── mprisinterface.h/cpp # MPRIS adaptors (org.mpris.MediaPlayer2 + Player)
│       ├── mprisclient.h/cpp    # MPRIS consumer for external player metadata/progress
│       ├── kvctl.cpp            # kv-ctl command-line D-Bus client
│       ├── kvctlcommand.h/cpp   # kv-ctl parser shared with tests
│       ├── evdevdevice.h/cpp    # RAII evdev device wrapper (fd, libevdev*, uinput)
│       ├── windowtracker.h/cpp  # XCB window-focus monitor for auto-profile switching
│       ├── screenutils.h        # Header-only: centerDialogOnScreenAt() for multi-monitor XWayland
│       └── audioapp.h           # AudioApp struct (display name, PA index, muted, volume)
├── pkg/
│   └── arch/
│       └── PKGBUILD             # Arch Linux package (keyboard-volume-app-git)
├── resources/
│   ├── icon.png
│   └── keyboard-volume-app.desktop  # Desktop entry (distribution copy, no hardcoded paths)
├── deploy/
│   └── keyboard-volume-app.service  # systemd user service unit
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

**CLI flags:** `keyboard-volume-app` supports `--version` and `--help` via `QCommandLineParser`. `APP_VERSION` is injected from `CMakeLists.txt` at build time.

**`kv-ctl`:** separate lightweight `QCoreApplication` binary linked only with `Qt6::Core` and `Qt6::DBus`. It does not start the tray app and does not shell out to `qdbus`; it sends synchronous calls to the running daemon on the session bus.

`Config` is held as `std::unique_ptr<Config>`; `m_osd` is explicitly `delete`d in `~App()`.

Signal wiring:
- `InputHandler` signals (`volume_up`, `volume_down`, `volume_mute`) → `changeVolume()` / `onMute()`
- `VolumeController` signal `volumeChanged(app, vol, muted)` → `OSDWindow::showVolume()`
- `VolumeController` signal `appsReady(list)` → `TrayApp::rebuildMenu()`
- `MprisClient` signals (`trackChanged`, `positionChanged`, `noPlayer`) → `OSDWindow` progress row
- `TrayApp` signals → device/settings changes, OSD preview
- `DbusInterface` sits alongside, caching volume/mute state and forwarding D-Bus calls to `VolumeController`/`TrayApp`/`Config`
- `App::cleanup()` stops evdev, closes PA, unregisters D-Bus objects and services

Build: `cmake -S cpp -B cpp/build && cmake --build cpp/build -j$(nproc)`  
Run: `cpp/build/keyboard-volume-app`

### `cpp/src/config.h/cpp` — `Config`

Reads/writes `$XDG_CONFIG_HOME/keyboard-volume-app/config.json` (via `QStandardPaths`). Saves are atomic via `QSaveFile` + `commit()`, so failed writes do not truncate the previous config. Uses deep-merge so new default keys are always present when loading old config files. All setters call `save()` immediately.

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
    "color_bg": "#1A1A1A", "color_text": "#FFFFFF", "color_bar": "#0078D7",
    "progress_enabled": false,
    "progress_interactive": true,
    "progress_poll_ms": 500,
    "progress_label_mode": "both",
    "tracked_players": ["spotify", "vlc", "strawberry", "harmonoid", "youtube"]
  },
  "volume_step": 5,
  "hotkeys": { "volume_up": 115, "volume_down": 114, "mute": 113 },
  "auto_profile_switch": false,
  "profiles": [
    { "id": "default", "name": "Default", "app": "youtube-music",
      "modifiers": [],
      "hotkeys": { "volume_up": 115, "volume_down": 114, "mute": 113 },
      "ducking": { "enabled": false, "volume": 25, "hotkey": 0 },
      "auto_switch": true },
    { "id": "firefox-ctrl", "name": "Firefox (Ctrl)", "app": "firefox",
      "modifiers": ["ctrl"],
      "hotkeys": { "volume_up": 115, "volume_down": 114, "mute": 113 },
      "ducking": { "enabled": true, "volume": 25, "hotkey": 88 },
      "auto_switch": true }
  ],
  "scenes": [
    { "id": "meeting", "name": "Meeting",
      "targets": [
        { "match": "Spotify", "volume": 10, "muted": false },
        { "match": "Discord", "volume": 80 },
        { "match": "Steam", "muted": true }
      ] }
  ]
}
```

Hotkey values are evdev bindings. Legacy integer values still mean `EV_KEY` Linux evdev key codes (`KEY_VOLUMEUP`=115, `KEY_VOLUMEDOWN`=114, `KEY_MUTE`=113). Scroll bindings use object form such as `{ "type": "rel", "code": 8, "direction": 1 }` for `EV_REL / REL_WHEEL`.

**OSD playback progress config.** `progress_enabled` is the master toggle. `progress_interactive` allows OSD seek controls when the active MPRIS player is seekable. `progress_poll_ms` is clamped to `200..2000` because many players do not emit position changes. `progress_label_mode` is one of `app`, `track`, or `both`. `tracked_players` is a priority list matched case-insensitively against MPRIS service names.

**Profiles** (canonical source of truth for hotkey → app mapping). Each entry:
- `struct Profile { QString id, name, app; HotkeyConfig hotkeys; QSet<Modifier> modifiers; DuckingConfig ducking; bool autoSwitch; }`
- `struct DuckingConfig { bool enabled; int volume; HotkeyBinding hotkey; }` — manual per-profile Focus Audio. `volume` is clamped to `0..100`; unassigned binding means no hotkey.
- `enum class Modifier { Ctrl, Shift }` — left/right collapsed to canonical (Ctrl/Shift only in v1; Alt/Meta out of scope)
- API: `profiles()`, `setProfiles(QList<Profile>)` (validates non-empty + uniqueifies ids with numeric suffix on collision), `defaultProfile()` (= `profiles().first()`), `setDefaultProfileApp(QString)`, `findProfileByApp(QString)` (case-insensitive contains match among auto_switch-enabled profiles)
- `autoSwitch` (default `true`) — whether this profile participates in auto-profile switching by focused window
- `autoProfileSwitch()` / `setAutoProfileSwitch(bool)` — global on/off (default `false`)
- `Modifier` ↔ string helpers: `modifierToString(Modifier)`, `modifierFromString(QString)`

**Audio scenes** (named mixer presets). Each scene:
- `struct AudioScene { QString id, name; QList<SceneTarget> targets; }`
- `struct SceneTarget { QString match; std::optional<int> volume; std::optional<bool> muted; }`
- `match` uses the same app/binary names accepted by `VolumeController`; `volume` is clamped to `0..100`; omitted `volume`/`muted` leaves that state unchanged.
- API: `scenes()`, `setScenes(QList<AudioScene>)` (uniqueifies ids and drops malformed targets).

**Migration & legacy mirror.** `Config::load()` synthesizes a single `default` profile from legacy `selected_app` + top-level `hotkeys` when `profiles` is missing/empty, then `saveUnlocked()` writes the new schema. Top-level `selected_app` and `hotkeys` are kept in sync as a deprecated mirror of `profiles[0]` (one release of compat) — `setSelectedApp()`/`setHotkeys()` mirror into the default profile and vice versa, so existing D-Bus/MPRIS clients keep working.

### `cpp/src/pwutils.h/cpp` — PipeWire utility

Shared utility for listing idle PipeWire audio clients and controlling PipeWire stream nodes via libpipewire. Used by both `VolumeController` (via `PaWorker`) and the first-run wizard (`AppPage`).

- **`PipeWireClient` struct** — `{ QString name; QString binary; QString id; }`
- **`PipeWireNode` struct** — `{ uint32_t id; double volume; bool muted; }`
- **`SYSTEM_BINARIES` / `SKIP_APP_NAMES`** — `QSet<QString>` filter constants shared between `pwutils` and `VolumeController`
- **`listPipeWireClients()`** — snapshots the PipeWire registry through libpipewire, filters system binaries, renames skipped app names to their binary, and returns a deduplicated list. Returns empty list on connection failure or timeout.
- **Display name vs control target** — `PipeWireClient::name` is the UI label, `PipeWireClient::binary` is the stable control target, and `PipeWireClient::id` is the PipeWire client global id when known. Some apps expose a friendly client (`harmonoid`) but play through a technical stream node (`mpv`). In that case the UI should show the client name and store/control the stream target.
- **Stream node ownership** — when a `PipeWire:Interface:Node` has `client.id`, map it back to the owning `PipeWire:Interface:Client` by object/global id. Do not infer ownership from `application.process.binary` alone; Harmonoid exposes a node with `application.name=mpv`, `application.process.binary=mpv`, `node.name=mpv`, `media.name=Harmonoid`, and `client.id` pointing to the `harmonoid` client.
- **Generic names** — `media.name=Playback` is not an app name. For browser wrappers (for example YouTube Music using Chromium), prefer the process binary (`youtube-music`) over generic `application.name=Chromium` / `media.name=Playback`.
- **`findPipeWireNodeForApp()` / `setPipeWireNodeVolume()` / `setPipeWireNodeMute()`** — bind PipeWire stream nodes and read/write `SPA_PARAM_Props` without spawning `pw-cli`.

### `cpp/src/applistwidget.h/cpp` — `AppListWidget`

Reusable `QWidget` containing a `QListWidget` + Refresh button for PipeWire audio client selection. Shared between `AppPage` (first-run wizard) and `AppSelectorDialog` (tray).

- **`populate(Config *)`** — calls `listPipeWireClients()`, adds "No default application" (enabled, first), adds "No audio applications found" (disabled) if empty, otherwise lists each client; pre-selects the current `Config::selectedApp()`.
- **`selectedAppName()`** — returns the `Qt::UserRole` data of the selected item. This is the control target (`PipeWireClient::binary`), not necessarily the visible label.
- **`setSelectedApp(name)`** — selects the item with matching data or visible text, case-insensitively, so older configs using display labels still preselect correctly.
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

All PulseAudio/PipeWire operations run on a dedicated `PaWorker` thread (moved via `moveToThread`). The public API is **async**: `changeVolume`/`toggleMute`/`toggleDucking` post work via `QMetaObject::invokeMethod`; results come back as signals.

`toggleDucking(keepApp, duckVolume)` is manual per-profile Focus Audio: first call snapshots all known apps except `keepApp` and lowers them to `duckVolume`; the next call restores the saved volumes. v1 changes only volume, not mute.

4-level fallback strategy (in hot-path order):

1. **Active sink input** (libpulse IPC, ~0.5ms) — fastest, for currently playing apps
2. **Stream restore DB** (libpulse) — persists across pause/resume
3. **PipeWire node** (libpipewire) — for paused apps with an idle node
4. **Pending watcher** — stores desired volume/mute in `PaWorker`; applies when app reconnects

`listApps()` returns cached data immediately and posts a background refresh that emits `appsReady(list)`. The background watcher listens for new sink-input events and applies pending volumes.

Active sink inputs are matched case-insensitively against `application.name`, `application.process.binary`, and `media.name`, because some pipewire-pulse streams expose the controllable target and the user-facing app name in different fields. `listApps()` normalizes active stream display labels through `listPipeWireClients()` using `client.id` where possible, so tray names match the profile picker while the internal target remains controllable (for example tray/profile display `harmonoid`, target `mpv`).

`PaWorker` detects `PA_CONTEXT_FAILED` / `PA_CONTEXT_TERMINATED`, tears down the stale libpulse context, and reconnects with exponential backoff (500ms up to 30s). PA hot paths first check `contextReady()`; if PA is down, they skip libpulse, try the PipeWire libpipewire fallback, and otherwise park pending volume/mute until the app reconnects.

`PaWatcherThread` emits both `sinkInputAppeared` (PA NEW event) and `sinkInputRemoved` (PA REMOVE event). It also rebuilds its PA subscription after context loss. `PaWorker` connects both sink-input signals to a 500ms debounce `QTimer` (`m_refreshTimer`) that calls `doListApps(true)` — so the tray menu rebuilds automatically whenever an audio app starts or stops, without any manual Refresh click. The existing `appsReady → TrayApp::rebuildMenu` connection handles the UI update. `doApplyPending` (100ms one-shot) always fires before the debounce timer, so pending volumes are applied before the refreshed list is emitted.

App/binary filter constants (`SYSTEM_BINARIES`, `SKIP_APP_NAMES`) live in `pwutils.h` and are shared with `AppPage`.

### `cpp/src/inputhandler.h/cpp` — `InputHandler`, `KeyCaptureThread`

**`InputHandler`** (extends `QThread`): reads evdev events from the selected device and all other devices advertising any configured hotkey binding (`EV_KEY` keys or scroll `EV_REL` codes). All such devices are grabbed exclusively and mirrored via uinput so non-hotkey events pass through transparently. Hotkey events that resolve to a profile are swallowed (never re-injected); hotkey events with no matching profile are forwarded so typing/scrolling isn't blocked.

API: `setProfiles(QList<Profile>)` / `currentProfiles()`. Signals carry the resolved `profileId` so `App` can route to the right audio app:
```cpp
void volume_up(const QString &profileId);
void volume_down(const QString &profileId);
void volume_mute(const QString &profileId);
void ducking_toggle(const QString &profileId);
```

**Modifier tracking.** `m_heldModifiers` holds raw evdev modifier codes (`KEY_LEFTCTRL`, `KEY_RIGHTCTRL`, `KEY_LEFTSHIFT`, `KEY_RIGHTSHIFT`) updated on press/release events from grabbed devices. Modifier press/release events are **forwarded to uinput** so the rest of the desktop sees them. Free helpers (testable, no Qt event loop required):
- `bool isTrackedModifierCode(int code)`
- `QSet<Modifier> normalizeHeldModifiers(const QSet<int> &raw)` — collapses L/R variants
- `QString resolveProfile(HotkeyBinding binding, const QSet<Modifier> &held, const QList<Profile> &profiles)` — picks the profile whose `modifiers` set is a subset of `held` and whose binding matches; tie-broken by **specificity** (most required modifiers wins), then first-in-list. Returns empty string when no profile matches. `int` overloads treat the value as an `EV_KEY` binding.
- `ProfileHotkeyMatch resolveProfileHotkey(...)` — returns both profile id and action (`VolumeUp`, `VolumeDown`, `Mute`, `DuckingToggle`) for runtime dispatch.

**Debounce** is keyed per `(HotkeyBinding, profileId)` so key and scroll bindings with the same numeric code don't block each other (100ms window).

**v1 limitations** (TODO comments in source):
- A modifier key on a separate keyboard with no hotkey bindings won't be observed (`findHotkeyDevices` only opens devices with at least one hotkey binding).
- `m_heldModifiers` can go stale on focus loss (rare with grabbed devices) — accepted in v1.

Key repeat events (`ev.value == 2`) are handled alongside regular press events (`ev.value == 1`).

`std::atomic<bool>` used for all thread-shared flags (`m_running`) — never `volatile bool`.

**`EvdevDevice`** (RAII, move-only) in `evdevdevice.h/cpp` — manages fd, `libevdev*`, grab/ungrab, and `libevdev_uinput*` with automatic cleanup in destructor. Used by `InputHandler`, `DeviceSelectorDialog`, and `FirstRunWizard`. `getVolumeDevices()` also lives in `inputhandler.h/cpp`.

**Device selection logic:**
- `findSiblingDevices(path)` — finds all nodes sharing the same `phys` prefix
- `findHotkeyDevices(path, bindings)` — siblings + every other device exposing at least one hotkey binding; all marked exclusive (grabbed + uinput mirror)

**`KeyCaptureThread`** (extends `QThread`): used in the settings dialog to capture a single keypress or wheel scroll for hotkey rebinding. Grabs all candidate devices, emits `hotkey_captured(binding)` or `cancelled()`.

### `cpp/src/osdwindow.h/cpp` — `OSDWindow`

Frameless, always-on-top Qt widget. Base size is 220×70 px for volume only; when `OsdConfig::progressEnabled` and an active MPRIS track are both present, it expands to 220×112 px with a progress row.

`showVolume(app, volume, muted)` — main display call, starts the auto-hide timer.  
After `show()`, position is also set via `QWindow::setPosition()` for XWayland compatibility.

Progress row API:
- `setProgressEnabled(bool)` caches the master toggle and hides the row when disabled.
- `setProgressVisible(bool)` shows or hides the row for player presence and refreshes labels immediately so `progressLabelMode=track` falls back to the app label when the row disappears; it is a no-op while disabled.
- `updateTrack(title, artist, lengthUs, canSeek)` refreshes label mode, seekability and duration. `lengthUs <= 0` is live-stream mode: disabled bar and `LIVE` label.
- `updatePosition(positionUs)` maps the position to the progress bar's 0-1000 range and updates the elapsed/total time label.
- `eventFilter()` handles click/drag seeking on the progress bar. It emits `seekStarted()`, repeated `seekRequested(positionUs)`, applies the release coordinate as the final seek target, and emits `seekFinished()` on release/cancel. Always clear `m_seeking` via `finishSeeking()` before returning through interaction guards, otherwise `MprisClient` polling can remain suspended.
- `progressLabelMode` controls whether the main label shows the app, track, or app plus track row.

### `cpp/src/mprisclient.h/cpp` — `MprisClient`

Consumes external MPRIS2 players from the session bus. It detects services named `org.mpris.MediaPlayer2.*`, fetches `org.mpris.MediaPlayer2.Player` properties asynchronously, and emits active-player, track, position, playback-status and no-player signals for OSD playback progress wiring.

Selection is deterministic: filter by `Config::osd().trackedPlayers`, sort by that priority list, then prefer the first Playing player over the first Paused player. Matching uses the full service suffix after `org.mpris.MediaPlayer2.`, so instance names like `vlc.7389` still match tracked player `vlc`. `reload()` re-reads tracked players and poll interval, re-evaluates the active player, and re-emits the current track so settings toggles can show an already-active player immediately.

`MprisClient` lives in the main Qt thread only. All D-Bus reads are async via QtDBus; do not call it from `PaWorker` or `InputHandler`. It may keep active-player and metadata state current while progress is disabled, but it must not poll `Position` unless `Config::osd().progressEnabled` is true.

`PlayerInfo` struct: `service` (D-Bus name), `displayName`, `status` (`Playing`/`Paused`/`Stopped`), `canSeek`, `lengthUs`, `trackId`, `title`, `artist`.

Signals:
- `activePlayerChanged(PlayerInfo)` — new active player selected or lost
- `trackChanged(title, artist, lengthUs, canSeek)` — track metadata updated
- `positionChanged(qint64 positionUs)` — position polled from active player (only when `progressEnabled`)
- `playbackStatusChanged(QString status)` — Playing/Paused/Stopped state changed
- `noPlayer()` — no tracked player available on the session bus

### `cpp/src/trayapp.h/cpp` — `TrayApp`

System tray icon with context menu. `currentApp()` returns `Config::defaultProfile().app` (the default profile's app, NOT the legacy `selectedApp()` directly). Selecting an app from the tray's radio list calls `Config::setDefaultProfileApp(name)`, which updates the default profile and mirrors to legacy `selected_app` for D-Bus/MPRIS continuity.

Rebuilds the app list when `VolumeController::appsReady` fires. If a transient refresh/reconnect list does not contain the configured app, `TrayApp` keeps the default profile's app unchanged instead of auto-selecting the first available app.
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

Caches volume/mute/app state by listening to `VolumeController::volumeChanged`, `VolumeController::appsReady`, and `TrayApp::appChanged`. D-Bus properties (`Volume`, `Muted`, `ActiveApp`, `Apps`, `VolumeStep`, `Profiles`, `Scenes`) are served from the cache; setters delegate to `VolumeController`/`Config` async.

D-Bus methods:
- `VolumeUp()`, `VolumeDown()`, `ToggleMute()`, `ToggleDucking()`, `RefreshApps()` — bare methods target the default profile / `m_activeApp`, kept for backwards compat and script-friendly default-profile control.
- `VolumeUpProfile(QString id)`, `VolumeDownProfile(QString id)`, `ToggleMuteProfile(QString id)`, `ToggleDuckingProfile(QString id)` — per-profile methods, route directly to the profile's app via `m_volumeCtrl`.
- `ApplyScene(QString id)` — applies a configured audio scene by posting absolute volume/mute operations to `VolumeController`; unknown scene ids are a no-op.
- `ShowVolume()` — queries the current volume of `m_activeApp` via `VolumeController::queryVolume()` and emits `volumeChanged` → OSD displays without any value change.
- `ShowVolumeProfile(QString id)` — same as `ShowVolume()` but for the app in the named profile.

`Profiles` property is `QVariantList` of `QVariantMap` entries — `{id, name, app, modifiers (QStringList "ctrl"/"shift"), hotkeys ({volume_up, volume_down, mute, show}), ducking ({enabled, volume, hotkey})}`. Hotkey values are legacy ints for `EV_KEY` bindings or maps for scroll bindings. `reloadProfiles()` rebuilds the cache from `Config` and emits `profilesChanged(QVariantList)` only when the value actually changed; wired from `TrayApp::settingsChanged` in `App`.
`Scenes` property is `QVariantList` of `QVariantMap` entries — `{id, name, targets ([{match, volume?, muted?}])}`.

### `cpp/src/kvctl.cpp`, `cpp/src/kvctlcommand.h/cpp` — `kv-ctl`

`kv-ctl` is a small CLI client for scripts and tiling WM keybinds. Commands:
- `kv-ctl up|down|mute [--profile id]` maps to bare D-Bus methods or `VolumeUpProfile` / `VolumeDownProfile` / `ToggleMuteProfile`.
- `kv-ctl duck [--profile id]` maps to bare `ToggleDucking` or `ToggleDuckingProfile(id)`; without `--profile`, the daemon resolves the current default profile server-side.
- `kv-ctl show [--profile id]` maps to `ShowVolume()` or `ShowVolumeProfile(id)`; displays the OSD with the current volume without changing it.
- `kv-ctl scene ID` maps to `ApplyScene(ID)`.
- `kv-ctl refresh` maps to `RefreshApps`.
- `kv-ctl get volume|muted|active-app|apps|step|profiles|scenes` reads D-Bus properties through `org.freedesktop.DBus.Properties.Get`.
- `kv-ctl set volume|muted|active-app|step VALUE` writes properties through `org.freedesktop.DBus.Properties.Set`; volume is given as `0..100` percent and mapped to `0.0..1.0`.

Exit codes: `0` success, `1` usage, `2` daemon unavailable, `3` D-Bus error, `4` invalid value. Parser logic lives in `kvctlcommand` so it can be unit-tested without a session bus.

### `cpp/src/mprisinterface.h/cpp` — `MprisRootAdaptor`, `MprisPlayerAdaptor`

`QDBusAbstractAdaptor` subclasses providing MPRIS v2 compliance (bus name `org.mpris.MediaPlayer2.keyboardvolumeapp`, object path `/org/mpris/MediaPlayer2`).

- **`MprisRootAdaptor`** — `org.mpris.MediaPlayer2`: `Identity`="Keyboard Volume App", `CanQuit`=true, `Quit` → `qApp->quit()`.
- **`MprisPlayerAdaptor`** — `org.mpris.MediaPlayer2.Player`: `Volume` (R/W, delegates to `DbusInterface`), `PlaybackStatus`="Stopped", `CanControl`=true, `Metadata` with `xesam:title`=active app name. Play/Pause/Next/Previous are no-ops.

Enables integration with KDE Connect, Plasma widgets, and other MPRIS-aware tools.

Settings dialog with live OSD position and color preview, plus a **Profiles** section: `QTableWidget` (`Name | App | Modifiers | VolUp | VolDown | Mute | Ducking`) with Add / Edit / Remove / Set as default buttons. The default profile is row 0 and rendered with a `(default)` suffix; Remove is disabled when only one profile remains. `saveAndAccept()` calls `Config::setProfiles(...)`; the existing `settingsChanged` signal triggers `App::onHotkeysMaybeChanged` which restarts `InputHandler` with the new profile set.

`HotkeyCapture` stops `InputHandler` during capture (releases the grabbed device) and restarts it after. Right-clicking a `HotkeyCapture` button opens a context menu with **Unassign** (`settings.hotkey.unassign`) that resets its binding to `HotkeyBinding{}` (unassigned).

**`KeyCaptureDialog`** has two parallel capture paths:
- evdev thread (`KeyCaptureThread`) — catches media/Consumer-Control keys
- Qt `keyPressEvent` + `nativeScanCode()` — catches regular keyboard keys (evdev code = X11 keycode − 8)

### `cpp/src/profileeditdialog.h/cpp` — `ProfileEditDialog`

Sub-dialog launched from the Settings → Profiles section to add or edit a single audio profile. Fields:
- `QLineEdit` — profile name
- `AppListWidget` — picker for the audio app (reused from tray/first-run wizard)
- 2× `QCheckBox` — Ctrl, Shift required modifiers
- 3× `HotkeyCapture` — VolUp / VolDown / Mute hotkey bindings (each is a `HotkeyBinding`, supports `EV_KEY` and `EV_REL`); right-click for **Unassign**
- Ducking controls — enable checkbox, other-apps volume slider/spinbox, and Focus Audio `HotkeyCapture` with the same right-click **Unassign** action

`result()` builds a `Profile` from the widgets and preserves the original `id` when editing (a fresh Add gets the empty id and `Config::setProfiles()` slugifies/uniqueifies it).

An `auto_switch` checkbox (per-profile) controls whether the profile participates in auto-switching.

### `cpp/src/windowtracker.h/cpp` — `WindowTracker`

Runs in a dedicated `QThread`, polling XCB every 500ms for the active window via `_NET_ACTIVE_WINDOW`. Reads `_NET_WM_PID` from the focused window, then resolves the PID to a binary name via `/proc/PID/comm`. Emits `focusedBinaryChanged(QString)` when the binary changes.

`App` receives the signal, matches the binary name against the PipeWire app cache (case-insensitive `contains` check against both `AudioApp::name` and `AudioApp::binary`), checks for an `auto_switch=true` profile whose `app` field matches, and overrides the volume target via `effectiveApp()`. When the focus moves to a window with no matching audio app, the fallback profile-resolved app is used instead.

Thread-safety: `WindowTracker::start()` sets `m_running = true` (atomic) before calling `QThread::start()`, avoiding a race where `stop()`'s `m_running = false` could be overwritten if `run()` hadn't yet entered its polling loop.

**Limitation:** XCB requires X11/XWayland. On pure Wayland without XWayland the tracker fails gracefully with an error log — the app continues to work normally, just without auto-switching.

### `cpp/src/i18n.h/cpp`

Static string tables for `en` and `pl`. `tr(key)` falls back to English if a key is missing in the current language.

---

## Signal Flow

```
Keyboard keypress (evdev)
    └─► InputHandler::run() [QThread]
            ├─ modifier key (Ctrl/Shift L/R) → update m_heldModifiers; forward to uinput
            ├─ hotkey → resolveProfileHotkey(binding, normalizeHeldModifiers(held), profiles)
            │           ├─ matched → emit volume_up/down/mute/ducking_toggle(profileId); swallow
            │           └─ no match → forward to uinput (don't block typing)
            └─ other keys → UInput re-injection

volume_up/volume_down(profileId)
    └─► App::changeVolume(profileId, direction)
            └─► effectiveApp(profileId) — return m_autoActiveApp when auto-switch active
            └─► (fallback) findProfile(profileId) → Profile.app
            └─► VolumeController::changeVolume(app, delta) [async → PaWorker]
                    └─► emit volumeChanged(app, newVol, muted)
                            ├─► OSDWindow::showVolume(app, vol, muted)
                            └─► DbusInterface (cache update → D-Bus signals)

volume_mute(profileId)
    └─► App::onMute(profileId)
            └─► findProfile(profileId) → Profile.app
            └─► VolumeController::toggleMute(app) [async → PaWorker]
                    └─► emit volumeChanged(app, vol, muted=true/false)

ducking_toggle(profileId)
    └─► App::onDuckingToggle(profileId)
            └─► findProfile(profileId) → Profile.app + DuckingConfig
            └─► VolumeController::toggleDucking(app, ducking.volume / 100.0)

D-Bus external call (e.g. qdbus or kv-ctl)
    └─► DbusInterface::VolumeUp/Down/ToggleMute
            └─► VolumeController::changeVolume/toggleMute [async → PaWorker]
                    └─► (same cache update + D-Bus property notification)

MPRIS external call
    └─► MprisPlayerAdaptor::setVolume() / MprisRootAdaptor::Quit()
            └─► DbusInterface / qApp->quit()

Window focus change (X11/XWayland)
    └─► WindowTracker::run() [QThread — 500ms poll]
            ├─ _NET_ACTIVE_WINDOW → window PID via _NET_WM_PID
            ├─ /proc/PID/comm → binary name
            └─► emit focusedBinaryChanged(binary)
                    └─► App::onFocusedBinaryChanged(binary)
                            ├─ matchBinaryToApp(binary) → PipeWire app name
                            ├─ Config::findProfileByApp(app) → auto_switch profile
                            └─► effectiveApp() overrides volume target until focus changes
```

---

## Threading Model

| Thread | Type | Purpose |
|---|---|---|---|
| Main | Qt event loop | UI, signals, OSD updates, D-Bus dispatch |
| `InputHandler` | `QThread` | evdev polling with `epoll()` (50ms timeout) |
| `KeyCaptureThread` | `QThread` | One-shot key capture for hotkey rebinding |
| `WindowTracker` | `QThread` | XCB active-window polling (500ms interval) for auto-profile switching |
| `PaWorker` | `QThread` (via `moveToThread`) | All PulseAudio/libpipewire operations |

D-Bus calls arrive on the main thread and are forwarded to `VolumeController` (which posts to `PaWorker`). `DbusInterface` property reads are served from main-thread caches — no blocking.

### `pkg/arch/PKGBUILD` — Arch Linux package

`keyboard-volume-app-git` package for Arch Linux / AUR. Builds from the `main` branch via `git clone`.

- `pkgver()` uses `git describe --tags --long` to generate a version like `r0.1.0.24.gc2cd813`
- `depends`: `qt6-base libevdev libpulse libpipewire pipewire layer-shell-qt`
- `makedepends`: `cmake gcc pkg-config git wayland layer-shell-qt`
- CMake Release build with `BUILD_TESTING=OFF` and `DESTDIR` install
- Installs: binary → `/usr/bin/`, `.desktop` → `/usr/share/applications/`, icon → `/usr/share/pixmaps/`, systemd user service → `/usr/lib/systemd/user/`

To build locally: `cd pkg/arch && makepkg -f --skipchecksums`  
To validate: `namcap PKGBUILD`  
Before AUR submission: `makepkg --printsrcinfo > .SRCINFO`

**Distribution files installed by CMake:**
- `resources/keyboard-volume-app.desktop` — clean `.desktop` without hardcoded paths; tracked in git (the root-level `keyboard-volume-app.desktop` with dev paths remains gitignored)
- `resources/icon.png` — installed to both `share/keyboard-volume-app/` (legacy) and `share/pixmaps/keyboard-volume-app.png` (DE icon lookup)
- `deploy/keyboard-volume-app.service` — systemd user service for non-KDE/WM autostart; installed system-wide to `/usr/lib/systemd/user/`, while `$HOME/.config/systemd/user/` is only for manual per-user copies

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

For breaking schema changes (new structured field replacing legacy keys), follow the **profiles** pattern: synthesize the new shape from legacy keys in `Config::load()`, write it back via `saveUnlocked()`, and keep legacy keys mirrored on every write for one release cycle so existing D-Bus/MPRIS callers don't break.

For additive structured config like `scenes`, add defaults through `defaultJson()`, typed accessors, sanitizing JSON conversion helpers, D-Bus property exposure, CLI parser coverage, and Config round-trip tests. Do not migrate or mirror unless replacing an existing field.

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
- `test_config` — 32 tests (merge, load/save, atomic save failure, thread-safety, profile migration / round-trip / mirror / ducking / scroll hotkeys / show hotkey / id uniqueification)
- `test_i18n` — 7 tests (lookup, fallback)
- `test_kvctlcommand` — 9 tests (subcommand parser, profile option, get/set fields, show command, invalid input)
- `test_pwutils` — 3 tests (PipeWire client filtering, skipped-name fallback, deduplication)
- `test_volumecontroller` — 5 smoke tests
- `test_inputhandler` — 26 tests (API, evdev device listing, modifier normalize, `resolveProfile` / ducking action / scroll binding / show volume action specificity)
- `test_mprisclient` — 13 tests (MPRIS player detection, metadata and track-id changes, seek forwarding, reload behavior, instance suffix matching, priority, polling guards)

Run locally: `cd cpp/build && ctest -E test_mprisclient --output-on-failure` and `cd cpp/build && dbus-run-session -- ctest -R test_mprisclient --output-on-failure`.

GitHub Actions CI is enabled in `.github/workflows/ci.yml` for PRs and pushes to
`main`. It builds and runs CTest in both Debug and Release, and checks
`clang-format` only for changed C++ files under `cpp/src` and `cpp/tests`.
The CI workflow is path-filtered: docs-only changes such as Markdown updates do
not run CI, while changes under `cpp/`, `pkg/`, `deploy/`, `resources/`, CMake
files, or `.github/workflows/ci.yml` do.
`clang-tidy` is not part of CI yet. `Claude Code Review` is currently
temporarily disabled via `if: false` in `.github/workflows/claude-code-review.yml`.

---

## Key Conventions

- **No global master volume changes** — all volume operations target a specific app's sink input
- **Evdev hotkey bindings** in config/hotkeys, not Qt key codes. Legacy integer values are `EV_KEY` codes; scroll is stored as `EV_REL` binding objects. Conversion from X11 keys: evdev = X11 keycode − 8
- **Config saves on every setter** — no explicit "save all" step needed
- **All PA/PipeWire operations on PaWorker thread** — never block the Qt event loop with libpulse or libpipewire calls
- **PA reconnect is part of the audio contract** — on context failure/termination, reconnect in `PaWorker` with backoff; do not lose pending volume/mute or the configured `selected_app`.
- **All hotkey devices grabbed exclusively** — siblings of the primary device AND every other device advertising the configured hotkey bindings; non-hotkey events re-injected via uinput so typing/scrolling is unaffected
- **No PipeWire subprocess fallback** — idle-app lookup and PW-node fallback use libpipewire directly, so `pw-dump`/`pw-cli` do not need to be in `PATH`
- **Wayland position workaround** — after every `widget.show()` that positions the OSD, also call `QWindow::setPosition()` on `windowHandle()`
- **Dialog centering on multi-monitor** — use `centerDialogOnScreenAt(window, QCursor::pos())` from `screenutils.h` before `exec()` for parentless dialogs. Never use event filters, QTimer hacks, or `Qt::Dialog` flag changes for positioning.
- **Icon embedded as Qt resource** — loaded via `:/icon.png` from `resources.qrc`; no external file needed at runtime
- **Two-phase App init** — constructor creates only `Config`; `init()` creates the rest after optional first-run wizard
- **`QDBusConnection::sessionBus()` returns by value** in Qt6 (not by reference as in Qt5) — use `auto bus = ...` not `auto &bus`
- **`ExportAdaptors` flag required** when registering objects that have `QDBusAbstractAdaptor` children (like the MPRIS endpoint). Without it, adaptor interfaces are not exported
- **Profiles are the source of truth** for hotkey → app mapping. Legacy `selected_app` and top-level `hotkeys` are deprecated mirrors of `profiles[0]`, kept in sync on every write for one release cycle of backwards compat (D-Bus/MPRIS callers reading the old fields keep working).
- **Scenes are additive mixer presets** — they do not change the active app/profile, and applying one only posts absolute per-app volume/mute operations to the existing audio worker.
- **Modifiers tracked only from grabbed devices** — a modifier key on a separate keyboard with no hotkey bindings won't be observed in v1. Documented limitation; v2 may add passive read-only opens for modifier-only devices.
