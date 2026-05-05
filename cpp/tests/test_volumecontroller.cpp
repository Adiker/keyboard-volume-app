#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QByteArray>
#include <chrono>
#include <thread>

#include "volumecontroller.h"

// Smoke tests: verify the VolumeController can be constructed, used, and
// destroyed without crashing.  These require a running PulseAudio daemon.
// If PA is unavailable the tests may time out in close().

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

TEST(VolumeController, UnavailablePulseAudioDoesNotBlockOperations)
{
    const bool hadPulseServer = qEnvironmentVariableIsSet("PULSE_SERVER");
    const QByteArray oldPulseServer = qgetenv("PULSE_SERVER");
    qputenv("PULSE_SERVER", "unix:/tmp/keyboard-volume-app-missing-pa.sock");

    {
        VolumeController vc;

        vc.listApps(true);
        vc.changeVolume("nonexistent_app", 0.05);
        vc.toggleMute("nonexistent_app");
        vc.toggleDucking("nonexistent_app", 0.25);

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        vc.close();
    }

    if (hadPulseServer)
        qputenv("PULSE_SERVER", oldPulseServer);
    else
        qunsetenv("PULSE_SERVER");

    SUCCEED();
}

int main(int argc, char** argv)
{
    // VolumeController uses Qt signals/slots — QCoreApplication required.
    QCoreApplication app(argc, argv);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
