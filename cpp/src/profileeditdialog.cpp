#include "profileeditdialog.h"
#include "applistwidget.h"
#include "settingsdialog.h"   // HotkeyCapture
#include "i18n.h"

#include <QLineEdit>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>

ProfileEditDialog::ProfileEditDialog(const Profile &initial,
                                     Config *config,
                                     InputHandler *inputHandler,
                                     QWidget *parent)
    : QDialog(parent)
    , m_initial(initial)
    , m_config(config)
    , m_inputHandler(inputHandler)
{
    setWindowTitle(::tr(QStringLiteral("settings.profiles.edit_title")));
    setMinimumWidth(420);
    setWindowModality(Qt::ApplicationModal);

    auto *layout = new QVBoxLayout(this);
    auto *form   = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->setSpacing(8);

    // Name
    m_name = new QLineEdit(initial.name, this);
    form->addRow(::tr(QStringLiteral("settings.profiles.name_label")), m_name);

    // App
    m_appList = new AppListWidget(this);
    m_appList->populate(m_config);
    if (!initial.app.isEmpty())
        m_appList->setSelectedApp(initial.app);
    form->addRow(::tr(QStringLiteral("app_selector.subtitle")), m_appList);

    // Modifiers
    m_modCtrl  = new QCheckBox(::tr(QStringLiteral("settings.profiles.modifier_ctrl")),  this);
    m_modShift = new QCheckBox(::tr(QStringLiteral("settings.profiles.modifier_shift")), this);
    m_modCtrl->setChecked (initial.modifiers.contains(Modifier::Ctrl));
    m_modShift->setChecked(initial.modifiers.contains(Modifier::Shift));
    auto *modRow = new QHBoxLayout;
    modRow->addWidget(m_modCtrl);
    modRow->addWidget(m_modShift);
    modRow->addStretch();
    auto *modWrap = new QWidget(this);
    modWrap->setLayout(modRow);
    form->addRow(::tr(QStringLiteral("settings.profiles.col_modifiers")), modWrap);

    // Hotkeys
    m_hkUp   = new HotkeyCapture(initial.hotkeys.volumeUp,   m_inputHandler, this);
    m_hkDown = new HotkeyCapture(initial.hotkeys.volumeDown, m_inputHandler, this);
    m_hkMute = new HotkeyCapture(initial.hotkeys.mute,       m_inputHandler, this);
    form->addRow(::tr(QStringLiteral("settings.hotkey.volume_up")),   m_hkUp);
    form->addRow(::tr(QStringLiteral("settings.hotkey.volume_down")), m_hkDown);
    form->addRow(::tr(QStringLiteral("settings.hotkey.mute")),        m_hkMute);

    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

Profile ProfileEditDialog::result() const
{
    Profile p;
    p.id   = m_initial.id;       // preserve id; SettingsDialog assigns one for new
    p.name = m_name->text().trimmed();
    if (p.name.isEmpty()) p.name = QStringLiteral("Profile");
    p.app  = m_appList->selectedAppName();

    p.hotkeys.volumeUp   = m_hkUp->evdevCode();
    p.hotkeys.volumeDown = m_hkDown->evdevCode();
    p.hotkeys.mute       = m_hkMute->evdevCode();

    if (m_modCtrl->isChecked())  p.modifiers.insert(Modifier::Ctrl);
    if (m_modShift->isChecked()) p.modifiers.insert(Modifier::Shift);

    return p;
}
