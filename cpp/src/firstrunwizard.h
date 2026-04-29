#pragma once
#include <QWizard>

class Config;
class QListWidget;
class QComboBox;
class AppListWidget;

class FirstRunWizard : public QWizard
{
    Q_OBJECT
public:
    explicit FirstRunWizard(Config *config, QWidget *parent = nullptr);

private:
    Config *m_config;
};

class WelcomePage : public QWizardPage
{
    Q_OBJECT
public:
    explicit WelcomePage(Config *config, QWidget *parent = nullptr);

    bool validatePage() override;

private:
    Config *m_config;
    QComboBox *m_langCombo = nullptr;
};

class DevicePage : public QWizardPage
{
    Q_OBJECT
public:
    explicit DevicePage(Config *config, QWidget *parent = nullptr);

    void initializePage() override;
    bool validatePage() override;

private:
    Config *m_config;
    QListWidget *m_list = nullptr;
};

class AppPage : public QWizardPage
{
    Q_OBJECT
public:
    explicit AppPage(Config *config, QWidget *parent = nullptr);

    void initializePage() override;
    bool validatePage() override;

private:
    Config         *m_config;
    AppListWidget  *m_appList = nullptr;
};
