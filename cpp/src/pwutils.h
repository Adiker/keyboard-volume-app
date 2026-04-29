#pragma once
#include <QString>
#include <QList>
#include <QSet>

struct PipeWireClient {
    QString name;    // application.name or binary
    QString binary;  // application.process.binary
};

// Filter sets shared between pwutils and VolumeController
extern const QSet<QString> SYSTEM_BINARIES;
extern const QSet<QString> SKIP_APP_NAMES;

// Runs pw-dump, returns idle PipeWire clients.
// Returns empty list on subprocess failure, timeout, or parse error.
QList<PipeWireClient> listPipeWireClients();
