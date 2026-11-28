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
#include <cstdint>
#include <optional>
#include <QBitArray>
#include <linux/input.h>

struct libevdev;

// ─── Device enumeration helpers ───────────────────────────────────────────────
// Returns absolute paths of all /dev/input/event* devices.
QList<QString> listEvdevDevices();

// Returns all evdev nodes that share the same physical parent as primaryPath
// AND expose at least one EV_KEY or EV_REL capability.
QList<QString> findSiblingDevices(const QString& primaryPath);

// Returns devices for the key-capture dialog:
//   siblings of primaryPath + all other EV_KEY / scroll-capable EV_REL devices
QList<QString> findCaptureDevices(const QString& primaryPath);

// Kernel input sysfs bitmap format: high words first, native/compat word width.
// Malformed data returns nullopt so enumeration can fall back to libevdev.
std::optional<QBitArray> parseInputCapabilityBitmap(const QByteArray& text, int wordBits);

// True if the device exposes at least one assigned key/relative binding.
bool deviceHasConfiguredHotkey(const libevdev* dev, const QSet<HotkeyBinding>& hotkeys);

// True if the device exposes any tracked modifier key (Left/Right Ctrl or Shift).
bool deviceHasTrackedModifier(const libevdev* dev);

struct HotkeyDeviceCandidate
{
    QString path;
    QString phys;
    bool usefulInput = false;
    bool hasHotkey = false;
    bool hasModifiers = false;
};

// Pure device selection helper shared by findHotkeyDevices() and unit tests:
//   - devices with configured hotkeys → exclusive grab + uinput mirror
//   - siblings without configured hotkeys → passive modifier observation
//   - independent keyboards with tracked modifiers → passive read-only observation
QList<std::pair<QString, bool>> selectHotkeyDevices(const QString& primaryPath,
                                                    const QList<HotkeyDeviceCandidate>& candidates);

// Returns (path, exclusiveGrab) pairs for runtime hotkey handling:
//   - devices exposing any configured hotkey binding → exclusive grab + uinput
//   - siblings without configured bindings → passive observation
//   - independent modifier-bearing devices → passive read-only observation
QList<std::pair<QString, bool>> findHotkeyDevices(const QString& primaryPath,
                                                  const QSet<HotkeyBinding>& hotkeys);

// Returns (path, "Device Name  [path]") for every device that has KEY_VOLUMEUP
// or KEY_VOLUMEDOWN.
QList<std::pair<QString, QString>> getVolumeDevices();

// ─── Profile resolution helpers (point 8) ────────────────────────────────────
// Pure functions — no I/O — so unit tests can drive them directly.
enum class ProfileHotkeyAction : std::uint8_t
{
    None,
    VolumeUp,
    VolumeDown,
    Mute,
    DuckingToggle,
    ShowVolume,
};

struct ProfileHotkeyMatch
{
    QString profileId;
    ProfileHotkeyAction action = ProfileHotkeyAction::None;
};

// ─── Media hotkey resolution (global, MPRIS dispatch) ────────────────────────
// Profile-independent. Resolved after profile match fails so per-profile
// bindings keep priority when a user has chosen to bind the same key with
// modifiers (e.g. Ctrl+KEY_PLAYPAUSE → VolumeUp on a profile, bare
// KEY_PLAYPAUSE → media play/pause).
enum class MediaAction : std::uint8_t
{
    None,
    PlayPause,
    Next,
    Previous,
    Stop,
};

MediaAction resolveMediaHotkey(const HotkeyBinding& binding, const MediaHotkeyConfig& cfg);
MediaAction resolveMediaHotkey(int code, const MediaHotkeyConfig& cfg);

// ─── OSD layout hotkey resolution (visible-gated in InputHandler) ────────────
enum class OsdLayoutAction : std::uint8_t
{
    None,
    SnapUp,
    SnapDown,
    SnapLeft,
    SnapRight,
    ScaleUp,
    ScaleDown,
};

struct OsdLayoutInputConfig
{
    bool positionControlsEnabled = false;
    bool positionKeyboardEnabled = false;
    OsdLayoutHotkeyConfig hotkeys;
};

OsdLayoutAction resolveOsdLayoutAction(const HotkeyBinding& binding,
                                       const OsdLayoutHotkeyConfig& cfg);
OsdLayoutAction resolveOsdLayoutAction(const HotkeyBinding& binding, const input_event& ev,
                                       const OsdLayoutHotkeyConfig& cfg);

inline bool operator==(const OsdLayoutInputConfig& a, const OsdLayoutInputConfig& b)
{
    return a.positionControlsEnabled == b.positionControlsEnabled &&
           a.positionKeyboardEnabled == b.positionKeyboardEnabled && a.hotkeys == b.hotkeys;
}
inline bool operator!=(const OsdLayoutInputConfig& a, const OsdLayoutInputConfig& b)
{
    return !(a == b);
}

// ─── Scene hotkey resolution (global, scene dispatch) ────────────────────────
// Resolved after profile match fails but before media so a profile binding
// always wins. When two scenes share the same hotkey binding, the first scene
// in the list wins. Returns the scene id, or "" when no scene matches.
QString resolveSceneHotkey(const HotkeyBinding& binding, const QList<AudioScene>& scenes);
QString resolveSceneHotkey(int code, const QList<AudioScene>& scenes);

// Map a set of raw evdev modifier codes (KEY_LEFTCTRL, KEY_RIGHTCTRL,
// KEY_LEFTSHIFT, KEY_RIGHTSHIFT) to the canonical Modifier set used by
// profile matching. L/R variants collapse.
QSet<Modifier> normalizeHeldModifiers(const QSet<int>& heldRaw);

// Aggregate raw held modifier codes tracked per open device into the canonical
// Modifier set. Keeping per-device state prevents a disconnect or release on
// one keyboard from wiping modifiers still held on another.
QSet<Modifier> aggregateHeldModifiers(const QMap<const void*, QSet<int>>& heldByDevice);

// Find the most specific profile whose hotkey set contains `binding` and whose
// required modifiers are a subset of `held`. Specificity = number of required
// modifiers; ties broken by first-in-list. Returns the profile id, or "" if no
// profile matches. The int overloads treat the code as an EV_KEY binding.
QString resolveProfile(int code, const QSet<Modifier>& held, const QList<Profile>& profiles);
QString resolveProfile(const HotkeyBinding& binding, const QSet<Modifier>& held,
                       const QList<Profile>& profiles);

ProfileHotkeyMatch resolveProfileHotkey(int code, const QSet<Modifier>& held,
                                        const QList<Profile>& profiles);
ProfileHotkeyMatch resolveProfileHotkey(const HotkeyBinding& binding, const QSet<Modifier>& held,
                                        const QList<Profile>& profiles);

// True when `binding` represents this evdev event. EV_REL bindings compare the
// sign of ev.value so high-resolution/multistep wheel deltas still match.
bool matchesInputEvent(const HotkeyBinding& binding, const input_event& ev);

// True when `code` is one of the modifiers we track (KEY_LEFTCTRL,
// KEY_RIGHTCTRL, KEY_LEFTSHIFT, KEY_RIGHTSHIFT).
bool isTrackedModifierCode(int code);

// ─── KeyCaptureThread ─────────────────────────────────────────────────────────
// One-shot: grabs candidate devices, waits for a single key-down or scroll
// event, then releases. KEY_ESC → cancelled(); anything else → hotkey_captured().
class KeyCaptureThread : public QThread
{
    Q_OBJECT
  public:
    explicit KeyCaptureThread(const QString& devicePath, QObject* parent = nullptr);

    // Stop the thread and wait for it to finish (max 1 s).
    void cancel();

  signals:
    void hotkey_captured(HotkeyBinding binding);
    void cancelled();

  protected:
    void run() override;

  private:
    QString m_devicePath;
    std::atomic<bool> m_running{true};
};

// ─── InputHandler ─────────────────────────────────────────────────────────────
// Reads evdev events from the selected device, its siblings, any other device
// exposing configured hotkey bindings (exclusive grab + uinput mirror), and
// independent modifier-bearing keyboards (passive read-only, no grab/uinput).
// Hotkey events from any configured profile are intercepted and dispatched
// as Qt signals carrying the matched profile id. Modifier keys are tracked
// per device for profile selection so a modifier held on an independent
// keyboard qualifies hotkeys pressed on a separate multimedia keypad. Captured
// events are forwarded to uinput; passive device events reach the desktop
// directly. Mirrors are prepared before grabbing, and nodes with held
// keys/buttons defer their handoff. Supports key repeat (ev.value == 2).
// Debounce is per-(code, profileId) — Ctrl+VolUp and bare VolUp don't suppress
// each other.
class InputHandler : public QThread
{
    Q_OBJECT
  public:
    explicit InputHandler(QObject* parent = nullptr);

    void setProfiles(const QList<Profile>& profiles);
    QList<Profile> currentProfiles() const;

    void setMediaHotkeys(const MediaHotkeyConfig& cfg);
    MediaHotkeyConfig currentMediaHotkeys() const;

    void setScenes(const QList<AudioScene>& scenes);
    QList<AudioScene> currentScenes() const;

    void setOsdLayoutInput(const OsdLayoutInputConfig& cfg);
    OsdLayoutInputConfig currentOsdLayoutInput() const;

    void setOsdLayoutKeysActive(bool active);

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
    void show_volume(const QString& profileId);

    // Media controls — global, no profileId. Slots in App connect these to
    // MprisClient. No-op when no MPRIS player is active.
    void media_play_pause();
    void media_next();
    void media_previous();
    void media_stop();

    // Scene apply — global, carries the matched scene id. App connects this to
    // VolumeController::applyScene via Config lookup.
    void scene_apply(const QString& sceneId);

    // OSD layout — snap/scale while the overlay is visible (visible-gated).
    void osdLayoutAction(OsdLayoutAction action);

    // Input device lifecycle (initial attach, hot-plug attach, disconnect).
    void deviceConnected(const QString& path, const QString& name);
    void deviceDisconnected(const QString& path, const QString& name);

  protected:
    void run() override;

  private:
    QString m_devicePath;
    std::atomic<bool> m_running{false};

    mutable QMutex m_profilesMutex;
    QList<Profile> m_profiles;             // guarded by m_profilesMutex
    MediaHotkeyConfig m_mediaHotkeys;      // guarded by m_profilesMutex
    QList<AudioScene> m_scenes;            // guarded by m_profilesMutex
    OsdLayoutInputConfig m_osdLayoutInput; // guarded by m_profilesMutex

    std::atomic<bool> m_osdLayoutKeysActive{false};

    // Per-(binding, profileId) last-trigger timestamp (ms) for 100 ms debounce.
    // Media hotkeys reuse the same map with profileId == "__media__".
    // Scene hotkeys reuse it with profileId == "__scene__:" + sceneId.
    QMap<QPair<HotkeyBinding, QString>, qint64> m_lastTriggerMs;
};
