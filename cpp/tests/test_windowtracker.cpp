#include "windowtracker.h"

#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTimer>
#include <QUuid>

class FakeScripting;
class FakeScript : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kwin.Script")
  public:
    FakeScript(FakeScripting* owner, QString source, bool foreign)
        : m_owner(owner), m_source(std::move(source)), m_foreign(foreign)
    {
    }
    bool registered = false;
    QString path;
  public slots:
    Q_SCRIPTABLE void run();

  private:
    FakeScripting* m_owner;
    QString m_source;
    bool m_foreign;
};

class FakeScripting : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kwin.Scripting")
  public:
    explicit FakeScripting(int collision, bool noHandshake) : noHandshake(noHandshake)
    {
        for (int i = 0; i < collision; ++i)
        {
            auto* foreign = new FakeScript(this, {}, true);
            foreign->path = QStringLiteral("/Scripting/Script%1").arg(collision + i);
            foreign->registered = QDBusConnection::sessionBus().registerObject(
                foreign->path, foreign, QDBusConnection::ExportScriptableSlots);
            scripts.append({QStringLiteral("foreign%1").arg(i), foreign});
        }
    }
    bool noHandshake;
    int foreignRuns = 0;
    QList<QPair<QString, FakeScript*>> scripts;
  public slots:
    Q_SCRIPTABLE int loadScript(const QString& file, const QString& name)
    {
        if (isScriptLoaded(name)) return -1;
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly)) return -1;
        const int id = static_cast<int>(scripts.size());
        auto* script = new FakeScript(this, QString::fromUtf8(f.readAll()), false);
        script->path = QStringLiteral("/Scripting/Script%1").arg(id);
        script->registered = QDBusConnection::sessionBus().registerObject(
            script->path, script, QDBusConnection::ExportScriptableSlots);
        scripts.append({name, script});
        return id;
    }
    Q_SCRIPTABLE bool isScriptLoaded(const QString& name) const
    {
        for (const auto& script : scripts)
            if (script.first == name) return true;
        return false;
    }
    Q_SCRIPTABLE bool unloadScript(const QString& name)
    {
        for (int i = 0; i < scripts.size(); ++i)
        {
            if (scripts[i].first != name) continue;
            auto* script = scripts.takeAt(i).second;
            if (script->registered) QDBusConnection::sessionBus().unregisterObject(script->path);
            script->deleteLater();
            return true;
        }
        return false;
    }
    Q_SCRIPTABLE int foreignRunCount() const
    {
        return foreignRuns;
    }
    Q_SCRIPTABLE int loadedCount() const
    {
        return static_cast<int>(scripts.size());
    }
};

void FakeScript::run()
{
    if (m_foreign)
    {
        ++m_owner->foreignRuns;
        return;
    }
    const auto service = QRegularExpression(QStringLiteral("const service = \\\"([^\\\"]+)\\\""))
                             .match(m_source)
                             .captured(1);
    const auto token = QRegularExpression(QStringLiteral("const token = \\\"([^\\\"]+)\\\""))
                           .match(m_source)
                           .captured(1);
    if (service.isEmpty() || token.isEmpty() || m_owner->noHandshake) return;
    auto ready = QDBusMessage::createMethodCall(
        service, QStringLiteral("/org/keyboardvolumeapp/WindowTracker"),
        QStringLiteral("org.keyboardvolumeapp.WindowTracker"), QStringLiteral("TrackerReady"));
    ready << QStringLiteral("incorrect-token");
    QDBusConnection::sessionBus().asyncCall(ready);
    ready.setArguments({token});
    QDBusConnection::sessionBus().asyncCall(ready);
    auto focus = QDBusMessage::createMethodCall(
        service, QStringLiteral("/org/keyboardvolumeapp/WindowTracker"),
        QStringLiteral("org.keyboardvolumeapp.WindowTracker"), QStringLiteral("ReportFocusedApp"));
    focus << QStringLiteral("stale-player") << QStringLiteral("incorrect-token");
    QDBusConnection::sessionBus().asyncCall(focus);
    focus.setArguments({QStringLiteral("limusic"), token});
    QDBusConnection::sessionBus().asyncCall(focus);
}

class TrackerTest : public ::testing::Test
{
  protected:
    QProcess server;
    QString service;
    void startServer(int collision, bool noHandshake = false)
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("FAKE_COLLISION"), QString::number(collision));
        env.insert(QStringLiteral("FAKE_NO_HANDSHAKE"),
                   noHandshake ? QStringLiteral("1") : QStringLiteral("0"));
        server.setProcessEnvironment(env);
        server.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--fake")});
        ASSERT_TRUE(server.waitForStarted(3000));
        ASSERT_TRUE(server.waitForReadyRead(3000));
        ASSERT_TRUE(server.readAllStandardOutput().contains("ready"));
        service = QStringLiteral("org.keyboardvolumeapp.test.p") +
                  QString::number(QCoreApplication::applicationPid());
        ASSERT_TRUE(QDBusConnection::sessionBus().registerService(service));
        qputenv("WAYLAND_DISPLAY", "fake-test");
        qunsetenv("DISPLAY");
    }
    int value(const char* method)
    {
        QDBusInterface api(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
                           QStringLiteral("org.kde.kwin.Scripting"), QDBusConnection::sessionBus());
        QDBusReply<int> reply = api.call(QString::fromLatin1(method));
        return reply.isValid() ? reply.value() : -1;
    }
    void TearDown() override
    {
        QDBusConnection::sessionBus().unregisterService(service);
        server.terminate();
        if (!server.waitForFinished(3000))
        {
            server.kill();
            server.waitForFinished();
        }
    }
};

TEST_F(TrackerTest, CollisionReservesOccupiedIdsWithoutRunningForeignScript)
{
    startServer(true);
    WindowTracker tracker(nullptr, service, QStringLiteral("test-focus"));
    QSignalSpy focus(&tracker, &WindowTracker::focusedBinaryChanged);
    QSignalSpy errors(&tracker, &WindowTracker::error);
    tracker.start();
    ASSERT_TRUE(focus.wait(5000));
    EXPECT_EQ(focus.takeFirst().at(0).toString(), QStringLiteral("limusic"));
    EXPECT_EQ(value("foreignRunCount"), 0);
    EXPECT_EQ(value("loadedCount"), 3);
    tracker.stop();
    EXPECT_EQ(value("loadedCount"), 1);
    EXPECT_EQ(value("foreignRunCount"), 0);
    EXPECT_EQ(errors.size(), 0);
}

TEST_F(TrackerTest, RepeatedCollisionsDoNotRunOtherScripts)
{
    startServer(3);
    WindowTracker tracker(nullptr, service, QStringLiteral("test-focus"));
    QSignalSpy focus(&tracker, &WindowTracker::focusedBinaryChanged);
    tracker.start();
    ASSERT_TRUE(focus.wait(5000));
    EXPECT_EQ(focus.takeFirst().at(0).toString(), QStringLiteral("limusic"));
    EXPECT_EQ(value("foreignRunCount"), 0);
    EXPECT_EQ(value("loadedCount"), 7);
    tracker.stop();
    EXPECT_EQ(value("loadedCount"), 3);
}

TEST_F(TrackerTest, CollisionLimitFailsSafelyAndCleansReservations)
{
    startServer(17);
    WindowTracker tracker(nullptr, service, QStringLiteral("test-focus"));
    QSignalSpy errors(&tracker, &WindowTracker::error);
    tracker.start();
    ASSERT_TRUE(errors.wait(5000));
    EXPECT_TRUE(errors.first().at(0).toString().contains(QStringLiteral("unoccupied")));
    tracker.stop();
    EXPECT_EQ(value("foreignRunCount"), 0);
    EXPECT_EQ(value("loadedCount"), 17);
}

TEST_F(TrackerTest, StartsWithoutCollision)
{
    startServer(false);
    WindowTracker tracker(nullptr, service, QStringLiteral("test-focus"));
    QSignalSpy focus(&tracker, &WindowTracker::focusedBinaryChanged);
    tracker.start();
    ASSERT_TRUE(focus.wait(5000));
    EXPECT_EQ(focus.takeFirst().at(0).toString(), QStringLiteral("limusic"));
    EXPECT_EQ(value("loadedCount"), 1);
    tracker.stop();
    EXPECT_EQ(value("loadedCount"), 0);
}

TEST_F(TrackerTest, MissingHandshakeFailsAndCleansUp)
{
    startServer(false, true);
    WindowTracker tracker(nullptr, service, QStringLiteral("test-focus"));
    QSignalSpy errors(&tracker, &WindowTracker::error);
    tracker.start();
    ASSERT_TRUE(errors.wait(5000));
    EXPECT_TRUE(errors.first().at(0).toString().contains(QStringLiteral("confirm startup")));
    tracker.stop();
    EXPECT_EQ(value("loadedCount"), 0);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--fake")))
    {
        auto bus = QDBusConnection::sessionBus();
        if (!bus.registerService(QStringLiteral("org.kde.KWin"))) return 2;
        FakeScripting fake(qEnvironmentVariableIntValue("FAKE_COLLISION"),
                           qEnvironmentVariableIntValue("FAKE_NO_HANDSHAKE") != 0);
        if (!bus.registerObject(QStringLiteral("/Scripting"), &fake,
                                QDBusConnection::ExportScriptableSlots))
            return 3;
        printf("ready\n");
        fflush(stdout);
        return app.exec();
    }
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#include "test_windowtracker.moc"
