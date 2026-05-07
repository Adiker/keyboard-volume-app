#include "osdwindow.h"
#include "config.h"
#include "i18n.h"
#include "waylandstate.h"

#ifdef HAVE_LAYER_SHELL_QT
#include <LayerShellQt/Window>
#endif

#include <QApplication>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QPainter>
#include <QScreen>
#include <QWindow>
#include <QPaintEvent>

OSDWindow::OSDWindow(Config* config, QWidget* parent) : QWidget(parent), m_config(config)
{
    buildUi();
    applyStyles();
}

void OSDWindow::buildUi()
{
    Qt::WindowFlags flags = Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool;
    if (!g_nativeWayland)
    {
        // On X11/XWayland this keeps the OSD above all windows without WM intervention.
        // On native Wayland LayerOverlay handles layering; the flag is ignored or
        // causes issues on some compositors, so we omit it there.
        flags |= Qt::BypassWindowManagerHint;
    }
    setWindowFlags(flags);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(220, 70);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(4);

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

    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    connect(m_hideTimer, &QTimer::timeout, this, &OSDWindow::hide);
}

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

    // Background is NOT set in stylesheet — it is drawn in paintEvent.
    setStyleSheet(
        QStringLiteral(
            "QLabel { color: #%1; background: transparent; }"
            "QLabel#name_label { font-size: 11pt; font-weight: bold; font-family: 'Noto Sans', "
            "'Segoe UI', sans-serif; }"
            "QLabel#pct_label  { font-size: 9pt;  font-family: 'Noto Sans', 'Segoe UI', "
            "sans-serif; }"
            "QProgressBar { background-color: #333333; border: none; border-radius: 3px; }"
            "QProgressBar::chunk { background-color: #%2; border-radius: 3px; }")
            .arg(text, bar));

    m_labelName->setStyleSheet(
        QStringLiteral("font-size: 11pt; font-weight: bold; color: #%1; background: transparent;")
            .arg(text));
    m_labelPct->setStyleSheet(
        QStringLiteral("font-size: 9pt; color: #%1; background: transparent;").arg(text));
    update();
}

void OSDWindow::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_bgColor);
    painter.drawRoundedRect(rect(), 8, 8);
    QWidget::paintEvent(event);
}

// ── Wayland layer-shell initialisation ───────────────────────────────────────
void OSDWindow::initLayerShell()
{
#ifdef HAVE_LAYER_SHELL_QT
    if (!g_nativeWayland) return;

    // Force native QWindow creation without showing the widget.
    // winId() on a hidden top-level QWidget creates the platform window.
    (void)winId();
    QWindow* win = windowHandle();
    if (!win)
    {
        qWarning() << "[OSDWindow] initLayerShell: no windowHandle() after winId()";
        return;
    }

    // Window::get() attaches layer-shell configuration to this specific QWindow.
    // Must be called before the first show() so the compositor receives the
    // layer-shell role before any wl_surface commit.
    m_lsWindow = LayerShellQt::Window::get(win);
    if (!m_lsWindow)
    {
        qWarning() << "[OSDWindow] initLayerShell: LayerShellQt::Window::get() returned null";
        return;
    }

    m_lsWindow->setLayer(LayerShellQt::Window::LayerOverlay);
    // Anchor top-left; margins (left=X, top=Y) position the OSD from that corner.
    m_lsWindow->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop |
                                                         LayerShellQt::Window::AnchorLeft));
    m_lsWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    m_lsWindow->setExclusiveZone(0);      // OSD does not reserve screen estate
    m_lsWindow->setActivateOnShow(false); // never steal keyboard focus
    m_lsWindow->setScope(QStringLiteral("keyboard-volume-app-osd"));

    m_layerShellActive = true;
    qDebug() << "[OSDWindow] layer-shell initialised (native Wayland OSD)";
#endif
}

// ── Position helpers ──────────────────────────────────────────────────────────
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
        // Resolve which screen owns these global coordinates.
        QScreen* screen = QApplication::screenAt(QPoint(absX, absY));
        if (!screen && !QApplication::screens().isEmpty()) screen = QApplication::screens().first();

        // Route the surface to the correct output when the target screen changes.
        if (screen && screen != m_lsScreen)
        {
            m_lsWindow->setScreen(screen);
            m_lsScreen = screen;
        }

        // Convert global coords to screen-relative margins.
        // QMargins(left, top, right, bottom): left=relX pushes from left anchor,
        // top=relY pushes from top anchor.
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
    // X11 / XWayland path (existing behaviour)
    move(absX, absY);
    setGeometry(absX, absY, width(), height());
    show();
    raise();
    // Re-apply via native QWindow after show() — needed on XWayland.
    if (QWindow* wh = windowHandle()) wh->setPosition(absX, absY);
}

// ── Public API ────────────────────────────────────────────────────────────────
void OSDWindow::showVolume(const QString& appName, double volume, bool muted)
{
    m_previewMode = false;
    OsdConfig osd = m_config->osd();
    int pct = qRound(volume * 100);

    m_labelName->setText(appName);
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
        hide();
    }
}

void OSDWindow::showPreviewHeld(int screenIdx, int x, int y)
{
    showPreview(screenIdx, x, y);
    m_hideTimer->stop();
}

void OSDWindow::releasePreview(int timeoutMs)
{
    if (isVisible() && m_previewMode) m_hideTimer->start(timeoutMs);
}

void OSDWindow::applyPreviewColors(const QString& colorBg, const QString& colorText,
                                   const QString& colorBar, int opacity)
{
    applyColorStyles(colorBg, colorText, colorBar, opacity);
}

void OSDWindow::reloadStyles()
{
    applyStyles();
    auto [absX, absY] = absPos();
#ifdef HAVE_LAYER_SHELL_QT
    if (m_layerShellActive && m_lsWindow)
    {
        // Update layer-shell margins when config changes; skip move()/setGeometry().
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
