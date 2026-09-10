[English](README.md)

# keyboard-volume-app

Natywne narzędzie dla Linuksa do sterowania głośnością pojedynczej aplikacji
za pomocą globalnych skrótów klawiatury lub kółka myszy. Zmienia głośność
wybranego programu zamiast głośności głównej systemu i pokazuje wynik na
konfigurowalnym OSD.

![C++](https://img.shields.io/badge/C%2B%2B-20-blue)
![Qt](https://img.shields.io/badge/Qt-6-green)
![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey)
![Audio](https://img.shields.io/badge/Audio-PipeWire%20%2F%20PulseAudio-orange)

## Najważniejsze możliwości

- Sterowanie jedną aplikacją audio naraz, z wyciszeniem, limitami głośności i wykrywaniem bezczynnych aplikacji.
- Wiele profili, skróty z modyfikatorami, powiązania kółka myszy i automatyczne przełączanie według aktywnego okna.
- Sceny zmieniające głośność, wyciszenie i urządzenie wyjściowe; opcjonalny ducking tymczasowo ścisza inne aplikacje audio.
- Resizowalne OSD z opcjonalnymi metadanymi MPRIS, postępem, seekowaniem, okładką i sterowaniem odtwarzaniem.
- Bezpośrednie przechwytywanie skonfigurowanych klawiszy przez evdev, niezależne od aktywnego okna.
- Integracja z PipeWire/PulseAudio, spójna głośność widoczna w mikserze i ponowne łączenie po restarcie demona audio.
- Ikona zasobnika, asystent pierwszego uruchomienia, interfejs PL/EN, `kv-ctl`, D-Bus i opcjonalny endpoint MPRIS.

## Instalacja i uruchomienie

### Arch Linux / AUR

```bash
yay -S keyboard-volume-app-git
```

Bez pomocnika AUR:

```bash
sudo pacman -S --needed base-devel git
git clone https://aur.archlinux.org/keyboard-volume-app-git.git
cd keyboard-volume-app-git
makepkg -si
```

### Pakiety lub źródła

CI publikuje artefakty `.deb` i `.rpm`. Pobierz pakiet dla swojej dystrybucji
z ostatniego udanego uruchomienia GitHub Actions i zainstaluj go menedżerem
pakietów. Pełne informacje są w [przewodniku instalacji](docs/pl/installation.md).

Aby zbudować bieżące źródła:

```bash
git clone https://github.com/Adiker/keyboard-volume-app.git
cd keyboard-volume-app
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build -j"$(nproc)"
```

### Pierwsze uruchomienie

1. Zapewnij użytkownikowi odczyt urządzeń `/dev/input/event*`:

   ```bash
   sudo usermod -aG input "$USER"
   ```

   Po zmianie grupy wyloguj się i zaloguj ponownie.
2. Uruchom `keyboard-volume-app` z menu aplikacji albo
   `cpp/build/keyboard-volume-app` przy instalacji ze źródeł.
3. W kreatorze wybierz język, urządzenie wejściowe i początkową aplikację audio.
4. Używaj skonfigurowanych klawiszy głośności/wyciszenia lub kółka myszy.
   Ikona zasobnika otwiera wybór aplikacji i Ustawienia.

## Dokumentacja

| Temat | English | Polski |
|---|---|---|
| Instalacja, zależności, Wayland i systemd | [Installation](docs/en/installation.md) | [Instalacja](docs/pl/installation.md) |
| Codzienna obsługa, profile, OSD, sceny i diagnozowanie | [User guide](docs/en/user-guide.md) | [Przewodnik użytkownika](docs/pl/user-guide.md) |
| `config.json`, formaty skrótów i migracje | [Configuration](docs/en/configuration.md) | [Konfiguracja](docs/pl/configuration.md) |
| `kv-ctl`, `qdbus`, D-Bus i MPRIS | [Remote control](docs/en/remote-control.md) | [Sterowanie zdalne](docs/pl/remote-control.md) |
| Architektura, testy i workflow utrzymania | [ARCHITECTURE.md](ARCHITECTURE.md) | — |

Przewodnik budowania wymienia pakiety Qt6, libevdev, PulseAudio, PipeWire,
TagLib, Wayland/XCB i CMake. Natywne pozycjonowanie OSD na Waylandzie jest
opcjonalne; na nieobsługiwanym compositorze używany jest fallback XWayland.

Przykładowe sterowanie skryptowe po uruchomieniu aplikacji:

```bash
kv-ctl up
kv-ctl get apps
kv-ctl set volume 35
```

Pełna lista poleceń i typowane przykłady D-Bus są w [przewodniku sterowania
zdalnego](docs/pl/remote-control.md). Przepisy testów i izolowaną regresję
PipeWire opisano w [ARCHITECTURE.md](ARCHITECTURE.md).

## Wartości domyślne i zgodność

Domyślne skróty to `KEY_VOLUMEUP` (115), `KEY_VOLUMEDOWN` (114) i `KEY_MUTE`
(113). Skróty multimedialne i położenia OSD są początkowo nieprzypisane. Plik
konfiguracji znajduje się w `$XDG_CONFIG_HOME/keyboard-volume-app/config.json`
albo w `~/.config/keyboard-volume-app/config.json`, gdy zmienna nie jest
ustawiona. Fałszywy endpoint MPRIS jest domyślnie wyłączony. Istniejąca
konfiguracja Pythona i archiwalna gałąź Python nie są zmieniane przez aplikację C++.

Interfejs zasobnika i Ustawienia można w każdej chwili przełączyć między
angielskim i polskim. Wszystkie zapisy konfiguracji zachowują poprzedni plik,
jeśli zapis się nie powiedzie. Szczegóły zależne od compositora opisano w
sekcji Wayland przewodnika instalacji.

## Status projektu

Główną wersją projektu jest implementacja C++/Qt6. Oryginalna implementacja
Python/PyQt6 jest zachowana w gałęzi [`python-legacy`](https://github.com/Adiker/keyboard-volume-app/tree/python-legacy)
i oznaczona tagiem [`python-last`](https://github.com/Adiker/keyboard-volume-app/releases/tag/python-last).

## Licencja

[GPL-2.0-or-later](LICENSE)
