#pragma once

#include "pwsession.h"
#include "pwutils.h"

#include <QList>

namespace pwdetail
{

void populateNodeOwner(const QList<RegistryGlobal>& globals, PipeWireNode& node);

} // namespace pwdetail
