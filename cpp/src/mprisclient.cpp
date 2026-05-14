#include "mprisclient.h"
#include "config.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include <audioproperties.h>
#include <fileref.h>

static constexpr const char* MPRIS_PREFIX = "org.mpris.MediaPlayer2.";
static constexpr const char* MPRIS_PATH = "/org/mpris/MediaPlayer2";
static constexpr const char* MPRIS_PLAYER_IFACE = "org.mpris.MediaPlayer2.Player";
static constexpr const char* DBUS_PROPS_IFACE = "org.freedesktop.DBus.Properties";

// ─── MprisPlayerProxy ─────────────────────────────────────────────────────────
// One instance per tracked MPRIS service. Receives D-Bus signals and forwards
// them to MprisClient with the service name attached, so MprisClient always
// knows which player sent the signal.
class MprisPlayerProxy : public QObject
{
    Q_OBJECT
  public:
    MprisPlayerProxy(const QString& service, MprisClient* client, const QDBusConnection& bus,
                     QObject* parent = nullptr)
        : QObject(parent), m_service(service), m_client(client)
    {
        // Use a mutable copy — bus.connect() is non-const in Qt6.
        QDBusConnection b = bus;

        b.connect(m_service, QLatin1String(MPRIS_PATH), QLatin1String(DBUS_PROPS_IFACE),
                  QStringLiteral("PropertiesChanged"), this,
                  SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));

        b.connect(m_service, QLatin1String(MPRIS_PATH), QLatin1String(MPRIS_PLAYER_IFACE),
                  QStringLiteral("Seeked"), this, SLOT(onSeeked(qint64)));
    }

  private slots:
    void onPropertiesChanged(const QString& interface, const QVariantMap& changed,
                             const QStringList& /*invalidated*/)
    {
        m_client->onPropertiesChanged(m_service, interface, changed);
    }

    void onSeeked(qint64 positionUs)
    {
        m_client->onSeeked(m_service, positionUs);
    }

  private:
    QString m_service;
    MprisClient* m_client;
};

// ─── helpers ──────────────────────────────────────────────────────────────────

static QString serviceDisplayName(const QString& service)
{
    if (service.startsWith(QLatin1String(MPRIS_PREFIX)))
        return service.mid(QString::fromLatin1(MPRIS_PREFIX).size()).toLower();
    return service.toLower();
}

// Unwrap a QDBusVariant wrapper if present, returning the inner QVariant.
// QtDBus auto-demarshals a{sv} signal parameters leaving variant values wrapped
// in QDBusVariant. Calling e.g. .toLongLong() on such a wrapper returns 0.
// This mirrors the pattern already used in kvctl.cpp::variantToText().
static QVariant unwrapDBusVariant(const QVariant& v)
{
    if (v.userType() == qMetaTypeId<QDBusVariant>())
        return qvariant_cast<QDBusVariant>(v).variant();
    return v;
}

static QVariantMap variantMapFromDBusArg(const QVariant& v)
{
    // Unwrap a QDBusVariant wrapper before attempting QDBusArgument conversion.
    const QVariant inner = unwrapDBusVariant(v);
    if (inner.canConvert<QDBusArgument>())
    {
        QVariantMap m;
        inner.value<QDBusArgument>() >> m;
        return m;
    }
    return inner.toMap();
}

static qint64 localAudioDurationUs(const QString& urlText)
{
    const QUrl url(urlText);
    if (!url.isValid() || !url.isLocalFile()) return 0;

    const QString path = url.toLocalFile();
    if (!QFileInfo::exists(path)) return 0;

    const QByteArray encodedPath = QFile::encodeName(path);
    TagLib::FileRef file(encodedPath.constData(), true, TagLib::AudioProperties::Accurate);
    if (file.isNull() || !file.audioProperties()) return 0;

    const int durationMs = file.audioProperties()->lengthInMilliseconds();
    if (durationMs <= 0) return 0;
    return static_cast<qint64>(durationMs) * 1000LL;
}

static bool isHarmonoidService(const QString& service)
{
    return serviceDisplayName(service).contains(QStringLiteral("harmonoid"));
}

static bool progressDebugEnabled()
{
    return qEnvironmentVariableIsSet("KVA_DEBUG_PROGRESS");
}

static QStringList sortedKeys(const QVariantMap& props)
{
    QStringList keys = props.keys();
    keys.sort();
    return keys;
}

// ─── MprisClient ──────────────────────────────────────────────────────────────

MprisClient::MprisClient(Config* config, QObject* parent)
    : QObject(parent), m_config(config), m_bus(QDBusConnection::sessionBus())
{
    const OsdConfig osd = m_config->osd();
    m_trackedPlayers = osd.trackedPlayers;
    m_pollMs = std::clamp(osd.progressPollMs, 200, 2000);

    if (!m_bus.isConnected())
    {
        qWarning() << "[MprisClient] Session D-Bus not available — MPRIS disabled";
        return;
    }

    // Connect directly to NameOwnerChanged from the D-Bus daemon.
    // More reliable than QDBusServiceWatcher with a wildcard prefix.
    m_bus.connect(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                  QStringLiteral("org.freedesktop.DBus"), QStringLiteral("NameOwnerChanged"), this,
                  SLOT(onNameOwnerChanged(QString, QString, QString)));

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(m_pollMs);
    connect(m_pollTimer, &QTimer::timeout, this, &MprisClient::onPollTimer);

    scanExistingServices();
}

MprisClient::~MprisClient()
{
    if (m_pollTimer) m_pollTimer->stop();
}

// ─── public ───────────────────────────────────────────────────────────────────

void MprisClient::reload()
{
    const OsdConfig osd = m_config->osd();
    m_trackedPlayers = osd.trackedPlayers;
    const int newMs = std::clamp(osd.progressPollMs, 200, 2000);
    if (newMs != m_pollMs && m_pollTimer)
    {
        m_pollMs = newMs;
        m_pollTimer->setInterval(m_pollMs);
    }
    reevaluateActive(true);
}

void MprisClient::setSeeking(bool seeking)
{
    m_seeking = seeking;
}

void MprisClient::setPosition(qint64 positionUs)
{
    if (!m_hasActive || !m_active.canSeek || m_active.lengthUs <= 0) return;
    if (m_active.trackId.isEmpty()) return;

    QDBusMessage msg = QDBusMessage::createMethodCall(m_active.service, QLatin1String(MPRIS_PATH),
                                                      QLatin1String(MPRIS_PLAYER_IFACE),
                                                      QStringLiteral("SetPosition"));
    msg << QVariant::fromValue(QDBusObjectPath(m_active.trackId)) << positionUs;
    m_bus.asyncCall(msg);
}

void MprisClient::seekBy(qint64 deltaUs)
{
    if (!m_hasActive || !m_active.canSeek) return;

    QDBusMessage msg =
        QDBusMessage::createMethodCall(m_active.service, QLatin1String(MPRIS_PATH),
                                       QLatin1String(MPRIS_PLAYER_IFACE), QStringLiteral("Seek"));
    msg << deltaUs;
    m_bus.asyncCall(msg);
}

// ─── public slots (called by MprisPlayerProxy) ────────────────────────────────

void MprisClient::onPropertiesChanged(const QString& service, const QString& interface,
                                      const QVariantMap& changedProps)
{
    if (interface != QLatin1String(MPRIS_PLAYER_IFACE)) return;
    applyProperties(service, changedProps);
}

void MprisClient::onSeeked(const QString& service, qint64 positionUs)
{
    if (!m_hasActive || m_active.service != service) return;
    emitPositionUpdate(positionUs, QStringLiteral("seeked"));
}

// ─── private slots ────────────────────────────────────────────────────────────

void MprisClient::onNameOwnerChanged(const QString& name, const QString& oldOwner,
                                     const QString& newOwner)
{
    if (!name.startsWith(QLatin1String(MPRIS_PREFIX))) return;

    if (oldOwner.isEmpty() && !newOwner.isEmpty())
    {
        // Service appeared
        addPlayer(name);
    }
    else if (!oldOwner.isEmpty() && newOwner.isEmpty())
    {
        // Service disappeared
        removePlayer(name);
    }
}

void MprisClient::onPollTimer()
{
    pollPosition();
}

// ─── private helpers ──────────────────────────────────────────────────────────

void MprisClient::scanExistingServices()
{
    const QStringList services = m_bus.interface()->registeredServiceNames().value();
    for (const QString& svc : services)
    {
        if (svc.startsWith(QLatin1String(MPRIS_PREFIX))) addPlayer(svc);
    }
}

void MprisClient::addPlayer(const QString& service)
{
    if (m_players.contains(service)) return;

    KnownPlayer kp;
    kp.service = service;
    kp.displayName = serviceDisplayName(service);
    m_players.insert(service, kp);

    auto* proxy = new MprisPlayerProxy(service, this, m_bus, this);
    m_proxies.insert(service, proxy);

    fetchAllProperties(service);
    // reevaluateActive() is called inside applyProperties
}

void MprisClient::removePlayer(const QString& service)
{
    if (!m_players.remove(service)) return;
    if (auto* proxy = m_proxies.take(service)) proxy->deleteLater();
    qDebug() << "[MprisClient] player gone:" << service;
    reevaluateActive();
}

void MprisClient::fetchAllProperties(const QString& service)
{
    QDBusMessage msg =
        QDBusMessage::createMethodCall(service, QLatin1String(MPRIS_PATH),
                                       QLatin1String(DBUS_PROPS_IFACE), QStringLiteral("GetAll"));
    msg << QLatin1String(MPRIS_PLAYER_IFACE);

    QDBusPendingCall call = m_bus.asyncCall(msg);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    // Capture service name by value — the lambda outlives this stack frame.
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, service](QDBusPendingCallWatcher* w)
            {
                w->deleteLater();
                if (w->isError()) return;
                const QDBusMessage reply = w->reply();
                if (reply.arguments().isEmpty()) return;
                const QDBusArgument arg = reply.arguments().first().value<QDBusArgument>();
                QVariantMap props;
                arg >> props;
                if (!props.isEmpty()) applyProperties(service, props);
            });
}

void MprisClient::applyProperties(const QString& service, const QVariantMap& props)
{
    if (!m_players.contains(service)) return;

    const bool debugProgress = progressDebugEnabled() && isHarmonoidService(service);
    if (debugProgress)
    {
        qDebug() << "[ProgressDebug][MPRIS] PropertiesChanged service=" << service
                 << "keys=" << sortedKeys(props) << "active=" << m_active.service
                 << "hasActive=" << m_hasActive;
    }

    // Fast path: Harmonoid (and some other players) send Position via PropertiesChanged
    // at very high frequency (~12/sec). If Position is the only changed property, emit
    // positionChanged directly and skip the expensive reevaluateActive() call entirely —
    // nothing about player state has changed.
    // Throttle: emit at most once per m_pollMs ms to avoid hammering the OSD (which
    // repaints the label and progress bar on every positionChanged signal).
    if (props.size() == 1 && props.contains(QStringLiteral("Position")))
    {
        const qint64 pos = unwrapDBusVariant(props[QStringLiteral("Position")]).toLongLong();
        if (m_hasActive && m_active.service == service && pos >= 0 && !m_seeking)
        {
            if (!m_positionThrottle.isValid() || m_positionThrottle.hasExpired(m_pollMs))
            {
                m_positionThrottle.restart();
                if (debugProgress)
                    qDebug() << "[ProgressDebug][MPRIS] emit position-only posUs=" << pos;
                emitPositionUpdate(pos, QStringLiteral("position-only"));
            }
            else if (debugProgress)
            {
                qDebug() << "[ProgressDebug][MPRIS] suppress position-only posUs=" << pos
                         << "pollMs=" << m_pollMs;
            }
        }
        else if (debugProgress)
        {
            qDebug() << "[ProgressDebug][MPRIS] ignore position-only posUs=" << pos
                     << "seeking=" << m_seeking << "active=" << m_active.service;
        }
        return;
    }

    KnownPlayer& kp = m_players[service];
    const bool wasActivePlayer = m_hasActive && m_active.service == service;
    bool hasBundledPosition = false;
    qint64 bundledPosition = -1;

    if (props.contains(QStringLiteral("PlaybackStatus")))
        kp.status = unwrapDBusVariant(props[QStringLiteral("PlaybackStatus")]).toString();

    if (props.contains(QStringLiteral("CanSeek")))
        kp.canSeek = unwrapDBusVariant(props[QStringLiteral("CanSeek")]).toBool();

    if (props.contains(QStringLiteral("CanGoNext")))
        kp.canGoNext = unwrapDBusVariant(props[QStringLiteral("CanGoNext")]).toBool();
    if (props.contains(QStringLiteral("CanGoPrevious")))
        kp.canGoPrevious = unwrapDBusVariant(props[QStringLiteral("CanGoPrevious")]).toBool();
    if (props.contains(QStringLiteral("CanPause")))
        kp.canPause = unwrapDBusVariant(props[QStringLiteral("CanPause")]).toBool();
    if (props.contains(QStringLiteral("CanPlay")))
        kp.canPlay = unwrapDBusVariant(props[QStringLiteral("CanPlay")]).toBool();

    if (props.contains(QStringLiteral("Metadata")))
    {
        // variantMapFromDBusArg already handles QDBusVariant wrapping for the outer
        // Metadata container. The individual values inside the a{sv} metadata dict may
        // also arrive wrapped in QDBusVariant when delivered via PropertiesChanged
        // signals (as opposed to GetAll replies). Unwrap each value before reading.
        const QVariantMap meta = variantMapFromDBusArg(props[QStringLiteral("Metadata")]);

        QString title = unwrapDBusVariant(meta.value(QStringLiteral("xesam:title"))).toString();

        const QVariant artistVar = unwrapDBusVariant(meta.value(QStringLiteral("xesam:artist")));
        QString artist;
        if (artistVar.canConvert<QStringList>())
        {
            const QStringList al = artistVar.toStringList();
            artist = al.isEmpty() ? QString{} : al.first();
        }
        else
        {
            artist = artistVar.toString();
        }

        qint64 lengthUs =
            unwrapDBusVariant(meta.value(QStringLiteral("mpris:length"))).toLongLong();

        QString trackId;
        const QVariant tidVar = unwrapDBusVariant(meta.value(QStringLiteral("mpris:trackid")));
        if (tidVar.canConvert<QDBusObjectPath>())
            trackId = tidVar.value<QDBusObjectPath>().path();
        else
            trackId = tidVar.toString();

        const QVariant urlVar = unwrapDBusVariant(meta.value(QStringLiteral("xesam:url")));
        if (isHarmonoidService(service))
        {
            const bool sameTrackId = !trackId.isEmpty() && trackId == kp.trackId;
            const bool transientEmptyMetadata =
                title.isEmpty() && artist.isEmpty() && trackId.isEmpty() && kp.lengthUs > 0;
            const qint64 rawLengthUs = lengthUs;
            const QString rawTitle = title;
            const QString rawArtist = artist;
            const QString rawTrackId = trackId;
            if (transientEmptyMetadata)
            {
                title = kp.title;
                artist = kp.artist;
                trackId = kp.trackId;
                lengthUs = kp.lengthUs;
            }
            else if (sameTrackId)
            {
                if (title.isEmpty()) title = kp.title;
                if (artist.isEmpty()) artist = kp.artist;
                if (lengthUs <= 0) lengthUs = kp.lengthUs;
            }

            const QString lengthKey =
                trackId.isEmpty() ? QStringLiteral("%1\n%2").arg(title, artist) : trackId;
            const qint64 fileLengthUs = localAudioDurationUs(urlVar.toString());
            if (fileLengthUs > 0)
            {
                lengthUs = fileLengthUs;
                kp.harmonoidLocalLengthUs = fileLengthUs;
                kp.harmonoidLocalLengthKey = lengthKey;
            }
            else if (kp.harmonoidLocalLengthUs > 0 && kp.harmonoidLocalLengthKey == lengthKey)
            {
                lengthUs = kp.harmonoidLocalLengthUs;
            }

            if (debugProgress)
            {
                qDebug() << "[ProgressDebug][MPRIS] metadata rawTitle=" << rawTitle
                         << "rawArtist=" << rawArtist << "rawTrackId=" << rawTrackId
                         << "rawLen=" << rawLengthUs << "normTitle=" << title
                         << "normArtist=" << artist << "normTrackId=" << trackId
                         << "normLen=" << lengthUs << "prevTitle=" << kp.title
                         << "prevArtist=" << kp.artist << "prevTrackId=" << kp.trackId
                         << "prevLen=" << kp.lengthUs << "transientEmpty=" << transientEmptyMetadata
                         << "sameTrackId=" << sameTrackId << "fileLen=" << fileLengthUs;
            }
        }

        kp.title = title;
        kp.artist = artist;
        kp.lengthUs = lengthUs;
        kp.trackId = trackId;
    }

    // Also handle Position when it arrives alongside other properties (e.g. during
    // track-change signals that bundle Position with Metadata). Defer emission until
    // after reevaluateActive() so trackChanged reaches the OSD before the new
    // position; updateTrack() resets the progress bar.
    if (props.contains(QStringLiteral("Position")))
    {
        bundledPosition = unwrapDBusVariant(props[QStringLiteral("Position")]).toLongLong();
        hasBundledPosition = bundledPosition >= 0;
    }

    reevaluateActive();

    if (hasBundledPosition && wasActivePlayer && m_hasActive && m_active.service == service &&
        !m_seeking)
    {
        m_positionThrottle.restart();
        if (debugProgress)
            qDebug() << "[ProgressDebug][MPRIS] emit bundled position posUs=" << bundledPosition;
        emitPositionUpdate(bundledPosition, QStringLiteral("bundled"));
    }
}

int MprisClient::priorityOf(const QString& service) const
{
    const QString dn = serviceDisplayName(service);
    for (int i = 0; i < m_trackedPlayers.size(); ++i)
    {
        if (dn.contains(m_trackedPlayers[i], Qt::CaseInsensitive) ||
            m_trackedPlayers[i].contains(dn, Qt::CaseInsensitive))
            return i;
    }
    return -1; // not tracked
}

void MprisClient::reevaluateActive(bool forceTrackChanged)
{
    // Build candidate list: tracked players sorted by priority
    QList<const KnownPlayer*> candidates;
    for (const KnownPlayer& kp : m_players)
    {
        if (priorityOf(kp.service) >= 0) candidates.append(&kp);
    }
    std::sort(candidates.begin(), candidates.end(),
              [this](const KnownPlayer* a, const KnownPlayer* b)
              { return priorityOf(a->service) < priorityOf(b->service); });

    // Pick first Playing, then first Paused
    const KnownPlayer* chosen = nullptr;
    for (const KnownPlayer* kp : candidates)
    {
        if (kp->status == QLatin1String("Playing"))
        {
            chosen = kp;
            break;
        }
    }
    if (!chosen)
    {
        for (const KnownPlayer* kp : candidates)
        {
            if (kp->status == QLatin1String("Paused"))
            {
                chosen = kp;
                break;
            }
        }
    }

    if (!chosen)
    {
        if (m_hasActive)
        {
            if (progressDebugEnabled() && isHarmonoidService(m_active.service))
                qDebug() << "[ProgressDebug][MPRIS] emit noPlayer previous=" << m_active.service
                         << m_active.title << m_active.lengthUs;
            m_hasActive = false;
            m_active = {};
            if (m_pollTimer) m_pollTimer->stop();
            emit noPlayer();
        }
        return;
    }

    const bool serviceChanged = !m_hasActive || m_active.service != chosen->service;
    // canGoNext/canGoPrevious are intentionally excluded: toggling these at playlist
    // boundaries must not trigger trackChanged, which would reset the OSD progress bar.
    const bool trackChanged =
        serviceChanged || m_active.title != chosen->title || m_active.artist != chosen->artist ||
        m_active.lengthUs != chosen->lengthUs || m_active.canSeek != chosen->canSeek ||
        m_active.trackId != chosen->trackId;
    const bool statusChanged = !m_hasActive || m_active.status != chosen->status;
    const PlayerInfo previous = m_active;

    m_hasActive = true;
    m_active.service = chosen->service;
    m_active.displayName = chosen->displayName;
    m_active.status = chosen->status;
    m_active.canSeek = chosen->canSeek;
    m_active.canGoNext = chosen->canGoNext;
    m_active.canGoPrevious = chosen->canGoPrevious;
    m_active.canPause = chosen->canPause;
    m_active.canPlay = chosen->canPlay;
    m_active.lengthUs = chosen->lengthUs;
    m_active.trackId = chosen->trackId;
    m_active.title = chosen->title;
    m_active.artist = chosen->artist;

    if (serviceChanged) emit activePlayerChanged(m_active);

    if (trackChanged || forceTrackChanged)
    {
        const bool positionIdentityChanged = serviceChanged || previous.title != chosen->title ||
                                             previous.artist != chosen->artist ||
                                             previous.trackId != chosen->trackId;
        if (positionIdentityChanged) m_lastPositionUs = -1;

        if (progressDebugEnabled() && isHarmonoidService(m_active.service))
        {
            qDebug() << "[ProgressDebug][MPRIS] emit trackChanged serviceChanged=" << serviceChanged
                     << "force=" << forceTrackChanged << "oldTitle=" << previous.title
                     << "newTitle=" << chosen->title << "oldArtist=" << previous.artist
                     << "newArtist=" << chosen->artist << "oldTrackId=" << previous.trackId
                     << "newTrackId=" << chosen->trackId << "oldLen=" << previous.lengthUs
                     << "newLen=" << chosen->lengthUs << "oldCanSeek=" << previous.canSeek
                     << "newCanSeek=" << chosen->canSeek
                     << "positionIdentityChanged=" << positionIdentityChanged;
        }
        emit this->trackChanged(m_active.title, m_active.artist, m_active.lengthUs,
                                m_active.canSeek && m_active.lengthUs > 0);
    }

    if (statusChanged) emit playbackStatusChanged(m_active.status);

    // Manage poll timer
    if (m_pollTimer)
    {
        if (m_config->osd().progressEnabled && m_active.status == QLatin1String("Playing"))
        {
            if (!m_pollTimer->isActive()) m_pollTimer->start();
        }
        else
        {
            m_pollTimer->stop();
        }
    }
}

void MprisClient::emitPositionUpdate(qint64 positionUs, const QString& source)
{
    m_lastPositionUs = positionUs;
    if (progressDebugEnabled() && m_hasActive && isHarmonoidService(m_active.service))
        qDebug() << "[ProgressDebug][MPRIS] positionChanged source=" << source
                 << "posUs=" << positionUs;
    emit positionChanged(positionUs);
}

void MprisClient::playPause()
{
    if (!m_hasActive) return;
    QDBusMessage msg = QDBusMessage::createMethodCall(m_active.service, QLatin1String(MPRIS_PATH),
                                                      QLatin1String(MPRIS_PLAYER_IFACE),
                                                      QStringLiteral("PlayPause"));
    m_bus.asyncCall(msg);
}

void MprisClient::next()
{
    if (!m_hasActive || !m_active.canGoNext) return;
    QDBusMessage msg =
        QDBusMessage::createMethodCall(m_active.service, QLatin1String(MPRIS_PATH),
                                       QLatin1String(MPRIS_PLAYER_IFACE), QStringLiteral("Next"));
    m_bus.asyncCall(msg);
}

void MprisClient::previous()
{
    if (!m_hasActive) return;
    if (!m_active.canGoPrevious)
    {
        // Fallback: restart the current track
        setPosition(0);
        return;
    }
    QDBusMessage msg = QDBusMessage::createMethodCall(m_active.service, QLatin1String(MPRIS_PATH),
                                                      QLatin1String(MPRIS_PLAYER_IFACE),
                                                      QStringLiteral("Previous"));
    m_bus.asyncCall(msg);
}

void MprisClient::pollPosition()
{
    if (!m_hasActive || m_seeking) return;
    if (!m_config->osd().progressEnabled) return;
    if (m_active.status != QLatin1String("Playing")) return;

    // Skip the D-Bus round-trip if the active player already pushed a fresh Position
    // update via PropertiesChanged within this poll window (e.g. Harmonoid). The
    // throttle timer was restarted when we last emitted from the PropertiesChanged path,
    // so if it hasn't expired yet we have nothing to gain from polling.
    if (m_positionThrottle.isValid() && !m_positionThrottle.hasExpired(m_pollMs)) return;

    const QString service = m_active.service;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        service, QLatin1String(MPRIS_PATH), QLatin1String(DBUS_PROPS_IFACE), QStringLiteral("Get"));
    msg << QLatin1String(MPRIS_PLAYER_IFACE) << QStringLiteral("Position");

    QDBusPendingCall call = m_bus.asyncCall(msg);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, service](QDBusPendingCallWatcher* w)
            {
                w->deleteLater();
                if (w->isError() || !m_hasActive || m_active.service != service) return;
                const QDBusMessage reply = w->reply();
                if (reply.arguments().isEmpty()) return;
                const qint64 pos =
                    reply.arguments().first().value<QDBusVariant>().variant().toLongLong();
                if (pos < 0) return;

                const bool staleHarmonoidPoll = isHarmonoidService(service) &&
                                                m_lastPositionUs > 2000000LL &&
                                                pos + 2000000LL < m_lastPositionUs;
                if (staleHarmonoidPoll)
                {
                    if (progressDebugEnabled())
                        qDebug() << "[ProgressDebug][MPRIS] suppress stale poll posUs=" << pos
                                 << "lastPosUs=" << m_lastPositionUs << "service=" << service;
                    return;
                }

                emitPositionUpdate(pos, QStringLiteral("poll"));
            });
}

#include "mprisclient.moc"
