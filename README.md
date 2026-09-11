[Polski](README.pl.md)

# keyboard-volume-app

Linux per-application volume control for keyboard and mouse hotkeys. The app
changes the selected application's volume instead of the system master volume,
and shows the result in a configurable OSD overlay.

![C++](https://img.shields.io/badge/C%2B%2B-20-blue)
![Qt](https://img.shields.io/badge/Qt-6-green)
![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey)
![Audio](https://img.shields.io/badge/Audio-PipeWire%20%2F%20PulseAudio-orange)

## What it does

- Controls one audio application at a time, with mute, volume limits and idle-app detection.
- Supports multiple profiles, modifier-qualified hotkeys, mouse-wheel bindings and automatic switching by focused window.
- Applies scenes for volume, mute and output-sink changes; optional ducking lowers other known audio apps temporarily.
- Provides a resizable OSD with optional MPRIS track metadata, progress, seeking, album art and media controls.
- Captures configured keys directly through evdev, so hotkeys work independently of the focused window.
- Integrates with PipeWire/PulseAudio, keeps mixer-visible volume consistent, and reconnects after audio-daemon restarts.
- Includes a tray UI, first-run wizard, English/Polish translations, `kv-ctl`, D-Bus control and an opt-in MPRIS endpoint.

## Install and start

### Arch Linux / AUR

```bash
yay -S keyboard-volume-app-git
```

Without an AUR helper:

```bash
sudo pacman -S --needed base-devel git
git clone https://aur.archlinux.org/keyboard-volume-app-git.git
cd keyboard-volume-app-git
makepkg -si
```

### Packages or source

Release CI publishes `.deb` and `.rpm` artifacts. Download the package for
your distribution from the latest successful GitHub Actions run and install it
with the system package manager. Full package and dependency details are in
the [installation guide](docs/en/installation.md).

To build the current source tree:

```bash
git clone https://github.com/Adiker/keyboard-volume-app.git
cd keyboard-volume-app
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build -j"$(nproc)"
```

### First launch

1. Ensure the user can read `/dev/input/event*` devices:

   ```bash
   sudo usermod -aG input "$USER"
   ```

   Log out and in again after changing the group.
2. Start `keyboard-volume-app` from the application menu, or run
   `cpp/build/keyboard-volume-app` when using a source build.
3. Follow the wizard to choose the language, input device and initial audio app.
4. Use the configured volume/mute keys or wheel. The tray icon opens app
   selection and Settings.

## Documentation

| Topic | English | Polski |
|---|---|---|
| Installation, dependencies, Wayland and systemd | [Installation](docs/en/installation.md) | [Instalacja](docs/pl/installation.md) |
| Everyday use, profiles, OSD, scenes and troubleshooting | [User guide](docs/en/user-guide.md) | [Przewodnik użytkownika](docs/pl/user-guide.md) |
| `config.json`, hotkey formats and migrations | [Configuration](docs/en/configuration.md) | [Konfiguracja](docs/pl/configuration.md) |
| `kv-ctl`, `qdbus`, D-Bus and MPRIS | [Remote control](docs/en/remote-control.md) | [Sterowanie zdalne](docs/pl/remote-control.md) |
| Architecture, tests and maintainer workflows | [ARCHITECTURE.md](ARCHITECTURE.md) | — |

The source-build guide lists the required Qt6, libevdev, PulseAudio, PipeWire,
TagLib, Wayland/XCB and CMake packages. Native Wayland OSD positioning is
optional; unsupported compositors use the XWayland fallback.

For a quick script example, start the app and run:

```bash
kv-ctl up
kv-ctl get apps
kv-ctl set volume 35
```

See the [remote-control guide](docs/en/remote-control.md) for the complete
command list and typed D-Bus examples. Test recipes and the isolated PipeWire
regression are maintained in [ARCHITECTURE.md](ARCHITECTURE.md).

## Defaults and compatibility

The default bindings are `KEY_VOLUMEUP` (115), `KEY_VOLUMEDOWN` (114) and
`KEY_MUTE` (113). Media and OSD-position bindings start unassigned. The
configuration file is stored at `$XDG_CONFIG_HOME/keyboard-volume-app/config.json`
or `~/.config/keyboard-volume-app/config.json` when the variable is unset.
The optional fake MPRIS endpoint is disabled by default. Existing Python
configuration and the archived Python branch are not modified by the C++ app.

The tray and Settings UI can be switched between English and Polish at any
time. All configuration writes preserve the previous file when saving fails.
For compositor-specific behavior, see the Wayland section of the installation
guide.

## Project status

The C++/Qt6 implementation is the primary version. The original Python/PyQt6
implementation is preserved in the [`python-legacy`](https://github.com/Adiker/keyboard-volume-app/tree/python-legacy)
branch and the [`python-last`](https://github.com/Adiker/keyboard-volume-app/releases/tag/python-last)
tag.

## License

[GPL-2.0-or-later](LICENSE)
