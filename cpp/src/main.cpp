#include "config.h"
#include "i18n.h"
#include "volumecontroller.h"
#include "osdwindow.h"
#include "trayapp.h"
#include "inputhandler.h"
#include "deviceselector.h"

#include <QApplication>
#include <QMessageBox>
#include <QObject>

// ─── App ──────────────────────────────────────────────────────────────────────
// Root coordinator — wires all modules via Qt signals, mirrors App in main.py.
class App : public QObject
{
    Q_OBJECT
public:
    App()
    {
        m_config = new Config;
        setLanguage(m_config->language());

        m_volumeCtrl = new VolumeController(this);
        m_osd        = new OSDWindow(m_config);
        m_input      = new InputHandler(this);
        m_tray       = new TrayApp(m_config, m_volumeCtrl, m_input, this);

        connectSignals();
        initDevice();
    }

    void cleanup()
    {
        m_input->stop();
        m_volumeCtrl->close();
    }

private:
    void connectSignals()
    {
        // Input → volume
        connect(m_input, &InputHandler::volume_up,   this, &App::onVolumeUp);
        connect(m_input, &InputHandler::volume_down, this, &App::onVolumeDown);
        connect(m_input, &InputHandler::volume_mute, this, &App::onMute);

        // Tray
        connect(m_tray, &TrayApp::deviceChangeRequested, this,
                [this]() { onDeviceChangeRequested(false); });
        connect(m_tray, &TrayApp::settingsChanged, m_osd,  &OSDWindow::reloadStyles);
        connect(m_tray, &TrayApp::settingsChanged, m_tray, &TrayApp::rebuildMenu);
        connect(m_tray, &TrayApp::settingsChanged, this,   &App::onHotkeysMaybeChanged);
        connect(m_tray, &TrayApp::osdPreviewRequested,
                m_osd, [this](int s, int x, int y){ m_osd->showPreview(s, x, y); });
        connect(m_tray, &TrayApp::osdPreviewFinished,
                m_osd, &OSDWindow::hidePreview);
        connect(m_tray, &TrayApp::osdStylePreviewRequested,
                m_osd, &OSDWindow::applyPreviewColors);
        connect(m_tray, &TrayApp::osdPreviewHeldRequested,
                m_osd, &OSDWindow::showPreviewHeld);
        connect(m_tray, &TrayApp::osdPreviewReleased,
                m_osd, &OSDWindow::releasePreview);
    }

    void initDevice()
    {
        HotkeyConfig hks = m_config->hotkeys();
        m_input->setHotkeys(hks.volumeUp, hks.volumeDown, hks.mute);
        if (!m_config->inputDevice().isEmpty()) {
            m_input->startDevice(m_config->inputDevice());
        } else {
            onDeviceChangeRequested(/*startup=*/true);
        }
    }

    void onHotkeysMaybeChanged()
    {
        HotkeyConfig hks = m_config->hotkeys();
        auto [up, down, mute] = m_input->currentHotkeys();
        if (up == hks.volumeUp && down == hks.volumeDown && mute == hks.mute)
            return;
        m_input->stop();
        m_input->setHotkeys(hks.volumeUp, hks.volumeDown, hks.mute);
        if (!m_config->inputDevice().isEmpty())
            m_input->startDevice(m_config->inputDevice());
    }

    void onVolumeUp()   { changeVolume(+1); }
    void onVolumeDown() { changeVolume(-1); }

    void changeVolume(int direction)
    {
        const QString app = m_tray->currentApp();
        if (app.isEmpty()) return;
        double step  = m_config->volumeStep() / 100.0;
        auto   newVol = m_volumeCtrl->changeVolume(app, direction * step);
        if (newVol)
            m_osd->showVolume(app, *newVol);
    }

    void onMute()
    {
        const QString app = m_tray->currentApp();
        if (app.isEmpty()) return;
        auto result = m_volumeCtrl->toggleMute(app);
        if (result) {
            auto [muted, vol] = *result;
            m_osd->showVolume(app, vol, muted);
        }
    }

    void onDeviceChangeRequested(bool startup)
    {
        DeviceSelectorDialog dlg(m_config, startup);
        int res = dlg.exec();
        if (res == QDialog::Accepted && !dlg.selectedPath().isEmpty()) {
            m_input->startDevice(dlg.selectedPath());
        } else if (startup) {
            QMessageBox::warning(nullptr,
                ::tr(QStringLiteral("warn.no_device.title")),
                ::tr(QStringLiteral("warn.no_device.text")));
        }
    }

    Config           *m_config      = nullptr;
    VolumeController *m_volumeCtrl  = nullptr;
    OSDWindow        *m_osd         = nullptr;
    InputHandler     *m_input       = nullptr;
    TrayApp          *m_tray        = nullptr;
};

// ─── main() ───────────────────────────────────────────────────────────────────
int main(int argc, char *argv[])
{
    // On Wayland, Qt cannot position windows via move() — the compositor ignores it.
    // Force XWayland so the OSD overlay appears at the user-configured coordinates.
    // Only applied automatically; user can override with QT_QPA_PLATFORM=wayland.
    const char *waylandDisplay = qgetenv("WAYLAND_DISPLAY").constData();
    const char *sessionType    = qgetenv("XDG_SESSION_TYPE").constData();
    const char *qtPlatform     = qgetenv("QT_QPA_PLATFORM").constData();

    if (waylandDisplay && waylandDisplay[0]
        && qstrcmp(sessionType, "wayland") == 0
        && (!qtPlatform || !qtPlatform[0]))
    {
        qputenv("QT_QPA_PLATFORM", "xcb");
    }

    QApplication qtApp(argc, argv);
    qtApp.setQuitOnLastWindowClosed(false);

    App app;
    int exitCode = qtApp.exec();
    app.cleanup();
    return exitCode;
}

#include "main.moc"
