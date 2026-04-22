#pragma once
#include <QObject>
#include <QMap>

class QSystemTrayIcon;
class QMenu;
class QActionGroup;
class QAction;
class Config;
class VolumeController;
class InputHandler;

// System tray icon with context menu.
// Emits signals for device/settings changes and OSD previews.
class TrayApp : public QObject
{
    Q_OBJECT
public:
    explicit TrayApp(Config *config,
                     VolumeController *volumeCtrl,
                     InputHandler *inputHandler,
                     QObject *parent = nullptr);

    // Return the name of the currently selected audio app (may be empty).
    QString currentApp() const;

    // Rebuild the tray menu — call after language change.
    void rebuildMenu();

signals:
    void appChanged(const QString &name);
    void deviceChangeRequested();
    void settingsChanged();
    void osdPreviewRequested(int screenIdx, int x, int y);
    void osdPreviewFinished();
    void osdStylePreviewRequested(const QString &colorBg, const QString &colorText,
                                  const QString &colorBar, int opacity);
    void osdPreviewHeldRequested(int screenIdx, int x, int y);
    void osdPreviewReleased(int timeoutMs);

private slots:
    void onRefresh();
    void onAppSelected(const QString &name);
    void openSettings();

private:
    void buildMenu();
    void populateAppList();

    Config           *m_config;
    VolumeController *m_volumeCtrl;
    InputHandler     *m_inputHandler;

    QSystemTrayIcon  *m_tray      = nullptr;
    QMenu            *m_menu      = nullptr;
    QActionGroup     *m_appGroup  = nullptr;
    QMap<QString, QAction *> m_appActions;
};
