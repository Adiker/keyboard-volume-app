#include "albumartcache.h"
#include "appmatcher.h"
#include "audioapp.h"
#include "config.h"
#include "dbusinterface.h"
#include "deviceselector.h"
#include "firstrunwizard.h"
#include "i18n.h"
#include "inputhandler.h"
#include "mprisclient.h"
#include "mprisinterface.h"
#include "osdwindow.h"
#include "screenutils.h"
#include "trayapp.h"
#include "volumecontroller.h"
#include "waylandstate.h"
#include "windowtracker.h"

#ifdef HAVE_WAYLAND_CLIENT
#include <wayland-client.h>
#endif

#include <QApplication>
#include <QCommandLineParser>
#include <QCursor>
#include <QMessageBox>
#include <QObject>
#include <QProcess>
#include <QSocketNotifier>
#include <QTimer>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <unistd.h>

// ─── Wayland state ───────────────────────────────────────────────────────────
// Defined here; declared extern in waylandstate.h.
// Set before QApplication, read-only afterwards (no mutex needed).
bool g_nativeWayland = false;

namespace
{

int g_signalPipe[2] = {-1, -1};

void handleUnixSignal(int signo)
{
    if (g_signalPipe[1] < 0) return;
    const char byte = static_cast<char>(signo);
    const ssize_t ignored = ::write(g_signalPipe[1], &byte, 1);
    (void)ignored;
}

void setFdFlag(int fd, int command, int flag)
{
    const int flags = fcntl(fd, command, 0);
    if (flags >= 0) fcntl(fd, command == F_GETFD ? F_SETFD : F_SETFL, flags | flag);
}

std::unique_ptr<QSocketNotifier> installUnixSignalHandlers(QObject* parent)
{
    if (::pipe(g_signalPipe) != 0)
    {
        qWarning() << "Could not install signal pipe:" << strerror(errno);
        return {};
    }

    setFdFlag(g_signalPipe[0], F_GETFL, O_NONBLOCK);
    setFdFlag(g_signalPipe[1], F_GETFL, O_NONBLOCK);
    setFdFlag(g_signalPipe[0], F_GETFD, FD_CLOEXEC);
    setFdFlag(g_signalPipe[1], F_GETFD, FD_CLOEXEC);

    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_handler = handleUnixSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);

    auto notifier =
        std::make_unique<QSocketNotifier>(g_signalPipe[0], QSocketNotifier::Read, parent);
    QObject::connect(notifier.get(), &QSocketNotifier::activated, parent,
                     []
                     {
                         char buffer[32];
                         while (::read(g_signalPipe[0], buffer, sizeof(buffer)) > 0)
                         {
                         }
                         qApp->quit();
                     });
    return notifier;
}

} // namespace

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
        m_albumArtCache = new AlbumArtCache(this);

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
        unregisterMprisEndpoint();
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

        // Input → media (global, MPRIS dispatch via MprisClient)
        connect(m_input, &InputHandler::media_play_pause, m_mpris, &MprisClient::playPause);
        connect(m_input, &InputHandler::media_next, m_mpris, &MprisClient::next);
        connect(m_input, &InputHandler::media_previous, m_mpris, &MprisClient::previous);
        connect(m_input, &InputHandler::media_stop, m_mpris, &MprisClient::stop);

        // Input → media keys OSD (off/action/full, independent of MPRIS dispatch)
        connect(m_input, &InputHandler::media_play_pause, this,
                [this]() { showMediaKeyOsd(::tr(QStringLiteral("osd.media_play_pause"))); });
        connect(m_input, &InputHandler::media_next, this,
                [this]() { showMediaKeyOsd(::tr(QStringLiteral("osd.media_next"))); });
        connect(m_input, &InputHandler::media_previous, this,
                [this]() { showMediaKeyOsd(::tr(QStringLiteral("osd.media_previous"))); });
        connect(m_input, &InputHandler::media_stop, this,
                [this]() { showMediaKeyOsd(::tr(QStringLiteral("osd.media_stop"))); });

        // Input → scene apply (global). Look up the scene by id and route the
        // per-target loop through VolumeController::applyScene.
        connect(m_input, &InputHandler::scene_apply, this,
                [this](const QString& sceneId)
                {
                    const AudioScene scene = m_config->findSceneById(sceneId);
                    if (!scene.id.isEmpty()) m_volumeCtrl->applyScene(scene);
                });

        // Tray
        connect(m_tray, &TrayApp::deviceChangeRequested, this,
                [this]() { onDeviceChangeRequested(false); });
        connect(m_tray, &TrayApp::settingsChanged, m_osd, &OSDWindow::reloadStyles);
        connect(m_tray, &TrayApp::settingsChanged, m_tray, &TrayApp::rebuildMenu);
        connect(m_tray, &TrayApp::settingsChanged, this, &App::onHotkeysMaybeChanged);
        connect(m_tray, &TrayApp::settingsChanged, this, &App::onAutoSwitchMaybeChanged);
        connect(m_tray, &TrayApp::settingsChanged, this,
                [this]() { m_lastActivatedProfileId.clear(); });
        connect(m_tray, &TrayApp::osdPreviewRequested, m_osd,
                [this](int s, int x, int y) { m_osd->showPreview(s, x, y); });
        connect(m_tray, &TrayApp::osdPreviewFinished, m_osd, &OSDWindow::hidePreview);
        connect(m_tray, &TrayApp::osdStylePreviewRequested, m_osd, &OSDWindow::applyPreviewColors);
        connect(m_tray, &TrayApp::osdScalePreviewRequested, m_osd, &OSDWindow::applyPreviewScale);
        connect(m_tray, &TrayApp::osdPreviewHeldRequested, m_osd, &OSDWindow::showPreviewHeld);
        connect(m_tray, &TrayApp::osdPreviewReleased, m_osd, &OSDWindow::releasePreview);

        // App list cache for auto-switch matching
        connect(m_volumeCtrl, &VolumeController::appsReady, this,
                [this](QList<AudioApp> apps) { m_appCache = std::move(apps); });

        // Sink hot-plug (USB headset connect/disconnect, default sink change)
        // clears the profile-activation guard so the very next hotkey press
        // re-applies the configured sink — addresses the "device appeared after
        // a failed first attempt" case without rerouting on every press.
        connect(m_volumeCtrl, &VolumeController::sinksReady, this,
                [this](const QList<SinkInfo>&) { m_lastActivatedProfileId.clear(); });

        // MprisClient → OSDWindow progress row
        connect(m_mpris, &MprisClient::trackChanged, m_osd,
                [this](const QString& title, const QString& artist, qint64 lengthUs, bool canSeek)
                {
                    const auto info = m_mpris->activePlayer();
                    m_osd->updateTrack(title, artist, info.album, lengthUs, canSeek);
                    m_osd->setProgressVisible(true);
                });
        connect(m_mpris, &MprisClient::activePlayerChanged, m_osd,
                [this](const MprisClient::PlayerInfo& info)
                {
                    // Capitalize first letter so {player} reads "Spotify", not
                    // the lowercase service suffix "spotify". Mirrors how end
                    // users refer to these apps.
                    QString name = info.displayName;
                    if (!name.isEmpty()) name[0] = name[0].toUpper();
                    m_osd->setPlayerName(name);
                });
        connect(m_mpris, &MprisClient::albumArtChanged, this,
                [this](const QString& url)
                {
                    if (m_albumArtCache && !url.isEmpty())
                        m_albumArtCache->request(url);
                    else if (m_osd)
                        m_osd->setAlbumArt(QPixmap{});
                });
        connect(m_mpris, &MprisClient::positionChanged, m_osd, &OSDWindow::updatePosition);
        connect(m_mpris, &MprisClient::noPlayer, m_osd,
                [this]()
                {
                    m_osd->setProgressVisible(false);
                    m_osd->setPlayerName(QString{});
                });

        // OSDWindow seek interaction → MprisClient
        connect(m_osd, &OSDWindow::seekStarted, m_mpris, [this]() { m_mpris->setSeeking(true); });
        connect(m_osd, &OSDWindow::seekFinished, m_mpris, [this]() { m_mpris->setSeeking(false); });
        connect(m_osd, &OSDWindow::seekRequested, m_mpris, &MprisClient::setPosition);

        // OSDWindow media control buttons → MprisClient
        connect(m_osd, &OSDWindow::playPauseRequested, m_mpris, &MprisClient::playPause);
        connect(m_osd, &OSDWindow::nextRequested, m_mpris, &MprisClient::next);
        connect(m_osd, &OSDWindow::previousRequested, m_mpris, &MprisClient::previous);

        // MprisClient playback status → OSDWindow play/pause button icon
        connect(m_mpris, &MprisClient::playbackStatusChanged, m_osd,
                &OSDWindow::updatePlaybackStatus);

        // Settings change → reload MprisClient config
        connect(m_tray, &TrayApp::settingsChanged, m_mpris, &MprisClient::reload);

        // AlbumArtCache → OSDWindow
        connect(m_albumArtCache, &AlbumArtCache::ready, m_osd,
                [this](const QString& url, const QPixmap& pixmap)
                {
                    // Ignore stale results — only apply art if it still matches
                    // the active player's current art URL.
                    if (m_mpris && m_mpris->activePlayer().artUrl == url)
                        m_osd->setAlbumArt(pixmap);
                });
    }

    void initDevice()
    {
        m_input->setProfiles(m_config->profiles());
        m_input->setMediaHotkeys(m_config->mediaHotkeys());
        m_input->setScenes(m_config->scenes());
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
        const MediaHotkeyConfig newMedia = m_config->mediaHotkeys();
        const QList<AudioScene> newScenes = m_config->scenes();
        const bool profilesChanged = newProfiles != m_input->currentProfiles();
        const bool mediaChanged = newMedia != m_input->currentMediaHotkeys();
        const bool scenesChanged = newScenes != m_input->currentScenes();
        if (!profilesChanged && !mediaChanged && !scenesChanged) return;
        m_input->stop();
        if (profilesChanged) m_input->setProfiles(newProfiles);
        if (mediaChanged) m_input->setMediaHotkeys(newMedia);
        if (scenesChanged) m_input->setScenes(newScenes);
        if (!m_config->inputDevice().isEmpty()) m_input->startDevice(m_config->inputDevice());
    }

    void onAutoSwitchMaybeChanged()
    {
        if (m_config->autoProfileSwitch())
        {
            m_autoActiveApp =
                ::validateStickyAutoProfileTarget(m_autoActiveApp, m_config->profiles());
            if (m_mpris) m_mpris->setPreferredApp(m_autoActiveApp);
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

    // Route a profile's apps to its configured sink — runs once per profile-id
    // transition so repeated hotkey presses don't spam PA. The guard is cleared
    // on settingsChanged and on sinksReady so a freshly plugged USB device or a
    // sink edit in Settings triggers an automatic re-route on the next press.
    void activateProfile(const Profile& p)
    {
        if (p.id.isEmpty() || !m_volumeCtrl) return;
        if (p.id == m_lastActivatedProfileId) return;
        m_lastActivatedProfileId = p.id;
        if (p.sink.isEmpty()) return;
        for (const QString& app : p.apps)
        {
            if (app.isEmpty()) continue;
            m_volumeCtrl->setAppSink(app, p.sink);
        }
    }

    void changeVolume(const QString& profileId, int direction)
    {
        const QString app = effectiveApp(profileId);
        if (app.isEmpty()) return;
        double step = m_config->volumeStep() / 100.0;
        // Limits follow the *target* profile — when auto-switch redirects the
        // hotkey to a focused app belonging to a different profile, we must use
        // that profile's vol_min/vol_max, not the hotkey-emitting profile's.
        const Profile p = effectiveProfile(profileId);
        activateProfile(p);
        // async → volumeChanged signal; clamped to per-profile [vol_min, vol_max].
        m_volumeCtrl->changeVolume(app, direction * step, p.volMin / 100.0, p.volMax / 100.0);
    }

    void onMute(const QString& profileId)
    {
        const QString app = effectiveApp(profileId);
        if (app.isEmpty()) return;
        activateProfile(effectiveProfile(profileId));
        m_volumeCtrl->toggleMute(app); // async → volumeChanged signal
    }

    void onDuckingToggle(const QString& profileId)
    {
        const QString app = effectiveApp(profileId);
        if (app.isEmpty()) return;
        const Profile p = findProfile(profileId);
        if (!p.ducking.enabled || !p.ducking.hotkey.isAssigned()) return;
        activateProfile(effectiveProfile(profileId));
        m_volumeCtrl->toggleDucking(app, p.ducking.volume / 100.0);
    }

    void onShowVolume(const QString& profileId)
    {
        const QString app = effectiveApp(profileId);
        if (app.isEmpty()) return;
        activateProfile(effectiveProfile(profileId));
        m_volumeCtrl->queryVolume(app); // async → volumeChanged → OSD
    }

    void showMediaKeyOsd(const QString& actionLabel)
    {
        const MediaKeysOsdMode mode = m_config->osd().mediaKeysOsdMode;
        if (mode == MediaKeysOsdMode::Off) return;
        if (mode == MediaKeysOsdMode::Action)
        {
            m_osd->showMediaAction(actionLabel);
            return;
        }

        QString app;
        if (m_config->autoProfileSwitch() && !m_autoActiveApp.isEmpty())
        {
            app = m_autoActiveApp;
        }
        else
        {
            const QList<Profile> profiles = m_config->profiles();
            if (!profiles.isEmpty())
            {
                app = profiles.first().primaryApp();
            }
        }

        if (app.isEmpty())
        {
            m_osd->showMediaAction(actionLabel);
            return;
        }

        m_volumeCtrl->queryVolume(app); // async → volumeChanged → full OSD
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

    void registerMprisEndpoint()
    {
        if (m_mprisRegistered) return;
        auto bus = QDBusConnection::sessionBus();
        if (bus.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), m_mprisEndpoint,
                               QDBusConnection::ExportAdaptors |
                                   QDBusConnection::ExportScriptableSlots |
                                   QDBusConnection::ExportScriptableSignals |
                                   QDBusConnection::ExportScriptableProperties))
        {
            if (!bus.registerService(QStringLiteral("org.mpris.MediaPlayer2.keyboardvolumeapp")))
            {
                qWarning() << "MPRIS: failed to register service";
                bus.unregisterObject(QStringLiteral("/org/mpris/MediaPlayer2"));
                return;
            }
            m_mprisRegistered = true;
        }
        else
        {
            qWarning() << "MPRIS: failed to register object";
        }
    }

    void unregisterMprisEndpoint()
    {
        if (!m_mprisRegistered) return;
        auto bus = QDBusConnection::sessionBus();
        bus.unregisterObject(QStringLiteral("/org/mpris/MediaPlayer2"));
        bus.unregisterService(QStringLiteral("org.mpris.MediaPlayer2.keyboardvolumeapp"));
        m_mprisRegistered = false;
    }

    void onMprisExposureChanged()
    {
        if (m_config->osd().exposeMpris)
            registerMprisEndpoint();
        else
            unregisterMprisEndpoint();
    }

    void initDbus()
    {
        m_dbus = new DbusInterface(m_config.get(), m_volumeCtrl, this);
        m_dbus->setMprisClient(m_mpris);
        connect(m_tray, &TrayApp::appChanged, m_dbus, &DbusInterface::onActiveAppChanged);
        connect(m_tray, &TrayApp::settingsChanged, m_dbus, &DbusInterface::reloadProfiles);
        connect(m_tray, &TrayApp::settingsChanged, m_dbus, &DbusInterface::reloadProgressEnabled);
        connect(m_tray, &TrayApp::settingsChanged, m_dbus, &DbusInterface::reloadAutoProfileSwitch);
        connect(m_dbus, &DbusInterface::autoProfileSwitchChanged, this,
                &App::onAutoSwitchMaybeChanged);
        connect(m_tray, &TrayApp::settingsChanged, this, &App::onMprisExposureChanged);
        connect(m_dbus, &DbusInterface::progressEnabledChanged, m_osd,
                [this](bool) { m_osd->reloadStyles(); });
        connect(m_dbus, &DbusInterface::progressEnabledChanged, m_mpris,
                [this](bool) { m_mpris->reload(); });

        auto bus = QDBusConnection::sessionBus();

        // Custom interface (always active)
        if (bus.registerObject(QStringLiteral("/org/keyboardvolumeapp"), m_dbus,
                               QDBusConnection::ExportScriptableSignals |
                                   QDBusConnection::ExportScriptableProperties |
                                   QDBusConnection::ExportScriptableSlots))
        {
            bus.registerService(QStringLiteral("org.keyboardvolumeapp"));
        }

        // MPRIS interface — create adaptors always, but only register the D-Bus
        // service when expose_mpris is enabled (default: off)
        m_mprisEndpoint = new QObject(this);
        new MprisRootAdaptor(m_dbus, m_mprisEndpoint);
        new MprisPlayerAdaptor(m_dbus, m_mprisEndpoint);
        if (m_config->osd().exposeMpris) registerMprisEndpoint();
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
        // Auto-switch is off or shutting down: drop the sticky target.
        m_autoActiveApp.clear();
        if (m_mpris) m_mpris->setPreferredApp(QString{});
    }

    void onFocusedBinaryChanged(const QString& binary)
    {
        if (!m_config->autoProfileSwitch())
        {
            m_autoActiveApp.clear();
            if (m_mpris) m_mpris->setPreferredApp(QString{});
            return;
        }

        m_autoActiveApp = ::resolveStickyAutoProfileTarget(binary, m_appCache, m_config->profiles(),
                                                           m_autoActiveApp);
        if (m_mpris) m_mpris->setPreferredApp(m_autoActiveApp);
        if (!m_autoActiveApp.isEmpty())
        {
            const Profile matched = m_config->findProfileByApp(m_autoActiveApp);
            if (!matched.id.isEmpty()) activateProfile(matched);
        }
    }

    QString effectiveApp(const QString& profileId) const
    {
        if (m_config->autoProfileSwitch() && !m_autoActiveApp.isEmpty()) return m_autoActiveApp;
        return findProfile(profileId).primaryApp();
    }

    // Mirrors effectiveApp: when auto-switch redirects to a focused app, return
    // the profile that owns that app (so per-profile limits / settings track the
    // target, not the hotkey-emitting profile). Falls back to the hotkey profile
    // when auto-switch is off or no match is found.
    Profile effectiveProfile(const QString& profileId) const
    {
        if (m_config->autoProfileSwitch() && !m_autoActiveApp.isEmpty())
        {
            const Profile matched = m_config->findProfileByApp(m_autoActiveApp);
            if (!matched.id.isEmpty()) return matched;
        }
        return findProfile(profileId);
    }

    std::unique_ptr<Config> m_config;
    VolumeController* m_volumeCtrl = nullptr;
    OSDWindow* m_osd = nullptr; // QWidget, no parent — deleted in ~App()
    InputHandler* m_input = nullptr;
    TrayApp* m_tray = nullptr;
    MprisClient* m_mpris = nullptr;
    AlbumArtCache* m_albumArtCache = nullptr;

    DbusInterface* m_dbus = nullptr;
    QObject* m_mprisEndpoint = nullptr;
    bool m_mprisRegistered = false;

    WindowTracker* m_windowTracker = nullptr;
    QList<AudioApp> m_appCache;
    QString m_autoActiveApp;
    // Last profile id whose sink we routed — guards activateProfile() so the
    // sink is moved once per profile transition, not on every hotkey press.
    QString m_lastActivatedProfileId;
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
    auto signalNotifier = installUnixSignalHandlers(&qtApp);

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
    const bool restartRequested = qtApp.property("keyboardVolumeApp.restartRequested").toBool();
    app.cleanup();
    if (restartRequested)
    {
        QStringList args = qtApp.arguments();
        if (!args.isEmpty()) args.removeFirst();
        if (!QProcess::startDetached(qtApp.applicationFilePath(), args))
        {
            qWarning() << "Could not restart application after config import.";
            return 1;
        }
    }
    return exitCode;
}

#include "main.moc"
