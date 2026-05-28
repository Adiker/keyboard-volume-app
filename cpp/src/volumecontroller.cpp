#include "volumecontroller.h"
#include "pwutils.h"

#include <QDateTime>
#include <QDebug>
#include <QTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QStringList>
#include <algorithm>
#include <atomic>
#include <utility>

#include <pulse/thread-mainloop.h>
#include <pulse/context.h>
#include <pulse/introspect.h>
#include <pulse/subscribe.h>
#include <pulse/volume.h>
#include <pulse/ext-stream-restore.h>

namespace
{
constexpr int PA_CONNECT_TIMEOUT_MS = 1000;
constexpr int RECONNECT_BACKOFF_MAX_MS = 30000;

int nextReconnectBackoff(int currentMs)
{
    if (currentMs <= 0) return 500;
    return std::min(currentMs * 2, RECONNECT_BACKOFF_MAX_MS);
}

bool isGenericAppName(const QString& name)
{
    const QString lower = name.trimmed().toLower();
    return lower == QStringLiteral("chromium") || lower == QStringLiteral("chrome") ||
           lower == QStringLiteral("brave");
}

bool containsAppKey(const QMap<QString, AudioApp>& apps, const QString& name)
{
    for (auto it = apps.constBegin(); it != apps.constEnd(); ++it)
    {
        if (it.key().compare(name, Qt::CaseInsensitive) == 0 ||
            it.value().binary.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}
} // namespace

// ─── PaWatcherThread ──────────────────────────────────────────────────────────
// Maintains its own PA connection and subscribes to sink-input events.
// When a new sink input appears, emits sinkInputAppeared().
// Lives in its own QThread; signal is emitted from the PA internal thread
// (queued connections to any receiver are safe).
class PaWatcherThread : public QThread
{
    Q_OBJECT
  public:
    explicit PaWatcherThread(QObject* parent = nullptr) : QThread(parent) {}
    ~PaWatcherThread() override
    {
        stop();
    }

    void stop()
    {
        m_stopping = true;
        if (isRunning())
        {
            wait(1000);
            if (isRunning()) terminate();
        }
    }

  signals:
    void sinkInputAppeared();
    void sinkInputRemoved();
    void sinkChanged();
    void connectionReady();

  protected:
    void run() override
    {
        while (!m_stopping)
        {
            bool connected = connectOnce();
            if (connected)
            {
                m_backoffMs = 0;
                emit connectionReady();

                while (!m_stopping && contextIsGood()) msleep(100);
            }

            disconnectContext();
            if (m_stopping) break;

            m_backoffMs = nextReconnectBackoff(m_backoffMs);
            int sleptMs = 0;
            while (!m_stopping && sleptMs < m_backoffMs)
            {
                msleep(100);
                sleptMs += 100;
            }
        }
    }

  private:
    pa_threaded_mainloop* m_mainloop = nullptr;
    pa_context* m_ctx = nullptr;
    std::atomic<bool> m_stopping{false};
    int m_backoffMs = 0;

    bool connectOnce()
    {
        m_mainloop = pa_threaded_mainloop_new();
        if (!m_mainloop) return false;

        pa_mainloop_api* api = pa_threaded_mainloop_get_api(m_mainloop);
        m_ctx = pa_context_new(api, "keyboard-volume-app-watcher");
        if (!m_ctx)
        {
            disconnectContext();
            return false;
        }

        pa_context_set_state_callback(m_ctx, contextStateCallback, this);
        pa_threaded_mainloop_lock(m_mainloop);
        pa_threaded_mainloop_start(m_mainloop);

        if (pa_context_connect(m_ctx, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0)
        {
            pa_threaded_mainloop_unlock(m_mainloop);
            disconnectContext();
            return false;
        }

        QElapsedTimer timer;
        timer.start();
        while (!m_stopping && timer.elapsed() < PA_CONNECT_TIMEOUT_MS)
        {
            pa_context_state_t st = pa_context_get_state(m_ctx);
            if (st == PA_CONTEXT_READY)
            {
                pa_context_subscribe(
                    m_ctx,
                    static_cast<pa_subscription_mask_t>(PA_SUBSCRIPTION_MASK_SINK_INPUT |
                                                        PA_SUBSCRIPTION_MASK_SINK),
                    nullptr, nullptr);
                pa_context_set_subscribe_callback(m_ctx, subscribeCallback, this);
                pa_threaded_mainloop_unlock(m_mainloop);
                return true;
            }
            if (!PA_CONTEXT_IS_GOOD(st)) break;
            pa_threaded_mainloop_unlock(m_mainloop);
            msleep(20);
            pa_threaded_mainloop_lock(m_mainloop);
        }
        pa_threaded_mainloop_unlock(m_mainloop);
        return false;
    }

    bool contextIsGood()
    {
        if (!m_mainloop || !m_ctx) return false;
        pa_threaded_mainloop_lock(m_mainloop);
        const pa_context_state_t st = pa_context_get_state(m_ctx);
        const bool good = PA_CONTEXT_IS_GOOD(st);
        pa_threaded_mainloop_unlock(m_mainloop);
        return good;
    }

    void disconnectContext()
    {
        if (!m_mainloop) return;

        if (m_ctx)
        {
            pa_threaded_mainloop_lock(m_mainloop);
            pa_context_set_state_callback(m_ctx, nullptr, nullptr);
            pa_context_set_subscribe_callback(m_ctx, nullptr, nullptr);
            pa_context_disconnect(m_ctx);
            pa_context_unref(m_ctx);
            m_ctx = nullptr;
            pa_threaded_mainloop_unlock(m_mainloop);
        }

        pa_threaded_mainloop_stop(m_mainloop);
        pa_threaded_mainloop_free(m_mainloop);
        m_mainloop = nullptr;
    }

    static void contextStateCallback(pa_context* ctx, void* ud)
    {
        PaWatcherThread* self = static_cast<PaWatcherThread*>(ud);
        Q_UNUSED(ctx);
        Q_UNUSED(self);
    }

    static void subscribeCallback(pa_context*, pa_subscription_event_type_t t, uint32_t, void* ud)
    {
        PaWatcherThread* self = static_cast<PaWatcherThread*>(ud);
        int facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
        int type = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;
        if (facility == PA_SUBSCRIPTION_EVENT_SINK_INPUT)
        {
            if (type == PA_SUBSCRIPTION_EVENT_NEW)
                emit self->sinkInputAppeared();
            else if (type == PA_SUBSCRIPTION_EVENT_REMOVE)
                emit self->sinkInputRemoved();
        }
        else if (facility == PA_SUBSCRIPTION_EVENT_SINK)
        {
            emit self->sinkChanged();
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
    ~PaWorker() override
    {
        cleanup();
    }

  public slots:
    // Called once from the PA thread after moveToThread + start.
    void init()
    {
        if (m_stopping) return;

        // Debounced app-list refresh on sink input changes (500ms).
        m_refreshTimer = new QTimer(this);
        m_refreshTimer->setSingleShot(true);
        m_refreshTimer->setInterval(500);
        connect(m_refreshTimer, &QTimer::timeout, this,
                [this]() { doListApps(/*forceRefresh=*/true); });

        // Debounced sink-list refresh on PA sink changes (500ms).
        m_sinkRefreshTimer = new QTimer(this);
        m_sinkRefreshTimer->setSingleShot(true);
        m_sinkRefreshTimer->setInterval(500);
        connect(m_sinkRefreshTimer, &QTimer::timeout, this,
                [this]() { doListSinks(/*forceRefresh=*/true); });

        m_reconnectTimer = new QTimer(this);
        m_reconnectTimer->setSingleShot(true);
        connect(m_reconnectTimer, &QTimer::timeout, this, &PaWorker::attemptReconnect);

        attemptReconnect();
    }

    void cleanup()
    {
        if (m_cleanedUp)
        {
            emit cleanupFinished();
            return;
        }
        requestStop();

        if (m_reconnectTimer)
        {
            m_reconnectTimer->stop();
        }
        if (m_refreshTimer)
        {
            m_refreshTimer->stop();
        }
        if (m_sinkRefreshTimer)
        {
            m_sinkRefreshTimer->stop();
        }

        if (m_watcher)
        {
            disconnect(m_watcher, nullptr, this, nullptr);
            m_watcher->stop();
            delete m_watcher;
            m_watcher = nullptr;
        }
        disconnectContext();
        m_cleanedUp = true;
        emit cleanupFinished();
    }

    void requestStop()
    {
        m_stopping = true;
    }

    void attemptReconnect()
    {
        if (m_stopping || contextReady()) return;

        clearDuckingState();
        disconnectContext();
        if (connectContext())
        {
            m_reconnectBackoffMs = 0;
            startWatcher();
            doListSinks(/*forceRefresh=*/true);
            doApplyPending();
            doListApps(/*forceRefresh=*/true);
            return;
        }

        qWarning() << "[PaWorker] Cannot connect to PulseAudio";
        scheduleReconnect();
        doListApps(/*forceRefresh=*/true);
    }

    void handleContextLost()
    {
        if (m_stopping) return;
        clearDuckingState();
        disconnectContext();
        scheduleReconnect();
        doListApps(/*forceRefresh=*/true);
    }

    void doChangeVolume(const QString& appName, double delta, double minVol = 0.0,
                        double maxVol = 1.0)
    {
        if (m_stopping) return;

        // Sanitize bounds so a misconfigured profile cannot break volume control.
        minVol = std::clamp(minVol, 0.0, 1.0);
        maxVol = std::clamp(maxVol, 0.0, 1.0);
        if (minVol > maxVol) std::swap(minVol, maxVol);

        // 1. Active sink input (fast path)
        if (contextReady())
        {
            pa_threaded_mainloop_lock(m_mainloop);
            const auto inputs = getSinkInputs();
            for (const auto& si : inputs)
            {
                if (!si.matches(appName)) continue;

                double newVol = std::clamp(si.volume + delta, minVol, maxVol);
                pa_cvolume cv;
                pa_cvolume_set(&cv, 2, static_cast<pa_volume_t>(newVol * PA_VOLUME_NORM));
                pa_operation* op = pa_context_set_sink_input_volume(m_ctx, si.index, &cv,
                                                                    operationDoneCallback, this);
                if (!waitForOperation(op, "set sink input volume"))
                {
                    pa_threaded_mainloop_unlock(m_mainloop);
                    return;
                }
                pa_threaded_mainloop_unlock(m_mainloop);

                m_appVolumes[appName] = newVol;
                m_appMutes[appName] = si.muted;
                removePending(appName);
                emit volumeChanged(appName, newVol, si.muted);
                return;
            }
            pa_threaded_mainloop_unlock(m_mainloop);
        }

        // 2. Stream restore DB
        auto vol = streamRestoreChangeVolume(appName, delta, minVol, maxVol);
        if (vol)
        {
            m_appVolumes[appName] = *vol;
            removePending(appName);
            emit volumeChanged(appName, *vol, m_appMutes.value(appName, false));
            return;
        }

        // 3. PipeWire node (libpipewire)
        auto node = ::findPipeWireNodeForApp(appName);
        if (node)
        {
            double newVol = std::clamp(node->volume + delta, minVol, maxVol);
            if (::setPipeWireNodeVolume(node->id, newVol))
            {
                m_appVolumes[appName] = newVol;
                removePending(appName);
                emit volumeChanged(appName, newVol, m_appMutes.value(appName, false));
                return;
            }
        }

        // 4. App disconnected — park and show desired volume on OSD anyway
        double base = m_appVolumes.value(appName, 1.0);
        double newVol = std::clamp(base + delta, minVol, maxVol);
        m_appVolumes[appName] = newVol;
        {
            QMutexLocker lk(&m_pendingMutex);
            m_pendingVolumes[appName] = newVol;
        }
        emit volumeChanged(appName, newVol, m_appMutes.value(appName, false));
    }

    void doToggleMute(const QString& appName)
    {
        if (m_stopping) return;

        // 1. Active sink input
        if (contextReady())
        {
            pa_threaded_mainloop_lock(m_mainloop);
            const auto inputs = getSinkInputs();
            for (const auto& si : inputs)
            {
                if (!si.matches(appName)) continue;

                int newMute = si.muted ? 0 : 1;
                pa_operation* op = pa_context_set_sink_input_mute(m_ctx, si.index, newMute,
                                                                  operationDoneCallback, this);
                if (!waitForOperation(op, "set sink input mute"))
                {
                    pa_threaded_mainloop_unlock(m_mainloop);
                    return;
                }
                pa_threaded_mainloop_unlock(m_mainloop);

                m_appVolumes[appName] = si.volume;
                m_appMutes[appName] = static_cast<bool>(newMute);
                removePending(appName);
                emit volumeChanged(appName, si.volume, static_cast<bool>(newMute));
                return;
            }
            pa_threaded_mainloop_unlock(m_mainloop);
        }

        // 2. Stream restore
        auto result = streamRestoreToggleMute(appName);
        if (result)
        {
            m_appMutes[appName] = result->first;
            {
                QMutexLocker lk(&m_pendingMutex);
                m_pendingMutes.remove(appName);
            }
            emit volumeChanged(appName, result->second, result->first);
            return;
        }

        // 3. PipeWire node
        auto node = ::findPipeWireNodeForApp(appName);
        if (node)
        {
            bool newMuted = !node->muted;
            if (::setPipeWireNodeMute(node->id, newMuted))
            {
                m_appMutes[appName] = newMuted;
                {
                    QMutexLocker lk(&m_pendingMutex);
                    m_pendingMutes.remove(appName);
                }
                emit volumeChanged(appName, node->volume, newMuted);
                return;
            }
        }

        // 4. Disconnected
        bool curMuted = m_appMutes.value(appName, false);
        double vol = m_appVolumes.value(appName, 1.0);
        bool newMuted = !curMuted;
        m_appMutes[appName] = newMuted;
        {
            QMutexLocker lk(&m_pendingMutex);
            m_pendingMutes[appName] = newMuted;
        }
        emit volumeChanged(appName, vol, newMuted);
    }

    void setAppMute(const QString& appName, bool targetMuted)
    {
        if (m_stopping) return;

        // 1. Active sink input
        if (contextReady())
        {
            pa_threaded_mainloop_lock(m_mainloop);
            const auto inputs = getSinkInputs();
            for (const auto& si : inputs)
            {
                if (!si.matches(appName)) continue;

                if (si.muted == targetMuted)
                {
                    pa_threaded_mainloop_unlock(m_mainloop);
                    m_appVolumes[appName] = si.volume;
                    m_appMutes[appName] = targetMuted;
                    {
                        QMutexLocker lk(&m_pendingMutex);
                        m_pendingMutes.remove(appName);
                    }
                    emit volumeChanged(appName, si.volume, targetMuted);
                    return;
                }

                pa_operation* op = pa_context_set_sink_input_mute(
                    m_ctx, si.index, targetMuted ? 1 : 0, operationDoneCallback, this);
                if (!waitForOperation(op, "set sink input absolute mute"))
                {
                    pa_threaded_mainloop_unlock(m_mainloop);
                    return;
                }
                pa_threaded_mainloop_unlock(m_mainloop);

                m_appVolumes[appName] = si.volume;
                m_appMutes[appName] = targetMuted;
                {
                    QMutexLocker lk(&m_pendingMutex);
                    m_pendingMutes.remove(appName);
                }
                emit volumeChanged(appName, si.volume, targetMuted);
                return;
            }
            pa_threaded_mainloop_unlock(m_mainloop);
        }

        // 2. Stream restore
        auto result = streamRestoreSetMute(appName, targetMuted);
        if (result)
        {
            m_appMutes[appName] = result->first;
            {
                QMutexLocker lk(&m_pendingMutex);
                m_pendingMutes.remove(appName);
            }
            emit volumeChanged(appName, result->second, result->first);
            return;
        }

        // 3. PipeWire node
        auto node = ::findPipeWireNodeForApp(appName);
        if (node && ::setPipeWireNodeMute(node->id, targetMuted))
        {
            m_appMutes[appName] = targetMuted;
            {
                QMutexLocker lk(&m_pendingMutex);
                m_pendingMutes.remove(appName);
            }
            emit volumeChanged(appName, node->volume, targetMuted);
            return;
        }

        // 4. Disconnected — park desired absolute mute.
        const double vol = m_appVolumes.value(appName, 1.0);
        m_appMutes[appName] = targetMuted;
        {
            QMutexLocker lk(&m_pendingMutex);
            m_pendingMutes[appName] = targetMuted;
        }
        emit volumeChanged(appName, vol, targetMuted);
    }

    void setAppVolume(const QString& appName, double targetVolume, double minVol = 0.0,
                      double maxVol = 1.0)
    {
        if (m_stopping) return;

        // Sanitize bounds — caller may pass invalid values.
        minVol = std::clamp(minVol, 0.0, 1.0);
        maxVol = std::clamp(maxVol, 0.0, 1.0);
        if (minVol > maxVol) std::swap(minVol, maxVol);
        targetVolume = std::clamp(targetVolume, minVol, maxVol);

        // 1. Active sink input (absolute target, not relative delta)
        if (contextReady())
        {
            pa_threaded_mainloop_lock(m_mainloop);
            const auto inputs = getSinkInputs();
            for (const auto& si : inputs)
            {
                if (!si.matches(appName)) continue;

                pa_cvolume cv;
                pa_cvolume_set(&cv, 2, static_cast<pa_volume_t>(targetVolume * PA_VOLUME_NORM));
                pa_operation* op = pa_context_set_sink_input_volume(m_ctx, si.index, &cv,
                                                                    operationDoneCallback, this);
                if (!waitForOperation(op, "set sink input absolute volume"))
                {
                    pa_threaded_mainloop_unlock(m_mainloop);
                    return;
                }
                pa_threaded_mainloop_unlock(m_mainloop);

                m_appVolumes[appName] = targetVolume;
                m_appMutes[appName] = si.muted;
                removePending(appName);
                emit volumeChanged(appName, targetVolume, si.muted);
                return;
            }
            pa_threaded_mainloop_unlock(m_mainloop);
        }

        // 2. Stream restore DB
        if (streamRestoreSetVolume(appName, targetVolume, minVol, maxVol))
        {
            m_appVolumes[appName] = targetVolume;
            removePending(appName);
            emit volumeChanged(appName, targetVolume, m_appMutes.value(appName, false));
            return;
        }

        // 3. PipeWire node (libpipewire)
        auto node = ::findPipeWireNodeForApp(appName);
        if (node && ::setPipeWireNodeVolume(node->id, targetVolume))
        {
            m_appVolumes[appName] = targetVolume;
            removePending(appName);
            emit volumeChanged(appName, targetVolume, m_appMutes.value(appName, false));
            return;
        }

        // 4. Disconnected — park desired absolute volume.
        m_appVolumes[appName] = targetVolume;
        {
            QMutexLocker lk(&m_pendingMutex);
            m_pendingVolumes[appName] = targetVolume;
        }
        emit volumeChanged(appName, targetVolume, m_appMutes.value(appName, false));
    }

    void doToggleDucking(const QString& keepApp, double duckVolume)
    {
        if (m_stopping || keepApp.isEmpty()) return;

        duckVolume = std::clamp(duckVolume, 0.0, 1.0);

        if (m_duckingActive)
        {
            const QMap<QString, DuckingSnapshot> restore = m_duckingSnapshot;
            clearDuckingState();

            for (auto it = restore.begin(); it != restore.end(); ++it)
            {
                setAppVolume(it.key(), it.value().volume);
            }
            return;
        }

        doListApps(/*forceRefresh=*/true);

        QMap<QString, DuckingSnapshot> snapshot;
        for (const AudioApp& app : std::as_const(m_listCache))
        {
            if (app.name.isEmpty()) continue;
            if (app.name == keepApp || app.binary == keepApp) continue;

            double currentVolume = app.volume;
            if (!app.active)
            {
                if (auto node = ::findPipeWireNodeForApp(app.name))
                {
                    currentVolume = node->volume;
                }
            }

            if (qAbs(currentVolume - duckVolume) <= 0.0001) continue;

            snapshot[app.name] = DuckingSnapshot{currentVolume};
        }

        if (snapshot.isEmpty()) return;

        m_duckingActive = true;
        m_duckingKeepApp = keepApp;
        m_duckingSnapshot = snapshot;

        for (auto it = snapshot.begin(); it != snapshot.end(); ++it)
        {
            setAppVolume(it.key(), duckVolume);
        }
    }

    void doQueryVolume(const QString& appName)
    {
        if (m_stopping) return;

        // 1. Active sink input — live read, no write
        if (contextReady())
        {
            pa_threaded_mainloop_lock(m_mainloop);
            const auto inputs = getSinkInputs();
            for (const auto& si : inputs)
            {
                if (!si.matches(appName)) continue;

                pa_threaded_mainloop_unlock(m_mainloop);
                m_appVolumes[appName] = si.volume;
                m_appMutes[appName] = si.muted;
                emit volumeChanged(appName, si.volume, si.muted);
                return;
            }
            pa_threaded_mainloop_unlock(m_mainloop);
        }

        // 2. Stream restore DB (persisted volume for inactive apps)
        if (auto restored = streamRestoreQueryVolume(appName))
        {
            const auto [muted, volume] = *restored;
            m_appVolumes[appName] = volume;
            m_appMutes[appName] = muted;
            emit volumeChanged(appName, volume, muted);
            return;
        }

        // 3. PipeWire node (idle/paused app)
        auto node = ::findPipeWireNodeForApp(appName);
        if (node)
        {
            m_appVolumes[appName] = node->volume;
            m_appMutes[appName] = node->muted;
            emit volumeChanged(appName, node->volume, node->muted);
            return;
        }

        // 4. Cache fallback (app not currently present in PA or PW)
        emit volumeChanged(appName, m_appVolumes.value(appName, 1.0),
                           m_appMutes.value(appName, false));
    }

    void doListApps(bool forceRefresh)
    {
        if (m_stopping) return;

        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (!forceRefresh && (now - m_listCacheTs) < LIST_CACHE_TTL_MS)
            return; // cache still fresh — caller already has it

        const auto pwClients = ::listPipeWireClients();
        QMap<QString, QString> displayByTarget;
        QMap<QString, QString> displayByClientId;
        QMap<QString, QString> targetByClientId;
        for (const PipeWireClient& client : pwClients)
        {
            if (!client.binary.isEmpty()) displayByTarget[client.binary.toLower()] = client.name;
            if (!client.id.isEmpty()) displayByClientId[client.id] = client.name;
            if (!client.id.isEmpty() && !client.binary.isEmpty())
                targetByClientId[client.id] = client.binary;
        }

        QMap<QString, AudioApp> apps;
        QSet<QString> activeBinaries;

        // 1. Active sink inputs
        if (contextReady())
        {
            pa_threaded_mainloop_lock(m_mainloop);
            const auto inputs = getSinkInputs();
            pa_threaded_mainloop_unlock(m_mainloop);

            for (const auto& si : inputs)
            {
                const QString displayName = si.displayName();
                const QString targetName = si.targetName();
                QString mappedDisplay = displayByClientId.value(si.clientId);
                if (mappedDisplay.isEmpty())
                    mappedDisplay = displayByTarget.value(targetName.toLower(), displayName);
                const QString mappedTarget = targetByClientId.value(si.clientId, targetName);

                AudioApp app;
                app.sinkInputIndex = si.index;
                app.name = mappedDisplay;
                app.binary = mappedTarget;
                app.volume = si.volume;
                app.muted = si.muted;
                app.active = true;
                apps[mappedDisplay] = app;
                if (!mappedTarget.isEmpty()) activeBinaries.insert(mappedTarget);
            }
        }

        // 2. Idle PW clients (libpipewire — runs here in PA thread, not main thread)
        for (const PipeWireClient& client : pwClients)
        {
            if (SKIP_APP_NAMES.contains(client.name)) continue;
            if (activeBinaries.contains(client.binary)) continue;
            if (containsAppKey(apps, client.name)) continue;
            if (containsAppKey(apps, client.binary)) continue;

            AudioApp app;
            app.name = client.name;
            app.binary = client.binary;
            app.volume = 1.0;
            app.muted = false;
            app.active = false;
            apps[client.name] = app;
        }

        QList<AudioApp> result = apps.values();
        std::sort(result.begin(), result.end(),
                  [](const AudioApp& a, const AudioApp& b)
                  {
                      if (a.active != b.active) return a.active > b.active;
                      return a.name.toLower() < b.name.toLower();
                  });

        m_listCache = result;
        m_listCacheTs = now;
        emit appsReady(result);
    }

    // Called via QTimer::singleShot(100ms) — runs in PA thread.
    void doApplyPending()
    {
        if (m_stopping || !contextReady()) return;

        QMap<QString, double> pendVols;
        QMap<QString, bool> pendMutes;
        QMap<QString, QString> pendSinks;
        {
            QMutexLocker lk(&m_pendingMutex);
            if (m_pendingVolumes.isEmpty() && m_pendingMutes.isEmpty() && m_pendingSinks.isEmpty())
                return;
            pendVols = m_pendingVolumes;
            pendMutes = m_pendingMutes;
            pendSinks = m_pendingSinks;
        }

        pa_threaded_mainloop_lock(m_mainloop);
        const auto inputs = getSinkInputs();
        QSet<QString> applied;
        QSet<QString> appliedSinks;

        // First pass: move sink-inputs to their pending sinks before adjusting
        // volume/mute so any device-specific behaviour (per-sink default cvolume,
        // etc.) is settled by the time we write the volume. The success arg of
        // PA's done callback is checked so a stale/unknown sink name doesn't
        // get reported as a successful route.
        for (const auto& si : inputs)
        {
            for (auto it = pendSinks.begin(); it != pendSinks.end(); ++it)
            {
                const QString& app = it.key();
                if (!si.matches(app)) continue;

                const QByteArray sinkBytes = it.value().toUtf8();
                OperationOutcome outcome;
                pa_operation* op = pa_context_move_sink_input_by_name(
                    m_ctx, si.index, sinkBytes.constData(), operationOutcomeCallback, &outcome);
                const bool completed = waitForOperation(op, "apply pending sink move");
                if (completed && outcome.success)
                    appliedSinks.insert(app);
                else if (completed)
                    qWarning() << "[PaWorker] pending sink move for" << app << "→" << it.value()
                               << "rejected by PA";
            }
        }

        for (const auto& si : inputs)
        {
            for (auto it = pendVols.begin(); it != pendVols.end(); ++it)
            {
                const QString& app = it.key();
                if (!si.matches(app)) continue;

                pa_cvolume cv;
                pa_cvolume_set(&cv, 2, static_cast<pa_volume_t>(it.value() * PA_VOLUME_NORM));
                bool volumeApplied =
                    waitForOperation(pa_context_set_sink_input_volume(m_ctx, si.index, &cv,
                                                                      operationDoneCallback, this),
                                     "apply pending sink input volume");

                bool muteApplied = true;
                if (pendMutes.contains(app))
                {
                    muteApplied = waitForOperation(
                        pa_context_set_sink_input_mute(m_ctx, si.index, pendMutes[app] ? 1 : 0,
                                                       operationDoneCallback, this),
                        "apply pending sink input mute");
                }

                if (volumeApplied && muteApplied) applied.insert(app);
            }
        }

        // Second pass: apps with a pending mute but no pending volume change
        for (const auto& si : inputs)
        {
            for (auto it = pendMutes.begin(); it != pendMutes.end(); ++it)
            {
                const QString& app = it.key();
                if (pendVols.contains(app)) continue; // already handled above
                if (!si.matches(app)) continue;

                bool muteApplied = waitForOperation(
                    pa_context_set_sink_input_mute(m_ctx, si.index, it.value() ? 1 : 0,
                                                   operationDoneCallback, this),
                    "apply pending sink input mute only");

                if (muteApplied) applied.insert(app);
            }
        }
        pa_threaded_mainloop_unlock(m_mainloop);

        // Persist sink routing in stream-restore so it survives app restarts.
        // doSetAppSink() already attempted this, but if the app had no existing
        // stream-restore entry at that time (idle app, cleared restore DB) the
        // write was a no-op. Now that the stream exists we can succeed.
        // Only the apps where persistence actually wrote can be safely cleared
        // from m_pendingSinks — otherwise we keep the parked route so the next
        // sink-input appearance retries the SR write.
        QSet<QString> persistedSinks;
        for (const auto& app : appliedSinks)
        {
            auto it = pendSinks.find(app);
            if (it != pendSinks.end() && streamRestoreSetDevice(app, it.value()))
                persistedSinks.insert(app);
        }

        if (!applied.isEmpty() || !persistedSinks.isEmpty())
        {
            QMutexLocker lk(&m_pendingMutex);
            for (const auto& app : applied)
            {
                m_pendingVolumes.remove(app);
                m_pendingMutes.remove(app);
            }
            for (const auto& app : persistedSinks) m_pendingSinks.remove(app);
        }
    }

    // ── Sink enumeration ──────────────────────────────────────────────────────
    void doListSinks(bool forceRefresh)
    {
        if (m_stopping || !contextReady()) return;

        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (!forceRefresh && (now - m_sinkCacheTs) < LIST_CACHE_TTL_MS) return;

        QList<SinkInfo> sinks;
        QString defaultName;

        pa_threaded_mainloop_lock(m_mainloop);

        ServerInfoCbData srv{&defaultName};
        waitForOperation(pa_context_get_server_info(m_ctx, serverInfoCallback, &srv),
                         "get server info");

        SinkInfoListCbData sd{&sinks};
        waitForOperation(pa_context_get_sink_info_list(m_ctx, sinkInfoListCallback, &sd),
                         "list sinks");

        pa_threaded_mainloop_unlock(m_mainloop);

        for (SinkInfo& s : sinks) s.isDefault = (s.name == defaultName);

        m_sinkNamesByIdx.clear();
        for (const SinkInfo& s : std::as_const(sinks)) m_sinkNamesByIdx.insert(s.index, s.name);

        m_sinkCache = sinks;
        m_sinkCacheTs = now;
        emit sinksReady(sinks);
    }

    // Drop the persisted stream-restore device override for an app so the next
    // stream falls back to the system default. Also clears any pending sink so
    // it doesn't re-park the stream on the previous device.
    void doClearAppSinkOverride(const QString& appName)
    {
        if (m_stopping || appName.isEmpty()) return;

        {
            QMutexLocker lk(&m_pendingMutex);
            m_pendingSinks.remove(appName);
        }
        m_appSinks.remove(appName);

        if (!contextReady()) return;

        struct ClearCbData
        {
            PaWorker* self;
            QString target;
        };

        auto cb = [](pa_context* ctx, const pa_ext_stream_restore_info* info, int eol, void* ud)
        {
            auto* d = static_cast<ClearCbData*>(ud);
            if (eol || !info || !d) return;
            if (QString::fromUtf8(info->name) != d->target) return;
            pa_ext_stream_restore_info out = *info;
            out.device = nullptr; // drop preferred device → system default
            pa_operation* op = pa_ext_stream_restore_write(ctx, PA_UPDATE_REPLACE, &out, 1, 1,
                                                           operationDoneCallback, d->self);
            if (op) pa_operation_unref(op);
        };

        for (const QString& candidate : streamRestoreAppCandidates(appName))
        {
            ClearCbData d{this, QStringLiteral("sink-input-by-application-name:") + candidate};
            pa_threaded_mainloop_lock(m_mainloop);
            waitForOperation(pa_ext_stream_restore_read(m_ctx, cb, &d),
                             "stream restore clear device");
            pa_threaded_mainloop_unlock(m_mainloop);
        }

        emit sinkChanged(appName, QString{});
    }

    // ── Route an app's sink-input(s) to the named PA sink ────────────────────
    // Mirrors the volume/mute cascade: active sink-input(s) → stream-restore
    // device persistence → parking. Empty sinkName is a no-op.
    void doSetAppSink(const QString& appName, const QString& sinkName)
    {
        if (m_stopping) return;
        if (sinkName.isEmpty()) return;

        bool routedAny = false;

        // 1. Active sink inputs — move every matching stream (browser-tab case).
        if (contextReady())
        {
            const QByteArray sinkBytes = sinkName.toUtf8();
            pa_threaded_mainloop_lock(m_mainloop);
            const auto inputs = getSinkInputs();
            for (const auto& si : inputs)
            {
                if (!si.matches(appName)) continue;

                OperationOutcome outcome;
                pa_operation* op = pa_context_move_sink_input_by_name(
                    m_ctx, si.index, sinkBytes.constData(), operationOutcomeCallback, &outcome);
                const bool completed = waitForOperation(op, "move sink input to sink");
                if (completed && outcome.success)
                {
                    routedAny = true;
                }
                else
                {
                    qWarning() << "[PaWorker] move sink input" << si.index << "→" << sinkName
                               << (completed ? "rejected by PA (stale sink name?)" : "timed out");
                }
            }
            pa_threaded_mainloop_unlock(m_mainloop);
        }

        // 2. Persist via stream-restore so the routing survives app restart.
        streamRestoreSetDevice(appName, sinkName);

        // 3. Park desired sink so future sink-inputs land on the right device.
        // Always parked — even when the active move succeeded, so any future
        // sink-inputs created by the same app (browser tabs, reconnects) land
        // on the right device without waiting for the next profile activation.
        {
            QMutexLocker lk(&m_pendingMutex);
            m_pendingSinks[appName] = sinkName;
        }
        m_appSinks[appName] = sinkName;

        emit sinkChanged(appName, routedAny ? sinkName : QString{});
    }

  signals:
    void volumeChanged(const QString& app, double newVol, bool muted);
    void appsReady(QList<AudioApp> apps);
    void sinksReady(QList<SinkInfo> sinks);
    void sinkChanged(const QString& app, const QString& sinkName);
    void cleanupFinished();

  private:
    struct DuckingSnapshot
    {
        double volume = 1.0;
    };

    pa_threaded_mainloop* m_mainloop = nullptr;
    pa_context* m_ctx = nullptr;
    PaWatcherThread* m_watcher = nullptr;
    std::atomic<bool> m_stopping{false};
    bool m_cleanedUp = false;

    QTimer* m_refreshTimer = nullptr;
    QTimer* m_sinkRefreshTimer = nullptr;
    QTimer* m_reconnectTimer = nullptr;
    int m_reconnectBackoffMs = 0;
    QMutex m_pendingMutex;
    QMap<QString, double> m_pendingVolumes;
    QMap<QString, bool> m_pendingMutes;
    QMap<QString, QString> m_pendingSinks;
    QMap<QString, double> m_appVolumes;
    QMap<QString, bool> m_appMutes;
    QMap<QString, QString> m_appSinks; // last-known sink name per app
    bool m_duckingActive = false;
    QString m_duckingKeepApp;
    QMap<QString, DuckingSnapshot> m_duckingSnapshot;
    QList<AudioApp> m_listCache;
    qint64 m_listCacheTs = 0;
    QList<SinkInfo> m_sinkCache;
    qint64 m_sinkCacheTs = 0;
    QMap<uint32_t, QString> m_sinkNamesByIdx;
    static constexpr qint64 LIST_CACHE_TTL_MS = 5000;

    // ── PA context ────────────────────────────────────────────────────────────
    bool connectContext()
    {
        disconnectContext();

        m_mainloop = pa_threaded_mainloop_new();
        if (!m_mainloop) return false;

        pa_mainloop_api* api = pa_threaded_mainloop_get_api(m_mainloop);
        m_ctx = pa_context_new(api, "keyboard-volume-app");
        if (!m_ctx)
        {
            pa_threaded_mainloop_free(m_mainloop);
            m_mainloop = nullptr;
            return false;
        }

        pa_context_set_state_callback(m_ctx, contextStateCallback, this);
        pa_threaded_mainloop_lock(m_mainloop);
        pa_threaded_mainloop_start(m_mainloop);

        if (pa_context_connect(m_ctx, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0)
        {
            pa_threaded_mainloop_unlock(m_mainloop);
            disconnectContext();
            return false;
        }

        QElapsedTimer timer;
        timer.start();
        while (!m_stopping && timer.elapsed() < PA_CONNECT_TIMEOUT_MS)
        {
            pa_context_state_t st = pa_context_get_state(m_ctx);
            if (st == PA_CONTEXT_READY) break;
            if (!PA_CONTEXT_IS_GOOD(st))
            {
                pa_threaded_mainloop_unlock(m_mainloop);
                disconnectContext();
                return false;
            }
            pa_threaded_mainloop_unlock(m_mainloop);
            QThread::msleep(20);
            pa_threaded_mainloop_lock(m_mainloop);
        }
        pa_threaded_mainloop_unlock(m_mainloop);
        if (!contextReady())
        {
            disconnectContext();
            return false;
        }
        return true;
    }

    void disconnectContext()
    {
        if (!m_mainloop) return;

        if (m_ctx)
        {
            pa_threaded_mainloop_lock(m_mainloop);
            pa_context_set_state_callback(m_ctx, nullptr, nullptr);
            pa_context_disconnect(m_ctx);
            pa_context_unref(m_ctx);
            m_ctx = nullptr;
            pa_threaded_mainloop_unlock(m_mainloop);
        }

        pa_threaded_mainloop_stop(m_mainloop);
        pa_threaded_mainloop_free(m_mainloop);
        m_mainloop = nullptr;
    }

    bool contextReady()
    {
        if (!m_mainloop || !m_ctx) return false;
        pa_threaded_mainloop_lock(m_mainloop);
        const bool ready = pa_context_get_state(m_ctx) == PA_CONTEXT_READY;
        pa_threaded_mainloop_unlock(m_mainloop);
        return ready;
    }

    void scheduleReconnect()
    {
        if (m_stopping || !m_reconnectTimer) return;
        if (m_reconnectTimer->isActive()) return;

        m_reconnectBackoffMs = nextReconnectBackoff(m_reconnectBackoffMs);
        qWarning() << "[PaWorker] Scheduling PulseAudio reconnect in" << m_reconnectBackoffMs
                   << "ms";
        m_reconnectTimer->start(m_reconnectBackoffMs);
    }

    static void contextStateCallback(pa_context* ctx, void* ud)
    {
        PaWorker* self = static_cast<PaWorker*>(ud);
        const pa_context_state_t st = pa_context_get_state(ctx);

        if ((st == PA_CONTEXT_FAILED || st == PA_CONTEXT_TERMINATED) && !self->m_stopping)
        {
            QMetaObject::invokeMethod(self, "handleContextLost", Qt::QueuedConnection);
        }
    }

    static void operationDoneCallback(pa_context*, int, void* ud)
    {
        Q_UNUSED(ud);
    }

    // Outcome of a PA operation — used when we need to know whether libpulse
    // accepted or rejected the call (e.g. moving to a stale sink name).
    // pa_context completes the pa_operation either way; the rejection is
    // surfaced only through the success argument of the done callback.
    struct OperationOutcome
    {
        bool success = false;
    };

    static void operationOutcomeCallback(pa_context*, int success, void* ud)
    {
        auto* o = static_cast<OperationOutcome*>(ud);
        if (o) o->success = (success != 0);
    }

    void startWatcher()
    {
        if (m_watcher) return;

        m_watcher = new PaWatcherThread(); // no parent — lives in PA thread
        m_watcher->moveToThread(QThread::currentThread());

        // Use a one-shot timer instead of msleep so the PA thread event loop
        // stays free to process other queued calls during the 100 ms wait.
        connect(m_watcher, &PaWatcherThread::sinkInputAppeared, this,
                [this]()
                {
                    if (m_stopping) return;
                    QTimer::singleShot(100, this, &PaWorker::doApplyPending);
                });
        connect(m_watcher, &PaWatcherThread::sinkInputAppeared, this,
                [this]()
                {
                    if (!m_stopping && m_refreshTimer) m_refreshTimer->start();
                });
        connect(m_watcher, &PaWatcherThread::sinkInputRemoved, this,
                [this]()
                {
                    if (!m_stopping && m_refreshTimer) m_refreshTimer->start();
                });
        connect(m_watcher, &PaWatcherThread::sinkChanged, this,
                [this]()
                {
                    if (!m_stopping && m_sinkRefreshTimer) m_sinkRefreshTimer->start();
                });
        connect(m_watcher, &PaWatcherThread::connectionReady, this,
                [this]()
                {
                    if (!m_stopping && m_refreshTimer) m_refreshTimer->start();
                    if (!m_stopping && m_sinkRefreshTimer) m_sinkRefreshTimer->start();
                });

        m_watcher->start();
    }

    bool waitForOperation(pa_operation* op, const char* what, int timeoutMs = 1500)
    {
        if (!op)
        {
            qWarning() << "[PaWorker]" << what << "did not create a PA operation";
            return false;
        }

        QElapsedTimer timer;
        timer.start();
        while (!m_stopping && pa_operation_get_state(op) == PA_OPERATION_RUNNING)
        {
            if (timer.elapsed() >= timeoutMs) break;
            pa_threaded_mainloop_unlock(m_mainloop);
            QThread::msleep(20);
            pa_threaded_mainloop_lock(m_mainloop);
        }

        bool ok = pa_operation_get_state(op) == PA_OPERATION_DONE;
        if (!ok && !m_stopping)
        {
            qWarning() << "[PaWorker]" << what << "timed out or was cancelled after"
                       << timer.elapsed() << "ms";
        }
        if (!ok && op)
        {
            pa_operation_cancel(op);
        }
        if (op) pa_operation_unref(op);
        return ok;
    }

    // ── Sink input helpers ────────────────────────────────────────────────────
    struct SinkInputInfo
    {
        uint32_t index;
        QString name, binary, mediaName, clientId;
        double volume;
        bool muted;

        bool matches(const QString& appName) const
        {
            return name.compare(appName, Qt::CaseInsensitive) == 0 ||
                   binary.compare(appName, Qt::CaseInsensitive) == 0 ||
                   mediaName.compare(appName, Qt::CaseInsensitive) == 0;
        }

        QString displayName() const
        {
            if (isGenericAppName(name) && !binary.isEmpty() &&
                binary.compare(name, Qt::CaseInsensitive) != 0)
                return binary;
            return name;
        }

        QString targetName() const
        {
            if (!binary.isEmpty()) return binary;
            return name;
        }
    };

    struct SinkInputListCbData
    {
        PaWorker* self;
        QList<SinkInputInfo>* result;
    };
    static void sinkInputListCallback(pa_context*, const pa_sink_input_info* info, int eol,
                                      void* ud)
    {
        auto* d = static_cast<SinkInputListCbData*>(ud);
        if (eol || !info) return;

        SinkInputInfo si;
        si.index = info->index;
        const char* appName = pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_NAME);
        const char* mediaName = pa_proplist_gets(info->proplist, PA_PROP_MEDIA_NAME);
        const char* processBinary =
            pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_PROCESS_BINARY);
        const char* clientId = pa_proplist_gets(info->proplist, "client.id");

        si.name = QString::fromUtf8(appName ? appName : (mediaName ? mediaName : "Unknown"));
        si.binary = QString::fromUtf8(processBinary ? processBinary : "");
        si.mediaName = QString::fromUtf8(mediaName ? mediaName : "");
        si.clientId = QString::fromUtf8(clientId ? clientId : "");
        si.volume = static_cast<double>(pa_cvolume_avg(&info->volume)) / PA_VOLUME_NORM;
        si.muted = info->mute != 0;
        d->result->append(si);
    }

    QList<SinkInputInfo> getSinkInputs() // caller must hold mainloop lock
    {
        QList<SinkInputInfo> result;
        if (!m_ctx || pa_context_get_state(m_ctx) != PA_CONTEXT_READY) return result;

        SinkInputListCbData d{this, &result};
        waitForOperation(pa_context_get_sink_input_info_list(m_ctx, sinkInputListCallback, &d),
                         "list sink inputs");
        return result;
    }

    // ── Stream restore ────────────────────────────────────────────────────────
    struct StreamRestoreCbData
    {
        PaWorker* self;
        QString target;
        double delta;
        double targetVolume;
        bool absoluteVolume;
        bool isMute;
        bool targetMute;
        std::optional<double>* outVol;
        std::optional<std::pair<bool, double>>* outMute;
        double minVol = 0.0;
        double maxVol = 1.0;
    };

    static void streamRestoreReadCallback(pa_context* ctx, const pa_ext_stream_restore_info* info,
                                          int eol, void* ud)
    {
        auto* d = static_cast<StreamRestoreCbData*>(ud);
        if (eol) return;
        if (!info || QString::fromUtf8(info->name) != d->target) return;

        double vol = static_cast<double>(pa_cvolume_avg(&info->volume)) / PA_VOLUME_NORM;
        if (!d->isMute)
        {
            double newVol = d->absoluteVolume ? std::clamp(d->targetVolume, d->minVol, d->maxVol)
                                              : std::clamp(vol + d->delta, d->minVol, d->maxVol);
            pa_ext_stream_restore_info out = *info;
            pa_cvolume_set(const_cast<pa_cvolume*>(&out.volume), 2,
                           static_cast<pa_volume_t>(newVol * PA_VOLUME_NORM));
            pa_operation* op = pa_ext_stream_restore_write(ctx, PA_UPDATE_REPLACE, &out, 1, 1,
                                                           operationDoneCallback, d->self);
            if (op) pa_operation_unref(op);
            *d->outVol = newVol;
        }
        else
        {
            bool newMuted = d->absoluteVolume ? d->targetMute : !info->mute;
            pa_ext_stream_restore_info out = *info;
            out.mute = newMuted ? 1 : 0;
            pa_operation* op = pa_ext_stream_restore_write(ctx, PA_UPDATE_REPLACE, &out, 1, 1,
                                                           operationDoneCallback, d->self);
            if (op) pa_operation_unref(op);
            *d->outMute = std::make_pair(newMuted, vol);
        }
    }

    struct StreamRestoreQueryCbData
    {
        QString target;
        std::optional<std::pair<bool, double>>* out;
    };

    static void streamRestoreQueryCallback(pa_context*, const pa_ext_stream_restore_info* info,
                                           int eol, void* ud)
    {
        auto* d = static_cast<StreamRestoreQueryCbData*>(ud);
        if (eol) return;
        if (!info || QString::fromUtf8(info->name) != d->target) return;

        const double vol = static_cast<double>(pa_cvolume_avg(&info->volume)) / PA_VOLUME_NORM;
        *d->out = std::make_pair(info->mute != 0, vol);
    }

    QStringList streamRestoreAppCandidates(const QString& app) const
    {
        QStringList candidates;
        auto add = [&](const QString& candidate)
        {
            if (candidate.trimmed().isEmpty()) return;
            for (const QString& existing : std::as_const(candidates))
            {
                if (existing.compare(candidate, Qt::CaseInsensitive) == 0) return;
            }
            candidates.append(candidate);
        };

        add(app);
        for (const PipeWireClient& client : ::listPipeWireClients())
        {
            if (client.binary.compare(app, Qt::CaseInsensitive) == 0 ||
                client.name.compare(app, Qt::CaseInsensitive) == 0)
            {
                add(client.binary);
                add(client.name);
            }
        }
        return candidates;
    }

    std::optional<double> streamRestoreChangeVolume(const QString& app, double delta,
                                                    double minVol = 0.0, double maxVol = 1.0)
    {
        if (!contextReady()) return std::nullopt;
        for (const QString& candidate : streamRestoreAppCandidates(app))
        {
            std::optional<double> result;
            StreamRestoreCbData d{
                this,    QStringLiteral("sink-input-by-application-name:") + candidate,
                delta,   0.0,
                false,   false,
                false,   &result,
                nullptr, minVol,
                maxVol};
            pa_threaded_mainloop_lock(m_mainloop);
            waitForOperation(pa_ext_stream_restore_read(m_ctx, streamRestoreReadCallback, &d),
                             "read stream restore volume");
            pa_threaded_mainloop_unlock(m_mainloop);
            if (result) return result;
        }
        return std::nullopt;
    }

    std::optional<std::pair<bool, double>> streamRestoreQueryVolume(const QString& app)
    {
        if (!contextReady()) return std::nullopt;
        for (const QString& candidate : streamRestoreAppCandidates(app))
        {
            std::optional<std::pair<bool, double>> result;
            StreamRestoreQueryCbData d{
                QStringLiteral("sink-input-by-application-name:") + candidate, &result};
            pa_threaded_mainloop_lock(m_mainloop);
            waitForOperation(pa_ext_stream_restore_read(m_ctx, streamRestoreQueryCallback, &d),
                             "read stream restore query volume");
            pa_threaded_mainloop_unlock(m_mainloop);
            if (result) return result;
        }
        return std::nullopt;
    }

    std::optional<double> streamRestoreSetVolume(const QString& app, double targetVolume,
                                                 double minVol = 0.0, double maxVol = 1.0)
    {
        if (!contextReady()) return std::nullopt;
        targetVolume =
            std::clamp(targetVolume, std::clamp(minVol, 0.0, 1.0), std::clamp(maxVol, 0.0, 1.0));
        for (const QString& candidate : streamRestoreAppCandidates(app))
        {
            std::optional<double> result;
            StreamRestoreCbData d{
                this,    QStringLiteral("sink-input-by-application-name:") + candidate,
                0.0,     targetVolume,
                true,    false,
                false,   &result,
                nullptr, minVol,
                maxVol};
            pa_threaded_mainloop_lock(m_mainloop);
            waitForOperation(pa_ext_stream_restore_read(m_ctx, streamRestoreReadCallback, &d),
                             "read stream restore absolute volume");
            pa_threaded_mainloop_unlock(m_mainloop);
            if (result) return result;
        }
        return std::nullopt;
    }

    std::optional<std::pair<bool, double>> streamRestoreToggleMute(const QString& app)
    {
        if (!contextReady()) return std::nullopt;
        const bool targetMuted = false;
        for (const QString& candidate : streamRestoreAppCandidates(app))
        {
            std::optional<std::pair<bool, double>> result;
            StreamRestoreCbData d{
                this,        QStringLiteral("sink-input-by-application-name:") + candidate,
                0.0,         0.0,
                false,       true,
                targetMuted, nullptr,
                &result};
            pa_threaded_mainloop_lock(m_mainloop);
            waitForOperation(pa_ext_stream_restore_read(m_ctx, streamRestoreReadCallback, &d),
                             "read stream restore mute");
            pa_threaded_mainloop_unlock(m_mainloop);
            if (result) return result;
        }
        return std::nullopt;
    }

    std::optional<std::pair<bool, double>> streamRestoreSetMute(const QString& app,
                                                                bool targetMuted)
    {
        if (!contextReady()) return std::nullopt;
        for (const QString& candidate : streamRestoreAppCandidates(app))
        {
            std::optional<std::pair<bool, double>> result;
            StreamRestoreCbData d{
                this,        QStringLiteral("sink-input-by-application-name:") + candidate,
                0.0,         0.0,
                true,        true,
                targetMuted, nullptr,
                &result};
            pa_threaded_mainloop_lock(m_mainloop);
            waitForOperation(pa_ext_stream_restore_read(m_ctx, streamRestoreReadCallback, &d),
                             "read stream restore absolute mute");
            pa_threaded_mainloop_unlock(m_mainloop);
            if (result) return result;
        }
        return std::nullopt;
    }

    // ── Sink enumeration callbacks ────────────────────────────────────────────
    struct ServerInfoCbData
    {
        QString* defaultSinkName;
    };
    static void serverInfoCallback(pa_context*, const pa_server_info* info, void* ud)
    {
        auto* d = static_cast<ServerInfoCbData*>(ud);
        if (!info || !d || !d->defaultSinkName) return;
        if (info->default_sink_name)
            *d->defaultSinkName = QString::fromUtf8(info->default_sink_name);
    }

    struct SinkInfoListCbData
    {
        QList<SinkInfo>* result;
    };
    static void sinkInfoListCallback(pa_context*, const pa_sink_info* info, int eol, void* ud)
    {
        auto* d = static_cast<SinkInfoListCbData*>(ud);
        if (eol || !info || !d) return;
        SinkInfo s;
        s.name = QString::fromUtf8(info->name ? info->name : "");
        s.description = QString::fromUtf8(info->description ? info->description : "");
        s.index = info->index;
        d->result->append(s);
    }

    // Persist routing target in PA's stream-restore database so the chosen
    // sink survives restarts of the target app. Reads existing entries via the
    // streamRestoreAppCandidates set and rewrites them with device=sinkName.
    // Returns true when at least one entry matched and was rewritten — callers
    // (doApplyPending) use this to decide whether to keep pending parked for
    // a future retry when PA's SR module hasn't yet materialised the entry.
    bool streamRestoreSetDevice(const QString& app, const QString& sinkName)
    {
        if (!contextReady() || sinkName.isEmpty()) return false;
        const QByteArray sinkBytes = sinkName.toUtf8();

        struct DeviceCbData
        {
            PaWorker* self;
            QString target;
            const char* device;
            bool wrote = false;
        };

        auto cb = [](pa_context* ctx, const pa_ext_stream_restore_info* info, int eol, void* ud)
        {
            auto* d = static_cast<DeviceCbData*>(ud);
            if (eol || !info || !d) return;
            if (QString::fromUtf8(info->name) != d->target) return;
            pa_ext_stream_restore_info out = *info;
            out.device = d->device;
            pa_operation* op = pa_ext_stream_restore_write(ctx, PA_UPDATE_REPLACE, &out, 1, 1,
                                                           operationDoneCallback, d->self);
            if (op)
            {
                d->wrote = true;
                pa_operation_unref(op);
            }
        };

        bool anyWritten = false;
        for (const QString& candidate : streamRestoreAppCandidates(app))
        {
            DeviceCbData d{this, QStringLiteral("sink-input-by-application-name:") + candidate,
                           sinkBytes.constData(), false};
            pa_threaded_mainloop_lock(m_mainloop);
            waitForOperation(pa_ext_stream_restore_read(m_ctx, cb, &d),
                             "stream restore set device");
            pa_threaded_mainloop_unlock(m_mainloop);
            if (d.wrote) anyWritten = true;
        }
        return anyWritten;
    }

    void removePending(const QString& app)
    {
        QMutexLocker lk(&m_pendingMutex);
        m_pendingVolumes.remove(app);
        m_pendingMutes.remove(app);
    }

    void clearDuckingState()
    {
        m_duckingActive = false;
        m_duckingKeepApp.clear();
        m_duckingSnapshot.clear();
    }
};

// ─── VolumeController ─────────────────────────────────────────────────────────
VolumeController::VolumeController(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<QList<SinkInfo>>("QList<SinkInfo>");

    m_paThread = new QThread(this);
    m_worker = new PaWorker(); // no parent — will live in m_paThread
    m_worker->moveToThread(m_paThread);

    // Forward worker signals to our own signals (runs in main thread via queued)
    connect(m_worker, &PaWorker::volumeChanged, this, &VolumeController::volumeChanged);
    connect(m_worker, &PaWorker::appsReady, this,
            [this](const QList<AudioApp>& apps)
            {
                m_listCache = apps;
                emit appsReady(apps);
            });
    connect(m_worker, &PaWorker::sinksReady, this,
            [this](const QList<SinkInfo>& sinks)
            {
                m_sinkCache = sinks;
                emit sinksReady(sinks);
            });
    connect(m_worker, &PaWorker::sinkChanged, this, &VolumeController::sinkChanged);

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
    if (m_paThread && !m_closing)
    {
        m_closing = true;
        disconnect(m_worker, &PaWorker::volumeChanged, this, &VolumeController::volumeChanged);
        disconnect(m_worker, nullptr, this, nullptr);
        m_worker->requestStop();

        QEventLoop cleanupLoop;
        bool cleanupDone = false;
        connect(m_worker, &PaWorker::cleanupFinished, &cleanupLoop,
                [&]()
                {
                    cleanupDone = true;
                    cleanupLoop.quit();
                });
        QTimer::singleShot(2500, &cleanupLoop, &QEventLoop::quit);

        QMetaObject::invokeMethod(m_worker, "cleanup", Qt::QueuedConnection);
        cleanupLoop.exec();
        if (!cleanupDone) qWarning() << "[VolumeController] PA cleanup did not finish in 2.5s";

        m_paThread->quit();
        if (!m_paThread->wait(2000))
        {
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

QList<SinkInfo> VolumeController::listSinks(bool forceRefresh)
{
    if (m_closing || !m_worker) return m_sinkCache;

    QMetaObject::invokeMethod(m_worker, "doListSinks", Qt::QueuedConnection,
                              Q_ARG(bool, forceRefresh));
    return m_sinkCache;
}

void VolumeController::setAppSink(const QString& appName, const QString& sinkName)
{
    if (m_closing || !m_worker) return;
    if (appName.isEmpty() || sinkName.isEmpty()) return;

    QMetaObject::invokeMethod(m_worker, "doSetAppSink", Qt::QueuedConnection,
                              Q_ARG(QString, appName), Q_ARG(QString, sinkName));
}

void VolumeController::clearAppSinkOverride(const QString& appName)
{
    if (m_closing || !m_worker || appName.isEmpty()) return;

    QMetaObject::invokeMethod(m_worker, "doClearAppSinkOverride", Qt::QueuedConnection,
                              Q_ARG(QString, appName));
}

void VolumeController::changeVolume(const QString& appName, double delta, double minVol,
                                    double maxVol)
{
    if (m_closing || !m_worker) return;

    QMetaObject::invokeMethod(m_worker, "doChangeVolume", Qt::QueuedConnection,
                              Q_ARG(QString, appName), Q_ARG(double, delta), Q_ARG(double, minVol),
                              Q_ARG(double, maxVol));
}

void VolumeController::setVolume(const QString& appName, double targetVolume, double minVol,
                                 double maxVol)
{
    if (m_closing || !m_worker) return;

    QMetaObject::invokeMethod(m_worker, "setAppVolume", Qt::QueuedConnection,
                              Q_ARG(QString, appName), Q_ARG(double, targetVolume),
                              Q_ARG(double, minVol), Q_ARG(double, maxVol));
}

void VolumeController::toggleMute(const QString& appName)
{
    if (m_closing || !m_worker) return;

    QMetaObject::invokeMethod(m_worker, "doToggleMute", Qt::QueuedConnection,
                              Q_ARG(QString, appName));
}

void VolumeController::setMuted(const QString& appName, bool muted)
{
    if (m_closing || !m_worker) return;

    QMetaObject::invokeMethod(m_worker, "setAppMute", Qt::QueuedConnection, Q_ARG(QString, appName),
                              Q_ARG(bool, muted));
}

void VolumeController::toggleDucking(const QString& keepApp, double duckVolume)
{
    if (m_closing || !m_worker) return;

    QMetaObject::invokeMethod(m_worker, "doToggleDucking", Qt::QueuedConnection,
                              Q_ARG(QString, keepApp), Q_ARG(double, duckVolume));
}

void VolumeController::applyScene(const AudioScene& scene)
{
    if (m_closing || !m_worker) return;

    // Scenes intentionally bypass per-profile volume limits: each target is an
    // explicit mixer preset, so we use the full [0,1] range (default args of
    // setVolume). Targets with neither volume nor mute are already dropped by
    // Config sanitization, but we re-check defensively.
    for (const SceneTarget& target : scene.targets)
    {
        if (target.match.trimmed().isEmpty()) continue;
        if (target.volume) setVolume(target.match, std::clamp(*target.volume, 0, 100) / 100.0);
        if (target.muted) setMuted(target.match, *target.muted);
    }
}

void VolumeController::queryVolume(const QString& appName)
{
    if (m_closing || !m_worker) return;

    QMetaObject::invokeMethod(m_worker, "doQueryVolume", Qt::QueuedConnection,
                              Q_ARG(QString, appName));
}

#include "volumecontroller.moc"
