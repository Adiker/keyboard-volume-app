#include <gtest/gtest.h>
#include <QString>
#include <QMap>

#include "i18n.h"

TEST(I18n, DefaultLanguageEnglish)
{
    EXPECT_EQ(currentLanguage().toStdString(), "en");
}

TEST(I18n, SwitchLanguage)
{
    setLanguage("pl");
    EXPECT_EQ(currentLanguage().toStdString(), "pl");

    setLanguage("en");
    EXPECT_EQ(currentLanguage().toStdString(), "en");
}

TEST(I18n, UnknownCodeIgnored)
{
    setLanguage("pl");
    setLanguage("xyz"); // unknown → ignored
    EXPECT_EQ(currentLanguage().toStdString(), "pl");
}

TEST(I18n, LookupExistingKey)
{
    setLanguage("en");
    EXPECT_EQ(tr("settings.title").toStdString(), "Settings");
    EXPECT_EQ(tr("settings.profiles.ducking_hotkey").toStdString(), "Focus audio hotkey:");

    setLanguage("pl");
    EXPECT_EQ(tr("settings.title").toStdString(), "Ustawienia");
    EXPECT_EQ(tr("settings.profiles.ducking_hotkey").toStdString(), "Skrót trybu skupienia:");
}

TEST(I18n, FallbackToRawKey)
{
    // A key missing from every language table returns itself.
    setLanguage("en");
    EXPECT_EQ(tr("nonexistent_key_xyz").toStdString(), "nonexistent_key_xyz");

    setLanguage("pl");
    EXPECT_EQ(tr("nonexistent_key_xyz").toStdString(), "nonexistent_key_xyz");
}

TEST(I18n, BothLanguagesGiveDifferentTranslations)
{
    setLanguage("en");
    QString en = tr("tray.action.quit");
    ASSERT_FALSE(en.isEmpty());

    setLanguage("pl");
    QString pl = tr("tray.action.quit");
    ASSERT_FALSE(pl.isEmpty());

    EXPECT_NE(en.toStdString(), pl.toStdString());
}

TEST(I18n, LanguagesList)
{
    const auto& langs = languages();
    ASSERT_GE(langs.size(), 2);
    EXPECT_TRUE(langs.contains("en"));
    EXPECT_TRUE(langs.contains("pl"));
    EXPECT_EQ(langs["en"].toStdString(), "English");
    EXPECT_EQ(langs["pl"].toStdString(), "Polski");
}
