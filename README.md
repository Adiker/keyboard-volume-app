| [🇬🇧 English](#english) | [🇵🇱 Polski](#polski) |
|:---:|:---:|
| [Python](#python-en) · [C++](#cpp-en) | [Python](#python-pl) · [C++](#cpp-pl) |

---

<h2 id="english">🇬🇧 English</h2>

# keyboard-volume-app

A Linux-native alternative to AutoHotkey volume scripts for Windows. Controls the volume of a single chosen application via keyboard — without touching the system master volume. Pick an audio app from the tray icon, use the media volume wheel, and get an OSD overlay with the current level.

This project has two independent implementations that share the same config file and feature set:

| | Python (`main`) | C++ (`cpp-rewrite`) |
|---|---|---|
| Runtime | Python 3.10+ | native binary |
| GUI toolkit | PyQt6 | Qt6 |
| Audio | pulsectl | libpulse |
| Input | evdev (Python) | libevdev |
| Build | pip + venv | cmake |
| Entry point | `python3 -m src.main` | `cpp/build/keyboard-volume-app` |

Jump to: [Python](#python-en) | [C++](#cpp-en)

---

<h3 id="python-en">🐍 Python implementation (branch: <code>main</code>)</h3>

![Python](https://img.shields.io/badge/Python-3.10%2B-blue)
![PyQt6](https://img.shields.io/badge/PyQt6-6-blue)
![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey)
![Desktop](https://img.shields.io/badge/Desktop-KDE%20Plasma-blue)
![Audio](https://img.shields.io/badge/Audio-PipeWire%20%2F%20PulseAudio-orange)

#### Features

- **Per-app volume control** — changes the volume of only the selected application, not the system master
- **Global key capture** — reads directly from an evdev input device, works regardless of which window is focused
- **Multi-node grab** — automatically grabs all sibling event nodes of the chosen keyboard (e.g. main keyboard + Consumer Control interface) so the desktop never intercepts the keys
- **Configurable hotkeys** — reassign Volume Up, Volume Down and Mute to any key via Settings; defaults are the dedicated media keys
- **OSD overlay** — frameless, always-on-top window showing app name, volume bar and percentage; auto-hides after a configurable timeout
- **System tray** — select the active audio app, refresh the list, change input device or open settings from the tray menu
- **Idle app detection** — lists all apps connected to PipeWire, not just those currently playing audio
- **Mute toggle** — press the dedicated mute key on your keyboard (the media key that would normally mute the system) to toggle mute on the selected app only. The OSD appears showing the current volume level with a 🔇 indicator when muted
- **Persistent config** — all settings saved to `~/.config/keyboard-volume-app/config.json`
- **KDE autostart** — ships with a `.desktop` file for automatic startup with the desktop session
- **PL / EN interface** — switch language in Settings

#### Requirements

| Dependency | Purpose |
|---|---|
| Python 3.10+ | — |
| [PyQt6](https://pypi.org/project/PyQt6/) | System tray, OSD window, settings dialogs |
| [evdev](https://pypi.org/project/evdev/) | Global keyboard input capture |
| [pulsectl](https://pypi.org/project/pulsectl/) | Per-app volume control via PipeWire/PulseAudio socket |
| `pw-dump` | Listing idle audio apps (part of `pipewire` package) |

#### Installation

```bash
git clone https://github.com/Adiker/keyboard-volume-app.git
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

#### Running

```bash
cd keyboard-volume-app
source .venv/bin/activate
python3 -m src.main
```

On first launch a dialog will appear asking you to select an input device. The app filters the list to show only devices that expose volume keys (`KEY_VOLUMEUP` / `KEY_VOLUMEDOWN`).

#### Autostart with KDE

```bash
cp keyboard-volume-app.desktop ~/.config/autostart/
```

> **Note:** Edit the `Exec=` and `Path=` lines in the file if you installed the project to a different location or use a virtual environment.

#### Usage

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
   - **Hotkeys** — click a key button and press any key to rebind Volume Up, Volume Down or Mute

> **Hotkey capture note:** the app grabs its configured keys at the evdev level, so those exact keys won't be visible to Qt while the app is running. To reassign *currently active* hotkeys, first bind them to temporary placeholders (e.g. F9/F10/F11), save and reopen Settings, then set the final keys.

#### Configuration

Config file: `~/.config/keyboard-volume-app/config.json`

```json
{
  "input_device": "/dev/input/event3",
  "selected_app": "youtube-music",
  "language": "en",
  "osd": {
    "screen": 0,
    "x": 50,
    "y": 1150,
    "timeout_ms": 1200,
    "color_bg": "#1A1A1A",
    "color_text": "#FFFFFF",
    "color_bar": "#0078D7"
  },
  "volume_step": 5,
  "hotkeys": {
    "volume_up": 115,
    "volume_down": 114,
    "mute": 113
  }
}
```

Hotkey values are Linux evdev key codes (`KEY_VOLUMEUP` = 115, `KEY_VOLUMEDOWN` = 114, `KEY_MUTE` = 113). The Settings dialog lets you change them interactively — manual editing of the JSON is also possible, but the GUI is the recommended way. All fields are written automatically by the app — manual editing is optional.

#### Project structure

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

#### Performance

The volume change hot path (keypress → OSD update) uses a single pulsectl IPC call (~1ms). The heavier `pw-dump` subprocess is only invoked when the tray app list is opened, with a 5-second result cache to avoid redundant calls.

---

<h3 id="cpp-en">⚙️ C++ implementation (branch: <code>cpp-rewrite</code>)</h3>

![C++](https://img.shields.io/badge/C%2B%2B-20-blue)
![Qt6](https://img.shields.io/badge/Qt-6-green)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-red)
![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey)
![Desktop](https://img.shields.io/badge/Desktop-KDE%20Plasma-blue)
![Audio](https://img.shields.io/badge/Audio-PipeWire%20%2F%20PulseAudio-orange)

#### Features

- **Per-app volume control** — changes the volume of only the selected application, not the system master
- **Global key capture** — reads directly from an evdev input device, works regardless of which window is focused
- **Multi-node grab** — automatically grabs all sibling event nodes of the chosen keyboard, plus any other device advertising volume keys, so the desktop never intercepts them
- **Configurable hotkeys** — reassign Volume Up, Volume Down and Mute to any key via Settings; defaults are the dedicated media keys
- **OSD overlay** — frameless, always-on-top window showing app name, volume bar and percentage; auto-hides after a configurable timeout
- **System tray** — select the active audio app, refresh the list, change input device or open settings from the tray menu
- **Idle app detection** — lists all apps connected to PipeWire, not just those currently playing audio
- **Mute toggle** — press the dedicated mute key on your keyboard to toggle mute on the selected app only. The OSD appears showing the current volume level with a 🔇 indicator when muted
- **Persistent config** — all settings saved to `~/.config/keyboard-volume-app/config.json` (same format as the Python version)
- **KDE autostart** — ships with a `.desktop` file for automatic startup with the desktop session
- **PL / EN interface** — switch language in Settings

#### Requirements

| Dependency | Purpose |
|---|---|
| Qt6 (Core, Widgets, Gui) | System tray, OSD window, settings dialogs |
| libevdev | Global keyboard input capture |
| libpulse | Per-app volume control via PipeWire/PulseAudio socket |
| `pw-dump` | Listing idle audio apps (part of `pipewire` package) |
| CMake 3.20+ | Build system |
| C++20 compiler | GCC 11+ or Clang 13+ |

#### Installation

```bash
git clone https://github.com/Adiker/keyboard-volume-app.git
cd keyboard-volume-app
git checkout cpp-rewrite
```

**Install build dependencies**

Arch / Manjaro:
```bash
sudo pacman -S qt6-base libevdev libpulse cmake gcc
```

Ubuntu / Debian (24.04+):
```bash
sudo apt install qt6-base-dev libevdev-dev libpulse-dev cmake g++
```

Fedora:
```bash
sudo dnf install qt6-qtbase-devel libevdev-devel pulseaudio-libs-devel cmake gcc-c++
```

**Build:**
```bash
cd cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The binary is written to `cpp/build/keyboard-volume-app`.

**Input device permissions** — evdev requires read access to `/dev/input/event*`. Add your user to the `input` group:

```bash
sudo usermod -aG input $USER
```

Log out and back in for the change to take effect.

#### Running

```bash
cpp/build/keyboard-volume-app
```

On first launch a dialog will appear asking you to select an input device. The app filters the list to show only devices that expose volume keys (`KEY_VOLUMEUP` / `KEY_VOLUMEDOWN`).

#### Autostart with KDE

```bash
cp keyboard-volume-app.desktop ~/.config/autostart/
```

> **Note:** Edit the `Exec=` line to point to the compiled binary (`cpp/build/keyboard-volume-app`) and update `Path=` to the repo root.

#### Usage

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
   - **Hotkeys** — click a key button and press any key to rebind Volume Up, Volume Down or Mute

> **Hotkey capture note:** the app grabs its configured keys at the evdev level, so those exact keys won't be visible to Qt while the app is running. To reassign *currently active* hotkeys, first bind them to temporary placeholders (e.g. F9/F10/F11), save and reopen Settings, then set the final keys.

#### Configuration

Config file: `~/.config/keyboard-volume-app/config.json` — identical format to the Python version; both implementations read from and write to the same file.

```json
{
  "input_device": "/dev/input/event3",
  "selected_app": "youtube-music",
  "language": "en",
  "osd": {
    "screen": 0,
    "x": 50,
    "y": 1150,
    "timeout_ms": 1200,
    "color_bg": "#1A1A1A",
    "color_text": "#FFFFFF",
    "color_bar": "#0078D7"
  },
  "volume_step": 5,
  "hotkeys": {
    "volume_up": 115,
    "volume_down": 114,
    "mute": 113
  }
}
```

Hotkey values are Linux evdev key codes (`KEY_VOLUMEUP` = 115, `KEY_VOLUMEDOWN` = 114, `KEY_MUTE` = 113). The Settings dialog lets you change them interactively — manual editing of the JSON is also possible, but the GUI is the recommended way. All fields are written automatically by the app — manual editing is optional.

#### Project structure

```
keyboard-volume-app/
├── cpp/
│   ├── CMakeLists.txt
│   └── src/
│       ├── main.cpp / main.h
│       ├── config.h / config.cpp
│       ├── i18n.h / i18n.cpp
│       ├── volumecontroller.h / volumecontroller.cpp
│       ├── inputhandler.h / inputhandler.cpp
│       ├── osdwindow.h / osdwindow.cpp
│       ├── trayapp.h / trayapp.cpp
│       ├── deviceselector.h / deviceselector.cpp
│       ├── settingsdialog.h / settingsdialog.cpp
│       └── audioapp.h
└── resources/
    └── icon.png
```

#### Performance

All PulseAudio operations run on a dedicated worker thread — the Qt event loop is never blocked. The hot path (keypress → OSD update) completes in ~1ms via a single libpulse IPC call. The heavier `pw-dump` subprocess is only invoked when the tray app list is opened, with a 5-second result cache to avoid redundant calls.

### License

MIT

---

<h2 id="polski">🇵🇱 Polski</h2>

# keyboard-volume-app

Linuksowa alternatywa dla skryptów AutoHotkey sterujących głośnością na Windowsie. Zmienia głośność wybranej aplikacji za pomocą klawiatury — bez ingerowania w głośność systemową. Wybierz aplikację audio z ikony w zasobniku systemowym, użyj kółka multimedialnego i obserwuj nakładkę OSD z aktualnym poziomem głośności.

Projekt posiada dwie niezależne implementacje, które współdzielą ten sam plik konfiguracyjny i zestaw funkcji:

| | Python (`main`) | C++ (`cpp-rewrite`) |
|---|---|---|
| Środowisko | Python 3.10+ | natywny binarny |
| Toolkit GUI | PyQt6 | Qt6 |
| Audio | pulsectl | libpulse |
| Wejście | evdev (Python) | libevdev |
| Instalacja | pip + venv | cmake |
| Uruchomienie | `python3 -m src.main` | `cpp/build/keyboard-volume-app` |

Przejdź do: [Python](#python-pl) | [C++](#cpp-pl)

---

<h3 id="python-pl">🐍 Implementacja Python (gałąź: <code>main</code>)</h3>

![Python](https://img.shields.io/badge/Python-3.10%2B-blue)
![PyQt6](https://img.shields.io/badge/PyQt6-6-blue)
![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey)
![Desktop](https://img.shields.io/badge/Desktop-KDE%20Plasma-blue)
![Audio](https://img.shields.io/badge/Audio-PipeWire%20%2F%20PulseAudio-orange)

#### Funkcje

- **Sterowanie głośnością per aplikacja** — zmienia głośność wyłącznie wybranej aplikacji, nie ruszając głośności systemowej
- **Globalne przechwytywanie klawiszy** — odczytuje zdarzenia bezpośrednio z urządzenia evdev, działa niezależnie od tego, które okno jest aktywne
- **Przechwytywanie wielu węzłów** — automatycznie blokuje wszystkie powiązane węzły wejściowe wybranej klawiatury (np. główna klawiatura + interfejs Consumer Control), aby system nie przechwytywał klawiszy głośności
- **Konfigurowalne skróty** — przypisz Głośność w górę, Głośność w dół i Wyciszenie do dowolnego klawisza przez Ustawienia; domyślnie są to dedykowane klawisze multimedialne
- **Nakładka OSD** — bezramkowe okno wyświetlane zawsze na wierzchu, pokazujące nazwę aplikacji, pasek głośności i wartość procentową; znika automatycznie po upływie skonfigurowanego czasu
- **Zasobnik systemowy** — wybór aktywnej aplikacji audio, odświeżanie listy, zmiana urządzenia wejściowego oraz dostęp do ustawień z menu ikony tray
- **Wykrywanie nieaktywnych aplikacji** — lista zawiera wszystkie aplikacje podłączone do PipeWire, nie tylko te aktualnie odtwarzające dźwięk
- **Wyciszenie** — naciśnij dedykowany klawisz mute na klawiaturze (ten multimedialny, który normalnie wyciszałby cały system), aby wyciszyć lub odciszyć wyłącznie wybraną aplikację; OSD pokazuje aktualny poziom głośności ze wskaźnikiem 🔇
- **Trwała konfiguracja** — wszystkie ustawienia zapisywane w `~/.config/keyboard-volume-app/config.json`
- **Autostart w KDE** — dołączony plik `.desktop` do automatycznego uruchamiania razem z sesją pulpitu
- **Interfejs PL / EN** — przełączanie języka w oknie ustawień

#### Wymagania

| Zależność | Przeznaczenie |
|---|---|
| Python 3.10+ | — |
| [PyQt6](https://pypi.org/project/PyQt6/) | Zasobnik systemowy, okno OSD, dialogi ustawień |
| [evdev](https://pypi.org/project/evdev/) | Globalne przechwytywanie klawiszy |
| [pulsectl](https://pypi.org/project/pulsectl/) | Sterowanie głośnością per aplikacja przez gniazdo PipeWire/PulseAudio |
| `pw-dump` | Listowanie nieaktywnych aplikacji audio (część pakietu `pipewire`) |

#### Instalacja

```bash
git clone https://github.com/Adiker/keyboard-volume-app.git
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

#### Uruchamianie

```bash
cd keyboard-volume-app
source .venv/bin/activate
python3 -m src.main
```

Przy pierwszym uruchomieniu pojawi się okno z prośbą o wybranie urządzenia wejściowego. Lista jest filtrowana — pokazuje tylko urządzenia posiadające klawisze głośności (`KEY_VOLUMEUP` / `KEY_VOLUMEDOWN`).

#### Autostart w KDE

```bash
cp keyboard-volume-app.desktop ~/.config/autostart/
```

> **Uwaga:** jeśli zainstalowałeś projekt w innej lokalizacji lub używasz środowiska wirtualnego, dostosuj ścieżki w liniach `Exec=` i `Path=` tego pliku.

#### Użytkowanie

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
   - **Skróty klawiszowe** — kliknij przycisk z nazwą klawisza i naciśnij dowolny klawisz, by przypisać go do Głośności w górę, Głośności w dół lub Wyciszenia

> **Uwaga dot. przechwytywania klawiszy:** aplikacja blokuje aktualnie skonfigurowane klawisze na poziomie evdev, więc te właśnie klawisze nie są widoczne dla Qt podczas działania programu. Aby zmienić *aktywne* skróty, najpierw przypisz je do tymczasowych klawiszy (np. F9/F10/F11), zapisz i otwórz Ustawienia ponownie, by ustawić docelowe klawisze.

#### Konfiguracja

Plik konfiguracyjny: `~/.config/keyboard-volume-app/config.json`

```json
{
  "input_device": "/dev/input/event3",
  "selected_app": "youtube-music",
  "language": "pl",
  "osd": {
    "screen": 0,
    "x": 50,
    "y": 1150,
    "timeout_ms": 1200,
    "color_bg": "#1A1A1A",
    "color_text": "#FFFFFF",
    "color_bar": "#0078D7"
  },
  "volume_step": 5,
  "hotkeys": {
    "volume_up": 115,
    "volume_down": 114,
    "mute": 113
  }
}
```

Wartości skrótów to kody klawiszy evdev (`KEY_VOLUMEUP` = 115, `KEY_VOLUMEDOWN` = 114, `KEY_MUTE` = 113). Zalecanym sposobem zmiany jest dialog Ustawień — ręczna edycja JSON jest możliwa, ale nie jest konieczna. Wszystkie pola są zapisywane automatycznie przez aplikację — ręczna edycja jest opcjonalna.

#### Struktura projektu

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

#### Wydajność

Hot path zmiany głośności (naciśnięcie klawisza → aktualizacja OSD) wykonuje jedno wywołanie IPC przez pulsectl (~1ms). Cięższy subprocess `pw-dump` jest uruchamiany wyłącznie przy otwieraniu listy aplikacji w menu tray, z 5-sekundowym cache'em wyników.

---

<h3 id="cpp-pl">⚙️ Implementacja C++ (gałąź: <code>cpp-rewrite</code>)</h3>

![C++](https://img.shields.io/badge/C%2B%2B-20-blue)
![Qt6](https://img.shields.io/badge/Qt-6-green)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-red)
![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey)
![Desktop](https://img.shields.io/badge/Desktop-KDE%20Plasma-blue)
![Audio](https://img.shields.io/badge/Audio-PipeWire%20%2F%20PulseAudio-orange)

#### Funkcje

- **Sterowanie głośnością per aplikacja** — zmienia głośność wyłącznie wybranej aplikacji, nie ruszając głośności systemowej
- **Globalne przechwytywanie klawiszy** — odczytuje zdarzenia bezpośrednio z urządzenia evdev, działa niezależnie od tego, które okno jest aktywne
- **Przechwytywanie wielu węzłów** — automatycznie blokuje wszystkie powiązane węzły wejściowe wybranej klawiatury, a także inne urządzenia eksponujące klawisze głośności, aby system nie przechwytywał ich
- **Konfigurowalne skróty** — przypisz Głośność w górę, Głośność w dół i Wyciszenie do dowolnego klawisza przez Ustawienia; domyślnie są to dedykowane klawisze multimedialne
- **Nakładka OSD** — bezramkowe okno wyświetlane zawsze na wierzchu, pokazujące nazwę aplikacji, pasek głośności i wartość procentową; znika automatycznie po upływie skonfigurowanego czasu
- **Zasobnik systemowy** — wybór aktywnej aplikacji audio, odświeżanie listy, zmiana urządzenia wejściowego oraz dostęp do ustawień z menu ikony tray
- **Wykrywanie nieaktywnych aplikacji** — lista zawiera wszystkie aplikacje podłączone do PipeWire, nie tylko te aktualnie odtwarzające dźwięk
- **Wyciszenie** — naciśnij dedykowany klawisz mute na klawiaturze, aby wyciszyć lub odciszyć wyłącznie wybraną aplikację; OSD pokazuje aktualny poziom głośności ze wskaźnikiem 🔇
- **Trwała konfiguracja** — wszystkie ustawienia zapisywane w `~/.config/keyboard-volume-app/config.json` (ten sam format co wersja Python)
- **Autostart w KDE** — dołączony plik `.desktop` do automatycznego uruchamiania razem z sesją pulpitu
- **Interfejs PL / EN** — przełączanie języka w oknie ustawień

#### Wymagania

| Zależność | Przeznaczenie |
|---|---|
| Qt6 (Core, Widgets, Gui) | Zasobnik systemowy, okno OSD, dialogi ustawień |
| libevdev | Globalne przechwytywanie klawiszy |
| libpulse | Sterowanie głośnością per aplikacja przez gniazdo PipeWire/PulseAudio |
| `pw-dump` | Listowanie nieaktywnych aplikacji audio (część pakietu `pipewire`) |
| CMake 3.20+ | System budowania |
| Kompilator C++20 | GCC 11+ lub Clang 13+ |

#### Instalacja

```bash
git clone https://github.com/Adiker/keyboard-volume-app.git
cd keyboard-volume-app
git checkout cpp-rewrite
```

**Instalacja zależności budowania**

Arch / Manjaro:
```bash
sudo pacman -S qt6-base libevdev libpulse cmake gcc
```

Ubuntu / Debian (24.04+):
```bash
sudo apt install qt6-base-dev libevdev-dev libpulse-dev cmake g++
```

Fedora:
```bash
sudo dnf install qt6-qtbase-devel libevdev-devel pulseaudio-libs-devel cmake gcc-c++
```

**Budowanie:**
```bash
cd cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Binarny plik wynikowy: `cpp/build/keyboard-volume-app`.

**Uprawnienia do urządzenia wejściowego** — evdev wymaga dostępu do odczytu plików `/dev/input/event*`. Dodaj swojego użytkownika do grupy `input`:

```bash
sudo usermod -aG input $USER
```

Wyloguj się i zaloguj ponownie, by zmiana weszła w życie.

#### Uruchamianie

```bash
cpp/build/keyboard-volume-app
```

Przy pierwszym uruchomieniu pojawi się okno z prośbą o wybranie urządzenia wejściowego. Lista jest filtrowana — pokazuje tylko urządzenia posiadające klawisze głośności (`KEY_VOLUMEUP` / `KEY_VOLUMEDOWN`).

#### Autostart w KDE

```bash
cp keyboard-volume-app.desktop ~/.config/autostart/
```

> **Uwaga:** ustaw linię `Exec=` na ścieżkę do skompilowanego pliku binarnego (`cpp/build/keyboard-volume-app`) i zaktualizuj `Path=` na katalog główny repozytorium.

#### Użytkowanie

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
   - **Skróty klawiszowe** — kliknij przycisk z nazwą klawisza i naciśnij dowolny klawisz, by przypisać go do Głośności w górę, Głośności w dół lub Wyciszenia

> **Uwaga dot. przechwytywania klawiszy:** aplikacja blokuje aktualnie skonfigurowane klawisze na poziomie evdev, więc te właśnie klawisze nie są widoczne dla Qt podczas działania programu. Aby zmienić *aktywne* skróty, najpierw przypisz je do tymczasowych klawiszy (np. F9/F10/F11), zapisz i otwórz Ustawienia ponownie, by ustawić docelowe klawisze.

#### Konfiguracja

Plik konfiguracyjny: `~/.config/keyboard-volume-app/config.json` — identyczny format jak w wersji Python; obie implementacje czytają z tego samego pliku i zapisują do niego.

```json
{
  "input_device": "/dev/input/event3",
  "selected_app": "youtube-music",
  "language": "pl",
  "osd": {
    "screen": 0,
    "x": 50,
    "y": 1150,
    "timeout_ms": 1200,
    "color_bg": "#1A1A1A",
    "color_text": "#FFFFFF",
    "color_bar": "#0078D7"
  },
  "volume_step": 5,
  "hotkeys": {
    "volume_up": 115,
    "volume_down": 114,
    "mute": 113
  }
}
```

Wartości skrótów to kody klawiszy evdev (`KEY_VOLUMEUP` = 115, `KEY_VOLUMEDOWN` = 114, `KEY_MUTE` = 113). Zalecanym sposobem zmiany jest dialog Ustawień — ręczna edycja JSON jest możliwa, ale nie jest konieczna. Wszystkie pola są zapisywane automatycznie przez aplikację — ręczna edycja jest opcjonalna.

#### Struktura projektu

```
keyboard-volume-app/
├── cpp/
│   ├── CMakeLists.txt
│   └── src/
│       ├── main.cpp / main.h
│       ├── config.h / config.cpp
│       ├── i18n.h / i18n.cpp
│       ├── volumecontroller.h / volumecontroller.cpp
│       ├── inputhandler.h / inputhandler.cpp
│       ├── osdwindow.h / osdwindow.cpp
│       ├── trayapp.h / trayapp.cpp
│       ├── deviceselector.h / deviceselector.cpp
│       ├── settingsdialog.h / settingsdialog.cpp
│       └── audioapp.h
└── resources/
    └── icon.png
```

#### Wydajność

Wszystkie operacje PulseAudio wykonywane są w dedykowanym wątku roboczym — pętla zdarzeń Qt nigdy nie jest blokowana. Hot path (naciśnięcie klawisza → aktualizacja OSD) wykonuje jedno wywołanie IPC przez libpulse (~1ms). Cięższy subprocess `pw-dump` jest uruchamiany wyłącznie przy otwieraniu listy aplikacji w menu tray, z 5-sekundowym cache'em wyników.

### Licencja

MIT
