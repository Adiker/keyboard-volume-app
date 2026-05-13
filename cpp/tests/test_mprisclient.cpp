// test_mprisclient.cpp
// Tests for MprisClient — fake MPRIS player registered on session D-Bus.
//
// Tests are skipped automatically when the session bus is unavailable
// (headless CI without dbus-run-session). In CI the workflow wraps ctest
// with dbus-run-session so the bus is always present there.
//
// Requires QCoreApplication — provided by the custom main() at the bottom.

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusAbstractAdaptor>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDataStream>
#include <QEventLoop>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include "config.h"
#include "mprisclient.h"

// ─── helpers ──────────────────────────────────────────────────────────────────

static bool busAvailable()
{
    return QDBusConnection::sessionBus().isConnected();
}

// Macro: skip test when D-Bus is unavailable.
// clang-format off
#define SKIP_IF_NO_DBUS()                                          \
    do {                                                           \
        if (!busAvailable()) GTEST_SKIP() << "Session D-Bus not available"; \
    } while (false)
// clang-format on

// Pump the Qt event loop for up to maxMs milliseconds or until predicate
// returns true. Uses QEventLoop::exec() with a short timer — this is required
// to deliver cross-thread signals from the sessionBus() background I/O thread
// to the main thread. processEvents() alone is not sufficient for that.
static bool waitFor(std::function<bool()> pred, int maxMs = 2000)
{
    QDeadlineTimer deadline(maxMs);
    while (!deadline.hasExpired())
    {
        if (pred()) return true;
        QEventLoop loop;
        QTimer::singleShot(30, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return pred();
}

static bool writeSilentWav(const QString& path, int durationMs)
{
    constexpr quint16 channels = 1;
    constexpr quint32 sampleRate = 44100;
    constexpr quint16 bitsPerSample = 16;
    const quint16 blockAlign = channels * bitsPerSample / 8;
    const quint32 byteRate = sampleRate * blockAlign;
    const quint32 sampleCount = sampleRate * durationMs / 1000;
    const quint32 dataSize = sampleCount * blockAlign;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;

    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + dataSize);
    out.writeRawData("WAVE", 4);
    out.writeRawData("fmt ", 4);
    out << quint32(16);
    out << quint16(1);
    out << channels;
    out << sampleRate;
    out << byteRate;
    out << blockAlign;
    out << bitsPerSample;
    out.writeRawData("data", 4);
    out << dataSize;

    QByteArray silence;
    silence.resize(static_cast<qsizetype>(dataSize));
    silence.fill('\0');
    return file.write(silence) == silence.size();
}

// ─── Fake MPRIS player adaptor ────────────────────────────────────────────────

class FakeMprisPlayerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")

    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(qlonglong Position READ position)

  public:
    explicit FakeMprisPlayerAdaptor(QObject* parent) : QDBusAbstractAdaptor(parent) {}

    QString playbackStatus() const
    {
        return m_status;
    }
    bool canSeek() const
    {
        return m_canSeek;
    }
    bool canControl() const
    {
        return true;
    }
    QVariantMap metadata() const
    {
        return m_metadata;
    }
    qlonglong position() const
    {
        return m_position;
    }

    // Call after registering on a connection so we can send signals on it.
    void setConnection(const QDBusConnection& conn)
    {
        m_conn = conn;
    }

    void setStatus(const QString& s)
    {
        m_status = s;
        emitPropertiesChanged({{QStringLiteral("PlaybackStatus"), s}});
    }

    void setMetadata(const QString& title, const QString& artist, qint64 lengthUs,
                     const QString& trackId)
    {
        setMetadataWithUrl(title, artist, lengthUs, trackId, QString{});
    }

    void setMetadataWithUrl(const QString& title, const QString& artist, qint64 lengthUs,
                            const QString& trackId, const QString& url)
    {
        m_metadata[QStringLiteral("xesam:title")] = title;
        m_metadata[QStringLiteral("xesam:artist")] = QStringList{artist};
        m_metadata[QStringLiteral("mpris:length")] = lengthUs;
        m_metadata[QStringLiteral("mpris:trackid")] = QVariant::fromValue(QDBusObjectPath(trackId));
        if (url.isEmpty())
            m_metadata.remove(QStringLiteral("xesam:url"));
        else
            m_metadata[QStringLiteral("xesam:url")] = url;
        emitPropertiesChanged({{QStringLiteral("Metadata"), m_metadata}});
    }

    void setPosition(qlonglong pos)
    {
        m_position = pos;
    }

    void setCanGoNext(bool v)
    {
        m_canGoNext = v;
        emitPropertiesChanged({{QStringLiteral("CanGoNext"), v}});
    }
    void setCanGoPrevious(bool v)
    {
        m_canGoPrevious = v;
        emitPropertiesChanged({{QStringLiteral("CanGoPrevious"), v}});
    }

    // Simulate Harmonoid-style high-frequency Position updates via PropertiesChanged.
    void emitPositionChanged(qint64 pos)
    {
        emitPropertiesChanged({{QStringLiteral("Position"), pos}});
    }

    void emitMetadataAndPositionChanged(const QString& title, const QString& artist,
                                        qint64 lengthUs, const QString& trackId, qint64 pos)
    {
        m_metadata[QStringLiteral("xesam:title")] = title;
        m_metadata[QStringLiteral("xesam:artist")] = QStringList{artist};
        m_metadata[QStringLiteral("mpris:length")] = lengthUs;
        m_metadata[QStringLiteral("mpris:trackid")] = QVariant::fromValue(QDBusObjectPath(trackId));
        m_position = pos;
        emitPropertiesChanged(
            {{QStringLiteral("Metadata"), m_metadata}, {QStringLiteral("Position"), pos}});
    }

    void emitEmptyMetadataChanged()
    {
        m_metadata.clear();
        emitPropertiesChanged({{QStringLiteral("Metadata"), m_metadata}});
    }

    void emitSeeked(qint64 pos)
    {
        // Emit via the dedicated connection so the signal has the correct sender.
        QDBusMessage msg = QDBusMessage::createSignal(
            QStringLiteral("/org/mpris/MediaPlayer2"),
            QStringLiteral("org.mpris.MediaPlayer2.Player"), QStringLiteral("Seeked"));
        msg << pos;
        m_conn.send(msg);
    }

  signals:
    Q_SCRIPTABLE void Seeked(qint64 position);

  public slots:
    Q_SCRIPTABLE void Play() {}
    Q_SCRIPTABLE void Pause() {}
    Q_SCRIPTABLE void Stop() {}
    Q_SCRIPTABLE void Next() {}
    Q_SCRIPTABLE void Previous() {}
    Q_SCRIPTABLE void SetPosition(const QDBusObjectPath&, qint64) {}
    Q_SCRIPTABLE void Seek(qint64) {}

  private:
    void emitPropertiesChanged(const QVariantMap& changed)
    {
        // Send on the dedicated per-player connection so the signal arrives
        // with the well-known service name as sender (Qt resolves it).
        QDBusMessage msg = QDBusMessage::createSignal(
            QStringLiteral("/org/mpris/MediaPlayer2"),
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"));
        msg << QStringLiteral("org.mpris.MediaPlayer2.Player") << changed << QStringList{};
        m_conn.send(msg);
    }

    QString m_status = QStringLiteral("Stopped");
    bool m_canSeek = true;
    QVariantMap m_metadata;
    qlonglong m_position = 0;
    QDBusConnection m_conn{QStringLiteral("unset")};
};

class FakeMprisRootAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)

  public:
    explicit FakeMprisRootAdaptor(QObject* parent) : QDBusAbstractAdaptor(parent) {}
    QString identity() const
    {
        return QStringLiteral("FakePlayer");
    }
    bool canQuit() const
    {
        return false;
    }
    bool canRaise() const
    {
        return false;
    }

  public slots:
    Q_SCRIPTABLE void Quit() {}
    Q_SCRIPTABLE void Raise() {}
};

// ─── FakePlayer — registers a full MPRIS service on the session bus ───────────
// Uses a dedicated QDBusConnection (not sessionBus()) so that signals sent by
// the fake player are delivered to MprisClient which listens on sessionBus().
// Qt does not loop-back signals sent and received on the same connection object.

struct FakePlayer
{
    QObject* endpoint = nullptr;
    FakeMprisPlayerAdaptor* player = nullptr;
    QString service;
    QDBusConnection conn;

    explicit FakePlayer(const QString& suffix)
        : service(QStringLiteral("org.mpris.MediaPlayer2.") + suffix),
          conn(QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                             QStringLiteral("fake_") + suffix))
    {
        endpoint = new QObject();
        new FakeMprisRootAdaptor(endpoint);
        player = new FakeMprisPlayerAdaptor(endpoint);
        player->setConnection(conn);

        conn.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), endpoint,
                            QDBusConnection::ExportAdaptors);
        conn.registerService(service);
    }

    ~FakePlayer()
    {
        conn.unregisterObject(QStringLiteral("/org/mpris/MediaPlayer2"));
        conn.unregisterService(service);
        delete endpoint;
        QDBusConnection::disconnectFromBus(QStringLiteral("fake_") +
                                           service.mid(service.lastIndexOf(QLatin1Char('.')) + 1));
    }
};

// ─── Fixture ──────────────────────────────────────────────────────────────────

class MprisClientTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        if (!busAvailable()) return;
        m_tmpDir = std::make_unique<QTemporaryDir>();
        m_config = std::make_unique<Config>(m_tmpDir->path());
    }

    void TearDown() override {}

    std::unique_ptr<QTemporaryDir> m_tmpDir;
    std::unique_ptr<Config> m_config;
};

// ─── Tests ────────────────────────────────────────────────────────────────────

TEST_F(MprisClientTest, NoPlayerEmittedWhenNonePresent)
{
    SKIP_IF_NO_DBUS();

    MprisClient client(m_config.get());
    QSignalSpy spy(&client, &MprisClient::noPlayer);

    // No fake player registered — client should have no active player.
    EXPECT_FALSE(client.activePlayer().service.isEmpty() == false);
    // noPlayer() is not emitted on construction (nothing to lose), so spy stays 0.
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(MprisClientTest, DetectsExistingPlayingPlayer)
{
    SKIP_IF_NO_DBUS();

    FakePlayer fp(QStringLiteral("spotify"));
    fp.player->setMetadata(QStringLiteral("Test Song"), QStringLiteral("Artist"), 180000000LL,
                           QStringLiteral("/track/1"));
    fp.player->setStatus(QStringLiteral("Playing"));

    // Give D-Bus time to settle before creating client
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    MprisClient client(m_config.get());
    QSignalSpy trackSpy(&client, &MprisClient::trackChanged);

    // Client scans existing services on construction — should find spotify
    const bool found = waitFor([&] { return !client.activePlayer().service.isEmpty(); });
    EXPECT_TRUE(found);
    EXPECT_TRUE(client.activePlayer().service.contains(QLatin1String("spotify")));
    EXPECT_EQ(client.activePlayer().status.toStdString(), "Playing");
}

TEST_F(MprisClientTest, TrackChangedEmittedOnMetadataUpdate)
{
    SKIP_IF_NO_DBUS();

    FakePlayer fp(QStringLiteral("strawberry"));
    fp.player->setStatus(QStringLiteral("Playing"));

    MprisClient client(m_config.get());
    QSignalSpy spy(&client, &MprisClient::trackChanged);

    // Wait for player to be detected (may emit trackChanged with empty metadata)
    waitFor([&] { return !client.activePlayer().service.isEmpty(); });

    // Clear any initial emissions from the scan phase, then trigger a real update
    spy.clear();

    fp.player->setMetadata(QStringLiteral("My Song"), QStringLiteral("My Artist"), 240000000LL,
                           QStringLiteral("/track/42"));

    const bool got = waitFor([&] { return spy.count() > 0; });
    EXPECT_TRUE(got);
    if (!spy.isEmpty())
    {
        EXPECT_EQ(spy.last()[0].toString().toStdString(), "My Song");
        EXPECT_EQ(spy.last()[1].toString().toStdString(), "My Artist");
        EXPECT_EQ(spy.last()[2].toLongLong(), 240000000LL);
        EXPECT_TRUE(spy.last()[3].toBool()); // canSeek && length > 0
    }
}

TEST_F(MprisClientTest, SeekedSignalForwardsPosition)
{
    SKIP_IF_NO_DBUS();

    FakePlayer fp(QStringLiteral("strawberry"));
    fp.player->setStatus(QStringLiteral("Playing"));
    fp.player->setMetadata(QStringLiteral("T"), QStringLiteral("A"), 100000000LL,
                           QStringLiteral("/t/1"));

    MprisClient client(m_config.get());
    QSignalSpy spy(&client, &MprisClient::positionChanged);

    waitFor([&] { return !client.activePlayer().service.isEmpty(); });

    fp.player->emitSeeked(55000000LL);

    const bool got = waitFor([&] { return spy.count() > 0; });
    EXPECT_TRUE(got);
    if (!spy.isEmpty())
    {
        EXPECT_EQ(spy.last()[0].toLongLong(), 55000000LL);
    }
}

TEST_F(MprisClientTest, NoPlayerEmittedWhenPlayerDisappears)
{
    SKIP_IF_NO_DBUS();

    auto fp = std::make_unique<FakePlayer>(QStringLiteral("harmonoid"));
    fp->player->setStatus(QStringLiteral("Playing"));
    fp->player->setMetadata(QStringLiteral("T"), QStringLiteral("A"), 100000000LL,
                            QStringLiteral("/t/1"));

    MprisClient client(m_config.get());
    QSignalSpy spy(&client, &MprisClient::noPlayer);

    waitFor([&] { return !client.activePlayer().service.isEmpty(); });

    // Remove the player
    fp.reset();

    const bool gone = waitFor([&] { return spy.count() > 0; }, 3000);
    EXPECT_TRUE(gone);
    EXPECT_TRUE(client.activePlayer().service.isEmpty());
}

TEST_F(MprisClientTest, PriorityOrderRespected)
{
    SKIP_IF_NO_DBUS();

    // Both players playing — spotify has higher priority (index 0 in defaults)
    FakePlayer fp1(QStringLiteral("strawberry")); // priority 2
    fp1.player->setStatus(QStringLiteral("Playing"));
    fp1.player->setMetadata(QStringLiteral("S"), QStringLiteral("A"), 100000000LL,
                            QStringLiteral("/t/1"));

    // Give strawberry time to register before spotify
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    FakePlayer fp2(QStringLiteral("spotify")); // priority 0
    fp2.player->setStatus(QStringLiteral("Playing"));
    fp2.player->setMetadata(QStringLiteral("S2"), QStringLiteral("A2"), 200000000LL,
                            QStringLiteral("/t/2"));

    MprisClient client(m_config.get());

    waitFor([&] { return !client.activePlayer().service.isEmpty(); }, 3000);

    // Spotify should win due to higher priority
    EXPECT_TRUE(client.activePlayer().service.contains(QLatin1String("spotify")));
}

TEST_F(MprisClientTest, MatchesInstanceSuffixedServiceNames)
{
    SKIP_IF_NO_DBUS();

    OsdConfig osd = m_config->osd();
    osd.trackedPlayers = {QStringLiteral("vlc")};
    m_config->setOsd(osd);

    FakePlayer fp(QStringLiteral("vlc.instance7389"));
    fp.player->setStatus(QStringLiteral("Playing"));
    fp.player->setMetadata(QStringLiteral("Movie"), QStringLiteral(""), 100000000LL,
                           QStringLiteral("/t/1"));

    MprisClient client(m_config.get());

    const bool found = waitFor([&] { return !client.activePlayer().service.isEmpty(); }, 3000);
    EXPECT_TRUE(found);
    EXPECT_TRUE(client.activePlayer().service.contains(QLatin1String("vlc.instance7389")));
}

TEST_F(MprisClientTest, LiveStreamLengthZeroDisablesSeek)
{
    SKIP_IF_NO_DBUS();

    FakePlayer fp(QStringLiteral("youtube"));
    fp.player->setStatus(QStringLiteral("Playing"));
    // length = 0 → live stream
    fp.player->setMetadata(QStringLiteral("Live Stream"), QStringLiteral("Channel"), 0LL,
                           QStringLiteral("/t/live"));

    MprisClient client(m_config.get());
    QSignalSpy spy(&client, &MprisClient::trackChanged);

    waitFor([&] { return spy.count() > 0; }, 3000);

    if (!spy.isEmpty())
    {
        // canSeek should be false when length == 0 (even if CanSeek property is true)
        EXPECT_FALSE(spy.last()[3].toBool());
    }
}

TEST_F(MprisClientTest, ReloadUpdatesTrackedPlayers)
{
    SKIP_IF_NO_DBUS();

    // harmonoid is in default tracked list
    FakePlayer fp(QStringLiteral("harmonoid"));
    fp.player->setStatus(QStringLiteral("Playing"));
    fp.player->setMetadata(QStringLiteral("T"), QStringLiteral("A"), 100000000LL,
                           QStringLiteral("/t/1"));

    MprisClient client(m_config.get());
    waitFor([&] { return !client.activePlayer().service.isEmpty(); });
    EXPECT_TRUE(client.activePlayer().service.contains(QLatin1String("harmonoid")));

    // Remove harmonoid from tracked list and reload
    OsdConfig osd = m_config->osd();
    osd.trackedPlayers = {QStringLiteral("spotify"), QStringLiteral("strawberry")};
    m_config->setOsd(osd);

    QSignalSpy noPlayerSpy(&client, &MprisClient::noPlayer);
    client.reload();

    const bool gone = waitFor([&] { return noPlayerSpy.count() > 0; }, 2000);
    EXPECT_TRUE(gone);
}

TEST_F(MprisClientTest, ReloadReemitsCurrentTrack)
{
    SKIP_IF_NO_DBUS();

    FakePlayer fp(QStringLiteral("spotify"));
    fp.player->setStatus(QStringLiteral("Playing"));
    fp.player->setMetadata(QStringLiteral("Reload Song"), QStringLiteral("Reload Artist"),
                           180000000LL, QStringLiteral("/track/reload"));

    MprisClient client(m_config.get());
    QSignalSpy spy(&client, &MprisClient::trackChanged);

    const bool found = waitFor([&] { return !client.activePlayer().service.isEmpty(); });
    EXPECT_TRUE(found);
    spy.clear();

    client.reload();

    const bool got = waitFor([&] { return spy.count() > 0; }, 2000);
    EXPECT_TRUE(got);
    if (!spy.isEmpty())
    {
        EXPECT_EQ(spy.last()[0].toString().toStdString(), "Reload Song");
        EXPECT_EQ(spy.last()[1].toString().toStdString(), "Reload Artist");
        EXPECT_EQ(spy.last()[2].toLongLong(), 180000000LL);
        EXPECT_TRUE(spy.last()[3].toBool());
    }
}

TEST_F(MprisClientTest, TrackChangedEmittedOnTrackIdUpdate)
{
    SKIP_IF_NO_DBUS();

    FakePlayer fp(QStringLiteral("spotify"));
    fp.player->setStatus(QStringLiteral("Playing"));
    fp.player->setMetadata(QStringLiteral("Same Song"), QStringLiteral("Same Artist"), 180000000LL,
                           QStringLiteral("/track/one"));

    MprisClient client(m_config.get());
    QSignalSpy spy(&client, &MprisClient::trackChanged);

    const bool found = waitFor([&] { return !client.activePlayer().service.isEmpty(); });
    EXPECT_TRUE(found);
    spy.clear();

    fp.player->setMetadata(QStringLiteral("Same Song"), QStringLiteral("Same Artist"), 180000000LL,
                           QStringLiteral("/track/two"));

    const bool got = waitFor([&] { return spy.count() > 0; }, 2000);
    EXPECT_TRUE(got);
}

TEST_F(MprisClientTest, PollPausesWhenNotPlaying)
{
    SKIP_IF_NO_DBUS();

    // Set a very short poll interval so we can observe it quickly
    OsdConfig osd = m_config->osd();
    osd.progressEnabled = true;
    osd.progressPollMs = 200;
    m_config->setOsd(osd);

    FakePlayer fp(QStringLiteral("spotify"));
    fp.player->setStatus(QStringLiteral("Paused"));
    fp.player->setMetadata(QStringLiteral("T"), QStringLiteral("A"), 100000000LL,
                           QStringLiteral("/t/1"));

    MprisClient client(m_config.get());
    QSignalSpy spy(&client, &MprisClient::positionChanged);

    waitFor([&] { return !client.activePlayer().service.isEmpty(); });

    // Wait longer than one poll interval — no positionChanged should fire when Paused
    QCoreApplication::processEvents(QEventLoop::AllEvents, 600);
    EXPECT_EQ(spy.count(), 0);

    // Now set to Playing — positionChanged should start arriving
    fp.player->setPosition(10000000LL);
    fp.player->setStatus(QStringLiteral("Playing"));

    const bool got = waitFor([&] { return spy.count() > 0; }, 2000);
    EXPECT_TRUE(got);
}

TEST_F(MprisClientTest, PollPausesWhenProgressDisabled)
{
    SKIP_IF_NO_DBUS();

    OsdConfig osd = m_config->osd();
    osd.progressEnabled = false;
    osd.progressPollMs = 200;
    m_config->setOsd(osd);

    FakePlayer fp(QStringLiteral("spotify"));
    fp.player->setStatus(QStringLiteral("Playing"));
    fp.player->setMetadata(QStringLiteral("T"), QStringLiteral("A"), 100000000LL,
                           QStringLiteral("/t/1"));

    MprisClient client(m_config.get());
    QSignalSpy spy(&client, &MprisClient::positionChanged);

    waitFor([&] { return !client.activePlayer().service.isEmpty(); });

    QCoreApplication::processEvents(QEventLoop::AllEvents, 600);
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(MprisClientTest, PlayPauseCallsDBusMethod)
{
    SKIP_IF_NO_DBUS();

    FakePlayer fp(QStringLiteral("spotify"));
    fp.player->setStatus(QStringLiteral("Playing"));
    fp.player->setMetadata(QStringLiteral("T"), QStringLiteral("A"), 100000000LL,
                           QStringLiteral("/t/1"));

    MprisClient client(m_config.get());
    ASSERT_TRUE(waitFor([&] { return !client.activePlayer().service.isEmpty(); }));

    client.playPause();

    EXPECT_TRUE(waitFor([&] { return fp.player->playPauseCalled; }));
}

TEST_F(MprisClientTest, NextCallsDBusMethodWhenCanGoNext)
{
    SKIP_IF_NO_DBUS();

    FakePlayer fp(QStringLiteral("spotify"));
    // CanGoNext defaults to true in FakeMprisPlayerAdaptor
    fp.player->setStatus(QStringLiteral("Playing"));
    fp.player->setMetadata(QStringLiteral("T"), QStringLiteral("A"), 100000000LL,
                           QStringLiteral("/t/1"));

    MprisClient client(m_config.get());
    ASSERT_TRUE(waitFor([&] { return !client.activePlayer().service.isEmpty(); }));
    ASSERT_TRUE(waitFor([&] { return client.activePlayer().canGoNext; }));

    client.next();

    EXPECT_TRUE(waitFor([&] { return fp.player->nextCalled; }));
}

TEST_F(MprisClientTest, NextIsNoOpWhenCanNotGoNext)
{
    SKIP_IF_NO_DBUS();

    FakePlayer fp(QStringLiteral("spotify"));
    fp.player->setCanGoNext(false); // Set before client starts so initial fetch sees false
    fp.player->setStatus(QStringLiteral("Playing"));
    fp.player->setMetadata(QStringLiteral("T"), QStringLiteral("A"), 100000000LL,
                           QStringLiteral("/t/1"));

    MprisClient client(m_config.get());
    ASSERT_TRUE(waitFor([&] { return !client.activePlayer().service.isEmpty(); }));

    client.next();

    QCoreApplication::processEvents(QEventLoop::AllEvents, 300);
    EXPECT_FALSE(fp.player->nextCalled);
}

TEST_F(MprisClientTest, PreviousCallsDBusMethodWhenCanGoPrevious)
{
    SKIP_IF_NO_DBUS();

    FakePlayer fp(QStringLiteral("spotify"));
    // CanGoPrevious defaults to true in FakeMprisPlayerAdaptor
    fp.player->setStatus(QStringLiteral("Playing"));
    fp.player->setMetadata(QStringLiteral("T"), QStringLiteral("A"), 100000000LL,
                           QStringLiteral("/t/1"));

    MprisClient client(m_config.get());
    ASSERT_TRUE(waitFor([&] { return !client.activePlayer().service.isEmpty(); }));
    ASSERT_TRUE(waitFor([&] { return client.activePlayer().canGoPrevious; }));

    client.previous();

    EXPECT_TRUE(waitFor([&] { return fp.player->previousCalled; }));
}

TEST_F(MprisClientTest, PreviousFallsBackToSetPositionZeroWhenCanNotGoPrevious)
{
    SKIP_IF_NO_DBUS();

    FakePlayer fp(QStringLiteral("spotify"));
    fp.player->setCanGoPrevious(false); // Set before client starts so initial fetch sees false
    fp.player->setStatus(QStringLiteral("Playing"));
    fp.player->setMetadata(QStringLiteral("T"), QStringLiteral("A"), 100000000LL,
                           QStringLiteral("/t/1"));

    MprisClient client(m_config.get());
    ASSERT_TRUE(waitFor([&] { return !client.activePlayer().service.isEmpty(); }));

    client.previous();

    EXPECT_TRUE(waitFor([&] { return fp.player->setPositionCalled; }));
    EXPECT_EQ(fp.player->setPositionPos, 0LL);
}

// ─── Harmonoid-style Position via PropertiesChanged ───────────────────────────

// Players like Harmonoid send Position via PropertiesChanged at high frequency
// (~12/sec) instead of relying on client polling. The client should emit
// positionChanged directly without calling reevaluateActive.
TEST_F(MprisClientTest, PositionViaPropertiesChangedEmitsPositionChanged)
{
    SKIP_IF_NO_DBUS();

    FakePlayer fp(QStringLiteral("harmonoid"));
    fp.player->setStatus(QStringLiteral("Playing"));
    fp.player->setMetadata(QStringLiteral("Song"), QStringLiteral("Artist"), 200000000LL,
                           QStringLiteral("/t/1"));

    MprisClient client(m_config.get());
    ASSERT_TRUE(waitFor([&] { return !client.activePlayer().service.isEmpty(); }));

    QSignalSpy posSpy(&client, &MprisClient::positionChanged);

    fp.player->emitPositionChanged(42000000LL);

    const bool got = waitFor([&] { return posSpy.count() > 0; }, 2000);
    ASSERT_TRUE(got) << "positionChanged not emitted for Position-only PropertiesChanged";
    EXPECT_EQ(posSpy.first().first().toLongLong(), 42000000LL);
}

// Position-only PropertiesChanged must NOT trigger trackChanged — the track has
// not changed, only the playback position. reevaluateActive should be skipped.
TEST_F(MprisClientTest, PositionOnlyPropertiesChangedDoesNotEmitTrackChanged)
{
    SKIP_IF_NO_DBUS();

    FakePlayer fp(QStringLiteral("harmonoid"));
    fp.player->setStatus(QStringLiteral("Playing"));
    fp.player->setMetadata(QStringLiteral("Song"), QStringLiteral("Artist"), 200000000LL,
                           QStringLiteral("/t/1"));

    MprisClient client(m_config.get());
    ASSERT_TRUE(waitFor([&] { return !client.activePlayer().service.isEmpty(); }));

    QSignalSpy trackSpy(&client, &MprisClient::trackChanged);
    QSignalSpy posSpy(&client, &MprisClient::positionChanged);

    fp.player->emitPositionChanged(10000000LL);

    // Give the signal time to arrive and be processed.
    waitFor([&] { return posSpy.count() > 0; }, 1000);

    EXPECT_EQ(trackSpy.count(), 0) << "trackChanged must not fire for Position-only update";
    EXPECT_GT(posSpy.count(), 0) << "positionChanged must fire for Position-only update";
}

TEST_F(MprisClientTest, HarmonoidStalePollZeroDoesNotOverridePushedPosition)
{
    SKIP_IF_NO_DBUS();

    FakePlayer fp(QStringLiteral("harmonoid"));
    fp.player->setStatus(QStringLiteral("Playing"));
    fp.player->setMetadata(QStringLiteral("Song"), QStringLiteral("Artist"), 200000000LL,
                           QStringLiteral("/t/1"));

    MprisClient client(m_config.get());
    ASSERT_TRUE(waitFor([&] { return !client.activePlayer().service.isEmpty(); }));

    OsdConfig osd = m_config->osd();
    osd.progressEnabled = true;
    osd.progressPollMs = 200;
    m_config->setOsd(osd);
    client.reload();
    fp.player->setPosition(0);

    QSignalSpy posSpy(&client, &MprisClient::positionChanged);
    fp.player->emitPositionChanged(63311019LL);

    ASSERT_TRUE(waitFor([&] { return posSpy.count() > 0; }, 2000));
    EXPECT_EQ(posSpy.last().first().toLongLong(), 63311019LL);
    posSpy.clear();

    QEventLoop loop;
    QTimer::singleShot(500, &loop, &QEventLoop::quit);
    loop.exec();

    for (const QList<QVariant>& args : posSpy)
        EXPECT_NE(args.first().toLongLong(), 0LL)
            << "stale polled zero must not reset Harmonoid progress";
}

TEST_F(MprisClientTest, BundledMetadataAndPositionEmitsTrackBeforePosition)
{
    SKIP_IF_NO_DBUS();

    FakePlayer fp(QStringLiteral("harmonoid"));
    fp.player->setStatus(QStringLiteral("Playing"));
    fp.player->setMetadata(QStringLiteral("Song"), QStringLiteral("Artist"), 200000000LL,
                           QStringLiteral("/t/1"));

    MprisClient client(m_config.get());
    ASSERT_TRUE(waitFor([&] { return !client.activePlayer().service.isEmpty(); }));

    QStringList events;
    qint64 emittedPosition = -1;
    QObject::connect(&client, &MprisClient::trackChanged, &client,
                     [&events](const QString&, const QString&, qint64, bool)
                     { events.append(QStringLiteral("track")); });
    QObject::connect(&client, &MprisClient::positionChanged, &client,
                     [&events, &emittedPosition](qint64 pos)
                     {
                         events.append(QStringLiteral("position"));
                         emittedPosition = pos;
                     });

    fp.player->emitMetadataAndPositionChanged(QStringLiteral("Next Song"), QStringLiteral("Artist"),
                                              210000000LL, QStringLiteral("/t/2"), 12000000LL);

    ASSERT_TRUE(waitFor(
        [&]
        {
            return events.contains(QStringLiteral("track")) &&
                   events.contains(QStringLiteral("position"));
        }))
        << "bundled Metadata+Position update did not emit both track and position";
    ASSERT_GE(events.size(), 2);
    EXPECT_EQ(events.at(0), QStringLiteral("track"));
    EXPECT_EQ(events.at(1), QStringLiteral("position"));
    EXPECT_EQ(emittedPosition, 12000000LL);
}

TEST_F(MprisClientTest, HarmonoidLocalFileDurationOverridesStaleMprisLength)
{
    SKIP_IF_NO_DBUS();

    const QString wavPath = m_tmpDir->path() + QStringLiteral("/real-duration.wav");
    ASSERT_TRUE(writeSilentWav(wavPath, 3000));

    FakePlayer fp(QStringLiteral("harmonoid"));
    fp.player->setStatus(QStringLiteral("Playing"));

    MprisClient client(m_config.get());
    QSignalSpy trackSpy(&client, &MprisClient::trackChanged);

    ASSERT_TRUE(waitFor([&] { return !client.activePlayer().service.isEmpty(); }));
    trackSpy.clear();

    fp.player->setMetadataWithUrl(QStringLiteral("Superstar"), QStringLiteral("Jamelia"),
                                  169456734LL, QStringLiteral("/t/stale"),
                                  QUrl::fromLocalFile(wavPath).toString());

    const bool got = waitFor([&] { return trackSpy.count() > 0; }, 2000);
    ASSERT_TRUE(got) << "trackChanged not emitted after Harmonoid local file metadata";
    EXPECT_EQ(trackSpy.last().at(2).toLongLong(), 3000000LL);
}

TEST_F(MprisClientTest, HarmonoidKeepsLocalDurationWhenLaterMetadataLacksUrl)
{
    SKIP_IF_NO_DBUS();

    const QString wavPath = m_tmpDir->path() + QStringLiteral("/real-duration.wav");
    ASSERT_TRUE(writeSilentWav(wavPath, 3000));

    FakePlayer fp(QStringLiteral("harmonoid"));
    fp.player->setStatus(QStringLiteral("Playing"));

    MprisClient client(m_config.get());
    QSignalSpy trackSpy(&client, &MprisClient::trackChanged);

    ASSERT_TRUE(waitFor([&] { return !client.activePlayer().service.isEmpty(); }));
    trackSpy.clear();

    fp.player->setMetadataWithUrl(QStringLiteral("Superstar"), QStringLiteral("Jamelia"),
                                  169456734LL, QStringLiteral("/t/stable"),
                                  QUrl::fromLocalFile(wavPath).toString());

    ASSERT_TRUE(waitFor([&] { return trackSpy.count() > 0; }, 2000));
    EXPECT_EQ(trackSpy.last().at(2).toLongLong(), 3000000LL);
    trackSpy.clear();

    fp.player->setMetadata(QStringLiteral("Superstar"), QStringLiteral("Jamelia"), 169456734LL,
                           QStringLiteral("/t/stable"));

    QEventLoop loop;
    QTimer::singleShot(300, &loop, &QEventLoop::quit);
    loop.exec();

    EXPECT_EQ(trackSpy.count(), 0)
        << "same Harmonoid track must not reset progress when later metadata lacks URL";
    EXPECT_EQ(client.activePlayer().lengthUs, 3000000LL);
}

TEST_F(MprisClientTest, HarmonoidIgnoresTransientEmptyMetadataForCurrentTrack)
{
    SKIP_IF_NO_DBUS();

    FakePlayer fp(QStringLiteral("harmonoid"));
    fp.player->setStatus(QStringLiteral("Playing"));

    MprisClient client(m_config.get());
    QSignalSpy trackSpy(&client, &MprisClient::trackChanged);

    fp.player->setMetadata(QStringLiteral("Superstar"), QStringLiteral("Jamelia"), 3000000LL,
                           QStringLiteral("/t/stable"));

    ASSERT_TRUE(waitFor([&] { return !client.activePlayer().service.isEmpty(); }));
    ASSERT_TRUE(waitFor([&] { return trackSpy.count() > 0; }, 2000));
    trackSpy.clear();

    fp.player->emitEmptyMetadataChanged();

    QEventLoop loop;
    QTimer::singleShot(300, &loop, &QEventLoop::quit);
    loop.exec();

    EXPECT_EQ(trackSpy.count(), 0)
        << "transient empty Harmonoid metadata must not reset the active progress row";
    EXPECT_EQ(client.activePlayer().title, QStringLiteral("Superstar"));
    EXPECT_EQ(client.activePlayer().lengthUs, 3000000LL);
}

// Metadata values delivered via PropertiesChanged may arrive wrapped in
// QDBusVariant. Verify that mpris:length is correctly extracted so the OSD
// progress bar shows the right total time (regression test for Harmonoid).
TEST_F(MprisClientTest, MetadataLengthExtractedCorrectlyViaPropertiesChanged)
{
    SKIP_IF_NO_DBUS();

    FakePlayer fp(QStringLiteral("harmonoid"));
    fp.player->setStatus(QStringLiteral("Playing"));

    MprisClient client(m_config.get());

    QSignalSpy trackSpy(&client, &MprisClient::trackChanged);

    // Send metadata via PropertiesChanged (the path that may wrap values in QDBusVariant).
    fp.player->setMetadata(QStringLiteral("Liberty City"), QStringLiteral("Seryoga"), 147401588LL,
                           QStringLiteral("/t/abc"));

    const bool got = waitFor([&] { return trackSpy.count() > 0; }, 2000);
    ASSERT_TRUE(got) << "trackChanged not emitted after setMetadata";

    // The length must arrive correctly — if QDBusVariant unwrapping is broken,
    // toLongLong() returns 0 and the OSD treats the track as a live stream.
    const qint64 emittedLength = trackSpy.first().at(2).toLongLong();
    EXPECT_EQ(emittedLength, 147401588LL)
        << "mpris:length was not correctly unwrapped from QDBusVariant";
}

#include "test_mprisclient.moc"

// Custom main — QCoreApplication must exist before any Qt D-Bus calls.
// GTest::gtest_main does not create one, so we provide our own.
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
