#pragma once
#include "pabackend_helpers.h"
#include "pwutils.h"
#include "volumecontroller.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

class QObject;

// Internal I/O seam used by PaWorker so PulseAudio / PipeWire interactions can
// be driven deterministically by unit tests without a live audio server.
class IPaBackend
{
  public:
    virtual ~IPaBackend() = default;

    // Context & watcher lifecycle
    virtual bool connect(const std::atomic<bool>& stopping,
                         std::function<void()> onContextLost) = 0;
    virtual void disconnect() = 0;
    virtual bool isReady() = 0;
    virtual bool isPipeWirePulse() const = 0;
    virtual void startWatcher(QObject* receiver, std::function<void()> onSinkInputAppeared,
                              std::function<void()> onSinkInputRemoved,
                              std::function<void()> onSinkChanged,
                              std::function<void()> onConnectionReady) = 0;
    virtual void stopWatcher() = 0;

    // PulseAudio sink inputs & sinks
    virtual QList<pabackend::SinkInputInfo> listSinkInputs() = 0;
    virtual bool setSinkInputVolume(const pabackend::SinkInputInfo& input, double targetVolume) = 0;
    virtual bool setSinkInputChannelVolumes(uint32_t sinkInputIndex,
                                            const pa_cvolume& channelVolume,
                                            const char* operationName) = 0;
    virtual bool setSinkInputMute(uint32_t sinkInputIndex, bool muted,
                                  const char* operationName) = 0;
    virtual bool moveSinkInputByName(uint32_t sinkInputIndex, const QString& sinkName,
                                     const char* operationName, bool& rejectedByServer) = 0;
    virtual bool listSinks(QList<SinkInfo>& sinks, QString& defaultSinkName) = 0;

    // PulseAudio stream-restore database
    virtual QStringList enumerateStreamRestoreAliases(const QString& appLower) = 0;
    virtual std::optional<double> streamRestoreChangeVolume(const QStringList& candidates,
                                                            double delta, double minVol,
                                                            double maxVol) = 0;
    virtual std::optional<double> streamRestoreSetVolume(const QStringList& candidates,
                                                         double targetVolume, double minVol,
                                                         double maxVol) = 0;
    virtual std::optional<pabackend::StreamRestoreState>
    streamRestoreQueryState(const QStringList& candidates) = 0;
    virtual bool streamRestoreSetChannelVolumes(const QStringList& candidates,
                                                const pa_cvolume& channelVolume) = 0;
    virtual std::optional<std::pair<bool, double>>
    streamRestoreToggleMute(const QStringList& candidates) = 0;
    virtual std::optional<std::pair<bool, double>>
    streamRestoreSetMute(const QStringList& candidates, bool targetMuted) = 0;
    virtual bool streamRestoreSetDevice(const QStringList& candidates, const QString& sinkName) = 0;
    virtual bool streamRestoreClearDevice(const QStringList& candidates) = 0;

    // PipeWire backend
    virtual PipeWireSnapshot pwInspect(const QSet<QString>& systemBinaries,
                                       const QSet<QString>& skipAppNames) = 0;
    virtual QList<PipeWireClient> pwListClients(const QSet<QString>& systemBinaries,
                                                const QSet<QString>& skipAppNames) = 0;
    virtual QList<PipeWireNode> pwFindNodesForApp(const QString& appName,
                                                  const QStringList& matchCandidates) = 0;
    virtual std::optional<PipeWireNode> pwFindNodeForApp(const QString& appName,
                                                         const QStringList& matchCandidates) = 0;
    virtual std::optional<PipeWireNode> pwReadNode(uint32_t nodeId) = 0;
    virtual bool pwSetVisibleVolume(uint32_t nodeId, double volume, uint32_t channelCount) = 0;
    virtual bool pwSetVisibleChannelVolumes(uint32_t nodeId,
                                            const QList<double>& channelVolumes) = 0;
    virtual bool pwRestoreVolumeState(uint32_t nodeId, double rawVolume,
                                      const QList<double>& channelVolumes) = 0;
    virtual bool pwSetMute(uint32_t nodeId, bool muted) = 0;
    virtual void pwClose() = 0;
};

// Creates the production libpulse + libpipewire backend.
std::unique_ptr<IPaBackend> createLibPulseBackend();
