#pragma once
#include <QWidget>
#include <QColor>
#include <QTimer>

#ifdef HAVE_LAYER_SHELL_QT
#include <LayerShellQt/Window>
#endif

class QLabel;
class QProgressBar;
class QScreen;
class Config;

// Frameless always-on-top OSD overlay.
// Base size: 220×70 (volume only).
// Expanded size: 220×112 (volume + progress row).
//
// Background is drawn manually in paintEvent — Qt skips stylesheet background
// painting on translucent top-level windows (WA_TranslucentBackground).
//
// Progress row (optional, controlled by OsdConfig::progressEnabled):
//   - Track label  — title / app name / both, per progressLabelMode
//   - QProgressBar — 0–1000 range for sub-second precision
//   - Time label   — "m:ss / m:ss"  (or "LIVE" for streams)
//
// Hover over the OSD suspends the hide timer so the user can read / seek.
class OSDWindow : public QWidget
{
    Q_OBJECT
  public:
    explicit OSDWindow(Config* config, QWidget* parent = nullptr);

    // Display OSD at configured position.  volume is 0.0–1.0.
    void showVolume(const QString& appName, double volume, bool muted = false);

    // Show a preview OSD at the given screen-relative position.
    void showPreview(int screenIdx, int x, int y, int timeoutMs = 1500);

    // Hide the OSD if it is in preview mode.
    void hidePreview();

    // Show OSD without auto-hide (while Preview button is held).
    void showPreviewHeld(int screenIdx, int x, int y);

    // Start the hide timer after the Preview button is released.
    void releasePreview(int timeoutMs);

    // Apply colors live without saving — used for settings dialog preview.
    void applyPreviewColors(const QString& colorBg, const QString& colorText,
                            const QString& colorBar, int opacity = 85);

    // Call after config changes to refresh colors and position.
    void reloadStyles();

    // Initialize wlr-layer-shell surface. Must be called once from App::init()
    // before the first show(). No-op when g_nativeWayland is false or
    // HAVE_LAYER_SHELL_QT is not defined.
    void initLayerShell();

    // ── Progress row API ─────────────────────────────────────────────────────
    // Enable / disable the progress row (reads OsdConfig::progressEnabled).
    // Call after config changes.
    void setProgressEnabled(bool on);

    // Show or hide the progress row at runtime (e.g. when a player appears /
    // disappears). No-op when progressEnabled == false.
    void setProgressVisible(bool on);

    // Update track metadata. canSeek == false disables the bar interaction
    // (visual only in this PR; seek wired in PR 3).
    // lengthUs == 0 → live stream mode (bar greyed out, time shows "LIVE").
    void updateTrack(const QString& title, const QString& artist, qint64 lengthUs, bool canSeek);

    // Update playback position. Ignored when no track is loaded or during drag.
    void updatePosition(qint64 positionUs);

  signals:
    // Emitted when the user clicks / drags the seek bar (wired in PR 3).
    void seekRequested(qint64 positionUs);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

  private:
    // ── Volume row ───────────────────────────────────────────────────────────
    Config* m_config;
    QLabel* m_labelName = nullptr;
    QProgressBar* m_bar = nullptr;
    QLabel* m_labelPct = nullptr;
    QTimer* m_hideTimer = nullptr;
    QColor m_bgColor;
    bool m_previewMode = false;

    // ── Progress row ─────────────────────────────────────────────────────────
    QWidget* m_progressRow = nullptr; // container — show/hide as a unit
    QLabel* m_labelTrack = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_labelTime = nullptr;

    bool m_progressEnabled = false;
    bool m_progressVisible = false;
    qint64 m_trackLengthUs = 0;
    bool m_canSeek = false;
    QString m_trackTitle;
    QString m_trackArtist;
    QString m_currentAppName; // last app name passed to showVolume()

    // ── Layer-shell ──────────────────────────────────────────────────────────
    bool m_layerShellActive = false;
    QScreen* m_lsScreen = nullptr;
#ifdef HAVE_LAYER_SHELL_QT
    LayerShellQt::Window* m_lsWindow = nullptr;
#endif

    // ── Helpers ──────────────────────────────────────────────────────────────
    void buildUi();
    void applyStyles();
    void applyColorStyles(const QString& colorBg, const QString& colorText, const QString& colorBar,
                          int opacity);
    void positionWindow(int absX, int absY);
    std::pair<int, int> absPos() const;

    // Resize OSD and re-apply position (handles both X11 and layer-shell).
    void applySize();

    // Update m_labelName text based on progressLabelMode + cached track info.
    void refreshNameLabel();

    // Format microseconds → "m:ss".
    static QString formatTime(qint64 us);
};
