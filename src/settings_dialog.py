from __future__ import annotations
from PyQt6.QtWidgets import (
    QApplication, QDialog, QFormLayout, QSpinBox,
    QPushButton, QHBoxLayout, QVBoxLayout,
    QDialogButtonBox, QColorDialog, QComboBox,
)
from PyQt6.QtGui import QColor
from PyQt6.QtCore import Qt, pyqtSignal

from src.config import Config
from src.i18n import tr, set_language, LANGUAGES


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


class SettingsDialog(QDialog):
    # Emitted live as the user adjusts screen/x/y: (screen_idx, x, y)
    position_preview = pyqtSignal(int, int, int)

    def __init__(self, config: Config, parent=None):
        super().__init__(parent)
        self.config = config
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
        self.accept()
