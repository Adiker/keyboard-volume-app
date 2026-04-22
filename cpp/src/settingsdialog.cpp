#include "settingsdialog.h"
#include "config.h"
#include "i18n.h"
#include "inputhandler.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QColorDialog>
#include <QColor>
#include <QScreen>
#include <QKeyEvent>
#include <QCloseEvent>

#include <linux/input.h>    // KEY_* constants

// ─── ColorButton ──────────────────────────────────────────────────────────────
ColorButton::ColorButton(const QString &hexColor, QWidget *parent)
    : QPushButton(parent)
{
    setFixedWidth(80);
    setColor(hexColor);
    connect(this, &QPushButton::clicked, this, &ColorButton::pick);
}

void ColorButton::setColor(const QString &hexColor)
{
    m_color = hexColor;
    setStyleSheet(QStringLiteral(
        "background-color: %1; border: 1px solid #888; border-radius: 3px;")
        .arg(hexColor));
    setText(hexColor);
}

void ColorButton::pick()
{
    QColor chosen = QColorDialog::getColor(QColor(m_color), window(), QString{});
    if (chosen.isValid()) {
        setColor(chosen.name());
        emit colorChanged(chosen.name());
    }
}

// ─── KeyCaptureDialog ─────────────────────────────────────────────────────────
KeyCaptureDialog::KeyCaptureDialog(const QString &devicePath, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(::tr(QStringLiteral("settings.hotkey.capture_title")));
    setWindowModality(Qt::ApplicationModal);
    setMinimumWidth(300);

    QVBoxLayout *layout = new QVBoxLayout(this);
    QLabel *label = new QLabel(::tr(QStringLiteral("settings.hotkey.capture_prompt")), this);
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);

    QPushButton *cancelBtn =
        new QPushButton(::tr(QStringLiteral("settings.hotkey.capture_cancel")), this);
    connect(cancelBtn, &QPushButton::clicked, this, &KeyCaptureDialog::doCancel);
    layout->addWidget(cancelBtn);

    if (!devicePath.isEmpty()) {
        m_thread = new KeyCaptureThread(devicePath, this);
        connect(m_thread, &KeyCaptureThread::key_captured,
                this, &KeyCaptureDialog::onCaptured);
        connect(m_thread, &KeyCaptureThread::cancelled,
                this, &KeyCaptureDialog::doCancel);
        m_thread->start();
    }
}

KeyCaptureDialog::~KeyCaptureDialog()
{
    if (m_thread && m_thread->isRunning())
        m_thread->cancel();
}

void KeyCaptureDialog::onCaptured(int code)
{
    if (m_done) return;
    m_done = true;
    m_code = code;
    if (m_thread && m_thread->isRunning())
        m_thread->cancel();
    accept();
}

void KeyCaptureDialog::doCancel()
{
    if (m_done) return;
    m_done = true;
    if (m_thread && m_thread->isRunning())
        m_thread->cancel();
    reject();
}

void KeyCaptureDialog::keyPressEvent(QKeyEvent *event)
{
    if (m_done) { event->accept(); return; }
    if (event->key() == Qt::Key_Escape) { doCancel(); return; }

    // nativeScanCode() is the X11 keycode; evdev code = X11 keycode - 8
    quint32 x11 = event->nativeScanCode();
    if (x11 > 8) {
        int evdevCode = static_cast<int>(x11 - 8);
        // Validate: the code must be a known KEY_* value (≤ KEY_MAX=767)
        if (evdevCode > 0 && evdevCode <= 767) {
            onCaptured(evdevCode);
            return;
        }
    }
    event->accept();
}

void KeyCaptureDialog::closeEvent(QCloseEvent *event)
{
    if (m_thread && m_thread->isRunning())
        m_thread->cancel();
    QDialog::closeEvent(event);
}

// ─── HotkeyCapture ────────────────────────────────────────────────────────────
// Map of evdev KEY_* codes → display names (minimal subset used for hotkeys)
static const QMap<int, QString> &keyNames()
{
    static const QMap<int, QString> m {
        { KEY_VOLUMEUP,   QStringLiteral("VOLUMEUP") },
        { KEY_VOLUMEDOWN, QStringLiteral("VOLUMEDOWN") },
        { KEY_MUTE,       QStringLiteral("MUTE") },
        { KEY_MEDIA,      QStringLiteral("MEDIA") },
        { KEY_PLAYPAUSE,  QStringLiteral("PLAYPAUSE") },
        { KEY_STOPCD,     QStringLiteral("STOPCD") },
        { KEY_NEXTSONG,   QStringLiteral("NEXTSONG") },
        { KEY_PREVIOUSSONG, QStringLiteral("PREVIOUSSONG") },
        { KEY_F1,  QStringLiteral("F1")  }, { KEY_F2,  QStringLiteral("F2")  },
        { KEY_F3,  QStringLiteral("F3")  }, { KEY_F4,  QStringLiteral("F4")  },
        { KEY_F5,  QStringLiteral("F5")  }, { KEY_F6,  QStringLiteral("F6")  },
        { KEY_F7,  QStringLiteral("F7")  }, { KEY_F8,  QStringLiteral("F8")  },
        { KEY_F9,  QStringLiteral("F9")  }, { KEY_F10, QStringLiteral("F10") },
        { KEY_F11, QStringLiteral("F11") }, { KEY_F12, QStringLiteral("F12") },
    };
    return m;
}

QString HotkeyCapture::keyDisplayName(int code)
{
    auto it = keyNames().find(code);
    if (it != keyNames().end()) return it.value();
    return QString::number(code);
}

HotkeyCapture::HotkeyCapture(int evdevCode, InputHandler *inputHandler, QWidget *parent)
    : QPushButton(parent)
    , m_code(evdevCode)
    , m_inputHandler(inputHandler)
{
    setMinimumWidth(120);
    updateDisplay();
    connect(this, &QPushButton::clicked, this, &HotkeyCapture::capture);
}

void HotkeyCapture::updateDisplay()
{
    setText(keyDisplayName(m_code));
}

void HotkeyCapture::capture()
{
    if (m_inputHandler) m_inputHandler->stop();
    try {
        QString devPath = m_inputHandler ? m_inputHandler->devicePath() : QString{};
        KeyCaptureDialog dlg(devPath, this);
        if (dlg.exec() == QDialog::Accepted && dlg.capturedCode() >= 0) {
            m_code = dlg.capturedCode();
            updateDisplay();
        }
    } catch (...) {}
    if (m_inputHandler) m_inputHandler->restart();
}

// ─── SettingsDialog ───────────────────────────────────────────────────────────
SettingsDialog::SettingsDialog(Config *config, InputHandler *inputHandler, QWidget *parent)
    : QDialog(parent)
    , m_config(config)
    , m_inputHandler(inputHandler)
{
    setWindowTitle(::tr(QStringLiteral("settings.title")));
    setMinimumWidth(360);
    setWindowModality(Qt::ApplicationModal);
    buildUi();
}

void SettingsDialog::buildUi()
{
    QVBoxLayout  *layout = new QVBoxLayout(this);
    QFormLayout  *form   = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->setSpacing(10);

    OsdConfig    osd  = m_config->osd();
    HotkeyConfig hks  = m_config->hotkeys();

    // Language
    m_lang = new QComboBox(this);
    for (auto it = languages().begin(); it != languages().end(); ++it)
        m_lang->addItem(it.value(), it.key());
    {
        int idx = 0;
        for (int i = 0; i < m_lang->count(); ++i) {
            if (m_lang->itemData(i).toString() == m_config->language()) { idx = i; break; }
        }
        m_lang->setCurrentIndex(idx);
    }
    form->addRow(::tr(QStringLiteral("settings.language")), m_lang);

    // OSD screen
    m_screen = new QComboBox(this);
    const auto screens = QApplication::screens();
    QScreen *primary   = QApplication::primaryScreen();
    for (int i = 0; i < screens.size(); ++i) {
        QRect geo = screens[i]->geometry();
        QString label = QStringLiteral("%1:  %2\xD7%3")
            .arg(i + 1).arg(geo.width()).arg(geo.height());
        if (screens[i] == primary)
            label += QStringLiteral("  (%1)").arg(::tr(QStringLiteral("settings.screen_primary")));
        m_screen->addItem(label, i);
    }
    if (osd.screen < m_screen->count())
        m_screen->setCurrentIndex(osd.screen);
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
    QHBoxLayout *posRow = new QHBoxLayout;
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

    // Hotkeys section header
    QLabel *hkSection = new QLabel(::tr(QStringLiteral("settings.hotkeys_section")), this);
    hkSection->setStyleSheet(QStringLiteral("font-weight: bold; margin-top: 8px;"));
    form->addRow(hkSection);

    m_hkUp = new HotkeyCapture(hks.volumeUp, m_inputHandler, this);
    form->addRow(::tr(QStringLiteral("settings.hotkey.volume_up")), m_hkUp);

    m_hkDown = new HotkeyCapture(hks.volumeDown, m_inputHandler, this);
    form->addRow(::tr(QStringLiteral("settings.hotkey.volume_down")), m_hkDown);

    m_hkMute = new HotkeyCapture(hks.mute, m_inputHandler, this);
    form->addRow(::tr(QStringLiteral("settings.hotkey.mute")), m_hkMute);

    layout->addLayout(form);

    // Preview button
    QPushButton *previewBtn = new QPushButton(::tr(QStringLiteral("settings.preview_btn")), this);
    connect(previewBtn, &QPushButton::pressed,  this, &SettingsDialog::onPreviewPressed);
    connect(previewBtn, &QPushButton::released, this, &SettingsDialog::onPreviewReleased);
    layout->addWidget(previewBtn);

    // OK / Cancel
    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::saveAndAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    layout->addWidget(buttons);

    // Live position preview
    connect(m_screen, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::emitPositionPreview);
    connect(m_osdX, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsDialog::emitPositionPreview);
    connect(m_osdY, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsDialog::emitPositionPreview);

    // Live style preview
    connect(m_colorBg,   &ColorButton::colorChanged, this, [this](const QString &) { emitStylePreview(); });
    connect(m_colorText, &ColorButton::colorChanged, this, [this](const QString &) { emitStylePreview(); });
    connect(m_colorBar,  &ColorButton::colorChanged, this, [this](const QString &) { emitStylePreview(); });
    connect(m_opacity, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { emitStylePreview(); });
}

void SettingsDialog::onPreviewPressed()
{
    emitStylePreview();
    emit previewHeldRequested(m_screen->currentData().toInt(),
                              m_osdX->value(), m_osdY->value());
}

void SettingsDialog::onPreviewReleased()
{
    emit previewReleased(m_timeout->value());
}

void SettingsDialog::emitPositionPreview()
{
    emit positionPreview(m_screen->currentData().toInt(),
                         m_osdX->value(), m_osdY->value());
}

void SettingsDialog::emitStylePreview()
{
    emit stylePreview(m_colorBg->color(), m_colorText->color(),
                      m_colorBar->color(), m_opacity->value());
    emitPositionPreview();
}

void SettingsDialog::saveAndAccept()
{
    QString langCode = m_lang->currentData().toString();
    m_config->setLanguage(langCode);
    setLanguage(langCode);

    m_config->updateOsd(
        m_screen->currentData().toInt(),
        m_timeout->value(),
        m_osdX->value(),
        m_osdY->value(),
        m_opacity->value(),
        m_colorBg->color(),
        m_colorText->color(),
        m_colorBar->color()
    );
    m_config->setVolumeStep(m_step->value());
    m_config->setHotkeys(m_hkUp->evdevCode(),
                         m_hkDown->evdevCode(),
                         m_hkMute->evdevCode());
    accept();
}
