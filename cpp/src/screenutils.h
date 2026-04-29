#pragma once
#include <QWidget>
#include <QApplication>
#include <QScreen>

inline void centerDialogOnScreenAt(QWidget *window, const QPoint &globalPos)
{
    QScreen *screen = QApplication::screenAt(globalPos);
    if (!screen)
        screen = QApplication::primaryScreen();
    if (!screen)
        return;

    window->ensurePolished();
    window->adjustSize();

    QSize best = window->sizeHint();
    QSize minHint = window->minimumSizeHint();
    QSize minSize = window->minimumSize();
    best = best.expandedTo(minHint).expandedTo(minSize);
    if (best != window->size())
        window->resize(best);

    QRect sg = screen->availableGeometry();
    QSize ws = window->frameGeometry().size();
    int x = sg.x() + (sg.width()  - ws.width())  / 2;
    int y = sg.y() + (sg.height() - ws.height()) / 2;

    if (x < sg.left())  x = sg.left();
    if (y < sg.top())   y = sg.top();
    if (x + ws.width()  > sg.right())  x = sg.right()  - ws.width();
    if (y + ws.height() > sg.bottom()) y = sg.bottom() - ws.height();

    window->move(x, y);
}
