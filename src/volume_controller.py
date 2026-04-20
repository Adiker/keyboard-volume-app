from __future__ import annotations
import pulsectl


class AudioApp:
    """Represents a sink-input (audio stream) from a running application."""

    def __init__(self, index: int, name: str, binary: str, volume: float, muted: bool):
        self.index = index
        self.name = name        # human-readable (app name from proplist)
        self.binary = binary    # application.process.binary
        self.volume = volume    # 0.0 – 1.0
        self.muted = muted

    def __repr__(self):
        return f"AudioApp({self.name!r}, vol={self.volume:.2f}, muted={self.muted})"


class VolumeController:
    def __init__(self):
        self._pulse = pulsectl.Pulse("keyboard-volume-app")

    # --- listing ---

    def list_apps(self) -> list[AudioApp]:
        apps = []
        for si in self._pulse.sink_input_list():
            name = (
                si.proplist.get("application.name")
                or si.proplist.get("media.name")
                or "Unknown"
            )
            binary = si.proplist.get("application.process.binary", "")
            vol = self._pulse.volume_get_all_chans(si)
            muted = bool(si.mute)
            apps.append(AudioApp(si.index, name, binary, vol, muted))
        return apps

    def find_app(self, name: str) -> AudioApp | None:
        for app in self.list_apps():
            if app.name == name or app.binary == name:
                return app
        return None

    # --- volume ---

    def get_volume(self, app_name: str) -> float | None:
        app = self.find_app(app_name)
        return app.volume if app else None

    def set_volume(self, app_name: str, volume: float) -> float | None:
        """Set volume (0.0–1.0), clamp to [0, 1]. Returns actual new volume or None."""
        app = self.find_app(app_name)
        if app is None:
            return None
        volume = max(0.0, min(1.0, volume))
        for si in self._pulse.sink_input_list():
            if si.index == app.index:
                self._pulse.volume_set_all_chans(si, volume)
                return volume
        return None

    def change_volume(self, app_name: str, delta: float) -> float | None:
        """Change volume by delta (e.g. +0.05 / -0.05). Returns new volume or None."""
        current = self.get_volume(app_name)
        if current is None:
            return None
        return self.set_volume(app_name, current + delta)

    def toggle_mute(self, app_name: str) -> bool | None:
        """Toggle mute. Returns new mute state or None if app not found."""
        app = self.find_app(app_name)
        if app is None:
            return None
        for si in self._pulse.sink_input_list():
            if si.index == app.index:
                new_mute = 0 if si.mute else 1
                self._pulse.sink_input_mute(si.index, new_mute)
                return bool(new_mute)
        return None

    def close(self):
        self._pulse.close()
