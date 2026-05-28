#include "albumartcache.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>
#include <QDebug>

namespace
{

QPixmap decodeDataUri(const QString& url)
{
    // data:[<mime>][;base64],<payload>
    const int commaIdx = url.indexOf(QLatin1Char(','));
    if (commaIdx < 0) return {};
    const QStringView header = QStringView(url).left(commaIdx);
    const QString payload = url.mid(commaIdx + 1);
    QByteArray bytes;
    if (header.contains(QLatin1String(";base64")))
        bytes = QByteArray::fromBase64(payload.toLatin1());
    else
        bytes = QByteArray::fromPercentEncoding(payload.toLatin1());
    QPixmap pm;
    pm.loadFromData(bytes);
    return pm;
}

QPixmap loadFromFileUri(const QString& url)
{
    const QUrl u(url);
    const QString path = u.isLocalFile() ? u.toLocalFile() : QString{};
    if (path.isEmpty() || !QFile::exists(path)) return {};
    QPixmap pm;
    pm.load(path);
    return pm;
}

} // namespace

AlbumArtCache::AlbumArtCache(QObject* parent) : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
    connect(m_nam, &QNetworkAccessManager::finished, this, &AlbumArtCache::onReplyFinished);
}

AlbumArtCache::~AlbumArtCache()
{
    // QNetworkReply pointers are parented to m_nam — Qt cleans up automatically.
}

QPixmap AlbumArtCache::pixmapFor(const QString& url) const
{
    return m_memCache.value(url);
}

void AlbumArtCache::request(const QString& url)
{
    if (url.isEmpty()) return;
    if (m_memCache.contains(url))
    {
        // Touch LRU and emit synchronously so callers can rely on a single
        // signal path regardless of cache state.
        m_lruOrder.removeAll(url);
        m_lruOrder.append(url);
        emit ready(url, m_memCache.value(url));
        return;
    }

    if (url.startsWith(QLatin1String("file://")))
    {
        QPixmap pm = loadFromFileUri(url);
        storeAndEmit(url, pm); // pm may be null — still emit so callers can fall back
        return;
    }
    if (url.startsWith(QLatin1String("data:")))
    {
        QPixmap pm = decodeDataUri(url);
        storeAndEmit(url, pm);
        return;
    }
    if (url.startsWith(QLatin1String("http://")) || url.startsWith(QLatin1String("https://")))
    {
        QPixmap diskHit = tryLoadFromDisk(url);
        if (!diskHit.isNull())
        {
            storeAndEmit(url, diskHit);
            return;
        }
        if (m_pending.contains(url)) return; // already in flight

        QNetworkRequest req{QUrl(url)};
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply* reply = m_nam->get(req);
        reply->setProperty("artUrl", url);
        m_pending.insert(url, reply);
        return;
    }

    // Unknown scheme — emit empty so callers can show fallback.
    storeAndEmit(url, QPixmap{});
}

void AlbumArtCache::onReplyFinished(QNetworkReply* reply)
{
    const QString url = reply->property("artUrl").toString();
    m_pending.remove(url);
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError)
    {
        qWarning() << "[AlbumArtCache] HTTP error" << reply->errorString() << "url=" << url;
        storeAndEmit(url, QPixmap{});
        return;
    }

    const QByteArray bytes = reply->readAll();
    QPixmap pm;
    pm.loadFromData(bytes);
    if (!pm.isNull()) saveToDisk(url, bytes);
    storeAndEmit(url, pm);
}

void AlbumArtCache::storeAndEmit(const QString& url, QPixmap pixmap)
{
    if (m_memCache.size() >= kMaxMemoryEntries && !m_lruOrder.isEmpty())
    {
        const QString victim = m_lruOrder.takeFirst();
        m_memCache.remove(victim);
    }
    m_memCache.insert(url, pixmap);
    m_lruOrder.removeAll(url);
    m_lruOrder.append(url);
    emit ready(url, pixmap);
}

QString AlbumArtCache::diskPathFor(const QString& url) const
{
    const QByteArray hash =
        QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex();
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                         QStringLiteral("/keyboard-volume-app/art");
    return base + QLatin1Char('/') + QString::fromLatin1(hash) + QStringLiteral(".bin");
}

QPixmap AlbumArtCache::tryLoadFromDisk(const QString& url) const
{
    const QString path = diskPathFor(url);
    if (!QFile::exists(path)) return {};
    QPixmap pm;
    pm.load(path);
    return pm;
}

void AlbumArtCache::saveToDisk(const QString& url, const QByteArray& bytes) const
{
    const QString path = diskPathFor(url);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(bytes);
}
