#include <gtest/gtest.h>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QFile>

#include <sys/stat.h>
#include <unistd.h>
#include <linux/input.h>

#include "config.h"

// ─── deepMerge tests ───────────────────────────────────────────────────────────

TEST(ConfigDeepMerge, ScalarOverride)
{
    QJsonObject base{{"volume_step", 5}};
    QJsonObject override{{"volume_step", 10}};

    QJsonObject result = Config::deepMerge(base, override);

    EXPECT_EQ(result["volume_step"].toInt(), 10);
}

TEST(ConfigDeepMerge, NestedMerge)
{
    QJsonObject base{{"osd", QJsonObject{{"x", 50}, {"y", 1150}}}};
    QJsonObject override{{"osd", QJsonObject{{"x", 200}}}};

    QJsonObject result = Config::deepMerge(base, override);

    QJsonObject osd = result["osd"].toObject();
    EXPECT_EQ(osd["x"].toInt(), 200);  // overridden
    EXPECT_EQ(osd["y"].toInt(), 1150); // preserved from base
}

TEST(ConfigDeepMerge, DefaultsOnly)
{
    QJsonObject base{{"language", "en"}, {"volume_step", 5}};
    QJsonObject override{}; // empty

    QJsonObject result = Config::deepMerge(base, override);

    EXPECT_EQ(result["language"].toString().toStdString(), "en");
    EXPECT_EQ(result["volume_step"].toInt(), 5);
}

TEST(ConfigDeepMerge, OverrideOnlyKeys)
{
    QJsonObject base{{"language", "en"}};
    QJsonObject override{{"future_key", "future_value"}};

    QJsonObject result = Config::deepMerge(base, override);

    EXPECT_TRUE(result.contains("language"));
    EXPECT_TRUE(result.contains("future_key"));
    EXPECT_EQ(result["future_key"].toString().toStdString(), "future_value");
}

TEST(ConfigDeepMerge, NullPreserved)
{
    QJsonObject base{{"input_device", QJsonValue::Null}, {"language", "en"}};
    QJsonObject override{
        {"input_device", QJsonValue::Null} // explicitly null
    };

    QJsonObject result = Config::deepMerge(base, override);

    EXPECT_TRUE(result["input_device"].isNull());
    EXPECT_EQ(result["language"].toString().toStdString(), "en");
}

TEST(ConfigDeepMerge, NestedObjectNewKey)
{
    QJsonObject base{{"osd", QJsonObject{{"x", 50}}}};
    QJsonObject override{{"osd", QJsonObject{{"z", 999}}}};

    QJsonObject result = Config::deepMerge(base, override);

    QJsonObject osd = result["osd"].toObject();
    EXPECT_EQ(osd["x"].toInt(), 50);  // preserved from base
    EXPECT_EQ(osd["z"].toInt(), 999); // added from override
}

// ─── Config I/O tests (isolated via QTemporaryDir) ─────────────────────────────

TEST(Config, FirstRunWhenNoFile)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    Config config(tmp.path());
    EXPECT_TRUE(config.isFirstRun());
    EXPECT_EQ(config.language().toStdString(), "en");
    EXPECT_EQ(config.volumeStep(), 5);
}

TEST(Config, LoadExistingFile)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    // Write a config with some overrides
    QFile f(tmp.path() + "/config.json");
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    QJsonObject json{{"language", "pl"}, {"volume_step", 10}};
    f.write(QJsonDocument(json).toJson());
    f.close();

    Config config(tmp.path());
    EXPECT_FALSE(config.isFirstRun());
    EXPECT_EQ(config.language().toStdString(), "pl");
    EXPECT_EQ(config.volumeStep(), 10);
    // Fields not in file keep defaults (via deepMerge)
    EXPECT_EQ(config.hotkeys().volumeUp, 115);
}

TEST(Config, SettersRoundtrip)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    Config config(tmp.path());

    config.setLanguage("pl");
    EXPECT_EQ(config.language().toStdString(), "pl");

    config.setVolumeStep(15);
    EXPECT_EQ(config.volumeStep(), 15);

    config.setInputDevice("/dev/input/event3");
    EXPECT_EQ(config.inputDevice().toStdString(), "/dev/input/event3");

    config.setInputDevice(""); // clear
    EXPECT_TRUE(config.inputDevice().isEmpty());

    config.setSelectedApp("firefox");
    EXPECT_EQ(config.selectedApp().toStdString(), "firefox");
    config.setSelectedApp("");
    EXPECT_TRUE(config.selectedApp().isEmpty());

    // Reload from file — setters persist because save() was called
    Config config2(tmp.path());
    EXPECT_EQ(config2.language().toStdString(), "pl");
    EXPECT_EQ(config2.volumeStep(), 15);
    EXPECT_TRUE(config2.inputDevice().isEmpty());
    EXPECT_TRUE(config2.selectedApp().isEmpty());
}

TEST(Config, SaveFailureKeepsExistingFile)
{
    if (geteuid() == 0) GTEST_SKIP() << "Directory permissions do not block root writes";

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    Config config(tmp.path());
    config.setLanguage("pl");

    const QString configPath = tmp.path() + "/config.json";
    QFile beforeFile(configPath);
    ASSERT_TRUE(beforeFile.open(QIODevice::ReadOnly));
    const QByteArray before = beforeFile.readAll();
    beforeFile.close();

    const QByteArray dirPath = QFile::encodeName(tmp.path());
    ASSERT_EQ(chmod(dirPath.constData(), 0555), 0);

    config.setLanguage("en");

    ASSERT_EQ(chmod(dirPath.constData(), 0755), 0);

    QFile afterFile(configPath);
    ASSERT_TRUE(afterFile.open(QIODevice::ReadOnly));
    const QByteArray after = afterFile.readAll();
    afterFile.close();

    EXPECT_EQ(after, before);

    Config reloaded(tmp.path());
    EXPECT_EQ(reloaded.language().toStdString(), "pl");
}

TEST(Config, VolumeStepClamp)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());

    config.setVolumeStep(0);
    EXPECT_EQ(config.volumeStep(), 1);

    config.setVolumeStep(999);
    EXPECT_EQ(config.volumeStep(), 50);

    config.setVolumeStep(25);
    EXPECT_EQ(config.volumeStep(), 25);
}

TEST(Config, HotkeysRoundtrip)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());

    auto hk = config.hotkeys();
    EXPECT_EQ(hk.volumeUp, 115);
    EXPECT_EQ(hk.volumeDown, 114);
    EXPECT_EQ(hk.mute, 113);

    config.setHotkeys(200, 201, 202);
    hk = config.hotkeys();
    EXPECT_EQ(hk.volumeUp, 200);
    EXPECT_EQ(hk.volumeDown, 201);
    EXPECT_EQ(hk.mute, 202);

    // Persisted
    Config config2(tmp.path());
    hk = config2.hotkeys();
    EXPECT_EQ(hk.volumeUp, 200);
    EXPECT_EQ(hk.volumeDown, 201);
    EXPECT_EQ(hk.mute, 202);
}

TEST(Config, ScrollHotkeyRoundtrip)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    Profile p;
    p.id = "default";
    p.name = "Default";
    p.hotkeys.volumeUp = HotkeyBinding::relative(REL_WHEEL, 1);
    p.hotkeys.volumeDown = HotkeyBinding::relative(REL_WHEEL, -1);
    p.hotkeys.mute = KEY_MUTE;
    p.hotkeys.show = HotkeyBinding::relative(REL_HWHEEL, 1);
    p.ducking.enabled = true;
    p.ducking.hotkey = HotkeyBinding::relative(REL_HWHEEL, -1);

    {
        Config config(tmp.path());
        config.setProfiles({p});
    }

    Config reloaded(tmp.path());
    const auto profs = reloaded.profiles();
    ASSERT_EQ(profs.size(), 1);
    EXPECT_EQ(profs[0].hotkeys.volumeUp, HotkeyBinding::relative(REL_WHEEL, 1));
    EXPECT_EQ(profs[0].hotkeys.volumeDown, HotkeyBinding::relative(REL_WHEEL, -1));
    EXPECT_EQ(profs[0].hotkeys.show, HotkeyBinding::relative(REL_HWHEEL, 1));
    EXPECT_EQ(profs[0].ducking.hotkey, HotkeyBinding::relative(REL_HWHEEL, -1));

    QFile f(tmp.path() + "/config.json");
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject profile = root["profiles"].toArray().first().toObject();
    const QJsonObject hotkeys = profile["hotkeys"].toObject();
    const QJsonObject volumeUp = hotkeys["volume_up"].toObject();
    EXPECT_EQ(volumeUp["type"].toString().toStdString(), "rel");
    EXPECT_EQ(volumeUp["code"].toInt(), REL_WHEEL);
    EXPECT_EQ(volumeUp["direction"].toInt(), 1);
    const QJsonObject show = hotkeys["show"].toObject();
    EXPECT_EQ(show["type"].toString().toStdString(), "rel");
    EXPECT_EQ(show["code"].toInt(), REL_HWHEEL);
    EXPECT_EQ(show["direction"].toInt(), 1);
}

TEST(Config, OsdRoundtrip)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());

    auto osd = config.osd();
    EXPECT_EQ(osd.x, 50);
    EXPECT_EQ(osd.y, 1150);
    EXPECT_EQ(osd.timeoutMs, 1200);
    EXPECT_EQ(osd.opacity, 85);
    EXPECT_EQ(osd.screen, 0);

    OsdConfig newOsd;
    newOsd.x = 100;
    newOsd.y = 200;
    newOsd.timeoutMs = 800;
    newOsd.opacity = 50;
    newOsd.screen = 2;
    newOsd.colorBg = "#000000";
    newOsd.colorText = "#FF0000";
    newOsd.colorBar = "#00FF00";
    config.setOsd(newOsd);

    auto r = config.osd();
    EXPECT_EQ(r.x, 100);
    EXPECT_EQ(r.y, 200);
    EXPECT_EQ(r.timeoutMs, 800);
    EXPECT_EQ(r.opacity, 50);
    EXPECT_EQ(r.screen, 2);

    // Persisted
    Config config2(tmp.path());
    r = config2.osd();
    EXPECT_EQ(r.x, 100);
    EXPECT_EQ(r.y, 200);
}

TEST(Config, UpdateOsdPartial)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());

    config.updateOsd(3, 900, 300, 400, 70, "#AAA", "#BBB", "#CCC");

    auto r = config.osd();
    EXPECT_EQ(r.screen, 3);
    EXPECT_EQ(r.timeoutMs, 900);
    EXPECT_EQ(r.x, 300);
    EXPECT_EQ(r.y, 400);
    EXPECT_EQ(r.opacity, 70);
    EXPECT_EQ(r.colorBg.toStdString(), "#AAA");
    EXPECT_EQ(r.colorText.toStdString(), "#BBB");
    EXPECT_EQ(r.colorBar.toStdString(), "#CCC");

    // Persisted
    Config config2(tmp.path());
    r = config2.osd();
    EXPECT_EQ(r.screen, 3);
    EXPECT_EQ(r.colorBg.toStdString(), "#AAA");
}

// ─── Profiles tests ──────────────────────────────────────────────────────────

TEST(ConfigProfiles, MigrationOldConfigSynthesizesDefaultProfile)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    // Old-schema config: legacy selected_app + hotkeys, no "profiles" key.
    QFile f(tmp.path() + "/config.json");
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    QJsonObject json{
        {"selected_app", "spotify"},
        {"hotkeys",
         QJsonObject{
             {"volume_up", 200},
             {"volume_down", 201},
             {"mute", 202},
         }},
    };
    f.write(QJsonDocument(json).toJson());
    f.close();

    Config config(tmp.path());
    auto profs = config.profiles();
    ASSERT_EQ(profs.size(), 1);
    EXPECT_EQ(profs[0].id.toStdString(), "default");
    EXPECT_EQ(profs[0].name.toStdString(), "Default");
    EXPECT_EQ(profs[0].app.toStdString(), "spotify");
    EXPECT_EQ(profs[0].hotkeys.volumeUp, 200);
    EXPECT_EQ(profs[0].hotkeys.volumeDown, 201);
    EXPECT_EQ(profs[0].hotkeys.mute, 202);
    EXPECT_TRUE(profs[0].modifiers.isEmpty());
    EXPECT_FALSE(profs[0].ducking.enabled);
    EXPECT_EQ(profs[0].ducking.volume, 25);
    EXPECT_EQ(profs[0].ducking.hotkey, 0);

    // The config file should now contain a "profiles" array on disk.
    QFile f2(tmp.path() + "/config.json");
    ASSERT_TRUE(f2.open(QIODevice::ReadOnly));
    QJsonDocument doc = QJsonDocument::fromJson(f2.readAll());
    EXPECT_TRUE(doc.object().contains("profiles"));
}

TEST(ConfigProfiles, EmptyProfilesRebuildsDefaultProfile)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    QFile f(tmp.path() + "/config.json");
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    QJsonObject json{
        {"selected_app", "spotify"},
        {"hotkeys",
         QJsonObject{
             {"volume_up", 210},
             {"volume_down", 211},
             {"mute", 212},
         }},
        {"profiles", QJsonArray{}},
    };
    f.write(QJsonDocument(json).toJson());
    f.close();

    Config config(tmp.path());
    auto profs = config.profiles();
    ASSERT_EQ(profs.size(), 1);
    EXPECT_EQ(profs[0].id.toStdString(), "default");
    EXPECT_EQ(profs[0].app.toStdString(), "spotify");
    EXPECT_EQ(profs[0].hotkeys.volumeUp, 210);
    EXPECT_EQ(profs[0].hotkeys.volumeDown, 211);
    EXPECT_EQ(profs[0].hotkeys.mute, 212);
}

TEST(ConfigProfiles, MalformedProfilesRebuildsDefaultProfile)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    QFile f(tmp.path() + "/config.json");
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    QJsonObject json{
        {"selected_app", "vlc"},
        {"hotkeys",
         QJsonObject{
             {"volume_up", 220},
             {"volume_down", 221},
             {"mute", 222},
         }},
        {"profiles",
         QJsonArray{
             QJsonObject{{"name", "Missing id"}, {"app", "firefox"}},
             QStringLiteral("not an object"),
         }},
    };
    f.write(QJsonDocument(json).toJson());
    f.close();

    Config config(tmp.path());
    auto profs = config.profiles();
    ASSERT_EQ(profs.size(), 1);
    EXPECT_EQ(profs[0].id.toStdString(), "default");
    EXPECT_EQ(profs[0].app.toStdString(), "vlc");
    EXPECT_EQ(profs[0].hotkeys.volumeUp, 220);
    EXPECT_EQ(profs[0].hotkeys.volumeDown, 221);
    EXPECT_EQ(profs[0].hotkeys.mute, 222);
}

TEST(ConfigProfiles, RoundTripTwoProfiles)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    Profile a;
    a.id = "default";
    a.name = "Default";
    a.app = "spotify";
    a.hotkeys = {115, 114, 113, {}};

    Profile b;
    b.id = "firefox-ctrl";
    b.name = "Firefox (Ctrl)";
    b.app = "firefox";
    b.hotkeys = {115, 114, 113, {}};
    b.modifiers.insert(Modifier::Ctrl);
    b.ducking.enabled = true;
    b.ducking.volume = 20;
    b.ducking.hotkey = 88;

    {
        Config config(tmp.path());
        config.setProfiles({a, b});
    }

    Config config2(tmp.path());
    auto profs = config2.profiles();
    ASSERT_EQ(profs.size(), 2);
    EXPECT_EQ(profs[0], a);
    EXPECT_EQ(profs[1], b);
}

TEST(ConfigProfiles, MissingDuckingUsesDefaults)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    QFile f(tmp.path() + "/config.json");
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    QJsonObject profile{
        {"id", "default"},
        {"name", "Default"},
        {"app", "spotify"},
        {"modifiers", QJsonArray{}},
        {"hotkeys",
         QJsonObject{
             {"volume_up", 115},
             {"volume_down", 114},
             {"mute", 113},
         }},
    };
    QJsonObject json{{"profiles", QJsonArray{profile}}};
    f.write(QJsonDocument(json).toJson());
    f.close();

    Config config(tmp.path());
    auto profs = config.profiles();
    ASSERT_EQ(profs.size(), 1);
    EXPECT_FALSE(profs[0].ducking.enabled);
    EXPECT_EQ(profs[0].ducking.volume, 25);
    EXPECT_EQ(profs[0].ducking.hotkey, 0);
}

TEST(ConfigProfiles, DuckingVolumeAndHotkeyAreSanitized)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());

    Profile p;
    p.id = "default";
    p.name = "Default";
    p.hotkeys = {115, 114, 113, {}};
    p.ducking.enabled = true;
    p.ducking.volume = 150;
    p.ducking.hotkey = -9;

    config.setProfiles({p});
    auto profs = config.profiles();
    ASSERT_EQ(profs.size(), 1);
    EXPECT_TRUE(profs[0].ducking.enabled);
    EXPECT_EQ(profs[0].ducking.volume, 100);
    EXPECT_EQ(profs[0].ducking.hotkey, 0);
}

TEST(ConfigProfiles, DefaultMirroringToLegacyKeys)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());

    Profile a;
    a.id = "main";
    a.name = "Main";
    a.app = "vlc";
    a.hotkeys = {300, 301, 302, {}};

    Profile b;
    b.id = "other";
    b.name = "Other";
    b.app = "firefox";
    b.hotkeys = {115, 114, 113, {}};

    config.setProfiles({a, b});

    // Legacy keys must mirror profile[0].
    EXPECT_EQ(config.selectedApp().toStdString(), "vlc");
    EXPECT_EQ(config.hotkeys().volumeUp, 300);
    EXPECT_EQ(config.hotkeys().volumeDown, 301);
    EXPECT_EQ(config.hotkeys().mute, 302);
}

TEST(ConfigProfiles, RejectEmpty)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());

    auto before = config.profiles();
    ASSERT_FALSE(before.isEmpty());

    config.setProfiles({}); // should be no-op

    auto after = config.profiles();
    EXPECT_EQ(after, before);
}

TEST(ConfigProfiles, SetSelectedAppMirrorsToDefaultProfile)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());

    config.setSelectedApp("youtube-music");
    EXPECT_EQ(config.defaultProfile().app.toStdString(), "youtube-music");
    EXPECT_EQ(config.selectedApp().toStdString(), "youtube-music");
}

TEST(ConfigProfiles, SetHotkeysMirrorsToDefaultProfile)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());

    config.setHotkeys(500, 501, 502);
    auto def = config.defaultProfile();
    EXPECT_EQ(def.hotkeys.volumeUp, 500);
    EXPECT_EQ(def.hotkeys.volumeDown, 501);
    EXPECT_EQ(def.hotkeys.mute, 502);
}

TEST(ConfigProfiles, SetProfilesUniqueifiesIds)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());

    Profile a;
    a.id = "x";
    a.name = "X";
    a.app = "spotify";
    a.hotkeys = {1, 2, 3, {}};
    Profile b;
    b.id = "x";
    b.name = "X2";
    b.app = "firefox";
    b.hotkeys = {4, 5, 6, {}};

    config.setProfiles({a, b});
    auto profs = config.profiles();
    ASSERT_EQ(profs.size(), 2);
    EXPECT_EQ(profs[0].id.toStdString(), "x");
    EXPECT_NE(profs[1].id.toStdString(), "x"); // suffixed
}

// ─── Audio scene tests ───────────────────────────────────────────────────────

TEST(ConfigScenes, DefaultsToEmptyList)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    Config config(tmp.path());

    EXPECT_TRUE(config.scenes().isEmpty());
}

TEST(ConfigScenes, RoundTripSceneTargets)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    AudioScene scene;
    scene.id = "meeting";
    scene.name = "Meeting";
    scene.targets = {
        SceneTarget{QStringLiteral("Spotify"), 10, false},
        SceneTarget{QStringLiteral("Discord"), 80, std::nullopt},
        SceneTarget{QStringLiteral("Steam"), std::nullopt, true},
    };

    {
        Config config(tmp.path());
        config.setScenes({scene});
    }

    Config config2(tmp.path());
    const auto scenes = config2.scenes();
    ASSERT_EQ(scenes.size(), 1);
    EXPECT_EQ(scenes[0], scene);
}

TEST(ConfigScenes, SanitizesTargetsAndUniqueifiesIds)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    AudioScene a;
    a.id = "scene";
    a.name = "A";
    a.targets = {
        SceneTarget{QStringLiteral(" Spotify "), 150, std::nullopt},
        SceneTarget{QStringLiteral(""), 50, false},
        SceneTarget{QStringLiteral("Ignored"), std::nullopt, std::nullopt},
    };

    AudioScene b;
    b.id = "scene";
    b.name = "B";
    b.targets = {SceneTarget{QStringLiteral("Discord"), -10, true}};

    Config config(tmp.path());
    config.setScenes({a, b});

    const auto scenes = config.scenes();
    ASSERT_EQ(scenes.size(), 2);
    EXPECT_EQ(scenes[0].id.toStdString(), "scene");
    EXPECT_EQ(scenes[1].id.toStdString(), "scene-2");
    ASSERT_EQ(scenes[0].targets.size(), 1);
    EXPECT_EQ(scenes[0].targets[0].match.toStdString(), "Spotify");
    ASSERT_TRUE(scenes[0].targets[0].volume);
    EXPECT_EQ(*scenes[0].targets[0].volume, 100);
    ASSERT_EQ(scenes[1].targets.size(), 1);
    ASSERT_TRUE(scenes[1].targets[0].volume);
    EXPECT_EQ(*scenes[1].targets[0].volume, 0);
}

TEST(ConfigHotkeys, ShowHotkeyDefaultsToZero)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());
    EXPECT_FALSE(config.defaultProfile().hotkeys.show.isAssigned());
}

TEST(ConfigHotkeys, ShowHotkeyRoundTrip)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());

    QList<Profile> profs = config.profiles();
    profs[0].hotkeys.show = 99;
    config.setProfiles(profs);

    Config config2(tmp.path());
    EXPECT_EQ(config2.profiles()[0].hotkeys.show, HotkeyBinding::key(99));
}

TEST(ConfigHotkeys, ShowHotkeyMissingInOldFileDefaultsToZero)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    // Write a profile JSON without the "show" key (simulates an old config file).
    QFile f(tmp.path() + QStringLiteral("/config.json"));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    QJsonObject hk{{QStringLiteral("volume_up"), 115},
                   {QStringLiteral("volume_down"), 114},
                   {QStringLiteral("mute"), 113}};
    QJsonObject prof{{QStringLiteral("id"), QStringLiteral("default")},
                     {QStringLiteral("name"), QStringLiteral("Default")},
                     {QStringLiteral("hotkeys"), hk}};
    QJsonObject root{{QStringLiteral("profiles"), QJsonArray{prof}}};
    f.write(QJsonDocument(root).toJson());
    f.close();

    Config config(tmp.path());
    EXPECT_FALSE(config.defaultProfile().hotkeys.show.isAssigned());
}

// ─── OSD progress fields ───────────────────────────────────────────────────────

TEST(ConfigOsdProgress, DefaultsAreOff)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());
    const OsdConfig osd = config.osd();
    EXPECT_FALSE(osd.progressEnabled);
    EXPECT_TRUE(osd.progressInteractive);
    EXPECT_EQ(osd.progressPollMs, 500);
    EXPECT_EQ(osd.progressLabelMode.toStdString(), "app");
    EXPECT_EQ(osd.trackedPlayers.size(), 4);
    EXPECT_EQ(osd.trackedPlayers[0].toStdString(), "spotify");
    EXPECT_EQ(osd.trackedPlayers[1].toStdString(), "youtube");
    EXPECT_EQ(osd.trackedPlayers[2].toStdString(), "strawberry");
    EXPECT_EQ(osd.trackedPlayers[3].toStdString(), "harmonoid");
}

TEST(ConfigOsdProgress, RoundTrip)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());

    OsdConfig osd = config.osd();
    osd.progressEnabled = true;
    osd.progressInteractive = false;
    osd.progressPollMs = 800;
    osd.progressLabelMode = QStringLiteral("track");
    osd.trackedPlayers = {QStringLiteral("strawberry"), QStringLiteral("spotify")};
    config.setOsd(osd);

    Config config2(tmp.path());
    const OsdConfig osd2 = config2.osd();
    EXPECT_TRUE(osd2.progressEnabled);
    EXPECT_FALSE(osd2.progressInteractive);
    EXPECT_EQ(osd2.progressPollMs, 800);
    EXPECT_EQ(osd2.progressLabelMode.toStdString(), "track");
    ASSERT_EQ(osd2.trackedPlayers.size(), 2);
    EXPECT_EQ(osd2.trackedPlayers[0].toStdString(), "strawberry");
    EXPECT_EQ(osd2.trackedPlayers[1].toStdString(), "spotify");
}

TEST(ConfigOsdProgress, PollMsClampedOnLoad)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    // Below minimum (200)
    {
        QFile f(tmp.path() + QStringLiteral("/config.json"));
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        QJsonObject osdObj{{QStringLiteral("progress_poll_ms"), 50}};
        QJsonObject root{{QStringLiteral("osd"), osdObj}};
        f.write(QJsonDocument(root).toJson());
        f.close();
    }
    EXPECT_EQ(Config(tmp.path()).osd().progressPollMs, 200);

    // Above maximum (2000)
    {
        QFile f(tmp.path() + QStringLiteral("/config.json"));
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        QJsonObject osdObj{{QStringLiteral("progress_poll_ms"), 9999}};
        QJsonObject root{{QStringLiteral("osd"), osdObj}};
        f.write(QJsonDocument(root).toJson());
        f.close();
    }
    EXPECT_EQ(Config(tmp.path()).osd().progressPollMs, 2000);
}

TEST(ConfigOsdProgress, LabelModeValidation)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    auto writeMode = [&](const QString& mode)
    {
        QFile f(tmp.path() + QStringLiteral("/config.json"));
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        QJsonObject osdObj{{QStringLiteral("progress_label_mode"), mode}};
        QJsonObject root{{QStringLiteral("osd"), osdObj}};
        f.write(QJsonDocument(root).toJson());
        f.close();
    };

    writeMode(QStringLiteral("both"));
    EXPECT_EQ(Config(tmp.path()).osd().progressLabelMode.toStdString(), "both");

    writeMode(QStringLiteral("track"));
    EXPECT_EQ(Config(tmp.path()).osd().progressLabelMode.toStdString(), "track");

    writeMode(QStringLiteral("invalid_value"));
    EXPECT_EQ(Config(tmp.path()).osd().progressLabelMode.toStdString(), "app");
}

TEST(ConfigOsdProgress, MigrateOldConfigGetsDefaults)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    // Old config without any progress fields — deepMerge should supply defaults
    QFile f(tmp.path() + QStringLiteral("/config.json"));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    QJsonObject osdObj{{QStringLiteral("x"), 100}, {QStringLiteral("y"), 200}};
    QJsonObject root{{QStringLiteral("osd"), osdObj}};
    f.write(QJsonDocument(root).toJson());
    f.close();

    Config config(tmp.path());
    const OsdConfig osd = config.osd();
    EXPECT_EQ(osd.x, 100);
    EXPECT_EQ(osd.y, 200);
    EXPECT_FALSE(osd.progressEnabled);
    EXPECT_EQ(osd.progressPollMs, 500);
    EXPECT_EQ(osd.trackedPlayers.size(), 4);
}

TEST(ConfigOsdProgress, EmptyTrackedPlayersArrayIsPreserved)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    // Empty tracked_players array means "do not track any MPRIS players".
    QFile f(tmp.path() + QStringLiteral("/config.json"));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    QJsonObject osdObj{{QStringLiteral("tracked_players"), QJsonArray{}}};
    QJsonObject root{{QStringLiteral("osd"), osdObj}};
    f.write(QJsonDocument(root).toJson());
    f.close();

    Config config(tmp.path());
    EXPECT_TRUE(config.osd().trackedPlayers.isEmpty());
}

TEST(ConfigOsdProgress, MediaControlsDefaultOn)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());
    EXPECT_TRUE(config.osd().mediaControlsEnabled);
}

TEST(ConfigOsdProgress, MediaControlsRoundTrip)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());

    OsdConfig osd = config.osd();
    osd.mediaControlsEnabled = false;
    config.setOsd(osd);

    Config config2(tmp.path());
    EXPECT_FALSE(config2.osd().mediaControlsEnabled);
}

TEST(ConfigOsdProgress, OsdScaleDefaultOne)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());
    EXPECT_DOUBLE_EQ(config.osd().osdScale, 1.0);
}

TEST(ConfigOsdProgress, OsdScaleRoundTrip)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());

    OsdConfig osd = config.osd();
    osd.osdScale = 1.5;
    config.setOsd(osd);

    Config config2(tmp.path());
    EXPECT_DOUBLE_EQ(config2.osd().osdScale, 1.5);
}

TEST(ConfigOsdProgress, OsdScaleClamped)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    // Write a config with out-of-range scale values
    QFile f(tmp.path() + QStringLiteral("/config.json"));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    QJsonObject osdObj{{QStringLiteral("osd_scale"), 10.0}};
    QJsonObject root{{QStringLiteral("osd"), osdObj}};
    f.write(QJsonDocument(root).toJson());
    f.close();

    Config config(tmp.path());
    EXPECT_DOUBLE_EQ(config.osd().osdScale, 3.0); // clamped to max

    QFile f2(tmp.path() + QStringLiteral("/config.json"));
    ASSERT_TRUE(f2.open(QIODevice::WriteOnly));
    QJsonObject osdObj2{{QStringLiteral("osd_scale"), 0.1}};
    QJsonObject root2{{QStringLiteral("osd"), osdObj2}};
    f2.write(QJsonDocument(root2).toJson());
    f2.close();

    Config config3(tmp.path());
    EXPECT_DOUBLE_EQ(config3.osd().osdScale, 0.5); // clamped to min
}
