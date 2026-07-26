#include "kvctloutput.h"

#include <QDBusArgument>
#include <QDBusVariant>
#include <QMetaType>
#include <QVariantList>
#include <QVariantMap>

namespace
{

QVariant normalizeDbusValue(const QVariant& value)
{
    if (value.userType() == qMetaTypeId<QDBusVariant>())
        return normalizeDbusValue(qvariant_cast<QDBusVariant>(value).variant());

    if (value.userType() == qMetaTypeId<QDBusArgument>())
    {
        const QDBusArgument argument = qvariant_cast<QDBusArgument>(value);
        if (argument.currentType() == QDBusArgument::ArrayType)
        {
            if (argument.currentSignature() == QStringLiteral("as"))
            {
                QStringList strings;
                argument >> strings;
                return strings;
            }

            QVariantList list;
            argument >> list;
            for (QVariant& entry : list) entry = normalizeDbusValue(entry);
            return list;
        }
        if (argument.currentType() == QDBusArgument::MapType)
        {
            QVariantMap map;
            argument >> map;
            for (auto it = map.begin(); it != map.end(); ++it)
                it.value() = normalizeDbusValue(it.value());
            return map;
        }
        return {};
    }

    if (value.typeId() == QMetaType::QVariantList)
    {
        QVariantList list = value.toList();
        for (QVariant& entry : list) entry = normalizeDbusValue(entry);
        return list;
    }
    if (value.typeId() == QMetaType::QVariantMap)
    {
        QVariantMap map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it)
            it.value() = normalizeDbusValue(it.value());
        return map;
    }
    return value;
}

QString variantToText(const QVariant& value);

QStringList listToText(const QVariantList& list)
{
    QStringList lines;
    for (const QVariant& entry : list) lines << variantToText(entry);
    return lines;
}

QString hotkeyToText(const QVariant& value)
{
    const QVariantMap binding = value.toMap();
    if (binding.isEmpty()) return value.toString();

    const QString type = binding.value(QStringLiteral("type")).toString();
    const int code = binding.value(QStringLiteral("code")).toInt();
    const int direction = binding.value(QStringLiteral("direction")).toInt();
    return QStringLiteral("%1:%2:%3").arg(type, QString::number(code), QString::number(direction));
}

QString mapToText(const QVariantMap& map)
{
    const QString id = map.value(QStringLiteral("id")).toString();
    const QString name = map.value(QStringLiteral("name")).toString();
    const QString app = map.value(QStringLiteral("app")).toString();
    if (map.contains(QStringLiteral("targets")))
    {
        const QVariantList targets = map.value(QStringLiteral("targets")).toList();
        QStringList renderedTargets;
        for (const QVariant& targetValue : targets)
        {
            const QVariantMap target = targetValue.toMap();
            QStringList fields{target.value(QStringLiteral("match")).toString()};
            if (target.contains(QStringLiteral("volume")))
                fields << QStringLiteral("volume=%1")
                              .arg(target.value(QStringLiteral("volume")).toString());
            if (target.contains(QStringLiteral("muted")))
                fields << QStringLiteral("muted=%1")
                              .arg(target.value(QStringLiteral("muted")).toBool()
                                       ? QStringLiteral("true")
                                       : QStringLiteral("false"));
            if (target.contains(QStringLiteral("sink")))
                fields << QStringLiteral("sink=%1").arg(
                    target.value(QStringLiteral("sink")).toString());
            renderedTargets << fields.join(QLatin1Char(','));
        }
        return QStringLiteral("%1\t%2\t%3").arg(id, name, renderedTargets.join(QLatin1Char(';')));
    }

    // Sink map: {name, description, is_default}
    if (map.contains(QStringLiteral("description")) && map.contains(QStringLiteral("is_default")))
    {
        const QString sinkName = map.value(QStringLiteral("name")).toString();
        const QString desc = map.value(QStringLiteral("description")).toString();
        const bool isDefault = map.value(QStringLiteral("is_default")).toBool();
        return QStringLiteral("%1\t%2%3")
            .arg(sinkName, desc, isDefault ? QStringLiteral(" (default)") : QString());
    }

    if (!id.isEmpty() || !name.isEmpty() || !app.isEmpty())
    {
        QString modifiers =
            map.value(QStringLiteral("modifiers")).toStringList().join(QLatin1Char(','));
        if (modifiers.isEmpty()) modifiers = QStringLiteral("-");

        QString hotkeys = QStringLiteral("-");
        const QVariantMap hotkeyMap = map.value(QStringLiteral("hotkeys")).toMap();
        if (!hotkeyMap.isEmpty())
        {
            hotkeys = QStringLiteral("up=%1,down=%2,mute=%3")
                          .arg(hotkeyToText(hotkeyMap.value(QStringLiteral("volume_up"))),
                               hotkeyToText(hotkeyMap.value(QStringLiteral("volume_down"))),
                               hotkeyToText(hotkeyMap.value(QStringLiteral("mute"))));
        }

        QString sink = map.value(QStringLiteral("sink")).toString();
        if (sink.isEmpty()) sink = QStringLiteral("-");

        QStringList apps = map.value(QStringLiteral("apps")).toStringList();
        if (apps.isEmpty() && !app.isEmpty()) apps.append(app);
        QString appRegex = map.value(QStringLiteral("app_regex")).toString();
        if (appRegex.isEmpty()) appRegex = QStringLiteral("-");

        return QStringLiteral("%1\t%2\t%3\t%4\t%5\t%6\tapps=%7\tregex=%8")
            .arg(id, name, app, modifiers, hotkeys, sink, apps.join(QLatin1Char(',')), appRegex);
    }

    QStringList parts;
    for (auto it = map.cbegin(); it != map.cend(); ++it)
        parts << QStringLiteral("%1=%2").arg(it.key(), variantToText(it.value()));
    return parts.join(QLatin1Char('\t'));
}

QString variantToText(const QVariant& value)
{
    if (value.typeId() == QMetaType::Bool)
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");

    if (value.typeId() == QMetaType::QStringList)
        return value.toStringList().join(QLatin1Char('\n'));

    if (value.typeId() == QMetaType::QVariantList)
        return listToText(value.toList()).join(QLatin1Char('\n'));

    if (value.typeId() == QMetaType::QVariantMap) return mapToText(value.toMap());

    return value.toString();
}

} // namespace

QString formatKvCtlValue(const QVariant& value)
{
    return variantToText(normalizeDbusValue(value));
}
