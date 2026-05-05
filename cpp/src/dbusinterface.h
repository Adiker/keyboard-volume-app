#pragma once
#include "config.h" // Profile
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

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
    Q_PROPERTY(QVariantList Profiles READ profilesProp NOTIFY profilesChanged)

  public:
    explicit DbusInterface(Config* config, VolumeController* volumeCtrl, TrayApp* tray,
                           QObject* parent = nullptr);

    double volume() const
    {
        return m_volume;
    }
    bool isMuted() const
    {
        return m_muted;
    }
    QString activeApp() const
    {
        return m_activeApp;
    }
    QStringList apps() const
    {
        return m_apps;
    }
    int volumeStep() const
    {
        return m_volumeStep;
    }
    QVariantList profilesProp() const
    {
        return m_profilesProp;
    }

    void setVolume(double vol);
    void setMuted(bool muted);
    void setActiveApp(const QString& name);
    void setVolumeStep(int step);

    // Re-read profiles from Config and emit profilesChanged() if different.
    void reloadProfiles();

  public slots:
    Q_SCRIPTABLE void VolumeUp();
    Q_SCRIPTABLE void VolumeDown();
    Q_SCRIPTABLE void ToggleMute();
    Q_SCRIPTABLE void ToggleDucking();
    Q_SCRIPTABLE void RefreshApps();

    // Per-profile volume control. Routes to the profile's app regardless of
    // ActiveApp. Silently no-op if the profile is unknown or has no app.
    Q_SCRIPTABLE void VolumeUpProfile(const QString& profileId);
    Q_SCRIPTABLE void VolumeDownProfile(const QString& profileId);
    Q_SCRIPTABLE void ToggleMuteProfile(const QString& profileId);
    Q_SCRIPTABLE void ToggleDuckingProfile(const QString& profileId);

  signals:
    void volumeChanged(double vol);
    void mutedChanged(bool muted);
    void activeAppChanged(const QString& name);
    void appsUpdated();
    void profilesChanged(const QVariantList& profiles);

  private:
    // Build the QVariantList wire representation from current Config profiles.
    QVariantList buildProfilesProp() const;
    // Resolve a profile id to a Profile (or empty Profile if unknown).
    Profile findProfile(const QString& id) const;

    Config* m_config = nullptr;
    VolumeController* m_volumeCtrl = nullptr;
    TrayApp* m_tray = nullptr;

    double m_volume = 0.0;
    bool m_muted = false;
    QString m_activeApp;
    QStringList m_apps;
    int m_volumeStep = 5;
    QVariantList m_profilesProp;
};
