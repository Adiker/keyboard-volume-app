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

// Hi-res wheel companion codes — added in Linux 4.19; define fallbacks for
// older kernel headers so the build works everywhere.
#ifndef REL_WHEEL_HI_RES
#define REL_WHEEL_HI_RES 0x0b
#define REL_HWHEEL_HI_RES 0x0c
#endif

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
        const bool usefulInput = dev.hasEventType(EV_KEY) || dev.hasEventType(EV_REL);
        if (devPhys.isEmpty() || !devPhys.startsWith(physPrefix) || !usefulInput)
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
        const bool hasKey = dev.hasEventType(EV_KEY);
        const bool hasScroll =
            dev.hasEventCode(EV_REL, REL_WHEEL) || dev.hasEventCode(EV_REL, REL_HWHEEL);
        if (hasKey || hasScroll)
        {
            seen.insert(path);
            result.append(path);
        }
    }
    return result;
}

QList<std::pair<QString, bool>> findHotkeyDevices(const QString& primaryPath,
                                                  const QSet<HotkeyBinding>& hotkeys)
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
        for (const HotkeyBinding& binding : hotkeys)
        {
            const unsigned int type = binding.type == HotkeyBindingType::Relative ? EV_REL : EV_KEY;
            if (dev.hasEventCode(type, static_cast<unsigned int>(binding.code)))
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
    qRegisterMetaType<HotkeyBinding>("HotkeyBinding");
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
                if (ev.type == EV_KEY)
                {
                    if (ev.value != 1) continue;
                    m_running = false;
                    if (ev.code == KEY_ESC)
                        emit cancelled();
                    else
                        emit hotkey_captured(HotkeyBinding::key(static_cast<int>(ev.code)));
                    goto done;
                }
                if (ev.type == EV_REL && (ev.code == REL_WHEEL || ev.code == REL_HWHEEL) &&
                    ev.value != 0)
                {
                    m_running = false;
                    emit hotkey_captured(
                        HotkeyBinding::relative(static_cast<int>(ev.code), ev.value));
                    goto done;
                }
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

ProfileHotkeyAction actionForBinding(const Profile& profile, const HotkeyBinding& binding)
{
    if (profile.hotkeys.volumeUp == binding) return ProfileHotkeyAction::VolumeUp;
    if (profile.hotkeys.volumeDown == binding) return ProfileHotkeyAction::VolumeDown;
    if (profile.hotkeys.mute == binding) return ProfileHotkeyAction::Mute;
    if (profile.ducking.enabled && profile.ducking.hotkey.isAssigned() &&
        profile.ducking.hotkey == binding)
    {
        return ProfileHotkeyAction::DuckingToggle;
    }
    return ProfileHotkeyAction::None;
}

} // namespace

ProfileHotkeyMatch resolveProfileHotkey(const HotkeyBinding& binding, const QSet<Modifier>& held,
                                        const QList<Profile>& profiles)
{
    ProfileHotkeyMatch best;
    int bestSpec = -1;
    for (const Profile& p : profiles)
    {
        const ProfileHotkeyAction action = actionForBinding(p, binding);
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

ProfileHotkeyMatch resolveProfileHotkey(int code, const QSet<Modifier>& held,
                                        const QList<Profile>& profiles)
{
    return resolveProfileHotkey(HotkeyBinding::key(code), held, profiles);
}

QString resolveProfile(const HotkeyBinding& binding, const QSet<Modifier>& held,
                       const QList<Profile>& profiles)
{
    return resolveProfileHotkey(binding, held, profiles).profileId;
}

QString resolveProfile(int code, const QSet<Modifier>& held, const QList<Profile>& profiles)
{
    return resolveProfile(HotkeyBinding::key(code), held, profiles);
}

bool matchesInputEvent(const HotkeyBinding& binding, const input_event& ev)
{
    if (!binding.isAssigned()) return false;
    if (binding.type == HotkeyBindingType::Key)
    {
        return ev.type == EV_KEY && static_cast<int>(ev.code) == binding.code;
    }
    if (ev.type != EV_REL || static_cast<int>(ev.code) != binding.code || ev.value == 0)
        return false;
    return (binding.direction > 0 && ev.value > 0) || (binding.direction < 0 && ev.value < 0);
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

    // Union of every hotkey binding used by any profile.
    QSet<HotkeyBinding> hotkeys;
    for (const Profile& p : profiles)
    {
        if (p.hotkeys.volumeUp.isAssigned()) hotkeys.insert(p.hotkeys.volumeUp);
        if (p.hotkeys.volumeDown.isAssigned()) hotkeys.insert(p.hotkeys.volumeDown);
        if (p.hotkeys.mute.isAssigned()) hotkeys.insert(p.hotkeys.mute);
        if (p.ducking.enabled && p.ducking.hotkey.isAssigned()) hotkeys.insert(p.ducking.hotkey);
    }
    if (hotkeys.isEmpty())
    {
        qWarning() << "[InputHandler] No hotkeys configured — nothing to grab";
        return;
    }

    const auto candidates = findHotkeyDevices(m_devicePath, hotkeys);

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

                    const HotkeyBinding binding = HotkeyBinding::key(code);
                    if (hotkeys.contains(binding))
                    {
                        if (ev.value == 1 || ev.value == 2)
                        { // press or repeat
                            const ProfileHotkeyMatch match = resolveProfileHotkey(
                                binding, normalizeHeldModifiers(heldModifiers), profiles);
                            if (!match.profileId.isEmpty())
                            {
                                const QPair<HotkeyBinding, QString> key{binding, match.profileId};
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
                        // Release of a hotkey binding with no matched profile,
                        // or value == 2 with no match — forward.
                        forwardUinputEvent(e, ev);
                        continue;
                    }

                    forwardUinputEvent(e, ev);
                }
                else
                {
                    if (ev.type == EV_REL)
                    {
                        // Suppress hi-res companion events when the standard wheel
                        // binding would be consumed. Hi-res mice emit both
                        // REL_WHEEL_HI_RES and REL_WHEEL per notch; without this
                        // the focused app still scrolls while the hotkey fires.
                        if (ev.code == REL_WHEEL_HI_RES || ev.code == REL_HWHEEL_HI_RES)
                        {
                            const unsigned short stdCode =
                                ev.code == REL_WHEEL_HI_RES ? REL_WHEEL : REL_HWHEEL;
                            input_event companion = ev;
                            companion.code = stdCode;
                            bool suppress = false;
                            for (const HotkeyBinding& binding : std::as_const(hotkeys))
                            {
                                if (!matchesInputEvent(binding, companion)) continue;
                                if (!resolveProfileHotkey(
                                         binding, normalizeHeldModifiers(heldModifiers), profiles)
                                         .profileId.isEmpty())
                                {
                                    suppress = true;
                                    break;
                                }
                            }
                            if (!suppress) forwardUinputEvent(e, ev);
                            continue;
                        }

                        HotkeyBinding matchedBinding;
                        bool configured = false;
                        for (const HotkeyBinding& binding : std::as_const(hotkeys))
                        {
                            if (matchesInputEvent(binding, ev))
                            {
                                matchedBinding = binding;
                                configured = true;
                                break;
                            }
                        }

                        if (configured)
                        {
                            const ProfileHotkeyMatch match = resolveProfileHotkey(
                                matchedBinding, normalizeHeldModifiers(heldModifiers), profiles);
                            if (!match.profileId.isEmpty())
                            {
                                const QPair<HotkeyBinding, QString> key{matchedBinding,
                                                                        match.profileId};
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
                                continue;
                            }
                        }
                    }
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
