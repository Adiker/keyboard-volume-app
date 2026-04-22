#pragma once
#include <QWidget>
#include <QColor>
#include <QTimer>

class QLabel;
class QProgressBar;
class Config;

// Frameless always-on-top OSD overlay (220×70 px).
// Background is drawn manually in paintEvent — Qt skips stylesheet background
// painting on translucent top-level windows (WA_TranslucentBackground).
class OSDWindow : public QWidget
{
    Q_OBJECT
public:
    explicit OSDWindow(Config *config, QWidget *parent = nullptr);

    // Display OSD at configured position.  volume is 0.0–1.0.
    void showVolume(const QString &appName, double volume, bool muted = false);

    // Show a preview OSD at the given screen-relative position.
    void showPreview(int screenIdx, int x, int y, int timeoutMs = 1500);

    // Hide the OSD if it is in preview mode.
    void hidePreview();

    // Show OSD without auto-hide (while Preview button is held).
    void showPreviewHeld(int screenIdx, int x, int y);

    // Start the hide timer after the Preview button is released.
    void releasePreview(int timeoutMs);

    // Apply colors live without saving — used for settings dialog preview.
    void applyPreviewColors(const QString &colorBg, const QString &colorText,
                            const QString &colorBar, int opacity = 85);

    // Call after config changes to refresh colors and position.
    void reloadStyles();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Config        *m_config;
    QLabel        *m_labelName  = nullptr;
    QProgressBar  *m_bar        = nullptr;
    QLabel        *m_labelPct   = nullptr;
    QTimer        *m_hideTimer  = nullptr;
    QColor         m_bgColor;
    bool           m_previewMode = false;

    void buildUi();
    void applyStyles();
    void applyColorStyles(const QString &colorBg, const QString &colorText,
                          const QString &colorBar, int opacity);
    void positionWindow(int absX, int absY);
    std::pair<int, int> absPos() const;
};
