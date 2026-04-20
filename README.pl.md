# keyboard-volume-app

Linuksowa alternatywa dla skryptów AutoHotkey sterujących głośnością na Windowsie. Zmienia głośność wybranej aplikacji przez klawiaturę — bez dotykania głośności systemowej. Wybierz aplikację audio z ikony tray, użyj kółka multimedialnego i patrz na nakładkę OSD z aktualnym poziomem.

**[English README](README.md)**

![Python](https://img.shields.io/badge/Python-3.10%2B-blue)
![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey)
![Desktop](https://img.shields.io/badge/Desktop-KDE%20Plasma-blue)
![Audio](https://img.shields.io/badge/Audio-PipeWire%20%2F%20PulseAudio-orange)

---

## Funkcje

- **Sterowanie głośnością per aplikacja** — zmienia głośność tylko wybranej aplikacji, nie głośności systemowej
- **Globalne przechwytywanie klawiszy** — czyta bezpośrednio z urządzenia evdev, działa niezależnie od tego, które okno jest aktywne
- **Przechwytywanie wielu węzłów** — automatycznie grabuje wszystkie powiązane event nodes wybranej klawiatury (np. główna klawiatura + interfejs Consumer Control), by system nie przechwycił klawiszy
- **Nakładka OSD** — bezramkowe okno always-on-top pokazujące nazwę aplikacji, pasek głośności i procent; chowa się automatycznie po konfigurowalnym czasie
- **Ikona tray** — wybór aktywnej aplikacji audio, odświeżanie listy, zmiana urządzenia wejściowego lub otwieranie ustawień z menu tray
- **Wykrywanie nieaktywnych aplikacji** — lista zawiera wszystkie aplikacje podłączone do PipeWire, nie tylko te aktualnie odtwarzające audio
- **Wyciszenie** — naciśnij dedykowany klawisz mute na klawiaturze (ten multimedialny, który normalnie wyciszałby system), aby wyciszyć/odciszyć tylko wybraną aplikację. OSD pokazuje aktualny poziom głośności ze wskaźnikiem 🔇 gdy wyciszono.
- **Trwała konfiguracja** — wszystkie ustawienia zapisywane w `~/.config/keyboard-volume-app/config.json`
- **Autostart KDE** — dołączony plik `.desktop` do automatycznego uruchamiania z sesją pulpitu
- **Interfejs PL / EN** — przełączanie języka w Ustawieniach

---

## Wymagania

| Zależność | Cel |
|---|---|
| Python 3.10+ | — |
| [PyQt6](https://pypi.org/project/PyQt6/) | Ikona tray, okno OSD, dialogi ustawień |
| [evdev](https://pypi.org/project/evdev/) | Globalne przechwytywanie klawiszy |
| [pulsectl](https://pypi.org/project/pulsectl/) | Sterowanie głośnością per aplikacja przez gniazdo PipeWire/PulseAudio |
| `pw-dump` | Listowanie nieaktywnych aplikacji audio (część pakietu `pipewire`) |

---

## Instalacja

```bash
git clone git@github.com:Adiker/keyboard-volume-app.git
cd keyboard-volume-app

python3 -m venv .venv
source .venv/bin/activate

pip install -r requirements.txt
```

### Uprawnienia do urządzenia wejściowego

evdev wymaga dostępu do odczytu `/dev/input/event*`. Dodaj swojego użytkownika do grupy `input`:

```bash
sudo usermod -aG input $USER
```

Wyloguj się i zaloguj ponownie, by zmiana weszła w życie.

---

## Uruchamianie

```bash
cd keyboard-volume-app
source .venv/bin/activate
python3 -m src.main
```

Przy pierwszym uruchomieniu pojawi się dialog z prośbą o wybranie urządzenia wejściowego. Aplikacja filtruje listę, pokazując tylko urządzenia eksponujące klawisze głośności (`KEY_VOLUMEUP` / `KEY_VOLUMEDOWN`).

---

## Autostart w KDE

Skopiuj dołączony plik `.desktop` do katalogu autostartu KDE:

```bash
cp keyboard-volume-app.desktop ~/.config/autostart/
```

> **Uwaga:** Jeśli zainstalowałeś projekt w innej lokalizacji lub używasz środowiska wirtualnego, edytuj linie `Exec=` i `Path=` w pliku.

---

## Użytkowanie

1. **Wybór aplikacji audio** — kliknij lewym przyciskiem ikonę tray → wybierz aplikację z listy. Aplikacje aktualnie odtwarzające audio są na górze; nieaktywne (podłączone do PipeWire, ale zapauzowane) pojawiają się poniżej.
2. **Kółko głośności** — przekręć w górę/dół, aby zmienić głośność wybranej aplikacji o skonfigurowany krok.
3. **Wyciszenie** — naciśnij dedykowany klawisz mute na klawiaturze (ten multimedialny, który normalnie wyciszałby system), aby wyciszyć/odciszyć tylko wybraną aplikację. OSD pokazuje aktualny poziom głośności ze wskaźnikiem 🔇 gdy wyciszono.
4. **Odświeżenie listy** — menu tray → *Odśwież listę aplikacji*, by ponownie przeskanować aktywne aplikacje audio.
5. **Zmiana urządzenia wejściowego** — menu tray → *Zmień urządzenie wejściowe...*, by wybrać inną klawiaturę bez restartu aplikacji.
6. **Ustawienia** — menu tray → *Ustawienia...*, by skonfigurować:
   - Język interfejsu (English / Polski)
   - Czas wyświetlania OSD (ms)
   - Pozycja OSD na ekranie (X / Y)
   - Krok głośności na naciśnięcie klawisza (%)
   - Kolory OSD (tło, tekst, pasek)

---

## Konfiguracja

Plik konfiguracyjny: `~/.config/keyboard-volume-app/config.json`

```json
{
  "input_device": "/dev/input/event3",
  "selected_app": "youtube-music",
  "language": "pl",
  "osd": {
    "x": 50,
    "y": 1150,
    "timeout_ms": 1200,
    "color_bg": "#1A1A1A",
    "color_text": "#FFFFFF",
    "color_bar": "#0078D7"
  },
  "volume_step": 5
}
```

Wszystkie pola są zapisywane automatycznie przez aplikację — ręczna edycja jest opcjonalna.

---

## Struktura projektu

```
keyboard-volume-app/
├── src/
│   ├── main.py              # Punkt wejścia, łączy wszystkie moduły
│   ├── config.py            # Odczyt/zapis konfiguracji JSON
│   ├── i18n.py              # Tłumaczenia PL/EN i funkcja tr()
│   ├── volume_controller.py # pulsectl — głośność i wyciszenie per aplikacja
│   ├── input_handler.py     # evdev QThread — globalne przechwytywanie klawiszy
│   ├── osd_window.py        # Nakładka OSD (PyQt6)
│   ├── tray_app.py          # Ikona tray i menu
│   ├── device_selector.py   # Dialog wyboru urządzenia wejściowego
│   └── settings_dialog.py   # Dialog ustawień OSD i głośności
├── resources/
│   └── icon.png
├── keyboard-volume-app.desktop
├── README.md                # English
├── README.pl.md             # Polski
└── requirements.txt
```

---

## Wydajność

Hot path zmiany głośności (naciśnięcie klawisza → aktualizacja OSD) używa jednego wywołania pulsectl IPC (~1ms). Cięższy subprocess `pw-dump` jest wywoływany tylko przy otwieraniu listy aplikacji w tray, z 5-sekundowym cache'em wyników.

---

## Licencja

MIT
