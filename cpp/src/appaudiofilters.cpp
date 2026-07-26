#include "appaudiofilters.h"

const QSet<QString> SYSTEM_BINARIES{
    QStringLiteral("wireplumber"),
    QStringLiteral("pipewire"),
    QStringLiteral("kwin_wayland"),
    QStringLiteral("plasmashell"),
    QStringLiteral("kded5"),
    QStringLiteral("kded6"),
    QStringLiteral("xdg-desktop-portal"),
    QStringLiteral("xdg-desktop-portal-kde"),
    QStringLiteral("polkit-kde-authentication-agent-1"),
    QStringLiteral("pactl"),
    QStringLiteral("pw-cli"),
    QStringLiteral("pw-dump"),
    QStringLiteral("keyboard-volume-app"),
    QStringLiteral("python3"),
    QStringLiteral("python3.14"),
    QStringLiteral("python"),
    QStringLiteral("QtWebEngineProcess"),
    QString{},
};

const QSet<QString> SKIP_APP_NAMES{
    QStringLiteral("ringrtc"),
    QStringLiteral("WEBRTC VoiceEngine"),
    QStringLiteral("Chromium input"),
};
