#pragma once
#include <QWidget>

class QListWidget;
class QPushButton;
class Config;
struct PipeWireClient;

// Reusable widget: PipeWire audio app list with Refresh button.
// Shared between AppPage (first-run wizard) and AppSelectorDialog (tray).
class AppListWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AppListWidget(QWidget *parent = nullptr);

    // Repopulate the list from pw-dump and highlight the saved selection.
    void populate(Config *config);

    // Returns the currently selected app name, or "" for "No default".
    QString selectedAppName() const;

    // Select the item with the given Qt::UserRole data.
    void setSelectedApp(const QString &name);

private slots:
    void onRefresh();

private:
    QListWidget *m_list       = nullptr;
    QPushButton *m_refreshBtn = nullptr;
    Config      *m_config     = nullptr;  // set by populate()
};
