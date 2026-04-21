from __future__ import annotations

_STRINGS: dict[str, dict[str, str]] = {
    "en": {
        # tray
        "tray.section.audio_app":   "Audio application",
        "tray.action.refresh":      "Refresh app list",
        "tray.action.change_device":"Change input device...",
        "tray.action.settings":     "Settings...",
        "tray.action.quit":         "Quit",
        # device selector
        "device.title":             "Select input device",
        "device.title.first_run":   "Select input device (first launch)",
        "device.label":             "Devices with volume keys (Volume Up / Volume Down):",
        "device.btn.refresh":       "Refresh",
        "device.no_devices":        "No compatible devices found",
        # settings
        "settings.title":           "Settings",
        "settings.osd_timeout":     "OSD timeout:",
        "settings.osd_position":    "OSD position:",
        "settings.volume_step":     "Volume step:",
        "settings.color_bg":        "Background color:",
        "settings.color_text":      "Text color:",
        "settings.color_bar":       "Bar color:",
        "settings.language":        "Language:",
        "settings.osd_screen":      "OSD screen:",
        "settings.screen_primary":  "primary",
        # warnings
        "warn.no_device.title":     "No device selected",
        "warn.no_device.text":      (
            "No input device selected.\n"
            "You can select one later from tray menu → \"Change input device...\""
        ),
    },
    "pl": {
        # tray
        "tray.section.audio_app":   "Aplikacja audio",
        "tray.action.refresh":      "Odśwież listę aplikacji",
        "tray.action.change_device":"Zmień urządzenie wejściowe...",
        "tray.action.settings":     "Ustawienia...",
        "tray.action.quit":         "Wyjście",
        # device selector
        "device.title":             "Wybierz urządzenie wejściowe",
        "device.title.first_run":   "Wybierz urządzenie wejściowe (pierwsze uruchomienie)",
        "device.label":             "Urządzenia z klawiszami głośności (Volume Up / Volume Down):",
        "device.btn.refresh":       "Odśwież",
        "device.no_devices":        "Brak dostępnych urządzeń",
        # settings
        "settings.title":           "Ustawienia",
        "settings.osd_timeout":     "Czas OSD:",
        "settings.osd_position":    "Pozycja OSD:",
        "settings.volume_step":     "Krok głośności:",
        "settings.color_bg":        "Kolor tła:",
        "settings.color_text":      "Kolor tekstu:",
        "settings.color_bar":       "Kolor paska:",
        "settings.language":        "Język:",
        "settings.osd_screen":      "Ekran OSD:",
        "settings.screen_primary":  "główny",
        # warnings
        "warn.no_device.title":     "Brak urządzenia",
        "warn.no_device.text":      (
            "Nie wybrano urządzenia wejściowego.\n"
            "Możesz wybrać je później z menu tray → \"Zmień urządzenie wejściowe...\""
        ),
    },
}

LANGUAGES = {"en": "English", "pl": "Polski"}

_current: str = "en"


def set_language(code: str) -> None:
    global _current
    if code in _STRINGS:
        _current = code


def current_language() -> str:
    return _current


def tr(key: str) -> str:
    return _STRINGS.get(_current, {}).get(key) or _STRINGS["en"].get(key, key)
