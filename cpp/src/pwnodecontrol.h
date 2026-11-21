#pragma once

#include "pwsession.h"
#include "pwutils.h"

#include <QList>

#include <cstdint>
#include <optional>

namespace pwdetail
{

bool readNodeProps(PipeWireSession& session, uint32_t nodeId, PipeWireNode* state,
                   bool* sessionFailed = nullptr);

bool setNodeProps(PipeWireSession& session, uint32_t nodeId, std::optional<double> rawVolume,
                  const QList<double>& channelVolumes, std::optional<bool> muted,
                  bool* sessionFailed = nullptr);

} // namespace pwdetail
