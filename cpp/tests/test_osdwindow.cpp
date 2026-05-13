// test_osdwindow.cpp
// Focused OSDWindow progress-row regressions.

#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>
#include <QProgressBar>
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

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
