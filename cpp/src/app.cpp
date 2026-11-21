#include "app.h"
#include "albumartcache.h"
#include "appmatcher.h"
#include "dbusinterface.h"
#include "deviceselector.h"
#include "i18n.h"
#include "mprisclient.h"
#include "mprisinterface.h"
#include "osdwindow.h"
#include "screenutils.h"
#include "trayapp.h"
#include "volumecontroller.h"
#include "windowtracker.h"

#include <QCursor>
#include <QDBusConnection>
#include <QDebug>
#include <QMessageBox>
#include <QPixmap>

OsdLayoutInputConfig osdLayoutInputFromConfig(const Config* config)
{
    OsdLayoutInputConfig input;
    const OsdConfig osd = config->osd();
    input.positionControlsEnabled = osd.positionControlsEnabled;
    input.positionKeyboardEnabled = osd.positionKeyboardEnabled;
    input.hotkeys = osd.layoutHotkeys;
    return input;
}

App::App(std::unique_ptr<Config> config, QObject* parent)
    : QObject(parent), m_config(config ? std::move(config) : std::make_unique<Config>())
{
}

App::~App()
{
    delete m_osd;
}

void App::init(const AppInitOptions& options)
{
    if (m_initStarted)
    {
        qWarning() << "App::init() can only be called once per App instance";
        return;
    }
    m_initStarted = true;

    m_startInputDevice = options.startInputDevice;
    m_startWindowTracker = options.startWindowTracker;
    m_registerDbus = options.registerDbus;
    m_dbusServiceName = options.dbusServiceName;
    m_mprisServiceName = options.mprisServiceName;

    setLanguage(m_config->language());

    m_volumeCtrl = options.volumeCtrl ? options.volumeCtrl : new VolumeController(this);
    m_volumeCtrl->setConfig(m_config.get());
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

void App::cleanup()
{
    if (m_input) m_input->stop();
    stopWindowTracker();
    if (m_volumeCtrl) m_volumeCtrl->close();

    if (m_registerDbus)
    {
        auto bus = QDBusConnection::sessionBus();
        bus.unregisterObject(QStringLiteral("/org/keyboardvolumeapp"));
        if (!m_dbusServiceName.isEmpty()) bus.unregisterService(m_dbusServiceName);
    }
    unregisterMprisEndpoint();
}

void App::connectSignals()
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
    connect(m_input, &InputHandler::volume_mute, this, [this](const QString& id) { onMute(id); });
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

    connect(m_osd, &OSDWindow::layoutKeysActiveChanged, m_input,
            &InputHandler::setOsdLayoutKeysActive);

    connect(m_input, &InputHandler::osdLayoutAction, this,
            [this](OsdLayoutAction action)
            {
                if (!m_config->osd().positionControlsEnabled || !m_osd) return;
                switch (action)
                {
                case OsdLayoutAction::SnapUp:
                    m_osd->snapUp();
                    break;
                case OsdLayoutAction::SnapDown:
                    m_osd->snapDown();
                    break;
                case OsdLayoutAction::SnapLeft:
                    m_osd->snapLeft();
                    break;
                case OsdLayoutAction::SnapRight:
                    m_osd->snapRight();
                    break;
                case OsdLayoutAction::ScaleUp:
                    m_osd->stepScaleUp();
                    break;
                case OsdLayoutAction::ScaleDown:
                    m_osd->stepScaleDown();
                    break;
                case OsdLayoutAction::None:
                    break;
                }
            });

    // Tray
    connect(m_tray, &TrayApp::deviceChangeRequested, this,
            [this]() { onDeviceChangeRequested(false); });
    connect(m_tray, &TrayApp::settingsChanged, m_osd, &OSDWindow::reloadStyles);
    connect(m_tray, &TrayApp::settingsChanged, m_tray, &TrayApp::rebuildMenu);
    connect(m_tray, &TrayApp::settingsChanged, this, &App::onHotkeysMaybeChanged);
    connect(m_tray, &TrayApp::settingsChanged, this, &App::onAutoSwitchMaybeChanged);
    connect(m_tray, &TrayApp::settingsChanged, this,
            [this]()
            {
                m_lastActivatedProfileId.clear();
                m_lastActivatedRouteApp.clear();
            });
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
            [this](const QList<SinkInfo>&)
            {
                m_lastActivatedProfileId.clear();
                m_lastActivatedRouteApp.clear();
            });

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
    connect(m_mpris, &MprisClient::playbackStatusChanged, m_osd, &OSDWindow::updatePlaybackStatus);

    // Settings change → reload MprisClient config
    connect(m_tray, &TrayApp::settingsChanged, m_mpris, &MprisClient::reload);

    // AlbumArtCache → OSDWindow
    connect(m_albumArtCache, &AlbumArtCache::ready, m_osd,
            [this](const QString& url, const QPixmap& pixmap)
            {
                // Ignore stale results — only apply art if it still matches
                // the active player's current art URL.
                if (m_mpris && m_mpris->activePlayer().artUrl == url) m_osd->setAlbumArt(pixmap);
            });
}

void App::initDevice()
{
    m_input->setProfiles(m_config->profiles());
    m_input->setMediaHotkeys(m_config->mediaHotkeys());
    m_input->setScenes(m_config->scenes());
    m_input->setOsdLayoutInput(osdLayoutInputFromConfig(m_config.get()));
    if (!m_startInputDevice) return;
    if (!m_config->inputDevice().isEmpty())
    {
        m_input->startDevice(m_config->inputDevice());
    }
    else
    {
        onDeviceChangeRequested(/*startup=*/true);
    }
}

void App::onHotkeysMaybeChanged()
{
    const QList<Profile> newProfiles = m_config->profiles();
    const MediaHotkeyConfig newMedia = m_config->mediaHotkeys();
    const QList<AudioScene> newScenes = m_config->scenes();
    const OsdLayoutInputConfig newLayout = osdLayoutInputFromConfig(m_config.get());
    const bool profilesChanged = newProfiles != m_input->currentProfiles();
    const bool mediaChanged = newMedia != m_input->currentMediaHotkeys();
    const bool scenesChanged = newScenes != m_input->currentScenes();
    const bool layoutChanged = newLayout != m_input->currentOsdLayoutInput();
    if (!profilesChanged && !mediaChanged && !scenesChanged && !layoutChanged) return;
    if (m_startInputDevice) m_input->stop();
    if (profilesChanged) m_input->setProfiles(newProfiles);
    if (mediaChanged) m_input->setMediaHotkeys(newMedia);
    if (scenesChanged) m_input->setScenes(newScenes);
    if (layoutChanged) m_input->setOsdLayoutInput(newLayout);
    if (m_startInputDevice && !m_config->inputDevice().isEmpty())
        m_input->startDevice(m_config->inputDevice());
}

void App::onAutoSwitchMaybeChanged()
{
    const QString previousTarget = m_autoActiveApp;
    if (m_config->autoProfileSwitch())
    {
        m_autoActiveApp = ::validateStickyAutoProfileTarget(m_autoActiveApp, m_config->profiles(),
                                                            m_config->appAliases());
        if (m_mpris) m_mpris->setPreferredApp(m_autoActiveApp);
        startWindowTracker();
    }
    else
    {
        m_autoActiveApp.clear();
        stopWindowTracker();
    }
    const bool enabled = m_config->autoProfileSwitch() && m_config->focusUpdatesActiveApp();
    if (enabled != m_focusDisplayActive || (enabled && previousTarget != m_autoActiveApp))
        updateFocusedActiveApp();
}

void App::updateFocusedActiveApp()
{
    const bool enabled = m_config->autoProfileSwitch() && m_config->focusUpdatesActiveApp();
    if (!enabled && !m_focusDisplayActive) return;
    m_focusDisplayActive = enabled;
    const QString target = enabled ? m_autoActiveApp : QString{};
    if (m_tray) m_tray->setDisplayedApp(target);
    if (m_dbus) m_dbus->setFocusedApp(target);
}

// Lookup profile by id; returns empty Profile when not found.
Profile App::findProfile(const QString& profileId) const
{
    for (const Profile& p : m_config->profiles())
    {
        if (p.id == profileId) return p;
    }
    return Profile{};
}

// Route a profile's apps to its configured sink. Guarded by (profile id,
// alsoRouteApp) so Follow Focus can re-route when focus moves between apps
// that share one regex profile (e.g. discord → slack) without spamming PA
// on repeated hotkeys for the same pair. The guard is cleared on
// settingsChanged and on sinksReady.
// `alsoRouteApp` covers regex-only profiles (empty apps list) and Follow
// Focus targets that matched via app_regex rather than an explicit apps entry.
void App::activateProfile(const Profile& p, const QString& alsoRouteApp)
{
    if (p.id.isEmpty() || !m_volumeCtrl) return;

    const bool sameProfile = (p.id == m_lastActivatedProfileId);
    const bool sameRoutedApp =
        alsoRouteApp.compare(m_lastActivatedRouteApp, Qt::CaseInsensitive) == 0;
    if (sameProfile && sameRoutedApp) return;

    const bool profileChanged = !sameProfile;
    m_lastActivatedProfileId = p.id;
    m_lastActivatedRouteApp = alsoRouteApp;
    if (p.sink.isEmpty()) return;

    QStringList toRoute;
    if (profileChanged)
    {
        toRoute = p.apps;
        if (!alsoRouteApp.isEmpty())
        {
            bool alreadyListed = false;
            const QString lower = alsoRouteApp.toLower();
            for (const QString& app : toRoute)
            {
                if (appIdMatches(app, lower))
                {
                    alreadyListed = true;
                    break;
                }
            }
            if (!alreadyListed) toRoute.append(alsoRouteApp);
        }
    }
    else if (!alsoRouteApp.isEmpty())
    {
        // Same regex/shared profile, new focused app — route only that app.
        toRoute.append(alsoRouteApp);
    }
    else
    {
        return;
    }

    for (const QString& app : toRoute)
    {
        if (app.isEmpty()) continue;
        m_volumeCtrl->setProfileAppSink(p.id, app, p.sink);
    }
}

void App::changeVolume(const QString& profileId, int direction)
{
    const QString app = effectiveApp(profileId);
    if (app.isEmpty()) return;
    double step = m_config->volumeStep() / 100.0;
    // Limits follow the *target* profile — when auto-switch redirects the
    // hotkey to a focused app belonging to a different profile, we must use
    // that profile's vol_min/vol_max, not the hotkey-emitting profile's.
    const Profile p = effectiveProfile(profileId);
    activateProfile(p, app);
    // async → volumeChanged signal; clamped to per-profile [vol_min, vol_max].
    m_volumeCtrl->changeVolume(app, direction * step, p.volMin / 100.0, p.volMax / 100.0);
}

void App::onMute(const QString& profileId)
{
    const QString app = effectiveApp(profileId);
    if (app.isEmpty()) return;
    activateProfile(effectiveProfile(profileId), app);
    m_volumeCtrl->toggleMute(app); // async → volumeChanged signal
}

void App::onDuckingToggle(const QString& profileId)
{
    const QString app = effectiveApp(profileId);
    if (app.isEmpty()) return;
    const Profile p = findProfile(profileId);
    if (!p.ducking.enabled || !p.ducking.hotkey.isAssigned()) return;
    activateProfile(effectiveProfile(profileId), app);
    m_volumeCtrl->toggleDucking(app, p.ducking.volume / 100.0, p.ducking.fadeOutMs,
                                p.ducking.fadeInMs);
}

void App::onShowVolume(const QString& profileId)
{
    const QString app = effectiveApp(profileId);
    if (app.isEmpty()) return;
    activateProfile(effectiveProfile(profileId), app);
    m_volumeCtrl->queryVolume(app); // async → volumeChanged → OSD
}

void App::showMediaKeyOsd(const QString& actionLabel)
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

void App::onDeviceChangeRequested(bool startup)
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

void App::registerMprisEndpoint()
{
    if (!m_registerDbus || m_mprisRegistered || !m_mprisEndpoint) return;
    auto bus = QDBusConnection::sessionBus();
    if (bus.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), m_mprisEndpoint,
                           QDBusConnection::ExportAdaptors |
                               QDBusConnection::ExportScriptableSlots |
                               QDBusConnection::ExportScriptableSignals |
                               QDBusConnection::ExportScriptableProperties))
    {
        if (!m_mprisServiceName.isEmpty() && !bus.registerService(m_mprisServiceName))
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

void App::unregisterMprisEndpoint()
{
    if (!m_mprisRegistered) return;
    auto bus = QDBusConnection::sessionBus();
    bus.unregisterObject(QStringLiteral("/org/mpris/MediaPlayer2"));
    if (!m_mprisServiceName.isEmpty()) bus.unregisterService(m_mprisServiceName);
    m_mprisRegistered = false;
}

void App::onMprisExposureChanged()
{
    if (m_config->osd().exposeMpris)
        registerMprisEndpoint();
    else
        unregisterMprisEndpoint();
}

void App::initDbus()
{
    m_dbus = new DbusInterface(m_config.get(), m_volumeCtrl, this);
    m_dbus->setMprisClient(m_mpris);
    m_tray->connectDbus(m_dbus);
    connect(m_tray, &TrayApp::settingsChanged, m_dbus, &DbusInterface::reloadProfiles);
    connect(m_tray, &TrayApp::settingsChanged, m_dbus, &DbusInterface::reloadProgressEnabled);
    connect(m_tray, &TrayApp::settingsChanged, m_dbus, &DbusInterface::reloadAutoProfileSwitch);
    connect(m_dbus, &DbusInterface::autoProfileSwitchChanged, this, &App::onAutoSwitchMaybeChanged);
    connect(m_tray, &TrayApp::settingsChanged, this, &App::onMprisExposureChanged);
    connect(m_dbus, &DbusInterface::progressEnabledChanged, m_osd,
            [this](bool) { m_osd->reloadStyles(); });
    connect(m_dbus, &DbusInterface::progressEnabledChanged, m_mpris,
            [this](bool) { m_mpris->reload(); });

    // MPRIS interface — create adaptors always, but only register the D-Bus
    // service when expose_mpris is enabled (default: off)
    m_mprisEndpoint = new QObject(this);
    new MprisRootAdaptor(m_dbus, m_mprisEndpoint);
    new MprisPlayerAdaptor(m_dbus, m_mprisEndpoint);

    if (!m_registerDbus) return;

    auto bus = QDBusConnection::sessionBus();

    // Custom interface (always active)
    if (bus.registerObject(QStringLiteral("/org/keyboardvolumeapp"), m_dbus,
                           QDBusConnection::ExportScriptableSignals |
                               QDBusConnection::ExportScriptableProperties |
                               QDBusConnection::ExportScriptableSlots))
    {
        if (!m_dbusServiceName.isEmpty()) bus.registerService(m_dbusServiceName);
    }

    if (m_config->osd().exposeMpris) registerMprisEndpoint();
}

void App::initWindowTracker()
{
    if (!m_config->autoProfileSwitch()) return;
    startWindowTracker();
}

void App::startWindowTracker()
{
    if (!m_startWindowTracker) return;
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

void App::stopWindowTracker()
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

void App::onFocusedBinaryChanged(const QString& binary)
{
    if (!m_config->autoProfileSwitch())
    {
        m_autoActiveApp.clear();
        if (m_mpris) m_mpris->setPreferredApp(QString{});
        return;
    }

    const QString matchedTarget = ::resolveStickyAutoProfileTarget(
        binary, m_appCache, m_config->profiles(), {}, m_config->appAliases());
    if (!matchedTarget.isEmpty())
    {
        m_autoActiveApp = matchedTarget;
        if (m_config->focusUpdatesActiveApp()) updateFocusedActiveApp();
    }
    if (m_mpris) m_mpris->setPreferredApp(m_autoActiveApp);
    if (!m_autoActiveApp.isEmpty())
    {
        const Profile matched = m_config->findProfileByApp(m_autoActiveApp);
        if (!matched.id.isEmpty()) activateProfile(matched, m_autoActiveApp);
    }
}

QString App::effectiveApp(const QString& profileId) const
{
    if (m_config->autoProfileSwitch() && !m_autoActiveApp.isEmpty()) return m_autoActiveApp;
    return findProfile(profileId).primaryApp();
}

// Mirrors effectiveApp: when auto-switch redirects to a focused app, return
// the profile that owns that app (so per-profile limits / settings track the
// target, not the hotkey-emitting profile). Falls back to the hotkey profile
// when auto-switch is off or no match is found.
Profile App::effectiveProfile(const QString& profileId) const
{
    if (m_config->autoProfileSwitch() && !m_autoActiveApp.isEmpty())
    {
        const Profile matched = m_config->findProfileByApp(m_autoActiveApp);
        if (!matched.id.isEmpty()) return matched;
    }
    return findProfile(profileId);
}
