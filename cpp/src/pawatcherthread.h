#pragma once
#include <QThread>
#include <atomic>
#include <cstdint>

#include <pulse/context.h>
#include <pulse/subscribe.h>
#include <pulse/thread-mainloop.h>

// Maintains its own PA connection and subscribes to sink-input/sink/server
// events. Lives in its own QThread; signals are emitted from the PA internal
// thread (queued connections to any receiver are safe).
class PaWatcherThread : public QThread
{
    Q_OBJECT
  public:
    explicit PaWatcherThread(QObject* parent = nullptr);
    ~PaWatcherThread() override;

    void stop();

  signals:
    void sinkInputAppeared();
    void sinkInputRemoved();
    void sinkChanged();
    void connectionReady();

  protected:
    void run() override;

  private:
    bool connectOnce();
    bool contextIsGood();
    void disconnectContext();

    static void contextStateCallback(pa_context* ctx, void* ud);
    static void subscribeCallback(pa_context*, pa_subscription_event_type_t t, uint32_t, void* ud);

    pa_threaded_mainloop* m_mainloop = nullptr;
    pa_context* m_ctx = nullptr;
    std::atomic<bool> m_stopping{false};
    int m_backoffMs = 0;
};
