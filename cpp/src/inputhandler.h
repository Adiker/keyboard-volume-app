#pragma once
#include "config.h" // Profile, Modifier
#include <QThread>
#include <QSet>
#include <QList>
#include <QString>
#include <QMap>
#include <QPair>
#include <QMutex>
#include <atomic>

struct libevdev;

// ─── Device enumeration helpers ───────────────────────────────────────────────
// Returns absolute paths of all /dev/input/event* devices.
QList<QString> listEvdevDevices();

// Returns all evdev nodes that share the same physical parent as primaryPath
// AND expose at least one EV_KEY capability.
QList<QString> findSiblingDevices(const QString& primaryPath);

// Returns devices for the key-capture dialog:
//   siblings of primaryPath + all other EV_KEY devices
QList<QString> findCaptureDevices(const QString& primaryPath);

// Returns (path, exclusiveGrab) pairs for runtime hotkey handling:
//   siblings of primaryPath → exclusive grab + uinput re-injection
//   other EV_KEY devices exposing at least one configured hotkey → exclusive
//   grab + uinput re-injection, so global media hotkeys are swallowed everywhere.
QList<std::pair<QString, bool>> findHotkeyDevices(const QString& primaryPath,
                                                  const QSet<int>& hotkeyCodes);

// Returns (path, "Device Name  [path]") for every device that has KEY_VOLUMEUP
// or KEY_VOLUMEDOWN.
QList<std::pair<QString, QString>> getVolumeDevices();

// ─── Profile resolution helpers (point 8) ────────────────────────────────────
// Pure functions — no I/O — so unit tests can drive them directly.
enum class ProfileHotkeyAction
{
    None,
    VolumeUp,
    VolumeDown,
    Mute,
    DuckingToggle,
};

struct ProfileHotkeyMatch
{
    QString profileId;
    ProfileHotkeyAction action = ProfileHotkeyAction::None;
};

// Map a set of raw evdev modifier codes (KEY_LEFTCTRL, KEY_RIGHTCTRL,
// KEY_LEFTSHIFT, KEY_RIGHTSHIFT) to the canonical Modifier set used by
// profile matching. L/R variants collapse.
QSet<Modifier> normalizeHeldModifiers(const QSet<int>& heldRaw);

// Find the most specific profile whose hotkey set contains `code` and whose
// required modifiers are a subset of `held`. Specificity = number of required
// modifiers; ties broken by first-in-list. Returns the profile id, or "" if
// no profile matches.
QString resolveProfile(int code, const QSet<Modifier>& held, const QList<Profile>& profiles);

ProfileHotkeyMatch resolveProfileHotkey(int code, const QSet<Modifier>& held,
                                        const QList<Profile>& profiles);

// True when `code` is one of the modifiers we track (KEY_LEFTCTRL,
// KEY_RIGHTCTRL, KEY_LEFTSHIFT, KEY_RIGHTSHIFT).
bool isTrackedModifierCode(int code);

// ─── KeyCaptureThread ─────────────────────────────────────────────────────────
// One-shot: grabs candidate devices, waits for a single key-down event,
// then releases.  KEY_ESC → cancelled(); anything else → key_captured(code).
class KeyCaptureThread : public QThread
{
    Q_OBJECT
  public:
    explicit KeyCaptureThread(const QString& devicePath, QObject* parent = nullptr);

    // Stop the thread and wait for it to finish (max 1 s).
    void cancel();

  signals:
    void key_captured(int evdevCode);
    void cancelled();

  protected:
    void run() override;

  private:
    QString m_devicePath;
    std::atomic<bool> m_running{true};
};

// ─── InputHandler ─────────────────────────────────────────────────────────────
// Reads evdev events from the selected device and its siblings.
// Hotkey events from any configured profile are intercepted and dispatched
// as Qt signals carrying the matched profile id. Modifier keys are tracked
// internally for profile selection but always forwarded to uinput so the
// desktop sees them. All other events are re-injected through a UInput
// virtual device. Supports key repeat (ev.value == 2).
// Debounce is per-(code, profileId) — Ctrl+VolUp and bare VolUp don't
// suppress each other.
//
// v1 limitation: modifier state is observed only on devices that InputHandler
// already grabs (siblings of the primary device + devices exposing any
// configured hotkey code). A modifier held on a separate keyboard with no
// media keys is NOT tracked. TODO(v2): passive read-only open of
// modifier-bearing devices.
class InputHandler : public QThread
{
    Q_OBJECT
  public:
    explicit InputHandler(QObject* parent = nullptr);

    void setProfiles(const QList<Profile>& profiles);
    QList<Profile> currentProfiles() const;

    QString devicePath() const
    {
        return m_devicePath;
    }

    // Stop any running grab and start reading from newPath.
    void startDevice(const QString& newPath);

    // Restart with the same device path and current profile config.
    void restart();

    // Stop the event loop and wait for the thread to finish.
    void stop();

  signals:
    void volume_up(const QString& profileId);
    void volume_down(const QString& profileId);
    void volume_mute(const QString& profileId);
    void ducking_toggle(const QString& profileId);

  protected:
    void run() override;

  private:
    QString m_devicePath;
    std::atomic<bool> m_running{false};

    mutable QMutex m_profilesMutex;
    QList<Profile> m_profiles; // guarded by m_profilesMutex

    // Per-(code, profileId) last-trigger timestamp (ms) for 100 ms debounce.
    QMap<QPair<int, QString>, qint64> m_lastTriggerMs;
};
