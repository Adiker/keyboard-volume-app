from __future__ import annotations
from PyQt6.QtWidgets import (
    QApplication, QDialog, QFormLayout, QSpinBox,
    QPushButton, QHBoxLayout, QVBoxLayout,
    QDialogButtonBox, QColorDialog, QComboBox, QLabel,
)
from PyQt6.QtGui import QColor
from PyQt6.QtCore import Qt, pyqtSignal

from evdev import ecodes

from src.config import Config
from src.i18n import tr, set_language, LANGUAGES
from src.input_handler import KeyCaptureThread


class ColorButton(QPushButton):
    """Button that shows a color picker and stores the selected hex color."""

    def __init__(self, color: str, parent=None):
        super().__init__(parent)
        self.setFixedWidth(80)
        self.set_color(color)
        self.clicked.connect(self._pick)

    def set_color(self, hex_color: str):
        self._color = hex_color
        self.setStyleSheet(
            f"background-color: {hex_color}; border: 1px solid #888; border-radius: 3px;"
        )
        self.setText(hex_color)

    def color(self) -> str:
        return self._color

    def _pick(self):
        chosen = QColorDialog.getColor(QColor(self._color), self, "")
        if chosen.isValid():
            self.set_color(chosen.name())


class KeyCaptureDialog(QDialog):
    """
    Modal dialog that waits for one key press via evdev (exclusive grab).
    Grabs all sibling devices so the OS cannot intercept media keys.
    Press Escape or click Cancel to abort without capturing.
    """

    def __init__(self, device_path: str | None, parent=None):
        super().__init__(parent)
        self.setWindowTitle(tr("settings.hotkey.capture_title"))
        self.setWindowModality(Qt.WindowModality.ApplicationModal)
        self.setMinimumWidth(300)
        self._code: int | None = None
        self._thread: KeyCaptureThread | None = None

        layout = QVBoxLayout(self)
        label = QLabel(tr("settings.hotkey.capture_prompt"))
        label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(label)

        cancel_btn = QPushButton(tr("settings.hotkey.capture_cancel"))
        cancel_btn.clicked.connect(self._do_cancel)
        layout.addWidget(cancel_btn)

        if device_path:
            self._thread = KeyCaptureThread(device_path)
            self._thread.key_captured.connect(self._on_captured)
            self._thread.cancelled.connect(self._do_cancel)
            self._thread.start()

    def _on_captured(self, code: int):
        self._code = code
        self.accept()

    def _do_cancel(self):
        if self._thread and self._thread.isRunning():
            self._thread.cancel()
        self.reject()

    def closeEvent(self, event):
        if self._thread and self._thread.isRunning():
            self._thread.cancel()
        super().closeEvent(event)

    def captured_code(self) -> int | None:
        return self._code


class HotkeyCapture(QPushButton):
    """Button displaying the currently assigned key. Click to reassign."""

    def __init__(self, code: int, input_handler=None, parent=None):
        super().__init__(parent)
        self._code = code
        self._input_handler = input_handler
        self.setMinimumWidth(120)
        self._update_display()
        self.clicked.connect(self._capture)

    @staticmethod
    def _key_display_name(code: int) -> str:
        name = ecodes.KEY.get(code)
        if name is None:
            return str(code)
        if isinstance(name, (list, tuple)):
            name = name[0]
        return name[4:] if name.startswith("KEY_") else name

    def _update_display(self):
        self.setText(self._key_display_name(self._code))

    def evdev_code(self) -> int:
        return self._code

    def _capture(self):
        if self._input_handler:
            self._input_handler.stop()
        try:
            device_path = self._input_handler.device_path if self._input_handler else None
            dlg = KeyCaptureDialog(device_path, self)
            if dlg.exec() == QDialog.DialogCode.Accepted and dlg.captured_code() is not None:
                self._code = dlg.captured_code()
                self._update_display()
        finally:
            if self._input_handler:
                self._input_handler.restart()


class SettingsDialog(QDialog):
    # Emitted live as the user adjusts screen/x/y: (screen_idx, x, y)
    position_preview = pyqtSignal(int, int, int)

    def __init__(self, config: Config, input_handler=None, parent=None):
        super().__init__(parent)
        self.config = config
        self._input_handler = input_handler
        self.setWindowTitle(tr("settings.title"))
        self.setMinimumWidth(360)
        self.setWindowModality(Qt.WindowModality.ApplicationModal)
        self._build_ui()

    def _build_ui(self):
        layout = QVBoxLayout(self)
        form = QFormLayout()
        form.setLabelAlignment(Qt.AlignmentFlag.AlignRight)
        form.setSpacing(10)

        osd = self.config.osd

        # Language
        self._lang = QComboBox()
        for code, label in LANGUAGES.items():
            self._lang.addItem(label, code)
        current_idx = list(LANGUAGES.keys()).index(self.config.language)
        self._lang.setCurrentIndex(current_idx)
        form.addRow(tr("settings.language"), self._lang)

        # OSD screen
        self._screen = QComboBox()
        screens = QApplication.screens()
        primary = QApplication.primaryScreen()
        for i, scr in enumerate(screens):
            geo = scr.geometry()
            label = f"{i + 1}:  {geo.width()}×{geo.height()}"
            if scr is primary:
                label += f"  ({tr('settings.screen_primary')})"
            self._screen.addItem(label, i)
        saved_screen = osd.get("screen", 0)
        if saved_screen < len(screens):
            self._screen.setCurrentIndex(saved_screen)
        form.addRow(tr("settings.osd_screen"), self._screen)

        # OSD timeout
        self._timeout = QSpinBox()
        self._timeout.setRange(300, 10000)
        self._timeout.setSingleStep(100)
        self._timeout.setSuffix(" ms")
        self._timeout.setValue(osd["timeout_ms"])
        form.addRow(tr("settings.osd_timeout"), self._timeout)

        # OSD position (relative to selected screen)
        pos_row = QHBoxLayout()
        self._osd_x = QSpinBox()
        self._osd_x.setRange(0, 7680)
        self._osd_x.setValue(osd["x"])
        self._osd_x.setPrefix("X: ")
        self._osd_y = QSpinBox()
        self._osd_y.setRange(0, 4320)
        self._osd_y.setValue(osd["y"])
        self._osd_y.setPrefix("Y: ")
        pos_row.addWidget(self._osd_x)
        pos_row.addWidget(self._osd_y)
        form.addRow(tr("settings.osd_position"), pos_row)

        # Volume step
        self._step = QSpinBox()
        self._step.setRange(1, 50)
        self._step.setSuffix(" %")
        self._step.setValue(self.config.volume_step)
        form.addRow(tr("settings.volume_step"), self._step)

        # Colors
        self._color_bg = ColorButton(osd["color_bg"])
        form.addRow(tr("settings.color_bg"), self._color_bg)

        self._color_text = ColorButton(osd["color_text"])
        form.addRow(tr("settings.color_text"), self._color_text)

        self._color_bar = ColorButton(osd["color_bar"])
        form.addRow(tr("settings.color_bar"), self._color_bar)

        # --- Hotkeys section ---
        section_label = QLabel(tr("settings.hotkeys_section"))
        section_label.setStyleSheet("font-weight: bold; margin-top: 8px;")
        form.addRow(section_label)

        hotkeys = self.config.hotkeys
        self._hk_up = HotkeyCapture(hotkeys["volume_up"], self._input_handler)
        form.addRow(tr("settings.hotkey.volume_up"), self._hk_up)

        self._hk_down = HotkeyCapture(hotkeys["volume_down"], self._input_handler)
        form.addRow(tr("settings.hotkey.volume_down"), self._hk_down)

        self._hk_mute = HotkeyCapture(hotkeys["mute"], self._input_handler)
        form.addRow(tr("settings.hotkey.mute"), self._hk_mute)

        layout.addLayout(form)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(self._save_and_accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)

        # Live preview — emit whenever position fields change
        self._screen.currentIndexChanged.connect(self._emit_preview)
        self._osd_x.valueChanged.connect(self._emit_preview)
        self._osd_y.valueChanged.connect(self._emit_preview)

    def _emit_preview(self):
        self.position_preview.emit(
            self._screen.currentData(),
            self._osd_x.value(),
            self._osd_y.value(),
        )

    def _save_and_accept(self):
        lang_code = self._lang.currentData()
        self.config.language = lang_code
        set_language(lang_code)

        self.config.set_osd(
            screen=self._screen.currentData(),
            timeout_ms=self._timeout.value(),
            x=self._osd_x.value(),
            y=self._osd_y.value(),
            color_bg=self._color_bg.color(),
            color_text=self._color_text.color(),
            color_bar=self._color_bar.color(),
        )
        self.config.volume_step = self._step.value()

        self.config.set_hotkeys(
            volume_up=self._hk_up.evdev_code(),
            volume_down=self._hk_down.evdev_code(),
            mute=self._hk_mute.evdev_code(),
        )

        self.accept()
