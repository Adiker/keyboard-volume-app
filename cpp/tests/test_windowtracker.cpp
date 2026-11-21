#include "windowtracker.h"
#include "windowtracker_ipc.h"

#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>

#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

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
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        qunsetenv("NIRI_SOCKET");
        qunsetenv("SWAYSOCK");
        qunsetenv("I3SOCK");
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

namespace
{

int listenUnixSocket(const QString& path)
{
    const QByteArray utf8 = path.toUtf8();
    sockaddr_un addr{};
    if (static_cast<size_t>(utf8.size()) >= sizeof(addr.sun_path)) return -1;
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, utf8.constData(), static_cast<size_t>(utf8.size()) + 1);
    ::unlink(utf8.constData());
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(fd, 4) != 0)
    {
        ::close(fd);
        return -1;
    }
    return fd;
}

} // namespace

TEST(WindowTrackerIpcParsers, HyprlandParsesEventLinesAndActiveWindowJson)
{
    using namespace windowtracker_ipc;
    EXPECT_EQ(parseHyprlandEventLine("activewindow>>firefox,Page, with comma\n"),
              QStringLiteral("firefox"));
    EXPECT_EQ(parseHyprlandEventLine("activewindow>>,\n"), QStringLiteral(""));
    EXPECT_FALSE(parseHyprlandEventLine("workspace>>2\n").has_value());

    EXPECT_EQ(parseHyprlandActiveWindowJson(R"({"class":"Spotify","title":"Track"})"),
              QStringLiteral("Spotify"));
    EXPECT_FALSE(parseHyprlandActiveWindowJson("{}").has_value());
}

TEST(WindowTrackerIpcParsers, NiriTracksWindowMapAndFocusChanges)
{
    using namespace windowtracker_ipc;
    NiriFocusState state;

    const auto initial = applyNiriEventLine(
        R"({"WindowsChanged":{"windows":[{"id":1,"app_id":"firefox","is_focused":false},{"id":2,"app_id":"spotify","is_focused":true}]}})",
        state);
    EXPECT_EQ(initial, QStringLiteral("spotify"));

    const auto focused1 = applyNiriEventLine(R"({"WindowFocusChanged":{"id":1}})", state);
    EXPECT_EQ(focused1, QStringLiteral("firefox"));

    const auto opened = applyNiriEventLine(
        R"({"WindowOpenedOrChanged":{"window":{"id":3,"app_id":"discord","is_focused":true}}})",
        state);
    EXPECT_EQ(opened, QStringLiteral("discord"));

    const auto closed = applyNiriEventLine(R"({"WindowClosed":{"id":3}})", state);
    EXPECT_EQ(closed, QStringLiteral(""));

    const auto cleared = applyNiriEventLine(R"({"WindowFocusChanged":{"id":null}})", state);
    EXPECT_EQ(cleared, QStringLiteral(""));
}

TEST(WindowTrackerIpcParsers, SwayParsesTreeAndWindowFocusEvents)
{
    using namespace windowtracker_ipc;
    const QByteArray tree = R"({
      "nodes": [
        {"focused": false, "app_id": "alacritty", "nodes": []},
        {"focused": false, "nodes": [
          {"focused": true, "app_id": null, "window_properties": {"class": "Spotify"}}
        ]}
      ],
      "floating_nodes": []
    })";
    EXPECT_EQ(extractFocusedAppFromSwayTree(tree), QStringLiteral("Spotify"));

    EXPECT_EQ(parseSwayWindowEvent(R"({"change":"focus","container":{"app_id":"firefox"}})"),
              QStringLiteral("firefox"));
    EXPECT_FALSE(
        parseSwayWindowEvent(R"({"change":"title","container":{"app_id":"firefox"}})").has_value());
}

TEST(WindowTrackerIpcLive, HyprlandSocketStreamsInitialAndLiveFocus)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString sig = QStringLiteral("test-sig");
    ASSERT_TRUE(QDir(tmp.path()).mkpath(QStringLiteral("hypr/") + sig));
    const QString cmdPath = windowtracker_ipc::hyprlandSocketPath(sig, tmp.path(), false);
    const QString evPath = windowtracker_ipc::hyprlandSocketPath(sig, tmp.path(), true);

    const int cmdSrv = listenUnixSocket(cmdPath);
    const int evSrv = listenUnixSocket(evPath);
    ASSERT_GE(cmdSrv, 0);
    ASSERT_GE(evSrv, 0);

    std::thread serverThread(
        [cmdSrv, evSrv]()
        {
            const int c = ::accept(cmdSrv, nullptr, nullptr);
            if (c >= 0)
            {
                char req[64];
                (void)::read(c, req, sizeof(req));
                const QByteArray resp = R"({"class":"spotify"})";
                (void)::write(c, resp.constData(), static_cast<size_t>(resp.size()));
                ::close(c);
            }
            const int e = ::accept(evSrv, nullptr, nullptr);
            if (e >= 0)
            {
                const QByteArray ev = "activewindow>>firefox,Home\n";
                (void)::write(e, ev.constData(), static_cast<size_t>(ev.size()));
                usleep(150000);
                ::close(e);
            }
            ::close(cmdSrv);
            ::close(evSrv);
        });

    std::atomic<bool> running{true};
    QStringList seen;
    const bool ok = windowtracker_ipc::runHyprlandIpc(
        cmdPath, evPath, running,
        [&](const QString& app)
        {
            seen.append(app);
            if (seen.size() >= 2) running.store(false);
        },
        [](const QString&) {});
    serverThread.join();

    EXPECT_TRUE(ok);
    ASSERT_EQ(seen.size(), 2);
    EXPECT_EQ(seen[0], QStringLiteral("spotify"));
    EXPECT_EQ(seen[1], QStringLiteral("firefox"));
}

TEST(WindowTrackerIpcLive, NiriSocketStreamsEventStreamFocus)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString sockPath = QDir(tmp.path()).filePath(QStringLiteral("niri.sock"));
    const int srv = listenUnixSocket(sockPath);
    ASSERT_GE(srv, 0);

    std::thread serverThread(
        [srv]()
        {
            const int c = ::accept(srv, nullptr, nullptr);
            if (c >= 0)
            {
                char req[64];
                (void)::read(c, req, sizeof(req));
                const QByteArray events =
                    "{\"Ok\":\"Handled\"}\n"
                    "{\"WindowsChanged\":{\"windows\":[{\"id\":1,\"app_id\":\"mpv\",\"is_focused\":"
                    "true},"
                    "{\"id\":2,\"app_id\":\"firefox\",\"is_focused\":false}]}}\n"
                    "{\"WindowFocusChanged\":{\"id\":2}}\n";
                (void)::write(c, events.constData(), static_cast<size_t>(events.size()));
                usleep(150000);
                ::close(c);
            }
            ::close(srv);
        });

    std::atomic<bool> running{true};
    QStringList seen;
    const bool ok = windowtracker_ipc::runNiriIpc(
        sockPath, running,
        [&](const QString& app)
        {
            seen.append(app);
            if (seen.size() >= 2) running.store(false);
        },
        [](const QString&) {});
    serverThread.join();

    EXPECT_TRUE(ok);
    ASSERT_EQ(seen.size(), 2);
    EXPECT_EQ(seen[0], QStringLiteral("mpv"));
    EXPECT_EQ(seen[1], QStringLiteral("firefox"));
}

TEST(WindowTrackerIpcLive, SwaySocketQueriesTreeAndStreamsWindowFocus)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString sockPath = QDir(tmp.path()).filePath(QStringLiteral("sway.sock"));
    const int srv = listenUnixSocket(sockPath);
    ASSERT_GE(srv, 0);

    std::thread serverThread(
        [srv]()
        {
            const int c = ::accept(srv, nullptr, nullptr);
            if (c >= 0)
            {
                char buf[256];
                // Read GET_TREE header + reply
                (void)::read(c, buf, 14);
                const QByteArray treeReply = windowtracker_ipc::encodeSwayIpcMessage(
                    windowtracker_ipc::kSwayMsgGetTree,
                    R"({"nodes":[{"focused":true,"app_id":"vlc"}]})");
                (void)::write(c, treeReply.constData(), static_cast<size_t>(treeReply.size()));

                // Read SUBSCRIBE + reply
                (void)::read(c, buf, sizeof(buf));
                const QByteArray subReply = windowtracker_ipc::encodeSwayIpcMessage(
                    windowtracker_ipc::kSwayMsgSubscribe, R"({"success":true})");
                (void)::write(c, subReply.constData(), static_cast<size_t>(subReply.size()));

                // Emit window focus event
                const QByteArray ev = windowtracker_ipc::encodeSwayIpcMessage(
                    windowtracker_ipc::kSwayEventWindow,
                    R"({"change":"focus","container":{"app_id":"discord"}})");
                (void)::write(c, ev.constData(), static_cast<size_t>(ev.size()));
                usleep(150000);
                ::close(c);
            }
            ::close(srv);
        });

    std::atomic<bool> running{true};
    QStringList seen;
    const bool ok = windowtracker_ipc::runSwayIpc(
        sockPath, running,
        [&](const QString& app)
        {
            seen.append(app);
            if (seen.size() >= 2) running.store(false);
        },
        [](const QString&) {});
    serverThread.join();

    EXPECT_TRUE(ok);
    ASSERT_EQ(seen.size(), 2);
    EXPECT_EQ(seen[0], QStringLiteral("vlc"));
    EXPECT_EQ(seen[1], QStringLiteral("discord"));
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
