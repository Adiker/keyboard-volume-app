// test_dbusinterface.cpp
// Regression tests for DbusInterface property setters.
//
// The D-Bus `Volume` and `Muted` properties are absolute: a caller writing
// `Volume = 0.5` expects the active app's volume to be set to exactly 50%,
// not adjusted by the difference between 0.5 and whatever happens to be in
// the local cache. Likewise `Muted = true` must set the absolute mute state,
// not toggle whatever the live state happens to be.
//
// These tests use a MockVolumeController that records which API on the
// VolumeController got called, so we can assert that the property setters
// route to the absolute API (setVolume/setMuted) rather than the relative
// API (changeVolume/toggleMute).

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QMetaMethod>
#include <QTemporaryDir>

#include "config.h"
#include "dbusinterface.h"
#include "volumecontroller.h"

namespace
{

class MockVolumeController : public VolumeController
{
  public:
    void changeVolume(const QString& appName, double delta, double minVol = 0.0,
                      double maxVol = 1.0) override
    {
        Q_UNUSED(minVol);
        Q_UNUSED(maxVol);
        changeVolumeCalls.append({appName, delta});
    }
    void setVolume(const QString& appName, double targetVolume, double minVol = 0.0,
                   double maxVol = 1.0) override
    {
        Q_UNUSED(minVol);
        Q_UNUSED(maxVol);
        setVolumeCalls.append({appName, targetVolume});
    }
    void toggleMute(const QString& appName) override
    {
        toggleMuteCalls.append(appName);
    }
    void setMuted(const QString& appName, bool muted) override
    {
        setMutedCalls.append({appName, muted});
    }

    QList<QPair<QString, double>> changeVolumeCalls;
    QList<QPair<QString, double>> setVolumeCalls;
    QList<QString> toggleMuteCalls;
    QList<QPair<QString, bool>> setMutedCalls;
};

struct Harness
{
    QTemporaryDir tmp;
    Config config;
    MockVolumeController vc;
    DbusInterface dbus;

    Harness() : config(tmp.path()), dbus(&config, &vc)
    {
        // Seed an active app via the property setter so the property writes
        // below have a target to route to.
        dbus.setActiveApp(QStringLiteral("spotify"));
    }
};

} // namespace

// Bug: setVolume() previously computed a delta from the stale cached m_volume
// and called changeVolume() (relative). Now it must route to the absolute
// setVolume() so external D-Bus writes hit the requested target exactly.
TEST(DbusInterface, SetVolumePropertyIsAbsoluteEvenWithStaleCache)
{
    Harness h;

    // Cache is still 0.0 (no volumeChanged signal received yet), but the user
    // requests an absolute 50%. The fix must route through setVolume(), not
    // through changeVolume(target - 0.0) — that would happen to land at the
    // right value on a fresh cache, but for any non-zero stale cache it would
    // overshoot/undershoot.
    h.dbus.setVolume(0.5);

    ASSERT_EQ(h.vc.setVolumeCalls.size(), 1);
    EXPECT_EQ(h.vc.setVolumeCalls.first().first, QStringLiteral("spotify"));
    EXPECT_DOUBLE_EQ(h.vc.setVolumeCalls.first().second, 0.5);
    EXPECT_TRUE(h.vc.changeVolumeCalls.isEmpty());
}

TEST(DbusInterface, SetVolumeClampsOutOfRangeValues)
{
    Harness h;

    h.dbus.setVolume(2.5);
    h.dbus.setVolume(-0.1);

    ASSERT_EQ(h.vc.setVolumeCalls.size(), 2);
    EXPECT_DOUBLE_EQ(h.vc.setVolumeCalls[0].second, 1.0);
    EXPECT_DOUBLE_EQ(h.vc.setVolumeCalls[1].second, 0.0);
}

TEST(DbusInterface, SetVolumeIgnoredWhenNoActiveApp)
{
    QTemporaryDir tmp;
    Config config(tmp.path());
    MockVolumeController vc;
    DbusInterface dbus(&config, &vc);
    // Active app is empty (default profile has no app and no tray was provided).

    dbus.setVolume(0.5);

    EXPECT_TRUE(vc.setVolumeCalls.isEmpty());
    EXPECT_TRUE(vc.changeVolumeCalls.isEmpty());
}

// Bug: setMuted(true) previously called toggleMute(), which flips whatever
// the live state is. With a stale cache this inverts the user's intent.
TEST(DbusInterface, SetMutedPropertyIsAbsoluteNotToggle)
{
    Harness h;

    h.dbus.setMuted(true);
    h.dbus.setMuted(false);
    h.dbus.setMuted(true);

    ASSERT_EQ(h.vc.setMutedCalls.size(), 3);
    EXPECT_EQ(h.vc.setMutedCalls[0], (QPair<QString, bool>{QStringLiteral("spotify"), true}));
    EXPECT_EQ(h.vc.setMutedCalls[1], (QPair<QString, bool>{QStringLiteral("spotify"), false}));
    EXPECT_EQ(h.vc.setMutedCalls[2], (QPair<QString, bool>{QStringLiteral("spotify"), true}));
    EXPECT_TRUE(h.vc.toggleMuteCalls.isEmpty());
}

TEST(DbusInterface, SetMutedIgnoredWhenNoActiveApp)
{
    QTemporaryDir tmp;
    Config config(tmp.path());
    MockVolumeController vc;
    DbusInterface dbus(&config, &vc);

    dbus.setMuted(true);

    EXPECT_TRUE(vc.setMutedCalls.isEmpty());
    EXPECT_TRUE(vc.toggleMuteCalls.isEmpty());
}

// The ToggleMute() D-Bus method (distinct from the Muted property) is the
// intentional toggle entry point — confirm it still routes through toggleMute.
TEST(DbusInterface, ToggleMuteMethodStillRoutesThroughToggle)
{
    Harness h;

    h.dbus.ToggleMute();

    ASSERT_EQ(h.vc.toggleMuteCalls.size(), 1);
    EXPECT_EQ(h.vc.toggleMuteCalls.first(), QStringLiteral("spotify"));
    EXPECT_TRUE(h.vc.setMutedCalls.isEmpty());
}

// ─── Media controls ───────────────────────────────────────────────────────────
// We exercise the no-MprisClient path here: each Media* method must be a safe
// no-op (no crash, no VolumeController traffic). The positive path — that the
// methods actually relay to MprisClient slots — is covered by test_mprisclient
// (slot wiring) and verified manually via dbus-send. Building a mock that can
// stand in for MprisClient* would require either a virtual API on MprisClient
// or constructing a real one (which connects to the session bus), neither of
// which fits this unit test layer.

TEST(DbusInterface, MediaMethodsNoOpWithoutMprisClient)
{
    Harness h;
    // No setMprisClient() call → m_mpris stays null.

    h.dbus.MediaPlayPause();
    h.dbus.MediaNext();
    h.dbus.MediaPrevious();
    h.dbus.MediaStop();

    EXPECT_TRUE(h.vc.changeVolumeCalls.isEmpty());
    EXPECT_TRUE(h.vc.setVolumeCalls.isEmpty());
    EXPECT_TRUE(h.vc.toggleMuteCalls.isEmpty());
    EXPECT_TRUE(h.vc.setMutedCalls.isEmpty());
}

TEST(DbusInterface, MediaMethodsAreScriptable)
{
    Harness h;
    const QMetaObject* mo = h.dbus.metaObject();

    // Walk methods and confirm each Media* slot is registered as scriptable
    // (so it's exposed over D-Bus when the interface is registered).
    auto isScriptableSlot = [mo](const char* name)
    {
        for (int i = 0; i < mo->methodCount(); ++i)
        {
            QMetaMethod m = mo->method(i);
            if (m.methodType() != QMetaMethod::Slot) continue;
            if (m.name() != QByteArray(name)) continue;
            return (m.attributes() & QMetaMethod::Scriptable) != 0;
        }
        return false;
    };

    EXPECT_TRUE(isScriptableSlot("MediaPlayPause"));
    EXPECT_TRUE(isScriptableSlot("MediaNext"));
    EXPECT_TRUE(isScriptableSlot("MediaPrevious"));
    EXPECT_TRUE(isScriptableSlot("MediaStop"));
}

// ─── Scenes property ──────────────────────────────────────────────────────────
// The Scenes property must expose each scene's hotkey alongside id/name/targets.
TEST(DbusInterface, ScenesPropertyIncludesHotkey)
{
    QTemporaryDir tmp;
    Config config(tmp.path());

    AudioScene meeting;
    meeting.id = QStringLiteral("meeting");
    meeting.name = QStringLiteral("Meeting");
    meeting.hotkey = HotkeyBinding::key(88);
    meeting.targets = {SceneTarget{QStringLiteral("Spotify"), 10, std::nullopt}};

    AudioScene quiet;
    quiet.id = QStringLiteral("quiet");
    quiet.name = QStringLiteral("Quiet");
    // No hotkey assigned.
    quiet.targets = {SceneTarget{QStringLiteral("Discord"), std::nullopt, true}};

    config.setScenes({meeting, quiet});

    MockVolumeController vc;
    DbusInterface dbus(&config, &vc);
    dbus.reloadProfiles(); // rebuilds the scenes cache from Config

    const QVariantList scenes = dbus.scenesProp();
    ASSERT_EQ(scenes.size(), 2);

    const QVariantMap m0 = scenes[0].toMap();
    EXPECT_EQ(m0.value(QStringLiteral("id")).toString(), QStringLiteral("meeting"));
    ASSERT_TRUE(m0.contains(QStringLiteral("hotkey")));
    // Assigned EV_KEY binding → exposed as a plain int code.
    EXPECT_EQ(m0.value(QStringLiteral("hotkey")).toInt(), 88);

    const QVariantMap m1 = scenes[1].toMap();
    EXPECT_EQ(m1.value(QStringLiteral("id")).toString(), QStringLiteral("quiet"));
    ASSERT_TRUE(m1.contains(QStringLiteral("hotkey")));
    // Unassigned binding → 0.
    EXPECT_EQ(m1.value(QStringLiteral("hotkey")).toInt(), 0);
}

// ApplyScene must route the scene's targets through VolumeController::applyScene
// (which uses setVolume/setMuted), not leave them untouched.
TEST(DbusInterface, ApplySceneRoutesToVolumeController)
{
    QTemporaryDir tmp;
    Config config(tmp.path());

    AudioScene scene;
    scene.id = QStringLiteral("meeting");
    scene.name = QStringLiteral("Meeting");
    scene.targets = {
        SceneTarget{QStringLiteral("Spotify"), 10, std::nullopt},
        SceneTarget{QStringLiteral("Discord"), std::nullopt, true},
    };
    config.setScenes({scene});

    MockVolumeController vc;
    DbusInterface dbus(&config, &vc);

    dbus.ApplyScene(QStringLiteral("meeting"));

    ASSERT_EQ(vc.setVolumeCalls.size(), 1);
    EXPECT_EQ(vc.setVolumeCalls[0].first, QStringLiteral("Spotify"));
    EXPECT_DOUBLE_EQ(vc.setVolumeCalls[0].second, 0.1);
    ASSERT_EQ(vc.setMutedCalls.size(), 1);
    EXPECT_EQ(vc.setMutedCalls[0], (QPair<QString, bool>{QStringLiteral("Discord"), true}));
}

TEST(DbusInterface, ApplySceneUnknownIdIsNoOp)
{
    QTemporaryDir tmp;
    Config config(tmp.path());

    MockVolumeController vc;
    DbusInterface dbus(&config, &vc);

    dbus.ApplyScene(QStringLiteral("does-not-exist"));

    EXPECT_TRUE(vc.setVolumeCalls.isEmpty());
    EXPECT_TRUE(vc.setMutedCalls.isEmpty());
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
