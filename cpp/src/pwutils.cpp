#include "pwutils.h"
#include "appaudiofilters.h"

#include <QMap>
#include <QDebug>
#include <QElapsedTimer>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <numeric>
#include <vector>

#include <pipewire/client.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/raw.h>
#include <spa/param/param.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>

namespace
{
constexpr int PW_CONNECT_TIMEOUT_MS = 1000;
constexpr int PW_SYNC_TIMEOUT_MS = 1000;

void ensurePipeWireInitialized()
{
    static std::once_flag initOnce;
    std::call_once(initOnce, []() { pw_init(nullptr, nullptr); });
}

QString dictValue(const spa_dict* dict, const char* key)
{
    if (!dict) return {};

    const char* value = spa_dict_lookup(dict, key);
    return value ? QString::fromUtf8(value) : QString{};
}

bool isGenericAppName(const QString& name)
{
    const QString lower = name.trimmed().toLower();
    return lower == QStringLiteral("chromium") || lower == QStringLiteral("chrome") ||
           lower == QStringLiteral("brave");
}

QString displayNameForClient(const QString& name, const QString& binary)
{
    if (isGenericAppName(name) && !binary.isEmpty() &&
        binary.compare(name, Qt::CaseInsensitive) != 0)
        return binary;
    return name;
}

struct RegistryGlobal
{
    uint32_t id = SPA_ID_INVALID;
    QString type;
    QString name;
    QString binary;
    QString mediaClass;
    QString nodeName;
    QString objectSerial;
    QString clientId;
    QString mediaName;
};

struct PipeWireSession
{
    pw_thread_loop* loop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;
    pw_registry* registry = nullptr;
    spa_hook coreListener{};
    spa_hook registryListener{};
    bool loopStarted = false;
    bool coreListenerAdded = false;
    bool registryListenerAdded = false;
    QList<RegistryGlobal> globals;
    int pendingSeq = 0;
    bool syncDone = false;
    bool coreError = false;

    PipeWireSession()
    {
        ensurePipeWireInitialized();
    }

    ~PipeWireSession()
    {
        if (loop && loopStarted) pw_thread_loop_lock(loop);

        if (registryListenerAdded) spa_hook_remove(&registryListener);
        if (coreListenerAdded) spa_hook_remove(&coreListener);
        if (registry)
        {
            pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry));
            registry = nullptr;
        }
        if (core)
        {
            pw_core_disconnect(core);
            core = nullptr;
        }
        if (context)
        {
            pw_context_destroy(context);
            context = nullptr;
        }

        if (loop)
        {
            if (loopStarted)
            {
                pw_thread_loop_unlock(loop);
                pw_thread_loop_stop(loop);
            }
            pw_thread_loop_destroy(loop);
            loop = nullptr;
        }
    }

    bool connect()
    {
        loop = pw_thread_loop_new("keyboard-volume-app-pwutils", nullptr);
        if (!loop)
        {
            qWarning() << "[pwutils] Failed to create PipeWire thread loop";
            return false;
        }
        if (pw_thread_loop_start(loop) < 0)
        {
            qWarning() << "[pwutils] Failed to start PipeWire thread loop";
            return false;
        }
        loopStarted = true;

        pw_thread_loop_lock(loop);
        context = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
        if (!context)
        {
            qWarning() << "[pwutils] Failed to create PipeWire context";
            pw_thread_loop_unlock(loop);
            return false;
        }

        core = pw_context_connect(context, nullptr, 0);
        if (!core)
        {
            qWarning() << "[pwutils] Failed to connect to PipeWire";
            pw_thread_loop_unlock(loop);
            return false;
        }

        static const pw_core_events coreEvents{
            PW_VERSION_CORE_EVENTS,
            nullptr,
            &PipeWireSession::onCoreDone,
            nullptr,
            &PipeWireSession::onCoreError,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
        };
        pw_core_add_listener(core, &coreListener, &coreEvents, this);
        coreListenerAdded = true;

        registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
        if (!registry)
        {
            qWarning() << "[pwutils] Failed to get PipeWire registry";
            pw_thread_loop_unlock(loop);
            return false;
        }

        static const pw_registry_events registryEvents{
            PW_VERSION_REGISTRY_EVENTS,
            &PipeWireSession::onRegistryGlobal,
            nullptr,
        };
        pw_registry_add_listener(registry, &registryListener, &registryEvents, this);
        registryListenerAdded = true;

        const bool ok = sync(PW_CONNECT_TIMEOUT_MS);
        pw_thread_loop_unlock(loop);
        if (!ok) qWarning() << "[pwutils] PipeWire registry sync timed out or failed";
        return ok;
    }

    bool sync(int timeoutMs)
    {
        if (!loop || !core || coreError) return false;

        syncDone = false;
        pendingSeq = pw_core_sync(core, PW_ID_CORE, pendingSeq);

        QElapsedTimer timer;
        timer.start();
        while (!syncDone && !coreError && timer.elapsed() < timeoutMs)
        {
            timespec abstime{};
            const int remaining = std::max(1, timeoutMs - static_cast<int>(timer.elapsed()));
            if (pw_thread_loop_get_time(loop, &abstime, remaining * SPA_NSEC_PER_MSEC) < 0) break;
            pw_thread_loop_timed_wait_full(loop, &abstime);
        }
        return syncDone && !coreError;
    }

    static void onCoreDone(void* data, uint32_t id, int seq)
    {
        auto* self = static_cast<PipeWireSession*>(data);
        if (id == PW_ID_CORE && seq == self->pendingSeq)
        {
            self->syncDone = true;
            pw_thread_loop_signal(self->loop, false);
        }
    }

    static void onCoreError(void* data, uint32_t, int, int, const char* message)
    {
        auto* self = static_cast<PipeWireSession*>(data);
        self->coreError = true;
        qWarning() << "[pwutils] PipeWire error:" << (message ? message : "unknown");
        pw_thread_loop_signal(self->loop, false);
    }

    static void onRegistryGlobal(void* data, uint32_t id, uint32_t, const char* type, uint32_t,
                                 const spa_dict* props)
    {
        auto* self = static_cast<PipeWireSession*>(data);
        RegistryGlobal global;
        global.id = id;
        global.type = QString::fromUtf8(type ? type : "");
        global.name = dictValue(props, "application.name");
        global.binary = dictValue(props, "application.process.binary");
        global.mediaClass = dictValue(props, "media.class");
        global.objectSerial = dictValue(props, "object.serial");
        global.clientId = dictValue(props, "client.id");
        global.mediaName = dictValue(props, "media.name");
        self->globals.append(global);
    }
};

struct NodeParamReader
{
    PipeWireSession* session = nullptr;
    double rawVolume = 1.0;
    QList<double> channelVolumes;
    bool muted = false;
    bool hasProps = false;

    static void onNodeInfo(void*, const pw_node_info*) {}

    static void onNodeParam(void* data, int, uint32_t id, uint32_t, uint32_t, const spa_pod* param)
    {
        if (id != SPA_PARAM_Props || !param) return;

        auto* self = static_cast<NodeParamReader*>(data);
        float volume = 1.0f;
        bool muted = false;
        uint32_t channelValueSize = 0;
        uint32_t channelValueType = SPA_TYPE_None;
        uint32_t channelCount = 0;
        const void* channelData = nullptr;
        uint32_t objectId = SPA_ID_INVALID;
        const int res = spa_pod_parse_object(
            param, SPA_TYPE_OBJECT_Props, &objectId, SPA_PROP_volume, SPA_POD_OPT_Float(&volume),
            SPA_PROP_mute, SPA_POD_OPT_Bool(&muted), SPA_PROP_channelVolumes,
            SPA_POD_OPT_Array(&channelValueSize, &channelValueType, &channelCount, &channelData));
        if (res >= 0)
        {
            self->rawVolume = volume;
            self->muted = muted;
            self->channelVolumes.clear();
            if (channelData && channelValueSize == sizeof(float) &&
                channelValueType == SPA_TYPE_Float)
            {
                const auto* channels = static_cast<const float*>(channelData);
                self->channelVolumes.reserve(static_cast<qsizetype>(channelCount));
                for (uint32_t i = 0; i < channelCount; ++i)
                    self->channelVolumes.append(channels[i]);
            }
            self->hasProps = true;
        }
    }
};

struct ClientInfoReader
{
    RegistryGlobal* global = nullptr;

    static void onClientInfo(void* data, const pw_client_info* info)
    {
        if (!info || !info->props) return;

        auto* self = static_cast<ClientInfoReader*>(data);
        self->global->name = dictValue(info->props, "application.name");
        self->global->binary = dictValue(info->props, "application.process.binary");
    }

    static void onClientPermissions(void*, uint32_t, uint32_t, const pw_permission*) {}
};

struct NodeInfoReader
{
    RegistryGlobal* global = nullptr;

    static void onNodeInfo(void* data, const pw_node_info* info)
    {
        if (!info || !info->props) return;

        auto* self = static_cast<NodeInfoReader*>(data);
        self->global->name = dictValue(info->props, "application.name");
        self->global->binary = dictValue(info->props, "application.process.binary");
        self->global->mediaClass = dictValue(info->props, "media.class");
        self->global->nodeName = dictValue(info->props, "node.name");
        self->global->objectSerial = dictValue(info->props, "object.serial");
        self->global->clientId = dictValue(info->props, "client.id");
        self->global->mediaName = dictValue(info->props, "media.name");
    }

    static void onNodeParam(void*, int, uint32_t, uint32_t, uint32_t, const spa_pod*) {}
};

void refreshClientInfo(PipeWireSession& session, RegistryGlobal& global)
{
    auto* client = static_cast<pw_client*>(pw_registry_bind(
        session.registry, global.id, PW_TYPE_INTERFACE_Client, PW_VERSION_CLIENT, 0));
    if (!client) return;

    spa_hook clientListener{};
    ClientInfoReader reader{&global};
    static const pw_client_events clientEvents{
        PW_VERSION_CLIENT_EVENTS,
        &ClientInfoReader::onClientInfo,
        &ClientInfoReader::onClientPermissions,
    };

    pw_client_add_listener(client, &clientListener, &clientEvents, &reader);
    session.sync(PW_SYNC_TIMEOUT_MS);
    spa_hook_remove(&clientListener);
    pw_proxy_destroy(reinterpret_cast<pw_proxy*>(client));
}

void refreshNodeInfo(PipeWireSession& session, RegistryGlobal& global)
{
    auto* node = static_cast<pw_node*>(
        pw_registry_bind(session.registry, global.id, PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0));
    if (!node) return;

    spa_hook nodeListener{};
    NodeInfoReader reader{&global};
    static const pw_node_events nodeEvents{
        PW_VERSION_NODE_EVENTS,
        &NodeInfoReader::onNodeInfo,
        &NodeInfoReader::onNodeParam,
    };

    pw_node_add_listener(node, &nodeListener, &nodeEvents, &reader);
    session.sync(PW_SYNC_TIMEOUT_MS);
    spa_hook_remove(&nodeListener);
    pw_proxy_destroy(reinterpret_cast<pw_proxy*>(node));
}

void refreshObjectInfo(PipeWireSession& session)
{
    pw_thread_loop_lock(session.loop);
    for (RegistryGlobal& global : session.globals)
    {
        if (global.type == QString::fromUtf8(PW_TYPE_INTERFACE_Client))
            refreshClientInfo(session, global);
        else if (global.type == QString::fromUtf8(PW_TYPE_INTERFACE_Node))
            refreshNodeInfo(session, global);
    }
    pw_thread_loop_unlock(session.loop);
}

bool nodeMatchesApp(const RegistryGlobal& global, const QStringList& candidates)
{
    if (global.type != QString::fromUtf8(PW_TYPE_INTERFACE_Node)) return false;
    if (!global.mediaClass.startsWith(QStringLiteral("Stream/"))) return false;

    for (const QString& candidate : candidates)
    {
        if (candidate.isEmpty()) continue;
        if (global.name.compare(candidate, Qt::CaseInsensitive) == 0 ||
            global.binary.compare(candidate, Qt::CaseInsensitive) == 0 ||
            global.nodeName.compare(candidate, Qt::CaseInsensitive) == 0 ||
            global.mediaName.compare(candidate, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

bool readNodeProps(PipeWireSession& session, uint32_t nodeId, PipeWireNode* state)
{
    if (!state) return false;
    auto* node = static_cast<pw_node*>(
        pw_registry_bind(session.registry, nodeId, PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0));
    if (!node) return false;

    spa_hook nodeListener{};
    NodeParamReader reader;
    reader.session = &session;
    static const pw_node_events nodeEvents{
        PW_VERSION_NODE_EVENTS,
        &NodeParamReader::onNodeInfo,
        &NodeParamReader::onNodeParam,
    };

    pw_node_add_listener(node, &nodeListener, &nodeEvents, &reader);
    pw_node_enum_params(node, 0, SPA_PARAM_Props, 0, 1, nullptr);
    const bool ok = session.sync(PW_SYNC_TIMEOUT_MS) && reader.hasProps;

    if (ok)
    {
        state->id = nodeId;
        state->rawVolume = reader.rawVolume;
        state->channelVolumes = reader.channelVolumes;
        state->muted = reader.muted;
    }

    spa_hook_remove(&nodeListener);
    pw_proxy_destroy(reinterpret_cast<pw_proxy*>(node));
    return ok;
}

bool setNodeProps(uint32_t nodeId, std::optional<double> rawVolume,
                  const QList<double>& channelVolumes, std::optional<bool> muted)
{
    // This fallback is for paused/idle apps, so a short-lived session keeps the
    // implementation simple. Promote this to a persistent worker-owned session
    // before using it in any hot path.
    PipeWireSession session;
    if (!session.connect()) return false;

    pw_thread_loop_lock(session.loop);
    auto* node = static_cast<pw_node*>(
        pw_registry_bind(session.registry, nodeId, PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0));
    if (!node)
    {
        pw_thread_loop_unlock(session.loop);
        return false;
    }

    uint8_t buffer[4096];
    spa_pod_builder builder{};
    spa_pod_builder_init(&builder, buffer, sizeof(buffer));

    spa_pod_frame frame{};
    spa_pod_builder_push_object(&builder, &frame, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
    if (rawVolume)
    {
        const float value = static_cast<float>(*rawVolume);
        spa_pod_builder_add(&builder, SPA_PROP_volume, SPA_POD_Float(value), 0);
    }
    std::vector<float> channels;
    channels.reserve(static_cast<size_t>(channelVolumes.size()));
    for (double value : channelVolumes) channels.push_back(static_cast<float>(value));
    if (!channels.empty())
    {
        spa_pod_builder_add(&builder, SPA_PROP_channelVolumes,
                            SPA_POD_Array(sizeof(float), SPA_TYPE_Float,
                                          static_cast<uint32_t>(channels.size()), channels.data()),
                            0);
    }
    if (muted) spa_pod_builder_add(&builder, SPA_PROP_mute, SPA_POD_Bool(*muted), 0);
    const spa_pod* param = static_cast<const spa_pod*>(spa_pod_builder_pop(&builder, &frame));

    bool ok = false;
    if (param && pw_node_set_param(node, SPA_PARAM_Props, 0, param) >= 0)
        ok = session.sync(PW_SYNC_TIMEOUT_MS);

    pw_proxy_destroy(reinterpret_cast<pw_proxy*>(node));
    pw_thread_loop_unlock(session.loop);
    return ok;
}
} // namespace

double PipeWireNode::visibleVolume() const
{
    if (channelVolumes.isEmpty()) return std::cbrt(std::max(0.0, rawVolume));
    double total = 0.0;
    for (double value : channelVolumes) total += std::cbrt(std::max(0.0, value));
    return total / static_cast<double>(channelVolumes.size());
}

double PipeWireNode::effectiveVolume() const
{
    if (channelVolumes.isEmpty()) return std::cbrt(std::max(0.0, rawVolume));
    double total = 0.0;
    for (double value : channelVolumes) total += std::cbrt(std::max(0.0, rawVolume * value));
    return total / static_cast<double>(channelVolumes.size());
}

bool PipeWireNode::hasHiddenVolumeMultiplier() const
{
    return !channelVolumes.isEmpty() && std::abs(rawVolume - 1.0) > 0.0001;
}

// ─── Pure client filtering ────────────────────────────────────────────────────
QList<PipeWireClient> clientsFromPipeWireGlobals(const QList<PipeWireGlobalProps>& globals,
                                                 const QSet<QString>& systemBinaries,
                                                 const QSet<QString>& skipAppNames)
{
    QMap<QString, PipeWireClient> seen;
    QSet<QString> clientNames;
    QSet<QString> clientBinaries;
    QMap<QString, QString> clientNameByBinary;
    QMap<QString, QString> clientNameById;
    for (const PipeWireGlobalProps& global : globals)
    {
        if (!global.type.contains(QStringLiteral("Client"))) continue;

        QString binary = global.binary;
        if (binary.isEmpty() || systemBinaries.contains(binary)) continue;

        QString name = global.name;
        if (name.isEmpty()) name = binary;
        if (skipAppNames.contains(name) || name.toLower().contains(QStringLiteral("input")))
            name = binary;
        if (name.trimmed().isEmpty()) continue;

        const QString displayName = displayNameForClient(name, binary);
        seen[displayName] = PipeWireClient{displayName, binary, global.objectId};
        clientNames.insert(displayName);
        clientBinaries.insert(binary);
        clientNameByBinary[binary] = displayName;
        if (!global.objectId.isEmpty()) clientNameById[global.objectId] = displayName;
    }

    for (const PipeWireGlobalProps& global : globals)
    {
        const bool isStreamNode = global.type.contains(QStringLiteral("Node")) &&
                                  global.mediaClass.startsWith(QStringLiteral("Stream/"));
        if (!isStreamNode) continue;

        const QString ownerBinary = global.binary;
        if (ownerBinary.isEmpty() || systemBinaries.contains(ownerBinary)) continue;

        QString target = global.nodeName;
        if (target.isEmpty()) target = global.name;
        if (target.isEmpty()) target = ownerBinary;
        if (target.compare(global.name, Qt::CaseInsensitive) == 0 &&
            isGenericAppName(global.name) && !ownerBinary.isEmpty())
            target = ownerBinary;
        if (skipAppNames.contains(target) || target.toLower().contains(QStringLiteral("input")))
            target = ownerBinary;
        if (target.trimmed().isEmpty()) continue;

        QString ownerDisplay = clientNameById.value(global.clientId);
        if (ownerDisplay.isEmpty()) ownerDisplay = clientNameByBinary.value(ownerBinary);
        if (!ownerDisplay.isEmpty())
        {
            const QString ownerId = global.clientId;
            seen[ownerDisplay] = PipeWireClient{ownerDisplay, target, ownerId};
            continue;
        }

        QString displayName = global.name;
        if (displayName.isEmpty()) displayName = target;
        if (skipAppNames.contains(displayName) ||
            displayName.toLower().contains(QStringLiteral("input")))
            displayName = target;
        if (displayName.trimmed().isEmpty()) continue;
        if (clientNames.contains(displayName) || seen.contains(displayName)) continue;

        seen[displayName] = PipeWireClient{displayName, target, global.clientId};
    }

    QList<PipeWireClient> result;
    for (auto it = seen.begin(); it != seen.end(); ++it) result.append(it.value());
    return result;
}

// ─── libpipewire public helpers ───────────────────────────────────────────────
PipeWireSnapshot inspectPipeWire(const QSet<QString>& systemBinaries,
                                 const QSet<QString>& skipAppNames)
{
    PipeWireSnapshot snapshot;
    PipeWireSession session;
    if (!session.connect()) return snapshot;
    refreshObjectInfo(session);

    QList<PipeWireGlobalProps> globals;
    for (const RegistryGlobal& global : session.globals)
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

    pw_thread_loop_lock(session.loop);
    for (const RegistryGlobal& global : std::as_const(session.globals))
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
        if (readNodeProps(session, global.id, &node)) snapshot.nodes.append(std::move(node));
    }
    pw_thread_loop_unlock(session.loop);
    return snapshot;
}

QList<PipeWireClient> listPipeWireClients(const QSet<QString>& systemBinaries,
                                          const QSet<QString>& skipAppNames)
{
    return inspectPipeWire(systemBinaries, skipAppNames).clients;
}

QList<PipeWireNode> findPipeWireNodesForApp(const QString& appName,
                                            const QStringList& matchCandidates)
{
    QStringList candidates = matchCandidates;
    if (candidates.isEmpty() && !appName.isEmpty()) candidates.append(appName);

    QList<PipeWireNode> result;
    const PipeWireSnapshot snapshot = inspectPipeWire();
    for (const PipeWireNode& node : snapshot.nodes)
    {
        RegistryGlobal global;
        global.id = node.id;
        global.type = QString::fromUtf8(PW_TYPE_INTERFACE_Node);
        global.name = node.name;
        global.binary = node.binary;
        global.mediaClass = node.mediaClass;
        global.nodeName = node.nodeName;
        global.objectSerial = node.objectSerial;
        global.clientId = node.clientId;
        global.mediaName = node.mediaName;
        if (node.mediaClass.contains(QStringLiteral("Output")) &&
            nodeMatchesApp(global, candidates))
            result.append(node);
    }
    return result;
}

std::optional<PipeWireNode> findPipeWireNodeForApp(const QString& appName,
                                                   const QStringList& matchCandidates)
{
    const QList<PipeWireNode> nodes = findPipeWireNodesForApp(appName, matchCandidates);
    if (nodes.isEmpty()) return std::nullopt;
    return nodes.first();
}

std::optional<PipeWireNode> readPipeWireNode(uint32_t nodeId)
{
    PipeWireSession session;
    if (!session.connect()) return std::nullopt;
    PipeWireNode state;
    pw_thread_loop_lock(session.loop);
    const bool ok = readNodeProps(session, nodeId, &state);
    pw_thread_loop_unlock(session.loop);
    if (!ok) return std::nullopt;
    return state;
}

bool setPipeWireNodeVisibleVolume(uint32_t nodeId, double volume, uint32_t channelCount)
{
    volume = std::clamp(volume, 0.0, 1.0);
    channelCount = std::clamp(channelCount, 1U, static_cast<uint32_t>(SPA_AUDIO_MAX_CHANNELS));
    QList<double> channels(static_cast<qsizetype>(channelCount), volume);
    return setPipeWireNodeVisibleChannelVolumes(nodeId, channels);
}

bool setPipeWireNodeVisibleChannelVolumes(uint32_t nodeId, const QList<double>& channelVolumes)
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
    return setNodeProps(nodeId, 1.0, sanitized, std::nullopt);
}

bool setPipeWireNodeVolume(uint32_t nodeId, double volume)
{
    const std::optional<PipeWireNode> node = readPipeWireNode(nodeId);
    const uint32_t channels =
        node ? static_cast<uint32_t>(std::max(1, static_cast<int>(node->channelVolumes.size())))
             : 2U;
    return setPipeWireNodeVisibleVolume(nodeId, volume, channels);
}

bool restorePipeWireNodeVolumeState(uint32_t nodeId, double rawVolume,
                                    const QList<double>& channelVolumes)
{
    return setNodeProps(nodeId, rawVolume, channelVolumes, std::nullopt);
}

bool setPipeWireNodeMute(uint32_t nodeId, bool muted)
{
    // The node id came from an earlier snapshot. If it disappeared before this
    // write, bind fails and VolumeController falls through to pending state.
    return setNodeProps(nodeId, std::nullopt, {}, muted);
}
