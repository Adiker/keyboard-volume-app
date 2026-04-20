# keyboard-volume-app

Per-application volume control via keyboard on Linux (KDE Plasma + PipeWire). Replaces the need for AutoHotkey scripts on Windows — pick an audio app from the tray, scroll the volume wheel, see an OSD overlay.

![Python](https://img.shields.io/badge/Python-3.10%2B-blue)
![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey)
![Desktop](https://img.shields.io/badge/Desktop-KDE%20Plasma-blue)
![Audio](https://img.shields.io/badge/Audio-PipeWire%20%2F%20PulseAudio-orange)

---

## Features

- **Per-app volume control** — changes the volume of only the selected application, not the system master
- **Global key capture** — reads directly from an evdev input device, works regardless of which window is focused
- **Multi-node grab** — automatically grabs all sibling event nodes of the chosen keyboard (e.g. main keyboard + Consumer Control interface) so the desktop never intercepts the keys
- **OSD overlay** — frameless, always-on-top window showing app name, volume bar and percentage; auto-hides after a configurable timeout
- **System tray** — select the active audio app, refresh the list, change input device or open settings from the tray menu
- **Idle app detection** — lists all apps connected to PipeWire, not just those currently playing audio
- **Mute toggle** — mutes/unmutes the selected app and shows the state in the OSD
- **Persistent config** — all settings saved to `~/.config/keyboard-volume-app/config.json`
- **KDE autostart** — ships with a `.desktop` file for automatic startup with the desktop session

---

## Requirements

| Dependency | Purpose |
|---|---|
| Python 3.10+ | — |
| [PyQt6](https://pypi.org/project/PyQt6/) | System tray, OSD window, settings dialogs |
| [evdev](https://pypi.org/project/evdev/) | Global keyboard input capture |
| [pulsectl](https://pypi.org/project/pulsectl/) | Per-app volume control via PipeWire/PulseAudio socket |
| `pw-dump` | Listing idle audio apps (part of `pipewire` package) |

---

## Installation

```bash
git clone git@github.com:Adiker/keyboard-volume-app.git
cd keyboard-volume-app

python3 -m venv .venv
source .venv/bin/activate

pip install -r requirements.txt
```

### Input device permissions

evdev requires read access to `/dev/input/event*`. Add your user to the `input` group:

```bash
sudo usermod -aG input $USER
```

Log out and back in for the change to take effect.

---

## Running

```bash
cd keyboard-volume-app
source .venv/bin/activate
python3 -m src.main
```

On first launch a dialog will appear asking you to select an input device. The app filters the list to show only devices that expose volume keys (`KEY_VOLUMEUP` / `KEY_VOLUMEDOWN`).

---

## Autostart with KDE

Copy the provided `.desktop` file to the KDE autostart directory:

```bash
cp keyboard-volume-app.desktop ~/.config/autostart/
```

> **Note:** Edit the `Exec=` and `Path=` lines in the file if you installed the project to a different location or use a virtual environment.

---

## Usage

1. **Select audio app** — left-click the tray icon → pick an app from the list. Apps currently playing audio are listed first; idle apps (connected to PipeWire but paused) appear below.
2. **Volume wheel** — scroll up/down to change the selected app's volume by the configured step.
3. **Mute** — press the mute key to toggle mute on the selected app. The OSD shows the current volume and a 🔇 indicator.
4. **Refresh app list** — tray menu → *Odśwież listę aplikacji* to re-scan running audio apps.
5. **Change input device** — tray menu → *Zmień urządzenie wejściowe...* to pick a different keyboard without restarting.
6. **Settings** — tray menu → *Ustawienia...* to configure:
   - OSD display timeout (ms)
   - OSD screen position (X / Y)
   - Volume step per keypress (%)
   - OSD colors (background, text, progress bar)

---

## Configuration

Config file: `~/.config/keyboard-volume-app/config.json`

```json
{
  "input_device": "/dev/input/event3",
  "selected_app": "youtube-music",
  "osd": {
    "x": 50,
    "y": 1150,
    "timeout_ms": 1200,
    "color_bg": "#1A1A1A",
    "color_text": "#FFFFFF",
    "color_bar": "#0078D7"
  },
  "volume_step": 5
}
```

All fields are written automatically by the app — manual editing is optional.

---

## Project structure

```
keyboard-volume-app/
├── src/
│   ├── main.py              # Entry point, wires all modules together
│   ├── config.py            # JSON config read/write
│   ├── volume_controller.py # pulsectl — per-app volume and mute
│   ├── input_handler.py     # evdev QThread — global key capture
│   ├── osd_window.py        # PyQt6 OSD overlay
│   ├── tray_app.py          # System tray icon and menu
│   ├── device_selector.py   # Input device picker dialog
│   └── settings_dialog.py   # OSD/volume settings dialog
├── resources/
│   └── icon.png
├── keyboard-volume-app.desktop
└── requirements.txt
```

---

## Performance

The volume change hot path (keypress → OSD update) uses a single pulsectl IPC call (~1ms). The heavier `pw-dump` subprocess is only invoked when the tray app list is opened, with a 5-second result cache to avoid redundant calls.

---

## License

MIT
