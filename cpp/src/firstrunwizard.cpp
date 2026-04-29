#include "firstrunwizard.h"
#include "config.h"
#include "i18n.h"
#include "inputhandler.h"
#include "pwutils.h"

#include <QLabel>
#include <QComboBox>
#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>

FirstRunWizard::FirstRunWizard(Config *config, QWidget *parent)
    : QWizard(parent), m_config(config)
{
    setWindowTitle(QStringLiteral("Keyboard Volume App"));
    setMinimumSize(500, 380);
    setWizardStyle(QWizard::ModernStyle);

    addPage(new WelcomePage(config, this));
    addPage(new DevicePage(config, this));
    addPage(new AppPage(config, this));
}

WelcomePage::WelcomePage(Config *config, QWidget *parent)
    : QWizardPage(parent), m_config(config)
{
    setTitle(::tr(QStringLiteral("wizard.welcome_title")));

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(16);

    auto *label = new QLabel(::tr(QStringLiteral("wizard.welcome_text")), this);
    label->setWordWrap(true);
    layout->addWidget(label);

    layout->addSpacing(8);

    auto *langLayout = new QHBoxLayout;
    langLayout->addWidget(new QLabel(::tr(QStringLiteral("wizard.lang_label")), this));

    m_langCombo = new QComboBox(this);
    const auto langs = languages();
    for (auto it = langs.begin(); it != langs.end(); ++it) {
        m_langCombo->addItem(it.value(), it.key());
    }
    int idx = m_langCombo->findData(m_config->language());
    if (idx >= 0)
        m_langCombo->setCurrentIndex(idx);
    langLayout->addWidget(m_langCombo);
    langLayout->addStretch();

    layout->addLayout(langLayout);
    layout->addStretch();
}

bool WelcomePage::validatePage()
{
    QString code = m_langCombo->currentData().toString();
    if (!code.isEmpty())
        m_config->setLanguage(code);
    return true;
}

DevicePage::DevicePage(Config *config, QWidget *parent)
    : QWizardPage(parent), m_config(config)
{
    setTitle(::tr(QStringLiteral("wizard.device_title")));
    setSubTitle(::tr(QStringLiteral("device.label")));

    auto *layout = new QVBoxLayout(this);

    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(true);
    layout->addWidget(m_list);
}

void DevicePage::initializePage()
{
    m_list->clear();
    const auto devices = getVolumeDevices();
    if (devices.isEmpty()) {
        m_list->addItem(new QListWidgetItem(::tr(QStringLiteral("device.no_devices"))));
        return;
    }
    for (const auto &[path, description] : devices) {
        auto *item = new QListWidgetItem(description);
        item->setData(Qt::UserRole, path);
        m_list->addItem(item);
    }
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
}

bool DevicePage::validatePage()
{
    auto *item = m_list->currentItem();
    if (item) {
        QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty()) {
            m_config->setInputDevice(path);
            return true;
        }
    }
    return true;
}

// ─── AppPage ─────────────────────────────────────────────────────────────────
AppPage::AppPage(Config *config, QWidget *parent)
    : QWizardPage(parent)
    , m_config(config)
{
    setTitle(::tr(QStringLiteral("wizard.app_title")));
    setSubTitle(::tr(QStringLiteral("wizard.app_subtitle")));

    auto *layout = new QVBoxLayout(this);

    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(true);
    layout->addWidget(m_list);

    m_refreshBtn = new QPushButton(::tr(QStringLiteral("wizard.app_refresh")), this);
    connect(m_refreshBtn, &QPushButton::clicked, this, &AppPage::onRefresh);
    layout->addWidget(m_refreshBtn);
}

void AppPage::initializePage()
{
    populateList();
}

bool AppPage::validatePage()
{
    auto *item = m_list->currentItem();
    if (item) {
        QString name = item->data(Qt::UserRole).toString();
        m_config->setSelectedApp(name);
    }
    return true;
}

void AppPage::onRefresh()
{
    populateList();
}

void AppPage::populateList()
{
    m_list->clear();

    // "No default application" — always first, always enabled
    auto *emptyItem = new QListWidgetItem(::tr(QStringLiteral("wizard.app_empty")));
    emptyItem->setData(Qt::UserRole, QString{});
    m_list->addItem(emptyItem);

    const auto clients = ::listPipeWireClients();
    if (clients.isEmpty()) {
        auto *noAppsItem = new QListWidgetItem(::tr(QStringLiteral("wizard.app_no_apps")));
        noAppsItem->setFlags(noAppsItem->flags() & ~Qt::ItemIsEnabled);
        m_list->addItem(noAppsItem);
    } else {
        for (const PipeWireClient &client : clients) {
            auto *item = new QListWidgetItem(client.name);
            item->setData(Qt::UserRole, client.name);
            m_list->addItem(item);
        }
    }

    // Select matching item from config
    const QString selected = m_config->selectedApp();
    int targetRow = 0; // default: "No default application"
    if (!selected.isEmpty()) {
        for (int i = 0; i < m_list->count(); ++i) {
            if (m_list->item(i)->data(Qt::UserRole).toString() == selected) {
                targetRow = i;
                break;
            }
        }
    }
    if (targetRow < m_list->count())
        m_list->setCurrentRow(targetRow);
}
