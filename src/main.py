from __future__ import annotations
import sys
from PyQt6.QtWidgets import QApplication, QMessageBox
from PyQt6.QtCore import QObject

from src.config import Config
from src.volume_controller import VolumeController
from src.osd_window import OSDWindow
from src.tray_app import TrayApp
from src.input_handler import InputHandler
from src.device_selector import DeviceSelectorDialog


class App(QObject):
    def __init__(self):
        super().__init__()
        self.config = Config()
        self.volume_ctrl = VolumeController()
        self.osd = OSDWindow(self.config)
        self.input_handler = InputHandler()
        self.tray = TrayApp(self.config, self.volume_ctrl)

        self._connect_signals()
        self._init_device()

    # --- setup ---

    def _connect_signals(self):
        # Input → volume change
        self.input_handler.volume_up.connect(self._on_volume_up)
        self.input_handler.volume_down.connect(self._on_volume_down)
        self.input_handler.volume_mute.connect(self._on_mute)

        # Tray
        self.tray.device_change_requested.connect(self._on_device_change_requested)
        self.tray.settings_changed.connect(self.osd.reload_styles)

    def _init_device(self):
        if self.config.input_device:
            self.input_handler.start_device(self.config.input_device)
        else:
            self._on_device_change_requested(startup=True)

    # --- volume actions ---

    def _on_volume_up(self):
        self._change_volume(+1)

    def _on_volume_down(self):
        self._change_volume(-1)

    def _change_volume(self, direction: int):
        app_name = self.tray.current_app()
        if not app_name:
            return
        step = self.config.volume_step / 100.0
        new_vol = self.volume_ctrl.change_volume(app_name, direction * step)
        if new_vol is not None:
            self.osd.show_volume(app_name, new_vol)

    def _on_mute(self):
        app_name = self.tray.current_app()
        if not app_name:
            return
        result = self.volume_ctrl.toggle_mute(app_name)
        if result is not None:
            muted, vol = result
            self.osd.show_volume(app_name, vol, muted=muted)

    # --- device change ---

    def _on_device_change_requested(self, startup: bool = False):
        dlg = DeviceSelectorDialog(self.config)
        if startup:
            dlg.setWindowTitle("Wybierz urządzenie wejściowe (pierwsze uruchomienie)")
        result = dlg.exec()
        if result and dlg.selected_path:
            self.input_handler.start_device(dlg.selected_path)
        elif startup:
            # User cancelled at startup — warn and continue without input
            QMessageBox.warning(
                None,
                "Brak urządzenia",
                "Nie wybrano urządzenia wejściowego.\n"
                "Możesz wybrać je później z menu tray → \"Zmień urządzenie wejściowe...\"",
            )

    def cleanup(self):
        self.input_handler.stop()
        self.volume_ctrl.close()


def main():
    qt_app = QApplication(sys.argv)
    qt_app.setQuitOnLastWindowClosed(False)

    controller = App()
    exit_code = qt_app.exec()

    controller.cleanup()
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
