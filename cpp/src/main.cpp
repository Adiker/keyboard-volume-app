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
#include "mprisclient.h"
#include "screenutils.h"
#include "windowtracker.h"
#include "audioapp.h"
#include "waylandstate.h"

#ifdef HAVE_WAYLAND_CLIENT
#include <wayland-client.h>
#include <cstring>
#endif

#include <QApplication>
#include <QCommandLineParser>
#include <QCursor>
#include <QMessageBox>
#include <QObject>
#include <QTimer>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <memory>

// ─── Wayland state ───────────────────────────────────────────────────────────
// Defined here; declared extern in waylandstate.h.
// Set before QApplication, read-only afterwards (no mutex needed).
bool g_nativeWayland = false;

#ifdef HAVE_WAYLAND_CLIENT
// Probe Wayland registry for zwlr_layer_shell_v1 before QApplication is created.
// Opens a temporary wl_display connection, does one roundtrip, then disconnects.
static bool probeLayerShell()
{
    wl_display* dpy = wl_display_connect(nullptr);
    if (!dpy) return false;

    wl_registry* reg = wl_display_get_registry(dpy);
    bool found = false;

    static const wl_registry_listener lst = {
        // global announce
        [](void* d, wl_registry*, uint32_t, const char* iface, uint32_t)
        {
            if (strcmp(iface, "zwlr_layer_shell_v1") == 0) *static_cast<bool*>(d) = true;
        },
        // global remove — not needed
        [](void*, wl_registry*, uint32_t) {},
    };

    wl_registry_add_listener(reg, &lst, &found);
    wl_display_roundtrip(dpy); // synchronous: receives all current globals
    wl_registry_destroy(reg);
    wl_display_disconnect(dpy);
    return found;
}
#endif

// ─── App ──────────────────────────────────────────────────────────────────────
// Root coordinator — wires all modules via Qt signals, mirrors App in main.py.
class App : public QObject
{
    Q_OBJECT
  public:
    App() : m_config(std::make_unique<Config>()) {}

    ~App() override
    {
        delete m_osd;
    }

    Config* config() const
    {
        return m_config.get();
    }

    void init()
    {
        setLanguage(m_config->language());

        m_volumeCtrl = new VolumeController(this);
        m_osd = new OSDWindow(m_config.get());
        m_osd->initLayerShell(); // no-op unless g_nativeWayland && HAVE_LAYER_SHELL_QT
        m_input = new InputHandler(this);
        m_tray = new TrayApp(m_config.get(), m_volumeCtrl, m_input, this);
        m_mpris = new MprisClient(m_config.get(), this);

        connectSignals();
        initDevice();
        initDbus();
        initWindowTracker();
    }

    void cleanup()
    {
        m_input->stop();
        stopWindowTracker();
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
        connect(m_volumeCtrl, &VolumeController::volumeChanged, this,
                [this](const QString& app, double vol, bool muted)
                { m_osd->showVolume(app, vol, muted); });

        // Input → volume (per-profile dispatch)
        connect(m_input, &InputHandler::volume_up, this,
                [this](const QString& id) { changeVolume(id, +1); });
        connect(m_input, &InputHandler::volume_down, this,
                [this](const QString& id) { changeVolume(id, -1); });
        connect(m_input, &InputHandler::volume_mute, this,
                [this](const QString& id) { onMute(id); });
        connect(m_input, &InputHandler::ducking_toggle, this,
                [this](const QString& id) { onDuckingToggle(id); });
        connect(m_input, &InputHandler::show_volume, this,
                [this](const QString& id) { onShowVolume(id); });

        // Tray
        connect(m_tray, &TrayApp::deviceChangeRequested, this,
                [this]() { onDeviceChangeRequested(false); });
        connect(m_tray, &TrayApp::settingsChanged, m_osd, &OSDWindow::reloadStyles);
        connect(m_tray, &TrayApp::settingsChanged, m_tray, &TrayApp::rebuildMenu);
        connect(m_tray, &TrayApp::settingsChanged, this, &App::onHotkeysMaybeChanged);
        connect(m_tray, &TrayApp::settingsChanged, this, &App::onAutoSwitchMaybeChanged);
        connect(m_tray, &TrayApp::osdPreviewRequested, m_osd,
                [this](int s, int x, int y) { m_osd->showPreview(s, x, y); });
        connect(m_tray, &TrayApp::osdPreviewFinished, m_osd, &OSDWindow::hidePreview);
        connect(m_tray, &TrayApp::osdStylePreviewRequested, m_osd, &OSDWindow::applyPreviewColors);
        connect(m_tray, &TrayApp::osdPreviewHeldRequested, m_osd, &OSDWindow::showPreviewHeld);
        connect(m_tray, &TrayApp::osdPreviewReleased, m_osd, &OSDWindow::releasePreview);

        // App list cache for auto-switch matching
        connect(m_volumeCtrl, &VolumeController::appsReady, this,
                [this](QList<AudioApp> apps) { m_appCache = std::move(apps); });

        // MprisClient → OSDWindow progress row
        connect(m_mpris, &MprisClient::trackChanged, m_osd,
                [this](const QString& title, const QString& artist, qint64 lengthUs, bool canSeek)
                {
                    m_osd->updateTrack(title, artist, lengthUs, canSeek);
                    m_osd->setProgressVisible(true);
                });
        connect(m_mpris, &MprisClient::positionChanged, m_osd, &OSDWindow::updatePosition);
        connect(m_mpris, &MprisClient::noPlayer, m_osd,
                [this]() { m_osd->setProgressVisible(false); });

        // OSDWindow seek interaction → MprisClient
        connect(m_osd, &OSDWindow::seekStarted, m_mpris, [this]() { m_mpris->setSeeking(true); });
        connect(m_osd, &OSDWindow::seekFinished, m_mpris, [this]() { m_mpris->setSeeking(false); });
        connect(m_osd, &OSDWindow::seekRequested, m_mpris, &MprisClient::setPosition);

        // Settings change → reload MprisClient config
        connect(m_tray, &TrayApp::settingsChanged, m_mpris, &MprisClient::reload);
    }

    void initDevice()
    {
        m_input->setProfiles(m_config->profiles());
        if (!m_config->inputDevice().isEmpty())
        {
            m_input->startDevice(m_config->inputDevice());
        }
        else
        {
            onDeviceChangeRequested(/*startup=*/true);
        }
    }

    void onHotkeysMaybeChanged()
    {
        const QList<Profile> newProfiles = m_config->profiles();
        if (newProfiles == m_input->currentProfiles()) return;
        m_input->stop();
        m_input->setProfiles(newProfiles);
        if (!m_config->inputDevice().isEmpty()) m_input->startDevice(m_config->inputDevice());
    }

    void onAutoSwitchMaybeChanged()
    {
        if (m_config->autoProfileSwitch())
        {
            m_autoActiveApp.clear();
            startWindowTracker();
        }
        else
        {
            m_autoActiveApp.clear();
            stopWindowTracker();
        }
    }

    // Lookup profile by id; returns empty Profile when not found.
    Profile findProfile(const QString& profileId) const
    {
        for (const Profile& p : m_config->profiles())
        {
            if (p.id == profileId) return p;
        }
        return Profile{};
    }

    void changeVolume(const QString& profileId, int direction)
    {
        const QString app = effectiveApp(profileId);
        if (app.isEmpty()) return;
        double step = m_config->volumeStep() / 100.0;
        m_volumeCtrl->changeVolume(app, direction * step); // async → volumeChanged signal
    }

    void onMute(const QString& profileId)
    {
        const QString app = effectiveApp(profileId);
        if (app.isEmpty()) return;
        m_volumeCtrl->toggleMute(app); // async → volumeChanged signal
    }

    void onDuckingToggle(const QString& profileId)
    {
        const QString app = effectiveApp(profileId);
        if (app.isEmpty()) return;
        const Profile p = findProfile(profileId);
        if (!p.ducking.enabled || !p.ducking.hotkey.isAssigned()) return;
        m_volumeCtrl->toggleDucking(app, p.ducking.volume / 100.0);
    }

    void onShowVolume(const QString& profileId)
    {
        const QString app = effectiveApp(profileId);
        if (app.isEmpty()) return;
        m_volumeCtrl->queryVolume(app); // async → volumeChanged → OSD
    }

    void onDeviceChangeRequested(bool startup)
    {
        const QPoint anchor = QCursor::pos();
        DeviceSelectorDialog dlg(m_config.get(), startup);
        centerDialogOnScreenAt(&dlg, anchor);
        int res = dlg.exec();
        if (res == QDialog::Accepted && !dlg.selectedPath().isEmpty())
        {
            m_input->startDevice(dlg.selectedPath());
        }
        else if (startup)
        {
            QMessageBox::warning(nullptr, ::tr(QStringLiteral("warn.no_device.title")),
                                 ::tr(QStringLiteral("warn.no_device.text")));
        }
    }

    void initDbus()
    {
        m_dbus = new DbusInterface(m_config.get(), m_volumeCtrl, m_tray, this);
        connect(m_tray, &TrayApp::settingsChanged, m_dbus, &DbusInterface::reloadProfiles);
        connect(m_tray, &TrayApp::settingsChanged, m_dbus, &DbusInterface::reloadProgressEnabled);
        connect(m_dbus, &DbusInterface::progressEnabledChanged, m_osd,
                [this](bool) { m_osd->reloadStyles(); });
        connect(m_dbus, &DbusInterface::progressEnabledChanged, m_mpris,
                [this](bool) { m_mpris->reload(); });

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

    void initWindowTracker()
    {
        if (!m_config->autoProfileSwitch()) return;
        startWindowTracker();
    }

    void startWindowTracker()
    {
        if (m_windowTracker && m_windowTracker->isRunning()) return;
        if (!m_windowTracker)
        {
            m_windowTracker = new WindowTracker(this);
            connect(m_windowTracker, &WindowTracker::focusedBinaryChanged, this,
                    &App::onFocusedBinaryChanged);
            connect(m_windowTracker, &WindowTracker::error, this,
                    [](const QString& msg) { qWarning() << "[WindowTracker]" << msg; });
        }
        m_windowTracker->start();
    }

    void stopWindowTracker()
    {
        if (m_windowTracker)
        {
            m_windowTracker->stop();
            delete m_windowTracker;
            m_windowTracker = nullptr;
        }
        m_autoActiveApp.clear();
    }

    void onFocusedBinaryChanged(const QString& binary)
    {
        if (!m_config->autoProfileSwitch() || binary.isEmpty())
        {
            m_autoActiveApp.clear();
            return;
        }

        // Match binary name against PipeWire app cache
        const QString matchedApp = matchBinaryToApp(binary);
        if (matchedApp.isEmpty())
        {
            m_autoActiveApp.clear();
            return;
        }

        // Check if this app belongs to an auto-switch-enabled profile
        const Profile matchedProfile = m_config->findProfileByApp(matchedApp);
        if (matchedProfile.id.isEmpty())
        {
            m_autoActiveApp.clear();
            return;
        }

        // Only switch if the auto-detected app differs from current
        if (m_autoActiveApp == matchedApp) return;
        m_autoActiveApp = matchedApp;
    }

    QString matchBinaryToApp(const QString& binary) const
    {
        const QString lower = binary.toLower();
        for (const AudioApp& app : m_appCache)
        {
            if (app.name.toLower().contains(lower) || lower.contains(app.name.toLower()) ||
                app.binary.toLower().contains(lower) || lower.contains(app.binary.toLower()))
                return app.name;
        }
        return {};
    }

    QString effectiveApp(const QString& profileId) const
    {
        if (m_config->autoProfileSwitch() && !m_autoActiveApp.isEmpty()) return m_autoActiveApp;
        return findProfile(profileId).app;
    }

    std::unique_ptr<Config> m_config;
    VolumeController* m_volumeCtrl = nullptr;
    OSDWindow* m_osd = nullptr; // QWidget, no parent — deleted in ~App()
    InputHandler* m_input = nullptr;
    TrayApp* m_tray = nullptr;
    MprisClient* m_mpris = nullptr;

    DbusInterface* m_dbus = nullptr;
    QObject* m_mprisEndpoint = nullptr;

    WindowTracker* m_windowTracker = nullptr;
    QList<AudioApp> m_appCache;
    QString m_autoActiveApp;
};

// ─── main() ───────────────────────────────────────────────────────────────────

#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif

int main(int argc, char* argv[])
{
    // Wayland session detection and OSD positioning strategy:
    //
    //   Eligible when WAYLAND_DISPLAY is set, XDG_SESSION_TYPE=wayland, and
    //   QT_QPA_PLATFORM is either unset or explicitly "wayland":
    //
    //   a) zwlr_layer_shell_v1 available (Sway, Hyprland, KDE Plasma ≥5.27):
    //      → g_nativeWayland = true; Qt uses the Wayland platform (auto-detected
    //        or already set by the user). OSDWindow::initLayerShell() configures
    //        the surface via LayerShellQt. Do NOT set
    //        QT_WAYLAND_SHELL_INTEGRATION=layer-shell globally — that would make
    //        ALL windows (dialogs etc.) layer surfaces.
    //
    //   b) Protocol not available (GNOME, etc.) or LayerShellQt not compiled in:
    //      → If QT_QPA_PLATFORM was unset, force xcb (XWayland fallback).
    //        If the user explicitly set QT_QPA_PLATFORM=wayland, leave it alone —
    //        overriding the user's explicit choice would be unexpected.
    {
        const QByteArray waylandDisplay = qgetenv("WAYLAND_DISPLAY");
        const QByteArray sessionType = qgetenv("XDG_SESSION_TYPE");
        const QByteArray qtPlatform = qgetenv("QT_QPA_PLATFORM");

        const bool onWaylandSession = !waylandDisplay.isEmpty() && sessionType == "wayland";
        const bool qtPlatformUnset = qtPlatform.isEmpty();
        // Also match "wayland;xcb" and similar Qt semicolon fallback lists
        // (https://doc.qt.io/qt-6/qpa.html#selecting-a-qpa-plugin): Qt will try
        // the first entry first, so "wayland;..." is effectively a Wayland session.
        const bool qtPlatformWayland = qtPlatform == "wayland" || qtPlatform.startsWith("wayland;");

        // Probe when the session is Wayland and Qt will (or already does) use the
        // Wayland platform. Skip when the user pinned QT_QPA_PLATFORM to something
        // else (e.g. "xcb") — that is an intentional override we must respect.
        if (onWaylandSession && (qtPlatformUnset || qtPlatformWayland))
        {
#if defined(HAVE_WAYLAND_CLIENT) && defined(HAVE_LAYER_SHELL_QT)
            if (probeLayerShell())
            {
                g_nativeWayland = true;
            }
            else if (qtPlatformUnset)
            {
                // Layer-shell unavailable and no explicit platform set: fall back to
                // XWayland so QWidget::move() works for OSD positioning.
                qputenv("QT_QPA_PLATFORM", "xcb");
            }
            // qtPlatformWayland + no layer-shell: leave the user's setting intact.
#else
            if (qtPlatformUnset)
            {
                qputenv("QT_QPA_PLATFORM", "xcb");
            }
#endif
        }
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

    if (parser.isSet(QStringLiteral("help")))
    {
        fputs(qPrintable(parser.helpText()), stdout);
        return 0;
    }
    if (parser.isSet(QStringLiteral("version")))
    {
        printf("%s %s\n", qPrintable(qtApp.applicationName()),
               qPrintable(qtApp.applicationVersion()));
        return 0;
    }

    // ── Singleton check ──────────────────────────────────────────────────────
    {
        auto bus = QDBusConnection::sessionBus();
        if (bus.interface() &&
            bus.interface()->isServiceRegistered(QStringLiteral("org.keyboardvolumeapp")))
        {
            qWarning() << "Another instance is already running (D-Bus name taken). Exiting.";
            return 1;
        }
    }

    App app;

    // First-run wizard: guide the user through language and device selection.
    if (app.config()->isFirstRun())
    {
        const QPoint anchor = QCursor::pos();
        FirstRunWizard wizard(app.config());
        centerDialogOnScreenAt(&wizard, anchor);
        if (wizard.exec() != QWizard::Accepted) return 0;
    }

    app.init();

    int exitCode = qtApp.exec();
    app.cleanup();
    return exitCode;
}

#include "main.moc"
