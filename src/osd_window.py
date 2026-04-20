from __future__ import annotations
from PyQt6.QtWidgets import QWidget, QLabel, QProgressBar, QVBoxLayout
from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtGui import QColor

from src.config import Config


class OSDWindow(QWidget):
    def __init__(self, config: Config):
        super().__init__()
        self.config = config
        self._hide_timer = QTimer(self)
        self._hide_timer.setSingleShot(True)
        self._hide_timer.timeout.connect(self.hide)

        self._build_ui()
        self._apply_styles()

    # --- UI construction ---

    def _build_ui(self):
        self.setWindowFlags(
            Qt.WindowType.FramelessWindowHint
            | Qt.WindowType.WindowStaysOnTopHint
            | Qt.WindowType.Tool
            | Qt.WindowType.BypassWindowManagerHint
        )
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setFixedSize(220, 70)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(10, 8, 10, 8)
        layout.setSpacing(4)

        self._label_name = QLabel("", self)
        self._label_name.setAlignment(Qt.AlignmentFlag.AlignCenter)

        self._bar = QProgressBar(self)
        self._bar.setRange(0, 100)
        self._bar.setTextVisible(False)
        self._bar.setFixedHeight(10)

        self._label_pct = QLabel("", self)
        self._label_pct.setAlignment(Qt.AlignmentFlag.AlignCenter)

        layout.addWidget(self._label_name)
        layout.addWidget(self._bar)
        layout.addWidget(self._label_pct)

    def _apply_styles(self):
        osd = self.config.osd
        bg = osd["color_bg"].lstrip("#")
        text = osd["color_text"].lstrip("#")
        bar = osd["color_bar"].lstrip("#")

        self.setStyleSheet(f"""
            QWidget {{
                background-color: #{bg};
                border-radius: 8px;
            }}
            QLabel {{
                color: #{text};
                background: transparent;
            }}
            QLabel#name_label {{
                font-size: 11pt;
                font-weight: bold;
                font-family: 'Segoe UI', 'Noto Sans', sans-serif;
            }}
            QLabel#pct_label {{
                font-size: 9pt;
                font-family: 'Segoe UI', 'Noto Sans', sans-serif;
            }}
            QProgressBar {{
                background-color: #333333;
                border: none;
                border-radius: 3px;
            }}
            QProgressBar::chunk {{
                background-color: #{bar};
                border-radius: 3px;
            }}
        """)
        self._label_name.setObjectName("name_label")
        self._label_pct.setObjectName("pct_label")
        # Re-apply after setting object names
        self._label_name.setStyleSheet(
            f"font-size: 11pt; font-weight: bold; color: #{text}; background: transparent;"
        )
        self._label_pct.setStyleSheet(
            f"font-size: 9pt; color: #{text}; background: transparent;"
        )

    # --- public API ---

    def show_volume(self, app_name: str, volume: float, muted: bool = False):
        """Display OSD. volume is 0.0–1.0."""
        osd = self.config.osd
        pct = round(volume * 100)

        self._label_name.setText(app_name)
        self._bar.setValue(pct)
        self._label_pct.setText(f"{pct}%  🔇" if muted else f"{pct}%")

        self._move_to(osd["x"], osd["y"])
        self.show()
        self.raise_()
        # Re-apply via native QWindow after show() — Wayland creates the surface
        # only after show(), so setPosition() here is more reliable than move() before.
        wh = self.windowHandle()
        if wh:
            wh.setPosition(osd["x"], osd["y"])

        self._hide_timer.start(osd["timeout_ms"])

    def reload_styles(self):
        """Call after config changes to refresh colors and position."""
        self._apply_styles()
        osd = self.config.osd
        self._move_to(osd["x"], osd["y"])
        if self.isVisible():
            wh = self.windowHandle()
            if wh:
                wh.setPosition(osd["x"], osd["y"])

    def _move_to(self, x: int, y: int):
        self.move(x, y)
        self.setGeometry(x, y, self.width(), self.height())
