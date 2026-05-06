#include <gtest/gtest.h>

#include <algorithm>

#include "pwutils.h"

namespace
{
bool containsClient(const QList<PipeWireClient>& clients, const QString& name,
                    const QString& binary)
{
    return std::any_of(clients.begin(), clients.end(), [&](const PipeWireClient& client)
                       { return client.name == name && client.binary == binary; });
}
} // namespace

TEST(PwUtils, FiltersSystemClients)
{
    QList<PipeWireGlobalProps> globals{
        {QStringLiteral("PipeWire:Interface:Client"), QStringLiteral("Firefox"),
         QStringLiteral("firefox")},
        {QStringLiteral("PipeWire:Interface:Client"), QStringLiteral("PipeWire"),
         QStringLiteral("pipewire")},
        {QStringLiteral("PipeWire:Interface:Client"), QStringLiteral("keyboard-volume-app"),
         QStringLiteral("keyboard-volume-app")},
        {QStringLiteral("PipeWire:Interface:Client"), QStringLiteral("Portal"),
         QStringLiteral("xdg-desktop-portal")},
    };

    const auto clients = clientsFromPipeWireGlobals(globals);

    ASSERT_EQ(clients.size(), 1);
    EXPECT_EQ(clients[0].name, QStringLiteral("Firefox"));
    EXPECT_EQ(clients[0].binary, QStringLiteral("firefox"));
}

TEST(PwUtils, UsesBinaryWhenNameMissingOrSkipped)
{
    QList<PipeWireGlobalProps> globals{
        {QStringLiteral("PipeWire:Interface:Client"), QString(), QStringLiteral("vlc")},
        {QStringLiteral("PipeWire:Interface:Client"), QStringLiteral("WEBRTC VoiceEngine"),
         QStringLiteral("discord")},
        {QStringLiteral("PipeWire:Interface:Client"), QStringLiteral("Chromium input"),
         QStringLiteral("chromium")},
    };

    const auto clients = clientsFromPipeWireGlobals(globals);

    ASSERT_EQ(clients.size(), 3);
    EXPECT_TRUE(containsClient(clients, QStringLiteral("chromium"), QStringLiteral("chromium")));
    EXPECT_TRUE(containsClient(clients, QStringLiteral("discord"), QStringLiteral("discord")));
    EXPECT_TRUE(containsClient(clients, QStringLiteral("vlc"), QStringLiteral("vlc")));
}

TEST(PwUtils, DeduplicatesByDisplayName)
{
    QList<PipeWireGlobalProps> globals{
        {QStringLiteral("PipeWire:Interface:Client"), QStringLiteral("Firefox"),
         QStringLiteral("firefox")},
        {QStringLiteral("PipeWire:Interface:Client"), QStringLiteral("Firefox"),
         QStringLiteral("firefox-bin")},
        {QStringLiteral("PipeWire:Interface:Node"), QStringLiteral("Ignored"),
         QStringLiteral("ignored")},
    };

    const auto clients = clientsFromPipeWireGlobals(globals);

    ASSERT_EQ(clients.size(), 1);
    EXPECT_EQ(clients[0].name, QStringLiteral("Firefox"));
    EXPECT_EQ(clients[0].binary, QStringLiteral("firefox-bin"));
}

TEST(PwUtils, IncludesStreamNodes)
{
    QList<PipeWireGlobalProps> globals{
        {QStringLiteral("PipeWire:Interface:Client"), QStringLiteral("Harmonoid"),
         QStringLiteral("harmonoid")},
        {QStringLiteral("PipeWire:Interface:Node"), QStringLiteral("mpv"), QStringLiteral("mpv"),
         QStringLiteral("Stream/Output/Audio")},
        {QStringLiteral("PipeWire:Interface:Node"), QStringLiteral("alsa_output"),
         QStringLiteral("alsa"), QStringLiteral("Audio/Sink")},
    };

    const auto clients = clientsFromPipeWireGlobals(globals);

    ASSERT_EQ(clients.size(), 2);
    EXPECT_TRUE(containsClient(clients, QStringLiteral("Harmonoid"), QStringLiteral("harmonoid")));
    EXPECT_TRUE(containsClient(clients, QStringLiteral("mpv"), QStringLiteral("mpv")));
}

TEST(PwUtils, NodeWithoutStreamMediaClassSkipped)
{
    QList<PipeWireGlobalProps> globals{
        {QStringLiteral("PipeWire:Interface:Client"), QStringLiteral("Firefox"),
         QStringLiteral("firefox")},
        {QStringLiteral("PipeWire:Interface:Node"), QStringLiteral("SomeSink"),
         QStringLiteral("pulse"), QStringLiteral("Audio/Sink")},
        {QStringLiteral("PipeWire:Interface:Node"), QStringLiteral("SomeSource"),
         QStringLiteral("pulse"), QStringLiteral("Audio/Source")},
        {QStringLiteral("PipeWire:Interface:Node"), QStringLiteral("VideoNode"),
         QStringLiteral("obs"), QStringLiteral("Video/Source")},
    };

    const auto clients = clientsFromPipeWireGlobals(globals);

    ASSERT_EQ(clients.size(), 1);
    EXPECT_EQ(clients[0].name, QStringLiteral("Firefox"));
}

TEST(PwUtils, StreamNodeNameOverridesSkippedAppName)
{
    QList<PipeWireGlobalProps> globals{
        {QStringLiteral("PipeWire:Interface:Node"), QStringLiteral("ringrtc"),
         QStringLiteral("discord"), QStringLiteral("Stream/Output/Audio")},
    };

    const auto clients = clientsFromPipeWireGlobals(globals);

    ASSERT_EQ(clients.size(), 1);
    EXPECT_EQ(clients[0].name, QStringLiteral("discord"));
    EXPECT_EQ(clients[0].binary, QStringLiteral("discord"));
}

TEST(PwUtils, StreamNodeUsesNodeName)
{
    QList<PipeWireGlobalProps> globals{
        {QStringLiteral("PipeWire:Interface:Client"), QStringLiteral("HostApp"),
         QStringLiteral("hostapp")},
        {QStringLiteral("PipeWire:Interface:Node"), QStringLiteral("HostApp"),
         QStringLiteral("hostapp"), QStringLiteral("Stream/Output/Audio"), QStringLiteral("mpv")},
    };

    const auto clients = clientsFromPipeWireGlobals(globals);

    ASSERT_EQ(clients.size(), 2);
    EXPECT_TRUE(containsClient(clients, QStringLiteral("HostApp"), QStringLiteral("hostapp")));
    EXPECT_TRUE(containsClient(clients, QStringLiteral("mpv"), QStringLiteral("hostapp")));
}

TEST(PwUtils, StreamNodeIgnoresNodeNameSameAsAppName)
{
    QList<PipeWireGlobalProps> globals{
        {QStringLiteral("PipeWire:Interface:Node"), QStringLiteral("mpv"), QStringLiteral("mpv"),
         QStringLiteral("Stream/Output/Audio"), QStringLiteral("mpv")},
    };

    const auto clients = clientsFromPipeWireGlobals(globals);

    ASSERT_EQ(clients.size(), 1);
    EXPECT_EQ(clients[0].name, QStringLiteral("mpv"));
    EXPECT_EQ(clients[0].binary, QStringLiteral("mpv"));
}
