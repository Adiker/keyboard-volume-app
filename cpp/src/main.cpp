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
#include "screenutils.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCursor>
#include <QMessageBox>
#include <QObject>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <memory>

// ─── App ──────────────────────────────────────────────────────────────────────
// Root coordinator — wires all modules via Qt signals, mirrors App in main.py.
class App : public QObject
{
    Q_OBJECT
public:
    App()
        : m_config(std::make_unique<Config>())
    {}

    ~App() override { delete m_osd; }

    Config *config() const { return m_config.get(); }

    void init()
    {
        setLanguage(m_config->language());

        m_volumeCtrl = new VolumeController(this);
        m_osd        = new OSDWindow(m_config.get());
        m_input      = new InputHandler(this);
        m_tray       = new TrayApp(m_config.get(), m_volumeCtrl, m_input, this);

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

        // Input → volume (per-profile dispatch)
        connect(m_input, &InputHandler::volume_up, this,
                [this](const QString &id){ changeVolume(id, +1); });
        connect(m_input, &InputHandler::volume_down, this,
                [this](const QString &id){ changeVolume(id, -1); });
        connect(m_input, &InputHandler::volume_mute, this,
                [this](const QString &id){ onMute(id); });

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
        m_input->setProfiles(m_config->profiles());
        if (!m_config->inputDevice().isEmpty()) {
            m_input->startDevice(m_config->inputDevice());
        } else {
            onDeviceChangeRequested(/*startup=*/true);
        }
    }

    void onHotkeysMaybeChanged()
    {
        const QList<Profile> newProfiles = m_config->profiles();
        if (newProfiles == m_input->currentProfiles())
            return;
        m_input->stop();
        m_input->setProfiles(newProfiles);
        if (!m_config->inputDevice().isEmpty())
            m_input->startDevice(m_config->inputDevice());
    }

    // Lookup profile by id; returns empty Profile when not found.
    Profile findProfile(const QString &profileId) const
    {
        for (const Profile &p : m_config->profiles()) {
            if (p.id == profileId) return p;
        }
        return Profile{};
    }

    void changeVolume(const QString &profileId, int direction)
    {
        const Profile p = findProfile(profileId);
        if (p.app.isEmpty()) return;
        double step = m_config->volumeStep() / 100.0;
        m_volumeCtrl->changeVolume(p.app, direction * step); // async → volumeChanged signal
    }

    void onMute(const QString &profileId)
    {
        const Profile p = findProfile(profileId);
        if (p.app.isEmpty()) return;
        m_volumeCtrl->toggleMute(p.app); // async → volumeChanged signal
    }

    void onDeviceChangeRequested(bool startup)
    {
        const QPoint anchor = QCursor::pos();
        DeviceSelectorDialog dlg(m_config.get(), startup);
        centerDialogOnScreenAt(&dlg, anchor);
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
        m_dbus = new DbusInterface(m_config.get(), m_volumeCtrl, m_tray, this);
        connect(m_tray, &TrayApp::settingsChanged,
                m_dbus, &DbusInterface::reloadProfiles);

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

    std::unique_ptr<Config> m_config;
    VolumeController *m_volumeCtrl  = nullptr;
    OSDWindow        *m_osd         = nullptr;  // QWidget, no parent — deleted in ~App()
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
    // Use parse() instead of process() — process() calls ::exit(0) which
    // skips destructors and triggers QThreadStorage warnings.
    parser.parse(qtApp.arguments());

    if (parser.isSet(QStringLiteral("help"))) {
        fputs(qPrintable(parser.helpText()), stdout);
        return 0;
    }
    if (parser.isSet(QStringLiteral("version"))) {
        printf("%s %s\n",
               qPrintable(qtApp.applicationName()),
               qPrintable(qtApp.applicationVersion()));
        return 0;
    }

    // ── Singleton check ──────────────────────────────────────────────────────
    {
        auto bus = QDBusConnection::sessionBus();
        if (bus.interface() && bus.interface()->isServiceRegistered(
                QStringLiteral("org.keyboardvolumeapp"))) {
            qWarning() << "Another instance is already running (D-Bus name taken). Exiting.";
            return 1;
        }
    }

    App app;

    // First-run wizard: guide the user through language and device selection.
    if (app.config()->isFirstRun()) {
        const QPoint anchor = QCursor::pos();
        FirstRunWizard wizard(app.config());
        centerDialogOnScreenAt(&wizard, anchor);
        if (wizard.exec() != QWizard::Accepted)
            return 0;
    }

    app.init();

    int exitCode = qtApp.exec();
    app.cleanup();
    return exitCode;
}

#include "main.moc"
