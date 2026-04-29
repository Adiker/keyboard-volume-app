#include "appselectordialog.h"
#include "config.h"
#include "i18n.h"
#include "applistwidget.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QDialogButtonBox>

AppSelectorDialog::AppSelectorDialog(Config *config, QWidget *parent)
    : QDialog(parent)
    , m_config(config)
{
    setWindowTitle(::tr(QStringLiteral("app_selector.title")));
    setMinimumWidth(360);
    setWindowModality(Qt::ApplicationModal);

    auto *layout = new QVBoxLayout(this);

    auto *subtitle = new QLabel(::tr(QStringLiteral("app_selector.subtitle")), this);
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle);

    m_appList = new AppListWidget(this);
    layout->addWidget(m_appList);

    m_appList->populate(m_config);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttons, &QDialogButtonBox::accepted, this, [this]() {
        QString name = m_appList->selectedAppName();
        m_config->setSelectedApp(name);
        accept();
    });
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttons);
}
