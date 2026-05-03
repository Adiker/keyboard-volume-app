#pragma once
#include <QString>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QHashFunctions>
#include <mutex>
#include <optional>

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

// ─── Modifier set for profile matching ───────────────────────────────────────
// L/R variants of each modifier collapse to a single canonical value.
enum class Modifier { Ctrl, Shift };

inline size_t qHash(Modifier m, size_t seed = 0) noexcept
{
    return ::qHash(static_cast<int>(m), seed);
}

// ─── Audio profile (point 8) ──────────────────────────────────────────────────
// Each profile binds its own hotkey codes + optional modifier requirements
// to a target audio app. Multiple profiles allow controlling several apps from
// the keyboard (e.g. Vol+ → spotify, Ctrl+Vol+ → firefox, F11 → vlc).
struct Profile {
    QString        id;          // stable slug, unique within profiles list
    QString        name;        // user-facing label
    QString        app;         // audio app name (may be empty)
    HotkeyConfig   hotkeys;     // evdev codes for volume up/down/mute
    QSet<Modifier> modifiers;   // required held modifiers (empty = bare key)
};

inline bool operator==(const HotkeyConfig &a, const HotkeyConfig &b)
{
    return a.volumeUp == b.volumeUp && a.volumeDown == b.volumeDown && a.mute == b.mute;
}
inline bool operator!=(const HotkeyConfig &a, const HotkeyConfig &b) { return !(a == b); }

inline bool operator==(const Profile &a, const Profile &b)
{
    return a.id == b.id && a.name == b.name && a.app == b.app
        && a.hotkeys == b.hotkeys && a.modifiers == b.modifiers;
}
inline bool operator!=(const Profile &a, const Profile &b) { return !(a == b); }

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

    bool isFirstRun() const;

    // input device
    QString inputDevice() const;
    void    setInputDevice(const QString &path);

    // selected audio app name (legacy mirror of profile[0].app — kept for
    // D-Bus/MPRIS continuity; setter updates the default profile too)
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

    // Hotkeys (legacy mirror of profile[0].hotkeys — kept for back-compat;
    // setter updates the default profile too)
    HotkeyConfig hotkeys() const;
    void         setHotkeys(int volumeUp, int volumeDown, int mute);

    // Profiles (point 8 — multi-app routing). After load() / migration there
    // is always at least one profile (profiles().first() == defaultProfile()).
    QList<Profile> profiles() const;
    void           setProfiles(const QList<Profile> &profiles);
    Profile        defaultProfile() const;
    void           setDefaultProfileApp(const QString &app);

    // Modifier serialization helpers (kanoniczne nazwy: "ctrl", "shift")
    static QString                  modifierToString(Modifier m);
    static std::optional<Modifier>  modifierFromString(const QString &s);

    // Recursively merges two JSON objects. Nested objects are merged
    // recursively; scalars from override_ overwrite those from base.
    static QJsonObject deepMerge(const QJsonObject &base, const QJsonObject &override_);

private:
    QString configDir() const;
    QString configFile() const;
    static QJsonObject defaultJson();

    void saveUnlocked() const;

    // Profile <-> JSON conversion helpers (locks NOT held by these)
    static QJsonObject  profileToJson(const Profile &p);
    static Profile      profileFromJson(const QJsonObject &o);
    static QList<Profile> profilesFromJson(const QJsonArray &arr);
    static QJsonArray     profilesToJsonArray(const QList<Profile> &profiles);

    // Synthesize a default profile from legacy selected_app + hotkeys when
    // m_data has no "profiles" array. Caller must hold the mutex.
    void migrateLegacyToProfilesUnlocked();
    // Mirror profile[0] back into legacy selected_app + hotkeys keys.
    // Caller must hold the mutex.
    void mirrorDefaultProfileToLegacyUnlocked();

    mutable std::mutex m_mutex;
    QJsonObject m_data;
    QString     m_configDir;  // empty → use XDG default
    bool        m_firstRun = false;
};
