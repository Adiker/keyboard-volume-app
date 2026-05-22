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
#include <QListWidget>
#include <QPushButton>
#include <QMessageBox>

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

    // Apps (multi-app support)
    m_appsListWidget = new QListWidget(this);
    m_appsListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    for (const QString& app : initial.apps)
    {
        if (!app.isEmpty()) m_appsListWidget->addItem(app);
    }

    auto* addBtn = new QPushButton(::tr(QStringLiteral("settings.profiles.app_add")), this);
    auto* removeBtn = new QPushButton(::tr(QStringLiteral("settings.profiles.app_remove")), this);
    auto* upBtn = new QPushButton(::tr(QStringLiteral("settings.profiles.app_up")), this);
    auto* downBtn = new QPushButton(::tr(QStringLiteral("settings.profiles.app_down")), this);

    connect(addBtn, &QPushButton::clicked, this, [this]() {
        QDialog dlg(this);
        dlg.setWindowTitle(::tr(QStringLiteral("settings.profiles.app_add")));
        auto* lay = new QVBoxLayout(&dlg);
        auto* picker = new AppListWidget(&dlg);
        picker->populate(m_config);
        lay->addWidget(picker);
        auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        lay->addWidget(btns);
        connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() == QDialog::Accepted)
        {
            QString sel = picker->selectedAppName();
            if (!sel.isEmpty()) addAppToList(sel);
        }
    });

    connect(removeBtn, &QPushButton::clicked, this, [this]() {
        auto* item = m_appsListWidget->currentItem();
        if (item) delete m_appsListWidget->takeItem(m_appsListWidget->row(item));
    });

    connect(upBtn, &QPushButton::clicked, this, [this]() {
        int row = m_appsListWidget->currentRow();
        if (row > 0)
        {
            QListWidgetItem* item = m_appsListWidget->takeItem(row);
            m_appsListWidget->insertItem(row - 1, item);
            m_appsListWidget->setCurrentRow(row - 1);
        }
    });

    connect(downBtn, &QPushButton::clicked, this, [this]() {
        int row = m_appsListWidget->currentRow();
        if (row >= 0 && row < m_appsListWidget->count() - 1)
        {
            QListWidgetItem* item = m_appsListWidget->takeItem(row);
            m_appsListWidget->insertItem(row + 1, item);
            m_appsListWidget->setCurrentRow(row + 1);
        }
    });

    auto* appsButtons = new QHBoxLayout;
    appsButtons->addWidget(addBtn);
    appsButtons->addWidget(removeBtn);
    appsButtons->addWidget(upBtn);
    appsButtons->addWidget(downBtn);
    appsButtons->addStretch();

    auto* appsLayout = new QVBoxLayout;
    appsLayout->addWidget(m_appsListWidget);
    appsLayout->addLayout(appsButtons);

    auto* appsWidget = new QWidget(this);
    appsWidget->setLayout(appsLayout);
    form->addRow(::tr(QStringLiteral("app_selector.subtitle")), appsWidget);

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

    // Volume limits (percent, 0-100). Keep min <= max as the user types.
    m_volMin = new QSpinBox(this);
    m_volMin->setRange(0, 100);
    m_volMin->setSuffix(QStringLiteral("%"));
    m_volMin->setValue(std::clamp(initial.volMin, 0, 100));

    m_volMax = new QSpinBox(this);
    m_volMax->setRange(0, 100);
    m_volMax->setSuffix(QStringLiteral("%"));
    m_volMax->setValue(std::clamp(initial.volMax, 0, 100));

    connect(m_volMin, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int v)
            {
                if (m_volMax->value() < v) m_volMax->setValue(v);
            });
    connect(m_volMax, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int v)
            {
                if (m_volMin->value() > v) m_volMin->setValue(v);
            });

    auto* limitsRow = new QHBoxLayout;
    limitsRow->addWidget(new QLabel(::tr(QStringLiteral("settings.profiles.vol_min_label")), this));
    limitsRow->addWidget(m_volMin);
    limitsRow->addSpacing(12);
    limitsRow->addWidget(new QLabel(::tr(QStringLiteral("settings.profiles.vol_max_label")), this));
    limitsRow->addWidget(m_volMax);
    limitsRow->addStretch();
    auto* limitsWrap = new QWidget(this);
    limitsWrap->setLayout(limitsRow);
    form->addRow(::tr(QStringLiteral("settings.profiles.vol_limits")), limitsWrap);

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
    p.apps.clear();
    for (int i = 0; i < m_appsListWidget->count(); ++i)
    {
        p.apps.append(m_appsListWidget->item(i)->text());
    }

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
    p.volMin = m_volMin->value();
    p.volMax = m_volMax->value();

    return p;
}

void ProfileEditDialog::addAppToList(const QString& appName)
{
    const QString lower = appName.toLower();
    for (int i = 0; i < m_appsListWidget->count(); ++i)
    {
        if (m_appsListWidget->item(i)->text().toLower() == lower)
        {
            QMessageBox::warning(this,
                ::tr(QStringLiteral("settings.profiles.duplicate_title")),
                ::tr(QStringLiteral("settings.profiles.duplicate_msg")));
            return;
        }
    }
    m_appsListWidget->addItem(appName);
    m_appsListWidget->setCurrentRow(m_appsListWidget->count() - 1);
}
