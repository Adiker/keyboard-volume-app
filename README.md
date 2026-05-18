[English](#english) | [Polski](#polski)

---

> **Notice:** C++/Qt6 is now the primary version of this project. The original Python/PyQt6 implementation is preserved in the [`python-legacy`](https://github.com/Adiker/keyboard-volume-app/tree/python-legacy) branch and tagged as [`python-last`](https://github.com/Adiker/keyboard-volume-app/releases/tag/python-last).

<h2 id="english">🇬🇧 English</h2>

# keyboard-volume-app

A Linux-native alternative to AutoHotkey volume scripts for Windows. Controls the volume of a single chosen application via keyboard — without touching the system master volume. Pick an audio app from the tray icon, use the keyboard volume keys or wheel, and get an OSD overlay with the current level.

![C++](https://img.shields.io/badge/C%2B%2B-20-blue)
![Qt](https://img.shields.io/badge/Qt-6-green)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-red)
![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey)
![Desktop](https://img.shields.io/badge/Desktop-KDE%20Plasma-blue)
![Audio](https://img.shields.io/badge/Audio-PipeWire%20%2F%20PulseAudio-orange)

### Features

- **Per-app volume control** — changes the volume of only the selected application, not the system master
- **Multiple audio profiles** — define several profiles, each with its own hotkeys, optional `Ctrl`/`Shift` modifiers, and target audio app. Bare `VolUp` controls Spotify, `Ctrl+VolUp` controls Firefox, `F11` controls VLC — all from the same keyboard
- **Show volume hotkey** — each profile can bind an optional `show` hotkey that displays the OSD with the current volume of that profile's app without changing it; also available via `kv-ctl show [--profile id]` and D-Bus `ShowVolume()` / `ShowVolumeProfile(id)`
- **Focus audio / ducking** — each profile can bind a manual ducking hotkey that lowers every other known audio app to a configured percentage, then restores the previous levels on the next press
- **Auto-switch by window focus** — when enabled, the active (focused) window determines which profile's audio app receives volume keys; switch from Spotify to Firefox and volume keys follow automatically
- **Audio scenes / mixer presets** — define named presets in `config.json` that set volume and/or mute for several apps at once, then apply them from scripts with `kv-ctl scene ID`
- **Global key capture** — reads directly from an evdev input device, works regardless of which window is focused
- **Multi-node grab** — automatically grabs all sibling event nodes of the chosen keyboard (e.g. main keyboard + Consumer Control interface) plus any other device advertising volume keys from any profile, so the desktop never intercepts them
- **Configurable hotkeys** — every profile's Volume Up, Volume Down, Mute and Focus audio keys are reassignable via Settings → Profiles; right-click any hotkey field for an **Unassign** menu option to clear it; defaults are the dedicated media keys
- **OSD overlay** — frameless, always-on-top window showing app name, volume bar and percentage; can optionally expand with MPRIS playback progress, track label and elapsed/total time; click or drag the progress bar to seek when the player allows it; live streams show `LIVE`; auto-hides after a configurable timeout
- **System tray** — select the active audio app, refresh the list, change input device or open settings from the tray menu
- **Idle app detection** — lists non-system PipeWire audio clients, including apps that are connected but not currently playing
- **Friendly audio app names** — normalizes PipeWire/PulseAudio streams where the visible app and controllable stream differ, so wrappers such as Harmonoid can appear as the real app while still controlling the underlying stream
- **Audio backend recovery** — reconnects to PulseAudio/pipewire-pulse after daemon restarts while keeping the configured selected app
- **Mute toggle** — press the mute key to toggle mute on the selected app only; OSD shows current level with a 🔇 indicator
- **Persistent config** — all settings saved atomically to `$XDG_CONFIG_HOME/keyboard-volume-app/config.json` (defaults to `~/.config/keyboard-volume-app/`)
- **PL / EN interface** — switch language in Settings
- **First-run wizard** — on first launch, a QWizard guides through language, input device, and default audio app selection; the app is production-ready out of the box after a few clicks
- **D-Bus control** — full remote access via `org.keyboardvolumeapp.VolumeControl`: read/write volume, mute, active app, app list, volume step, **profiles**, **scenes**, and runtime `ProgressEnabled`; bare `VolumeUp/Down/ToggleMute/ToggleDucking/RefreshApps` methods, per-profile methods, plus `ApplyScene(id)`
- **`kv-ctl` CLI** — script-friendly command-line client for D-Bus control without calling the external `qdbus` program
- **MPRIS v2** — registered as `org.mpris.MediaPlayer2.keyboardvolumeapp` for desktop volume widgets, KDE Connect, and any MPRIS-compatible client
- **MPRIS playback tracking** — consumes other players' MPRIS metadata, position, seek support and player priority for the optional OSD playback progress features
- **Marquee labels** — app and track names that exceed the OSD width scroll automatically; short labels display statically
- **CLI flags** — `--help` and `--version` for quick help and version info without starting the app
- **Unit tests** — GTest + Qt Test suite covering Config, i18n, `kv-ctl` parsing, PipeWire utilities, VolumeController, InputHandler, and the MPRIS client

### Requirements

| Dependency | Purpose |
|---|---|
| Qt6 (Widgets, DBus) | System tray, OSD window, settings dialogs |
| libevdev + uinput access | Global keyboard input capture and re-injection |
| libpulse | Per-app volume control via PipeWire/PulseAudio socket |
| libpipewire | Listing and controlling idle PipeWire audio apps without subprocesses |
| TagLib | Reading local audio-file duration when a player reports stale MPRIS metadata |
| libxcb | X11 protocol for active-window detection (auto-switch feature) |
| wayland-client + LayerShellQt >= 6.6 | Optional native Wayland OSD positioning via `zwlr_layer_shell_v1`; Wayland focus tracking via `zwlr_foreign_toplevel_management_unstable_v1` |
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

This clones `main`, builds Release binaries, and installs everything to `/usr` including `keyboard-volume-app`, `kv-ctl`, the `.desktop` entry, icon, and systemd user service.

#### Build from source

```bash
git clone git@github.com:Adiker/keyboard-volume-app.git
cd keyboard-volume-app
```

**Install dependencies**

Arch / Manjaro:
```bash
sudo pacman -S qt6-base libevdev libpulse libxcb pipewire taglib wayland layer-shell-qt cmake gcc gtest
```

Ubuntu / Debian:
```bash
sudo apt install qt6-base-dev libevdev-dev libpulse-dev libpipewire-0.3-dev libtag1-dev libxcb-dev libwayland-dev cmake g++ libgtest-dev
```

Native Wayland OSD positioning is compiled in when `wayland-client` and `LayerShellQt >= 6.6` development files are available. On wlroots/KDE compositors that expose `zwlr_layer_shell_v1`, the OSD uses native Wayland layer-shell positioning. On GNOME or other compositors without that protocol, the app keeps the XWayland (`xcb`) fallback when `QT_QPA_PLATFORM` is unset. Auto-switch by focused window uses `zwlr_foreign_toplevel_management_unstable_v1` on wlroots-compatible Wayland compositors and falls back to X11/XWayland via XCB.

**Build**
```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build -j$(nproc)
```

This produces `cpp/build/keyboard-volume-app` and `cpp/build/kv-ctl`.

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
cpp/build/kv-ctl --help                  # Show CLI control commands
```

### Testing

```bash
cmake -S cpp -B cpp/build -DBUILD_TESTING=ON
cmake --build cpp/build
cd cpp/build && ctest -E test_mprisclient
cd cpp/build && dbus-run-session -- ctest -R test_mprisclient
```

Tests cover the Config manager, audio scenes, i18n translations, `kv-ctl` command parsing, PipeWire utilities, VolumeController (smoke test), InputHandler (API-only, no device required), and MPRIS client behavior. `test_mprisclient` should run under `dbus-run-session` so fake MPRIS players do not collide with the user's desktop session. Requires `gtest` / `libgtest-dev` package (see Requirements).

### Usage

1. **Select audio app** — click the tray icon → pick an app from the list. Apps currently playing audio are listed first; idle apps (connected to PipeWire but paused) appear below.
2. **Volume keys / wheel** — press the volume keys or scroll the wheel up/down to change the selected app's volume by the configured step.
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
   - **Playback progress** — enable the MPRIS progress row, allow/disable interactive seeking, choose poll interval, choose app/track/both label mode, and edit the comma-separated tracked-player priority list
   - **Profiles** — add / edit / remove audio profiles, each with its own hotkeys, optional `Ctrl`/`Shift` modifiers, target app, and optional Focus audio ducking hotkey; right-click any hotkey field to **Unassign** it; row 0 is the default profile (used by the tray and by bare D-Bus / MPRIS calls)

7. **CLI / D-Bus remote control** — use `kv-ctl` to drive the running tray app from scripts, custom keybinds, or external tools without calling the external `qdbus` program:

   ```bash
   # Bump volume on the default profile's app
   kv-ctl up

   # Bump volume on a specific profile
   kv-ctl up --profile firefox-ctrl

   # Toggle Focus audio ducking for the default profile
   kv-ctl duck

   # Toggle Focus audio ducking for a specific profile
   kv-ctl duck --profile discord

   # Show current volume on OSD without changing it
   kv-ctl show

   # Show current volume for a specific profile
   kv-ctl show --profile firefox-ctrl

   # List all profiles
   kv-ctl get profiles

   # List configured audio scenes and apply one
   kv-ctl get scenes
   kv-ctl scene meeting

   # Switch to Firefox
   kv-ctl set active-app Firefox

   # Read current volume
   kv-ctl get volume

   # Toggle OSD playback progress at runtime
   kv-ctl get progress-enabled
   kv-ctl set progress-enabled true
   ```

   `kv-ctl` still uses the app's existing session D-Bus API under the hood, so `keyboard-volume-app` must already be running.
   App names are case-sensitive; use `kv-ctl get apps` to list the exact names known by the daemon.

> **Hotkey capture note:** the app grabs its configured keys at the evdev level, so those exact keys won't be visible to Qt while the app is running. To reassign a *currently active* hotkey, right-click the hotkey field in Settings → Profiles, choose **Unassign**, save, reopen the profile, and capture the new key.

### Configuration

Config file: `$XDG_CONFIG_HOME/keyboard-volume-app/config.json` (defaults to `~/.config/keyboard-volume-app/`). Writes are atomic, so a failed save keeps the previous file intact.

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
    "color_bar": "#0078D7",
    "progress_enabled": false,
    "progress_interactive": true,
    "progress_poll_ms": 500,
    "progress_label_mode": "both",
    "tracked_players": ["spotify", "vlc", "strawberry", "harmonoid", "youtube"]
  },
  "volume_step": 5,
  "hotkeys": {
    "volume_up": 115,
    "volume_down": 114,
    "mute": 113
  },
  "auto_profile_switch": false,
  "profiles": [
    { "id": "default", "name": "Default", "app": "youtube-music",
      "modifiers": [],
      "hotkeys": { "volume_up": 115, "volume_down": 114, "mute": 113, "show": 0 },
      "ducking": { "enabled": false, "volume": 25, "hotkey": 0 },
      "auto_switch": true },
    { "id": "firefox-ctrl", "name": "Firefox (Ctrl)", "app": "firefox",
      "modifiers": ["ctrl"],
      "hotkeys": { "volume_up": 115, "volume_down": 114, "mute": 113, "show": 0 },
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

Hotkey values are evdev bindings: legacy integers are `EV_KEY` codes (`KEY_VOLUMEUP` = 115, `KEY_VOLUMEDOWN` = 114, `KEY_MUTE` = 113), while scroll bindings use objects such as `{ "type": "rel", "code": 8, "direction": 1 }`. `show` defaults to `0` (unassigned) and supports the same key/scroll binding formats. The top-level `selected_app` and `hotkeys` are kept as a deprecated mirror of `profiles[0]` for one release of backwards compatibility — `profiles` is the canonical source of truth. Old config files without `profiles` are migrated automatically on first launch. Scene target `match` values use the same app/binary names as `kv-ctl get apps`; `volume` is a `0..100` percent value, and omitted `volume` or `muted` fields leave that part unchanged.

`auto_profile_switch` (default `false`) globally enables auto-profile switching by focused window. Per-profile `auto_switch` (default `true`) controls whether a given profile participates in auto-switching.

OSD playback progress is configured under `osd`. `progress_enabled` is the master toggle, `progress_interactive` allows seek-capable players to be controlled from the progress bar, `progress_poll_ms` is clamped to `200..2000`, `progress_label_mode` is `app`, `track`, or `both`, and `tracked_players` is a priority list matched against MPRIS service names. When enabled and a tracked player is active, the OSD expands from the base volume view to a progress row with a track label, 0-1000 progress bar, and time label. Clicking or dragging the bar sends MPRIS `SetPosition` while the player reports `CanSeek` and a known length. Streams with unknown length disable the bar and show `LIVE`. Set `progress_interactive: false` to disable click/drag seek globally while keeping the visual progress row — useful if the player supports `CanSeek` but you prefer keyboard-only control.

For troubleshooting rare MPRIS progress glitches, start the app with `KVA_DEBUG_PROGRESS=1` to log progress metadata, position source, and OSD bar decisions.

### Project structure

```
keyboard-volume-app/
├── cpp/
│   ├── CMakeLists.txt
│   ├── resources.qrc            # Qt resource manifest (embeds icon)
│   ├── protocols/               # Custom Wayland protocol XML definitions
│   │   └── wlr-foreign-toplevel-management-unstable-v1.xml
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
│       ├── kvctl.cpp             # kv-ctl D-Bus CLI client
│       ├── kvctlcommand.h/cpp    # kv-ctl command parser
│       ├── pwutils.h/cpp         # PipeWire client listing utility
│       ├── applistwidget.h/cpp   # Reusable PW app list widget
│       ├── appselectordialog.h/cpp  # Dialog for changing default audio app
│       ├── screenutils.h         # Header-only multi-monitor dialog centering
│       ├── audioapp.h           # AudioApp struct
│       └── waylandstate.h       # Declares global extern bool g_nativeWayland
│   └── tests/
│       ├── CMakeLists.txt
│       ├── test_config.cpp
│       ├── test_i18n.cpp
│       ├── test_kvctlcommand.cpp
│       ├── test_inputhandler.cpp
│       ├── test_pwutils.cpp
│       ├── test_volumecontroller.cpp
│       ├── test_mprisclient.cpp
│       └── test_osdwindow.cpp
├── deploy/
│   └── keyboard-volume-app.service  # systemd user service
├── pkg/
│   └── arch/
│       └── PKGBUILD             # Arch Linux package (keyboard-volume-app-git)
├── resources/
│   ├── icon.png
│   └── keyboard-volume-app.desktop  # Desktop entry for distribution
├── .clang-format                # Code formatting configuration
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

Linuksowa alternatywa dla skryptów AutoHotkey sterujących głośnością na Windowsie. Zmienia głośność wybranej aplikacji za pomocą klawiatury — bez ingerowania w głośność systemową. Wybierz aplikację audio z ikony w zasobniku systemowym, użyj klawiszy lub pokrętła głośności na klawiaturze i obserwuj nakładkę OSD z aktualnym poziomem głośności.

![C++](https://img.shields.io/badge/C%2B%2B-20-blue)
![Qt](https://img.shields.io/badge/Qt-6-green)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-red)
![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey)
![Desktop](https://img.shields.io/badge/Desktop-KDE%20Plasma-blue)
![Audio](https://img.shields.io/badge/Audio-PipeWire%20%2F%20PulseAudio-orange)

### Funkcje

- **Sterowanie głośnością per aplikacja** — zmienia głośność wyłącznie wybranej aplikacji, nie ruszając głośności systemowej
- **Wiele profili audio** — definiuj kilka profili, każdy z własnymi skrótami, opcjonalnymi modyfikatorami `Ctrl`/`Shift` i docelową aplikacją. `VolUp` steruje Spotify, `Ctrl+VolUp` steruje Firefoxem, `F11` steruje VLC — wszystko z tej samej klawiatury
- **Hotkey „Pokaż głośność"** — każdy profil może mieć opcjonalny skrót `show`, który wyświetla OSD z aktualną głośnością aplikacji profilu bez jej zmieniania; dostępny też przez `kv-ctl show [--profile id]` i D-Bus `ShowVolume()` / `ShowVolumeProfile(id)`
- **Tryb skupienia audio / ducking** — każdy profil może mieć ręczny skrót, który ścisza wszystkie inne znane aplikacje audio do ustawionego procentu, a kolejne naciśnięcie przywraca poprzednie poziomy
- **Sceny audio / presety miksera** — definiuj nazwane presety w `config.json`, które ustawiają głośność i/lub wyciszenie kilku aplikacji naraz, a potem odpalaj je ze skryptów przez `kv-ctl scene ID`
- **Auto-przełączanie profilu wg aktywnego okna** — po włączeniu aktywne okno (np. Firefox, Spotify) automatycznie wybiera odpowiedni profil audio; klawisze głośności zawsze sterują aplikacją na wierzchu
- **Globalne przechwytywanie klawiszy** — odczytuje zdarzenia bezpośrednio z urządzenia evdev, działa niezależnie od tego, które okno jest aktywne
- **Przechwytywanie wielu węzłów** — automatycznie blokuje wszystkie powiązane węzły wejściowe wybranej klawiatury oraz każde inne urządzenie zgłaszające klawisze użyte w którymkolwiek profilu, aby system nie przechwytywał ich
- **Konfigurowalne skróty** — Głośność w górę, Głośność w dół, Wyciszenie i tryb skupienia każdego profilu można przypisać do dowolnego klawisza przez Ustawienia → Profile; prawy klik na polu hotkeya otwiera menu **Wyczyść**; domyślnie są to dedykowane klawisze multimedialne
- **Nakładka OSD** — bezramkowe okno wyświetlane zawsze na wierzchu, pokazujące nazwę aplikacji, pasek głośności i wartość procentową; opcjonalnie rozwija się o postęp MPRIS, etykietę utworu i czas odtwarzania; kliknięcie lub przeciągnięcie paska przewija odtwarzacz, jeśli ten na to pozwala; transmisje live pokazują `LIVE`; znika automatycznie po upływie skonfigurowanego czasu
- **Zasobnik systemowy** — wybór aktywnej aplikacji audio, odświeżanie listy, zmiana urządzenia wejściowego oraz dostęp do ustawień
- **Wykrywanie nieaktywnych aplikacji** — lista zawiera niesystemowe klienty audio PipeWire, także aplikacje podłączone, ale aktualnie nieodtwarzające dźwięku
- **Odzyskiwanie backendu audio** — ponownie łączy się z PulseAudio/pipewire-pulse po restarcie daemona i zachowuje skonfigurowaną wybraną aplikację
- **Wyciszenie** — naciśnij klawisz mute, aby wyciszyć lub odciszyć wyłącznie wybraną aplikację; OSD pokazuje aktualny poziom ze wskaźnikiem 🔇
- **Trwała konfiguracja** — wszystkie ustawienia zapisywane atomowo w `$XDG_CONFIG_HOME/keyboard-volume-app/config.json` (domyślnie `~/.config/keyboard-volume-app/`)
- **Interfejs PL / EN** — przełączanie języka w oknie ustawień
- **Asystent pierwszego uruchomienia** — przy pierwszym starcie QWizard przeprowadza przez wybór języka, urządzenia wejściowego i domyślnej aplikacji audio; aplikacja działa od razu po kilku kliknięciach
- **Sterowanie przez D-Bus** — pełne zdalne sterowanie przez `org.keyboardvolumeapp.VolumeControl`: odczyt/zapis głośności, wyciszenia, wybór aplikacji, lista aplikacji, krok głośności, **profile**, **sceny** oraz runtime `ProgressEnabled`; metody bez wskazania profilu, metody profilowe oraz `ApplyScene(id)`
- **CLI `kv-ctl`** — wygodny klient wiersza poleceń do sterowania przez D-Bus bez wywoływania zewnętrznego programu `qdbus`
- **MPRIS v2** — zarejestrowany jako `org.mpris.MediaPlayer2.keyboardvolumeapp` dla widżetów głośności pulpitu, KDE Connect i każdego klienta MPRIS
- **Śledzenie odtwarzania MPRIS** — odczytuje metadane, pozycję, możliwość seekowania i priorytet innych odtwarzaczy dla opcjonalnego paska postępu OSD
- **Etykiety marquee** — nazwy aplikacji i utworów przekraczające szerokość OSD przewijają się automatycznie; krótkie etykiety wyświetlają się statycznie
- **Flagi CLI** — `--help` i `--version` do szybkiego podglądu pomocy i wersji bez uruchamiania aplikacji
- **Testy jednostkowe** — GTest + Qt Test dla Config, i18n, parsera `kv-ctl`, narzędzi PipeWire, VolumeController, InputHandler i klienta MPRIS

### Wymagania

| Zależność | Przeznaczenie |
|---|---|
| Qt6 (Widgets, DBus) | Zasobnik systemowy, okno OSD, dialogi ustawień |
| libevdev + dostęp do uinput | Globalne przechwytywanie klawiszy i reinjekcja zdarzeń |
| libpulse | Sterowanie głośnością per aplikacja przez gniazdo PipeWire/PulseAudio |
| libpipewire | Listowanie i sterowanie nieaktywnymi aplikacjami PipeWire bez procesów pomocniczych |
| TagLib | Odczyt długości lokalnych plików audio, gdy odtwarzacz zgłasza nieaktualne metadane MPRIS |
| libxcb | Protokół X11 do wykrywania aktywnego okna (funkcja auto-przełączania) |
| wayland-client + LayerShellQt >= 6.6 | Opcjonalne natywne pozycjonowanie OSD na Waylandzie przez `zwlr_layer_shell_v1`; śledzenie fokusu przez `zwlr_foreign_toplevel_management_unstable_v1` |
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

Pobiera branch `main`, buduje binarki Release i instaluje wszystko do `/usr`, w tym `keyboard-volume-app`, `kv-ctl`, wpis `.desktop`, ikonę i usługę systemd user.

#### Budowanie ze źródeł

```bash
git clone git@github.com:Adiker/keyboard-volume-app.git
cd keyboard-volume-app
```

**Instalacja zależności**

Arch / Manjaro:
```bash
sudo pacman -S qt6-base libevdev libpulse libxcb pipewire taglib wayland layer-shell-qt cmake gcc gtest
```

Ubuntu / Debian:
```bash
sudo apt install qt6-base-dev libevdev-dev libpulse-dev libpipewire-0.3-dev libtag1-dev libxcb-dev libwayland-dev cmake g++ libgtest-dev
```

Natywne pozycjonowanie OSD na Waylandzie jest kompilowane, gdy dostępne są pliki deweloperskie `wayland-client` oraz `LayerShellQt >= 6.6`. Na kompozytorach wlroots/KDE z protokołem `zwlr_layer_shell_v1` OSD używa natywnego pozycjonowania layer-shell. Na GNOME lub innych kompozytorach bez tego protokołu aplikacja zachowuje fallback do XWayland (`xcb`), gdy `QT_QPA_PLATFORM` nie jest ustawione. Auto-przełączanie według aktywnego okna używa `zwlr_foreign_toplevel_management_unstable_v1` na zgodnych kompozytorach Wayland i fallbacku X11/XWayland przez XCB.

**Kompilacja**
```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build -j$(nproc)
```

Powstają binarki `cpp/build/keyboard-volume-app` oraz `cpp/build/kv-ctl`.

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
cpp/build/kv-ctl --help                  # Pokaż komendy sterowania CLI
```

### Testowanie

```bash
cmake -S cpp -B cpp/build -DBUILD_TESTING=ON
cmake --build cpp/build
cd cpp/build && ctest -E test_mprisclient
cd cpp/build && dbus-run-session -- ctest -R test_mprisclient
```

Testy obejmują Config, sceny audio, i18n, parser `kv-ctl`, narzędzia PipeWire, VolumeController (test dymny), InputHandler (API, bez potrzeby urządzenia) oraz klienta MPRIS. `test_mprisclient` uruchamiaj przez `dbus-run-session`, żeby fikcyjne odtwarzacze MPRIS nie mieszały się z sesją pulpitu użytkownika. Wymaga pakietu `gtest` / `libgtest-dev` (zobacz Wymagania).

### Użytkowanie

1. **Wybór aplikacji audio** — kliknij ikonę w zasobniku systemowym → wybierz aplikację z listy. Aplikacje aktualnie odtwarzające dźwięk są na górze; nieaktywne (podłączone do PipeWire, ale zapauzowane) pojawiają się poniżej.
2. **Klawisze / pokrętło głośności** — naciśnij lub przekręć w górę albo w dół, aby zmienić głośność wybranej aplikacji o skonfigurowany krok.
3. **Wyciszenie** — naciśnij klawisz mute, aby wyciszyć lub odciszyć wyłącznie wybraną aplikację; OSD pokazuje aktualny poziom ze wskaźnikiem 🔇.
4. **Odświeżenie listy** — menu zasobnika → *Odśwież listę aplikacji*, aby ponownie wczytać aktywne aplikacje audio.
5. **Zmiana urządzenia wejściowego** — menu zasobnika → *Zmień urządzenie wejściowe...*, aby wybrać inną klawiaturę bez restartu aplikacji.
6. **Ustawienia** — menu zasobnika → *Ustawienia...*, aby skonfigurować:
   - Język interfejsu (English / Polski)
   - Czas wyświetlania OSD (ms)
   - Pozycję OSD na ekranie (X / Y)
   - Krycie OSD (0–100%)
   - Krok zmiany głośności na jedno naciśnięcie klawisza (%)
   - Kolory OSD (tło, tekst, pasek)
   - **Postęp odtwarzania** — włączenie wiersza MPRIS, włączenie/wyłączenie interaktywnego seekowania, interwał odpytywania, tryb etykiety app/track/both oraz rozdzielona przecinkami lista priorytetów odtwarzaczy
   - **Profile** — dodaj / edytuj / usuwaj profile audio, każdy z własnymi skrótami, opcjonalnymi modyfikatorami `Ctrl`/`Shift`, docelową aplikacją i opcjonalnym skrótem trybu skupienia; prawy klik na polu hotkeya = **Wyczyść**; pierwszy wiersz jest profilem domyślnym (używanym przez zasobnik oraz przez metody D-Bus / MPRIS bez wskazania profilu)

7. **Zdalne sterowanie CLI / D-Bus** — użyj `kv-ctl` do kontrolowania działającej aplikacji ze skryptów, własnych skrótów lub zewnętrznych narzędzi bez uruchamiania zewnętrznego programu `qdbus`:

   ```bash
   # Zwiększ głośność aplikacji profilu domyślnego
   kv-ctl up

   # Zwiększ głośność wybranego profilu
   kv-ctl up --profile firefox-ctrl

   # Włącz lub wyłącz ducking profilu domyślnego
   kv-ctl duck

   # Włącz lub wyłącz ducking wybranego profilu
   kv-ctl duck --profile discord

   # Pokaż aktualną głośność na OSD bez zmieniania wartości
   kv-ctl show

   # Pokaż aktualną głośność wybranego profilu
   kv-ctl show --profile firefox-ctrl

   # Wylistuj wszystkie profile
   kv-ctl get profiles

   # Wylistuj sceny audio i zastosuj jedną z nich
   kv-ctl get scenes
   kv-ctl scene meeting

   # Przełącz na Firefox
   kv-ctl set active-app Firefox

   # Odczytaj aktualną głośność
   kv-ctl get volume

   # Przełącz postęp odtwarzania OSD w trakcie działania aplikacji
   kv-ctl get progress-enabled
   kv-ctl set progress-enabled true
   ```

   `kv-ctl` nadal używa istniejącego API D-Bus aplikacji, więc `keyboard-volume-app` musi już działać.
   Nazwy aplikacji rozróżniają wielkość liter; użyj `kv-ctl get apps`, żeby sprawdzić dokładne nazwy znane daemonowi.

> **Uwaga dot. przechwytywania klawiszy:** aplikacja blokuje aktualnie skonfigurowane klawisze na poziomie evdev, więc te właśnie klawisze nie są widoczne dla Qt podczas działania programu. Aby zmienić *aktywny* skrót, kliknij prawym przyciskiem pole hotkeya w Ustawienia → Profile, wybierz **Wyczyść**, zapisz, otwórz profil ponownie i przechwyć nowy klawisz.

### Konfiguracja

Plik konfiguracyjny: `$XDG_CONFIG_HOME/keyboard-volume-app/config.json` (domyślnie `~/.config/keyboard-volume-app/`). Zapisy są atomowe, więc nieudany zapis zostawia poprzedni plik bez zmian.

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
    "color_bar": "#0078D7",
    "progress_enabled": false,
    "progress_interactive": true,
    "progress_poll_ms": 500,
    "progress_label_mode": "both",
    "tracked_players": ["spotify", "vlc", "strawberry", "harmonoid", "youtube"]
  },
  "volume_step": 5,
  "hotkeys": {
    "volume_up": 115,
    "volume_down": 114,
    "mute": 113
  },
  "auto_profile_switch": false,
  "profiles": [
    { "id": "default", "name": "Default", "app": "youtube-music",
      "modifiers": [],
      "hotkeys": { "volume_up": 115, "volume_down": 114, "mute": 113, "show": 0 },
      "ducking": { "enabled": false, "volume": 25, "hotkey": 0 },
      "auto_switch": true },
    { "id": "firefox-ctrl", "name": "Firefox (Ctrl)", "app": "firefox",
      "modifiers": ["ctrl"],
      "hotkeys": { "volume_up": 115, "volume_down": 114, "mute": 113, "show": 0 },
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

Wartości skrótów to bindingi evdev: starsze liczby oznaczają kody `EV_KEY` (`KEY_VOLUMEUP` = 115, `KEY_VOLUMEDOWN` = 114, `KEY_MUTE` = 113), a scroll używa obiektów takich jak `{ "type": "rel", "code": 8, "direction": 1 }`. `show` domyślnie ma `0` (nieprzypisany) i obsługuje te same formaty klawiszy oraz scrolla. Pola `selected_app` i `hotkeys` na najwyższym poziomie są utrzymywane jako przestarzałe odbicie `profiles[0]` przez jedno wydanie w celu zachowania zgodności wstecznej — `profiles` jest kanonicznym źródłem prawdy. Stare pliki konfiguracyjne bez `profiles` są migrowane automatycznie przy pierwszym uruchomieniu. `match` w targetach scen używa tych samych nazw aplikacji/binarek co `kv-ctl get apps`; `volume` to procent `0..100`, a pominięte pola `volume` lub `muted` pozostawiają daną część stanu bez zmian.

`auto_profile_switch` (domyślnie `false`) globalnie włącza auto-przełączanie profilu wg aktywnego okna. Per-profilowe `auto_switch` (domyślnie `true`) kontroluje, czy dany profil bierze udział w auto-przełączaniu.

Postęp odtwarzania OSD jest konfigurowany w sekcji `osd`. `progress_enabled` jest głównym przełącznikiem, `progress_interactive` pozwala sterować seekowalnymi odtwarzaczami z paska postępu, `progress_poll_ms` jest ograniczane do `200..2000`, `progress_label_mode` przyjmuje `app`, `track` albo `both`, a `tracked_players` to lista priorytetów dopasowywana do nazw usług MPRIS. Gdy opcja jest włączona i działa śledzony odtwarzacz, OSD powiększa widok głośności o wiersz postępu z etykietą utworu, paskiem 0-1000 i czasem. Kliknięcie lub przeciągnięcie paska wysyła MPRIS `SetPosition`, jeśli odtwarzacz zgłasza `CanSeek` i znaną długość. Strumienie bez znanej długości wyłączają pasek i pokazują `LIVE`. Ustaw `progress_interactive: false`, aby wyłączyć seekowanie kliknięciem/przeciągnięciem globalnie, zachowując wizualny pasek postępu — przydatne gdy wolisz sterować wyłącznie z klawiatury.

Do diagnozowania rzadkich problemów z postępem MPRIS uruchom aplikację z `KVA_DEBUG_PROGRESS=1`, żeby logować metadane postępu, źródło pozycji i decyzje paska OSD.

### Struktura projektu

```
keyboard-volume-app/
├── cpp/
│   ├── CMakeLists.txt
│   ├── resources.qrc            # Manifest zasobów Qt (osadza ikonę)
│   ├── protocols/               # XML definicje protokołów Wayland
│   │   └── wlr-foreign-toplevel-management-unstable-v1.xml
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
│       ├── kvctl.cpp             # Klient CLI D-Bus kv-ctl
│       ├── kvctlcommand.h/cpp    # Parser komend kv-ctl
│       ├── pwutils.h/cpp         # Narzędzie do listowania klientów PipeWire
│       ├── applistwidget.h/cpp   # Reusable widget listy aplikacji PW
│       ├── appselectordialog.h/cpp  # Dialog zmiany domyślnej aplikacji audio
│       ├── windowtracker.h/cpp    # Monitor aktywnego okna X11 dla auto-przełączania profili
│       ├── screenutils.h         # Header-only centrowanie dialogów na właściwym monitorze
│       ├── audioapp.h           # Struct AudioApp
│       └── waylandstate.h       # Deklaracja globalnej zmiennej g_nativeWayland
│   └── tests/
│       ├── CMakeLists.txt
│       ├── test_config.cpp
│       ├── test_i18n.cpp
│       ├── test_kvctlcommand.cpp
│       ├── test_inputhandler.cpp
│       ├── test_pwutils.cpp
│       ├── test_volumecontroller.cpp
│       ├── test_mprisclient.cpp
│       └── test_osdwindow.cpp
├── deploy/
│   └── keyboard-volume-app.service  # Usługa systemd user
├── pkg/
│   └── arch/
│       └── PKGBUILD             # Paczka Arch Linux (keyboard-volume-app-git)
├── resources/
│   ├── icon.png
│   └── keyboard-volume-app.desktop  # Wpis .desktop do dystrybucji
├── .clang-format                # Konfiguracja formatowania kodu
├── LICENSE
├── AGENTS.md
├── CLAUDE.md
├── GEMINI.md
└── ROADMAP.md
```

### Wydajność

Ścieżka krytyczna zmiany głośności (naciśnięcie klawisza → aktualizacja OSD) wykonuje jedno wywołanie IPC przez libpulse (~1ms). Listowanie nieaktywnych aplikacji PipeWire i mechanizm zapasowy dla wstrzymanych węzłów używają bezpośrednio libpipewire, więc aplikacja nie uruchamia procesów pomocniczych `pw-dump` ani `pw-cli`. Wszystkie operacje PulseAudio/PipeWire działają na osobnym wątku — pętla zdarzeń Qt nigdy nie jest blokowana. Jeśli kontekst PulseAudio zakończy się błędem lub zostanie zerwany, wątek roboczy ponawia połączenie z narastającym opóźnieniem i zachowuje oczekujące zmiany głośności/wyciszenia do czasu ponownego pojawienia się aplikacji. Przejściowe odświeżenia listy podczas restartu daemona audio nie zmieniają skonfigurowanej wybranej aplikacji. Odczyty właściwości D-Bus są obsługiwane z lokalnej pamięci podręcznej (zero IPC); zapisy delegowane są asynchronicznie do wątku PulseAudio.

### Licencja

GPL-2.0-or-later — patrz [LICENSE](LICENSE)
