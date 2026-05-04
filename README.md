[English](#english) | [Polski](#polski)

---

> **Notice:** C++/Qt6 is now the primary version of this project. The original Python/PyQt6 implementation is preserved in the [`python-legacy`](https://github.com/Adiker/keyboard-volume-app/tree/python-legacy) branch and tagged as [`python-last`](https://github.com/Adiker/keyboard-volume-app/releases/tag/python-last).

<h2 id="english">🇬🇧 English</h2>

# keyboard-volume-app

A Linux-native alternative to AutoHotkey volume scripts for Windows. Controls the volume of a single chosen application via keyboard — without touching the system master volume. Pick an audio app from the tray icon, use the media volume wheel, and get an OSD overlay with the current level.

![C++](https://img.shields.io/badge/C%2B%2B-20-blue)
![Qt](https://img.shields.io/badge/Qt-6-green)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-red)
![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey)
![Desktop](https://img.shields.io/badge/Desktop-KDE%20Plasma-blue)
![Audio](https://img.shields.io/badge/Audio-PipeWire%20%2F%20PulseAudio-orange)

### Features

- **Per-app volume control** — changes the volume of only the selected application, not the system master
- **Multiple audio profiles** — define several profiles, each with its own hotkeys, optional `Ctrl`/`Shift` modifiers, and target audio app. Bare `VolUp` controls Spotify, `Ctrl+VolUp` controls Firefox, `F11` controls VLC — all from the same keyboard
- **Global key capture** — reads directly from an evdev input device, works regardless of which window is focused
- **Multi-node grab** — automatically grabs all sibling event nodes of the chosen keyboard (e.g. main keyboard + Consumer Control interface) plus any other device advertising volume keys from any profile, so the desktop never intercepts them
- **Configurable hotkeys** — every profile's Volume Up, Volume Down and Mute keys are reassignable via Settings → Profiles; defaults are the dedicated media keys
- **OSD overlay** — frameless, always-on-top window showing app name, volume bar and percentage; auto-hides after a configurable timeout
- **System tray** — select the active audio app, refresh the list, change input device or open settings from the tray menu
- **Idle app detection** — lists all apps connected to PipeWire, not just those currently playing audio
- **Audio backend recovery** — reconnects to PulseAudio/pipewire-pulse after daemon restarts while keeping the configured selected app
- **Mute toggle** — press the mute key to toggle mute on the selected app only; OSD shows current level with a 🔇 indicator
- **Persistent config** — all settings saved to `$XDG_CONFIG_HOME/keyboard-volume-app/config.json` (defaults to `~/.config/keyboard-volume-app/`)
- **PL / EN interface** — switch language in Settings
- **First-run wizard** — on first launch, a QWizard guides through language, input device, and default audio app selection; the app is production-ready out of the box after a few clicks
- **D-Bus control** — full remote access via `org.keyboardvolumeapp.VolumeControl`: read/write volume, mute, active app, app list, volume step, **profiles**; bare `VolumeUp/Down/ToggleMute/RefreshApps` methods plus per-profile `VolumeUpProfile/VolumeDownProfile/ToggleMuteProfile(id)`
- **MPRIS v2** — registered as `org.mpris.MediaPlayer2.keyboardvolumeapp` for desktop volume widgets, KDE Connect, and any MPRIS-compatible client
- **CLI flags** — `--help` and `--version` for quick help and version info without starting the app
- **Unit tests** — GTest + Qt Test suite covering Config, i18n, PipeWire utilities, VolumeController, and InputHandler

### Requirements

| Dependency | Purpose |
|---|---|
| Qt6 (Widgets, DBus) | System tray, OSD window, settings dialogs |
| libevdev + libuinput | Global keyboard input capture and re-injection |
| libpulse | Per-app volume control via PipeWire/PulseAudio socket |
| libpipewire | Listing and controlling idle PipeWire audio apps without subprocesses |
| GTest | Unit tests (optional, `BUILD_TESTING=ON`) |
| CMake 3.20+ | Build system |
| C++20 compiler | GCC 11+ or Clang 13+ |

### Installation

#### Arch Linux (PKGBUILD)

```bash
git clone https://github.com/Adiker/keyboard-volume-app.git
cd keyboard-volume-app/pkg/arch
makepkg -si
```

This clones `main`, builds a Release binary, and installs everything to `/usr` including the `.desktop` entry, icon, and systemd user service.

#### Build from source

```bash
git clone git@github.com:Adiker/keyboard-volume-app.git
cd keyboard-volume-app
```

**Install dependencies**

Arch / Manjaro:
```bash
sudo pacman -S qt6-base libevdev libpulse pipewire cmake gcc gtest
```

Ubuntu / Debian:
```bash
sudo apt install qt6-base-dev libevdev-dev libpulse-dev libpipewire-0.3-dev cmake g++ libgtest-dev
```

**Build**
```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build -j$(nproc)
```

**Input device permissions** — evdev requires read access to `/dev/input/event*`. Add your user to the `input` group:

```bash
sudo usermod -aG input $USER
```

Log out and back in for the change to take effect.

**Autostart with systemd user service** — packaged/system installs place the unit in `/usr/lib/systemd/user`. Enable it per user:

```bash
systemctl --user daemon-reload
systemctl --user enable --now keyboard-volume-app.service
```

Disable it with:

```bash
systemctl --user disable --now keyboard-volume-app.service
```

For a manual per-user install without a package, copy `deploy/keyboard-volume-app.service` to `$HOME/.config/systemd/user/` and adjust `ExecStart` if the binary is not installed as `/usr/bin/keyboard-volume-app`. The app still needs evdev access, so keep the `input` group setup above.

### Running

```bash
cpp/build/keyboard-volume-app
```

On first launch the **first-run wizard** guides you through language selection, input device configuration, and default audio app selection. The device list is filtered to show only keyboards that expose volume keys (`KEY_VOLUMEUP` / `KEY_VOLUMEDOWN`).

**CLI flags:**

```bash
cpp/build/keyboard-volume-app --help     # Show help
cpp/build/keyboard-volume-app --version  # Show version
```

### Testing

```bash
cmake -S cpp -B cpp/build -DBUILD_TESTING=ON
cmake --build cpp/build
cd cpp/build && ctest
```

Tests cover the Config manager, i18n translations, VolumeController (smoke test), and InputHandler (API-only, no device required). Requires `gtest` / `libgtest-dev` package (see Requirements).

### Usage

1. **Select audio app** — click the tray icon → pick an app from the list. Apps currently playing audio are listed first; idle apps (connected to PipeWire but paused) appear below.
2. **Volume wheel** — scroll up/down to change the selected app's volume by the configured step.
3. **Mute** — press the mute key to toggle mute on the selected app only. The OSD appears with a 🔇 indicator when muted.
4. **Refresh app list** — tray menu → *Refresh app list* to re-scan running audio apps.
5. **Change input device** — tray menu → *Change input device...* to pick a different keyboard without restarting.
6. **Settings** — tray menu → *Settings...* to configure:
   - Interface language (English / Polski)
   - OSD display timeout (ms)
   - OSD screen position (X / Y)
   - OSD opacity (0–100%)
   - Volume step per keypress (%)
   - OSD colors (background, text, progress bar)
   - **Profiles** — add / edit / remove audio profiles, each with its own hotkeys, optional `Ctrl`/`Shift` modifiers and target app; row 0 is the default profile (used by the tray and by bare D-Bus / MPRIS calls)

7. **D-Bus remote control** — use `qdbus` or `dbus-send` to drive the app from scripts, custom keybinds, or external tools:

   ```bash
   # Bump volume on the default profile's app
   qdbus org.keyboardvolumeapp /org/keyboardvolumeapp org.keyboardvolumeapp.VolumeControl.VolumeUp

   # Bump volume on a specific profile
   qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
       org.keyboardvolumeapp.VolumeControl.VolumeUpProfile firefox-ctrl

   # List all profiles
   qdbus org.keyboardvolumeapp /org/keyboardvolumeapp org.freedesktop.DBus.Properties.Get \
       org.keyboardvolumeapp.VolumeControl Profiles

   # Switch to Firefox
   qdbus org.keyboardvolumeapp /org/keyboardvolumeapp org.freedesktop.DBus.Properties.Set \
       org.keyboardvolumeapp.VolumeControl ActiveApp "Firefox"

   # Read current volume
   qdbus org.keyboardvolumeapp /org/keyboardvolumeapp org.freedesktop.DBus.Properties.Get \
       org.keyboardvolumeapp.VolumeControl Volume
   ```

> **Hotkey capture note:** the app grabs its configured keys at the evdev level, so those exact keys won't be visible to Qt while the app is running. To reassign *currently active* hotkeys, first bind them to temporary placeholders (e.g. F9/F10/F11), save and reopen Settings, then set the final keys.

### Configuration

Config file: `$XDG_CONFIG_HOME/keyboard-volume-app/config.json` (defaults to `~/.config/keyboard-volume-app/`)

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
    "opacity": 85,
    "color_bg": "#1A1A1A",
    "color_text": "#FFFFFF",
    "color_bar": "#0078D7"
  },
  "volume_step": 5,
  "hotkeys": {
    "volume_up": 115,
    "volume_down": 114,
    "mute": 113
  },
  "profiles": [
    { "id": "default", "name": "Default", "app": "youtube-music",
      "modifiers": [],
      "hotkeys": { "volume_up": 115, "volume_down": 114, "mute": 113 } },
    { "id": "firefox-ctrl", "name": "Firefox (Ctrl)", "app": "firefox",
      "modifiers": ["ctrl"],
      "hotkeys": { "volume_up": 115, "volume_down": 114, "mute": 113 } }
  ]
}
```

Hotkey values are Linux evdev key codes (`KEY_VOLUMEUP` = 115, `KEY_VOLUMEDOWN` = 114, `KEY_MUTE` = 113). The top-level `selected_app` and `hotkeys` are kept as a deprecated mirror of `profiles[0]` for one release of backwards compatibility — `profiles` is the canonical source of truth. Old config files without `profiles` are migrated automatically on first launch.

### Project structure

```
keyboard-volume-app/
├── cpp/
│   ├── CMakeLists.txt
│   ├── resources.qrc            # Qt resource manifest (embeds icon)
│   ├── src/
│       ├── main.cpp             # Entry point, wires all modules together
│       ├── config.h/cpp         # JSON config read/write
│       ├── i18n.h/cpp           # PL/EN translations and tr() helper
│       ├── volumecontroller.h/cpp  # libpulse — per-app volume and mute
│       ├── inputhandler.h/cpp   # evdev QThread — global key capture (epoll)
│       ├── evdevdevice.h/cpp    # RAII evdev device wrapper (open/close/grab)
│       ├── osdwindow.h/cpp      # Qt6 OSD overlay
│       ├── trayapp.h/cpp        # System tray icon and menu
│       ├── deviceselector.h/cpp # Input device picker dialog
│       ├── settingsdialog.h/cpp # OSD/volume/profiles settings dialog
│       ├── profileeditdialog.h/cpp # Sub-dialog for editing a single audio profile
│       ├── firstrunwizard.h/cpp  # First-run wizard (language + device + app)
│       ├── dbusinterface.h/cpp   # D-Bus VolumeControl interface
│       ├── mprisinterface.h/cpp  # MPRIS v2 adaptor
│       ├── pwutils.h/cpp         # PipeWire client listing utility
│       ├── applistwidget.h/cpp   # Reusable PW app list widget
│       ├── appselectordialog.h/cpp  # Dialog for changing default audio app
│       ├── screenutils.h         # Header-only multi-monitor dialog centering
│       └── audioapp.h           # AudioApp struct
│   └── tests/
│       ├── CMakeLists.txt
│       ├── test_config.cpp
│       ├── test_i18n.cpp
│       ├── test_inputhandler.cpp
│       ├── test_pwutils.cpp
│       └── test_volumecontroller.cpp
├── deploy/
│   └── keyboard-volume-app.service  # systemd user service
├── pkg/
│   └── arch/
│       └── PKGBUILD             # Arch Linux package (keyboard-volume-app-git)
├── resources/
│   ├── icon.png
│   └── keyboard-volume-app.desktop  # Desktop entry for distribution
├── LICENSE
├── AGENTS.md
├── CLAUDE.md
├── GEMINI.md
└── ROADMAP.md
```

### Performance

The volume change hot path (keypress → OSD update) uses a single libpulse IPC call (~1ms). Idle PipeWire app listing and paused-node fallback use libpipewire directly, so the app does not spawn `pw-dump` or `pw-cli` subprocesses. All PulseAudio/PipeWire operations run on a dedicated worker thread — the Qt event loop is never blocked. If the PulseAudio context fails or terminates, the worker reconnects with backoff and keeps pending volume/mute state until the target app reconnects. Transient app-list refreshes during audio daemon restarts do not replace the configured selected app. D-Bus property reads are served from a local cache (zero IPC); writes delegate asynchronously to the PulseAudio worker thread.

### License

GPL-2.0-or-later — see [LICENSE](LICENSE)

---

> **Uwaga:** C++/Qt6 jest teraz główną wersją tego projektu. Oryginalna implementacja Python/PyQt6 została zachowana w gałęzi [`python-legacy`](https://github.com/Adiker/keyboard-volume-app/tree/python-legacy) i oznaczona tagiem [`python-last`](https://github.com/Adiker/keyboard-volume-app/releases/tag/python-last).

<h2 id="polski">🇵🇱 Polski</h2>

# keyboard-volume-app

Linuksowa alternatywa dla skryptów AutoHotkey sterujących głośnością na Windowsie. Zmienia głośność wybranej aplikacji za pomocą klawiatury — bez ingerowania w głośność systemową. Wybierz aplikację audio z ikony w zasobniku systemowym, użyj kółka multimedialnego i obserwuj nakładkę OSD z aktualnym poziomem głośności.

![C++](https://img.shields.io/badge/C%2B%2B-20-blue)
![Qt](https://img.shields.io/badge/Qt-6-green)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-red)
![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey)
![Desktop](https://img.shields.io/badge/Desktop-KDE%20Plasma-blue)
![Audio](https://img.shields.io/badge/Audio-PipeWire%20%2F%20PulseAudio-orange)

### Funkcje

- **Sterowanie głośnością per aplikacja** — zmienia głośność wyłącznie wybranej aplikacji, nie ruszając głośności systemowej
- **Wiele profili audio** — definiuj kilka profili, każdy z własnymi skrótami, opcjonalnymi modyfikatorami `Ctrl`/`Shift` i docelową aplikacją. `VolUp` steruje Spotify, `Ctrl+VolUp` steruje Firefoxem, `F11` steruje VLC — wszystko z tej samej klawiatury
- **Globalne przechwytywanie klawiszy** — odczytuje zdarzenia bezpośrednio z urządzenia evdev, działa niezależnie od tego, które okno jest aktywne
- **Przechwytywanie wielu węzłów** — automatycznie blokuje wszystkie powiązane węzły wejściowe wybranej klawiatury oraz każde inne urządzenie zgłaszające klawisze użyte w którymkolwiek profilu, aby system nie przechwytywał ich
- **Konfigurowalne skróty** — Głośność w górę, Głośność w dół i Wyciszenie każdego profilu można przypisać do dowolnego klawisza przez Ustawienia → Profile; domyślnie są to dedykowane klawisze multimedialne
- **Nakładka OSD** — bezramkowe okno wyświetlane zawsze na wierzchu, pokazujące nazwę aplikacji, pasek głośności i wartość procentową; znika automatycznie po upływie skonfigurowanego czasu
- **Zasobnik systemowy** — wybór aktywnej aplikacji audio, odświeżanie listy, zmiana urządzenia wejściowego oraz dostęp do ustawień
- **Wykrywanie nieaktywnych aplikacji** — lista zawiera wszystkie aplikacje podłączone do PipeWire, nie tylko aktualnie odtwarzające dźwięk
- **Odzyskiwanie backendu audio** — ponownie łączy się z PulseAudio/pipewire-pulse po restarcie daemona i zachowuje skonfigurowaną wybraną aplikację
- **Wyciszenie** — naciśnij klawisz mute, aby wyciszyć lub odciszyć wyłącznie wybraną aplikację; OSD pokazuje aktualny poziom ze wskaźnikiem 🔇
- **Trwała konfiguracja** — wszystkie ustawienia zapisywane w `$XDG_CONFIG_HOME/keyboard-volume-app/config.json` (domyślnie `~/.config/keyboard-volume-app/`)
- **Interfejs PL / EN** — przełączanie języka w oknie ustawień
- **Asystent pierwszego uruchomienia** — przy pierwszym starcie QWizard przeprowadza przez wybór języka, urządzenia wejściowego i domyślnej aplikacji audio; aplikacja działa od razu po kilku kliknięciach
- **Sterowanie przez D-Bus** — pełne zdalne sterowanie przez `org.keyboardvolumeapp.VolumeControl`: odczyt/zapis głośności, wyciszenia, wybór aplikacji, lista aplikacji, krok głośności, **profile**; bare metody `VolumeUp/Down/ToggleMute/RefreshApps` plus per-profile `VolumeUpProfile/VolumeDownProfile/ToggleMuteProfile(id)`
- **MPRIS v2** — zarejestrowany jako `org.mpris.MediaPlayer2.keyboardvolumeapp` dla widżetów głośności pulpitu, KDE Connect i każdego klienta MPRIS
- **Flagi CLI** — `--help` i `--version` do szybkiego podglądu pomocy i wersji bez uruchamiania aplikacji
- **Testy jednostkowe** — GTest + Qt Test dla Config, i18n, narzędzi PipeWire, VolumeController i InputHandler

### Wymagania

| Zależność | Przeznaczenie |
|---|---|
| Qt6 (Widgets, DBus) | Zasobnik systemowy, okno OSD, dialogi ustawień |
| libevdev + libuinput | Globalne przechwytywanie klawiszy i reinjekcja zdarzeń |
| libpulse | Sterowanie głośnością per aplikacja przez gniazdo PipeWire/PulseAudio |
| libpipewire | Listowanie i sterowanie nieaktywnymi aplikacjami PipeWire bez subprocessów |
| GTest | Testy jednostkowe (opcjonalne, `BUILD_TESTING=ON`) |
| CMake 3.20+ | System budowania |
| Kompilator C++20 | GCC 11+ lub Clang 13+ |

### Instalacja

#### Arch Linux (PKGBUILD)

```bash
git clone https://github.com/Adiker/keyboard-volume-app.git
cd keyboard-volume-app/pkg/arch
makepkg -si
```

Pobiera branch `main`, buduje binarę Release i instaluje wszystko do `/usr` wraz z wpisem `.desktop`, ikoną i usługą systemd user.

#### Budowanie ze źródeł

```bash
git clone git@github.com:Adiker/keyboard-volume-app.git
cd keyboard-volume-app
```

**Instalacja zależności**

Arch / Manjaro:
```bash
sudo pacman -S qt6-base libevdev libpulse pipewire cmake gcc gtest
```

Ubuntu / Debian:
```bash
sudo apt install qt6-base-dev libevdev-dev libpulse-dev libpipewire-0.3-dev cmake g++ libgtest-dev
```

**Kompilacja**
```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build -j$(nproc)
```

**Uprawnienia do urządzenia wejściowego** — evdev wymaga dostępu do odczytu plików `/dev/input/event*`. Dodaj swojego użytkownika do grupy `input`:

```bash
sudo usermod -aG input $USER
```

Wyloguj się i zaloguj ponownie, by zmiana weszła w życie.

**Autostart przez systemd user service** — instalacja pakietowa/systemowa umieszcza unit w `/usr/lib/systemd/user`. Włącz go dla swojego użytkownika:

```bash
systemctl --user daemon-reload
systemctl --user enable --now keyboard-volume-app.service
```

Wyłącz go poleceniem:

```bash
systemctl --user disable --now keyboard-volume-app.service
```

Przy ręcznej instalacji per-user bez paczki skopiuj `deploy/keyboard-volume-app.service` do `$HOME/.config/systemd/user/` i dostosuj `ExecStart`, jeśli binarka nie jest zainstalowana jako `/usr/bin/keyboard-volume-app`. Aplikacja nadal wymaga dostępu evdev, więc konfiguracja grupy `input` powyżej pozostaje wymagana.

### Uruchamianie

```bash
cpp/build/keyboard-volume-app
```

Przy pierwszym uruchomieniu **asystent pierwszego uruchomienia** przeprowadzi przez wybór języka, urządzenia wejściowego i domyślnej aplikacji audio. Lista urządzeń jest filtrowana — pokazuje tylko klawiatury posiadające klawisze głośności (`KEY_VOLUMEUP` / `KEY_VOLUMEDOWN`).

**Flagi CLI:**

```bash
cpp/build/keyboard-volume-app --help     # Pokaż pomoc
cpp/build/keyboard-volume-app --version  # Pokaż wersję
```

### Testowanie

```bash
cmake -S cpp -B cpp/build -DBUILD_TESTING=ON
cmake --build cpp/build
cd cpp/build && ctest
```

Testy obejmują Config, i18n, VolumeController (test dymny) i InputHandler (API, bez potrzeby urządzenia). Wymaga pakietu `gtest` / `libgtest-dev` (zobacz Wymagania).

### Użytkowanie

1. **Wybór aplikacji audio** — kliknij ikonę w zasobniku systemowym → wybierz aplikację z listy. Aplikacje aktualnie odtwarzające dźwięk są na górze; nieaktywne (podłączone do PipeWire, ale zapauzowane) pojawiają się poniżej.
2. **Kółko głośności** — przekręć w górę lub w dół, by zmienić głośność wybranej aplikacji o skonfigurowany krok.
3. **Wyciszenie** — naciśnij klawisz mute, aby wyciszyć lub odciszyć wyłącznie wybraną aplikację; OSD pokazuje aktualny poziom ze wskaźnikiem 🔇.
4. **Odświeżenie listy** — menu tray → *Odśwież listę aplikacji*, by ponownie wczytać aktywne aplikacje audio.
5. **Zmiana urządzenia wejściowego** — menu tray → *Zmień urządzenie wejściowe...*, by wybrać inną klawiaturę bez restartu aplikacji.
6. **Ustawienia** — menu tray → *Ustawienia...*, by skonfigurować:
   - Język interfejsu (English / Polski)
   - Czas wyświetlania OSD (ms)
   - Pozycję OSD na ekranie (X / Y)
   - Krycie OSD (0–100%)
   - Krok zmiany głośności na jedno naciśnięcie klawisza (%)
   - Kolory OSD (tło, tekst, pasek)
   - **Profile** — dodaj / edytuj / usuwaj profile audio, każdy z własnymi skrótami, opcjonalnymi modyfikatorami `Ctrl`/`Shift` i docelową aplikacją; pierwszy wiersz jest profilem domyślnym (używanym przez tray oraz przez bare metody D-Bus / MPRIS)

7. **Zdalne sterowanie przez D-Bus** — użyj `qdbus` lub `dbus-send` do kontrolowania aplikacji ze skryptów, własnych skrótów lub zewnętrznych narzędzi:

   ```bash
   # Zwiększ głośność aplikacji profilu domyślnego
   qdbus org.keyboardvolumeapp /org/keyboardvolumeapp org.keyboardvolumeapp.VolumeControl.VolumeUp

   # Zwiększ głośność wybranego profilu
   qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
       org.keyboardvolumeapp.VolumeControl.VolumeUpProfile firefox-ctrl

   # Wylistuj wszystkie profile
   qdbus org.keyboardvolumeapp /org/keyboardvolumeapp org.freedesktop.DBus.Properties.Get \
       org.keyboardvolumeapp.VolumeControl Profiles

   # Przełącz na Firefox
   qdbus org.keyboardvolumeapp /org/keyboardvolumeapp org.freedesktop.DBus.Properties.Set \
       org.keyboardvolumeapp.VolumeControl ActiveApp "Firefox"

   # Odczytaj aktualną głośność
   qdbus org.keyboardvolumeapp /org/keyboardvolumeapp org.freedesktop.DBus.Properties.Get \
       org.keyboardvolumeapp.VolumeControl Volume
   ```

> **Uwaga dot. przechwytywania klawiszy:** aplikacja blokuje aktualnie skonfigurowane klawisze na poziomie evdev, więc te właśnie klawisze nie są widoczne dla Qt podczas działania programu. Aby zmienić *aktywne* skróty, najpierw przypisz je do tymczasowych klawiszy (np. F9/F10/F11), zapisz i otwórz Ustawienia ponownie.

### Konfiguracja

Plik konfiguracyjny: `$XDG_CONFIG_HOME/keyboard-volume-app/config.json` (domyślnie `~/.config/keyboard-volume-app/`)

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
    "opacity": 85,
    "color_bg": "#1A1A1A",
    "color_text": "#FFFFFF",
    "color_bar": "#0078D7"
  },
  "volume_step": 5,
  "hotkeys": {
    "volume_up": 115,
    "volume_down": 114,
    "mute": 113
  },
  "profiles": [
    { "id": "default", "name": "Default", "app": "youtube-music",
      "modifiers": [],
      "hotkeys": { "volume_up": 115, "volume_down": 114, "mute": 113 } },
    { "id": "firefox-ctrl", "name": "Firefox (Ctrl)", "app": "firefox",
      "modifiers": ["ctrl"],
      "hotkeys": { "volume_up": 115, "volume_down": 114, "mute": 113 } }
  ]
}
```

Wartości skrótów to kody klawiszy evdev (`KEY_VOLUMEUP` = 115, `KEY_VOLUMEDOWN` = 114, `KEY_MUTE` = 113). Pola `selected_app` i top-level `hotkeys` są utrzymywane jako deprecated mirror `profiles[0]` przez jeden release backwards compat — `profiles` jest kanonicznym źródłem prawdy. Stare pliki konfiguracyjne bez `profiles` są migrowane automatycznie przy pierwszym uruchomieniu.

### Struktura projektu

```
keyboard-volume-app/
├── cpp/
│   ├── CMakeLists.txt
│   ├── resources.qrc            # Manifest zasobów Qt (osadza ikonę)
│   ├── src/
│       ├── main.cpp             # Punkt wejścia, łączy wszystkie moduły
│       ├── config.h/cpp         # Odczyt i zapis konfiguracji JSON
│       ├── i18n.h/cpp           # Tłumaczenia PL/EN i funkcja tr()
│       ├── volumecontroller.h/cpp  # libpulse — głośność i wyciszenie per aplikacja
│       ├── inputhandler.h/cpp   # evdev QThread — globalne przechwytywanie klawiszy (epoll)
│       ├── evdevdevice.h/cpp    # Opakowanie RAII dla urządzeń evdev
│       ├── osdwindow.h/cpp      # Nakładka OSD (Qt6)
│       ├── trayapp.h/cpp        # Ikona tray i menu
│       ├── deviceselector.h/cpp # Dialog wyboru urządzenia wejściowego
│       ├── settingsdialog.h/cpp # Dialog ustawień OSD, głośności i profili
│       ├── profileeditdialog.h/cpp # Sub-dialog edycji pojedynczego profilu audio
│       ├── firstrunwizard.h/cpp  # Asystent pierwszego uruchomienia
│       ├── dbusinterface.h/cpp   # Interfejs D-Bus VolumeControl
│       ├── mprisinterface.h/cpp  # Adaptor MPRIS v2
│       ├── pwutils.h/cpp         # Narzędzie do listowania klientów PipeWire
│       ├── applistwidget.h/cpp   # Reusable widget listy aplikacji PW
│       ├── appselectordialog.h/cpp  # Dialog zmiany domyślnej aplikacji audio
│       ├── screenutils.h         # Header-only centrowanie dialogów na właściwym monitorze
│       └── audioapp.h           # Struct AudioApp
│   └── tests/
│       ├── CMakeLists.txt
│       ├── test_config.cpp
│       ├── test_i18n.cpp
│       ├── test_inputhandler.cpp
│       ├── test_pwutils.cpp
│       └── test_volumecontroller.cpp
├── deploy/
│   └── keyboard-volume-app.service  # Usługa systemd user
├── pkg/
│   └── arch/
│       └── PKGBUILD             # Paczka Arch Linux (keyboard-volume-app-git)
├── resources/
│   ├── icon.png
│   └── keyboard-volume-app.desktop  # Wpis .desktop do dystrybucji
├── LICENSE
├── AGENTS.md
├── CLAUDE.md
├── GEMINI.md
└── ROADMAP.md
```

### Wydajność

Hot path zmiany głośności (naciśnięcie klawisza → aktualizacja OSD) wykonuje jedno wywołanie IPC przez libpulse (~1ms). Listowanie nieaktywnych aplikacji PipeWire i fallback dla wstrzymanych node'ów używają bezpośrednio libpipewire, więc aplikacja nie uruchamia subprocessów `pw-dump` ani `pw-cli`. Wszystkie operacje PulseAudio/PipeWire działają na osobnym wątku — pętla zdarzeń Qt nigdy nie jest blokowana. Jeśli kontekst PulseAudio zakończy się błędem lub zostanie zerwany, worker reconnectuje z backoffem i zachowuje pending volume/mute do czasu ponownego pojawienia się aplikacji. Przejściowe odświeżenia listy podczas restartu daemona audio nie zmieniają skonfigurowanej wybranej aplikacji. Odczyty właściwości D-Bus są serwowane z lokalnej pamięci podręcznej (zero IPC); zapisy delegowane są asynchronicznie do wątku PulseAudio.

### Licencja

GPL-2.0-or-later — patrz [LICENSE](LICENSE)
