#pragma once
#include <QString>
#include <QJsonObject>

// ─── OSD configuration sub-struct ─────────────────────────────────────────────
struct OsdConfig {
    int     screen     = 0;
    int     x          = 50;
    int     y          = 1150;
    int     timeoutMs  = 1200;
    int     opacity    = 85;
    QString colorBg    = QStringLiteral("#1A1A1A");
    QString colorText  = QStringLiteral("#FFFFFF");
    QString colorBar   = QStringLiteral("#0078D7");
};

// ─── Hotkeys (raw evdev key codes) ────────────────────────────────────────────
struct HotkeyConfig {
    int volumeUp   = 115;   // KEY_VOLUMEUP
    int volumeDown = 114;   // KEY_VOLUMEDOWN
    int mute       = 113;   // KEY_MUTE
};

// ─── Config ───────────────────────────────────────────────────────────────────
// Reads/writes keyboard-volume-app/config.json in the XDG config directory.
// Pass a custom configDir to the constructor for isolated testing.
// Deep-merges existing file with built-in defaults so new keys are always
// present even when loading old config files.
// Every setter calls save() immediately — no explicit "save all" step.
class Config
{
public:
    // Uses XDG config directory for file I/O.
    Config();
    // Uses the given directory for file I/O (e.g. QTemporaryDir for tests).
    explicit Config(const QString &configDir);

    void load();
    void save() const;

    bool isFirstRun() const { return m_firstRun; }

    // input device
    QString inputDevice() const;
    void    setInputDevice(const QString &path);

    // selected audio app name
    QString selectedApp() const;
    void    setSelectedApp(const QString &name);

    // language code ("en" / "pl")
    QString language() const;
    void    setLanguage(const QString &code);

    // volume step 1–50
    int  volumeStep() const;
    void setVolumeStep(int step);

    // OSD
    OsdConfig osd() const;
    void      setOsd(const OsdConfig &osd);
    // Update individual OSD fields without touching others
    void updateOsd(
        int     screen,
        int     timeoutMs,
        int     x,
        int     y,
        int     opacity,
        const QString &colorBg,
        const QString &colorText,
        const QString &colorBar
    );

    // Hotkeys
    HotkeyConfig hotkeys() const;
    void         setHotkeys(int volumeUp, int volumeDown, int mute);

    // Recursively merges two JSON objects. Nested objects are merged
    // recursively; scalars from override_ overwrite those from base.
    static QJsonObject deepMerge(const QJsonObject &base, const QJsonObject &override_);

private:
    QString configDir() const;
    QString configFile() const;
    static QJsonObject defaultJson();

    QJsonObject m_data;
    QString     m_configDir;  // empty → use XDG default
    bool        m_firstRun = false;
};
