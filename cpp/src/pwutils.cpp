#include "pwutils.h"

#include <QMap>
#include <QDebug>
#include <QElapsedTimer>

#include <algorithm>
#include <mutex>

#include <pipewire/client.h>
#include <pipewire/pipewire.h>
#include <spa/param/param.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>

// ─── Filter constants ─────────────────────────────────────────────────────────
const QSet<QString> SYSTEM_BINARIES{
    QStringLiteral("wireplumber"),
    QStringLiteral("pipewire"),
    QStringLiteral("kwin_wayland"),
    QStringLiteral("plasmashell"),
    QStringLiteral("kded5"),
    QStringLiteral("kded6"),
    QStringLiteral("xdg-desktop-portal"),
    QStringLiteral("xdg-desktop-portal-kde"),
    QStringLiteral("polkit-kde-authentication-agent-1"),
    QStringLiteral("pactl"),
    QStringLiteral("pw-cli"),
    QStringLiteral("pw-dump"),
    QStringLiteral("keyboard-volume-app"),
    QStringLiteral("python3"),
    QStringLiteral("python3.14"),
    QStringLiteral("python"),
    QStringLiteral("QtWebEngineProcess"),
    QString{},
};

const QSet<QString> SKIP_APP_NAMES{
    QStringLiteral("ringrtc"),
    QStringLiteral("WEBRTC VoiceEngine"),
    QStringLiteral("Chromium input"),
};

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

struct RegistryGlobal
{
    uint32_t id = SPA_ID_INVALID;
    QString type;
    QString name;
    QString binary;
    QString mediaClass;
    QString nodeName;
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
        self->globals.append(global);
    }
};

struct NodeParamReader
{
    PipeWireSession* session = nullptr;
    double volume = 1.0;
    bool muted = false;
    bool hasProps = false;

    static void onNodeInfo(void*, const pw_node_info*) {}

    static void onNodeParam(void* data, int, uint32_t id, uint32_t, uint32_t, const spa_pod* param)
    {
        if (id != SPA_PARAM_Props || !param) return;

        auto* self = static_cast<NodeParamReader*>(data);
        float volume = 1.0f;
        bool muted = false;
        uint32_t objectId = SPA_ID_INVALID;
        const int res = spa_pod_parse_object(param, SPA_TYPE_OBJECT_Props, &objectId,
                                             SPA_PROP_volume, SPA_POD_OPT_Float(&volume),
                                             SPA_PROP_mute, SPA_POD_OPT_Bool(&muted));
        if (res >= 0)
        {
            self->volume = volume;
            self->muted = muted;
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

bool nodeMatchesApp(const RegistryGlobal& global, const QString& appName)
{
    if (global.type != QString::fromUtf8(PW_TYPE_INTERFACE_Node)) return false;
    if (global.name != appName && global.binary != appName) return false;
    return global.mediaClass.startsWith(QStringLiteral("Stream/"));
}

bool readNodeProps(PipeWireSession& session, uint32_t nodeId, double* volume, bool* muted)
{
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
        *volume = reader.volume;
        *muted = reader.muted;
    }

    spa_hook_remove(&nodeListener);
    pw_proxy_destroy(reinterpret_cast<pw_proxy*>(node));
    return ok;
}

bool setNodeProp(uint32_t nodeId, std::optional<double> volume, std::optional<bool> muted)
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

    uint8_t buffer[256];
    spa_pod_builder builder;
    spa_pod_builder_init(&builder, buffer, sizeof(buffer));

    const spa_pod* param = nullptr;
    if (volume && muted)
    {
        const float vol = static_cast<float>(*volume);
        param = static_cast<const spa_pod*>(spa_pod_builder_add_object(
            &builder, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props, SPA_PROP_volume, SPA_POD_Float(vol),
            SPA_PROP_mute, SPA_POD_Bool(*muted)));
    }
    else if (volume)
    {
        const float vol = static_cast<float>(*volume);
        param = static_cast<const spa_pod*>(spa_pod_builder_add_object(
            &builder, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props, SPA_PROP_volume, SPA_POD_Float(vol)));
    }
    else if (muted)
    {
        param = static_cast<const spa_pod*>(spa_pod_builder_add_object(
            &builder, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props, SPA_PROP_mute, SPA_POD_Bool(*muted)));
    }

    bool ok = false;
    if (param && pw_node_set_param(node, SPA_PARAM_Props, 0, param) >= 0)
        ok = session.sync(PW_SYNC_TIMEOUT_MS);

    pw_proxy_destroy(reinterpret_cast<pw_proxy*>(node));
    pw_thread_loop_unlock(session.loop);
    return ok;
}
} // namespace

// ─── Pure client filtering ────────────────────────────────────────────────────
QList<PipeWireClient> clientsFromPipeWireGlobals(const QList<PipeWireGlobalProps>& globals)
{
    QMap<QString, QString> seen;
    for (const PipeWireGlobalProps& global : globals)
    {
        const bool isClient = global.type.contains(QStringLiteral("Client"));
        const bool isStreamNode = global.type.contains(QStringLiteral("Node")) &&
                                  global.mediaClass.startsWith(QStringLiteral("Stream/"));
        if (!isClient && !isStreamNode) continue;

        QString binary = global.binary;
        if (binary.isEmpty() || SYSTEM_BINARIES.contains(binary)) continue;

        QString name = global.name;
        if (isStreamNode && !global.nodeName.isEmpty() && global.nodeName != global.name)
        {
            name = global.nodeName;
        }
        if (name.isEmpty()) name = binary;
        if (SKIP_APP_NAMES.contains(name) || name.toLower().contains(QStringLiteral("input")))
            name = binary;
        if (name.trimmed().isEmpty()) continue;

        seen[name] = binary;
    }

    QList<PipeWireClient> result;
    for (auto it = seen.begin(); it != seen.end(); ++it) result.append({it.key(), it.value()});
    return result;
}

// ─── libpipewire public helpers ───────────────────────────────────────────────
QList<PipeWireClient> listPipeWireClients()
{
    PipeWireSession session;
    if (!session.connect()) return {};
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
        });
    }
    return clientsFromPipeWireGlobals(globals);
}

std::optional<PipeWireNode> findPipeWireNodeForApp(const QString& appName)
{
    PipeWireSession session;
    if (!session.connect()) return std::nullopt;
    refreshObjectInfo(session);

    std::optional<RegistryGlobal> best;
    for (const RegistryGlobal& global : session.globals)
    {
        if (!nodeMatchesApp(global, appName)) continue;

        if (global.mediaClass.contains(QStringLiteral("Output")))
        {
            best = global;
            break;
        }
        best = global;
    }
    if (!best) return std::nullopt;

    double volume = 1.0;
    bool muted = false;

    pw_thread_loop_lock(session.loop);
    const bool ok = readNodeProps(session, best->id, &volume, &muted);
    pw_thread_loop_unlock(session.loop);
    if (!ok) return std::nullopt;

    return PipeWireNode{best->id, volume, muted};
}

bool setPipeWireNodeVolume(uint32_t nodeId, double volume)
{
    // The node id came from an earlier snapshot. If it disappeared before this
    // write, bind fails and VolumeController falls through to pending state.
    return setNodeProp(nodeId, volume, std::nullopt);
}

bool setPipeWireNodeMute(uint32_t nodeId, bool muted)
{
    // The node id came from an earlier snapshot. If it disappeared before this
    // write, bind fails and VolumeController falls through to pending state.
    return setNodeProp(nodeId, std::nullopt, muted);
}
