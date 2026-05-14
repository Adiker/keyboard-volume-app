#pragma once
#include <QString>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QHashFunctions>
#include <QMetaType>
#include <algorithm>
#include <mutex>
#include <optional>

// ─── OSD configuration sub-struct ─────────────────────────────────────────────
struct OsdConfig
{
    int screen = 0;
    int x = 50;
    int y = 1150;
    int timeoutMs = 1200;
    int opacity = 85;
    QString colorBg = QStringLiteral("#1A1A1A");
    QString colorText = QStringLiteral("#FFFFFF");
    QString colorBar = QStringLiteral("#0078D7");

    // ── Progress bar (MPRIS playback position) ────────────────────────────────
    bool progressEnabled = false;    // master toggle (default OFF)
    bool progressInteractive = true; // allow click + drag to seek
    int progressPollMs = 500;        // position poll interval, 200–2000 ms
    // "app"   — show audio app name (legacy behaviour)
    // "track" — show track title / artist
    // "both"  — show "<app> — <title>"
    QString progressLabelMode = QStringLiteral("app");
    // Priority-ordered list of MPRIS service name substrings to track.
    // First Playing player wins; then first Paused; then noPlayer().
    QStringList trackedPlayers = {QStringLiteral("spotify"), QStringLiteral("youtube"),
                                  QStringLiteral("strawberry"), QStringLiteral("harmonoid")};

    // ── Media controls (play/pause/next/prev buttons in progress row) ─────────
    bool mediaControlsEnabled = true; // show media control buttons (default ON)

    // ── OSD scale ─────────────────────────────────────────────────────────────
    double osdScale = 1.0; // overall OSD scale factor, clamped 0.5–3.0
};

// ─── Hotkeys ──────────────────────────────────────────────────────────────────
enum class HotkeyBindingType
{
    Key,
    Relative,
};

struct HotkeyBinding
{
    HotkeyBindingType type = HotkeyBindingType::Key;
    int code = 0;
    int direction = 0; // 0 for keys, sign (+/-) for EV_REL bindings

    HotkeyBinding() = default;
    HotkeyBinding(int keyCode) : type(HotkeyBindingType::Key), code(std::max(0, keyCode)) {}

    static HotkeyBinding key(int keyCode)
    {
        return HotkeyBinding(keyCode);
    }
    static HotkeyBinding relative(int relCode, int relDirection)
    {
        HotkeyBinding b;
        b.type = HotkeyBindingType::Relative;
        b.code = std::max(0, relCode);
        b.direction = relDirection < 0 ? -1 : (relDirection > 0 ? 1 : 0);
        return b;
    }
    bool isAssigned() const
    {
        return code > 0 && (type == HotkeyBindingType::Key || direction != 0);
    }
};

struct HotkeyConfig
{
    HotkeyBinding volumeUp = 115;   // KEY_VOLUMEUP
    HotkeyBinding volumeDown = 114; // KEY_VOLUMEDOWN
    HotkeyBinding mute = 113;       // KEY_MUTE
    HotkeyBinding show;             // unassigned by default
};

struct DuckingConfig
{
    bool enabled = false;
    int volume = 25;      // Percent, 0-100
    HotkeyBinding hotkey; // unassigned by default
};

struct SceneTarget
{
    QString match;             // Audio app name or binary name
    std::optional<int> volume; // Percent, 0-100; nullopt = leave unchanged
    std::optional<bool> muted; // nullopt = leave unchanged
};

struct AudioScene
{
    QString id; // stable slug, unique within scenes list
    QString name;
    QList<SceneTarget> targets;
};

// ─── Modifier set for profile matching ───────────────────────────────────────
// L/R variants of each modifier collapse to a single canonical value.
enum class Modifier
{
    Ctrl,
    Shift
};

inline size_t qHash(Modifier m, size_t seed = 0) noexcept
{
    return ::qHash(static_cast<int>(m), seed);
}

inline bool operator==(const HotkeyBinding& a, const HotkeyBinding& b)
{
    return a.type == b.type && a.code == b.code && a.direction == b.direction;
}
inline bool operator!=(const HotkeyBinding& a, const HotkeyBinding& b)
{
    return !(a == b);
}
inline bool operator<(const HotkeyBinding& a, const HotkeyBinding& b)
{
    if (a.type != b.type) return static_cast<int>(a.type) < static_cast<int>(b.type);
    if (a.code != b.code) return a.code < b.code;
    return a.direction < b.direction;
}

inline size_t qHash(const HotkeyBinding& b, size_t seed = 0) noexcept
{
    return ::qHash(static_cast<int>(b.type), seed) ^ ::qHash(b.code, seed << 1) ^
           ::qHash(b.direction, seed << 2);
}

// ─── Audio profile (point 8) ──────────────────────────────────────────────────
// Each profile binds its own hotkey bindings + optional modifier requirements
// to a target audio app. Multiple profiles allow controlling several apps from
// the keyboard (e.g. Vol+ → spotify, Ctrl+Vol+ → firefox, F11 → vlc).
struct Profile
{
    QString id;               // stable slug, unique within profiles list
    QString name;             // user-facing label
    QString app;              // audio app name (may be empty)
    HotkeyConfig hotkeys;     // evdev codes for volume up/down/mute
    QSet<Modifier> modifiers; // required held modifiers (empty = bare key)
    DuckingConfig ducking;    // manual per-profile audio ducking
    bool autoSwitch = true;   // participate in auto-profile switching by window focus
};

inline bool operator==(const HotkeyConfig& a, const HotkeyConfig& b)
{
    return a.volumeUp == b.volumeUp && a.volumeDown == b.volumeDown && a.mute == b.mute &&
           a.show == b.show;
}
inline bool operator!=(const HotkeyConfig& a, const HotkeyConfig& b)
{
    return !(a == b);
}

inline bool operator==(const DuckingConfig& a, const DuckingConfig& b)
{
    return a.enabled == b.enabled && a.volume == b.volume && a.hotkey == b.hotkey;
}
inline bool operator!=(const DuckingConfig& a, const DuckingConfig& b)
{
    return !(a == b);
}

inline bool operator==(const SceneTarget& a, const SceneTarget& b)
{
    return a.match == b.match && a.volume == b.volume && a.muted == b.muted;
}
inline bool operator!=(const SceneTarget& a, const SceneTarget& b)
{
    return !(a == b);
}

inline bool operator==(const AudioScene& a, const AudioScene& b)
{
    return a.id == b.id && a.name == b.name && a.targets == b.targets;
}
inline bool operator!=(const AudioScene& a, const AudioScene& b)
{
    return !(a == b);
}

inline bool operator==(const Profile& a, const Profile& b)
{
    return a.id == b.id && a.name == b.name && a.app == b.app && a.hotkeys == b.hotkeys &&
           a.modifiers == b.modifiers && a.ducking == b.ducking && a.autoSwitch == b.autoSwitch;
}
inline bool operator!=(const Profile& a, const Profile& b)
{
    return !(a == b);
}

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
    explicit Config(const QString& configDir);

    void load();
    void save() const;

    bool isFirstRun() const;

    // input device
    QString inputDevice() const;
    void setInputDevice(const QString& path);

    // selected audio app name (legacy mirror of profile[0].app — kept for
    // D-Bus/MPRIS continuity; setter updates the default profile too)
    QString selectedApp() const;
    void setSelectedApp(const QString& name);

    // language code ("en" / "pl")
    QString language() const;
    void setLanguage(const QString& code);

    // volume step 1–50
    int volumeStep() const;
    void setVolumeStep(int step);

    // OSD
    OsdConfig osd() const;
    void setOsd(const OsdConfig& osd);
    // Update individual OSD fields without touching others
    void updateOsd(int screen, int timeoutMs, int x, int y, int opacity, const QString& colorBg,
                   const QString& colorText, const QString& colorBar);

    // Hotkeys (legacy mirror of profile[0].hotkeys — kept for back-compat;
    // setter updates the default profile too)
    HotkeyConfig hotkeys() const;
    void setHotkeys(int volumeUp, int volumeDown, int mute);

    // Profiles (point 8 — multi-app routing). After load() / migration there
    // is always at least one profile (profiles().first() == defaultProfile()).
    QList<Profile> profiles() const;
    void setProfiles(const QList<Profile>& profiles);
    Profile defaultProfile() const;
    void setDefaultProfileApp(const QString& app);

    // Audio scenes: named mixer presets applied through D-Bus / kv-ctl.
    QList<AudioScene> scenes() const;
    void setScenes(const QList<AudioScene>& scenes);

    // Auto-profile switching: when enabled, focused window determines the active
    // audio target. Disabled by default (off).
    bool autoProfileSwitch() const;
    void setAutoProfileSwitch(bool enabled);

    // Find the first profile whose app field matches `appName` (case-insensitive
    // contains match) and has autoSwitch == true. Returns default-constructed
    // Profile when nothing matches.
    Profile findProfileByApp(const QString& appName) const;

    // Modifier serialization helpers (kanoniczne nazwy: "ctrl", "shift")
    static QString modifierToString(Modifier m);
    static std::optional<Modifier> modifierFromString(const QString& s);

    // Recursively merges two JSON objects. Nested objects are merged
    // recursively; scalars from override_ overwrite those from base.
    static QJsonObject deepMerge(const QJsonObject& base, const QJsonObject& override_);

  private:
    QString configDir() const;
    QString configFile() const;
    static QJsonObject defaultJson();

    void saveUnlocked() const;

    // Profile <-> JSON conversion helpers (locks NOT held by these)
    static QJsonObject profileToJson(const Profile& p);
    static Profile profileFromJson(const QJsonObject& o);
    static QList<Profile> profilesFromJson(const QJsonArray& arr);
    static QJsonArray profilesToJsonArray(const QList<Profile>& profiles);
    static QJsonObject sceneToJson(const AudioScene& scene);
    static AudioScene sceneFromJson(const QJsonObject& o);
    static QList<AudioScene> scenesFromJson(const QJsonArray& arr);
    static QJsonArray scenesToJsonArray(const QList<AudioScene>& scenes);

    // Synthesize a default profile from legacy selected_app + hotkeys when
    // m_data has no "profiles" array. Caller must hold the mutex.
    void migrateLegacyToProfilesUnlocked();
    // Mirror profile[0] back into legacy selected_app + hotkeys keys.
    // Caller must hold the mutex.
    void mirrorDefaultProfileToLegacyUnlocked();

    mutable std::mutex m_mutex;
    QJsonObject m_data;
    QString m_configDir; // empty → use XDG default
    bool m_firstRun = false;
};

Q_DECLARE_METATYPE(HotkeyBinding)
