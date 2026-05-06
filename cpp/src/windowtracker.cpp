#include "windowtracker.h"

#include <QFile>
#include <QDebug>

#include <xcb/xcb.h>
#include <cstring>
#include <cstdlib>

WindowTracker::WindowTracker(QObject* parent) : QThread(parent) {}

WindowTracker::~WindowTracker()
{
    stop();
}

void WindowTracker::stop()
{
    m_running = false;
    if (isRunning())
    {
        wait(2000);
        if (isRunning())
        {
            terminate();
            wait(1000);
        }
    }
}

void WindowTracker::start()
{
    m_running = true;
    QThread::start();
}

QString WindowTracker::windowToBinary(xcb_connection_t* conn, xcb_window_t win,
                                      xcb_atom_t pidAtom) const
{
    if (win == 0) return {};

    auto cookie = xcb_get_property(conn, 0, win, pidAtom, XCB_ATOM_CARDINAL, 0, 1);
    auto reply = xcb_get_property_reply(conn, cookie, nullptr);
    if (!reply)
    {
        return {};
    }

    uint32_t pid = 0;
    if (reply->type == XCB_ATOM_CARDINAL && reply->format == 32)
    {
        const auto* data = static_cast<const uint32_t*>(xcb_get_property_value(reply));
        if (data) pid = *data;
    }
    free(reply);

    if (pid == 0) return {};

    const QString procPath = QStringLiteral("/proc/%1/comm").arg(pid);
    QFile f(procPath);
    if (f.open(QIODevice::ReadOnly))
    {
        return QString::fromUtf8(f.readAll()).trimmed();
    }
    return {};
}

void WindowTracker::run()
{
    m_conn = xcb_connect(nullptr, nullptr);
    if (int err = xcb_connection_has_error(m_conn); err != 0)
    {
        emit error(QStringLiteral("XCB connection failed (error %1)").arg(err));
        return;
    }

    const xcb_setup_t* setup = xcb_get_setup(m_conn);
    if (!setup)
    {
        emit error(QStringLiteral("XCB setup failed"));
        xcb_disconnect(m_conn);
        return;
    }

    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
    if (iter.rem == 0)
    {
        emit error(QStringLiteral("No XCB screens found"));
        xcb_disconnect(m_conn);
        return;
    }
    xcb_screen_t* screen = iter.data;

    auto internActive =
        xcb_intern_atom(m_conn, 0, std::strlen("_NET_ACTIVE_WINDOW"), "_NET_ACTIVE_WINDOW");
    auto internPid = xcb_intern_atom(m_conn, 0, std::strlen("_NET_WM_PID"), "_NET_WM_PID");

    auto* replyActive = xcb_intern_atom_reply(m_conn, internActive, nullptr);
    if (!replyActive)
    {
        emit error(QStringLiteral("_NET_ACTIVE_WINDOW atom failed"));
        xcb_disconnect(m_conn);
        return;
    }
    xcb_atom_t atomActive = replyActive->atom;
    free(replyActive);

    auto* replyPid = xcb_intern_atom_reply(m_conn, internPid, nullptr);
    if (!replyPid)
    {
        emit error(QStringLiteral("_NET_WM_PID atom failed"));
        xcb_disconnect(m_conn);
        return;
    }
    xcb_atom_t atomPid = replyPid->atom;
    free(replyPid);

    xcb_window_t lastWindow = 0;
    QString lastBinary;

    while (m_running)
    {
        auto propCookie =
            xcb_get_property(m_conn, 0, screen->root, atomActive, XCB_ATOM_WINDOW, 0, 1);
        auto* propReply = xcb_get_property_reply(m_conn, propCookie, nullptr);
        if (propReply && propReply->type == XCB_ATOM_WINDOW && propReply->format == 32)
        {
            xcb_window_t win = *static_cast<const xcb_window_t*>(xcb_get_property_value(propReply));
            if (win != lastWindow)
            {
                lastWindow = win;
                QString binary = windowToBinary(m_conn, win, atomPid);

                // Avoid emitting when binary didn't actually change
                // (some WMs may report same window even when atom doesn't change)
                if (binary != lastBinary)
                {
                    lastBinary = binary;
                    emit focusedBinaryChanged(binary);
                }
            }
        }
        if (propReply) free(propReply);

        QThread::msleep(500);
    }

    xcb_disconnect(m_conn);
}
