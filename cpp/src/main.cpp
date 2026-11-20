#include "app.h"
#include "config.h"
#include "firstrunwizard.h"
#include "i18n.h"
#include "screenutils.h"
#include "waylandstate.h"

#ifdef HAVE_WAYLAND_CLIENT
#include <wayland-client.h>
#endif

#include <QApplication>
#include <QCommandLineParser>
#include <QCursor>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDebug>
#include <QLocale>
#include <QMessageBox>
#include <QProcess>
#include <QSocketNotifier>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <unistd.h>

// ─── Wayland state ───────────────────────────────────────────────────────────
// Defined here; declared extern in waylandstate.h.
// Set before QApplication, read-only afterwards (no mutex needed).
bool g_nativeWayland = false;

namespace
{

int g_signalPipe[2] = {-1, -1};

void handleUnixSignal(int signo)
{
    if (g_signalPipe[1] < 0) return;
    const char byte = static_cast<char>(signo);
    const ssize_t ignored = ::write(g_signalPipe[1], &byte, 1);
    (void)ignored;
}

void setFdFlag(int fd, int command, int flag)
{
    const int flags = fcntl(fd, command, 0);
    if (flags >= 0)
    {
        const int updated =
            static_cast<int>(static_cast<unsigned int>(flags) | static_cast<unsigned int>(flag));
        fcntl(fd, command == F_GETFD ? F_SETFD : F_SETFL, updated);
    }
}

std::unique_ptr<QSocketNotifier> installUnixSignalHandlers(QObject* parent)
{
    if (::pipe(g_signalPipe) != 0)
    {
        qWarning() << "Could not install signal pipe:" << strerror(errno);
        return {};
    }

    setFdFlag(g_signalPipe[0], F_GETFL, O_NONBLOCK);
    setFdFlag(g_signalPipe[1], F_GETFL, O_NONBLOCK);
    setFdFlag(g_signalPipe[0], F_GETFD, FD_CLOEXEC);
    setFdFlag(g_signalPipe[1], F_GETFD, FD_CLOEXEC);

    struct sigaction action = {};
    action.sa_handler = handleUnixSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);

    auto notifier =
        std::make_unique<QSocketNotifier>(g_signalPipe[0], QSocketNotifier::Read, parent);
    QObject::connect(notifier.get(), &QSocketNotifier::activated, parent,
                     []
                     {
                         char buffer[32];
                         while (::read(g_signalPipe[0], buffer, sizeof(buffer)) > 0)
                         {
                         }
                         qApp->quit();
                     });
    return notifier;
}

} // namespace

#ifdef HAVE_WAYLAND_CLIENT
// Probe Wayland registry for zwlr_layer_shell_v1 before QApplication is created.
// Opens a temporary wl_display connection, does one roundtrip, then disconnects.
static bool probeLayerShell()
{
    wl_display* dpy = wl_display_connect(nullptr);
    if (!dpy) return false;

    wl_registry* reg = wl_display_get_registry(dpy);
    bool found = false;

    static const wl_registry_listener lst = {
        // global announce
        [](void* d, wl_registry*, uint32_t, const char* iface, uint32_t)
        {
            if (strcmp(iface, "zwlr_layer_shell_v1") == 0) *static_cast<bool*>(d) = true;
        },
        // global remove — not needed
        [](void*, wl_registry*, uint32_t) {},
    };

    wl_registry_add_listener(reg, &lst, &found);
    wl_display_roundtrip(dpy); // synchronous: receives all current globals
    wl_registry_destroy(reg);
    wl_display_disconnect(dpy);
    return found;
}
#endif

// ─── main() ───────────────────────────────────────────────────────────────────

#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif

int main(int argc, char* argv[])
{
    // Wayland session detection and OSD positioning strategy:
    //
    //   Eligible when WAYLAND_DISPLAY is set, XDG_SESSION_TYPE=wayland, and
    //   QT_QPA_PLATFORM is either unset or explicitly "wayland":
    //
    //   a) zwlr_layer_shell_v1 available (Sway, Hyprland, KDE Plasma ≥5.27):
    //      → g_nativeWayland = true; Qt uses the Wayland platform (auto-detected
    //        or already set by the user). OSDWindow::initLayerShell() configures
    //        the surface via LayerShellQt. Do NOT set
    //        QT_WAYLAND_SHELL_INTEGRATION=layer-shell globally — that would make
    //        ALL windows (dialogs etc.) layer surfaces.
    //
    //   b) Protocol not available (GNOME, etc.) or LayerShellQt not compiled in:
    //      → If QT_QPA_PLATFORM was unset, force xcb (XWayland fallback).
    //        If the user explicitly set QT_QPA_PLATFORM=wayland, leave it alone —
    //        overriding the user's explicit choice would be unexpected.
    {
        const QByteArray waylandDisplay = qgetenv("WAYLAND_DISPLAY");
        const QByteArray sessionType = qgetenv("XDG_SESSION_TYPE");
        const QByteArray qtPlatform = qgetenv("QT_QPA_PLATFORM");

        const bool onWaylandSession = !waylandDisplay.isEmpty() && sessionType == "wayland";
        const bool qtPlatformUnset = qtPlatform.isEmpty();
        // Also match "wayland;xcb" and similar Qt semicolon fallback lists
        // (https://doc.qt.io/qt-6/qpa.html#selecting-a-qpa-plugin): Qt will try
        // the first entry first, so "wayland;..." is effectively a Wayland session.
        const bool qtPlatformWayland = qtPlatform == "wayland" || qtPlatform.startsWith("wayland;");

        // Probe when the session is Wayland and Qt will (or already does) use the
        // Wayland platform. Skip when the user pinned QT_QPA_PLATFORM to something
        // else (e.g. "xcb") — that is an intentional override we must respect.
        if (onWaylandSession && (qtPlatformUnset || qtPlatformWayland))
        {
#if defined(HAVE_WAYLAND_CLIENT) && defined(HAVE_LAYER_SHELL_QT)
            if (probeLayerShell())
            {
                g_nativeWayland = true;
            }
            else if (qtPlatformUnset)
            {
                // Layer-shell unavailable and no explicit platform set: fall back to
                // XWayland so QWidget::move() works for OSD positioning.
                qputenv("QT_QPA_PLATFORM", "xcb");
            }
            // qtPlatformWayland + no layer-shell: leave the user's setting intact.
#else
            if (qtPlatformUnset)
            {
                qputenv("QT_QPA_PLATFORM", "xcb");
            }
#endif
        }
    }

    QApplication qtApp(argc, argv);
    qtApp.setQuitOnLastWindowClosed(false);
    qtApp.setApplicationVersion(QStringLiteral(APP_VERSION));
    auto signalNotifier = installUnixSignalHandlers(&qtApp);

    QCommandLineParser parser;
    parser.setApplicationDescription("Keyboard volume controller with OSD");
    parser.addHelpOption();
    parser.addVersionOption();
    // Use parse() instead of process() — process() calls ::exit(0) which
    // skips destructors and triggers QThreadStorage warnings.
    parser.parse(qtApp.arguments());

    if (parser.isSet(QStringLiteral("help")))
    {
        fputs(qPrintable(parser.helpText()), stdout);
        return 0;
    }
    if (parser.isSet(QStringLiteral("version")))
    {
        printf("%s %s\n", qPrintable(qtApp.applicationName()),
               qPrintable(qtApp.applicationVersion()));
        return 0;
    }

    // ── Singleton check ──────────────────────────────────────────────────────
    {
        auto bus = QDBusConnection::sessionBus();
        if (bus.interface() &&
            bus.interface()->isServiceRegistered(QStringLiteral("org.keyboardvolumeapp")))
        {
            qWarning() << "Another instance is already running (D-Bus name taken). Exiting.";
            return 1;
        }
    }

    App app;

    if (app.config()->isSaveBlocked())
    {
        setLanguage(QLocale::system().language() == QLocale::Polish ? QStringLiteral("pl")
                                                                    : QStringLiteral("en"));
        QMessageBox::warning(
            nullptr, ::tr(QStringLiteral("warn.config_blocked.title")),
            ::tr(QStringLiteral("warn.config_blocked.text")).arg(app.config()->configFilePath()));
    }

    // First-run wizard: guide the user through language and device selection.
    if (app.config()->isFirstRun())
    {
        const QPoint anchor = QCursor::pos();
        FirstRunWizard wizard(app.config());
        centerDialogOnScreenAt(&wizard, anchor);
        if (wizard.exec() != QWizard::Accepted) return 0;
    }

    app.init();

    int exitCode = qtApp.exec();
    const bool restartRequested = qtApp.property("keyboardVolumeApp.restartRequested").toBool();
    app.cleanup();
    if (restartRequested)
    {
        QStringList args = qtApp.arguments();
        if (!args.isEmpty()) args.removeFirst();
        if (!QProcess::startDetached(qtApp.applicationFilePath(), args))
        {
            qWarning() << "Could not restart application after config import.";
            return 1;
        }
    }
    return exitCode;
}
