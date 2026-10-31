#include "inputhandler.h"
#include "evdevdevice.h"
#include "inputpoll.h"

#include <QDir>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDebug>
#include <QFile>

#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
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

bool deviceHasConfiguredHotkey(const libevdev* dev, const QSet<HotkeyBinding>& hotkeys)
{
    if (!dev) return false;
    for (const HotkeyBinding& binding : hotkeys)
    {
        if (!binding.isAssigned()) continue;
        const unsigned int type = binding.type == HotkeyBindingType::Relative ? EV_REL : EV_KEY;
        if (libevdev_has_event_code(dev, type, static_cast<unsigned int>(binding.code)))
            return true;
    }
    return false;
}

std::optional<QBitArray> parseInputCapabilityBitmap(const QByteArray& text, int wordBits)
{
    if (wordBits != 32 && wordBits != 64) return std::nullopt;
    const QByteArray clean = text.simplified();
    if (clean.isEmpty()) return std::nullopt;
    const auto words = clean.split(' ');
    if (words.size() > (KEY_CNT + wordBits - 1) / wordBits) return std::nullopt;
    QBitArray bits(static_cast<int>(words.size()) * wordBits);
    for (qsizetype i = 0; i < words.size(); ++i)
    {
        const auto& word = words[words.size() - 1 - i];
        if (word.isEmpty() || word.size() > wordBits / 4) return std::nullopt;
        for (char c : word)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                return std::nullopt;
        bool ok = false;
        const quint64 value = word.toULongLong(&ok, 16);
        if (!ok) return std::nullopt;
        for (int bit = 0; bit < wordBits; ++bit)
            if ((value & (quint64{1} << bit)) != 0)
                bits.setBit(static_cast<int>(i) * wordBits + bit);
    }
    return bits;
}

namespace
{
struct RuntimeDeviceInfo
{
    QString phys;
    bool usefulInput;
    bool exclusive;
};

std::optional<QByteArray> readInputAttribute(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
    const QByteArray data = file.readAll();
    if (file.error() != QFileDevice::NoError) return std::nullopt;
    return data;
}

std::optional<RuntimeDeviceInfo> runtimeDeviceInfo(const QString& path,
                                                   const QSet<HotkeyBinding>& hotkeys)
{
    if (::access(path.toLocal8Bit().constData(), R_OK) != 0) return std::nullopt;
    struct stat st = {};
    // Synthetic nodes, missing sysfs or unexpected metadata use libevdev.
    // sysfs input bitmaps print words for the reader's ABI, including compat32.
    const QByteArray encoded = path.toLocal8Bit();
    if (::lstat(encoded.constData(), &st) == 0 && (S_ISCHR(st.st_mode) || S_ISLNK(st.st_mode)))
    {
        const QString canonical = S_ISLNK(st.st_mode) ? QFileInfo(path).canonicalFilePath() : path;
        if (canonical.startsWith(QLatin1String("/dev/input/event")))
        {
            const QString root =
                QStringLiteral("/sys/class/input/%1/device/").arg(QFileInfo(canonical).fileName());
            const auto phys = readInputAttribute(root + QStringLiteral("phys"));
            const auto ev = readInputAttribute(root + QStringLiteral("capabilities/ev"));
            const auto key = readInputAttribute(root + QStringLiteral("capabilities/key"));
            const auto rel = readInputAttribute(root + QStringLiteral("capabilities/rel"));
            if (phys && ev && key && rel)
            {
                constexpr int wordBits = sizeof(unsigned long) * 8;
                const auto types = parseInputCapabilityBitmap(*ev, wordBits);
                const auto keys = parseInputCapabilityBitmap(*key, wordBits);
                const auto relative = parseInputCapabilityBitmap(*rel, wordBits);
                if (types && keys && relative)
                {
                    auto has = [](const QBitArray& bits, int code)
                    { return code >= 0 && code < bits.size() && bits.testBit(code); };
                    bool exclusive = false;
                    for (const HotkeyBinding& binding : hotkeys)
                    {
                        if (!binding.isAssigned()) continue;
                        const bool scroll = binding.type == HotkeyBindingType::Relative;
                        if (has(*types, scroll ? EV_REL : EV_KEY) &&
                            has(scroll ? *relative : *keys, binding.code))
                        {
                            exclusive = true;
                            break;
                        }
                    }
                    QByteArray physical = *phys;
                    if (physical.endsWith('\n')) physical.chop(1);
                    return RuntimeDeviceInfo{QString::fromLocal8Bit(physical),
                                             has(*types, EV_KEY) || has(*types, EV_REL), exclusive};
                }
            }
        }
    }
    EvdevDevice dev(path);
    if (!dev.isOpen()) return std::nullopt;
    const char* phys = dev.phys();
    return RuntimeDeviceInfo{phys ? QString::fromLocal8Bit(phys) : QString{},
                             dev.hasEventType(EV_KEY) || dev.hasEventType(EV_REL),
                             deviceHasConfiguredHotkey(dev.dev(), hotkeys)};
}
} // namespace

QList<std::pair<QString, bool>> findHotkeyDevices(const QString& primaryPath,
                                                  const QSet<HotkeyBinding>& hotkeys)
{
    QString physPrefix;
    if (const auto primary = runtimeDeviceInfo(primaryPath, hotkeys)) physPrefix = primary->phys;
    const qsizetype slash = physPrefix.lastIndexOf(QLatin1String("/input"));
    if (slash >= 0) physPrefix.truncate(slash);

    // Read static sysfs metadata without opening/closing every evdev node.
    // Useful siblings still contribute modifiers without an exclusive mirror.
    QList<std::pair<QString, bool>> result;
    for (const QString& path : listEvdevDevices())
    {
        const auto info = runtimeDeviceInfo(path, hotkeys);
        if (!info) continue;
        const bool sibling =
            path == primaryPath ||
            (!physPrefix.isEmpty() && !info->phys.isEmpty() && info->phys.startsWith(physPrefix));
        if (info->exclusive || (sibling && info->usefulInput))
            result.append({path, info->exclusive});
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
            if (!e->isOpen()) continue;
            if (events[i].events & (EPOLLERR | EPOLLHUP))
            {
                epoll_ctl(epfd, EPOLL_CTL_DEL, e->fd(), nullptr);
                e->close();
                continue;
            }
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
            if (rc < 0 && rc != -EAGAIN && rc != -EINTR)
            {
                epoll_ctl(epfd, EPOLL_CTL_DEL, e->fd(), nullptr);
                e->close();
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
    if (!dev || !dev->isGrabbed() || !isMirroredLedCode(code) || !dev->hasEventCode(EV_LED, code))
        return;

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

bool drainUinputOutput(EvdevDevice* dev)
{
    const int fd = dev ? dev->uinputFd() : -1;
    if (fd < 0) return false;

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
            return true;
        }
        if (n < 0)
            qWarning() << "[InputHandler] Cannot read uinput output event:" << strerror(errno);
        return false;
    }
}

void syncPhysicalLedsFromUinput(EvdevDevice* dev)
{
    if (!dev || !dev->uinput() || !dev->isWritable() || !dev->hasEventType(EV_LED)) return;

    const int fd = dev->uinputLedStateFd();
    if (fd < 0)
    {
        qWarning() << "[InputHandler] Cannot open uinput node for initial LED sync";
        return;
    }

    std::array<unsigned long, kLedStateWords> bits{};
    const int rc = ioctl(fd, EVIOCGLED(sizeof(bits)), bits.data());
    const int savedErrno = errno;

    if (rc < 0)
    {
        qWarning() << "[InputHandler] Cannot query initial LED state for" << dev->name() << ":"
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
    // EV_LED read from a physical node is output-state feedback, including
    // echoes of our own LED writes. Reinjecting it makes uinput emit another
    // output event and can keep the same LED bouncing through this loop.
    if (ev.type != EV_LED && dev->isGrabbed() && dev->uinput())
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

OsdLayoutAction resolveOsdLayoutAction(const HotkeyBinding& binding,
                                       const OsdLayoutHotkeyConfig& cfg)
{
    if (!binding.isAssigned()) return OsdLayoutAction::None;
    if (cfg.snapUp.isAssigned() && cfg.snapUp == binding) return OsdLayoutAction::SnapUp;
    if (cfg.snapDown.isAssigned() && cfg.snapDown == binding) return OsdLayoutAction::SnapDown;
    if (cfg.snapLeft.isAssigned() && cfg.snapLeft == binding) return OsdLayoutAction::SnapLeft;
    if (cfg.snapRight.isAssigned() && cfg.snapRight == binding) return OsdLayoutAction::SnapRight;
    if (cfg.scaleUp.isAssigned() && cfg.scaleUp == binding) return OsdLayoutAction::ScaleUp;
    if (cfg.scaleDown.isAssigned() && cfg.scaleDown == binding) return OsdLayoutAction::ScaleDown;
    return OsdLayoutAction::None;
}

OsdLayoutAction resolveOsdLayoutAction(const HotkeyBinding& binding, const input_event& ev,
                                       const OsdLayoutHotkeyConfig& cfg)
{
    auto matchField = [&](const HotkeyBinding& hk, OsdLayoutAction action) -> OsdLayoutAction
    {
        if (!hk.isAssigned()) return OsdLayoutAction::None;
        if (hk.type == HotkeyBindingType::Key)
            return hk == binding ? action : OsdLayoutAction::None;
        return matchesInputEvent(hk, ev) ? action : OsdLayoutAction::None;
    };

    if (OsdLayoutAction a = matchField(cfg.snapUp, OsdLayoutAction::SnapUp);
        a != OsdLayoutAction::None)
        return a;
    if (OsdLayoutAction a = matchField(cfg.snapDown, OsdLayoutAction::SnapDown);
        a != OsdLayoutAction::None)
        return a;
    if (OsdLayoutAction a = matchField(cfg.snapLeft, OsdLayoutAction::SnapLeft);
        a != OsdLayoutAction::None)
        return a;
    if (OsdLayoutAction a = matchField(cfg.snapRight, OsdLayoutAction::SnapRight);
        a != OsdLayoutAction::None)
        return a;
    if (OsdLayoutAction a = matchField(cfg.scaleUp, OsdLayoutAction::ScaleUp);
        a != OsdLayoutAction::None)
        return a;
    if (OsdLayoutAction a = matchField(cfg.scaleDown, OsdLayoutAction::ScaleDown);
        a != OsdLayoutAction::None)
        return a;
    return OsdLayoutAction::None;
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
    qRegisterMetaType<OsdLayoutAction>("OsdLayoutAction");
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

void InputHandler::setOsdLayoutInput(const OsdLayoutInputConfig& cfg)
{
    QMutexLocker lock(&m_profilesMutex);
    m_osdLayoutInput = cfg;
}

OsdLayoutInputConfig InputHandler::currentOsdLayoutInput() const
{
    QMutexLocker lock(&m_profilesMutex);
    return m_osdLayoutInput;
}

void InputHandler::setOsdLayoutKeysActive(bool active)
{
    m_osdLayoutKeysActive.store(active);
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
    QElapsedTimer startupTimer;
    startupTimer.start();
    constexpr qint64 kMirrorWarmupMs = 500;

    // Snapshot current profiles + media config for the lifetime of this run.
    // Caller (App) restarts the thread on profile/media changes, so this
    // snapshot is fresh for each restart.
    const QList<Profile> profiles = currentProfiles();
    const MediaHotkeyConfig mediaCfg = currentMediaHotkeys();
    const QList<AudioScene> scenes = currentScenes();
    const OsdLayoutInputConfig layoutInput = currentOsdLayoutInput();
    const bool layoutGrab =
        layoutInput.positionControlsEnabled && layoutInput.positionKeyboardEnabled;
    const OsdLayoutHotkeyConfig layoutCfg = layoutInput.hotkeys;

    // Sentinel profile id used as the second key of m_lastTriggerMs for media
    // bindings, so debounce buckets never collide with real profile ids.
    const QString kMediaProfile = QStringLiteral("__media__");
    // Sentinel prefix for scene debounce buckets (one bucket per scene id).
    const QString kSceneProfilePrefix = QStringLiteral("__scene__:");
    const QString kOsdLayoutProfile = QStringLiteral("__osd_layout__");

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
    if (layoutGrab)
    {
        if (layoutCfg.snapUp.isAssigned()) hotkeys.insert(layoutCfg.snapUp);
        if (layoutCfg.snapDown.isAssigned()) hotkeys.insert(layoutCfg.snapDown);
        if (layoutCfg.snapLeft.isAssigned()) hotkeys.insert(layoutCfg.snapLeft);
        if (layoutCfg.snapRight.isAssigned()) hotkeys.insert(layoutCfg.snapRight);
        if (layoutCfg.scaleUp.isAssigned()) hotkeys.insert(layoutCfg.scaleUp);
        if (layoutCfg.scaleDown.isAssigned()) hotkeys.insert(layoutCfg.scaleDown);
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

    auto dispatchOsdLayout = [&](const HotkeyBinding& binding, OsdLayoutAction action)
    {
        const QPair<HotkeyBinding, QString> key{binding, kOsdLayoutProfile};
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 last = m_lastTriggerMs.value(key, 0LL);
        if (now - last < 100) return;
        m_lastTriggerMs[key] = now;
        emit osdLayoutAction(action);
    };

    const auto candidates = findHotkeyDevices(m_devicePath, hotkeys);
    qDebug() << "[InputHandler] Device discovery took" << startupTimer.elapsed() << "ms";

    std::vector<std::unique_ptr<EvdevDevice>> devices;
    QMap<EvdevDevice*, qint64> mirrorReadyAfter;

    for (const auto& [path, exclusive] : candidates)
    {
        if (!m_running) break;
        // Open writable once for LED-capable keyboards instead of reopening
        // after libevdev initialization (evdev close can wait for kernel RCU).
        auto dev = std::make_unique<EvdevDevice>();
        const auto mode =
            exclusive ? EvdevDevice::OpenMode::ReadWrite : EvdevDevice::OpenMode::ReadOnly;
        if (!dev->open(path, mode) && (!exclusive || !dev->open(path)))
        {
            qDebug() << "[InputHandler] Cannot open" << path;
            continue;
        }

        if (exclusive)
        {
            if (!dev->createUinput())
            {
                qDebug() << "[InputHandler] Cannot create uinput for" << path
                         << "— leaving physical input untouched";
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
            mirrorReadyAfter.insert(dev.get(), startupTimer.elapsed() + kMirrorWarmupMs);
            qDebug() << "[InputHandler] Created uinput for" << path;
        }
        qDebug() << "[InputHandler] Opened" << path
                 << (dev->uinput() ? "[prepared uinput]" : "[passive]");
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
            if (epoll_ctl(epfd, EPOLL_CTL_ADD, source.fd, &evnt) < 0)
            {
                qWarning() << "[InputHandler] Cannot register input polling:" << strerror(errno);
                ::close(epfd);
                epfd = -1;
                break;
            }
        }
    }

    // Modifier state — observed on hotkey devices and passive siblings.
    QSet<int> heldModifiers;
    for (const auto& dev : devices)
        for (int code : {KEY_LEFTCTRL, KEY_RIGHTCTRL, KEY_LEFTSHIFT, KEY_RIGHTSHIFT})
            if (libevdev_get_event_value(dev->dev(), EV_KEY, code) != 0) heldModifiers.insert(code);
    // Mirrors created early can settle while later nodes are prepared.
    // Drain pre-grab input without replay, then activate each ready mirror.
    qint64 startupLedSyncUntilMs = QDateTime::currentMSecsSinceEpoch() + 2000;
    qint64 nextStartupLedSyncMs = QDateTime::currentMSecsSinceEpoch();

    auto syncStartupLedsIfDue = [&]()
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now > startupLedSyncUntilMs || now < nextStartupLedSyncMs) return;
        for (auto& dev : devices)
            if (dev->isGrabbed()) syncPhysicalLedsFromUinput(dev.get());
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
        for (int i = 0; i < n; ++i)
        {
            auto* source = static_cast<PollSource*>(events[i].data.ptr);
            if (!source || !source->dev) continue;
            if (events[i].events & (EPOLLERR | EPOLLHUP))
            {
                auto* dev = source->dev;
                qWarning()
                    << "[InputHandler] Input source disconnected or failed; releasing device";
                retirePollSources(epfd, pollSources, dev);
                dev->close();
                heldModifiers.clear();
                continue;
            }
            if (source->type == PollSource::Type::UinputOutput)
            {
                if (!drainUinputOutput(source->dev))
                {
                    auto* dev = source->dev;
                    retirePollSources(epfd, pollSources, dev);
                    dev->close();
                    heldModifiers.clear();
                }
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

                    // Passive siblings and warming mirrors must not dispatch
                    // events that the desktop is still receiving directly.
                    if (!e->isGrabbed()) continue;

                    const HotkeyBinding binding = HotkeyBinding::key(code);
                    if (hotkeys.contains(binding))
                    {
                        if (ev.value == 1 || ev.value == 2)
                        { // press or repeat
                            if (layoutGrab && m_osdLayoutKeysActive.load())
                            {
                                const OsdLayoutAction layoutAction =
                                    resolveOsdLayoutAction(binding, layoutCfg);
                                if (layoutAction != OsdLayoutAction::None)
                                {
                                    dispatchOsdLayout(binding, layoutAction);
                                    continue;
                                }
                            }
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
                            if (layoutGrab && m_osdLayoutKeysActive.load() &&
                                resolveOsdLayoutAction(binding, layoutCfg) != OsdLayoutAction::None)
                                continue;
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
                    if (!e->isGrabbed()) continue;
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
                                    resolveMediaHotkey(binding, mediaCfg) != MediaAction::None ||
                                    (layoutGrab && m_osdLayoutKeysActive.load() &&
                                     resolveOsdLayoutAction(binding, companion, layoutCfg) !=
                                         OsdLayoutAction::None))
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
                            if (layoutGrab && m_osdLayoutKeysActive.load())
                            {
                                const OsdLayoutAction layoutAction =
                                    resolveOsdLayoutAction(matchedBinding, ev, layoutCfg);
                                if (layoutAction != OsdLayoutAction::None)
                                {
                                    dispatchOsdLayout(matchedBinding, layoutAction);
                                    continue;
                                }
                            }
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
            if (rc < 0 && rc != -EAGAIN && rc != -EINTR)
            {
                qWarning() << "[InputHandler] Cannot read input device:" << strerror(-rc);
                retirePollSources(epfd, pollSources, e);
                e->close();
                heldModifiers.clear();
            }
        }

        if (!mirrorReadyAfter.isEmpty() && m_running)
        {
            for (auto& dev : devices)
            {
                const auto ready = mirrorReadyAfter.constFind(dev.get());
                if (ready == mirrorReadyAfter.cend() || startupTimer.elapsed() < ready.value())
                    continue;
                // Never switch a held key/button between physical and virtual
                // devices: its release must reach the device that sent the press.
                std::array<unsigned long, (KEY_CNT + kLedBitsPerWord - 1) / kLedBitsPerWord> keys{};
                if (ioctl(dev->fd(), EVIOCGKEY(sizeof(keys)), keys.data()) < 0) continue;
                if (std::any_of(keys.begin(), keys.end(),
                                [](unsigned long bits) { return bits != 0; }))
                    continue;
                // Retry after the next poll if events arrived after the drain.
                // This avoids replaying a pre-grab backlog as duplicate input.
                if (libevdev_has_event_pending(dev->dev()) != 0) continue;
                mirrorReadyAfter.remove(dev.get());
                if (!dev->grab())
                {
                    qWarning() << "[InputHandler] Cannot activate mirror for" << dev->name()
                               << "— leaving physical input untouched";
                    dev->destroyUinput();
                    continue;
                }
                qDebug() << "[InputHandler] Activated mirror for" << dev->name() << "after"
                         << startupTimer.elapsed() << "ms";
                // Each device can activate later (e.g. while a key is held).
                startupLedSyncUntilMs = QDateTime::currentMSecsSinceEpoch() + 2000;
                nextStartupLedSyncMs = QDateTime::currentMSecsSinceEpoch();
            }
        }
    }

    if (epfd >= 0) ::close(epfd);

    if (!devices.empty())
    {
        for (auto& dev : devices)
            if (dev->isGrabbed()) dev->preserveLedStateFromUinput();
        for (auto& dev : devices) dev->destroyUinput();
        for (auto& dev : devices) dev->ungrab();
        QThread::msleep(750);
        for (auto& dev : devices) dev->restorePreservedLedState();
    }

    if (!devices.empty()) qDebug() << "[InputHandler] Released" << devices.size() << "devices";
}
