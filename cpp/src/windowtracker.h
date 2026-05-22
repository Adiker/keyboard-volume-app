#pragma once
#include <QThread>
#include <QString>
#include <atomic>

class WindowTracker : public QThread
{
    Q_OBJECT
  public:
    explicit WindowTracker(QObject* parent = nullptr);
    ~WindowTracker() override;

    void stop();
    void start();

  signals:
    void focusedBinaryChanged(const QString& binaryName);
    void error(const QString& message);

  protected:
    void run() override;

  private:
    enum class Backend
    {
        None,
        Xcb,
        KWinScript,
        WaylandForeignToplevel,
    };

    Backend chooseBackend() const;
    void runXcb();
    bool runKWinScript();
    bool runWayland();

    QObject* m_kwinReceiver = nullptr;
    bool m_kwinReceiverRegistered = false;
    std::atomic<bool> m_running{false};
};
