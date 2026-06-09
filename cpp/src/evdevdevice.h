#pragma once

#include <QString>

struct libevdev;
struct libevdev_uinput;

class EvdevDevice
{
  public:
    enum class OpenMode
    {
        ReadOnly,
        ReadWrite,
    };

    EvdevDevice() = default;
    explicit EvdevDevice(const QString& path, OpenMode mode = OpenMode::ReadOnly);
    ~EvdevDevice();

    EvdevDevice(const EvdevDevice&) = delete;
    EvdevDevice& operator=(const EvdevDevice&) = delete;
    EvdevDevice(EvdevDevice&& other) noexcept;
    EvdevDevice& operator=(EvdevDevice&& other) noexcept;

    bool open(const QString& path, OpenMode mode = OpenMode::ReadOnly);
    bool isOpen() const;
    bool isWritable() const;
    void close();

    int fd() const;
    libevdev* dev() const;

    bool grab();
    bool isGrabbed() const;

    bool createUinput();
    libevdev_uinput* uinput() const;
    int uinputFd() const;

    const char* name() const;
    const char* phys() const;
    bool hasEventType(unsigned int type) const;
    bool hasEventCode(unsigned int type, unsigned int code) const;
    bool setLedValue(unsigned int code, bool enabled);

  private:
    int m_fd = -1;
    libevdev* m_dev = nullptr;
    libevdev_uinput* m_ui = nullptr;
    bool m_writable = false;
    bool m_grabbed = false;
};
