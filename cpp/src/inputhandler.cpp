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
#include <sys/ioctl.h>
#include <linux/input.h>
#include <algorithm>
#include <array>
#include <cerrno>
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

    qsizetype slashIdx = phys.lastIndexOf(QLatin1String("/input"));
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
namespace
{

constexpr std::array<unsigned int, 3> kMirroredLedCodes{LED_NUML, LED_CAPSL, LED_SCROLLL};
constexpr int kLedBitsPerWord = static_cast<int>(sizeof(unsigned long) * 8);
constexpr int kLedStateWords = (LED_CNT + kLedBitsPerWord - 1) / kLedBitsPerWord;

struct PollSource
{
    enum class Type
    {
        EvdevInput,
        UinputOutput,
    };

    Type type;
    EvdevDevice* dev;
    int fd;
};

bool isMirroredLedCode(unsigned int code)
{
    return std::find(kMirroredLedCodes.begin(), kMirroredLedCodes.end(), code) !=
           kMirroredLedCodes.end();
}

bool ledBitIsSet(const std::array<unsigned long, kLedStateWords>& bits, unsigned int code)
{
    return (bits[code / kLedBitsPerWord] & (1UL << (code % kLedBitsPerWord))) != 0;
}

bool setFdNonBlocking(int fd)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    if ((flags & O_NONBLOCK) != 0) return true;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void mirrorLedToPhysical(EvdevDevice* dev, unsigned int code, bool enabled)
{
    if (!dev || !isMirroredLedCode(code) || !dev->hasEventCode(EV_LED, code)) return;

    if (!dev->setLedValue(code, enabled))
    {
        qWarning() << "[InputHandler] Cannot mirror keyboard LED" << code
                   << (enabled ? "on" : "off") << "to physical device";
    }
}

void handleUinputOutputEvent(EvdevDevice* dev, const input_event& ev)
{
    if (ev.type != EV_LED) return;
    mirrorLedToPhysical(dev, ev.code, ev.value != 0);
}

void drainUinputOutput(EvdevDevice* dev)
{
    const int fd = dev ? dev->uinputFd() : -1;
    if (fd < 0) return;

    while (true)
    {
        input_event ev{};
        const ssize_t n = ::read(fd, &ev, sizeof(ev));
        if (n == static_cast<ssize_t>(sizeof(ev)))
        {
            handleUinputOutputEvent(dev, ev);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        {
            if (errno == EINTR) continue;
            break;
        }
        if (n < 0)
        {
            qWarning() << "[InputHandler] Cannot read uinput output event:" << strerror(errno);
        }
        break;
    }
}

void syncPhysicalLedsFromUinput(EvdevDevice* dev)
{
    if (!dev || !dev->uinput() || !dev->hasEventType(EV_LED)) return;

    const char* devnode = libevdev_uinput_get_devnode(dev->uinput());
    if (!devnode)
    {
        qWarning() << "[InputHandler] Cannot query uinput devnode for initial LED sync";
        return;
    }

    const int fd = ::open(devnode, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
    {
        qWarning() << "[InputHandler] Cannot open" << devnode
                   << "for initial LED sync:" << strerror(errno);
        return;
    }

    std::array<unsigned long, kLedStateWords> bits{};
    const int rc = ioctl(fd, EVIOCGLED(sizeof(bits)), bits.data());
    const int savedErrno = errno;
    ::close(fd);

    if (rc < 0)
    {
        qWarning() << "[InputHandler] Cannot query initial LED state for" << devnode << ":"
                   << strerror(savedErrno);
        return;
    }

    for (unsigned int code : kMirroredLedCodes)
    {
        if (dev->hasEventCode(EV_LED, code))
            mirrorLedToPhysical(dev, code, ledBitIsSet(bits, code));
    }
}

} // namespace

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
    if (profile.hotkeys.show.isAssigned() && profile.hotkeys.show == binding)
    {
        return ProfileHotkeyAction::ShowVolume;
    }
    return ProfileHotkeyAction::None;
}

} // namespace

ProfileHotkeyMatch resolveProfileHotkey(const HotkeyBinding& binding, const QSet<Modifier>& held,
                                        const QList<Profile>& profiles)
{
    ProfileHotkeyMatch best;
    qsizetype bestSpec = -1;
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

        const qsizetype spec = p.modifiers.size();
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

MediaAction resolveMediaHotkey(const HotkeyBinding& binding, const MediaHotkeyConfig& cfg)
{
    if (!binding.isAssigned()) return MediaAction::None;
    if (cfg.playPause.isAssigned() && cfg.playPause == binding) return MediaAction::PlayPause;
    if (cfg.next.isAssigned() && cfg.next == binding) return MediaAction::Next;
    if (cfg.previous.isAssigned() && cfg.previous == binding) return MediaAction::Previous;
    if (cfg.stop.isAssigned() && cfg.stop == binding) return MediaAction::Stop;
    return MediaAction::None;
}

MediaAction resolveMediaHotkey(int code, const MediaHotkeyConfig& cfg)
{
    return resolveMediaHotkey(HotkeyBinding::key(code), cfg);
}

QString resolveSceneHotkey(const HotkeyBinding& binding, const QList<AudioScene>& scenes)
{
    if (!binding.isAssigned()) return {};
    for (const AudioScene& scene : scenes)
    {
        if (scene.id.isEmpty()) continue;
        if (scene.hotkey.isAssigned() && scene.hotkey == binding) return scene.id;
    }
    return {};
}

QString resolveSceneHotkey(int code, const QList<AudioScene>& scenes)
{
    return resolveSceneHotkey(HotkeyBinding::key(code), scenes);
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

void InputHandler::setMediaHotkeys(const MediaHotkeyConfig& cfg)
{
    QMutexLocker lock(&m_profilesMutex);
    m_mediaHotkeys = cfg;
}

MediaHotkeyConfig InputHandler::currentMediaHotkeys() const
{
    QMutexLocker lock(&m_profilesMutex);
    return m_mediaHotkeys;
}

void InputHandler::setScenes(const QList<AudioScene>& scenes)
{
    QMutexLocker lock(&m_profilesMutex);
    m_scenes = scenes;
}

QList<AudioScene> InputHandler::currentScenes() const
{
    QMutexLocker lock(&m_profilesMutex);
    return m_scenes;
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

    // Snapshot current profiles + media config for the lifetime of this run.
    // Caller (App) restarts the thread on profile/media changes, so this
    // snapshot is fresh for each restart.
    const QList<Profile> profiles = currentProfiles();
    const MediaHotkeyConfig mediaCfg = currentMediaHotkeys();
    const QList<AudioScene> scenes = currentScenes();

    // Sentinel profile id used as the second key of m_lastTriggerMs for media
    // bindings, so debounce buckets never collide with real profile ids.
    const QString kMediaProfile = QStringLiteral("__media__");
    // Sentinel prefix for scene debounce buckets (one bucket per scene id).
    const QString kSceneProfilePrefix = QStringLiteral("__scene__:");

    // Union of every hotkey binding used by any profile + global media bindings
    // + scene apply bindings.
    QSet<HotkeyBinding> hotkeys;
    for (const Profile& p : profiles)
    {
        if (p.hotkeys.volumeUp.isAssigned()) hotkeys.insert(p.hotkeys.volumeUp);
        if (p.hotkeys.volumeDown.isAssigned()) hotkeys.insert(p.hotkeys.volumeDown);
        if (p.hotkeys.mute.isAssigned()) hotkeys.insert(p.hotkeys.mute);
        if (p.ducking.enabled && p.ducking.hotkey.isAssigned()) hotkeys.insert(p.ducking.hotkey);
        if (p.hotkeys.show.isAssigned()) hotkeys.insert(p.hotkeys.show);
    }
    if (mediaCfg.playPause.isAssigned()) hotkeys.insert(mediaCfg.playPause);
    if (mediaCfg.next.isAssigned()) hotkeys.insert(mediaCfg.next);
    if (mediaCfg.previous.isAssigned()) hotkeys.insert(mediaCfg.previous);
    if (mediaCfg.stop.isAssigned()) hotkeys.insert(mediaCfg.stop);
    for (const AudioScene& scene : scenes)
    {
        if (scene.hotkey.isAssigned()) hotkeys.insert(scene.hotkey);
    }

    if (hotkeys.isEmpty())
    {
        qWarning() << "[InputHandler] No hotkeys configured — nothing to grab";
        return;
    }

    // Helper: dispatch a profile match (debounced). Returns true on dispatch.
    auto dispatchProfile = [&](const HotkeyBinding& binding, const ProfileHotkeyMatch& match)
    {
        const QPair<HotkeyBinding, QString> key{binding, match.profileId};
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 last = m_lastTriggerMs.value(key, 0LL);
        if (now - last < 100) return;
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
        case ProfileHotkeyAction::ShowVolume:
            emit show_volume(match.profileId);
            break;
        case ProfileHotkeyAction::None:
            break;
        }
    };

    // Helper: dispatch a media match (debounced). Returns true on dispatch.
    auto dispatchMedia = [&](const HotkeyBinding& binding, MediaAction action)
    {
        const QPair<HotkeyBinding, QString> key{binding, kMediaProfile};
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 last = m_lastTriggerMs.value(key, 0LL);
        if (now - last < 100) return;
        m_lastTriggerMs[key] = now;
        switch (action)
        {
        case MediaAction::PlayPause:
            emit media_play_pause();
            break;
        case MediaAction::Next:
            emit media_next();
            break;
        case MediaAction::Previous:
            emit media_previous();
            break;
        case MediaAction::Stop:
            emit media_stop();
            break;
        case MediaAction::None:
            break;
        }
    };

    // Helper: dispatch a scene apply (debounced per scene id).
    auto dispatchScene = [&](const HotkeyBinding& binding, const QString& sceneId)
    {
        if (sceneId.isEmpty()) return;
        const QPair<HotkeyBinding, QString> key{binding, kSceneProfilePrefix + sceneId};
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 last = m_lastTriggerMs.value(key, 0LL);
        if (now - last < 100) return;
        m_lastTriggerMs[key] = now;
        emit scene_apply(sceneId);
    };

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

        if (exclusive && dev->hasEventType(EV_LED))
        {
            dev->close();
            if (!dev->open(path, EvdevDevice::OpenMode::ReadWrite))
            {
                qWarning() << "[InputHandler] Cannot open" << path
                           << "read/write for LED sync — retrying read-only";
                if (!dev->open(path))
                {
                    qDebug() << "[InputHandler] Cannot reopen" << path << "read-only";
                    continue;
                }
            }
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
            if (dev->hasEventType(EV_LED) && dev->isWritable())
            {
                const int uinputFd = dev->uinputFd();
                if (uinputFd >= 0 && !setFdNonBlocking(uinputFd))
                {
                    qWarning() << "[InputHandler] Cannot make uinput output fd non-blocking for"
                               << path << "— LED sync disabled for this device";
                }
            }
            else if (dev->hasEventType(EV_LED))
            {
                qWarning() << "[InputHandler] LED sync disabled for" << path
                           << "because the device is not writable";
            }
            qDebug() << "[InputHandler] Created uinput for" << path;
        }
        qDebug() << "[InputHandler] Opened" << path
                 << (dev->isGrabbed() ? "[grabbed+uinput]" : "[passive]");
        devices.push_back(std::move(dev));
    }

    if (devices.empty()) return;

    int epfd = epoll_create1(0);
    std::vector<PollSource> pollSources;
    pollSources.reserve(devices.size() * 2);
    if (epfd >= 0)
    {
        for (auto& dev : devices)
        {
            pollSources.push_back({PollSource::Type::EvdevInput, dev.get(), dev->fd()});

            const int uinputFd = dev->uinputFd();
            if (dev->isWritable() && dev->hasEventType(EV_LED) && uinputFd >= 0 &&
                setFdNonBlocking(uinputFd))
            {
                pollSources.push_back({PollSource::Type::UinputOutput, dev.get(), uinputFd});
            }
        }
        for (auto& source : pollSources)
        {
            epoll_event evnt{};
            evnt.events = EPOLLIN;
            evnt.data.ptr = &source;
            epoll_ctl(epfd, EPOLL_CTL_ADD, source.fd, &evnt);
        }
    }

    // Modifier state — observed only on grabbed devices (v1 limitation).
    QSet<int> heldModifiers;
    qint64 startupLedSyncUntilMs = QDateTime::currentMSecsSinceEpoch() + 2000;
    qint64 nextStartupLedSyncMs = QDateTime::currentMSecsSinceEpoch();

    auto syncStartupLedsIfDue = [&]()
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now > startupLedSyncUntilMs || now < nextStartupLedSyncMs) return;
        for (auto& dev : devices) syncPhysicalLedsFromUinput(dev.get());
        nextStartupLedSyncMs = now + 250;
    };

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
        syncStartupLedsIfDue();
        if (n == 0) continue;

        for (int i = 0; i < n; ++i)
        {
            auto* source = static_cast<PollSource*>(events[i].data.ptr);
            if (!source || !source->dev) continue;
            if (source->type == PollSource::Type::UinputOutput)
            {
                drainUinputOutput(source->dev);
                continue;
            }

            auto* e = source->dev;
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
                                dispatchProfile(binding, match);
                                // Hotkey consumed — do NOT forward.
                                continue;
                            }
                            // No profile matched — try scenes next (priority:
                            // profile > scene > media). Scene bindings ignore
                            // modifiers in v1.
                            const QString sceneId = resolveSceneHotkey(binding, scenes);
                            if (!sceneId.isEmpty())
                            {
                                dispatchScene(binding, sceneId);
                                continue;
                            }
                            // No scene matched — fall back to media. Media
                            // dispatch ignores modifiers in v1: a profile that
                            // requires modifiers had its chance above.
                            const MediaAction maction = resolveMediaHotkey(binding, mediaCfg);
                            if (maction != MediaAction::None)
                            {
                                dispatchMedia(binding, maction);
                                continue;
                            }
                            // Nothing matched (e.g. modifier combo was wrong) —
                            // forward as a normal key so typing isn't blocked.
                        }
                        else if (ev.value == 0)
                        {
                            // Release of a scene-only or media-only binding must
                            // also be consumed so the desktop doesn't see a
                            // key-up without a key-down.
                            if (!resolveSceneHotkey(binding, scenes).isEmpty()) continue;
                            if (resolveMediaHotkey(binding, mediaCfg) != MediaAction::None)
                                continue;
                        }
                        // Forward releases / unhandled events.
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
                                         .profileId.isEmpty() ||
                                    !resolveSceneHotkey(binding, scenes).isEmpty() ||
                                    resolveMediaHotkey(binding, mediaCfg) != MediaAction::None)
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
                                dispatchProfile(matchedBinding, match);
                                continue;
                            }
                            const QString sceneId = resolveSceneHotkey(matchedBinding, scenes);
                            if (!sceneId.isEmpty())
                            {
                                dispatchScene(matchedBinding, sceneId);
                                continue;
                            }
                            const MediaAction maction =
                                resolveMediaHotkey(matchedBinding, mediaCfg);
                            if (maction != MediaAction::None)
                            {
                                dispatchMedia(matchedBinding, maction);
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

    if (!devices.empty())
    {
        for (auto& dev : devices) dev->preserveLedStateFromUinput();
        for (auto& dev : devices) dev->destroyUinput();
        for (auto& dev : devices) dev->ungrab();
        QThread::msleep(750);
        for (auto& dev : devices) dev->restorePreservedLedState();
    }

    if (!devices.empty()) qDebug() << "[InputHandler] Released" << devices.size() << "devices";
}
