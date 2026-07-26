#include <gtest/gtest.h>

#include "kvctloutput.h"

#include <QVariantList>
#include <QVariantMap>

TEST(KvCtlOutput, FormatsIdentityFieldsWithoutChangingLegacyColumns)
{
    const QVariantMap profile{
        {QStringLiteral("id"), QStringLiteral("comms")},
        {QStringLiteral("name"), QStringLiteral("Comms")},
        {QStringLiteral("app"), QStringLiteral("discord")},
        {QStringLiteral("apps"), QStringList{QStringLiteral("discord"), QStringLiteral("slack")}},
        {QStringLiteral("app_regex"), QStringLiteral(".*(discord|slack|teams).*")},
        {QStringLiteral("modifiers"), QStringList{QStringLiteral("ctrl")}},
        {QStringLiteral("hotkeys"),
         QVariantMap{
             {QStringLiteral("volume_up"), 115},
             {QStringLiteral("volume_down"), 114},
             {QStringLiteral("mute"), 113},
         }},
        {QStringLiteral("sink"), QStringLiteral("alsa_output.headset")},
    };

    EXPECT_EQ(formatKvCtlValue(QVariantList{profile}).toStdString(),
              "comms\tComms\tdiscord\tctrl\tup=115,down=114,mute=113\talsa_output.headset\t"
              "apps=discord,slack\tregex=.*(discord|slack|teams).*");
}

TEST(KvCtlOutput, FormatsSceneAndSinkMaps)
{
    const QVariantMap scene{
        {QStringLiteral("id"), QStringLiteral("meeting")},
        {QStringLiteral("name"), QStringLiteral("Meeting")},
        {QStringLiteral("targets"), QVariantList{QVariantMap{
                                        {QStringLiteral("match"), QStringLiteral("discord")},
                                        {QStringLiteral("volume"), 80},
                                        {QStringLiteral("muted"), true},
                                    }}},
    };
    EXPECT_EQ(formatKvCtlValue(QVariantList{scene}).toStdString(),
              "meeting\tMeeting\tdiscord,volume=80,muted=true");

    const QVariantMap sink{
        {QStringLiteral("name"), QStringLiteral("alsa_output.headset")},
        {QStringLiteral("description"), QStringLiteral("USB Headset")},
        {QStringLiteral("is_default"), true},
    };
    EXPECT_EQ(formatKvCtlValue(QVariantList{sink}).toStdString(),
              "alsa_output.headset\tUSB Headset (default)");
}
