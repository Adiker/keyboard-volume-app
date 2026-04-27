#include <gtest/gtest.h>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QFile>

#include "config.h"

// ─── deepMerge tests ───────────────────────────────────────────────────────────

TEST(ConfigDeepMerge, ScalarOverride)
{
    QJsonObject base    {{"volume_step", 5}};
    QJsonObject override{{"volume_step", 10}};

    QJsonObject result = Config::deepMerge(base, override);

    EXPECT_EQ(result["volume_step"].toInt(), 10);
}

TEST(ConfigDeepMerge, NestedMerge)
{
    QJsonObject base{
        {"osd", QJsonObject{{"x", 50}, {"y", 1150}}}
    };
    QJsonObject override{
        {"osd", QJsonObject{{"x", 200}}}
    };

    QJsonObject result = Config::deepMerge(base, override);

    QJsonObject osd = result["osd"].toObject();
    EXPECT_EQ(osd["x"].toInt(), 200);     // overridden
    EXPECT_EQ(osd["y"].toInt(), 1150);    // preserved from base
}

TEST(ConfigDeepMerge, DefaultsOnly)
{
    QJsonObject base{
        {"language", "en"},
        {"volume_step", 5}
    };
    QJsonObject override{};   // empty

    QJsonObject result = Config::deepMerge(base, override);

    EXPECT_EQ(result["language"].toString().toStdString(), "en");
    EXPECT_EQ(result["volume_step"].toInt(), 5);
}

TEST(ConfigDeepMerge, OverrideOnlyKeys)
{
    QJsonObject base{
        {"language", "en"}
    };
    QJsonObject override{
        {"future_key", "future_value"}
    };

    QJsonObject result = Config::deepMerge(base, override);

    EXPECT_TRUE(result.contains("language"));
    EXPECT_TRUE(result.contains("future_key"));
    EXPECT_EQ(result["future_key"].toString().toStdString(), "future_value");
}

TEST(ConfigDeepMerge, NullPreserved)
{
    QJsonObject base{
        {"input_device", QJsonValue::Null},
        {"language", "en"}
    };
    QJsonObject override{
        {"input_device", QJsonValue::Null}  // explicitly null
    };

    QJsonObject result = Config::deepMerge(base, override);

    EXPECT_TRUE(result["input_device"].isNull());
    EXPECT_EQ(result["language"].toString().toStdString(), "en");
}

TEST(ConfigDeepMerge, NestedObjectNewKey)
{
    QJsonObject base{
        {"osd", QJsonObject{{"x", 50}}}
    };
    QJsonObject override{
        {"osd", QJsonObject{{"z", 999}}}
    };

    QJsonObject result = Config::deepMerge(base, override);

    QJsonObject osd = result["osd"].toObject();
    EXPECT_EQ(osd["x"].toInt(), 50);   // preserved from base
    EXPECT_EQ(osd["z"].toInt(), 999);  // added from override
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
    QJsonObject json{
        {"language", "pl"},
        {"volume_step", 10}
    };
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

    config.setInputDevice("");   // clear
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
    EXPECT_EQ(hk.volumeUp,   115);
    EXPECT_EQ(hk.volumeDown, 114);
    EXPECT_EQ(hk.mute,       113);

    config.setHotkeys(200, 201, 202);
    hk = config.hotkeys();
    EXPECT_EQ(hk.volumeUp,   200);
    EXPECT_EQ(hk.volumeDown, 201);
    EXPECT_EQ(hk.mute,       202);

    // Persisted
    Config config2(tmp.path());
    hk = config2.hotkeys();
    EXPECT_EQ(hk.volumeUp,   200);
    EXPECT_EQ(hk.volumeDown, 201);
    EXPECT_EQ(hk.mute,       202);
}

TEST(Config, OsdRoundtrip)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());

    auto osd = config.osd();
    EXPECT_EQ(osd.x,         50);
    EXPECT_EQ(osd.y,         1150);
    EXPECT_EQ(osd.timeoutMs, 1200);
    EXPECT_EQ(osd.opacity,   85);
    EXPECT_EQ(osd.screen,    0);

    OsdConfig newOsd;
    newOsd.x         = 100;
    newOsd.y         = 200;
    newOsd.timeoutMs = 800;
    newOsd.opacity   = 50;
    newOsd.screen    = 2;
    newOsd.colorBg   = "#000000";
    newOsd.colorText = "#FF0000";
    newOsd.colorBar  = "#00FF00";
    config.setOsd(newOsd);

    auto r = config.osd();
    EXPECT_EQ(r.x,         100);
    EXPECT_EQ(r.y,         200);
    EXPECT_EQ(r.timeoutMs, 800);
    EXPECT_EQ(r.opacity,   50);
    EXPECT_EQ(r.screen,    2);

    // Persisted
    Config config2(tmp.path());
    r = config2.osd();
    EXPECT_EQ(r.x,         100);
    EXPECT_EQ(r.y,         200);
}

TEST(Config, UpdateOsdPartial)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    Config config(tmp.path());

    config.updateOsd(3, 900, 300, 400, 70, "#AAA", "#BBB", "#CCC");

    auto r = config.osd();
    EXPECT_EQ(r.screen,    3);
    EXPECT_EQ(r.timeoutMs, 900);
    EXPECT_EQ(r.x,         300);
    EXPECT_EQ(r.y,         400);
    EXPECT_EQ(r.opacity,   70);
    EXPECT_EQ(r.colorBg.toStdString(),   "#AAA");
    EXPECT_EQ(r.colorText.toStdString(), "#BBB");
    EXPECT_EQ(r.colorBar.toStdString(),  "#CCC");

    // Persisted
    Config config2(tmp.path());
    r = config2.osd();
    EXPECT_EQ(r.screen,    3);
    EXPECT_EQ(r.colorBg.toStdString(), "#AAA");
}
