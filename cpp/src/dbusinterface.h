#pragma once
#include <QObject>
#include <QString>
#include <QStringList>

class Config;
class VolumeController;
class TrayApp;

class DbusInterface : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.keyboardvolumeapp.VolumeControl")

    Q_PROPERTY(double Volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool Muted READ isMuted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(QString ActiveApp READ activeApp WRITE setActiveApp NOTIFY activeAppChanged)
    Q_PROPERTY(QStringList Apps READ apps NOTIFY appsUpdated)
    Q_PROPERTY(int VolumeStep READ volumeStep WRITE setVolumeStep)

public:
    explicit DbusInterface(Config *config,
                           VolumeController *volumeCtrl,
                           TrayApp *tray,
                           QObject *parent = nullptr);

    double      volume() const     { return m_volume; }
    bool        isMuted() const    { return m_muted; }
    QString     activeApp() const  { return m_activeApp; }
    QStringList apps() const       { return m_apps; }
    int         volumeStep() const { return m_volumeStep; }

    void setVolume(double vol);
    void setMuted(bool muted);
    void setActiveApp(const QString &name);
    void setVolumeStep(int step);

public slots:
    Q_SCRIPTABLE void VolumeUp();
    Q_SCRIPTABLE void VolumeDown();
    Q_SCRIPTABLE void ToggleMute();
    Q_SCRIPTABLE void RefreshApps();

signals:
    void volumeChanged(double vol);
    void mutedChanged(bool muted);
    void activeAppChanged(const QString &name);
    void appsUpdated();

private:
    Config           *m_config     = nullptr;
    VolumeController *m_volumeCtrl = nullptr;
    TrayApp          *m_tray       = nullptr;

    double      m_volume      = 0.0;
    bool        m_muted       = false;
    QString     m_activeApp;
    QStringList m_apps;
    int         m_volumeStep  = 5;
};
