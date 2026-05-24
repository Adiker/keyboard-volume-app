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

Profile makeProfile(const QString& id, const QStringList& apps, bool autoSwitch = true)
{
    Profile profile;
    profile.id = id;
    profile.name = id;
    profile.apps = apps;
    profile.autoSwitch = autoSwitch;
    return profile;
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

TEST(AppMatcher, ExactBinaryMatchReturnsBinaryTarget)
{
    const QList<AudioApp> cache{makeApp(QStringLiteral("Firefox"), QStringLiteral("firefox"))};
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("firefox"), cache), QStringLiteral("firefox"));
}

TEST(AppMatcher, SubstringMatchInBothDirections)
{
    // Cached app is "firefox"; focused window is "firefox-developer-edition".
    // lower.contains(appBinary) wins.
    const QList<AudioApp> cache{makeApp(QStringLiteral("Firefox"), QStringLiteral("firefox"))};
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("firefox-developer-edition"), cache),
              QStringLiteral("firefox"));

    // Cached app is the longer name; focused window is the short binary.
    // appBinary.contains(lower) wins.
    const QList<AudioApp> cache2{
        makeApp(QStringLiteral("youtube-music"), QStringLiteral("youtube-music"))};
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("youtube"), cache2), QStringLiteral("youtube-music"));
}

TEST(AppMatcher, NormalizedAppIdMatchesBinaryTarget)
{
    const QList<AudioApp> cache{
        makeApp(QStringLiteral("YouTube Music"), QStringLiteral("youtube-music"))};
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("YouTube Music"), cache),
              QStringLiteral("youtube-music"));
}

TEST(AppMatcher, NormalizedAppIdSkipsEarlierNonMatches)
{
    const QList<AudioApp> cache{
        makeApp(QStringLiteral("harmonoid"), QStringLiteral("harmonoid")),
        makeApp(QStringLiteral("youtube-music"), QStringLiteral("youtube-music"))};
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("YouTube Music"), cache),
              QStringLiteral("youtube-music"));
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
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("firefox"), cache), QStringLiteral("firefox"));
}

TEST(AppMatcher, CaseInsensitive)
{
    const QList<AudioApp> cache{makeApp(QStringLiteral("Firefox"), QStringLiteral("firefox"))};
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("FIREFOX"), cache), QStringLiteral("firefox"));
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("FireFox"), cache), QStringLiteral("firefox"));
}

TEST(AppMatcher, MatchesAfterAppWithEmptyFields)
{
    // Entries with empty fields must not short-circuit the loop — a later
    // real match must still be found.
    QList<AudioApp> cache;
    cache.append(makeApp(QString(), QString()));
    cache.append(makeApp(QStringLiteral("Firefox"), QStringLiteral("firefox")));
    EXPECT_EQ(matchBinaryToApp(QStringLiteral("firefox"), cache), QStringLiteral("firefox"));
}

TEST(AppMatcher, StickyAutoProfileFocusOnProfiledAppSetsTarget)
{
    const QList<AudioApp> cache{makeApp(QStringLiteral("Spotify"), QStringLiteral("spotify"))};
    const QList<Profile> profiles{
        makeProfile(QStringLiteral("music"), {QStringLiteral("spotify")})};

    EXPECT_EQ(resolveStickyAutoProfileTarget(QStringLiteral("spotify"), cache, profiles, QString()),
              QStringLiteral("spotify"));
}

TEST(AppMatcher, StickyAutoProfileUnprofiledFocusKeepsPreviousTarget)
{
    const QList<AudioApp> cache{makeApp(QStringLiteral("Spotify"), QStringLiteral("spotify")),
                                makeApp(QStringLiteral("Firefox"), QStringLiteral("firefox"))};
    const QList<Profile> profiles{
        makeProfile(QStringLiteral("music"), {QStringLiteral("spotify")})};

    EXPECT_EQ(resolveStickyAutoProfileTarget(QStringLiteral("firefox"), cache, profiles,
                                             QStringLiteral("spotify")),
              QStringLiteral("spotify"));
}

TEST(AppMatcher, StickyAutoProfileEmptyFocusKeepsPreviousTarget)
{
    const QList<AudioApp> cache{makeApp(QStringLiteral("Spotify"), QStringLiteral("spotify"))};
    const QList<Profile> profiles{
        makeProfile(QStringLiteral("music"), {QStringLiteral("spotify")})};

    EXPECT_EQ(resolveStickyAutoProfileTarget(QString(), cache, profiles, QStringLiteral("spotify")),
              QStringLiteral("spotify"));
}

TEST(AppMatcher, StickyAutoProfileOtherProfiledFocusSwitchesTarget)
{
    const QList<AudioApp> cache{makeApp(QStringLiteral("Spotify"), QStringLiteral("spotify")),
                                makeApp(QStringLiteral("Firefox"), QStringLiteral("firefox"))};
    const QList<Profile> profiles{
        makeProfile(QStringLiteral("music"), {QStringLiteral("spotify")}),
        makeProfile(QStringLiteral("browser"), {QStringLiteral("firefox")})};

    EXPECT_EQ(resolveStickyAutoProfileTarget(QStringLiteral("firefox"), cache, profiles,
                                             QStringLiteral("spotify")),
              QStringLiteral("firefox"));
}

TEST(AppMatcher, StickyAutoProfileDisabledProfileDoesNotSwitchTarget)
{
    const QList<AudioApp> cache{makeApp(QStringLiteral("Spotify"), QStringLiteral("spotify")),
                                makeApp(QStringLiteral("Firefox"), QStringLiteral("firefox"))};
    const QList<Profile> profiles{
        makeProfile(QStringLiteral("music"), {QStringLiteral("spotify")}),
        makeProfile(QStringLiteral("browser"), {QStringLiteral("firefox")}, false)};

    EXPECT_EQ(resolveStickyAutoProfileTarget(QStringLiteral("firefox"), cache, profiles,
                                             QStringLiteral("spotify")),
              QStringLiteral("spotify"));
}

TEST(AppMatcher, StickyAutoProfileValidationClearsRemovedOrDisabledTarget)
{
    const QList<Profile> activeProfiles{
        makeProfile(QStringLiteral("music"), {QStringLiteral("spotify")})};
    const QList<Profile> disabledProfiles{
        makeProfile(QStringLiteral("music"), {QStringLiteral("spotify")}, false)};

    EXPECT_EQ(validateStickyAutoProfileTarget(QStringLiteral("spotify"), activeProfiles),
              QStringLiteral("spotify"));
    EXPECT_EQ(validateStickyAutoProfileTarget(QStringLiteral("spotify"), disabledProfiles),
              QString());
    EXPECT_EQ(validateStickyAutoProfileTarget(QStringLiteral("spotify"), {}), QString());
}
