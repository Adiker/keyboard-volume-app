#include "logging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <cstdio>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HAVE_LIBSYSTEMD
#define SD_JOURNAL_SUPPRESS_LOCATION
#include <systemd/sd-journal.h>
#include <syslog.h>
#endif

namespace
{

struct LoggingState
{
    std::mutex mutex;
    LogConfig config;
    QFile file;
    bool installed = false;
};

LoggingState& loggingState()
{
    static LoggingState state;
    return state;
}

const char* msgTypeTag(QtMsgType type)
{
    switch (type)
    {
    case QtDebugMsg:
        return "DEBUG";
    case QtInfoMsg:
        return "INFO";
    case QtWarningMsg:
        return "WARN";
    case QtCriticalMsg:
        return "ERROR";
    case QtFatalMsg:
        return "FATAL";
    }
    return "INFO";
}

#ifdef HAVE_LIBSYSTEMD
int syslogPriority(QtMsgType type)
{
    switch (type)
    {
    case QtDebugMsg:
        return LOG_DEBUG;
    case QtInfoMsg:
        return LOG_INFO;
    case QtWarningMsg:
        return LOG_WARNING;
    case QtCriticalMsg:
        return LOG_ERR;
    case QtFatalMsg:
        return LOG_CRIT;
    }
    return LOG_INFO;
}
#endif

void openLogFileUnlocked(LoggingState& state)
{
    if (state.file.isOpen()) state.file.close();
    if (state.config.filePath.isEmpty()) return;

    const QFileInfo info(state.config.filePath);
    QDir().mkpath(info.absolutePath());
    state.file.setFileName(state.config.filePath);
    if (!state.file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        const QByteArray err = QStringLiteral("[logging] Cannot open log file %1: %2\n")
                                   .arg(state.config.filePath, state.file.errorString())
                                   .toUtf8();
        (void)std::fwrite(err.constData(), 1, static_cast<size_t>(err.size()), stderr);
        std::fflush(stderr);
    }
}

void rotateLogFileIfNeededUnlocked(LoggingState& state)
{
    if (!state.file.isOpen() || state.config.maxFileBytes <= 0) return;
    if (state.file.size() < state.config.maxFileBytes) return;

    const QString path = state.config.filePath;
    const QString backupPath = path + QStringLiteral(".1");
    state.file.close();
    QFile::remove(backupPath);
    QFile::rename(path, backupPath);
    openLogFileUnlocked(state);
}

void qtLogMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    auto& state = loggingState();
    std::lock_guard<std::mutex> lock(state.mutex);

    if (!shouldEmitLog(type, state.config.level)) return;

    const QString line = formatLogLine(type, context, msg);

#ifdef HAVE_LIBSYSTEMD
    if (state.config.useJournal)
    {
        const QByteArray utf8Msg = msg.toUtf8();
        const char* category =
            (context.category && context.category[0] != '\0') ? context.category : "default";
        (void)sd_journal_send("MESSAGE=%s", utf8Msg.constData(), "PRIORITY=%d",
                              syslogPriority(type), "SYSLOG_IDENTIFIER=keyboard-volume-app",
                              "QT_CATEGORY=%s", category, nullptr);
    }
#endif

    if (state.config.useStderr)
    {
        const QByteArray utf8 = line.toUtf8();
        (void)std::fwrite(utf8.constData(), 1, static_cast<size_t>(utf8.size()), stderr);
        (void)std::fputc('\n', stderr);
        std::fflush(stderr);
    }

    if (state.file.isOpen())
    {
        rotateLogFileIfNeededUnlocked(state);
        if (state.file.isOpen())
        {
            const QByteArray utf8 = line.toUtf8();
            state.file.write(utf8);
            state.file.write("\n");
            state.file.flush();
        }
    }
}

} // namespace

std::optional<LogLevel> parseLogLevel(const QString& raw)
{
    const QString lower = raw.trimmed().toLower();
    if (lower == QLatin1String("off")) return LogLevel::Off;
    if (lower == QLatin1String("warn") || lower == QLatin1String("warning")) return LogLevel::Warn;
    if (lower == QLatin1String("info")) return LogLevel::Info;
    if (lower == QLatin1String("debug")) return LogLevel::Debug;
    if (lower == QLatin1String("trace")) return LogLevel::Trace;
    return std::nullopt;
}

QString logLevelToString(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Off:
        return QStringLiteral("off");
    case LogLevel::Warn:
        return QStringLiteral("warn");
    case LogLevel::Info:
        return QStringLiteral("info");
    case LogLevel::Debug:
        return QStringLiteral("debug");
    case LogLevel::Trace:
        return QStringLiteral("trace");
    }
    return QStringLiteral("info");
}

QString defaultLogFilePath()
{
    QString stateHome = QString::fromLocal8Bit(qgetenv("XDG_STATE_HOME")).trimmed();
    if (stateHome.isEmpty())
    {
        stateHome = QDir::homePath() + QStringLiteral("/.local/state");
    }
    return QDir(stateHome).filePath(QStringLiteral("keyboard-volume-app/keyboard-volume-app.log"));
}

bool isRunningUnderJournald()
{
    const QByteArray stream = qgetenv("JOURNAL_STREAM");
    if (stream.isEmpty()) return false;
    const qsizetype colon = stream.indexOf(':');
    if (colon <= 0) return false;
    bool devOk = false;
    bool inoOk = false;
    const quint64 expectedDev = stream.left(colon).toULongLong(&devOk);
    const quint64 expectedIno = stream.mid(colon + 1).toULongLong(&inoOk);
    if (!devOk || !inoOk) return false;

    struct stat st = {};
    if (::fstat(STDERR_FILENO, &st) != 0) return false;
    return static_cast<quint64>(st.st_dev) == expectedDev &&
           static_cast<quint64>(st.st_ino) == expectedIno;
}

bool hasJournaldSupport()
{
#ifdef HAVE_LIBSYSTEMD
    return true;
#else
    return false;
#endif
}

bool shouldEmitLog(QtMsgType type, LogLevel configuredLevel)
{
    if (type == QtFatalMsg) return true;
    switch (configuredLevel)
    {
    case LogLevel::Off:
        return false;
    case LogLevel::Warn:
        return type == QtWarningMsg || type == QtCriticalMsg;
    case LogLevel::Info:
        return type == QtInfoMsg || type == QtWarningMsg || type == QtCriticalMsg;
    case LogLevel::Debug:
    case LogLevel::Trace:
        return true;
    }
    return false;
}

QString formatLogLine(QtMsgType type, const QMessageLogContext& context, const QString& msg,
                      const QDateTime& timestamp)
{
    const QString ts = timestamp.toUTC().toString(Qt::ISODateWithMs);
    const QString tag = QString::fromLatin1(msgTypeTag(type));
    if (context.category && context.category[0] != '\0' &&
        QString::fromLatin1(context.category) != QLatin1String("default"))
    {
        return QStringLiteral("%1 [%2] [%3] %4")
            .arg(ts, tag, QString::fromLatin1(context.category), msg);
    }
    return QStringLiteral("%1 [%2] %3").arg(ts, tag, msg);
}

void installLogging(const LogConfig& config)
{
    auto& state = loggingState();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.config = config;
#ifndef HAVE_LIBSYSTEMD
        if (state.config.useJournal)
        {
            state.config.useJournal = false;
            state.config.useStderr = true;
        }
#endif
        openLogFileUnlocked(state);
        state.installed = true;
    }
    qInstallMessageHandler(qtLogMessageHandler);
}

void shutdownLogging()
{
    qInstallMessageHandler(nullptr);
    auto& state = loggingState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.file.isOpen())
    {
        state.file.flush();
        state.file.close();
    }
    state.installed = false;
}

LogLevel currentLogLevel()
{
    auto& state = loggingState();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.config.level;
}
