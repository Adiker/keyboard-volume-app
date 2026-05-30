#pragma once
#include <QObject>
#include <QPixmap>
#include <QHash>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;

// ─── AlbumArtCache ────────────────────────────────────────────────────────────
// Resolves a `mpris:artUrl` value into a QPixmap, with three backends:
//   • file://       — loaded synchronously inside request()
//   • data:         — decoded inline (base64 PNG/JPEG)
//   • http(s)://    — downloaded asynchronously through QNetworkAccessManager,
//                     persisted to QStandardPaths::CacheLocation as
//                     keyboard-volume-app/art/<sha1>.bin
//
// Usage pattern:
//   cache->request(url);
//   QPixmap p = cache->pixmapFor(url);  // immediate cache hit, else empty
//   connect(cache, &AlbumArtCache::ready, this, &MyClass::onArt);
//
// Lives in the main thread. Network replies are serialized through the same
// QNetworkAccessManager; no threading concerns. Cache is bounded by
// kMaxMemoryEntries (LRU). Disk cache has no hard limit in v1.
class AlbumArtCache : public QObject
{
    Q_OBJECT
  public:
    explicit AlbumArtCache(QObject* parent = nullptr);
    ~AlbumArtCache() override;

    // Kick off a load. Safe to call repeatedly with the same URL. Empty URL is
    // a no-op. Always emits ready() once data is available (immediately for
    // file/data and cache hits; asynchronously for HTTP).
    void request(const QString& url);

    // Synchronous cache lookup. Returns an empty QPixmap on miss.
    QPixmap pixmapFor(const QString& url) const;

  signals:
    // Emitted when a pixmap for `url` becomes available (cache hit or fresh
    // load). Always carries the original URL so callers can ignore stale
    // results when the active track has moved on.
    void ready(const QString& url, const QPixmap& pixmap);

  private slots:
    void onReplyFinished(QNetworkReply* reply);

  private:
    void storeAndEmit(const QString& url, const QPixmap& pixmap);
    QString diskPathFor(const QString& url) const;
    QPixmap tryLoadFromDisk(const QString& url) const;
    void saveToDisk(const QString& url, const QByteArray& bytes) const;

    QHash<QString, QPixmap> m_memCache;
    QStringList m_lruOrder; // most-recent at back
    QHash<QString, QNetworkReply*> m_pending;
    QNetworkAccessManager* m_nam = nullptr;

    static constexpr int kMaxMemoryEntries = 64;
};
