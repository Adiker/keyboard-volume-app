#include "trayapp.h"
#include "config.h"
#include "volumecontroller.h"
#include "inputhandler.h"
#include "i18n.h"
#include "settingsdialog.h"
#include "appselectordialog.h"
#include "screenutils.h"

#include <QApplication>
#include <QCursor>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QActionGroup>
#include <QAction>
#include <QIcon>

TrayApp::TrayApp(Config* config, VolumeController* volumeCtrl, InputHandler* inputHandler,
                 QObject* parent)
    : QObject(parent), m_config(config), m_volumeCtrl(volumeCtrl), m_inputHandler(inputHandler)
{
    m_tray = new QSystemTrayIcon(QIcon(QStringLiteral(":/icon.png")), this);
    m_tray->setToolTip(QStringLiteral("Keyboard Volume App"));

    m_menu = new QMenu();
    m_appGroup = new QActionGroup(m_menu);
    m_appGroup->setExclusive(true);

    m_tray->setContextMenu(m_menu);

    // Rebuild menu whenever the PA worker delivers a fresh app list.
    connect(m_volumeCtrl, &VolumeController::appsReady, this, &TrayApp::rebuildMenu);

    buildMenu(); // builds from cache (may be empty on first call; appsReady will refresh)
    m_tray->show();
}

// ─── Menu ─────────────────────────────────────────────────────────────────────
void TrayApp::buildMenu()
{
    m_menu->clear();
    for (QAction* a : m_appGroup->actions()) m_appGroup->removeAction(a);
    m_appActions.clear();

    m_menu->addSection(::tr(QStringLiteral("tray.section.audio_app")));
    populateAppList();

    m_menu->addSeparator();

    QAction* refreshAct = m_menu->addAction(::tr(QStringLiteral("tray.action.refresh")));
    connect(refreshAct, &QAction::triggered, this, &TrayApp::onRefresh);

    QAction* changeAppAct = m_menu->addAction(::tr(QStringLiteral("tray.action.change_app")));
    connect(changeAppAct, &QAction::triggered, this, &TrayApp::openAppSelector);

    m_menu->addSeparator();

    QAction* deviceAct = m_menu->addAction(::tr(QStringLiteral("tray.action.change_device")));
    connect(deviceAct, &QAction::triggered, this, &TrayApp::deviceChangeRequested);

    QAction* settingsAct = m_menu->addAction(::tr(QStringLiteral("tray.action.settings")));
    connect(settingsAct, &QAction::triggered, this, &TrayApp::openSettings);

    m_menu->addSeparator();

    QAction* quitAct = m_menu->addAction(::tr(QStringLiteral("tray.action.quit")));
    connect(quitAct, &QAction::triggered, qApp, &QApplication::quit);
}

void TrayApp::populateAppList()
{
    const auto apps = m_volumeCtrl->listApps();

    // Insert before the first separator in the menu
    QAction* insertBefore = nullptr;
    for (QAction* a : m_menu->actions())
    {
        if (a->isSeparator())
        {
            insertBefore = a;
            break;
        }
    }

    for (const AudioApp& app : apps)
    {
        QAction* act = new QAction(app.name, m_menu);
        act->setCheckable(true);
        act->setData(app.name);
        m_appGroup->addAction(act);
        m_appActions[app.name] = act;

        if (insertBefore)
            m_menu->insertAction(insertBefore, act);
        else
            m_menu->addAction(act);

        if (app.name.compare(m_config->defaultProfile().app, Qt::CaseInsensitive) == 0 ||
            app.binary.compare(m_config->defaultProfile().app, Qt::CaseInsensitive) == 0)
            act->setChecked(true);

        connect(act, &QAction::triggered, this, [this, name = app.name]() { onAppSelected(name); });
    }

    // Keep the user's configured app even if a transient refresh/reconnect
    // produces a list that does not currently contain it.
}

void TrayApp::onRefresh()
{
    // Triggers an async PipeWire refresh in the PA worker thread.
    // Menu will be rebuilt automatically when appsReady fires.
    m_volumeCtrl->listApps(/*forceRefresh=*/true);
}

void TrayApp::onAppSelected(const QString& name)
{
    m_config->setDefaultProfileApp(name);
    auto it = m_appActions.find(name);
    if (it != m_appActions.end()) it.value()->setChecked(true);
    emit appChanged(name);
}

void TrayApp::openSettings()
{
    const QPoint anchor = QCursor::pos();
    SettingsDialog dlg(m_config, m_inputHandler);
    connect(&dlg, &SettingsDialog::positionPreview, this, &TrayApp::osdPreviewRequested);
    connect(&dlg, &SettingsDialog::stylePreview, this, &TrayApp::osdStylePreviewRequested);
    connect(&dlg, &SettingsDialog::previewHeldRequested, this, &TrayApp::osdPreviewHeldRequested);
    connect(&dlg, &SettingsDialog::previewReleased, this, &TrayApp::osdPreviewReleased);
    connect(&dlg, &QDialog::finished, this, [this](int) { emit osdPreviewFinished(); });
    centerDialogOnScreenAt(&dlg, anchor);
    dlg.exec();
    // Reload styles regardless of accepted/rejected so preview colors revert
    emit settingsChanged();
}

void TrayApp::openAppSelector()
{
    const QPoint anchor = QCursor::pos();
    AppSelectorDialog dlg(m_config);
    centerDialogOnScreenAt(&dlg, anchor);
    if (dlg.exec() == QDialog::Accepted) onAppSelected(m_config->defaultProfile().app);
}

void TrayApp::rebuildMenu()
{
    buildMenu();
}

QString TrayApp::currentApp() const
{
    return m_config->defaultProfile().app;
}
