#include "kvctlcommand.h"

#include <QCoreApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QTextStream>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <cmath>

#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif

namespace {

constexpr int ExitOk = 0;
constexpr int ExitUsage = 1;
constexpr int ExitUnavailable = 2;
constexpr int ExitDbusError = 3;
constexpr int ExitInvalidValue = 4;

const QString Service = QStringLiteral("org.keyboardvolumeapp");
const QString ObjectPath = QStringLiteral("/org/keyboardvolumeapp");
const QString ControlInterface = QStringLiteral("org.keyboardvolumeapp.VolumeControl");
const QString PropertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");

void printLine(const QString &text)
{
    QTextStream(stdout) << text << Qt::endl;
}

void printError(const QString &text)
{
    QTextStream(stderr) << "kv-ctl: " << text << Qt::endl;
}

QString propertyName(KvCtlCommand::Field field)
{
    switch (field) {
    case KvCtlCommand::Field::Volume:    return QStringLiteral("Volume");
    case KvCtlCommand::Field::Muted:     return QStringLiteral("Muted");
    case KvCtlCommand::Field::ActiveApp: return QStringLiteral("ActiveApp");
    case KvCtlCommand::Field::Apps:      return QStringLiteral("Apps");
    case KvCtlCommand::Field::Step:      return QStringLiteral("VolumeStep");
    case KvCtlCommand::Field::Profiles:  return QStringLiteral("Profiles");
    case KvCtlCommand::Field::None:      return QString();
    }
    return QString();
}

bool parseBool(const QString &value, bool *out)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("true")
        || normalized == QStringLiteral("1")
        || normalized == QStringLiteral("yes")
        || normalized == QStringLiteral("on"))
    {
        *out = true;
        return true;
    }
    if (normalized == QStringLiteral("false")
        || normalized == QStringLiteral("0")
        || normalized == QStringLiteral("no")
        || normalized == QStringLiteral("off"))
    {
        *out = false;
        return true;
    }
    return false;
}

bool parseVolumePercent(const QString &value, double *out)
{
    bool ok = false;
    const double percent = value.toDouble(&ok);
    if (!ok || percent < 0.0 || percent > 100.0)
        return false;
    *out = percent / 100.0;
    return true;
}

bool parseStep(const QString &value, int *out)
{
    bool ok = false;
    const int step = value.toInt(&ok);
    if (!ok || step < 1 || step > 50)
        return false;
    *out = step;
    return true;
}

QString percentString(double fraction)
{
    const double percent = fraction * 100.0;
    if (std::fabs(percent - std::round(percent)) < 0.05)
        return QString::number(static_cast<int>(std::round(percent)));
    return QString::number(percent, 'f', 1);
}

QString variantToText(const QVariant &value);

QStringList listToText(const QVariantList &list)
{
    QStringList lines;
    for (const QVariant &entry : list)
        lines << variantToText(entry);
    return lines;
}

QString mapToText(const QVariantMap &map)
{
    const QString id = map.value(QStringLiteral("id")).toString();
    const QString name = map.value(QStringLiteral("name")).toString();
    const QString app = map.value(QStringLiteral("app")).toString();
    if (!id.isEmpty() || !name.isEmpty() || !app.isEmpty())
        return QStringLiteral("%1\t%2\t%3").arg(id, name, app);

    QStringList parts;
    for (auto it = map.cbegin(); it != map.cend(); ++it)
        parts << QStringLiteral("%1=%2").arg(it.key(), variantToText(it.value()));
    return parts.join(QLatin1Char('\t'));
}

QString variantToText(const QVariant &value)
{
    if (value.userType() == qMetaTypeId<QDBusVariant>())
        return variantToText(qvariant_cast<QDBusVariant>(value).variant());

    if (value.typeId() == QMetaType::Bool)
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");

    if (value.canConvert<QStringList>())
        return value.toStringList().join(QLatin1Char('\n'));

    if (value.canConvert<QVariantList>())
        return listToText(value.toList()).join(QLatin1Char('\n'));

    if (value.canConvert<QVariantMap>())
        return mapToText(value.toMap());

    return value.toString();
}

QVariant setValueForCommand(const KvCtlCommand &cmd, bool *ok)
{
    *ok = true;
    switch (cmd.field) {
    case KvCtlCommand::Field::Volume: {
        double volume = 0.0;
        if (!parseVolumePercent(cmd.value, &volume)) {
            *ok = false;
            return {};
        }
        return volume;
    }
    case KvCtlCommand::Field::Muted: {
        bool muted = false;
        if (!parseBool(cmd.value, &muted)) {
            *ok = false;
            return {};
        }
        return muted;
    }
    case KvCtlCommand::Field::ActiveApp:
        if (cmd.value.trimmed().isEmpty()) {
            *ok = false;
            return {};
        }
        return cmd.value;
    case KvCtlCommand::Field::Step: {
        int step = 0;
        if (!parseStep(cmd.value, &step)) {
            *ok = false;
            return {};
        }
        return step;
    }
    case KvCtlCommand::Field::None:
    case KvCtlCommand::Field::Apps:
    case KvCtlCommand::Field::Profiles:
        *ok = false;
        return {};
    }
    *ok = false;
    return {};
}

QString methodName(const KvCtlCommand &cmd)
{
    const bool profile = !cmd.profile.isEmpty();
    switch (cmd.action) {
    case KvCtlCommand::Action::VolumeUp:
        return profile ? QStringLiteral("VolumeUpProfile") : QStringLiteral("VolumeUp");
    case KvCtlCommand::Action::VolumeDown:
        return profile ? QStringLiteral("VolumeDownProfile") : QStringLiteral("VolumeDown");
    case KvCtlCommand::Action::ToggleMute:
        return profile ? QStringLiteral("ToggleMuteProfile") : QStringLiteral("ToggleMute");
    case KvCtlCommand::Action::Refresh:
        return QStringLiteral("RefreshApps");
    case KvCtlCommand::Action::Get:
    case KvCtlCommand::Action::Set:
        return QString();
    }
    return QString();
}

bool ensureServiceAvailable(QDBusConnectionInterface *iface)
{
    if (!iface)
        return false;

    QDBusReply<bool> registered = iface->isServiceRegistered(Service);
    return registered.isValid() && registered.value();
}

bool ensureDaemonAvailable()
{
    auto bus = QDBusConnection::sessionBus();
    if (ensureServiceAvailable(bus.interface()))
        return true;

    printError(QStringLiteral("keyboard-volume-app is not running on the session bus"));
    return false;
}

int callControlMethod(const KvCtlCommand &cmd)
{
    if (!ensureDaemonAvailable())
        return ExitUnavailable;

    QDBusInterface control(Service, ObjectPath, ControlInterface, QDBusConnection::sessionBus());
    if (!control.isValid()) {
        printError(control.lastError().message());
        return ExitDbusError;
    }

    QDBusMessage reply;
    if (cmd.profile.isEmpty())
        reply = control.call(methodName(cmd));
    else
        reply = control.call(methodName(cmd), cmd.profile);

    if (reply.type() == QDBusMessage::ErrorMessage) {
        printError(reply.errorMessage());
        return ExitDbusError;
    }
    return ExitOk;
}

int getProperty(KvCtlCommand::Field field)
{
    if (!ensureDaemonAvailable())
        return ExitUnavailable;

    QDBusInterface props(Service, ObjectPath, PropertiesInterface, QDBusConnection::sessionBus());
    QDBusReply<QVariant> reply = props.call(QStringLiteral("Get"),
                                            ControlInterface,
                                            propertyName(field));
    if (!reply.isValid()) {
        printError(reply.error().message());
        return ExitDbusError;
    }

    const QVariant value = reply.value();
    if (field == KvCtlCommand::Field::Volume) {
        printLine(percentString(value.toDouble()));
    } else {
        printLine(variantToText(value));
    }
    return ExitOk;
}

int setProperty(const KvCtlCommand &cmd)
{
    bool ok = false;
    const QVariant value = setValueForCommand(cmd, &ok);
    if (!ok) {
        printError(QStringLiteral("invalid value for '%1'").arg(propertyName(cmd.field)));
        return ExitInvalidValue;
    }

    if (!ensureDaemonAvailable())
        return ExitUnavailable;

    QDBusInterface props(Service, ObjectPath, PropertiesInterface, QDBusConnection::sessionBus());
    QDBusMessage reply = props.call(QStringLiteral("Set"),
                                    ControlInterface,
                                    propertyName(cmd.field),
                                    QVariant::fromValue(QDBusVariant(value)));
    if (reply.type() == QDBusMessage::ErrorMessage) {
        printError(reply.errorMessage());
        return ExitDbusError;
    }

    return ExitOk;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kv-ctl"));
    app.setApplicationVersion(QStringLiteral(APP_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("CLI control client for keyboard-volume-app"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption(QStringList{QStringLiteral("p"), QStringLiteral("profile")},
                                        QStringLiteral("Target profile id for up/down/mute."),
                                        QStringLiteral("ID")));
    parser.addPositionalArgument(QStringLiteral("command"), kvCtlUsageText());

    if (!parser.parse(app.arguments())) {
        printError(parser.errorText());
        QTextStream(stderr) << kvCtlUsageText();
        return ExitUsage;
    }
    if (parser.isSet(QStringLiteral("help"))) {
        QTextStream(stdout) << parser.helpText() << Qt::endl << kvCtlUsageText();
        return ExitOk;
    }
    if (parser.isSet(QStringLiteral("version"))) {
        printLine(QStringLiteral("kv-ctl %1").arg(app.applicationVersion()));
        return ExitOk;
    }

    KvCtlParseResult parsed = parseKvCtlCommand(parser.positionalArguments(),
                                                parser.value(QStringLiteral("profile")),
                                                parser.isSet(QStringLiteral("profile")));
    if (!parsed.ok) {
        printError(parsed.error);
        QTextStream(stderr) << kvCtlUsageText();
        return ExitUsage;
    }

    const KvCtlCommand &cmd = parsed.command;
    if (cmd.action == KvCtlCommand::Action::Get)
        return getProperty(cmd.field);
    if (cmd.action == KvCtlCommand::Action::Set)
        return setProperty(cmd);

    return callControlMethod(cmd);
}
