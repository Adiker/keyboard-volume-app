// Opt-in manual integration check against the current KWin session.
#include "windowtracker.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QTimer>
#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString service = QStringLiteral("org.keyboardvolumeapp.KwinTest.p") +
                            QString::number(QCoreApplication::applicationPid());
    if (!QDBusConnection::sessionBus().registerService(service)) return 2;
    WindowTracker tracker(nullptr, service, QStringLiteral("keyboard-volume-app-test"));
    bool limusic = false;
    bool strawberry = false;
    QObject::connect(&tracker, &WindowTracker::focusedBinaryChanged, &app,
                     [&](const QString& name)
                     {
                         fprintf(stdout, "FOCUS %s\n", qPrintable(name));
                         fflush(stdout);
                         if (name.contains(QLatin1String("limusic"), Qt::CaseInsensitive))
                             limusic = true;
                         if (name.contains(QLatin1String("strawberry"), Qt::CaseInsensitive))
                             strawberry = true;
                         if (limusic && strawberry) app.quit();
                     });
    QObject::connect(&tracker, &WindowTracker::error, &app,
                     [&](const QString& msg)
                     {
                         fprintf(stderr, "ERROR %s\n", qPrintable(msg));
                         app.exit(3);
                     });
    QTimer::singleShot(120000, &app, [&]() { app.exit(4); });
    tracker.start();
    const int code = app.exec();
    tracker.stop();
    return code;
}
