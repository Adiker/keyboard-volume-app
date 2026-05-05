#include "kvctlcommand.h"

namespace
{

KvCtlParseResult fail(const QString& message)
{
    KvCtlParseResult result;
    result.error = message;
    return result;
}

KvCtlCommand::Field parseField(const QString& name)
{
    if (name == QStringLiteral("volume")) return KvCtlCommand::Field::Volume;
    if (name == QStringLiteral("muted")) return KvCtlCommand::Field::Muted;
    if (name == QStringLiteral("active-app")) return KvCtlCommand::Field::ActiveApp;
    if (name == QStringLiteral("apps")) return KvCtlCommand::Field::Apps;
    if (name == QStringLiteral("step")) return KvCtlCommand::Field::Step;
    if (name == QStringLiteral("profiles")) return KvCtlCommand::Field::Profiles;
    if (name == QStringLiteral("scenes")) return KvCtlCommand::Field::Scenes;
    return KvCtlCommand::Field::None;
}

} // namespace

KvCtlParseResult parseKvCtlCommand(const QStringList& positionalArgs, const QString& profile,
                                   bool profileSet)
{
    if (positionalArgs.isEmpty()) return fail(QStringLiteral("missing command"));

    KvCtlCommand cmd;
    cmd.profile = profileSet ? profile : QString();

    if (profileSet && profile.trimmed().isEmpty())
        return fail(QStringLiteral("--profile requires a non-empty id"));

    const QString action = positionalArgs[0];

    if (action == QStringLiteral("up") || action == QStringLiteral("down") ||
        action == QStringLiteral("mute") || action == QStringLiteral("duck"))
    {
        if (positionalArgs.size() != 1)
            return fail(
                QStringLiteral("command '%1' does not accept positional arguments").arg(action));

        if (action == QStringLiteral("up"))
            cmd.action = KvCtlCommand::Action::VolumeUp;
        else if (action == QStringLiteral("down"))
            cmd.action = KvCtlCommand::Action::VolumeDown;
        else if (action == QStringLiteral("mute"))
            cmd.action = KvCtlCommand::Action::ToggleMute;
        else
            cmd.action = KvCtlCommand::Action::ToggleDucking;

        return {true, cmd, QString()};
    }

    if (action == QStringLiteral("refresh"))
    {
        if (profileSet) return fail(QStringLiteral("refresh does not accept --profile"));
        if (positionalArgs.size() != 1)
            return fail(QStringLiteral("refresh does not accept positional arguments"));

        cmd.action = KvCtlCommand::Action::Refresh;
        return {true, cmd, QString()};
    }

    if (action == QStringLiteral("scene"))
    {
        if (profileSet) return fail(QStringLiteral("scene does not accept --profile"));
        if (positionalArgs.size() != 2) return fail(QStringLiteral("usage: kv-ctl scene ID"));

        cmd.scene = positionalArgs[1].trimmed();
        if (cmd.scene.isEmpty()) return fail(QStringLiteral("scene requires a non-empty id"));

        cmd.action = KvCtlCommand::Action::ApplyScene;
        return {true, cmd, QString()};
    }

    if (action == QStringLiteral("get"))
    {
        if (profileSet) return fail(QStringLiteral("get does not accept --profile"));
        if (positionalArgs.size() != 2)
            return fail(QStringLiteral(
                "usage: kv-ctl get volume|muted|active-app|apps|step|profiles|scenes"));

        cmd.action = KvCtlCommand::Action::Get;
        cmd.field = parseField(positionalArgs[1]);
        if (cmd.field == KvCtlCommand::Field::None)
            return fail(QStringLiteral("unknown get field '%1'").arg(positionalArgs[1]));

        return {true, cmd, QString()};
    }

    if (action == QStringLiteral("set"))
    {
        if (profileSet) return fail(QStringLiteral("set does not accept --profile"));
        if (positionalArgs.size() != 3)
            return fail(QStringLiteral("usage: kv-ctl set volume|muted|active-app|step VALUE"));

        cmd.action = KvCtlCommand::Action::Set;
        cmd.field = parseField(positionalArgs[1]);
        cmd.value = positionalArgs[2];

        if (cmd.field != KvCtlCommand::Field::Volume && cmd.field != KvCtlCommand::Field::Muted &&
            cmd.field != KvCtlCommand::Field::ActiveApp && cmd.field != KvCtlCommand::Field::Step)
        {
            return fail(
                QStringLiteral("unknown or read-only set field '%1'").arg(positionalArgs[1]));
        }

        return {true, cmd, QString()};
    }

    return fail(QStringLiteral("unknown command '%1'").arg(action));
}

QString kvCtlUsageText()
{
    return QStringLiteral("Usage:\n"
                          "  kv-ctl up [--profile ID]\n"
                          "  kv-ctl down [--profile ID]\n"
                          "  kv-ctl mute [--profile ID]\n"
                          "  kv-ctl duck [--profile ID]\n"
                          "  kv-ctl scene ID\n"
                          "  kv-ctl refresh\n"
                          "  kv-ctl get volume|muted|active-app|apps|step|profiles|scenes\n"
                          "  kv-ctl set volume VALUE\n"
                          "  kv-ctl set muted true|false\n"
                          "  kv-ctl set active-app NAME\n"
                          "  kv-ctl set step VALUE\n");
}
