#pragma once
#include "audioapp.h"
#include "config.h"
#include "volumecontroller.h"

#include <QList>
#include <QMap>
#include <QString>
#include <cstdint>
#include <optional>
#include <utility>

#include <pulse/context.h>
#include <pulse/ext-stream-restore.h>
#include <pulse/introspect.h>
#include <pulse/volume.h>

namespace pabackend
{

constexpr int PA_CONNECT_TIMEOUT_MS = 1000;
constexpr int RECONNECT_BACKOFF_MAX_MS = 30000;

int nextReconnectBackoff(int currentMs);
bool isGenericAppName(const QString& name);
bool containsAppKey(const QMap<QString, AudioApp>& apps, const QString& name);
QList<double> channelVolumesFromPulse(const pa_cvolume& volume);
pa_cvolume pulseVolumeFromChannels(const QList<double>& channels);

// Outcome of a PA operation — used when we need to know whether libpulse
// accepted or rejected the call (e.g. moving to a stale sink name).
// pa_context completes the pa_operation either way; the rejection is
// surfaced only through the success argument of the done callback.
struct OperationOutcome
{
    bool success = false;
};

struct SinkInputInfo
{
    uint32_t index = PA_INVALID_INDEX;
    QString name;
    QString binary;
    QString mediaName;
    QString clientId;
    std::optional<uint32_t> pipeWireNodeId;
    pa_cvolume channelVolume{};
    double volume = 1.0;
    bool muted = false;

    bool matches(const QString& appName, const QList<AppAlias>& aliases = {}) const;
    QString displayName() const;
    QString targetName() const;
};

struct StreamRestoreState
{
    bool muted = false;
    pa_cvolume volume{};

    double averageVolume() const;
};

struct SinkInputListCbData
{
    QList<SinkInputInfo>* result = nullptr;
};

struct StreamRestoreCbData
{
    QString target;
    double delta = 0.0;
    double targetVolume = 0.0;
    bool absoluteVolume = false;
    bool isMute = false;
    bool targetMute = false;
    std::optional<double>* outVol = nullptr;
    std::optional<std::pair<bool, double>>* outMute = nullptr;
    double minVol = 0.0;
    double maxVol = 1.0;
};

struct StreamRestoreQueryCbData
{
    QString target;
    std::optional<StreamRestoreState>* out = nullptr;
};

struct ServerInfoCbData
{
    QString* defaultSinkName = nullptr;
};

struct ServerBackendCbData
{
    QString serverName;
};

struct SinkInfoListCbData
{
    QList<SinkInfo>* result = nullptr;
};

void operationDoneCallback(pa_context*, int, void* ud);
void operationOutcomeCallback(pa_context*, int success, void* ud);
void sinkInputListCallback(pa_context*, const pa_sink_input_info* info, int eol, void* ud);
void streamRestoreReadCallback(pa_context* ctx, const pa_ext_stream_restore_info* info, int eol,
                               void* ud);
void streamRestoreQueryCallback(pa_context*, const pa_ext_stream_restore_info* info, int eol,
                                void* ud);
void serverInfoCallback(pa_context*, const pa_server_info* info, void* ud);
void serverBackendCallback(pa_context*, const pa_server_info* info, void* ud);
void sinkInfoListCallback(pa_context*, const pa_sink_info* info, int eol, void* ud);

} // namespace pabackend
