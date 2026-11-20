#include "paworker.h"
#include "appmatcher.h"

#include <QDateTime>
#include <QDebug>
#include <QMutexLocker>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <numeric>

PaWorker::PaWorker(std::unique_ptr<IPaBackend> backend)
    : m_backend(backend ? std::move(backend) : createLibPulseBackend())
{
}

PaWorker::~PaWorker()
{
    cleanup();
}

void PaWorker::setConfig(Config* config)
{
    m_config.store(config, std::memory_order_release);
}

QList<AppAlias> PaWorker::currentAliases() const
{
    Config* cfg = m_config.load(std::memory_order_acquire);
    return cfg ? cfg->appAliases() : QList<AppAlias>{};
}

QStringList PaWorker::matchCandidatesFor(const QString& appName) const
{
    return appMatchCandidates(appName, currentAliases());
}

bool PaWorker::hasPendingVolume(const QString& app) const
{
    QMutexLocker lk(&m_pendingMutex);
    return m_pendingVolumes.contains(app);
}

bool PaWorker::hasPendingChannelVolumes(const QString& app) const
{
    QMutexLocker lk(&m_pendingMutex);
    return m_pendingChannelVolumes.contains(app);
}

bool PaWorker::hasPendingMute(const QString& app) const
{
    QMutexLocker lk(&m_pendingMutex);
    return m_pendingMutes.contains(app);
}

bool PaWorker::hasPendingSink(const QString& app) const
{
    QMutexLocker lk(&m_pendingMutex);
    return m_pendingSinks.contains(app);
}

bool PaWorker::hasPendingSinkClear(const QString& app) const
{
    QMutexLocker lk(&m_pendingMutex);
    return m_pendingSinkClears.contains(app);
}

void PaWorker::init()
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

void PaWorker::cleanup()
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

    if (m_backend)
    {
        m_backend->stopWatcher();
        m_backend->pwClose();
        disconnectContext();
    }
    m_cleanedUp = true;
    emit cleanupFinished();
}

void PaWorker::requestStop()
{
    m_stopping = true;
}

void PaWorker::attemptReconnect()
{
    if (m_stopping || contextReady()) return;

    disconnectContext();
    if (connectContext())
    {
        m_reconnectBackoffMs = 0;
        startWatcher();
        doListSinks(/*forceRefresh=*/true);
        doApplyPending();
        doFlushPendingSinkClears();
        doListApps(/*forceRefresh=*/true);
        return;
    }

    qWarning() << "[PaWorker] Cannot connect to PulseAudio";
    scheduleReconnect();
    doListApps(/*forceRefresh=*/true);
}

void PaWorker::handleContextLost()
{
    if (m_stopping) return;
    for (DuckingSnapshot& snapshot : m_duckingSnapshot)
    {
        snapshot.pipeWireNodeId.reset();
        snapshot.pulseSinkInputIndex = PA_INVALID_INDEX;
    }
    disconnectContext();
    scheduleReconnect();
    doListApps(/*forceRefresh=*/true);
}

void PaWorker::doChangeVolume(const QString& appName, double delta, double minVol, double maxVol)
{
    if (m_stopping) return;

    // Sanitize bounds so a misconfigured profile cannot break volume control.
    minVol = std::clamp(minVol, 0.0, 1.0);
    maxVol = std::clamp(maxVol, 0.0, 1.0);
    if (minVol > maxVol) std::swap(minVol, maxVol);
    const QList<AppAlias> aliases = currentAliases();

    // 1. Active sink inputs. On pipewire-pulse, object.id identifies the
    // exact PipeWire node; write channelVolumes there so no hidden scalar
    // multiplier can survive an explicit operation.
    if (contextReady())
    {
        const auto inputs = m_backend->listSinkInputs();
        std::optional<double> reportedVolume;
        bool reportedMute = false;
        for (const auto& si : inputs)
        {
            std::optional<PipeWireNode> node;
            if (!sinkInputMatches(si, appName, aliases, &node)) continue;

            double baseVolume = si.volume;
            if (node)
            {
                reportHiddenVolume(si.displayName(), *node);
                baseVolume = node->effectiveVolume();
            }
            const double newVol = std::clamp(baseVolume + delta, minVol, maxVol);
            if (!setActiveStreamVisibleVolume(si, newVol)) continue;
            if (!reportedVolume) reportedVolume = newVol;
            reportedMute = si.muted;
        }
        if (reportedVolume)
        {
            m_appVolumes[appName] = *reportedVolume;
            m_appMutes[appName] = reportedMute;
            removePendingVolume(appName);
            emit volumeChanged(appName, *reportedVolume, reportedMute);
            return;
        }
    }

    // 2. Paused/idle PipeWire nodes. Normalize an existing hidden
    // multiplier while preserving its former effective level as the base
    // for this relative user action.
    const QList<PipeWireNode> nodes =
        m_backend->pwFindNodesForApp(appName, matchCandidatesFor(appName));
    bool hasPipeWireVolume = false;
    double pipeWireVolume = 0.0;
    bool pipeWireMuted = false;
    for (const PipeWireNode& node : nodes)
    {
        reportHiddenVolume(appName, node);
        const double newVol = std::clamp(node.effectiveVolume() + delta, minVol, maxVol);
        const uint32_t channels =
            static_cast<uint32_t>(std::max(1, static_cast<int>(node.channelVolumes.size())));
        if (!m_backend->pwSetVisibleVolume(node.id, newVol, channels)) continue;
        if (!hasPipeWireVolume)
        {
            hasPipeWireVolume = true;
            pipeWireVolume = newVol;
        }
        pipeWireMuted = node.muted;
    }
    if (hasPipeWireVolume)
    {
        m_appVolumes[appName] = pipeWireVolume;
        m_appMutes[appName] = pipeWireMuted;
        removePendingVolume(appName);
        emit volumeChanged(appName, pipeWireVolume, pipeWireMuted);
        return;
    }

    // 3. Stream restore DB (pure PulseAudio, or no live PipeWire node).
    auto vol = streamRestoreChangeVolume(appName, delta, minVol, maxVol);
    if (vol)
    {
        m_appVolumes[appName] = *vol;
        if (m_backend->isPipeWirePulse())
            parkPendingVolume(appName, *vol);
        else
            removePendingVolume(appName);
        emit volumeChanged(appName, *vol, m_appMutes.value(appName, false));
        return;
    }

    // 4. App disconnected — park and show desired volume on OSD anyway
    double base = m_appVolumes.value(appName, 1.0);
    double newVol = std::clamp(base + delta, minVol, maxVol);
    m_appVolumes[appName] = newVol;
    parkPendingVolume(appName, newVol);
    emit volumeChanged(appName, newVol, m_appMutes.value(appName, false));
}

void PaWorker::doToggleMute(const QString& appName)
{
    if (m_stopping) return;
    const QList<AppAlias> aliases = currentAliases();

    // 1. Active sink input
    if (contextReady())
    {
        const auto inputs = m_backend->listSinkInputs();
        for (const auto& si : inputs)
        {
            if (!sinkInputMatches(si, appName, aliases)) continue;

            const bool newMute = !si.muted;
            if (!m_backend->setSinkInputMute(si.index, newMute, "set sink input mute")) return;

            m_appVolumes[appName] = si.volume;
            m_appMutes[appName] = newMute;
            removePendingMute(appName);
            emit volumeChanged(appName, si.volume, newMute);
            return;
        }
    }

    // 2. Stream restore
    auto result = streamRestoreToggleMute(appName);
    if (result)
    {
        m_appMutes[appName] = result->first;
        removePendingMute(appName);
        emit volumeChanged(appName, result->second, result->first);
        return;
    }

    // 3. PipeWire node
    auto node = m_backend->pwFindNodeForApp(appName, matchCandidatesFor(appName));
    if (node)
    {
        bool newMuted = !node->muted;
        if (m_backend->pwSetMute(node->id, newMuted))
        {
            m_appMutes[appName] = newMuted;
            removePendingMute(appName);
            emit volumeChanged(appName, node->visibleVolume(), newMuted);
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

void PaWorker::setAppMute(const QString& appName, bool targetMuted)
{
    if (m_stopping) return;
    const QList<AppAlias> aliases = currentAliases();

    // 1. Active sink input
    if (contextReady())
    {
        const auto inputs = m_backend->listSinkInputs();
        for (const auto& si : inputs)
        {
            if (!sinkInputMatches(si, appName, aliases)) continue;

            if (si.muted == targetMuted)
            {
                m_appVolumes[appName] = si.volume;
                m_appMutes[appName] = targetMuted;
                removePendingMute(appName);
                emit volumeChanged(appName, si.volume, targetMuted);
                return;
            }

            if (!m_backend->setSinkInputMute(si.index, targetMuted, "set sink input absolute mute"))
                return;

            m_appVolumes[appName] = si.volume;
            m_appMutes[appName] = targetMuted;
            removePendingMute(appName);
            emit volumeChanged(appName, si.volume, targetMuted);
            return;
        }
    }

    // 2. Stream restore
    auto result = streamRestoreSetMute(appName, targetMuted);
    if (result)
    {
        m_appMutes[appName] = result->first;
        removePendingMute(appName);
        emit volumeChanged(appName, result->second, result->first);
        return;
    }

    // 3. PipeWire node
    auto node = m_backend->pwFindNodeForApp(appName, matchCandidatesFor(appName));
    if (node && m_backend->pwSetMute(node->id, targetMuted))
    {
        m_appMutes[appName] = targetMuted;
        removePendingMute(appName);
        emit volumeChanged(appName, node->visibleVolume(), targetMuted);
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

void PaWorker::setAppVolume(const QString& appName, double targetVolume, double minVol,
                            double maxVol)
{
    if (m_stopping) return;

    // Sanitize bounds — caller may pass invalid values.
    minVol = std::clamp(minVol, 0.0, 1.0);
    maxVol = std::clamp(maxVol, 0.0, 1.0);
    if (minVol > maxVol) std::swap(minVol, maxVol);
    targetVolume = std::clamp(targetVolume, minVol, maxVol);
    const QList<AppAlias> aliases = currentAliases();

    // 1. Active sink inputs (absolute target, not relative delta).
    if (contextReady())
    {
        const auto inputs = m_backend->listSinkInputs();
        bool applied = false;
        bool muted = false;
        for (const auto& si : inputs)
        {
            if (!sinkInputMatches(si, appName, aliases)) continue;

            if (!setActiveStreamVisibleVolume(si, targetVolume)) continue;
            applied = true;
            muted = si.muted;
        }
        if (applied)
        {
            m_appVolumes[appName] = targetVolume;
            m_appMutes[appName] = muted;
            removePendingVolume(appName);
            emit volumeChanged(appName, targetVolume, muted);
            return;
        }
    }

    // 2. Paused/idle PipeWire nodes.
    bool pipeWireApplied = false;
    bool pipeWireMuted = false;
    for (const PipeWireNode& node :
         m_backend->pwFindNodesForApp(appName, matchCandidatesFor(appName)))
    {
        reportHiddenVolume(appName, node);
        const uint32_t channels =
            static_cast<uint32_t>(std::max(1, static_cast<int>(node.channelVolumes.size())));
        if (!m_backend->pwSetVisibleVolume(node.id, targetVolume, channels)) continue;
        pipeWireApplied = true;
        pipeWireMuted = node.muted;
    }
    if (pipeWireApplied)
    {
        m_appVolumes[appName] = targetVolume;
        m_appMutes[appName] = pipeWireMuted;
        removePendingVolume(appName);
        emit volumeChanged(appName, targetVolume, pipeWireMuted);
        return;
    }

    // 3. Stream restore DB.
    if (streamRestoreSetVolume(appName, targetVolume, minVol, maxVol))
    {
        m_appVolumes[appName] = targetVolume;
        if (m_backend->isPipeWirePulse())
            parkPendingVolume(appName, targetVolume);
        else
            removePendingVolume(appName);
        emit volumeChanged(appName, targetVolume, m_appMutes.value(appName, false));
        return;
    }

    // 4. Disconnected — park desired absolute volume.
    m_appVolumes[appName] = targetVolume;
    parkPendingVolume(appName, targetVolume);
    emit volumeChanged(appName, targetVolume, m_appMutes.value(appName, false));
}

void PaWorker::doToggleDucking(const QString& keepApp, double duckVolume)
{
    if (m_stopping || keepApp.isEmpty()) return;

    duckVolume = std::clamp(duckVolume, 0.0, 1.0);
    const QList<AppAlias> aliases = currentAliases();

    if (m_duckingActive)
    {
        const QList<DuckingSnapshot> restore = m_duckingSnapshot;
        clearDuckingState();

        QList<pabackend::SinkInputInfo> currentInputs;
        if (contextReady()) currentInputs = m_backend->listSinkInputs();
        QSet<uint32_t> restoredInputIndexes;
        QSet<uint32_t> restoredPipeWireNodes;

        for (const DuckingSnapshot& snapshot : restore)
        {
            bool restored = false;
            for (const pabackend::SinkInputInfo& input : std::as_const(currentInputs))
            {
                if (restoredInputIndexes.contains(input.index) ||
                    !sinkInputMatches(input, snapshot.app, aliases) ||
                    (snapshot.pulseSinkInputIndex != PA_INVALID_INDEX &&
                     input.index != snapshot.pulseSinkInputIndex))
                    continue;
                if (input.pipeWireNodeId && !snapshot.visibleChannelVolumes.isEmpty())
                {
                    restored = m_backend->pwSetVisibleChannelVolumes(
                        *input.pipeWireNodeId, snapshot.visibleChannelVolumes);
                }
                if (!restored && snapshot.pulseChannelVolume.channels > 0)
                {
                    restored = m_backend->setSinkInputChannelVolumes(
                        input.index, snapshot.pulseChannelVolume,
                        "restore exact ducked sink input volume");
                }
                if (restored)
                {
                    restoredInputIndexes.insert(input.index);
                    if (input.pipeWireNodeId) restoredPipeWireNodes.insert(*input.pipeWireNodeId);
                }
                break;
            }

            if (!restored && !snapshot.visibleChannelVolumes.isEmpty())
            {
                const QList<PipeWireNode> nodes =
                    m_backend->pwFindNodesForApp(snapshot.app, matchCandidatesFor(snapshot.app));
                for (const PipeWireNode& node : nodes)
                {
                    const uint32_t expectedNodeId = snapshot.pipeWireNodeId.value_or(node.id);
                    if (restoredPipeWireNodes.contains(node.id) || node.id != expectedNodeId)
                        continue;
                    restored = m_backend->pwSetVisibleChannelVolumes(
                        node.id, snapshot.visibleChannelVolumes);
                    if (restored) restoredPipeWireNodes.insert(node.id);
                    break;
                }
            }

            if (!restored && snapshot.pulseChannelVolume.channels > 0 &&
                streamRestoreSetChannelVolumes(snapshot.app, snapshot.pulseChannelVolume))
            {
                restored = true;
                if (m_backend->isPipeWirePulse())
                {
                    const QList<double> channels =
                        !snapshot.visibleChannelVolumes.isEmpty()
                            ? snapshot.visibleChannelVolumes
                            : pabackend::channelVolumesFromPulse(snapshot.pulseChannelVolume);
                    parkPendingChannelVolumes(snapshot.app, channels);
                }
            }

            if (!restored) setAppVolume(snapshot.app, snapshot.volume);
        }
        return;
    }

    doListApps(/*forceRefresh=*/true);

    QList<DuckingSnapshot> snapshot;
    QSet<uint32_t> handledPipeWireNodes;
    QSet<QString> handledApps;

    // Snapshot and duck every active stream separately. Hidden scalar
    // multipliers are folded into the visible per-channel values here, so
    // this explicit action preserves the pre-duck effective level while
    // normalizing the representation.
    if (contextReady())
    {
        const QList<pabackend::SinkInputInfo> inputs = m_backend->listSinkInputs();
        for (const pabackend::SinkInputInfo& input : inputs)
        {
            std::optional<PipeWireNode> node;
            if (sinkInputMatches(input, keepApp, aliases, &node)) continue;

            DuckingSnapshot entry;
            entry.app = input.targetName();
            entry.pulseSinkInputIndex = input.index;
            entry.pulseChannelVolume = input.channelVolume;
            entry.volume = input.volume;

            if (!node) node = pipeWireStateFor(input);
            bool hasHiddenMultiplier = false;
            if (node.has_value())
            {
                const PipeWireNode& pwNode = *node;
                hasHiddenMultiplier = pwNode.hasHiddenVolumeMultiplier();
                entry.pipeWireNodeId = pwNode.id;
                handledPipeWireNodes.insert(pwNode.id);
                reportHiddenVolume(input.displayName(), pwNode);
                if (pwNode.channelVolumes.isEmpty())
                {
                    const int count = std::max<int>(1, input.channelVolume.channels);
                    entry.visibleChannelVolumes = QList<double>(count, pwNode.effectiveVolume());
                }
                else
                {
                    entry.visibleChannelVolumes = pwNode.channelVolumes;
                    for (double& value : entry.visibleChannelVolumes)
                        value = std::cbrt(std::max(0.0, value * pwNode.rawVolume));
                }
                entry.volume = std::accumulate(entry.visibleChannelVolumes.cbegin(),
                                               entry.visibleChannelVolumes.cend(), 0.0) /
                               static_cast<double>(entry.visibleChannelVolumes.size());
            }

            handledApps.insert(entry.app.toLower());
            const bool needsWrite = qAbs(entry.volume - duckVolume) > 0.0001 || hasHiddenMultiplier;
            if (needsWrite && setActiveStreamVisibleVolume(input, duckVolume))
                snapshot.append(std::move(entry));
        }
    }

    // Idle/paused PipeWire nodes do not appear as PA sink inputs. Snapshot
    // their exact channels too, then normalize and duck them directly.
    for (const AudioApp& app : std::as_const(m_listCache))
    {
        if (app.name.isEmpty()) continue;
        if (app.name == keepApp || app.binary == keepApp) continue;
        if (app.active || handledApps.contains(app.binary.toLower()) ||
            handledApps.contains(app.name.toLower()))
            continue;

        bool handledNode = false;
        for (const PipeWireNode& node :
             m_backend->pwFindNodesForApp(app.name, matchCandidatesFor(app.name)))
        {
            if (handledPipeWireNodes.contains(node.id)) continue;
            handledPipeWireNodes.insert(node.id);
            handledNode = true;
            reportHiddenVolume(app.name, node);

            DuckingSnapshot entry;
            entry.app = app.binary.isEmpty() ? app.name : app.binary;
            entry.pipeWireNodeId = node.id;
            if (node.channelVolumes.isEmpty())
                entry.visibleChannelVolumes = {node.effectiveVolume()};
            else
            {
                entry.visibleChannelVolumes = node.channelVolumes;
                for (double& value : entry.visibleChannelVolumes)
                    value = std::cbrt(std::max(0.0, value * node.rawVolume));
            }
            entry.volume = std::accumulate(entry.visibleChannelVolumes.cbegin(),
                                           entry.visibleChannelVolumes.cend(), 0.0) /
                           static_cast<double>(entry.visibleChannelVolumes.size());
            const bool needsWrite =
                qAbs(entry.volume - duckVolume) > 0.0001 || node.hasHiddenVolumeMultiplier();
            if (needsWrite &&
                m_backend->pwSetVisibleVolume(
                    node.id, duckVolume, std::max(1, static_cast<int>(node.channelVolumes.size()))))
                snapshot.append(std::move(entry));
        }

        if (!handledNode)
        {
            DuckingSnapshot entry;
            entry.app = app.binary.isEmpty() ? app.name : app.binary;
            if (const auto restored = streamRestoreQueryState(entry.app))
            {
                entry.pulseChannelVolume = restored->volume;
                entry.visibleChannelVolumes = pabackend::channelVolumesFromPulse(restored->volume);
                entry.volume = restored->averageVolume();
            }
            else
            {
                entry.volume = app.volume;
            }
            if (qAbs(entry.volume - duckVolume) > 0.0001)
            {
                snapshot.append(entry);
                setAppVolume(entry.app, duckVolume);
            }
        }
    }

    if (snapshot.isEmpty()) return;

    m_duckingActive = true;
    m_duckingKeepApp = keepApp;
    m_duckingSnapshot = std::move(snapshot);
}

void PaWorker::doQueryVolume(const QString& appName, bool silent)
{
    const auto report = [this, silent](const QString& app, double volume, bool muted)
    {
        if (silent)
            emit volumeQueriedSilently(app, volume, muted);
        else
            emit volumeChanged(app, volume, muted);
    };
    if (m_stopping) return;
    const QList<AppAlias> aliases = currentAliases();

    // 1. Active sink input — live read, no write
    if (contextReady())
    {
        const auto inputs = m_backend->listSinkInputs();
        for (const auto& si : inputs)
        {
            if (!sinkInputMatches(si, appName, aliases)) continue;

            m_appVolumes[appName] = si.volume;
            m_appMutes[appName] = si.muted;
            report(appName, si.volume, si.muted);
            return;
        }
    }

    // 2. Stream restore DB (persisted volume for inactive apps)
    if (auto restored = streamRestoreQueryVolume(appName))
    {
        const auto [muted, volume] = *restored;
        m_appVolumes[appName] = volume;
        m_appMutes[appName] = muted;
        report(appName, volume, muted);
        return;
    }

    // 3. PipeWire node (idle/paused app)
    auto node = m_backend->pwFindNodeForApp(appName, matchCandidatesFor(appName));
    if (node)
    {
        m_appVolumes[appName] = node->visibleVolume();
        m_appMutes[appName] = node->muted;
        report(appName, node->visibleVolume(), node->muted);
        return;
    }

    // 4. Cache fallback (app not currently present in PA or PW)
    report(appName, m_appVolumes.value(appName, 1.0), m_appMutes.value(appName, false));
}

void PaWorker::doListApps(bool forceRefresh)
{
    if (m_stopping) return;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!forceRefresh && (now - m_listCacheTs) < LIST_CACHE_TTL_MS)
        return; // cache still fresh — caller already has it

    Config* cfg = m_config.load(std::memory_order_acquire);
    const QSet<QString> systemBinaries = cfg ? cfg->effectiveSystemBinaries() : SYSTEM_BINARIES;
    const QSet<QString> skipAppNames = cfg ? cfg->effectiveSkipAppNames() : SKIP_APP_NAMES;
    const PipeWireSnapshot pwSnapshot = m_backend->pwInspect(systemBinaries, skipAppNames);
    const auto& pwClientsRaw = pwSnapshot.clients;
    const QList<AppAlias> aliases = cfg ? cfg->appAliases() : QList<AppAlias>{};
    const auto pwClients = applyAppAliases(pwClientsRaw, aliases);
    QMap<uint32_t, PipeWireNode> pwNodeById;
    QSet<QString> currentlyHiddenNodes;
    for (const PipeWireNode& node : pwSnapshot.nodes)
    {
        if (!node.mediaClass.contains(QStringLiteral("Output"))) continue;
        pwNodeById[node.id] = node;
        if (!node.hasHiddenVolumeMultiplier()) continue;
        currentlyHiddenNodes.insert(hiddenNodeKey(node));
        const QString label = !node.name.isEmpty()     ? node.name
                              : !node.binary.isEmpty() ? node.binary
                                                       : node.nodeName;
        reportHiddenVolume(label, node);
    }
    m_reportedHiddenNodes.intersect(currentlyHiddenNodes);
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
        const auto inputs = m_backend->listSinkInputs();
        for (const auto& si : inputs)
        {
            if (systemBinaries.contains(si.binary)) continue;

            QString mappedDisplay = displayByClientId.value(si.clientId);
            if (mappedDisplay.isEmpty())
                mappedDisplay = displayByTarget.value(si.targetName().toLower(), si.displayName());
            QString mappedTarget = targetByClientId.value(si.clientId, si.targetName());
            if (skipAppNames.contains(mappedDisplay) ||
                mappedDisplay.toLower().contains(QStringLiteral("input")))
                mappedDisplay = mappedTarget;
            if (mappedDisplay.trimmed().isEmpty()) continue;
            applyAppAliasToNames(mappedDisplay, mappedTarget, aliases);

            AudioApp app;
            app.sinkInputIndex = si.index;
            app.name = mappedDisplay;
            app.binary = mappedTarget;
            const auto nodeIt = pwNodeById.constFind(si.pipeWireNodeId.value_or(0));
            app.volume = nodeIt != pwNodeById.cend() ? nodeIt->visibleVolume() : si.volume;
            app.muted = nodeIt != pwNodeById.cend() ? nodeIt->muted : si.muted;
            app.active = true;
            apps[mappedDisplay] = app;
            if (!mappedTarget.isEmpty()) activeBinaries.insert(mappedTarget);
        }
    }

    // 2. Idle PW clients (libpipewire — runs here in PA thread, not main thread)
    for (const PipeWireClient& client : pwClients)
    {
        if (skipAppNames.contains(client.name)) continue;
        if (activeBinaries.contains(client.binary)) continue;
        if (pabackend::containsAppKey(apps, client.name)) continue;
        if (pabackend::containsAppKey(apps, client.binary)) continue;

        AudioApp app;
        app.name = client.name;
        app.binary = client.binary;
        app.volume = 1.0;
        app.muted = false;
        app.active = false;
        for (const PipeWireNode& node : pwSnapshot.nodes)
        {
            if (!node.mediaClass.contains(QStringLiteral("Output"))) continue;
            const QStringList candidates = appMatchCandidates(client.binary, aliases);
            if (!pipeWireNodeMatchesApp(node, candidates)) continue;
            app.volume = node.visibleVolume();
            app.muted = node.muted;
            break;
        }
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

void PaWorker::doApplyPending()
{
    if (m_stopping || !contextReady()) return;

    QMap<QString, double> pendVols;
    QMap<QString, QList<double>> pendChannelVols;
    QMap<QString, bool> pendMutes;
    QMap<QString, QString> pendSinks;
    {
        QMutexLocker lk(&m_pendingMutex);
        if (m_pendingVolumes.isEmpty() && m_pendingChannelVolumes.isEmpty() &&
            m_pendingMutes.isEmpty() && m_pendingSinks.isEmpty())
            return;
        pendVols = m_pendingVolumes;
        pendChannelVols = m_pendingChannelVolumes;
        pendMutes = m_pendingMutes;
        pendSinks = m_pendingSinks;
    }

    const QList<AppAlias> aliases = currentAliases();
    const auto inputs = m_backend->listSinkInputs();
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
            if (!sinkInputMatches(si, app, aliases)) continue;

            const std::optional<PipeWireNode> beforeMove = pipeWireStateFor(si);
            if (beforeMove) reportHiddenVolume(si.displayName(), *beforeMove);
            bool rejectedByServer = false;
            const bool moved = m_backend->moveSinkInputByName(
                si.index, it.value(), "apply pending sink move", rejectedByServer);
            if (moved)
            {
                if (restoreActiveStreamVolume(sinkInputAfterMove(si), beforeMove))
                    appliedSinks.insert(app);
                else
                    qWarning() << "[PaWorker] pending sink move succeeded but exact volume "
                                  "restore failed for"
                               << app << "stream" << si.index;
            }
            else if (rejectedByServer)
                qWarning() << "[PaWorker] pending sink move for" << app << "→" << it.value()
                           << "rejected by PA";
        }
    }

    for (const auto& si : inputs)
    {
        for (auto it = pendChannelVols.begin(); it != pendChannelVols.end(); ++it)
        {
            const QString& app = it.key();
            if (!sinkInputMatches(si, app, aliases)) continue;

            QList<double> channels = it.value();
            const int streamChannels = std::max<int>(1, si.channelVolume.channels);
            if (channels.size() != streamChannels)
            {
                const double average = std::accumulate(channels.cbegin(), channels.cend(), 0.0) /
                                       static_cast<double>(channels.size());
                channels = QList<double>(streamChannels, average);
            }

            bool volumeApplied = false;
            const uint32_t pipeWireNodeId = si.pipeWireNodeId.value_or(0);
            if (m_backend->isPipeWirePulse() && pipeWireNodeId != 0)
                volumeApplied = m_backend->pwSetVisibleChannelVolumes(pipeWireNodeId, channels);
            else
            {
                volumeApplied = m_backend->setSinkInputChannelVolumes(
                    si.index, pabackend::pulseVolumeFromChannels(channels),
                    "apply pending exact sink input channel volumes");
            }

            bool muteApplied = true;
            if (pendMutes.contains(app))
            {
                muteApplied = m_backend->setSinkInputMute(
                    si.index, pendMutes[app], "apply pending exact-volume sink input mute");
            }
            if (volumeApplied && muteApplied) applied.insert(app);
        }
    }

    for (const auto& si : inputs)
    {
        for (auto it = pendVols.begin(); it != pendVols.end(); ++it)
        {
            const QString& app = it.key();
            if (!sinkInputMatches(si, app, aliases)) continue;

            const bool volumeApplied = setActiveStreamVisibleVolume(si, it.value());

            bool muteApplied = true;
            if (pendMutes.contains(app))
            {
                muteApplied = m_backend->setSinkInputMute(si.index, pendMutes[app],
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
            if (pendVols.contains(app) || pendChannelVols.contains(app))
                continue; // already handled above
            if (!sinkInputMatches(si, app, aliases)) continue;

            const bool muteApplied = m_backend->setSinkInputMute(
                si.index, it.value(), "apply pending sink input mute only");

            if (muteApplied) applied.insert(app);
        }
    }
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
            m_pendingChannelVolumes.remove(app);
            m_pendingMutes.remove(app);
        }
        for (const auto& app : persistedSinks) m_pendingSinks.remove(app);
    }
}

void PaWorker::doListSinks(bool forceRefresh)
{
    if (m_stopping || !contextReady()) return;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!forceRefresh && (now - m_sinkCacheTs) < LIST_CACHE_TTL_MS) return;

    QList<SinkInfo> sinks;
    QString defaultName;
    if (!m_backend->listSinks(sinks, defaultName)) return;

    for (SinkInfo& s : sinks) s.isDefault = (s.name == defaultName);

    m_sinkNamesByIdx.clear();
    for (const SinkInfo& s : std::as_const(sinks)) m_sinkNamesByIdx.insert(s.index, s.name);

    m_sinkCache = sinks;
    m_sinkCacheTs = now;
    emit sinksReady(sinks);
}

void PaWorker::doClearAppSinkOverride(const QString& appName)
{
    if (m_stopping || appName.isEmpty()) return;

    {
        QMutexLocker lk(&m_pendingMutex);
        m_pendingSinks.remove(appName);
    }
    m_appSinks.remove(appName);

    if (!contextReady())
    {
        QMutexLocker lk(&m_pendingMutex);
        m_pendingSinkClears.insert(appName);
        return;
    }

    streamRestoreClearDevice(appName);
    {
        QMutexLocker lk(&m_pendingMutex);
        m_pendingSinkClears.remove(appName);
    }
    emit sinkChanged(appName, QString{});
}

void PaWorker::doFlushPendingSinkClears()
{
    if (m_stopping || !contextReady()) return;

    QSet<QString> pending;
    {
        QMutexLocker lk(&m_pendingMutex);
        if (m_pendingSinkClears.isEmpty()) return;
        pending = m_pendingSinkClears;
    }

    for (const QString& app : pending)
    {
        if (app.isEmpty()) continue;
        streamRestoreClearDevice(app);
        {
            QMutexLocker lk(&m_pendingMutex);
            m_pendingSinkClears.remove(app);
        }
        emit sinkChanged(app, QString{});
    }
}

void PaWorker::doSetAppSink(const QString& appName, const QString& sinkName)
{
    if (m_stopping) return;
    if (sinkName.isEmpty()) return;

    {
        QMutexLocker lk(&m_pendingMutex);
        m_pendingSinkClears.remove(appName);
    }

    bool routedAny = false;
    const QList<AppAlias> aliases = currentAliases();

    // 1. Active sink inputs — move every matching stream (browser-tab case).
    if (contextReady())
    {
        const auto inputs = m_backend->listSinkInputs();
        for (const auto& si : inputs)
        {
            if (!sinkInputMatches(si, appName, aliases)) continue;

            const std::optional<PipeWireNode> beforeMove = pipeWireStateFor(si);
            if (beforeMove) reportHiddenVolume(si.displayName(), *beforeMove);
            bool rejectedByServer = false;
            const bool moved = m_backend->moveSinkInputByName(
                si.index, sinkName, "move sink input to sink", rejectedByServer);
            if (moved)
            {
                if (restoreActiveStreamVolume(sinkInputAfterMove(si), beforeMove))
                    routedAny = true;
                else
                    qWarning()
                        << "[PaWorker] sink move succeeded but exact volume restore failed for"
                        << appName << "stream" << si.index;
            }
            else
            {
                qWarning() << "[PaWorker] move sink input" << si.index << "→" << sinkName
                           << (rejectedByServer ? "rejected by PA (stale sink name?)"
                                                : "timed out");
            }
        }
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

bool PaWorker::connectContext()
{
    if (!m_backend) return false;
    return m_backend->connect(m_stopping,
                              [this]()
                              {
                                  if (!m_stopping)
                                      QMetaObject::invokeMethod(this, "handleContextLost",
                                                                Qt::QueuedConnection);
                              });
}

void PaWorker::disconnectContext()
{
    if (m_backend) m_backend->disconnect();
}

bool PaWorker::contextReady()
{
    return m_backend && m_backend->isReady();
}

void PaWorker::scheduleReconnect()
{
    if (m_stopping || !m_reconnectTimer) return;
    if (m_reconnectTimer->isActive()) return;

    m_reconnectBackoffMs = pabackend::nextReconnectBackoff(m_reconnectBackoffMs);
    qWarning() << "[PaWorker] Scheduling PulseAudio reconnect in" << m_reconnectBackoffMs << "ms";
    m_reconnectTimer->start(m_reconnectBackoffMs);
}

void PaWorker::startWatcher()
{
    if (!m_backend) return;
    m_backend->startWatcher(
        this,
        [this]()
        {
            if (m_stopping) return;
            // Use a one-shot timer instead of msleep so the PA thread event loop
            // stays free to process other queued calls during the 100 ms wait.
            QTimer::singleShot(100, this, &PaWorker::doApplyPending);
            if (m_refreshTimer) m_refreshTimer->start();
        },
        [this]()
        {
            if (!m_stopping && m_refreshTimer) m_refreshTimer->start();
        },
        [this]()
        {
            if (!m_stopping && m_sinkRefreshTimer) m_sinkRefreshTimer->start();
        },
        [this]()
        {
            if (!m_stopping && m_refreshTimer) m_refreshTimer->start();
            if (!m_stopping && m_sinkRefreshTimer) m_sinkRefreshTimer->start();
        });
}

bool PaWorker::sinkInputMatches(const pabackend::SinkInputInfo& input, const QString& appName,
                                const QList<AppAlias>& aliases,
                                std::optional<PipeWireNode>* pipeWireState)
{
    if (m_backend->isPipeWirePulse() && input.pipeWireNodeId)
    {
        const std::optional<PipeWireNode> node = pipeWireStateFor(input);
        if (pipeWireState) *pipeWireState = node;
        if (node && (!node->clientName.isEmpty() || !node->clientBinary.isEmpty()))
            return pipeWireNodeMatchesApp(*node, appMatchCandidates(appName, aliases));
    }

    // Fall back to the PulseAudio identity when the PipeWire node or its
    // owning client is unavailable during a reconnect/race.
    return input.matches(appName, aliases);
}

std::optional<PipeWireNode> PaWorker::pipeWireStateFor(const pabackend::SinkInputInfo& input)
{
    if (!m_backend->isPipeWirePulse() || !input.pipeWireNodeId) return std::nullopt;
    return m_backend->pwReadNode(*input.pipeWireNodeId);
}

bool PaWorker::setActiveStreamVisibleVolume(const pabackend::SinkInputInfo& input,
                                            double targetVolume)
{
    if (m_backend->isPipeWirePulse() && input.pipeWireNodeId)
    {
        const uint32_t channels = std::max<uint32_t>(1, input.channelVolume.channels);
        return m_backend->pwSetVisibleVolume(*input.pipeWireNodeId, targetVolume, channels);
    }
    return m_backend->setSinkInputVolume(input, targetVolume);
}

bool PaWorker::restoreActiveStreamVolume(const pabackend::SinkInputInfo& input,
                                         const std::optional<PipeWireNode>& pipeWireState)
{
    if (m_backend->isPipeWirePulse() && pipeWireState)
    {
        if (!input.pipeWireNodeId) return false;
        return m_backend->pwRestoreVolumeState(*input.pipeWireNodeId, pipeWireState->rawVolume,
                                               pipeWireState->channelVolumes);
    }

    if (!contextReady()) return false;
    return m_backend->setSinkInputChannelVolumes(input.index, input.channelVolume,
                                                 "restore exact sink input volume");
}

pabackend::SinkInputInfo PaWorker::sinkInputAfterMove(const pabackend::SinkInputInfo& beforeMove)
{
    pabackend::SinkInputInfo result = beforeMove;
    if (!contextReady()) return result;

    const QList<pabackend::SinkInputInfo> currentInputs = m_backend->listSinkInputs();
    for (const pabackend::SinkInputInfo& current : currentInputs)
    {
        if (current.index != beforeMove.index) continue;
        result.pipeWireNodeId = current.pipeWireNodeId;
        break;
    }
    return result;
}

QStringList PaWorker::streamRestoreAppCandidates(const QString& app)
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
    const QList<AppAlias> aliases = currentAliases();
    for (const QString& candidate : appMatchCandidates(app, aliases)) add(candidate);

    const QString lowerApp = app.toLower();
    const QSet<QString> systemBinaries = [&]()
    {
        Config* cfg = m_config.load(std::memory_order_acquire);
        return cfg ? cfg->effectiveSystemBinaries() : SYSTEM_BINARIES;
    }();
    const QSet<QString> skipAppNames = [&]()
    {
        Config* cfg = m_config.load(std::memory_order_acquire);
        return cfg ? cfg->effectiveSkipAppNames() : SKIP_APP_NAMES;
    }();
    for (const PipeWireClient& client : m_backend->pwListClients(systemBinaries, skipAppNames))
    {
        if (appIdMatches(client.binary, lowerApp) || appIdMatches(client.name, lowerApp))
        {
            add(client.binary);
            add(client.name);
        }
    }
    return candidates;
}

void PaWorker::appendStreamRestoreAliasesFromDatabase(QStringList& candidates, const QString& app)
{
    if (!contextReady() || app.isEmpty()) return;

    const QStringList extra = m_backend->enumerateStreamRestoreAliases(app.toLower());
    for (const QString& entryApp : extra)
    {
        bool exists = false;
        for (const QString& existing : std::as_const(candidates))
        {
            if (existing.compare(entryApp, Qt::CaseInsensitive) == 0)
            {
                exists = true;
                break;
            }
        }
        if (!exists) candidates.append(entryApp);
    }
}

std::optional<double> PaWorker::streamRestoreChangeVolume(const QString& app, double delta,
                                                          double minVol, double maxVol)
{
    if (!contextReady()) return std::nullopt;
    return m_backend->streamRestoreChangeVolume(streamRestoreAppCandidates(app), delta, minVol,
                                                maxVol);
}

std::optional<pabackend::StreamRestoreState> PaWorker::streamRestoreQueryState(const QString& app)
{
    if (!contextReady()) return std::nullopt;
    return m_backend->streamRestoreQueryState(streamRestoreAppCandidates(app));
}

std::optional<std::pair<bool, double>> PaWorker::streamRestoreQueryVolume(const QString& app)
{
    const auto state = streamRestoreQueryState(app);
    if (!state) return std::nullopt;
    return std::make_pair(state->muted, state->averageVolume());
}

bool PaWorker::streamRestoreSetChannelVolumes(const QString& app, const pa_cvolume& channelVolume)
{
    if (!contextReady()) return false;
    return m_backend->streamRestoreSetChannelVolumes(streamRestoreAppCandidates(app),
                                                     channelVolume);
}

std::optional<double> PaWorker::streamRestoreSetVolume(const QString& app, double targetVolume,
                                                       double minVol, double maxVol)
{
    if (!contextReady()) return std::nullopt;
    return m_backend->streamRestoreSetVolume(streamRestoreAppCandidates(app), targetVolume, minVol,
                                             maxVol);
}

std::optional<std::pair<bool, double>> PaWorker::streamRestoreToggleMute(const QString& app)
{
    if (!contextReady()) return std::nullopt;
    return m_backend->streamRestoreToggleMute(streamRestoreAppCandidates(app));
}

std::optional<std::pair<bool, double>> PaWorker::streamRestoreSetMute(const QString& app,
                                                                      bool targetMuted)
{
    if (!contextReady()) return std::nullopt;
    return m_backend->streamRestoreSetMute(streamRestoreAppCandidates(app), targetMuted);
}

bool PaWorker::streamRestoreSetDevice(const QString& app, const QString& sinkName)
{
    if (!contextReady() || sinkName.isEmpty()) return false;
    return m_backend->streamRestoreSetDevice(streamRestoreAppCandidates(app), sinkName);
}

bool PaWorker::streamRestoreClearDevice(const QString& app)
{
    if (!contextReady()) return false;
    QStringList candidates = streamRestoreAppCandidates(app);
    appendStreamRestoreAliasesFromDatabase(candidates, app);
    return m_backend->streamRestoreClearDevice(candidates);
}

void PaWorker::reportHiddenVolume(const QString& app, const PipeWireNode& node)
{
    const QString key = hiddenNodeKey(node);
    if (!node.hasHiddenVolumeMultiplier() || m_reportedHiddenNodes.contains(key)) return;
    m_reportedHiddenNodes.insert(key);
    const QString label = app.trimmed().isEmpty() ? QStringLiteral("unknown") : app;
    qWarning().nospace() << "[PaWorker] Hidden PipeWire volume multiplier detected for " << label
                         << " (node " << node.id << "): raw=" << node.rawVolume
                         << ", visible=" << node.visibleVolume()
                         << ", effective=" << node.effectiveVolume()
                         << ". It will be normalized only by an explicit volume action.";
    emit hiddenVolumeDetected(label, node.rawVolume, node.visibleVolume(), node.effectiveVolume());
}

QString PaWorker::hiddenNodeKey(const PipeWireNode& node)
{
    if (!node.objectSerial.isEmpty()) return node.objectSerial;
    return QStringLiteral("%1:%2:%3").arg(node.id).arg(node.nodeName, node.name);
}

void PaWorker::parkPendingVolume(const QString& app, double volume)
{
    QMutexLocker lk(&m_pendingMutex);
    m_pendingChannelVolumes.remove(app);
    m_pendingVolumes[app] = volume;
}

void PaWorker::parkPendingChannelVolumes(const QString& app, const QList<double>& volumes)
{
    if (volumes.isEmpty()) return;
    QMutexLocker lk(&m_pendingMutex);
    m_pendingVolumes.remove(app);
    m_pendingChannelVolumes[app] = volumes;
}

void PaWorker::removePendingVolume(const QString& app)
{
    QMutexLocker lk(&m_pendingMutex);
    m_pendingVolumes.remove(app);
    m_pendingChannelVolumes.remove(app);
}

void PaWorker::removePendingMute(const QString& app)
{
    QMutexLocker lk(&m_pendingMutex);
    m_pendingMutes.remove(app);
}

void PaWorker::clearDuckingState()
{
    m_duckingActive = false;
    m_duckingKeepApp.clear();
    m_duckingSnapshot.clear();
}
