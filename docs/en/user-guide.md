[Polski](../pl/user-guide.md) · [Back to README](../../README.md)

# User guide

This guide covers the normal tray, hotkey and OSD workflow. Advanced JSON
editing is described in the [configuration guide](configuration.md), and
scripts/D-Bus clients in the [remote-control guide](remote-control.md).

## First launch and app selection

The first-run wizard asks for the interface language, an input device and a
default audio app. The tray app list puts actively playing clients first, then
shows idle PipeWire clients that are connected but currently paused.

Use the tray menu to:

- select the active audio app;
- refresh the audio-app list;
- change the captured input device;
- open Settings;
- open the bilingual About dialog with version, links and the offline license.

The selected app is preserved while PulseAudio or PipeWire reconnects. A
temporary disappearance from a refresh does not replace the configured choice.

## Everyday controls

- Volume Up/Down changes only the selected app by the configured step.
- Mute toggles only that app and shows a mute indicator in the OSD.
- A profile `show` hotkey displays the current value without changing it.
- Mouse-wheel bindings can be used alongside ordinary evdev key bindings.
- The tray remains available for selection and settings while the global input
  handler is running.

The input handler grabs sibling event nodes of the chosen keyboard, such as a
separate Consumer Control interface, and any other device advertising a
configured key or wheel binding. This prevents the desktop from also acting on
the same event.

Configured hotkeys are captured at the evdev level. The desktop and Qt do not
see those exact events while the app is running. To replace an active binding,
right-click its field in Settings → Profiles, choose **Unassign**, save, reopen
the profile and capture the new key or wheel direction.

## OSD

The OSD is a frameless, always-on-top overlay showing the app name, volume bar
and percentage. Its timeout, opacity, colors, screen and position are
configurable. Drag any visible edge or corner to resize it; the size is saved
automatically.

Optional position controls can add:

- arrow buttons that snap the OSD to screen edges;
- dragging from the interior of the OSD;
- keyboard shortcuts active only while the OSD is visible.

The playback row can show app/track labels, elapsed and total time, a progress
bar and album art. Clicking or dragging the bar seeks when the player reports
`CanSeek` and a known duration. Unknown-length streams show `LIVE` instead.
Set `progress_interactive` to `false` to retain the visual row without mouse
seeking.

The label mode can be `app`, `title_artist`, `artist_title`, `app_track`,
`player_track`, `player_track_art` or a custom top/bottom template. Long app and
track names use a marquee; short labels remain static. See the configuration
guide for template tokens and persisted field names.

Settings also controls the interface language, volume step, OSD timeout and
colors, playback poll interval, tracked-player priority, media-action OSD mode,
optional fake MPRIS exposure, profile membership and app aliases. Changes are
saved to `config.json` atomically.

## Profiles and focused-window switching

Profiles let one keyboard control several applications. Each profile has:

- one or more target app names and an optional case-insensitive `app_regex`;
- its own volume-up, volume-down, mute, show and ducking bindings;
- optional `Ctrl` and/or `Shift` modifiers;
- an optional PulseAudio output sink;
- optional minimum and maximum volume limits;
- an `auto_switch` flag controlling participation in focused-window switching.

The first profile is the default. Bare tray, D-Bus and `kv-ctl` actions target
it unless a profile is explicitly selected. When global auto-profile switching
is enabled, the focused window chooses the matching profile; per-profile
`auto_switch` can opt individual profiles out.

## Ducking, scenes and routing

Enable Focus audio ducking in a profile to lower every other known audio app to
a configured percentage. Press the profile's ducking binding again to restore
the saved levels.

Audio scenes are named presets. A scene target can set any combination of:

- volume from 0 to 100;
- muted/unmuted state;
- a PulseAudio sink name.

Scenes can have a global hotkey and can be applied from Settings or `kv-ctl`.
Profile bindings take precedence over scene bindings, which take precedence
over media bindings. If two scenes share a binding, the first scene in the
configuration wins.

Profile sinks and scene sinks use stable PulseAudio sink names, not their
human-readable descriptions. The remote-control guide shows ad-hoc routing
with `kv-ctl set sink APP DEVICE`.

## App names and aliases

PipeWire wrappers sometimes expose a friendly client and a different stream
binary. The app normalizes those identities so the tray can show the friendly
name while volume control still targets the real stream. Settings → App aliases
can override this mapping:

```json
{ "match": "chromium", "display": "YouTube Music", "target": "youtube-music" }
```

`audio_app_filters` can add or remove system-binary and skipped-name entries.
These options refine the built-in lists; they do not replace them wholesale.

## Playback and media hotkeys

The app consumes other players' MPRIS metadata, position, seek support and
player priority. When focused-window switching identifies an audio app, its
matching MPRIS player is preferred; otherwise `tracked_players` priority and
playback state select the player.

Media hotkeys for play/pause, next, previous and stop are unassigned by
default. They dispatch to the selected MPRIS player and are independent of
profile volume bindings. If an active profile claims the same binding, the
profile wins. The media-controls OSD can be disabled, limited to the pressed
action or shown as the full volume OSD.

The optional fake MPRIS endpoint
`org.mpris.MediaPlayer2.keyboardvolumeapp` is disabled by default. Enable it
under Settings → Playback progress only when desktop widgets or another MPRIS
client should see the app as a player.

## PipeWire volume warning

**Hidden PipeWire volume detected** means that a raw PipeWire multiplier differs
from `1.0` while mixer-visible channel values report another level. Detection at
startup, refresh, reconnect, profile switching and stream appearance is
read-only. The next explicit volume action folds the effective gain into the
visible channels and resets the raw multiplier to `1.0`; no config migration is
needed.

Native PulseAudio has no separate PipeWire raw multiplier, so it continues to
use libpulse and stream-restore channel volumes.

## Troubleshooting

- If no input device is listed, verify the `input` group membership and start a
  new login session.
- If the selected app disappears after an audio-daemon restart, wait for the
  reconnect/backoff to finish; the configured app is retained.
- If progress metadata needs investigation, start the app with
  `KVA_DEBUG_PROGRESS=1` to log metadata, position sources and OSD decisions.
- If a hotkey cannot be recaptured, unassign it first as described above; the
  running evdev grab intentionally hides it from Qt's key events.
