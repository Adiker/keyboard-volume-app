[English](#english) | [Polski](#polski)

---

<h2 id="english">🇬🇧 English</h2>

# keyboard-volume-app

A Linux-native alternative to AutoHotkey volume scripts for Windows. Controls the volume of a single chosen application via keyboard — without touching the system master volume. Pick an audio app from the tray icon, use the media volume wheel, and get an OSD overlay with the current level.

![Python](https://img.shields.io/badge/Python-3.10%2B-blue)
![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey)
![Desktop](https://img.shields.io/badge/Desktop-KDE%20Plasma-blue)
![Audio](https://img.shields.io/badge/Audio-PipeWire%20%2F%20PulseAudio-orange)

### Features

- **Per-app volume control** — changes the volume of only the selected application, not the system master
- **Global key capture** — reads directly from an evdev input device, works regardless of which window is focused
- **Multi-node grab** — automatically grabs all sibling event nodes of the chosen keyboard (e.g. main keyboard + Consumer Control interface) so the desktop never intercepts the keys
- **OSD overlay** — frameless, always-on-top window showing app name, volume bar and percentage; auto-hides after a configurable timeout
- **System tray** — select the active audio app, refresh the list, change input device or open settings from the tray menu
- **Idle app detection** — lists all apps connected to PipeWire, not just those currently playing audio
- **Mute toggle** — press the dedicated mute key on your keyboard (the media key that would normally mute the system) to toggle mute on the selected app only. The OSD appears showing the current volume level with a 🔇 indicator when muted
- **Persistent config** — all settings saved to `~/.config/keyboard-volume-app/config.json`
- **KDE autostart** — ships with a `.desktop` file for automatic startup with the desktop session
- **PL / EN interface** — switch language in Settings

### Requirements

| Dependency | Purpose |
|---|---|
| Python 3.10+ | — |
| [PyQt6](https://pypi.org/project/PyQt6/) | System tray, OSD window, settings dialogs |
| [evdev](https://pypi.org/project/evdev/) | Global keyboard input capture |
| [pulsectl](https://pypi.org/project/pulsectl/) | Per-app volume control via PipeWire/PulseAudio socket |
| `pw-dump` | Listing idle audio apps (part of `pipewire` package) |

### Installation

```bash
git clone git@github.com:Adiker/keyboard-volume-app.git
cd keyboard-volume-app

python3 -m venv .venv
source .venv/bin/activate

pip install -r requirements.txt
```

**Input device permissions** — evdev requires read access to `/dev/input/event*`. Add your user to the `input` group:

```bash
sudo usermod -aG input $USER
```

Log out and back in for the change to take effect.

### Running

```bash
cd keyboard-volume-app
source .venv/bin/activate
python3 -m src.main
```

On first launch a dialog will appear asking you to select an input device. The app filters the list to show only devices that expose volume keys (`KEY_VOLUMEUP` / `KEY_VOLUMEDOWN`).

### Autostart with KDE

```bash
cp keyboard-volume-app.desktop ~/.config/autostart/
```

> **Note:** Edit the `Exec=` and `Path=` lines in the file if you installed the project to a different location or use a virtual environment.

### Usage

1. **Select audio app** — click the tray icon → pick an app from the list. Apps currently playing audio are listed first; idle apps (connected to PipeWire but paused) appear below.
2. **Volume wheel** — scroll up/down to change the selected app's volume by the configured step.
3. **Mute** — press the dedicated mute key on your keyboard (the media key that would normally mute the system) to toggle mute on the selected app only. The OSD appears showing the current volume level with a 🔇 indicator when muted.
4. **Refresh app list** — tray menu → *Refresh app list* to re-scan running audio apps.
5. **Change input device** — tray menu → *Change input device...* to pick a different keyboard without restarting.
6. **Settings** — tray menu → *Settings...* to configure:
   - Interface language (English / Polski)
   - OSD display timeout (ms)
   - OSD screen position (X / Y)
   - Volume step per keypress (%)
   - OSD colors (background, text, progress bar)

### Configuration

Config file: `~/.config/keyboard-volume-app/config.json`

```json
{
  "input_device": "/dev/input/event3",
  "selected_app": "youtube-music",
  "language": "en",
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

### Project structure

```
keyboard-volume-app/
├── src/
│   ├── main.py              # Entry point, wires all modules together
│   ├── config.py            # JSON config read/write
│   ├── i18n.py              # PL/EN translations and tr() helper
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

### Performance

The volume change hot path (keypress → OSD update) uses a single pulsectl IPC call (~1ms). The heavier `pw-dump` subprocess is only invoked when the tray app list is opened, with a 5-second result cache to avoid redundant calls.

### License

MIT

---

<h2 id="polski">🇵🇱 Polski</h2>

# keyboard-volume-app

Linuksowa alternatywa dla skryptów AutoHotkey sterujących głośnością na Windowsie. Zmienia głośność wybranej aplikacji za pomocą klawiatury — bez ingerowania w głośność systemową. Wybierz aplikację audio z ikony w zasobniku systemowym, użyj kółka multimedialnego i obserwuj nakładkę OSD z aktualnym poziomem głośności.

![Python](https://img.shields.io/badge/Python-3.10%2B-blue)
![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey)
![Desktop](https://img.shields.io/badge/Desktop-KDE%20Plasma-blue)
![Audio](https://img.shields.io/badge/Audio-PipeWire%20%2F%20PulseAudio-orange)

### Funkcje

- **Sterowanie głośnością per aplikacja** — zmienia głośność wyłącznie wybranej aplikacji, nie ruszając głośności systemowej
- **Globalne przechwytywanie klawiszy** — odczytuje zdarzenia bezpośrednio z urządzenia evdev, działa niezależnie od tego, które okno jest aktywne
- **Przechwytywanie wielu węzłów** — automatycznie blokuje wszystkie powiązane węzły wejściowe wybranej klawiatury (np. główna klawiatura + interfejs Consumer Control), aby system nie przechwytywał klawiszy głośności
- **Nakładka OSD** — bezramkowe okno wyświetlane zawsze na wierzchu, pokazujące nazwę aplikacji, pasek głośności i wartość procentową; znika automatycznie po upływie skonfigurowanego czasu
- **Zasobnik systemowy** — wybór aktywnej aplikacji audio, odświeżanie listy, zmiana urządzenia wejściowego oraz dostęp do ustawień z menu ikony tray
- **Wykrywanie nieaktywnych aplikacji** — lista zawiera wszystkie aplikacje podłączone do PipeWire, nie tylko te aktualnie odtwarzające dźwięk
- **Wyciszenie** — naciśnij dedykowany klawisz mute na klawiaturze (ten multimedialny, który normalnie wyciszałby cały system), aby wyciszyć lub odciszyć wyłącznie wybraną aplikację; OSD pokazuje aktualny poziom głośności ze wskaźnikiem 🔇
- **Trwała konfiguracja** — wszystkie ustawienia zapisywane w `~/.config/keyboard-volume-app/config.json`
- **Autostart w KDE** — dołączony plik `.desktop` do automatycznego uruchamiania razem z sesją pulpitu
- **Interfejs PL / EN** — przełączanie języka w oknie ustawień

### Wymagania

| Zależność | Przeznaczenie |
|---|---|
| Python 3.10+ | — |
| [PyQt6](https://pypi.org/project/PyQt6/) | Zasobnik systemowy, okno OSD, dialogi ustawień |
| [evdev](https://pypi.org/project/evdev/) | Globalne przechwytywanie klawiszy |
| [pulsectl](https://pypi.org/project/pulsectl/) | Sterowanie głośnością per aplikacja przez gniazdo PipeWire/PulseAudio |
| `pw-dump` | Listowanie nieaktywnych aplikacji audio (część pakietu `pipewire`) |

### Instalacja

```bash
git clone git@github.com:Adiker/keyboard-volume-app.git
cd keyboard-volume-app

python3 -m venv .venv
source .venv/bin/activate

pip install -r requirements.txt
```

**Uprawnienia do urządzenia wejściowego** — evdev wymaga dostępu do odczytu plików `/dev/input/event*`. Dodaj swojego użytkownika do grupy `input`:

```bash
sudo usermod -aG input $USER
```

Wyloguj się i zaloguj ponownie, by zmiana weszła w życie.

### Uruchamianie

```bash
cd keyboard-volume-app
source .venv/bin/activate
python3 -m src.main
```

Przy pierwszym uruchomieniu pojawi się okno z prośbą o wybranie urządzenia wejściowego. Lista jest filtrowana — pokazuje tylko urządzenia posiadające klawisze głośności (`KEY_VOLUMEUP` / `KEY_VOLUMEDOWN`).

### Autostart w KDE

```bash
cp keyboard-volume-app.desktop ~/.config/autostart/
```

> **Uwaga:** jeśli zainstalowałeś projekt w innej lokalizacji lub używasz środowiska wirtualnego, dostosuj ścieżki w liniach `Exec=` i `Path=` tego pliku.

### Użytkowanie

1. **Wybór aplikacji audio** — kliknij ikonę w zasobniku systemowym → wybierz aplikację z listy. Aplikacje aktualnie odtwarzające dźwięk są na górze; nieaktywne (podłączone do PipeWire, ale zapauzowane) pojawiają się poniżej.
2. **Kółko głośności** — przekręć w górę lub w dół, by zmienić głośność wybranej aplikacji o skonfigurowany krok.
3. **Wyciszenie** — naciśnij dedykowany klawisz mute na klawiaturze (ten multimedialny, który normalnie wyciszałby cały system), aby wyciszyć lub odciszyć wyłącznie wybraną aplikację; OSD pokazuje aktualny poziom głośności ze wskaźnikiem 🔇.
4. **Odświeżenie listy** — menu tray → *Odśwież listę aplikacji*, by ponownie wczytać aktywne aplikacje audio.
5. **Zmiana urządzenia wejściowego** — menu tray → *Zmień urządzenie wejściowe...*, by wybrać inną klawiaturę bez restartu aplikacji.
6. **Ustawienia** — menu tray → *Ustawienia...*, by skonfigurować:
   - Język interfejsu (English / Polski)
   - Czas wyświetlania OSD (ms)
   - Pozycję OSD na ekranie (X / Y)
   - Krok zmiany głośności na jedno naciśnięcie klawisza (%)
   - Kolory OSD (tło, tekst, pasek)

### Konfiguracja

Plik konfiguracyjny: `~/.config/keyboard-volume-app/config.json`

```json
{
  "input_device": "/dev/input/event3",
  "selected_app": "youtube-music",
  "language": "pl",
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

Wszystkie pola są zapisywane automatycznie przez aplikację — ręczna edycja jest opcjonalna.

### Struktura projektu

```
keyboard-volume-app/
├── src/
│   ├── main.py              # Punkt wejścia, łączy wszystkie moduły
│   ├── config.py            # Odczyt i zapis konfiguracji JSON
│   ├── i18n.py              # Tłumaczenia PL/EN i funkcja tr()
│   ├── volume_controller.py # pulsectl — głośność i wyciszenie per aplikacja
│   ├── input_handler.py     # evdev QThread — globalne przechwytywanie klawiszy
│   ├── osd_window.py        # Nakładka OSD (PyQt6)
│   ├── tray_app.py          # Ikona tray i menu
│   ├── device_selector.py   # Dialog wyboru urządzenia wejściowego
│   └── settings_dialog.py   # Dialog ustawień OSD i głośności
├── resources/
│   └── icon.png
├── keyboard-volume-app.desktop
└── requirements.txt
```

### Wydajność

Hot path zmiany głośności (naciśnięcie klawisza → aktualizacja OSD) wykonuje jedno wywołanie IPC przez pulsectl (~1ms). Cięższy subprocess `pw-dump` jest uruchamiany wyłącznie przy otwieraniu listy aplikacji w menu tray, z 5-sekundowym cache'em wyników.

### Licencja

MIT
