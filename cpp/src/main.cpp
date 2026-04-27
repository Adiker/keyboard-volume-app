#include "config.h"
#include "i18n.h"
#include "volumecontroller.h"
#include "osdwindow.h"
#include "trayapp.h"
#include "inputhandler.h"
#include "deviceselector.h"
#include "firstrunwizard.h"
#include "dbusinterface.h"
#include "mprisinterface.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QObject>
#include <QDBusConnection>

// ─── App ──────────────────────────────────────────────────────────────────────
// Root coordinator — wires all modules via Qt signals, mirrors App in main.py.
class App : public QObject
{
    Q_OBJECT
public:
    App()
    {
        m_config = new Config;
    }

    Config *config() const { return m_config; }

    void init()
    {
        setLanguage(m_config->language());

        m_volumeCtrl = new VolumeController(this);
        m_osd        = new OSDWindow(m_config);
        m_input      = new InputHandler(this);
        m_tray       = new TrayApp(m_config, m_volumeCtrl, m_input, this);

        connectSignals();
        initDevice();
        initDbus();
    }

    void cleanup()
    {
        m_input->stop();
        m_volumeCtrl->close();

        auto bus = QDBusConnection::sessionBus();
        bus.unregisterObject(QStringLiteral("/org/keyboardvolumeapp"));
        bus.unregisterService(QStringLiteral("org.keyboardvolumeapp"));
        bus.unregisterObject(QStringLiteral("/org/mpris/MediaPlayer2"));
        bus.unregisterService(QStringLiteral("org.mpris.MediaPlayer2.keyboardvolumeapp"));
    }

private:
    void connectSignals()
    {
        // Volume result (async) → OSD
        connect(m_volumeCtrl, &VolumeController::volumeChanged,
                this, [this](const QString &app, double vol, bool muted) {
            m_osd->showVolume(app, vol, muted);
        });

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
        double step = m_config->volumeStep() / 100.0;
        m_volumeCtrl->changeVolume(app, direction * step); // async → volumeChanged signal
    }

    void onMute()
    {
        const QString app = m_tray->currentApp();
        if (app.isEmpty()) return;
        m_volumeCtrl->toggleMute(app); // async → volumeChanged signal
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

    void initDbus()
    {
        m_dbus = new DbusInterface(m_config, m_volumeCtrl, m_tray, this);

        auto bus = QDBusConnection::sessionBus();

        // Custom interface
        if (bus.registerObject(QStringLiteral("/org/keyboardvolumeapp"), m_dbus,
                QDBusConnection::ExportScriptableSignals |
                QDBusConnection::ExportScriptableProperties |
                QDBusConnection::ExportScriptableSlots))
        {
            bus.registerService(QStringLiteral("org.keyboardvolumeapp"));
        }

        // MPRIS interface
        m_mprisEndpoint = new QObject(this);
        new MprisRootAdaptor(m_dbus, m_mprisEndpoint);
        new MprisPlayerAdaptor(m_dbus, m_mprisEndpoint);
        if (bus.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), m_mprisEndpoint,
                QDBusConnection::ExportAdaptors |
                QDBusConnection::ExportScriptableSlots |
                QDBusConnection::ExportScriptableSignals |
                QDBusConnection::ExportScriptableProperties))
        {
            bus.registerService(QStringLiteral("org.mpris.MediaPlayer2.keyboardvolumeapp"));
        }
    }

    Config           *m_config      = nullptr;
    VolumeController *m_volumeCtrl  = nullptr;
    OSDWindow        *m_osd         = nullptr;
    InputHandler     *m_input       = nullptr;
    TrayApp          *m_tray        = nullptr;

    DbusInterface    *m_dbus         = nullptr;
    QObject          *m_mprisEndpoint = nullptr;
};

// ─── main() ───────────────────────────────────────────────────────────────────

#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif

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
    qtApp.setApplicationVersion(QStringLiteral(APP_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription("Keyboard volume controller with OSD");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(qtApp);

    App app;

    // First-run wizard: guide the user through language and device selection.
    if (app.config()->isFirstRun()) {
        FirstRunWizard wizard(app.config());
        if (wizard.exec() != QWizard::Accepted)
            return 0;
    }

    app.init();

    int exitCode = qtApp.exec();
    app.cleanup();
    return exitCode;
}

#include "main.moc"
