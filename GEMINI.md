# GEMINI.md — keyboard-volume-app

This is the concise working guide for Gemini / Antigravity agents in this repository. It summarizes the rules that matter during implementation; the full project reference lives in `CLAUDE.md`, and sharp agent-specific constraints live in `AGENTS.md`.

## Project Overview

- **What:** Linux desktop utility that intercepts keyboard volume/mute keys (and configurable mouse-wheel scroll) through evdev and applies them to the audio application of the active profile, instead of the system master volume.
- **Stack:** C++20, Qt6 Widgets, Qt6 DBus, CMake 3.20+.
- **Audio:** PulseAudio-compatible IPC via libpulse, with native PipeWire support through libpipewire.
- **Input:** libevdev + libuinput.
- **Platform:** Linux only. KDE Plasma is the primary target. The app picks Wayland-vs-XWayland at startup based on protocol availability — see *Environment — Wayland* below.

## Non-Negotiable Rules

- **Do not commit directly to `main`** unless the user explicitly asks. Create a branch from the latest `origin/main`, use `feature/`, `fix/`, `refactor/`, `docs/`, or `chore/`, push it, and open a PR to `main`. Never push `claude/*` or other agent-generated branch names to `origin`.
- Never force-push to `main`, delete branches, or rewrite published history without explicit consent. GitHub auto-deletes head branches after merge; do not rely on stale remote branches as long-lived workspaces. Archive branches (`python-legacy`, `cpp-rewrite`) and tags (`python-last`, `main-v0.1.0`, `cpp-rewrite-v0.1.0`) are excluded from cleanup. Audit local stale branches via `.github/scripts/audit-branches.sh` (uses `git cherry` patch equivalence — catches squash-merged PRs).
- Before opening a PR, run the relevant build/tests. For evdev, libpulse, D-Bus, MPRIS, threading, CMake, or config migration changes, include a short risk/rollback note in the PR description.
- Config hotkeys and internal key handling use **Linux evdev bindings**, not Qt key codes. Legacy integer values mean `EV_KEY` codes (e.g. `KEY_VOLUMEUP` = 115). Scroll bindings are stored as `{ "type": "rel", "code": 8, "direction": 1 }` for `EV_REL / REL_WHEEL`. X11 native scan code conversion is `evdev = X11_keycode - 8`.
- `InputHandler` is a direct `QThread` subclass. It runs `epoll()` in `run()` with a 50ms timeout and handles both key press (`ev.value == 1`) and repeat (`ev.value == 2`) with 100ms debounce per `(binding, profileId)`.
- `PaWorker` uses `moveToThread()`. All libpulse calls must go through `QMetaObject::invokeMethod` targeting the PaWorker thread. Never call libpulse from the main thread.
- Manual Focus Audio / ducking is per profile. `InputHandler` emits `ducking_toggle(profileId)` from the configured evdev hotkey, and `VolumeController::toggleDucking()` snapshots/restores other apps on the PA worker thread.
- PipeWire idle-app lookup and node fallback use libpipewire. Keep those calls off the main Qt thread.
- Use `std::atomic<bool>` for thread-shared flags such as `m_running` and `m_stopping`; do not use `volatile bool`.
- `EvdevDevice` is the RAII, move-only wrapper for fd, `libevdev*`, grab/ungrab, and `libevdev_uinput*`. Reuse it instead of duplicating evdev cleanup logic.
- The OSD is a translucent top-level window. Draw its background manually in `OSDWindow::paintEvent()` with `QPainter::drawRoundedRect()`; stylesheet backgrounds will not render there.
- After showing the OSD on X11/XWayland, also set position through `QWindow::setPosition()` on `windowHandle()`. The native Wayland path updates layer-shell margins instead of calling `move()`.
- In Qt6, `QDBusConnection::sessionBus()` returns by value. Use `auto bus = QDBusConnection::sessionBus();`, not `auto &bus = ...`.
- MPRIS adaptors require `QDBusConnection::ExportAdaptors` when registering the object.
- **MPRIS registration is opt-in.** It is gated on `OsdConfig::exposeMpris` (default `false`). The adaptors are always created, but the D-Bus object/service are only registered when the option is true. `App::registerMprisEndpoint()` / `App::unregisterMprisEndpoint()` toggle at runtime via `TrayApp::settingsChanged`. Do not register the MPRIS service unconditionally — it would be misdetected by `discord-music-presence` and similar tools.
- The tray icon is embedded through `cpp/resources.qrc` as `:/icon.png`. Do not add post-build copy steps for the icon.
- Profiles are the canonical source of truth for hotkey → app mapping. One profile can target multiple apps (`AudioApp[]`); the legacy single-string `selected_app` and top-level `hotkeys` are kept as a deprecated mirror of `profiles[0]`.

## Environment — Wayland

The app reads `WAYLAND_DISPLAY` / `XDG_SESSION_TYPE` at startup. On Wayland, regular `QWidget::move()` is ignored by compositors, so OSD positioning takes one of two guarded paths:

- If built with `wayland-client` + `LayerShellQt >= 6.6` and the compositor advertises `zwlr_layer_shell_v1`, keep Qt on native Wayland and position only the OSD through `LayerShellQt::Window::get()`. `g_nativeWayland` (declared in `waylandstate.h`) is set before `QApplication`.
- Otherwise, when `QT_QPA_PLATFORM` is unset, force `xcb` (XWayland) so the existing `move()` / `QWindow::setPosition()` fallback works.

Do not remove the startup decision logic from `main.cpp`, and do not set `QT_WAYLAND_SHELL_INTEGRATION=layer-shell` globally — that would turn dialogs and other windows into layer-shell surfaces too.

`WindowTracker` selects its backend at runtime: `zwlr_foreign_toplevel_management_unstable_v1` when `WAYLAND_DISPLAY` is set and the registry exposes it, a KWin-script focus backend when running under KWin, otherwise the XCB `_NET_ACTIVE_WINDOW` poll. All paths emit the same `focusedBinaryChanged(QString)` signal.

## Architecture Map

- `cpp/src/main.cpp`: entry point and `App` coordinator. Two-phase init: constructor creates only `Config`; `init()` creates UI, input, audio, and D-Bus/MPRIS integration after the optional first-run wizard. Holds the Wayland/XWayland decision logic.
- `cpp/src/config.h/cpp`: thread-safe JSON config via `QStandardPaths::ConfigLocation`. Loads with deep-merge defaults. Setters save automatically via atomic `QSaveFile` commits (`commit()`-only, no direct-write fallback).
- `cpp/src/i18n.h/cpp`: PL/EN translation tables; `tr(key)` lookup with English fallback.
- `cpp/src/inputhandler.h/cpp`: evdev hotkey capture, exclusive device grabbing, uinput mirroring, per-profile dispatch (`volume_up`, `volume_down`, `mute`, `show`, `ducking_toggle`), modifier tracking on grabbed devices, and `KeyCaptureThread` for hotkey rebinding. Exposes `getVolumeDevices()`.
- `cpp/src/volumecontroller.h/cpp`: async per-app volume/mute/ducking controller. Fast path is libpulse active sink-input → stream-restore DB → libpipewire node fallback → pending state in `PaWorker`. Reconnects PA context with backoff (500 ms → 30 s) on daemon/context loss; `PaWatcherThread` rebuilds the sink-input subscription on reconnect. Exposes absolute setters `setVolume(app, target, vol_min, vol_max)` and `setMuted(app, bool)` for D-Bus/scenes; relative `change()`/`toggleMute()` are still used by the hot path.
- `cpp/src/pwutils.h/cpp`: shared libpipewire helpers. Maps `Stream/Output/Audio` nodes to owners by `client.id`, normalizes display names (Harmonoid → mpv stream, YouTube Music ↔ Chromium / `media.name`), and exposes `PipeWireClient`, `PipeWireNode`, and filter constants (`SYSTEM_BINARIES`, `SKIP_APP_NAMES`).
- `cpp/src/appmatcher.h`: `matchBinaryToApp()` resolves a focused-window binary name against a profile's `AudioApp[]` (multi-app profiles, PR #59). Skips empty fields so a half-filled `AudioApp` does not false-match every window.
- `cpp/src/applistwidget.h/cpp`: reusable `QWidget` (list + Refresh). Shared by `AppPage` (first-run wizard) and `AppSelectorDialog` (tray).
- `cpp/src/appselectordialog.h/cpp`: modal `QDialog` for changing the default audio app. Embeds `AppListWidget`. Opened from tray.
- `cpp/src/osdwindow.h/cpp`: frameless always-on-top OSD. Custom translucent painting in `paintEvent()`, optional MPRIS progress row + media-control buttons, `setPosition()` on XWayland, layer-shell margins on native Wayland, `osd_scale` config (0.5–3.0×).
- `cpp/src/trayapp.h/cpp`: tray icon and context menu. Radio list of audio apps, *Change default application…*, **Apply scene** submenu (#56), Refresh, Change device, Settings, Quit. Rebuilds never replace the configured selected app just because it temporarily disappeared during a refresh.
- `cpp/src/deviceselector.h/cpp`: dialog for picking an evdev input device with volume keys.
- `cpp/src/settingsdialog.h/cpp`: language, OSD position/colors/timeout/opacity/scale, volume step, **Playback progress** section (`progress_enabled`, `progress_interactive`, `progress_poll_ms`, `progress_label_mode`, `tracked_players`, `expose_mpris`, `media_controls_enabled`), **Profiles** section, *Auto-switch profile by focused window* checkbox.
- `cpp/src/profileeditdialog.h/cpp`: Name, multi-app target list, Ctrl/Shift modifiers, hotkey captures (`volume_up`, `volume_down`, `mute`, `show`), ducking (enable + other-apps volume + Focus Audio hotkey), per-profile **Volume limits** (`vol_min`, `vol_max`, cross-clamped), `auto_switch` checkbox.
- `cpp/src/firstrunwizard.h/cpp`: `WelcomePage` (language) → `DevicePage` (evdev) → `AppPage` (default app via `AppListWidget`).
- `cpp/src/dbusinterface.h/cpp`: `org.keyboardvolumeapp.VolumeControl`. Properties `Volume`, `Muted`, `ActiveApp`, `Apps`, `VolumeStep`, `Profiles`, `Scenes`, `ProgressEnabled`, `AutoProfileSwitch`. Methods `VolumeUp/Down`, `ToggleMute`, `SetMute(bool)`, `ToggleDucking`, `RefreshApps`, `*Profile(id)` variants, `SetMuteProfile(id, bool)`, `SetVolumeProfile(id, vol)`, `ApplyScene(id)`, `ShowVolume`/`ShowVolumeProfile`. The `Volume` and `Muted` writers route to absolute setters on `VolumeController`, never relative delta/toggle.
- `cpp/src/mprisinterface.h/cpp`: MPRIS endpoint `org.mpris.MediaPlayer2.keyboardvolumeapp` (opt-in, see Non-Negotiable Rules).
- `cpp/src/mprisclient.h/cpp`: MPRIS *consumer* — discovers active players (Spotify, VLC, Strawberry, Harmonoid, YouTube Music, etc.), fetches metadata and position, supports seek and play/pause/next/previous on capable players. Drives the OSD progress row.
- `cpp/src/windowtracker.h/cpp`: focus monitor with three runtime backends (KWin script, Wayland foreign-toplevel, X11/XCB). Emits `focusedBinaryChanged(QString)` consumed by `App::onFocusedBinaryChanged()`.
- `cpp/src/kvctl.cpp`, `cpp/src/kvctlcommand.h/cpp`: `kv-ctl` CLI client. Subcommands `up|down|mute [--profile id]`, `mute on|off [--profile id]`, `duck [--profile id]`, `show [--profile id]`, `scene ID`, `refresh`, `get|set` for `volume|muted|active-app|step|profiles|scenes|progress-enabled|auto-profile-switch`. `set volume X --profile id` routes via `SetVolumeProfile` rather than the daemon-wide property.
- `cpp/src/evdevdevice.h/cpp`: shared RAII wrapper for evdev/uinput resources.
- `cpp/src/screenutils.h`: header-only `centerDialogOnScreenAt(QWidget*, QPoint)` — used by all parentless dialogs.
- `cpp/src/waylandstate.h`: declares the `g_nativeWayland` flag set before `QApplication`.

## Development Workflow

Build:

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build -j$(nproc)
```

Run:

```bash
cpp/build/keyboard-volume-app
```

The user must be in the `input` group for evdev access to `/dev/input/event*` devices.

Common edits:

- **Adding a translation key:** add the English string to `_en` in `cpp/src/i18n.cpp`, add the Polish string to `_pl`, then call `tr("your.key")` from UI code.
- **Adding a config field:** add the default to the merge logic in `Config::load()`, add a typed getter/setter pair in `Config`, and rely on the setter's automatic save.
- **Changing D-Bus behavior:** keep property reads served from cached main-thread state and delegate setters/methods asynchronously to `VolumeController` or config. `Profiles` exposes nested `hotkeys` (with `volume_up`/`volume_down`/`mute`/`show`) and `ducking` maps. Property writers for `Volume` and `Muted` must call absolute setters, not relative delta/toggle.
- **Touching hotkey display:** `HotkeyCapture::keyDisplayName()` formats `EV_KEY` bindings as `"Volume Up (115)"` (friendly map → libevdev `KEY_*` symbol with the `KEY_` prefix stripped → `"Key (N)"`). `EV_REL` shows `"Wheel Up"` etc. Unassigned shows `"-"`. Strip the prefix only when the symbol actually starts with `KEY_`.

## Risk Areas

- **evdev/libuinput:** exclusive grabs must not swallow normal keyboard input. Non-hotkey events should still be mirrored through uinput. If `libevdev_uinput_create_from_device()` fails, immediately ungrab and skip the device — never leave a grabbed device without a working mirror.
- **Threading:** main-thread blocking freezes tray/UI/D-Bus dispatch. Audio operations belong on PaWorker; evdev polling belongs in `InputHandler`; window-focus polling belongs in `WindowTracker`'s thread.
- **Volume fallback:** keep the hot path fast. libpulse remains primary; libpipewire is only for idle-app listing and PipeWire node fallback. Per-profile `vol_min`/`vol_max` clamping must happen in `PaWorker` for all four code paths (active sink-input, stream-restore, PipeWire node, parked-pending).
- **PA reconnect/cleanup:** context loss is expected; keep reconnect/backoff and cleanup on the PA worker side, preserve pending volume/mute, and do not clear `selected_app` during transient app-list refreshes.
- **Wayland/OSD:** removing the XWayland/Wayland decision logic, the layer-shell `initLayerShell()` call, or `QWindow::setPosition()` on XWayland can make the OSD land at `(0,0)` or get clipped on multi-monitor.
- **Config migration:** preserve deep-merge behavior so old config files receive new defaults, including the multi-app profile field (`apps` array vs single `app` string), `vol_min`/`vol_max`, `hotkeys.show`, `auto_switch`, `expose_mpris`.
- **D-Bus/MPRIS:** unregister both objects and services during cleanup; keep `Qt6::DBus` in CMake when touching these modules. Toggling `expose_mpris` at runtime must register/unregister without restarting the app — `App::onMprisExposureChanged()` is the entry point.
- **Auto-profile-switch:** `Config::autoProfileSwitch()` controls whether `WindowTracker` is started. The D-Bus property `AutoProfileSwitch` and `kv-ctl set auto-profile-switch true|false` go through the same `App::onAutoSwitchMaybeChanged()` slot so the change takes effect without a restart.

## Testing

Unit tests are in `cpp/tests/` and run through CTest:

- `test_config` — config merge, load/save, atomic save failure, thread-safety, profile migration / round-trip / mirror / ducking / scroll hotkeys / show hotkey / id uniqueification / per-profile `vol_min` and `vol_max`.
- `test_i18n` — lookup and fallback.
- `test_kvctlcommand` — CLI parser, including `duck`, `show`, `scene`, per-profile `set volume`, `mute on|off`.
- `test_pwutils` — PipeWire client filtering, skipped-name fallback, deduplication.
- `test_appmatcher` — focused-window → `AudioApp` lookup, including the empty-field regression from PR #54.
- `test_volumecontroller` — smoke tests including unavailable PulseAudio.
- `test_inputhandler` — API, evdev device listing, modifier normalize, `resolveProfile` specificity, scroll binding matching, show-volume action, ducking action.
- `test_mprisclient` — player detection, metadata/track-id changes, seek forwarding, reload, instance-suffix matching, priority, polling guards. Run under `dbus-run-session`.
- `test_dbusinterface` — `Volume`/`Muted` property writers route to absolute setters, clamping, no-op when no active app, `ToggleMute()` still toggles.
- `test_osdwindow` — progress-row metadata update and position preservation.

Run:

```bash
cd cpp/build && ctest -E test_mprisclient --output-on-failure
cd cpp/build && dbus-run-session -- ctest -R test_mprisclient --output-on-failure
```

Documentation-only changes do not require a build. For code changes under `cpp/src` or `cpp/tests`, build first and run the focused or full test set based on risk.

## CI and Formatting

GitHub Actions CI in `.github/workflows/ci.yml` runs for PRs and pushes to `main`. It builds in Debug and Release, runs CTest, and checks `clang-format --dry-run --Werror` only for *changed* C++ files in `cpp/src` and `cpp/tests`. The workflow is path-filtered: docs-only Markdown changes do not trigger CI.

Before opening a PR that touches `cpp/src` or `cpp/tests`, run `clang-format --dry-run --Werror <changed-cpp-files>`. If the check fails, run `clang-format -i` only on the changed files — do not bulk-format the repo.

`clang-tidy` is not part of CI yet. `Claude Code Review` is currently disabled via `if: false` in `.github/workflows/claude-code-review.yml`.

## References

- `AGENTS.md`: mandatory git workflow, branch hygiene, high-risk implementation constraints, `dbus-send` recipes for scripts.
- `CLAUDE.md`: full architecture, module behavior, config schema, signal flow, key conventions.
- `README.md`: user-facing project overview, usage, `kv-ctl` examples, `qdbus` recipes for end users.
- `ROADMAP.md`: prioritized backlog and merged review notes.

Public bus names and object paths:

| Service | Object path | Interface |
|---|---|---|
| `org.keyboardvolumeapp` | `/org/keyboardvolumeapp` | `org.keyboardvolumeapp.VolumeControl` |
| `org.mpris.MediaPlayer2.keyboardvolumeapp` *(opt-in, default OFF)* | `/org/mpris/MediaPlayer2` | `org.mpris.MediaPlayer2` + `.Player` |
