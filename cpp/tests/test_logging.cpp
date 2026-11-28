#include "logging.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>
#include <thread>
#include <vector>

TEST(LoggingTest, ParseLogLevelHandlesAllValidValuesAndRejectsUnknown)
{
    EXPECT_EQ(parseLogLevel(QStringLiteral("off")), LogLevel::Off);
    EXPECT_EQ(parseLogLevel(QStringLiteral("WARN")), LogLevel::Warn);
    EXPECT_EQ(parseLogLevel(QStringLiteral("warning")), LogLevel::Warn);
    EXPECT_EQ(parseLogLevel(QStringLiteral(" info ")), LogLevel::Info);
    EXPECT_EQ(parseLogLevel(QStringLiteral("Debug")), LogLevel::Debug);
    EXPECT_EQ(parseLogLevel(QStringLiteral("trace")), LogLevel::Trace);
    EXPECT_FALSE(parseLogLevel(QStringLiteral("verbose")).has_value());
    EXPECT_FALSE(parseLogLevel(QStringLiteral("")).has_value());
}

TEST(LoggingTest, ShouldEmitLogRespectsLevelThresholds)
{
    EXPECT_FALSE(shouldEmitLog(QtWarningMsg, LogLevel::Off));
    EXPECT_FALSE(shouldEmitLog(QtCriticalMsg, LogLevel::Off));
    EXPECT_TRUE(shouldEmitLog(QtFatalMsg, LogLevel::Off));

    EXPECT_FALSE(shouldEmitLog(QtDebugMsg, LogLevel::Warn));
    EXPECT_FALSE(shouldEmitLog(QtInfoMsg, LogLevel::Warn));
    EXPECT_TRUE(shouldEmitLog(QtWarningMsg, LogLevel::Warn));
    EXPECT_TRUE(shouldEmitLog(QtCriticalMsg, LogLevel::Warn));

    EXPECT_FALSE(shouldEmitLog(QtDebugMsg, LogLevel::Info));
    EXPECT_TRUE(shouldEmitLog(QtInfoMsg, LogLevel::Info));
    EXPECT_TRUE(shouldEmitLog(QtWarningMsg, LogLevel::Info));

    EXPECT_TRUE(shouldEmitLog(QtDebugMsg, LogLevel::Debug));
    EXPECT_TRUE(shouldEmitLog(QtDebugMsg, LogLevel::Trace));
}

TEST(LoggingTest, FormatLogLineIncludesUtcTimestampAndTag)
{
    const QDateTime ts =
        QDateTime::fromString(QStringLiteral("2026-11-21T12:34:56.789Z"), Qt::ISODateWithMs);
    const QMessageLogContext defaultCtx(nullptr, 0, nullptr, "default");
    EXPECT_EQ(formatLogLine(QtWarningMsg, defaultCtx, QStringLiteral("hello"), ts),
              QStringLiteral("2026-11-21T12:34:56.789Z [WARN] hello"));

    const QMessageLogContext catCtx(nullptr, 0, nullptr, "kva.audio");
    EXPECT_EQ(formatLogLine(QtInfoMsg, catCtx, QStringLiteral("ready"), ts),
              QStringLiteral("2026-11-21T12:34:56.789Z [INFO] [kva.audio] ready"));
}

TEST(LoggingTest, DefaultLogFilePathRespectsXdgStateHome)
{
    const QByteArray saved = qgetenv("XDG_STATE_HOME");
    qputenv("XDG_STATE_HOME", "/tmp/kva-state-test");
    EXPECT_EQ(defaultLogFilePath(),
              QStringLiteral("/tmp/kva-state-test/keyboard-volume-app/keyboard-volume-app.log"));
    if (saved.isEmpty())
        qunsetenv("XDG_STATE_HOME");
    else
        qputenv("XDG_STATE_HOME", saved);
}

TEST(LoggingTest, FileSinkFiltersRotatesAndIsThreadSafe)
{
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString logPath = QDir(tmpDir.path()).filePath(QStringLiteral("sub/app.log"));

    LogConfig cfg;
    cfg.level = LogLevel::Info;
    cfg.useStderr = false;
    cfg.useJournal = false;
    cfg.filePath = logPath;
    cfg.maxFileBytes = 512;

    installLogging(cfg);
    EXPECT_EQ(currentLogLevel(), LogLevel::Info);

    qDebug() << "hidden-debug-message";
    qInfo() << "visible-info-message";
    qWarning() << "visible-warn-message";

    std::vector<std::thread> workers;
    workers.reserve(4);
    for (int t = 0; t < 4; ++t)
    {
        workers.emplace_back(
            [t]()
            {
                for (int i = 0; i < 8; ++i)
                {
                    qInfo() << "thread-msg" << t << i << "padding-payload-0123456789";
                }
            });
    }
    for (auto& w : workers) w.join();

    shutdownLogging();

    QFile current(logPath);
    ASSERT_TRUE(current.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString currentText = QString::fromUtf8(current.readAll());
    EXPECT_FALSE(currentText.contains(QStringLiteral("hidden-debug-message")));
    EXPECT_TRUE(currentText.contains(QStringLiteral("thread-msg")));

    QFile rotated(logPath + QStringLiteral(".1"));
    EXPECT_TRUE(rotated.exists());
    ASSERT_TRUE(rotated.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString rotatedText = QString::fromUtf8(rotated.readAll());
    EXPECT_FALSE(rotatedText.contains(QStringLiteral("hidden-debug-message")));
    EXPECT_FALSE(rotatedText.isEmpty());
}
