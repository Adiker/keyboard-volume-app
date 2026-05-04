#include "config.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSaveFile>
#include <QStandardPaths>
#include <QDebug>
#include <QStringList>
#include <algorithm>

// ─── Paths ────────────────────────────────────────────────────────────────────
QString Config::configDir() const
{
    if (!m_configDir.isEmpty())
        return m_configDir;
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
           + QStringLiteral("/keyboard-volume-app");
}

QString Config::configFile() const
{
    return configDir() + QStringLiteral("/config.json");
}

// ─── Defaults ─────────────────────────────────────────────────────────────────
QJsonObject Config::defaultJson()
{
    QJsonObject defaultProfile {
        { QStringLiteral("id"),        QStringLiteral("default") },
        { QStringLiteral("name"),      QStringLiteral("Default") },
        { QStringLiteral("app"),       QJsonValue::Null },
        { QStringLiteral("modifiers"), QJsonArray{} },
        { QStringLiteral("hotkeys"), QJsonObject {
            { QStringLiteral("volume_up"),   115 },
            { QStringLiteral("volume_down"), 114 },
            { QStringLiteral("mute"),        113 },
        }},
    };

    return QJsonObject {
        { QStringLiteral("input_device"),  QJsonValue::Null },
        { QStringLiteral("selected_app"),  QJsonValue::Null },
        { QStringLiteral("language"),      QStringLiteral("en") },
        { QStringLiteral("volume_step"),   5 },
        { QStringLiteral("osd"), QJsonObject {
            { QStringLiteral("screen"),     0 },
            { QStringLiteral("x"),          50 },
            { QStringLiteral("y"),          1150 },
            { QStringLiteral("timeout_ms"), 1200 },
            { QStringLiteral("opacity"),    85 },
            { QStringLiteral("color_bg"),   QStringLiteral("#1A1A1A") },
            { QStringLiteral("color_text"), QStringLiteral("#FFFFFF") },
            { QStringLiteral("color_bar"),  QStringLiteral("#0078D7") },
        }},
        { QStringLiteral("hotkeys"), QJsonObject {
            { QStringLiteral("volume_up"),   115 },
            { QStringLiteral("volume_down"), 114 },
            { QStringLiteral("mute"),        113 },
        }},
        { QStringLiteral("profiles"), QJsonArray { defaultProfile } },
    };
}

// Recursively merge: keys present in override_ overwrite base; nested objects
// are merged recursively so that missing keys in override keep their defaults.
QJsonObject Config::deepMerge(const QJsonObject &base, const QJsonObject &override_)
{
    QJsonObject result = base;
    for (auto it = override_.begin(); it != override_.end(); ++it) {
        const QString &key = it.key();
        if (base.contains(key)
            && base[key].isObject()
            && it.value().isObject())
        {
            result[key] = deepMerge(base[key].toObject(), it.value().toObject());
        } else {
            result[key] = it.value();
        }
    }
    return result;
}

// ─── Modifier helpers ────────────────────────────────────────────────────────
QString Config::modifierToString(Modifier m)
{
    switch (m) {
        case Modifier::Ctrl:  return QStringLiteral("ctrl");
        case Modifier::Shift: return QStringLiteral("shift");
    }
    return {};
}

std::optional<Modifier> Config::modifierFromString(const QString &s)
{
    const QString l = s.trimmed().toLower();
    if (l == QStringLiteral("ctrl"))  return Modifier::Ctrl;
    if (l == QStringLiteral("shift")) return Modifier::Shift;
    return std::nullopt;
}

// ─── Profile <-> JSON ────────────────────────────────────────────────────────
QJsonObject Config::profileToJson(const Profile &p)
{
    QJsonArray mods;
    // Stable serialization order: ctrl before shift.
    if (p.modifiers.contains(Modifier::Ctrl))  mods.append(modifierToString(Modifier::Ctrl));
    if (p.modifiers.contains(Modifier::Shift)) mods.append(modifierToString(Modifier::Shift));

    return QJsonObject {
        { QStringLiteral("id"),        p.id },
        { QStringLiteral("name"),      p.name },
        { QStringLiteral("app"),       p.app.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(p.app) },
        { QStringLiteral("modifiers"), mods },
        { QStringLiteral("hotkeys"),   QJsonObject {
            { QStringLiteral("volume_up"),   p.hotkeys.volumeUp },
            { QStringLiteral("volume_down"), p.hotkeys.volumeDown },
            { QStringLiteral("mute"),        p.hotkeys.mute },
        }},
    };
}

Profile Config::profileFromJson(const QJsonObject &o)
{
    Profile p;
    p.id   = o[QStringLiteral("id")].toString();
    p.name = o[QStringLiteral("name")].toString();
    QJsonValue av = o[QStringLiteral("app")];
    p.app  = av.isString() ? av.toString() : QString{};

    QJsonObject hk = o[QStringLiteral("hotkeys")].toObject();
    p.hotkeys.volumeUp   = hk[QStringLiteral("volume_up")].toInt(115);
    p.hotkeys.volumeDown = hk[QStringLiteral("volume_down")].toInt(114);
    p.hotkeys.mute       = hk[QStringLiteral("mute")].toInt(113);

    QJsonArray mods = o[QStringLiteral("modifiers")].toArray();
    for (const auto &v : mods) {
        if (auto m = modifierFromString(v.toString()))
            p.modifiers.insert(*m);
    }
    return p;
}

QList<Profile> Config::profilesFromJson(const QJsonArray &arr)
{
    QList<Profile> out;
    out.reserve(arr.size());
    for (const auto &v : arr) {
        if (!v.isObject()) continue;
        Profile p = profileFromJson(v.toObject());
        if (p.id.isEmpty()) continue;  // skip malformed entries
        out.push_back(std::move(p));
    }
    return out;
}

QJsonArray Config::profilesToJsonArray(const QList<Profile> &profiles)
{
    QJsonArray arr;
    for (const auto &p : profiles)
        arr.append(profileToJson(p));
    return arr;
}

// ─── Migration / mirroring ──────────────────────────────────────────────────
void Config::migrateLegacyToProfilesUnlocked()
{
    // Already has at least one profile? Nothing to do.
    QJsonArray arr = m_data[QStringLiteral("profiles")].toArray();
    if (!arr.isEmpty())
        return;

    // Synthesize one default profile from legacy selected_app + hotkeys.
    QJsonValue av = m_data[QStringLiteral("selected_app")];
    QString app = av.isString() ? av.toString() : QString{};

    QJsonObject hk = m_data[QStringLiteral("hotkeys")].toObject();
    int vUp   = hk[QStringLiteral("volume_up")].toInt(115);
    int vDown = hk[QStringLiteral("volume_down")].toInt(114);
    int mute  = hk[QStringLiteral("mute")].toInt(113);

    QJsonObject defaultProfile {
        { QStringLiteral("id"),        QStringLiteral("default") },
        { QStringLiteral("name"),      QStringLiteral("Default") },
        { QStringLiteral("app"),       app.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(app) },
        { QStringLiteral("modifiers"), QJsonArray{} },
        { QStringLiteral("hotkeys"), QJsonObject {
            { QStringLiteral("volume_up"),   vUp },
            { QStringLiteral("volume_down"), vDown },
            { QStringLiteral("mute"),        mute },
        }},
    };
    m_data[QStringLiteral("profiles")] = QJsonArray { defaultProfile };
}

void Config::mirrorDefaultProfileToLegacyUnlocked()
{
    QJsonArray arr = m_data[QStringLiteral("profiles")].toArray();
    if (arr.isEmpty()) return;

    QJsonObject p0 = arr.first().toObject();
    QJsonValue av = p0[QStringLiteral("app")];
    m_data[QStringLiteral("selected_app")] =
        (av.isString() && !av.toString().isEmpty())
            ? av
            : QJsonValue(QJsonValue::Null);

    QJsonObject hk = p0[QStringLiteral("hotkeys")].toObject();
    m_data[QStringLiteral("hotkeys")] = QJsonObject {
        { QStringLiteral("volume_up"),   hk[QStringLiteral("volume_up")].toInt(115) },
        { QStringLiteral("volume_down"), hk[QStringLiteral("volume_down")].toInt(114) },
        { QStringLiteral("mute"),        hk[QStringLiteral("mute")].toInt(113) },
    };
}

// ─── Config ───────────────────────────────────────────────────────────────────
Config::Config()
{
    load();
}

Config::Config(const QString &configDir)
    : m_configDir(configDir)
{
    load();
}

void Config::load()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    QDir().mkpath(configDir());
    QFile f(configFile());
    if (f.exists() && f.open(QIODevice::ReadOnly)) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        f.close();
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject loaded = doc.object();
            m_data = deepMerge(defaultJson(), loaded);
            // If the loaded file had no "profiles" key, deepMerge gave us the
            // default profile from defaultJson(). But if it had legacy
            // selected_app + hotkeys with non-default values, those wouldn't
            // be reflected. Also recover when the new-schema profiles key is
            // present but empty or contains no valid profile entries.
            const bool needsProfileMigration =
                !loaded.contains(QStringLiteral("profiles"))
                || profilesFromJson(m_data[QStringLiteral("profiles")].toArray()).isEmpty();
            if (needsProfileMigration) {
                // Replace synthetic/invalid profiles with one built from legacy fields.
                m_data[QStringLiteral("profiles")] = QJsonArray{};
                migrateLegacyToProfilesUnlocked();
                mirrorDefaultProfileToLegacyUnlocked();
                saveUnlocked();
            } else {
                // New schema present; still mirror profile[0] to legacy fields
                // so anything reading them sees current values.
                mirrorDefaultProfileToLegacyUnlocked();
            }
            return;
        }
        qWarning() << "[Config] Parse error:" << err.errorString();
    }
    m_firstRun = true;
    m_data = defaultJson();
    saveUnlocked();
}

bool Config::isFirstRun() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_firstRun;
}

void Config::save() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    saveUnlocked();
}

void Config::saveUnlocked() const
{
    QDir().mkpath(configDir());
    QSaveFile f(configFile());
    f.setDirectWriteFallback(false);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "[Config] Cannot write" << configFile();
        return;
    }

    const QByteArray data = QJsonDocument(m_data).toJson(QJsonDocument::Indented);
    if (f.write(data) != data.size()) {
        qWarning() << "[Config] Incomplete write" << configFile() << f.errorString();
        return;
    }

    if (!f.commit())
        qWarning() << "[Config] Cannot commit" << configFile() << f.errorString();
}

// ─── Getters / setters ────────────────────────────────────────────────────────
QString Config::inputDevice() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    QJsonValue v = m_data[QStringLiteral("input_device")];
    return v.isString() ? v.toString() : QString{};
}
void Config::setInputDevice(const QString &path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data[QStringLiteral("input_device")] = path.isEmpty()
        ? QJsonValue(QJsonValue::Null)
        : QJsonValue(path);
    saveUnlocked();
}

QString Config::selectedApp() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    QJsonValue v = m_data[QStringLiteral("selected_app")];
    return v.isString() ? v.toString() : QString{};
}
void Config::setSelectedApp(const QString &name)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // Update default profile (profiles[0]) so behavior stays consistent.
    QJsonArray arr = m_data[QStringLiteral("profiles")].toArray();
    if (!arr.isEmpty()) {
        QJsonObject p0 = arr.first().toObject();
        p0[QStringLiteral("app")] = name.isEmpty()
            ? QJsonValue(QJsonValue::Null)
            : QJsonValue(name);
        arr.replace(0, p0);
        m_data[QStringLiteral("profiles")] = arr;
    }
    m_data[QStringLiteral("selected_app")] = name.isEmpty()
        ? QJsonValue(QJsonValue::Null)
        : QJsonValue(name);
    saveUnlocked();
}

QString Config::language() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data[QStringLiteral("language")].toString(QStringLiteral("en"));
}
void Config::setLanguage(const QString &code)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data[QStringLiteral("language")] = code;
    saveUnlocked();
}

int Config::volumeStep() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data[QStringLiteral("volume_step")].toInt(5);
}
void Config::setVolumeStep(int step)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data[QStringLiteral("volume_step")] = std::clamp(step, 1, 50);
    saveUnlocked();
}

OsdConfig Config::osd() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    QJsonObject o = m_data[QStringLiteral("osd")].toObject();
    OsdConfig c;
    c.screen    = o[QStringLiteral("screen")].toInt(0);
    c.x         = o[QStringLiteral("x")].toInt(50);
    c.y         = o[QStringLiteral("y")].toInt(1150);
    c.timeoutMs = o[QStringLiteral("timeout_ms")].toInt(1200);
    c.opacity   = o[QStringLiteral("opacity")].toInt(85);
    c.colorBg   = o[QStringLiteral("color_bg")].toString(QStringLiteral("#1A1A1A"));
    c.colorText = o[QStringLiteral("color_text")].toString(QStringLiteral("#FFFFFF"));
    c.colorBar  = o[QStringLiteral("color_bar")].toString(QStringLiteral("#0078D7"));
    return c;
}

void Config::setOsd(const OsdConfig &c)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data[QStringLiteral("osd")] = QJsonObject {
        { QStringLiteral("screen"),     c.screen },
        { QStringLiteral("x"),          c.x },
        { QStringLiteral("y"),          c.y },
        { QStringLiteral("timeout_ms"), c.timeoutMs },
        { QStringLiteral("opacity"),    c.opacity },
        { QStringLiteral("color_bg"),   c.colorBg },
        { QStringLiteral("color_text"), c.colorText },
        { QStringLiteral("color_bar"),  c.colorBar },
    };
    saveUnlocked();
}

void Config::updateOsd(int screen, int timeoutMs, int x, int y, int opacity,
                       const QString &colorBg, const QString &colorText,
                       const QString &colorBar)
{
    OsdConfig c = osd();
    c.screen    = screen;
    c.timeoutMs = timeoutMs;
    c.x         = x;
    c.y         = y;
    c.opacity   = opacity;
    c.colorBg   = colorBg;
    c.colorText = colorText;
    c.colorBar  = colorBar;
    setOsd(c);
}

HotkeyConfig Config::hotkeys() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    QJsonObject o = m_data[QStringLiteral("hotkeys")].toObject();
    HotkeyConfig h;
    h.volumeUp   = o[QStringLiteral("volume_up")].toInt(115);
    h.volumeDown = o[QStringLiteral("volume_down")].toInt(114);
    h.mute       = o[QStringLiteral("mute")].toInt(113);
    return h;
}

void Config::setHotkeys(int volumeUp, int volumeDown, int mute)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    QJsonObject hk {
        { QStringLiteral("volume_up"),   volumeUp },
        { QStringLiteral("volume_down"), volumeDown },
        { QStringLiteral("mute"),        mute },
    };
    // Update default profile (profiles[0]) so behavior stays consistent.
    QJsonArray arr = m_data[QStringLiteral("profiles")].toArray();
    if (!arr.isEmpty()) {
        QJsonObject p0 = arr.first().toObject();
        p0[QStringLiteral("hotkeys")] = hk;
        arr.replace(0, p0);
        m_data[QStringLiteral("profiles")] = arr;
    }
    m_data[QStringLiteral("hotkeys")] = hk;
    saveUnlocked();
}

// ─── Profiles API ────────────────────────────────────────────────────────────
QList<Profile> Config::profiles() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    QJsonArray arr = m_data[QStringLiteral("profiles")].toArray();
    return profilesFromJson(arr);
}

void Config::setProfiles(const QList<Profile> &profiles)
{
    if (profiles.isEmpty()) {
        qWarning() << "[Config] setProfiles: empty list rejected (must have ≥1)";
        return;
    }

    // Ensure unique ids — append numeric suffix on collision.
    QList<Profile> sanitized = profiles;
    QSet<QString> seen;
    for (auto &p : sanitized) {
        if (p.id.isEmpty()) p.id = QStringLiteral("profile");
        QString base = p.id;
        int suffix = 2;
        while (seen.contains(p.id))
            p.id = base + QStringLiteral("-") + QString::number(suffix++);
        seen.insert(p.id);
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_data[QStringLiteral("profiles")] = profilesToJsonArray(sanitized);
    mirrorDefaultProfileToLegacyUnlocked();
    saveUnlocked();
}

Profile Config::defaultProfile() const
{
    QList<Profile> all = profiles();
    if (all.isEmpty()) {
        Profile p;
        p.id   = QStringLiteral("default");
        p.name = QStringLiteral("Default");
        return p;
    }
    return all.first();
}

void Config::setDefaultProfileApp(const QString &app)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    QJsonArray arr = m_data[QStringLiteral("profiles")].toArray();
    if (arr.isEmpty()) {
        // Should not happen post-migration, but be defensive.
        return;
    }
    QJsonObject p0 = arr.first().toObject();
    p0[QStringLiteral("app")] = app.isEmpty()
        ? QJsonValue(QJsonValue::Null)
        : QJsonValue(app);
    arr.replace(0, p0);
    m_data[QStringLiteral("profiles")] = arr;
    m_data[QStringLiteral("selected_app")] = app.isEmpty()
        ? QJsonValue(QJsonValue::Null)
        : QJsonValue(app);
    saveUnlocked();
}
