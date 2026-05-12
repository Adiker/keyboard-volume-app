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
    const int dot = service.lastIndexOf(QLatin1Char('.'));
    return (dot >= 0) ? service.mid(dot + 1).toLower() : service.toLower();
}

static QVariantMap variantMapFromDBusArg(const QVariant& v)
{
    if (v.canConvert<QDBusArgument>())
    {
        QVariantMap m;
        v.value<QDBusArgument>() >> m;
        return m;
    }
    return v.toMap();
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
    reevaluateActive();
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
    emit positionChanged(positionUs);
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
    KnownPlayer& kp = m_players[service];

    if (props.contains(QStringLiteral("PlaybackStatus")))
        kp.status = props[QStringLiteral("PlaybackStatus")].toString();

    if (props.contains(QStringLiteral("CanSeek")))
        kp.canSeek = props[QStringLiteral("CanSeek")].toBool();

    if (props.contains(QStringLiteral("Metadata")))
    {
        const QVariantMap meta = variantMapFromDBusArg(props[QStringLiteral("Metadata")]);

        kp.title = meta.value(QStringLiteral("xesam:title")).toString();

        const QVariant artistVar = meta.value(QStringLiteral("xesam:artist"));
        if (artistVar.canConvert<QStringList>())
        {
            const QStringList al = artistVar.toStringList();
            kp.artist = al.isEmpty() ? QString{} : al.first();
        }
        else
        {
            kp.artist = artistVar.toString();
        }

        kp.lengthUs = meta.value(QStringLiteral("mpris:length")).toLongLong();

        const QVariant tidVar = meta.value(QStringLiteral("mpris:trackid"));
        if (tidVar.canConvert<QDBusObjectPath>())
            kp.trackId = tidVar.value<QDBusObjectPath>().path();
        else
            kp.trackId = tidVar.toString();
    }

    reevaluateActive();
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

void MprisClient::reevaluateActive()
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
            m_hasActive = false;
            m_active = {};
            if (m_pollTimer) m_pollTimer->stop();
            emit noPlayer();
        }
        return;
    }

    const bool serviceChanged = !m_hasActive || m_active.service != chosen->service;
    const bool trackChanged =
        serviceChanged || m_active.title != chosen->title || m_active.artist != chosen->artist ||
        m_active.lengthUs != chosen->lengthUs || m_active.canSeek != chosen->canSeek;
    const bool statusChanged = !m_hasActive || m_active.status != chosen->status;

    m_hasActive = true;
    m_active.service = chosen->service;
    m_active.displayName = chosen->displayName;
    m_active.status = chosen->status;
    m_active.canSeek = chosen->canSeek;
    m_active.lengthUs = chosen->lengthUs;
    m_active.trackId = chosen->trackId;
    m_active.title = chosen->title;
    m_active.artist = chosen->artist;

    if (serviceChanged) emit activePlayerChanged(m_active);

    if (trackChanged)
        emit this->trackChanged(m_active.title, m_active.artist, m_active.lengthUs,
                                m_active.canSeek && m_active.lengthUs > 0);

    if (statusChanged) emit playbackStatusChanged(m_active.status);

    // Manage poll timer
    if (m_pollTimer)
    {
        if (m_active.status == QLatin1String("Playing"))
        {
            if (!m_pollTimer->isActive()) m_pollTimer->start();
        }
        else
        {
            m_pollTimer->stop();
        }
    }
}

void MprisClient::pollPosition()
{
    if (!m_hasActive || m_seeking) return;
    if (m_active.status != QLatin1String("Playing")) return;

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
                if (pos >= 0) emit positionChanged(pos);
            });
}

#include "mprisclient.moc"
