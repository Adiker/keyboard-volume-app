#pragma once
#include <QWidget>
#include <QColor>
#include <QTimer>

#ifdef HAVE_LAYER_SHELL_QT
#include <LayerShellQt/Window>
#endif

class QHBoxLayout;
class QLabel;
class QMouseEvent;
class QPushButton;
class QProgressBar;
class QScreen;
class QVBoxLayout;
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

    // Apply scale live without saving — used for settings dialog preview.
    // Call before applyPreviewColors() so font sizes use the preview scale.
    void applyPreviewScale(double scale);

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

    // Update track metadata. canSeek == false disables bar interaction.
    // lengthUs == 0 → live stream mode (bar greyed out, time shows "LIVE").
    void updateTrack(const QString& title, const QString& artist, qint64 lengthUs, bool canSeek);

    // Update playback position. Ignored when no track is loaded or during drag.
    void updatePosition(qint64 positionUs);

    // Update the play/pause button icon. Call when MprisClient::playbackStatusChanged fires.
    void updatePlaybackStatus(const QString& status);

    // Show or hide the media control buttons (prev/play-pause/next).
    // No-op when the progress row is not enabled.
    void setMediaControlsEnabled(bool on);

  signals:
    // Emitted when the user starts a seek drag — caller should suspend polling.
    void seekStarted();
    // Emitted when the user releases the seek bar — caller should resume polling.
    void seekFinished();
    // Emitted on press and every drag move with the target position in µs.
    void seekRequested(qint64 positionUs);

    // Media control button clicks — connect to MprisClient slots.
    void playPauseRequested();
    void nextRequested();
    void previousRequested();

  protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

  private:
    // ── Volume row ───────────────────────────────────────────────────────────
    Config* m_config;
    QVBoxLayout* m_mainLayout = nullptr;
    QLabel* m_labelName = nullptr;
    QProgressBar* m_bar = nullptr;
    QLabel* m_labelPct = nullptr;
    QTimer* m_hideTimer = nullptr;
    QColor m_bgColor;
    bool m_previewMode = false;
    bool m_previewHeld = false;
    int m_previewTimeoutMs = 1500;
    double m_previewScale = -1.0; // overrides config scale during settings preview; -1 = inactive

    // ── Progress row ─────────────────────────────────────────────────────────
    QWidget* m_progressRow = nullptr; // container — show/hide as a unit
    QVBoxLayout* m_progressLayout = nullptr;
    QLabel* m_labelTrack = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_labelTime = nullptr;

    // ── Media controls row (within progress row) ──────────────────────────────
    QWidget* m_controlsRow = nullptr;
    QHBoxLayout* m_controlsLayout = nullptr;
    QPushButton* m_btnPrev = nullptr;
    QPushButton* m_btnPlayPause = nullptr;
    QPushButton* m_btnNext = nullptr;
    QString m_playbackStatus; // "Playing" / "Paused" / "Stopped"

    bool m_progressEnabled = false;
    bool m_progressVisible = false;
    bool m_mediaControlsEnabled = true;
    qint64 m_trackLengthUs = 0;
    qint64 m_lastPositionUs = 0;
    bool m_canSeek = false;
    bool m_seeking = false; // true while user is dragging the seek bar
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
    // Clamp (absX, absY) so the window stays within its screen's available geometry.
    std::pair<int, int> clampedPos(int absX, int absY) const;

    // Resize OSD and re-apply position (handles both X11 and layer-shell).
    void applySize();

    // Scale a pixel dimension by the configured osdScale factor.
    int scaled(int base) const;

    // Re-apply all inner widget sizes/margins after osdScale changes.
    // Must be called from reloadStyles() whenever scale may have changed.
    void rescale();

    // Update m_labelName text based on progressLabelMode + cached track info.
    void refreshNameLabel();
    void finishSeeking();

    // Format microseconds → "m:ss".
    static QString formatTime(qint64 us);
};
