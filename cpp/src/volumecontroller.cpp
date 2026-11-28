#include "volumecontroller.h"
#include "paworker.h"

#include <QDebug>
#include <QEventLoop>
#include <QTimer>
#include <algorithm>
#include <utility>

VolumeController::VolumeController(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<QList<SinkInfo>>("QList<SinkInfo>");

    m_paThread = new QThread(this);
    m_worker = new PaWorker(); // no parent — will live in m_paThread
    m_worker->moveToThread(m_paThread);

    // Forward worker signals to our own signals (runs in main thread via queued)
    connect(m_worker, &PaWorker::volumeChanged, this, &VolumeController::volumeChanged);
    connect(m_worker, &PaWorker::volumeQueriedSilently, this,
            &VolumeController::volumeQueriedSilently);
    connect(m_worker, &PaWorker::appsReady, this,
            [this](const QList<AudioApp>& apps)
            {
                m_listCache = apps;
                emit appsReady(apps);
            });
    connect(m_worker, &PaWorker::sinksReady, this,
            [this](const QList<SinkInfo>& sinks)
            {
                m_sinkCache = sinks;
                emit sinksReady(sinks);
            });
    connect(m_worker, &PaWorker::sinkChanged, this, &VolumeController::sinkChanged);
    connect(
        m_worker, &PaWorker::hiddenVolumeDetected, this,
        [this](const QString& app, double rawVolume, double visibleVolume, double effectiveVolume)
        {
            if (!m_hiddenWarningsReplayed)
                m_hiddenVolumeWarnings.append({app, rawVolume, visibleVolume, effectiveVolume});
            emit hiddenVolumeDetected(app, rawVolume, visibleVolume, effectiveVolume);
        });

    // Clean up worker when thread finishes
    connect(m_paThread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_paThread->start();
    // init() runs in PA thread: connects to PA, starts watcher, fetches initial list
    QMetaObject::invokeMethod(m_worker, "init", Qt::QueuedConnection);
}

VolumeController::~VolumeController()
{
    close();
}

void VolumeController::setConfig(Config* config)
{
    if (!m_worker) return;
    m_worker->setConfig(config);
}

void VolumeController::close()
{
    if (m_paThread && !m_closing)
    {
        m_closing = true;
        disconnect(m_worker, &PaWorker::volumeChanged, this, &VolumeController::volumeChanged);
        disconnect(m_worker, nullptr, this, nullptr);
        m_worker->requestStop();

        QEventLoop cleanupLoop;
        bool cleanupDone = false;
        connect(m_worker, &PaWorker::cleanupFinished, &cleanupLoop,
                [&]()
                {
                    cleanupDone = true;
                    cleanupLoop.quit();
                });
        QTimer::singleShot(2500, &cleanupLoop, &QEventLoop::quit);

        QMetaObject::invokeMethod(m_worker, "cleanup", Qt::QueuedConnection);
        cleanupLoop.exec();
        if (!cleanupDone) qWarning() << "[VolumeController] PA cleanup did not finish in 2.5s";

        m_paThread->quit();
        if (!m_paThread->wait(2000))
        {
            qWarning() << "[VolumeController] PA thread did not quit in 2s — terminating";
            m_paThread->terminate();
            m_paThread->wait(500);
        }
        m_paThread = nullptr;
        // m_worker deleted via QThread::finished → deleteLater
        m_worker = nullptr;
    }
}

QList<AudioApp> VolumeController::listApps(bool forceRefresh)
{
    if (m_closing || !m_worker) return m_listCache;

    // Kick off a background refresh (worker checks cache TTL unless forced)
    QMetaObject::invokeMethod(m_worker, "doListApps", Qt::QueuedConnection,
                              Q_ARG(bool, forceRefresh));
    return m_listCache; // return cached data immediately — no blocking
}

QList<SinkInfo> VolumeController::listSinks(bool forceRefresh)
{
    if (m_closing || !m_worker) return m_sinkCache;

    QMetaObject::invokeMethod(m_worker, "doListSinks", Qt::QueuedConnection,
                              Q_ARG(bool, forceRefresh));
    return m_sinkCache;
}

void VolumeController::setAppSink(const QString& appName, const QString& sinkName)
{
    if (m_closing || !m_worker) return;
    if (appName.isEmpty() || sinkName.isEmpty()) return;

    QMetaObject::invokeMethod(m_worker, "doSetAppSink", Qt::QueuedConnection,
                              Q_ARG(QString, appName), Q_ARG(QString, sinkName));
}

void VolumeController::setProfileAppSink(const QString& profileId, const QString& appName,
                                         const QString& sinkName)
{
    if (profileId.isEmpty() || appName.isEmpty() || sinkName.isEmpty()) return;

    QStringList& routedApps = m_profileRoutedApps[profileId];
    bool alreadyTracked = false;
    for (const QString& tracked : std::as_const(routedApps))
    {
        if (tracked.compare(appName, Qt::CaseInsensitive) == 0)
        {
            alreadyTracked = true;
            break;
        }
    }
    if (!alreadyTracked) routedApps.append(appName);

    setAppSink(appName, sinkName);
}

QStringList VolumeController::profileRoutedApps(const QString& profileId) const
{
    return m_profileRoutedApps.value(profileId);
}

void VolumeController::clearAppSinkOverride(const QString& appName)
{
    if (m_closing || !m_worker || appName.isEmpty()) return;

    for (auto it = m_profileRoutedApps.begin(); it != m_profileRoutedApps.end();)
    {
        QStringList& routedApps = it.value();
        routedApps.removeIf([&](const QString& tracked)
                            { return tracked.compare(appName, Qt::CaseInsensitive) == 0; });
        if (routedApps.isEmpty())
            it = m_profileRoutedApps.erase(it);
        else
            ++it;
    }

    QMetaObject::invokeMethod(m_worker, "doClearAppSinkOverride", Qt::QueuedConnection,
                              Q_ARG(QString, appName));
}

void VolumeController::changeVolume(const QString& appName, double delta, double minVol,
                                    double maxVol)
{
    if (m_closing || !m_worker) return;

    QMetaObject::invokeMethod(m_worker, "doChangeVolume", Qt::QueuedConnection,
                              Q_ARG(QString, appName), Q_ARG(double, delta), Q_ARG(double, minVol),
                              Q_ARG(double, maxVol));
}

void VolumeController::setVolume(const QString& appName, double targetVolume, double minVol,
                                 double maxVol)
{
    if (m_closing || !m_worker) return;

    QMetaObject::invokeMethod(m_worker, "setAppVolume", Qt::QueuedConnection,
                              Q_ARG(QString, appName), Q_ARG(double, targetVolume),
                              Q_ARG(double, minVol), Q_ARG(double, maxVol));
}

void VolumeController::toggleMute(const QString& appName)
{
    if (m_closing || !m_worker) return;

    QMetaObject::invokeMethod(m_worker, "doToggleMute", Qt::QueuedConnection,
                              Q_ARG(QString, appName));
}

void VolumeController::setMuted(const QString& appName, bool muted)
{
    if (m_closing || !m_worker) return;

    QMetaObject::invokeMethod(m_worker, "setAppMute", Qt::QueuedConnection, Q_ARG(QString, appName),
                              Q_ARG(bool, muted));
}

void VolumeController::toggleDucking(const QString& keepApp, double duckVolume, int fadeOutMs,
                                     int fadeInMs)
{
    if (m_closing || !m_worker) return;

    QMetaObject::invokeMethod(m_worker, "doToggleDucking", Qt::QueuedConnection,
                              Q_ARG(QString, keepApp), Q_ARG(double, duckVolume),
                              Q_ARG(int, fadeOutMs), Q_ARG(int, fadeInMs));
}

void VolumeController::applyScene(const AudioScene& scene)
{
    if (m_closing || !m_worker) return;

    // Scenes intentionally bypass per-profile volume limits: each target is an
    // explicit mixer preset, so we use the full [0,1] range (default args of
    // setVolume). Targets with neither volume nor mute are already dropped by
    // Config sanitization, but we re-check defensively. Sink routing is applied
    // before volume/mute so per-device default volume can't override the
    // scene's requested level (mirrors doApplyPending ordering).
    for (const SceneTarget& target : scene.targets)
    {
        if (target.match.trimmed().isEmpty()) continue;
        if (target.sink && !target.sink->isEmpty()) setAppSink(target.match, *target.sink);
        if (target.volume) setVolume(target.match, std::clamp(*target.volume, 0, 100) / 100.0);
        if (target.muted) setMuted(target.match, *target.muted);
    }
}

void VolumeController::queryVolume(const QString& appName)
{
    if (m_closing || !m_worker) return;

    QMetaObject::invokeMethod(m_worker, "doQueryVolume", Qt::QueuedConnection,
                              Q_ARG(QString, appName), Q_ARG(bool, false));
}

void VolumeController::queryVolumeSilently(const QString& appName)
{
    if (m_closing || !m_worker) return;
    QMetaObject::invokeMethod(m_worker, "doQueryVolume", Qt::QueuedConnection,
                              Q_ARG(QString, appName), Q_ARG(bool, true));
}

void VolumeController::replayHiddenVolumeWarnings()
{
    m_hiddenWarningsReplayed = true;
    const QList<HiddenVolumeWarning> warnings = std::exchange(m_hiddenVolumeWarnings, {});
    for (const HiddenVolumeWarning& warning : warnings)
        emit hiddenVolumeDetected(warning.app, warning.rawVolume, warning.visibleVolume,
                                  warning.effectiveVolume);
}
