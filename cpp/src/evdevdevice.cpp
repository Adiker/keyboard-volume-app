#include "evdevdevice.h"

#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>

#include <fcntl.h>
#include <unistd.h>
#include <utility>

EvdevDevice::EvdevDevice(const QString &path)
{
    open(path);
}

EvdevDevice::~EvdevDevice()
{
    close();
}

EvdevDevice::EvdevDevice(EvdevDevice &&other) noexcept
    : m_fd(std::exchange(other.m_fd, -1))
    , m_dev(std::exchange(other.m_dev, nullptr))
    , m_ui(std::exchange(other.m_ui, nullptr))
    , m_grabbed(std::exchange(other.m_grabbed, false))
{}

EvdevDevice &EvdevDevice::operator=(EvdevDevice &&other) noexcept
{
    if (this != &other) {
        close();
        m_fd = std::exchange(other.m_fd, -1);
        m_dev = std::exchange(other.m_dev, nullptr);
        m_ui = std::exchange(other.m_ui, nullptr);
        m_grabbed = std::exchange(other.m_grabbed, false);
    }
    return *this;
}

bool EvdevDevice::open(const QString &path)
{
    close();
    m_fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
    if (m_fd < 0) return false;
    if (libevdev_new_from_fd(m_fd, &m_dev) < 0) {
        ::close(m_fd);
        m_fd = -1;
        return false;
    }
    return true;
}

bool EvdevDevice::isOpen() const
{
    return m_fd >= 0 && m_dev != nullptr;
}

void EvdevDevice::close()
{
    if (m_ui) {
        libevdev_uinput_destroy(m_ui);
        m_ui = nullptr;
    }
    if (m_grabbed && m_dev) {
        libevdev_grab(m_dev, LIBEVDEV_UNGRAB);
        m_grabbed = false;
    }
    if (m_dev) {
        libevdev_free(m_dev);
        m_dev = nullptr;
    }
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

int EvdevDevice::fd() const
{
    return m_fd;
}

libevdev *EvdevDevice::dev() const
{
    return m_dev;
}

bool EvdevDevice::grab()
{
    if (!m_dev) return false;
    int rc = libevdev_grab(m_dev, LIBEVDEV_GRAB);
    if (rc == 0) {
        m_grabbed = true;
        return true;
    }
    return false;
}

bool EvdevDevice::isGrabbed() const
{
    return m_grabbed;
}

bool EvdevDevice::createUinput()
{
    if (!m_dev) return false;
    if (m_ui) return true;
    int rc = libevdev_uinput_create_from_device(
        m_dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &m_ui);
    return rc == 0;
}

libevdev_uinput *EvdevDevice::uinput() const
{
    return m_ui;
}

const char *EvdevDevice::name() const
{
    return m_dev ? libevdev_get_name(m_dev) : nullptr;
}

const char *EvdevDevice::phys() const
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
