#include "pabackend.h"
#include "appmatcher.h"
#include "pawatcherthread.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QObject>
#include <QThread>
#include <algorithm>
#include <utility>

#include <pulse/context.h>
#include <pulse/ext-stream-restore.h>
#include <pulse/introspect.h>
#include <pulse/thread-mainloop.h>
#include <pulse/volume.h>

namespace
{

class LibPulseBackend final : public IPaBackend
{
  public:
    ~LibPulseBackend() override
    {
        stopWatcher();
        pwClose();
        disconnect();
    }

    bool connect(const std::atomic<bool>& stopping, std::function<void()> onContextLost) override
    {
        disconnect();
        m_stopping = &stopping;
        m_onContextLost = std::move(onContextLost);

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
            disconnect();
            return false;
        }

        QElapsedTimer timer;
        timer.start();
        while (!isStopping() && timer.elapsed() < pabackend::PA_CONNECT_TIMEOUT_MS)
        {
            pa_context_state_t st = pa_context_get_state(m_ctx);
            if (st == PA_CONTEXT_READY) break;
            if (!PA_CONTEXT_IS_GOOD(st))
            {
                pa_threaded_mainloop_unlock(m_mainloop);
                disconnect();
                return false;
            }
            pa_threaded_mainloop_unlock(m_mainloop);
            QThread::msleep(20);
            pa_threaded_mainloop_lock(m_mainloop);
        }
        pa_threaded_mainloop_unlock(m_mainloop);
        if (!isReady())
        {
            disconnect();
            return false;
        }
        detectPulseServerBackend();
        return true;
    }

    void disconnect() override
    {
        m_isPipeWirePulse = false;
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

    bool isReady() override
    {
        if (!m_mainloop || !m_ctx) return false;
        pa_threaded_mainloop_lock(m_mainloop);
        const bool ready = pa_context_get_state(m_ctx) == PA_CONTEXT_READY;
        pa_threaded_mainloop_unlock(m_mainloop);
        return ready;
    }

    bool isPipeWirePulse() const override
    {
        return m_isPipeWirePulse;
    }

    void startWatcher(QObject* receiver, std::function<void()> onSinkInputAppeared,
                      std::function<void()> onSinkInputRemoved, std::function<void()> onSinkChanged,
                      std::function<void()> onConnectionReady) override
    {
        if (m_watcher) return;

        m_watcher = new PaWatcherThread(); // no parent — lives in PA thread
        m_watcher->moveToThread(QThread::currentThread());

        QObject::connect(m_watcher, &PaWatcherThread::sinkInputAppeared, receiver,
                         std::move(onSinkInputAppeared));
        QObject::connect(m_watcher, &PaWatcherThread::sinkInputRemoved, receiver,
                         std::move(onSinkInputRemoved));
        QObject::connect(m_watcher, &PaWatcherThread::sinkChanged, receiver,
                         std::move(onSinkChanged));
        QObject::connect(m_watcher, &PaWatcherThread::connectionReady, receiver,
                         std::move(onConnectionReady));

        m_watcher->start();
    }

    void stopWatcher() override
    {
        if (!m_watcher) return;
        QObject::disconnect(m_watcher, nullptr, nullptr, nullptr);
        m_watcher->stop();
        delete m_watcher;
        m_watcher = nullptr;
    }

    QList<pabackend::SinkInputInfo> listSinkInputs() override
    {
        QList<pabackend::SinkInputInfo> result;
        if (!isReady()) return result;

        pa_threaded_mainloop_lock(m_mainloop);
        if (m_ctx && pa_context_get_state(m_ctx) == PA_CONTEXT_READY)
        {
            pabackend::SinkInputListCbData d{&result};
            waitForOperation(
                pa_context_get_sink_input_info_list(m_ctx, pabackend::sinkInputListCallback, &d),
                "list sink inputs");
        }
        pa_threaded_mainloop_unlock(m_mainloop);
        return result;
    }

    bool setSinkInputVolume(const pabackend::SinkInputInfo& input, double targetVolume) override
    {
        if (!isReady()) return false;
        pa_cvolume volume;
        const uint8_t channels = std::max<uint8_t>(1, input.channelVolume.channels);
        pa_cvolume_set(&volume, channels, static_cast<pa_volume_t>(targetVolume * PA_VOLUME_NORM));
        pa_threaded_mainloop_lock(m_mainloop);
        const bool ok = waitForOperation(
            pa_context_set_sink_input_volume(m_ctx, input.index, &volume,
                                             pabackend::operationDoneCallback, this),
            "set sink input visible volume");
        pa_threaded_mainloop_unlock(m_mainloop);
        return ok;
    }

    bool setSinkInputChannelVolumes(uint32_t sinkInputIndex, const pa_cvolume& channelVolume,
                                    const char* operationName) override
    {
        if (!isReady()) return false;
        pa_cvolume volume = channelVolume;
        pa_threaded_mainloop_lock(m_mainloop);
        const bool ok = waitForOperation(
            pa_context_set_sink_input_volume(m_ctx, sinkInputIndex, &volume,
                                             pabackend::operationDoneCallback, this),
            operationName);
        pa_threaded_mainloop_unlock(m_mainloop);
        return ok;
    }

    bool setSinkInputMute(uint32_t sinkInputIndex, bool muted, const char* operationName) override
    {
        if (!isReady()) return false;
        pa_threaded_mainloop_lock(m_mainloop);
        const bool ok =
            waitForOperation(pa_context_set_sink_input_mute(m_ctx, sinkInputIndex, muted ? 1 : 0,
                                                            pabackend::operationDoneCallback, this),
                             operationName);
        pa_threaded_mainloop_unlock(m_mainloop);
        return ok;
    }

    bool moveSinkInputByName(uint32_t sinkInputIndex, const QString& sinkName,
                             const char* operationName, bool& rejectedByServer) override
    {
        rejectedByServer = false;
        if (!isReady()) return false;
        const QByteArray sinkBytes = sinkName.toUtf8();
        pabackend::OperationOutcome outcome;
        pa_threaded_mainloop_lock(m_mainloop);
        pa_operation* op =
            pa_context_move_sink_input_by_name(m_ctx, sinkInputIndex, sinkBytes.constData(),
                                               pabackend::operationOutcomeCallback, &outcome);
        const bool completed = waitForOperation(op, operationName);
        pa_threaded_mainloop_unlock(m_mainloop);
        if (completed && !outcome.success) rejectedByServer = true;
        return completed && outcome.success;
    }

    bool listSinks(QList<SinkInfo>& sinks, QString& defaultSinkName) override
    {
        if (!isReady()) return false;

        pa_threaded_mainloop_lock(m_mainloop);

        pabackend::ServerInfoCbData srv{&defaultSinkName};
        waitForOperation(pa_context_get_server_info(m_ctx, pabackend::serverInfoCallback, &srv),
                         "get server info");

        pabackend::SinkInfoListCbData sd{&sinks};
        waitForOperation(pa_context_get_sink_info_list(m_ctx, pabackend::sinkInfoListCallback, &sd),
                         "list sinks");

        pa_threaded_mainloop_unlock(m_mainloop);
        return true;
    }

    QStringList enumerateStreamRestoreAliases(const QString& appLower) override
    {
        QStringList found;
        if (!isReady() || appLower.isEmpty()) return found;

        struct CollectData
        {
            QString appLower;
            QStringList* candidates;
        };

        auto collectCb = [](pa_context*, const pa_ext_stream_restore_info* info, int eol, void* ud)
        {
            auto* d = static_cast<CollectData*>(ud);
            if (eol || !info) return;
            const QString fullName = QString::fromUtf8(info->name);
            static const QString prefix = QStringLiteral("sink-input-by-application-name:");
            if (!fullName.startsWith(prefix)) return;
            const QString entryApp = fullName.mid(prefix.size());
            if (!appIdMatches(entryApp, d->appLower)) return;
            if (entryApp.trimmed().isEmpty()) return;
            for (const QString& existing : std::as_const(*d->candidates))
            {
                if (existing.compare(entryApp, Qt::CaseInsensitive) == 0) return;
            }
            d->candidates->append(entryApp);
        };

        CollectData collect{appLower, &found};
        pa_threaded_mainloop_lock(m_mainloop);
        waitForOperation(pa_ext_stream_restore_read(m_ctx, collectCb, &collect),
                         "stream restore enumerate aliases");
        pa_threaded_mainloop_unlock(m_mainloop);
        return found;
    }

    std::optional<double> streamRestoreChangeVolume(const QStringList& candidates, double delta,
                                                    double minVol, double maxVol) override
    {
        if (!isReady()) return std::nullopt;
        for (const QString& candidate : candidates)
        {
            std::optional<double> result;
            pabackend::StreamRestoreCbData d{QStringLiteral("sink-input-by-application-name:") +
                                                 candidate,
                                             delta,
                                             0.0,
                                             false,
                                             false,
                                             false,
                                             &result,
                                             nullptr,
                                             minVol,
                                             maxVol};
            pa_threaded_mainloop_lock(m_mainloop);
            waitForOperation(
                pa_ext_stream_restore_read(m_ctx, pabackend::streamRestoreReadCallback, &d),
                "read stream restore volume");
            pa_threaded_mainloop_unlock(m_mainloop);
            if (result) return result;
        }
        return std::nullopt;
    }

    std::optional<double> streamRestoreSetVolume(const QStringList& candidates, double targetVolume,
                                                 double minVol, double maxVol) override
    {
        if (!isReady()) return std::nullopt;
        targetVolume =
            std::clamp(targetVolume, std::clamp(minVol, 0.0, 1.0), std::clamp(maxVol, 0.0, 1.0));
        for (const QString& candidate : candidates)
        {
            std::optional<double> result;
            pabackend::StreamRestoreCbData d{QStringLiteral("sink-input-by-application-name:") +
                                                 candidate,
                                             0.0,
                                             targetVolume,
                                             true,
                                             false,
                                             false,
                                             &result,
                                             nullptr,
                                             minVol,
                                             maxVol};
            pa_threaded_mainloop_lock(m_mainloop);
            waitForOperation(
                pa_ext_stream_restore_read(m_ctx, pabackend::streamRestoreReadCallback, &d),
                "read stream restore absolute volume");
            pa_threaded_mainloop_unlock(m_mainloop);
            if (result) return result;
        }
        return std::nullopt;
    }

    std::optional<pabackend::StreamRestoreState>
    streamRestoreQueryState(const QStringList& candidates) override
    {
        if (!isReady()) return std::nullopt;
        for (const QString& candidate : candidates)
        {
            std::optional<pabackend::StreamRestoreState> result;
            pabackend::StreamRestoreQueryCbData d{
                QStringLiteral("sink-input-by-application-name:") + candidate, &result};
            pa_threaded_mainloop_lock(m_mainloop);
            waitForOperation(
                pa_ext_stream_restore_read(m_ctx, pabackend::streamRestoreQueryCallback, &d),
                "read stream restore query volume");
            pa_threaded_mainloop_unlock(m_mainloop);
            if (result) return result;
        }
        return std::nullopt;
    }

    bool streamRestoreSetChannelVolumes(const QStringList& candidates,
                                        const pa_cvolume& channelVolume) override
    {
        if (!isReady() || !pa_cvolume_valid(&channelVolume)) return false;

        struct ExactVolumeData
        {
            QString target;
            pa_cvolume volume;
            bool wrote = false;
        };
        auto callback =
            [](pa_context* ctx, const pa_ext_stream_restore_info* info, int eol, void* ud)
        {
            auto* data = static_cast<ExactVolumeData*>(ud);
            if (eol || !info || QString::fromUtf8(info->name) != data->target) return;
            pa_ext_stream_restore_info out = *info;
            out.volume = data->volume;
            pa_operation* op = pa_ext_stream_restore_write(
                ctx, PA_UPDATE_REPLACE, &out, 1, 1, pabackend::operationDoneCallback, nullptr);
            if (op)
            {
                data->wrote = true;
                pa_operation_unref(op);
            }
        };

        for (const QString& candidate : candidates)
        {
            ExactVolumeData data{QStringLiteral("sink-input-by-application-name:") + candidate,
                                 channelVolume, false};
            pa_threaded_mainloop_lock(m_mainloop);
            waitForOperation(pa_ext_stream_restore_read(m_ctx, callback, &data),
                             "restore exact stream restore channel volumes");
            pa_threaded_mainloop_unlock(m_mainloop);
            if (data.wrote) return true;
        }
        return false;
    }

    std::optional<std::pair<bool, double>>
    streamRestoreToggleMute(const QStringList& candidates) override
    {
        if (!isReady()) return std::nullopt;
        const bool targetMuted = false;
        for (const QString& candidate : candidates)
        {
            std::optional<std::pair<bool, double>> result;
            pabackend::StreamRestoreCbData d{QStringLiteral("sink-input-by-application-name:") +
                                                 candidate,
                                             0.0,
                                             0.0,
                                             false,
                                             true,
                                             targetMuted,
                                             nullptr,
                                             &result};
            pa_threaded_mainloop_lock(m_mainloop);
            waitForOperation(
                pa_ext_stream_restore_read(m_ctx, pabackend::streamRestoreReadCallback, &d),
                "read stream restore mute");
            pa_threaded_mainloop_unlock(m_mainloop);
            if (result) return result;
        }
        return std::nullopt;
    }

    std::optional<std::pair<bool, double>> streamRestoreSetMute(const QStringList& candidates,
                                                                bool targetMuted) override
    {
        if (!isReady()) return std::nullopt;
        for (const QString& candidate : candidates)
        {
            std::optional<std::pair<bool, double>> result;
            pabackend::StreamRestoreCbData d{QStringLiteral("sink-input-by-application-name:") +
                                                 candidate,
                                             0.0,
                                             0.0,
                                             true,
                                             true,
                                             targetMuted,
                                             nullptr,
                                             &result};
            pa_threaded_mainloop_lock(m_mainloop);
            waitForOperation(
                pa_ext_stream_restore_read(m_ctx, pabackend::streamRestoreReadCallback, &d),
                "read stream restore absolute mute");
            pa_threaded_mainloop_unlock(m_mainloop);
            if (result) return result;
        }
        return std::nullopt;
    }

    bool streamRestoreSetDevice(const QStringList& candidates, const QString& sinkName) override
    {
        if (!isReady() || sinkName.isEmpty()) return false;
        const QByteArray sinkBytes = sinkName.toUtf8();

        struct DeviceCbData
        {
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
            pa_operation* op = pa_ext_stream_restore_write(
                ctx, PA_UPDATE_REPLACE, &out, 1, 1, pabackend::operationDoneCallback, nullptr);
            if (op)
            {
                d->wrote = true;
                pa_operation_unref(op);
            }
        };

        bool anyWritten = false;
        for (const QString& candidate : candidates)
        {
            DeviceCbData d{QStringLiteral("sink-input-by-application-name:") + candidate,
                           sinkBytes.constData(), false};
            pa_threaded_mainloop_lock(m_mainloop);
            waitForOperation(pa_ext_stream_restore_read(m_ctx, cb, &d),
                             "stream restore set device");
            pa_threaded_mainloop_unlock(m_mainloop);
            if (d.wrote) anyWritten = true;
        }
        return anyWritten;
    }

    bool streamRestoreClearDevice(const QStringList& candidates) override
    {
        if (!isReady()) return false;

        struct ClearCbData
        {
            QString target;
            bool wrote = false;
        };

        auto cb = [](pa_context* ctx, const pa_ext_stream_restore_info* info, int eol, void* ud)
        {
            auto* d = static_cast<ClearCbData*>(ud);
            if (eol || !info || !d) return;
            if (QString::fromUtf8(info->name) != d->target) return;
            pa_ext_stream_restore_info out = *info;
            out.device = nullptr;
            pa_operation* op = pa_ext_stream_restore_write(
                ctx, PA_UPDATE_REPLACE, &out, 1, 1, pabackend::operationDoneCallback, nullptr);
            if (op)
            {
                d->wrote = true;
                pa_operation_unref(op);
            }
        };

        bool anyWritten = false;
        for (const QString& candidate : candidates)
        {
            ClearCbData d{QStringLiteral("sink-input-by-application-name:") + candidate, false};
            pa_threaded_mainloop_lock(m_mainloop);
            waitForOperation(pa_ext_stream_restore_read(m_ctx, cb, &d),
                             "stream restore clear device");
            pa_threaded_mainloop_unlock(m_mainloop);
            if (d.wrote) anyWritten = true;
        }
        return anyWritten;
    }

    PipeWireSnapshot pwInspect(const QSet<QString>& systemBinaries,
                               const QSet<QString>& skipAppNames) override
    {
        return m_pipeWire.inspect(systemBinaries, skipAppNames);
    }

    QList<PipeWireClient> pwListClients(const QSet<QString>& systemBinaries,
                                        const QSet<QString>& skipAppNames) override
    {
        return m_pipeWire.listClients(systemBinaries, skipAppNames);
    }

    QList<PipeWireNode> pwFindNodesForApp(const QString& appName,
                                          const QStringList& matchCandidates) override
    {
        return m_pipeWire.findNodesForApp(appName, matchCandidates);
    }

    std::optional<PipeWireNode> pwFindNodeForApp(const QString& appName,
                                                 const QStringList& matchCandidates) override
    {
        return m_pipeWire.findNodeForApp(appName, matchCandidates);
    }

    std::optional<PipeWireNode> pwReadNode(uint32_t nodeId) override
    {
        return m_pipeWire.readNode(nodeId);
    }

    bool pwSetVisibleVolume(uint32_t nodeId, double volume, uint32_t channelCount) override
    {
        return m_pipeWire.setVisibleVolume(nodeId, volume, channelCount);
    }

    bool pwSetVisibleChannelVolumes(uint32_t nodeId, const QList<double>& channelVolumes) override
    {
        return m_pipeWire.setVisibleChannelVolumes(nodeId, channelVolumes);
    }

    bool pwRestoreVolumeState(uint32_t nodeId, double rawVolume,
                              const QList<double>& channelVolumes) override
    {
        return m_pipeWire.restoreVolumeState(nodeId, rawVolume, channelVolumes);
    }

    bool pwSetMute(uint32_t nodeId, bool muted) override
    {
        return m_pipeWire.setMute(nodeId, muted);
    }

    void pwClose() override
    {
        m_pipeWire.close();
    }

  private:
    bool isStopping() const
    {
        return m_stopping && m_stopping->load(std::memory_order_acquire);
    }

    static void contextStateCallback(pa_context* ctx, void* ud)
    {
        auto* self = static_cast<LibPulseBackend*>(ud);
        const pa_context_state_t st = pa_context_get_state(ctx);

        if ((st == PA_CONTEXT_FAILED || st == PA_CONTEXT_TERMINATED) && !self->isStopping() &&
            self->m_onContextLost)
        {
            self->m_onContextLost();
        }
    }

    void detectPulseServerBackend()
    {
        m_isPipeWirePulse = false;
        if (!isReady()) return;
        pabackend::ServerBackendCbData data;
        pa_threaded_mainloop_lock(m_mainloop);
        waitForOperation(pa_context_get_server_info(m_ctx, pabackend::serverBackendCallback, &data),
                         "detect PulseAudio server backend");
        pa_threaded_mainloop_unlock(m_mainloop);
        m_isPipeWirePulse =
            data.serverName.contains(QStringLiteral("PipeWire"), Qt::CaseInsensitive);
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
        while (!isStopping() && pa_operation_get_state(op) == PA_OPERATION_RUNNING)
        {
            if (timer.elapsed() >= timeoutMs) break;
            pa_threaded_mainloop_unlock(m_mainloop);
            QThread::msleep(20);
            pa_threaded_mainloop_lock(m_mainloop);
        }

        bool ok = pa_operation_get_state(op) == PA_OPERATION_DONE;
        if (!ok && !isStopping())
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

    const std::atomic<bool>* m_stopping = nullptr;
    std::function<void()> m_onContextLost;
    pa_threaded_mainloop* m_mainloop = nullptr;
    pa_context* m_ctx = nullptr;
    PaWatcherThread* m_watcher = nullptr;
    bool m_isPipeWirePulse = false;
    PipeWireVolumeBackend m_pipeWire;
};

} // namespace

std::unique_ptr<IPaBackend> createLibPulseBackend()
{
    return std::make_unique<LibPulseBackend>();
}
