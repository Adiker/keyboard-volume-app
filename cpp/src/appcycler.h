#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

// Stable active-app cycling with a configurable snapshot window (default 500 ms).
// The first step (or any step after > windowMs since the previous step) freezes
// a snapshot of the live app list; subsequent presses inside the window cycle
// through that frozen snapshot so dynamic sink-input churn cannot skip or repeat
// apps mid-press.
class AppCycler
{
  public:
    static constexpr qint64 kDefaultSnapshotWindowMs = 500;

    explicit AppCycler(qint64 windowMs = kDefaultSnapshotWindowMs) : m_windowMs(windowMs) {}

    QString step(const QStringList& liveApps, const QString& currentApp, int direction,
                 qint64 nowMs)
    {
        if (direction == 0) return currentApp;

        if (m_snapshot.isEmpty() || m_lastStepMs < 0 || (nowMs - m_lastStepMs) > m_windowMs)
        {
            m_snapshot.clear();
            for (const QString& app : liveApps)
            {
                if (!app.isEmpty() && !m_snapshot.contains(app)) m_snapshot.append(app);
            }
        }
        if (m_snapshot.isEmpty()) return {};

        m_lastStepMs = nowMs;
        const qsizetype count = m_snapshot.size();
        const qsizetype currentIdx = m_snapshot.indexOf(currentApp);
        qsizetype nextIdx = 0;
        if (currentIdx < 0)
        {
            nextIdx = direction > 0 ? 0 : (count - 1);
        }
        else
        {
            const qsizetype delta = direction > 0 ? 1 : -1;
            nextIdx = (currentIdx + delta + count) % count;
        }
        return m_snapshot[nextIdx];
    }

    void reset()
    {
        m_snapshot.clear();
        m_lastStepMs = -1;
    }

  private:
    qint64 m_windowMs = kDefaultSnapshotWindowMs;
    QStringList m_snapshot;
    qint64 m_lastStepMs = -1;
};
