#include "osdwindow.h"
#include "config.h"
#include "i18n.h"
#include "osdlabelformat.h"
#include "waylandstate.h"

#ifdef HAVE_LAYER_SHELL_QT
#include <LayerShellQt/Window>
#endif

#include <QApplication>
#include <QEnterEvent>
#include <QFont>
#include <QLabel>
#include <QHideEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QDebug>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QScreen>
#include <QWindow>
#include <QPaintEvent>

#include <cmath>

static bool progressDebugEnabled()
{
    return qEnvironmentVariableIsSet("KVA_DEBUG_PROGRESS");
}

// ─── MarqueeLabel ─────────────────────────────────────────────────────────────
// QLabel that scrolls its text horizontally when the text is wider than the
// widget.  State machine: pause 1.5 s → scroll left → pause 1 s → reset.
// No Q_OBJECT needed — uses a lambda slot on the timer.
class MarqueeLabel : public QLabel
{
  public:
    explicit MarqueeLabel(QWidget* parent = nullptr) : QLabel(parent)
    {
        m_timer = new QTimer(this);
        m_timer->setInterval(30); // ~33 fps
        QObject::connect(m_timer, &QTimer::timeout, m_timer, [this] { tick(); });
    }

  protected:
    void paintEvent(QPaintEvent* event) override
    {
        // Detect text changes (QLabel::setText is not virtual).
        const QString cur = text();
        if (cur != m_lastText)
        {
            m_lastText = cur;
            reset();
        }

        const int textW = fontMetrics().horizontalAdvance(cur);
        if (textW <= width())
        {
            // Text fits — stop any animation and let QLabel render normally.
            if (m_timer->isActive()) reset();
            QLabel::paintEvent(event);
            return;
        }

        // Text overflows — clip and draw at current scroll offset.
        if (!m_timer->isActive())
        {
            m_state = PauseStart;
            m_pauseMs = 0;
            m_timer->start();
        }
        QPainter p(this);
        p.setClipRect(rect());
        p.setPen(palette().color(QPalette::WindowText));
        p.setFont(font());
        p.drawText(QRect(-m_offset, 0, textW, height()), Qt::AlignVCenter | Qt::AlignLeft, cur);
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        reset();
    }

    void hideEvent(QHideEvent* event) override
    {
        QLabel::hideEvent(event);
        m_timer->stop(); // no wakeups while invisible
    }

    void showEvent(QShowEvent* event) override
    {
        QLabel::showEvent(event);
        reset(); // restart from the beginning when the label reappears
    }

  private:
    void tick()
    {
        const int textW = fontMetrics().horizontalAdvance(text());
        const int maxOff = textW - width();
        if (maxOff <= 0)
        {
            reset();
            return;
        }
        switch (m_state)
        {
        case PauseStart:
            m_pauseMs += 30;
            if (m_pauseMs >= 1500) m_state = Scrolling;
            break;
        case Scrolling:
            ++m_offset;
            if (m_offset >= maxOff)
            {
                m_offset = maxOff;
                m_state = PauseEnd;
                m_pauseMs = 0;
            }
            break;
        case PauseEnd:
            m_pauseMs += 30;
            if (m_pauseMs >= 1000)
            {
                m_offset = 0;
                m_state = PauseStart;
                m_pauseMs = 0;
            }
            break;
        }
        update();
    }

    void reset()
    {
        m_timer->stop();
        m_offset = 0;
        m_state = PauseStart;
        m_pauseMs = 0;
        update();
    }

    enum State
    {
        PauseStart,
        Scrolling,
        PauseEnd
    };
    QTimer* m_timer = nullptr;
    int m_offset = 0;
    int m_pauseMs = 0;
    State m_state = PauseStart;
    QString m_lastText;
};

static constexpr int OSD_W = 220;
static constexpr int OSD_H_BASE = 70;      // volume only
static constexpr int OSD_H_PROGRESS = 112; // volume + progress row
static constexpr int OSD_H_CONTROLS = 138; // volume + progress row + controls row
static constexpr int OSD_POS_BTN_W = 22;
static constexpr int OSD_POS_BTN_H = 18;
static constexpr int OSD_POS_INSET = 3;

// ─── Construction ─────────────────────────────────────────────────────────────

OSDWindow::OSDWindow(Config* config, QWidget* parent) : QWidget(parent), m_config(config)
{
    buildUi();
    applyStyles();
    const OsdConfig osd = m_config->osd();
    m_mediaControlsEnabled = osd.mediaControlsEnabled;
    setProgressEnabled(osd.progressEnabled);
    setPositionControlsEnabled(osd.positionControlsEnabled);
}

void OSDWindow::buildUi()
{
    Qt::WindowFlags flags = Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool;
    if (!g_nativeWayland) flags |= Qt::BypassWindowManagerHint;
    setWindowFlags(flags);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setFixedSize(scaled(OSD_W), scaled(OSD_H_BASE));

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(scaled(10), scaled(8), scaled(10), scaled(8));
    m_mainLayout->setSpacing(scaled(4));

    // ── Volume row ────────────────────────────────────────────────────────────
    m_labelName = new MarqueeLabel(this);
    m_labelName->setAlignment(Qt::AlignCenter);
    m_labelName->setObjectName(QStringLiteral("name_label"));

    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 100);
    m_bar->setTextVisible(false);
    m_bar->setFixedHeight(scaled(10));

    m_labelPct = new QLabel(this);
    m_labelPct->setAlignment(Qt::AlignCenter);
    m_labelPct->setObjectName(QStringLiteral("pct_label"));

    m_mainLayout->addWidget(m_labelName);
    m_mainLayout->addWidget(m_bar);
    m_mainLayout->addWidget(m_labelPct);

    // ── Progress row (hidden by default) ─────────────────────────────────────
    m_progressRow = new QWidget(this);
    m_progressRow->setVisible(false);

    m_progressOuterLayout = new QHBoxLayout(m_progressRow);
    m_progressOuterLayout->setContentsMargins(0, scaled(2), 0, 0);
    m_progressOuterLayout->setSpacing(scaled(6));

    m_albumArt = new QLabel(m_progressRow);
    m_albumArt->setObjectName(QStringLiteral("album_art"));
    m_albumArt->setFixedSize(scaled(36), scaled(36));
    m_albumArt->setScaledContents(true);
    m_albumArt->setVisible(false);

    m_progressContent = new QWidget(m_progressRow);
    m_progressLayout = new QVBoxLayout(m_progressContent);
    m_progressLayout->setContentsMargins(0, 0, 0, 0);
    m_progressLayout->setSpacing(scaled(2));

    m_labelTrack = new MarqueeLabel(m_progressContent);
    m_labelTrack->setAlignment(Qt::AlignCenter);
    m_labelTrack->setObjectName(QStringLiteral("track_label"));

    m_progressBar = new QProgressBar(m_progressContent);
    m_progressBar->setRange(0, 1000);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(scaled(8));
    m_progressBar->installEventFilter(this); // seek interaction

    m_labelTime = new QLabel(m_progressContent);
    m_labelTime->setAlignment(Qt::AlignCenter);
    m_labelTime->setObjectName(QStringLiteral("time_label"));

    // ── Media controls row ────────────────────────────────────────────────────
    m_controlsRow = new QWidget(m_progressContent);
    m_controlsRow->setObjectName(QStringLiteral("controls_row"));
    m_controlsLayout = new QHBoxLayout(m_controlsRow);
    m_controlsLayout->setContentsMargins(0, 0, 0, 0);
    m_controlsLayout->setSpacing(scaled(8));
    m_controlsLayout->addStretch();

    m_btnPrev = new QPushButton(QStringLiteral("\u23EE"), m_controlsRow);      // ⏮
    m_btnPlayPause = new QPushButton(QStringLiteral("\u23F5"), m_controlsRow); // ⏵
    m_btnNext = new QPushButton(QStringLiteral("\u23ED"), m_controlsRow);      // ⏭

    for (auto* btn : {m_btnPrev, m_btnPlayPause, m_btnNext})
    {
        btn->setFixedSize(scaled(24), scaled(20));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFlat(true);
        btn->setObjectName(QStringLiteral("ctrl_btn"));
    }

    m_controlsLayout->addWidget(m_btnPrev);
    m_controlsLayout->addWidget(m_btnPlayPause);
    m_controlsLayout->addWidget(m_btnNext);
    m_controlsLayout->addStretch();

    connect(m_btnPrev, &QPushButton::clicked, this, &OSDWindow::previousRequested);
    connect(m_btnPlayPause, &QPushButton::clicked, this, &OSDWindow::playPauseRequested);
    connect(m_btnNext, &QPushButton::clicked, this, &OSDWindow::nextRequested);

    m_progressLayout->addWidget(m_labelTrack);
    m_progressLayout->addWidget(m_progressBar);
    m_progressLayout->addWidget(m_controlsRow);
    m_progressLayout->addWidget(m_labelTime);

    m_progressOuterLayout->addWidget(m_albumArt, 0, Qt::AlignVCenter);
    m_progressOuterLayout->addWidget(m_progressContent, 1);

    m_mainLayout->addWidget(m_progressRow);

    // ── Position controls (corner overlay — not in layout flow) ─────────────
    m_btnPosUp = new QPushButton(QStringLiteral("\u2191"), this);
    m_btnPosLeft = new QPushButton(QStringLiteral("\u2190"), this);
    m_btnPosDown = new QPushButton(QStringLiteral("\u2193"), this);
    m_btnPosRight = new QPushButton(QStringLiteral("\u2192"), this);
    for (auto* btn : {m_btnPosUp, m_btnPosLeft, m_btnPosDown, m_btnPosRight})
    {
        btn->setObjectName(QStringLiteral("pos_btn"));
        btn->setFixedSize(scaled(OSD_POS_BTN_W), scaled(OSD_POS_BTN_H));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->hide();
    }

    connect(m_btnPosUp, &QPushButton::clicked, this, &OSDWindow::snapUp);
    connect(m_btnPosDown, &QPushButton::clicked, this, &OSDWindow::snapDown);
    connect(m_btnPosLeft, &QPushButton::clicked, this, &OSDWindow::snapLeft);
    connect(m_btnPosRight, &QPushButton::clicked, this, &OSDWindow::snapRight);

    // ── Hide timer ────────────────────────────────────────────────────────────
    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    connect(m_hideTimer, &QTimer::timeout, this,
            [this]
            {
                m_mediaActionMode = false;
                hide();
            });

    installResizeEventFilters();
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

    m_cachedStyleText = colorText;
    if (m_cachedStyleText.startsWith(QLatin1Char('#'))) m_cachedStyleText.remove(0, 1);
    m_cachedStyleBar = colorBar;
    if (m_cachedStyleBar.startsWith(QLatin1Char('#'))) m_cachedStyleBar.remove(0, 1);
    applyScaleFonts();
}

void OSDWindow::ensureColorCache()
{
    if (!m_cachedStyleText.isEmpty() && !m_cachedStyleBar.isEmpty()) return;

    const OsdConfig osd = m_config->osd();
    m_cachedStyleText = osd.colorText;
    if (m_cachedStyleText.startsWith(QLatin1Char('#'))) m_cachedStyleText.remove(0, 1);
    m_cachedStyleBar = osd.colorBar;
    if (m_cachedStyleBar.startsWith(QLatin1Char('#'))) m_cachedStyleBar.remove(0, 1);
}

void OSDWindow::applyScaleFonts()
{
    ensureColorCache();

    const double s = activeScale();
    const int ptName = qMax(6, qRound(11 * s));
    const int ptPct = qMax(5, qRound(9 * s));
    const int ptTrack = qMax(5, qRound(9 * s));
    const int ptTime = qMax(4, qRound(8 * s));
    const int ptCtrl = qMax(6, qRound(12 * s));

    const QString& text = m_cachedStyleText;
    const QString& bar = m_cachedStyleBar;

    setStyleSheet(
        QStringLiteral(
            "QLabel { color: #%1; background: transparent; }"
            "QLabel#name_label  { font-size: %3pt; font-weight: bold; font-family: 'Noto Sans', "
            "'Segoe UI', sans-serif; }"
            "QLabel#pct_label   { font-size: %4pt; font-family: 'Noto Sans', 'Segoe UI', "
            "sans-serif; }"
            "QLabel#track_label { font-size: %5pt; font-family: 'Noto Sans', 'Segoe UI', "
            "sans-serif; }"
            "QLabel#time_label  { font-size: %6pt; font-family: 'Noto Sans', 'Segoe UI', "
            "sans-serif; }"
            "QProgressBar { background-color: #333333; border: none; border-radius: 3px; }"
            "QProgressBar::chunk { background-color: #%2; border-radius: 3px; }")
            .arg(text, bar)
            .arg(ptName)
            .arg(ptPct)
            .arg(ptTrack)
            .arg(ptTime));

    m_labelName->setStyleSheet(
        QStringLiteral("font-size: %2pt; font-weight: bold; color: #%1; background: transparent;")
            .arg(text)
            .arg(ptName));
    m_labelPct->setStyleSheet(
        QStringLiteral("font-size: %2pt; color: #%1; background: transparent;")
            .arg(text)
            .arg(ptPct));
    if (m_labelTrack)
        m_labelTrack->setStyleSheet(
            QStringLiteral("font-size: %2pt; color: #%1; background: transparent;")
                .arg(text)
                .arg(ptTrack));
    if (m_labelTime)
        m_labelTime->setStyleSheet(
            QStringLiteral("font-size: %2pt; color: #%1; background: transparent;")
                .arg(text)
                .arg(ptTime));
    const QString ctrlStyle =
        QStringLiteral("QPushButton#ctrl_btn { background: transparent; border: none; "
                       "color: #%1; font-size: %3pt; font-family: 'Noto Sans', 'Segoe UI', "
                       "sans-serif; padding: 0; }"
                       "QPushButton#ctrl_btn:hover { color: #%2; }")
            .arg(text, bar)
            .arg(ptCtrl);
    if (m_controlsRow) m_controlsRow->setStyleSheet(ctrlStyle);
    const QString posStyle =
        QStringLiteral("QPushButton#pos_btn { background: rgba(0,0,0,40); border: none; "
                       "border-radius: %4px; color: #%1; font-size: %3pt; "
                       "font-family: 'Noto Sans', 'Segoe UI', sans-serif; padding: 0; }"
                       "QPushButton#pos_btn:hover { background: rgba(0,0,0,64); color: #%2; }")
            .arg(text, bar)
            .arg(ptCtrl)
            .arg(qMax(2, scaled(3)));
    for (auto* btn : {m_btnPosUp, m_btnPosDown, m_btnPosLeft, m_btnPosRight})
        if (btn) btn->setStyleSheet(posStyle);
    update();
}

void OSDWindow::applyResizeFontsFast(double scale)
{
    const auto setPt = [](QWidget* widget, double basePt, int minPt, double s)
    {
        if (!widget) return;
        QFont font = widget->font();
        font.setPointSizeF(std::max(static_cast<double>(minPt), basePt * s));
        widget->setFont(font);
    };

    setPt(m_labelName, 11, 6, scale);
    setPt(m_labelPct, 9, 5, scale);
    if (m_labelTrack) setPt(m_labelTrack, 9, 5, scale);
    if (m_labelTime) setPt(m_labelTime, 8, 4, scale);
    for (auto* btn : {m_btnPrev, m_btnPlayPause, m_btnNext}) setPt(btn, 12, 6, scale);
    for (auto* btn : {m_btnPosUp, m_btnPosDown, m_btnPosLeft, m_btnPosRight})
        setPt(btn, 11, 6, scale);
}

void OSDWindow::enterResizeStyleMode()
{
    ensureColorCache();

    const QString& text = m_cachedStyleText;
    const QString& bar = m_cachedStyleBar;
    const QString labelBase = QStringLiteral("color: #%1; background: transparent;").arg(text);

    setStyleSheet(
        QStringLiteral(
            "QLabel { color: #%1; background: transparent; }"
            "QProgressBar { background-color: #333333; border: none; border-radius: 3px; }"
            "QProgressBar::chunk { background-color: #%2; border-radius: 3px; }")
            .arg(text, bar));

    m_labelName->setStyleSheet(QStringLiteral("font-weight: bold; ") + labelBase);
    m_labelPct->setStyleSheet(labelBase);
    if (m_labelTrack) m_labelTrack->setStyleSheet(labelBase);
    if (m_labelTime) m_labelTime->setStyleSheet(labelBase);
    if (m_controlsRow)
    {
        m_controlsRow->setStyleSheet(
            QStringLiteral("QPushButton#ctrl_btn { background: transparent; border: none; "
                           "color: #%1; padding: 0; }"
                           "QPushButton#ctrl_btn:hover { color: #%2; }")
                .arg(text, bar));
    }
    for (auto* btn : {m_btnPosUp, m_btnPosDown, m_btnPosLeft, m_btnPosRight})
    {
        if (!btn) continue;
        btn->setStyleSheet(
            QStringLiteral("QPushButton#pos_btn { background: rgba(0,0,0,40); border: none; "
                           "color: #%1; padding: 0; }"
                           "QPushButton#pos_btn:hover { color: #%2; }")
                .arg(text, bar));
    }
}

// ─── paintEvent ───────────────────────────────────────────────────────────────

void OSDWindow::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_bgColor);
    painter.drawRoundedRect(rect(), scaled(8), scaled(8));
    QWidget::paintEvent(event);
}

void OSDWindow::hideEvent(QHideEvent* event)
{
    m_mediaActionMode = false;
    if (m_moving) finishMove(false);
    updateLayoutKeysActive();
    QWidget::hideEvent(event);
}

void OSDWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    layoutPositionButtons();
}

// ─── Hover — suspend hide timer ───────────────────────────────────────────────

void OSDWindow::enterEvent(QEnterEvent* /*event*/)
{
    if (m_hideTimer->isActive()) m_hideTimer->stop();
}

void OSDWindow::leaveEvent(QEvent* /*event*/)
{
    if (!isVisible()) return;
    if (m_resizing || m_moving) return;
    if (m_previewMode)
    {
        if (!m_previewHeld) m_hideTimer->start(m_previewTimeoutMs);
        return;
    }
    m_hideTimer->start(m_config->osd().timeoutMs);
}

void OSDWindow::mousePressEvent(QMouseEvent* event)
{
    if (handleResizeMouseEvent(this, event)) return;
    QWidget::mousePressEvent(event);
}

void OSDWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (handleResizeMouseEvent(this, event)) return;
    QWidget::mouseMoveEvent(event);
}

void OSDWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (handleResizeMouseEvent(this, event)) return;
    QWidget::mouseReleaseEvent(event);
}

// ─── Size helpers ─────────────────────────────────────────────────────────────

void OSDWindow::rescale()
{
    auto [absX, absY] = absPos();
    rescaleAt(absX, absY);
}

void OSDWindow::rescaleAt(int absX, int absY, bool restartHideTimer)
{
    // reloadStyles() / applyPreviewScale() can fire while a resize drag is active.
    if (m_resizing)
    {
        rescaleDuringResize(absX, absY, qRound(OSD_W * activeScale()),
                            qRound(m_resizeBaseHeight * activeScale()));
        return;
    }

    // Re-apply all inner widget sizes that were baked in buildUi() with the
    // scale at construction time.  Must be called whenever osdScale changes.
    m_mainLayout->setContentsMargins(scaled(10), scaled(8), scaled(10), scaled(8));
    m_mainLayout->setSpacing(scaled(4));

    m_bar->setFixedHeight(scaled(10));

    if (m_progressOuterLayout)
    {
        m_progressOuterLayout->setContentsMargins(0, scaled(2), 0, 0);
        m_progressOuterLayout->setSpacing(scaled(6));
    }
    if (m_progressLayout)
    {
        m_progressLayout->setContentsMargins(0, 0, 0, 0);
        m_progressLayout->setSpacing(scaled(2));
    }
    if (m_albumArt) m_albumArt->setFixedSize(scaled(36), scaled(36));
    if (m_progressBar) m_progressBar->setFixedHeight(scaled(8));
    if (m_controlsLayout) m_controlsLayout->setSpacing(scaled(8));
    for (auto* btn : {m_btnPrev, m_btnPlayPause, m_btnNext})
        if (btn) btn->setFixedSize(scaled(24), scaled(20));
    for (auto* btn : {m_btnPosUp, m_btnPosDown, m_btnPosLeft, m_btnPosRight})
        if (btn) btn->setFixedSize(scaled(OSD_POS_BTN_W), scaled(OSD_POS_BTN_H));

    applySizeAt(absX, absY, restartHideTimer);
    layoutPositionButtons();
}

void OSDWindow::rescaleDuringResize(int absX, int absY, int newW, int newH)
{
    const int layoutKey = (newW << 16) | newH;

    if (layoutKey != m_resizeCachedLayoutKey)
    {
        m_resizeCachedLayoutKey = layoutKey;
        m_mainLayout->setContentsMargins(scaled(10), scaled(8), scaled(10), scaled(8));
        m_mainLayout->setSpacing(scaled(4));
        m_bar->setFixedHeight(scaled(10));

        if (m_progressOuterLayout)
        {
            m_progressOuterLayout->setContentsMargins(0, scaled(2), 0, 0);
            m_progressOuterLayout->setSpacing(scaled(6));
        }
        if (m_progressLayout)
        {
            m_progressLayout->setContentsMargins(0, 0, 0, 0);
            m_progressLayout->setSpacing(scaled(2));
        }
        if (m_albumArt) m_albumArt->setFixedSize(scaled(36), scaled(36));
        if (m_progressBar) m_progressBar->setFixedHeight(scaled(8));
        if (m_controlsLayout) m_controlsLayout->setSpacing(scaled(8));
        for (auto* btn : {m_btnPrev, m_btnPlayPause, m_btnNext})
            if (btn) btn->setFixedSize(scaled(24), scaled(20));
    }

    const bool sizeChanged = size() != QSize(newW, newH);
    if (sizeChanged) setFixedSize(newW, newH);
    const bool posChanged = m_resizeLastAppliedPos != QPoint(absX, absY);
    if (posChanged || sizeChanged) positionWindowDuringResize(absX, absY, newW, newH);
    applyResizeFontsFast(activeScale());
    layoutPositionButtons();
}

void OSDWindow::applySize()
{
    auto [absX, absY] = absPos();
    applySizeAt(absX, absY);
}

void OSDWindow::applySizeAt(int absX, int absY, bool restartHideTimer)
{
    const QSize oldSize = size();
    const int h = currentBaseHeight();
    setFixedSize(scaled(OSD_W), scaled(h));
    const bool sizeChanged = oldSize != size();

#ifdef HAVE_LAYER_SHELL_QT
    if (m_layerShellActive && m_lsWindow && isVisible())
    {
        positionWindow(absX, absY);
        if (sizeChanged && restartHideTimer) restartHideTimerAfterResize();
        return;
    }
#endif
    if (isVisible())
    {
        auto [x, y] = clampedPos(absX, absY);
        m_currentAbsPos = QPoint(x, y);
        move(x, y);
        setGeometry(x, y, scaled(OSD_W), scaled(h));
        if (QWindow* wh = windowHandle()) wh->setPosition(x, y);
        if (sizeChanged && restartHideTimer) restartHideTimerAfterResize();
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

// Clamp absX/absY so the OSD window (width() × height()) stays fully within
// the available geometry of the screen that contains the requested position.
std::pair<int, int> OSDWindow::clampedPosOnScreen(int absX, int absY, QScreen* screen) const
{
    if (!screen) return {absX, absY};

    const int w = width();
    const int h = height();
    const QRect avail = screen->availableGeometry();
    const int maxX = std::max(avail.left(), avail.right() - w + 1);
    const int maxY = std::max(avail.top(), avail.bottom() - h + 1);
    const int x = std::clamp(absX, avail.left(), maxX);
    const int y = std::clamp(absY, avail.top(), maxY);
    return {x, y};
}

std::pair<int, int> OSDWindow::clampedPos(int absX, int absY) const
{
    const int w = width();
    const int h = height();
    const QPoint center(absX + w / 2, absY + h / 2);
    QScreen* screen = QApplication::screenAt(center);
    if (!screen && !QApplication::screens().isEmpty()) screen = QApplication::screens().first();
    return clampedPosOnScreen(absX, absY, screen);
}

QPoint OSDWindow::clampMoveLocal(const QPoint& localPos) const
{
    const int w = qMax(1, width());
    const int h = qMax(1, height());
    return {std::clamp(localPos.x(), 0, w - 1), std::clamp(localPos.y(), 0, h - 1)};
}

QPoint OSDWindow::layerShellAbsolutePos() const
{
#ifdef HAVE_LAYER_SHELL_QT
    if (m_layerShellActive && m_lsWindow && m_lsScreen)
    {
        const QMargins margins = m_lsWindow->margins();
        const QRect geo = m_lsScreen->geometry();
        return geo.topLeft() + QPoint(margins.left(), margins.top());
    }
#endif
    return m_currentAbsPos;
}

void OSDWindow::positionWindow(int absX, int absY)
{
    auto [x, y] = clampedPos(absX, absY);
    m_currentAbsPos = QPoint(x, y);

#ifdef HAVE_LAYER_SHELL_QT
    if (m_layerShellActive && m_lsWindow)
    {
        QScreen* screen = QApplication::screenAt(QPoint(x, y));
        if (!screen && !QApplication::screens().isEmpty()) screen = QApplication::screens().first();

        if (screen && screen != m_lsScreen)
        {
            m_lsWindow->setScreen(screen);
            m_lsScreen = screen;
        }

        int relX = x;
        int relY = y;
        if (screen)
        {
            const QRect geo = screen->geometry();
            relX = x - geo.x();
            relY = y - geo.y();
        }
        m_lsWindow->setMargins(QMargins(relX, relY, 0, 0));

        if (!m_resizing)
        {
            show();
            raise();
        }
        return;
    }
#endif
    move(x, y);
    setGeometry(x, y, width(), height());
    if (!m_resizing)
    {
        show();
        raise();
    }
    if (QWindow* wh = windowHandle()) wh->setPosition(x, y);
}

void OSDWindow::positionWindowDuringMove(int absX, int absY)
{
    m_currentAbsPos = QPoint(absX, absY);
    const int w = width();
    const int h = height();

#ifdef HAVE_LAYER_SHELL_QT
    if (m_layerShellActive && m_lsWindow)
    {
        QScreen* screen = m_moving && m_moveDragScreen ? m_moveDragScreen : nullptr;
        if (!screen)
        {
            const QPoint center(absX + w / 2, absY + h / 2);
            screen = QApplication::screenAt(center);
        }
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
        return;
    }
#endif
    setGeometry(absX, absY, w, h);
    if (QWindow* wh = windowHandle()) wh->setPosition(absX, absY);
}

void OSDWindow::positionWindowDuringResize(int absX, int absY, int w, int h)
{
    m_resizeLastAppliedPos = QPoint(absX, absY);
    m_currentAbsPos = QPoint(absX, absY);

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

        if (QWindow* wh = windowHandle()) wh->resize(w, h);

        int relX = absX;
        int relY = absY;
        if (screen)
        {
            const QRect geo = screen->geometry();
            relX = absX - geo.x();
            relY = absY - geo.y();
        }
        m_lsWindow->setMargins(QMargins(relX, relY, 0, 0));
        return;
    }
#endif
    setGeometry(absX, absY, w, h);
    if (QWindow* wh = windowHandle()) wh->setPosition(absX, absY);
}

// ─── Manual mouse resize ──────────────────────────────────────────────────────

void OSDWindow::installResizeEventFilters()
{
    for (QWidget* child : findChildren<QWidget*>())
    {
        child->setMouseTracking(true);
        child->installEventFilter(this);
    }
}

bool OSDWindow::handleResizeEvent(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease ||
        event->type() == QEvent::MouseMove)
    {
        return handleResizeMouseEvent(obj, static_cast<QMouseEvent*>(event));
    }
    if (m_resizing && (event->type() == QEvent::Hide || event->type() == QEvent::UngrabMouse))
    {
        finishResize(false);
    }
    if (m_moving && (event->type() == QEvent::Hide || event->type() == QEvent::UngrabMouse))
    {
        finishMove(false);
    }
    return false;
}

bool OSDWindow::handleResizeMouseEvent(QObject* obj, QMouseEvent* event)
{
    if (!isVisible()) return false;

    if (handleMoveMouseEvent(obj, event)) return true;

    if (m_resizing)
    {
        if (event->type() == QEvent::MouseMove)
        {
            updateResize(event->globalPosition().toPoint());
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease && event->button() == Qt::LeftButton)
        {
            finishResize(true);
            return true;
        }
        return true;
    }

    const int edges = resizeEdgesAt(resizeLocalPos(obj, event));
    if (event->type() == QEvent::MouseMove)
    {
        if (m_positionDragEnabled && m_positionControlsEnabled && !m_previewMode &&
            edges == EdgeNone && isMoveDragZone(resizeLocalPos(obj, event)) &&
            !isMoveDragBlocked(obj))
            updateResizeCursor(obj, EdgeNone); // open hand set below
        else
            updateResizeCursor(obj, edges);
        if (m_positionDragEnabled && m_positionControlsEnabled && !m_previewMode &&
            edges == EdgeNone && isMoveDragZone(resizeLocalPos(obj, event)) &&
            !isMoveDragBlocked(obj))
        {
            auto* widget = qobject_cast<QWidget*>(obj);
            if (widget) widget->setCursor(Qt::OpenHandCursor);
        }
        return false;
    }

    if (event->type() == QEvent::MouseButtonPress && event->button() == Qt::LeftButton &&
        edges != EdgeNone)
    {
        startResize(edges, event->globalPosition().toPoint());
        return true;
    }
    return false;
}

QPoint OSDWindow::resizeLocalPos(QObject* obj, QMouseEvent* event) const
{
    auto* widget = qobject_cast<QWidget*>(obj);
    if (!widget || widget == this) return event->position().toPoint();
    return widget->mapTo(const_cast<OSDWindow*>(this), event->position().toPoint());
}

int OSDWindow::resizeEdgesAt(const QPoint& pos) const
{
    if (pos.x() < 0 || pos.y() < 0 || pos.x() >= width() || pos.y() >= height()) return EdgeNone;

    const int margin = resizeHitMargin();
    int edges = EdgeNone;
    if (pos.x() <= margin) edges |= EdgeLeft;
    if (pos.x() >= width() - margin) edges |= EdgeRight;
    if (pos.y() <= margin) edges |= EdgeTop;
    if (pos.y() >= height() - margin) edges |= EdgeBottom;
    return edges;
}

int OSDWindow::resizeHitMargin() const
{
    return std::clamp(scaled(8), 6, 18);
}

Qt::CursorShape OSDWindow::resizeCursorForEdges(int edges) const
{
    const bool left = edges & EdgeLeft;
    const bool right = edges & EdgeRight;
    const bool top = edges & EdgeTop;
    const bool bottom = edges & EdgeBottom;

    if ((left && top) || (right && bottom)) return Qt::SizeFDiagCursor;
    if ((right && top) || (left && bottom)) return Qt::SizeBDiagCursor;
    if (left || right) return Qt::SizeHorCursor;
    if (top || bottom) return Qt::SizeVerCursor;
    return Qt::ArrowCursor;
}

void OSDWindow::updateResizeCursor(QObject* obj, int edges)
{
    auto* widget = qobject_cast<QWidget*>(obj);
    if (!widget) return;
    if (edges != EdgeNone)
    {
        widget->setCursor(resizeCursorForEdges(edges));
        return;
    }
    if (widget == m_btnPrev || widget == m_btnPlayPause || widget == m_btnNext)
        widget->setCursor(Qt::PointingHandCursor);
    else if (widget == m_btnPosUp || widget == m_btnPosDown || widget == m_btnPosLeft ||
             widget == m_btnPosRight)
        widget->setCursor(Qt::PointingHandCursor);
    else
        widget->unsetCursor();
}

void OSDWindow::startResize(int edges, const QPoint& globalPos)
{
    if (edges == EdgeNone) return;
    finishSeeking();
    m_resizeCachedLayoutKey = -1;
    m_resizeLastAppliedPos = QPoint(-1, -1);
    m_resizing = true;
    m_resizeEdges = edges;
    m_resizeStartGlobal = globalPos;
    m_resizeStartGeometry = QRect(m_currentAbsPos, size());
    m_resizeCurrentAbsPos = m_resizeStartGeometry.topLeft();
    m_resizeStartScale = activeScale();
    m_resizeBaseHeight = currentBaseHeight();
    m_hideTimer->stop();
    enterResizeStyleMode();
    setCursor(resizeCursorForEdges(edges));
    grabMouse();
}

void OSDWindow::updateResize(const QPoint& globalPos)
{
    setUpdatesEnabled(false);
    const double scale = scaleForResize(globalPos);
    const int newW = qRound(OSD_W * scale);
    const int newH = qRound(m_resizeBaseHeight * scale);
    const QPoint anchored = anchoredResizePos(newW, newH);
    const QPoint pos = clampedResizePos(anchored, newW, newH);
    m_previewScale = scale;
    m_resizeCurrentAbsPos = pos;
    rescaleDuringResize(pos.x(), pos.y(), newW, newH);
    setUpdatesEnabled(true);
    update();
}

void OSDWindow::finishResize(bool persist)
{
    if (!m_resizing) return;

    const double finalScale = std::clamp(activeScale(), 0.5, 3.0);
    const QPoint finalPos = m_resizeCurrentAbsPos;
    releaseMouse();
    m_resizing = false;
    m_resizeEdges = EdgeNone;
    m_resizeCachedLayoutKey = -1;
    m_resizeLastAppliedPos = QPoint(-1, -1);
    unsetCursor();

    if (persist)
    {
        persistResize(finalScale, finalPos);
        m_previewScale = -1.0;
        applyStyles();
        rescaleAt(finalPos.x(), finalPos.y());
        restartHideTimerAfterResize();
        return;
    }

    m_previewScale = -1.0;
    applyStyles();
    rescale();
}

double OSDWindow::scaleForResize(const QPoint& globalPos) const
{
    const QPoint delta = globalPos - m_resizeStartGlobal;
    const bool horiz = (m_resizeEdges & (EdgeLeft | EdgeRight)) != 0;
    const bool vert = (m_resizeEdges & (EdgeTop | EdgeBottom)) != 0;

    if (horiz && vert)
    {
        const int anchorX = (m_resizeEdges & EdgeLeft) ? m_resizeStartGeometry.right()
                                                       : m_resizeStartGeometry.left();
        const int anchorY = (m_resizeEdges & EdgeTop) ? m_resizeStartGeometry.bottom()
                                                      : m_resizeStartGeometry.top();
        const QPoint anchor(anchorX, anchorY);
        const double startDist =
            std::hypot(m_resizeStartGlobal.x() - anchor.x(), m_resizeStartGlobal.y() - anchor.y());
        const double currentDist =
            std::hypot(globalPos.x() - anchor.x(), globalPos.y() - anchor.y());
        if (startDist < 1.0) return std::clamp(m_resizeStartScale, 0.5, 3.0);

        // Empirically tuned: sqrt(2)≈1.41 felt too fast; 1.0 felt sluggish on diagonal drags.
        constexpr double kCornerGain = 1.18;
        const double ratio = currentDist / startDist;
        const double boosted = 1.0 + (ratio - 1.0) * kCornerGain;
        return std::clamp(m_resizeStartScale * boosted, 0.5, 3.0);
    }

    double scale = m_resizeStartScale;
    if (m_resizeEdges & EdgeLeft)
        scale = static_cast<double>(m_resizeStartGeometry.width() - delta.x()) / OSD_W;
    else if (m_resizeEdges & EdgeRight)
        scale = static_cast<double>(m_resizeStartGeometry.width() + delta.x()) / OSD_W;
    else if (m_resizeEdges & EdgeTop)
        scale =
            static_cast<double>(m_resizeStartGeometry.height() - delta.y()) / m_resizeBaseHeight;
    else if (m_resizeEdges & EdgeBottom)
        scale =
            static_cast<double>(m_resizeStartGeometry.height() + delta.y()) / m_resizeBaseHeight;

    return std::clamp(scale, 0.5, 3.0);
}

QPoint OSDWindow::anchoredResizePos(int newW, int newH) const
{
    const int startRight = m_resizeStartGeometry.x() + m_resizeStartGeometry.width();
    const int startBottom = m_resizeStartGeometry.y() + m_resizeStartGeometry.height();

    QPoint pos = m_resizeStartGeometry.topLeft();
    if (m_resizeEdges & EdgeLeft) pos.setX(startRight - newW);
    if (m_resizeEdges & EdgeTop) pos.setY(startBottom - newH);
    return pos;
}

QPoint OSDWindow::clampedResizePos(const QPoint& absPos, int newW, int newH) const
{
    QScreen* screen = QApplication::screenAt(absPos);
    if (!screen) screen = QApplication::screenAt(m_resizeStartGeometry.center());
    if (!screen && !QApplication::screens().isEmpty()) screen = QApplication::screens().first();
    if (!screen) return absPos;

    const QRect avail = screen->availableGeometry();
    const int maxX = std::max(avail.left(), avail.left() + avail.width() - newW);
    const int maxY = std::max(avail.top(), avail.top() + avail.height() - newH);
    return {std::clamp(absPos.x(), avail.left(), maxX), std::clamp(absPos.y(), avail.top(), maxY)};
}

void OSDWindow::persistResize(double scale, const QPoint& absPos)
{
    persistPosition(absPos);
    OsdConfig osd = m_config->osd();
    osd.osdScale = std::clamp(scale, 0.5, 3.0);
    m_config->setOsd(osd);
}

void OSDWindow::persistPosition(const QPoint& absPos)
{
    OsdConfig osd = m_config->osd();
    const QList<QScreen*> screens = QApplication::screens();
    QScreen* screen = QApplication::screenAt(absPos);
    int screenIdx = osd.screen;
    if (screen)
    {
        const int idx = screens.indexOf(screen);
        if (idx >= 0) screenIdx = idx;
    }
    if (screenIdx < 0 || screenIdx >= screens.size()) screenIdx = 0;

    osd.screen = screenIdx;
    if (screenIdx >= 0 && screenIdx < screens.size())
    {
        const QRect geo = screens[screenIdx]->geometry();
        osd.x = absPos.x() - geo.x();
        osd.y = absPos.y() - geo.y();
    }
    else
    {
        osd.x = absPos.x();
        osd.y = absPos.y();
    }
    m_config->setOsd(osd);
}

void OSDWindow::restartHideTimerAfterResize()
{
    if (!isVisible()) return;
    if (m_previewMode)
    {
        if (!m_previewHeld) m_hideTimer->start(m_previewTimeoutMs);
        return;
    }
    m_hideTimer->start(m_config->osd().timeoutMs);
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
        if (m_controlsRow) m_controlsRow->setVisible(false);
        if (m_albumArt)
        {
            m_albumArtVisible = false;
            m_albumArt->setVisible(false);
        }
        if (m_mediaActionMode) return;
        refreshAlbumArtVisibility();
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
    if (!on && m_albumArt)
    {
        m_albumArtVisible = false;
        m_albumArt->setVisible(false);
    }
    if (m_mediaActionMode) return;
    m_progressRow->setVisible(on);
    if (m_controlsRow) m_controlsRow->setVisible(on && m_mediaControlsEnabled);
    refreshAlbumArtVisibility();
    refreshNameLabel();
    applySize();
}

void OSDWindow::updateTrack(const QString& title, const QString& artist, qint64 lengthUs,
                            bool canSeek)
{
    updateTrack(title, artist, QString{}, lengthUs, canSeek);
}

void OSDWindow::updateTrack(const QString& title, const QString& artist, const QString& album,
                            qint64 lengthUs, bool canSeek)
{
    m_trackAlbum = album;
    const qint64 oldLengthUs = m_trackLengthUs;
    const qint64 oldPositionUs = m_lastPositionUs;
    const int oldProgressValue = m_progressBar ? m_progressBar->value() : -1;
    const QString oldTitle = m_trackTitle;
    const QString oldArtist = m_trackArtist;
    const bool oldCanSeek = m_canSeek;

    const bool identityChanged = m_trackTitle != title || m_trackArtist != artist;
    const bool transientMissingLength = !identityChanged && m_trackLengthUs > 0 && lengthUs <= 0;
    if (transientMissingLength)
    {
        lengthUs = m_trackLengthUs;
        canSeek = m_canSeek;
    }

    const bool trackChanged =
        identityChanged || m_trackLengthUs != lengthUs || m_canSeek != canSeek;
    if (m_seeking && trackChanged) finishSeeking();

    if (identityChanged) m_lastPositionUs = 0;

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
        const qint64 posUs = identityChanged ? 0 : std::clamp(m_lastPositionUs, 0LL, lengthUs);
        m_lastPositionUs = posUs;
        const int val = static_cast<int>(std::clamp(posUs * 1000LL / lengthUs, 0LL, 1000LL));
        m_progressBar->setValue(val);
        m_labelTime->setText(
            QStringLiteral("%1 / %2").arg(formatTime(posUs), formatTime(lengthUs)));
    }

    if (progressDebugEnabled())
    {
        qDebug() << "[ProgressDebug][OSD] updateTrack oldTitle=" << oldTitle << "newTitle=" << title
                 << "oldArtist=" << oldArtist << "newArtist=" << artist << "oldLen=" << oldLengthUs
                 << "newLen=" << lengthUs << "oldPos=" << oldPositionUs
                 << "newPos=" << m_lastPositionUs << "oldCanSeek=" << oldCanSeek
                 << "newCanSeek=" << canSeek << "identityChanged=" << identityChanged
                 << "transientMissingLength=" << transientMissingLength
                 << "oldBar=" << oldProgressValue << "newBar=" << m_progressBar->value()
                 << "time=" << m_labelTime->text();
    }
}

void OSDWindow::updatePosition(qint64 positionUs)
{
    if (m_trackLengthUs <= 0 || m_seeking) return;

    const qint64 oldPositionUs = m_lastPositionUs;
    const int oldProgressValue = m_progressBar ? m_progressBar->value() : -1;
    m_lastPositionUs = std::clamp(positionUs, 0LL, m_trackLengthUs);
    const int val =
        static_cast<int>(std::clamp(m_lastPositionUs * 1000LL / m_trackLengthUs, 0LL, 1000LL));
    m_progressBar->setValue(val);
    m_labelTime->setText(
        QStringLiteral("%1 / %2").arg(formatTime(m_lastPositionUs), formatTime(m_trackLengthUs)));

    if (progressDebugEnabled() &&
        (m_lastPositionUs + 2000000LL < oldPositionUs || val + 20 < oldProgressValue))
    {
        qDebug() << "[ProgressDebug][OSD] positionBackwards oldPos=" << oldPositionUs
                 << "newPos=" << m_lastPositionUs << "oldBar=" << oldProgressValue
                 << "newBar=" << val << "title=" << m_trackTitle << "artist=" << m_trackArtist
                 << "len=" << m_trackLengthUs;
    }
}

// ─── Seek interaction (event filter on m_progressBar) ─────────────────────────

bool OSDWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (handleResizeEvent(obj, event)) return true;
    if (obj != m_progressBar) return QWidget::eventFilter(obj, event);

    auto posFromMouseX = [this](int mouseX) -> qint64
    {
        const int w = m_progressBar->width();
        if (w <= 0) return 0;
        const double ratio = std::clamp(static_cast<double>(mouseX) / w, 0.0, 1.0);
        return static_cast<qint64>(ratio * m_trackLengthUs);
    };

    // Update the OSD display only — no D-Bus call. Used during press and drag
    // to show the seek preview without spamming SetPosition to the player.
    auto previewSeekPosition = [this, &posFromMouseX](int mouseX)
    {
        const qint64 posUs = posFromMouseX(mouseX);
        const int val = static_cast<int>(posUs * 1000LL / m_trackLengthUs);
        m_progressBar->setValue(val);
        m_labelTime->setText(
            QStringLiteral("%1 / %2").arg(formatTime(posUs), formatTime(m_trackLengthUs)));
    };

    if (m_seeking)
    {
        if (event->type() == QEvent::MouseButtonRelease)
        {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton)
            {
                // Commit the seek exactly once on release. Using the release
                // coordinate (not the press coordinate) so drag-to-seek always
                // commits at the position the user chose.
                if (m_canSeek && m_trackLengthUs > 0 && m_config->osd().progressInteractive)
                {
                    const qint64 posUs = posFromMouseX(me->position().toPoint().x());
                    previewSeekPosition(me->position().toPoint().x());
                    emit seekRequested(posUs);
                }
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
            // Show visual preview immediately on press — no D-Bus call yet.
            previewSeekPosition(me->position().toPoint().x());
            return true;
        }
    }
    else if (event->type() == QEvent::MouseMove && m_seeking)
    {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->buttons() & Qt::LeftButton)
        {
            // Update preview during drag — still no D-Bus call until release.
            previewSeekPosition(me->position().toPoint().x());
            return true;
        }
        finishSeeking();
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void OSDWindow::refreshNameLabel()
{
    if (m_mediaActionMode) return;

    const OsdConfig osd = m_config->osd();
    const QString& mode = osd.progressLabelMode;

    LabelTokens tokens;
    tokens.app = m_currentAppName;
    tokens.player = m_playerName;
    tokens.title = m_trackTitle;
    tokens.artist = m_trackArtist;
    tokens.album = m_trackAlbum;

    // Default to the simple "app" template; presets below may override.
    QString topTpl = QStringLiteral("{app}");
    QString bottomTpl;

    if (!m_progressEnabled || !m_progressVisible || mode == QLatin1String("app"))
    {
        // already set above
    }
    else if (mode == QLatin1String("title_artist"))
    {
        topTpl = QStringLiteral("{title} \u2014 {artist}");
    }
    else if (mode == QLatin1String("artist_title"))
    {
        topTpl = QStringLiteral("{artist} \u2014 {title}");
    }
    else if (mode == QLatin1String("app_track"))
    {
        bottomTpl = QStringLiteral("{title} \u2014 {artist}");
    }
    else if (mode == QLatin1String("player_track") || mode == QLatin1String("player_track_art"))
    {
        topTpl = QStringLiteral("{player}");
        bottomTpl = QStringLiteral("{title} \u2014 {artist}");
    }
    else if (mode == QLatin1String("custom"))
    {
        topTpl = osd.customLabelTop;
        bottomTpl = osd.customLabelBottom;
    }
    // Unknown mode falls through with the default "{app}" template.

    const QString top = formatOsdLabelTemplate(topTpl, tokens);
    const QString bottom = formatOsdLabelTemplate(bottomTpl, tokens);

    m_labelName->setText(top);
    if (m_labelTrack)
    {
        m_labelTrack->setText(bottom);
        // Hide an empty bottom line so spacing collapses cleanly.
        m_labelTrack->setVisible(m_progressEnabled && m_progressVisible && !bottom.isEmpty());
    }
}

void OSDWindow::refreshAlbumArtVisibility()
{
    if (!m_albumArt) return;
    if (m_mediaActionMode) return;
    const OsdConfig osd = m_config->osd();
    const bool wantArt =
        m_progressEnabled && m_progressVisible &&
        (osd.progressLabelMode == QLatin1String("player_track_art") ||
         (osd.progressLabelMode == QLatin1String("custom") && osd.customLabelShowArt));
    if (m_albumArtVisible == wantArt) return;
    m_albumArtVisible = wantArt;
    m_albumArt->setVisible(wantArt);
}

int OSDWindow::scaled(int base) const
{
    return qRound(base * activeScale());
}

double OSDWindow::activeScale() const
{
    return m_previewScale > 0.0 ? m_previewScale : m_config->osd().osdScale;
}

int OSDWindow::currentBaseHeight() const
{
    int h = OSD_H_BASE;
    if (m_progressEnabled && m_progressVisible)
        h = (m_mediaControlsEnabled && m_controlsRow) ? OSD_H_CONTROLS : OSD_H_PROGRESS;
    return h;
}

bool OSDWindow::positionArrowsVisible() const
{
    return m_positionControlsEnabled && m_positionArrowsEnabled && !m_previewMode &&
           m_btnPosUp != nullptr;
}

void OSDWindow::layoutPositionButtons()
{
    if (!positionArrowsVisible()) return;

    const int btnW = scaled(OSD_POS_BTN_W);
    const int btnH = scaled(OSD_POS_BTN_H);
    const int inset = scaled(OSD_POS_INSET);

    // Corner placement: each arrow sits in the corner toward its snap direction.
    m_btnPosUp->setGeometry(inset, inset, btnW, btnH);
    m_btnPosRight->setGeometry(width() - inset - btnW, inset, btnW, btnH);
    m_btnPosLeft->setGeometry(inset, height() - inset - btnH, btnW, btnH);
    m_btnPosDown->setGeometry(width() - inset - btnW, height() - inset - btnH, btnW, btnH);

    for (auto* btn : {m_btnPosUp, m_btnPosDown, m_btnPosLeft, m_btnPosRight})
        if (btn) btn->raise();
}

void OSDWindow::updatePositionButtonsVisibility()
{
    const bool show = positionArrowsVisible();
    for (auto* btn : {m_btnPosUp, m_btnPosDown, m_btnPosLeft, m_btnPosRight})
        if (btn) btn->setVisible(show);
    if (show) layoutPositionButtons();
}

void OSDWindow::applyPreviewScale(double scale)
{
    m_previewScale = std::clamp(scale, 0.5, 3.0);
    applyStyles();
    rescale();
}

void OSDWindow::finishSeeking()
{
    if (!m_seeking) return;
    m_seeking = false;
    emit seekFinished();
}

void OSDWindow::updatePlaybackStatus(const QString& status)
{
    m_playbackStatus = status;
    if (m_btnPlayPause)
    {
        m_btnPlayPause->setText(status == QLatin1String("Playing") ? QStringLiteral("\u23F8")
                                                                   : QStringLiteral("\u23F5"));
    }
}

void OSDWindow::setMediaControlsEnabled(bool on)
{
    m_mediaControlsEnabled = on;
    if (m_mediaActionMode) return;
    if (m_controlsRow) m_controlsRow->setVisible(on && m_progressVisible);
    applySize();
}

void OSDWindow::setPlayerName(const QString& playerName)
{
    if (m_playerName == playerName) return;
    m_playerName = playerName;
    refreshNameLabel();
}

void OSDWindow::setAlbumArt(const QPixmap& pixmap)
{
    if (!m_albumArt) return;
    if (pixmap.isNull())
    {
        m_albumArt->clear();
    }
    else
    {
        // setScaledContents handles fitting to fixed size; storing the original
        // avoids progressively blurring across rescales.
        m_albumArt->setPixmap(pixmap);
    }
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

// ─── Position controls ────────────────────────────────────────────────────────

void OSDWindow::setPositionControlsEnabled(bool on)
{
    const OsdConfig osd = m_config->osd();
    m_positionControlsEnabled = on;
    m_positionArrowsEnabled = osd.positionArrowsEnabled;
    m_positionDragEnabled = osd.positionDragEnabled;
    updatePositionButtonsVisibility();
    if (!on && m_moving) finishMove(false);
}

void OSDWindow::updateLayoutKeysActive()
{
    const OsdConfig osd = m_config->osd();
    const bool active =
        isVisible() && !m_previewMode && osd.positionControlsEnabled && osd.positionKeyboardEnabled;
    if (active == m_layoutKeysActiveEmitted) return;
    m_layoutKeysActiveEmitted = active;
    emit layoutKeysActiveChanged(active);
}

void OSDWindow::snapToScreenEdge(ScreenEdge edge)
{
    if (!m_positionControlsEnabled || m_previewMode) return;

    QScreen* screen = QApplication::screenAt(m_currentAbsPos);
    if (!screen && !QApplication::screens().isEmpty()) screen = QApplication::screens().first();
    if (!screen) return;

    const QRect avail = screen->availableGeometry();
    int absX = m_currentAbsPos.x();
    int absY = m_currentAbsPos.y();
    const int w = width();
    const int h = height();

    switch (edge)
    {
    case ScreenTop:
        absY = avail.top();
        break;
    case ScreenBottom:
        absY = avail.top() + std::max(0, avail.height() - h);
        break;
    case ScreenLeft:
        absX = avail.left();
        break;
    case ScreenRight:
        absX = avail.left() + std::max(0, avail.width() - w);
        break;
    }

    auto [x, y] = clampedPos(absX, absY);
    positionWindow(x, y);
    persistPosition(QPoint(x, y));
    restartHideTimerAfterResize();
}

void OSDWindow::snapUp()
{
    snapToScreenEdge(ScreenTop);
}
void OSDWindow::snapDown()
{
    snapToScreenEdge(ScreenBottom);
}
void OSDWindow::snapLeft()
{
    snapToScreenEdge(ScreenLeft);
}
void OSDWindow::snapRight()
{
    snapToScreenEdge(ScreenRight);
}

void OSDWindow::stepScale(double delta)
{
    if (!m_positionControlsEnabled || m_previewMode) return;

    const double current = activeScale();
    const double next = std::clamp(current + delta, 0.5, 3.0);
    if (qFuzzyCompare(next, current)) return;

    const QPoint center = m_currentAbsPos + QPoint(width() / 2, height() / 2);
    m_previewScale = -1.0;

    const int newW = qRound(OSD_W * next);
    const int newH = qRound(currentBaseHeight() * next);
    const QPoint newTopLeft(center.x() - newW / 2, center.y() - newH / 2);
    const QPoint pos = clampedResizePos(newTopLeft, newW, newH);

    persistResize(next, pos);
    applyStyles();
    rescaleAt(pos.x(), pos.y());
    restartHideTimerAfterResize();
}

void OSDWindow::stepScaleUp()
{
    stepScale(0.1);
}
void OSDWindow::stepScaleDown()
{
    stepScale(-0.1);
}

bool OSDWindow::isMoveDragBlocked(QObject* obj) const
{
    if (!obj) return true;
    if (obj == m_progressBar && m_config->osd().progressInteractive && m_canSeek &&
        m_trackLengthUs > 0)
        return true;
    if (obj == m_btnPrev || obj == m_btnPlayPause || obj == m_btnNext) return true;
    if (obj == m_btnPosUp || obj == m_btnPosDown || obj == m_btnPosLeft || obj == m_btnPosRight)
        return true;
    return false;
}

bool OSDWindow::isMoveDragZone(const QPoint& localPos) const
{
    if (!m_positionControlsEnabled || !m_positionDragEnabled || m_previewMode) return false;
    if (localPos.x() < 0 || localPos.y() < 0 || localPos.x() >= width() || localPos.y() >= height())
        return false;
    if (resizeEdgesAt(localPos) != EdgeNone) return false;

    if (positionArrowsVisible())
    {
        const int inset = scaled(OSD_POS_INSET);
        const int btnW = scaled(OSD_POS_BTN_W);
        const int btnH = scaled(OSD_POS_BTN_H);
        const int pad = scaled(2);
        const QRect topLeft(inset - pad, inset - pad, btnW + 2 * pad, btnH + 2 * pad);
        const QRect topRight(width() - inset - btnW - pad, inset - pad, btnW + 2 * pad,
                             btnH + 2 * pad);
        const QRect bottomLeft(inset - pad, height() - inset - btnH - pad, btnW + 2 * pad,
                               btnH + 2 * pad);
        const QRect bottomRight(width() - inset - btnW - pad, height() - inset - btnH - pad,
                                btnW + 2 * pad, btnH + 2 * pad);
        if (topLeft.contains(localPos) || topRight.contains(localPos) ||
            bottomLeft.contains(localPos) || bottomRight.contains(localPos))
            return false;
    }
    return true;
}

bool OSDWindow::handleMoveMouseEvent(QObject* obj, QMouseEvent* event)
{
    if (!m_positionControlsEnabled || !m_positionDragEnabled || m_previewMode) return false;

    if (m_moving)
    {
        if (event->type() == QEvent::MouseMove)
        {
            updateMove(event->globalPosition().toPoint());
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease && event->button() == Qt::LeftButton)
        {
            finishMove(true);
            return true;
        }
        return true;
    }

    if (event->type() == QEvent::MouseButtonPress && event->button() == Qt::LeftButton)
    {
        const QPoint local = resizeLocalPos(obj, event);
        if (resizeEdgesAt(local) != EdgeNone) return false;
        if (!isMoveDragZone(local) || isMoveDragBlocked(obj)) return false;
        startMove(event->globalPosition().toPoint(), local);
        return true;
    }
    return false;
}

void OSDWindow::startMove(const QPoint& globalPos, const QPoint& localPos)
{
    Q_UNUSED(localPos);
    finishSeeking();
    m_moving = true;
    m_moveLastAppliedPos = QPoint(-1, -1);
    m_hideTimer->stop();
    setCursor(Qt::ClosedHandCursor);
    grabMouse();

    m_moveStartGlobal = globalPos;
#ifdef HAVE_LAYER_SHELL_QT
    m_moveStartAbsPos =
        (m_layerShellActive && m_lsWindow) ? layerShellAbsolutePos() : m_currentAbsPos;
#else
    m_moveStartAbsPos = m_currentAbsPos;
#endif

    m_moveDragScreen =
        QApplication::screenAt(m_moveStartAbsPos + QPoint(width() / 2, height() / 2));
#ifdef HAVE_LAYER_SHELL_QT
    if (!m_moveDragScreen && m_lsScreen) m_moveDragScreen = m_lsScreen;
#endif
}

void OSDWindow::updateMove(const QPoint& eventGlobalPos)
{
    const QPoint delta = eventGlobalPos - m_moveStartGlobal;
    const QPoint target = m_moveStartAbsPos + delta;
    auto [x, y] = m_moveDragScreen ? clampedPosOnScreen(target.x(), target.y(), m_moveDragScreen)
                                   : clampedPos(target.x(), target.y());
    if (m_moveLastAppliedPos == QPoint(x, y)) return;

    setUpdatesEnabled(false);
    m_moveLastAppliedPos = QPoint(x, y);
    m_currentAbsPos = QPoint(x, y);
    positionWindowDuringMove(x, y);
    setUpdatesEnabled(true);
}

void OSDWindow::finishMove(bool persist)
{
    if (!m_moving) return;

    releaseMouse();
    m_moving = false;
    m_moveDragScreen = nullptr;
    m_moveLastAppliedPos = QPoint(-1, -1);
    unsetCursor();
    if (persist) persistPosition(m_currentAbsPos);
    restartHideTimerAfterResize();
}

// ─── Public API ───────────────────────────────────────────────────────────────

void OSDWindow::showVolume(const QString& appName, double volume, bool muted)
{
    m_mediaActionMode = false;
    m_previewMode = false;
    m_previewHeld = false;
    m_bar->show();
    m_labelPct->show();
    if (m_progressEnabled && m_progressVisible && m_progressRow) m_progressRow->show();
    if (m_controlsRow) m_controlsRow->setVisible(m_progressVisible && m_mediaControlsEnabled);
    m_currentAppName = appName;
    OsdConfig osd = m_config->osd();
    int pct = qRound(volume * 100);

    refreshAlbumArtVisibility();
    refreshNameLabel();
    m_bar->setValue(pct);
    m_labelPct->setText(muted ? QStringLiteral("%1%  \U0001F507").arg(pct)
                              : QStringLiteral("%1%").arg(pct));

    applySize();
    auto [absX, absY] = absPos();
    positionWindow(absX, absY);
    m_hideTimer->start(osd.timeoutMs);
    updateLayoutKeysActive();
    updatePositionButtonsVisibility();
}

void OSDWindow::showMediaAction(const QString& actionLabel)
{
    m_mediaActionMode = true;
    m_previewMode = false;
    m_previewHeld = false;
    m_bar->hide();
    m_labelPct->hide();
    if (m_progressRow) m_progressRow->hide();
    m_labelName->setText(actionLabel);

    setFixedSize(scaled(OSD_W), scaled(OSD_H_BASE));
    auto [absX, absY] = absPos();
    positionWindow(absX, absY);
    OsdConfig osd = m_config->osd();
    m_hideTimer->start(osd.timeoutMs);
    updateLayoutKeysActive();
    updatePositionButtonsVisibility();
}

void OSDWindow::showPreview(int screenIdx, int x, int y, int timeoutMs)
{
    m_mediaActionMode = false;
    m_previewMode = true;
    m_previewHeld = false;
    m_previewTimeoutMs = timeoutMs;
    m_hideTimer->stop();
    m_bar->show();
    m_labelPct->show();
    if (m_progressEnabled && m_progressVisible && m_progressRow) m_progressRow->show();
    if (m_controlsRow) m_controlsRow->setVisible(m_progressVisible && m_mediaControlsEnabled);
    refreshAlbumArtVisibility();

    m_labelName->setText(::tr(QStringLiteral("osd.preview")));
    m_bar->setValue(60);
    m_labelPct->setText(QStringLiteral("60%"));

    applySize();
    QList<QScreen*> screens = QApplication::screens();
    if (screens.isEmpty()) return;
    if (screenIdx >= screens.size()) screenIdx = 0;
    QRect geo = screens[screenIdx]->geometry();
    positionWindow(geo.x() + x, geo.y() + y);
    m_hideTimer->start(timeoutMs);
    updateLayoutKeysActive();
    updatePositionButtonsVisibility();
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
    m_previewScale = -1.0; // clear any live preview override before applying saved config
    applyStyles();         // font sizes scale via scaled() internally
    rescale();             // re-apply inner widget sizes (bar heights, button sizes, margins)
    const OsdConfig osd = m_config->osd();
    m_mediaControlsEnabled = osd.mediaControlsEnabled;
    setMediaControlsEnabled(m_mediaControlsEnabled);
    setProgressEnabled(osd.progressEnabled);
    setPositionControlsEnabled(osd.positionControlsEnabled);
    refreshAlbumArtVisibility();
    refreshNameLabel();
    if (!osd.progressInteractive) finishSeeking();
    auto [absX, absY] = absPos();
#ifdef HAVE_LAYER_SHELL_QT
    if (m_layerShellActive && m_lsWindow)
    {
        if (isVisible()) positionWindow(absX, absY);
        update();
        return;
    }
#endif
    auto [x, y] = clampedPos(absX, absY);
    m_currentAbsPos = QPoint(x, y);
    move(x, y);
    setGeometry(x, y, width(), height());
    if (isVisible())
    {
        if (QWindow* wh = windowHandle()) wh->setPosition(x, y);
    }
    update();
}
