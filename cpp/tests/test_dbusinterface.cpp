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
#include <QTemporaryDir>

#include "config.h"
#include "dbusinterface.h"
#include "volumecontroller.h"

namespace
{

class MockVolumeController : public VolumeController
{
  public:
    void changeVolume(const QString& appName, double delta) override
    {
        changeVolumeCalls.append({appName, delta});
    }
    void setVolume(const QString& appName, double targetVolume) override
    {
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

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
