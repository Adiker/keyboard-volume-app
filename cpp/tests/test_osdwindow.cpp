// test_osdwindow.cpp
// Focused OSDWindow progress-row regressions.

#include <gtest/gtest.h>

#include <array>

#include <QApplication>
#include <QEventLoop>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
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
    window.startMove(startGlobal);
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

// Count actual paints rather than timing the event loop on a shared CI runner.
class PaintCountingOSD : public OSDWindow
{
  public:
    using OSDWindow::OSDWindow;
    int paints = 0;

  protected:
    void paintEvent(QPaintEvent* event) override
    {
        ++paints;
        OSDWindow::paintEvent(event);
    }
};

TEST(OSDWindowPosition, MovingStaticContentDoesNotRepaintIt)
{
    if (QGuiApplication::platformName() != QLatin1String("offscreen"))
        GTEST_SKIP() << "Real compositors may request exposes on move; count paints offscreen.";
    QTemporaryDir tmp;
    Config config(tmp.path());
    moveOsdConfig(config);
    enablePositionControls(config);
    PaintCountingOSD window(&config);
    window.showVolume(QStringLiteral("app"), 0.5);
    QApplication::processEvents();
    const QPoint start = window.m_currentAbsPos + window.rect().center();
    window.startMove(start);
    window.updateMove(start + QPoint(1, 1));
    QApplication::processEvents();
    window.paints = 0;
    for (int i = 2; i <= 20; ++i)
    {
        window.updateMove(start + QPoint(i, i));
        QApplication::processEvents();
    }
    EXPECT_EQ(window.paints, 0);
    EXPECT_EQ(window.m_currentAbsPos, window.m_moveStartAbsPos + QPoint(20, 20));
    window.finishMove(true);
}

TEST(OSDWindowResize, IdenticalGeometryDoesNotRepaint)
{
    QTemporaryDir tmp;
    Config config(tmp.path());
    moveOsdConfig(config);
    PaintCountingOSD window(&config);
    window.showVolume(QStringLiteral("app"), 0.5);
    QApplication::processEvents();
    const QPoint start = window.m_currentAbsPos + QPoint(window.width() - 2, window.height() / 2);
    window.startResize(OSDWindow::EdgeRight, start);
    window.updateResize(start + QPoint(20, 0));
    QApplication::processEvents();
    window.paints = 0;
    for (int i = 0; i < 20; ++i)
    {
        window.updateResize(start + QPoint(20, 0));
        QApplication::processEvents();
    }
    EXPECT_EQ(window.paints, 0);
    window.finishResize(true);
}

TEST(OSDWindowResize, LiveFontsMatchReleasedFonts)
{
    QTemporaryDir tmp;
    Config config(tmp.path());
    moveOsdConfig(config);
    enablePositionControls(config);
    OsdConfig osd = config.osd();
    osd.progressEnabled = true;
    config.setOsd(osd);
    OSDWindow window(&config);
    window.setProgressVisible(true);
    window.showVolume(QStringLiteral("app"), 0.5);
    const QPoint start = window.m_currentAbsPos + QPoint(window.width() - 2, window.height() / 2);
    window.startResize(OSDWindow::EdgeRight, start);
    window.updateResize(start + QPoint(17, 0));
    QApplication::processEvents();
    const std::array<QWidget*, 11> labelsAndButtons{
        window.m_labelName,  window.m_labelPct,     window.m_labelTrack, window.m_labelTime,
        window.m_btnPrev,    window.m_btnPlayPause, window.m_btnNext,    window.m_btnPosUp,
        window.m_btnPosDown, window.m_btnPosLeft,   window.m_btnPosRight};
    std::array<double, 11> liveFonts{};
    for (size_t i = 0; i < labelsAndButtons.size(); ++i)
        liveFonts[i] = labelsAndButtons[i]->font().pointSizeF();
    window.finishResize(true);
    QApplication::processEvents();
    for (size_t i = 0; i < labelsAndButtons.size(); ++i)
    {
        SCOPED_TRACE(labelsAndButtons[i]->objectName().toStdString());
        EXPECT_DOUBLE_EQ(labelsAndButtons[i]->font().pointSizeF(), liveFonts[i]);
    }
}

TEST(OSDWindowPosition, ReleaseCoordinateIsPersistedWithoutLastMoveEvent)
{
    QTemporaryDir tmp;
    Config config(tmp.path());
    moveOsdConfig(config);
    enablePositionControls(config);
    OSDWindow window(&config);
    window.showVolume(QStringLiteral("app"), 0.5);
    const QPoint origin = window.m_currentAbsPos;
    const QPoint local = window.rect().center();
    const QPoint start = origin + local;
    QMouseEvent press(QEvent::MouseButtonPress, local, start, Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&window, &press);
    ASSERT_TRUE(window.m_moving);
    QMouseEvent release(QEvent::MouseButtonRelease, local, start + QPoint(35, 25), Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &release);
    EXPECT_FALSE(window.m_moving);
    EXPECT_EQ(screenRelativeToAbs(config.osd()), origin + QPoint(35, 25));
}

TEST(OSDWindowResize, ReleaseCoordinateIsPersistedWithoutLastMoveEvent)
{
    QTemporaryDir tmp;
    Config config(tmp.path());
    moveOsdConfig(config);
    OSDWindow window(&config);
    window.showVolume(QStringLiteral("app"), 0.5);
    const QPoint local(window.width() - 2, window.height() / 2);
    const QPoint start = window.m_currentAbsPos + local;
    QMouseEvent press(QEvent::MouseButtonPress, local, start, Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&window, &press);
    ASSERT_TRUE(window.m_resizing);
    QMouseEvent release(QEvent::MouseButtonRelease, local, start + QPoint(44, 0), Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &release);
    EXPECT_FALSE(window.m_resizing);
    EXPECT_NEAR(config.osd().osdScale, 1.2, 0.001);
}

TEST(OSDWindowDrag, MoveIsImmediateWhileResizeCoalesces)
{
    for (bool resize : {false, true})
    {
        QTemporaryDir tmp;
        Config config(tmp.path());
        moveOsdConfig(config);
        enablePositionControls(config);
        OSDWindow window(&config);
        window.showVolume(QStringLiteral("app"), 0.5);
        const QPoint origin = window.m_currentAbsPos;
        const QSize initialSize = window.size();
        const QPoint local =
            resize ? QPoint(window.width() - 2, window.height() / 2) : window.rect().center();
        const QPoint start = origin + local;
        QMouseEvent press(QEvent::MouseButtonPress, local, start, Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
        QApplication::sendEvent(&window, &press);
        for (int i = 1; i <= 40; ++i)
        {
            QMouseEvent move(QEvent::MouseMove, local, start + QPoint(i, 0), Qt::NoButton,
                             Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&window, &move);
            if (!resize)
            {
                EXPECT_EQ(window.m_currentAbsPos, origin + QPoint(i, 0));
                EXPECT_FALSE(window.m_resizeUpdateTimer.isActive());
            }
        }
        EXPECT_EQ(window.size(), initialSize);
        if (resize)
        {
            EXPECT_EQ(window.m_currentAbsPos, origin);
            ASSERT_TRUE(window.m_resizeUpdateTimer.isActive());
            QEventLoop loop;
            QObject::connect(&window.m_resizeUpdateTimer, &QTimer::timeout, &loop,
                             &QEventLoop::quit);
            QTimer::singleShot(1000, &loop, &QEventLoop::quit);
            loop.exec();
            EXPECT_FALSE(window.m_resizeUpdateTimer.isActive());
            EXPECT_EQ(window.width(), initialSize.width() + 40);
            window.finishResize(true);
        }
        else
        {
            EXPECT_EQ(window.m_currentAbsPos, origin + QPoint(40, 0));
            window.finishMove(true);
        }
    }
}

TEST(OSDWindowDrag, CancelDiscardsPendingUpdate)
{
    for (bool resize : {false, true})
    {
        QTemporaryDir tmp;
        Config config(tmp.path());
        moveOsdConfig(config);
        enablePositionControls(config);
        OSDWindow window(&config);
        window.showVolume(QStringLiteral("app"), 0.5);
        const QPoint origin = window.m_currentAbsPos;
        const QSize initialSize = window.size();
        const QPoint start = origin + window.rect().center();
        if (resize)
            window.startResize(OSDWindow::EdgeRight, start);
        else
            window.startMove(start);
        if (resize)
        {
            window.scheduleResizeUpdate(start + QPoint(40, 20));
            ASSERT_TRUE(window.m_resizeUpdateTimer.isActive());
        }
        else
            window.updateMove(start + QPoint(40, 20));
        QEvent ungrab(QEvent::UngrabMouse);
        window.handleResizeEvent(&window, &ungrab);
        EXPECT_FALSE(window.m_moving);
        EXPECT_FALSE(window.m_resizing);
        EXPECT_FALSE(window.m_resizeUpdateTimer.isActive());
        window.applyPendingResizeUpdate();
        EXPECT_EQ(window.m_currentAbsPos, resize ? origin : origin + QPoint(40, 20));
        EXPECT_EQ(window.size(), initialSize);
        EXPECT_EQ(screenRelativeToAbs(config.osd()), origin);
        EXPECT_DOUBLE_EQ(config.osd().osdScale, 1.0);
    }
}

TEST(OSDWindowDrag, ReleaseUsesFinalCoordinateAndStopsResizeTimer)
{
    for (bool resize : {false, true})
    {
        QTemporaryDir tmp;
        Config config(tmp.path());
        moveOsdConfig(config);
        enablePositionControls(config);
        OSDWindow window(&config);
        window.showVolume(QStringLiteral("app"), 0.5);
        const QPoint origin = window.m_currentAbsPos;
        const QPoint local =
            resize ? QPoint(window.width() - 2, window.height() / 2) : window.rect().center();
        const QPoint start = origin + local;
        QMouseEvent press(QEvent::MouseButtonPress, local, start, Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
        QApplication::sendEvent(&window, &press);
        if (resize)
            window.scheduleResizeUpdate(start + QPoint(10, 0));
        else
            window.updateMove(start + QPoint(10, 0));
        QMouseEvent release(QEvent::MouseButtonRelease, local, start + QPoint(44, 0),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &release);
        EXPECT_FALSE(window.m_resizeUpdateTimer.isActive());
        EXPECT_FALSE(window.m_moving);
        EXPECT_FALSE(window.m_resizing);
        if (resize)
            EXPECT_DOUBLE_EQ(config.osd().osdScale, 1.2);
        else
            EXPECT_EQ(screenRelativeToAbs(config.osd()), origin + QPoint(44, 0));
        const QPoint finalPos = window.m_currentAbsPos;
        const QSize finalSize = window.size();
        window.applyPendingResizeUpdate();
        EXPECT_EQ(window.m_currentAbsPos, finalPos);
        EXPECT_EQ(window.size(), finalSize);
        EXPECT_TRUE(window.m_hideTimer->isActive());
    }
}

TEST(OSDWindowResize, AllEdgesPreserveAnchorsInEveryHeightMode)
{
    for (int mode = 0; mode < 3; ++mode)
    {
        for (int edge :
             std::array<int, 8>{OSDWindow::EdgeLeft, OSDWindow::EdgeRight, OSDWindow::EdgeTop,
                                OSDWindow::EdgeBottom, OSDWindow::EdgeLeft | OSDWindow::EdgeTop,
                                OSDWindow::EdgeRight | OSDWindow::EdgeTop,
                                OSDWindow::EdgeLeft | OSDWindow::EdgeBottom,
                                OSDWindow::EdgeRight | OSDWindow::EdgeBottom})
        {
            SCOPED_TRACE(::testing::Message() << "mode=" << mode << " edge=" << edge);
            QTemporaryDir tmp;
            Config config(tmp.path());
            moveOsdConfig(config, 200, 200);
            OsdConfig osd = config.osd();
            osd.progressEnabled = mode > 0;
            osd.mediaControlsEnabled = mode == 2;
            config.setOsd(osd);
            OSDWindow window(&config);
            window.setProgressVisible(mode > 0);
            window.showVolume(QStringLiteral("app"), 0.5);
            const QRect initial(window.m_currentAbsPos, window.size());
            const QPoint local((edge & OSDWindow::EdgeLeft) ? 2 : window.width() - 2,
                               (edge & OSDWindow::EdgeTop) ? 2 : window.height() - 2);
            const QPoint start = initial.topLeft() + local;
            const QPoint delta((edge & OSDWindow::EdgeLeft) ? -15 : 15,
                               (edge & OSDWindow::EdgeTop) ? -10 : 10);
            window.startResize(edge, start);
            window.updateResize(start + delta);
            window.finishResize(true);
            EXPECT_GT(window.width(), initial.width());
            if (edge & OSDWindow::EdgeLeft)
                EXPECT_EQ(window.m_currentAbsPos.x() + window.width(),
                          initial.x() + initial.width());
            else
                EXPECT_EQ(window.m_currentAbsPos.x(), initial.x());
            if (edge & OSDWindow::EdgeTop)
                EXPECT_EQ(window.m_currentAbsPos.y() + window.height(),
                          initial.y() + initial.height());
            else
                EXPECT_EQ(window.m_currentAbsPos.y(), initial.y());
            EXPECT_EQ(screenRelativeToAbs(config.osd()), window.m_currentAbsPos);
        }
    }
}

TEST(OSDWindowDrag, SeekAndMediaButtonsRemainInteractive)
{
    QTemporaryDir tmp;
    Config config(tmp.path());
    moveOsdConfig(config);
    enablePositionControls(config);
    OsdConfig osd = config.osd();
    osd.progressEnabled = true;
    osd.progressInteractive = true;
    config.setOsd(osd);
    OSDWindow window(&config);
    window.setProgressVisible(true);
    window.showVolume(QStringLiteral("app"), 0.5);
    window.updateTrack(QStringLiteral("Title"), QStringLiteral("Artist"), 100000000LL, true);
    QApplication::processEvents();
    int started = 0, finished = 0, requested = 0, next = 0;
    qint64 target = 0;
    QObject::connect(&window, &OSDWindow::seekStarted, [&] { ++started; });
    QObject::connect(&window, &OSDWindow::seekFinished, [&] { ++finished; });
    QObject::connect(&window, &OSDWindow::seekRequested,
                     [&](qint64 pos)
                     {
                         ++requested;
                         target = pos;
                     });
    QObject::connect(&window, &OSDWindow::nextRequested, [&] { ++next; });
    auto clickAt = [&](QWidget* child, QPoint local)
    {
        const QPoint global = child->mapToGlobal(local);
        QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
        QApplication::sendEvent(child, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton,
                            Qt::NoModifier);
        QApplication::sendEvent(child, &release);
    };
    clickAt(window.m_progressBar, QPoint(window.m_progressBar->width() / 2, 4));
    EXPECT_EQ(started, 1);
    EXPECT_EQ(finished, 1);
    EXPECT_EQ(requested, 1);
    EXPECT_NEAR(target, 50000000LL, 1000000LL);
    clickAt(window.m_btnNext, window.m_btnNext->rect().center());
    EXPECT_EQ(next, 1);
    EXPECT_FALSE(window.m_moving);
    EXPECT_FALSE(window.m_resizing);
    EXPECT_FALSE(window.m_seeking);
    EXPECT_FALSE(window.m_resizeUpdateTimer.isActive());
}

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
