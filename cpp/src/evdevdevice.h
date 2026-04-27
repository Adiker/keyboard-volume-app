#pragma once

#include <QString>

struct libevdev;
struct libevdev_uinput;

class EvdevDevice
{
public:
    EvdevDevice() = default;
    explicit EvdevDevice(const QString &path);
    ~EvdevDevice();

    EvdevDevice(const EvdevDevice &) = delete;
    EvdevDevice &operator=(const EvdevDevice &) = delete;
    EvdevDevice(EvdevDevice &&other) noexcept;
    EvdevDevice &operator=(EvdevDevice &&other) noexcept;

    bool open(const QString &path);
    bool isOpen() const;
    void close();

    int fd() const;
    libevdev *dev() const;

    bool grab();
    bool isGrabbed() const;

    bool createUinput();
    libevdev_uinput *uinput() const;

    const char *name() const;
    const char *phys() const;
    bool hasEventType(unsigned int type) const;
    bool hasEventCode(unsigned int type, unsigned int code) const;

private:
    int m_fd = -1;
    libevdev *m_dev = nullptr;
    libevdev_uinput *m_ui = nullptr;
    bool m_grabbed = false;
};
