[English](../en/configuration.md) · [Powrót do README](../../README.pl.md)

# Konfiguracja

Główny plik to:

```text
$XDG_CONFIG_HOME/keyboard-volume-app/config.json
```

Gdy `XDG_CONFIG_HOME` nie jest ustawione, używane jest
`~/.config/keyboard-volume-app/config.json`. Zapisy są atomowe: nieudany zapis
pozostawia poprzedni plik bez zmian.

## Import i eksport

Ustawienia mogą wyeksportować bieżący JSON albo zaimportować poprawny plik.
Import tworzy kopię `config.json.backup-*`. Po udanym imporcie aplikacja pyta,
czy od razu uruchomić się ponownie, czy zakończyć pracę. Zamknięcie przed
zastosowaniem zapobiega nadpisaniu importu starym stanem z pamięci.

## Pełny przykład

Poniższy przykład przedstawia kompletny bieżący schemat. Brakujące pola są przy
ładowaniu uzupełniane tymi samymi wartościami domyślnymi.

```json
{
  "input_device": "/dev/input/event3",
  "selected_app": "youtube-music",
  "language": "pl",
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

## Powiązania skrótów

Starsze wartości liczbowe to kody Linux evdev `EV_KEY`:

- `115` = `KEY_VOLUMEUP`
- `114` = `KEY_VOLUMEDOWN`
- `113` = `KEY_MUTE`

Skrót kółka myszy ma postać obiektu:

```json
{ "type": "rel", "code": 8, "direction": 1 }
```

`code: 8` oznacza `REL_WHEEL`; dla przeciwnego kierunku użyj `direction: -1`.
Ten format działa dla skrótów profili, mediów, scen i położenia OSD. Liczbowe
`0` oznacza brak przypisania.

## Profile i zgodność wsteczna

`profiles` jest kanonicznym źródłem tożsamości i skrótów. Najwyższe poziomy
`selected_app` oraz `hotkeys` są przestarzałym odbiciem `profiles[0]` i pozostają
przez jedno wydanie dla starszych klientów. Pliki bez tablicy `profiles` są
automatycznie migrowane przy pierwszym uruchomieniu.

`apps` zawiera nazwy dopasowywane przez profil; starsza pojedyncza forma `app`
jest nadal akceptowana i zapisywana jako pierwsza aplikacja. `app_regex` to
wyrażenie bez rozróżniania wielkości liter używane obok nazw jawnych.
`auto_switch` określa udział profilu w przełączaniu według aktywnego okna.

`vol_min` i `vol_max` ograniczają zmiany głośności z hotkeyów, D-Bus i `kv-ctl`
dla danego profilu. Są to wartości 0–100; błędne lub odwrócone granice są
porządkowane. Sceny i ducking zachowują własny zakres. `sink` to stabilna nazwa
wyjścia PulseAudio; pusty wpis oznacza domyślne wyjście systemu.

`auto_profile_switch` globalnie włącza wybór profilu według aktywnego okna i
domyślnie ma wartość `false`; `auto_switch` profilu domyślnie ma `true`.
`media_hotkeys` zawiera `play_pause`, `next`, `previous` i `stop`, wszystkie
domyślnie nieprzypisane. Pasujący skrót profilu wygrywa ze skrótem medialnym;
w przeciwnym razie akcja trafia do wybranego odtwarzacza MPRIS.

## Sceny, aliasy i filtry

`match` celu sceny używa tych samych nazw wyświetlanych/binarnych co
`kv-ctl get apps`. `volume` jest procentem 0–100, a pominięte `volume`, `muted`
lub `sink` pozostawiają daną część bez zmian. ID scen są stabilnymi slugami,
a hotkey sceny jest globalny.

`app_aliases.match` jest porównywane bez rozróżniania wielkości liter z wykrytą
nazwą/binarką. `display` zmienia etykietę w zasobniku i pickerze, `target`
zmienia binarkę sterowania, a pusty target zachowuje oryginał.
`audio_app_filters` dodaje lub usuwa wpisy wbudowanych list systemowych przez
cztery tablice `extra_*` i `remove_*`.

## Pola postępu OSD i położenia

`progress_enabled` włącza wiersz postępu, `progress_interactive` steruje
seekowaniem myszą, a `progress_poll_ms` jest ograniczane do 200–2000 ms.
`progress_label_mode` przyjmuje `app`, `title_artist`, `artist_title`,
`app_track`, `player_track`, `player_track_art` albo `custom`. Starsze wartości
`track` i `both` są normalizowane przy migracji. Szablony używają tokenów
`{app}`, `{player}`, `{title}`, `{artist}` i `{album}`. `tracked_players` to lista
priorytetów nazw usług MPRIS.

`expose_mpris` steruje opcjonalnym endpointem MPRIS. `media_keys_osd_mode`
określa, czy akcje multimedialne nie pokazują OSD, pokazują tylko akcję, czy
pełny OSD głośności. Boolean `show_media_keys_osd` jest zachowany dla zgodności
ze starszymi plikami.

`position_controls_enabled` oraz trzy pola podrzędne włączają strzałki OSD,
przeciąganie wewnętrzne i pozycjonowanie z klawiatury. `layout_hotkeys` używa
tego samego formatu evdev/powiązań względnych.

Przy rzadkich problemach z postępem MPRIS uruchom aplikację z
`KVA_DEBUG_PROGRESS=1`.
