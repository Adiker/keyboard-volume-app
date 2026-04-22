#pragma once
#include <QDialog>
#include <QPushButton>

class QSpinBox;
class QComboBox;
class Config;
class InputHandler;

// ─── ColorButton ──────────────────────────────────────────────────────────────
// Button that shows the selected hex color as its background.
// Opens QColorDialog on click and emits colorChanged() when a new color is chosen.
class ColorButton : public QPushButton
{
    Q_OBJECT
public:
    explicit ColorButton(const QString &hexColor, QWidget *parent = nullptr);

    QString color() const { return m_color; }
    void    setColor(const QString &hexColor);

signals:
    void colorChanged(const QString &hexColor);

private slots:
    void pick();

private:
    QString m_color;
};

// ─── KeyCaptureDialog ─────────────────────────────────────────────────────────
// Modal dialog that waits for a single key press.
//
// Two parallel capture paths (whichever fires first wins):
//   1. KeyCaptureThread  — grabs devices via evdev; catches media/CC keys.
//   2. keyPressEvent     — catches regular keys forwarded by XWayland.
//      Conversion: evdev_code = X11_keycode − 8.
//
// ESC or Cancel → rejected; any other key → accepted, capturedCode() set.
class KeyCaptureThread;   // forward — declared in inputhandler.h

class KeyCaptureDialog : public QDialog
{
    Q_OBJECT
public:
    explicit KeyCaptureDialog(const QString &devicePath, QWidget *parent = nullptr);
    ~KeyCaptureDialog() override;

    // The captured evdev code, or -1 if cancelled.
    int capturedCode() const { return m_code; }

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void onCaptured(int code);
    void doCancel();

    int                m_code  = -1;
    bool               m_done  = false;
    KeyCaptureThread  *m_thread = nullptr;
};

// ─── HotkeyCapture ────────────────────────────────────────────────────────────
// Button displaying the currently assigned key.
// Click → opens KeyCaptureDialog, stops/restarts InputHandler around it.
class HotkeyCapture : public QPushButton
{
    Q_OBJECT
public:
    explicit HotkeyCapture(int evdevCode, InputHandler *inputHandler,
                           QWidget *parent = nullptr);

    int evdevCode() const { return m_code; }

private slots:
    void capture();

private:
    static QString keyDisplayName(int code);
    void updateDisplay();

    int            m_code;
    InputHandler  *m_inputHandler;
};

// ─── SettingsDialog ───────────────────────────────────────────────────────────
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(Config *config, InputHandler *inputHandler,
                            QWidget *parent = nullptr);

signals:
    // Emitted live while user adjusts screen/x/y: (screenIdx, x, y)
    void positionPreview(int screenIdx, int x, int y);
    // Emitted live as any color or opacity changes: (colorBg, colorText, colorBar, opacity)
    void stylePreview(const QString &colorBg, const QString &colorText,
                      const QString &colorBar, int opacity);
    // Emitted while Preview button is held: (screenIdx, x, y)
    void previewHeldRequested(int screenIdx, int x, int y);
    // Emitted when Preview button released: (timeoutMs)
    void previewReleased(int timeoutMs);

private slots:
    void onPreviewPressed();
    void onPreviewReleased();
    void emitPositionPreview();
    void emitStylePreview();
    void saveAndAccept();

private:
    void buildUi();

    Config        *m_config;
    InputHandler  *m_inputHandler;

    QComboBox     *m_lang       = nullptr;
    QComboBox     *m_screen     = nullptr;
    QSpinBox      *m_timeout    = nullptr;
    QSpinBox      *m_osdX       = nullptr;
    QSpinBox      *m_osdY       = nullptr;
    QSpinBox      *m_step       = nullptr;
    ColorButton   *m_colorBg    = nullptr;
    ColorButton   *m_colorText  = nullptr;
    ColorButton   *m_colorBar   = nullptr;
    QSpinBox      *m_opacity    = nullptr;
    HotkeyCapture *m_hkUp       = nullptr;
    HotkeyCapture *m_hkDown     = nullptr;
    HotkeyCapture *m_hkMute     = nullptr;
};
