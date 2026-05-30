#include "osdlabelformat.h"

#include <gtest/gtest.h>
#include <QString>

namespace
{

LabelTokens fullTokens()
{
    LabelTokens t;
    t.app = QStringLiteral("spotify");
    t.player = QStringLiteral("Spotify");
    t.title = QStringLiteral("Title");
    t.artist = QStringLiteral("Artist");
    t.album = QStringLiteral("Album");
    return t;
}

} // namespace

TEST(OsdLabelFormat, SubstitutesAllTokens)
{
    const QString out = formatOsdLabelTemplate(
        QStringLiteral("{player}: {title} — {artist} [{album}] ({app})"), fullTokens());
    EXPECT_EQ(out.toStdString(), "Spotify: Title — Artist [Album] (spotify)");
}

TEST(OsdLabelFormat, EmptyTokenTrimsTrailingSeparator)
{
    LabelTokens t = fullTokens();
    t.artist.clear();
    const QString out = formatOsdLabelTemplate(QStringLiteral("{title} — {artist}"), t);
    EXPECT_EQ(out.toStdString(), "Title");
}

TEST(OsdLabelFormat, EmptyTokenTrimsLeadingSeparator)
{
    LabelTokens t = fullTokens();
    t.title.clear();
    const QString out = formatOsdLabelTemplate(QStringLiteral("{title} — {artist}"), t);
    EXPECT_EQ(out.toStdString(), "Artist");
}

TEST(OsdLabelFormat, EmptyMiddleTokenCollapses)
{
    LabelTokens t = fullTokens();
    t.title.clear();
    const QString out = formatOsdLabelTemplate(QStringLiteral("{player} — {title} — {artist}"), t);
    // After substitution: "Spotify —  — Artist"; collapse to single separator.
    EXPECT_EQ(out.toStdString(), "Spotify — Artist");
}

TEST(OsdLabelFormat, UnknownTokenStaysLiteral)
{
    const QString out = formatOsdLabelTemplate(QStringLiteral("{nope} {title}"), fullTokens());
    EXPECT_EQ(out.toStdString(), "{nope} Title");
}

TEST(OsdLabelFormat, AllEmptyReturnsEmpty)
{
    LabelTokens t; // all empty
    const QString out = formatOsdLabelTemplate(QStringLiteral("{title} — {artist}"), t);
    EXPECT_TRUE(out.isEmpty());
}

TEST(OsdLabelFormat, EmptyTemplateReturnsEmpty)
{
    const QString out = formatOsdLabelTemplate(QString{}, fullTokens());
    EXPECT_TRUE(out.isEmpty());
}

TEST(OsdLabelFormat, TokenAppearsMultipleTimes)
{
    LabelTokens t;
    t.title = QStringLiteral("Hey");
    const QString out = formatOsdLabelTemplate(QStringLiteral("{title} {title} {title}"), t);
    EXPECT_EQ(out.toStdString(), "Hey Hey Hey");
}

TEST(OsdLabelFormat, PreservesInternalWhitespaceInTokenValues)
{
    LabelTokens t;
    t.title = QStringLiteral("Title With Spaces");
    t.artist = QStringLiteral("Artist Name");
    const QString out = formatOsdLabelTemplate(QStringLiteral("{title} — {artist}"), t);
    EXPECT_EQ(out.toStdString(), "Title With Spaces — Artist Name");
}
