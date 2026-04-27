#include "firstrunwizard.h"
#include "config.h"
#include "i18n.h"
#include "inputhandler.h"

#include <QLabel>
#include <QComboBox>
#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>
#include <QHBoxLayout>

FirstRunWizard::FirstRunWizard(Config *config, QWidget *parent)
    : QWizard(parent), m_config(config)
{
    setWindowTitle(QStringLiteral("Keyboard Volume App"));
    setMinimumSize(500, 380);
    setWizardStyle(QWizard::ModernStyle);

    addPage(new WelcomePage(config, this));
    addPage(new DevicePage(config, this));
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
