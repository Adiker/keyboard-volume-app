#include "pwclientfilter.h"

#include <QMap>
#include <QSet>

namespace
{

bool isGenericAppName(const QString& name)
{
    const QString lower = name.trimmed().toLower();
    return lower == QStringLiteral("chromium") || lower == QStringLiteral("chrome") ||
           lower == QStringLiteral("brave");
}

QString displayNameForClient(const QString& name, const QString& binary)
{
    if (isGenericAppName(name) && !binary.isEmpty() &&
        binary.compare(name, Qt::CaseInsensitive) != 0)
        return binary;
    return name;
}

} // namespace

namespace pwdetail
{

void populateNodeOwner(const QList<RegistryGlobal>& globals, PipeWireNode& node)
{
    if (node.clientId.isEmpty()) return;

    for (const RegistryGlobal& global : globals)
    {
        if (global.type != QString::fromUtf8(PW_TYPE_INTERFACE_Client) ||
            QString::number(global.id) != node.clientId)
            continue;

        node.clientName = global.name;
        node.clientBinary = global.binary;
        return;
    }
}

} // namespace pwdetail

bool pipeWireNodeMatchesApp(const PipeWireNode& node, const QStringList& candidates)
{
    // An embedded renderer can deliberately use another application's node
    // identity (for example limusic-app owns a node named mpv).  Once the
    // PipeWire client is known, it is the canonical app identity: do not let a
    // profile for the renderer steal a stream from its owning application.
    const QStringList ownerFields{node.clientName, node.clientBinary};
    const bool ownerKnown = !node.clientName.isEmpty() || !node.clientBinary.isEmpty();
    const QStringList fields =
        ownerKnown ? ownerFields
                   : QStringList{node.name, node.binary, node.nodeName, node.mediaName};
    for (const QString& candidate : candidates)
    {
        if (candidate.isEmpty()) continue;
        for (const QString& field : fields)
        {
            if (field.compare(candidate, Qt::CaseInsensitive) == 0) return true;
        }
    }
    return false;
}

// ─── Pure client filtering ────────────────────────────────────────────────────
QList<PipeWireClient> clientsFromPipeWireGlobals(const QList<PipeWireGlobalProps>& globals,
                                                 const QSet<QString>& systemBinaries,
                                                 const QSet<QString>& skipAppNames)
{
    QMap<QString, PipeWireClient> seen;
    QSet<QString> clientNames;
    QSet<QString> clientBinaries;
    QMap<QString, QString> clientNameByBinary;
    QMap<QString, QString> clientNameById;
    QMap<QString, QString> clientBinaryById;
    for (const PipeWireGlobalProps& global : globals)
    {
        if (!global.type.contains(QStringLiteral("Client"))) continue;

        QString binary = global.binary;
        if (binary.isEmpty() || systemBinaries.contains(binary)) continue;

        QString name = global.name;
        if (name.isEmpty()) name = binary;
        if (skipAppNames.contains(name) || name.toLower().contains(QStringLiteral("input")))
            name = binary;
        if (name.trimmed().isEmpty()) continue;

        const QString displayName = displayNameForClient(name, binary);
        seen[displayName] = PipeWireClient{displayName, binary, global.objectId};
        clientNames.insert(displayName);
        clientBinaries.insert(binary);
        clientNameByBinary[binary] = displayName;
        if (!global.objectId.isEmpty())
        {
            clientNameById[global.objectId] = displayName;
            clientBinaryById[global.objectId] = binary;
        }
    }

    for (const PipeWireGlobalProps& global : globals)
    {
        const bool isStreamNode = global.type.contains(QStringLiteral("Node")) &&
                                  global.mediaClass.startsWith(QStringLiteral("Stream/"));
        if (!isStreamNode) continue;

        const QString ownerBinary = global.binary;
        if (ownerBinary.isEmpty() || systemBinaries.contains(ownerBinary)) continue;

        QString target = global.nodeName;
        if (target.isEmpty()) target = global.name;
        if (target.isEmpty()) target = ownerBinary;
        if (target.compare(global.name, Qt::CaseInsensitive) == 0 &&
            isGenericAppName(global.name) && !ownerBinary.isEmpty())
            target = ownerBinary;
        if (skipAppNames.contains(target) || target.toLower().contains(QStringLiteral("input")))
            target = ownerBinary;
        if (target.trimmed().isEmpty()) continue;

        QString ownerDisplay = clientNameById.value(global.clientId);
        if (ownerDisplay.isEmpty()) ownerDisplay = clientNameByBinary.value(ownerBinary);
        if (!ownerDisplay.isEmpty())
        {
            const QString ownerId = global.clientId;
            const QString ownerTarget = clientBinaryById.value(ownerId, ownerBinary);
            seen[ownerDisplay] = PipeWireClient{ownerDisplay, ownerTarget, ownerId};
            continue;
        }

        QString displayName = global.name;
        if (displayName.isEmpty()) displayName = target;
        if (skipAppNames.contains(displayName) ||
            displayName.toLower().contains(QStringLiteral("input")))
            displayName = target;
        if (displayName.trimmed().isEmpty()) continue;
        if (clientNames.contains(displayName) || seen.contains(displayName)) continue;

        seen[displayName] = PipeWireClient{displayName, target, global.clientId};
    }

    QList<PipeWireClient> result;
    for (auto it = seen.begin(); it != seen.end(); ++it) result.append(it.value());
    return result;
}
