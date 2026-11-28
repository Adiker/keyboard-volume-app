#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <memory>

#include "config.h"
#include "pabackend.h"
#include "paworker.h"

namespace
{

pabackend::SinkInputInfo makeSinkInput(uint32_t index, const QString& name, const QString& binary,
                                       double vol, bool muted = false,
                                       std::optional<uint32_t> pwNodeId = std::nullopt)
{
    pabackend::SinkInputInfo si;
    si.index = index;
    si.name = name;
    si.binary = binary;
    si.volume = vol;
    si.muted = muted;
    si.pipeWireNodeId = pwNodeId;
    pa_cvolume_set(&si.channelVolume, 2, static_cast<pa_volume_t>(vol * PA_VOLUME_NORM));
    return si;
}

class FakePaBackend final : public IPaBackend
{
  public:
    bool allowConnect = false;
    bool ready = false;
    bool pipeWirePulse = false;
    bool rejectSinkMove = false;
    bool streamRestoreDeviceWriteSucceeds = true;

    int connectAttempts = 0;
    int watcherStarts = 0;
    int watcherStops = 0;
    int pwCloses = 0;

    QList<pabackend::SinkInputInfo> sinkInputs;
    QList<SinkInfo> sinks;
    QString defaultSinkName;
    QMap<QString, pabackend::StreamRestoreState> streamRestoreDb;
    QList<PipeWireNode> pwNodes;
    QList<PipeWireClient> pwClients;

    QStringList operationLog;
    QMap<uint32_t, double> writtenSinkInputVolumes;
    QMap<uint32_t, QList<double>> writtenSinkInputChannels;
    QMap<uint32_t, bool> writtenSinkInputMutes;
    QMap<uint32_t, QString> movedSinkInputs;
    QMap<uint32_t, double> writtenPwVolumes;
    QMap<uint32_t, QList<double>> writtenPwChannels;
    QMap<uint32_t, bool> writtenPwMutes;
    QStringList clearedStreamRestoreApps;
    QMap<QString, QString> persistedStreamRestoreDevices;

    std::function<void()> onContextLostCb;
    std::function<void()> onSinkInputAppearedCb;
    std::function<void()> onSinkInputRemovedCb;
    std::function<void()> onSinkChangedCb;
    std::function<void()> onConnectionReadyCb;

    bool connect(const std::atomic<bool>&, std::function<void()> onContextLost) override
    {
        ++connectAttempts;
        onContextLostCb = std::move(onContextLost);
        if (!allowConnect)
        {
            ready = false;
            return false;
        }
        ready = true;
        return true;
    }

    void disconnect() override
    {
        ready = false;
    }

    bool isReady() override
    {
        return ready;
    }

    bool isPipeWirePulse() const override
    {
        return pipeWirePulse;
    }

    void startWatcher(QObject*, std::function<void()> onSinkInputAppeared,
                      std::function<void()> onSinkInputRemoved, std::function<void()> onSinkChanged,
                      std::function<void()> onConnectionReady) override
    {
        ++watcherStarts;
        onSinkInputAppearedCb = std::move(onSinkInputAppeared);
        onSinkInputRemovedCb = std::move(onSinkInputRemoved);
        onSinkChangedCb = std::move(onSinkChanged);
        onConnectionReadyCb = std::move(onConnectionReady);
    }

    void stopWatcher() override
    {
        ++watcherStops;
    }

    QList<pabackend::SinkInputInfo> listSinkInputs() override
    {
        return ready ? sinkInputs : QList<pabackend::SinkInputInfo>{};
    }

    bool setSinkInputVolume(const pabackend::SinkInputInfo& input, double targetVolume) override
    {
        if (!ready) return false;
        operationLog.append(
            QStringLiteral("setVol:%1:%2").arg(input.index).arg(targetVolume, 0, 'f', 2));
        writtenSinkInputVolumes[input.index] = targetVolume;
        for (auto& si : sinkInputs)
        {
            if (si.index == input.index)
            {
                si.volume = targetVolume;
                pa_cvolume_set(&si.channelVolume, std::max<uint8_t>(1, si.channelVolume.channels),
                               static_cast<pa_volume_t>(targetVolume * PA_VOLUME_NORM));
            }
        }
        return true;
    }

    bool setSinkInputChannelVolumes(uint32_t sinkInputIndex, const pa_cvolume& channelVolume,
                                    const char*) override
    {
        if (!ready) return false;
        const QList<double> channels = pabackend::channelVolumesFromPulse(channelVolume);
        operationLog.append(QStringLiteral("setChVol:%1").arg(sinkInputIndex));
        writtenSinkInputChannels[sinkInputIndex] = channels;
        for (auto& si : sinkInputs)
        {
            if (si.index == sinkInputIndex)
            {
                si.channelVolume = channelVolume;
                si.volume = static_cast<double>(pa_cvolume_avg(&channelVolume)) / PA_VOLUME_NORM;
            }
        }
        return true;
    }

    bool setSinkInputMute(uint32_t sinkInputIndex, bool muted, const char*) override
    {
        if (!ready) return false;
        operationLog.append(QStringLiteral("setMute:%1:%2").arg(sinkInputIndex).arg(muted ? 1 : 0));
        writtenSinkInputMutes[sinkInputIndex] = muted;
        for (auto& si : sinkInputs)
        {
            if (si.index == sinkInputIndex) si.muted = muted;
        }
        return true;
    }

    bool moveSinkInputByName(uint32_t sinkInputIndex, const QString& sinkName, const char*,
                             bool& rejectedByServer) override
    {
        rejectedByServer = false;
        if (!ready) return false;
        operationLog.append(QStringLiteral("moveSink:%1:%2").arg(sinkInputIndex).arg(sinkName));
        if (rejectSinkMove)
        {
            rejectedByServer = true;
            return false;
        }
        movedSinkInputs[sinkInputIndex] = sinkName;
        return true;
    }

    bool listSinks(QList<SinkInfo>& outSinks, QString& outDefaultSinkName) override
    {
        if (!ready) return false;
        outSinks = sinks;
        outDefaultSinkName = defaultSinkName;
        return true;
    }

    QStringList enumerateStreamRestoreAliases(const QString& appLower) override
    {
        QStringList out;
        if (!ready) return out;
        for (auto it = streamRestoreDb.constBegin(); it != streamRestoreDb.constEnd(); ++it)
        {
            if (it.key().compare(appLower, Qt::CaseInsensitive) == 0) out.append(it.key());
        }
        return out;
    }

    std::optional<double> streamRestoreChangeVolume(const QStringList& candidates, double delta,
                                                    double minVol, double maxVol) override
    {
        if (!ready) return std::nullopt;
        for (const QString& c : candidates)
        {
            auto it = streamRestoreDb.find(c);
            if (it == streamRestoreDb.end()) continue;
            const double newVol = std::clamp(it->averageVolume() + delta, minVol, maxVol);
            pa_cvolume_set(&it->volume, std::max<uint8_t>(1, it->volume.channels),
                           static_cast<pa_volume_t>(newVol * PA_VOLUME_NORM));
            operationLog.append(QStringLiteral("srChangeVol:%1:%2").arg(c).arg(newVol, 0, 'f', 2));
            return newVol;
        }
        return std::nullopt;
    }

    std::optional<double> streamRestoreSetVolume(const QStringList& candidates, double targetVolume,
                                                 double minVol, double maxVol) override
    {
        if (!ready) return std::nullopt;
        targetVolume =
            std::clamp(targetVolume, std::clamp(minVol, 0.0, 1.0), std::clamp(maxVol, 0.0, 1.0));
        for (const QString& c : candidates)
        {
            auto it = streamRestoreDb.find(c);
            if (it == streamRestoreDb.end()) continue;
            pa_cvolume_set(&it->volume, std::max<uint8_t>(1, it->volume.channels),
                           static_cast<pa_volume_t>(targetVolume * PA_VOLUME_NORM));
            operationLog.append(
                QStringLiteral("srSetVol:%1:%2").arg(c).arg(targetVolume, 0, 'f', 2));
            return targetVolume;
        }
        return std::nullopt;
    }

    std::optional<pabackend::StreamRestoreState>
    streamRestoreQueryState(const QStringList& candidates) override
    {
        if (!ready) return std::nullopt;
        for (const QString& c : candidates)
        {
            auto it = streamRestoreDb.constFind(c);
            if (it != streamRestoreDb.cend()) return *it;
        }
        return std::nullopt;
    }

    bool streamRestoreSetChannelVolumes(const QStringList& candidates,
                                        const pa_cvolume& channelVolume) override
    {
        if (!ready) return false;
        for (const QString& c : candidates)
        {
            auto it = streamRestoreDb.find(c);
            if (it == streamRestoreDb.end()) continue;
            it->volume = channelVolume;
            return true;
        }
        return false;
    }

    std::optional<std::pair<bool, double>>
    streamRestoreToggleMute(const QStringList& candidates) override
    {
        if (!ready) return std::nullopt;
        for (const QString& c : candidates)
        {
            auto it = streamRestoreDb.find(c);
            if (it == streamRestoreDb.end()) continue;
            it->muted = !it->muted;
            return std::make_pair(it->muted, it->averageVolume());
        }
        return std::nullopt;
    }

    std::optional<std::pair<bool, double>> streamRestoreSetMute(const QStringList& candidates,
                                                                bool targetMuted) override
    {
        if (!ready) return std::nullopt;
        for (const QString& c : candidates)
        {
            auto it = streamRestoreDb.find(c);
            if (it == streamRestoreDb.end()) continue;
            it->muted = targetMuted;
            return std::make_pair(it->muted, it->averageVolume());
        }
        return std::nullopt;
    }

    bool streamRestoreSetDevice(const QStringList& candidates, const QString& sinkName) override
    {
        if (!ready || !streamRestoreDeviceWriteSucceeds) return false;
        if (candidates.isEmpty()) return false;
        persistedStreamRestoreDevices[candidates.first()] = sinkName;
        operationLog.append(QStringLiteral("srSetDev:%1:%2").arg(candidates.first(), sinkName));
        return true;
    }

    bool streamRestoreClearDevice(const QStringList& candidates) override
    {
        if (!ready || candidates.isEmpty()) return false;
        clearedStreamRestoreApps.append(candidates.first());
        operationLog.append(QStringLiteral("srClearDev:%1").arg(candidates.first()));
        return true;
    }

    PipeWireSnapshot pwInspect(const QSet<QString>&, const QSet<QString>&) override
    {
        return {pwClients, pwNodes};
    }

    QList<PipeWireClient> pwListClients(const QSet<QString>&, const QSet<QString>&) override
    {
        return pwClients;
    }

    QList<PipeWireNode> pwFindNodesForApp(const QString&,
                                          const QStringList& matchCandidates) override
    {
        QList<PipeWireNode> matched;
        for (const PipeWireNode& node : pwNodes)
        {
            if (pipeWireNodeMatchesApp(node, matchCandidates)) matched.append(node);
        }
        return matched;
    }

    std::optional<PipeWireNode> pwFindNodeForApp(const QString& appName,
                                                 const QStringList& matchCandidates) override
    {
        const auto nodes = pwFindNodesForApp(appName, matchCandidates);
        if (nodes.isEmpty()) return std::nullopt;
        return nodes.first();
    }

    std::optional<PipeWireNode> pwReadNode(uint32_t nodeId) override
    {
        for (const PipeWireNode& node : pwNodes)
        {
            if (node.id == nodeId) return node;
        }
        return std::nullopt;
    }

    bool pwSetVisibleVolume(uint32_t nodeId, double volume, uint32_t channelCount) override
    {
        operationLog.append(QStringLiteral("pwSetVol:%1:%2").arg(nodeId).arg(volume, 0, 'f', 2));
        writtenPwVolumes[nodeId] = volume;
        const double cubed = volume * volume * volume;
        for (auto& node : pwNodes)
        {
            if (node.id == nodeId)
            {
                node.rawVolume = 1.0;
                node.channelVolumes = QList<double>(std::max<uint32_t>(1, channelCount), cubed);
            }
        }
        return true;
    }

    bool pwSetVisibleChannelVolumes(uint32_t nodeId, const QList<double>& channelVolumes) override
    {
        operationLog.append(QStringLiteral("pwSetChVol:%1").arg(nodeId));
        writtenPwChannels[nodeId] = channelVolumes;
        for (auto& node : pwNodes)
        {
            if (node.id == nodeId)
            {
                node.rawVolume = 1.0;
                node.channelVolumes.clear();
                for (double v : channelVolumes) node.channelVolumes.append(v * v * v);
            }
        }
        return true;
    }

    bool pwRestoreVolumeState(uint32_t nodeId, double rawVolume,
                              const QList<double>& channelVolumes) override
    {
        operationLog.append(QStringLiteral("pwRestore:%1").arg(nodeId));
        for (auto& node : pwNodes)
        {
            if (node.id == nodeId)
            {
                node.rawVolume = rawVolume;
                node.channelVolumes = channelVolumes;
            }
        }
        return true;
    }

    bool pwSetMute(uint32_t nodeId, bool muted) override
    {
        writtenPwMutes[nodeId] = muted;
        for (auto& node : pwNodes)
        {
            if (node.id == nodeId) node.muted = muted;
        }
        return true;
    }

    void pwClose() override
    {
        ++pwCloses;
    }
};

} // namespace

TEST(PaWorkerUnit, ReconnectBackoffEscalatesAndResetsOnSuccess)
{
    auto fakePtr = std::make_unique<FakePaBackend>();
    auto* fake = fakePtr.get();
    fake->allowConnect = false;

    PaWorker worker(std::move(fakePtr));
    worker.init();

    // First failed connect schedules 500ms backoff.
    EXPECT_EQ(fake->connectAttempts, 1);
    EXPECT_EQ(worker.reconnectBackoffMs(), 500);

    // Stop reconnect timer before driving manual attempts so scheduleReconnect() advances backoff.
    auto timers = worker.findChildren<QTimer*>();
    auto stopTimers = [&]()
    {
        for (QTimer* t : timers) t->stop();
    };

    stopTimers();
    worker.attemptReconnect();
    EXPECT_EQ(worker.reconnectBackoffMs(), 1000);

    stopTimers();
    worker.attemptReconnect();
    EXPECT_EQ(worker.reconnectBackoffMs(), 2000);

    // Verify helper clamps at RECONNECT_BACKOFF_MAX_MS.
    EXPECT_EQ(pabackend::nextReconnectBackoff(20000), pabackend::RECONNECT_BACKOFF_MAX_MS);
    EXPECT_EQ(pabackend::nextReconnectBackoff(30000), pabackend::RECONNECT_BACKOFF_MAX_MS);

    // Now PulseAudio comes back: backoff resets to 0, watcher starts, sinks/apps refresh.
    fake->allowConnect = true;
    fake->sinks = {{QStringLiteral("alsa_output.usb"), QStringLiteral("USB"), 1, false}};
    fake->defaultSinkName = QStringLiteral("alsa_output.usb");
    QSignalSpy sinksSpy(&worker, &PaWorker::sinksReady);
    QSignalSpy appsSpy(&worker, &PaWorker::appsReady);

    stopTimers();
    worker.attemptReconnect();

    EXPECT_EQ(worker.reconnectBackoffMs(), 0);
    EXPECT_EQ(fake->watcherStarts, 1);
    ASSERT_EQ(sinksSpy.count(), 1);
    const auto emittedSinks = sinksSpy.first().first().value<QList<SinkInfo>>();
    ASSERT_EQ(emittedSinks.size(), 1);
    EXPECT_TRUE(emittedSinks.first().isDefault);
    EXPECT_EQ(appsSpy.count(), 1);

    worker.cleanup();
    EXPECT_EQ(fake->watcherStops, 1);
    EXPECT_EQ(fake->pwCloses, 1);
}

TEST(PaWorkerUnit, OfflineOperationsParkAndReplayInOrderOnReconnect)
{
    QTemporaryDir tmp;
    Config cfg(tmp.path());
    cfg.setSelectedApp(QStringLiteral("spotify"));

    auto fakePtr = std::make_unique<FakePaBackend>();
    auto* fake = fakePtr.get();
    fake->allowConnect = false;

    PaWorker worker(std::move(fakePtr));
    worker.setConfig(&cfg);
    worker.init();

    QSignalSpy volSpy(&worker, &PaWorker::volumeChanged);

    // Offline operations must park pending state and still emit volumeChanged for OSD feedback.
    worker.setAppVolume(QStringLiteral("spotify"), 0.40);
    worker.setAppMute(QStringLiteral("spotify"), true);
    worker.doSetAppSink(QStringLiteral("spotify"), QStringLiteral("alsa_output.headset"));
    worker.doClearAppSinkOverride(QStringLiteral("discord"));

    EXPECT_TRUE(worker.hasPendingVolume(QStringLiteral("spotify")));
    EXPECT_TRUE(worker.hasPendingMute(QStringLiteral("spotify")));
    EXPECT_TRUE(worker.hasPendingSink(QStringLiteral("spotify")));
    EXPECT_TRUE(worker.hasPendingSinkClear(QStringLiteral("discord")));
    ASSERT_EQ(volSpy.count(), 2);
    EXPECT_DOUBLE_EQ(volSpy.at(0).at(1).toDouble(), 0.40);
    EXPECT_TRUE(volSpy.at(1).at(2).toBool());

    // Config's selected_app must remain untouched across reconnects.
    EXPECT_EQ(cfg.selectedApp(), QStringLiteral("spotify"));

    // Bring PA online with spotify active and verify replay order:
    // moveSink -> restore volume -> apply pending volume -> apply pending mute -> persist SR device
    // -> clear discord SR override.
    fake->allowConnect = true;
    fake->sinkInputs = {
        makeSinkInput(42, QStringLiteral("Spotify"), QStringLiteral("spotify"), 0.80, false)};
    fake->operationLog.clear();

    for (QTimer* t : worker.findChildren<QTimer*>()) t->stop();
    worker.attemptReconnect();

    EXPECT_FALSE(worker.hasPendingVolume(QStringLiteral("spotify")));
    EXPECT_FALSE(worker.hasPendingMute(QStringLiteral("spotify")));
    EXPECT_FALSE(worker.hasPendingSink(QStringLiteral("spotify")));
    EXPECT_FALSE(worker.hasPendingSinkClear(QStringLiteral("discord")));

    EXPECT_EQ(fake->movedSinkInputs.value(42), QStringLiteral("alsa_output.headset"));
    EXPECT_DOUBLE_EQ(fake->writtenSinkInputVolumes.value(42), 0.40);
    EXPECT_TRUE(fake->writtenSinkInputMutes.value(42));
    EXPECT_EQ(fake->persistedStreamRestoreDevices.value(QStringLiteral("spotify")),
              QStringLiteral("alsa_output.headset"));
    EXPECT_TRUE(fake->clearedStreamRestoreApps.contains(QStringLiteral("discord")));

    const qsizetype moveIdx =
        fake->operationLog.indexOf(QStringLiteral("moveSink:42:alsa_output.headset"));
    const qsizetype volIdx = fake->operationLog.indexOf(QStringLiteral("setVol:42:0.40"));
    const qsizetype muteIdx = fake->operationLog.indexOf(QStringLiteral("setMute:42:1"));
    ASSERT_GE(moveIdx, 0);
    ASSERT_GE(volIdx, 0);
    ASSERT_GE(muteIdx, 0);
    EXPECT_LT(moveIdx, volIdx);
    EXPECT_LT(volIdx, muteIdx);
    EXPECT_EQ(cfg.selectedApp(), QStringLiteral("spotify"));

    worker.cleanup();
}

TEST(PaWorkerUnit, PendingSinkStaysParkedWhenMoveRejectedOrStreamRestoreFails)
{
    auto fakePtr = std::make_unique<FakePaBackend>();
    auto* fake = fakePtr.get();
    fake->allowConnect = true;
    fake->rejectSinkMove = true;
    fake->streamRestoreDeviceWriteSucceeds = false;
    fake->sinkInputs = {
        makeSinkInput(7, QStringLiteral("Firefox"), QStringLiteral("firefox"), 0.70, false)};

    PaWorker worker(std::move(fakePtr));
    worker.init();

    worker.doSetAppSink(QStringLiteral("firefox"), QStringLiteral("alsa_output.stale"));
    EXPECT_TRUE(worker.hasPendingSink(QStringLiteral("firefox")));

    // Rejected move keeps sink parked.
    worker.doApplyPending();
    EXPECT_TRUE(worker.hasPendingSink(QStringLiteral("firefox")));

    // Move succeeds, but SR persistence fails -> still parked so future streams retry SR write.
    fake->rejectSinkMove = false;
    worker.doApplyPending();
    EXPECT_EQ(fake->movedSinkInputs.value(7), QStringLiteral("alsa_output.stale"));
    EXPECT_TRUE(worker.hasPendingSink(QStringLiteral("firefox")));

    // Once SR persistence succeeds, pending sink is cleared.
    fake->streamRestoreDeviceWriteSucceeds = true;
    worker.doApplyPending();
    EXPECT_FALSE(worker.hasPendingSink(QStringLiteral("firefox")));

    worker.cleanup();
}

TEST(PaWorkerUnit, PendingMuteOnlyPassAppliesWithoutPendingVolume)
{
    auto fakePtr = std::make_unique<FakePaBackend>();
    auto* fake = fakePtr.get();
    fake->allowConnect = false;

    PaWorker worker(std::move(fakePtr));
    worker.init();

    worker.doToggleMute(QStringLiteral("vlc"));
    EXPECT_FALSE(worker.hasPendingVolume(QStringLiteral("vlc")));
    EXPECT_TRUE(worker.hasPendingMute(QStringLiteral("vlc")));

    fake->allowConnect = true;
    fake->sinkInputs = {
        makeSinkInput(11, QStringLiteral("VLC"), QStringLiteral("vlc"), 0.90, false)};
    for (QTimer* t : worker.findChildren<QTimer*>()) t->stop();
    worker.attemptReconnect();

    EXPECT_FALSE(worker.hasPendingMute(QStringLiteral("vlc")));
    EXPECT_TRUE(fake->writtenSinkInputMutes.value(11));

    worker.cleanup();
}

TEST(PaWorkerUnit, ClampsPerProfileBoundsAcrossAllFallbackLevels)
{
    auto fakePtr = std::make_unique<FakePaBackend>();
    auto* fake = fakePtr.get();
    fake->allowConnect = true;
    fake->sinkInputs = {
        makeSinkInput(1, QStringLiteral("Spotify"), QStringLiteral("spotify"), 0.50, false)};

    PipeWireNode pwNode;
    pwNode.id = 99;
    pwNode.name = QStringLiteral("IdlePlayer");
    pwNode.binary = QStringLiteral("idleplayer");
    pwNode.mediaClass = QStringLiteral("Stream/Output/Audio");
    pwNode.rawVolume = 1.0;
    pwNode.channelVolumes = {0.125, 0.125}; // cbrt(0.125) = 0.50
    fake->pwNodes = {pwNode};

    pabackend::StreamRestoreState srState;
    srState.muted = false;
    pa_cvolume_set(&srState.volume, 2, static_cast<pa_volume_t>(0.50 * PA_VOLUME_NORM));
    fake->streamRestoreDb.insert(QStringLiteral("srapp"), srState);

    PaWorker worker(std::move(fakePtr));
    worker.init();

    QSignalSpy volSpy(&worker, &PaWorker::volumeChanged);

    // 1. Active sink input with inverted bounds (0.80, 0.20) -> swapped to [0.20, 0.80].
    worker.doChangeVolume(QStringLiteral("spotify"), +0.50, 0.80, 0.20);
    EXPECT_DOUBLE_EQ(fake->writtenSinkInputVolumes.value(1), 0.80);

    // 2. Idle PipeWire node clamped to floor 0.30.
    worker.doChangeVolume(QStringLiteral("idleplayer"), -0.40, 0.30, 0.90);
    EXPECT_DOUBLE_EQ(fake->writtenPwVolumes.value(99), 0.30);

    // 3. Stream restore clamped to ceiling 0.65 (native PA -> does not park pending).
    worker.setAppVolume(QStringLiteral("srapp"), 0.95, 0.10, 0.65);
    EXPECT_FALSE(worker.hasPendingVolume(QStringLiteral("srapp")));
    EXPECT_NEAR(volSpy.last().at(1).toDouble(), 0.65, 1e-6);

    // 4. Disconnected app parks clamped volume.
    worker.setAppVolume(QStringLiteral("offlineapp"), 0.05, 0.25, 0.75);
    EXPECT_TRUE(worker.hasPendingVolume(QStringLiteral("offlineapp")));
    EXPECT_NEAR(volSpy.last().at(1).toDouble(), 0.25, 1e-6);

    worker.cleanup();
}

TEST(PaWorkerUnit, PipeWirePulseStreamRestoreParksPendingVolume)
{
    auto fakePtr = std::make_unique<FakePaBackend>();
    auto* fake = fakePtr.get();
    fake->allowConnect = true;
    fake->pipeWirePulse = true;

    pabackend::StreamRestoreState srState;
    pa_cvolume_set(&srState.volume, 2, static_cast<pa_volume_t>(0.50 * PA_VOLUME_NORM));
    fake->streamRestoreDb.insert(QStringLiteral("mpv"), srState);

    PaWorker worker(std::move(fakePtr));
    worker.init();

    worker.setAppVolume(QStringLiteral("mpv"), 0.35);
    EXPECT_TRUE(worker.hasPendingVolume(QStringLiteral("mpv")));

    worker.cleanup();
}

TEST(PaWorkerUnit, ContextLossInvalidatesDuckingIdsAndReconnectRestoresVolume)
{
    auto fakePtr = std::make_unique<FakePaBackend>();
    auto* fake = fakePtr.get();
    fake->allowConnect = true;
    fake->sinkInputs = {
        makeSinkInput(10, QStringLiteral("Discord"), QStringLiteral("discord"), 0.90, false),
        makeSinkInput(20, QStringLiteral("Spotify"), QStringLiteral("spotify"), 0.80, false)};

    PaWorker worker(std::move(fakePtr));
    worker.init();

    // Duck everything except discord -> spotify drops to 0.20.
    worker.doToggleDucking(QStringLiteral("discord"), 0.20);
    EXPECT_TRUE(worker.isDuckingActive());
    EXPECT_DOUBLE_EQ(fake->writtenSinkInputVolumes.value(20), 0.20);
    EXPECT_FALSE(fake->writtenSinkInputVolumes.contains(10));

    // Simulate PA context loss: snapshot index 20 is invalidated and reconnect is scheduled.
    fake->allowConnect = false;
    worker.handleContextLost();
    EXPECT_FALSE(fake->isReady());
    EXPECT_GT(worker.reconnectBackoffMs(), 0);
    EXPECT_TRUE(worker.isDuckingActive());

    // PA reconnects with a BRAND NEW sink input index (99) for spotify.
    fake->allowConnect = true;
    fake->sinkInputs = {
        makeSinkInput(15, QStringLiteral("Discord"), QStringLiteral("discord"), 0.90, false),
        makeSinkInput(99, QStringLiteral("Spotify"), QStringLiteral("spotify"), 0.20, false)};
    for (QTimer* t : worker.findChildren<QTimer*>()) t->stop();
    worker.attemptReconnect();

    // Second toggle restores spotify's original 0.80 on the new index 99.
    worker.doToggleDucking(QStringLiteral("discord"), 0.20);
    EXPECT_FALSE(worker.isDuckingActive());
    ASSERT_TRUE(fake->writtenSinkInputChannels.contains(99));
    ASSERT_EQ(fake->writtenSinkInputChannels.value(99).size(), 2);
    EXPECT_NEAR(fake->writtenSinkInputChannels.value(99).at(0), 0.80, 1e-3);

    worker.cleanup();
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
