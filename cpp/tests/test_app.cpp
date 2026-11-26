#include <gtest/gtest.h>

#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <memory>

#include "app.h"
#include "config.h"
#include "dbusinterface.h"
#include "inputhandler.h"
#include "osdwindow.h"
#include "trayapp.h"
#include "volumecontroller.h"

// Defined in main.cpp for the app binary; tests supply their own definition.
bool g_nativeWayland = false;

namespace
{

class MockVolumeController : public VolumeController
{
  public:
    MockVolumeController()
    {
        close();
    }

    QList<AudioApp> listApps(bool = false) override
    {
        return apps;
    }

    void changeVolume(const QString& appName, double delta, double minVol = 0.0,
                      double maxVol = 1.0) override
    {
        changeCalls.append({appName, delta, minVol, maxVol});
    }

    void setVolume(const QString& appName, double targetVolume, double minVol = 0.0,
                   double maxVol = 1.0) override
    {
        setVolumeCalls.append({appName, targetVolume, minVol, maxVol});
    }

    void toggleMute(const QString& appName) override
    {
        toggleMuteCalls.append(appName);
    }

    void setMuted(const QString& appName, bool muted) override
    {
        setMutedCalls.append({appName, muted});
    }

    void toggleDucking(const QString& keepApp, double duckVolume) override
    {
        duckingCalls.append({keepApp, duckVolume});
    }

    void setAppSink(const QString& appName, const QString& sinkName) override
    {
        sinkCalls.append({appName, sinkName});
    }

    void applyScene(const AudioScene& scene) override
    {
        appliedSceneIds.append(scene.id);
    }

    void queryVolume(const QString& appName) override
    {
        queryVolumeCalls.append(appName);
    }

    void queryVolumeSilently(const QString& appName) override
    {
        silentQueryCalls.append(appName);
    }

    struct VolumeCall
    {
        QString app;
        double value = 0.0;
        double minVol = 0.0;
        double maxVol = 1.0;
    };

    QList<AudioApp> apps;
    QList<VolumeCall> changeCalls;
    QList<VolumeCall> setVolumeCalls;
    QStringList toggleMuteCalls;
    QList<QPair<QString, bool>> setMutedCalls;
    QList<QPair<QString, double>> duckingCalls;
    QList<QPair<QString, QString>> sinkCalls;
    QStringList appliedSceneIds;
    QStringList queryVolumeCalls;
    QStringList silentQueryCalls;
};

struct AppHarness
{
    QTemporaryDir tmp;
    MockVolumeController vc;
    std::unique_ptr<App> app;
    QString dbusService;
    QString mprisService;

    explicit AppHarness(const QString& suffix, bool registerDbus = false)
        : dbusService(QStringLiteral("org.keyboardvolumeapp.test.%1").arg(suffix)),
          mprisService(
              QStringLiteral("org.mpris.MediaPlayer2.keyboardvolumeapp_test_%1").arg(suffix))
    {
        auto cfg = std::make_unique<Config>(tmp.path());
        cfg->setVolumeStep(10);

        Profile def;
        def.id = QStringLiteral("default");
        def.name = QStringLiteral("Default");
        def.apps = {QStringLiteral("spotify")};
        def.volMin = 10;
        def.volMax = 90;
        def.ducking.enabled = true;
        def.ducking.volume = 25;
        def.ducking.hotkey = HotkeyBinding::key(88);
        def.sink = QStringLiteral("alsa_output.speakers");

        Profile comms;
        comms.id = QStringLiteral("comms");
        comms.name = QStringLiteral("Comms");
        comms.apps = {QStringLiteral("discord")};
        comms.appRegex = QStringLiteral("discord|slack");
        comms.volMin = 20;
        comms.volMax = 80;
        comms.autoSwitch = true;
        comms.sink = QStringLiteral("alsa_output.headset");

        cfg->setProfiles({def, comms});

        AudioScene meeting;
        meeting.id = QStringLiteral("meeting");
        meeting.name = QStringLiteral("Meeting");
        SceneTarget t;
        t.match = QStringLiteral("spotify");
        t.volume = 15;
        meeting.targets = {t};
        meeting.hotkey = HotkeyBinding::key(87);
        cfg->setScenes({meeting});

        app = std::make_unique<App>(std::move(cfg));

        AppInitOptions opts;
        opts.volumeCtrl = &vc;
        opts.startInputDevice = false;
        opts.startWindowTracker = false;
        opts.registerDbus = registerDbus;
        opts.dbusServiceName = dbusService;
        opts.mprisServiceName = mprisService;
        app->init(opts);
    }

    ~AppHarness()
    {
        if (app) app->cleanup();
    }
};

} // namespace

TEST(AppWiring, InitializationIsSingleUseEvenAfterCleanup)
{
    AppHarness h(QStringLiteral("single_init"));
    auto* input = h.app->inputHandler();
    auto* tray = h.app->trayApp();
    auto* osd = h.app->osdWindow();

    h.app->init();
    EXPECT_EQ(h.app->inputHandler(), input);
    EXPECT_EQ(h.app->trayApp(), tray);
    EXPECT_EQ(h.app->osdWindow(), osd);
    EXPECT_EQ(h.app->findChildren<InputHandler*>().size(), 1);

    emit input->volume_up(QStringLiteral("default"));
    EXPECT_EQ(h.vc.changeCalls.size(), 1);

    h.app->cleanup();
    h.app->init();
    EXPECT_EQ(h.app->inputHandler(), input);
    EXPECT_EQ(h.app->trayApp(), tray);
    EXPECT_EQ(h.app->osdWindow(), osd);
    EXPECT_EQ(h.app->findChildren<InputHandler*>().size(), 1);
}

TEST(AppWiring, InputHandlerSignalsRouteToVolumeControllerWithProfileLimits)
{
    AppHarness h(QStringLiteral("routing"));

    emit h.app->inputHandler()->volume_up(QStringLiteral("default"));
    emit h.app->inputHandler()->volume_down(QStringLiteral("comms"));
    emit h.app->inputHandler()->volume_mute(QStringLiteral("default"));
    emit h.app->inputHandler()->ducking_toggle(QStringLiteral("default"));
    emit h.app->inputHandler()->show_volume(QStringLiteral("comms"));
    emit h.app->inputHandler()->scene_apply(QStringLiteral("meeting"));
    emit h.app->inputHandler()->scene_apply(QStringLiteral("nonexistent"));

    ASSERT_EQ(h.vc.changeCalls.size(), 2);
    EXPECT_EQ(h.vc.changeCalls[0].app, QStringLiteral("spotify"));
    EXPECT_DOUBLE_EQ(h.vc.changeCalls[0].value, 0.10);
    EXPECT_DOUBLE_EQ(h.vc.changeCalls[0].minVol, 0.10);
    EXPECT_DOUBLE_EQ(h.vc.changeCalls[0].maxVol, 0.90);

    EXPECT_EQ(h.vc.changeCalls[1].app, QStringLiteral("discord"));
    EXPECT_DOUBLE_EQ(h.vc.changeCalls[1].value, -0.10);
    EXPECT_DOUBLE_EQ(h.vc.changeCalls[1].minVol, 0.20);
    EXPECT_DOUBLE_EQ(h.vc.changeCalls[1].maxVol, 0.80);

    EXPECT_EQ(h.vc.toggleMuteCalls, QStringList{QStringLiteral("spotify")});
    ASSERT_EQ(h.vc.duckingCalls.size(), 1);
    EXPECT_EQ(h.vc.duckingCalls[0].first, QStringLiteral("spotify"));
    EXPECT_DOUBLE_EQ(h.vc.duckingCalls[0].second, 0.25);

    EXPECT_EQ(h.vc.queryVolumeCalls, QStringList{QStringLiteral("discord")});
    EXPECT_EQ(h.vc.appliedSceneIds, QStringList{QStringLiteral("meeting")});
}

TEST(AppWiring, FollowFocusRedirectsLimitsRoutesSinkOnceAndMirrorsToTrayAndDbus)
{
    AppHarness h(QStringLiteral("focus"));
    h.app->config()->setAutoProfileSwitch(true);
    h.app->config()->setFocusUpdatesActiveApp(true);
    h.app->onAutoSwitchMaybeChanged();

    // Seed app cache via VolumeController::appsReady.
    emit h.vc.appsReady(
        {{1, QStringLiteral("Spotify"), QStringLiteral("spotify"), 0.8, false, true},
         {2, QStringLiteral("Discord"), QStringLiteral("discord"), 0.7, false, true},
         {3, QStringLiteral("Slack"), QStringLiteral("slack"), 0.6, false, true}});

    // Focusing discord matches the "comms" profile (volMin=20, volMax=80, sink=headset).
    h.app->onFocusedBinaryChanged(QStringLiteral("discord"));
    EXPECT_EQ(h.app->autoActiveApp(), QStringLiteral("discord"));
    EXPECT_EQ(h.app->effectiveApp(QStringLiteral("default")), QStringLiteral("discord"));
    EXPECT_EQ(h.app->effectiveProfile(QStringLiteral("default")).id, QStringLiteral("comms"));
    EXPECT_EQ(h.app->dbusInterface()->activeApp(), QStringLiteral("discord"));
    EXPECT_EQ(h.vc.sinkCalls.size(), 1);
    EXPECT_EQ(h.vc.sinkCalls.first(),
              (QPair<QString, QString>{QStringLiteral("discord"),
                                       QStringLiteral("alsa_output.headset")}));
    EXPECT_EQ(h.vc.profileRoutedApps(QStringLiteral("comms")),
              QStringList{QStringLiteral("discord")});
    EXPECT_TRUE(h.vc.profileRoutedApps(QStringLiteral("default")).isEmpty());

    // Hotkey on "default" must use focused target's limits [0.20, 0.80] and NOT spam sink routing.
    emit h.app->inputHandler()->volume_up(QStringLiteral("default"));
    ASSERT_EQ(h.vc.changeCalls.size(), 1);
    EXPECT_EQ(h.vc.changeCalls.first().app, QStringLiteral("discord"));
    EXPECT_DOUBLE_EQ(h.vc.changeCalls.first().minVol, 0.20);
    EXPECT_DOUBLE_EQ(h.vc.changeCalls.first().maxVol, 0.80);
    EXPECT_EQ(h.vc.sinkCalls.size(), 1);

    // Moving focus to another app in the same regex profile ("slack") routes slack without
    // re-routing discord.
    h.app->onFocusedBinaryChanged(QStringLiteral("slack"));
    ASSERT_EQ(h.vc.sinkCalls.size(), 2);
    EXPECT_EQ(
        h.vc.sinkCalls.last(),
        (QPair<QString, QString>{QStringLiteral("slack"), QStringLiteral("alsa_output.headset")}));
    EXPECT_EQ(h.vc.profileRoutedApps(QStringLiteral("comms")),
              (QStringList{QStringLiteral("discord"), QStringLiteral("slack")}));

    // Sink hot-plug clears the activation guard so the next press re-applies the configured sink.
    emit h.vc.sinksReady({});
    emit h.app->inputHandler()->volume_up(QStringLiteral("default"));
    EXPECT_GT(h.vc.sinkCalls.size(), 2);

    // Disabling auto-switch clears sticky target and reverts effectiveApp to default profile.
    h.app->config()->setAutoProfileSwitch(false);
    h.app->onAutoSwitchMaybeChanged();
    EXPECT_TRUE(h.app->autoActiveApp().isEmpty());
    EXPECT_EQ(h.app->effectiveApp(QStringLiteral("default")), QStringLiteral("spotify"));
}

TEST(AppWiring, HotkeysReloadUpdatesInputHandlerSnapshots)
{
    AppHarness h(QStringLiteral("hotkeys"));
    ASSERT_EQ(h.app->inputHandler()->currentProfiles().size(), 2);

    MediaHotkeyConfig media = h.app->config()->mediaHotkeys();
    media.playPause = HotkeyBinding::key(164);
    h.app->config()->setMediaHotkeys(media);

    emit h.app->trayApp()->settingsChanged();

    EXPECT_EQ(h.app->inputHandler()->currentMediaHotkeys().playPause, HotkeyBinding::key(164));
}

TEST(AppWiring, MediaKeyOsdModesRespectConfiguration)
{
    AppHarness h(QStringLiteral("mediaosd"));

    // Mode::Off -> no queryVolume call.
    OsdConfig osd = h.app->config()->osd();
    osd.mediaKeysOsdMode = MediaKeysOsdMode::Off;
    h.app->config()->setOsd(osd);
    emit h.app->inputHandler()->media_play_pause();
    EXPECT_TRUE(h.vc.queryVolumeCalls.isEmpty());

    // Mode::Action -> shows action OSD directly, still no queryVolume call.
    osd.mediaKeysOsdMode = MediaKeysOsdMode::Action;
    h.app->config()->setOsd(osd);
    emit h.app->inputHandler()->media_next();
    EXPECT_TRUE(h.vc.queryVolumeCalls.isEmpty());

    // Mode::Full -> queries the active/default app's volume for full OSD.
    osd.mediaKeysOsdMode = MediaKeysOsdMode::Full;
    h.app->config()->setOsd(osd);
    emit h.app->inputHandler()->media_previous();
    EXPECT_EQ(h.vc.queryVolumeCalls, QStringList{QStringLiteral("spotify")});
}

TEST(AppWiring, MprisExposureTogglesLiveOnSessionBusAndCleansUp)
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface())
    {
        GTEST_SKIP() << "Session D-Bus is not available";
    }

    AppHarness h(QStringLiteral("mpris"), /*registerDbus=*/true);
    EXPECT_TRUE(bus.interface()->isServiceRegistered(h.dbusService));
    EXPECT_FALSE(h.app->isMprisRegistered());
    EXPECT_FALSE(bus.interface()->isServiceRegistered(h.mprisService));

    // Enabling expose_mpris registers MPRIS live without restarting App.
    OsdConfig osd = h.app->config()->osd();
    osd.exposeMpris = true;
    h.app->config()->setOsd(osd);
    emit h.app->trayApp()->settingsChanged();

    EXPECT_TRUE(h.app->isMprisRegistered());
    EXPECT_TRUE(bus.interface()->isServiceRegistered(h.mprisService));

    // Disabling expose_mpris unregisters MPRIS immediately.
    osd.exposeMpris = false;
    h.app->config()->setOsd(osd);
    emit h.app->trayApp()->settingsChanged();

    EXPECT_FALSE(h.app->isMprisRegistered());
    EXPECT_FALSE(bus.interface()->isServiceRegistered(h.mprisService));

    // Cleanup unregisters main D-Bus service cleanly.
    h.app->cleanup();
    EXPECT_FALSE(bus.interface()->isServiceRegistered(h.dbusService));
}

TEST(AppWiring, DbusProgressEnabledUpdatesConfigAndOsdWithoutCrash)
{
    AppHarness h(QStringLiteral("progress"));
    QSignalSpy progressSpy(h.app->dbusInterface(), &DbusInterface::progressEnabledChanged);

    h.app->dbusInterface()->setProgressEnabled(true);
    EXPECT_TRUE(h.app->config()->osd().progressEnabled);
    EXPECT_EQ(progressSpy.count(), 1);
    EXPECT_TRUE(progressSpy.first().first().toBool());

    h.app->dbusInterface()->setProgressEnabled(false);
    EXPECT_FALSE(h.app->config()->osd().progressEnabled);
    EXPECT_EQ(progressSpy.count(), 2);
    EXPECT_FALSE(progressSpy.last().first().toBool());
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    qputenv("PULSE_SERVER", QByteArrayLiteral("unix:/nonexistent/keyboard-volume-test"));
    qputenv("PIPEWIRE_RUNTIME_DIR", QByteArrayLiteral("/nonexistent/keyboard-volume-test"));
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
