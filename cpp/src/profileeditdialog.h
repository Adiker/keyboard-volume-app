#pragma once
#include "config.h" // Profile, Modifier
#include <QDialog>

class QLineEdit;
class QCheckBox;
class QSlider;
class QSpinBox;
class AppListWidget;
class HotkeyCapture;
class InputHandler;

// Sub-dialog for editing a single audio profile.
// Used by SettingsDialog when adding or editing a profile entry.
class ProfileEditDialog : public QDialog
{
    Q_OBJECT
  public:
    ProfileEditDialog(const Profile& initial, Config* config, InputHandler* inputHandler,
                      QWidget* parent = nullptr);

    // Profile assembled from the dialog widgets after acceptance.
    // Preserves the original id (or assigns a new one when initial.id was empty).
    Profile result() const;

  private:
    Profile m_initial;
    Config* m_config;
    InputHandler* m_inputHandler;

    QLineEdit* m_name = nullptr;
    AppListWidget* m_appList = nullptr;
    QCheckBox* m_modCtrl = nullptr;
    QCheckBox* m_modShift = nullptr;
    QCheckBox* m_duckingEnabled = nullptr;
    QSlider* m_duckingSlider = nullptr;
    QSpinBox* m_duckingSpin = nullptr;
    QCheckBox* m_autoSwitch = nullptr;
    HotkeyCapture* m_hkUp = nullptr;
    HotkeyCapture* m_hkDown = nullptr;
    HotkeyCapture* m_hkMute = nullptr;
    HotkeyCapture* m_hkDucking = nullptr;
};
