#pragma once
#include <QString>
#include <QStringList>
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
    uint32_t id = 0;
    QString name{};
    QString binary{};
    QString mediaClass{};
    QString nodeName{};
    QString objectSerial{};
    QString clientId{};
    QString mediaName{};
    double rawVolume = 1.0;
    QList<double> channelVolumes{};
    bool muted = false;

    // KDE Plasma, pavucontrol and wpctl expose the per-channel values. PipeWire's
    // scalar SPA_PROP_volume is a separate multiplier and must stay at unity for
    // writes made by keyboard-volume-app.
    double visibleVolume() const;
    double effectiveVolume() const;
    bool hasHiddenVolumeMultiplier() const;
};

struct PipeWireSnapshot
{
    QList<PipeWireClient> clients;
    QList<PipeWireNode> nodes;
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

// Takes one registry snapshot and reads Props for every audio stream node.
PipeWireSnapshot inspectPipeWire(const QSet<QString>& systemBinaries = SYSTEM_BINARIES,
                                 const QSet<QString>& skipAppNames = SKIP_APP_NAMES);

// Uses libpipewire to inspect and update PipeWire stream node Props.
// Optional matchCandidates expand the lookup beyond appName (alias reverse
// resolution). Empty list → match appName only.
std::optional<PipeWireNode> findPipeWireNodeForApp(const QString& appName,
                                                   const QStringList& matchCandidates = {});
QList<PipeWireNode> findPipeWireNodesForApp(const QString& appName,
                                            const QStringList& matchCandidates = {});
std::optional<PipeWireNode> readPipeWireNode(uint32_t nodeId);

// An intentional volume change is always represented by channelVolumes while
// resetting the hidden scalar multiplier to 1.0. The state restore helper is
// reserved for sink moves: it writes back the exact pre-move representation and
// therefore never silently normalizes a discrepancy discovered during routing.
bool setPipeWireNodeVisibleVolume(uint32_t nodeId, double volume, uint32_t channelCount = 2);
bool setPipeWireNodeVisibleChannelVolumes(uint32_t nodeId, const QList<double>& channelVolumes);
// Compatibility wrapper for existing internal callers; now follows the same
// mixer-visible contract instead of writing SPA_PROP_volume directly.
bool setPipeWireNodeVolume(uint32_t nodeId, double volume);
bool restorePipeWireNodeVolumeState(uint32_t nodeId, double rawVolume,
                                    const QList<double>& channelVolumes);
bool setPipeWireNodeMute(uint32_t nodeId, bool muted);
