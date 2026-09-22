[English](../en/installation.md) · [Powrót do README](../../README.pl.md)

# Instalacja

`keyboard-volume-app` to aplikacja desktopowa dla Linuksa. Głównym celem jest
KDE Plasma, z PipeWire/pipewire-pulse lub natywnym PulseAudio oraz evdev do
globalnego przechwytywania wejścia.

## Wybór sposobu instalacji

### Arch Linux / AUR

Pakiet `keyboard-volume-app-git` śledzi gałąź upstream `main`:

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

Aby zbudować pakiet bezpośrednio z tego checkoutu:

```bash
cd pkg/arch
makepkg -si
```

Pakiet instaluje `keyboard-volume-app`, `kv-ctl`, wpis desktopowy, ikonę oraz
user unit systemd w `/usr`.

### Pakiety Debian i RPM

CI buduje artefakty `.deb` i `.rpm`. Pobierz właściwy artefakt z ostatniego
udanego uruchomienia GitHub Actions i zainstaluj go menedżerem pakietów:

```bash
sudo apt install ./keyboard-volume-app_0.1.0-1_amd64.deb
sudo dnf install ./keyboard-volume-app-0.1.0-1.x86_64.rpm
```

Dokładna nazwa zawiera wersję projektu. Pakiety instalują binarkę, `kv-ctl`,
wpis desktopowy, ikonę i user unit systemd. Nadal trzeba skonfigurować dostęp
do evdev zgodnie z sekcją poniżej.

### Budowanie ze źródeł

```bash
git clone https://github.com/Adiker/keyboard-volume-app.git
cd keyboard-volume-app
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build -j"$(nproc)"
```

Powstają binarki `cpp/build/keyboard-volume-app` i `cpp/build/kv-ctl`.

## Zależności

| Zależność | Zastosowanie |
|---|---|
| Qt6 Widgets i DBus | Zasobnik, dialogi, OSD i endpointy D-Bus |
| libevdev i dostęp do uinput | Globalne skróty klawiatury/myszy i reiniekcja |
| libpulse | Szybkie sterowanie głośnością aplikacji |
| libpipewire | Lista bezczynnych klientów i fallback węzłów PipeWire |
| TagLib | Lokalny czas trwania pliku przy niepełnych metadanych MPRIS |
| libxcb | Wykrywanie aktywnego okna X11/XWayland |
| wayland-client i LayerShellQt >= 6.6 | Opcjonalne natywne pozycjonowanie OSD |
| CMake >= 3.20 i kompilator C++20 | System budowania i kompilacja |
| GTest | Testy przy `BUILD_TESTING=ON` |

Zależności deweloperskie Arch / Manjaro:

```bash
sudo pacman -S qt6-base libevdev libpulse libpipewire taglib libxcb \
  libwayland cmake gcc gtest
```

Zależności deweloperskie Ubuntu / Debian:

```bash
sudo apt install qt6-base-dev libevdev-dev libpulse-dev \
  libpipewire-0.3-dev libtag1-dev libxcb-dev libwayland-dev \
  cmake g++ libgtest-dev
```

`LayerShellQt` jest opcjonalny i na niektórych dystrybucjach ma osobny pakiet.
Bez jego plików deweloperskich build nadal działa, a Wayland używa ścieżki
pozycjonowania przez XWayland.

## Uprawnienia do wejścia

Aplikacja odczytuje `/dev/input/event*` i może używać uinput do synchronizacji
stanu diod klawiatury:

```bash
sudo usermod -aG input "$USER"
```

Po zmianie grupy wyloguj się i zaloguj ponownie. Kreator pierwszego uruchomienia
filtruje urządzenia do klawiatur zgłaszających np. `KEY_VOLUMEUP` i
`KEY_VOLUMEDOWN`.

## Uruchamianie i autostart

Uruchom build ze źródeł:

```bash
cpp/build/keyboard-volume-app
```

Pakiety instalują również wpis desktopowy. Przy pierwszym uruchomieniu kreator
wybiera język, urządzenie wejściowe i domyślną aplikację audio.

Pakiety umieszczają user unit systemd w `/usr/lib/systemd/user`:

```bash
systemctl --user daemon-reload
systemctl --user enable --now keyboard-volume-app.service
```

Wyłączenie:

```bash
systemctl --user disable --now keyboard-volume-app.service
```

Przy ręcznej instalacji per-user skopiuj `deploy/keyboard-volume-app.service`
do `$HOME/.config/systemd/user/` i popraw `ExecStart`, jeśli binarka nie jest
w `/usr/bin/keyboard-volume-app`.

## Flagi wiersza poleceń

Flagi wypisują informację i kończą program bez uruchamiania zasobnika:

```bash
keyboard-volume-app --help
keyboard-volume-app --version
kv-ctl --help
kv-ctl --version
```

## Wayland i XWayland

Przy starcie aplikacja sprawdza `WAYLAND_DISPLAY`, `XDG_SESSION_TYPE` i
`QT_QPA_PLATFORM`. Gdy build zawiera `wayland-client` i LayerShellQt >= 6.6,
a compositor udostępnia `zwlr_layer_shell_v1`, OSD używa natywnego
pozycjonowania layer-shell. Pozostałe okna nadal są zwykłymi oknami Qt.

Na GNOME i compositorach bez tego protokołu, przy nieustawionym
`QT_QPA_PLATFORM`, używany jest fallback XWayland (`xcb`). Nie ustawiaj
globalnie `QT_WAYLAND_SHELL_INTEGRATION=layer-shell`, bo zmieniłoby to także
dialogi i pozostałe okna. Zmiana rozmiaru OSD jest obsługiwana wewnątrz aplikacji
i działa na obu ścieżkach.

Przełączanie profili według aktywnego okna używa
`zwlr_foreign_toplevel_management_unstable_v1`, jeśli jest dostępne, a w
pozostałych przypadkach przechodzi przez X11/XWayland i XCB.

## Testy i utrzymanie

Pełne przepisy budowania, CTest, prywatnej sesji D-Bus dla MPRIS i izolowanej
regresji PipeWire znajdują się w [ARCHITECTURE.md](../../ARCHITECTURE.md).
Przewodnik instalacji nie miesza konfiguracji użytkownika ze szczegółami testów
utrzymaniowych.
