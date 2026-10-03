// Opt-in compositor regression: a margin change must reach wl_surface.commit
// even when no volume, progress or marquee update repaints the OSD.
#include <gtest/gtest.h>

#include <QApplication>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QTemporaryDir>
#include <QTest>

#include "config.h"
#include "osdwindow.h"

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

TEST(OSDWindowWayland, StaticContentMoveSchedulesSurfaceUpdates)
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

    const QPoint local = window.rect().center();
    const QPoint start = window.mapToGlobal(local);
    QMouseEvent press(QEvent::MouseButtonPress, local, start, Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&window, &press);
    QTest::qWait(50);

    window.paints = 0;
    for (int i = 1; i <= 8; ++i)
    {
        const int before = window.paints;
        QMouseEvent move(QEvent::MouseMove, local, start + QPoint(i * 12, 0), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &move);
        QTRY_VERIFY_WITH_TIMEOUT(window.paints > before, 500);
        EXPECT_EQ(window.lastDamage, QRect(0, 0, 1, 1));
    }
    // With no unrelated content updates, the broken margin-only path paints
    // nothing and never submits the new positions to the compositor.
    EXPECT_GT(window.paints, 0);

    QMouseEvent release(QEvent::MouseButtonRelease, local, start + QPoint(96, 0), Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &release);
    EXPECT_EQ(config.osd().x, osd.x + 96);
    EXPECT_EQ(config.osd().y, osd.y);
}

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
