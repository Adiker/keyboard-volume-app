#pragma once
#include <QDialog>
#include <QString>

class QListWidget;
class Config;

// Dialog that lets the user pick an evdev input device.
// Filters /dev/input/event* to show only devices exposing
// KEY_VOLUMEUP and/or KEY_VOLUMEDOWN.
class DeviceSelectorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DeviceSelectorDialog(Config *config,
                                  bool firstRun = false,
                                  QWidget *parent = nullptr);

    // Path chosen by the user (set on accept).
    QString selectedPath() const { return m_selectedPath; }

protected:
    void accept() override;

private:
    void buildUi(bool firstRun);
    void populate();

    Config       *m_config;
    QListWidget  *m_list = nullptr;
    QString       m_selectedPath;
};
