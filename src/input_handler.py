from __future__ import annotations
import ctypes
import ctypes.util
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


# ---------------------------------------------------------------------------
# X11 structures and constants (ctypes)
# ---------------------------------------------------------------------------

_libX11 = None

def _get_libX11():
    global _libX11
    if _libX11 is None:
        name = ctypes.util.find_library("X11")
        if name:
            lib = ctypes.cdll.LoadLibrary(name)
            # Must be called before any other Xlib calls when multiple threads
            # use X11 — also prevents crashes from Qt's own X11 usage.
            lib.XInitThreads()
            # Set return types for functions that return pointers or non-int
            lib.XOpenDisplay.restype = ctypes.c_void_p
            lib.XOpenDisplay.argtypes = [ctypes.c_char_p]
            lib.XDefaultScreen.restype = ctypes.c_int
            lib.XDefaultScreen.argtypes = [ctypes.c_void_p]
            lib.XRootWindow.restype = ctypes.c_ulong
            lib.XRootWindow.argtypes = [ctypes.c_void_p, ctypes.c_int]
            lib.XConnectionNumber.restype = ctypes.c_int
            lib.XConnectionNumber.argtypes = [ctypes.c_void_p]
            lib.XGrabKey.restype = ctypes.c_int
            lib.XGrabKey.argtypes = [
                ctypes.c_void_p, ctypes.c_int, ctypes.c_uint,
                ctypes.c_ulong, ctypes.c_int, ctypes.c_int, ctypes.c_int,
            ]
            lib.XUngrabKey.restype = ctypes.c_int
            lib.XUngrabKey.argtypes = [
                ctypes.c_void_p, ctypes.c_int, ctypes.c_uint, ctypes.c_ulong,
            ]
            lib.XPending.restype = ctypes.c_int
            lib.XPending.argtypes = [ctypes.c_void_p]
            lib.XNextEvent.restype = ctypes.c_int
            lib.XNextEvent.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
            lib.XFlush.restype = ctypes.c_int
            lib.XFlush.argtypes = [ctypes.c_void_p]
            lib.XCloseDisplay.restype = ctypes.c_int
            lib.XCloseDisplay.argtypes = [ctypes.c_void_p]
            _libX11 = lib
    return _libX11


class _XKeyEvent(ctypes.Structure):
    _fields_ = [
        ("type",        ctypes.c_int),
        ("serial",      ctypes.c_ulong),
        ("send_event",  ctypes.c_int),
        ("display",     ctypes.c_void_p),
        ("window",      ctypes.c_ulong),
        ("root",        ctypes.c_ulong),
        ("subwindow",   ctypes.c_ulong),
        ("time",        ctypes.c_ulong),
        ("x",           ctypes.c_int),
        ("y",           ctypes.c_int),
        ("x_root",      ctypes.c_int),
        ("y_root",      ctypes.c_int),
        ("state",       ctypes.c_uint),
        ("keycode",     ctypes.c_uint),
        ("same_screen", ctypes.c_int),
    ]


# XEvent is 192 bytes on 64-bit Linux; use a raw buffer large enough.
class _XEvent(ctypes.Union):
    _fields_ = [
        ("type",    ctypes.c_int),
        ("xkey",    _XKeyEvent),
        ("pad",     ctypes.c_byte * 192),
    ]


_KeyPress = 2
_GrabModeAsync = 1
_Mod2Mask = 1 << 4   # NumLock
_LockMask = 1 << 1   # CapsLock
_Mod5Mask = 1 << 5   # ScrollLock

# Grab each key 8× — once per combination of the three "irrelevant" modifiers
# (NumLock, CapsLock, ScrollLock). This is the standard xbindkeys approach.
_IGNORED_MOD_COMBOS: list[int] = [
    mask
    for bits in range(8)
    for mask in [
        ((_Mod2Mask if bits & 1 else 0)
         | (_LockMask if bits & 2 else 0)
         | (_Mod5Mask if bits & 4 else 0))
    ]
]


class X11HotkeyThread(QThread):
    """
    Registers XGrabKey on the X11 root window for a set of evdev key codes
    and emits hotkey_triggered(evdev_code) whenever one fires.

    This covers keys that the evdev InputHandler cannot see because
    KWin/libinput holds an exclusive EVIOCGRAB on the main keyboard
    interface (e.g. F-keys, numpad, KEY_MUTE on the main matrix).
    """
    hotkey_triggered = pyqtSignal(int)

    def __init__(self, evdev_codes: list[int], parent=None):
        super().__init__(parent)
        self._codes = list(evdev_codes)
        self._running = True

    def stop_thread(self):
        self._running = False
        self.wait(2000)

    def run(self):
        lib = _get_libX11()
        if lib is None:
            print("[X11HotkeyThread] libX11 not found, X11 hotkeys disabled")
            return

        dpy = lib.XOpenDisplay(None)
        if not dpy:
            print("[X11HotkeyThread] XOpenDisplay failed")
            return

        screen = lib.XDefaultScreen(dpy)
        root = lib.XRootWindow(dpy, screen)
        conn_fd = lib.XConnectionNumber(dpy)

        # Register each hotkey for all combinations of ignored modifiers
        grabbed_codes = []
        for evdev_code in self._codes:
            x11_keycode = evdev_code + 8
            ok = True
            for mod in _IGNORED_MOD_COMBOS:
                ret = lib.XGrabKey(
                    dpy, x11_keycode, mod, root,
                    False, _GrabModeAsync, _GrabModeAsync,
                )
                if not ret:
                    ok = False
            if ok:
                grabbed_codes.append(x11_keycode)
                print(f"[X11HotkeyThread] Grabbed X11 keycode {x11_keycode} (evdev {evdev_code})")
            else:
                print(f"[X11HotkeyThread] XGrabKey failed for keycode {x11_keycode}")

        lib.XFlush(dpy)

        event = _XEvent()

        try:
            while self._running:
                # Check if events are pending via select
                readable, _, _ = select.select([conn_fd], [], [], 0.1)
                if not readable:
                    continue

                while lib.XPending(dpy):
                    lib.XNextEvent(dpy, ctypes.byref(event))
                    if event.type == _KeyPress:
                        kc = event.xkey.keycode
                        evdev_code = kc - 8
                        if evdev_code in self._codes:
                            self.hotkey_triggered.emit(evdev_code)
        finally:
            for kc in grabbed_codes:
                for mod in _IGNORED_MOD_COMBOS:
                    lib.XUngrabKey(dpy, kc, mod, root)
            lib.XCloseDisplay(dpy)
            print("[X11HotkeyThread] Stopped, all keys ungrabbed")


class KeyCaptureThread(QThread):
    """
    Grabs sibling devices of the given path and waits for one key-down
    event. Emits key_captured(code) with the raw evdev key code.
    KEY_ESC is treated as cancel — captured but not emitted, and
    cancelled() is emitted instead.
    """
    key_captured = pyqtSignal(int)
    cancelled = pyqtSignal()

    def __init__(self, device_path: str, parent=None):
        super().__init__(parent)
        self._device_path = device_path
        self._running = True

    def cancel(self):
        self._running = False
        self.wait(1000)

    def run(self):
        siblings = find_sibling_devices(self._device_path)
        devices: list[evdev.InputDevice] = []
        for path in siblings:
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
        self._x11: X11HotkeyThread | None = None
        # Per-code timestamp for dedup between evdev and X11 paths (100 ms window)
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
        self._start_x11()
        self.start()

    def _start_x11(self):
        """Start (or restart) the X11HotkeyThread for currently configured hotkeys."""
        if self._x11 is not None and self._x11.isRunning():
            self._x11.stop_thread()
            self._x11 = None

        codes = [self._key_up, self._key_down, self._key_mute]
        self._x11 = X11HotkeyThread(codes)
        self._x11.hotkey_triggered.connect(self._on_x11_hotkey)
        self._x11.start()

    def _on_x11_hotkey(self, evdev_code: int):
        now = time.monotonic() * 1000
        last = self._last_trigger_ms.get(evdev_code, 0.0)
        if now - last < 100:
            return
        self._last_trigger_ms[evdev_code] = now

        if evdev_code == self._key_up:
            self.volume_up.emit()
        elif evdev_code == self._key_down:
            self.volume_down.emit()
        elif evdev_code == self._key_mute:
            self.volume_mute.emit()

    def restart(self):
        """Restart grabbing the same device with the current hotkey configuration."""
        if self._device_path:
            self.start_device(self._device_path)

    def stop(self):
        self._running = False
        if self._x11 is not None and self._x11.isRunning():
            self._x11.stop_thread()
            self._x11 = None
        self.quit()
        self.wait(2000)

    def run(self):
        if not self._device_path:
            return

        siblings = find_sibling_devices(self._device_path)
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
                            if event.type != ecodes.EV_KEY:
                                continue
                            if event.value != 1:  # key-down only
                                continue
                            now = time.monotonic() * 1000
                            last = self._last_trigger_ms.get(event.code, 0.0)
                            if now - last < 100:
                                continue
                            self._last_trigger_ms[event.code] = now
                            if event.code == key_up:
                                self.volume_up.emit()
                            elif event.code == key_down:
                                self.volume_down.emit()
                            elif event.code == key_mute:
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
