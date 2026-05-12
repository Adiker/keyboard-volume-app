#include <gtest/gtest.h>

#include "kvctlcommand.h"

TEST(KvCtlCommand, ParsesBareVolumeCommands)
{
    auto up = parseKvCtlCommand({QStringLiteral("up")}, {}, false);
    ASSERT_TRUE(up.ok) << up.error.toStdString();
    EXPECT_EQ(up.command.action, KvCtlCommand::Action::VolumeUp);
    EXPECT_TRUE(up.command.profile.isEmpty());

    auto down = parseKvCtlCommand({QStringLiteral("down")}, {}, false);
    ASSERT_TRUE(down.ok) << down.error.toStdString();
    EXPECT_EQ(down.command.action, KvCtlCommand::Action::VolumeDown);

    auto mute = parseKvCtlCommand({QStringLiteral("mute")}, {}, false);
    ASSERT_TRUE(mute.ok) << mute.error.toStdString();
    EXPECT_EQ(mute.command.action, KvCtlCommand::Action::ToggleMute);

    auto duck = parseKvCtlCommand({QStringLiteral("duck")}, {}, false);
    ASSERT_TRUE(duck.ok) << duck.error.toStdString();
    EXPECT_EQ(duck.command.action, KvCtlCommand::Action::ToggleDucking);
}

TEST(KvCtlCommand, ParsesProfileForVolumeCommands)
{
    auto result = parseKvCtlCommand({QStringLiteral("up")}, QStringLiteral("firefox-ctrl"), true);

    ASSERT_TRUE(result.ok) << result.error.toStdString();
    EXPECT_EQ(result.command.action, KvCtlCommand::Action::VolumeUp);
    EXPECT_EQ(result.command.profile.toStdString(), "firefox-ctrl");

    auto duck = parseKvCtlCommand({QStringLiteral("duck")}, QStringLiteral("firefox-ctrl"), true);

    ASSERT_TRUE(duck.ok) << duck.error.toStdString();
    EXPECT_EQ(duck.command.action, KvCtlCommand::Action::ToggleDucking);
    EXPECT_EQ(duck.command.profile.toStdString(), "firefox-ctrl");
}

TEST(KvCtlCommand, RejectsProfileWhereUnsupported)
{
    auto get = parseKvCtlCommand({QStringLiteral("get"), QStringLiteral("volume")},
                                 QStringLiteral("firefox-ctrl"), true);
    EXPECT_FALSE(get.ok);

    auto set =
        parseKvCtlCommand({QStringLiteral("set"), QStringLiteral("step"), QStringLiteral("10")},
                          QStringLiteral("firefox-ctrl"), true);
    EXPECT_FALSE(set.ok);

    auto refresh =
        parseKvCtlCommand({QStringLiteral("refresh")}, QStringLiteral("firefox-ctrl"), true);
    EXPECT_FALSE(refresh.ok);

    auto scene = parseKvCtlCommand({QStringLiteral("scene"), QStringLiteral("meeting")},
                                   QStringLiteral("firefox-ctrl"), true);
    EXPECT_FALSE(scene.ok);
}

TEST(KvCtlCommand, ParsesGetFields)
{
    const QStringList fields = {
        QStringLiteral("volume"),     QStringLiteral("muted"),
        QStringLiteral("active-app"), QStringLiteral("apps"),
        QStringLiteral("step"),       QStringLiteral("profiles"),
        QStringLiteral("scenes"),     QStringLiteral("progress-enabled"),
    };

    for (const QString& field : fields)
    {
        auto result = parseKvCtlCommand({QStringLiteral("get"), field}, {}, false);
        ASSERT_TRUE(result.ok) << result.error.toStdString();
        EXPECT_EQ(result.command.action, KvCtlCommand::Action::Get);
        EXPECT_NE(result.command.field, KvCtlCommand::Field::None);
    }
}

TEST(KvCtlCommand, ParsesSceneCommand)
{
    auto result =
        parseKvCtlCommand({QStringLiteral("scene"), QStringLiteral("meeting")}, {}, false);

    ASSERT_TRUE(result.ok) << result.error.toStdString();
    EXPECT_EQ(result.command.action, KvCtlCommand::Action::ApplyScene);
    EXPECT_EQ(result.command.scene.toStdString(), "meeting");
}

TEST(KvCtlCommand, ParsesSetFields)
{
    auto volume = parseKvCtlCommand(
        {QStringLiteral("set"), QStringLiteral("volume"), QStringLiteral("35")}, {}, false);
    ASSERT_TRUE(volume.ok) << volume.error.toStdString();
    EXPECT_EQ(volume.command.action, KvCtlCommand::Action::Set);
    EXPECT_EQ(volume.command.field, KvCtlCommand::Field::Volume);
    EXPECT_EQ(volume.command.value.toStdString(), "35");

    auto muted = parseKvCtlCommand(
        {QStringLiteral("set"), QStringLiteral("muted"), QStringLiteral("true")}, {}, false);
    ASSERT_TRUE(muted.ok) << muted.error.toStdString();
    EXPECT_EQ(muted.command.field, KvCtlCommand::Field::Muted);

    auto app = parseKvCtlCommand(
        {QStringLiteral("set"), QStringLiteral("active-app"), QStringLiteral("Firefox")}, {},
        false);
    ASSERT_TRUE(app.ok) << app.error.toStdString();
    EXPECT_EQ(app.command.field, KvCtlCommand::Field::ActiveApp);

    auto step = parseKvCtlCommand(
        {QStringLiteral("set"), QStringLiteral("step"), QStringLiteral("10")}, {}, false);
    ASSERT_TRUE(step.ok) << step.error.toStdString();
    EXPECT_EQ(step.command.field, KvCtlCommand::Field::Step);

    auto progEnabled = parseKvCtlCommand(
        {QStringLiteral("set"), QStringLiteral("progress-enabled"), QStringLiteral("true")}, {},
        false);
    ASSERT_TRUE(progEnabled.ok) << progEnabled.error.toStdString();
    EXPECT_EQ(progEnabled.command.field, KvCtlCommand::Field::ProgressEnabled);
    EXPECT_EQ(progEnabled.command.value.toStdString(), "true");
}

TEST(KvCtlCommand, RejectsBadCommands)
{
    EXPECT_FALSE(parseKvCtlCommand({}, {}, false).ok);
    EXPECT_FALSE(parseKvCtlCommand({QStringLiteral("unknown")}, {}, false).ok);
    EXPECT_FALSE(parseKvCtlCommand({QStringLiteral("get"), QStringLiteral("bogus")}, {}, false).ok);
    EXPECT_FALSE(
        parseKvCtlCommand({QStringLiteral("set"), QStringLiteral("apps"), QStringLiteral("x")}, {},
                          false)
            .ok);
    EXPECT_FALSE(parseKvCtlCommand({QStringLiteral("up"), QStringLiteral("extra")}, {}, false).ok);
    EXPECT_FALSE(parseKvCtlCommand({QStringLiteral("up")}, {}, true).ok);
    EXPECT_FALSE(
        parseKvCtlCommand({QStringLiteral("duck"), QStringLiteral("extra")}, {}, false).ok);
    EXPECT_FALSE(parseKvCtlCommand({QStringLiteral("scene")}, {}, false).ok);
    EXPECT_FALSE(parseKvCtlCommand({QStringLiteral("scene"), QStringLiteral(" ")}, {}, false).ok);
    EXPECT_FALSE(parseKvCtlCommand(
                     {QStringLiteral("scene"), QStringLiteral("meeting"), QStringLiteral("extra")},
                     {}, false)
                     .ok);
    EXPECT_FALSE(
        parseKvCtlCommand({QStringLiteral("show"), QStringLiteral("extra")}, {}, false).ok);
}

TEST(KvCtlCommand, ParsesBareShowCommand)
{
    auto result = parseKvCtlCommand({QStringLiteral("show")}, {}, false);
    ASSERT_TRUE(result.ok) << result.error.toStdString();
    EXPECT_EQ(result.command.action, KvCtlCommand::Action::Show);
    EXPECT_TRUE(result.command.profile.isEmpty());
}

TEST(KvCtlCommand, ParsesShowWithProfile)
{
    auto result = parseKvCtlCommand({QStringLiteral("show")}, QStringLiteral("spotify"), true);
    ASSERT_TRUE(result.ok) << result.error.toStdString();
    EXPECT_EQ(result.command.action, KvCtlCommand::Action::Show);
    EXPECT_EQ(result.command.profile.toStdString(), "spotify");
}
