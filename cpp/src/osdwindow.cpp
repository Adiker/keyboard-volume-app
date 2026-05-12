#include "osdwindow.h"
#include "config.h"
#include "i18n.h"
#include "waylandstate.h"

#ifdef HAVE_LAYER_SHELL_QT
#include <LayerShellQt/Window>
#endif

#include <QApplication>
#include <QEnterEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QScreen>
#include <QWindow>
#include <QPaintEvent>

static constexpr int OSD_W = 220;
static constexpr int OSD_H_BASE = 70;      // volume only
static constexpr int OSD_H_PROGRESS = 112; // volume + progress row

// ─── Construction ─────────────────────────────────────────────────────────────

OSDWindow::OSDWindow(Config* config, QWidget* parent) : QWidget(parent), m_config(config)
{
    buildUi();
    applyStyles();
    setProgressEnabled(m_config->osd().progressEnabled);
}

void OSDWindow::buildUi()
{
    Qt::WindowFlags flags = Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool;
    if (!g_nativeWayland) flags |= Qt::BypassWindowManagerHint;
    setWindowFlags(flags);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(OSD_W, OSD_H_BASE);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(4);

    // ── Volume row ────────────────────────────────────────────────────────────
    m_labelName = new QLabel(this);
    m_labelName->setAlignment(Qt::AlignCenter);
    m_labelName->setObjectName(QStringLiteral("name_label"));

    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 100);
    m_bar->setTextVisible(false);
    m_bar->setFixedHeight(10);

    m_labelPct = new QLabel(this);
    m_labelPct->setAlignment(Qt::AlignCenter);
    m_labelPct->setObjectName(QStringLiteral("pct_label"));

    layout->addWidget(m_labelName);
    layout->addWidget(m_bar);
    layout->addWidget(m_labelPct);

    // ── Progress row (hidden by default) ─────────────────────────────────────
    m_progressRow = new QWidget(this);
    m_progressRow->setVisible(false);

    QVBoxLayout* pLayout = new QVBoxLayout(m_progressRow);
    pLayout->setContentsMargins(0, 2, 0, 0);
    pLayout->setSpacing(2);

    m_labelTrack = new QLabel(m_progressRow);
    m_labelTrack->setAlignment(Qt::AlignCenter);
    m_labelTrack->setObjectName(QStringLiteral("track_label"));

    m_progressBar = new QProgressBar(m_progressRow);
    m_progressBar->setRange(0, 1000);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(8);
    m_progressBar->installEventFilter(this); // seek interaction

    m_labelTime = new QLabel(m_progressRow);
    m_labelTime->setAlignment(Qt::AlignCenter);
    m_labelTime->setObjectName(QStringLiteral("time_label"));

    pLayout->addWidget(m_labelTrack);
    pLayout->addWidget(m_progressBar);
    pLayout->addWidget(m_labelTime);

    layout->addWidget(m_progressRow);

    // ── Hide timer ────────────────────────────────────────────────────────────
    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    connect(m_hideTimer, &QTimer::timeout, this, &OSDWindow::hide);
}

// ─── Styles ───────────────────────────────────────────────────────────────────

void OSDWindow::applyStyles()
{
    OsdConfig osd = m_config->osd();
    applyColorStyles(osd.colorBg, osd.colorText, osd.colorBar, osd.opacity);
}

void OSDWindow::applyColorStyles(const QString& colorBg, const QString& colorText,
                                 const QString& colorBar, int opacity)
{
    m_bgColor = QColor(colorBg);
    m_bgColor.setAlpha(qRound(opacity / 100.0 * 255));

    QString text = colorText;
    if (text.startsWith(QLatin1Char('#'))) text.remove(0, 1);
    QString bar = colorBar;
    if (bar.startsWith(QLatin1Char('#'))) bar.remove(0, 1);

    setStyleSheet(
        QStringLiteral(
            "QLabel { color: #%1; background: transparent; }"
            "QLabel#name_label  { font-size: 11pt; font-weight: bold; font-family: 'Noto Sans', "
            "'Segoe UI', sans-serif; }"
            "QLabel#pct_label   { font-size: 9pt;  font-family: 'Noto Sans', 'Segoe UI', "
            "sans-serif; }"
            "QLabel#track_label { font-size: 9pt;  font-family: 'Noto Sans', 'Segoe UI', "
            "sans-serif; }"
            "QLabel#time_label  { font-size: 8pt;  font-family: 'Noto Sans', 'Segoe UI', "
            "sans-serif; }"
            "QProgressBar { background-color: #333333; border: none; border-radius: 3px; }"
            "QProgressBar::chunk { background-color: #%2; border-radius: 3px; }")
            .arg(text, bar));

    m_labelName->setStyleSheet(
        QStringLiteral("font-size: 11pt; font-weight: bold; color: #%1; background: transparent;")
            .arg(text));
    m_labelPct->setStyleSheet(
        QStringLiteral("font-size: 9pt; color: #%1; background: transparent;").arg(text));
    if (m_labelTrack)
        m_labelTrack->setStyleSheet(
            QStringLiteral("font-size: 9pt; color: #%1; background: transparent;").arg(text));
    if (m_labelTime)
        m_labelTime->setStyleSheet(
            QStringLiteral("font-size: 8pt; color: #%1; background: transparent;").arg(text));
    update();
}

// ─── paintEvent ───────────────────────────────────────────────────────────────

void OSDWindow::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_bgColor);
    painter.drawRoundedRect(rect(), 8, 8);
    QWidget::paintEvent(event);
}

// ─── Hover — suspend hide timer ───────────────────────────────────────────────

void OSDWindow::enterEvent(QEnterEvent* /*event*/)
{
    if (m_hideTimer->isActive()) m_hideTimer->stop();
}

void OSDWindow::leaveEvent(QEvent* /*event*/)
{
    if (!isVisible()) return;
    if (m_previewMode)
    {
        if (!m_previewHeld) m_hideTimer->start(m_previewTimeoutMs);
        return;
    }
    m_hideTimer->start(m_config->osd().timeoutMs);
}

// ─── Size helpers ─────────────────────────────────────────────────────────────

void OSDWindow::applySize()
{
    const int h = (m_progressEnabled && m_progressVisible) ? OSD_H_PROGRESS : OSD_H_BASE;
    setFixedSize(OSD_W, h);

#ifdef HAVE_LAYER_SHELL_QT
    if (m_layerShellActive && m_lsWindow && isVisible())
    {
        auto [absX, absY] = absPos();
        positionWindow(absX, absY);
        return;
    }
#endif
    if (isVisible())
    {
        auto [absX, absY] = absPos();
        move(absX, absY);
        setGeometry(absX, absY, OSD_W, h);
        if (QWindow* wh = windowHandle()) wh->setPosition(absX, absY);
    }
}

// ─── Wayland layer-shell initialisation ───────────────────────────────────────

void OSDWindow::initLayerShell()
{
#ifdef HAVE_LAYER_SHELL_QT
    if (!g_nativeWayland) return;

    if (QGuiApplication::platformName() != QLatin1String("wayland"))
    {
        qDebug() << "[OSDWindow] initLayerShell: Qt platform is" << QGuiApplication::platformName()
                 << "— skipping layer-shell";
        return;
    }

    (void)winId();
    QWindow* win = windowHandle();
    if (!win)
    {
        qWarning() << "[OSDWindow] initLayerShell: no windowHandle() after winId()";
        return;
    }

    m_lsWindow = LayerShellQt::Window::get(win);
    if (!m_lsWindow)
    {
        qWarning() << "[OSDWindow] initLayerShell: LayerShellQt::Window::get() returned null";
        return;
    }

    m_lsWindow->setLayer(LayerShellQt::Window::LayerOverlay);
    m_lsWindow->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop |
                                                         LayerShellQt::Window::AnchorLeft));
    m_lsWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    m_lsWindow->setExclusiveZone(0);
    m_lsWindow->setActivateOnShow(false);
    m_lsWindow->setScope(QStringLiteral("keyboard-volume-app-osd"));

    m_layerShellActive = true;
    qDebug() << "[OSDWindow] layer-shell initialised (native Wayland OSD)";
#endif
}

// ─── Position helpers ─────────────────────────────────────────────────────────

std::pair<int, int> OSDWindow::absPos() const
{
    OsdConfig osd = m_config->osd();
    QList<QScreen*> screens = QApplication::screens();
    if (screens.isEmpty()) return {osd.x, osd.y};
    int idx = osd.screen;
    if (idx >= screens.size()) idx = 0;
    QRect geo = screens[idx]->geometry();
    return {geo.x() + osd.x, geo.y() + osd.y};
}

void OSDWindow::positionWindow(int absX, int absY)
{
#ifdef HAVE_LAYER_SHELL_QT
    if (m_layerShellActive && m_lsWindow)
    {
        QScreen* screen = QApplication::screenAt(QPoint(absX, absY));
        if (!screen && !QApplication::screens().isEmpty()) screen = QApplication::screens().first();

        if (screen && screen != m_lsScreen)
        {
            m_lsWindow->setScreen(screen);
            m_lsScreen = screen;
        }

        int relX = absX;
        int relY = absY;
        if (screen)
        {
            const QRect geo = screen->geometry();
            relX = absX - geo.x();
            relY = absY - geo.y();
        }
        m_lsWindow->setMargins(QMargins(relX, relY, 0, 0));

        show();
        raise();
        return;
    }
#endif
    move(absX, absY);
    setGeometry(absX, absY, width(), height());
    show();
    raise();
    if (QWindow* wh = windowHandle()) wh->setPosition(absX, absY);
}

// ─── Progress row API ─────────────────────────────────────────────────────────

void OSDWindow::setProgressEnabled(bool on)
{
    m_progressEnabled = on;
    if (!on)
    {
        finishSeeking();
        m_progressVisible = false;
        m_progressRow->setVisible(false);
        refreshNameLabel();
        applySize();
    }
}

void OSDWindow::setProgressVisible(bool on)
{
    if (!m_progressEnabled) return;
    if (m_progressVisible == on) return;
    if (!on) finishSeeking();
    m_progressVisible = on;
    m_progressRow->setVisible(on);
    refreshNameLabel();
    applySize();
}

void OSDWindow::updateTrack(const QString& title, const QString& artist, qint64 lengthUs,
                            bool canSeek)
{
    m_trackTitle = title;
    m_trackArtist = artist;
    m_trackLengthUs = lengthUs;
    m_canSeek = canSeek;
    if (!m_canSeek || m_trackLengthUs <= 0) finishSeeking();

    refreshNameLabel();

    if (lengthUs <= 0)
    {
        // Live stream
        m_progressBar->setEnabled(false);
        m_progressBar->setValue(0);
        m_labelTime->setText(QStringLiteral("LIVE"));
    }
    else
    {
        m_progressBar->setEnabled(true);
        m_progressBar->setValue(0);
        m_labelTime->setText(QStringLiteral("0:00 / %1").arg(formatTime(lengthUs)));
    }
}

void OSDWindow::updatePosition(qint64 positionUs)
{
    if (m_trackLengthUs <= 0 || m_seeking) return;

    const int val =
        static_cast<int>(std::clamp(positionUs * 1000LL / m_trackLengthUs, 0LL, 1000LL));
    m_progressBar->setValue(val);
    m_labelTime->setText(
        QStringLiteral("%1 / %2").arg(formatTime(positionUs), formatTime(m_trackLengthUs)));
}

// ─── Seek interaction (event filter on m_progressBar) ─────────────────────────

bool OSDWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (obj != m_progressBar) return QWidget::eventFilter(obj, event);

    auto posFromMouseX = [this](int mouseX) -> qint64
    {
        const int w = m_progressBar->width();
        if (w <= 0) return 0;
        const double ratio = std::clamp(static_cast<double>(mouseX) / w, 0.0, 1.0);
        return static_cast<qint64>(ratio * m_trackLengthUs);
    };

    auto applySeekPosition = [this, &posFromMouseX](int mouseX)
    {
        const qint64 posUs = posFromMouseX(mouseX);
        const int val = static_cast<int>(posUs * 1000LL / m_trackLengthUs);
        m_progressBar->setValue(val);
        m_labelTime->setText(
            QStringLiteral("%1 / %2").arg(formatTime(posUs), formatTime(m_trackLengthUs)));
        emit seekRequested(posUs);
    };

    if (m_seeking)
    {
        if (event->type() == QEvent::MouseButtonRelease)
        {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton)
            {
                if (m_canSeek && m_trackLengthUs > 0 && m_config->osd().progressInteractive)
                    applySeekPosition(me->position().toPoint().x());
                finishSeeking();
                return true;
            }
        }
        else if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::Hide)
        {
            finishSeeking();
        }
    }

    if (!m_canSeek || m_trackLengthUs <= 0 || !m_config->osd().progressInteractive)
        return QWidget::eventFilter(obj, event);

    if (event->type() == QEvent::MouseButtonPress)
    {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton)
        {
            m_seeking = true;
            emit seekStarted();
            applySeekPosition(me->position().toPoint().x());
            return true;
        }
    }
    else if (event->type() == QEvent::MouseMove && m_seeking)
    {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->buttons() & Qt::LeftButton)
        {
            applySeekPosition(me->position().toPoint().x());
            return true;
        }
        finishSeeking();
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void OSDWindow::refreshNameLabel()
{
    const OsdConfig osd = m_config->osd();
    const QString& mode = osd.progressLabelMode;

    if (!m_progressEnabled || !m_progressVisible || mode == QLatin1String("app"))
    {
        m_labelName->setText(m_currentAppName);
        if (m_labelTrack) m_labelTrack->setText(m_trackTitle);
        return;
    }

    if (mode == QLatin1String("track"))
    {
        const QString trackText =
            m_trackArtist.isEmpty()
                ? m_trackTitle
                : QStringLiteral("%1 \u2014 %2").arg(m_trackTitle, m_trackArtist);
        m_labelName->setText(trackText);
        if (m_labelTrack) m_labelTrack->setText(QString{});
    }
    else // "both"
    {
        m_labelName->setText(m_currentAppName);
        const QString trackText =
            m_trackArtist.isEmpty()
                ? m_trackTitle
                : QStringLiteral("%1 \u2014 %2").arg(m_trackTitle, m_trackArtist);
        if (m_labelTrack) m_labelTrack->setText(trackText);
    }
}

void OSDWindow::finishSeeking()
{
    if (!m_seeking) return;
    m_seeking = false;
    emit seekFinished();
}

// static
QString OSDWindow::formatTime(qint64 us)
{
    if (us < 0) us = 0;
    const qint64 totalSec = us / 1000000LL;
    const qint64 min = totalSec / 60;
    const qint64 sec = totalSec % 60;
    return QStringLiteral("%1:%2").arg(min).arg(sec, 2, 10, QLatin1Char('0'));
}

// ─── Public API ───────────────────────────────────────────────────────────────

void OSDWindow::showVolume(const QString& appName, double volume, bool muted)
{
    m_previewMode = false;
    m_previewHeld = false;
    m_currentAppName = appName;
    OsdConfig osd = m_config->osd();
    int pct = qRound(volume * 100);

    refreshNameLabel();
    m_bar->setValue(pct);
    m_labelPct->setText(muted ? QStringLiteral("%1%  \U0001F507").arg(pct)
                              : QStringLiteral("%1%").arg(pct));

    auto [absX, absY] = absPos();
    positionWindow(absX, absY);
    m_hideTimer->start(osd.timeoutMs);
}

void OSDWindow::showPreview(int screenIdx, int x, int y, int timeoutMs)
{
    m_previewMode = true;
    m_previewHeld = false;
    m_previewTimeoutMs = timeoutMs;
    m_hideTimer->stop();

    m_labelName->setText(::tr(QStringLiteral("osd.preview")));
    m_bar->setValue(60);
    m_labelPct->setText(QStringLiteral("60%"));

    QList<QScreen*> screens = QApplication::screens();
    if (screens.isEmpty()) return;
    if (screenIdx >= screens.size()) screenIdx = 0;
    QRect geo = screens[screenIdx]->geometry();
    positionWindow(geo.x() + x, geo.y() + y);
    m_hideTimer->start(timeoutMs);
}

void OSDWindow::hidePreview()
{
    if (m_previewMode)
    {
        m_previewMode = false;
        m_previewHeld = false;
        hide();
    }
}

void OSDWindow::showPreviewHeld(int screenIdx, int x, int y)
{
    showPreview(screenIdx, x, y);
    m_previewHeld = true;
    m_hideTimer->stop();
}

void OSDWindow::releasePreview(int timeoutMs)
{
    m_previewHeld = false;
    m_previewTimeoutMs = timeoutMs;
    if (isVisible() && m_previewMode) m_hideTimer->start(m_previewTimeoutMs);
}

void OSDWindow::applyPreviewColors(const QString& colorBg, const QString& colorText,
                                   const QString& colorBar, int opacity)
{
    applyColorStyles(colorBg, colorText, colorBar, opacity);
}

void OSDWindow::reloadStyles()
{
    applyStyles();
    setProgressEnabled(m_config->osd().progressEnabled);
    if (!m_config->osd().progressInteractive) finishSeeking();
    auto [absX, absY] = absPos();
#ifdef HAVE_LAYER_SHELL_QT
    if (m_layerShellActive && m_lsWindow)
    {
        if (isVisible()) positionWindow(absX, absY);
        update();
        return;
    }
#endif
    move(absX, absY);
    setGeometry(absX, absY, width(), height());
    if (isVisible())
    {
        if (QWindow* wh = windowHandle()) wh->setPosition(absX, absY);
    }
    update();
}
