#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QEventLoop>
#include <QFile>
#include <QMenu>
#include <QSignalSpy>
#include <QSystemTrayIcon>
#include <QTemporaryDir>

#include "config.h"
#include "dbusinterface.h"
#include "trayapp.h"
#include "volumecontroller.h"

namespace
{
class TestVolumeController : public VolumeController
{
  public:
    TestVolumeController()
    {
        close();
    }
    QList<AudioApp> listApps(bool = false) override
    {
        return apps;
    }
    QList<AudioApp> apps = {{std::nullopt, QStringLiteral("Spotify"), QStringLiteral("spotify")},
                            {std::nullopt, QStringLiteral("firefox"), QStringLiteral("firefox")}};
};

class TrayDbusTest : public ::testing::Test
{
  protected:
    QTemporaryDir tmp;
    Config config{tmp.path()};
    TestVolumeController volume;
    TrayApp tray{&config, &volume, nullptr};
    DbusInterface dbus{&config, &volume};
    QDBusConnection server = QDBusConnection::sessionBus();
    QDBusConnection client = QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                                           QStringLiteral("tray-test-client"));

    void SetUp() override
    {
        ASSERT_TRUE(server.isConnected());
        ASSERT_TRUE(client.isConnected());
        tray.connectDbus(&dbus);
        emit volume.appsReady(volume.apps);
        ASSERT_TRUE(server.registerService(QStringLiteral("org.keyboardvolumeapp.test")));
        ASSERT_TRUE(server.registerObject(QStringLiteral("/org/keyboardvolumeapp"), &dbus,
                                          QDBusConnection::ExportScriptableProperties));
    }

    void TearDown() override
    {
        server.unregisterObject(QStringLiteral("/org/keyboardvolumeapp"));
        server.unregisterService(QStringLiteral("org.keyboardvolumeapp.test"));
        QDBusConnection::disconnectFromBus(QStringLiteral("tray-test-client"));
    }

    bool setActiveApp(const QString& name)
    {
        auto message = QDBusMessage::createMethodCall(
            QStringLiteral("org.keyboardvolumeapp.test"), QStringLiteral("/org/keyboardvolumeapp"),
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Set"));
        message << QStringLiteral("org.keyboardvolumeapp.VolumeControl")
                << QStringLiteral("ActiveApp") << QVariant::fromValue(QDBusVariant(name));
        QDBusPendingCallWatcher watcher(client.asyncCall(message, 3000));
        QEventLoop loop;
        QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
        if (!watcher.isFinished()) loop.exec();
        QDBusPendingReply<> reply = watcher;
        return !reply.isError();
    }

    QAction* action(const QString& label)
    {
        auto* icon = tray.findChild<QSystemTrayIcon*>();
        if (!icon || !icon->contextMenu()) return nullptr;
        for (QAction* item : icon->contextMenu()->actions())
            if (item->isCheckable() && item->text() == label) return item;
        return nullptr;
    }
};

TEST_F(TrayDbusTest, ExternalSelectionUpdatesMenuAndSurvivesRefresh)
{
    QSignalSpy manual(&dbus, &DbusInterface::manualAppSelected);
    QSignalSpy fromTray(&tray, &TrayApp::appChanged);
    ASSERT_TRUE(setActiveApp(QStringLiteral("Spotify")));
    ASSERT_NE(action(QStringLiteral("Spotify")), nullptr);
    EXPECT_TRUE(action(QStringLiteral("Spotify"))->isChecked());
    EXPECT_EQ(config.defaultProfile().primaryApp(), QStringLiteral("Spotify"));
    EXPECT_EQ(manual.count(), 1);
    EXPECT_EQ(fromTray.count(), 0);

    ASSERT_TRUE(setActiveApp(QStringLiteral("firefox")));
    ASSERT_NE(action(QStringLiteral("firefox")), nullptr);
    EXPECT_TRUE(action(QStringLiteral("firefox"))->isChecked());
    EXPECT_FALSE(action(QStringLiteral("Spotify"))->isChecked());
    EXPECT_EQ(config.selectedApp(), QStringLiteral("firefox"));
    QFile saved(tmp.path() + QStringLiteral("/config.json"));
    ASSERT_TRUE(saved.open(QIODevice::ReadOnly));
    const QByteArray beforeRefresh = saved.readAll();
    saved.close();
    tray.rebuildMenu();
    ASSERT_TRUE(saved.open(QIODevice::ReadOnly));
    EXPECT_EQ(saved.readAll(), beforeRefresh);
    EXPECT_TRUE(action(QStringLiteral("firefox"))->isChecked());
    ASSERT_TRUE(setActiveApp(QStringLiteral("firefox")));
    EXPECT_EQ(manual.count(), 2);
    ASSERT_TRUE(setActiveApp(QStringLiteral("spotify"))); // binary is not in the D-Bus Apps list
    EXPECT_EQ(dbus.activeApp(), QStringLiteral("firefox"));
    EXPECT_TRUE(action(QStringLiteral("firefox"))->isChecked());
    EXPECT_EQ(fromTray.count(), 0);

    const auto apps = volume.apps;
    volume.apps.clear();
    emit volume.appsReady(volume.apps);
    EXPECT_EQ(config.selectedApp(), QStringLiteral("firefox"));
    EXPECT_EQ(action(QStringLiteral("firefox")), nullptr);
    volume.apps = apps;
    emit volume.appsReady(volume.apps);
    ASSERT_NE(action(QStringLiteral("firefox")), nullptr);
    EXPECT_TRUE(action(QStringLiteral("firefox"))->isChecked());
}

TEST_F(TrayDbusTest, ManualSelectionClearsTemporaryFocusDisplay)
{
    ASSERT_TRUE(setActiveApp(QStringLiteral("Spotify")));
    tray.setDisplayedApp(QStringLiteral("firefox"));
    dbus.setFocusedApp(QStringLiteral("firefox"));
    ASSERT_TRUE(action(QStringLiteral("firefox"))->isChecked());
    ASSERT_TRUE(setActiveApp(QStringLiteral("Spotify")));
    EXPECT_TRUE(action(QStringLiteral("Spotify"))->isChecked());
    EXPECT_EQ(tray.currentApp(), QStringLiteral("Spotify"));
    tray.setDisplayedApp(QStringLiteral("firefox"));
    dbus.setFocusedApp(QStringLiteral("firefox"));
    ASSERT_TRUE(setActiveApp(QStringLiteral("firefox")));
    EXPECT_EQ(config.defaultProfile().primaryApp(), QStringLiteral("firefox"));
    EXPECT_EQ(tray.currentApp(), QStringLiteral("firefox"));
    tray.rebuildMenu();
    EXPECT_TRUE(action(QStringLiteral("firefox"))->isChecked());
}

TEST_F(TrayDbusTest, BinaryTargetMatchesDisplayNameWhenAppListWasEmpty)
{
    emit volume.appsReady({});
    ASSERT_TRUE(setActiveApp(QStringLiteral("spotify")));
    EXPECT_EQ(config.selectedApp(), QStringLiteral("spotify"));
    emit volume.appsReady(volume.apps);
    ASSERT_NE(action(QStringLiteral("Spotify")), nullptr);
    EXPECT_TRUE(action(QStringLiteral("Spotify"))->isChecked());
}
} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    qputenv("PULSE_SERVER", QByteArrayLiteral("unix:/nonexistent/keyboard-volume-test"));
    qputenv("PIPEWIRE_RUNTIME_DIR", QByteArrayLiteral("/nonexistent/keyboard-volume-test"));
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
