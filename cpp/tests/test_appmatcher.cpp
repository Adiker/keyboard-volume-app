#include <gtest/gtest.h>

#include "appmatcher.h"
#include "audioapp.h"

namespace
{

AudioApp makeApp(const QString& name, const QString& binary)
{
    AudioApp a;
    a.name = name;
    a.binary = binary;
    return a;
}

} // namespace

TEST(AppMatcher, ReturnsEmptyForEmptyBinary)
{
    QList<AudioApp> apps = {makeApp(QStringLiteral("Spotify"), QStringLiteral("spotify"))};
    EXPECT_TRUE(matchBinaryToApp(QString{}, apps).isEmpty());
}

TEST(AppMatcher, ReturnsEmptyForEmptyCache)
{
    EXPECT_TRUE(matchBinaryToApp(QStringLiteral("firefox"), {}).isEmpty());
}

TEST(AppMatcher, ExactBinaryMatch)
{
    QList<AudioApp> apps = {
        makeApp(QStringLiteral("Spotify"), QStringLiteral("spotify")),
        makeApp(QStringLiteral("Firefox"), QStringLiteral("firefox")),
    };
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("firefox"), apps).toStdString(), "Firefox");
}

TEST(AppMatcher, CaseInsensitiveMatch)
{
    QList<AudioApp> apps = {makeApp(QStringLiteral("Firefox"), QStringLiteral("firefox"))};
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("FIREFOX"), apps).toStdString(), "Firefox");
}

TEST(AppMatcher, FocusBinaryContainsAppName)
{
    QList<AudioApp> apps = {makeApp(QStringLiteral("Firefox"), QStringLiteral("firefox"))};
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("firefox-developer-edition"), apps).toStdString(),
              "Firefox");
}

TEST(AppMatcher, AppBinaryContainsFocusBinary)
{
    QList<AudioApp> apps = {
        makeApp(QStringLiteral("YouTube Music"), QStringLiteral("youtube-music"))};
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("youtube"), apps).toStdString(), "YouTube Music");
}

// Regression: an AudioApp with an empty name or binary must NOT match every
// focused window. QString::contains("") returns true, so without an explicit
// guard the bidirectional substring check would treat any focused binary as a
// match against the empty field — silently hijacking auto-profile switching.
TEST(AppMatcher, EmptyBinaryFieldDoesNotMatchEverything)
{
    QList<AudioApp> apps = {
        makeApp(QStringLiteral("Spotify"), QString{}),
        makeApp(QStringLiteral("Firefox"), QStringLiteral("firefox")),
    };
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("firefox"), apps).toStdString(), "Firefox");
    EXPECT_TRUE(matchBinaryToApp(QStringLiteral("kitty"), apps).isEmpty());
}

TEST(AppMatcher, EmptyNameFieldDoesNotMatchEverything)
{
    QList<AudioApp> apps = {
        makeApp(QString{}, QStringLiteral("spotifyd")),
        makeApp(QStringLiteral("Firefox"), QStringLiteral("firefox")),
    };
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("firefox"), apps).toStdString(), "Firefox");
    EXPECT_TRUE(matchBinaryToApp(QStringLiteral("kitty"), apps).isEmpty());
}

TEST(AppMatcher, BothFieldsEmptyNeverMatches)
{
    QList<AudioApp> apps = {makeApp(QString{}, QString{})};
    EXPECT_TRUE(matchBinaryToApp(QStringLiteral("firefox"), apps).isEmpty());
    EXPECT_TRUE(matchBinaryToApp(QStringLiteral("anything"), apps).isEmpty());
}

TEST(AppMatcher, NoMatchReturnsEmpty)
{
    QList<AudioApp> apps = {
        makeApp(QStringLiteral("Spotify"), QStringLiteral("spotify")),
        makeApp(QStringLiteral("Firefox"), QStringLiteral("firefox")),
    };
    EXPECT_TRUE(matchBinaryToApp(QStringLiteral("kitty"), apps).isEmpty());
}

TEST(AppMatcher, FirstMatchWinsOnTie)
{
    QList<AudioApp> apps = {
        makeApp(QStringLiteral("First"), QStringLiteral("firefox")),
        makeApp(QStringLiteral("Second"), QStringLiteral("firefox")),
    };
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("firefox"), apps).toStdString(), "First");
}
