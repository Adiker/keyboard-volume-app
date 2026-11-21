#pragma once

#include <QDateTime>
#include <QMessageLogContext>
#include <QString>
#include <QtGlobal>

#include <cstdint>
#include <optional>

enum class LogLevel : std::uint8_t
{
    Off = 0,
    Warn,
    Info,
    Debug,
    Trace,
};

struct LogConfig
{
    LogLevel level = LogLevel::Info;
    bool useJournal = false;
    bool useStderr = true;
    QString filePath;
    qint64 maxFileBytes = 5LL * 1024LL * 1024LL;
};

// Parse case-insensitive level name ("off", "warn", "warning", "info", "debug", "trace").
std::optional<LogLevel> parseLogLevel(const QString& raw);
QString logLevelToString(LogLevel level);

// Default log file under $XDG_STATE_HOME/keyboard-volume-app/keyboard-volume-app.log
// (falling back to ~/.local/state/keyboard-volume-app/keyboard-volume-app.log).
QString defaultLogFilePath();

// True when systemd journal auto-detection should be used (JOURNAL_STREAM env set).
bool isRunningUnderJournald();

// True when compiled with libsystemd journald support.
bool hasJournaldSupport();

bool shouldEmitLog(QtMsgType type, LogLevel configuredLevel);

QString formatLogLine(QtMsgType type, const QMessageLogContext& context, const QString& msg,
                      const QDateTime& timestamp = QDateTime::currentDateTimeUtc());

void installLogging(const LogConfig& config);
void shutdownLogging();
LogLevel currentLogLevel();

// RAII helper to ensure shutdownLogging() runs on all exit paths.
struct ScopedLoggingShutdown
{
    ScopedLoggingShutdown() = default;
    ~ScopedLoggingShutdown()
    {
        shutdownLogging();
    }
    ScopedLoggingShutdown(const ScopedLoggingShutdown&) = delete;
    ScopedLoggingShutdown& operator=(const ScopedLoggingShutdown&) = delete;
};
