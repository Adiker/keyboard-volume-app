#pragma once
#include "audioapp.h"

#include <QObject>
#include <QThread>
#include <QString>
#include <QList>

// VolumeController — public API, always called from the main thread.
// All blocking PA/libpipewire operations run inside a
// dedicated PA worker thread so the Qt event loop is never stalled.
class VolumeController : public QObject
{
    Q_OBJECT
  public:
    explicit VolumeController(QObject* parent = nullptr);
    ~VolumeController() override;

    // Returns the cached app list immediately and posts an async refresh.
    // Connect to appsReady() to know when fresh data has arrived.
    QList<AudioApp> listApps(bool forceRefresh = false);

    // Async volume operations — result arrives via volumeChanged().
    void changeVolume(const QString& appName, double delta);
    void setVolume(const QString& appName, double targetVolume);
    void toggleMute(const QString& appName);
    void setMuted(const QString& appName, bool muted);
    void toggleDucking(const QString& keepApp, double duckVolume);

    // Async read of the app's current volume — result arrives via volumeChanged().
    // Does NOT modify any volume. Falls back to cached value when PA is unavailable.
    void queryVolume(const QString& appName);

    void close();

  signals:
    // Emitted in the main thread after a volume/mute change completes.
    void volumeChanged(const QString& app, double newVol, bool muted);

    // Emitted in the main thread after an async app-list refresh finishes.
    void appsReady(QList<AudioApp> apps);

  private:
    QThread* m_paThread = nullptr;
    class PaWorker* m_worker = nullptr;
    QList<AudioApp> m_listCache;
    bool m_closing = false;
};
