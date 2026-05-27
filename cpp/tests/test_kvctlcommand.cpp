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

    auto setStep =
        parseKvCtlCommand({QStringLiteral("set"), QStringLiteral("step"), QStringLiteral("10")},
                          QStringLiteral("firefox-ctrl"), true);
    EXPECT_FALSE(setStep.ok);

    auto setActive = parseKvCtlCommand(
        {QStringLiteral("set"), QStringLiteral("active-app"), QStringLiteral("Firefox")},
        QStringLiteral("firefox-ctrl"), true);
    EXPECT_FALSE(setActive.ok);

    // Per-profile mute is handled by `kv-ctl mute on|off --profile id` (PR #47);
    // `set muted --profile id` is intentionally rejected to avoid a duplicate
    // CLI surface for the same operation.
    auto setMuted =
        parseKvCtlCommand({QStringLiteral("set"), QStringLiteral("muted"), QStringLiteral("true")},
                          QStringLiteral("spotify"), true);
    EXPECT_FALSE(setMuted.ok);

    auto setProgress = parseKvCtlCommand(
        {QStringLiteral("set"), QStringLiteral("progress-enabled"), QStringLiteral("true")},
        QStringLiteral("firefox-ctrl"), true);
    EXPECT_FALSE(setProgress.ok);

    auto setAutoSwitch = parseKvCtlCommand(
        {QStringLiteral("set"), QStringLiteral("auto-profile-switch"), QStringLiteral("true")},
        QStringLiteral("firefox-ctrl"), true);
    EXPECT_FALSE(setAutoSwitch.ok);

    auto refresh =
        parseKvCtlCommand({QStringLiteral("refresh")}, QStringLiteral("firefox-ctrl"), true);
    EXPECT_FALSE(refresh.ok);

    auto scene = parseKvCtlCommand({QStringLiteral("scene"), QStringLiteral("meeting")},
                                   QStringLiteral("firefox-ctrl"), true);
    EXPECT_FALSE(scene.ok);
}

TEST(KvCtlCommand, ParsesSetVolumeWithProfile)
{
    auto result =
        parseKvCtlCommand({QStringLiteral("set"), QStringLiteral("volume"), QStringLiteral("42")},
                          QStringLiteral("firefox-ctrl"), true);

    ASSERT_TRUE(result.ok) << result.error.toStdString();
    EXPECT_EQ(result.command.action, KvCtlCommand::Action::Set);
    EXPECT_EQ(result.command.field, KvCtlCommand::Field::Volume);
    EXPECT_EQ(result.command.profile.toStdString(), "firefox-ctrl");
    EXPECT_EQ(result.command.value.toStdString(), "42");
}

TEST(KvCtlCommand, ParsesGetFields)
{
    const QStringList fields = {
        QStringLiteral("volume"),
        QStringLiteral("muted"),
        QStringLiteral("active-app"),
        QStringLiteral("apps"),
        QStringLiteral("step"),
        QStringLiteral("profiles"),
        QStringLiteral("scenes"),
        QStringLiteral("progress-enabled"),
        QStringLiteral("auto-profile-switch"),
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

    auto autoSwitch = parseKvCtlCommand(
        {QStringLiteral("set"), QStringLiteral("auto-profile-switch"), QStringLiteral("true")}, {},
        false);
    ASSERT_TRUE(autoSwitch.ok) << autoSwitch.error.toStdString();
    EXPECT_EQ(autoSwitch.command.field, KvCtlCommand::Field::AutoProfileSwitch);
    EXPECT_EQ(autoSwitch.command.value.toStdString(), "true");

    auto autoSwitchFalse = parseKvCtlCommand(
        {QStringLiteral("set"), QStringLiteral("auto-profile-switch"), QStringLiteral("false")}, {},
        false);
    ASSERT_TRUE(autoSwitchFalse.ok) << autoSwitchFalse.error.toStdString();
    EXPECT_EQ(autoSwitchFalse.command.field, KvCtlCommand::Field::AutoProfileSwitch);
    EXPECT_EQ(autoSwitchFalse.command.value.toStdString(), "false");
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

TEST(KvCtlCommand, ParsesMuteOnOff)
{
    auto on = parseKvCtlCommand({QStringLiteral("mute"), QStringLiteral("on")}, {}, false);
    ASSERT_TRUE(on.ok) << on.error.toStdString();
    EXPECT_EQ(on.command.action, KvCtlCommand::Action::SetMute);
    EXPECT_EQ(on.command.value.toStdString(), "true");
    EXPECT_TRUE(on.command.profile.isEmpty());

    auto off = parseKvCtlCommand({QStringLiteral("mute"), QStringLiteral("off")}, {}, false);
    ASSERT_TRUE(off.ok) << off.error.toStdString();
    EXPECT_EQ(off.command.action, KvCtlCommand::Action::SetMute);
    EXPECT_EQ(off.command.value.toStdString(), "false");

    // Aliases — true/false, 1/0, yes/no all map to the same SetMute action.
    for (const QString& token :
         {QStringLiteral("true"), QStringLiteral("1"), QStringLiteral("yes")})
    {
        auto r = parseKvCtlCommand({QStringLiteral("mute"), token}, {}, false);
        ASSERT_TRUE(r.ok) << token.toStdString();
        EXPECT_EQ(r.command.action, KvCtlCommand::Action::SetMute);
        EXPECT_EQ(r.command.value.toStdString(), "true");
    }
    for (const QString& token :
         {QStringLiteral("false"), QStringLiteral("0"), QStringLiteral("no")})
    {
        auto r = parseKvCtlCommand({QStringLiteral("mute"), token}, {}, false);
        ASSERT_TRUE(r.ok) << token.toStdString();
        EXPECT_EQ(r.command.action, KvCtlCommand::Action::SetMute);
        EXPECT_EQ(r.command.value.toStdString(), "false");
    }
}

TEST(KvCtlCommand, ParsesMuteOnWithProfile)
{
    auto result = parseKvCtlCommand({QStringLiteral("mute"), QStringLiteral("on")},
                                    QStringLiteral("spotify"), true);
    ASSERT_TRUE(result.ok) << result.error.toStdString();
    EXPECT_EQ(result.command.action, KvCtlCommand::Action::SetMute);
    EXPECT_EQ(result.command.value.toStdString(), "true");
    EXPECT_EQ(result.command.profile.toStdString(), "spotify");
}

TEST(KvCtlCommand, BareMuteStillToggles)
{
    auto bare = parseKvCtlCommand({QStringLiteral("mute")}, {}, false);
    ASSERT_TRUE(bare.ok) << bare.error.toStdString();
    EXPECT_EQ(bare.command.action, KvCtlCommand::Action::ToggleMute);

    auto bareProfile = parseKvCtlCommand({QStringLiteral("mute")}, QStringLiteral("firefox"), true);
    ASSERT_TRUE(bareProfile.ok) << bareProfile.error.toStdString();
    EXPECT_EQ(bareProfile.command.action, KvCtlCommand::Action::ToggleMute);
    EXPECT_EQ(bareProfile.command.profile.toStdString(), "firefox");
}

TEST(KvCtlCommand, RejectsInvalidMuteState)
{
    EXPECT_FALSE(
        parseKvCtlCommand({QStringLiteral("mute"), QStringLiteral("bogus")}, {}, false).ok);
    EXPECT_FALSE(
        parseKvCtlCommand({QStringLiteral("mute"), QStringLiteral("on"), QStringLiteral("extra")},
                          {}, false)
            .ok);
}

TEST(KvCtlCommand, ParsesMediaSubcommands)
{
    struct Case
    {
        const char* sub;
        KvCtlCommand::Action expected;
    };
    const Case cases[] = {
        {"play-pause", KvCtlCommand::Action::MediaPlayPause},
        {"next", KvCtlCommand::Action::MediaNext},
        {"previous", KvCtlCommand::Action::MediaPrevious},
        {"stop", KvCtlCommand::Action::MediaStop},
    };

    for (const Case& c : cases)
    {
        auto result =
            parseKvCtlCommand({QStringLiteral("media"), QString::fromUtf8(c.sub)}, {}, false);
        ASSERT_TRUE(result.ok) << c.sub << ": " << result.error.toStdString();
        EXPECT_EQ(result.command.action, c.expected) << c.sub;
        EXPECT_TRUE(result.command.profile.isEmpty()) << c.sub;
    }
}

TEST(KvCtlCommand, RejectsInvalidMediaSubcommand)
{
    // Missing subcommand
    EXPECT_FALSE(parseKvCtlCommand({QStringLiteral("media")}, {}, false).ok);

    // Unknown subcommand
    EXPECT_FALSE(
        parseKvCtlCommand({QStringLiteral("media"), QStringLiteral("rewind")}, {}, false).ok);

    // Extra positional
    EXPECT_FALSE(
        parseKvCtlCommand(
            {QStringLiteral("media"), QStringLiteral("next"), QStringLiteral("extra")}, {}, false)
            .ok);

    // --profile is rejected — media controls are global, not per-profile.
    EXPECT_FALSE(parseKvCtlCommand({QStringLiteral("media"), QStringLiteral("play-pause")},
                                   QStringLiteral("spotify"), true)
                     .ok);
}
