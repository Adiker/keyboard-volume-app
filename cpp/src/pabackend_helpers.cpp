#include "pabackend_helpers.h"
#include "appmatcher.h"

#include <algorithm>

namespace pabackend
{

int nextReconnectBackoff(int currentMs)
{
    if (currentMs <= 0) return 500;
    return std::min(currentMs * 2, RECONNECT_BACKOFF_MAX_MS);
}

bool isGenericAppName(const QString& name)
{
    const QString lower = name.trimmed().toLower();
    return lower == QStringLiteral("chromium") || lower == QStringLiteral("chrome") ||
           lower == QStringLiteral("brave");
}

bool containsAppKey(const QMap<QString, AudioApp>& apps, const QString& name)
{
    for (auto it = apps.constBegin(); it != apps.constEnd(); ++it)
    {
        if (it.key().compare(name, Qt::CaseInsensitive) == 0 ||
            it.value().binary.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QList<double> channelVolumesFromPulse(const pa_cvolume& volume)
{
    QList<double> result;
    result.reserve(volume.channels);
    for (uint8_t i = 0; i < volume.channels; ++i)
        result.append(static_cast<double>(volume.values[i]) / PA_VOLUME_NORM);
    return result;
}

pa_cvolume pulseVolumeFromChannels(const QList<double>& channels)
{
    pa_cvolume result;
    result.channels =
        static_cast<uint8_t>(std::clamp<qsizetype>(channels.size(), 1, PA_CHANNELS_MAX));
    for (uint8_t i = 0; i < result.channels; ++i)
    {
        const double value = std::max(0.0, channels[static_cast<qsizetype>(i)]);
        result.values[i] = static_cast<pa_volume_t>(
            std::min(value * PA_VOLUME_NORM, static_cast<double>(PA_VOLUME_MAX)));
    }
    return result;
}

bool SinkInputInfo::matches(const QString& appName, const QList<AppAlias>& aliases) const
{
    return appNameMatchesFields(appName, name, binary, mediaName, aliases);
}

QString SinkInputInfo::displayName() const
{
    if (isGenericAppName(name) && !binary.isEmpty() &&
        binary.compare(name, Qt::CaseInsensitive) != 0)
        return binary;
    return name;
}

QString SinkInputInfo::targetName() const
{
    if (!binary.isEmpty()) return binary;
    return name;
}

double StreamRestoreState::averageVolume() const
{
    return static_cast<double>(pa_cvolume_avg(&volume)) / PA_VOLUME_NORM;
}

void operationDoneCallback(pa_context*, int, void* ud)
{
    Q_UNUSED(ud);
}

void operationOutcomeCallback(pa_context*, int success, void* ud)
{
    auto* o = static_cast<OperationOutcome*>(ud);
    if (o) o->success = (success != 0);
}

void sinkInputListCallback(pa_context*, const pa_sink_input_info* info, int eol, void* ud)
{
    auto* d = static_cast<SinkInputListCbData*>(ud);
    if (eol || !info || !d || !d->result) return;

    SinkInputInfo si;
    si.index = info->index;
    const char* appName = pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_NAME);
    const char* mediaName = pa_proplist_gets(info->proplist, PA_PROP_MEDIA_NAME);
    const char* processBinary =
        pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_PROCESS_BINARY);
    const char* clientId = pa_proplist_gets(info->proplist, "client.id");
    const char* objectId = pa_proplist_gets(info->proplist, "object.id");

    si.name = QString::fromUtf8(appName ? appName : (mediaName ? mediaName : "Unknown"));
    si.binary = QString::fromUtf8(processBinary ? processBinary : "");
    si.mediaName = QString::fromUtf8(mediaName ? mediaName : "");
    si.clientId = QString::fromUtf8(clientId ? clientId : "");
    if (objectId)
    {
        bool ok = false;
        const uint32_t id = QString::fromUtf8(objectId).toUInt(&ok);
        if (ok) si.pipeWireNodeId = id;
    }
    si.channelVolume = info->volume;
    si.volume = static_cast<double>(pa_cvolume_avg(&info->volume)) / PA_VOLUME_NORM;
    si.muted = info->mute != 0;
    d->result->append(si);
}

void streamRestoreReadCallback(pa_context* ctx, const pa_ext_stream_restore_info* info, int eol,
                               void* ud)
{
    auto* d = static_cast<StreamRestoreCbData*>(ud);
    if (eol || !d) return;
    if (!info || QString::fromUtf8(info->name) != d->target) return;

    double vol = static_cast<double>(pa_cvolume_avg(&info->volume)) / PA_VOLUME_NORM;
    if (!d->isMute)
    {
        double newVol = d->absoluteVolume ? std::clamp(d->targetVolume, d->minVol, d->maxVol)
                                          : std::clamp(vol + d->delta, d->minVol, d->maxVol);
        pa_ext_stream_restore_info out = *info;
        const uint8_t channels = std::max<uint8_t>(1, info->volume.channels);
        pa_cvolume_set(const_cast<pa_cvolume*>(&out.volume), channels,
                       static_cast<pa_volume_t>(newVol * PA_VOLUME_NORM));
        pa_operation* op = pa_ext_stream_restore_write(ctx, PA_UPDATE_REPLACE, &out, 1, 1,
                                                       operationDoneCallback, nullptr);
        if (op) pa_operation_unref(op);
        if (d->outVol) *d->outVol = newVol;
    }
    else
    {
        bool newMuted = d->absoluteVolume ? d->targetMute : !info->mute;
        pa_ext_stream_restore_info out = *info;
        out.mute = newMuted ? 1 : 0;
        pa_operation* op = pa_ext_stream_restore_write(ctx, PA_UPDATE_REPLACE, &out, 1, 1,
                                                       operationDoneCallback, nullptr);
        if (op) pa_operation_unref(op);
        if (d->outMute) *d->outMute = std::make_pair(newMuted, vol);
    }
}

void streamRestoreQueryCallback(pa_context*, const pa_ext_stream_restore_info* info, int eol,
                                void* ud)
{
    auto* d = static_cast<StreamRestoreQueryCbData*>(ud);
    if (eol || !d || !d->out) return;
    if (!info || QString::fromUtf8(info->name) != d->target) return;

    *d->out = StreamRestoreState{info->mute != 0, info->volume};
}

void serverInfoCallback(pa_context*, const pa_server_info* info, void* ud)
{
    auto* d = static_cast<ServerInfoCbData*>(ud);
    if (!info || !d || !d->defaultSinkName) return;
    if (info->default_sink_name) *d->defaultSinkName = QString::fromUtf8(info->default_sink_name);
}

void serverBackendCallback(pa_context*, const pa_server_info* info, void* ud)
{
    auto* d = static_cast<ServerBackendCbData*>(ud);
    if (!info || !d) return;
    d->serverName = QString::fromUtf8(info->server_name ? info->server_name : "");
}

void sinkInfoListCallback(pa_context*, const pa_sink_info* info, int eol, void* ud)
{
    auto* d = static_cast<SinkInfoListCbData*>(ud);
    if (eol || !info || !d || !d->result) return;
    SinkInfo s;
    s.name = QString::fromUtf8(info->name ? info->name : "");
    s.description = QString::fromUtf8(info->description ? info->description : "");
    s.index = info->index;
    d->result->append(s);
}

} // namespace pabackend
