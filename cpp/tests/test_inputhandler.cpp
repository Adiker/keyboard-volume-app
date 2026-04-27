#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QSet>
#include <tuple>

#include "inputhandler.h"

TEST(InputHandler, DefaultHotkeys)
{
    InputHandler handler;

    auto [up, down, mute] = handler.currentHotkeys();
    EXPECT_EQ(up,   115);  // KEY_VOLUMEUP
    EXPECT_EQ(down, 114);  // KEY_VOLUMEDOWN
    EXPECT_EQ(mute, 113);  // KEY_MUTE
}

TEST(InputHandler, SetHotkeys)
{
    InputHandler handler;

    handler.setHotkeys(200, 201, 202);

    auto [up, down, mute] = handler.currentHotkeys();
    EXPECT_EQ(up,   200);
    EXPECT_EQ(down, 201);
    EXPECT_EQ(mute, 202);
}

TEST(InputHandler, DevicePathInitiallyEmpty)
{
    InputHandler handler;

    EXPECT_TRUE(handler.devicePath().isEmpty());
}

TEST(InputHandler, SignalsConnectable)
{
    // Verify signals are valid for Qt MOC (no compile warnings at runtime).
    InputHandler handler;
    QSignalSpy spyUp(&handler, &InputHandler::volume_up);
    QSignalSpy spyDown(&handler, &InputHandler::volume_down);
    QSignalSpy spyMute(&handler, &InputHandler::volume_mute);

    EXPECT_TRUE(spyUp.isValid());
    EXPECT_TRUE(spyDown.isValid());
    EXPECT_TRUE(spyMute.isValid());
}

TEST(InputHandler, ListEvdevDevices)
{
    // Enumerate /dev/input/event*.  Returns a (possibly empty) list of paths.
    QList<QString> devices = listEvdevDevices();
    for (const QString &d : devices) {
        EXPECT_TRUE(d.startsWith("/dev/input/event"))
            << d.toStdString() << " does not start with /dev/input/event";
    }
}

TEST(InputHandler, FindWithNonExistentPath)
{
    // None of these should crash or throw when given a device that does not exist.
    findSiblingDevices("/dev/input/event99999");
    findCaptureDevices("/dev/input/event99999");
    QSet<int> hotkeys{115, 114, 113};
    findHotkeyDevices("/dev/input/event99999", hotkeys);
    SUCCEED();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
