[Polski](../pl/remote-control.md) · [Back to README](../../README.md)

# Remote control

`kv-ctl` is the recommended script-friendly client. The tray app must already
be running because the client talks to its session-bus service.

## `kv-ctl`

```bash
# Relative volume and mute actions
kv-ctl up [--profile ID]
kv-ctl down [--profile ID]
kv-ctl mute                 # toggle
kv-ctl mute on [--profile ID]
kv-ctl mute off [--profile ID]

# Profiles, ducking and scenes
kv-ctl duck [--profile ID]
kv-ctl show [--profile ID]       # display volume without changing it
kv-ctl get profiles
kv-ctl get scenes
kv-ctl scene meeting

# App discovery and selection
kv-ctl refresh
kv-ctl get apps
kv-ctl get active-app
kv-ctl set active-app Firefox

# Absolute values and settings
kv-ctl get volume
kv-ctl set volume 35 [--profile ID]
kv-ctl get muted
kv-ctl set muted true
kv-ctl get step
kv-ctl set step 5

# Runtime switches
kv-ctl get progress-enabled
kv-ctl set progress-enabled true
kv-ctl get auto-profile-switch
kv-ctl set auto-profile-switch false

# Sinks and ad-hoc routing
kv-ctl get sinks
kv-ctl set sink chromium alsa_output.usb-headset

# MPRIS media forwarding
kv-ctl media play-pause
kv-ctl media next
kv-ctl media previous
kv-ctl media stop
```

`--profile ID` applies to `up`, `down`, `mute`, `duck`, `show` and `set
volume`. App names are case-sensitive; use `kv-ctl get apps` for the exact
names known by the daemon. `kv-ctl get profiles` retains its original first six
tab-separated columns and appends complete `apps=` and `regex=` fields.

Use `kv-ctl --help` for the version-specific synopsis. `kv-ctl` has no direct
configuration-file mode; it delegates changes to the running app so the same
validation, profile limits and asynchronous audio worker are used as for tray
actions.

## Custom D-Bus service

The app always registers this session-bus endpoint:

```text
service: org.keyboardvolumeapp
path:    /org/keyboardvolumeapp
interface: org.keyboardvolumeapp.VolumeControl
```

The following examples use `qdbus`.

### Properties

```bash
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.freedesktop.DBus.Properties.Get \
  org.keyboardvolumeapp.VolumeControl Volume

qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.freedesktop.DBus.Properties.Get \
  org.keyboardvolumeapp.VolumeControl Apps

qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.freedesktop.DBus.Properties.Get \
  org.keyboardvolumeapp.VolumeControl ActiveApp

qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.freedesktop.DBus.Properties.Set \
  org.keyboardvolumeapp.VolumeControl Volume 0.75

qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.freedesktop.DBus.Properties.Set \
  org.keyboardvolumeapp.VolumeControl Muted true

qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.freedesktop.DBus.Properties.Set \
  org.keyboardvolumeapp.VolumeControl ActiveApp Firefox
```

### Methods

```bash
# Default profile
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.keyboardvolumeapp.VolumeControl.VolumeUp
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.keyboardvolumeapp.VolumeControl.VolumeDown
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.keyboardvolumeapp.VolumeControl.ToggleMute
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.keyboardvolumeapp.VolumeControl.SetMute true
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.keyboardvolumeapp.VolumeControl.ToggleDucking
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.keyboardvolumeapp.VolumeControl.ShowVolume
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.keyboardvolumeapp.VolumeControl.RefreshApps

# Profile and scene actions
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.keyboardvolumeapp.VolumeControl.VolumeUpProfile firefox-ctrl
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.keyboardvolumeapp.VolumeControl.VolumeDownProfile firefox-ctrl
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.keyboardvolumeapp.VolumeControl.ToggleMuteProfile firefox-ctrl
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.keyboardvolumeapp.VolumeControl.SetVolumeProfile firefox-ctrl 0.35
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.keyboardvolumeapp.VolumeControl.SetMuteProfile firefox-ctrl true
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.keyboardvolumeapp.VolumeControl.ToggleDuckingProfile discord
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.keyboardvolumeapp.VolumeControl.ShowVolumeProfile firefox-ctrl
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.keyboardvolumeapp.VolumeControl.ApplyScene meeting
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.keyboardvolumeapp.VolumeControl.SetAppSink chromium alsa_output.usb-headset

# Media actions forwarded to the selected MPRIS player
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.keyboardvolumeapp.VolumeControl.MediaPlayPause
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.keyboardvolumeapp.VolumeControl.MediaNext
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.keyboardvolumeapp.VolumeControl.MediaPrevious
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.keyboardvolumeapp.VolumeControl.MediaStop
```

For discovery and debugging:

```bash
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.freedesktop.DBus.Properties.GetAll \
  org.keyboardvolumeapp.VolumeControl
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.freedesktop.DBus.Introspectable.Introspect
```

The `VolumeControl` properties are `Volume`, `Muted`, `ActiveApp`, `Apps`,
`VolumeStep`, `Profiles`, `Scenes`, `Sinks`, `ProgressEnabled` and
`AutoProfileSwitch`. Use the standard `Get`, `Set` and `GetAll` calls above for
the writable properties; `Profiles`, `Scenes` and `Sinks` are read-only lists.

## Optional fake MPRIS endpoint

When Settings → Playback progress → **Expose fake MPRIS player endpoint** is
enabled, the app additionally registers:

```text
service: org.mpris.MediaPlayer2.keyboardvolumeapp
path:    /org/mpris/MediaPlayer2
```

It is disabled by default to avoid being mistaken for a real player by clients
such as Discord Music Presence. Example calls:

```bash
qdbus org.mpris.MediaPlayer2.keyboardvolumeapp /org/mpris/MediaPlayer2 \
  org.freedesktop.DBus.Properties.Get org.mpris.MediaPlayer2 Identity
qdbus org.mpris.MediaPlayer2.keyboardvolumeapp /org/mpris/MediaPlayer2 \
  org.freedesktop.DBus.Properties.Get org.mpris.MediaPlayer2.Player Volume
qdbus org.mpris.MediaPlayer2.keyboardvolumeapp /org/mpris/MediaPlayer2 \
  org.freedesktop.DBus.Properties.Set org.mpris.MediaPlayer2.Player Volume 0.5
qdbus org.mpris.MediaPlayer2.keyboardvolumeapp /org/mpris/MediaPlayer2 \
  org.mpris.MediaPlayer2.Quit
```

`qdbus` is optional on some Qt6 distributions. Typed `dbus-send` recipes for
the custom interface and MPRIS endpoint are maintained in the D-Bus section of
[ARCHITECTURE.md](../../ARCHITECTURE.md).
