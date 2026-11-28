#include "pwsession.h"

#include <QDebug>
#include <QElapsedTimer>

#include <algorithm>
#include <cerrno>
#include <mutex>

#include <pipewire/client.h>

namespace pwdetail
{

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

PipeWireSession::PipeWireSession()
{
    ensurePipeWireInitialized();
}

PipeWireSession::~PipeWireSession()
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

bool PipeWireSession::connect()
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

    const bool ok = sync(kPwConnectTimeoutMs);
    pw_thread_loop_unlock(loop);
    if (!ok) qWarning() << "[pwutils] PipeWire registry sync timed out or failed";
    return ok;
}

bool PipeWireSession::sync(int timeoutMs)
{
    if (!loop || !core || coreError.load(std::memory_order_acquire)) return false;

    syncDone = false;
    pendingSeq = pw_core_sync(core, PW_ID_CORE, pendingSeq);

    QElapsedTimer timer;
    timer.start();
    while (!syncDone && !coreError.load(std::memory_order_acquire) && timer.elapsed() < timeoutMs)
    {
        timespec abstime{};
        const int remaining = std::max(1, timeoutMs - static_cast<int>(timer.elapsed()));
        if (pw_thread_loop_get_time(loop, &abstime, remaining * SPA_NSEC_PER_MSEC) < 0) break;
        pw_thread_loop_timed_wait_full(loop, &abstime);
    }
    return syncDone && !coreError.load(std::memory_order_acquire);
}

bool PipeWireSession::healthy() const
{
    return loop && loopStarted && context && core && registry &&
           !coreError.load(std::memory_order_acquire);
}

void PipeWireSession::onCoreDone(void* data, uint32_t id, int seq)
{
    auto* self = static_cast<PipeWireSession*>(data);
    if (id == PW_ID_CORE && seq == self->pendingSeq)
    {
        self->syncDone = true;
        pw_thread_loop_signal(self->loop, false);
    }
}

void PipeWireSession::onCoreError(void* data, uint32_t id, int, int result, const char* message)
{
    auto* self = static_cast<PipeWireSession*>(data);
    if (id == PW_ID_CORE || result == -EPIPE || result == -ECONNRESET)
        self->coreError.store(true, std::memory_order_release);
    else if (id == self->activeProxyId)
        self->activeProxyError = true;
    qWarning() << "[pwutils] PipeWire error on object" << id << "(" << result
               << ") :" << (message ? message : "unknown");
    pw_thread_loop_signal(self->loop, false);
}

void PipeWireSession::onRegistryGlobal(void* data, uint32_t id, uint32_t, const char* type,
                                       uint32_t, const spa_dict* props)
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
    self->globals.append(std::move(global));
}

void PipeWireSession::onRegistryGlobalRemove(void* data, uint32_t id)
{
    auto* self = static_cast<PipeWireSession*>(data);
    self->globals.removeIf([id](const RegistryGlobal& global) { return global.id == id; });
}

namespace
{

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
    const bool ok = session.sync(kPwSyncTimeoutMs);
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
    const bool ok = session.sync(kPwSyncTimeoutMs);
    spa_hook_remove(&nodeListener);
    pw_proxy_destroy(reinterpret_cast<pw_proxy*>(node));
    return ok;
}

} // namespace

std::optional<QList<RegistryGlobal>> snapshotObjectInfo(PipeWireSession& session)
{
    pw_thread_loop_lock(session.loop);
    if (!session.sync(kPwSyncTimeoutMs))
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

} // namespace pwdetail
