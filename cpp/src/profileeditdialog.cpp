#include "profileeditdialog.h"
#include "applistwidget.h"
#include "settingsdialog.h" // HotkeyCapture
#include "i18n.h"

#include <QLineEdit>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QSlider>
#include <QSpinBox>

ProfileEditDialog::ProfileEditDialog(const Profile& initial, Config* config,
                                     InputHandler* inputHandler, QWidget* parent)
    : QDialog(parent), m_initial(initial), m_config(config), m_inputHandler(inputHandler)
{
    setWindowTitle(::tr(QStringLiteral("settings.profiles.edit_title")));
    setMinimumWidth(420);
    setWindowModality(Qt::ApplicationModal);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->setSpacing(8);

    // Name
    m_name = new QLineEdit(initial.name, this);
    form->addRow(::tr(QStringLiteral("settings.profiles.name_label")), m_name);

    // App
    m_appList = new AppListWidget(this);
    m_appList->populate(m_config);
    if (!initial.app.isEmpty()) m_appList->setSelectedApp(initial.app);
    form->addRow(::tr(QStringLiteral("app_selector.subtitle")), m_appList);

    // Modifiers
    m_modCtrl = new QCheckBox(::tr(QStringLiteral("settings.profiles.modifier_ctrl")), this);
    m_modShift = new QCheckBox(::tr(QStringLiteral("settings.profiles.modifier_shift")), this);
    m_modCtrl->setChecked(initial.modifiers.contains(Modifier::Ctrl));
    m_modShift->setChecked(initial.modifiers.contains(Modifier::Shift));
    auto* modRow = new QHBoxLayout;
    modRow->addWidget(m_modCtrl);
    modRow->addWidget(m_modShift);
    modRow->addStretch();
    auto* modWrap = new QWidget(this);
    modWrap->setLayout(modRow);
    form->addRow(::tr(QStringLiteral("settings.profiles.col_modifiers")), modWrap);

    // Hotkeys
    m_hkUp = new HotkeyCapture(initial.hotkeys.volumeUp, m_inputHandler, this);
    m_hkDown = new HotkeyCapture(initial.hotkeys.volumeDown, m_inputHandler, this);
    m_hkMute = new HotkeyCapture(initial.hotkeys.mute, m_inputHandler, this);
    m_hkShow = new HotkeyCapture(initial.hotkeys.show, m_inputHandler, this);
    form->addRow(::tr(QStringLiteral("settings.hotkey.volume_up")), m_hkUp);
    form->addRow(::tr(QStringLiteral("settings.hotkey.volume_down")), m_hkDown);
    form->addRow(::tr(QStringLiteral("settings.hotkey.mute")), m_hkMute);
    form->addRow(::tr(QStringLiteral("settings.hotkey.show")), m_hkShow);

    // Audio ducking
    m_duckingEnabled =
        new QCheckBox(::tr(QStringLiteral("settings.profiles.ducking_enabled")), this);
    m_duckingEnabled->setChecked(initial.ducking.enabled);
    form->addRow(QString(), m_duckingEnabled);

    m_duckingSlider = new QSlider(Qt::Horizontal, this);
    m_duckingSlider->setRange(0, 100);
    m_duckingSlider->setValue(initial.ducking.volume);
    m_duckingSpin = new QSpinBox(this);
    m_duckingSpin->setRange(0, 100);
    m_duckingSpin->setSuffix(QStringLiteral("%"));
    m_duckingSpin->setValue(initial.ducking.volume);
    connect(m_duckingSlider, &QSlider::valueChanged, m_duckingSpin, &QSpinBox::setValue);
    connect(m_duckingSpin, qOverload<int>(&QSpinBox::valueChanged), m_duckingSlider,
            &QSlider::setValue);

    auto* duckLevelRow = new QHBoxLayout;
    duckLevelRow->addWidget(m_duckingSlider, 1);
    duckLevelRow->addWidget(m_duckingSpin);
    auto* duckLevelWrap = new QWidget(this);
    duckLevelWrap->setLayout(duckLevelRow);
    form->addRow(::tr(QStringLiteral("settings.profiles.ducking_volume")), duckLevelWrap);

    m_hkDucking = new HotkeyCapture(initial.ducking.hotkey, m_inputHandler, this);
    form->addRow(::tr(QStringLiteral("settings.profiles.ducking_hotkey")), m_hkDucking);

    // Auto-switch participation
    m_autoSwitch = new QCheckBox(::tr(QStringLiteral("settings.profiles.auto_switch")), this);
    m_autoSwitch->setChecked(initial.autoSwitch);
    form->addRow(QString(), m_autoSwitch);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

Profile ProfileEditDialog::result() const
{
    Profile p;
    p.id = m_initial.id; // preserve id; SettingsDialog assigns one for new
    p.name = m_name->text().trimmed();
    if (p.name.isEmpty()) p.name = QStringLiteral("Profile");
    p.app = m_appList->selectedAppName();

    p.hotkeys.volumeUp = m_hkUp->binding();
    p.hotkeys.volumeDown = m_hkDown->binding();
    p.hotkeys.mute = m_hkMute->binding();
    p.hotkeys.show = m_hkShow->binding();

    if (m_modCtrl->isChecked()) p.modifiers.insert(Modifier::Ctrl);
    if (m_modShift->isChecked()) p.modifiers.insert(Modifier::Shift);

    p.ducking.enabled = m_duckingEnabled->isChecked();
    p.ducking.volume = m_duckingSpin->value();
    p.ducking.hotkey = m_hkDucking->binding();
    p.autoSwitch = m_autoSwitch->isChecked();

    return p;
}
