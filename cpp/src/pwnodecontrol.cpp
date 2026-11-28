#include "pwnodecontrol.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include <spa/param/param.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>

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

namespace pwdetail
{

namespace
{

struct NodeParamReader
{
    PipeWireSession* session = nullptr;
    PipeWireNode* state = nullptr;
    double rawVolume = 1.0;
    QList<double> channelVolumes;
    bool muted = false;
    bool hasProps = false;

    static void onNodeInfo(void* data, const pw_node_info* info)
    {
        if (!info || !info->props) return;

        auto* self = static_cast<NodeParamReader*>(data);
        if (!self->state) return;
        self->state->name = dictValue(info->props, "application.name");
        self->state->binary = dictValue(info->props, "application.process.binary");
        self->state->mediaClass = dictValue(info->props, "media.class");
        self->state->nodeName = dictValue(info->props, "node.name");
        self->state->objectSerial = dictValue(info->props, "object.serial");
        self->state->clientId = dictValue(info->props, "client.id");
        self->state->mediaName = dictValue(info->props, "media.name");
    }

    static void onNodeParam(void* data, int, uint32_t id, uint32_t, uint32_t, const spa_pod* param)
    {
        if (id != SPA_PARAM_Props || !param) return;

        auto* self = static_cast<NodeParamReader*>(data);
        float volume = 1.0F;
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

} // namespace

bool readNodeProps(PipeWireSession& session, uint32_t nodeId, PipeWireNode* state,
                   bool* sessionFailed)
{
    if (sessionFailed) *sessionFailed = false;
    if (!state) return false;
    auto* node = static_cast<pw_node*>(
        pw_registry_bind(session.registry, nodeId, PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0));
    if (!node) return false;

    spa_hook nodeListener{};
    NodeParamReader reader;
    reader.session = &session;
    reader.state = state;
    static const pw_node_events nodeEvents{
        PW_VERSION_NODE_EVENTS,
        &NodeParamReader::onNodeInfo,
        &NodeParamReader::onNodeParam,
    };

    pw_node_add_listener(node, &nodeListener, &nodeEvents, &reader);
    const uint32_t proxyId = pw_proxy_get_id(reinterpret_cast<pw_proxy*>(node));
    session.activeProxyId = proxyId;
    session.activeProxyError = false;
    pw_node_enum_params(node, 0, SPA_PARAM_Props, 0, 1, nullptr);
    const bool synced = session.sync(kPwSyncTimeoutMs);
    if (!synced && sessionFailed) *sessionFailed = true;
    const bool ok = synced && !session.activeProxyError && reader.hasProps;
    session.activeProxyId = SPA_ID_INVALID;

    if (ok)
    {
        state->id = nodeId;
        if (state->clientId.isEmpty())
        {
            for (const RegistryGlobal& global : session.globals)
            {
                if (global.type != QString::fromUtf8(PW_TYPE_INTERFACE_Node) || global.id != nodeId)
                    continue;
                state->clientId = global.clientId;
                break;
            }
        }
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
                  bool* sessionFailed)
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

    std::array<uint8_t, 4096> buffer{};
    spa_pod_builder builder{};
    spa_pod_builder_init(&builder, buffer.data(), static_cast<uint32_t>(buffer.size()));

    spa_pod_frame frame{};
    spa_pod_builder_push_object(&builder, &frame, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
    if (rawVolume)
    {
        const auto value = static_cast<float>(*rawVolume);
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
    const auto* param = static_cast<const spa_pod*>(spa_pod_builder_pop(&builder, &frame));

    const uint32_t proxyId = pw_proxy_get_id(reinterpret_cast<pw_proxy*>(node));
    session.activeProxyId = proxyId;
    session.activeProxyError = false;
    bool ok = false;
    if (param && pw_node_set_param(node, SPA_PARAM_Props, 0, param) >= 0)
    {
        const bool synced = session.sync(kPwSyncTimeoutMs);
        if (!synced && sessionFailed) *sessionFailed = true;
        ok = synced && !session.activeProxyError;
    }

    session.activeProxyId = SPA_ID_INVALID;
    pw_proxy_destroy(reinterpret_cast<pw_proxy*>(node));
    pw_thread_loop_unlock(session.loop);
    return ok;
}

} // namespace pwdetail
