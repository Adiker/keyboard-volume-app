#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>
#include <chrono>
#include <thread>

#include "volumecontroller.h"

// Smoke tests: verify the VolumeController can be constructed, used, and
// destroyed without crashing. The test process is isolated from desktop audio.
// The opt-in PipeWire integration test exercises the connected paths.

TEST(VolumeController, ConstructAndDestruct)
{
    VolumeController vc;

    // listApps() returns cached list immediately — never blocks.
    vc.listApps();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // Destructor calls close() → cleans up PA thread
}

TEST(VolumeController, ChangeVolumeAndToggleMute)
{
    VolumeController vc;

    // Should not crash even when the target app does not exist.
    vc.changeVolume("nonexistent_app", 0.05);
    vc.toggleMute("nonexistent_app");
    vc.toggleDucking("nonexistent_app", 0.25);
    vc.toggleDucking("nonexistent_app", 0.25);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

TEST(VolumeController, ListAppsForceRefresh)
{
    VolumeController vc;

    // Forces an async refresh; returns cached data synchronously.
    vc.listApps(true);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

TEST(VolumeController, CloseIsIdempotent)
{
    VolumeController vc;

    vc.close();
    vc.close(); // second close must be harmless

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    SUCCEED();
}

TEST(VolumeController, ListSinksReturnsCacheWithoutBlocking)
{
    VolumeController vc;

    // listSinks() returns cached list immediately — never blocks.
    auto sinks = vc.listSinks();
    (void)sinks;
    vc.listSinks(true);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    SUCCEED();
}

TEST(VolumeController, SetAppSinkDoesNotCrashOnInvalidInput)
{
    VolumeController vc;

    // Empty sinkName is a no-op (does not even reach the worker).
    vc.setAppSink("nonexistent_app", "");
    // Empty appName is a no-op.
    vc.setAppSink("", "alsa_output.unknown");
    // Both empty.
    vc.setAppSink("", "");

    // Non-empty but unknown sink — worker logs failure and parks.
    vc.setAppSink("nonexistent_app", "alsa_output.totally-not-real");

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    SUCCEED();
}

TEST(VolumeController, TracksProfileRoutedAppsForSettingsCleanup)
{
    VolumeController vc;

    vc.setProfileAppSink("comms", "discord", "alsa_output.totally-not-real");
    vc.setProfileAppSink("comms", "Discord", "alsa_output.totally-not-real");
    vc.setProfileAppSink("comms", "slack", "alsa_output.totally-not-real");

    EXPECT_EQ(vc.profileRoutedApps("comms"),
              (QStringList{QStringLiteral("discord"), QStringLiteral("slack")}));

    vc.clearAppSinkOverride("discord");
    EXPECT_EQ(vc.profileRoutedApps("comms"), QStringList{QStringLiteral("slack")});
}

TEST(VolumeController, UnavailablePulseAudioDoesNotBlockOperations)
{
    {
        VolumeController vc;

        vc.listApps(true);
        vc.listSinks(true);
        vc.changeVolume("nonexistent_app", 0.05);
        vc.toggleMute("nonexistent_app");
        vc.toggleDucking("nonexistent_app", 0.25);
        vc.setAppSink("nonexistent_app", "alsa_output.fake");

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        vc.close();
    }

    SUCCEED();
}

int main(int argc, char** argv)
{
    // A private D-Bus session does not isolate audio sockets. Set these before
    // constructing any controller, including when this binary is run directly.
    QTemporaryDir audioRuntime;
    if (!audioRuntime.isValid()) return 1;
    const QByteArray runtime = audioRuntime.path().toLocal8Bit();
    QFile pulseConfig(audioRuntime.filePath(QStringLiteral("pulse-client.conf")));
    if (!pulseConfig.open(QIODevice::WriteOnly) || pulseConfig.write("autospawn = no\n") < 0)
        return 1;
    pulseConfig.close();
    qputenv("XDG_RUNTIME_DIR", runtime);
    qputenv("PIPEWIRE_RUNTIME_DIR", runtime);
    qunsetenv("PIPEWIRE_REMOTE");
    qunsetenv("PIPEWIRE_CORE");
    qputenv("PULSE_SERVER", "unix:" + runtime + "/missing-pulse.sock");
    qputenv("PULSE_CLIENTCONFIG", pulseConfig.fileName().toLocal8Bit());

    // VolumeController uses Qt signals/slots — QCoreApplication required.
    QCoreApplication app(argc, argv);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
