from __future__ import annotations
import select
import time
import evdev
from evdev import ecodes
from PyQt6.QtCore import QThread, pyqtSignal


def find_sibling_devices(primary_path: str) -> list[str]:
    """
    Return all evdev devices that belong to the same physical device
    as primary_path (matched by phys prefix) AND expose at least one
    EV_KEY capability.
    We grab every sibling node so that no key event from this keyboard
    can be intercepted by the OS/desktop, regardless of which keys are
    configured as hotkeys.
    """
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
            has_keys = bool(d.capabilities().get(ecodes.EV_KEY))
            d.close()
            if phys.startswith(phys_prefix) and has_keys:
                siblings.append(path)
        except (PermissionError, OSError):
            pass

    return siblings if siblings else [primary_path]


def find_capture_devices(primary_path: str | None) -> list[str]:
    """
    Return devices to use during key-capture dialog.

    Includes:
    - siblings of the selected device (if provided)
    - all EV_KEY devices as fallback
    """
    result: list[str] = []
    seen: set[str] = set()

    if primary_path:
        for path in find_sibling_devices(primary_path):
            if path not in seen:
                seen.add(path)
                result.append(path)

    for path in evdev.list_devices():
        if path in seen:
            continue
        try:
            d = evdev.InputDevice(path)
            has_keys = bool(d.capabilities().get(ecodes.EV_KEY))
            d.close()
            if has_keys:
                seen.add(path)
                result.append(path)
        except (PermissionError, OSError):
            pass

    return result


def find_hotkey_devices(primary_path: str | None, hotkey_codes: list[int]) -> list[tuple[str, bool]]:
    """
    Return devices for runtime hotkey handling.

    Return devices that should be grabbed for runtime hotkey handling.

    Includes:
    - siblings of selected device (exclusive grab = True)
    - any EV_KEY device exposing at least one configured hotkey code
      (passive read, no grab = False)

    This avoids globally blocking typing when hotkeys live on the main keyboard
    event node.
    """
    result: list[tuple[str, bool]] = []
    seen: set[str] = set()
    wanted = set(hotkey_codes)

    if primary_path:
        for path in find_sibling_devices(primary_path):
            if path not in seen:
                seen.add(path)
                result.append((path, True))

    for path in evdev.list_devices():
        if path in seen:
            continue
        try:
            d = evdev.InputDevice(path)
            keys = set(d.capabilities().get(ecodes.EV_KEY, []))
            d.close()
            if keys & wanted:
                seen.add(path)
                result.append((path, False))
        except (PermissionError, OSError):
            pass

    return result


class KeyCaptureThread(QThread):
    """
    Grabs sibling devices of the given path and waits for one key-down
    event. Emits key_captured(code) with the raw evdev key code.
    KEY_ESC is treated as cancel — captured but not emitted, and
    cancelled() is emitted instead.
    """
    key_captured = pyqtSignal(int)
    cancelled = pyqtSignal()

    def __init__(self, device_path: str | None, parent=None):
        super().__init__(parent)
        self._device_path = device_path
        self._running = True

    def cancel(self):
        self._running = False
        self.wait(1000)

    def run(self):
        candidates = find_capture_devices(self._device_path)
        devices: list[evdev.InputDevice] = []
        for path in candidates:
            try:
                d = evdev.InputDevice(path)
                d.grab()
                devices.append(d)
                print(f"[CaptureThread] Grabbed {path!r} ({d.name})")
            except (PermissionError, OSError) as e:
                print(f"[CaptureThread] Cannot grab {path!r}: {e}")

        if not devices:
            self.cancelled.emit()
            return

        fd_map = {d.fd: d for d in devices}
        try:
            while self._running:
                readable, _, _ = select.select(fd_map.keys(), [], [], 0.1)
                for fd in readable:
                    dev = fd_map[fd]
                    try:
                        for event in dev.read():
                            if event.type != ecodes.EV_KEY:
                                continue
                            if event.value != 1:  # key-down only
                                continue
                            self._running = False
                            if event.code == ecodes.KEY_ESC:
                                self.cancelled.emit()
                            else:
                                self.key_captured.emit(event.code)
                            return
                    except OSError:
                        self._running = False
                        self.cancelled.emit()
                        return
        finally:
            for d in devices:
                try:
                    d.ungrab()
                    d.close()
                    print(f"[CaptureThread] Released {d.path!r}")
                except OSError:
                    pass


class InputHandler(QThread):
    volume_up = pyqtSignal()
    volume_down = pyqtSignal()
    volume_mute = pyqtSignal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._device_path: str | None = None
        self._running = False
        self._key_up: int = ecodes.KEY_VOLUMEUP
        self._key_down: int = ecodes.KEY_VOLUMEDOWN
        self._key_mute: int = ecodes.KEY_MUTE
        # Per-code timestamp for debounce (100 ms window)
        self._last_trigger_ms: dict[int, float] = {}

    @property
    def device_path(self) -> str | None:
        return self._device_path

    def set_hotkeys(self, up: int, down: int, mute: int):
        self._key_up = up
        self._key_down = down
        self._key_mute = mute

    def current_hotkeys(self) -> tuple[int, int, int]:
        return self._key_up, self._key_down, self._key_mute

    def start_device(self, path: str):
        """Stop current device (if any) and start reading from new path."""
        self.stop()
        self._device_path = path
        self._running = True
        self.start()

    def restart(self):
        """Restart grabbing the same device with the current hotkey configuration."""
        if self._device_path:
            self.start_device(self._device_path)

    def stop(self):
        self._running = False
        self.quit()
        self.wait(2000)

    def run(self):
        if not self._device_path:
            return

        hotkeys = [self._key_up, self._key_down, self._key_mute]
        candidates = find_hotkey_devices(self._device_path, hotkeys)
        devices: list[evdev.InputDevice] = []
        grabbed_fds: set[int] = set()
        uinput_devices: dict[int, evdev.UInput] = {}  # fd → UInput re-injector

        for path, exclusive_grab in candidates:
            try:
                d = evdev.InputDevice(path)
                if exclusive_grab:
                    d.grab()
                    grabbed_fds.add(d.fd)
                    try:
                        _UINPUT_TYPES = frozenset({
                            ecodes.EV_KEY, ecodes.EV_REL, ecodes.EV_ABS,
                            ecodes.EV_MSC, ecodes.EV_REP,
                        })
                        caps = {k: v for k, v in d.capabilities().items()
                                if k in _UINPUT_TYPES}
                        ui = evdev.UInput(
                            events=caps,
                            name=f"kva-reinj-{d.name}",
                            vendor=d.info.vendor,
                            product=d.info.product,
                            version=d.info.version,
                            bustype=d.info.bustype,
                        )
                        uinput_devices[d.fd] = ui
                        print(f"[InputHandler] Created uinput for {path!r}")
                    except (PermissionError, OSError) as e:
                        print(f"[InputHandler] Cannot create uinput for {path!r}: {e}")
                devices.append(d)
                mode = "grabbed" if exclusive_grab else "passive"
                print(f"[InputHandler] Opened {path!r} ({d.name}) [{mode}]")
            except (PermissionError, OSError) as e:
                print(f"[InputHandler] Cannot grab {path!r}: {e}")

        if not devices:
            return

        fd_map = {d.fd: d for d in devices}
        key_up = self._key_up
        key_down = self._key_down
        key_mute = self._key_mute

        try:
            while self._running:
                readable, _, _ = select.select(fd_map.keys(), [], [], 0.2)
                for fd in readable:
                    dev = fd_map[fd]
                    try:
                        for event in dev.read():
                            is_grabbed = dev.fd in grabbed_fds
                            ui = uinput_devices.get(dev.fd)

                            if event.type == ecodes.EV_KEY:
                                is_hotkey = event.code in (key_up, key_down, key_mute)
                                if is_hotkey:
                                    # swallow all states (down/repeat/up) for assigned hotkeys
                                    if event.value == 1:  # key-down only for triggering
                                        now = time.monotonic() * 1000
                                        last = self._last_trigger_ms.get(event.code, 0.0)
                                        if now - last >= 100:
                                            self._last_trigger_ms[event.code] = now
                                            if event.code == key_up:
                                                self.volume_up.emit()
                                            elif event.code == key_down:
                                                self.volume_down.emit()
                                            elif event.code == key_mute:
                                                self.volume_mute.emit()
                                    continue  # never re-inject hotkey events
                                # non-hotkey EV_KEY → re-inject for grabbed devices
                                if is_grabbed and ui is not None:
                                    try:
                                        ui.write(event.type, event.code, event.value)
                                    except OSError:
                                        pass
                            else:
                                # EV_SYN, EV_MSC, EV_REP, etc. → re-inject for grabbed devices;
                                # EV_SYN flows naturally after EV_KEY written above
                                if is_grabbed and ui is not None:
                                    try:
                                        ui.write(event.type, event.code, event.value)
                                    except OSError:
                                        pass
                    except OSError:
                        self._running = False
                        break
        finally:
            for ui in uinput_devices.values():
                try:
                    ui.close()
                except OSError:
                    pass
            uinput_devices.clear()
            for d in devices:
                try:
                    if d.fd in grabbed_fds:
                        d.ungrab()
                    d.close()
                    print(f"[InputHandler] Released {d.path!r}")
                except OSError:
                    pass
