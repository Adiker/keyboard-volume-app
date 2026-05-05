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
    a.app = QStringLiteral("spotify");
    a.hotkeys.volumeUp = 200;
    a.hotkeys.volumeDown = 201;
    a.hotkeys.mute = 202;

    Profile b;
    b.id = QStringLiteral("b");
    b.name = QStringLiteral("B");
    b.app = QStringLiteral("firefox");
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

    EXPECT_TRUE(spyUp.isValid());
    EXPECT_TRUE(spyDown.isValid());
    EXPECT_TRUE(spyMute.isValid());
    EXPECT_TRUE(spyDuck.isValid());
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
    QSet<int> hotkeys{115, 114, 113};
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
    p.app = id;
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

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
