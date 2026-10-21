# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] - 2026-10-21

First tagged C++ release on the `main` line after the rewrite. Install `.deb` /
`.rpm` assets from the GitHub Release, or use AUR `keyboard-volume-app-git`.

### Added

- Multi-app **profiles** with per-profile hotkeys, optional Ctrl/Shift modifiers,
  volume min/max limits, and Follow Focus (`auto_switch`)
- **Scenes** (mixer presets) with GUI editor, optional hotkeys, volume/mute and
  output-sink targets; `kv-ctl scene` / D-Bus `ApplyScene`
- **Audio ducking** (Focus Audio) per profile with hotkey, D-Bus and CLI
- **First-run wizard** (language, input device, default audio app)
- **Native Wayland OSD** via `zwlr_layer_shell_v1` / LayerShellQt when available
  (otherwise XWayland/`xcb` fallback)
- **Follow Focus on Wayland** via `zwlr_foreign_toplevel_management_unstable_v1`
- Resizable OSD with optional **MPRIS** track metadata, progress, seeking, album
  art and media controls; opt-in MPRIS endpoint exposure
- Global **media hotkeys** (play/pause, next, previous, stop) dispatching to the
  active MPRIS player
- Show-volume hotkey / `kv-ctl show` / D-Bus `ShowVolume` without changing level
- `kv-ctl` CLI client and session D-Bus `org.keyboardvolumeapp.VolumeControl`
- App aliases, configurable audio-app filters, PipeWire client identity mapping
- Config import/export in Settings; About dialog
- Arch AUR package `keyboard-volume-app-git`, CPack `.deb`/`.rpm`, systemd user unit
- CI (build, CTest, clang-format; clang-tidy warning-only) and tag-triggered
  GitHub Release publishing

### Changed

- Input path uses `epoll` (50 ms) with key-repeat handling and per-binding debounce
- Volume path: libpulse primary, stream-restore and libpipewire fallbacks, PA
  reconnect with backoff; pending volume/mute when the target app is idle
- Project version bumped from `0.1.0` to `0.2.0`

### Known limitations

- Modifier keys are observed only on devices the app already grabs (dual-keyboard
  setups: see ROADMAP #23)
- GNOME without XWayland and without `zwlr_layer_shell_v1`: OSD positioning is
  out of scope
- Follow Focus needs `zwlr_foreign_toplevel_management_unstable_v1` or X11/XWayland
- Ducking applies instantly (no fade yet)
- OSD still uses absolute X/Y (anchors / custom QSS planned)

## [0.1.0] - 2026-04-25

- Initial C++/Qt6 rewrite snapshot (`cpp-rewrite-v0.1.0`)
- Earlier `main-v0.1.0` tagged the archived Python application — do not use for
  current installs

[0.2.0]: https://github.com/Adiker/keyboard-volume-app/releases/tag/v0.2.0
[0.1.0]: https://github.com/Adiker/keyboard-volume-app/releases/tag/cpp-rewrite-v0.1.0
