#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QSet>
#include <QList>

#include <linux/input.h>

#include "inputhandler.h"
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

// ─── Scene hotkey resolution ──────────────────────────────────────────────────
namespace
{
AudioScene mkScene(const QString& id, const HotkeyBinding& hotkey)
{
    AudioScene s;
    s.id = id;
    s.name = id;
    s.hotkey = hotkey;
    s.targets = {SceneTarget{QStringLiteral("Spotify"), 10, std::nullopt}};
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
