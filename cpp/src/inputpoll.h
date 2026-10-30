#pragma once

#include <sys/epoll.h>
#include <vector>

class EvdevDevice;

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

// Keep the source storage alive until the current epoll batch has been handled:
// that batch may contain both the evdev and uinput entries for this device.
inline void retirePollSources(int epfd, std::vector<PollSource>& sources, EvdevDevice* dev)
{
    for (auto& source : sources)
    {
        if (source.dev != dev) continue;
        epoll_ctl(epfd, EPOLL_CTL_DEL, source.fd, nullptr);
        source.dev = nullptr;
    }
}
