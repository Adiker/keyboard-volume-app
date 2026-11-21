#pragma once

#include <QList>
#include <QString>

#include <atomic>
#include <cstdint>
#include <optional>

#include <pipewire/pipewire.h>
#include <spa/utils/dict.h>

namespace pwdetail
{

constexpr int kPwConnectTimeoutMs = 1000;
constexpr int kPwSyncTimeoutMs = 1000;

void ensurePipeWireInitialized();
QString dictValue(const spa_dict* dict, const char* key);

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

    PipeWireSession();
    ~PipeWireSession();

    PipeWireSession(const PipeWireSession&) = delete;
    PipeWireSession& operator=(const PipeWireSession&) = delete;

    bool connect();
    bool sync(int timeoutMs);
    bool healthy() const;

    static void onCoreDone(void* data, uint32_t id, int seq);
    static void onCoreError(void* data, uint32_t id, int seq, int res, const char* message);
    static void onRegistryGlobal(void* data, uint32_t id, uint32_t permissions, const char* type,
                                 uint32_t version, const spa_dict* props);
    static void onRegistryGlobalRemove(void* data, uint32_t id);
};

std::optional<QList<RegistryGlobal>> snapshotObjectInfo(PipeWireSession& session);

} // namespace pwdetail
