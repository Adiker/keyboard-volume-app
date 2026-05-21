#include <gtest/gtest.h>

#include "appmatcher.h"
#include "audioapp.h"

namespace
{

AudioApp makeApp(const QString& name, const QString& binary)
{
    AudioApp app;
    app.name = name;
    app.binary = binary;
    return app;
}

} // namespace

TEST(AppMatcher, EmptyBinaryReturnsEmpty)
{
    const QList<AudioApp> cache{makeApp(QStringLiteral("Spotify"), QStringLiteral("spotify"))};
    EXPECT_EQ(matchBinaryToApp(QString(), cache), QString());
    EXPECT_EQ(matchBinaryToApp(QStringLiteral(""), cache), QString());
}

TEST(AppMatcher, EmptyCacheReturnsEmpty)
{
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("firefox"), {}), QString());
}

TEST(AppMatcher, NoMatchReturnsEmpty)
{
    const QList<AudioApp> cache{makeApp(QStringLiteral("Spotify"), QStringLiteral("spotify"))};
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("firefox"), cache), QString());
}

TEST(AppMatcher, ExactBinaryMatchReturnsAppName)
{
    const QList<AudioApp> cache{makeApp(QStringLiteral("Firefox"), QStringLiteral("firefox"))};
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("firefox"), cache), QStringLiteral("Firefox"));
}

TEST(AppMatcher, SubstringMatchInBothDirections)
{
    // Cached app is "firefox"; focused window is "firefox-developer-edition".
    // lower.contains(appBinary) wins.
    const QList<AudioApp> cache{makeApp(QStringLiteral("Firefox"), QStringLiteral("firefox"))};
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("firefox-developer-edition"), cache),
              QStringLiteral("Firefox"));

    // Cached app is the longer name; focused window is the short binary.
    // appBinary.contains(lower) wins.
    const QList<AudioApp> cache2{
        makeApp(QStringLiteral("youtube-music"), QStringLiteral("youtube-music"))};
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("youtube"), cache2), QStringLiteral("youtube-music"));
}

TEST(AppMatcher, EmptyBinaryFieldDoesNotFalseMatch)
{
    // Regression: QString::contains("") returns true, so without the empty
    // guard an AudioApp with an empty binary would match every focused
    // window and break auto-profile switching.
    const QList<AudioApp> cache{makeApp(QStringLiteral("Spotify"), QString())};
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("firefox"), cache), QString());
}

TEST(AppMatcher, EmptyNameFieldDoesNotFalseMatch)
{
    // Same regression on the AudioApp::name field.
    const QList<AudioApp> cache{makeApp(QString(), QStringLiteral("spotify"))};
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("firefox"), cache), QString());
}

TEST(AppMatcher, EmptyBinaryFieldStillMatchesByName)
{
    // An AudioApp with a populated name but empty binary must still match by
    // name — the guard only suppresses the empty-field false-positive, not
    // the legitimate match on the other field.
    const QList<AudioApp> cache{makeApp(QStringLiteral("Spotify"), QString())};
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("spotify"), cache), QStringLiteral("Spotify"));
}

TEST(AppMatcher, FirstMatchWins)
{
    QList<AudioApp> cache;
    cache.append(makeApp(QStringLiteral("Firefox"), QStringLiteral("firefox")));
    cache.append(makeApp(QStringLiteral("Firefox Dev"), QStringLiteral("firefox-developer")));
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("firefox"), cache), QStringLiteral("Firefox"));
}

TEST(AppMatcher, CaseInsensitive)
{
    const QList<AudioApp> cache{makeApp(QStringLiteral("Firefox"), QStringLiteral("firefox"))};
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("FIREFOX"), cache), QStringLiteral("Firefox"));
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("FireFox"), cache), QStringLiteral("Firefox"));
}

TEST(AppMatcher, MatchesAfterAppWithEmptyFields)
{
    // Entries with empty fields must not short-circuit the loop — a later
    // real match must still be found.
    QList<AudioApp> cache;
    cache.append(makeApp(QString(), QString()));
    cache.append(makeApp(QStringLiteral("Firefox"), QStringLiteral("firefox")));
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("firefox"), cache), QStringLiteral("Firefox"));
}
