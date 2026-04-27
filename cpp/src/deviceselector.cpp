#include "deviceselector.h"
#include "config.h"
#include "i18n.h"
#include "inputhandler.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QDialogButtonBox>

// ─── DeviceSelectorDialog ─────────────────────────────────────────────────────
DeviceSelectorDialog::DeviceSelectorDialog(Config *config, bool firstRun, QWidget *parent)
    : QDialog(parent), m_config(config)
{
    setWindowTitle(::tr(firstRun
        ? QStringLiteral("device.title.first_run")
        : QStringLiteral("device.title")));
    setMinimumWidth(460);
    setWindowModality(Qt::ApplicationModal);

    buildUi(firstRun);
    populate();
}

void DeviceSelectorDialog::buildUi(bool /*firstRun*/)
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setSpacing(8);

    layout->addWidget(new QLabel(::tr(QStringLiteral("device.label")), this));

    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(true);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &DeviceSelectorDialog::accept);
    layout->addWidget(m_list);

    QHBoxLayout *btnRow = new QHBoxLayout;
    QPushButton *refreshBtn = new QPushButton(::tr(QStringLiteral("device.btn.refresh")), this);
    connect(refreshBtn, &QPushButton::clicked, this, &DeviceSelectorDialog::populate);
    btnRow->addWidget(refreshBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &DeviceSelectorDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &DeviceSelectorDialog::reject);
    layout->addWidget(buttons);
}

void DeviceSelectorDialog::populate()
{
    m_list->clear();
    const auto devices = getVolumeDevices();
    if (devices.isEmpty()) {
        m_list->addItem(new QListWidgetItem(::tr(QStringLiteral("device.no_devices"))));
        return;
    }
    for (const auto &[path, description] : devices) {
        QListWidgetItem *item = new QListWidgetItem(description);
        item->setData(Qt::UserRole, path);
        m_list->addItem(item);
        if (path == m_config->inputDevice())
            m_list->setCurrentItem(item);
    }
    if (!m_list->currentItem())
        m_list->setCurrentRow(0);
}

void DeviceSelectorDialog::accept()
{
    QListWidgetItem *item = m_list->currentItem();
    if (item) {
        QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty()) {
            m_selectedPath = path;
            m_config->setInputDevice(path);
        }
    }
    QDialog::accept();
}
