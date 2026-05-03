#include "dbusinterface.h"
#include "config.h"
#include "volumecontroller.h"
#include "trayapp.h"
#include "audioapp.h"

#include <QDebug>
#include <QVariantMap>
#include <algorithm>
#include <cmath>

DbusInterface::DbusInterface(Config *config,
                             VolumeController *volumeCtrl,
                             TrayApp *tray,
                             QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_volumeCtrl(volumeCtrl)
    , m_tray(tray)
{
    m_activeApp    = m_tray->currentApp();
    m_volumeStep   = m_config->volumeStep();
    m_profilesProp = buildProfilesProp();

    connect(m_volumeCtrl, &VolumeController::volumeChanged,
            this, [this](const QString &app, double vol, bool muted) {
        if (app != m_activeApp) return;
        if (qAbs(m_volume - vol) > 0.0001) {
            m_volume = vol;
            emit volumeChanged(m_volume);
        }
        if (m_muted != muted) {
            m_muted = muted;
            emit mutedChanged(m_muted);
        }
    });

    connect(m_volumeCtrl, &VolumeController::appsReady,
            this, [this](QList<AudioApp> apps) {
        QStringList names;
        names.reserve(apps.size());
        for (const auto &a : apps)
            names.append(a.name);
        if (m_apps != names) {
            m_apps = names;
            emit appsUpdated();
        }
    });

    connect(m_tray, &TrayApp::appChanged,
            this, [this](const QString &name) {
        if (m_activeApp != name) {
            m_activeApp = name;
            m_volume = 0.0;
            m_muted  = false;
            emit activeAppChanged(m_activeApp);
        }
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

void DbusInterface::setActiveApp(const QString &name)
{
    if (m_activeApp == name) return;
    if (!m_apps.contains(name) && !m_apps.isEmpty()) {
        qDebug() << "[DbusInterface] unknown app:" << name;
        return;
    }
    m_config->setSelectedApp(name);
    m_activeApp = name;
    m_volume = 0.0;
    m_muted  = false;
    emit activeAppChanged(m_activeApp);
}

void DbusInterface::setVolumeStep(int step)
{
    m_config->setVolumeStep(step);
    m_volumeStep = m_config->volumeStep();
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

void DbusInterface::RefreshApps()
{
    m_volumeCtrl->listApps(true);
}

// ─── Profiles ────────────────────────────────────────────────────────────────
QVariantList DbusInterface::buildProfilesProp() const
{
    QVariantList out;
    for (const Profile &p : m_config->profiles()) {
        QStringList mods;
        if (p.modifiers.contains(Modifier::Ctrl))  mods << QStringLiteral("ctrl");
        if (p.modifiers.contains(Modifier::Shift)) mods << QStringLiteral("shift");

        QVariantMap hk;
        hk[QStringLiteral("volume_up")]   = p.hotkeys.volumeUp;
        hk[QStringLiteral("volume_down")] = p.hotkeys.volumeDown;
        hk[QStringLiteral("mute")]        = p.hotkeys.mute;

        QVariantMap m;
        m[QStringLiteral("id")]        = p.id;
        m[QStringLiteral("name")]      = p.name;
        m[QStringLiteral("app")]       = p.app;
        m[QStringLiteral("modifiers")] = mods;
        m[QStringLiteral("hotkeys")]   = hk;
        out.append(m);
    }
    return out;
}

Profile DbusInterface::findProfile(const QString &id) const
{
    for (const Profile &p : m_config->profiles()) {
        if (p.id == id) return p;
    }
    return Profile{};
}

void DbusInterface::reloadProfiles()
{
    QVariantList fresh = buildProfilesProp();
    if (fresh == m_profilesProp) return;
    m_profilesProp = fresh;
    emit profilesChanged(m_profilesProp);
}

void DbusInterface::VolumeUpProfile(const QString &profileId)
{
    Profile p = findProfile(profileId);
    if (p.app.isEmpty()) return;
    double step = m_volumeStep / 100.0;
    m_volumeCtrl->changeVolume(p.app, step);
}

void DbusInterface::VolumeDownProfile(const QString &profileId)
{
    Profile p = findProfile(profileId);
    if (p.app.isEmpty()) return;
    double step = m_volumeStep / 100.0;
    m_volumeCtrl->changeVolume(p.app, -step);
}

void DbusInterface::ToggleMuteProfile(const QString &profileId)
{
    Profile p = findProfile(profileId);
    if (p.app.isEmpty()) return;
    m_volumeCtrl->toggleMute(p.app);
}
