[Polski](../pl/configuration.md) · [Back to README](../../README.md)

# Configuration

The canonical file is:

```text
$XDG_CONFIG_HOME/keyboard-volume-app/config.json
```

When `XDG_CONFIG_HOME` is unset, this is
`~/.config/keyboard-volume-app/config.json`. Settings writes are atomic: a
failed save leaves the previous file intact.

## Import and export

Settings can export the current JSON or import another valid configuration.
Import creates a `config.json.backup-*` copy. After a successful import the app
asks whether to restart immediately or exit. It closes before applying the
new file so stale in-memory settings cannot overwrite it.

## Complete example

The following is a representative complete configuration using the current
schema. Missing fields are filled from the same defaults at load time.

```json
{
  "input_device": "/dev/input/event3",
  "selected_app": "youtube-music",
  "language": "en",
  "volume_step": 5,
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
    "progress_label_mode": "app",
    "custom_label_top": "{app}",
    "custom_label_bottom": "{title} — {artist}",
    "custom_label_show_art": false,
    "tracked_players": ["spotify", "youtube", "strawberry", "harmonoid"],
    "media_controls_enabled": true,
    "expose_mpris": false,
    "media_keys_osd_mode": "off",
    "show_media_keys_osd": false,
    "osd_scale": 1.0,
    "position_controls_enabled": false,
    "position_arrows_enabled": false,
    "position_drag_enabled": false,
    "position_keyboard_enabled": false,
    "layout_hotkeys": {
      "snap_up": 0,
      "snap_down": 0,
      "snap_left": 0,
      "snap_right": 0,
      "scale_up": 0,
      "scale_down": 0
    }
  },
  "hotkeys": {
    "volume_up": 115,
    "volume_down": 114,
    "mute": 113
  },
  "media_hotkeys": {
    "play_pause": 0,
    "next": 0,
    "previous": 0,
    "stop": 0
  },
  "auto_profile_switch": false,
  "profiles": [
    {
      "id": "default",
      "name": "Default",
      "apps": ["youtube-music"],
      "app_regex": "",
      "modifiers": [],
      "hotkeys": {
        "volume_up": 115,
        "volume_down": 114,
        "mute": 113,
        "show": 0
      },
      "ducking": { "enabled": false, "volume": 25, "hotkey": 0 },
      "auto_switch": true,
      "vol_min": 0,
      "vol_max": 100,
      "sink": ""
    },
    {
      "id": "comms",
      "name": "Comms",
      "apps": ["discord", "teams"],
      "app_regex": ".*(discord|teams|slack|zoom).*",
      "modifiers": ["ctrl"],
      "hotkeys": {
        "volume_up": 115,
        "volume_down": 114,
        "mute": 113,
        "show": 0
      },
      "ducking": { "enabled": true, "volume": 25, "hotkey": 88 },
      "auto_switch": true,
      "vol_min": 0,
      "vol_max": 100,
      "sink": ""
    }
  ],
  "scenes": [
    {
      "id": "meeting",
      "name": "Meeting",
      "hotkey": 88,
      "targets": [
        { "match": "Spotify", "volume": 10, "muted": false },
        { "match": "Discord", "volume": 80 },
        { "match": "Steam", "muted": true, "sink": "alsa_output.usb-headset" }
      ]
    }
  ],
  "app_aliases": [
    { "match": "chromium", "display": "YouTube Music", "target": "youtube-music" }
  ],
  "audio_app_filters": {
    "extra_system_binaries": [],
    "remove_system_binaries": [],
    "extra_skip_app_names": [],
    "remove_skip_app_names": []
  },
  "ui": {
    "settings_dialog_width": 0,
    "settings_dialog_height": 0
  }
}
```

## Hotkey bindings

Legacy numeric values are Linux evdev `EV_KEY` codes:

- `115` = `KEY_VOLUMEUP`
- `114` = `KEY_VOLUMEDOWN`
- `113` = `KEY_MUTE`

Mouse wheel bindings use an object, for example:

```json
{ "type": "rel", "code": 8, "direction": 1 }
```

Here `code: 8` is `REL_WHEEL`; use `direction: -1` for the opposite
direction. The same format is accepted by profile, media, scene and OSD layout
hotkeys. A numeric `0` means unassigned.

## Profiles and compatibility fields

`profiles` is the canonical source of profile identity and hotkeys. The
top-level `selected_app` and `hotkeys` fields are a deprecated mirror of
`profiles[0]`, retained for one release for older callers. Old files without a
`profiles` array are migrated automatically on first launch.

`apps` is the list of names matched by a profile; the older singular `app` form
is still accepted and is written as the first app. `app_regex` is a
case-insensitive expression used alongside the explicit names. `auto_switch`
controls whether the profile participates in focused-window switching.

`vol_min` and `vol_max` clamp hotkey, D-Bus and `kv-ctl` volume changes for that
profile. Values are percentages from 0 to 100; invalid or reversed limits are
sanitized. Scenes and ducking intentionally retain their own configured range.
`sink` is a stable PulseAudio sink name; an empty value uses the system default.

`auto_profile_switch` enables focused-window selection globally and defaults to
`false`; each profile's `auto_switch` defaults to `true`. `media_hotkeys` has
`play_pause`, `next`, `previous` and `stop` bindings, all unassigned by
default. A matching profile binding wins over a media binding; otherwise the
media action is sent to the selected MPRIS player.

## Scenes, aliases and filters

Scene target `match` uses the same display/binary names returned by
`kv-ctl get apps`. `volume` is a 0–100 percentage, while omitted `volume`,
`muted` or `sink` leaves that part unchanged. Scene IDs are stable slugs and
scene hotkeys are global.

`app_aliases.match` is compared case-insensitively against detected application
names/binaries. `display` changes the tray and picker label; `target` changes
the controllable binary, and an empty target preserves the original.
`audio_app_filters` adds or removes entries from the built-in system-binary and
skip-name lists through its four `extra_*` and `remove_*` arrays.

## OSD playback and layout fields

`progress_enabled` controls the progress row; `progress_interactive` controls
mouse seeking; `progress_poll_ms` is clamped to 200–2000 ms. `progress_label_mode`
accepts `app`, `title_artist`, `artist_title`, `app_track`, `player_track`,
`player_track_art` or `custom`. Legacy `track` and `both` values are normalized
when loaded. Custom labels use `{app}`, `{player}`, `{title}`, `{artist}` and
`{album}` tokens. `tracked_players` is the MPRIS service-name priority list.

`expose_mpris` controls registration of the optional fake MPRIS endpoint.
`media_keys_osd_mode` controls whether media actions show no OSD, only the
pressed action or the full volume OSD. The `show_media_keys_osd` boolean is kept
for compatibility with older configs.

Set `position_controls_enabled` and its three sub-options to enable OSD arrows,
interior dragging or keyboard positioning. `layout_hotkeys` uses the same
evdev/relative-binding representation.

For rare MPRIS progress issues, run the app with `KVA_DEBUG_PROGRESS=1`.
