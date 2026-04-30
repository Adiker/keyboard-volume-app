#include "volumecontroller.h"
#include "pwutils.h"

#include <QDateTime>
#include <QProcess>
#include <QDebug>
#include <QTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

#include <pulse/thread-mainloop.h>
#include <pulse/context.h>
#include <pulse/introspect.h>
#include <pulse/subscribe.h>
#include <pulse/volume.h>
#include <pulse/ext-stream-restore.h>

// ─── PaWatcherThread ──────────────────────────────────────────────────────────
// Maintains its own PA connection and subscribes to sink-input events.
// When a new sink input appears, emits sinkInputAppeared().
// Lives in its own QThread; signal is emitted from the PA internal thread
// (queued connections to any receiver are safe).
class PaWatcherThread : public QThread
{
    Q_OBJECT
public:
    explicit PaWatcherThread(QObject *parent = nullptr) : QThread(parent) {}
    ~PaWatcherThread() override { stop(); }

    void stop()
    {
        m_stopping = true;
        if (m_mainloop) pa_threaded_mainloop_signal(m_mainloop, 0);
        if (isRunning()) { wait(1000); if (isRunning()) terminate(); }
    }

    QMutex                pendingMutex;
    QMap<QString, double> pendingVolumes;
    QMap<QString, bool>   pendingMutes;

signals:
    void sinkInputAppeared();
    void sinkInputRemoved();

protected:
    void run() override
    {
        m_mainloop = pa_threaded_mainloop_new();
        if (!m_mainloop) return;

        pa_mainloop_api *api = pa_threaded_mainloop_get_api(m_mainloop);
        m_ctx = pa_context_new(api, "keyboard-volume-app-watcher");
        if (!m_ctx) { pa_threaded_mainloop_free(m_mainloop); m_mainloop = nullptr; return; }

        pa_context_set_state_callback(m_ctx, contextStateCallback, this);
        pa_threaded_mainloop_lock(m_mainloop);
        pa_threaded_mainloop_start(m_mainloop);

        if (pa_context_connect(m_ctx, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) {
            pa_threaded_mainloop_unlock(m_mainloop);
            pa_context_unref(m_ctx); m_ctx = nullptr;
            pa_threaded_mainloop_stop(m_mainloop);
            pa_threaded_mainloop_free(m_mainloop); m_mainloop = nullptr;
            return;
        }

        while (!m_stopping) {
            pa_context_state_t st = pa_context_get_state(m_ctx);
            if (st == PA_CONTEXT_READY) break;
            if (!PA_CONTEXT_IS_GOOD(st)) break;
            pa_threaded_mainloop_wait(m_mainloop);
        }
        pa_threaded_mainloop_unlock(m_mainloop);

        // Keep this Qt thread alive; the PA internal thread drives events.
        while (!m_stopping) msleep(100);

        pa_threaded_mainloop_lock(m_mainloop);
        pa_context_disconnect(m_ctx);
        pa_context_unref(m_ctx); m_ctx = nullptr;
        pa_threaded_mainloop_unlock(m_mainloop);
        pa_threaded_mainloop_stop(m_mainloop);
        pa_threaded_mainloop_free(m_mainloop); m_mainloop = nullptr;
    }

private:
    pa_threaded_mainloop *m_mainloop = nullptr;
    pa_context           *m_ctx      = nullptr;
    std::atomic<bool>     m_stopping{false};

    static void contextStateCallback(pa_context *ctx, void *ud)
    {
        PaWatcherThread *self = static_cast<PaWatcherThread *>(ud);
        if (pa_context_get_state(ctx) == PA_CONTEXT_READY) {
            pa_context_subscribe(ctx, PA_SUBSCRIPTION_MASK_SINK_INPUT, nullptr, nullptr);
            pa_context_set_subscribe_callback(ctx, subscribeCallback, self);
        }
        pa_threaded_mainloop_signal(self->m_mainloop, 0);
    }

    static void subscribeCallback(pa_context *, pa_subscription_event_type_t t,
                                  uint32_t, void *ud)
    {
        PaWatcherThread *self = static_cast<PaWatcherThread *>(ud);
        int facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
        int type     = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;
        if (facility == PA_SUBSCRIPTION_EVENT_SINK_INPUT) {
            if (type == PA_SUBSCRIPTION_EVENT_NEW)
                emit self->sinkInputAppeared();
            else if (type == PA_SUBSCRIPTION_EVENT_REMOVE)
                emit self->sinkInputRemoved();
        }
    }
};

// ─── PaWorker ────────────────────────────────────────────────────────────────
// Owns the PA connection used for volume operations.
// Lives in m_paThread — all PA blocking calls happen here,
// never on the main Qt thread.
class PaWorker : public QObject
{
    Q_OBJECT
public:
    PaWorker() = default;
    ~PaWorker() override { cleanup(); }

public slots:
    // Called once from the PA thread after moveToThread + start.
    void init()
    {
        if (m_stopping) return;
        if (!connectContext()) {
            qWarning() << "[PaWorker] Cannot connect to PulseAudio";
        }

        m_watcher = new PaWatcherThread(); // no parent — lives in PA thread
        m_watcher->moveToThread(QThread::currentThread());

        // Use a one-shot timer instead of msleep so the PA thread event loop
        // stays free to process other queued calls during the 100 ms wait.
        connect(m_watcher, &PaWatcherThread::sinkInputAppeared, this, [this]() {
            if (m_stopping) return;
            QTimer::singleShot(100, this, &PaWorker::doApplyPending);
        });

        // Debounced app-list refresh on sink input changes (500ms).
        m_refreshTimer = new QTimer(this);
        m_refreshTimer->setSingleShot(true);
        m_refreshTimer->setInterval(500);
        connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
            doListApps(/*forceRefresh=*/true);
        });

        connect(m_watcher, &PaWatcherThread::sinkInputAppeared, this, [this]() {
            if (!m_stopping && m_refreshTimer) m_refreshTimer->start();
        });
        connect(m_watcher, &PaWatcherThread::sinkInputRemoved, this, [this]() {
            if (!m_stopping && m_refreshTimer) m_refreshTimer->start();
        });

        m_watcher->start();

        // Initial app list
        doListApps(/*forceRefresh=*/true);
    }

    void cleanup()
    {
        if (m_cleanedUp) { emit cleanupFinished(); return; }
        m_stopping = true;
        if (m_mainloop) pa_threaded_mainloop_signal(m_mainloop, 0);

        if (m_refreshTimer) {
            m_refreshTimer->stop();
        }

        if (m_watcher) {
            disconnect(m_watcher, nullptr, this, nullptr);
            m_watcher->stop();
            delete m_watcher;
            m_watcher = nullptr;
        }
        if (m_mainloop && m_ctx) {
            pa_threaded_mainloop_lock(m_mainloop);
            pa_context_disconnect(m_ctx);
            pa_context_unref(m_ctx); m_ctx = nullptr;
            pa_threaded_mainloop_unlock(m_mainloop);
            pa_threaded_mainloop_stop(m_mainloop);
            pa_threaded_mainloop_free(m_mainloop); m_mainloop = nullptr;
        }
        m_cleanedUp = true;
        emit cleanupFinished();
    }

    void doChangeVolume(const QString &appName, double delta)
    {
        if (m_stopping) return;

        // 1. Active sink input (fast path)
        if (m_mainloop && m_ctx) {
            pa_threaded_mainloop_lock(m_mainloop);
            const auto inputs = getSinkInputs();
            for (const auto &si : inputs) {
                if (si.name != appName && si.binary != appName) continue;

                double newVol = std::clamp(si.volume + delta, 0.0, 1.0);
                pa_cvolume cv;
                pa_cvolume_set(&cv, 2, static_cast<pa_volume_t>(newVol * PA_VOLUME_NORM));
                pa_operation *op = pa_context_set_sink_input_volume(
                    m_ctx, si.index, &cv, operationDoneCallback, this);
                if (!waitForOperation(op, "set sink input volume")) {
                    pa_threaded_mainloop_unlock(m_mainloop);
                    return;
                }
                pa_threaded_mainloop_unlock(m_mainloop);

                m_appVolumes[appName] = newVol;
                m_appMutes[appName]   = si.muted;
                removePending(appName);
                emit volumeChanged(appName, newVol, si.muted);
                return;
            }
            pa_threaded_mainloop_unlock(m_mainloop);
        }

        // 2. Stream restore DB
        auto vol = streamRestoreChangeVolume(appName, delta);
        if (vol) {
            m_appVolumes[appName] = *vol;
            removePending(appName);
            emit volumeChanged(appName, *vol, m_appMutes.value(appName, false));
            return;
        }

        // 3. PipeWire node (subprocess)
        auto node = findPwNodeForApp(appName);
        if (node) {
            double newVol = std::clamp(node->volume + delta, 0.0, 1.0);
            if (setPwNodeVolume(node->id, newVol)) {
                m_appVolumes[appName] = newVol;
                removePending(appName);
                emit volumeChanged(appName, newVol, m_appMutes.value(appName, false));
                return;
            }
        }

        // 4. App disconnected — park and show desired volume on OSD anyway
        double base   = m_appVolumes.value(appName, 1.0);
        double newVol = std::clamp(base + delta, 0.0, 1.0);
        m_appVolumes[appName] = newVol;
        if (m_watcher) {
            QMutexLocker lk(&m_watcher->pendingMutex);
            m_watcher->pendingVolumes[appName] = newVol;
        }
        emit volumeChanged(appName, newVol, m_appMutes.value(appName, false));
    }

    void doToggleMute(const QString &appName)
    {
        if (m_stopping) return;

        // 1. Active sink input
        if (m_mainloop && m_ctx) {
            pa_threaded_mainloop_lock(m_mainloop);
            const auto inputs = getSinkInputs();
            for (const auto &si : inputs) {
                if (si.name != appName && si.binary != appName) continue;

                int newMute = si.muted ? 0 : 1;
                pa_operation *op = pa_context_set_sink_input_mute(
                    m_ctx, si.index, newMute, operationDoneCallback, this);
                if (!waitForOperation(op, "set sink input mute")) {
                    pa_threaded_mainloop_unlock(m_mainloop);
                    return;
                }
                pa_threaded_mainloop_unlock(m_mainloop);

                m_appVolumes[appName] = si.volume;
                m_appMutes[appName]   = static_cast<bool>(newMute);
                removePending(appName);
                emit volumeChanged(appName, si.volume, static_cast<bool>(newMute));
                return;
            }
            pa_threaded_mainloop_unlock(m_mainloop);
        }

        // 2. Stream restore
        auto result = streamRestoreToggleMute(appName);
        if (result) {
            m_appMutes[appName] = result->first;
            if (m_watcher) {
                QMutexLocker lk(&m_watcher->pendingMutex);
                m_watcher->pendingMutes.remove(appName);
            }
            emit volumeChanged(appName, result->second, result->first);
            return;
        }

        // 3. PipeWire node
        auto node = findPwNodeForApp(appName);
        if (node) {
            bool newMuted = !node->muted;
            if (setPwNodeMute(node->id, newMuted)) {
                m_appMutes[appName] = newMuted;
                if (m_watcher) {
                    QMutexLocker lk(&m_watcher->pendingMutex);
                    m_watcher->pendingMutes.remove(appName);
                }
                emit volumeChanged(appName, node->volume, newMuted);
                return;
            }
        }

        // 4. Disconnected
        bool curMuted = m_appMutes.value(appName, false);
        double vol    = m_appVolumes.value(appName, 1.0);
        bool newMuted = !curMuted;
        m_appMutes[appName] = newMuted;
        if (m_watcher) {
            QMutexLocker lk(&m_watcher->pendingMutex);
            m_watcher->pendingMutes[appName] = newMuted;
        }
        emit volumeChanged(appName, vol, newMuted);
    }

    void doListApps(bool forceRefresh)
    {
        if (m_stopping) return;

        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (!forceRefresh && (now - m_listCacheTs) < LIST_CACHE_TTL_MS)
            return; // cache still fresh — caller already has it

        QMap<QString, AudioApp> apps;
        QSet<QString> activeBinaries;

        // 1. Active sink inputs
        if (m_mainloop && m_ctx) {
            pa_threaded_mainloop_lock(m_mainloop);
            const auto inputs = getSinkInputs();
            pa_threaded_mainloop_unlock(m_mainloop);

            for (const auto &si : inputs) {
                AudioApp app;
                app.sinkInputIndex = si.index;
                app.name   = si.name;
                app.binary = si.binary;
                app.volume = si.volume;
                app.muted  = si.muted;
                app.active = true;
                apps[si.name] = app;
                if (!si.binary.isEmpty()) activeBinaries.insert(si.binary);
            }
        }

        // 2. Idle PW clients (subprocess — runs here in PA thread, not main thread)
        for (const PipeWireClient &client : ::listPipeWireClients()) {
            if (SKIP_APP_NAMES.contains(client.name)) continue;
            if (activeBinaries.contains(client.binary)) continue;
            if (apps.contains(client.name)) continue;

            AudioApp app;
            app.name   = client.name;
            app.binary = client.binary;
            app.volume = 1.0;
            app.muted  = false;
            app.active = false;
            apps[client.name] = app;
        }

        QList<AudioApp> result = apps.values();
        std::sort(result.begin(), result.end(), [](const AudioApp &a, const AudioApp &b) {
            if (a.active != b.active) return a.active > b.active;
            return a.name.toLower() < b.name.toLower();
        });

        m_listCache   = result;
        m_listCacheTs = now;
        emit appsReady(result);
    }

    // Called via QTimer::singleShot(100ms) — runs in PA thread.
    void doApplyPending()
    {
        if (m_stopping || !m_mainloop || !m_ctx || !m_watcher) return;

        QMap<QString, double> pendVols;
        QMap<QString, bool>   pendMutes;
        {
            QMutexLocker lk(&m_watcher->pendingMutex);
            if (m_watcher->pendingVolumes.isEmpty() && m_watcher->pendingMutes.isEmpty())
                return;
            pendVols  = m_watcher->pendingVolumes;
            pendMutes = m_watcher->pendingMutes;
        }

        pa_threaded_mainloop_lock(m_mainloop);
        const auto inputs = getSinkInputs();
        QSet<QString> applied;

        for (const auto &si : inputs) {
            for (auto it = pendVols.begin(); it != pendVols.end(); ++it) {
                const QString &app = it.key();
                if (si.name != app && si.binary != app) continue;

                pa_cvolume cv;
                pa_cvolume_set(&cv, 2, static_cast<pa_volume_t>(it.value() * PA_VOLUME_NORM));
                bool volumeApplied = waitForOperation(pa_context_set_sink_input_volume(
                    m_ctx, si.index, &cv, operationDoneCallback, this),
                    "apply pending sink input volume");

                bool muteApplied = true;
                if (pendMutes.contains(app)) {
                    muteApplied = waitForOperation(pa_context_set_sink_input_mute(
                        m_ctx, si.index, pendMutes[app] ? 1 : 0,
                        operationDoneCallback, this),
                        "apply pending sink input mute");
                }

                if (volumeApplied && muteApplied)
                    applied.insert(app);
            }
        }
        pa_threaded_mainloop_unlock(m_mainloop);

        if (!applied.isEmpty()) {
            QMutexLocker lk(&m_watcher->pendingMutex);
            for (const auto &app : applied) {
                m_watcher->pendingVolumes.remove(app);
                m_watcher->pendingMutes.remove(app);
            }
        }
    }

signals:
    void volumeChanged(const QString &app, double newVol, bool muted);
    void appsReady(QList<AudioApp> apps);
    void cleanupFinished();

private:
    pa_threaded_mainloop *m_mainloop     = nullptr;
    pa_context           *m_ctx          = nullptr;
    PaWatcherThread      *m_watcher      = nullptr;
    bool                  m_stopping     = false;
    bool                  m_cleanedUp    = false;

    QTimer               *m_refreshTimer = nullptr;
    QMap<QString, double>  m_appVolumes;
    QMap<QString, bool>    m_appMutes;
    QList<AudioApp>        m_listCache;
    qint64                 m_listCacheTs = 0;
    static constexpr qint64 LIST_CACHE_TTL_MS = 5000;

    // ── PA context ────────────────────────────────────────────────────────────
    bool connectContext()
    {
        m_mainloop = pa_threaded_mainloop_new();
        if (!m_mainloop) return false;

        pa_mainloop_api *api = pa_threaded_mainloop_get_api(m_mainloop);
        m_ctx = pa_context_new(api, "keyboard-volume-app");
        if (!m_ctx) {
            pa_threaded_mainloop_free(m_mainloop); m_mainloop = nullptr;
            return false;
        }

        pa_context_set_state_callback(m_ctx, contextStateCallback, this);
        pa_threaded_mainloop_lock(m_mainloop);
        pa_threaded_mainloop_start(m_mainloop);

        if (pa_context_connect(m_ctx, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) {
            pa_threaded_mainloop_unlock(m_mainloop);
            return false;
        }

        QElapsedTimer timer;
        timer.start();
        while (!m_stopping && timer.elapsed() < 3000) {
            pa_context_state_t st = pa_context_get_state(m_ctx);
            if (st == PA_CONTEXT_READY) break;
            if (!PA_CONTEXT_IS_GOOD(st)) {
                pa_threaded_mainloop_unlock(m_mainloop);
                return false;
            }
            waitForMainloopSignal(100);
        }
        pa_threaded_mainloop_unlock(m_mainloop);
        return pa_context_get_state(m_ctx) == PA_CONTEXT_READY;
    }

    static void contextStateCallback(pa_context *, void *ud)
    {
        PaWorker *self = static_cast<PaWorker *>(ud);
        pa_threaded_mainloop_signal(self->m_mainloop, 0);
    }

    static void operationDoneCallback(pa_context *, int, void *ud)
    {
        PaWorker *self = static_cast<PaWorker *>(ud);
        if (self && self->m_mainloop)
            pa_threaded_mainloop_signal(self->m_mainloop, 0);
    }

    void waitForMainloopSignal(int timeoutMs)
    {
        pa_threaded_mainloop *mainloop = m_mainloop;
        std::atomic_bool waiting { true };
        std::thread watchdog([mainloop, timeoutMs, &waiting]() {
            int waitedMs = 0;
            while (waiting && waitedMs < timeoutMs) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                waitedMs += 10;
            }
            if (waiting && mainloop)
                pa_threaded_mainloop_signal(mainloop, 0);
        });
        pa_threaded_mainloop_wait(m_mainloop);
        waiting = false;
        watchdog.join();
    }

    bool waitForOperation(pa_operation *op, const char *what, int timeoutMs = 1500)
    {
        if (!op) {
            qWarning() << "[PaWorker]" << what << "did not create a PA operation";
            return false;
        }

        QElapsedTimer timer;
        timer.start();
        while (!m_stopping && pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
            if (timer.elapsed() >= timeoutMs)
                break;
            waitForMainloopSignal(100);
        }

        bool ok = pa_operation_get_state(op) == PA_OPERATION_DONE;
        if (!ok && !m_stopping) {
            qWarning() << "[PaWorker]" << what << "timed out or was cancelled after"
                       << timer.elapsed() << "ms";
        }
        if (op) pa_operation_unref(op);
        return ok;
    }

    // ── Sink input helpers ────────────────────────────────────────────────────
    struct SinkInputInfo { uint32_t index; QString name, binary; double volume; bool muted; };

    struct SinkInputListCbData { PaWorker *self; QList<SinkInputInfo> *result; };
    static void sinkInputListCallback(pa_context *, const pa_sink_input_info *info,
                                      int eol, void *ud)
    {
        auto *d = static_cast<SinkInputListCbData *>(ud);
        if (eol || !info) { pa_threaded_mainloop_signal(d->self->m_mainloop, 0); return; }

        SinkInputInfo si;
        si.index  = info->index;
        si.name   = QString::fromUtf8(
            pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_NAME)
            ?: pa_proplist_gets(info->proplist, PA_PROP_MEDIA_NAME)
            ?: "Unknown");
        si.binary = QString::fromUtf8(
            pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_PROCESS_BINARY) ?: "");
        si.volume = static_cast<double>(pa_cvolume_avg(&info->volume)) / PA_VOLUME_NORM;
        si.muted  = info->mute != 0;
        d->result->append(si);
    }

    QList<SinkInputInfo> getSinkInputs() // caller must hold mainloop lock
    {
        QList<SinkInputInfo> result;
        SinkInputListCbData d { this, &result };
        waitForOperation(pa_context_get_sink_input_info_list(m_ctx, sinkInputListCallback, &d),
                         "list sink inputs");
        return result;
    }

    // ── Stream restore ────────────────────────────────────────────────────────
    struct StreamRestoreCbData {
        PaWorker *self;
        QString   target;
        double    delta;
        bool      isMute;
        std::optional<double>                 *outVol;
        std::optional<std::pair<bool,double>> *outMute;
    };

    static void streamRestoreReadCallback(pa_context *ctx,
                                          const pa_ext_stream_restore_info *info,
                                          int eol, void *ud)
    {
        auto *d = static_cast<StreamRestoreCbData *>(ud);
        if (eol) { pa_threaded_mainloop_signal(d->self->m_mainloop, 0); return; }
        if (!info || QString::fromUtf8(info->name) != d->target) return;

        double vol = static_cast<double>(pa_cvolume_avg(&info->volume)) / PA_VOLUME_NORM;
        if (!d->isMute) {
            double newVol = std::clamp(vol + d->delta, 0.0, 1.0);
            pa_ext_stream_restore_info out = *info;
            pa_cvolume_set(const_cast<pa_cvolume *>(&out.volume), 2,
                           static_cast<pa_volume_t>(newVol * PA_VOLUME_NORM));
            pa_operation *op = pa_ext_stream_restore_write(
                ctx, PA_UPDATE_REPLACE, &out, 1, 1, operationDoneCallback, d->self);
            if (op) pa_operation_unref(op);
            *d->outVol = newVol;
        } else {
            bool newMuted = !info->mute;
            pa_ext_stream_restore_info out = *info;
            out.mute = newMuted ? 1 : 0;
            pa_operation *op = pa_ext_stream_restore_write(
                ctx, PA_UPDATE_REPLACE, &out, 1, 1, operationDoneCallback, d->self);
            if (op) pa_operation_unref(op);
            *d->outMute = std::make_pair(newMuted, vol);
        }
    }

    std::optional<double> streamRestoreChangeVolume(const QString &app, double delta)
    {
        if (!m_mainloop || !m_ctx) return std::nullopt;
        std::optional<double> result;
        StreamRestoreCbData d { this,
            QStringLiteral("sink-input-by-application-name:") + app,
            delta, false, &result, nullptr };
        pa_threaded_mainloop_lock(m_mainloop);
        waitForOperation(pa_ext_stream_restore_read(m_ctx, streamRestoreReadCallback, &d),
                         "read stream restore volume");
        pa_threaded_mainloop_unlock(m_mainloop);
        return result;
    }

    std::optional<std::pair<bool,double>> streamRestoreToggleMute(const QString &app)
    {
        if (!m_mainloop || !m_ctx) return std::nullopt;
        std::optional<std::pair<bool,double>> result;
        StreamRestoreCbData d { this,
            QStringLiteral("sink-input-by-application-name:") + app,
            0.0, true, nullptr, &result };
        pa_threaded_mainloop_lock(m_mainloop);
        waitForOperation(pa_ext_stream_restore_read(m_ctx, streamRestoreReadCallback, &d),
                         "read stream restore mute");
        pa_threaded_mainloop_unlock(m_mainloop);
        return result;
    }

    // ── PipeWire subprocess helpers ───────────────────────────────────────────
    struct PwNode { int id; double volume; bool muted; };

    std::optional<PwNode> findPwNodeForApp(const QString &appName)
    {
        QProcess p;
        p.start(QStringLiteral("pw-dump"), QStringList{});
        if (!p.waitForFinished(2000)) return std::nullopt;

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(p.readAllStandardOutput(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isArray()) return std::nullopt;

        std::optional<PwNode> best;
        for (const QJsonValue &val : doc.array()) {
            QJsonObject obj = val.toObject();
            if (!obj[QStringLiteral("type")].toString().contains(QStringLiteral("Node")))
                continue;
            QJsonObject info  = obj[QStringLiteral("info")].toObject();
            QJsonObject props = info[QStringLiteral("props")].toObject();
            QString name   = props[QStringLiteral("application.name")].toString();
            QString binary = props[QStringLiteral("application.process.binary")].toString();
            if (name != appName && binary != appName) continue;

            QString mediaClass = props[QStringLiteral("media.class")].toString();
            if (!mediaClass.startsWith(QStringLiteral("Stream/"))) continue;

            QJsonArray propList = info[QStringLiteral("params")].toObject()
                                      [QStringLiteral("Props")].toArray();
            double vol   = 1.0;
            bool   muted = false;
            if (!propList.isEmpty()) {
                QJsonObject p0 = propList[0].toObject();
                vol   = p0[QStringLiteral("volume")].toDouble(1.0);
                muted = p0[QStringLiteral("mute")].toBool(false);
            }
            PwNode node { obj[QStringLiteral("id")].toInt(), vol, muted };
            if (mediaClass.contains(QStringLiteral("Output"))) return node;
            best = node;
        }
        return best;
    }

    bool setPwNodeVolume(int nodeId, double volume)
    {
        QProcess p;
        p.start(QStringLiteral("pw-cli"), {
            QStringLiteral("set-param"), QString::number(nodeId),
            QStringLiteral("Props"),
            QStringLiteral("{ volume: %1 }").arg(volume, 0, 'f', 6),
        });
        return p.waitForFinished(1000);
    }

    bool setPwNodeMute(int nodeId, bool muted)
    {
        QProcess p;
        p.start(QStringLiteral("pw-cli"), {
            QStringLiteral("set-param"), QString::number(nodeId),
            QStringLiteral("Props"),
            muted ? QStringLiteral("{ mute: true }") : QStringLiteral("{ mute: false }"),
        });
        return p.waitForFinished(1000);
    }

    void removePending(const QString &app)
    {
        if (m_watcher) {
            QMutexLocker lk(&m_watcher->pendingMutex);
            m_watcher->pendingVolumes.remove(app);
            m_watcher->pendingMutes.remove(app);
        }
    }
};

// ─── VolumeController ─────────────────────────────────────────────────────────
VolumeController::VolumeController(QObject *parent)
    : QObject(parent)
{
    m_paThread = new QThread(this);
    m_worker   = new PaWorker();        // no parent — will live in m_paThread
    m_worker->moveToThread(m_paThread);

    // Forward worker signals to our own signals (runs in main thread via queued)
    connect(m_worker, &PaWorker::volumeChanged, this, &VolumeController::volumeChanged);
    connect(m_worker, &PaWorker::appsReady, this, [this](const QList<AudioApp> &apps) {
        m_listCache = apps;
        emit appsReady(apps);
    });

    // Clean up worker when thread finishes
    connect(m_paThread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_paThread->start();
    // init() runs in PA thread: connects to PA, starts watcher, fetches initial list
    QMetaObject::invokeMethod(m_worker, "init", Qt::QueuedConnection);
}

VolumeController::~VolumeController()
{
    close();
}

void VolumeController::close()
{
    if (m_paThread && !m_closing) {
        m_closing = true;
        disconnect(m_worker, &PaWorker::volumeChanged, this, &VolumeController::volumeChanged);
        disconnect(m_worker, nullptr, this, nullptr);

        QEventLoop cleanupLoop;
        bool cleanupDone = false;
        connect(m_worker, &PaWorker::cleanupFinished, &cleanupLoop, [&]() {
            cleanupDone = true;
            cleanupLoop.quit();
        });
        QTimer::singleShot(2500, &cleanupLoop, &QEventLoop::quit);

        QMetaObject::invokeMethod(m_worker, "cleanup", Qt::QueuedConnection);
        cleanupLoop.exec();
        if (!cleanupDone)
            qWarning() << "[VolumeController] PA cleanup did not finish in 2.5s";

        m_paThread->quit();
        if (!m_paThread->wait(2000)) {
            qWarning() << "[VolumeController] PA thread did not quit in 2s — terminating";
            m_paThread->terminate();
            m_paThread->wait(500);
        }
        m_paThread = nullptr;
        // m_worker deleted via QThread::finished → deleteLater
        m_worker = nullptr;
    }
}

QList<AudioApp> VolumeController::listApps(bool forceRefresh)
{
    if (m_closing || !m_worker) return m_listCache;

    // Kick off a background refresh (worker checks cache TTL unless forced)
    QMetaObject::invokeMethod(m_worker, "doListApps", Qt::QueuedConnection,
                              Q_ARG(bool, forceRefresh));
    return m_listCache; // return cached data immediately — no blocking
}

void VolumeController::changeVolume(const QString &appName, double delta)
{
    if (m_closing || !m_worker) return;

    QMetaObject::invokeMethod(m_worker, "doChangeVolume", Qt::QueuedConnection,
                              Q_ARG(QString, appName), Q_ARG(double, delta));
}

void VolumeController::toggleMute(const QString &appName)
{
    if (m_closing || !m_worker) return;

    QMetaObject::invokeMethod(m_worker, "doToggleMute", Qt::QueuedConnection,
                              Q_ARG(QString, appName));
}

#include "volumecontroller.moc"
