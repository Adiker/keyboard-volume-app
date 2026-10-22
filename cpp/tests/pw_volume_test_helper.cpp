#include "pwutils.h"

#include <QCoreApplication>
#include <QTextStream>
#include <QThread>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() == 2 && args[1] == QStringLiteral("snapshot"))
    {
        const PipeWireSnapshot snapshot = inspectPipeWire();
        QTextStream out(stdout);
        for (const PipeWireNode& node : snapshot.nodes)
        {
            out << "id=" << node.id << " class=" << node.mediaClass << " name=" << node.name
                << " binary=" << node.binary << " raw=" << node.rawVolume
                << " visible=" << node.visibleVolume()
                << " hidden=" << (node.hasHiddenVolumeMultiplier() ? "true" : "false") << '\n';
        }
        return 0;
    }
    if (args.size() != 3 ||
        (args[1] != QStringLiteral("inspect") && args[1] != QStringLiteral("reuse")))
    {
        QTextStream(stderr) << "usage: pw_volume_test_helper inspect|reuse NODE_ID | snapshot\n";
        return 2;
    }

    bool ok = false;
    const uint32_t nodeId = args[2].toUInt(&ok);
    if (!ok) return 2;
    PipeWireVolumeBackend backend;
    const auto node = backend.readNode(nodeId);
    if (!node) return 1;

    if (args[1] == QStringLiteral("reuse"))
    {
        const uint64_t firstGeneration = backend.connectionGeneration();
        constexpr int reads = 256;
        for (int i = 0; i < reads; ++i)
        {
            QThread::msleep(2); // let concurrent registry churn overlap the reads
            const auto next = backend.readNode(nodeId);
            if (!next || firstGeneration == 0 ||
                backend.connectionGeneration() != firstGeneration || next->id != node->id ||
                next->objectSerial != node->objectSerial || next->clientId != node->clientId ||
                next->clientName != node->clientName || next->clientBinary != node->clientBinary)
                return 1;
        }
        QTextStream(stdout) << "connection_generation=" << firstGeneration
                            << " reused=true owner_reads=" << reads << '\n';
        return 0;
    }

    QTextStream out(stdout);
    out << "id=" << node->id << " raw=" << node->rawVolume << " visible=" << node->visibleVolume()
        << " effective=" << node->effectiveVolume()
        << " hidden=" << (node->hasHiddenVolumeMultiplier() ? "true" : "false") << " channels=";
    for (qsizetype i = 0; i < node->channelVolumes.size(); ++i)
    {
        if (i > 0) out << ',';
        out << node->channelVolumes[i];
    }
    out << '\n';
    return 0;
}
