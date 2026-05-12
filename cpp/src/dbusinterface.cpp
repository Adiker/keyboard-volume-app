#include "dbusinterface.h"
#include "config.h"
#include "volumecontroller.h"
#include "trayapp.h"
#include "audioapp.h"

#include <QDebug>
#include <QVariantMap>
#include <algorithm>
#include <cmath>

namespace
{

QVariant hotkeyBindingVariant(const HotkeyBinding& binding)
{
    if (!binding.isAssigned()) return 0;
    if (binding.type == HotkeyBindingType::Key) return binding.code;

    QVariantMap out;
    out[QStringLiteral("type")] = QStringLiteral("rel");
    out[QStringLiteral("code")] = binding.code;
    out[QStringLiteral("direction")] = binding.direction < 0 ? -1 : 1;
    return out;
}

} // namespace

DbusInterface::DbusInterface(Config* config, VolumeController* volumeCtrl, TrayApp* tray,
                             QObject* parent)
    : QObject(parent), m_config(config), m_volumeCtrl(volumeCtrl), m_tray(tray)
{
    m_activeApp = m_tray->currentApp();
    m_volumeStep = m_config->volumeStep();
    m_progressEnabled = m_config->osd().progressEnabled;
    m_profilesProp = buildProfilesProp();
    m_scenesProp = buildScenesProp();

    connect(m_volumeCtrl, &VolumeController::volumeChanged, this,
            [this](const QString& app, double vol, bool muted)
            {
                if (app != m_activeApp) return;
                if (qAbs(m_volume - vol) > 0.0001)
                {
                    m_volume = vol;
                    emit volumeChanged(m_volume);
                }
                if (m_muted != muted)
                {
                    m_muted = muted;
                    emit mutedChanged(m_muted);
                }
            });

    connect(m_volumeCtrl, &VolumeController::appsReady, this,
            [this](QList<AudioApp> apps)
            {
                QStringList names;
                names.reserve(apps.size());
                for (const auto& a : apps) names.append(a.name);
                if (m_apps != names)
                {
                    m_apps = names;
                    emit appsUpdated();
                }
            });

    connect(m_tray, &TrayApp::appChanged, this,
            [this](const QString& name)
            {
                if (m_activeApp != name)
                {
                    m_activeApp = name;
                    m_volume = 0.0;
                    m_muted = false;
                    emit activeAppChanged(m_activeApp);
                }
                reloadProfiles();
            });
}

void DbusInterface::setVolume(double vol)
{
    vol = std::clamp(vol, 0.0, 1.0);
    if (m_activeApp.isEmpty()) return;
    double delta = vol - m_volume;
    if (qAbs(delta) < 0.0001) return;
    m_volumeCtrl->changeVolume(m_activeApp, delta);
}

void DbusInterface::setMuted(bool muted)
{
    if (m_activeApp.isEmpty()) return;
    if (m_muted == muted) return;
    m_volumeCtrl->toggleMute(m_activeApp);
}

void DbusInterface::setActiveApp(const QString& name)
{
    if (m_activeApp == name) return;
    if (!m_apps.contains(name) && !m_apps.isEmpty())
    {
        qDebug() << "[DbusInterface] unknown app:" << name;
        return;
    }
    m_config->setSelectedApp(name);
    m_activeApp = name;
    m_volume = 0.0;
    m_muted = false;
    emit activeAppChanged(m_activeApp);
    reloadProfiles();
}

void DbusInterface::setVolumeStep(int step)
{
    m_config->setVolumeStep(step);
    m_volumeStep = m_config->volumeStep();
}

void DbusInterface::setProgressEnabled(bool on)
{
    if (m_progressEnabled == on) return;
    OsdConfig osd = m_config->osd();
    osd.progressEnabled = on;
    m_config->setOsd(osd);
    m_progressEnabled = on;
    emit progressEnabledChanged(m_progressEnabled);
}

void DbusInterface::VolumeUp()
{
    if (m_activeApp.isEmpty()) return;
    double step = m_volumeStep / 100.0;
    m_volumeCtrl->changeVolume(m_activeApp, step);
}

void DbusInterface::VolumeDown()
{
    if (m_activeApp.isEmpty()) return;
    double step = m_volumeStep / 100.0;
    m_volumeCtrl->changeVolume(m_activeApp, -step);
}

void DbusInterface::ToggleMute()
{
    if (m_activeApp.isEmpty()) return;
    m_volumeCtrl->toggleMute(m_activeApp);
}

void DbusInterface::ToggleDucking()
{
    const Profile p = m_config->defaultProfile();
    if (p.app.isEmpty() || !p.ducking.enabled) return;
    m_volumeCtrl->toggleDucking(p.app, p.ducking.volume / 100.0);
}

void DbusInterface::RefreshApps()
{
    m_volumeCtrl->listApps(true);
}

// ─── Profiles ────────────────────────────────────────────────────────────────
QVariantList DbusInterface::buildProfilesProp() const
{
    QVariantList out;
    for (const Profile& p : m_config->profiles())
    {
        QStringList mods;
        if (p.modifiers.contains(Modifier::Ctrl)) mods << QStringLiteral("ctrl");
        if (p.modifiers.contains(Modifier::Shift)) mods << QStringLiteral("shift");

        QVariantMap hk;
        hk[QStringLiteral("volume_up")] = hotkeyBindingVariant(p.hotkeys.volumeUp);
        hk[QStringLiteral("volume_down")] = hotkeyBindingVariant(p.hotkeys.volumeDown);
        hk[QStringLiteral("mute")] = hotkeyBindingVariant(p.hotkeys.mute);
        hk[QStringLiteral("show")] = hotkeyBindingVariant(p.hotkeys.show);

        QVariantMap ducking;
        ducking[QStringLiteral("enabled")] = p.ducking.enabled;
        ducking[QStringLiteral("volume")] = p.ducking.volume;
        ducking[QStringLiteral("hotkey")] = hotkeyBindingVariant(p.ducking.hotkey);

        QVariantMap m;
        m[QStringLiteral("id")] = p.id;
        m[QStringLiteral("name")] = p.name;
        m[QStringLiteral("app")] = p.app;
        m[QStringLiteral("modifiers")] = mods;
        m[QStringLiteral("hotkeys")] = hk;
        m[QStringLiteral("ducking")] = ducking;
        out.append(m);
    }
    return out;
}

// ─── Scenes ──────────────────────────────────────────────────────────────────
QVariantList DbusInterface::buildScenesProp() const
{
    QVariantList out;
    for (const AudioScene& scene : m_config->scenes())
    {
        QVariantList targets;
        for (const SceneTarget& target : scene.targets)
        {
            QVariantMap tm;
            tm[QStringLiteral("match")] = target.match;
            if (target.volume) tm[QStringLiteral("volume")] = *target.volume;
            if (target.muted) tm[QStringLiteral("muted")] = *target.muted;
            targets.append(tm);
        }

        QVariantMap m;
        m[QStringLiteral("id")] = scene.id;
        m[QStringLiteral("name")] = scene.name;
        m[QStringLiteral("targets")] = targets;
        out.append(m);
    }
    return out;
}

Profile DbusInterface::findProfile(const QString& id) const
{
    for (const Profile& p : m_config->profiles())
    {
        if (p.id == id) return p;
    }
    return Profile{};
}

AudioScene DbusInterface::findScene(const QString& id) const
{
    for (const AudioScene& scene : m_config->scenes())
    {
        if (scene.id == id) return scene;
    }
    return AudioScene{};
}

void DbusInterface::reloadProfiles()
{
    QVariantList fresh = buildProfilesProp();
    if (fresh != m_profilesProp)
    {
        m_profilesProp = fresh;
        emit profilesChanged(m_profilesProp);
    }

    QVariantList freshScenes = buildScenesProp();
    if (freshScenes == m_scenesProp) return;
    m_scenesProp = freshScenes;
    emit scenesChanged(m_scenesProp);
}

void DbusInterface::VolumeUpProfile(const QString& profileId)
{
    Profile p = findProfile(profileId);
    if (p.app.isEmpty()) return;
    double step = m_volumeStep / 100.0;
    m_volumeCtrl->changeVolume(p.app, step);
}

void DbusInterface::VolumeDownProfile(const QString& profileId)
{
    Profile p = findProfile(profileId);
    if (p.app.isEmpty()) return;
    double step = m_volumeStep / 100.0;
    m_volumeCtrl->changeVolume(p.app, -step);
}

void DbusInterface::ToggleMuteProfile(const QString& profileId)
{
    Profile p = findProfile(profileId);
    if (p.app.isEmpty()) return;
    m_volumeCtrl->toggleMute(p.app);
}

void DbusInterface::ToggleDuckingProfile(const QString& profileId)
{
    Profile p = findProfile(profileId);
    if (p.app.isEmpty() || !p.ducking.enabled) return;
    m_volumeCtrl->toggleDucking(p.app, p.ducking.volume / 100.0);
}

void DbusInterface::ApplyScene(const QString& sceneId)
{
    const AudioScene scene = findScene(sceneId);
    if (scene.id.isEmpty()) return;

    for (const SceneTarget& target : scene.targets)
    {
        if (target.match.isEmpty()) continue;
        if (target.volume) m_volumeCtrl->setVolume(target.match, *target.volume / 100.0);
        if (target.muted) m_volumeCtrl->setMuted(target.match, *target.muted);
    }
}

void DbusInterface::ShowVolume()
{
    if (m_activeApp.isEmpty()) return;
    m_volumeCtrl->queryVolume(m_activeApp);
}

void DbusInterface::ShowVolumeProfile(const QString& profileId)
{
    const Profile p = findProfile(profileId);
    if (p.app.isEmpty()) return;
    m_volumeCtrl->queryVolume(p.app);
}
