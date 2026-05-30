#include "settingsdialog.h"
#include "config.h"
#include "i18n.h"
#include "inputhandler.h"
#include "profileeditdialog.h"
#include "sceneeditdialog.h"
#include "screenutils.h"
#include "volumecontroller.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QColorDialog>
#include <QColor>
#include <QScreen>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QCursor>
#include <QDebug>
#include <QAction>
#include <QTableWidget>
#include <QHeaderView>
#include <QStringList>

#include <linux/input.h>       // KEY_* constants
#include <libevdev/libevdev.h> // libevdev_event_code_get_name()

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
// Human-readable names for commonly assigned EV_KEY codes.
// All other codes fall back to libevdev_event_code_get_name() (strip "KEY_").
static const QMap<int, QString>& keyNames()
{
    static const QMap<int, QString> m{
        // Media / Consumer Control
        {KEY_VOLUMEUP, QStringLiteral("Volume Up")},
        {KEY_VOLUMEDOWN, QStringLiteral("Volume Down")},
        {KEY_MUTE, QStringLiteral("Mute")},
        {KEY_MICMUTE, QStringLiteral("Mic Mute")},
        {KEY_MEDIA, QStringLiteral("Media")},
        {KEY_PLAYPAUSE, QStringLiteral("Play / Pause")},
        {KEY_PLAY, QStringLiteral("Play")},
        {KEY_STOPCD, QStringLiteral("Stop")},
        {KEY_NEXTSONG, QStringLiteral("Next Track")},
        {KEY_PREVIOUSSONG, QStringLiteral("Prev Track")},
        {KEY_REWIND, QStringLiteral("Rewind")},
        {KEY_FASTFORWARD, QStringLiteral("Fast Forward")},
        {KEY_RECORD, QStringLiteral("Record")},
        {KEY_EJECTCD, QStringLiteral("Eject")},
        // Brightness / keyboard backlight
        {KEY_BRIGHTNESSUP, QStringLiteral("Brightness Up")},
        {KEY_BRIGHTNESSDOWN, QStringLiteral("Brightness Down")},
        {KEY_KBDILLUMUP, QStringLiteral("KB Light Up")},
        {KEY_KBDILLUMDOWN, QStringLiteral("KB Light Down")},
        {KEY_KBDILLUMTOGGLE, QStringLiteral("KB Light Toggle")},
        // Function keys F1–F24
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
        {KEY_F13, QStringLiteral("F13")},
        {KEY_F14, QStringLiteral("F14")},
        {KEY_F15, QStringLiteral("F15")},
        {KEY_F16, QStringLiteral("F16")},
        {KEY_F17, QStringLiteral("F17")},
        {KEY_F18, QStringLiteral("F18")},
        {KEY_F19, QStringLiteral("F19")},
        {KEY_F20, QStringLiteral("F20")},
        {KEY_F21, QStringLiteral("F21")},
        {KEY_F22, QStringLiteral("F22")},
        {KEY_F23, QStringLiteral("F23")},
        {KEY_F24, QStringLiteral("F24")},
        // Special / editing keys
        {KEY_ESC, QStringLiteral("Escape")},
        {KEY_TAB, QStringLiteral("Tab")},
        {KEY_CAPSLOCK, QStringLiteral("Caps Lock")},
        {KEY_BACKSPACE, QStringLiteral("Backspace")},
        {KEY_ENTER, QStringLiteral("Enter")},
        {KEY_KPENTER, QStringLiteral("Numpad Enter")},
        {KEY_SPACE, QStringLiteral("Space")},
        {KEY_INSERT, QStringLiteral("Insert")},
        {KEY_DELETE, QStringLiteral("Delete")},
        {KEY_HOME, QStringLiteral("Home")},
        {KEY_END, QStringLiteral("End")},
        {KEY_PAGEUP, QStringLiteral("Page Up")},
        {KEY_PAGEDOWN, QStringLiteral("Page Down")},
        {KEY_PAUSE, QStringLiteral("Pause")},
        {KEY_PRINT, QStringLiteral("Print Screen")},
        {KEY_SCROLLLOCK, QStringLiteral("Scroll Lock")},
        {KEY_NUMLOCK, QStringLiteral("Num Lock")},
        {KEY_SYSRQ, QStringLiteral("SysRq")},
        // Arrow keys
        {KEY_UP, QStringLiteral("Up")},
        {KEY_DOWN, QStringLiteral("Down")},
        {KEY_LEFT, QStringLiteral("Left")},
        {KEY_RIGHT, QStringLiteral("Right")},
        // Modifier keys
        {KEY_LEFTSHIFT, QStringLiteral("Left Shift")},
        {KEY_RIGHTSHIFT, QStringLiteral("Right Shift")},
        {KEY_LEFTCTRL, QStringLiteral("Left Ctrl")},
        {KEY_RIGHTCTRL, QStringLiteral("Right Ctrl")},
        {KEY_LEFTALT, QStringLiteral("Left Alt")},
        {KEY_RIGHTALT, QStringLiteral("Right Alt")},
        {KEY_LEFTMETA, QStringLiteral("Left Meta")},
        {KEY_RIGHTMETA, QStringLiteral("Right Meta")},
        {KEY_COMPOSE, QStringLiteral("Compose")},
        // Numpad
        {KEY_KP0, QStringLiteral("Numpad 0")},
        {KEY_KP1, QStringLiteral("Numpad 1")},
        {KEY_KP2, QStringLiteral("Numpad 2")},
        {KEY_KP3, QStringLiteral("Numpad 3")},
        {KEY_KP4, QStringLiteral("Numpad 4")},
        {KEY_KP5, QStringLiteral("Numpad 5")},
        {KEY_KP6, QStringLiteral("Numpad 6")},
        {KEY_KP7, QStringLiteral("Numpad 7")},
        {KEY_KP8, QStringLiteral("Numpad 8")},
        {KEY_KP9, QStringLiteral("Numpad 9")},
        {KEY_KPMINUS, QStringLiteral("Numpad -")},
        {KEY_KPPLUS, QStringLiteral("Numpad +")},
        {KEY_KPASTERISK, QStringLiteral("Numpad *")},
        {KEY_KPSLASH, QStringLiteral("Numpad /")},
        {KEY_KPDOT, QStringLiteral("Numpad .")},
        {KEY_KPEQUAL, QStringLiteral("Numpad =")},
        // Application / shortcut keys
        {KEY_MENU, QStringLiteral("Menu")},
        {KEY_POWER, QStringLiteral("Power")},
        {KEY_SLEEP, QStringLiteral("Sleep")},
        {KEY_WAKEUP, QStringLiteral("Wake Up")},
        {KEY_HOMEPAGE, QStringLiteral("Home Page")},
        {KEY_MAIL, QStringLiteral("Mail")},
        {KEY_CALC, QStringLiteral("Calculator")},
        {KEY_SEARCH, QStringLiteral("Search")},
        {KEY_BACK, QStringLiteral("Back")},
        {KEY_FORWARD, QStringLiteral("Forward")},
        {KEY_REFRESH, QStringLiteral("Refresh")},
        {KEY_BOOKMARKS, QStringLiteral("Bookmarks")},
        {KEY_COMPUTER, QStringLiteral("My Computer")},
        {KEY_HELP, QStringLiteral("Help")},
    };
    return m;
}

// Returns a display string for a hotkey binding in the form "Name (code)".
// EV_KEY codes: checked against the friendly-name map first, then libevdev
// symbolic name (strip "KEY_" prefix), then generic "Key (N)" fallback.
// EV_REL codes: "Wheel Up/Down/Left/Right" or "REL N ±1" (unchanged).
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

    const int code = binding.code;

    // Level 1: explicit friendly-name map
    auto it = keyNames().find(code);
    if (it != keyNames().end()) return QStringLiteral("%1 (%2)").arg(it.value()).arg(code);

    // Level 2: libevdev symbolic name — strip "KEY_" only; keep other prefixes
    // (e.g. BTN_LEFT) intact to avoid collisions with real key names.
    const char* evdevName = libevdev_event_code_get_name(EV_KEY, static_cast<unsigned int>(code));
    if (evdevName)
    {
        const QLatin1StringView sym(evdevName);
        const QString display = sym.startsWith(QLatin1StringView("KEY_"))
                                    ? QString::fromLatin1(evdevName + 4)
                                    : QString::fromLatin1(evdevName);
        return QStringLiteral("%1 (%2)").arg(display).arg(code);
    }

    // Level 3: completely unknown code
    return QStringLiteral("Key (%1)").arg(code);
}

HotkeyCapture::HotkeyCapture(HotkeyBinding binding, InputHandler* inputHandler, QWidget* parent)
    : QPushButton(parent), m_binding(binding), m_inputHandler(inputHandler)
{
    setMinimumWidth(140);
    updateDisplay();
    connect(this, &QPushButton::clicked, this, &HotkeyCapture::capture);
    setContextMenuPolicy(Qt::ActionsContextMenu);
    auto* clearAction = new QAction(::tr(QStringLiteral("settings.hotkey.unassign")), this);
    connect(clearAction, &QAction::triggered, this, &HotkeyCapture::unassign);
    addAction(clearAction);
}

void HotkeyCapture::unassign()
{
    m_binding = {};
    updateDisplay();
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
SettingsDialog::SettingsDialog(Config* config, InputHandler* inputHandler,
                               VolumeController* volumeCtrl, QWidget* parent)
    : QDialog(parent), m_config(config), m_inputHandler(inputHandler), m_volumeCtrl(volumeCtrl)
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
    m_scenes = m_config->scenes();

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

    // OSD scale
    m_osdScale = new QDoubleSpinBox(this);
    m_osdScale->setRange(0.5, 3.0);
    m_osdScale->setSingleStep(0.1);
    m_osdScale->setDecimals(1);
    m_osdScale->setSuffix(QStringLiteral("x"));
    m_osdScale->setValue(osd.osdScale);
    form->addRow(::tr(QStringLiteral("settings.osd_scale")), m_osdScale);

    // Auto-switch profile
    m_autoProfile = new QCheckBox(::tr(QStringLiteral("settings.auto_profile_switch")), this);
    m_autoProfile->setChecked(m_config->autoProfileSwitch());
    form->addRow(QString(), m_autoProfile);

    layout->addLayout(form);

    // ── Media hotkeys section ────────────────────────────────────────────
    // Global media bindings — independent of profiles. Bound keys dispatch
    // play-pause / next / previous / stop to the active MPRIS player chosen
    // by MprisClient. Defaults are unassigned so the app does not silently
    // capture the user's media keys.
    QLabel* mediaHeader = new QLabel(::tr(QStringLiteral("settings.media.section")), this);
    mediaHeader->setStyleSheet(QStringLiteral("font-weight: bold; margin-top: 8px;"));
    layout->addWidget(mediaHeader);

    QFormLayout* mediaForm = new QFormLayout;
    mediaForm->setLabelAlignment(Qt::AlignRight);
    mediaForm->setSpacing(8);

    const MediaHotkeyConfig media = m_config->mediaHotkeys();
    m_mediaPlayPause = new HotkeyCapture(media.playPause, m_inputHandler, this);
    m_mediaNext = new HotkeyCapture(media.next, m_inputHandler, this);
    m_mediaPrevious = new HotkeyCapture(media.previous, m_inputHandler, this);
    m_mediaStop = new HotkeyCapture(media.stop, m_inputHandler, this);

    mediaForm->addRow(::tr(QStringLiteral("settings.media.play_pause")), m_mediaPlayPause);
    mediaForm->addRow(::tr(QStringLiteral("settings.media.next")), m_mediaNext);
    mediaForm->addRow(::tr(QStringLiteral("settings.media.previous")), m_mediaPrevious);
    mediaForm->addRow(::tr(QStringLiteral("settings.media.stop")), m_mediaStop);

    layout->addLayout(mediaForm);

    // ── Playback progress section ────────────────────────────────────────
    QLabel* progressHeader = new QLabel(::tr(QStringLiteral("settings.progress.section")), this);
    progressHeader->setStyleSheet(QStringLiteral("font-weight: bold; margin-top: 8px;"));
    layout->addWidget(progressHeader);

    QFormLayout* progressForm = new QFormLayout;
    progressForm->setLabelAlignment(Qt::AlignRight);
    progressForm->setSpacing(8);

    m_progressEnabled = new QCheckBox(::tr(QStringLiteral("settings.progress.enabled")), this);
    m_progressEnabled->setChecked(osd.progressEnabled);
    progressForm->addRow(QString(), m_progressEnabled);

    m_progressInteractive =
        new QCheckBox(::tr(QStringLiteral("settings.progress.interactive")), this);
    m_progressInteractive->setChecked(osd.progressInteractive);
    progressForm->addRow(QString(), m_progressInteractive);

    m_progressPollMs = new QSpinBox(this);
    m_progressPollMs->setRange(200, 2000);
    m_progressPollMs->setSingleStep(100);
    m_progressPollMs->setSuffix(QStringLiteral(" ms"));
    m_progressPollMs->setValue(osd.progressPollMs);
    progressForm->addRow(::tr(QStringLiteral("settings.progress.poll_ms")), m_progressPollMs);

    m_progressLabelMode = new QComboBox(this);
    m_progressLabelMode->addItem(::tr(QStringLiteral("settings.progress.label_app")),
                                 QStringLiteral("app"));
    m_progressLabelMode->addItem(::tr(QStringLiteral("settings.progress.label_title_artist")),
                                 QStringLiteral("title_artist"));
    m_progressLabelMode->addItem(::tr(QStringLiteral("settings.progress.label_artist_title")),
                                 QStringLiteral("artist_title"));
    m_progressLabelMode->addItem(::tr(QStringLiteral("settings.progress.label_app_track")),
                                 QStringLiteral("app_track"));
    m_progressLabelMode->addItem(::tr(QStringLiteral("settings.progress.label_player_track")),
                                 QStringLiteral("player_track"));
    m_progressLabelMode->addItem(::tr(QStringLiteral("settings.progress.label_player_track_art")),
                                 QStringLiteral("player_track_art"));
    m_progressLabelMode->addItem(::tr(QStringLiteral("settings.progress.label_custom")),
                                 QStringLiteral("custom"));
    {
        int idx = m_progressLabelMode->findData(osd.progressLabelMode);
        if (idx >= 0) m_progressLabelMode->setCurrentIndex(idx);
    }
    progressForm->addRow(::tr(QStringLiteral("settings.progress.label_mode")), m_progressLabelMode);

    // Custom label group — visible only when "custom" is selected.
    m_customLabelGroup = new QWidget(this);
    QFormLayout* customForm = new QFormLayout(m_customLabelGroup);
    customForm->setLabelAlignment(Qt::AlignRight);
    customForm->setSpacing(8);
    customForm->setContentsMargins(0, 0, 0, 0);

    m_customLabelTop = new QLineEdit(m_customLabelGroup);
    m_customLabelTop->setText(osd.customLabelTop);
    customForm->addRow(::tr(QStringLiteral("settings.progress.custom_top")), m_customLabelTop);

    m_customLabelBottom = new QLineEdit(m_customLabelGroup);
    m_customLabelBottom->setText(osd.customLabelBottom);
    customForm->addRow(::tr(QStringLiteral("settings.progress.custom_bottom")),
                       m_customLabelBottom);

    m_customLabelShowArt = new QCheckBox(::tr(QStringLiteral("settings.progress.custom_show_art")),
                                         m_customLabelGroup);
    m_customLabelShowArt->setChecked(osd.customLabelShowArt);
    customForm->addRow(QString(), m_customLabelShowArt);

    QLabel* tokensHint = new QLabel(::tr(QStringLiteral("settings.progress.custom_tokens_hint")),
                                    m_customLabelGroup);
    tokensHint->setStyleSheet(QStringLiteral("color: gray; font-style: italic; font-size: 9pt;"));
    tokensHint->setWordWrap(true);
    customForm->addRow(QString(), tokensHint);

    progressForm->addRow(QString(), m_customLabelGroup);
    connect(m_progressLabelMode, &QComboBox::currentIndexChanged, this,
            &SettingsDialog::updateCustomLabelVisibility);
    updateCustomLabelVisibility();

    m_trackedPlayers = new QLineEdit(this);
    m_trackedPlayers->setText(osd.trackedPlayers.join(QStringLiteral(", ")));
    m_trackedPlayers->setPlaceholderText(
        ::tr(QStringLiteral("settings.progress.tracked_players_hint")));
    progressForm->addRow(::tr(QStringLiteral("settings.progress.tracked_players")),
                         m_trackedPlayers);

    m_mediaControlsEnabled =
        new QCheckBox(::tr(QStringLiteral("settings.progress.media_controls")), this);
    m_mediaControlsEnabled->setChecked(osd.mediaControlsEnabled);
    progressForm->addRow(QString(), m_mediaControlsEnabled);

    m_exposeMpris = new QCheckBox(::tr(QStringLiteral("settings.progress.expose_mpris")), this);
    m_exposeMpris->setChecked(osd.exposeMpris);
    progressForm->addRow(QString(), m_exposeMpris);

    layout->addLayout(progressForm);

    // ── Profiles section ────────────────────────────────────────────────
    QLabel* profilesHeader = new QLabel(::tr(QStringLiteral("settings.profiles.section")), this);
    profilesHeader->setStyleSheet(QStringLiteral("font-weight: bold; margin-top: 8px;"));
    layout->addWidget(profilesHeader);

    m_profilesTable = new QTableWidget(this);
    m_profilesTable->setColumnCount(8);
    m_profilesTable->setHorizontalHeaderLabels(QStringList{
        ::tr(QStringLiteral("settings.profiles.col_name")),
        ::tr(QStringLiteral("settings.profiles.col_app")),
        ::tr(QStringLiteral("settings.profiles.col_modifiers")),
        ::tr(QStringLiteral("settings.profiles.col_volume_up")),
        ::tr(QStringLiteral("settings.profiles.col_volume_down")),
        ::tr(QStringLiteral("settings.profiles.col_mute")),
        ::tr(QStringLiteral("settings.profiles.col_ducking")),
        ::tr(QStringLiteral("settings.profiles.col_sink")),
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

    // ── Scenes section ───────────────────────────────────────────────────
    QLabel* scenesHeader = new QLabel(::tr(QStringLiteral("settings.scenes.section")), this);
    scenesHeader->setStyleSheet(QStringLiteral("font-weight: bold; margin-top: 8px;"));
    layout->addWidget(scenesHeader);

    m_scenesTable = new QTableWidget(this);
    m_scenesTable->setColumnCount(3);
    m_scenesTable->setHorizontalHeaderLabels(QStringList{
        ::tr(QStringLiteral("settings.scenes.col_name")),
        ::tr(QStringLiteral("settings.scenes.col_targets")),
        ::tr(QStringLiteral("settings.scenes.col_hotkey")),
    });
    m_scenesTable->verticalHeader()->setVisible(false);
    m_scenesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_scenesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_scenesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_scenesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_scenesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_scenesTable->setMinimumHeight(100);
    layout->addWidget(m_scenesTable);

    auto* sceneBtns = new QHBoxLayout;
    m_btnSceneAdd = new QPushButton(::tr(QStringLiteral("settings.scenes.add")), this);
    m_btnSceneEdit = new QPushButton(::tr(QStringLiteral("settings.scenes.edit")), this);
    m_btnSceneRemove = new QPushButton(::tr(QStringLiteral("settings.scenes.remove")), this);
    m_btnSceneDuplicate = new QPushButton(::tr(QStringLiteral("settings.scenes.duplicate")), this);
    m_btnSceneApply = new QPushButton(::tr(QStringLiteral("settings.scenes.apply")), this);
    sceneBtns->addWidget(m_btnSceneAdd);
    sceneBtns->addWidget(m_btnSceneEdit);
    sceneBtns->addWidget(m_btnSceneRemove);
    sceneBtns->addWidget(m_btnSceneDuplicate);
    sceneBtns->addWidget(m_btnSceneApply);
    sceneBtns->addStretch();
    layout->addLayout(sceneBtns);

    connect(m_btnSceneAdd, &QPushButton::clicked, this, &SettingsDialog::onAddScene);
    connect(m_btnSceneEdit, &QPushButton::clicked, this, &SettingsDialog::onEditScene);
    connect(m_btnSceneRemove, &QPushButton::clicked, this, &SettingsDialog::onRemoveScene);
    connect(m_btnSceneDuplicate, &QPushButton::clicked, this, &SettingsDialog::onDuplicateScene);
    connect(m_btnSceneApply, &QPushButton::clicked, this, &SettingsDialog::onApplyScene);
    connect(m_scenesTable, &QTableWidget::doubleClicked, this,
            [this](const QModelIndex&) { onEditScene(); });
    connect(m_scenesTable->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this](const QItemSelection&, const QItemSelection&)
            {
                const bool hasSel = selectedSceneRow() >= 0;
                m_btnSceneEdit->setEnabled(hasSel);
                m_btnSceneRemove->setEnabled(hasSel);
                m_btnSceneDuplicate->setEnabled(hasSel);
                m_btnSceneApply->setEnabled(hasSel);
            });

    refreshScenesTable();

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
    connect(m_osdScale, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double) { emitStylePreview(); });
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
    emit scalePreview(m_osdScale->value());
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

    // Progress / MPRIS fields — read current OsdConfig, patch, write back
    OsdConfig osd = m_config->osd();
    osd.progressEnabled = m_progressEnabled->isChecked();
    osd.progressInteractive = m_progressInteractive->isChecked();
    osd.progressPollMs = m_progressPollMs->value();
    osd.progressLabelMode = m_progressLabelMode->currentData().toString();
    osd.customLabelTop = m_customLabelTop->text();
    osd.customLabelBottom = m_customLabelBottom->text();
    osd.customLabelShowArt = m_customLabelShowArt->isChecked();
    {
        QStringList players;
        const QStringList raw =
            m_trackedPlayers->text().split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString& s : raw)
        {
            const QString t = s.trimmed().toLower();
            if (!t.isEmpty()) players.append(t);
        }
        osd.trackedPlayers = players;
    }
    osd.mediaControlsEnabled = m_mediaControlsEnabled->isChecked();
    osd.exposeMpris = m_exposeMpris->isChecked();
    osd.osdScale = std::clamp(m_osdScale->value(), 0.5, 3.0);
    m_config->setOsd(osd);

    m_config->setVolumeStep(m_step->value());
    m_config->setAutoProfileSwitch(m_autoProfile->isChecked());

    // Persist media hotkeys (global). Note: we do not validate conflicts with
    // profile bindings here — profile resolution is checked first at dispatch
    // time, so a profile binding always wins, and an overlap simply means the
    // media binding is shadowed when the matching profile is active.
    MediaHotkeyConfig media;
    media.playPause = m_mediaPlayPause->binding();
    media.next = m_mediaNext->binding();
    media.previous = m_mediaPrevious->binding();
    media.stop = m_mediaStop->binding();
    m_config->setMediaHotkeys(media);

    // Capture pre-save sinks so we can clear PA stream-restore for any profile
    // whose sink was explicitly switched back to "(system default)". Without
    // this, the previous device pref keeps re-routing the app on next launch
    // even though the UI shows "system default".
    QList<Profile> previousProfiles;
    if (m_volumeCtrl && !m_profiles.isEmpty()) previousProfiles = m_config->profiles();

    if (!m_profiles.isEmpty()) m_config->setProfiles(m_profiles);

    if (m_volumeCtrl && !previousProfiles.isEmpty())
    {
        for (const Profile& neu : m_profiles)
        {
            if (!neu.sink.isEmpty()) continue;
            for (const Profile& old : previousProfiles)
            {
                if (old.id != neu.id || old.sink.isEmpty()) continue;
                for (const QString& app : old.apps)
                    if (!app.isEmpty()) m_volumeCtrl->clearAppSinkOverride(app);
                break;
            }
        }
    }
    m_config->setScenes(m_scenes);
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
    m_profilesTable->setRowCount(static_cast<int>(m_profiles.size()));
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
        setCell(1, p.apps.join(", "));
        setCell(2, modsDisplay);
        setCell(3, HotkeyCapture::keyDisplayName(p.hotkeys.volumeUp));
        setCell(4, HotkeyCapture::keyDisplayName(p.hotkeys.volumeDown));
        setCell(5, HotkeyCapture::keyDisplayName(p.hotkeys.mute));
        setCell(6, p.ducking.enabled ? QStringLiteral("%1% / %2")
                                           .arg(p.ducking.volume)
                                           .arg(HotkeyCapture::keyDisplayName(p.ducking.hotkey))
                                     : ::tr(QStringLiteral("settings.profiles.modifier_none")));

        QString sinkDisplay;
        if (p.sink.isEmpty())
        {
            sinkDisplay = ::tr(QStringLiteral("settings.profiles.sink_default"));
        }
        else if (m_volumeCtrl)
        {
            // Match by stable PA name; fall back to "(missing) raw" when the sink
            // currently isn't enumerated (USB unplugged, profile carried over).
            QString matchedDesc;
            for (const SinkInfo& s : m_volumeCtrl->listSinks())
            {
                if (s.name == p.sink)
                {
                    matchedDesc = s.description;
                    break;
                }
            }
            sinkDisplay = matchedDesc.isEmpty()
                              ? ::tr(QStringLiteral("settings.profiles.sink_missing")).arg(p.sink)
                              : matchedDesc;
        }
        else
        {
            sinkDisplay = p.sink;
        }
        setCell(7, sinkDisplay);
    }

    m_btnRemove->setEnabled(m_profiles.size() > 1);
}

void SettingsDialog::updateCustomLabelVisibility()
{
    if (!m_customLabelGroup || !m_progressLabelMode) return;
    const bool custom = m_progressLabelMode->currentData().toString() == QLatin1String("custom");
    m_customLabelGroup->setVisible(custom);
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
    ProfileEditDialog dlg(blank, m_config, m_inputHandler, m_volumeCtrl, this);
    centerDialogOnScreenAt(&dlg, anchor);
    if (dlg.exec() == QDialog::Accepted)
    {
        m_profiles.append(dlg.result());
        refreshProfilesTable();
        m_profilesTable->selectRow(static_cast<int>(m_profiles.size()) - 1);
    }
}

void SettingsDialog::onEditProfile()
{
    int row = selectedProfileRow();
    if (row < 0 || row >= m_profiles.size()) return;

    const QPoint anchor = QCursor::pos();
    ProfileEditDialog dlg(m_profiles[row], m_config, m_inputHandler, m_volumeCtrl, this);
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
    if (!m_profiles.isEmpty())
        m_profilesTable->selectRow(qMin(row, static_cast<int>(m_profiles.size()) - 1));
}

void SettingsDialog::onSetDefaultProfile()
{
    int row = selectedProfileRow();
    if (row <= 0 || row >= m_profiles.size()) return; // already default or invalid
    m_profiles.move(row, 0);
    refreshProfilesTable();
    m_profilesTable->selectRow(0);
}

// ─── Scenes section ─────────────────────────────────────────────────────────
namespace
{
// Build a stable slug from a scene name, falling back to "scene". Only
// [a-z0-9-] are kept; runs of other characters collapse to a single '-'.
// Config::setScenes() guarantees final uniqueness, so we only need a sensible
// base here.
QString sceneSlug(const QString& name)
{
    QString slug;
    bool lastDash = false;
    for (const QChar c : name.toLower())
    {
        if (c.isLetterOrNumber())
        {
            slug.append(c);
            lastDash = false;
        }
        else if (!lastDash && !slug.isEmpty())
        {
            slug.append(QLatin1Char('-'));
            lastDash = true;
        }
    }
    while (slug.endsWith(QLatin1Char('-'))) slug.chop(1);
    return slug.isEmpty() ? QStringLiteral("scene") : slug;
}
} // namespace

int SettingsDialog::selectedSceneRow() const
{
    auto sel = m_scenesTable->selectionModel()->selectedRows();
    if (sel.isEmpty()) return -1;
    return sel.first().row();
}

void SettingsDialog::refreshScenesTable()
{
    m_scenesTable->setRowCount(static_cast<int>(m_scenes.size()));
    for (int row = 0; row < m_scenes.size(); ++row)
    {
        const AudioScene& s = m_scenes[row];

        QString targetsDisplay;
        QStringList names;
        for (const SceneTarget& t : s.targets)
            if (!t.match.isEmpty()) names << t.match;
        if (names.isEmpty())
            targetsDisplay = ::tr(QStringLiteral("settings.scenes.no_targets"));
        else if (names.size() <= 2)
            targetsDisplay = names.join(QStringLiteral(", "));
        else
            targetsDisplay =
                QStringLiteral("%1, %2 +%3").arg(names[0], names[1]).arg(names.size() - 2);

        auto setCell = [&](int col, const QString& text)
        {
            auto* item = new QTableWidgetItem(text);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            m_scenesTable->setItem(row, col, item);
        };
        setCell(0, s.name.isEmpty() ? s.id : s.name);
        setCell(1, targetsDisplay);
        setCell(2, HotkeyCapture::keyDisplayName(s.hotkey));
    }

    const bool hasSel = selectedSceneRow() >= 0;
    m_btnSceneEdit->setEnabled(hasSel);
    m_btnSceneRemove->setEnabled(hasSel);
    m_btnSceneDuplicate->setEnabled(hasSel);
    m_btnSceneApply->setEnabled(hasSel);
}

void SettingsDialog::onAddScene()
{
    AudioScene blank;
    blank.name = QStringLiteral("Scene ") + QString::number(m_scenes.size() + 1);

    const QPoint anchor = QCursor::pos();
    SceneEditDialog dlg(blank, m_config, m_inputHandler, this);
    centerDialogOnScreenAt(&dlg, anchor);
    if (dlg.exec() == QDialog::Accepted)
    {
        AudioScene scene = dlg.result();
        scene.id = sceneSlug(scene.name);
        m_scenes.append(scene);
        refreshScenesTable();
        m_scenesTable->selectRow(static_cast<int>(m_scenes.size()) - 1);
    }
}

void SettingsDialog::onEditScene()
{
    int row = selectedSceneRow();
    if (row < 0 || row >= m_scenes.size()) return;

    const QPoint anchor = QCursor::pos();
    SceneEditDialog dlg(m_scenes[row], m_config, m_inputHandler, this);
    centerDialogOnScreenAt(&dlg, anchor);
    if (dlg.exec() == QDialog::Accepted)
    {
        AudioScene scene = dlg.result();
        // Preserve the original id on edit (SceneEditDialog already does this,
        // but a freshly-named scene with an empty id gets a slug).
        if (scene.id.isEmpty()) scene.id = sceneSlug(scene.name);
        m_scenes[row] = scene;
        refreshScenesTable();
        m_scenesTable->selectRow(row);
    }
}

void SettingsDialog::onRemoveScene()
{
    int row = selectedSceneRow();
    if (row < 0 || row >= m_scenes.size()) return;
    m_scenes.removeAt(row);
    refreshScenesTable();
    if (!m_scenes.isEmpty())
        m_scenesTable->selectRow(qMin(row, static_cast<int>(m_scenes.size()) - 1));
}

void SettingsDialog::onDuplicateScene()
{
    int row = selectedSceneRow();
    if (row < 0 || row >= m_scenes.size()) return;

    AudioScene copy = m_scenes[row];
    copy.name = copy.name + ::tr(QStringLiteral("settings.scenes.copy_suffix"));
    copy.id = sceneSlug(copy.name);
    // Duplicate must not inherit the original's hotkey — two scenes sharing a
    // binding would shadow each other (first wins). Leave the copy unbound.
    copy.hotkey = {};
    m_scenes.insert(row + 1, copy);
    refreshScenesTable();
    m_scenesTable->selectRow(row + 1);
}

void SettingsDialog::onApplyScene()
{
    int row = selectedSceneRow();
    if (row < 0 || row >= m_scenes.size()) return;
    emit applySceneRequested(m_scenes[row]);
}
