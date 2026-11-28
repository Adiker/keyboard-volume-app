#pragma once
#include "audioapp.h"
#include "config.h"
#include "pabackend_helpers.h"
#include "pwutils.h"
#include "volumecontroller.h"

#include <QList>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <atomic>
#include <cstdint>
#include <optional>
#include <utility>

#include <pulse/context.h>
#include <pulse/thread-mainloop.h>
#include <pulse/volume.h>

class PaWatcherThread;
class QTimer;

// Owns the PA connection used for volume operations.
// Lives in m_paThread — all PA blocking calls happen here,
// never on the main Qt thread.
class PaWorker : public QObject
{
    Q_OBJECT
  public:
    PaWorker() = default;
    ~PaWorker() override;

    // Direct call from any thread — Config* is only stored, never owned.
    // Config getters take their own mutex, so PA-thread reads are safe.
    void setConfig(Config* config);

    QList<AppAlias> currentAliases() const;
    QStringList matchCandidatesFor(const QString& appName) const;

  public slots:
    // Called once from the PA thread after moveToThread + start.
    void init();
    void cleanup();
    void requestStop();
    void attemptReconnect();
    void handleContextLost();

    void doChangeVolume(const QString& appName, double delta, double minVol = 0.0,
                        double maxVol = 1.0);
    void doToggleMute(const QString& appName);
    void setAppMute(const QString& appName, bool targetMuted);
    void setAppVolume(const QString& appName, double targetVolume, double minVol = 0.0,
                      double maxVol = 1.0);
    void doToggleDucking(const QString& keepApp, double duckVolume);
    void doQueryVolume(const QString& appName, bool silent);
    void doListApps(bool forceRefresh);
    void doApplyPending();
    void doListSinks(bool forceRefresh);
    void doClearAppSinkOverride(const QString& appName);
    void doFlushPendingSinkClears();
    void doSetAppSink(const QString& appName, const QString& sinkName);

  signals:
    void volumeChanged(const QString& app, double newVol, bool muted);
    void volumeQueriedSilently(const QString& app, double newVol, bool muted);
    void appsReady(QList<AudioApp> apps);
    void sinksReady(QList<SinkInfo> sinks);
    void sinkChanged(const QString& app, const QString& sinkName);
    void hiddenVolumeDetected(const QString& app, double rawVolume, double visibleVolume,
                              double effectiveVolume);
    void cleanupFinished();

  private:
    struct DuckingSnapshot
    {
        QString app;
        std::optional<uint32_t> pipeWireNodeId;
        uint32_t pulseSinkInputIndex = PA_INVALID_INDEX;
        QList<double> visibleChannelVolumes;
        pa_cvolume pulseChannelVolume{};
        double volume = 1.0;
    };

    bool connectContext();
    void disconnectContext();
    bool contextReady();
    void scheduleReconnect();
    static void contextStateCallback(pa_context* ctx, void* ud);
    void startWatcher();
    bool waitForOperation(pa_operation* op, const char* what, int timeoutMs = 1500);

    bool sinkInputMatches(const pabackend::SinkInputInfo& input, const QString& appName,
                          const QList<AppAlias>& aliases,
                          std::optional<PipeWireNode>* pipeWireState = nullptr);
    QList<pabackend::SinkInputInfo> getSinkInputs(); // caller must hold mainloop lock
    std::optional<PipeWireNode> pipeWireStateFor(const pabackend::SinkInputInfo& input);
    bool setPulseSinkInputVolume(const pabackend::SinkInputInfo& input, double targetVolume);
    bool setPulseSinkInputChannelVolumes(uint32_t sinkInputIndex, const pa_cvolume& channelVolume,
                                         const char* operationName);
    bool setActiveStreamVisibleVolume(const pabackend::SinkInputInfo& input, double targetVolume);
    bool restoreActiveStreamVolume(const pabackend::SinkInputInfo& input,
                                   const std::optional<PipeWireNode>& pipeWireState);
    pabackend::SinkInputInfo sinkInputAfterMove(const pabackend::SinkInputInfo& beforeMove);

    QStringList streamRestoreAppCandidates(const QString& app);
    void appendStreamRestoreAliasesFromDatabase(QStringList& candidates, const QString& app);
    std::optional<double> streamRestoreChangeVolume(const QString& app, double delta,
                                                    double minVol = 0.0, double maxVol = 1.0);
    std::optional<pabackend::StreamRestoreState> streamRestoreQueryState(const QString& app);
    std::optional<std::pair<bool, double>> streamRestoreQueryVolume(const QString& app);
    bool streamRestoreSetChannelVolumes(const QString& app, const pa_cvolume& channelVolume);
    std::optional<double> streamRestoreSetVolume(const QString& app, double targetVolume,
                                                 double minVol = 0.0, double maxVol = 1.0);
    std::optional<std::pair<bool, double>> streamRestoreToggleMute(const QString& app);
    std::optional<std::pair<bool, double>> streamRestoreSetMute(const QString& app,
                                                                bool targetMuted);
    void detectPulseServerBackend();
    bool streamRestoreSetDevice(const QString& app, const QString& sinkName);
    bool streamRestoreClearDevice(const QString& app);

    void reportHiddenVolume(const QString& app, const PipeWireNode& node);
    static QString hiddenNodeKey(const PipeWireNode& node);
    void parkPendingVolume(const QString& app, double volume);
    void parkPendingChannelVolumes(const QString& app, const QList<double>& volumes);
    void removePendingVolume(const QString& app);
    void removePendingMute(const QString& app);
    void clearDuckingState();

    pa_threaded_mainloop* m_mainloop = nullptr;
    pa_context* m_ctx = nullptr;
    PaWatcherThread* m_watcher = nullptr;
    std::atomic<Config*> m_config{nullptr};
    std::atomic<bool> m_stopping{false};
    bool m_cleanedUp = false;
    bool m_isPipeWirePulse = false;
    PipeWireVolumeBackend m_pipeWire;

    QTimer* m_refreshTimer = nullptr;
    QTimer* m_sinkRefreshTimer = nullptr;
    QTimer* m_reconnectTimer = nullptr;
    int m_reconnectBackoffMs = 0;
    QMutex m_pendingMutex;
    QMap<QString, double> m_pendingVolumes;
    QMap<QString, QList<double>> m_pendingChannelVolumes;
    QMap<QString, bool> m_pendingMutes;
    QMap<QString, QString> m_pendingSinks;
    QSet<QString> m_pendingSinkClears; // SR device=nullptr writes deferred while PA is down
    QMap<QString, double> m_appVolumes;
    QMap<QString, bool> m_appMutes;
    QMap<QString, QString> m_appSinks; // last-known sink name per app
    QSet<QString> m_reportedHiddenNodes;
    bool m_duckingActive = false;
    QString m_duckingKeepApp;
    QList<DuckingSnapshot> m_duckingSnapshot;
    QList<AudioApp> m_listCache;
    qint64 m_listCacheTs = 0;
    QList<SinkInfo> m_sinkCache;
    qint64 m_sinkCacheTs = 0;
    QMap<uint32_t, QString> m_sinkNamesByIdx;
    static constexpr qint64 LIST_CACHE_TTL_MS = 5000;
};
