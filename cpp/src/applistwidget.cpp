#include "applistwidget.h"
#include "config.h"
#include "i18n.h"
#include "pwutils.h"

#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

AppListWidget::AppListWidget(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(true);
    layout->addWidget(m_list);

    m_refreshBtn = new QPushButton(::tr(QStringLiteral("app_selector.refresh")), this);
    connect(m_refreshBtn, &QPushButton::clicked, this, &AppListWidget::onRefresh);
    layout->addWidget(m_refreshBtn);
}

void AppListWidget::populate(Config* config)
{
    m_config = config;
    m_list->clear();

    // "No default application" — always first, always enabled
    auto* emptyItem = new QListWidgetItem(::tr(QStringLiteral("app_selector.no_default")));
    emptyItem->setData(Qt::UserRole, QString{});
    m_list->addItem(emptyItem);

    const auto clients = ::listPipeWireClients();
    if (clients.isEmpty())
    {
        auto* noAppsItem = new QListWidgetItem(::tr(QStringLiteral("app_selector.no_apps")));
        noAppsItem->setFlags(noAppsItem->flags() & ~Qt::ItemIsEnabled);
        m_list->addItem(noAppsItem);
    }
    else
    {
        for (const PipeWireClient& client : clients)
        {
            auto* item = new QListWidgetItem(client.name);
            item->setData(Qt::UserRole, client.binary);
            m_list->addItem(item);
        }
    }

    // Select matching item from config
    if (m_config)
    {
        const QString selected = m_config->selectedApp();
        int targetRow = 0; // default: "No default application"
        if (!selected.isEmpty())
        {
            for (int i = 0; i < m_list->count(); ++i)
            {
                QListWidgetItem* item = m_list->item(i);
                if (item->data(Qt::UserRole).toString().compare(selected, Qt::CaseInsensitive) ==
                        0 ||
                    item->text().compare(selected, Qt::CaseInsensitive) == 0)
                {
                    targetRow = i;
                    break;
                }
            }
        }
        if (targetRow < m_list->count()) m_list->setCurrentRow(targetRow);
    }
}

QString AppListWidget::selectedAppName() const
{
    auto* item = m_list->currentItem();
    if (!item) return {};
    return item->data(Qt::UserRole).toString();
}

void AppListWidget::setSelectedApp(const QString& name)
{
    for (int i = 0; i < m_list->count(); ++i)
    {
        QListWidgetItem* item = m_list->item(i);
        if (item->data(Qt::UserRole).toString().compare(name, Qt::CaseInsensitive) == 0 ||
            item->text().compare(name, Qt::CaseInsensitive) == 0)
        {
            m_list->setCurrentRow(i);
            return;
        }
    }
    m_list->setCurrentRow(0); // fallback: "No default application"
}

void AppListWidget::onRefresh()
{
    if (m_config) populate(m_config);
}
