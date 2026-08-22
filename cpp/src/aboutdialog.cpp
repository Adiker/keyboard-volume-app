#include "aboutdialog.h"

#include "i18n.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

namespace
{

QLabel* linkLabel(const QString& text, const QString& url, QWidget* parent)
{
    auto* label = new QLabel(
        QStringLiteral("<a href=\"%1\">%2</a>").arg(url.toHtmlEscaped(), text.toHtmlEscaped()),
        parent);
    label->setTextFormat(Qt::RichText);
    label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    label->setOpenExternalLinks(true);
    return label;
}

QString licenseText()
{
    QFile licenseFile(QStringLiteral(":/license.txt"));
    if (!licenseFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return ::tr(QStringLiteral("about.license_load_error"));

    return QString::fromUtf8(licenseFile.readAll());
}

} // namespace

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(::tr(QStringLiteral("about.title")));
    setWindowIcon(QIcon(QStringLiteral(":/icon.png")));
    setWindowModality(Qt::ApplicationModal);
    setMinimumSize(560, 440);
    resize(680, 520);

    auto* rootLayout = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("aboutTabs"));

    auto* infoTab = new QWidget(tabs);
    auto* infoLayout = new QVBoxLayout(infoTab);

    auto* headerLayout = new QHBoxLayout();
    auto* icon = new QLabel(infoTab);
    icon->setObjectName(QStringLiteral("aboutIcon"));
    icon->setPixmap(QIcon(QStringLiteral(":/icon.png")).pixmap(72, 72));
    icon->setFixedSize(72, 72);
    headerLayout->addWidget(icon, 0, Qt::AlignTop);

    auto* titleLayout = new QVBoxLayout();
    auto* name = new QLabel(QStringLiteral("Keyboard Volume App"), infoTab);
    name->setObjectName(QStringLiteral("aboutName"));
    QFont nameFont = name->font();
    nameFont.setBold(true);
    nameFont.setPointSize(nameFont.pointSize() + 4);
    name->setFont(nameFont);
    titleLayout->addWidget(name);

    auto* description = new QLabel(::tr(QStringLiteral("about.description")), infoTab);
    description->setObjectName(QStringLiteral("aboutDescription"));
    description->setWordWrap(true);
    titleLayout->addWidget(description);
    titleLayout->addStretch();
    headerLayout->addLayout(titleLayout, 1);
    infoLayout->addLayout(headerLayout);

    auto* details = new QFormLayout();
    auto* version = new QLabel(QApplication::applicationVersion(), infoTab);
    version->setObjectName(QStringLiteral("aboutVersionValue"));
    details->addRow(::tr(QStringLiteral("about.version")), version);

    auto* author =
        linkLabel(QStringLiteral("Adiker"), QStringLiteral("https://github.com/Adiker"), infoTab);
    author->setObjectName(QStringLiteral("aboutAuthorLink"));
    details->addRow(::tr(QStringLiteral("about.author")), author);

    auto* project =
        linkLabel(QStringLiteral("github.com/Adiker/keyboard-volume-app"),
                  QStringLiteral("https://github.com/Adiker/keyboard-volume-app"), infoTab);
    project->setObjectName(QStringLiteral("aboutProjectLink"));
    details->addRow(::tr(QStringLiteral("about.project")), project);

    auto* licenseId = new QLabel(QStringLiteral("GPL-2.0-or-later"), infoTab);
    licenseId->setObjectName(QStringLiteral("aboutLicenseId"));
    licenseId->setTextInteractionFlags(Qt::TextSelectableByMouse);
    details->addRow(::tr(QStringLiteral("about.license")), licenseId);
    infoLayout->addLayout(details);
    infoLayout->addStretch();

    auto* licenseTab = new QWidget(tabs);
    auto* licenseLayout = new QVBoxLayout(licenseTab);
    auto* licenseSummary = new QLabel(::tr(QStringLiteral("about.license_summary")), licenseTab);
    licenseSummary->setObjectName(QStringLiteral("aboutLicenseSummary"));
    licenseSummary->setWordWrap(true);
    licenseLayout->addWidget(licenseSummary);

    auto* licenseView = new QPlainTextEdit(licenseTab);
    licenseView->setObjectName(QStringLiteral("aboutLicenseText"));
    licenseView->setReadOnly(true);
    licenseView->setLineWrapMode(QPlainTextEdit::NoWrap);
    licenseView->setPlainText(licenseText());
    licenseLayout->addWidget(licenseView, 1);

    tabs->addTab(infoTab, ::tr(QStringLiteral("about.tab.info")));
    tabs->addTab(licenseTab, ::tr(QStringLiteral("about.tab.license")));
    rootLayout->addWidget(tabs, 1);

    auto* buttons = new QDialogButtonBox(this);
    auto* closeButton =
        buttons->addButton(::tr(QStringLiteral("about.close")), QDialogButtonBox::RejectRole);
    closeButton->setObjectName(QStringLiteral("aboutCloseButton"));
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
    rootLayout->addWidget(buttons);
}
