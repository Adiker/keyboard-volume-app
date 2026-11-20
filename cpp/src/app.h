#pragma once
#include "audioapp.h"
#include "config.h"
#include "inputhandler.h"

#include <QList>
#include <QObject>
#include <QString>
#include <memory>

class AlbumArtCache;
class DbusInterface;
class MprisClient;
class OSDWindow;
class TrayApp;
class VolumeController;
class WindowTracker;

OsdLayoutInputConfig osdLayoutInputFromConfig(const Config* config);

struct AppInitOptions
{
    // Optional externally owned VolumeController (for unit/integration tests).
    // When null, App constructs and owns a production VolumeController.
    VolumeController* volumeCtrl = nullptr;
    bool startInputDevice = true;
    bool startWindowTracker = true;
    bool registerDbus = true;
    QString dbusServiceName = QStringLiteral("org.keyboardvolumeapp");
    QString mprisServiceName = QStringLiteral("org.mpris.MediaPlayer2.keyboardvolumeapp");
};

// Root coordinator — wires all modules via Qt signals.
class App : public QObject
{
    Q_OBJECT
  public:
    explicit App(std::unique_ptr<Config> config = std::make_unique<Config>(),
                 QObject* parent = nullptr);
    ~App() override;

    Config* config() const
    {
        return m_config.get();
    }
    VolumeController* volumeController() const
    {
        return m_volumeCtrl;
    }
    OSDWindow* osdWindow() const
    {
        return m_osd;
    }
    InputHandler* inputHandler() const
    {
        return m_input;
    }
    TrayApp* trayApp() const
    {
        return m_tray;
    }
    MprisClient* mprisClient() const
    {
        return m_mpris;
    }
    DbusInterface* dbusInterface() const
    {
        return m_dbus;
    }
    bool isMprisRegistered() const
    {
        return m_mprisRegistered;
    }
    QString autoActiveApp() const
    {
        return m_autoActiveApp;
    }

    void init(const AppInitOptions& options = {});
    void cleanup();

    QString effectiveApp(const QString& profileId) const;
    Profile effectiveProfile(const QString& profileId) const;

  public slots:
    void onHotkeysMaybeChanged();
    void onAutoSwitchMaybeChanged();
    void onMprisExposureChanged();
    void onFocusedBinaryChanged(const QString& binary);

  private:
    void connectSignals();
    void initDevice();
    void updateFocusedActiveApp();
    Profile findProfile(const QString& profileId) const;
    void activateProfile(const Profile& p, const QString& alsoRouteApp = {});
    void changeVolume(const QString& profileId, int direction);
    void onMute(const QString& profileId);
    void onDuckingToggle(const QString& profileId);
    void onShowVolume(const QString& profileId);
    void showMediaKeyOsd(const QString& actionLabel);
    void onDeviceChangeRequested(bool startup);
    void registerMprisEndpoint();
    void unregisterMprisEndpoint();
    void initDbus();
    void initWindowTracker();
    void startWindowTracker();
    void stopWindowTracker();

    std::unique_ptr<Config> m_config;
    VolumeController* m_volumeCtrl = nullptr;
    OSDWindow* m_osd = nullptr; // QWidget, no parent — deleted in ~App()
    InputHandler* m_input = nullptr;
    TrayApp* m_tray = nullptr;
    MprisClient* m_mpris = nullptr;
    AlbumArtCache* m_albumArtCache = nullptr;

    DbusInterface* m_dbus = nullptr;
    QObject* m_mprisEndpoint = nullptr;
    bool m_mprisRegistered = false;
    bool m_startInputDevice = true;
    bool m_startWindowTracker = true;
    bool m_registerDbus = true;
    QString m_dbusServiceName = QStringLiteral("org.keyboardvolumeapp");
    QString m_mprisServiceName = QStringLiteral("org.mpris.MediaPlayer2.keyboardvolumeapp");

    WindowTracker* m_windowTracker = nullptr;
    QList<AudioApp> m_appCache;
    QString m_autoActiveApp;
    bool m_focusDisplayActive = false;
    // Last profile id + routed app whose sink we applied — guards activateProfile()
    // so the sink is moved once per (profile, focused-app) pair, not on every
    // hotkey press, while still re-routing when Follow Focus changes app inside
    // the same regex profile.
    QString m_lastActivatedProfileId;
    QString m_lastActivatedRouteApp;
};
