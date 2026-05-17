#include "windowtracker.h"

#include <QFile>
#include <QList>
#include <QString>

#include <xcb/xcb.h>

#include <algorithm>
#include <cstring>
#include <cstdlib>

#ifdef HAVE_WAYLAND_FOREIGN_TOPLEVEL
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

#include <poll.h>
#include <wayland-client.h>

#include <cerrno>
#include <cstdint>
#endif

namespace
{
QString windowToBinary(xcb_connection_t* conn, xcb_window_t win, xcb_atom_t pidAtom)
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

#ifdef HAVE_WAYLAND_FOREIGN_TOPLEVEL
constexpr uint32_t WlrForeignToplevelVersion = 3;

struct RegistryProbe
{
    bool found = false;
};

void probeRegistryGlobal(void* data, wl_registry*, uint32_t, const char* interface, uint32_t)
{
    auto* probe = static_cast<RegistryProbe*>(data);
    if (std::strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0)
    {
        probe->found = true;
    }
}

void probeRegistryRemove(void*, wl_registry*, uint32_t) {}

const wl_registry_listener ProbeRegistryListener = {
    probeRegistryGlobal,
    probeRegistryRemove,
};

bool probeWaylandForeignToplevel()
{
    wl_display* display = wl_display_connect(nullptr);
    if (!display) return false;

    wl_registry* registry = wl_display_get_registry(display);
    if (!registry)
    {
        wl_display_disconnect(display);
        return false;
    }

    RegistryProbe probe;
    wl_registry_add_listener(registry, &ProbeRegistryListener, &probe);
    const int rc = wl_display_roundtrip(display);

    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    return rc >= 0 && probe.found;
}

struct WaylandToplevel;

struct WaylandState
{
    WindowTracker* tracker = nullptr;
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    zwlr_foreign_toplevel_manager_v1* manager = nullptr;
    QList<WaylandToplevel*> toplevels;
    QString lastFocusedApp;
    bool managerFinished = false;
};

struct WaylandToplevel
{
    WaylandState* state = nullptr;
    zwlr_foreign_toplevel_handle_v1* handle = nullptr;
    QString appId;
    bool activated = false;
};

void emitFocusedApp(WaylandState* state, const QString& appId)
{
    if (state->lastFocusedApp == appId) return;
    state->lastFocusedApp = appId;
    emit state->tracker->focusedBinaryChanged(appId);
}

void handleToplevelTitle(void*, zwlr_foreign_toplevel_handle_v1*, const char*) {}

void handleToplevelAppId(void* data, zwlr_foreign_toplevel_handle_v1*, const char* appId)
{
    auto* toplevel = static_cast<WaylandToplevel*>(data);
    toplevel->appId = QString::fromUtf8(appId ? appId : "");
}

void handleToplevelOutputEnter(void*, zwlr_foreign_toplevel_handle_v1*, wl_output*) {}

void handleToplevelOutputLeave(void*, zwlr_foreign_toplevel_handle_v1*, wl_output*) {}

void handleToplevelState(void* data, zwlr_foreign_toplevel_handle_v1*, wl_array* states)
{
    auto* toplevel = static_cast<WaylandToplevel*>(data);
    toplevel->activated = false;

    const auto* pos = static_cast<const char*>(states->data);
    if (!pos) return;
    const auto* end = pos + states->size;
    while (pos + sizeof(uint32_t) <= end)
    {
        uint32_t state = 0;
        std::memcpy(&state, pos, sizeof(state));
        if (state == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED)
        {
            toplevel->activated = true;
            return;
        }
        pos += sizeof(state);
    }
}

void handleToplevelDone(void* data, zwlr_foreign_toplevel_handle_v1*)
{
    auto* toplevel = static_cast<WaylandToplevel*>(data);
    if (toplevel->activated)
    {
        emitFocusedApp(toplevel->state, toplevel->appId);
    }
    else if (toplevel->state->lastFocusedApp == toplevel->appId)
    {
        emitFocusedApp(toplevel->state, {});
    }
}

void handleToplevelClosed(void* data, zwlr_foreign_toplevel_handle_v1*)
{
    auto* toplevel = static_cast<WaylandToplevel*>(data);
    WaylandState* state = toplevel->state;

    if (toplevel->activated && state->lastFocusedApp == toplevel->appId)
    {
        emitFocusedApp(state, {});
    }

    state->toplevels.removeOne(toplevel);
    if (toplevel->handle)
    {
        zwlr_foreign_toplevel_handle_v1_destroy(toplevel->handle);
        toplevel->handle = nullptr;
    }
    delete toplevel;
}

void handleToplevelParent(void*, zwlr_foreign_toplevel_handle_v1*, zwlr_foreign_toplevel_handle_v1*)
{
}

const zwlr_foreign_toplevel_handle_v1_listener ToplevelListener = {
    handleToplevelTitle, handleToplevelAppId, handleToplevelOutputEnter, handleToplevelOutputLeave,
    handleToplevelState, handleToplevelDone,  handleToplevelClosed,      handleToplevelParent,
};

void handleManagerToplevel(void* data, zwlr_foreign_toplevel_manager_v1*,
                           zwlr_foreign_toplevel_handle_v1* handle)
{
    auto* state = static_cast<WaylandState*>(data);
    auto* toplevel = new WaylandToplevel;
    toplevel->state = state;
    toplevel->handle = handle;
    state->toplevels.append(toplevel);
    zwlr_foreign_toplevel_handle_v1_add_listener(handle, &ToplevelListener, toplevel);
}

void handleManagerFinished(void* data, zwlr_foreign_toplevel_manager_v1*)
{
    auto* state = static_cast<WaylandState*>(data);
    state->managerFinished = true;
}

const zwlr_foreign_toplevel_manager_v1_listener ManagerListener = {
    handleManagerToplevel,
    handleManagerFinished,
};

void handleRegistryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface,
                          uint32_t version)
{
    auto* state = static_cast<WaylandState*>(data);
    if (state->manager ||
        std::strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) != 0)
        return;

    const uint32_t bindVersion = std::min(version, WlrForeignToplevelVersion);
    state->manager = static_cast<zwlr_foreign_toplevel_manager_v1*>(
        wl_registry_bind(registry, name, &zwlr_foreign_toplevel_manager_v1_interface, bindVersion));
    zwlr_foreign_toplevel_manager_v1_add_listener(state->manager, &ManagerListener, state);
}

void handleRegistryRemove(void*, wl_registry*, uint32_t) {}

const wl_registry_listener RegistryListener = {
    handleRegistryGlobal,
    handleRegistryRemove,
};

bool dispatchWaylandPending(wl_display* display)
{
    while (wl_display_dispatch_pending(display) > 0)
    {
    }
    return wl_display_get_error(display) == 0;
}

void cleanupWaylandState(WaylandState* state)
{
    for (WaylandToplevel* toplevel : std::as_const(state->toplevels))
    {
        if (toplevel->handle)
        {
            zwlr_foreign_toplevel_handle_v1_destroy(toplevel->handle);
        }
        delete toplevel;
    }
    state->toplevels.clear();

    if (state->manager)
    {
        if (!state->managerFinished)
        {
            zwlr_foreign_toplevel_manager_v1_stop(state->manager);
        }
        zwlr_foreign_toplevel_manager_v1_destroy(state->manager);
        state->manager = nullptr;
    }

    if (state->registry)
    {
        wl_registry_destroy(state->registry);
        state->registry = nullptr;
    }

    if (state->display)
    {
        wl_display_flush(state->display);
        wl_display_disconnect(state->display);
        state->display = nullptr;
    }
}
#endif
} // namespace

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

WindowTracker::Backend WindowTracker::chooseBackend() const
{
#ifdef HAVE_WAYLAND_FOREIGN_TOPLEVEL
    if (!qgetenv("WAYLAND_DISPLAY").isEmpty() && probeWaylandForeignToplevel())
    {
        return Backend::WaylandForeignToplevel;
    }
#endif

    if (!qgetenv("DISPLAY").isEmpty())
    {
        return Backend::Xcb;
    }

    return Backend::None;
}

void WindowTracker::run()
{
    const Backend backend = chooseBackend();
    switch (backend)
    {
    case Backend::WaylandForeignToplevel:
        runWayland();
        break;
    case Backend::Xcb:
        runXcb();
        break;
    case Backend::None:
        emit error(QStringLiteral("No supported window tracking backend found"));
        break;
    }
    m_running = false;
}

void WindowTracker::runXcb()
{
    xcb_connection_t* conn = xcb_connect(nullptr, nullptr);
    if (int err = xcb_connection_has_error(conn); err != 0)
    {
        emit error(QStringLiteral("XCB connection failed (error %1)").arg(err));
        xcb_disconnect(conn);
        return;
    }

    const xcb_setup_t* setup = xcb_get_setup(conn);
    if (!setup)
    {
        emit error(QStringLiteral("XCB setup failed"));
        xcb_disconnect(conn);
        return;
    }

    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
    if (iter.rem == 0)
    {
        emit error(QStringLiteral("No XCB screens found"));
        xcb_disconnect(conn);
        return;
    }
    xcb_screen_t* screen = iter.data;

    auto internActive =
        xcb_intern_atom(conn, 0, std::strlen("_NET_ACTIVE_WINDOW"), "_NET_ACTIVE_WINDOW");
    auto internPid = xcb_intern_atom(conn, 0, std::strlen("_NET_WM_PID"), "_NET_WM_PID");

    auto* replyActive = xcb_intern_atom_reply(conn, internActive, nullptr);
    if (!replyActive)
    {
        emit error(QStringLiteral("_NET_ACTIVE_WINDOW atom failed"));
        xcb_disconnect(conn);
        return;
    }
    xcb_atom_t atomActive = replyActive->atom;
    free(replyActive);

    auto* replyPid = xcb_intern_atom_reply(conn, internPid, nullptr);
    if (!replyPid)
    {
        emit error(QStringLiteral("_NET_WM_PID atom failed"));
        xcb_disconnect(conn);
        return;
    }
    xcb_atom_t atomPid = replyPid->atom;
    free(replyPid);

    xcb_window_t lastWindow = 0;
    QString lastBinary;

    while (m_running)
    {
        auto propCookie =
            xcb_get_property(conn, 0, screen->root, atomActive, XCB_ATOM_WINDOW, 0, 1);
        auto* propReply = xcb_get_property_reply(conn, propCookie, nullptr);
        if (propReply && propReply->type == XCB_ATOM_WINDOW && propReply->format == 32)
        {
            xcb_window_t win = *static_cast<const xcb_window_t*>(xcb_get_property_value(propReply));
            if (win != lastWindow)
            {
                lastWindow = win;
                QString binary = windowToBinary(conn, win, atomPid);

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

    xcb_disconnect(conn);
}

void WindowTracker::runWayland()
{
#ifdef HAVE_WAYLAND_FOREIGN_TOPLEVEL
    WaylandState state;
    state.tracker = this;
    state.display = wl_display_connect(nullptr);
    if (!state.display)
    {
        emit error(QStringLiteral("Wayland connection failed"));
        return;
    }

    state.registry = wl_display_get_registry(state.display);
    if (!state.registry)
    {
        emit error(QStringLiteral("Wayland registry failed"));
        cleanupWaylandState(&state);
        return;
    }

    wl_registry_add_listener(state.registry, &RegistryListener, &state);
    if (wl_display_roundtrip(state.display) < 0)
    {
        emit error(QStringLiteral("Wayland registry roundtrip failed"));
        cleanupWaylandState(&state);
        return;
    }

    if (!state.manager)
    {
        emit error(QStringLiteral("Wayland foreign-toplevel protocol unavailable"));
        cleanupWaylandState(&state);
        return;
    }

    if (wl_display_roundtrip(state.display) < 0)
    {
        emit error(QStringLiteral("Wayland foreign-toplevel initialization failed"));
        cleanupWaylandState(&state);
        return;
    }

    const int fd = wl_display_get_fd(state.display);
    while (m_running && !state.managerFinished)
    {
        if (!dispatchWaylandPending(state.display))
        {
            emit error(QStringLiteral("Wayland event dispatch failed"));
            break;
        }
        if (state.managerFinished) break;

        while (m_running && wl_display_prepare_read(state.display) != 0)
        {
            if (!dispatchWaylandPending(state.display))
            {
                emit error(QStringLiteral("Wayland event dispatch failed"));
                cleanupWaylandState(&state);
                return;
            }
        }
        if (!m_running)
        {
            wl_display_cancel_read(state.display);
            break;
        }

        if (wl_display_flush(state.display) < 0 && errno != EAGAIN)
        {
            wl_display_cancel_read(state.display);
            emit error(QStringLiteral("Wayland display flush failed"));
            break;
        }

        pollfd pfd = {fd, POLLIN, 0};
        const int pollResult = poll(&pfd, 1, 50);
        if (pollResult > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)))
        {
            wl_display_cancel_read(state.display);
            emit error(QStringLiteral("Wayland display connection closed"));
            break;
        }
        if (pollResult > 0 && (pfd.revents & POLLIN))
        {
            if (wl_display_read_events(state.display) < 0)
            {
                emit error(QStringLiteral("Wayland event read failed"));
                break;
            }
            continue;
        }

        wl_display_cancel_read(state.display);
        if (pollResult < 0 && errno != EINTR)
        {
            emit error(QStringLiteral("Wayland display poll failed"));
            break;
        }
    }

    cleanupWaylandState(&state);
#else
    emit error(QStringLiteral("Wayland foreign-toplevel backend not compiled in"));
#endif
}
