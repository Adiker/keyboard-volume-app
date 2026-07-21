#pragma once
#include <QString>
#include <QList>
#include <QSet>
#include <optional>

struct PipeWireClient
{
    QString name;   // Display name: application.name or binary
    QString binary; // Stable control target: app binary or stream node name
    QString id;     // PipeWire global id for Client entries when known
};

struct PipeWireGlobalProps
{
    QString type{};
    QString name{};
    QString binary{};
    QString mediaClass{};
    QString nodeName{};
    QString objectId{};
    QString clientId{};
    QString mediaName{};
};

struct PipeWireNode
{
    uint32_t id;
    double volume;
    bool muted;
};

// Filter sets — defined in appaudiofilters.cpp; re-exported here for callers
// that include pwutils.h.
#include "appaudiofilters.h"

// Pure helper used by tests and the live PipeWire snapshot path.
// Optional filter sets default to the built-in constants.
QList<PipeWireClient>
clientsFromPipeWireGlobals(const QList<PipeWireGlobalProps>& globals,
                           const QSet<QString>& systemBinaries = SYSTEM_BINARIES,
                           const QSet<QString>& skipAppNames = SKIP_APP_NAMES);

// Uses libpipewire to return idle PipeWire clients.
// Returns empty list on connection failure, timeout, or parse error.
QList<PipeWireClient> listPipeWireClients(const QSet<QString>& systemBinaries = SYSTEM_BINARIES,
                                          const QSet<QString>& skipAppNames = SKIP_APP_NAMES);

// Uses libpipewire to inspect and update PipeWire stream node Props.
std::optional<PipeWireNode> findPipeWireNodeForApp(const QString& appName);
bool setPipeWireNodeVolume(uint32_t nodeId, double volume);
bool setPipeWireNodeMute(uint32_t nodeId, bool muted);
