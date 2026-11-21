#pragma once
#include <QThread>
#include <QString>
#include <atomic>
#include <cstdint>

class WindowTracker : public QThread
{
    Q_OBJECT
  public:
    explicit WindowTracker(
        QObject* parent = nullptr,
        const QString& serviceName = QStringLiteral("org.keyboardvolumeapp"),
        const QString& pluginName = QStringLiteral("keyboard-volume-app-focus-tracker"));
    ~WindowTracker() override;

    void stop();
    void start();

  signals:
    void focusedBinaryChanged(const QString& binaryName);
    void error(const QString& message);

  protected:
    void run() override;

  private:
    enum class Backend : std::uint8_t
    {
        None,
        HyprlandIpc,
        NiriIpc,
        SwayIpc,
        KWinScript,
        WaylandForeignToplevel,
        Xcb,
    };

    Backend chooseBackend() const;
    void runXcb();
    bool runKWinScript();
    bool runWayland();
    bool runHyprland();
    bool runNiri();
    bool runSway();

    QString m_token;
    QString m_serviceName;
    QString m_pluginName;
    QObject* m_kwinReceiver = nullptr;
    bool m_kwinReceiverRegistered = false;
    std::atomic<bool> m_running{false};
};
