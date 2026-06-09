# ARCHITECTURE.md — keyboard-volume-app

This is the full technical reference for the project: structure, module behavior,
runtime flows, threading, D-Bus/MPRIS details, config conventions, packaging, and
tests.

For mandatory agent workflow rules, branch policy, and sharp implementation
guardrails, read `AGENTS.md`. For Claude-specific quick-start context, read
`CLAUDE.md`.

---

## Project Context

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

> On native Wayland, Qt cannot position regular top-level windows via `move()` — the compositor ignores it. When built with `wayland-client` and `LayerShellQt >= 6.6`, the app probes `zwlr_layer_shell_v1` before `QApplication`; wlroots/KDE sessions use a per-window LayerShellQt OSD surface, while GNOME/unsupported compositors keep the `QT_QPA_PLATFORM=xcb` fallback unless the user explicitly overrides Qt's platform. OSD mouse resizing is handled inside `OSDWindow` rather than via compositor/window decorations, so it works on both the layer-shell path and the XWayland fallback.

---

## Project Structure

```
keyboard-volume-app/
├── cpp/
│   ├── CMakeLists.txt           # CMake build (Qt6, libevdev, libpulse, libpipewire, DBus)
│   ├── resources.qrc            # Qt resource file — embeds icon.png
│   ├── protocols/               # Wayland protocol XML definitions for window-tracker
│   │   └── wlr-foreign-toplevel-management-unstable-v1.xml
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
│       ├── osdlabelformat.h/cpp # Free function formatOsdLabelTemplate + LabelTokens
│       ├── albumartcache.h/cpp  # AlbumArtCache (file/data/http with on-disk cache)
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
│       ├── windowtracker.h/cpp  # Window focus monitor (X11 + Wayland backends) for auto-profile switching
│       ├── screenutils.h        # Header-only: centerDialogOnScreenAt() for multi-monitor XWayland
│       ├── appmatcher.h         # Header-only: matchBinaryToApp() — focused-window → AudioApp lookup
│       ├── audioapp.h           # AudioApp struct (display name, PA index, muted, volume)
│       └── waylandstate.h       # Declares global extern bool g_nativeWayland for LayerShellQt OSD routing
├── pkg/
│   └── arch/
│       └── PKGBUILD             # Arch Linux package (keyboard-volume-app-git)
├── resources/
│   ├── icon.png
│   └── keyboard-volume-app.desktop  # Desktop entry (distribution copy, no hardcoded paths)
├── deploy/
│   └── keyboard-volume-app.service  # systemd user service unit
├── .clang-format                # C++ code formatting style guidelines
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

**Termination signals:** `main.cpp` installs SIGTERM/SIGINT handlers through a self-pipe and `QSocketNotifier`, so systemd stop, logout, and terminal interrupts request `qApp->quit()` through the Qt event loop. This is required for `App::cleanup()` to stop `InputHandler` cleanly and preserve physical keyboard LED state.

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
    "tracked_players": ["spotify", "vlc", "strawberry", "harmonoid", "youtube"],
    "media_controls_enabled": true,
    "expose_mpris": false,
    "osd_scale": 1.0
  },
  "volume_step": 5,
  "hotkeys": { "volume_up": 115, "volume_down": 114, "mute": 113 },
  "media_hotkeys": {
    "play_pause": 0,
    "next": 0,
    "previous": 0,
    "stop": 0
  },
  "auto_profile_switch": false,
  "profiles": [
    { "id": "default", "name": "Default", "app": "youtube-music",
      "modifiers": [],
      "hotkeys": { "volume_up": 115, "volume_down": 114, "mute": 113 },
      "ducking": { "enabled": false, "volume": 25, "hotkey": 0 },
      "auto_switch": true,
      "vol_min": 0, "vol_max": 100,
      "sink": "" },
    { "id": "firefox-ctrl", "name": "Firefox (Ctrl)", "app": "firefox",
      "modifiers": ["ctrl"],
      "hotkeys": { "volume_up": 115, "volume_down": 114, "mute": 113 },
      "ducking": { "enabled": true, "volume": 25, "hotkey": 88 },
      "auto_switch": true,
      "vol_min": 10, "vol_max": 80,
      "sink": "alsa_output.usb-headset" }
  ],
  "scenes": [
    { "id": "meeting", "name": "Meeting", "hotkey": 88,
      "targets": [
        { "match": "Spotify", "volume": 10, "muted": false },
        { "match": "Discord", "volume": 80, "sink": "alsa_output.usb-headset" },
        { "match": "Steam", "muted": true }
      ] }
  ]
}
```

Hotkey values are evdev bindings. Legacy integer values still mean `EV_KEY` Linux evdev key codes (`KEY_VOLUMEUP`=115, `KEY_VOLUMEDOWN`=114, `KEY_MUTE`=113). Scroll bindings use object form such as `{ "type": "rel", "code": 8, "direction": 1 }` for `EV_REL / REL_WHEEL`.

**Media hotkeys (global, MPRIS dispatch).** `media_hotkeys` is a top-level object with `play_pause`, `next`, `previous`, `stop`. Each accepts the same `EV_KEY` integer or scroll-binding object as profile hotkeys. All four default to `0` (unassigned). Stored as `struct MediaHotkeyConfig { HotkeyBinding playPause, next, previous, stop; }` exposed via `Config::mediaHotkeys()` / `Config::setMediaHotkeys()`. Independent of profiles — `InputHandler` resolves bindings in the order **profile > scene > media**: profile bindings first (modifier-aware via `resolveProfileHotkey()`), then scene apply bindings (`resolveSceneHotkey()`, modifier-agnostic in v1, first scene wins on a duplicate binding), then `resolveMediaHotkey()`. Bound keys dispatch via signals `media_play_pause/next/previous/stop` from the InputHandler thread to `MprisClient` slots in the main thread (queued connection); when auto-profile switching has a focused audio target, `App::onFocusedBinaryChanged()` passes it to `MprisClient::setPreferredApp()` so media controls prefer the matching tracked player. If no focused player matches, `MprisClient` falls back to `tracked_players` priority and then the first Playing → Paused player. The same controls are exposed on D-Bus as `org.keyboardvolumeapp.VolumeControl.Media{PlayPause,Next,Previous,Stop}` and via `kv-ctl media play-pause|next|previous|stop`. Debounce reuses the 100 ms profile debounce table with sentinel keys `__media__` (media) and `__scene__:<id>` (scenes). `OsdConfig::mediaKeysOsdMode` controls optional OSD feedback for these hotkeys: `off` shows nothing, `action` shows only the pressed media action label, and `full` queries the selected/auto-active app volume through `VolumeController::queryVolume()` so the normal volume OSD appears.

**Scene hotkeys (global, scene dispatch).** Each `AudioScene` carries an optional `hotkey` binding. `InputHandler::setScenes()` / `currentScenes()` snapshot the scene list per run; assigned scene hotkeys join the grabbed-binding union so they are swallowed everywhere. A matched scene fires `scene_apply(QString sceneId)` from the InputHandler thread; `App` looks the id up in `Config` and calls `VolumeController::applyScene`. `App::onHotkeysMaybeChanged()` restarts the InputHandler when profiles, media hotkeys, **or scenes** change after Settings is saved.

**OSD playback progress config.** `progress_enabled` is the master toggle. `progress_interactive` allows OSD seek controls when the active MPRIS player is seekable. `progress_poll_ms` is clamped to `200..2000` because many players do not emit position changes.

`progress_label_mode` selects the track label preset:

- `app` — audio app name only (default).
- `title_artist` — `Title — Artist` on a single line.
- `artist_title` — `Artist — Title` on a single line.
- `app_track` — `{app}` on top, `Title — Artist` below.
- `player_track` — MPRIS player display name on top, `Title — Artist` below.
- `player_track_art` — same as `player_track` plus the album art on the left.
- `custom` — render `custom_label_top` and `custom_label_bottom` as templates with tokens `{app}`, `{player}`, `{title}`, `{artist}`, `{album}` (see `formatOsdLabelTemplate` in `osdlabelformat.h`). Empty token values are removed along with the immediately-following separator run, so `{title} — {artist}` collapses cleanly when an artist is missing. `custom_label_show_art` (bool, default `false`) toggles the album-art widget for this preset.

Legacy `progress_label_mode` values are migrated on load and persisted: `"track"` → `"title_artist"`, `"both"` → `"app_track"`. Unknown values collapse to `"app"`.

`tracked_players` is a priority allowlist matched case/format-insensitively against MPRIS service names, so names like `YoutubeMusic` can match config entries like `youtube-music`. When focus auto-switch has a matching audio target, that target is preferred within the allowlist before the priority fallback. `media_controls_enabled` shows or hides the prev/play-pause/next buttons row (default `true`). `media_keys_osd_mode` is `off` / `action` / `full` (default `off`); legacy `show_media_keys_osd: true` migrates to `action`, `false` migrates to `off`, and saves keep `show_media_keys_osd = mode != off` for older builds. `expose_mpris` controls whether `org.mpris.MediaPlayer2.keyboardvolumeapp` is registered on the session bus (default `false` — disabled to avoid false-positive detection by apps like discord-music-presence). `osd_scale` is an application-level size multiplier (0.5–3.0, default 1.0) applied on top of Qt DPI scaling; visible OSD edges/corners can update it at runtime through custom mouse resizing.

**Profiles** (canonical source of truth for hotkey → app mapping). Each entry:
- `struct Profile { QString id, name; QStringList apps; HotkeyConfig hotkeys; QSet<Modifier> modifiers; DuckingConfig ducking; bool autoSwitch; int volMin; int volMax; QString sink; }` — `sink` is the stable PulseAudio sink **name** (empty = system default; cleared in stream-restore when the user switches back to default in Settings)
- `struct DuckingConfig { bool enabled; int volume; HotkeyBinding hotkey; }` — manual per-profile Focus Audio. `volume` is clamped to `0..100`; unassigned binding means no hotkey.
- `enum class Modifier { Ctrl, Shift }` — left/right collapsed to canonical (Ctrl/Shift only in v1; Alt/Meta out of scope)
- API: `profiles()`, `setProfiles(QList<Profile>)` (validates non-empty + uniqueifies ids with numeric suffix on collision; clamps `volMin`/`volMax` to `0..100` and swaps when inverted), `defaultProfile()` (= `profiles().first()`), `setDefaultProfileApp(QString)`, `findProfileByApp(QString)` (case-insensitive contains match among auto_switch-enabled profiles)
- `autoSwitch` (default `true`) — whether this profile participates in auto-profile switching by focused window
- `volMin` / `volMax` (defaults `0` / `100`, percent) — per-profile absolute volume limits. `VolumeController::changeVolume`/`setVolume` clamp the resulting volume to `[volMin/100, volMax/100]` across all 4 fallback paths (active sink-input, stream-restore DB, PipeWire node, parked). Used by profile hotkeys (`App::changeVolume`), bare D-Bus `VolumeUp`/`VolumeDown`/`Volume` setter (against the default profile), and `VolumeUpProfile`/`VolumeDownProfile`. `ApplyScene` and `toggleDucking` intentionally bypass these limits — scenes are explicit mixer presets and ducking is a temporary override.
- `autoProfileSwitch()` / `setAutoProfileSwitch(bool)` — global on/off (default `false`)
- `Modifier` ↔ string helpers: `modifierToString(Modifier)`, `modifierFromString(QString)`

**Audio scenes** (named mixer presets). Each scene:
- `struct AudioScene { QString id, name; QList<SceneTarget> targets; HotkeyBinding hotkey; }`
- `struct SceneTarget { QString match; std::optional<int> volume; std::optional<bool> muted; std::optional<QString> sink; }`
- `match` uses the same app/binary names accepted by `VolumeController`; `volume` is clamped to `0..100`; omitted `volume`/`muted`/`sink` leaves that state unchanged. Sink-only targets are valid.
- `hotkey` is an optional global evdev binding (same key/scroll format as profile hotkeys). Serialized as the JSON `hotkey` field; missing in legacy configs → unassigned. Applying it routes through `scene_apply(id)` → `VolumeController::applyScene`.
- API: `scenes()`, `setScenes(QList<AudioScene>)` (uniqueifies ids and drops malformed targets; preserves each scene's hotkey).
- Editing: the Settings dialog has a Scenes table (name / target summary / hotkey) with Add / Edit / Remove / Duplicate / Apply, backed by `SceneEditDialog` (`sceneeditdialog.h/cpp`). Each target row has a `match` (typed or picked from `AppListWidget`), an optional `0..100` volume, and a mute mode of leave-unchanged / mute / unmute. Duplicating a scene clears the copy's hotkey so two scenes never share a binding by accident.
- Apply is single-sourced through `VolumeController::applyScene(const AudioScene&)` — tray menu, D-Bus `ApplyScene`, the Settings "Apply" button, and scene hotkeys all call it instead of duplicating the per-target loop. Sink routing is applied before volume/mute so per-device default volume can't override the scene's requested level. Scenes intentionally bypass per-profile volume limits.

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

### `cpp/src/appmatcher.h` — `matchBinaryToApp()`

Header-only helper that maps a focused-window binary name to an `AudioApp` from the PipeWire app cache, used by `App::onFocusedBinaryChanged()` to drive auto-profile switching.

```cpp
inline QString matchBinaryToApp(const QString &binary, const QList<AudioApp> &apps)
```

- Returns the `AudioApp::name` of the first app whose `name` or `binary` is a case-insensitive substring of the focused binary (or vice versa); empty otherwise.
- **Skips candidate fields that are empty** — `QString::contains("")` returns `true`, so an `AudioApp` with an empty `name` or `binary` would otherwise match every focused window and hijack auto-switching.

### `cpp/src/volumecontroller.h/cpp` — `VolumeController`, `PaWorker`, `AudioApp`

All PulseAudio/PipeWire operations run on a dedicated `PaWorker` thread (moved via `moveToThread`). The public API is **async**: `changeVolume`/`toggleMute`/`toggleDucking` post work via `QMetaObject::invokeMethod`; results come back as signals.

`toggleDucking(keepApp, duckVolume)` is manual per-profile Focus Audio: first call snapshots all known apps except `keepApp` and lowers them to `duckVolume`; the next call restores the saved volumes. v1 changes only volume, not mute.

4-level fallback strategy (in hot-path order):

1. **Active sink input** (libpulse IPC, ~0.5ms) — fastest, for currently playing apps
2. **Stream restore DB** (libpulse) — persists across pause/resume
3. **PipeWire node** (libpipewire) — for paused apps with an idle node
4. **Pending watcher** — stores desired volume/mute in `PaWorker`; applies when app reconnects

**Output sink routing** (separate from volume): `listSinks()` / `setAppSink(app, sinkName)` / `clearAppSinkOverride(app)` on the PA worker thread. Cascade: move all matching active sink-inputs → stream-restore `device` field → park in `m_pendingSinks` (not cleared by volume/mute `removePending()`; cleared only after stream-restore persistence succeeds in `doApplyPending`). `clearAppSinkOverride` writes `device=nullptr` to stream-restore; requests are parked in `m_pendingSinkClears` while PA is down and flushed on reconnect. `PA_SUBSCRIPTION_MASK_SINK` debounces `sinksReady`. `App::activateProfile()` routes profile sinks once per profile transition (`m_lastActivatedProfileId`, cleared on `settingsChanged` and `sinksReady`). Per-profile D-Bus methods call `applyProfileSink()` before volume/mute. PipeWire `target.object` fallback is intentionally out of scope in v1.

`listApps()` returns cached data immediately and posts a background refresh that emits `appsReady(list)`. The background watcher listens for new sink-input events and applies pending volumes.

Active sink inputs are matched case-insensitively against `application.name`, `application.process.binary`, and `media.name`, because some pipewire-pulse streams expose the controllable target and the user-facing app name in different fields. `listApps()` normalizes active stream display labels through `listPipeWireClients()` using `client.id` where possible, so tray names match the profile picker while the internal target remains controllable (for example tray/profile display `harmonoid`, target `mpv`).

`PaWorker` detects `PA_CONTEXT_FAILED` / `PA_CONTEXT_TERMINATED`, tears down the stale libpulse context, and reconnects with exponential backoff (500ms up to 30s). PA hot paths first check `contextReady()`; if PA is down, they skip libpulse, try the PipeWire libpipewire fallback, and otherwise park pending volume/mute until the app reconnects.

`PaWatcherThread` emits both `sinkInputAppeared` (PA NEW event) and `sinkInputRemoved` (PA REMOVE event). It also rebuilds its PA subscription after context loss. `PaWorker` connects both sink-input signals to a 500ms debounce `QTimer` (`m_refreshTimer`) that calls `doListApps(true)` — so the tray menu rebuilds automatically whenever an audio app starts or stops, without any manual Refresh click. The existing `appsReady → TrayApp::rebuildMenu` connection handles the UI update. `doApplyPending` (100ms one-shot) always fires before the debounce timer, so pending volumes are applied before the refreshed list is emitted.

App/binary filter constants (`SYSTEM_BINARIES`, `SKIP_APP_NAMES`) live in `pwutils.h` and are shared with `AppPage`.

### `cpp/src/inputhandler.h/cpp` — `InputHandler`, `KeyCaptureThread`

**`InputHandler`** (extends `QThread`): reads evdev events from the selected device and all other devices advertising any configured hotkey binding (`EV_KEY` keys or scroll `EV_REL` codes). All such devices are grabbed exclusively and mirrored via uinput so non-hotkey events pass through transparently. Hotkey events that resolve to a profile are swallowed (never re-injected); hotkey events with no matching profile are forwarded so typing/scrolling isn't blocked.

For grabbed devices with keyboard LEDs, the physical evdev node is opened read/write when possible. The uinput fd is added to the same `epoll()` loop so output `EV_LED` events from the desktop (`LED_NUML`, `LED_CAPSL`, `LED_SCROLLL`) are mirrored back to the physical keyboard via `libevdev_kernel_set_led_value()`. This keeps logical lock state and hardware indicators in sync while the desktop talks to the virtual uinput mirror. On shutdown, `InputHandler` preserves LED state for all uinput mirrors, destroys all mirrors/grabs, waits briefly for the desktop input stack to settle, then restores the preserved state on the physical devices; do not collapse this into per-device destructor cleanup or later uinput removals can switch LEDs off again.

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

**`EvdevDevice`** (RAII, move-only) in `evdevdevice.h/cpp` — manages fd, optional read/write mode for LED mirroring, `libevdev*`, grab/ungrab, and `libevdev_uinput*` with automatic cleanup in destructor. Used by `InputHandler`, `DeviceSelectorDialog`, and `FirstRunWizard`. `getVolumeDevices()` also lives in `inputhandler.h/cpp`.

**Device selection logic:**
- `findSiblingDevices(path)` — finds all nodes sharing the same `phys` prefix
- `findHotkeyDevices(path, bindings)` — siblings + every other device exposing at least one hotkey binding; all marked exclusive (grabbed + uinput mirror)

**`KeyCaptureThread`** (extends `QThread`): used in the settings dialog to capture a single keypress or wheel scroll for hotkey rebinding. Grabs all candidate devices, emits `hotkey_captured(binding)` or `cancelled()`.

### `cpp/src/osdwindow.h/cpp` — `OSDWindow`

Frameless, always-on-top Qt widget. Three height modes (all dimensions scale via `scaled()`):
- `OSD_H_BASE = 70` — volume only
- `OSD_H_PROGRESS = 112` — volume + progress row (no controls)
- `OSD_H_CONTROLS = 138` — volume + progress row + media controls row

Width is `OSD_W = 220` px. All constants are logical base values — `scaled(int base)` multiplies by `OsdConfig::osdScale` (0.5–3.0, default 1.0) and rounds.

Mouse resize. Because the OSD is frameless/layer-shell-capable, resizing is custom: `OSDWindow` detects all edges/corners, shows resize cursors, computes a proportional scale for the current height mode, clamps it to `0.5..3.0`, stops the hide timer while dragging, and persists the final `osd_scale` plus the adjusted `screen/x/y` on mouse release. Left/top drags move the OSD so the opposite edge stays anchored. Do not replace this with platform window decorations or compositor resize calls.

`showVolume(app, volume, muted)` — main display call, starts the auto-hide timer.
After `show()`, position is also set via `QWindow::setPosition()` for XWayland compatibility. On native Wayland, resize still updates the fixed widget size, but position changes go through LayerShellQt margins.

Progress row API:
- `setProgressEnabled(bool)` caches the master toggle and hides the row when disabled.
- `setProgressVisible(bool)` shows or hides the row for player presence and refreshes labels immediately so `progressLabelMode=track` falls back to the app label when the row disappears; it is a no-op while disabled.
- `updateTrack(title, artist, lengthUs, canSeek)` refreshes label mode, seekability and duration. `lengthUs <= 0` is live-stream mode: disabled bar and `LIVE` label.
- `updatePosition(positionUs)` maps the position to the progress bar's 0-1000 range and updates the elapsed/total time label.
- `eventFilter()` handles click/drag seeking on the progress bar. It emits `seekStarted()`, repeated `seekRequested(positionUs)`, applies the release coordinate as the final seek target, and emits `seekFinished()` on release/cancel. Always clear `m_seeking` via `finishSeeking()` before returning through interaction guards, otherwise `MprisClient` polling can remain suspended.
- `progressLabelMode` controls whether the main label shows the app, track, or app plus track row.

App and track labels use `MarqueeLabel` (inner class in `osdwindow.cpp`): when the text exceeds the widget width, it pauses 1.5 s, scrolls left at ~33 fps, pauses 1 s at the end, then resets. Short text that fits displays statically. No config option — always active for both labels.

Track label rendering. `refreshNameLabel()` builds `LabelTokens { app, player, title, artist, album }` and feeds `osd.progressLabelMode` through a switch: built-in presets map to fixed templates, `custom` reads `osd.customLabelTop` / `osd.customLabelBottom`. Both lines run through `formatOsdLabelTemplate(template, tokens)` (free function in `osdlabelformat.h/cpp`) which substitutes `{app|player|title|artist|album}`, then sentinel-walks empty tokens to strip the following separator run — so `"Title — "` collapses to `"Title"` without disturbing `"Spotify: Title"`. Unknown `{tokens}` are left literal for debuggability. An empty bottom-line result hides `m_labelTrack` entirely. `m_playerName` is fed by `App` from `MprisClient::activePlayerChanged` (capitalized for display, e.g. `"Spotify"` vs the service suffix `"spotify"`).

Album art. `m_albumArt` is a square `QLabel` (`scaled(36)`) placed to the left of the progress content via a new outer `QHBoxLayout` on `m_progressRow`. `refreshAlbumArtVisibility()` shows it for `player_track_art` always and for `custom` when `customLabelShowArt` is true. `OSDWindow::setAlbumArt(QPixmap)` (slot) installs a fresh image; `App` wires `AlbumArtCache::ready` to it, filtering stale results by comparing against `MprisClient::activePlayer().artUrl`. `AlbumArtCache` (`albumartcache.h/cpp`) backs three schemes: `file://` (synchronous), `data:` (base64 decode), `http(s)://` (`QNetworkAccessManager` async + on-disk SHA1 cache under `QStandardPaths::CacheLocation/keyboard-volume-app/art/`). LRU memory cache caps at 64 entries; no hard disk-size limit in v1.

Media controls row (inside `m_progressRow`, below the progress bar):
- `setMediaControlsEnabled(bool)` — shows/hides `m_controlsRow` (⏮ prev / ⏵⏸ play-pause / ⏭ next buttons) and calls `applySize()` to resize the window. Called from `reloadStyles()`.
- `updatePlaybackStatus(status)` — switches the play/pause button glyph: `\u23F5` (▶) when Paused/Stopped, `\u23F8` (⏸) when Playing. Connected from `MprisClient::playbackStatusChanged`.
- Clicking the buttons emits `playPauseRequested()`, `previousRequested()`, `nextRequested()` — wired to `MprisClient::playPause`, `previous`, `next` in `main.cpp`.
- Button styling: transparent background, OSD text color, hover = bar color; applied in `applyColorStyles()`.
- `applySize()` uses three-way logic: base / progress / progress+controls.

### `cpp/src/mprisclient.h/cpp` — `MprisClient`

Consumes external MPRIS2 players from the session bus. It detects services named `org.mpris.MediaPlayer2.*`, fetches `org.mpris.MediaPlayer2.Player` properties asynchronously, and emits active-player, track, position, playback-status and no-player signals for OSD playback progress wiring.

Selection is deterministic: filter by `Config::osd().trackedPlayers`, sort by that priority list, then prefer the first Playing player over the first Paused player. Matching uses the full service suffix after `org.mpris.MediaPlayer2.`, so instance names like `vlc.7389` still match tracked player `vlc`. `reload()` re-reads tracked players and poll interval, re-evaluates the active player, and re-emits the current track so settings toggles can show an already-active player immediately.

`MprisClient` lives in the main Qt thread only. All D-Bus reads are async via QtDBus; do not call it from `PaWorker` or `InputHandler`. It may keep active-player and metadata state current while progress is disabled, but it must not poll `Position` unless `Config::osd().progressEnabled` is true.

Harmonoid-specific guards: Harmonoid can push high-frequency `Position` via `PropertiesChanged` while an async `Get(Position)` poll briefly returns stale `0`; suppress Harmonoid poll replies that jump backwards by more than ~2s from the last accepted position. Harmonoid may also send transient incomplete metadata; keep the existing track identity/local TagLib duration when the same track briefly lacks URL, title/artist, or length. For live diagnosis, run with `KVA_DEBUG_PROGRESS=1` to log MPRIS→OSD progress decisions.

`PlayerInfo` struct: `service` (D-Bus name), `displayName`, `status` (`Playing`/`Paused`/`Stopped`), `canSeek`, `lengthUs`, `trackId`, `title`, `artist`, `album`, `artUrl`.

`album` and `artUrl` are parsed from `xesam:album` and `mpris:artUrl` metadata respectively. `artUrl` may be a `file://`, `http(s)://`, or `data:` URI; consumers route it through `AlbumArtCache` before rendering. Harmonoid transient-empty guards apply to `album` and `artUrl` too — when a fresh `Metadata` push lacks them but the trackid is unchanged, the previous values are preserved.

`PlayerInfo` includes playback capability flags: `canGoNext`, `canGoPrevious`, `canPause`, `canPlay` — read from D-Bus properties `CanGoNext`, `CanGoPrevious`, `CanPause`, `CanPlay` during initial fetch and `PropertiesChanged` updates.

Signals:
- `activePlayerChanged(PlayerInfo)` — new active player selected or lost
- `trackChanged(title, artist, lengthUs, canSeek)` — track metadata updated
- `albumArtChanged(QString artUrl)` — fired whenever the active player's `mpris:artUrl` changes, including when the player vanishes (empty string). Independent from `trackChanged` so callers can keep the album-art pipeline lean.
- `positionChanged(qint64 positionUs)` — position polled from active player (only when `progressEnabled`)
- `playbackStatusChanged(QString status)` — Playing/Paused/Stopped state changed
- `noPlayer()` — no tracked player available on the session bus

Playback control slots (async D-Bus calls to the active player, no-op when no active player):
- `playPause()` — calls `org.mpris.MediaPlayer2.Player.PlayPause()`
- `next()` — calls `Next()`; no-op when `!canGoNext`
- `previous()` — calls `Previous()`; falls back to `setPosition(0)` (restart track) when `!canGoPrevious`

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

Caches volume/mute/app state by listening to `VolumeController::volumeChanged` and `VolumeController::appsReady`; `App::initDbus()` wires `TrayApp::appChanged` to `DbusInterface::onActiveAppChanged()` so the tray drives active-app invalidation without `DbusInterface` having to depend on the GUI stack. D-Bus properties are `Volume`, `Muted`, `ActiveApp`, `Apps`, `VolumeStep`, `Profiles`, `Scenes`, `Sinks`, `ProgressEnabled`, and `AutoProfileSwitch`; setters delegate to `VolumeController`/`Config` async. `Sinks` is a `QVariantList` of `{name, description, is_default}` rebuilt on `sinksReady`. The `Volume` and `Muted` property writers route to absolute setters on `VolumeController` (`setVolume(app, target, vol_min, vol_max)` / `setMuted(app, muted)`) rather than relative delta/toggle calls — the cached values can be stale (startup, external pavucontrol changes), so an absolute set is the only way to honor the requested target. The global `Volume` property applies the default profile's `vol_min`/`vol_max` bounds. `ProgressEnabled` reads current `Config::osd()` to avoid stale cache after GUI settings changes.

D-Bus methods:
- `VolumeUp()`, `VolumeDown()`, `ToggleMute()`, `SetMute(bool)`, `ToggleDucking()`, `RefreshApps()` — bare methods target the default profile / `m_activeApp`, kept for backwards compat and script-friendly default-profile control. `SetMute` and the `Muted` property both drive an absolute mute state via `setMuted`.
- `VolumeUpProfile(QString id)`, `VolumeDownProfile(QString id)`, `ToggleMuteProfile(QString id)`, `SetMuteProfile(QString id, bool muted)`, `ToggleDuckingProfile(QString id)` — per-profile methods, route directly to the profile's app via `m_volumeCtrl`.
- `SetVolumeProfile(QString id, double vol)` — per-profile absolute volume setter. `vol` is `0.0..1.0` (clamped); unknown profile ids are silently a no-op. Used by `kv-ctl set volume X --profile id`.
- `ApplyScene(QString id)` — applies a configured audio scene through `VolumeController::applyScene` (posts `setAppSink` when `sink` set, before absolute volume/mute operations); unknown scene ids are a no-op.
- `SetAppSink(QString app, QString sink)` — ad-hoc per-app sink routing (empty args are no-ops).
- `ShowVolume()` — queries the current volume of `m_activeApp` via `VolumeController::queryVolume()` and emits `volumeChanged` → OSD displays without any value change.
- `ShowVolumeProfile(QString id)` — same as `ShowVolume()` but for the app in the named profile.

`Profiles` property is `QVariantList` of `QVariantMap` entries — `{id, name, app, apps, modifiers (QStringList "ctrl"/"shift"), hotkeys ({volume_up, volume_down, mute, show}), ducking ({enabled, volume, hotkey}), sink?}`. Hotkey values are legacy ints for `EV_KEY` bindings or maps for scroll bindings. `reloadProfiles()` rebuilds the cache from `Config` and emits `profilesChanged(QVariantList)` only when the value actually changed; wired from `TrayApp::settingsChanged` in `App`.
`Scenes` property is `QVariantList` of `QVariantMap` entries — `{id, name, hotkey, targets ([{match, volume?, muted?, sink?}])}`. `hotkey` is a legacy int for `EV_KEY` bindings (0 = unassigned) or a map for scroll bindings, matching the profile hotkey wire format. Rebuilt and signaled by `reloadProfiles()` alongside `Profiles`.
`ProgressEnabled` patches `OsdConfig::progressEnabled` and emits `progressEnabledChanged`; `App::initDbus()` wires that signal to `OSDWindow::reloadStyles()` and `MprisClient::reload()` so `kv-ctl set progress-enabled ...` takes effect without opening Settings or restarting the app. `reloadProgressEnabled()` keeps the D-Bus signal/cache aligned when Settings changes the same field.
`AutoProfileSwitch` maps to `Config::autoProfileSwitch()` and emits `autoProfileSwitchChanged`; `App::initDbus()` wires that signal to `App::onAutoSwitchMaybeChanged()` so `kv-ctl set auto-profile-switch true|false` starts/stops `WindowTracker` at runtime without a restart. `reloadAutoProfileSwitch()` keeps the D-Bus cache aligned when the Settings checkbox changes.

### `cpp/src/kvctl.cpp`, `cpp/src/kvctlcommand.h/cpp` — `kv-ctl`

`kv-ctl` is a small CLI client for scripts and tiling WM keybinds. Commands:
- `kv-ctl up|down|mute [--profile id]` maps to bare D-Bus methods or `VolumeUpProfile` / `VolumeDownProfile` / `ToggleMuteProfile`.
- `kv-ctl mute on|off [--profile id]` maps to `SetMute(bool)` or `SetMuteProfile(id, bool)` for deterministic mute state from scripts. Accepts `on|true|1|yes` and `off|false|0|no`.
- `kv-ctl duck [--profile id]` maps to bare `ToggleDucking` or `ToggleDuckingProfile(id)`; without `--profile`, the daemon resolves the current default profile server-side.
- `kv-ctl show [--profile id]` maps to `ShowVolume()` or `ShowVolumeProfile(id)`; displays the OSD with the current volume without changing it.
- `kv-ctl scene ID` maps to `ApplyScene(ID)`.
- `kv-ctl refresh` maps to `RefreshApps`.
- `kv-ctl get volume|muted|active-app|apps|step|profiles|scenes|sinks|progress-enabled|auto-profile-switch` reads D-Bus properties through `org.freedesktop.DBus.Properties.Get`. `kv-ctl get sink` (singular) is rejected with a hint to use `sinks`.
- `kv-ctl set volume|muted|active-app|step|progress-enabled|auto-profile-switch VALUE` writes properties through `org.freedesktop.DBus.Properties.Set`; volume is given as `0..100` percent and mapped to `0.0..1.0`, while `progress-enabled` and `auto-profile-switch` accept `true|false`. The optional `--profile ID` flag is accepted only for `set volume` and routes through `SetVolumeProfile` instead of the daemon-wide property setter. (Per-profile mute uses `kv-ctl mute on|off --profile id` via `SetMute`/`SetMuteProfile`.)
- `kv-ctl set sink APP DEVICE` calls `SetAppSink` (4-arg form; `--profile` rejected in v1).

Exit codes: `0` success, `1` usage, `2` daemon unavailable, `3` D-Bus error, `4` invalid value. Parser logic lives in `kvctlcommand` so it can be unit-tested without a session bus.

### `cpp/src/mprisinterface.h/cpp` — `MprisRootAdaptor`, `MprisPlayerAdaptor`

`QDBusAbstractAdaptor` subclasses providing MPRIS v2 compliance (bus name `org.mpris.MediaPlayer2.keyboardvolumeapp`, object path `/org/mpris/MediaPlayer2`).

- **`MprisRootAdaptor`** — `org.mpris.MediaPlayer2`: `Identity`="Keyboard Volume App", `CanQuit`=true, `Quit` → `qApp->quit()`.
- **`MprisPlayerAdaptor`** — `org.mpris.MediaPlayer2.Player`: `Volume` (R/W, delegates to `DbusInterface`), `PlaybackStatus`="Stopped", `CanControl`=true, `Metadata` with `xesam:title`=active app name. Play/Pause/Next/Previous are no-ops.

**Registration is controlled by `OsdConfig::exposeMpris` (default `false`).** The adaptors and their parent `QObject` are always created in `App::initDbus()`, but the D-Bus object and service are only registered when `exposeMpris` is `true`. `App::registerMprisEndpoint()` / `App::unregisterMprisEndpoint()` handle the toggle; `App::onMprisExposureChanged()` is wired to `TrayApp::settingsChanged` for live enable/disable without restart. The setting is exposed in Settings → Playback progress as a checkbox. This default-off behaviour prevents false-positive detection by apps like discord-music-presence that expect a fully compliant MPRIS player.

Enables integration with KDE Connect, Plasma widgets, and other MPRIS-aware tools when opted in.

Settings dialog with live OSD position and color preview, a **Playback progress** section, and a **Profiles** section. Playback progress controls patch `OsdConfig` through `Config::setOsd()`: `progressEnabled` checkbox, `progressInteractive` checkbox, `progressPollMs` spinbox (`200..2000`), `progressLabelMode` combo (`app` / `track` / `both`), comma-separated `trackedPlayers`, and `mediaKeysOsdMode` combo (`off` / `action` / `full`). `TrayApp::settingsChanged` is wired to `OSDWindow::reloadStyles()` and `MprisClient::reload()`, so enabling progress in the GUI refreshes the current active track immediately.

Profiles use a `QTableWidget` (`Name | App | Modifiers | VolUp | VolDown | Mute | Ducking`) with Add / Edit / Remove / Set as default buttons. The default profile is row 0 and rendered with a `(default)` suffix; Remove is disabled when only one profile remains. `saveAndAccept()` calls `Config::setProfiles(...)`; the existing `settingsChanged` signal triggers `App::onHotkeysMaybeChanged` which restarts `InputHandler` with the new profile set.

`HotkeyCapture` stops `InputHandler` during capture (releases the grabbed device) and restarts it after. Right-clicking a `HotkeyCapture` button opens a context menu with **Unassign** (`settings.hotkey.unassign`) that resets its binding to `HotkeyBinding{}` (unassigned).

`HotkeyCapture::keyDisplayName(const HotkeyBinding&)` converts a binding to a user-facing string (used in both button labels and the profiles table). Includes `<libevdev/libevdev.h>` in `settingsdialog.cpp` (no header change needed). Three-level fallback for `EV_KEY` bindings:
1. **Friendly-name map** (~95 entries, Title Case) — media keys, F1–F24, special/editing keys, arrows, modifiers, numpad, app-launch keys. Format: `"Volume Up (115)"`.
2. **libevdev symbolic name** — `libevdev_event_code_get_name(EV_KEY, code)`, strip the 4-char `"KEY_"` prefix. Format: `"Q (16)"`, `"F13 (183)"`.
3. **Generic fallback** — `"Key (N)"` for codes unknown to libevdev.
`EV_REL` bindings display as `"Wheel Up"` / `"Wheel Down"` / `"Wheel Right"` / `"Wheel Left"` (or generic `"REL N ±1"`) — unchanged. Unassigned bindings display as `"-"` — unchanged.

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
- 2× `QSpinBox` — per-profile **Volume limits** (`vol_min` / `vol_max`, both 0–100%, suffix `%`). Cross-clamped on edit so `vol_min <= vol_max` stays invariant as the user types. Defaults are `0` / `100` (no clamping).

`result()` builds a `Profile` from the widgets and preserves the original `id` when editing (a fresh Add gets the empty id and `Config::setProfiles()` slugifies/uniqueifies it).

An `auto_switch` checkbox (per-profile) controls whether the profile participates in auto-switching.

### `cpp/src/windowtracker.h/cpp` — `WindowTracker`

Runs in a dedicated `QThread` with two runtime-selected backends. On Wayland compositors exposing `zwlr_foreign_toplevel_management_unstable_v1`, it listens for foreign-toplevel `app_id` and `state` updates and emits after `done()` when the activated toplevel changes. Otherwise it falls back to polling XCB every 500ms for `_NET_ACTIVE_WINDOW`, reading `_NET_WM_PID`, and resolving the PID to a binary name via `/proc/PID/comm`. Emits `focusedBinaryChanged(QString)` when the focused app changes.

`App` receives the signal, matches the binary name against the PipeWire app cache (case-insensitive `contains` check against both `AudioApp::name` and `AudioApp::binary`), checks for an `auto_switch=true` profile whose `app` field matches, and overrides the volume target via `effectiveApp()`. When the focus moves to a window with no matching audio app, the fallback profile-resolved app is used instead.

Thread-safety: `WindowTracker::start()` sets `m_running = true` (atomic) before calling `QThread::start()`, avoiding a race where `stop()`'s `m_running = false` could be overwritten if `run()` hadn't yet entered its polling loop.

**Limitation:** Wayland focus tracking depends on `zwlr_foreign_toplevel_management_unstable_v1` (wlroots-compatible compositors). On unsupported pure Wayland sessions without XWayland, the tracker fails gracefully with an error log — the app continues to work normally, just without auto-switching.

### `cpp/src/i18n.h/cpp`

Static string tables for `en` and `pl`. `tr(key)` falls back to English if a key is missing in the current language.

### `cpp/src/waylandstate.h`

Declares the global boolean `g_nativeWayland` (`extern bool g_nativeWayland;`). It is initialized in `main()` before `QApplication` construction and is read-only thereafter. If `true`, the OSD uses native Wayland layer-shell for positioning; if `false`, it falls back to X11/XWayland window positioning via `move()` / `QWindow::setPosition()`.

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

OSD media control button click
    └─► OSDWindow emits playPauseRequested() / nextRequested() / previousRequested()
            └─► MprisClient::playPause() / next() / previous() [main thread]
                    ├─ playPause → async D-Bus call PlayPause() to active player
                    ├─ next     → async D-Bus call Next() (no-op if !canGoNext)
                    └─ previous → async D-Bus call Previous() (or setPosition(0) fallback)

MprisClient::playbackStatusChanged(status)
    └─► OSDWindow::updatePlaybackStatus(status) — switches ⏵/⏸ glyph on play-pause button
```

---

## Threading Model

| Thread | Type | Purpose |
|---|---|---|---|
| Main | Qt event loop | UI, signals, OSD updates, D-Bus dispatch |
| `InputHandler` | `QThread` | evdev polling with `epoll()` (50ms timeout) |
| `KeyCaptureThread` | `QThread` | One-shot key capture for hotkey rebinding |
| `WindowTracker` | `QThread` | Wayland foreign-toplevel events or XCB active-window polling for auto-profile switching |
| `PaWorker` | `QThread` (via `moveToThread`) | All PulseAudio/libpipewire operations |

D-Bus calls arrive on the main thread and are forwarded to `VolumeController` (which posts to `PaWorker`). `DbusInterface` property reads are served from main-thread caches — no blocking.

---

## D-Bus / MPRIS — `dbus-send` recipes

`kv-ctl` is the recommended client. When debugging without a `kv-ctl` build, or scripting on systems that do not ship `qdbus` (Qt6 makes it an optional package on several distros), use `dbus-send`. End-user `qdbus` examples live in `README.md`.

```bash
# Bump volume / mute on the default profile
dbus-send --session --dest=org.keyboardvolumeapp --type=method_call --print-reply \
  /org/keyboardvolumeapp org.keyboardvolumeapp.VolumeControl.VolumeUp
dbus-send --session --dest=org.keyboardvolumeapp --type=method_call --print-reply \
  /org/keyboardvolumeapp org.keyboardvolumeapp.VolumeControl.VolumeDown
dbus-send --session --dest=org.keyboardvolumeapp --type=method_call --print-reply \
  /org/keyboardvolumeapp org.keyboardvolumeapp.VolumeControl.ToggleMute
dbus-send --session --dest=org.keyboardvolumeapp --type=method_call --print-reply \
  /org/keyboardvolumeapp org.keyboardvolumeapp.VolumeControl.SetMute boolean:true

# Properties — Set (typed!) and Get
dbus-send --session --dest=org.keyboardvolumeapp --type=method_call --print-reply \
  /org/keyboardvolumeapp org.freedesktop.DBus.Properties.Set \
  string:org.keyboardvolumeapp.VolumeControl string:Volume variant:double:0.5
dbus-send --session --dest=org.keyboardvolumeapp --type=method_call --print-reply \
  /org/keyboardvolumeapp org.freedesktop.DBus.Properties.Set \
  string:org.keyboardvolumeapp.VolumeControl string:Muted variant:boolean:true
dbus-send --session --dest=org.keyboardvolumeapp --type=method_call --print-reply \
  /org/keyboardvolumeapp org.freedesktop.DBus.Properties.Set \
  string:org.keyboardvolumeapp.VolumeControl string:ActiveApp variant:string:"Firefox"
dbus-send --session --dest=org.keyboardvolumeapp --type=method_call --print-reply \
  /org/keyboardvolumeapp org.freedesktop.DBus.Properties.Set \
  string:org.keyboardvolumeapp.VolumeControl string:VolumeStep variant:int32:10
dbus-send --session --dest=org.keyboardvolumeapp --type=method_call --print-reply \
  /org/keyboardvolumeapp org.freedesktop.DBus.Properties.Get \
  string:org.keyboardvolumeapp.VolumeControl string:Volume
dbus-send --session --dest=org.keyboardvolumeapp --type=method_call --print-reply \
  /org/keyboardvolumeapp org.freedesktop.DBus.Properties.Get \
  string:org.keyboardvolumeapp.VolumeControl string:Apps

# Per-profile and scenes
dbus-send --session --dest=org.keyboardvolumeapp --type=method_call --print-reply \
  /org/keyboardvolumeapp org.keyboardvolumeapp.VolumeControl.SetVolumeProfile \
  string:firefox-ctrl double:0.35
dbus-send --session --dest=org.keyboardvolumeapp --type=method_call --print-reply \
  /org/keyboardvolumeapp org.keyboardvolumeapp.VolumeControl.SetMuteProfile \
  string:firefox-ctrl boolean:true
dbus-send --session --dest=org.keyboardvolumeapp --type=method_call --print-reply \
  /org/keyboardvolumeapp org.keyboardvolumeapp.VolumeControl.ApplyScene \
  string:meeting

# Refresh app list
dbus-send --session --dest=org.keyboardvolumeapp --type=method_call --print-reply \
  /org/keyboardvolumeapp org.keyboardvolumeapp.VolumeControl.RefreshApps

# Media controls (relayed to active MPRIS player)
dbus-send --session --dest=org.keyboardvolumeapp --type=method_call --print-reply \
  /org/keyboardvolumeapp org.keyboardvolumeapp.VolumeControl.MediaPlayPause
dbus-send --session --dest=org.keyboardvolumeapp --type=method_call --print-reply \
  /org/keyboardvolumeapp org.keyboardvolumeapp.VolumeControl.MediaNext
dbus-send --session --dest=org.keyboardvolumeapp --type=method_call --print-reply \
  /org/keyboardvolumeapp org.keyboardvolumeapp.VolumeControl.MediaPrevious
dbus-send --session --dest=org.keyboardvolumeapp --type=method_call --print-reply \
  /org/keyboardvolumeapp org.keyboardvolumeapp.VolumeControl.MediaStop
```

`dbus-send` requires explicit `variant:<type>:` for property writes; missing the variant wrapper or the wrong inner type returns `org.freedesktop.DBus.Error.InvalidArgs` from the property setter. The MPRIS endpoint follows the same recipes against bus name `org.mpris.MediaPlayer2.keyboardvolumeapp` and path `/org/mpris/MediaPlayer2`, but is registered only when `OsdConfig::exposeMpris == true`.

---

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

See **Git Workflow (mandatory)** and **Branch hygiene** in `AGENTS.md` — that file is the canonical source for branch naming, PR rules, force-push policy, and risk/rollback notes.

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
- `test_config` — 40 tests (merge, load/save, atomic save failure, thread-safety, profile migration / round-trip / mirror / ducking / scroll hotkeys / show hotkey / id uniqueification / per-profile vol_min and vol_max, legacy label-mode migration, custom-label defaults)
- `test_i18n` — 7 tests (lookup, fallback)
- `test_kvctlcommand` — 10 tests (subcommand parser, profile option, get/set fields, per-profile set volume, show command, invalid input)
- `test_pwutils` — 3 tests (PipeWire client filtering, skipped-name fallback, deduplication)
- `test_appmatcher` — 11 tests (focused-window → AudioApp lookup, including empty-field regression)
- `test_volumecontroller` — 5 smoke tests
- `test_inputhandler` — 26 tests (API, evdev device listing, modifier normalize, `resolveProfile` / ducking action / scroll binding / show volume action specificity)
- `test_mprisclient` — 15 tests (MPRIS player detection, metadata and track-id changes, seek forwarding, reload behavior, instance suffix matching, priority, polling guards, `mpris:artUrl` / `xesam:album` parsing, `albumArtChanged` empty-on-disconnect)
- `test_osdwindow` — OSD progress-row, label-preset, album-art, and mouse-resize regressions (including scale persistence, edge anchoring, clamp, timer, and progress-bar hit testing)
- `test_osdlabelformat` — 9 tests (token substitution, leading/trailing/middle separator trimming, unknown tokens preserved, multi-occurrence, internal whitespace preservation, all-empty)
- `test_dbusinterface` — 6 tests (Volume/Muted property writers route to absolute `setVolume`/`setMuted` instead of relative delta/toggle, clamping, no-op when no active app, `ToggleMute()` method still toggles)

Run locally: `cd cpp/build && ctest -E test_mprisclient --output-on-failure` and `cd cpp/build && dbus-run-session -- ctest -R test_mprisclient --output-on-failure`.

GitHub Actions CI is enabled in `.github/workflows/ci.yml` for PRs and pushes to
`main`. It builds and runs CTest in both Debug and Release, and checks
`clang-format` only for changed C++ files under `cpp/src` and `cpp/tests`.
The CI workflow is path-filtered: docs-only changes such as Markdown updates do
not run CI, while changes under `cpp/`, `pkg/`, `deploy/`, `resources/`, CMake
files, or `.github/workflows/ci.yml` do.
`clang-tidy` runs in CI as a warning-only job ("Clang-Tidy changed C++ files") on
changed `.cpp`/`.cc`/`.cxx` files under `cpp/src` and `cpp/tests`. It does **not**
block merge (`continue-on-error: true`). Configuration lives in `.clang-tidy` at
the repo root.

To run clang-tidy locally on changed files (requires a Debug build with
`compile_commands.json`):

```bash
# 1. Configure with compile_commands.json (only needed once / after CMake changes)
cmake -S cpp -B cpp/build-tidy -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++

# 2. Run on a specific file
clang-tidy -p cpp/build-tidy cpp/src/config.cpp

# 3. Run on all changed files vs main
git diff --name-only --diff-filter=ACMR origin/main...HEAD \
  | grep -E '^cpp/(src|tests)/.*\.(cpp|cc|cxx)$' \
  | xargs -r clang-tidy -p cpp/build-tidy
```

`Claude Code Review` is currently temporarily disabled via `if: false` in
`.github/workflows/claude-code-review.yml`.

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
