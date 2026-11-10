// Opt-in compositor regression: a margin change must reach wl_surface.commit
// even when no volume, progress or marquee update repaints the OSD.
#include <gtest/gtest.h>

#include <QApplication>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QWindow>

#include "config.h"
#define private public
#include "osdwindow.h"
#undef private

bool g_nativeWayland = false;

namespace
{
class PaintCountingOSD : public OSDWindow
{
  public:
    using OSDWindow::OSDWindow;
    int paints = 0;
    QRect lastDamage;

  protected:
    void paintEvent(QPaintEvent* event) override
    {
        ++paints;
        lastDamage = event->region().boundingRect();
        OSDWindow::paintEvent(event);
    }
};
} // namespace

TEST(OSDWindowWayland, StaticContentMoveRepaintsWholeSurfaceAtFrameCadence)
{
    if (!qEnvironmentVariableIsSet("KVA_TEST_NATIVE_WAYLAND") ||
        QGuiApplication::platformName() != QLatin1String("wayland"))
        GTEST_SKIP() << "Opt in on a layer-shell compositor with KVA_TEST_NATIVE_WAYLAND=1 "
                        "and QT_QPA_PLATFORM=wayland.";

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());
    OsdConfig osd = config.osd();
    osd.x = 300;
    osd.y = 300;
    osd.timeoutMs = 60000;
    osd.positionControlsEnabled = true;
    osd.positionDragEnabled = true;
    config.setOsd(osd);

    g_nativeWayland = true;
    PaintCountingOSD window(&config);
    window.initLayerShell();
    window.showVolume(QStringLiteral("app"), 0.5);
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    QTest::qWait(50);

    const QPoint grabLocal = window.rect().center();
    const QPoint start = window.mapToGlobal(grabLocal);
    QMouseEvent press(QEvent::MouseButtonPress, grabLocal, start, Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&window, &press);
    QTest::qWait(50);
    ASSERT_TRUE(window.m_dragOverlayActive);
    ASSERT_NE(window.m_panel, nullptr);

    window.paints = 0;
    for (int i = 1; i <= 8; ++i)
    {
        const int before = window.paints;
        // Overlay mode uses fullscreen-local coordinates: panelPos + grab.
        const QPoint local = window.m_panel->pos() + grabLocal + QPoint(i * 12, 0);
        QMouseEvent move(QEvent::MouseMove, local, window.mapToGlobal(local), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &move);
        QTRY_VERIFY_WITH_TIMEOUT(window.paints > before, 500);
        EXPECT_EQ(window.m_currentAbsPos.x(), osd.x + i * 12);
    }
    EXPECT_GT(window.paints, 0);

    auto* layer = LayerShellQt::Window::get(window.windowHandle());
    ASSERT_NE(layer, nullptr);
    const int paintsBeforeBurst = window.paints;
    for (int i = 1; i <= 40; ++i)
    {
        const QPoint local = window.m_panel->pos() + grabLocal + QPoint(i, 0);
        QMouseEvent move(QEvent::MouseMove, local, window.mapToGlobal(local), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &move);
    }
    // Overlay applies panel moves immediately; grab stays under the pointer.
    EXPECT_EQ(window.m_currentAbsPos.x(), osd.x + 40);
    QTRY_VERIFY_WITH_TIMEOUT(window.paints > paintsBeforeBurst, 500);
    EXPECT_EQ(window.lastDamage, window.rect());

    QMouseEvent pending(QEvent::MouseMove, window.m_panel->pos() + grabLocal + QPoint(150, 0),
                        window.mapToGlobal(window.m_panel->pos() + grabLocal + QPoint(150, 0)),
                        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &pending);
    QMouseEvent release(QEvent::MouseButtonRelease,
                        window.m_panel->pos() + grabLocal + QPoint(160, 0),
                        window.mapToGlobal(window.m_panel->pos() + grabLocal + QPoint(160, 0)),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &release);
    EXPECT_FALSE(window.m_dragOverlayActive);
    EXPECT_EQ(layer->margins().left(), osd.x + 160);
    EXPECT_EQ(config.osd().x, osd.x + 160);
    EXPECT_EQ(config.osd().y, osd.y);
    QTest::qWait(50);
    EXPECT_EQ(layer->margins().left(), osd.x + 160);

    QMouseEvent nextPress(QEvent::MouseButtonPress, grabLocal, start, Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &nextPress);
    QMouseEvent cancelled(QEvent::MouseMove, window.m_panel->pos() + grabLocal + QPoint(50, 0),
                          window.mapToGlobal(window.m_panel->pos() + grabLocal + QPoint(50, 0)),
                          Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &cancelled);
    window.hide();
    QTest::qWait(50);
    EXPECT_FALSE(window.m_dragOverlayActive);
    EXPECT_EQ(layer->margins().left(), osd.x + 160);
    EXPECT_EQ(config.osd().x, osd.x + 160);
}

TEST(OSDWindowWayland, FrameMovesFollowSurfaceRelativeCoordinates)
{
    if (!qEnvironmentVariableIsSet("KVA_TEST_NATIVE_WAYLAND") ||
        QGuiApplication::platformName() != QLatin1String("wayland"))
        GTEST_SKIP() << "Requires an explicitly selected native layer-shell compositor.";

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());
    OsdConfig osd = config.osd();
    osd.x = 300;
    osd.y = 300;
    osd.timeoutMs = 60000;
    osd.positionControlsEnabled = true;
    osd.positionDragEnabled = true;
    config.setOsd(osd);
    g_nativeWayland = true;
    PaintCountingOSD window(&config);
    window.initLayerShell();
    window.showVolume(QStringLiteral("app"), 0.5);
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    QTest::qWait(50);
    auto* layer = LayerShellQt::Window::get(window.windowHandle());
    const QPoint grabLocal = QPoint(window.osdWidth() / 2, window.osdHeight() / 2);
    const QPoint start = window.mapToGlobal(grabLocal);
    window.startMove(start, grabLocal);
    ASSERT_TRUE(window.m_dragOverlayActive);
    for (int i = 1; i <= 8; ++i)
    {
        const QPoint local = window.m_panel->pos() + grabLocal + QPoint(12, 0);
        QMouseEvent move(QEvent::MouseMove, local, window.mapToGlobal(local), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &move);
        QTRY_COMPARE_WITH_TIMEOUT(window.m_currentAbsPos.x(), osd.x + i * 12, 500);
        // Grab stays locked: panelPos + grabLocal == pointer local.
        EXPECT_EQ(window.m_panel->pos() + grabLocal, local);
    }
    QMouseEvent release(QEvent::MouseButtonRelease, window.m_panel->pos() + grabLocal,
                        window.mapToGlobal(window.m_panel->pos() + grabLocal), Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &release);
    EXPECT_FALSE(window.m_dragOverlayActive);
    EXPECT_EQ(config.osd().x, osd.x + 96);
    EXPECT_EQ(config.osd().y, osd.y);
    QTest::qWait(50);
    EXPECT_EQ(layer->margins().left(), osd.x + 96);

    window.setProgressEnabled(true);
    window.setProgressVisible(true);
    window.setMediaControlsEnabled(true);
    window.updateTrack(QStringLiteral("title"), QStringLiteral("artist"), 120000000, true);
    QTest::qWait(50);
    int seeks = 0;
    int plays = 0;
    QObject::connect(&window, &OSDWindow::seekRequested, [&seeks] { ++seeks; });
    QObject::connect(&window, &OSDWindow::playPauseRequested, [&plays] { ++plays; });
    QTest::mouseClick(window.windowHandle(), Qt::LeftButton, Qt::NoModifier,
                      window.m_progressBar->mapTo(&window, window.m_progressBar->rect().center()));
    EXPECT_EQ(seeks, 1);
    EXPECT_FALSE(window.m_moving);
    QTest::mouseClick(
        window.windowHandle(), Qt::LeftButton, Qt::NoModifier,
        window.m_btnPlayPause->mapTo(&window, window.m_btnPlayPause->rect().center()));
    EXPECT_EQ(plays, 1);
    EXPECT_FALSE(window.m_moving);
    EXPECT_EQ(config.osd().x, osd.x + 96);
}

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
