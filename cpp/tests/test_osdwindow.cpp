// test_osdwindow.cpp
// Focused OSDWindow progress-row regressions.

#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QTemporaryDir>

#define private public
#include "osdwindow.h"
#undef private

#include "config.h"

bool g_nativeWayland = false;

namespace
{

void moveOsdConfig(Config& config, int x = 120, int y = 120)
{
    OsdConfig osd = config.osd();
    osd.x = x;
    osd.y = y;
    osd.timeoutMs = 5000;
    config.setOsd(osd);
}

QPoint screenRelativeToAbs(const OsdConfig& osd)
{
    const auto screens = QApplication::screens();
    if (screens.isEmpty()) return {osd.x, osd.y};
    int idx = osd.screen;
    if (idx < 0 || idx >= screens.size()) idx = 0;
    const QRect geo = screens[idx]->geometry();
    return {geo.x() + osd.x, geo.y() + osd.y};
}

} // namespace

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

TEST(OSDWindowResize, RightEdgePersistsScaleAndRestartsTimer)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());
    moveOsdConfig(config);

    OSDWindow window(&config);
    window.showVolume(QStringLiteral("spotify"), 0.5);
    QApplication::processEvents();

    const int oldWidth = window.width();
    const QPoint start = window.pos() + QPoint(window.width() - 2, window.height() / 2);
    ASSERT_TRUE(window.m_hideTimer->isActive());

    window.startResize(OSDWindow::EdgeRight, start);
    EXPECT_FALSE(window.m_hideTimer->isActive());
    window.updateResize(start + QPoint(44, 0));
    EXPECT_GT(window.width(), oldWidth);

    window.finishResize(true);
    EXPECT_TRUE(window.m_hideTimer->isActive());
    EXPECT_DOUBLE_EQ(window.m_previewScale, -1.0);
    EXPECT_NEAR(config.osd().osdScale, 1.2, 0.02);
}

TEST(OSDWindowResize, LeftTopResizeKeepsOppositeEdgesAnchored)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());
    moveOsdConfig(config, 140, 140);

    OSDWindow window(&config);
    window.showVolume(QStringLiteral("spotify"), 0.5);
    QApplication::processEvents();

    const int oldRight = window.pos().x() + window.width();
    const int oldBottom = window.pos().y() + window.height();
    const QPoint start = window.pos() + QPoint(2, 2);

    window.startResize(OSDWindow::EdgeLeft | OSDWindow::EdgeTop, start);
    window.updateResize(start - QPoint(22, 7));
    window.finishResize(true);

    EXPECT_NEAR(config.osd().osdScale, 1.1, 0.02);
    EXPECT_EQ(window.pos().x() + window.width(), oldRight);
    EXPECT_EQ(window.pos().y() + window.height(), oldBottom);
    EXPECT_EQ(screenRelativeToAbs(config.osd()), window.pos());
}

TEST(OSDWindowResize, ScaleIsClampedToConfigRange)
{
    {
        QTemporaryDir tmp;
        ASSERT_TRUE(tmp.isValid());
        Config config(tmp.path());
        moveOsdConfig(config);

        OSDWindow window(&config);
        window.showVolume(QStringLiteral("spotify"), 0.5);
        const QPoint start = window.pos() + QPoint(window.width() - 2, window.height() / 2);

        window.startResize(OSDWindow::EdgeRight, start);
        window.updateResize(start + QPoint(10000, 0));
        window.finishResize(true);
        EXPECT_DOUBLE_EQ(config.osd().osdScale, 3.0);
    }

    {
        QTemporaryDir tmp;
        ASSERT_TRUE(tmp.isValid());
        Config config(tmp.path());
        moveOsdConfig(config);

        OSDWindow window(&config);
        window.showVolume(QStringLiteral("spotify"), 0.5);
        const QPoint start = window.pos() + QPoint(window.width() - 2, window.height() / 2);

        window.startResize(OSDWindow::EdgeRight, start);
        window.updateResize(start - QPoint(10000, 0));
        window.finishResize(true);
        EXPECT_DOUBLE_EQ(config.osd().osdScale, 0.5);
    }
}

TEST(OSDWindowResize, PreviewHeldResizeDoesNotStartHideTimer)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());
    moveOsdConfig(config);

    OSDWindow window(&config);
    window.showPreviewHeld(0, 120, 120);
    QApplication::processEvents();
    ASSERT_FALSE(window.m_hideTimer->isActive());

    const QPoint start = window.pos() + QPoint(window.width() - 2, window.height() / 2);
    window.startResize(OSDWindow::EdgeRight, start);
    window.updateResize(start + QPoint(44, 0));
    window.finishResize(true);

    EXPECT_FALSE(window.m_hideTimer->isActive());
}

TEST(OSDWindowResize, ProgressBarCenterIsNotAResizeGrip)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());
    moveOsdConfig(config);
    OsdConfig osd = config.osd();
    osd.progressEnabled = true;
    config.setOsd(osd);

    OSDWindow window(&config);
    window.setProgressEnabled(true);
    window.setProgressVisible(true);
    window.showVolume(QStringLiteral("spotify"), 0.5);
    window.updateTrack(QStringLiteral("Title"), QStringLiteral("Artist"), 60000000LL, true);
    QApplication::processEvents();

    const QPoint center =
        window.m_progressBar->mapTo(&window, window.m_progressBar->rect().center());
    EXPECT_EQ(window.resizeEdgesAt(center), OSDWindow::EdgeNone);
}

void enablePositionControls(Config& config)
{
    OsdConfig osd = config.osd();
    osd.positionControlsEnabled = true;
    osd.positionArrowsEnabled = true;
    osd.positionDragEnabled = true;
    config.setOsd(osd);
}

TEST(OSDWindowPosition, SnapTopSetsYToAvailableTop)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());
    moveOsdConfig(config, 200, 200);
    enablePositionControls(config);

    OSDWindow window(&config);
    window.showVolume(QStringLiteral("spotify"), 0.5);
    QApplication::processEvents();

    QScreen* screen = QApplication::screenAt(window.pos());
    ASSERT_NE(screen, nullptr);
    const int expectedTop = screen->availableGeometry().top();

    window.snapUp();
    QApplication::processEvents();

    EXPECT_EQ(window.pos().y(), expectedTop);
    EXPECT_EQ(config.osd().y, expectedTop - screen->geometry().y());
}

TEST(OSDWindowPosition, DragPersistsPosition)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());
    moveOsdConfig(config, 160, 160);
    enablePositionControls(config);

    OSDWindow window(&config);
    window.showVolume(QStringLiteral("spotify"), 0.5);
    QApplication::processEvents();

    const QPoint startGlobal = window.mapToGlobal(window.rect().center());
    const QPoint startLocal = window.rect().center();
    window.startMove(startGlobal, startLocal);
    window.updateMove(startGlobal + QPoint(30, 20));
    window.finishMove(true);

    EXPECT_EQ(window.m_currentAbsPos, screenRelativeToAbs(config.osd()));
}

TEST(OSDWindowPosition, DisabledHidesArrowRow)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());

    OSDWindow window(&config);
    EXPECT_TRUE(window.m_btnPosUp->isHidden());

    enablePositionControls(config);
    window.setPositionControlsEnabled(true);
    EXPECT_FALSE(window.m_btnPosUp->isHidden());
}

TEST(OSDWindowPosition, StepScaleAnchorsCenter)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());
    moveOsdConfig(config, 160, 160);
    enablePositionControls(config);

    OSDWindow window(&config);
    window.showVolume(QStringLiteral("spotify"), 0.5);
    QApplication::processEvents();

    const QPoint centerBefore = window.pos() + QPoint(window.width() / 2, window.height() / 2);
    window.stepScaleUp();
    QApplication::processEvents();
    const QPoint centerAfter = window.pos() + QPoint(window.width() / 2, window.height() / 2);

    EXPECT_NEAR(centerBefore.x(), centerAfter.x(), 2);
    EXPECT_NEAR(centerBefore.y(), centerAfter.y(), 2);
    EXPECT_NEAR(config.osd().osdScale, 1.1, 0.02);
}

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
