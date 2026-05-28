#pragma once
#include "audioapp.h"
#include "config.h"

#include <QObject>
#include <QThread>
#include <QString>
#include <QList>

// Output device (PulseAudio sink) descriptor exposed via listSinks().
// Lives on the main thread; PaWorker rebuilds it on each enumeration pass.
struct SinkInfo
{
    QString name;        // Stable PA sink name (e.g. alsa_output.usb-headset)
    QString description; // User-facing label (e.g. "USB Headset")
    uint32_t index = 0;  // PA sink index (lifetime: PA context)
    bool isDefault = false;
};

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

    // Returns the cached sink list immediately and posts an async refresh.
    // Connect to sinksReady() to know when fresh data has arrived.
    QList<SinkInfo> listSinks(bool forceRefresh = false);

    // Async volume operations — result arrives via volumeChanged().
    // Marked virtual purely so unit tests can mock them; production code never
    // subclasses VolumeController.
    // The optional [minVol, maxVol] bounds clamp the resulting absolute volume
    // (used to enforce per-profile vol_min / vol_max). Both must lie in [0,1]
    // with minVol <= maxVol; defaults preserve the full range.
    virtual void changeVolume(const QString& appName, double delta, double minVol = 0.0,
                              double maxVol = 1.0);
    virtual void setVolume(const QString& appName, double targetVolume, double minVol = 0.0,
                           double maxVol = 1.0);
    virtual void toggleMute(const QString& appName);
    virtual void setMuted(const QString& appName, bool muted);
    void toggleDucking(const QString& keepApp, double duckVolume);

    // Async output routing — move the app's active sink-input(s) to the named
    // sink and persist the choice via stream-restore. Empty sinkName is a
    // no-op. Result (success or no-op) arrives via sinkChanged(); failures are
    // logged but do not raise.
    virtual void setAppSink(const QString& appName, const QString& sinkName);

    // Drop the persisted stream-restore device override for an app so the next
    // sink-input created by that app falls back to the system default sink.
    // Called from Settings when a profile's sink is changed back to "(system
    // default)" so the previous routing doesn't keep sticking after restart.
    virtual void clearAppSinkOverride(const QString& appName);

    // Apply an audio scene: iterate its targets and set per-target volume/mute.
    // Scenes intentionally bypass per-profile volume limits (explicit presets).
    // Single entry point shared by tray, D-Bus, hotkey, and Settings so the
    // per-target loop is not duplicated. Async → results arrive via volumeChanged().
    void applyScene(const AudioScene& scene);

    // Async read of the app's current volume — result arrives via volumeChanged().
    // Does NOT modify any volume. Falls back to cached value when PA is unavailable.
    void queryVolume(const QString& appName);

    void close();

  signals:
    // Emitted in the main thread after a volume/mute change completes.
    void volumeChanged(const QString& app, double newVol, bool muted);

    // Emitted in the main thread after an async app-list refresh finishes.
    void appsReady(QList<AudioApp> apps);

    // Emitted in the main thread after an async sink-list refresh finishes.
    void sinksReady(QList<SinkInfo> sinks);

    // Emitted in the main thread after setAppSink() routing completes
    // (sinkName empty when routing could not be applied yet — parked).
    void sinkChanged(const QString& app, const QString& sinkName);

  private:
    QThread* m_paThread = nullptr;
    class PaWorker* m_worker = nullptr;
    QList<AudioApp> m_listCache;
    QList<SinkInfo> m_sinkCache;
    bool m_closing = false;
};

Q_DECLARE_METATYPE(SinkInfo)
Q_DECLARE_METATYPE(QList<SinkInfo>)
