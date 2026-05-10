#include "settingsdialog.h"
#include "config.h"
#include "i18n.h"
#include "inputhandler.h"
#include "profileeditdialog.h"
#include "screenutils.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QColorDialog>
#include <QColor>
#include <QScreen>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QCursor>
#include <QDebug>
#include <QTableWidget>
#include <QHeaderView>
#include <QStringList>

#include <linux/input.h> // KEY_* constants

// ─── ColorButton ──────────────────────────────────────────────────────────────
ColorButton::ColorButton(const QString& hexColor, QWidget* parent) : QPushButton(parent)
{
    setFixedWidth(80);
    setColor(hexColor);
    connect(this, &QPushButton::clicked, this, &ColorButton::pick);
}

void ColorButton::setColor(const QString& hexColor)
{
    m_color = hexColor;
    setStyleSheet(
        QStringLiteral("background-color: %1; border: 1px solid #888; border-radius: 3px;")
            .arg(hexColor));
    setText(hexColor);
}

void ColorButton::pick()
{
    QColor chosen = QColorDialog::getColor(QColor(m_color), window(), QString{});
    if (chosen.isValid())
    {
        setColor(chosen.name());
        emit colorChanged(chosen.name());
    }
}

// ─── KeyCaptureDialog ─────────────────────────────────────────────────────────
KeyCaptureDialog::KeyCaptureDialog(const QString& devicePath, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(::tr(QStringLiteral("settings.hotkey.capture_title")));
    setWindowModality(Qt::ApplicationModal);
    setMinimumWidth(300);

    QVBoxLayout* layout = new QVBoxLayout(this);
    QLabel* label = new QLabel(::tr(QStringLiteral("settings.hotkey.capture_prompt")), this);
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);

    QPushButton* cancelBtn =
        new QPushButton(::tr(QStringLiteral("settings.hotkey.capture_cancel")), this);
    connect(cancelBtn, &QPushButton::clicked, this, &KeyCaptureDialog::doCancel);
    layout->addWidget(cancelBtn);

    if (!devicePath.isEmpty())
    {
        m_thread = new KeyCaptureThread(devicePath, this);
        connect(m_thread, &KeyCaptureThread::hotkey_captured, this, &KeyCaptureDialog::onCaptured);
        connect(m_thread, &KeyCaptureThread::cancelled, this, &KeyCaptureDialog::doCancel);
        m_thread->start();
    }
}

KeyCaptureDialog::~KeyCaptureDialog()
{
    if (m_thread && m_thread->isRunning()) m_thread->cancel();
}

void KeyCaptureDialog::onCaptured(HotkeyBinding binding)
{
    if (m_done) return;
    m_done = true;
    m_binding = binding;
    if (m_thread && m_thread->isRunning()) m_thread->cancel();
    accept();
}

void KeyCaptureDialog::doCancel()
{
    if (m_done) return;
    m_done = true;
    if (m_thread && m_thread->isRunning()) m_thread->cancel();
    reject();
}

void KeyCaptureDialog::keyPressEvent(QKeyEvent* event)
{
    if (m_done)
    {
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape)
    {
        doCancel();
        return;
    }

    // nativeScanCode() is the X11 keycode; evdev code = X11 keycode - 8
    quint32 x11 = event->nativeScanCode();
    if (x11 > 8)
    {
        int evdevCode = static_cast<int>(x11 - 8);
        // Validate: the code must be a known KEY_* value (≤ KEY_MAX=767)
        if (evdevCode > 0 && evdevCode <= 767)
        {
            onCaptured(HotkeyBinding::key(evdevCode));
            return;
        }
    }
    event->accept();
}

void KeyCaptureDialog::closeEvent(QCloseEvent* event)
{
    if (m_thread && m_thread->isRunning()) m_thread->cancel();
    QDialog::closeEvent(event);
}

// ─── HotkeyCapture ────────────────────────────────────────────────────────────
// Map of evdev KEY_* codes → display names (minimal subset used for hotkeys)
static const QMap<int, QString>& keyNames()
{
    static const QMap<int, QString> m{
        {KEY_VOLUMEUP, QStringLiteral("VOLUMEUP")},
        {KEY_VOLUMEDOWN, QStringLiteral("VOLUMEDOWN")},
        {KEY_MUTE, QStringLiteral("MUTE")},
        {KEY_MEDIA, QStringLiteral("MEDIA")},
        {KEY_PLAYPAUSE, QStringLiteral("PLAYPAUSE")},
        {KEY_STOPCD, QStringLiteral("STOPCD")},
        {KEY_NEXTSONG, QStringLiteral("NEXTSONG")},
        {KEY_PREVIOUSSONG, QStringLiteral("PREVIOUSSONG")},
        {KEY_F1, QStringLiteral("F1")},
        {KEY_F2, QStringLiteral("F2")},
        {KEY_F3, QStringLiteral("F3")},
        {KEY_F4, QStringLiteral("F4")},
        {KEY_F5, QStringLiteral("F5")},
        {KEY_F6, QStringLiteral("F6")},
        {KEY_F7, QStringLiteral("F7")},
        {KEY_F8, QStringLiteral("F8")},
        {KEY_F9, QStringLiteral("F9")},
        {KEY_F10, QStringLiteral("F10")},
        {KEY_F11, QStringLiteral("F11")},
        {KEY_F12, QStringLiteral("F12")},
    };
    return m;
}

QString HotkeyCapture::keyDisplayName(const HotkeyBinding& binding)
{
    if (!binding.isAssigned()) return QStringLiteral("-");
    if (binding.type == HotkeyBindingType::Relative)
    {
        if (binding.code == REL_WHEEL)
            return binding.direction > 0 ? QStringLiteral("Wheel Up")
                                         : QStringLiteral("Wheel Down");
        if (binding.code == REL_HWHEEL)
            return binding.direction > 0 ? QStringLiteral("Wheel Right")
                                         : QStringLiteral("Wheel Left");
        return QStringLiteral("REL %1 %2").arg(binding.code).arg(binding.direction > 0 ? 1 : -1);
    }

    auto it = keyNames().find(binding.code);
    if (it != keyNames().end()) return it.value();
    return QString::number(binding.code);
}

HotkeyCapture::HotkeyCapture(HotkeyBinding binding, InputHandler* inputHandler, QWidget* parent)
    : QPushButton(parent), m_binding(binding), m_inputHandler(inputHandler)
{
    setMinimumWidth(120);
    updateDisplay();
    connect(this, &QPushButton::clicked, this, &HotkeyCapture::capture);
    setContextMenuPolicy(Qt::ActionsContextMenu);
    auto* clearAction = new QAction(::tr(QStringLiteral("settings.hotkey.unassign")), this);
    connect(clearAction, &QAction::triggered, this,
            [this]
            {
                m_code = 0;
                updateDisplay();
            });
    addAction(clearAction);
}

void HotkeyCapture::updateDisplay()
{
    setText(keyDisplayName(m_binding));
}

void HotkeyCapture::capture()
{
    if (m_inputHandler) m_inputHandler->stop();
    try
    {
        QString devPath = m_inputHandler ? m_inputHandler->devicePath() : QString{};
        KeyCaptureDialog dlg(devPath, this);
        if (dlg.exec() == QDialog::Accepted && dlg.capturedBinding().isAssigned())
        {
            m_binding = dlg.capturedBinding();
            updateDisplay();
        }
    }
    catch (...)
    {
        qWarning() << "[HotkeyCapture] Exception during key capture";
    }
    if (m_inputHandler) m_inputHandler->restart();
}

// ─── SettingsDialog ───────────────────────────────────────────────────────────
SettingsDialog::SettingsDialog(Config* config, InputHandler* inputHandler, QWidget* parent)
    : QDialog(parent), m_config(config), m_inputHandler(inputHandler)
{
    setWindowTitle(::tr(QStringLiteral("settings.title")));
    setMinimumWidth(360);
    setWindowModality(Qt::ApplicationModal);
    buildUi();
}

void SettingsDialog::buildUi()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    QFormLayout* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->setSpacing(10);

    OsdConfig osd = m_config->osd();
    m_profiles = m_config->profiles();

    // Language
    m_lang = new QComboBox(this);
    for (auto it = languages().begin(); it != languages().end(); ++it)
        m_lang->addItem(it.value(), it.key());
    {
        int idx = 0;
        for (int i = 0; i < m_lang->count(); ++i)
        {
            if (m_lang->itemData(i).toString() == m_config->language())
            {
                idx = i;
                break;
            }
        }
        m_lang->setCurrentIndex(idx);
    }
    form->addRow(::tr(QStringLiteral("settings.language")), m_lang);

    // OSD screen
    m_screen = new QComboBox(this);
    const auto screens = QApplication::screens();
    QScreen* primary = QApplication::primaryScreen();
    for (int i = 0; i < screens.size(); ++i)
    {
        QRect geo = screens[i]->geometry();
        QString label =
            QStringLiteral("%1:  %2\xD7%3").arg(i + 1).arg(geo.width()).arg(geo.height());
        if (screens[i] == primary)
            label += QStringLiteral("  (%1)").arg(::tr(QStringLiteral("settings.screen_primary")));
        m_screen->addItem(label, i);
    }
    if (osd.screen < m_screen->count()) m_screen->setCurrentIndex(osd.screen);
    form->addRow(::tr(QStringLiteral("settings.osd_screen")), m_screen);

    // OSD timeout
    m_timeout = new QSpinBox(this);
    m_timeout->setRange(300, 10000);
    m_timeout->setSingleStep(100);
    m_timeout->setSuffix(QStringLiteral(" ms"));
    m_timeout->setValue(osd.timeoutMs);
    form->addRow(::tr(QStringLiteral("settings.osd_timeout")), m_timeout);

    // OSD position (relative to selected screen)
    m_osdX = new QSpinBox(this);
    m_osdX->setRange(0, 7680);
    m_osdX->setValue(osd.x);
    m_osdX->setPrefix(QStringLiteral("X: "));
    m_osdY = new QSpinBox(this);
    m_osdY->setRange(0, 4320);
    m_osdY->setValue(osd.y);
    m_osdY->setPrefix(QStringLiteral("Y: "));
    QHBoxLayout* posRow = new QHBoxLayout;
    posRow->addWidget(m_osdX);
    posRow->addWidget(m_osdY);
    form->addRow(::tr(QStringLiteral("settings.osd_position")), posRow);

    // Volume step
    m_step = new QSpinBox(this);
    m_step->setRange(1, 50);
    m_step->setSuffix(QStringLiteral(" %"));
    m_step->setValue(m_config->volumeStep());
    form->addRow(::tr(QStringLiteral("settings.volume_step")), m_step);

    // Colors
    m_colorBg = new ColorButton(osd.colorBg, this);
    form->addRow(::tr(QStringLiteral("settings.color_bg")), m_colorBg);

    m_colorText = new ColorButton(osd.colorText, this);
    form->addRow(::tr(QStringLiteral("settings.color_text")), m_colorText);

    m_colorBar = new ColorButton(osd.colorBar, this);
    form->addRow(::tr(QStringLiteral("settings.color_bar")), m_colorBar);

    // Opacity
    m_opacity = new QSpinBox(this);
    m_opacity->setRange(0, 100);
    m_opacity->setSingleStep(5);
    m_opacity->setSuffix(QStringLiteral(" %"));
    m_opacity->setValue(osd.opacity);
    form->addRow(::tr(QStringLiteral("settings.opacity")), m_opacity);

    // Auto-switch profile
    m_autoProfile = new QCheckBox(::tr(QStringLiteral("settings.auto_profile_switch")), this);
    m_autoProfile->setChecked(m_config->autoProfileSwitch());
    form->addRow(QString(), m_autoProfile);

    layout->addLayout(form);

    // ── Profiles section ────────────────────────────────────────────────
    QLabel* profilesHeader = new QLabel(::tr(QStringLiteral("settings.profiles.section")), this);
    profilesHeader->setStyleSheet(QStringLiteral("font-weight: bold; margin-top: 8px;"));
    layout->addWidget(profilesHeader);

    m_profilesTable = new QTableWidget(this);
    m_profilesTable->setColumnCount(7);
    m_profilesTable->setHorizontalHeaderLabels(QStringList{
        ::tr(QStringLiteral("settings.profiles.col_name")),
        ::tr(QStringLiteral("settings.profiles.col_app")),
        ::tr(QStringLiteral("settings.profiles.col_modifiers")),
        ::tr(QStringLiteral("settings.profiles.col_volume_up")),
        ::tr(QStringLiteral("settings.profiles.col_volume_down")),
        ::tr(QStringLiteral("settings.profiles.col_mute")),
        ::tr(QStringLiteral("settings.profiles.col_ducking")),
    });
    m_profilesTable->verticalHeader()->setVisible(false);
    m_profilesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_profilesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_profilesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_profilesTable->horizontalHeader()->setStretchLastSection(false);
    m_profilesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_profilesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_profilesTable->setMinimumHeight(120);
    layout->addWidget(m_profilesTable);

    auto* profileBtns = new QHBoxLayout;
    m_btnAdd = new QPushButton(::tr(QStringLiteral("settings.profiles.add")), this);
    m_btnEdit = new QPushButton(::tr(QStringLiteral("settings.profiles.edit")), this);
    m_btnRemove = new QPushButton(::tr(QStringLiteral("settings.profiles.remove")), this);
    m_btnSetDefault = new QPushButton(::tr(QStringLiteral("settings.profiles.set_default")), this);
    profileBtns->addWidget(m_btnAdd);
    profileBtns->addWidget(m_btnEdit);
    profileBtns->addWidget(m_btnRemove);
    profileBtns->addWidget(m_btnSetDefault);
    profileBtns->addStretch();
    layout->addLayout(profileBtns);

    connect(m_btnAdd, &QPushButton::clicked, this, &SettingsDialog::onAddProfile);
    connect(m_btnEdit, &QPushButton::clicked, this, &SettingsDialog::onEditProfile);
    connect(m_btnRemove, &QPushButton::clicked, this, &SettingsDialog::onRemoveProfile);
    connect(m_btnSetDefault, &QPushButton::clicked, this, &SettingsDialog::onSetDefaultProfile);
    connect(m_profilesTable, &QTableWidget::doubleClicked, this,
            [this](const QModelIndex&) { onEditProfile(); });

    refreshProfilesTable();

    // Preview button
    QPushButton* previewBtn = new QPushButton(::tr(QStringLiteral("settings.preview_btn")), this);
    connect(previewBtn, &QPushButton::pressed, this, &SettingsDialog::onPreviewPressed);
    connect(previewBtn, &QPushButton::released, this, &SettingsDialog::onPreviewReleased);
    layout->addWidget(previewBtn);

    // OK / Cancel
    QDialogButtonBox* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::saveAndAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    layout->addWidget(buttons);

    // Live position preview
    connect(m_screen, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::emitPositionPreview);
    connect(m_osdX, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::emitPositionPreview);
    connect(m_osdY, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::emitPositionPreview);

    // Live style preview
    connect(m_colorBg, &ColorButton::colorChanged, this,
            [this](const QString&) { emitStylePreview(); });
    connect(m_colorText, &ColorButton::colorChanged, this,
            [this](const QString&) { emitStylePreview(); });
    connect(m_colorBar, &ColorButton::colorChanged, this,
            [this](const QString&) { emitStylePreview(); });
    connect(m_opacity, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { emitStylePreview(); });
}

void SettingsDialog::onPreviewPressed()
{
    emitStylePreview();
    emit previewHeldRequested(m_screen->currentData().toInt(), m_osdX->value(), m_osdY->value());
}

void SettingsDialog::onPreviewReleased()
{
    emit previewReleased(m_timeout->value());
}

void SettingsDialog::emitPositionPreview()
{
    emit positionPreview(m_screen->currentData().toInt(), m_osdX->value(), m_osdY->value());
}

void SettingsDialog::emitStylePreview()
{
    emit stylePreview(m_colorBg->color(), m_colorText->color(), m_colorBar->color(),
                      m_opacity->value());
    emitPositionPreview();
}

void SettingsDialog::saveAndAccept()
{
    QString langCode = m_lang->currentData().toString();
    m_config->setLanguage(langCode);
    setLanguage(langCode);

    m_config->updateOsd(m_screen->currentData().toInt(), m_timeout->value(), m_osdX->value(),
                        m_osdY->value(), m_opacity->value(), m_colorBg->color(),
                        m_colorText->color(), m_colorBar->color());
    m_config->setVolumeStep(m_step->value());
    m_config->setAutoProfileSwitch(m_autoProfile->isChecked());
    if (!m_profiles.isEmpty()) m_config->setProfiles(m_profiles);
    accept();
}

// ─── Profiles section ─────────────────────────────────────────────────────────
int SettingsDialog::selectedProfileRow() const
{
    auto sel = m_profilesTable->selectionModel()->selectedRows();
    if (sel.isEmpty()) return -1;
    return sel.first().row();
}

void SettingsDialog::refreshProfilesTable()
{
    m_profilesTable->setRowCount(m_profiles.size());
    for (int row = 0; row < m_profiles.size(); ++row)
    {
        const Profile& p = m_profiles[row];

        QString nameDisplay = p.name;
        if (row == 0) nameDisplay += QStringLiteral(" (default)");

        QStringList mods;
        if (p.modifiers.contains(Modifier::Ctrl)) mods << QStringLiteral("Ctrl");
        if (p.modifiers.contains(Modifier::Shift)) mods << QStringLiteral("Shift");
        QString modsDisplay = mods.isEmpty()
                                  ? ::tr(QStringLiteral("settings.profiles.modifier_none"))
                                  : mods.join(QStringLiteral("+"));

        auto setCell = [&](int col, const QString& text)
        {
            auto* item = new QTableWidgetItem(text);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            m_profilesTable->setItem(row, col, item);
        };
        setCell(0, nameDisplay);
        setCell(1, p.app);
        setCell(2, modsDisplay);
        setCell(3, HotkeyCapture::keyDisplayName(p.hotkeys.volumeUp));
        setCell(4, HotkeyCapture::keyDisplayName(p.hotkeys.volumeDown));
        setCell(5, HotkeyCapture::keyDisplayName(p.hotkeys.mute));
        setCell(6, p.ducking.enabled ? QStringLiteral("%1% / %2")
                                           .arg(p.ducking.volume)
                                           .arg(HotkeyCapture::keyDisplayName(p.ducking.hotkey))
                                     : ::tr(QStringLiteral("settings.profiles.modifier_none")));
    }

    m_btnRemove->setEnabled(m_profiles.size() > 1);
}

void SettingsDialog::onAddProfile()
{
    Profile blank;
    blank.id = QStringLiteral("profile-") + QString::number(m_profiles.size() + 1);
    blank.name = QStringLiteral("Profile ") + QString::number(m_profiles.size() + 1);
    blank.hotkeys.volumeUp = HotkeyBinding::key(115);
    blank.hotkeys.volumeDown = HotkeyBinding::key(114);
    blank.hotkeys.mute = HotkeyBinding::key(113);

    const QPoint anchor = QCursor::pos();
    ProfileEditDialog dlg(blank, m_config, m_inputHandler, this);
    centerDialogOnScreenAt(&dlg, anchor);
    if (dlg.exec() == QDialog::Accepted)
    {
        m_profiles.append(dlg.result());
        refreshProfilesTable();
        m_profilesTable->selectRow(m_profiles.size() - 1);
    }
}

void SettingsDialog::onEditProfile()
{
    int row = selectedProfileRow();
    if (row < 0 || row >= m_profiles.size()) return;

    const QPoint anchor = QCursor::pos();
    ProfileEditDialog dlg(m_profiles[row], m_config, m_inputHandler, this);
    centerDialogOnScreenAt(&dlg, anchor);
    if (dlg.exec() == QDialog::Accepted)
    {
        m_profiles[row] = dlg.result();
        refreshProfilesTable();
        m_profilesTable->selectRow(row);
    }
}

void SettingsDialog::onRemoveProfile()
{
    int row = selectedProfileRow();
    if (row < 0 || row >= m_profiles.size()) return;
    if (m_profiles.size() <= 1) return; // safeguard — UI also disables button

    m_profiles.removeAt(row);
    refreshProfilesTable();
    if (!m_profiles.isEmpty()) m_profilesTable->selectRow(qMin(row, m_profiles.size() - 1));
}

void SettingsDialog::onSetDefaultProfile()
{
    int row = selectedProfileRow();
    if (row <= 0 || row >= m_profiles.size()) return; // already default or invalid
    m_profiles.move(row, 0);
    refreshProfilesTable();
    m_profilesTable->selectRow(0);
}
