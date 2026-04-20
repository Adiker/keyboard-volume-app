from __future__ import annotations
import json
import subprocess
import pulsectl

# System processes to exclude from the app list
_SYSTEM_BINARIES = {
    "wireplumber", "pipewire", "kwin_wayland", "plasmashell",
    "kded5", "kded6", "xdg-desktop-portal", "xdg-desktop-portal-kde",
    "polkit-kde-authentication-agent-1", "pactl", "pw-cli", "pw-dump",
    "python3", "python3.14", "python", "QtWebEngineProcess", "",
}

# Internal sub-process names that are part of a parent app — skip them
_SKIP_APP_NAMES = {
    "ringrtc", "WEBRTC VoiceEngine", "Chromium input",
}


class AudioApp:
    """Represents an audio application — may or may not have an active stream."""

    def __init__(self, index: int | None, name: str, binary: str, volume: float, muted: bool):
        self.index = index      # sink-input index, None if app has no active stream
        self.name = name
        self.binary = binary
        self.volume = volume    # 0.0 – 1.0
        self.muted = muted
        self.active = index is not None  # currently playing audio

    def __repr__(self):
        state = "active" if self.active else "idle"
        return f"AudioApp({self.name!r}, vol={self.volume:.2f}, muted={self.muted}, {state})"


class VolumeController:
    def __init__(self):
        self._pulse = pulsectl.Pulse("keyboard-volume-app")

    # --- listing ---

    def list_apps(self) -> list[AudioApp]:
        """Return all audio-capable apps: active streams + idle PipeWire clients."""
        apps: dict[str, AudioApp] = {}      # keyed by name
        active_binaries: set[str] = set()   # track binaries with active streams

        # 1. Active sink inputs (currently playing)
        for si in self._pulse.sink_input_list():
            name = (
                si.proplist.get("application.name")
                or si.proplist.get("media.name")
                or "Unknown"
            )
            binary = si.proplist.get("application.process.binary", "")
            vol = self._pulse.volume_get_all_chans(si)
            muted = bool(si.mute)
            apps[name] = AudioApp(si.index, name, binary, vol, muted)
            if binary:
                active_binaries.add(binary)

        # 2. Idle PipeWire clients (connected but not actively playing)
        for name, binary in self._list_pipewire_clients():
            if name in _SKIP_APP_NAMES:
                continue
            # Skip if we already have this binary as an active stream
            if binary in active_binaries:
                continue
            if name not in apps:
                apps[name] = AudioApp(None, name, binary, 1.0, False)

        return sorted(apps.values(), key=lambda a: (not a.active, a.name.lower()))

    def _list_pipewire_clients(self) -> list[tuple[str, str]]:
        """Get app names from PipeWire client list via pw-dump."""
        try:
            result = subprocess.run(
                ["pw-dump"], capture_output=True, text=True, timeout=2
            )
            data = json.loads(result.stdout)
        except Exception:
            return []

        seen: dict[str, str] = {}
        for obj in data:
            if "Client" not in obj.get("type", ""):
                continue
            props = obj.get("info", {}).get("props", {})
            binary = props.get("application.process.binary", "")
            if not binary or binary in _SYSTEM_BINARIES:
                continue
            name = props.get("application.name") or binary
            if name in _SKIP_APP_NAMES or "input" in name.lower():
                name = binary
            if not name.strip():
                continue
            seen[name] = binary

        return list(seen.items())

    def find_app(self, app_name: str) -> AudioApp | None:
        for app in self.list_apps():
            if app.name == app_name or app.binary == app_name:
                return app
        return None

    # --- volume ---

    def get_volume(self, app_name: str) -> float | None:
        app = self.find_app(app_name)
        if app is None:
            return None
        if not app.active:
            return app.volume  # return stored value for idle apps
        return app.volume

    def set_volume(self, app_name: str, volume: float) -> float | None:
        """Set volume (0.0–1.0). Returns actual new volume or None if app not found."""
        app = self.find_app(app_name)
        if app is None:
            return None
        volume = max(0.0, min(1.0, volume))
        if not app.active:
            # App is idle — nothing to set right now, return clamped value
            return volume
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
        """Toggle mute. Returns new mute state or None if app not found / idle."""
        app = self.find_app(app_name)
        if app is None or not app.active:
            return None
        for si in self._pulse.sink_input_list():
            if si.index == app.index:
                new_mute = 0 if si.mute else 1
                self._pulse.sink_input_mute(si.index, new_mute)
                return bool(new_mute)
        return None

    def close(self):
        self._pulse.close()
