#pragma once
#include "audioapp.h"

#include <QObject>
#include <QMutex>
#include <QThread>
#include <QString>
#include <QList>
#include <QMap>
#include <optional>

#include <pulse/pulseaudio.h>
#include <pulse/ext-stream-restore.h>

// ─── PipeWire watcher thread ───────────────────────────────────────────────────
// Subscribes to PulseAudio sink-input events and applies any pending
// volume/mute when an app re-connects.
class PaWatcherThread : public QThread
{
    Q_OBJECT
public:
    explicit PaWatcherThread(QObject *parent = nullptr);
    ~PaWatcherThread() override;

    void stop();

    // Pending operations written by VolumeController main thread,
    // applied by this thread when a new sink input appears.
    QMutex                    pendingMutex;
    QMap<QString, double>     pendingVolumes;
    QMap<QString, bool>       pendingMutes;

signals:
    // Emitted when a new sink input event fires — main controller should
    // apply pending volumes immediately.
    void sinkInputAppeared();

protected:
    void run() override;

private:
    pa_threaded_mainloop *m_mainloop = nullptr;
    pa_context           *m_ctx      = nullptr;
    volatile bool         m_stopping = false;

    static void contextStateCallback(pa_context *ctx, void *userdata);
    static void subscribeCallback(pa_context *ctx, pa_subscription_event_type_t t,
                                  uint32_t idx, void *userdata);
};

// ─── VolumeController ─────────────────────────────────────────────────────────
class VolumeController : public QObject
{
    Q_OBJECT
public:
    explicit VolumeController(QObject *parent = nullptr);
    ~VolumeController() override;

    // Return all audio-capable apps (active streams + idle PipeWire clients).
    // Result is cached for 5 s to avoid repeated pw-dump calls.
    QList<AudioApp> listApps(bool forceRefresh = false);

    // Change volume by delta (±step/100). Returns new volume or nullopt.
    // Tries 4 backends in order (fast → slow → pending watcher).
    std::optional<double> changeVolume(const QString &appName, double delta);

    // Toggle mute. Returns (newMuted, currentVolume) or nullopt.
    std::optional<std::pair<bool, double>> toggleMute(const QString &appName);

    void close();

private:
    // PulseAudio main connection (operations run via threaded mainloop so
    // callers see blocking-style semantics from any thread).
    pa_threaded_mainloop *m_mainloop = nullptr;
    pa_context           *m_ctx      = nullptr;

    // Per-app last-known state
    QMap<QString, double>  m_appVolumes;
    QMap<QString, bool>    m_appMutes;

    // Cache for listApps()
    QList<AudioApp>        m_listCache;
    qint64                 m_listCacheTs = 0;
    static constexpr qint64 LIST_CACHE_TTL_MS = 5000;

    // Background watcher for pending volumes
    PaWatcherThread       *m_watcher = nullptr;

    // ── PA helpers ────────────────────────────────────────────────────────────
    bool connectContext();
    void waitForOperation(pa_operation *op);

    // Enumerate active sink inputs; caller must hold mainloop lock.
    struct SinkInputInfo {
        uint32_t index;
        QString  name;
        QString  binary;
        double   volume;
        bool     muted;
    };
    QList<SinkInputInfo> getSinkInputs();

    // Stream restore DB
    std::optional<double>              streamRestoreChangeVolume(const QString &app, double delta);
    std::optional<std::pair<bool,double>> streamRestoreToggleMute(const QString &app);

    // PipeWire node fallback (subprocess pw-dump + pw-cli)
    struct PwNode { int id; double volume; bool muted; };
    std::optional<PwNode> findPwNodeForApp(const QString &appName);
    bool setPwNodeVolume(int nodeId, double volume);
    bool setPwNodeMute(int nodeId, bool muted);

    // Idle PipeWire clients for listApps()
    QList<std::pair<QString,QString>> listPipeWireClients();

    // Apply pending volumes when a new sink input appears
    void applyPendingVolumes();

    // ── PA static callbacks ───────────────────────────────────────────────────
    struct SinkInputListCallbackData {
        VolumeController           *self;
        QList<SinkInputInfo>       *result;
    };
    static void sinkInputListCallback(pa_context *ctx, const pa_sink_input_info *info,
                                      int eol, void *ud);

    struct StreamRestoreReadData {
        VolumeController *self;
        QString           target;
        double            delta;
        bool              isMute;
        std::optional<double>                    *outVol;
        std::optional<std::pair<bool,double>>    *outMute;
    };
    static void streamRestoreReadCallback(pa_context *ctx,
                                          const pa_ext_stream_restore_info *info,
                                          int eol, void *ud);

    static void contextStateCallback(pa_context *ctx, void *userdata);
};
