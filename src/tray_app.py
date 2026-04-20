from __future__ import annotations
from pathlib import Path
from PyQt6.QtWidgets import QSystemTrayIcon, QMenu, QApplication
from PyQt6.QtGui import QIcon, QAction, QActionGroup
from PyQt6.QtCore import QObject, pyqtSignal

from src.config import Config
from src.volume_controller import VolumeController
from src.i18n import tr

ICON_PATH = Path(__file__).parent.parent / "resources" / "icon.png"


class TrayApp(QObject):
    app_changed = pyqtSignal(str)
    device_change_requested = pyqtSignal()
    settings_changed = pyqtSignal()

    def __init__(self, config: Config, volume_ctrl: VolumeController, parent=None):
        super().__init__(parent)
        self.config = config
        self.volume_ctrl = volume_ctrl

        self._tray = QSystemTrayIcon(QIcon(str(ICON_PATH)))
        self._tray.setToolTip("Keyboard Volume App")
        self._menu = QMenu()
        self._tray.setContextMenu(self._menu)
        self._app_group = QActionGroup(self._menu)
        self._app_group.setExclusive(True)
        self._app_actions: dict[str, QAction] = {}

        self._build_menu()
        self._tray.show()

    # --- menu ---

    def _build_menu(self):
        self._menu.clear()
        for act in self._app_group.actions():
            self._app_group.removeAction(act)
        self._app_actions.clear()

        self._menu.addSection(tr("tray.section.audio_app"))
        self._populate_app_list()

        self._menu.addSeparator()

        refresh_act = self._menu.addAction(tr("tray.action.refresh"))
        refresh_act.triggered.connect(self._on_refresh)

        self._menu.addSeparator()

        device_act = self._menu.addAction(tr("tray.action.change_device"))
        device_act.triggered.connect(self.device_change_requested.emit)

        settings_act = self._menu.addAction(tr("tray.action.settings"))
        settings_act.triggered.connect(self._open_settings)

        self._menu.addSeparator()

        quit_act = self._menu.addAction(tr("tray.action.quit"))
        quit_act.triggered.connect(QApplication.quit)

    def _populate_app_list(self):
        apps = self.volume_ctrl.list_apps()

        # Find insertion point: before first separator
        insert_before = next(
            (a for a in self._menu.actions() if a.isSeparator()), None
        )

        for app in apps:
            act = QAction(app.name, self._menu, checkable=True)
            act.setData(app.name)
            self._app_group.addAction(act)
            self._app_actions[app.name] = act
            if insert_before:
                self._menu.insertAction(insert_before, act)
            else:
                self._menu.addAction(act)
            if app.name == self.config.selected_app:
                act.setChecked(True)
            act.triggered.connect(lambda checked, name=app.name: self._select_app(name))

        if self.config.selected_app not in self._app_actions and apps:
            self._select_app(apps[0].name)

    def _on_refresh(self):
        self.volume_ctrl.list_apps(force_refresh=True)
        self._build_menu()

    def _select_app(self, name: str):
        self.config.selected_app = name
        if name in self._app_actions:
            self._app_actions[name].setChecked(True)
        self.app_changed.emit(name)

    def _open_settings(self):
        from src.settings_dialog import SettingsDialog
        dlg = SettingsDialog(self.config)
        if dlg.exec():
            self.settings_changed.emit()

    def rebuild_menu(self):
        """Rebuild the tray menu — call after language change."""
        self._build_menu()

    def current_app(self) -> str | None:
        return self.config.selected_app
