#pragma once
#include <QThread>
#include <QString>
#include <atomic>

struct xcb_connection_t;
using xcb_atom_t = unsigned int;
using xcb_window_t = unsigned int;

class WindowTracker : public QThread
{
    Q_OBJECT
  public:
    explicit WindowTracker(QObject* parent = nullptr);
    ~WindowTracker() override;

    void stop();

  signals:
    void focusedBinaryChanged(const QString& binaryName);
    void error(const QString& message);

  protected:
    void run() override;

  private:
    QString windowToBinary(xcb_connection_t* conn, xcb_window_t win, xcb_atom_t pidAtom) const;

    xcb_connection_t* m_conn = nullptr;
    std::atomic<bool> m_running{false};
};
