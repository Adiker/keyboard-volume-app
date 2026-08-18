#include "pwutils.h"
#include "appaudiofilters.h"

#include <QMap>
#include <QDebug>
#include <QElapsedTimer>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
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
    std::atomic_bool coreError{false};

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
            &PipeWireSession::onRegistryGlobalRemove,
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
        if (!loop || !core || coreError.load(std::memory_order_acquire)) return false;

        syncDone = false;
        pendingSeq = pw_core_sync(core, PW_ID_CORE, pendingSeq);

        QElapsedTimer timer;
        timer.start();
        while (!syncDone && !coreError.load(std::memory_order_acquire) &&
               timer.elapsed() < timeoutMs)
        {
            timespec abstime{};
            const int remaining = std::max(1, timeoutMs - static_cast<int>(timer.elapsed()));
            if (pw_thread_loop_get_time(loop, &abstime, remaining * SPA_NSEC_PER_MSEC) < 0) break;
            pw_thread_loop_timed_wait_full(loop, &abstime);
        }
        return syncDone && !coreError.load(std::memory_order_acquire);
    }

    bool healthy() const
    {
        return loop && loopStarted && context && core && registry &&
               !coreError.load(std::memory_order_acquire);
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
        self->coreError.store(true, std::memory_order_release);
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
        self->globals.removeIf([id](const RegistryGlobal& existing) { return existing.id == id; });
        self->globals.append(global);
    }

    static void onRegistryGlobalRemove(void* data, uint32_t id)
    {
        auto* self = static_cast<PipeWireSession*>(data);
        self->globals.removeIf([id](const RegistryGlobal& global) { return global.id == id; });
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

bool refreshClientInfo(PipeWireSession& session, RegistryGlobal& global)
{
    auto* client = static_cast<pw_client*>(pw_registry_bind(
        session.registry, global.id, PW_TYPE_INTERFACE_Client, PW_VERSION_CLIENT, 0));
    if (!client) return true; // The object may have disappeared after the registry snapshot.

    spa_hook clientListener{};
    ClientInfoReader reader{&global};
    static const pw_client_events clientEvents{
        PW_VERSION_CLIENT_EVENTS,
        &ClientInfoReader::onClientInfo,
        &ClientInfoReader::onClientPermissions,
    };

    pw_client_add_listener(client, &clientListener, &clientEvents, &reader);
    const bool ok = session.sync(PW_SYNC_TIMEOUT_MS);
    spa_hook_remove(&clientListener);
    pw_proxy_destroy(reinterpret_cast<pw_proxy*>(client));
    return ok;
}

bool refreshNodeInfo(PipeWireSession& session, RegistryGlobal& global)
{
    auto* node = static_cast<pw_node*>(
        pw_registry_bind(session.registry, global.id, PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0));
    if (!node) return true; // The object may have disappeared after the registry snapshot.

    spa_hook nodeListener{};
    NodeInfoReader reader{&global};
    static const pw_node_events nodeEvents{
        PW_VERSION_NODE_EVENTS,
        &NodeInfoReader::onNodeInfo,
        &NodeInfoReader::onNodeParam,
    };

    pw_node_add_listener(node, &nodeListener, &nodeEvents, &reader);
    const bool ok = session.sync(PW_SYNC_TIMEOUT_MS);
    spa_hook_remove(&nodeListener);
    pw_proxy_destroy(reinterpret_cast<pw_proxy*>(node));
    return ok;
}

std::optional<QList<RegistryGlobal>> snapshotObjectInfo(PipeWireSession& session)
{
    pw_thread_loop_lock(session.loop);
    if (!session.sync(PW_SYNC_TIMEOUT_MS))
    {
        pw_thread_loop_unlock(session.loop);
        return std::nullopt;
    }

    // Registry callbacks can add/remove globals while a later info sync waits.
    // Refresh a stable local copy so those callbacks cannot invalidate iteration.
    QList<RegistryGlobal> globals = session.globals;
    bool ok = true;
    for (RegistryGlobal& global : globals)
    {
        if (global.type == QString::fromUtf8(PW_TYPE_INTERFACE_Client))
            ok = refreshClientInfo(session, global) && ok;
        else if (global.type == QString::fromUtf8(PW_TYPE_INTERFACE_Node))
            ok = refreshNodeInfo(session, global) && ok;
        if (!session.healthy())
        {
            ok = false;
            break;
        }
    }
    pw_thread_loop_unlock(session.loop);
    if (!ok) return std::nullopt;
    return globals;
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

bool readNodeProps(PipeWireSession& session, uint32_t nodeId, PipeWireNode* state,
                   bool* sessionFailed = nullptr)
{
    if (sessionFailed) *sessionFailed = false;
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
    const bool synced = session.sync(PW_SYNC_TIMEOUT_MS);
    if (!synced && sessionFailed) *sessionFailed = true;
    const bool ok = synced && reader.hasProps;

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

bool setNodeProps(PipeWireSession& session, uint32_t nodeId, std::optional<double> rawVolume,
                  const QList<double>& channelVolumes, std::optional<bool> muted,
                  bool* sessionFailed = nullptr)
{
    if (sessionFailed) *sessionFailed = false;
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
    {
        ok = session.sync(PW_SYNC_TIMEOUT_MS);
        if (!ok && sessionFailed) *sessionFailed = true;
    }

    pw_proxy_destroy(reinterpret_cast<pw_proxy*>(node));
    pw_thread_loop_unlock(session.loop);
    return ok;
}

std::optional<PipeWireSnapshot> inspectSession(PipeWireSession& session,
                                               const QSet<QString>& systemBinaries,
                                               const QSet<QString>& skipAppNames)
{
    const auto registryGlobals = snapshotObjectInfo(session);
    if (!registryGlobals) return std::nullopt;

    PipeWireSnapshot snapshot;
    QList<PipeWireGlobalProps> globals;
    for (const RegistryGlobal& global : *registryGlobals)
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

    for (const RegistryGlobal& global : *registryGlobals)
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
        const bool read = readNodeProps(session, global.id, &node, &sessionFailed);
        pw_thread_loop_unlock(session.loop);
        if (sessionFailed) return std::nullopt;
        if (read) snapshot.nodes.append(std::move(node));
    }
    return snapshot;
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
struct PipeWireVolumeBackend::Impl
{
    std::unique_ptr<PipeWireSession> session;
    uint64_t generation = 0;

    PipeWireSession* acquireSession()
    {
        if (session && session->healthy()) return session.get();
        session.reset();

        auto candidate = std::make_unique<PipeWireSession>();
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

PipeWireSnapshot PipeWireVolumeBackend::inspect(const QSet<QString>& systemBinaries,
                                                const QSet<QString>& skipAppNames)
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        PipeWireSession* session = m_impl->acquireSession();
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
        PipeWireSession* session = m_impl->acquireSession();
        if (!session) return std::nullopt;

        PipeWireNode state;
        bool sessionFailed = false;
        pw_thread_loop_lock(session->loop);
        const bool ok = readNodeProps(*session, nodeId, &state, &sessionFailed);
        pw_thread_loop_unlock(session->loop);
        if (ok) return state;
        if (!sessionFailed) return std::nullopt;
        m_impl->invalidate();
    }
    return std::nullopt;
}

bool PipeWireVolumeBackend::setVisibleVolume(uint32_t nodeId, double volume, uint32_t channelCount)
{
    volume = std::clamp(volume, 0.0, 1.0);
    channelCount = std::clamp(channelCount, 1U, static_cast<uint32_t>(SPA_AUDIO_MAX_CHANNELS));
    QList<double> channels(static_cast<qsizetype>(channelCount), volume);
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
        PipeWireSession* session = m_impl->acquireSession();
        if (!session) return false;
        bool sessionFailed = false;
        const bool ok =
            setNodeProps(*session, nodeId, 1.0, sanitized, std::nullopt, &sessionFailed);
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
        PipeWireSession* session = m_impl->acquireSession();
        if (!session) return false;
        bool sessionFailed = false;
        const bool ok =
            setNodeProps(*session, nodeId, rawVolume, channelVolumes, std::nullopt, &sessionFailed);
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
        PipeWireSession* session = m_impl->acquireSession();
        if (!session) return false;
        bool sessionFailed = false;
        const bool ok = setNodeProps(*session, nodeId, std::nullopt, {}, muted, &sessionFailed);
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
