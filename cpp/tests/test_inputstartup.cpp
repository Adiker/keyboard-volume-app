// Real InputHandler event loop with simulated evdev/uinput endpoints.
// The runner hides physical devices; linker wrappers supply pipe-backed input
// and record forwarding without creating any desktop-visible uinput devices.
#include "inputhandler.h"
#include "evdevdevice.h"
#include <QCoreApplication>
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>
#include <gtest/gtest.h>
#include <atomic>
#include <algorithm>
#include <array>
#include <functional>
#include <chrono>
#include <cstdarg>
#include <cstring>
#include <iostream>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <unistd.h>

namespace
{
constexpr int kWordBits = sizeof(unsigned long) * 8;
constexpr int kKeyWords = (KEY_CNT + kWordBits - 1) / kWordBits;
struct Source
{
    int id;
    int wr;
    libevdev* dev = nullptr;
    bool grabbed = false;
    bool ledReader = false;
};
struct Mirror
{
    const libevdev* dev;
    QByteArray node;
    int rd = -1;
    int wr = -1;
};
std::mutex mx;
std::map<int, Source> sources;
std::map<libevdev*, int> devfds;
std::map<const libevdev*, Mirror*> mirrors;
std::vector<input_event> forwarded;
std::atomic<int> creates{0}, completedCreates{0}, grabs{0}, overlap{0}, opens{0};
std::atomic<int> secondCreateDelayMs{120};
std::atomic<bool> failCreate{false}, failPoll{false}, withLeds{false}, failReadWrite{false};
std::atomic<bool> echoPhysicalLeds{false};
std::atomic<int> ledOpens{0}, ledCloses{0}, ledQueries{0}, ledWrites{0};
std::array<unsigned long, 3> mirrorLeds{};
std::array<std::array<unsigned long, kKeyWords>, 3> held{};
std::array<std::atomic<int>, 3> processed{};

bool waitFor(const std::function<bool()>& check, int ms = 2500)
{
    for (int i = 0; i < ms / 5; ++i)
    {
        if (check()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return check();
}
void send(int id, unsigned short type, unsigned short code, int value)
{
    std::lock_guard lock(mx);
    if (type == EV_KEY)
    {
        if (value == 1) held[id][code / kWordBits] |= 1UL << (code % kWordBits);
        if (value == 0) held[id][code / kWordBits] &= ~(1UL << (code % kWordBits));
    }
    input_event ev{};
    ev.type = type;
    ev.code = code;
    ev.value = value;
    for (auto& [fd, s] : sources)
        if (s.id == id && s.dev)
        {
            ::write(s.wr, &ev, sizeof(ev));
            input_event syn{};
            syn.type = EV_SYN;
            ::write(s.wr, &syn, sizeof(syn));
        }
}
int active()
{
    std::lock_guard lock(mx);
    int n = 0;
    for (auto& [fd, s] : sources) n += s.grabbed;
    return n;
}
int forwardedCode(int code)
{
    std::lock_guard lock(mx);
    int n = 0;
    for (auto& e : forwarded)
        if (e.type == EV_KEY && e.code == code && e.value == 1) ++n;
    return n;
}
void sendLedOutput(int id, int value)
{
    std::lock_guard lock(mx);
    mirrorLeds[id] = value ? 1UL << LED_CAPSL : 0;
    for (const auto& [dev, mirror] : mirrors)
        if (sources.at(devfds.at(const_cast<libevdev*>(dev))).id == id)
        {
            input_event ev{};
            ev.type = EV_LED;
            ev.code = LED_CAPSL;
            ev.value = value;
            ::write(mirror->wr, &ev, sizeof(ev));
        }
}
void reset()
{
    std::lock_guard lock(mx);
    forwarded.clear();
    for (auto& bits : held) bits.fill(0);
    for (auto& n : processed) n = 0;
    creates = 0;
    completedCreates = 0;
    secondCreateDelayMs = 120;
    grabs = 0;
    overlap = 0;
    opens = 0;
    failCreate = false;
    failPoll = false;
    withLeds = false;
    failReadWrite = false;
    echoPhysicalLeds = false;
    mirrorLeds.fill(0);
    ledOpens = 0;
    ledCloses = 0;
    ledQueries = 0;
    ledWrites = 0;
}
struct Running
{
    InputHandler handler;
    ~Running()
    {
        handler.stop();
    }
};
} // namespace
extern "C"
{
    int __real_open(const char*, int, ...);
    int __real_close(int);
    void __real_libevdev_free(libevdev*);
    int __real_ioctl(int, unsigned long, ...);
    int __real_epoll_ctl(int, int, int, epoll_event*);
    int __wrap_open(const char* path, int flags, ...)
    {
        if (strncmp(path, "/dev/input/", 11) == 0)
        {
            int id = -1;
            if (strcmp(path, "/dev/input/event0") == 0) id = 0;
            if (strcmp(path, "/dev/input/event1") == 0) id = 1;
            if (strcmp(path, "/dev/input/event2") == 0) id = 2;
            const bool ledReader = strncmp(path, "/dev/input/mirror", 17) == 0;
            if (ledReader) id = path[17] - '0';
            if (id < 0 || id > 2)
            {
                errno = ENOENT;
                return -1;
            }
            if (!ledReader && failReadWrite && (flags & O_ACCMODE) == O_RDWR)
            {
                errno = EACCES;
                return -1;
            }
            int pair[2];
            if (pipe2(pair, O_NONBLOCK | O_CLOEXEC) < 0) return -1;
            std::lock_guard lock(mx);
            sources.emplace(pair[0], Source{id, pair[1], nullptr, false, ledReader});
            if (ledReader) ++ledOpens;
            ++opens;
            return pair[0];
        }
        if (strcmp(path, "/dev/uinput") == 0)
        {
            errno = EACCES;
            return -1;
        }
        // Production opens only existing paths without O_CREAT.
        return __real_open(path, flags);
    }
    int __wrap_close(int fd)
    {
        {
            std::lock_guard lock(mx);
            auto it = sources.find(fd);
            if (it != sources.end())
            {
                if (it->second.ledReader) ++ledCloses;
                __real_close(it->second.wr);
                sources.erase(it);
            }
        }
        return __real_close(fd);
    }
    int __wrap_libevdev_new_from_fd(int fd, libevdev** out)
    {
        std::lock_guard lock(mx);
        auto it = sources.find(fd);
        if (it == sources.end()) return -ENODEV;
        auto* dev = libevdev_new();
        *out = dev;
        it->second.dev = dev;
        devfds[dev] = fd;
        libevdev_set_name(dev, it->second.id == 1 ? "Passive sibling mouse" : "Test keyboard");
        libevdev_set_phys(dev, it->second.id == 2 ? "test-other/input0" : "test-primary/input0");
        if (it->second.id == 1)
        {
            libevdev_enable_event_code(dev, EV_REL, REL_X, nullptr);
            libevdev_enable_event_code(dev, EV_KEY, KEY_LEFTSHIFT, nullptr);
        }
        else
        {
            for (int c : {KEY_VOLUMEUP, KEY_VOLUMEDOWN, KEY_A, KEY_B, KEY_LEFTSHIFT})
                libevdev_enable_event_code(dev, EV_KEY, c, nullptr);
        }
        if (withLeds && it->second.id != 1)
            libevdev_enable_event_code(dev, EV_LED, LED_CAPSL, nullptr);
        for (int code : {KEY_LEFTCTRL, KEY_RIGHTCTRL, KEY_LEFTSHIFT, KEY_RIGHTSHIFT})
            if ((held[it->second.id][code / kWordBits] & (1UL << (code % kWordBits))) != 0)
                libevdev_set_event_value(dev, EV_KEY, code, 1);
        return 0;
    }
    void __wrap_libevdev_free(libevdev* dev)
    {
        {
            std::lock_guard lock(mx);
            auto it = devfds.find(dev);
            if (it != devfds.end())
            {
                auto fd = it->second;
                __real_close(sources.at(fd).wr);
                sources.erase(fd);
                devfds.erase(it);
            }
        }
        __real_libevdev_free(dev);
    }
    int __wrap_libevdev_grab(libevdev* dev, libevdev_grab_mode mode)
    {
        std::lock_guard lock(mx);
        auto& s = sources.at(devfds.at(dev));
        s.grabbed = mode == LIBEVDEV_GRAB;
        if (s.grabbed) ++grabs;
        return 0;
    }
    int __wrap_libevdev_next_event(libevdev* dev, unsigned int, input_event* ev)
    {
        std::lock_guard lock(mx);
        auto fd = devfds.at(dev);
        auto rc = ::read(fd, ev, sizeof(*ev));
        if (rc < 0) return -errno;
        if (rc != sizeof(*ev)) return -EAGAIN;
        ++processed[sources.at(fd).id];
        if (ev->type == EV_KEY) libevdev_set_event_value(dev, ev->type, ev->code, ev->value);
        return LIBEVDEV_READ_STATUS_SUCCESS;
    }
    int __wrap_libevdev_has_event_pending(libevdev* dev)
    {
        std::lock_guard lock(mx);
        int n = 0;
        __real_ioctl(devfds.at(dev), FIONREAD, &n);
        return n > 0;
    }
    int __wrap_libevdev_uinput_create_from_device(const libevdev* dev, int, libevdev_uinput** out)
    {
        const int number = ++creates;
        if (active() > 0) ++overlap;
        std::this_thread::sleep_for(
            std::chrono::milliseconds(number == 2 ? secondCreateDelayMs.load() : 120));
        ++completedCreates;
        if (failCreate)
        {
            *out = nullptr;
            return -EACCES;
        }
        std::lock_guard lock(mx);
        const int id = sources.at(devfds.at(const_cast<libevdev*>(dev))).id;
        auto* mirror = new Mirror{dev, QByteArray("/dev/input/mirror") + QByteArray::number(id)};
        if (withLeds)
        {
            int pair[2];
            if (pipe2(pair, O_NONBLOCK | O_CLOEXEC) < 0)
            {
                delete mirror;
                return -errno;
            }
            mirror->rd = pair[0];
            mirror->wr = pair[1];
        }
        mirrors.emplace(dev, mirror);
        *out = reinterpret_cast<libevdev_uinput*>(mirror);
        return 0;
    }
    void __wrap_libevdev_uinput_destroy(libevdev_uinput* ui)
    {
        std::lock_guard lock(mx);
        auto* mirror = reinterpret_cast<Mirror*>(ui);
        mirrors.erase(mirror->dev);
        if (mirror->rd >= 0) __real_close(mirror->rd);
        if (mirror->wr >= 0) __real_close(mirror->wr);
        delete mirror;
    }
    int __wrap_libevdev_uinput_get_fd(const libevdev_uinput* ui)
    {
        return reinterpret_cast<const Mirror*>(ui)->rd;
    }
    const char* __wrap_libevdev_uinput_get_devnode(const libevdev_uinput* ui)
    {
        return reinterpret_cast<const Mirror*>(ui)->node.constData();
    }
    int __wrap_libevdev_kernel_set_led_value(libevdev* dev, unsigned int code,
                                             libevdev_led_value value)
    {
        ++ledWrites;
        if (echoPhysicalLeds)
        {
            int id;
            {
                std::lock_guard lock(mx);
                id = sources.at(devfds.at(dev)).id;
            }
            send(id, EV_LED, static_cast<unsigned short>(code), value == LIBEVDEV_LED_ON);
        }
        return 0;
    }
    int __wrap_libevdev_uinput_write_event(const libevdev_uinput* ui, unsigned int type,
                                           unsigned int code, int value)
    {
        std::lock_guard lock(mx);
        input_event e{};
        e.type = type;
        e.code = code;
        e.value = value;
        forwarded.push_back(e);
        // uinput LED injection is reported back as output to its creator.
        const auto* mirror = reinterpret_cast<const Mirror*>(ui);
        if (type == EV_LED && mirror->wr >= 0) ::write(mirror->wr, &e, sizeof(e));
        return 0;
    }
    int __wrap_ioctl(int fd, unsigned long req, ...)
    {
        va_list ap;
        va_start(ap, req);
        auto* ptr = va_arg(ap, void*);
        va_end(ap);
        std::lock_guard lock(mx);
        auto it = sources.find(fd);
        if (it != sources.end() && _IOC_NR(req) == _IOC_NR(EVIOCGLED(1)))
        {
            ++ledQueries;
            std::memset(ptr, 0, _IOC_SIZE(req));
            std::memcpy(ptr, &mirrorLeds[it->second.id],
                        std::min(size_t(_IOC_SIZE(req)), sizeof(unsigned long)));
            return 0;
        }
        if (it != sources.end() && _IOC_NR(req) == _IOC_NR(EVIOCGKEY(1)))
        {
            std::memcpy(ptr, held[it->second.id].data(),
                        std::min(size_t(_IOC_SIZE(req)), sizeof(held[it->second.id])));
            return 0;
        }
        return __real_ioctl(fd, req, ptr);
    }
    int __wrap_epoll_ctl(int epfd, int op, int fd, epoll_event* ev)
    {
        if (failPoll)
        {
            errno = ENOSPC;
            return -1;
        }
        return __real_epoll_ctl(epfd, op, fd, ev);
    }
}

TEST(Startup, SingleCapabilityScanKeepsUnboundSiblingsPassive)
{
    reset();
    const auto candidates = findHotkeyDevices("/dev/input/event0", {KEY_VOLUMEUP});
    ASSERT_EQ(candidates.size(), 3);
    EXPECT_EQ(opens, 4); // primary identity plus one scan of the three nodes
    EXPECT_TRUE(candidates[0].second);
    EXPECT_FALSE(candidates[1].second);
    EXPECT_TRUE(candidates[2].second);
    EXPECT_EQ(creates, 0);
    EXPECT_EQ(grabs, 0);
}

TEST(Startup, PreparedBeforeGrabAndNoPreGrabReplay)
{
    reset();
    Running app;
    std::atomic<int> hits{0};
    QObject::connect(
        &app.handler, &InputHandler::volume_up, &app.handler, [&](const QString&) { ++hits; },
        Qt::DirectConnection);
    app.handler.startDevice("/dev/input/event0");
    ASSERT_TRUE(waitFor([] { return creates == 2; }));
    EXPECT_EQ(active(), 0);
    send(0, EV_KEY, KEY_A, 1);
    send(0, EV_KEY, KEY_A, 0);
    send(0, EV_KEY, KEY_VOLUMEUP, 1);
    send(0, EV_KEY, KEY_VOLUMEUP, 0);
    ASSERT_TRUE(waitFor([] { return active() == 2; }));
    EXPECT_EQ(creates, 2);
    EXPECT_EQ(overlap, 0);
    EXPECT_EQ(forwardedCode(KEY_A), 0);
    EXPECT_EQ(hits, 0);
    send(0, EV_KEY, KEY_B, 1);
    send(0, EV_KEY, KEY_B, 0);
    send(0, EV_KEY, KEY_VOLUMEUP, 1);
    send(0, EV_KEY, KEY_VOLUMEUP, 0);
    EXPECT_TRUE(waitFor([&] { return forwardedCode(KEY_B) == 1 && hits == 1; }));
    EXPECT_EQ(forwardedCode(KEY_VOLUMEUP), 0);
}
TEST(Startup, EarlyMirrorsSettleWhileLaterMirrorsArePrepared)
{
    reset();
    secondCreateDelayMs = 750;
    Running app;
    app.handler.startDevice("/dev/input/event0");
    ASSERT_TRUE(waitFor([] { return completedCreates == 2; }));
    // The first mirror has already settled; do not add a global 500 ms wait.
    EXPECT_TRUE(waitFor([] { return active() >= 1; }, 350));
    EXPECT_EQ(overlap, 0);
}

TEST(Startup, HeldKeyDelaysHandoffAndPassiveSiblingModifiersStillResolve)
{
    reset();
    Running app;
    Profile p;
    p.id = "shift";
    p.modifiers = {Modifier::Shift};
    p.hotkeys.volumeUp = KEY_VOLUMEUP;
    p.hotkeys.volumeDown = 0;
    p.hotkeys.mute = 0;
    app.handler.setProfiles({p});
    std::atomic<int> hits{0};
    QObject::connect(
        &app.handler, &InputHandler::volume_up, &app.handler,
        [&](const QString& id)
        {
            if (id == "shift") ++hits;
        },
        Qt::DirectConnection);
    app.handler.startDevice("/dev/input/event0");
    ASSERT_TRUE(waitFor([] { return creates == 2; }));
    send(0, EV_KEY, KEY_A, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    EXPECT_EQ(active(), 1);
    send(0, EV_KEY, KEY_A, 0);
    ASSERT_TRUE(waitFor([] { return active() == 2; }));
    send(1, EV_KEY, KEY_LEFTSHIFT, 1);
    ASSERT_TRUE(waitFor([] { return processed[1] >= 2; }));
    send(0, EV_KEY, KEY_VOLUMEUP, 1);
    send(0, EV_KEY, KEY_VOLUMEUP, 0);
    EXPECT_TRUE(waitFor([&] { return hits == 1; }));
    EXPECT_EQ(forwardedCode(KEY_LEFTSHIFT), 0);
    send(1, EV_KEY, KEY_LEFTSHIFT, 0);
}
TEST(Startup, ModifierAlreadyHeldAtStartupIsObserved)
{
    reset();
    held[1][KEY_LEFTSHIFT / kWordBits] |= 1UL << (KEY_LEFTSHIFT % kWordBits);
    Running app;
    Profile p;
    p.id = "shift";
    p.modifiers = {Modifier::Shift};
    p.hotkeys.volumeUp = KEY_VOLUMEUP;
    p.hotkeys.volumeDown = 0;
    p.hotkeys.mute = 0;
    app.handler.setProfiles({p});
    std::atomic<int> hits{0};
    QObject::connect(
        &app.handler, &InputHandler::volume_up, &app.handler,
        [&](const QString& id)
        {
            if (id == "shift") ++hits;
        },
        Qt::DirectConnection);
    app.handler.startDevice("/dev/input/event0");
    ASSERT_TRUE(waitFor([] { return active() == 2; }));
    send(0, EV_KEY, KEY_VOLUMEUP, 1);
    send(0, EV_KEY, KEY_VOLUMEUP, 0);
    EXPECT_TRUE(waitFor([&] { return hits == 1; }));
    send(1, EV_KEY, KEY_LEFTSHIFT, 0);
}

TEST(Startup, StartupLedQueriesReuseDescriptors)
{
    reset();
    withLeds = true;
    Running app;
    app.handler.startDevice("/dev/input/event0");
    ASSERT_TRUE(waitFor([] { return active() == 2; }));
    ASSERT_TRUE(waitFor([] { return ledQueries >= 6; }));
    EXPECT_EQ(ledOpens, 2);
    EXPECT_EQ(ledCloses, 0);
    EXPECT_GT(ledWrites, 0);
    app.handler.stop();
    EXPECT_EQ(ledCloses, 2);
}

TEST(Startup, DesktopLedOutputDoesNotReinjectPhysicalFeedback)
{
    reset();
    withLeds = true;
    echoPhysicalLeds = true;
    Running app;
    app.handler.startDevice("/dev/input/event0");
    ASSERT_TRUE(waitFor([] { return active() == 2; }));
    const int before = ledWrites;
    sendLedOutput(0, 1);
    ASSERT_TRUE(waitFor([&] { return ledWrites > before; }));
    send(0, EV_KEY, KEY_A, 1);
    send(0, EV_KEY, KEY_A, 0);
    ASSERT_TRUE(waitFor([] { return forwardedCode(KEY_A) == 1; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_LT(ledWrites.load() - before, 10);
    {
        std::lock_guard lock(mx);
        EXPECT_EQ(std::count_if(forwarded.begin(), forwarded.end(),
                                [](const input_event& event) { return event.type == EV_LED; }),
                  0);
        EXPECT_EQ(mirrorLeds[0], 1UL << LED_CAPSL);
    }
}

TEST(Startup, CancelledPreparationDoesNotRestoreUnactivatedLedDefaults)
{
    reset();
    withLeds = true;
    Running app;
    app.handler.startDevice("/dev/input/event0");
    ASSERT_TRUE(waitFor([] { return creates == 1; }));
    app.handler.stop();
    EXPECT_EQ(grabs, 0);
    EXPECT_EQ(ledWrites, 0);
    EXPECT_EQ(ledOpens, 0);
}

TEST(Startup, LedQueryDescriptorFollowsMoveOwnership)
{
    reset();
    withLeds = true;
    EvdevDevice source("/dev/input/event0", EvdevDevice::OpenMode::ReadWrite);
    ASSERT_TRUE(source.createUinput());
    const int fd = source.uinputLedStateFd();
    ASSERT_GE(fd, 0);
    EvdevDevice moved(std::move(source));
    EXPECT_EQ(source.uinputLedStateFd(), -1);
    EXPECT_EQ(moved.uinputLedStateFd(), fd);
    EvdevDevice assigned;
    assigned = std::move(moved);
    EXPECT_EQ(moved.uinputLedStateFd(), -1);
    EXPECT_EQ(assigned.uinputLedStateFd(), fd);
    EXPECT_EQ(ledOpens, 1);
    assigned.close();
    EXPECT_EQ(ledCloses, 1);
}

TEST(Startup, ReadOnlyFallbackStillHandlesHotkeysWithoutLedWrites)
{
    reset();
    withLeds = true;
    failReadWrite = true;
    Running app;
    std::atomic<int> hits{0};
    QObject::connect(
        &app.handler, &InputHandler::volume_up, &app.handler, [&](const QString&) { ++hits; },
        Qt::DirectConnection);
    app.handler.startDevice("/dev/input/event0");
    ASSERT_TRUE(waitFor([] { return active() == 2; }));
    send(0, EV_KEY, KEY_VOLUMEUP, 1);
    send(0, EV_KEY, KEY_VOLUMEUP, 0);
    EXPECT_TRUE(waitFor([&] { return hits == 1; }));
    app.handler.stop();
    EXPECT_EQ(ledOpens, 0);
    EXPECT_EQ(ledWrites, 0);
}

TEST(Startup, StopDuringPreparationDoesNotGrab)
{
    reset();
    Running app;
    app.handler.startDevice("/dev/input/event0");
    ASSERT_TRUE(waitFor([] { return creates == 1; }));
    app.handler.stop();
    EXPECT_FALSE(app.handler.isRunning());
    EXPECT_EQ(grabs, 0);
    EXPECT_EQ(active(), 0);
}
TEST(Startup, MirrorFailureLeavesPhysicalInputUntouched)
{
    reset();
    failCreate = true;
    Running app;
    app.handler.startDevice("/dev/input/event0");
    ASSERT_TRUE(waitFor([] { return creates == 2; }));
    app.handler.stop();
    EXPECT_EQ(grabs, 0);
}
TEST(Startup, PollFailureLeavesPhysicalInputUntouched)
{
    reset();
    failPoll = true;
    Running app;
    app.handler.startDevice("/dev/input/event0");
    ASSERT_TRUE(waitFor([] { return creates == 2; }));
    app.handler.stop();
    EXPECT_EQ(grabs, 0);
}
int main(int argc, char** argv)
{
    for (const char* path : {"/dev/input/event0", "/dev/input/event1", "/dev/input/event2"})
    {
        struct stat st{};
        if (stat(path, &st) != 0 || !S_ISFIFO(st.st_mode))
        {
            std::cerr << "Run through run_inputstartup_test.sh in a private input namespace\n";
            return 2;
        }
    }
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
