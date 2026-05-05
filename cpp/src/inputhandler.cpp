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
    dir.setNameFilters({QStringLiteral("event*")});
    dir.setSorting(QDir::Name);
    for (const QFileInfo& fi : dir.entryInfoList(QDir::System))
        result.append(fi.absoluteFilePath());
    return result;
}

QList<QString> findSiblingDevices(const QString& primaryPath)
{
    EvdevDevice primary(primaryPath);
    if (!primary.isOpen()) return {primaryPath};

    const char* physRaw = primary.phys();
    QString phys = physRaw ? QString::fromLocal8Bit(physRaw) : QString{};
    primary.close();

    if (phys.isEmpty()) return {primaryPath};

    int slashIdx = phys.lastIndexOf(QLatin1String("/input"));
    QString physPrefix = (slashIdx >= 0) ? phys.left(slashIdx) : phys;

    QList<QString> siblings;
    for (const QString& path : listEvdevDevices())
    {
        EvdevDevice dev(path);
        if (!dev.isOpen()) continue;
        const char* p = dev.phys();
        QString devPhys = p ? QString::fromLocal8Bit(p) : QString{};
        if (devPhys.isEmpty() || !devPhys.startsWith(physPrefix) || !dev.hasEventType(EV_KEY))
        {
            dev.close();
            continue;
        }
        dev.close();
        siblings.append(path);
    }
    return siblings.isEmpty() ? QList<QString>{primaryPath} : siblings;
}

QList<QString> findCaptureDevices(const QString& primaryPath)
{
    QList<QString> result;
    QSet<QString> seen;

    if (!primaryPath.isEmpty())
    {
        for (const QString& p : findSiblingDevices(primaryPath))
        {
            if (!seen.contains(p))
            {
                seen.insert(p);
                result.append(p);
            }
        }
    }

    for (const QString& path : listEvdevDevices())
    {
        if (seen.contains(path)) continue;
        EvdevDevice dev(path);
        if (!dev.isOpen()) continue;
        if (dev.hasEventType(EV_KEY))
        {
            seen.insert(path);
            result.append(path);
        }
    }
    return result;
}

QList<std::pair<QString, bool>> findHotkeyDevices(const QString& primaryPath,
                                                  const QSet<int>& hotkeyCodes)
{
    QList<std::pair<QString, bool>> result;
    QSet<QString> seen;

    if (!primaryPath.isEmpty())
    {
        for (const QString& p : findSiblingDevices(primaryPath))
        {
            if (!seen.contains(p))
            {
                seen.insert(p);
                result.append({p, true});
            }
        }
    }

    for (const QString& path : listEvdevDevices())
    {
        if (seen.contains(path)) continue;
        EvdevDevice dev(path);
        if (!dev.isOpen()) continue;
        bool hasHotkey = false;
        for (int code : hotkeyCodes)
        {
            if (dev.hasEventCode(EV_KEY, static_cast<unsigned int>(code)))
            {
                hasHotkey = true;
                break;
            }
        }
        if (hasHotkey)
        {
            seen.insert(path);
            result.append({path, true});
        }
    }
    return result;
}

QList<std::pair<QString, QString>> getVolumeDevices()
{
    QList<std::pair<QString, QString>> result;
    for (const QString& path : listEvdevDevices())
    {
        EvdevDevice dev(path);
        if (!dev.isOpen()) continue;
        bool hasVol =
            dev.hasEventCode(EV_KEY, KEY_VOLUMEUP) || dev.hasEventCode(EV_KEY, KEY_VOLUMEDOWN);
        QString name = QString::fromUtf8(dev.name());
        if (hasVol) result.append({path, QStringLiteral("%1  [%2]").arg(name, path)});
    }
    return result;
}

// ─── KeyCaptureThread ─────────────────────────────────────────────────────────
KeyCaptureThread::KeyCaptureThread(const QString& devicePath, QObject* parent)
    : QThread(parent), m_devicePath(devicePath)
{
}

void KeyCaptureThread::cancel()
{
    m_running = false;
    wait(1000);
}

void KeyCaptureThread::run()
{
    const QList<QString> candidates = findCaptureDevices(m_devicePath);

    std::vector<std::unique_ptr<EvdevDevice>> devices;

    for (const QString& path : candidates)
    {
        auto dev = std::make_unique<EvdevDevice>(path);
        if (!dev->isOpen()) continue;
        if (dev->grab())
            qDebug() << "[CaptureThread] Grabbed" << path;
        else
            qDebug() << "[CaptureThread] Cannot grab" << path;
        devices.push_back(std::move(dev));
    }

    if (devices.empty())
    {
        emit cancelled();
        return;
    }

    int epfd = epoll_create1(0);
    if (epfd >= 0)
    {
        for (auto& dev : devices)
        {
            epoll_event evnt{};
            evnt.events = EPOLLIN;
            evnt.data.ptr = dev.get();
            epoll_ctl(epfd, EPOLL_CTL_ADD, dev->fd(), &evnt);
        }
    }

    while (m_running)
    {
        if (epfd < 0)
        {
            m_running = false;
            break;
        }
        epoll_event events[16];
        int n = epoll_wait(epfd, events, 16, 50);
        if (n < 0)
        {
            if (errno == EINTR) continue;
            m_running = false;
            break;
        }
        if (n == 0) continue;

        for (int i = 0; i < n; ++i)
        {
            auto* e = static_cast<EvdevDevice*>(events[i].data.ptr);
            input_event ev{};
            int rc;
            while ((rc = libevdev_next_event(e->dev(), LIBEVDEV_READ_FLAG_NORMAL, &ev)) >= 0)
            {
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
    if (!devices.empty()) qDebug() << "[CaptureThread] Released" << devices.size() << "devices";
}

// ─── Shared helpers ────────────────────────────────────────────────────────────
static inline void forwardUinputEvent(EvdevDevice* dev, const input_event& ev)
{
    if (dev->isGrabbed() && dev->uinput())
        libevdev_uinput_write_event(dev->uinput(), ev.type, ev.code, ev.value);
}

// ─── Profile resolution helpers ───────────────────────────────────────────────
bool isTrackedModifierCode(int code)
{
    return code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL || code == KEY_LEFTSHIFT ||
           code == KEY_RIGHTSHIFT;
}

QSet<Modifier> normalizeHeldModifiers(const QSet<int>& heldRaw)
{
    QSet<Modifier> out;
    for (int c : heldRaw)
    {
        if (c == KEY_LEFTCTRL || c == KEY_RIGHTCTRL) out.insert(Modifier::Ctrl);
        if (c == KEY_LEFTSHIFT || c == KEY_RIGHTSHIFT) out.insert(Modifier::Shift);
    }
    return out;
}

namespace
{

ProfileHotkeyAction actionForCode(const Profile& profile, int code)
{
    if (profile.hotkeys.volumeUp == code) return ProfileHotkeyAction::VolumeUp;
    if (profile.hotkeys.volumeDown == code) return ProfileHotkeyAction::VolumeDown;
    if (profile.hotkeys.mute == code) return ProfileHotkeyAction::Mute;
    if (profile.ducking.enabled && profile.ducking.hotkey > 0 && profile.ducking.hotkey == code)
    {
        return ProfileHotkeyAction::DuckingToggle;
    }
    return ProfileHotkeyAction::None;
}

} // namespace

ProfileHotkeyMatch resolveProfileHotkey(int code, const QSet<Modifier>& held,
                                        const QList<Profile>& profiles)
{
    ProfileHotkeyMatch best;
    int bestSpec = -1;
    for (const Profile& p : profiles)
    {
        const ProfileHotkeyAction action = actionForCode(p, code);
        if (action == ProfileHotkeyAction::None) continue;

        // profile.modifiers must be a subset of held modifiers
        bool subset = true;
        for (Modifier m : p.modifiers)
        {
            if (!held.contains(m))
            {
                subset = false;
                break;
            }
        }
        if (!subset) continue;

        const int spec = p.modifiers.size();
        if (spec > bestSpec)
        {
            bestSpec = spec;
            best.profileId = p.id;
            best.action = action;
        }
    }
    return best;
}

QString resolveProfile(int code, const QSet<Modifier>& held, const QList<Profile>& profiles)
{
    return resolveProfileHotkey(code, held, profiles).profileId;
}

// ─── InputHandler ─────────────────────────────────────────────────────────────
InputHandler::InputHandler(QObject* parent) : QThread(parent)
{
    // Seed with one default profile so a fresh InputHandler is usable
    // even before the App calls setProfiles().
    Profile def;
    def.id = QStringLiteral("default");
    def.name = QStringLiteral("Default");
    m_profiles = {def};
}

void InputHandler::setProfiles(const QList<Profile>& profiles)
{
    QMutexLocker lock(&m_profilesMutex);
    m_profiles = profiles;
}

QList<Profile> InputHandler::currentProfiles() const
{
    QMutexLocker lock(&m_profilesMutex);
    return m_profiles;
}

void InputHandler::startDevice(const QString& newPath)
{
    stop();
    m_devicePath = newPath;
    m_running = true;
    start();
}

void InputHandler::restart()
{
    if (!m_devicePath.isEmpty()) startDevice(m_devicePath);
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

    // Snapshot current profiles for the lifetime of this run. Caller (App)
    // restarts the thread on profile changes, so this snapshot is fresh
    // for each restart.
    const QList<Profile> profiles = currentProfiles();

    // Union of every hotkey code used by any profile.
    QSet<int> hotkeySet;
    for (const Profile& p : profiles)
    {
        hotkeySet.insert(p.hotkeys.volumeUp);
        hotkeySet.insert(p.hotkeys.volumeDown);
        hotkeySet.insert(p.hotkeys.mute);
        if (p.ducking.enabled && p.ducking.hotkey > 0) hotkeySet.insert(p.ducking.hotkey);
    }
    if (hotkeySet.isEmpty())
    {
        qWarning() << "[InputHandler] No hotkeys configured — nothing to grab";
        return;
    }

    const auto candidates = findHotkeyDevices(m_devicePath, hotkeySet);

    std::vector<std::unique_ptr<EvdevDevice>> devices;

    for (const auto& [path, exclusive] : candidates)
    {
        auto dev = std::make_unique<EvdevDevice>(path);
        if (!dev->isOpen())
        {
            qDebug() << "[InputHandler] Cannot open" << path;
            continue;
        }

        if (exclusive)
        {
            if (!dev->grab())
            {
                qDebug() << "[InputHandler] Cannot grab" << path << "— skipping";
                continue;
            }
            if (!dev->createUinput())
            {
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

    int epfd = epoll_create1(0);
    if (epfd >= 0)
    {
        for (auto& dev : devices)
        {
            epoll_event evnt{};
            evnt.events = EPOLLIN;
            evnt.data.ptr = dev.get();
            epoll_ctl(epfd, EPOLL_CTL_ADD, dev->fd(), &evnt);
        }
    }

    // Modifier state — observed only on grabbed devices (v1 limitation).
    QSet<int> heldModifiers;

    while (m_running)
    {
        if (epfd < 0)
        {
            m_running = false;
            break;
        }
        epoll_event events[32];
        int n = epoll_wait(epfd, events, 32, 50);
        if (n < 0)
        {
            if (errno == EINTR) continue;
            m_running = false;
            break;
        }
        if (n == 0) continue;

        for (int i = 0; i < n; ++i)
        {
            auto* e = static_cast<EvdevDevice*>(events[i].data.ptr);
            input_event ev{};
            int rc;
            while ((rc = libevdev_next_event(e->dev(), LIBEVDEV_READ_FLAG_NORMAL, &ev)) >= 0)
            {
                if (rc == LIBEVDEV_READ_STATUS_SYNC)
                {
                    while (libevdev_next_event(e->dev(), LIBEVDEV_READ_FLAG_SYNC, &ev) >= 0)
                        forwardUinputEvent(e, ev);
                    continue;
                }

                if (ev.type == EV_KEY)
                {
                    const int code = static_cast<int>(ev.code);

                    if (isTrackedModifierCode(code))
                    {
                        // Update modifier state, then forward — the desktop
                        // must still see Ctrl/Shift presses for normal use.
                        if (ev.value == 1)
                            heldModifiers.insert(code);
                        else if (ev.value == 0)
                            heldModifiers.remove(code);
                        // value == 2 (repeat) leaves state unchanged
                        forwardUinputEvent(e, ev);
                        continue;
                    }

                    if (hotkeySet.contains(code))
                    {
                        if (ev.value == 1 || ev.value == 2)
                        { // press or repeat
                            const ProfileHotkeyMatch match = resolveProfileHotkey(
                                code, normalizeHeldModifiers(heldModifiers), profiles);
                            if (!match.profileId.isEmpty())
                            {
                                const QPair<int, QString> key{code, match.profileId};
                                qint64 now = QDateTime::currentMSecsSinceEpoch();
                                qint64 last = m_lastTriggerMs.value(key, 0LL);
                                if (now - last >= 100)
                                {
                                    m_lastTriggerMs[key] = now;
                                    switch (match.action)
                                    {
                                    case ProfileHotkeyAction::VolumeUp:
                                        emit volume_up(match.profileId);
                                        break;
                                    case ProfileHotkeyAction::VolumeDown:
                                        emit volume_down(match.profileId);
                                        break;
                                    case ProfileHotkeyAction::Mute:
                                        emit volume_mute(match.profileId);
                                        break;
                                    case ProfileHotkeyAction::DuckingToggle:
                                        emit ducking_toggle(match.profileId);
                                        break;
                                    case ProfileHotkeyAction::None:
                                        break;
                                    }
                                }
                                // Hotkey consumed — do NOT forward.
                                continue;
                            }
                            // No profile matched (unusual modifier combo) —
                            // forward as a normal key so typing isn't blocked.
                        }
                        // Release of a hotkey code with no matched profile,
                        // or value == 2 with no match — forward.
                        forwardUinputEvent(e, ev);
                        continue;
                    }

                    forwardUinputEvent(e, ev);
                }
                else
                {
                    forwardUinputEvent(e, ev);
                }
            }
            if (rc == -EIO)
            {
                m_running = false;
                break;
            }
        }
    }

    if (epfd >= 0) ::close(epfd);

    if (!devices.empty()) qDebug() << "[InputHandler] Released" << devices.size() << "devices";
}
