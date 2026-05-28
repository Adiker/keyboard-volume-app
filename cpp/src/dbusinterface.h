#pragma once
#include "config.h" // Profile
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class VolumeController;
class MprisClient;

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
    Q_PROPERTY(QVariantList Scenes READ scenesProp NOTIFY scenesChanged)
    Q_PROPERTY(QVariantList Sinks READ sinksProp NOTIFY sinksChanged)
    Q_PROPERTY(bool ProgressEnabled READ progressEnabled WRITE setProgressEnabled NOTIFY
                   progressEnabledChanged)
    Q_PROPERTY(bool AutoProfileSwitch READ autoProfileSwitch WRITE setAutoProfileSwitch NOTIFY
                   autoProfileSwitchChanged)

  public:
    explicit DbusInterface(Config* config, VolumeController* volumeCtrl, QObject* parent = nullptr);

    // Wire an MprisClient instance for the Media* methods. Optional — if not
    // set, all Media* methods are silent no-ops (graceful degradation when
    // the app is built/run without MPRIS support). Caller retains ownership.
    void setMprisClient(MprisClient* client);

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
    QVariantList scenesProp() const
    {
        return m_scenesProp;
    }
    QVariantList sinksProp() const
    {
        return m_sinksProp;
    }
    bool progressEnabled() const;
    bool autoProfileSwitch() const;

    void setVolume(double vol);
    void setMuted(bool muted);
    void setActiveApp(const QString& name);
    void setVolumeStep(int step);
    void setProgressEnabled(bool on);
    void setAutoProfileSwitch(bool on);

    // Re-read from Config and emit the corresponding signal if the value changed.
    void reloadProfiles();
    void reloadProgressEnabled();
    void reloadAutoProfileSwitch();
    // Rebuild Sinks property from VolumeController's cached list. Called when
    // VolumeController::sinksReady fires.
    void reloadSinks();

    // Update the cached active-app state when the tray (or any other source)
    // changes the selected application. Wired up by main.cpp.
    void onActiveAppChanged(const QString& name);

  public slots:
    Q_SCRIPTABLE void VolumeUp();
    Q_SCRIPTABLE void VolumeDown();
    Q_SCRIPTABLE void ToggleMute();
    Q_SCRIPTABLE void SetMute(bool muted);
    Q_SCRIPTABLE void ToggleDucking();
    Q_SCRIPTABLE void RefreshApps();

    // Per-profile volume control. Routes to the profile's app regardless of
    // ActiveApp. Silently no-op if the profile is unknown or has no app.
    Q_SCRIPTABLE void VolumeUpProfile(const QString& profileId);
    Q_SCRIPTABLE void VolumeDownProfile(const QString& profileId);
    Q_SCRIPTABLE void ToggleMuteProfile(const QString& profileId);
    Q_SCRIPTABLE void SetMuteProfile(const QString& profileId, bool muted);
    Q_SCRIPTABLE void ToggleDuckingProfile(const QString& profileId);
    // Per-profile absolute volume. Routes to the profile's app (independent of
    // ActiveApp). Silently no-op for unknown profiles.
    Q_SCRIPTABLE void SetVolumeProfile(const QString& profileId, double vol);
    Q_SCRIPTABLE void ApplyScene(const QString& sceneId);
    Q_SCRIPTABLE void ShowVolume();
    Q_SCRIPTABLE void ShowVolumeProfile(const QString& profileId);

    // Route an app's active sink-input(s) to the named PA sink, and persist via
    // stream-restore. Empty arguments are no-ops.
    Q_SCRIPTABLE void SetAppSink(const QString& app, const QString& sink);

    // Media controls — relayed to MprisClient when one is wired. Each is a
    // no-op when no MprisClient is set or when no MPRIS player is active.
    Q_SCRIPTABLE void MediaPlayPause();
    Q_SCRIPTABLE void MediaNext();
    Q_SCRIPTABLE void MediaPrevious();
    Q_SCRIPTABLE void MediaStop();

  signals:
    void volumeChanged(double vol);
    void mutedChanged(bool muted);
    void activeAppChanged(const QString& name);
    void appsUpdated();
    void profilesChanged(const QVariantList& profiles);
    void scenesChanged(const QVariantList& scenes);
    void sinksChanged(const QVariantList& sinks);
    void progressEnabledChanged(bool on);
    void autoProfileSwitchChanged(bool on);

  private:
    // Build the QVariantList wire representation from current Config profiles.
    QVariantList buildProfilesProp() const;
    QVariantList buildScenesProp() const;
    // Resolve a profile id to a Profile (or empty Profile if unknown).
    Profile findProfile(const QString& id) const;

    Config* m_config = nullptr;
    VolumeController* m_volumeCtrl = nullptr;
    MprisClient* m_mpris = nullptr;

    double m_volume = 0.0;
    bool m_muted = false;
    QString m_activeApp;
    QStringList m_apps;
    int m_volumeStep = 5;
    bool m_progressEnabled = false;
    bool m_autoProfileSwitch = false;
    QVariantList m_profilesProp;
    QVariantList m_scenesProp;
    QVariantList m_sinksProp;
};
