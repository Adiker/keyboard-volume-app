#include <gtest/gtest.h>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QFile>

#include <sys/stat.h>
#include <unistd.h>

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
    a.hotkeys = {115, 114, 113};

    Profile b;
    b.id = "firefox-ctrl";
    b.name = "Firefox (Ctrl)";
    b.app = "firefox";
    b.hotkeys = {115, 114, 113};
    b.modifiers.insert(Modifier::Ctrl);

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

TEST(ConfigProfiles, DefaultMirroringToLegacyKeys)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());

    Profile a;
    a.id = "main";
    a.name = "Main";
    a.app = "vlc";
    a.hotkeys = {300, 301, 302};

    Profile b;
    b.id = "other";
    b.name = "Other";
    b.app = "firefox";
    b.hotkeys = {115, 114, 113};

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
    a.hotkeys = {1, 2, 3};
    Profile b;
    b.id = "x";
    b.name = "X2";
    b.app = "firefox";
    b.hotkeys = {4, 5, 6};

    config.setProfiles({a, b});
    auto profs = config.profiles();
    ASSERT_EQ(profs.size(), 2);
    EXPECT_EQ(profs[0].id.toStdString(), "x");
    EXPECT_NE(profs[1].id.toStdString(), "x"); // suffixed
}
