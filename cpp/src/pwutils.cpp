#include "pwutils.h"
#include "pwclientfilter.h"
#include "pwnodecontrol.h"
#include "pwsession.h"

#include <algorithm>
#include <memory>

#include <spa/param/audio/raw.h>

namespace
{

std::optional<PipeWireSnapshot> inspectSession(pwdetail::PipeWireSession& session,
                                               const QSet<QString>& systemBinaries,
                                               const QSet<QString>& skipAppNames)
{
    const auto registryGlobals = pwdetail::snapshotObjectInfo(session);
    if (!registryGlobals) return std::nullopt;

    PipeWireSnapshot snapshot;
    QList<PipeWireGlobalProps> globals;
    for (const pwdetail::RegistryGlobal& global : *registryGlobals)
    {
        globals.append({
            global.type,
            global.name,
            global.binary,
            global.mediaClass,
            global.nodeName,
            QString::number(global.id),
            global.clientId,
            global.mediaName,
        });
    }
    snapshot.clients = clientsFromPipeWireGlobals(globals, systemBinaries, skipAppNames);

    for (const pwdetail::RegistryGlobal& global : *registryGlobals)
    {
        if (global.type != QString::fromUtf8(PW_TYPE_INTERFACE_Node) ||
            !global.mediaClass.startsWith(QStringLiteral("Stream/")))
            continue;

        PipeWireNode node;
        node.id = global.id;
        node.name = global.name;
        node.binary = global.binary;
        node.mediaClass = global.mediaClass;
        node.nodeName = global.nodeName;
        node.objectSerial = global.objectSerial;
        node.clientId = global.clientId;
        node.mediaName = global.mediaName;

        bool sessionFailed = false;
        pw_thread_loop_lock(session.loop);
        const bool read = pwdetail::readNodeProps(session, global.id, &node, &sessionFailed);
        pw_thread_loop_unlock(session.loop);
        if (sessionFailed) return std::nullopt;
        if (read)
        {
            pwdetail::populateNodeOwner(*registryGlobals, node);
            snapshot.nodes.append(std::move(node));
        }
    }
    return snapshot;
}

} // namespace

// ─── libpipewire public helpers ───────────────────────────────────────────────
struct PipeWireVolumeBackend::Impl
{
    std::unique_ptr<pwdetail::PipeWireSession> session;
    uint64_t generation = 0;

    pwdetail::PipeWireSession* acquireSession()
    {
        if (session && session->healthy()) return session.get();
        session.reset();

        auto candidate = std::make_unique<pwdetail::PipeWireSession>();
        if (!candidate->connect()) return nullptr;
        session = std::move(candidate);
        ++generation;
        return session.get();
    }

    void invalidate()
    {
        session.reset();
    }
};

PipeWireVolumeBackend::PipeWireVolumeBackend() : m_impl(std::make_unique<Impl>()) {}

PipeWireVolumeBackend::~PipeWireVolumeBackend() = default;

void PipeWireVolumeBackend::close()
{
    m_impl->invalidate();
}

PipeWireSnapshot PipeWireVolumeBackend::inspect(const QSet<QString>& systemBinaries,
                                                const QSet<QString>& skipAppNames)
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        pwdetail::PipeWireSession* session = m_impl->acquireSession();
        if (!session) return {};
        if (auto snapshot = inspectSession(*session, systemBinaries, skipAppNames))
            return *snapshot;
        m_impl->invalidate();
    }
    return {};
}

QList<PipeWireClient> PipeWireVolumeBackend::listClients(const QSet<QString>& systemBinaries,
                                                         const QSet<QString>& skipAppNames)
{
    return inspect(systemBinaries, skipAppNames).clients;
}

QList<PipeWireNode> PipeWireVolumeBackend::findNodesForApp(const QString& appName,
                                                           const QStringList& matchCandidates)
{
    QStringList candidates = matchCandidates;
    if (candidates.isEmpty() && !appName.isEmpty()) candidates.append(appName);

    QList<PipeWireNode> result;
    const PipeWireSnapshot snapshot = inspect();
    for (const PipeWireNode& node : snapshot.nodes)
    {
        if (node.mediaClass.contains(QStringLiteral("Output")) &&
            pipeWireNodeMatchesApp(node, candidates))
            result.append(node);
    }
    return result;
}

std::optional<PipeWireNode>
PipeWireVolumeBackend::findNodeForApp(const QString& appName, const QStringList& matchCandidates)
{
    const QList<PipeWireNode> nodes = findNodesForApp(appName, matchCandidates);
    if (nodes.isEmpty()) return std::nullopt;
    return nodes.first();
}

std::optional<PipeWireNode> PipeWireVolumeBackend::readNode(uint32_t nodeId)
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        pwdetail::PipeWireSession* session = m_impl->acquireSession();
        if (!session) return std::nullopt;

        PipeWireNode state;
        bool sessionFailed = false;
        pw_thread_loop_lock(session->loop);
        const bool ok = pwdetail::readNodeProps(*session, nodeId, &state, &sessionFailed);
        pw_thread_loop_unlock(session->loop);
        if (ok)
        {
            pwdetail::populateNodeOwner(session->globals, state);
            return state;
        }
        if (!sessionFailed) return std::nullopt;
        m_impl->invalidate();
    }
    return std::nullopt;
}

bool PipeWireVolumeBackend::setVisibleVolume(uint32_t nodeId, double volume, uint32_t channelCount)
{
    volume = std::clamp(volume, 0.0, 1.0);
    channelCount = std::clamp(channelCount, 1U, static_cast<uint32_t>(SPA_AUDIO_MAX_CHANNELS));
    const QList<double> channels(static_cast<qsizetype>(channelCount), volume);
    return setVisibleChannelVolumes(nodeId, channels);
}

bool PipeWireVolumeBackend::setVisibleChannelVolumes(uint32_t nodeId,
                                                     const QList<double>& channelVolumes)
{
    if (channelVolumes.isEmpty() ||
        channelVolumes.size() > static_cast<qsizetype>(SPA_AUDIO_MAX_CHANNELS))
        return false;
    QList<double> sanitized = channelVolumes;
    for (double& value : sanitized)
    {
        value = std::max(0.0, value);
        value = value * value * value;
    }
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        pwdetail::PipeWireSession* session = m_impl->acquireSession();
        if (!session) return false;
        bool sessionFailed = false;
        const bool ok =
            pwdetail::setNodeProps(*session, nodeId, 1.0, sanitized, std::nullopt, &sessionFailed);
        if (ok) return true;
        if (!sessionFailed) return false;
        m_impl->invalidate();
    }
    return false;
}

bool PipeWireVolumeBackend::restoreVolumeState(uint32_t nodeId, double rawVolume,
                                               const QList<double>& channelVolumes)
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        pwdetail::PipeWireSession* session = m_impl->acquireSession();
        if (!session) return false;
        bool sessionFailed = false;
        const bool ok = pwdetail::setNodeProps(*session, nodeId, rawVolume, channelVolumes,
                                               std::nullopt, &sessionFailed);
        if (ok) return true;
        if (!sessionFailed) return false;
        m_impl->invalidate();
    }
    return false;
}

bool PipeWireVolumeBackend::setMute(uint32_t nodeId, bool muted)
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        pwdetail::PipeWireSession* session = m_impl->acquireSession();
        if (!session) return false;
        bool sessionFailed = false;
        const bool ok =
            pwdetail::setNodeProps(*session, nodeId, std::nullopt, {}, muted, &sessionFailed);
        if (ok) return true;
        if (!sessionFailed) return false;
        m_impl->invalidate();
    }
    return false;
}

uint64_t PipeWireVolumeBackend::connectionGeneration() const
{
    return m_impl->generation;
}

PipeWireSnapshot inspectPipeWire(const QSet<QString>& systemBinaries,
                                 const QSet<QString>& skipAppNames)
{
    PipeWireVolumeBackend backend;
    return backend.inspect(systemBinaries, skipAppNames);
}

QList<PipeWireClient> listPipeWireClients(const QSet<QString>& systemBinaries,
                                          const QSet<QString>& skipAppNames)
{
    PipeWireVolumeBackend backend;
    return backend.listClients(systemBinaries, skipAppNames);
}

QList<PipeWireNode> findPipeWireNodesForApp(const QString& appName,
                                            const QStringList& matchCandidates)
{
    PipeWireVolumeBackend backend;
    return backend.findNodesForApp(appName, matchCandidates);
}

std::optional<PipeWireNode> findPipeWireNodeForApp(const QString& appName,
                                                   const QStringList& matchCandidates)
{
    PipeWireVolumeBackend backend;
    return backend.findNodeForApp(appName, matchCandidates);
}

std::optional<PipeWireNode> readPipeWireNode(uint32_t nodeId)
{
    PipeWireVolumeBackend backend;
    return backend.readNode(nodeId);
}

bool setPipeWireNodeVisibleVolume(uint32_t nodeId, double volume, uint32_t channelCount)
{
    PipeWireVolumeBackend backend;
    return backend.setVisibleVolume(nodeId, volume, channelCount);
}

bool setPipeWireNodeVisibleChannelVolumes(uint32_t nodeId, const QList<double>& channelVolumes)
{
    PipeWireVolumeBackend backend;
    return backend.setVisibleChannelVolumes(nodeId, channelVolumes);
}

bool setPipeWireNodeVolume(uint32_t nodeId, double volume)
{
    PipeWireVolumeBackend backend;
    const std::optional<PipeWireNode> node = backend.readNode(nodeId);
    const uint32_t channels =
        node ? static_cast<uint32_t>(std::max(1, static_cast<int>(node->channelVolumes.size())))
             : 2U;
    return backend.setVisibleVolume(nodeId, volume, channels);
}

bool restorePipeWireNodeVolumeState(uint32_t nodeId, double rawVolume,
                                    const QList<double>& channelVolumes)
{
    PipeWireVolumeBackend backend;
    return backend.restoreVolumeState(nodeId, rawVolume, channelVolumes);
}

bool setPipeWireNodeMute(uint32_t nodeId, bool muted)
{
    PipeWireVolumeBackend backend;
    return backend.setMute(nodeId, muted);
}
