#pragma once
#include <QThread>
#include <QSet>
#include <QList>
#include <QString>
#include <QMap>

// ─── Device enumeration helpers ───────────────────────────────────────────────
// Returns absolute paths of all /dev/input/event* devices.
QList<QString> listEvdevDevices();

// Returns all evdev nodes that share the same physical parent as primaryPath
// AND expose at least one EV_KEY capability.
QList<QString> findSiblingDevices(const QString &primaryPath);

// Returns devices for the key-capture dialog:
//   siblings of primaryPath + all other EV_KEY devices
QList<QString> findCaptureDevices(const QString &primaryPath);

// Returns (path, exclusiveGrab) pairs for runtime hotkey handling:
//   siblings of primaryPath → exclusive grab + uinput re-injection
//   other EV_KEY devices exposing at least one configured hotkey → exclusive
//   grab + uinput re-injection, so global media hotkeys are swallowed everywhere.
QList<std::pair<QString, bool>> findHotkeyDevices(
    const QString &primaryPath,
    const QSet<int> &hotkeyCodes);

// ─── KeyCaptureThread ─────────────────────────────────────────────────────────
// One-shot: grabs candidate devices, waits for a single key-down event,
// then releases.  KEY_ESC → cancelled(); anything else → key_captured(code).
class KeyCaptureThread : public QThread
{
    Q_OBJECT
public:
    explicit KeyCaptureThread(const QString &devicePath, QObject *parent = nullptr);

    // Stop the thread and wait for it to finish (max 1 s).
    void cancel();

signals:
    void key_captured(int evdevCode);
    void cancelled();

protected:
    void run() override;

private:
    QString       m_devicePath;
    volatile bool m_running = true;
};

// ─── InputHandler ─────────────────────────────────────────────────────────────
// Reads evdev events from the selected device and its siblings.
// Hotkey events are intercepted and emitted as Qt signals.
// All other events are re-injected through a UInput virtual device.
// 100 ms debounce per key code.
class InputHandler : public QThread
{
    Q_OBJECT
public:
    explicit InputHandler(QObject *parent = nullptr);

    void setHotkeys(int up, int down, int mute);
    std::tuple<int, int, int> currentHotkeys() const;

    QString devicePath() const { return m_devicePath; }

    // Stop any running grab and start reading from newPath.
    void startDevice(const QString &newPath);

    // Restart with the same device path and current hotkey config.
    void restart();

    // Stop the event loop and wait for the thread to finish.
    void stop();

signals:
    void volume_up();
    void volume_down();
    void volume_mute();

protected:
    void run() override;

private:
    QString m_devicePath;
    volatile bool m_running = false;
    int m_keyUp   = 115;   // KEY_VOLUMEUP
    int m_keyDown = 114;   // KEY_VOLUMEDOWN
    int m_keyMute = 113;   // KEY_MUTE

    // Per-code last-trigger timestamp (ms) for 100 ms debounce
    QMap<int, qint64> m_lastTriggerMs;
};
