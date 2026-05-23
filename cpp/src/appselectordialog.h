#pragma once
#include <QDialog>
#include <QString>

class Config;
class AppListWidget;
class QDialogButtonBox;

class AppSelectorDialog : public QDialog
{
    Q_OBJECT
  public:
    explicit AppSelectorDialog(Config* config, QWidget* parent = nullptr);
    QString selectedAppName() const;

  private:
    Config* m_config;
    AppListWidget* m_appList = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
    QString m_selectedApp;
};
