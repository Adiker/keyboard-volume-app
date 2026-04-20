from __future__ import annotations
import evdev
from PyQt6.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QLabel,
    QListWidget, QListWidgetItem, QPushButton, QDialogButtonBox,
)
from PyQt6.QtCore import Qt

from src.config import Config
from src.i18n import tr


def _get_volume_devices() -> list[tuple[str, str]]:
    """Return list of (path, description) for devices that have volume keys."""
    result = []
    volume_keys = {evdev.ecodes.KEY_VOLUMEUP, evdev.ecodes.KEY_VOLUMEDOWN}
    for path in sorted(evdev.list_devices()):
        try:
            dev = evdev.InputDevice(path)
            caps = dev.capabilities()
            keys = set(caps.get(evdev.ecodes.EV_KEY, []))
            if volume_keys & keys:
                result.append((path, f"{dev.name}  [{path}]"))
            dev.close()
        except (PermissionError, OSError):
            pass
    return result


class DeviceSelectorDialog(QDialog):
    def __init__(self, config: Config, first_run: bool = False, parent=None):
        super().__init__(parent)
        self.config = config
        self.selected_path: str | None = None

        title_key = "device.title.first_run" if first_run else "device.title"
        self.setWindowTitle(tr(title_key))
        self.setMinimumWidth(460)
        self.setWindowModality(Qt.WindowModality.ApplicationModal)

        self._build_ui()
        self._populate()

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setSpacing(8)

        layout.addWidget(QLabel(tr("device.label")))

        self._list = QListWidget()
        self._list.setAlternatingRowColors(True)
        self._list.itemDoubleClicked.connect(self.accept)
        layout.addWidget(self._list)

        btn_row = QHBoxLayout()
        refresh_btn = QPushButton(tr("device.btn.refresh"))
        refresh_btn.clicked.connect(self._populate)
        btn_row.addWidget(refresh_btn)
        btn_row.addStretch()
        layout.addLayout(btn_row)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)

    def _populate(self):
        self._list.clear()
        devices = _get_volume_devices()
        if not devices:
            self._list.addItem(QListWidgetItem(tr("device.no_devices")))
            return
        for path, description in devices:
            item = QListWidgetItem(description)
            item.setData(Qt.ItemDataRole.UserRole, path)
            self._list.addItem(item)
            if path == self.config.input_device:
                self._list.setCurrentItem(item)
        if self._list.currentItem() is None:
            self._list.setCurrentRow(0)

    def accept(self):
        item = self._list.currentItem()
        if item:
            path = item.data(Qt.ItemDataRole.UserRole)
            if path:
                self.selected_path = path
                self.config.input_device = path
        super().accept()
