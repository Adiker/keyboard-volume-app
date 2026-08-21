#include <gtest/gtest.h>

#include "aboutdialog.h"
#include "i18n.h"

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QTabWidget>

#ifndef PROJECT_LICENSE_PATH
#define PROJECT_LICENSE_PATH ""
#endif

namespace
{

template <typename T> T* requiredChild(const AboutDialog& dialog, const char* objectName)
{
    T* child = dialog.findChild<T*>(QString::fromUtf8(objectName));
    EXPECT_NE(child, nullptr) << objectName;
    return child;
}

} // namespace

TEST(AboutDialog, ShowsEnglishProjectInformation)
{
    setLanguage(QStringLiteral("en"));
    QApplication::setApplicationVersion(QStringLiteral("9.8.7-test"));

    AboutDialog dialog;

    EXPECT_EQ(dialog.windowTitle(), QStringLiteral("About Keyboard Volume App"));

    auto* tabs = requiredChild<QTabWidget>(dialog, "aboutTabs");
    ASSERT_NE(tabs, nullptr);
    ASSERT_EQ(tabs->count(), 2);
    EXPECT_EQ(tabs->tabText(0), QStringLiteral("About"));
    EXPECT_EQ(tabs->tabText(1), QStringLiteral("License"));

    auto* icon = requiredChild<QLabel>(dialog, "aboutIcon");
    ASSERT_NE(icon, nullptr);
    EXPECT_FALSE(icon->pixmap().isNull());

    auto* version = requiredChild<QLabel>(dialog, "aboutVersionValue");
    ASSERT_NE(version, nullptr);
    EXPECT_EQ(version->text(), QStringLiteral("9.8.7-test"));

    auto* author = requiredChild<QLabel>(dialog, "aboutAuthorLink");
    ASSERT_NE(author, nullptr);
    EXPECT_TRUE(author->text().contains(QStringLiteral("https://github.com/Adiker")));
    EXPECT_TRUE(author->text().contains(QStringLiteral("Adiker")));
    EXPECT_FALSE(author->text().contains(QLatin1Char('@')));
    EXPECT_TRUE(author->openExternalLinks());

    auto* project = requiredChild<QLabel>(dialog, "aboutProjectLink");
    ASSERT_NE(project, nullptr);
    EXPECT_TRUE(
        project->text().contains(QStringLiteral("https://github.com/Adiker/keyboard-volume-app")));
    EXPECT_TRUE(project->openExternalLinks());

    auto* licenseId = requiredChild<QLabel>(dialog, "aboutLicenseId");
    ASSERT_NE(licenseId, nullptr);
    EXPECT_EQ(licenseId->text(), QStringLiteral("GPL-2.0-or-later"));

    auto* licenseText = requiredChild<QPlainTextEdit>(dialog, "aboutLicenseText");
    ASSERT_NE(licenseText, nullptr);
    EXPECT_TRUE(licenseText->isReadOnly());
    EXPECT_TRUE(licenseText->toPlainText().contains(QStringLiteral("GNU GENERAL PUBLIC LICENSE")));
    EXPECT_TRUE(licenseText->toPlainText().contains(QStringLiteral("Version 2, June 1991")));

    auto* closeButton = requiredChild<QPushButton>(dialog, "aboutCloseButton");
    ASSERT_NE(closeButton, nullptr);
    EXPECT_EQ(closeButton->text(), QStringLiteral("Close"));
    closeButton->click();
    EXPECT_EQ(dialog.result(), QDialog::Rejected);
}

TEST(AboutDialog, UsesPolishTranslations)
{
    setLanguage(QStringLiteral("pl"));

    AboutDialog dialog;

    EXPECT_EQ(dialog.windowTitle(), QStringLiteral("O programie Keyboard Volume App"));

    auto* tabs = requiredChild<QTabWidget>(dialog, "aboutTabs");
    ASSERT_NE(tabs, nullptr);
    EXPECT_EQ(tabs->tabText(0), QStringLiteral("Informacje"));
    EXPECT_EQ(tabs->tabText(1), QStringLiteral("Licencja"));

    auto* closeButton = requiredChild<QPushButton>(dialog, "aboutCloseButton");
    ASSERT_NE(closeButton, nullptr);
    EXPECT_EQ(closeButton->text(), QStringLiteral("Zamknij"));
}

TEST(AboutDialog, EmbeddedLicenseMatchesCanonicalFile)
{
    QFile embedded(QStringLiteral(":/license.txt"));
    ASSERT_TRUE(embedded.open(QIODevice::ReadOnly));

    QFile canonical(QStringLiteral(PROJECT_LICENSE_PATH));
    ASSERT_TRUE(canonical.open(QIODevice::ReadOnly));

    EXPECT_EQ(embedded.readAll(), canonical.readAll());
}

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
