from __future__ import annotations
import json
import subprocess
import time
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

# How long to cache the pw-dump app list (seconds)
_LIST_CACHE_TTL = 5.0


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
        self._list_cache: list[AudioApp] = []
        self._list_cache_ts: float = 0.0

    # ------------------------------------------------------------------
    # Tray menu — full list including idle apps (pw-dump, cached)
    # ------------------------------------------------------------------

    def list_apps(self, force_refresh: bool = False) -> list[AudioApp]:
        """Return all audio-capable apps: active streams + idle PipeWire clients.
        Result is cached for _LIST_CACHE_TTL seconds to avoid repeated pw-dump calls.
        """
        now = time.monotonic()
        if not force_refresh and (now - self._list_cache_ts) < _LIST_CACHE_TTL:
            return self._list_cache

        apps: dict[str, AudioApp] = {}
        active_binaries: set[str] = set()

        # 1. Active sink inputs (fast pulsectl IPC)
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

        # 2. Idle PipeWire clients (slow subprocess — cached)
        for name, binary in self._list_pipewire_clients():
            if name in _SKIP_APP_NAMES:
                continue
            if binary in active_binaries:
                continue
            if name not in apps:
                apps[name] = AudioApp(None, name, binary, 1.0, False)

        result = sorted(apps.values(), key=lambda a: (not a.active, a.name.lower()))
        self._list_cache = result
        self._list_cache_ts = now
        return result

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

    # ------------------------------------------------------------------
    # Hot path — volume change (no pw-dump, single sink_input_list call)
    # ------------------------------------------------------------------

    def change_volume(self, app_name: str, delta: float) -> float | None:
        """Change volume by delta. Fast path: active sink input (~0.5ms).
        Slow path fallback: idle PipeWire node via pw-dump + pw-cli (~30ms).
        Returns new volume (0.0–1.0) or None if app not found anywhere.
        """
        # Fast path: active sink input
        for si in self._pulse.sink_input_list():
            name = si.proplist.get("application.name") or si.proplist.get("media.name", "")
            binary = si.proplist.get("application.process.binary", "")
            if name != app_name and binary != app_name:
                continue
            current = self._pulse.volume_get_all_chans(si)
            new_vol = max(0.0, min(1.0, current + delta))
            self._pulse.volume_set_all_chans(si, new_vol)
            return new_vol

        # Slow path: app is paused/idle — find its PipeWire node directly
        node = self._find_pw_node_for_app(app_name)
        if node:
            node_id, current_vol, _ = node
            new_vol = max(0.0, min(1.0, current_vol + delta))
            if self._set_pw_node_volume(node_id, new_vol):
                return new_vol
        return None

    def toggle_mute(self, app_name: str) -> tuple[bool, float] | None:
        """Toggle mute. Returns (new_mute_state, current_volume) or None if not found."""
        # Fast path: active sink input
        for si in self._pulse.sink_input_list():
            name = si.proplist.get("application.name") or si.proplist.get("media.name", "")
            binary = si.proplist.get("application.process.binary", "")
            if name != app_name and binary != app_name:
                continue
            new_mute = 0 if si.mute else 1
            self._pulse.sink_input_mute(si.index, new_mute)
            vol = self._pulse.volume_get_all_chans(si)
            return (bool(new_mute), vol)

        # Slow path: idle PipeWire node
        node = self._find_pw_node_for_app(app_name)
        if node:
            node_id, vol, muted = node
            new_muted = not muted
            if self._set_pw_node_mute(node_id, new_muted):
                return (new_muted, vol)
        return None

    # ------------------------------------------------------------------
    # PipeWire node fallback (for paused/idle apps with no active sink input)
    # ------------------------------------------------------------------

    def _find_pw_node_for_app(self, app_name: str) -> tuple[int, float, bool] | None:
        """Find an idle PipeWire stream node by app name/binary.
        Returns (node_id, volume, muted) or None.
        """
        try:
            result = subprocess.run(
                ["pw-dump"], capture_output=True, text=True, timeout=2
            )
            data = json.loads(result.stdout)
        except Exception:
            return None

        best: tuple[int, float, bool] | None = None
        for obj in data:
            if "Node" not in obj.get("type", ""):
                continue
            info = obj.get("info", {})
            props = info.get("props", {})
            name = props.get("application.name", "")
            binary = props.get("application.process.binary", "")
            if name != app_name and binary != app_name:
                continue
            # Only stream nodes (not hardware sinks/sources)
            media_class = props.get("media.class", "")
            if not media_class.startswith("Stream/"):
                continue

            params = info.get("params", {})
            prop_list = params.get("Props", [])
            vol, muted = 1.0, False
            if prop_list and isinstance(prop_list, list):
                p = prop_list[0]
                vol = float(p.get("volume", 1.0))
                muted = bool(p.get("mute", False))

            entry = (int(obj["id"]), vol, muted)
            # Prefer output streams (playback) over input streams (capture)
            if "Output" in media_class:
                return entry
            best = entry  # keep as fallback if only input found

        return best

    def _set_pw_node_volume(self, node_id: int, volume: float) -> bool:
        try:
            subprocess.run(
                ["pw-cli", "set-param", str(node_id), "Props",
                 f"{{ volume: {volume:.6f} }}"],
                capture_output=True, timeout=1,
            )
            return True
        except Exception:
            return False

    def _set_pw_node_mute(self, node_id: int, muted: bool) -> bool:
        try:
            subprocess.run(
                ["pw-cli", "set-param", str(node_id), "Props",
                 f"{{ mute: {'true' if muted else 'false'} }}"],
                capture_output=True, timeout=1,
            )
            return True
        except Exception:
            return False

    def close(self):
        self._pulse.close()
