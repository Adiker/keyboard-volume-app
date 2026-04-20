from __future__ import annotations
from pathlib import Path
from PyQt6.QtWidgets import QSystemTrayIcon, QMenu, QApplication
from PyQt6.QtGui import QIcon, QAction, QActionGroup
from PyQt6.QtCore import QObject, pyqtSignal

from src.config import Config
from src.volume_controller import VolumeController, AudioApp

ICON_PATH = Path(__file__).parent.parent / "resources" / "icon.png"


class TrayApp(QObject):
    # Emitted when user picks a different audio app
    app_changed = pyqtSignal(str)  # app name
    # Emitted when user requests device change
    device_change_requested = pyqtSignal()
    # Emitted when settings saved (so OSD can reload styles)
    settings_changed = pyqtSignal()

    def __init__(self, config: Config, volume_ctrl: VolumeController, parent=None):
        super().__init__(parent)
        self.config = config
        self.volume_ctrl = volume_ctrl

        self._tray = QSystemTrayIcon(QIcon(str(ICON_PATH)))
        self._tray.setToolTip("Keyboard Volume App")
        self._build_menu()
        self._tray.show()

    # --- menu ---

    def _build_menu(self):
        menu = QMenu()

        # Audio app selection section
        self._app_group = QActionGroup(menu)
        self._app_group.setExclusive(True)
        self._app_actions: dict[str, QAction] = {}

        self._apps_section_title = menu.addSection("Aplikacja audio")
        self._refresh_app_list(menu)

        menu.addSeparator()

        refresh_act = menu.addAction("Odśwież listę aplikacji")
        refresh_act.triggered.connect(lambda: self._refresh_app_list(menu, rebuild=True))

        menu.addSeparator()

        device_act = menu.addAction("Zmień urządzenie wejściowe...")
        device_act.triggered.connect(self.device_change_requested.emit)

        settings_act = menu.addAction("Ustawienia...")
        settings_act.triggered.connect(self._open_settings)

        menu.addSeparator()

        quit_act = menu.addAction("Wyjście")
        quit_act.triggered.connect(QApplication.quit)

        self._menu = menu
        self._tray.setContextMenu(menu)

    def _refresh_app_list(self, menu: QMenu, rebuild: bool = False):
        # Remove old app actions
        for act in self._app_actions.values():
            self._app_group.removeAction(act)
            menu.removeAction(act)
        self._app_actions.clear()

        apps = self.volume_ctrl.list_apps()

        # Insert actions after the section title
        insert_before = None
        actions = menu.actions()
        # Find separator after section (first separator)
        for i, act in enumerate(actions):
            if act.isSeparator():
                insert_before = act
                break

        for app in apps:
            act = QAction(app.name, menu, checkable=True)
            act.setData(app.name)
            self._app_group.addAction(act)
            self._app_actions[app.name] = act
            if insert_before:
                menu.insertAction(insert_before, act)
            else:
                menu.addAction(act)
            if app.name == self.config.selected_app:
                act.setChecked(True)
            act.triggered.connect(lambda checked, name=app.name: self._select_app(name))

        # If saved app no longer exists, pick first available
        if self.config.selected_app not in self._app_actions and apps:
            self._select_app(apps[0].name)

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

    # --- public ---

    def current_app(self) -> str | None:
        return self.config.selected_app
