[Polski](../pl/installation.md) · [Back to README](../../README.md)

# Installation

`keyboard-volume-app` is a Linux desktop application. KDE Plasma is the
primary target, with PipeWire/pipewire-pulse or native PulseAudio for audio
and evdev for global input capture.

## Choose an installation method

### Arch Linux / AUR

The `keyboard-volume-app-git` package follows the upstream `main` branch:

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

To build the package from this checkout instead:

```bash
cd pkg/arch
makepkg -si
```

The package installs `keyboard-volume-app`, `kv-ctl`, the desktop entry, icon
and systemd user unit under `/usr`.

### Debian and RPM packages

Release CI builds `.deb` and `.rpm` artifacts. Download the matching artifact
from the latest successful GitHub Actions run, then install it with the native
package manager:

```bash
sudo apt install ./keyboard-volume-app_0.1.0-1_amd64.deb
sudo dnf install ./keyboard-volume-app-0.1.0-1.x86_64.rpm
```

The exact filename contains the project version. Packages install the binary,
`kv-ctl`, desktop entry, icon and systemd user service. The user still needs
evdev access; follow the permissions section below.

### Build from source

Clone the repository and configure a Release build:

```bash
git clone https://github.com/Adiker/keyboard-volume-app.git
cd keyboard-volume-app
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build -j"$(nproc)"
```

The binaries are `cpp/build/keyboard-volume-app` and `cpp/build/kv-ctl`.

## Dependencies

| Dependency | Why it is needed |
|---|---|
| Qt6 Widgets and DBus | Tray, dialogs, OSD and D-Bus endpoints |
| libevdev and uinput access | Global keyboard/mouse capture and reinjection |
| libpulse | Fast per-application volume control |
| libpipewire | Idle-client listing and PipeWire node fallback |
| TagLib | Local audio-file duration when MPRIS metadata is stale |
| libxcb | X11/XWayland active-window detection |
| wayland-client and LayerShellQt >= 6.6 | Optional native Wayland OSD positioning |
| CMake >= 3.20 and C++20 compiler | Build system and application build |
| GTest | Tests when `BUILD_TESTING=ON` |

Arch / Manjaro build dependencies:

```bash
sudo pacman -S qt6-base libevdev libpulse libpipewire taglib libxcb \
  libwayland cmake gcc gtest
```

Ubuntu / Debian development dependencies:

```bash
sudo apt install qt6-base-dev libevdev-dev libpulse-dev \
  libpipewire-0.3-dev libtag1-dev libxcb-dev libwayland-dev \
  cmake g++ libgtest-dev
```

`LayerShellQt` is optional and is packaged separately on some distributions.
When its development files are unavailable, the build remains functional and
uses the XWayland positioning path on Wayland sessions.

## Input permissions

The app reads `/dev/input/event*` and may use uinput to keep mirrored keyboard
LED state synchronized. Add the user to the `input` group:

```bash
sudo usermod -aG input "$USER"
```

Log out and in again before starting the app. The first-run wizard filters
devices to keyboards that advertise volume keys such as `KEY_VOLUMEUP` and
`KEY_VOLUMEDOWN`.

## Start and autostart

Run a source build directly:

```bash
cpp/build/keyboard-volume-app
```

Installed packages also provide a desktop entry. On first launch, the wizard
selects language, input device and default audio app.

Packaged installs place the systemd user unit in `/usr/lib/systemd/user`:

```bash
systemctl --user daemon-reload
systemctl --user enable --now keyboard-volume-app.service
```

Disable it with:

```bash
systemctl --user disable --now keyboard-volume-app.service
```

For a manual per-user install, copy `deploy/keyboard-volume-app.service` to
`$HOME/.config/systemd/user/` and adjust `ExecStart` when the binary is not at
`/usr/bin/keyboard-volume-app`.

## Command-line flags

These flags print information and exit without starting the tray app:

```bash
keyboard-volume-app --help
keyboard-volume-app --version
kv-ctl --help
kv-ctl --version
```

## Wayland and XWayland

At startup the app checks `WAYLAND_DISPLAY`, `XDG_SESSION_TYPE` and
`QT_QPA_PLATFORM`. When built with `wayland-client` and LayerShellQt >= 6.6,
and when the compositor advertises `zwlr_layer_shell_v1`, the OSD uses native
Wayland layer-shell positioning. The rest of the application remains ordinary
Qt windows.

On GNOME or another compositor without the protocol, an unset
`QT_QPA_PLATFORM` allows the app to use the XWayland (`xcb`) fallback so OSD
positioning continues to work. Do not set
`QT_WAYLAND_SHELL_INTEGRATION=layer-shell` globally: that would affect dialogs
and other windows as well. OSD edge/corner resizing is implemented inside the
app and works on both paths.

Focused-window profile switching uses the native
`zwlr_foreign_toplevel_management_unstable_v1` protocol where available and
falls back to X11/XWayland via XCB.

## Testing and maintenance

The complete build, CTest, private-D-Bus MPRIS and isolated PipeWire recipes
are maintained in [ARCHITECTURE.md](../../ARCHITECTURE.md). The installation
guide intentionally keeps runtime setup separate from maintainer test details.
