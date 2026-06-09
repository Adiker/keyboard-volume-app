#pragma once
#include "config.h" // Profile
#include <QDialog>
#include <QList>
#include <QLineEdit>
#include <QPushButton>

class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QCheckBox;
class QTableWidget;
class QLabel;
class QScrollArea;
class QDialogButtonBox;
class QCloseEvent;
class QHideEvent;
class QShowEvent;
class Config;
class InputHandler;

// ─── ColorButton ──────────────────────────────────────────────────────────────
// Button that shows the selected hex color as its background.
// Opens QColorDialog on click and emits colorChanged() when a new color is chosen.
class ColorButton : public QPushButton
{
    Q_OBJECT
  public:
    explicit ColorButton(const QString& hexColor, QWidget* parent = nullptr);

    QString color() const
    {
        return m_color;
    }
    void setColor(const QString& hexColor);

  signals:
    void colorChanged(const QString& hexColor);

  private slots:
    void pick();

  private:
    QString m_color;
};

// ─── KeyCaptureDialog ─────────────────────────────────────────────────────────
// Modal dialog that waits for a single key press.
//
// Two parallel capture paths (whichever fires first wins):
//   1. KeyCaptureThread  — grabs devices via evdev; catches media/CC keys and wheel scroll.
//   2. keyPressEvent     — catches regular keys forwarded by XWayland.
//      Conversion: evdev_code = X11_keycode − 8.
//
// ESC or Cancel → rejected; any other key/scroll → accepted, capturedBinding() set.
class KeyCaptureThread; // forward — declared in inputhandler.h

class KeyCaptureDialog : public QDialog
{
    Q_OBJECT
  public:
    explicit KeyCaptureDialog(const QString& devicePath, QWidget* parent = nullptr);
    ~KeyCaptureDialog() override;

    // The captured binding, or an unassigned binding if cancelled.
    HotkeyBinding capturedBinding() const
    {
        return m_binding;
    }

  protected:
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

  private:
    void onCaptured(HotkeyBinding binding);
    void doCancel();

    HotkeyBinding m_binding;
    bool m_done = false;
    KeyCaptureThread* m_thread = nullptr;
};

// ─── HotkeyCapture ────────────────────────────────────────────────────────────
// Button displaying the currently assigned key.
// Click → opens KeyCaptureDialog, stops/restarts InputHandler around it.
class HotkeyCapture : public QPushButton
{
    Q_OBJECT
  public:
    explicit HotkeyCapture(HotkeyBinding binding, InputHandler* inputHandler,
                           QWidget* parent = nullptr);

    HotkeyBinding binding() const
    {
        return m_binding;
    }
    static QString keyDisplayName(const HotkeyBinding& binding);

  private slots:
    void capture();
    void unassign();

  private:
    void updateDisplay();

    HotkeyBinding m_binding;
    InputHandler* m_inputHandler;
};

// ─── SettingsDialog ───────────────────────────────────────────────────────────
class SettingsDialog : public QDialog
{
    Q_OBJECT
  public:
    explicit SettingsDialog(Config* config, InputHandler* inputHandler,
                            class VolumeController* volumeCtrl = nullptr,
                            QWidget* parent = nullptr);

  signals:
    // Emitted live while user adjusts screen/x/y: (screenIdx, x, y)
    void positionPreview(int screenIdx, int x, int y);
    // Emitted live as any color or opacity changes: (colorBg, colorText, colorBar, opacity)
    void stylePreview(const QString& colorBg, const QString& colorText, const QString& colorBar,
                      int opacity);
    // Emitted live when the OSD scale spinbox changes.
    void scalePreview(double scale);
    // Emitted while Preview button is held: (screenIdx, x, y)
    void previewHeldRequested(int screenIdx, int x, int y);
    // Emitted when Preview button released: (timeoutMs)
    void previewReleased(int timeoutMs);

    // Emitted when the user clicks "Apply" on a scene in the Scenes table.
    // Carries the (working-copy) scene so it applies even before Save. TrayApp
    // routes this to VolumeController::applyScene.
    void applySceneRequested(const AudioScene& scene);

  private slots:
    void onPreviewPressed();
    void onPreviewReleased();
    void emitPositionPreview();
    void emitStylePreview();
    void saveAndAccept();
    void updateCustomLabelVisibility();
    void onExportConfig();
    void onImportConfig();

    // Profiles section
    void onAddProfile();
    void onEditProfile();
    void onRemoveProfile();
    void onSetDefaultProfile();

    // Scenes section
    void onAddScene();
    void onEditScene();
    void onRemoveScene();
    void onDuplicateScene();
    void onApplyScene();

  protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

  private:
    void buildUi();
    void refreshProfilesTable();
    int selectedProfileRow() const;
    void refreshScenesTable();
    int selectedSceneRow() const;

    Config* m_config;
    InputHandler* m_inputHandler;
    class VolumeController* m_volumeCtrl;

    QComboBox* m_lang = nullptr;
    QComboBox* m_screen = nullptr;
    QSpinBox* m_timeout = nullptr;
    QSpinBox* m_osdX = nullptr;
    QSpinBox* m_osdY = nullptr;
    QSpinBox* m_step = nullptr;
    ColorButton* m_colorBg = nullptr;
    ColorButton* m_colorText = nullptr;
    ColorButton* m_colorBar = nullptr;
    QSpinBox* m_opacity = nullptr;
    QCheckBox* m_autoProfile = nullptr;

    // Media hotkeys (global, MPRIS dispatch — independent of profiles)
    HotkeyCapture* m_mediaPlayPause = nullptr;
    HotkeyCapture* m_mediaNext = nullptr;
    HotkeyCapture* m_mediaPrevious = nullptr;
    HotkeyCapture* m_mediaStop = nullptr;

    // Progress / MPRIS section
    QCheckBox* m_progressEnabled = nullptr;
    QCheckBox* m_progressInteractive = nullptr;
    QSpinBox* m_progressPollMs = nullptr;
    QComboBox* m_progressLabelMode = nullptr;
    // Custom-mode label controls — hidden unless progressLabelMode == "custom".
    QWidget* m_customLabelGroup = nullptr;
    QLineEdit* m_customLabelTop = nullptr;
    QLineEdit* m_customLabelBottom = nullptr;
    QCheckBox* m_customLabelShowArt = nullptr;
    QLabel* m_tokensHint = nullptr;
    QLineEdit* m_trackedPlayers = nullptr;
    QCheckBox* m_mediaControlsEnabled = nullptr;
    QCheckBox* m_exposeMpris = nullptr;
    QComboBox* m_mediaKeysOsdMode = nullptr;

    // OSD scale
    QDoubleSpinBox* m_osdScale = nullptr;

    // Profiles section
    QTableWidget* m_profilesTable = nullptr;
    QPushButton* m_btnAdd = nullptr;
    QPushButton* m_btnEdit = nullptr;
    QPushButton* m_btnRemove = nullptr;
    QPushButton* m_btnSetDefault = nullptr;
    QList<Profile> m_profiles; // working copy until saveAndAccept()

    // Scenes section
    QTableWidget* m_scenesTable = nullptr;
    QPushButton* m_btnSceneAdd = nullptr;
    QPushButton* m_btnSceneEdit = nullptr;
    QPushButton* m_btnSceneRemove = nullptr;
    QPushButton* m_btnSceneDuplicate = nullptr;
    QPushButton* m_btnSceneApply = nullptr;
    QList<AudioScene> m_scenes; // working copy until saveAndAccept()

    QScrollArea* m_scrollArea = nullptr;
    QPushButton* m_previewBtn = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
    bool m_sizeReadyToPersist = false;
};
