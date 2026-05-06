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

// Filter sets shared between pwutils and VolumeController
extern const QSet<QString> SYSTEM_BINARIES;
extern const QSet<QString> SKIP_APP_NAMES;

// Pure helper used by tests and the live PipeWire snapshot path.
QList<PipeWireClient> clientsFromPipeWireGlobals(const QList<PipeWireGlobalProps>& globals);

// Uses libpipewire to return idle PipeWire clients.
// Returns empty list on connection failure, timeout, or parse error.
QList<PipeWireClient> listPipeWireClients();

// Uses libpipewire to inspect and update PipeWire stream node Props.
std::optional<PipeWireNode> findPipeWireNodeForApp(const QString& appName);
bool setPipeWireNodeVolume(uint32_t nodeId, double volume);
bool setPipeWireNodeMute(uint32_t nodeId, bool muted);
