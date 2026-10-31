#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QSet>
#include <QList>

#include <linux/input.h>
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

#include "evdevdevice.h"
#include "inputpoll.h"

#include "inputhandler.h"
#include <libevdev/libevdev.h>
#include <memory>
#include "config.h"

// ─── Construction / API ───────────────────────────────────────────────────────
TEST(InputHandler, DefaultProfileSeed)
{
    InputHandler handler;
    QList<Profile> profs = handler.currentProfiles();
    ASSERT_EQ(profs.size(), 1);
    EXPECT_EQ(profs[0].id, QStringLiteral("default"));
    EXPECT_EQ(profs[0].hotkeys.volumeUp, 115);
    EXPECT_EQ(profs[0].hotkeys.volumeDown, 114);
    EXPECT_EQ(profs[0].hotkeys.mute, 113);
}

TEST(InputHandler, SetProfilesRoundTrip)
{
    InputHandler handler;

    Profile a;
    a.id = QStringLiteral("a");
    a.name = QStringLiteral("A");
    a.apps = {QStringLiteral("spotify")};
    a.hotkeys.volumeUp = 200;
    a.hotkeys.volumeDown = 201;
    a.hotkeys.mute = 202;

    Profile b;
    b.id = QStringLiteral("b");
    b.name = QStringLiteral("B");
    b.apps = {QStringLiteral("firefox")};
    b.modifiers.insert(Modifier::Ctrl);
    b.hotkeys.volumeUp = 200;
    b.hotkeys.volumeDown = 201;
    b.hotkeys.mute = 202;

    handler.setProfiles({a, b});

    QList<Profile> got = handler.currentProfiles();
    ASSERT_EQ(got.size(), 2);
    EXPECT_EQ(got[0], a);
    EXPECT_EQ(got[1], b);
}

TEST(InputHandler, DevicePathInitiallyEmpty)
{
    InputHandler handler;
    EXPECT_TRUE(handler.devicePath().isEmpty());
}

TEST(InputHandler, SignalsConnectable)
{
    InputHandler handler;
    QSignalSpy spyUp(&handler, &InputHandler::volume_up);
    QSignalSpy spyDown(&handler, &InputHandler::volume_down);
    QSignalSpy spyMute(&handler, &InputHandler::volume_mute);
    QSignalSpy spyDuck(&handler, &InputHandler::ducking_toggle);
    QSignalSpy spyPlayPause(&handler, &InputHandler::media_play_pause);
    QSignalSpy spyNext(&handler, &InputHandler::media_next);
    QSignalSpy spyPrev(&handler, &InputHandler::media_previous);
    QSignalSpy spyStop(&handler, &InputHandler::media_stop);

    EXPECT_TRUE(spyUp.isValid());
    EXPECT_TRUE(spyDown.isValid());
    EXPECT_TRUE(spyMute.isValid());
    EXPECT_TRUE(spyDuck.isValid());
    EXPECT_TRUE(spyPlayPause.isValid());
    EXPECT_TRUE(spyNext.isValid());
    EXPECT_TRUE(spyPrev.isValid());
    EXPECT_TRUE(spyStop.isValid());
}

TEST(InputHandler, MediaHotkeysRoundTrip)
{
    InputHandler handler;
    EXPECT_FALSE(handler.currentMediaHotkeys().playPause.isAssigned());

    MediaHotkeyConfig cfg;
    cfg.playPause = HotkeyBinding::key(KEY_PLAYPAUSE);
    cfg.next = HotkeyBinding::key(KEY_NEXTSONG);
    cfg.previous = HotkeyBinding::key(KEY_PREVIOUSSONG);
    cfg.stop = HotkeyBinding::key(KEY_STOPCD);
    handler.setMediaHotkeys(cfg);

    const auto got = handler.currentMediaHotkeys();
    EXPECT_EQ(got.playPause, HotkeyBinding::key(KEY_PLAYPAUSE));
    EXPECT_EQ(got.next, HotkeyBinding::key(KEY_NEXTSONG));
    EXPECT_EQ(got.previous, HotkeyBinding::key(KEY_PREVIOUSSONG));
    EXPECT_EQ(got.stop, HotkeyBinding::key(KEY_STOPCD));
}

TEST(InputHandler, ListEvdevDevices)
{
    QList<QString> devices = listEvdevDevices();
    for (const QString& d : devices)
    {
        EXPECT_TRUE(d.startsWith("/dev/input/event"))
            << d.toStdString() << " does not start with /dev/input/event";
    }
}

TEST(InputHandler, FindWithNonExistentPath)
{
    findSiblingDevices("/dev/input/event99999");
    findCaptureDevices("/dev/input/event99999");
    QSet<HotkeyBinding> hotkeys{115, 114, 113};
    findHotkeyDevices("/dev/input/event99999", hotkeys);
    SUCCEED();
}

// ─── normalizeHeldModifiers / isTrackedModifierCode ──────────────────────────
TEST(Modifier, IsTrackedModifierCode)
{
    EXPECT_TRUE(isTrackedModifierCode(KEY_LEFTCTRL));
    EXPECT_TRUE(isTrackedModifierCode(KEY_RIGHTCTRL));
    EXPECT_TRUE(isTrackedModifierCode(KEY_LEFTSHIFT));
    EXPECT_TRUE(isTrackedModifierCode(KEY_RIGHTSHIFT));
    EXPECT_FALSE(isTrackedModifierCode(KEY_LEFTALT));
    EXPECT_FALSE(isTrackedModifierCode(KEY_VOLUMEUP));
}

TEST(Modifier, LeftRightCollapseToCanonical)
{
    QSet<int> raw{KEY_LEFTCTRL, KEY_RIGHTCTRL};
    QSet<Modifier> norm = normalizeHeldModifiers(raw);
    EXPECT_EQ(norm.size(), 1);
    EXPECT_TRUE(norm.contains(Modifier::Ctrl));

    QSet<int> raw2{KEY_RIGHTSHIFT, KEY_LEFTCTRL};
    QSet<Modifier> norm2 = normalizeHeldModifiers(raw2);
    EXPECT_EQ(norm2.size(), 2);
    EXPECT_TRUE(norm2.contains(Modifier::Ctrl));
    EXPECT_TRUE(norm2.contains(Modifier::Shift));
}

// ─── resolveProfile ──────────────────────────────────────────────────────────
namespace
{
Profile mkProfile(const QString& id, int up, int down, int mute,
                  std::initializer_list<Modifier> mods = {})
{
    Profile p;
    p.id = id;
    p.name = id;
    p.apps = {id};
    p.hotkeys.volumeUp = up;
    p.hotkeys.volumeDown = down;
    p.hotkeys.mute = mute;
    for (Modifier m : mods) p.modifiers.insert(m);
    return p;
}
} // namespace

TEST(ResolveProfile, BareKeyPrefersBareProfile)
{
    QList<Profile> profs{
        mkProfile("default", 115, 114, 113, {}),
        mkProfile("ctrl", 115, 114, 113, {Modifier::Ctrl}),
    };
    EXPECT_EQ(resolveProfile(115, {}, profs), QStringLiteral("default"));
}

TEST(ResolveProfile, CtrlHeldPrefersCtrlProfile)
{
    QList<Profile> profs{
        mkProfile("default", 115, 114, 113, {}),
        mkProfile("ctrl", 115, 114, 113, {Modifier::Ctrl}),
    };
    EXPECT_EQ(resolveProfile(115, {Modifier::Ctrl}, profs), QStringLiteral("ctrl"));
}

TEST(ResolveProfile, MoreSpecificWinsOnSubsetTie)
{
    QList<Profile> profs{
        mkProfile("ctrl", 115, 114, 113, {Modifier::Ctrl}),
        mkProfile("ctrl-shift", 115, 114, 113, {Modifier::Ctrl, Modifier::Shift}),
    };
    QSet<Modifier> held{Modifier::Ctrl, Modifier::Shift};
    EXPECT_EQ(resolveProfile(115, held, profs), QStringLiteral("ctrl-shift"));
}

TEST(ResolveProfile, ShiftRequiredButNotHeld_NoMatch)
{
    QList<Profile> profs{
        mkProfile("shift", 87, 86, 85, {Modifier::Shift}),
    };
    EXPECT_TRUE(resolveProfile(87, {}, profs).isEmpty());
    EXPECT_TRUE(resolveProfile(87, {Modifier::Ctrl}, profs).isEmpty());
    EXPECT_EQ(resolveProfile(87, {Modifier::Shift}, profs), QStringLiteral("shift"));
}

TEST(ResolveProfile, FirstWinsOnTie)
{
    QList<Profile> profs{
        mkProfile("first", 115, 114, 113, {}),
        mkProfile("second", 115, 114, 113, {}),
    };
    EXPECT_EQ(resolveProfile(115, {}, profs), QStringLiteral("first"));
}

TEST(ResolveProfile, NoProfileReturnsEmpty)
{
    QList<Profile> profs{
        mkProfile("default", 115, 114, 113, {}),
    };
    // Code not bound to anything
    EXPECT_TRUE(resolveProfile(99, {}, profs).isEmpty());
}

TEST(ResolveProfile, MuteCodeMatchesProfile)
{
    QList<Profile> profs{
        mkProfile("default", 115, 114, 113, {}),
    };
    EXPECT_EQ(resolveProfile(113, {}, profs), QStringLiteral("default"));
}

TEST(ResolveProfileHotkey, DuckingHotkeyMatchesWhenEnabled)
{
    Profile p = mkProfile("default", 115, 114, 113, {});
    p.ducking.enabled = true;
    p.ducking.hotkey = 88;
    p.ducking.volume = 20;

    ProfileHotkeyMatch match = resolveProfileHotkey(88, {}, {p});
    EXPECT_EQ(match.profileId, QStringLiteral("default"));
    EXPECT_EQ(match.action, ProfileHotkeyAction::DuckingToggle);
}

TEST(ResolveProfileHotkey, DuckingHotkeyIgnoredWhenDisabledOrUnset)
{
    Profile disabled = mkProfile("disabled", 115, 114, 113, {});
    disabled.ducking.enabled = false;
    disabled.ducking.hotkey = 88;

    Profile unset = mkProfile("unset", 215, 214, 213, {});
    unset.ducking.enabled = true;
    unset.ducking.hotkey = 0;

    EXPECT_TRUE(resolveProfileHotkey(88, {}, {disabled}).profileId.isEmpty());
    EXPECT_TRUE(resolveProfileHotkey(0, {}, {unset}).profileId.isEmpty());
}

TEST(ResolveProfileHotkey, DuckingUsesProfileModifiers)
{
    Profile p = mkProfile("ctrl", 115, 114, 113, {Modifier::Ctrl});
    p.ducking.enabled = true;
    p.ducking.hotkey = 88;

    EXPECT_TRUE(resolveProfileHotkey(88, {}, {p}).profileId.isEmpty());

    ProfileHotkeyMatch match = resolveProfileHotkey(88, {Modifier::Ctrl}, {p});
    EXPECT_EQ(match.profileId, QStringLiteral("ctrl"));
    EXPECT_EQ(match.action, ProfileHotkeyAction::DuckingToggle);
}

TEST(ResolveProfileHotkey, RelativeWheelMatchesDirection)
{
    Profile p = mkProfile("default", 115, 114, 113, {});
    p.hotkeys.volumeUp = HotkeyBinding::relative(REL_WHEEL, 1);
    p.hotkeys.volumeDown = HotkeyBinding::relative(REL_WHEEL, -1);

    ProfileHotkeyMatch up = resolveProfileHotkey(HotkeyBinding::relative(REL_WHEEL, 1), {}, {p});
    EXPECT_EQ(up.profileId, QStringLiteral("default"));
    EXPECT_EQ(up.action, ProfileHotkeyAction::VolumeUp);

    ProfileHotkeyMatch down = resolveProfileHotkey(HotkeyBinding::relative(REL_WHEEL, -1), {}, {p});
    EXPECT_EQ(down.profileId, QStringLiteral("default"));
    EXPECT_EQ(down.action, ProfileHotkeyAction::VolumeDown);
}

TEST(ResolveProfileHotkey, RelativeWheelDoesNotMatchOppositeDirection)
{
    Profile p = mkProfile("default", 115, 114, 113, {});
    p.hotkeys.volumeUp = HotkeyBinding::relative(REL_WHEEL, 1);

    EXPECT_TRUE(
        resolveProfileHotkey(HotkeyBinding::relative(REL_WHEEL, -1), {}, {p}).profileId.isEmpty());
}

TEST(InputEventMatching, RelativeWheelComparesSign)
{
    input_event ev{};
    ev.type = EV_REL;
    ev.code = REL_WHEEL;
    ev.value = 2;

    EXPECT_TRUE(matchesInputEvent(HotkeyBinding::relative(REL_WHEEL, 1), ev));
    EXPECT_FALSE(matchesInputEvent(HotkeyBinding::relative(REL_WHEEL, -1), ev));

    ev.value = -1;
    EXPECT_FALSE(matchesInputEvent(HotkeyBinding::relative(REL_WHEEL, 1), ev));
    EXPECT_TRUE(matchesInputEvent(HotkeyBinding::relative(REL_WHEEL, -1), ev));
}

TEST(InputEventMatching, HiResWheelCompanionSuppression)
{
    // Verify the logic used to suppress REL_WHEEL_HI_RES when a REL_WHEEL
    // binding is configured: build a synthetic companion event with the standard
    // code and confirm matchesInputEvent sees the binding.
    constexpr unsigned short REL_WHEEL_HI_RES_CODE = 0x0b;
    constexpr unsigned short REL_HWHEEL_HI_RES_CODE = 0x0c;

    input_event hiRes{};
    hiRes.type = EV_REL;
    hiRes.code = REL_WHEEL_HI_RES_CODE;
    hiRes.value = 120; // high-res ticks per notch

    // The hi-res event itself must NOT match a REL_WHEEL binding.
    HotkeyBinding wheelUp = HotkeyBinding::relative(REL_WHEEL, 1);
    EXPECT_FALSE(matchesInputEvent(wheelUp, hiRes));

    // A companion with the standard code MUST match — this is what the suppress
    // check in InputHandler::run() uses to decide whether to swallow the hi-res.
    input_event companion = hiRes;
    companion.code = REL_WHEEL;
    EXPECT_TRUE(matchesInputEvent(wheelUp, companion));

    // Same for HWHEEL.
    input_event hiResH{};
    hiResH.type = EV_REL;
    hiResH.code = REL_HWHEEL_HI_RES_CODE;
    hiResH.value = -120;

    HotkeyBinding hWheelDown = HotkeyBinding::relative(REL_HWHEEL, -1);
    EXPECT_FALSE(matchesInputEvent(hWheelDown, hiResH));

    input_event companionH = hiResH;
    companionH.code = REL_HWHEEL;
    EXPECT_TRUE(matchesInputEvent(hWheelDown, companionH));
}

TEST(ResolveProfileHotkey, ShowVolumeActionMatches)
{
    Profile p = mkProfile("sp", 115, 114, 113, {});
    p.hotkeys.show = 99;

    ProfileHotkeyMatch m = resolveProfileHotkey(99, {}, {p});
    EXPECT_EQ(m.profileId, QStringLiteral("sp"));
    EXPECT_EQ(m.action, ProfileHotkeyAction::ShowVolume);
}

TEST(ResolveProfileHotkey, ShowVolumeNotFiredWhenZero)
{
    // show = 0 (unassigned) must never match — code 0 is a null/synthetic evdev event.
    Profile p = mkProfile("sp", 115, 114, 113, {});
    p.hotkeys.show = {};

    ProfileHotkeyMatch m = resolveProfileHotkey(0, {}, {p});
    EXPECT_EQ(m.action, ProfileHotkeyAction::None);
}

TEST(ResolveProfileHotkey, ShowVolumeUsesProfileModifiers)
{
    Profile p = mkProfile("ctrl", 115, 114, 113, {Modifier::Ctrl});
    p.hotkeys.show = 99;

    EXPECT_TRUE(resolveProfileHotkey(99, {}, {p}).profileId.isEmpty());

    ProfileHotkeyMatch match = resolveProfileHotkey(99, {Modifier::Ctrl}, {p});
    EXPECT_EQ(match.profileId, QStringLiteral("ctrl"));
    EXPECT_EQ(match.action, ProfileHotkeyAction::ShowVolume);
}

TEST(ResolveProfileHotkey, ShowVolumeRelativeWheelMatchesDirection)
{
    Profile p = mkProfile("wheel", 115, 114, 113, {});
    p.hotkeys.show = HotkeyBinding::relative(REL_WHEEL, 1);

    ProfileHotkeyMatch up = resolveProfileHotkey(HotkeyBinding::relative(REL_WHEEL, 1), {}, {p});
    EXPECT_EQ(up.profileId, QStringLiteral("wheel"));
    EXPECT_EQ(up.action, ProfileHotkeyAction::ShowVolume);

    EXPECT_TRUE(
        resolveProfileHotkey(HotkeyBinding::relative(REL_WHEEL, -1), {}, {p}).profileId.isEmpty());
}

// ─── Media hotkey resolution ──────────────────────────────────────────────────

TEST(ResolveMediaHotkey, UnassignedConfigReturnsNone)
{
    MediaHotkeyConfig cfg;
    EXPECT_EQ(resolveMediaHotkey(KEY_PLAYPAUSE, cfg), MediaAction::None);
    EXPECT_EQ(resolveMediaHotkey(KEY_NEXTSONG, cfg), MediaAction::None);
}

TEST(ResolveMediaHotkey, KeyMatchesAction)
{
    MediaHotkeyConfig cfg;
    cfg.playPause = HotkeyBinding::key(KEY_PLAYPAUSE);
    cfg.next = HotkeyBinding::key(KEY_NEXTSONG);
    cfg.previous = HotkeyBinding::key(KEY_PREVIOUSSONG);
    cfg.stop = HotkeyBinding::key(KEY_STOPCD);

    EXPECT_EQ(resolveMediaHotkey(KEY_PLAYPAUSE, cfg), MediaAction::PlayPause);
    EXPECT_EQ(resolveMediaHotkey(KEY_NEXTSONG, cfg), MediaAction::Next);
    EXPECT_EQ(resolveMediaHotkey(KEY_PREVIOUSSONG, cfg), MediaAction::Previous);
    EXPECT_EQ(resolveMediaHotkey(KEY_STOPCD, cfg), MediaAction::Stop);
}

TEST(ResolveMediaHotkey, UnknownCodeReturnsNone)
{
    MediaHotkeyConfig cfg;
    cfg.playPause = HotkeyBinding::key(KEY_PLAYPAUSE);
    EXPECT_EQ(resolveMediaHotkey(KEY_VOLUMEUP, cfg), MediaAction::None);
}

TEST(ResolveMediaHotkey, UnassignedBindingReturnsNone)
{
    MediaHotkeyConfig cfg;
    cfg.playPause = HotkeyBinding::key(KEY_PLAYPAUSE);
    EXPECT_EQ(resolveMediaHotkey(HotkeyBinding{}, cfg), MediaAction::None);
}

TEST(ResolveMediaHotkey, RelativeWheelMatchesDirection)
{
    MediaHotkeyConfig cfg;
    cfg.next = HotkeyBinding::relative(REL_HWHEEL, 1);
    cfg.previous = HotkeyBinding::relative(REL_HWHEEL, -1);

    EXPECT_EQ(resolveMediaHotkey(HotkeyBinding::relative(REL_HWHEEL, 1), cfg), MediaAction::Next);
    EXPECT_EQ(resolveMediaHotkey(HotkeyBinding::relative(REL_HWHEEL, -1), cfg),
              MediaAction::Previous);
    EXPECT_EQ(resolveMediaHotkey(HotkeyBinding::relative(REL_WHEEL, 1), cfg), MediaAction::None);
}

// ─── OSD layout hotkey resolution ─────────────────────────────────────────────

TEST(ResolveOsdLayoutAction, UnassignedReturnsNone)
{
    OsdLayoutHotkeyConfig cfg;
    EXPECT_EQ(resolveOsdLayoutAction(HotkeyBinding::key(KEY_UP), cfg), OsdLayoutAction::None);
}

TEST(ResolveOsdLayoutAction, AssignedSnapUpMatches)
{
    OsdLayoutHotkeyConfig cfg;
    cfg.snapUp = HotkeyBinding::key(KEY_UP);
    EXPECT_EQ(resolveOsdLayoutAction(HotkeyBinding::key(KEY_UP), cfg), OsdLayoutAction::SnapUp);
    EXPECT_EQ(resolveOsdLayoutAction(HotkeyBinding::key(KEY_DOWN), cfg), OsdLayoutAction::None);
}

TEST(ResolveOsdLayoutAction, ScaleDownRelativeMatchesEvent)
{
    OsdLayoutHotkeyConfig cfg;
    cfg.scaleDown = HotkeyBinding::relative(REL_WHEEL, -1);
    input_event ev{};
    ev.type = EV_REL;
    ev.code = REL_WHEEL;
    ev.value = -1;
    EXPECT_EQ(resolveOsdLayoutAction(HotkeyBinding::relative(REL_WHEEL, -1), ev, cfg),
              OsdLayoutAction::ScaleDown);
}

// ─── Scene hotkey resolution ──────────────────────────────────────────────────
namespace
{
AudioScene mkScene(const QString& id, const HotkeyBinding& hotkey)
{
    AudioScene s;
    s.id = id;
    s.name = id;
    s.hotkey = hotkey;
    s.targets = {SceneTarget{QStringLiteral("Spotify"), 10, std::nullopt, std::nullopt}};
    return s;
}
} // namespace

TEST(ResolveSceneHotkey, EmptyScenesReturnsEmpty)
{
    QList<AudioScene> scenes;
    EXPECT_TRUE(resolveSceneHotkey(88, scenes).isEmpty());
}

TEST(ResolveSceneHotkey, KeyMatchesScene)
{
    QList<AudioScene> scenes{
        mkScene(QStringLiteral("meeting"), HotkeyBinding::key(88)),
        mkScene(QStringLiteral("gaming"), HotkeyBinding::key(89)),
    };
    EXPECT_EQ(resolveSceneHotkey(88, scenes), QStringLiteral("meeting"));
    EXPECT_EQ(resolveSceneHotkey(89, scenes), QStringLiteral("gaming"));
}

TEST(ResolveSceneHotkey, UnboundCodeReturnsEmpty)
{
    QList<AudioScene> scenes{
        mkScene(QStringLiteral("meeting"), HotkeyBinding::key(88)),
    };
    EXPECT_TRUE(resolveSceneHotkey(99, scenes).isEmpty());
}

TEST(ResolveSceneHotkey, UnassignedSceneHotkeyNeverMatches)
{
    // A scene with no hotkey must never match, even on code 0.
    QList<AudioScene> scenes{
        mkScene(QStringLiteral("nohotkey"), HotkeyBinding{}),
    };
    EXPECT_TRUE(resolveSceneHotkey(0, scenes).isEmpty());
    EXPECT_TRUE(resolveSceneHotkey(HotkeyBinding{}, scenes).isEmpty());
}

TEST(ResolveSceneHotkey, FirstSceneWinsOnDuplicateBinding)
{
    // Two scenes share the same hotkey — the first scene in the list wins.
    QList<AudioScene> scenes{
        mkScene(QStringLiteral("first"), HotkeyBinding::key(88)),
        mkScene(QStringLiteral("second"), HotkeyBinding::key(88)),
    };
    EXPECT_EQ(resolveSceneHotkey(88, scenes), QStringLiteral("first"));
}

TEST(ResolveSceneHotkey, RelativeWheelMatchesDirection)
{
    QList<AudioScene> scenes{
        mkScene(QStringLiteral("up"), HotkeyBinding::relative(REL_WHEEL, 1)),
        mkScene(QStringLiteral("down"), HotkeyBinding::relative(REL_WHEEL, -1)),
    };
    EXPECT_EQ(resolveSceneHotkey(HotkeyBinding::relative(REL_WHEEL, 1), scenes),
              QStringLiteral("up"));
    EXPECT_EQ(resolveSceneHotkey(HotkeyBinding::relative(REL_WHEEL, -1), scenes),
              QStringLiteral("down"));
}

TEST(ResolveSceneHotkey, ScenesWithEmptyIdSkipped)
{
    AudioScene noId;
    noId.id = QString();
    noId.hotkey = HotkeyBinding::key(88);
    QList<AudioScene> scenes{noId};
    EXPECT_TRUE(resolveSceneHotkey(88, scenes).isEmpty());
}

// ─── Priority: profile > scene > media ────────────────────────────────────────
// These verify the resolution helpers honor the documented precedence when the
// same binding is bound at multiple layers. InputHandler::run() dispatches in
// the order profile → scene → media, so a non-empty profile match always wins,
// then a non-empty scene id, then a media action.

TEST(ScenePriority, ProfileWinsOverSceneAndMedia)
{
    // Same code 88 bound as a profile volume-up, a scene, and a media action.
    QList<Profile> profiles{mkProfile("p", 88, 114, 113, {})};
    QList<AudioScene> scenes{mkScene(QStringLiteral("s"), HotkeyBinding::key(88))};
    MediaHotkeyConfig media;
    media.playPause = HotkeyBinding::key(88);

    const HotkeyBinding binding = HotkeyBinding::key(88);
    // Profile match is non-empty → profile layer consumes the event first.
    const ProfileHotkeyMatch pmatch = resolveProfileHotkey(binding, {}, profiles);
    EXPECT_FALSE(pmatch.profileId.isEmpty());
    EXPECT_EQ(pmatch.action, ProfileHotkeyAction::VolumeUp);
    // Scene and media would also match, but are only consulted when the profile
    // layer yields nothing.
    EXPECT_EQ(resolveSceneHotkey(binding, scenes), QStringLiteral("s"));
    EXPECT_EQ(resolveMediaHotkey(binding, media), MediaAction::PlayPause);
}

TEST(ScenePriority, SceneWinsOverMediaWhenNoProfileMatch)
{
    // Code 88 bound to a scene and a media action, but to no profile.
    QList<Profile> profiles{mkProfile("p", 115, 114, 113, {})};
    QList<AudioScene> scenes{mkScene(QStringLiteral("s"), HotkeyBinding::key(88))};
    MediaHotkeyConfig media;
    media.playPause = HotkeyBinding::key(88);

    const HotkeyBinding binding = HotkeyBinding::key(88);
    EXPECT_TRUE(resolveProfileHotkey(binding, {}, profiles).profileId.isEmpty());
    EXPECT_EQ(resolveSceneHotkey(binding, scenes), QStringLiteral("s"));
    // Media still resolves, but scene is consulted first in run().
    EXPECT_EQ(resolveMediaHotkey(binding, media), MediaAction::PlayPause);
}

// ─── setScenes / currentScenes round-trip ────────────────────────────────────
TEST(InputHandler, ScenesRoundTrip)
{
    InputHandler handler;
    EXPECT_TRUE(handler.currentScenes().isEmpty());

    QList<AudioScene> scenes{
        mkScene(QStringLiteral("meeting"), HotkeyBinding::key(88)),
        mkScene(QStringLiteral("gaming"), HotkeyBinding::key(89)),
    };
    handler.setScenes(scenes);

    const auto got = handler.currentScenes();
    ASSERT_EQ(got.size(), 2);
    EXPECT_EQ(got[0].id, QStringLiteral("meeting"));
    EXPECT_EQ(got[0].hotkey, HotkeyBinding::key(88));
    EXPECT_EQ(got[1].id, QStringLiteral("gaming"));
    EXPECT_EQ(got[1].hotkey, HotkeyBinding::key(89));
}

TEST(InputHandler, SceneApplySignalConnectable)
{
    InputHandler handler;
    QSignalSpy spy(&handler, &InputHandler::scene_apply);
    EXPECT_TRUE(spy.isValid());
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

// Capabilities, not physical siblings, determine which devices need a mirror.
TEST(InputDevices, OnlyConfiguredCapabilitiesNeedExclusiveGrab)
{
    std::unique_ptr<libevdev, decltype(&libevdev_free)> keyboard(libevdev_new(), libevdev_free);
    libevdev_enable_event_code(keyboard.get(), EV_KEY, KEY_LEFTSHIFT, nullptr);
    libevdev_enable_event_code(keyboard.get(), EV_KEY, KEY_A, nullptr);
    EXPECT_FALSE(deviceHasConfiguredHotkey(keyboard.get(), {KEY_VOLUMEUP, KEY_VOLUMEDOWN}));
    EXPECT_TRUE(deviceHasConfiguredHotkey(keyboard.get(), {KEY_A}));
    EXPECT_FALSE(deviceHasConfiguredHotkey(keyboard.get(), {0}));
    EXPECT_FALSE(deviceHasConfiguredHotkey(nullptr, {KEY_A}));

    libevdev_enable_event_code(keyboard.get(), EV_KEY, KEY_VOLUMEUP, nullptr);
    EXPECT_TRUE(deviceHasConfiguredHotkey(keyboard.get(), {KEY_VOLUMEUP}));
    EXPECT_FALSE(
        deviceHasConfiguredHotkey(keyboard.get(), {HotkeyBinding::relative(REL_WHEEL, 1)}));
}

TEST(InputDevices, WheelBindingsRequireMatchingAxisButEitherDirection)
{
    std::unique_ptr<libevdev, decltype(&libevdev_free)> mouse(libevdev_new(), libevdev_free);
    libevdev_enable_event_code(mouse.get(), EV_REL, REL_WHEEL, nullptr);
    EXPECT_TRUE(deviceHasConfiguredHotkey(mouse.get(), {HotkeyBinding::relative(REL_WHEEL, 1)}));
    EXPECT_TRUE(deviceHasConfiguredHotkey(mouse.get(), {HotkeyBinding::relative(REL_WHEEL, -1)}));
    EXPECT_FALSE(deviceHasConfiguredHotkey(mouse.get(), {HotkeyBinding::relative(REL_HWHEEL, 1)}));
    EXPECT_FALSE(deviceHasConfiguredHotkey(mouse.get(), {HotkeyBinding::key(REL_WHEEL)}));
}

TEST(InputDevices, SysfsBitmapNativeAndCompatWordOrder)
{
    const auto native = parseInputCapabilityBitmap("1000000000000000 0 8000000000000001\n", 64);
    ASSERT_TRUE(native);
    EXPECT_TRUE(native->testBit(0));
    EXPECT_TRUE(native->testBit(63));
    EXPECT_FALSE(native->testBit(64));
    EXPECT_TRUE(native->testBit(188));
    const auto compat = parseInputCapabilityBitmap("10000000 0 0 0 80000000 1", 32);
    ASSERT_TRUE(compat);
    EXPECT_EQ(*native, *compat);
    const auto zero = parseInputCapabilityBitmap("0\n", 64);
    ASSERT_TRUE(zero);
    EXPECT_EQ(zero->count(true), 0);
}

TEST(InputDevices, InvalidSysfsBitmapFallsBack)
{
    for (const QByteArray text :
         {QByteArray{}, QByteArray("garbage"), QByteArray("-1"), QByteArray("+1"),
          QByteArray("0x10"), QByteArray("10000000000000000")})
        EXPECT_FALSE(parseInputCapabilityBitmap(text, 64));
    EXPECT_FALSE(parseInputCapabilityBitmap("100000000", 32));
    EXPECT_FALSE(parseInputCapabilityBitmap("1", 16));
}

// Exercise the same epoll removal path with a real evdev disconnect. A uinput
// device is private to this test; no physical input devices are grabbed.
TEST(InputPoll, DisconnectedEvdevIsRetiredWithoutStoppingOtherSources)
{
    libevdev* spec = libevdev_new();
    ASSERT_NE(spec, nullptr);
    libevdev_set_name(spec, "keyboard-volume-app disconnect test");
    ASSERT_EQ(libevdev_enable_event_code(spec, EV_KEY, KEY_F24, nullptr), 0);
    libevdev_uinput* ui = nullptr;
    int result = libevdev_uinput_create_from_device(spec, LIBEVDEV_UINPUT_OPEN_MANAGED, &ui);
    libevdev_free(spec);
    if (result < 0) GTEST_SKIP() << "uinput unavailable: " << strerror(-result);
    ASSERT_NE(ui, nullptr);
    const char* node = libevdev_uinput_get_devnode(ui);
    ASSERT_NE(node, nullptr);
    EvdevDevice disconnected;
    for (int attempt = 0; attempt < 100 && !disconnected.isOpen(); ++attempt)
    {
        disconnected.open(QString::fromLocal8Bit(node));
        if (!disconnected.isOpen()) usleep(10000);
    }
    if (!disconnected.isOpen())
    {
        libevdev_uinput_destroy(ui);
        GTEST_SKIP() << "Cannot open test evdev node (device permissions unavailable)";
    }
    int pipefd[2];
    ASSERT_EQ(pipe2(pipefd, O_NONBLOCK | O_CLOEXEC), 0);
    int otherPipe[2];
    ASSERT_EQ(pipe2(otherPipe, O_NONBLOCK | O_CLOEXEC), 0);
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(epfd, 0);
    EvdevDevice other;
    std::vector<PollSource> sources{
        {PollSource::Type::EvdevInput, &disconnected, disconnected.fd()},
        {PollSource::Type::UinputOutput, &disconnected, pipefd[0]},
        {PollSource::Type::EvdevInput, &other, otherPipe[0]}};
    for (int i = 0; i < 3; ++i)
    {
        epoll_event event{};
        event.events = EPOLLIN;
        event.data.ptr = &sources[i];
        ASSERT_EQ(epoll_ctl(epfd, EPOLL_CTL_ADD, sources[i].fd, &event), 0);
    }
    libevdev_uinput_destroy(ui);
    epoll_event event{};
    ASSERT_EQ(epoll_wait(epfd, &event, 1, 1000), 1);
    EXPECT_NE(event.events & (EPOLLHUP | EPOLLERR), 0U);
    input_event ev{};
    EXPECT_EQ(libevdev_next_event(disconnected.dev(), LIBEVDEV_READ_FLAG_NORMAL, &ev), -ENODEV);
    retirePollSources(epfd, sources, &disconnected);
    disconnected.close();
    EXPECT_EQ(sources[0].dev, nullptr);
    EXPECT_EQ(sources[1].dev, nullptr);
    EXPECT_EQ(sources[2].dev, &other);
    EXPECT_EQ(epoll_wait(epfd, &event, 1, 50), 0);
    ASSERT_EQ(write(otherPipe[1], "x", 1), 1);
    ASSERT_EQ(epoll_wait(epfd, &event, 1, 100), 1);
    EXPECT_EQ(event.data.ptr, &sources[2]);
    close(otherPipe[0]);
    close(otherPipe[1]);
    close(epfd);
    close(pipefd[0]);
    close(pipefd[1]);
}
