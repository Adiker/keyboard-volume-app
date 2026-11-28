#include "pawatcherthread.h"
#include "pabackend_helpers.h"

#include <QElapsedTimer>

PaWatcherThread::PaWatcherThread(QObject* parent) : QThread(parent) {}

PaWatcherThread::~PaWatcherThread()
{
    stop();
}

void PaWatcherThread::stop()
{
    m_stopping = true;
    if (isRunning())
    {
        wait(1000);
        if (isRunning()) terminate();
    }
}

void PaWatcherThread::run()
{
    while (!m_stopping)
    {
        bool connected = connectOnce();
        if (connected)
        {
            m_backoffMs = 0;
            emit connectionReady();

            while (!m_stopping && contextIsGood()) msleep(100);
        }

        disconnectContext();
        if (m_stopping) break;

        m_backoffMs = pabackend::nextReconnectBackoff(m_backoffMs);
        int sleptMs = 0;
        while (!m_stopping && sleptMs < m_backoffMs)
        {
            msleep(100);
            sleptMs += 100;
        }
    }
}

bool PaWatcherThread::connectOnce()
{
    m_mainloop = pa_threaded_mainloop_new();
    if (!m_mainloop) return false;

    pa_mainloop_api* api = pa_threaded_mainloop_get_api(m_mainloop);
    m_ctx = pa_context_new(api, "keyboard-volume-app-watcher");
    if (!m_ctx)
    {
        disconnectContext();
        return false;
    }

    pa_context_set_state_callback(m_ctx, contextStateCallback, this);
    pa_threaded_mainloop_lock(m_mainloop);
    pa_threaded_mainloop_start(m_mainloop);

    if (pa_context_connect(m_ctx, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0)
    {
        pa_threaded_mainloop_unlock(m_mainloop);
        disconnectContext();
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (!m_stopping && timer.elapsed() < pabackend::PA_CONNECT_TIMEOUT_MS)
    {
        pa_context_state_t st = pa_context_get_state(m_ctx);
        if (st == PA_CONTEXT_READY)
        {
            pa_context_subscribe(m_ctx,
                                 static_cast<pa_subscription_mask_t>(
                                     PA_SUBSCRIPTION_MASK_SINK_INPUT | PA_SUBSCRIPTION_MASK_SINK |
                                     PA_SUBSCRIPTION_MASK_SERVER),
                                 nullptr, nullptr);
            pa_context_set_subscribe_callback(m_ctx, subscribeCallback, this);
            pa_threaded_mainloop_unlock(m_mainloop);
            return true;
        }
        if (!PA_CONTEXT_IS_GOOD(st)) break;
        pa_threaded_mainloop_unlock(m_mainloop);
        msleep(20);
        pa_threaded_mainloop_lock(m_mainloop);
    }
    pa_threaded_mainloop_unlock(m_mainloop);
    return false;
}

bool PaWatcherThread::contextIsGood()
{
    if (!m_mainloop || !m_ctx) return false;
    pa_threaded_mainloop_lock(m_mainloop);
    const pa_context_state_t st = pa_context_get_state(m_ctx);
    const bool good = PA_CONTEXT_IS_GOOD(st);
    pa_threaded_mainloop_unlock(m_mainloop);
    return good;
}

void PaWatcherThread::disconnectContext()
{
    if (!m_mainloop) return;

    if (m_ctx)
    {
        pa_threaded_mainloop_lock(m_mainloop);
        pa_context_set_state_callback(m_ctx, nullptr, nullptr);
        pa_context_set_subscribe_callback(m_ctx, nullptr, nullptr);
        pa_context_disconnect(m_ctx);
        pa_context_unref(m_ctx);
        m_ctx = nullptr;
        pa_threaded_mainloop_unlock(m_mainloop);
    }

    pa_threaded_mainloop_stop(m_mainloop);
    pa_threaded_mainloop_free(m_mainloop);
    m_mainloop = nullptr;
}

void PaWatcherThread::contextStateCallback(pa_context* ctx, void* ud)
{
    PaWatcherThread* self = static_cast<PaWatcherThread*>(ud);
    Q_UNUSED(ctx);
    Q_UNUSED(self);
}

void PaWatcherThread::subscribeCallback(pa_context*, pa_subscription_event_type_t t, uint32_t,
                                        void* ud)
{
    PaWatcherThread* self = static_cast<PaWatcherThread*>(ud);
    int facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    int type = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;
    if (facility == PA_SUBSCRIPTION_EVENT_SINK_INPUT)
    {
        if (type == PA_SUBSCRIPTION_EVENT_NEW)
            emit self->sinkInputAppeared();
        else if (type == PA_SUBSCRIPTION_EVENT_REMOVE)
            emit self->sinkInputRemoved();
    }
    else if (facility == PA_SUBSCRIPTION_EVENT_SINK || facility == PA_SUBSCRIPTION_EVENT_SERVER)
    {
        emit self->sinkChanged();
    }
}
