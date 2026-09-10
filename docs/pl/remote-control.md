[English](../en/remote-control.md) · [Powrót do README](../../README.pl.md)

# Sterowanie zdalne

`kv-ctl` jest zalecanym klientem do skryptów. Aplikacja zasobnika musi już
działać, ponieważ klient korzysta z jej usługi na sesyjnej szynie D-Bus.

## `kv-ctl`

```bash
# Zmiana względna i wyciszenie
kv-ctl up [--profile ID]
kv-ctl down [--profile ID]
kv-ctl mute                 # przełącz
kv-ctl mute on [--profile ID]
kv-ctl mute off [--profile ID]

# Profile, ducking i sceny
kv-ctl duck [--profile ID]
kv-ctl show [--profile ID]       # pokaż bez zmiany
kv-ctl get profiles
kv-ctl get scenes
kv-ctl scene meeting

# Wykrywanie i wybór aplikacji
kv-ctl refresh
kv-ctl get apps
kv-ctl get active-app
kv-ctl set active-app Firefox

# Wartości bezwzględne i ustawienia
kv-ctl get volume
kv-ctl set volume 35 [--profile ID]
kv-ctl get muted
kv-ctl set muted true
kv-ctl get step
kv-ctl set step 5

# Przełączniki w trakcie działania
kv-ctl get progress-enabled
kv-ctl set progress-enabled true
kv-ctl get auto-profile-switch
kv-ctl set auto-profile-switch false

# Sink i routing ad-hoc
kv-ctl get sinks
kv-ctl set sink chromium alsa_output.usb-headset

# Przekazywanie sterowania MPRIS
kv-ctl media play-pause
kv-ctl media next
kv-ctl media previous
kv-ctl media stop
```

`--profile ID` działa dla `up`, `down`, `mute`, `duck`, `show` i `set volume`.
Nazwy aplikacji rozróżniają wielkość liter; dokładne wartości pokazuje
`kv-ctl get apps`. `kv-ctl get profiles` zachowuje pierwsze sześć kolumn
rozdzielonych tabulatorami i dodaje pełne pola `apps=` oraz `regex=`.

`kv-ctl --help` pokazuje aktualny skrót składni. `kv-ctl` nie edytuje pliku
konfiguracyjnego bezpośrednio: deleguje zmiany do działającej aplikacji, dzięki
czemu obowiązują te same walidacje, limity profili i asynchroniczny worker audio.

## Własna usługa D-Bus

Aplikacja zawsze rejestruje na sesyjnej szynie:

```text
service:   org.keyboardvolumeapp
path:      /org/keyboardvolumeapp
interface: org.keyboardvolumeapp.VolumeControl
```

Poniższe przykłady używają `qdbus`.

### Właściwości

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

### Metody

```bash
# Profil domyślny
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

# Profile i sceny
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

# Akcje multimedialne przekazywane do wybranego odtwarzacza MPRIS
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.keyboardvolumeapp.VolumeControl.MediaPlayPause
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.keyboardvolumeapp.VolumeControl.MediaNext
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.keyboardvolumeapp.VolumeControl.MediaPrevious
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.keyboardvolumeapp.VolumeControl.MediaStop
```

Do wyszukiwania i diagnozowania:

```bash
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.freedesktop.DBus.Properties.GetAll \
  org.keyboardvolumeapp.VolumeControl
qdbus org.keyboardvolumeapp /org/keyboardvolumeapp \
  org.freedesktop.DBus.Introspectable.Introspect
```

Właściwości `VolumeControl` to `Volume`, `Muted`, `ActiveApp`, `Apps`,
`VolumeStep`, `Profiles`, `Scenes`, `Sinks`, `ProgressEnabled` i
`AutoProfileSwitch`. Dla zapisywalnych właściwości używaj `Get`, `Set` i
`GetAll`; `Profiles`, `Scenes` i `Sinks` są listami tylko do odczytu.

## Opcjonalny fałszywy endpoint MPRIS

Po włączeniu Ustawienia → Postęp odtwarzania → **Eksponuj fałszywy endpoint
MPRIS** aplikacja dodatkowo rejestruje:

```text
service: org.mpris.MediaPlayer2.keyboardvolumeapp
path:    /org/mpris/MediaPlayer2
```

Domyślnie endpoint jest wyłączony, aby nie był wykrywany jako prawdziwy
odtwarzacz przez np. Discord Music Presence. Przykłady:

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

`qdbus` jest opcjonalny na części dystrybucji Qt6. Typowane przepisy
`dbus-send` dla własnego interfejsu i endpointu MPRIS są w sekcji D-Bus pliku
[ARCHITECTURE.md](../../ARCHITECTURE.md).
