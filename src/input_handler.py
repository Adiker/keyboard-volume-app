from __future__ import annotations
import select
import evdev
from evdev import ecodes
from PyQt6.QtCore import QThread, pyqtSignal


def _find_sibling_devices(primary_path: str) -> list[str]:
    """
    Return all evdev devices that belong to the same physical device
    as primary_path (matched by phys prefix) AND have volume keys.
    This is needed because keyboards often expose multiple event nodes
    (e.g. main keyboard + Consumer Control) — we must grab all of them
    to fully intercept volume keys from the OS/desktop.
    """
    volume_keys = {ecodes.KEY_VOLUMEUP, ecodes.KEY_VOLUMEDOWN, ecodes.KEY_MUTE}

    try:
        dev = evdev.InputDevice(primary_path)
        primary_phys: str = dev.phys or ""
        dev.close()
    except OSError:
        return [primary_path]

    if not primary_phys:
        return [primary_path]

    # Strip "/inputN" suffix → physical parent prefix
    phys_prefix = primary_phys.rsplit("/input", 1)[0]

    siblings = []
    for path in evdev.list_devices():
        try:
            d = evdev.InputDevice(path)
            phys = d.phys or ""
            caps = d.capabilities()
            keys = set(caps.get(ecodes.EV_KEY, []))
            d.close()
            if phys.startswith(phys_prefix) and (volume_keys & keys):
                siblings.append(path)
        except (PermissionError, OSError):
            pass

    return siblings if siblings else [primary_path]


class InputHandler(QThread):
    volume_up = pyqtSignal()
    volume_down = pyqtSignal()
    volume_mute = pyqtSignal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._device_path: str | None = None
        self._running = False

    def start_device(self, path: str):
        """Stop current device (if any) and start reading from new path."""
        self.stop()
        self._device_path = path
        self._running = True
        self.start()

    def stop(self):
        self._running = False
        self.quit()
        self.wait(2000)

    def run(self):
        if not self._device_path:
            return

        siblings = _find_sibling_devices(self._device_path)
        devices: list[evdev.InputDevice] = []

        for path in siblings:
            try:
                d = evdev.InputDevice(path)
                d.grab()
                devices.append(d)
                print(f"[InputHandler] Grabbed {path!r} ({d.name})")
            except (PermissionError, OSError) as e:
                print(f"[InputHandler] Cannot grab {path!r}: {e}")

        if not devices:
            return

        fd_map = {d.fd: d for d in devices}

        try:
            while self._running:
                readable, _, _ = select.select(fd_map.keys(), [], [], 0.2)
                for fd in readable:
                    dev = fd_map[fd]
                    try:
                        for event in dev.read():
                            if event.type != ecodes.EV_KEY:
                                continue
                            if event.value != 1:  # key-down only
                                continue
                            if event.code == ecodes.KEY_VOLUMEUP:
                                self.volume_up.emit()
                            elif event.code == ecodes.KEY_VOLUMEDOWN:
                                self.volume_down.emit()
                            elif event.code == ecodes.KEY_MUTE:
                                self.volume_mute.emit()
                    except OSError:
                        self._running = False
                        break
        finally:
            for d in devices:
                try:
                    d.ungrab()
                    d.close()
                    print(f"[InputHandler] Released {d.path!r}")
                except OSError:
                    pass
