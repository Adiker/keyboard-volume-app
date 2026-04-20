from __future__ import annotations
import evdev
from evdev import ecodes
from PyQt6.QtCore import QThread, pyqtSignal


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
        try:
            dev = evdev.InputDevice(self._device_path)
            dev.grab()
        except (PermissionError, OSError) as e:
            print(f"[InputHandler] Cannot open {self._device_path}: {e}")
            return

        try:
            for event in dev.read_loop():
                if not self._running:
                    break
                if event.type != ecodes.EV_KEY:
                    continue
                # Fire only on key-down (value == 1)
                if event.value != 1:
                    continue
                if event.code == ecodes.KEY_VOLUMEUP:
                    self.volume_up.emit()
                elif event.code == ecodes.KEY_VOLUMEDOWN:
                    self.volume_down.emit()
                elif event.code == ecodes.KEY_MUTE:
                    self.volume_mute.emit()
        except OSError:
            pass
        finally:
            try:
                dev.ungrab()
                dev.close()
            except OSError:
                pass
