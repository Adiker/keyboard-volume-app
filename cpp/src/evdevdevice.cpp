#include "evdevdevice.h"

#include <QDebug>

#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>

#include <linux/input.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <array>
#include <cerrno>
#include <cstring>
#include <utility>

namespace
{

constexpr std::array<unsigned int, 3> kPreservedLedCodes{LED_NUML, LED_CAPSL, LED_SCROLLL};
constexpr int kLedBitsPerWord = static_cast<int>(sizeof(unsigned long) * 8);
constexpr int kLedStateWords = (LED_CNT + kLedBitsPerWord - 1) / kLedBitsPerWord;

struct LedSnapshot
{
    bool valid = false;
    unsigned int mask = 0;
};

bool ledBitIsSet(const std::array<unsigned long, kLedStateWords>& bits, unsigned int code)
{
    return (bits[code / kLedBitsPerWord] & (1UL << (code % kLedBitsPerWord))) != 0;
}

LedSnapshot readUinputLedSnapshot(libevdev* dev, libevdev_uinput* ui)
{
    LedSnapshot snapshot;
    if (!dev || !ui || !libevdev_has_event_type(dev, EV_LED)) return snapshot;

    const char* devnode = libevdev_uinput_get_devnode(ui);
    if (!devnode) return snapshot;

    const int fd = ::open(devnode, O_RDONLY | O_NONBLOCK);
    if (fd < 0) return snapshot;

    std::array<unsigned long, kLedStateWords> bits{};
    const int rc = ioctl(fd, EVIOCGLED(sizeof(bits)), bits.data());
    ::close(fd);

    if (rc < 0) return snapshot;

    snapshot.valid = true;
    for (size_t i = 0; i < kPreservedLedCodes.size(); ++i)
    {
        const unsigned int code = kPreservedLedCodes[i];
        if (libevdev_has_event_code(dev, EV_LED, code) && ledBitIsSet(bits, code))
            snapshot.mask |= 1U << i;
    }
    return snapshot;
}

void restorePhysicalLeds(libevdev* dev, const LedSnapshot& snapshot)
{
    if (!dev || !snapshot.valid) return;

    for (size_t i = 0; i < kPreservedLedCodes.size(); ++i)
    {
        const unsigned int code = kPreservedLedCodes[i];
        if (!libevdev_has_event_code(dev, EV_LED, code)) continue;

        const bool enabled = (snapshot.mask & (1U << i)) != 0;
        const auto value = enabled ? LIBEVDEV_LED_ON : LIBEVDEV_LED_OFF;
        if (libevdev_kernel_set_led_value(dev, code, value) != 0)
        {
            qWarning() << "[EvdevDevice] Cannot restore keyboard LED" << code
                       << (enabled ? "on" : "off") << "during device close";
        }
    }
}

} // namespace

EvdevDevice::EvdevDevice(const QString& path, OpenMode mode)
{
    open(path, mode);
}

EvdevDevice::~EvdevDevice()
{
    close();
}

EvdevDevice::EvdevDevice(EvdevDevice&& other) noexcept
    : m_fd(std::exchange(other.m_fd, -1)), m_dev(std::exchange(other.m_dev, nullptr)),
      m_ui(std::exchange(other.m_ui, nullptr)), m_writable(std::exchange(other.m_writable, false)),
      m_grabbed(std::exchange(other.m_grabbed, false)),
      m_hasPreservedLedState(std::exchange(other.m_hasPreservedLedState, false)),
      m_preservedLedMask(std::exchange(other.m_preservedLedMask, 0U))
{
}

EvdevDevice& EvdevDevice::operator=(EvdevDevice&& other) noexcept
{
    if (this != &other)
    {
        close();
        m_fd = std::exchange(other.m_fd, -1);
        m_dev = std::exchange(other.m_dev, nullptr);
        m_ui = std::exchange(other.m_ui, nullptr);
        m_writable = std::exchange(other.m_writable, false);
        m_grabbed = std::exchange(other.m_grabbed, false);
        m_hasPreservedLedState = std::exchange(other.m_hasPreservedLedState, false);
        m_preservedLedMask = std::exchange(other.m_preservedLedMask, 0U);
    }
    return *this;
}

bool EvdevDevice::open(const QString& path, OpenMode mode)
{
    close();
    const int accessMode = mode == OpenMode::ReadWrite ? O_RDWR : O_RDONLY;
    m_fd = ::open(path.toLocal8Bit().constData(), accessMode | O_NONBLOCK);
    if (m_fd < 0) return false;
    if (libevdev_new_from_fd(m_fd, &m_dev) < 0)
    {
        ::close(m_fd);
        m_fd = -1;
        return false;
    }
    m_writable = mode == OpenMode::ReadWrite;
    return true;
}

bool EvdevDevice::isOpen() const
{
    return m_fd >= 0 && m_dev != nullptr;
}

bool EvdevDevice::isWritable() const
{
    return m_writable;
}

void EvdevDevice::close()
{
    preserveLedStateFromUinput();
    destroyUinput();
    ungrab();
    restorePreservedLedState();
    if (m_dev)
    {
        libevdev_free(m_dev);
        m_dev = nullptr;
    }
    if (m_fd >= 0)
    {
        ::close(m_fd);
        m_fd = -1;
    }
    m_writable = false;
    m_hasPreservedLedState = false;
    m_preservedLedMask = 0;
}

int EvdevDevice::fd() const
{
    return m_fd;
}

libevdev* EvdevDevice::dev() const
{
    return m_dev;
}

bool EvdevDevice::grab()
{
    if (!m_dev) return false;
    int rc = libevdev_grab(m_dev, LIBEVDEV_GRAB);
    if (rc == 0)
    {
        m_grabbed = true;
        return true;
    }
    return false;
}

void EvdevDevice::ungrab()
{
    if (m_grabbed && m_dev)
    {
        libevdev_grab(m_dev, LIBEVDEV_UNGRAB);
        m_grabbed = false;
    }
}

bool EvdevDevice::isGrabbed() const
{
    return m_grabbed;
}

bool EvdevDevice::createUinput()
{
    if (!m_dev) return false;
    if (m_ui) return true;
    int rc = libevdev_uinput_create_from_device(m_dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &m_ui);
    return rc == 0;
}

void EvdevDevice::destroyUinput()
{
    if (m_ui)
    {
        libevdev_uinput_destroy(m_ui);
        m_ui = nullptr;
    }
}

libevdev_uinput* EvdevDevice::uinput() const
{
    return m_ui;
}

int EvdevDevice::uinputFd() const
{
    return m_ui ? libevdev_uinput_get_fd(m_ui) : -1;
}

const char* EvdevDevice::name() const
{
    return m_dev ? libevdev_get_name(m_dev) : nullptr;
}

const char* EvdevDevice::phys() const
{
    return m_dev ? libevdev_get_phys(m_dev) : nullptr;
}

bool EvdevDevice::hasEventType(unsigned int type) const
{
    return m_dev ? libevdev_has_event_type(m_dev, type) : false;
}

bool EvdevDevice::hasEventCode(unsigned int type, unsigned int code) const
{
    return m_dev ? libevdev_has_event_code(m_dev, type, code) : false;
}

bool EvdevDevice::setLedValue(unsigned int code, bool enabled)
{
    if (!m_dev || !m_writable || !hasEventCode(EV_LED, code)) return false;
    const auto value = enabled ? LIBEVDEV_LED_ON : LIBEVDEV_LED_OFF;
    return libevdev_kernel_set_led_value(m_dev, code, value) == 0;
}

void EvdevDevice::preserveLedStateFromUinput()
{
    if (!m_writable)
    {
        m_hasPreservedLedState = false;
        m_preservedLedMask = 0;
        return;
    }

    const LedSnapshot snapshot = readUinputLedSnapshot(m_dev, m_ui);
    m_hasPreservedLedState = snapshot.valid;
    m_preservedLedMask = snapshot.mask;
}

void EvdevDevice::restorePreservedLedState()
{
    if (!m_hasPreservedLedState) return;

    LedSnapshot snapshot;
    snapshot.valid = true;
    snapshot.mask = m_preservedLedMask;
    restorePhysicalLeds(m_dev, snapshot);

    m_hasPreservedLedState = false;
    m_preservedLedMask = 0;
}
