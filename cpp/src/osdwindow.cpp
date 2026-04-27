#include "osdwindow.h"
#include "config.h"
#include "i18n.h"

#include <QApplication>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QPainter>
#include <QScreen>
#include <QWindow>
#include <QPaintEvent>

OSDWindow::OSDWindow(Config *config, QWidget *parent)
    : QWidget(parent), m_config(config)
{
    buildUi();
    applyStyles();
}

void OSDWindow::buildUi()
{
    setWindowFlags(
        Qt::FramelessWindowHint
        | Qt::WindowStaysOnTopHint
        | Qt::Tool
        | Qt::BypassWindowManagerHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(220, 70);

    QVBoxLayout *layout = new QVBoxLayout(this);
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

void OSDWindow::applyColorStyles(const QString &colorBg, const QString &colorText,
                                  const QString &colorBar, int opacity)
{
    m_bgColor = QColor(colorBg);
    m_bgColor.setAlpha(qRound(opacity / 100.0 * 255));

    QString text = colorText;
    if (text.startsWith(QLatin1Char('#'))) text.remove(0, 1);
    QString bar = colorBar;
    if (bar.startsWith(QLatin1Char('#'))) bar.remove(0, 1);

    // Background is NOT set in stylesheet — it is drawn in paintEvent.
    setStyleSheet(QStringLiteral(
        "QLabel { color: #%1; background: transparent; }"
        "QLabel#name_label { font-size: 11pt; font-weight: bold; font-family: 'Noto Sans', 'Segoe UI', sans-serif; }"
        "QLabel#pct_label  { font-size: 9pt;  font-family: 'Noto Sans', 'Segoe UI', sans-serif; }"
        "QProgressBar { background-color: #333333; border: none; border-radius: 3px; }"
        "QProgressBar::chunk { background-color: #%2; border-radius: 3px; }"
    ).arg(text, bar));

    m_labelName->setStyleSheet(
        QStringLiteral("font-size: 11pt; font-weight: bold; color: #%1; background: transparent;")
        .arg(text));
    m_labelPct->setStyleSheet(
        QStringLiteral("font-size: 9pt; color: #%1; background: transparent;")
        .arg(text));
    update();
}

void OSDWindow::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_bgColor);
    painter.drawRoundedRect(rect(), 8, 8);
    QWidget::paintEvent(event);
}

// ── Position helpers ──────────────────────────────────────────────────────────
std::pair<int, int> OSDWindow::absPos() const
{
    OsdConfig osd = m_config->osd();
    QList<QScreen *> screens = QApplication::screens();
    if (screens.isEmpty()) return { osd.x, osd.y };
    int idx = osd.screen;
    if (idx >= screens.size()) idx = 0;
    QRect geo = screens[idx]->geometry();
    return { geo.x() + osd.x, geo.y() + osd.y };
}

void OSDWindow::positionWindow(int absX, int absY)
{
    move(absX, absY);
    setGeometry(absX, absY, width(), height());
    show();
    raise();
    // Re-apply via native QWindow after show() — needed on Wayland (XWayland).
    if (QWindow *wh = windowHandle())
        wh->setPosition(absX, absY);
}

// ── Public API ────────────────────────────────────────────────────────────────
void OSDWindow::showVolume(const QString &appName, double volume, bool muted)
{
    m_previewMode = false;
    OsdConfig osd = m_config->osd();
    int pct = qRound(volume * 100);

    m_labelName->setText(appName);
    m_bar->setValue(pct);
    m_labelPct->setText(muted
        ? QStringLiteral("%1%  \U0001F507").arg(pct)
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

    QList<QScreen *> screens = QApplication::screens();
    if (screens.isEmpty()) return;
    if (screenIdx >= screens.size()) screenIdx = 0;
    QRect geo = screens[screenIdx]->geometry();
    positionWindow(geo.x() + x, geo.y() + y);
    m_hideTimer->start(timeoutMs);
}

void OSDWindow::hidePreview()
{
    if (m_previewMode) {
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
    if (isVisible() && m_previewMode)
        m_hideTimer->start(timeoutMs);
}

void OSDWindow::applyPreviewColors(const QString &colorBg, const QString &colorText,
                                    const QString &colorBar, int opacity)
{
    applyColorStyles(colorBg, colorText, colorBar, opacity);
}

void OSDWindow::reloadStyles()
{
    applyStyles();
    auto [absX, absY] = absPos();
    move(absX, absY);
    setGeometry(absX, absY, width(), height());
    if (isVisible()) {
        if (QWindow *wh = windowHandle())
            wh->setPosition(absX, absY);
    }
    update();
}
