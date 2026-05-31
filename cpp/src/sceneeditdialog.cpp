#include "sceneeditdialog.h"
#include "applistwidget.h"
#include "settingsdialog.h" // HotkeyCapture
#include "i18n.h"

#include <QLineEdit>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QSlider>
#include <QSpinBox>
#include <QListWidget>

#include <algorithm>

namespace
{
// Mute-mode combo indices, kept in one place so add/read stay in sync.
enum MuteMode
{
    MuteLeave = 0, // std::nullopt — leave unchanged
    MuteOn = 1,    // true  — mute
    MuteOff = 2,   // false — unmute
};

QString muteModeLabel(const std::optional<bool>& muted)
{
    if (!muted) return ::tr(QStringLiteral("settings.scenes.mute_leave"));
    return *muted ? ::tr(QStringLiteral("settings.scenes.mute_on"))
                  : ::tr(QStringLiteral("settings.scenes.mute_off"));
}

QString volumeLabel(const std::optional<int>& volume)
{
    if (!volume) return ::tr(QStringLiteral("settings.scenes.vol_leave"));
    return QStringLiteral("%1%").arg(std::clamp(*volume, 0, 100));
}
} // namespace

SceneEditDialog::SceneEditDialog(const AudioScene& initial, Config* config,
                                 InputHandler* inputHandler, QWidget* parent)
    : QDialog(parent), m_initial(initial), m_config(config), m_inputHandler(inputHandler),
      m_targets(initial.targets)
{
    setWindowTitle(::tr(QStringLiteral("settings.scenes.edit_title")));
    setMinimumWidth(440);
    setWindowModality(Qt::ApplicationModal);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->setSpacing(8);

    // Name
    m_name = new QLineEdit(initial.name, this);
    form->addRow(::tr(QStringLiteral("settings.scenes.name_label")), m_name);

    // Optional global hotkey
    m_hotkey = new HotkeyCapture(initial.hotkey, m_inputHandler, this);
    form->addRow(::tr(QStringLiteral("settings.scenes.hotkey_label")), m_hotkey);

    layout->addLayout(form);

    // Targets table
    auto* targetsHeader = new QLabel(::tr(QStringLiteral("settings.scenes.targets_label")), this);
    targetsHeader->setStyleSheet(QStringLiteral("font-weight: bold; margin-top: 6px;"));
    layout->addWidget(targetsHeader);

    m_targetsTable = new QTableWidget(this);
    m_targetsTable->setColumnCount(3);
    m_targetsTable->setHorizontalHeaderLabels(QStringList{
        ::tr(QStringLiteral("settings.scenes.col_match")),
        ::tr(QStringLiteral("settings.scenes.col_volume")),
        ::tr(QStringLiteral("settings.scenes.col_mute")),
    });
    m_targetsTable->verticalHeader()->setVisible(false);
    m_targetsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_targetsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_targetsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_targetsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_targetsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_targetsTable->setMinimumHeight(120);
    layout->addWidget(m_targetsTable);

    auto* targetBtns = new QHBoxLayout;
    auto* btnAdd = new QPushButton(::tr(QStringLiteral("settings.scenes.target_add")), this);
    auto* btnEdit = new QPushButton(::tr(QStringLiteral("settings.scenes.target_edit")), this);
    auto* btnRemove = new QPushButton(::tr(QStringLiteral("settings.scenes.target_remove")), this);
    targetBtns->addWidget(btnAdd);
    targetBtns->addWidget(btnEdit);
    targetBtns->addWidget(btnRemove);
    targetBtns->addStretch();
    layout->addLayout(targetBtns);

    connect(btnAdd, &QPushButton::clicked, this, &SceneEditDialog::onAddTarget);
    connect(btnEdit, &QPushButton::clicked, this, &SceneEditDialog::onEditTarget);
    connect(btnRemove, &QPushButton::clicked, this, &SceneEditDialog::onRemoveTarget);
    connect(m_targetsTable, &QTableWidget::doubleClicked, this,
            [this](const QModelIndex&) { onEditTarget(); });

    refreshTargetsTable();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

int SceneEditDialog::selectedTargetRow() const
{
    auto sel = m_targetsTable->selectionModel()->selectedRows();
    if (sel.isEmpty()) return -1;
    return sel.first().row();
}

void SceneEditDialog::refreshTargetsTable()
{
    m_targetsTable->setRowCount(static_cast<int>(m_targets.size()));
    for (int row = 0; row < m_targets.size(); ++row)
    {
        const SceneTarget& t = m_targets[row];
        auto setCell = [&](int col, const QString& text)
        {
            auto* item = new QTableWidgetItem(text);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            m_targetsTable->setItem(row, col, item);
        };
        setCell(0, t.match);
        setCell(1, volumeLabel(t.volume));
        setCell(2, muteModeLabel(t.muted));
    }
}

bool SceneEditDialog::editTargetDialog(SceneTarget& target)
{
    QDialog dlg(this);
    dlg.setWindowTitle(::tr(QStringLiteral("settings.scenes.target_title")));
    dlg.setMinimumWidth(360);
    dlg.setWindowModality(Qt::ApplicationModal);

    auto* layout = new QVBoxLayout(&dlg);
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->setSpacing(8);

    // Match — manual entry; an app list below fills it on selection.
    auto* matchEdit = new QLineEdit(target.match, &dlg);
    matchEdit->setPlaceholderText(::tr(QStringLiteral("settings.scenes.match_hint")));
    form->addRow(::tr(QStringLiteral("settings.scenes.col_match")), matchEdit);

    // Volume: checkbox toggles whether the scene sets a volume at all.
    auto* volEnabled = new QCheckBox(::tr(QStringLiteral("settings.scenes.set_volume")), &dlg);
    volEnabled->setChecked(target.volume.has_value());
    form->addRow(QString(), volEnabled);

    auto* volSlider = new QSlider(Qt::Horizontal, &dlg);
    volSlider->setRange(0, 100);
    volSlider->setValue(target.volume ? std::clamp(*target.volume, 0, 100) : 50);
    auto* volSpin = new QSpinBox(&dlg);
    volSpin->setRange(0, 100);
    volSpin->setSuffix(QStringLiteral("%"));
    volSpin->setValue(volSlider->value());
    QObject::connect(volSlider, &QSlider::valueChanged, volSpin, &QSpinBox::setValue);
    QObject::connect(volSpin, qOverload<int>(&QSpinBox::valueChanged), volSlider,
                     &QSlider::setValue);
    auto* volRow = new QHBoxLayout;
    volRow->addWidget(volSlider, 1);
    volRow->addWidget(volSpin);
    auto* volWrap = new QWidget(&dlg);
    volWrap->setLayout(volRow);
    form->addRow(::tr(QStringLiteral("settings.scenes.col_volume")), volWrap);

    auto syncVolEnabled = [&]()
    {
        const bool on = volEnabled->isChecked();
        volSlider->setEnabled(on);
        volSpin->setEnabled(on);
    };
    QObject::connect(volEnabled, &QCheckBox::toggled, &dlg, [&](bool) { syncVolEnabled(); });
    syncVolEnabled();

    // Mute mode
    auto* muteMode = new QComboBox(&dlg);
    muteMode->addItem(::tr(QStringLiteral("settings.scenes.mute_leave")), MuteLeave);
    muteMode->addItem(::tr(QStringLiteral("settings.scenes.mute_on")), MuteOn);
    muteMode->addItem(::tr(QStringLiteral("settings.scenes.mute_off")), MuteOff);
    int modeIdx = MuteLeave;
    if (target.muted) modeIdx = *target.muted ? MuteOn : MuteOff;
    muteMode->setCurrentIndex(modeIdx);
    form->addRow(::tr(QStringLiteral("settings.scenes.col_mute")), muteMode);

    layout->addLayout(form);

    // App picker — selecting an item fills the match field.
    auto* picker = new AppListWidget(&dlg);
    picker->populate(m_config);
    layout->addWidget(picker);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    // Clicking/activating an item in the picker copies it into the match field.
    QObject::connect(picker, &AppListWidget::appActivated, &dlg,
                     [matchEdit](const QString& name)
                     {
                         if (!name.isEmpty()) matchEdit->setText(name);
                     });

    if (dlg.exec() != QDialog::Accepted) return false;

    const QString match = matchEdit->text().trimmed();
    if (match.isEmpty()) return false;

    target.match = match;
    target.volume = volEnabled->isChecked() ? std::optional<int>(volSpin->value()) : std::nullopt;
    switch (muteMode->currentData().toInt())
    {
    case MuteOn:
        target.muted = true;
        break;
    case MuteOff:
        target.muted = false;
        break;
    default:
        target.muted = std::nullopt;
        break;
    }
    return true;
}

void SceneEditDialog::onAddTarget()
{
    SceneTarget target;
    if (!editTargetDialog(target)) return;
    m_targets.append(target);
    refreshTargetsTable();
    m_targetsTable->selectRow(static_cast<int>(m_targets.size()) - 1);
}

void SceneEditDialog::onEditTarget()
{
    int row = selectedTargetRow();
    if (row < 0 || row >= m_targets.size()) return;
    SceneTarget target = m_targets[row];
    if (!editTargetDialog(target)) return;
    m_targets[row] = target;
    refreshTargetsTable();
    m_targetsTable->selectRow(row);
}

void SceneEditDialog::onRemoveTarget()
{
    int row = selectedTargetRow();
    if (row < 0 || row >= m_targets.size()) return;
    m_targets.removeAt(row);
    refreshTargetsTable();
    if (!m_targets.isEmpty())
        m_targetsTable->selectRow(qMin(row, static_cast<int>(m_targets.size()) - 1));
}

AudioScene SceneEditDialog::result() const
{
    AudioScene scene;
    scene.id = m_initial.id; // preserve id; SettingsDialog assigns one for new
    scene.name = m_name->text().trimmed();
    if (scene.name.isEmpty()) scene.name = QStringLiteral("Scene");
    scene.hotkey = m_hotkey->binding();
    scene.targets = m_targets;
    return scene;
}
