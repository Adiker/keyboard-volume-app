#include "config.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDebug>
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
            m_data = deepMerge(defaultJson(), doc.object());
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
    QFile f(configFile());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "[Config] Cannot write" << configFile();
        return;
    }
    f.write(QJsonDocument(m_data).toJson(QJsonDocument::Indented));
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
    m_data[QStringLiteral("hotkeys")] = QJsonObject {
        { QStringLiteral("volume_up"),   volumeUp },
        { QStringLiteral("volume_down"), volumeDown },
        { QStringLiteral("mute"),        mute },
    };
    saveUnlocked();
}
