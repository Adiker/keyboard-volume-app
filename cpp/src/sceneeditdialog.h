#pragma once
#include "config.h" // AudioScene, SceneTarget, HotkeyBinding
#include <QDialog>
#include <QList>

class QLineEdit;
class QTableWidget;
class HotkeyCapture;
class Config;
class InputHandler;

// Sub-dialog for editing a single audio scene.
// Used by SettingsDialog when adding or editing a scene entry.
// A scene carries a name, an optional global hotkey, and a list of targets.
// Each target sets an app's volume and/or mute state when the scene is applied.
class SceneEditDialog : public QDialog
{
    Q_OBJECT
  public:
    SceneEditDialog(const AudioScene& initial, Config* config, InputHandler* inputHandler,
                    QWidget* parent = nullptr);

    // Scene assembled from the dialog widgets after acceptance.
    // Preserves the original id (or leaves it empty for SettingsDialog to assign).
    AudioScene result() const;

  private slots:
    void onAddTarget();
    void onEditTarget();
    void onRemoveTarget();

  private:
    void refreshTargetsTable();
    int selectedTargetRow() const;
    // Opens the single-target editor on `target`. Returns true if accepted.
    bool editTargetDialog(SceneTarget& target);

    AudioScene m_initial;
    Config* m_config;
    InputHandler* m_inputHandler;

    QLineEdit* m_name = nullptr;
    HotkeyCapture* m_hotkey = nullptr;
    QTableWidget* m_targetsTable = nullptr;
    QList<SceneTarget> m_targets; // working copy until accept()
};
