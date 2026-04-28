#include "inputhandler.h"
#include "evdevdevice.h"

#include <QDir>
#include <QDateTime>
#include <QDebug>

#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <linux/input.h>
#include <cstring>
#include <memory>
#include <vector>

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

QList<QString> findSiblingDevices(const QString &primaryPath)
{
    EvdevDevice primary(primaryPath);
    if (!primary.isOpen()) return { primaryPath };

    const char *physRaw = primary.phys();
    QString phys = physRaw ? QString::fromLocal8Bit(physRaw) : QString{};
    primary.close();

    if (phys.isEmpty()) return { primaryPath };

    int slashIdx = phys.lastIndexOf(QLatin1String("/input"));
    QString physPrefix = (slashIdx >= 0) ? phys.left(slashIdx) : phys;

    QList<QString> siblings;
    for (const QString &path : listEvdevDevices()) {
        EvdevDevice dev(path);
        if (!dev.isOpen()) continue;
        const char *p = dev.phys();
        QString devPhys = p ? QString::fromLocal8Bit(p) : QString{};
        if (devPhys.isEmpty() || !devPhys.startsWith(physPrefix)
            || !dev.hasEventType(EV_KEY))
        {
            dev.close();
            continue;
        }
        dev.close();
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
        EvdevDevice dev(path);
        if (!dev.isOpen()) continue;
        if (dev.hasEventType(EV_KEY)) { seen.insert(path); result.append(path); }
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
        EvdevDevice dev(path);
        if (!dev.isOpen()) continue;
        bool hasHotkey = false;
        for (int code : hotkeyCodes) {
            if (dev.hasEventCode(EV_KEY, static_cast<unsigned int>(code))) {
                hasHotkey = true;
                break;
            }
        }
        if (hasHotkey) {
            seen.insert(path);
            result.append({ path, true });
        }
    }
    return result;
}

QList<std::pair<QString, QString>> getVolumeDevices()
{
    QList<std::pair<QString, QString>> result;
    for (const QString &path : listEvdevDevices()) {
        EvdevDevice dev(path);
        if (!dev.isOpen()) continue;
        bool hasVol = dev.hasEventCode(EV_KEY, KEY_VOLUMEUP)
                   || dev.hasEventCode(EV_KEY, KEY_VOLUMEDOWN);
        QString name = QString::fromUtf8(dev.name());
        if (hasVol)
            result.append({ path, QStringLiteral("%1  [%2]").arg(name, path) });
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

    std::vector<std::unique_ptr<EvdevDevice>> devices;

    for (const QString &path : candidates) {
        auto dev = std::make_unique<EvdevDevice>(path);
        if (!dev->isOpen()) continue;
        if (dev->grab())
            qDebug() << "[CaptureThread] Grabbed" << path;
        else
            qDebug() << "[CaptureThread] Cannot grab" << path;
        devices.push_back(std::move(dev));
    }

    if (devices.empty()) {
        emit cancelled();
        return;
    }

    int epfd = epoll_create1(0);
    if (epfd >= 0) {
        for (auto &dev : devices) {
            epoll_event evnt{};
            evnt.events = EPOLLIN;
            evnt.data.ptr = dev.get();
            epoll_ctl(epfd, EPOLL_CTL_ADD, dev->fd(), &evnt);
        }
    }

    while (m_running) {
        if (epfd < 0) { m_running = false; break; }
        epoll_event events[16];
        int n = epoll_wait(epfd, events, 16, 50);
        if (n < 0) {
            if (errno == EINTR) continue;
            m_running = false; break;
        }
        if (n == 0) continue;

        for (int i = 0; i < n; ++i) {
            auto *e = static_cast<EvdevDevice *>(events[i].data.ptr);
            input_event ev{};
            int rc;
            while ((rc = libevdev_next_event(e->dev(), LIBEVDEV_READ_FLAG_NORMAL, &ev)) >= 0) {
                if (rc == LIBEVDEV_READ_STATUS_SYNC) continue;
                if (ev.type != EV_KEY) continue;
                if (ev.value != 1) continue;
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
    if (epfd >= 0) ::close(epfd);
    for (auto &dev : devices)
        qDebug() << "[CaptureThread] Released device";
}

// ─── Shared helpers ────────────────────────────────────────────────────────────
static inline void forwardUinputEvent(EvdevDevice *dev, const input_event &ev)
{
    if (dev->isGrabbed() && dev->uinput())
        libevdev_uinput_write_event(dev->uinput(), ev.type, ev.code, ev.value);
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

    std::vector<std::unique_ptr<EvdevDevice>> devices;

    for (const auto &[path, exclusive] : candidates) {
        auto dev = std::make_unique<EvdevDevice>(path);
        if (!dev->isOpen()) {
            qDebug() << "[InputHandler] Cannot open" << path;
            continue;
        }

        if (exclusive) {
            if (!dev->grab()) {
                qDebug() << "[InputHandler] Cannot grab" << path << "— skipping";
                continue;
            }
            if (!dev->createUinput()) {
                qDebug() << "[InputHandler] Cannot create uinput for" << path
                         << "— releasing grab and skipping";
                continue;
            }
            qDebug() << "[InputHandler] Created uinput for" << path;
        }
        qDebug() << "[InputHandler] Opened" << path
                 << (dev->isGrabbed() ? "[grabbed+uinput]" : "[passive]");
        devices.push_back(std::move(dev));
    }

    if (devices.empty()) return;

    const int keyUp   = m_keyUp;
    const int keyDown = m_keyDown;
    const int keyMute = m_keyMute;

    int epfd = epoll_create1(0);
    if (epfd >= 0) {
        for (auto &dev : devices) {
            epoll_event evnt{};
            evnt.events = EPOLLIN;
            evnt.data.ptr = dev.get();
            epoll_ctl(epfd, EPOLL_CTL_ADD, dev->fd(), &evnt);
        }
    }

    while (m_running) {
        if (epfd < 0) { m_running = false; break; }
        epoll_event events[32];
        int n = epoll_wait(epfd, events, 32, 50);
        if (n < 0) {
            if (errno == EINTR) continue;
            m_running = false; break;
        }
        if (n == 0) continue;

        for (int i = 0; i < n; ++i) {
            auto *e = static_cast<EvdevDevice *>(events[i].data.ptr);
            input_event ev{};
            int rc;
            while ((rc = libevdev_next_event(e->dev(), LIBEVDEV_READ_FLAG_NORMAL, &ev)) >= 0) {
                if (rc == LIBEVDEV_READ_STATUS_SYNC) {
                    while (libevdev_next_event(e->dev(), LIBEVDEV_READ_FLAG_SYNC, &ev) >= 0)
                        forwardUinputEvent(e, ev);
                    continue;
                }

                if (ev.type == EV_KEY) {
                    bool isHotkey = (ev.code == static_cast<unsigned>(keyUp)
                                  || ev.code == static_cast<unsigned>(keyDown)
                                  || ev.code == static_cast<unsigned>(keyMute));
                    if (isHotkey) {
                        if (ev.value == 1 || ev.value == 2) {  // press or repeat
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
                        continue;
                    }
                    forwardUinputEvent(e, ev);
                } else {
                    forwardUinputEvent(e, ev);
                }
            }
            if (rc == -EIO) { m_running = false; break; }
        }
    }

    if (epfd >= 0) ::close(epfd);

    for (auto &dev : devices)
        qDebug() << "[InputHandler] Released device";
}
