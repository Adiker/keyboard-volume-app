// test_osdwindow.cpp
// Focused OSDWindow progress-row regressions.

#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>
#include <QProgressBar>
#include <QScreen>
#include <QTemporaryDir>

#define private public
#include "osdwindow.h"
#undef private

#include "config.h"

bool g_nativeWayland = false;

TEST(OSDWindowProgress, SameTrackMetadataUpdatePreservesPosition)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    Config config(tmp.path());
    OsdConfig osd = config.osd();
    osd.progressEnabled = true;
    config.setOsd(osd);

    OSDWindow window(&config);
    window.setProgressEnabled(true);
    window.setProgressVisible(true);

    window.updateTrack(QStringLiteral("Superstar"), QStringLiteral("Jamelia"), 100000000LL, true);
    window.updatePosition(50000000LL);
    ASSERT_EQ(window.m_progressBar->value(), 500);

    window.updateTrack(QStringLiteral("Superstar"), QStringLiteral("Jamelia"), 200000000LL, false);
    EXPECT_EQ(window.m_progressBar->value(), 250);
    EXPECT_EQ(window.m_labelTime->text(), QStringLiteral("0:50 / 3:20"));

    window.updateTrack(QStringLiteral("Superstar"), QStringLiteral("Jamelia"), 0LL, false);
    EXPECT_EQ(window.m_progressBar->value(), 250);
    EXPECT_EQ(window.m_labelTime->text(), QStringLiteral("0:50 / 3:20"));

    window.updateTrack(QStringLiteral("Different"), QStringLiteral("Artist"), 200000000LL, true);
    EXPECT_EQ(window.m_progressBar->value(), 0);
    EXPECT_EQ(window.m_labelTime->text(), QStringLiteral("0:00 / 3:20"));
}

TEST(OSDWindowLabel, AppOnlyPresetHidesBottomLine)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());

    OSDWindow window(&config);
    window.showVolume(QStringLiteral("spotify"), 0.5, false);
    window.setPlayerName(QStringLiteral("Spotify"));
    window.updateTrack(QStringLiteral("T"), QStringLiteral("A"), QStringLiteral("Alb"), 100000000LL,
                       true);

    EXPECT_EQ(window.m_labelName->text().toStdString(), "spotify");
    // Bottom track label hidden when progress row is not visible.
    EXPECT_FALSE(window.m_labelTrack->isVisible());
    EXPECT_FALSE(window.m_albumArtVisible);
}

TEST(OSDWindowLabel, PlayerTrackPresetPopulatesBothLines)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());

    OsdConfig osd = config.osd();
    osd.progressEnabled = true;
    osd.progressLabelMode = QStringLiteral("player_track");
    config.setOsd(osd);

    OSDWindow window(&config);
    window.setProgressEnabled(true);
    window.setProgressVisible(true);
    window.showVolume(QStringLiteral("spotify"), 0.5);
    window.setPlayerName(QStringLiteral("Spotify"));
    window.updateTrack(QStringLiteral("Title"), QStringLiteral("Artist"), QStringLiteral("Album"),
                       60000000LL, true);

    EXPECT_EQ(window.m_labelName->text().toStdString(), "Spotify");
    EXPECT_EQ(window.m_labelTrack->text().toStdString(), "Title \xE2\x80\x94 Artist");
    EXPECT_FALSE(window.m_albumArtVisible);
}

TEST(OSDWindowGeometry, VolumeAfterMediaActionClampsWithRestoredProgressHeight)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());

    OsdConfig osd = config.osd();
    osd.progressEnabled = true;
    osd.y = 100000;
    config.setOsd(osd);

    OSDWindow window(&config);
    window.setProgressEnabled(true);
    window.setProgressVisible(true);

    window.showVolume(QStringLiteral("spotify"), 0.5);
    const int progressHeight = window.height();

    window.showMediaAction(QStringLiteral("Next"));
    EXPECT_LT(window.height(), progressHeight);

    window.showVolume(QStringLiteral("spotify"), 0.6);
    EXPECT_EQ(window.height(), progressHeight);

    ASSERT_NE(QApplication::primaryScreen(), nullptr);
    const QRect avail = QApplication::primaryScreen()->availableGeometry();
    EXPECT_LE(window.y() + window.height(), avail.bottom() + 1);
}

TEST(OSDWindowMediaAction, MprisUpdatesDoNotReplaceActionOverlay)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());

    OsdConfig osd = config.osd();
    osd.progressEnabled = true;
    osd.progressLabelMode = QStringLiteral("player_track");
    config.setOsd(osd);

    OSDWindow window(&config);
    window.setProgressEnabled(true);
    window.setProgressVisible(true);
    window.showVolume(QStringLiteral("spotify"), 0.5);
    const int progressHeight = window.height();

    window.showMediaAction(QStringLiteral("Next"));
    const int actionHeight = window.height();

    window.setPlayerName(QStringLiteral("Spotify"));
    window.updateTrack(QStringLiteral("New Title"), QStringLiteral("Artist"),
                       QStringLiteral("Album"), 60000000LL, true);
    window.setProgressVisible(true);

    EXPECT_TRUE(window.m_mediaActionMode);
    EXPECT_EQ(window.m_labelName->text(), QStringLiteral("Next"));
    EXPECT_FALSE(window.m_progressRow->isVisible());
    EXPECT_EQ(window.height(), actionHeight);

    window.showVolume(QStringLiteral("spotify"), 0.6);
    EXPECT_FALSE(window.m_mediaActionMode);
    EXPECT_TRUE(window.m_progressRow->isVisible());
    EXPECT_EQ(window.height(), progressHeight);
    EXPECT_EQ(window.m_labelName->text(), QStringLiteral("Spotify"));
    EXPECT_EQ(window.m_labelTrack->text().toStdString(), "New Title \xE2\x80\x94 Artist");
}

TEST(OSDWindowLabel, PlayerTrackArtPresetEnablesArt)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());

    OsdConfig osd = config.osd();
    osd.progressEnabled = true;
    osd.progressLabelMode = QStringLiteral("player_track_art");
    config.setOsd(osd);

    OSDWindow window(&config);
    window.setProgressEnabled(true);
    window.setProgressVisible(true);

    EXPECT_TRUE(window.m_albumArtVisible);
}

TEST(OSDWindowLabel, CustomPresetUsesTemplates)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());

    OsdConfig osd = config.osd();
    osd.progressEnabled = true;
    osd.progressLabelMode = QStringLiteral("custom");
    osd.customLabelTop = QStringLiteral("{player}");
    osd.customLabelBottom = QStringLiteral("{album}");
    osd.customLabelShowArt = true;
    config.setOsd(osd);

    OSDWindow window(&config);
    window.setProgressEnabled(true);
    window.setProgressVisible(true);
    window.setPlayerName(QStringLiteral("Spotify"));
    window.updateTrack(QStringLiteral("Title"), QStringLiteral("Artist"),
                       QStringLiteral("My Album"), 60000000LL, true);

    EXPECT_EQ(window.m_labelName->text().toStdString(), "Spotify");
    EXPECT_EQ(window.m_labelTrack->text().toStdString(), "My Album");
    EXPECT_TRUE(window.m_albumArtVisible);
}

TEST(OSDWindowLabel, CustomEmptyBottomHidesTrackLabel)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());

    OsdConfig osd = config.osd();
    osd.progressEnabled = true;
    osd.progressLabelMode = QStringLiteral("custom");
    osd.customLabelTop = QStringLiteral("{title}");
    osd.customLabelBottom = QString{};
    config.setOsd(osd);

    OSDWindow window(&config);
    window.setProgressEnabled(true);
    window.setProgressVisible(true);
    window.updateTrack(QStringLiteral("Hello"), QStringLiteral("X"), QString{}, 60000000LL, true);

    EXPECT_EQ(window.m_labelName->text().toStdString(), "Hello");
    EXPECT_FALSE(window.m_labelTrack->isVisible());
}

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
