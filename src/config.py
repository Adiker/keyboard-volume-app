import json
import os
from pathlib import Path

CONFIG_DIR = Path.home() / ".config" / "keyboard-volume-app"
CONFIG_FILE = CONFIG_DIR / "config.json"

DEFAULTS = {
    "input_device": None,
    "selected_app": None,
    "language": "en",
    "osd": {
        "screen": 0,
        "x": 50,
        "y": 1150,
        "timeout_ms": 1200,
        "color_bg": "#1A1A1A",
        "color_text": "#FFFFFF",
        "color_bar": "#0078D7",
    },
    "volume_step": 5,
}


class Config:
    def __init__(self):
        self._data: dict = {}
        self.load()

    def load(self):
        CONFIG_DIR.mkdir(parents=True, exist_ok=True)
        if CONFIG_FILE.exists():
            try:
                with open(CONFIG_FILE, "r", encoding="utf-8") as f:
                    loaded = json.load(f)
                self._data = self._merge(DEFAULTS, loaded)
            except (json.JSONDecodeError, OSError):
                self._data = json.loads(json.dumps(DEFAULTS))
        else:
            self._data = json.loads(json.dumps(DEFAULTS))
            self.save()

    def save(self):
        CONFIG_DIR.mkdir(parents=True, exist_ok=True)
        with open(CONFIG_FILE, "w", encoding="utf-8") as f:
            json.dump(self._data, f, indent=2, ensure_ascii=False)

    # --- getters / setters ---

    @property
    def input_device(self) -> str | None:
        return self._data.get("input_device")

    @input_device.setter
    def input_device(self, value: str | None):
        self._data["input_device"] = value
        self.save()

    @property
    def selected_app(self) -> str | None:
        return self._data.get("selected_app")

    @selected_app.setter
    def selected_app(self, value: str | None):
        self._data["selected_app"] = value
        self.save()

    @property
    def language(self) -> str:
        return self._data.get("language", "en")

    @language.setter
    def language(self, value: str):
        self._data["language"] = value
        self.save()

    @property
    def volume_step(self) -> int:
        return self._data.get("volume_step", 5)

    @volume_step.setter
    def volume_step(self, value: int):
        self._data["volume_step"] = max(1, min(50, int(value)))
        self.save()

    # OSD helpers

    @property
    def osd(self) -> dict:
        return self._data["osd"]

    def set_osd(self, **kwargs):
        for key, value in kwargs.items():
            if key in self._data["osd"]:
                self._data["osd"][key] = value
        self.save()

    # --- internal ---

    @staticmethod
    def _merge(base: dict, override: dict) -> dict:
        result = json.loads(json.dumps(base))
        for key, value in override.items():
            if key in result and isinstance(result[key], dict) and isinstance(value, dict):
                result[key] = Config._merge(result[key], value)
            else:
                result[key] = value
        return result
