# Project Context

**keyboard-volume-app** is a Linux desktop utility that intercepts keyboard volume/mute keys at the evdev level and routes them to a single user-selected audio application, rather than the system master volume. It displays an OSD overlay (PyQt6 frameless window) showing the app name, volume bar, and percentage.

**Stack:** Python 3.10+, PyQt6, evdev, pulsectl  
**Audio backend:** PipeWire / PulseAudio (via pulsectl IPC + `pw-dump`/`pw-cli` subprocesses)  
**Platform:** Linux only (KDE Plasma primary target; XWayland required on Wayland sessions)

---

## Required Environment Variables

These are read at startup in `src/main.py` to decide whether to force XWayland:

- `WAYLAND_DISPLAY` — set by compositor when running under Wayland
- `XDG_SESSION_TYPE` — `"wayland"` or `"x11"`
- `QT_QPA_PLATFORM` — if unset and Wayland is detected, the app forces `xcb` (XWayland) so `QWidget.move()` works for OSD positioning

> On Wayland, Qt cannot position windows via `move()` — the compositor ignores it. The app auto-sets `QT_QPA_PLATFORM=xcb` unless the user explicitly overrides it.

---

## Project Structure

```
keyboard-volume-app/
├── src/
│   ├── main.py              # Entry point; App class wires all modules
│   ├── config.py            # JSON config r/w (~/.config/keyboard-volume-app/config.json)
│   ├── i18n.py              # PL/EN translations; tr() lookup function
│   ├── volume_controller.py # Per-app volume/mute via pulsectl + pw-dump/pw-cli
│   ├── input_handler.py     # evdev QThread — global key capture + uinput re-injection
│   ├── osd_window.py        # Frameless always-on-top PyQt6 OSD overlay
│   ├── tray_app.py          # System tray icon and context menu
│   ├── device_selector.py   # Dialog for picking an evdev input device
│   └── settings_dialog.py   # Settings dialog (OSD, volume step, hotkeys, colors)
├── resources/
│   └── icon.png
├── keyboard-volume-app.desktop  # KDE autostart entry
└── requirements.txt             # PyQt6, evdev, pulsectl
```

---

## Module Reference

### `src/main.py` — `App`, `main()`

The root coordinator. `App.__init__` constructs all components and wires Qt signals:

- `InputHandler` signals (`volume_up`, `volume_down`, `volume_mute`) → `_change_volume` / `_on_mute`
- `TrayApp` signals → device/settings changes, OSD preview
- `App.cleanup()` must be called on exit (stops evdev thread, closes pulsectl connections)

Entry point: `python3 -m src.main`

### `src/config.py` — `Config`

Reads/writes `~/.config/keyboard-volume-app/config.json`. Uses deep-merge so new default keys are always present even when loading old config files. All setters call `save()` immediately.

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

### `src/volume_controller.py` — `VolumeController`, `AudioApp`

Manages per-app volume and mute with a 4-level fallback strategy (in hot-path order):

1. **Active sink input** (pulsectl IPC, ~0.5ms) — fastest, for currently playing apps
2. **Stream restore DB** (pulsectl) — persists across pause/resume for PA-compatible apps
3. **PipeWire node** (`pw-dump` + `pw-cli`, ~30ms) — for paused apps with an idle node
4. **Pending watcher** — stores desired volume; background thread applies it when app reconnects

`list_apps()` returns active streams + idle PipeWire clients, sorted active-first. Results are cached for 5 seconds (`_LIST_CACHE_TTL`). The background watcher thread (`_watch_sink_inputs`) listens for new sink-input events and applies pending volumes immediately.

`_SYSTEM_BINARIES` and `_SKIP_APP_NAMES` filter out compositor/system processes from the app list.

### `src/input_handler.py` — `InputHandler`, `KeyCaptureThread`

**`InputHandler`** (extends `QThread`): reads evdev events from the selected device and all its siblings. Hotkey events are intercepted and emitted as Qt signals. Non-hotkey events are re-injected through a `UInput` virtual device so typing is not disrupted. Uses 100ms debounce per key code.

**Device selection logic:**
- `find_sibling_devices(path)` — finds all nodes sharing the same `phys` prefix (e.g., keyboard + Consumer Control interface); these are grabbed exclusively
- `find_hotkey_devices(path, codes)` — siblings get exclusive grab + uinput; other EV_KEY devices that expose at least one configured hotkey code get passive (no-grab) monitoring

**`KeyCaptureThread`** (extends `QThread`): used in the settings dialog to capture a single keypress for hotkey rebinding. Grabs all candidate devices, emits `key_captured(code)` or `cancelled()`.

### `src/osd_window.py` — `OSDWindow`

Frameless, always-on-top Qt widget (220×70 px). Uses `WA_TranslucentBackground` + custom `paintEvent` for rounded corners (background drawn manually via `QPainter` because Qt skips stylesheet background painting on translucent top-level windows).

Position is stored as screen-relative offset in config; `_abs_pos()` converts to absolute coordinates. After `show()`, position is also set via `QWindow.setPosition()` for XWayland compatibility.

`show_volume(app_name, volume, muted)` — main display call, starts the auto-hide timer.  
`apply_preview_colors(...)` — live color preview without saving.  
`show_preview_held(...)` / `release_preview(timeout_ms)` — used by the settings dialog Preview button press/release.

### `src/tray_app.py` — `TrayApp`

System tray icon with context menu. Emits signals for device/settings changes and OSD previews. The app list in the menu is rebuilt on refresh and after settings changes. `rebuild_menu()` is called on language change.

### `src/device_selector.py` — `DeviceSelectorDialog`

Filters `/dev/input/event*` to show only devices exposing `KEY_VOLUMEUP`/`KEY_VOLUMEDOWN`. Sets `config.input_device` on accept.

### `src/settings_dialog.py` — `SettingsDialog`, `ColorButton`, `HotkeyCapture`, `KeyCaptureDialog`

Settings dialog with live OSD position and color preview while editing. `HotkeyCapture` button stops the `InputHandler` during capture (so the grabbed device is released) and restarts it after.

**`KeyCaptureDialog`** has two parallel capture paths:
- evdev thread (`KeyCaptureThread`) — catches media/Consumer-Control keys
- Qt `keyPressEvent` + `nativeScanCode()` — catches regular keyboard keys (evdev code = X11 keycode − 8)

The first path to fire wins.

### `src/i18n.py`

Static string tables for `en` and `pl`. `tr(key)` falls back to English if a key is missing in the current language. `LANGUAGES` dict drives the language combo box.

---

## Signal Flow

```
Keyboard keypress (evdev)
    └─► InputHandler.run() [QThread]
            ├─ hotkey → emit volume_up/volume_down/volume_mute
            └─ other keys → UInput re-injection

volume_up/volume_down
    └─► App._change_volume(direction)
            └─► VolumeController.change_volume(app, delta)
                    └─► OSDWindow.show_volume(app, new_vol)

volume_mute
    └─► App._on_mute()
            └─► VolumeController.toggle_mute(app)
                    └─► OSDWindow.show_volume(app, vol, muted=True/False)
```

---

## Threading Model

| Thread | Type | Purpose |
|---|---|---|
| Main | Qt event loop | UI, signals, OSD updates |
| `InputHandler` | `QThread` | evdev polling with `select()` (0.2s timeout) |
| `KeyCaptureThread` | `QThread` | One-shot key capture for hotkey rebinding |
| `VolumeController._watcher_thread` | `threading.Thread` (daemon) | Watches for new sink inputs; applies pending volumes |

`VolumeController._lock` protects `_pending_volumes` / `_pending_mutes` between the main thread and watcher thread.

---

## Development Workflow

### Setup

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

User must be in the `input` group for evdev access:
```bash
sudo usermod -aG input $USER
# log out and back in
```

### Running

```bash
python3 -m src.main
```

### Adding a Translation Key

1. Add the key + English string to `_STRINGS["en"]` in `src/i18n.py`
2. Add the Polish translation to `_STRINGS["pl"]`
3. Use `tr("your.key")` in the UI code

### Adding a Config Field

1. Add the field with its default to `DEFAULTS` in `src/config.py`
2. Add a typed property with getter and setter (setter calls `self.save()`)
3. The deep-merge in `Config.load()` ensures existing config files get the new default

### OSD Styling

OSD background is not set via stylesheet (Qt skips it for translucent top-level windows). Background is drawn in `OSDWindow.paintEvent()` using `QPainter.drawRoundedRect()`. Colors and opacity are applied by `_apply_color_styles()`.

---

## Key Conventions

- **No global master volume changes** — all volume operations target a specific app's sink input
- **Evdev key codes** in config/hotkeys, not Qt key codes. Conversion: evdev = X11 keycode − 8
- **`from __future__ import annotations`** in all source files (deferred type evaluation)
- **Config saves on every setter** — no explicit "save all" step needed
- **`pw-dump` is slow** (~30ms) — only called for idle-app lookup and PW-node fallback; never in the main hotkey path
- **Wayland position workaround** — after every `widget.show()` that positions the OSD, also call `QWindow.setPosition()` on the `windowHandle()`

---

## Navigation Aids

- `.codesight/CODESIGHT.md` — auto-generated AI context map (class/function inventory)
- `.codesight/wiki/index.md` — quick orientation, lists all source files
- `.codesight/wiki/overview.md` — high-level overview and env var list
