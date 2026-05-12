#pragma once
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QString>
#include <QStringList>
#include <QMap>

class Config;
class MprisPlayerProxy; // one per tracked player, defined in mprisclient.cpp

// ─── MprisClient ──────────────────────────────────────────────────────────────
// Consumes MPRIS2 (org.mpris.MediaPlayer2.Player) from the session D-Bus.
//
// Lives exclusively in the main thread — uses QTimer + QtDBus (both require
// the Qt event loop). NEVER call from PaWorker or InputHandler threads.
//
// Player selection:
//   1. Filter active services by trackedPlayers substrings (case-insensitive).
//   2. Sort by index in trackedPlayers (user priority).
//   3. Pick first Playing → first Paused → emit noPlayer().
//
// Position polling:
//   QTimer fires every progressPollMs ms, but only when progressEnabled is true,
//   status == "Playing", and no seek drag is in progress. Spotify does not emit
//   PropertiesChanged for Position, so polling is mandatory when progress is shown.
//
// Seek:
//   setPosition(us) calls SetPosition(trackId, us) via D-Bus.
//   seekBy(deltaUs)  calls Seek(deltaUs).
//   Both are no-ops when canSeek == false or lengthUs <= 0.
//
// connectionName: optional named connection to session bus. Defaults to a
//   private connection ("mpris_client"). Tests may pass a different name so
//   that signals from a FakePlayer on sessionBus() are delivered correctly
//   (Qt does not loop-back signals on the same connection object).
class MprisClient : public QObject
{
    Q_OBJECT
  public:
    struct PlayerInfo
    {
        QString service;     // full bus name, e.g. "org.mpris.MediaPlayer2.spotify"
        QString displayName; // lowercase suffix, e.g. "spotify"
        QString status;      // "Playing" / "Paused" / "Stopped"
        bool canSeek = false;
        qint64 lengthUs = 0; // 0 = unknown / live stream
        QString trackId;     // ObjectPath — required for SetPosition
        QString title;
        QString artist;
    };

    explicit MprisClient(Config* config, QObject* parent = nullptr);
    ~MprisClient() override;

    PlayerInfo activePlayer() const
    {
        return m_active;
    }

    // Re-read trackedPlayers + progressPollMs from Config and re-evaluate
    // active player. Call after settings change.
    void reload();

    // Suspend / resume position polling during seek drag.
    void setSeeking(bool seeking);

  signals:
    void activePlayerChanged(const MprisClient::PlayerInfo& info);
    void trackChanged(const QString& title, const QString& artist, qint64 lengthUs, bool canSeek);
    void positionChanged(qint64 positionUs);
    void playbackStatusChanged(const QString& status);
    void noPlayer();

  public slots:
    // Seek to absolute position (µs). No-op when canSeek==false or length<=0.
    void setPosition(qint64 positionUs);
    // Seek relative (µs, may be negative). No-op when canSeek==false.
    void seekBy(qint64 deltaUs);

    // Called by MprisPlayerProxy — not for external use.
    void onPropertiesChanged(const QString& service, const QString& interface,
                             const QVariantMap& changedProps);
    void onSeeked(const QString& service, qint64 positionUs);

  private slots:
    // Connected to org.freedesktop.DBus NameOwnerChanged — fires for all
    // service name changes, filtered to MPRIS prefix inside the slot.
    void onNameOwnerChanged(const QString& name, const QString& oldOwner, const QString& newOwner);
    void onPollTimer();

  private:
    struct KnownPlayer
    {
        QString service;
        QString displayName;
        QString status; // "Playing" / "Paused" / "Stopped"
        bool canSeek = false;
        qint64 lengthUs = 0;
        QString trackId;
        QString title;
        QString artist;
    };

    void scanExistingServices();
    void addPlayer(const QString& service);
    void removePlayer(const QString& service);
    void fetchAllProperties(const QString& service);
    void applyProperties(const QString& service, const QVariantMap& props);
    void reevaluateActive(bool forceTrackChanged = false);
    int priorityOf(const QString& service) const; // lower = higher priority; -1 = not tracked
    void pollPosition();

    Config* m_config = nullptr;
    QDBusConnection m_bus;
    QTimer* m_pollTimer = nullptr;
    bool m_seeking = false;

    QMap<QString, KnownPlayer> m_players;       // keyed by full service name
    QMap<QString, MprisPlayerProxy*> m_proxies; // one proxy per player
    PlayerInfo m_active;
    bool m_hasActive = false;

    QStringList m_trackedPlayers; // cached from config
    int m_pollMs = 500;

    // Throttle for Position updates delivered via PropertiesChanged (e.g. Harmonoid
    // sends ~12/sec). Only emit positionChanged once per m_pollMs window; also used
    // to skip the redundant D-Bus poll when fresh data already arrived this cycle.
    QElapsedTimer m_positionThrottle;
};
