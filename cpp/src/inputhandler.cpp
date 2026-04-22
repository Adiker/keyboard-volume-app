#include "inputhandler.h"

#include <QDir>
#include <QDateTime>
#include <QDebug>

#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <linux/input.h>
#include <cstring>

// ─── Device enumeration ───────────────────────────────────────────────────────
QList<QString> listEvdevDevices()
{
    QList<QString> result;
    QDir dir(QStringLiteral("/dev/input"));
    dir.setNameFilters({ QStringLiteral("event*") });
    dir.setSorting(QDir::Name);
    for (const QFileInfo &fi : dir.entryInfoList(QDir::System))
        result.append(fi.absoluteFilePath());
    return result;
}

// Helper: open device read-only non-blocking and attach libevdev.
// Returns {fd, dev}; fd == -1 on failure.
static std::pair<int, libevdev *> openDev(const QString &path)
{
    int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) return { -1, nullptr };

    libevdev *dev = nullptr;
    if (libevdev_new_from_fd(fd, &dev) < 0) {
        ::close(fd);
        return { -1, nullptr };
    }
    return { fd, dev };
}

static void closeDev(int fd, libevdev *dev)
{
    if (dev) libevdev_free(dev);
    if (fd >= 0) ::close(fd);
}

QList<QString> findSiblingDevices(const QString &primaryPath)
{
    auto [fd, dev] = openDev(primaryPath);
    if (fd < 0) return { primaryPath };

    const char *physRaw = libevdev_get_phys(dev);
    QString phys = physRaw ? QString::fromLocal8Bit(physRaw) : QString{};
    closeDev(fd, dev);

    if (phys.isEmpty()) return { primaryPath };

    // Strip "/inputN" suffix → physical parent prefix
    int slashIdx = phys.lastIndexOf(QLatin1String("/input"));
    QString physPrefix = (slashIdx >= 0) ? phys.left(slashIdx) : phys;

    QList<QString> siblings;
    for (const QString &path : listEvdevDevices()) {
        auto [fd2, dev2] = openDev(path);
        if (fd2 < 0) continue;
        const char *p = libevdev_get_phys(dev2);
        bool hasKeys = libevdev_has_event_type(dev2, EV_KEY);
        closeDev(fd2, dev2);
        if (p && QString::fromLocal8Bit(p).startsWith(physPrefix) && hasKeys)
            siblings.append(path);
    }
    return siblings.isEmpty() ? QList<QString>{ primaryPath } : siblings;
}

QList<QString> findCaptureDevices(const QString &primaryPath)
{
    QList<QString> result;
    QSet<QString>  seen;

    if (!primaryPath.isEmpty()) {
        for (const QString &p : findSiblingDevices(primaryPath)) {
            if (!seen.contains(p)) { seen.insert(p); result.append(p); }
        }
    }

    for (const QString &path : listEvdevDevices()) {
        if (seen.contains(path)) continue;
        auto [fd, dev] = openDev(path);
        if (fd < 0) continue;
        bool hasKeys = libevdev_has_event_type(dev, EV_KEY);
        closeDev(fd, dev);
        if (hasKeys) { seen.insert(path); result.append(path); }
    }
    return result;
}

QList<std::pair<QString, bool>> findHotkeyDevices(const QString &primaryPath,
                                                    const QSet<int> &hotkeyCodes)
{
    QList<std::pair<QString, bool>> result;
    QSet<QString> seen;

    if (!primaryPath.isEmpty()) {
        for (const QString &p : findSiblingDevices(primaryPath)) {
            if (!seen.contains(p)) {
                seen.insert(p);
                result.append({ p, true });
            }
        }
    }

    for (const QString &path : listEvdevDevices()) {
        if (seen.contains(path)) continue;
        auto [fd, dev] = openDev(path);
        if (fd < 0) continue;
        bool hasHotkey = false;
        for (int code : hotkeyCodes) {
            if (libevdev_has_event_code(dev, EV_KEY, static_cast<unsigned int>(code))) {
                hasHotkey = true;
                break;
            }
        }
        closeDev(fd, dev);
        if (hasHotkey) {
            seen.insert(path);
            result.append({ path, false });
        }
    }
    return result;
}

// ─── KeyCaptureThread ─────────────────────────────────────────────────────────
KeyCaptureThread::KeyCaptureThread(const QString &devicePath, QObject *parent)
    : QThread(parent), m_devicePath(devicePath)
{}

void KeyCaptureThread::cancel()
{
    m_running = false;
    wait(1000);
}

void KeyCaptureThread::run()
{
    const QList<QString> candidates = findCaptureDevices(m_devicePath);

    // {fd, dev, grabbed}
    struct DevEntry { int fd; libevdev *dev; bool grabbed; };
    QList<DevEntry> devices;

    for (const QString &path : candidates) {
        int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        libevdev *dev = nullptr;
        if (libevdev_new_from_fd(fd, &dev) < 0) { ::close(fd); continue; }
        int rc = libevdev_grab(dev, LIBEVDEV_GRAB);
        bool grabbed = (rc == 0);
        if (!grabbed) {
            qDebug() << "[CaptureThread] Cannot grab" << path << ":" << strerror(-rc);
        } else {
            qDebug() << "[CaptureThread] Grabbed" << path;
        }
        devices.append({ fd, dev, grabbed });
    }

    if (devices.isEmpty()) {
        emit cancelled();
        return;
    }

    // Build fd_set for select()
    while (m_running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = -1;
        for (const auto &e : devices) {
            FD_SET(e.fd, &rfds);
            if (e.fd > maxfd) maxfd = e.fd;
        }
        struct timeval tv { 0, 100'000 };   // 100 ms
        int ret = ::select(maxfd + 1, &rfds, nullptr, nullptr, &tv);
        if (ret <= 0) continue;

        for (const auto &e : devices) {
            if (!FD_ISSET(e.fd, &rfds)) continue;
            input_event ev{};
            int rc;
            while ((rc = libevdev_next_event(e.dev, LIBEVDEV_READ_FLAG_NORMAL, &ev)) >= 0) {
                if (rc == LIBEVDEV_READ_STATUS_SYNC) continue;
                if (ev.type != EV_KEY) continue;
                if (ev.value != 1) continue;  // key-down only
                m_running = false;
                if (ev.code == KEY_ESC)
                    emit cancelled();
                else
                    emit key_captured(static_cast<int>(ev.code));
                goto done;
            }
        }
    }
done:
    for (auto &e : devices) {
        if (e.grabbed) libevdev_grab(e.dev, LIBEVDEV_UNGRAB);
        closeDev(e.fd, e.dev);
        qDebug() << "[CaptureThread] Released device";
    }
}

// ─── InputHandler ─────────────────────────────────────────────────────────────
InputHandler::InputHandler(QObject *parent)
    : QThread(parent)
{}

void InputHandler::setHotkeys(int up, int down, int mute)
{
    m_keyUp   = up;
    m_keyDown = down;
    m_keyMute = mute;
}

std::tuple<int,int,int> InputHandler::currentHotkeys() const
{
    return { m_keyUp, m_keyDown, m_keyMute };
}

void InputHandler::startDevice(const QString &newPath)
{
    stop();
    m_devicePath = newPath;
    m_running    = true;
    start();
}

void InputHandler::restart()
{
    if (!m_devicePath.isEmpty())
        startDevice(m_devicePath);
}

void InputHandler::stop()
{
    m_running = false;
    quit();
    wait(2000);
}

void InputHandler::run()
{
    if (m_devicePath.isEmpty()) return;

    const QSet<int> hotkeys { m_keyUp, m_keyDown, m_keyMute };
    const auto candidates = findHotkeyDevices(m_devicePath, hotkeys);

    struct DevEntry {
        int             fd;
        libevdev        *dev;
        libevdev_uinput *ui;
        bool             grabbed;
    };
    QList<DevEntry> devices;

    for (const auto &[path, exclusive] : candidates) {
        int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            qDebug() << "[InputHandler] Cannot open" << path;
            continue;
        }
        libevdev *dev = nullptr;
        if (libevdev_new_from_fd(fd, &dev) < 0) {
            ::close(fd);
            qDebug() << "[InputHandler] libevdev_new_from_fd failed" << path;
            continue;
        }

        bool grabbed = false;
        libevdev_uinput *ui = nullptr;

        if (exclusive) {
            if (libevdev_grab(dev, LIBEVDEV_GRAB) == 0) {
                grabbed = true;
                // Create a mirror UInput device for re-injection
                int rc = libevdev_uinput_create_from_device(
                    dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &ui);
                if (rc < 0) {
                    qDebug() << "[InputHandler] Cannot create uinput for" << path
                             << ":" << strerror(-rc);
                    ui = nullptr;
                } else {
                    qDebug() << "[InputHandler] Created uinput for" << path;
                }
            } else {
                qDebug() << "[InputHandler] Cannot grab" << path;
            }
        }
        devices.append({ fd, dev, ui, grabbed });
        qDebug() << "[InputHandler] Opened" << path
                 << (exclusive ? "[grabbed]" : "[passive]");
    }

    if (devices.isEmpty()) return;

    const int keyUp   = m_keyUp;
    const int keyDown = m_keyDown;
    const int keyMute = m_keyMute;

    while (m_running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = -1;
        for (const auto &e : devices) {
            FD_SET(e.fd, &rfds);
            if (e.fd > maxfd) maxfd = e.fd;
        }
        struct timeval tv { 0, 200'000 };   // 200 ms
        int ret = ::select(maxfd + 1, &rfds, nullptr, nullptr, &tv);
        if (ret < 0) { m_running = false; break; }
        if (ret == 0) continue;

        for (auto &e : devices) {
            if (!FD_ISSET(e.fd, &rfds)) continue;
            input_event ev{};
            int rc;
            while ((rc = libevdev_next_event(e.dev, LIBEVDEV_READ_FLAG_NORMAL, &ev)) >= 0) {
                if (rc == LIBEVDEV_READ_STATUS_SYNC) {
                    // Drain sync queue
                    while (libevdev_next_event(e.dev, LIBEVDEV_READ_FLAG_SYNC, &ev) >= 0) {
                        if (e.grabbed && e.ui)
                            libevdev_uinput_write_event(e.ui, ev.type, ev.code, ev.value);
                    }
                    continue;
                }

                if (ev.type == EV_KEY) {
                    bool isHotkey = (ev.code == static_cast<unsigned>(keyUp)
                                  || ev.code == static_cast<unsigned>(keyDown)
                                  || ev.code == static_cast<unsigned>(keyMute));
                    if (isHotkey) {
                        // Swallow all states (down/repeat/up)
                        if (ev.value == 1) {   // key-down only for triggering
                            qint64 now  = QDateTime::currentMSecsSinceEpoch();
                            qint64 last = m_lastTriggerMs.value(ev.code, 0LL);
                            if (now - last >= 100) {
                                m_lastTriggerMs[ev.code] = now;
                                if (static_cast<int>(ev.code) == keyUp)
                                    emit volume_up();
                                else if (static_cast<int>(ev.code) == keyDown)
                                    emit volume_down();
                                else if (static_cast<int>(ev.code) == keyMute)
                                    emit volume_mute();
                            }
                        }
                        continue;   // never re-inject hotkey events
                    }
                    // Non-hotkey EV_KEY → re-inject for grabbed devices
                    if (e.grabbed && e.ui)
                        libevdev_uinput_write_event(e.ui, ev.type, ev.code, ev.value);
                } else {
                    // EV_SYN, EV_MSC, EV_REP → re-inject for grabbed devices
                    if (e.grabbed && e.ui)
                        libevdev_uinput_write_event(e.ui, ev.type, ev.code, ev.value);
                }
            }
            // LIBEVDEV_READ_STATUS_ERROR (-EIO) — device disconnected
            if (rc == -EIO) { m_running = false; break; }
        }
    }

    // Cleanup
    for (auto &e : devices) {
        if (e.ui) libevdev_uinput_destroy(e.ui);
        if (e.grabbed) libevdev_grab(e.dev, LIBEVDEV_UNGRAB);
        closeDev(e.fd, e.dev);
        qDebug() << "[InputHandler] Released device";
    }
}
