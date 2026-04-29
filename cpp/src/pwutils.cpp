#include "pwutils.h"

#include <QProcess>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QDebug>

// ─── Filter constants ─────────────────────────────────────────────────────────
const QSet<QString> SYSTEM_BINARIES {
    QStringLiteral("wireplumber"), QStringLiteral("pipewire"),
    QStringLiteral("kwin_wayland"), QStringLiteral("plasmashell"),
    QStringLiteral("kded5"), QStringLiteral("kded6"),
    QStringLiteral("xdg-desktop-portal"), QStringLiteral("xdg-desktop-portal-kde"),
    QStringLiteral("polkit-kde-authentication-agent-1"),
    QStringLiteral("pactl"), QStringLiteral("pw-cli"), QStringLiteral("pw-dump"),
    QStringLiteral("python3"), QStringLiteral("python3.14"), QStringLiteral("python"),
    QStringLiteral("QtWebEngineProcess"), QString{},
};

const QSet<QString> SKIP_APP_NAMES {
    QStringLiteral("ringrtc"),
    QStringLiteral("WEBRTC VoiceEngine"),
    QStringLiteral("Chromium input"),
};

// ─── listPipeWireClients ──────────────────────────────────────────────────────
QList<PipeWireClient> listPipeWireClients()
{
    QProcess p;
    p.start(QStringLiteral("pw-dump"), QStringList{});

    if (!p.waitForStarted(3000)) {
        qWarning() << "[pwutils] pw-dump failed to start";
        return {};
    }
    if (!p.waitForFinished(3000)) {
        qWarning() << "[pwutils] pw-dump timed out";
        return {};
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        qWarning() << "[pwutils] pw-dump exit status:" << p.exitStatus()
                    << "code:" << p.exitCode();
        return {};
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(p.readAllStandardOutput(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "[pwutils] pw-dump JSON parse error:" << err.errorString();
        return {};
    }
    if (!doc.isArray()) {
        qWarning() << "[pwutils] pw-dump output is not a JSON array";
        return {};
    }

    QMap<QString, QString> seen;
    for (const QJsonValue &val : doc.array()) {
        QJsonObject obj = val.toObject();
        if (!obj[QStringLiteral("type")].toString().contains(QStringLiteral("Client")))
            continue;

        QJsonObject props = obj[QStringLiteral("info")].toObject()
                                [QStringLiteral("props")].toObject();
        QString binary = props[QStringLiteral("application.process.binary")].toString();
        if (binary.isEmpty() || SYSTEM_BINARIES.contains(binary))
            continue;

        QString name = props[QStringLiteral("application.name")].toString();
        if (name.isEmpty())
            name = binary;
        if (SKIP_APP_NAMES.contains(name)
            || name.toLower().contains(QStringLiteral("input")))
            name = binary;
        if (name.trimmed().isEmpty())
            continue;

        seen[name] = binary;
    }

    QList<PipeWireClient> result;
    for (auto it = seen.begin(); it != seen.end(); ++it)
        result.append({ it.key(), it.value() });
    return result;
}
