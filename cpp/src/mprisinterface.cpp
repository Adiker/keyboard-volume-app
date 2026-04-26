#include "mprisinterface.h"
#include "dbusinterface.h"

#include <QApplication>

MprisRootAdaptor::MprisRootAdaptor(DbusInterface *dbus, QObject *parent)
    : QDBusAbstractAdaptor(parent)
{
    Q_UNUSED(dbus);
}

QString MprisRootAdaptor::identity() const
{
    return QStringLiteral("Keyboard Volume App");
}

bool MprisRootAdaptor::canQuit() const  { return true; }
bool MprisRootAdaptor::canRaise() const { return false; }

void MprisRootAdaptor::Quit()  { qApp->quit(); }
void MprisRootAdaptor::Raise() {}

MprisPlayerAdaptor::MprisPlayerAdaptor(DbusInterface *dbus, QObject *parent)
    : QDBusAbstractAdaptor(parent)
    , m_dbus(dbus)
{
    m_metadata[QStringLiteral("xesam:title")] = m_dbus->activeApp();

    connect(m_dbus, &DbusInterface::volumeChanged,
            this, &MprisPlayerAdaptor::VolumeChanged);
    connect(m_dbus, &DbusInterface::activeAppChanged,
            this, [this](const QString &name) {
        m_metadata.clear();
        m_metadata[QStringLiteral("xesam:title")] = name;
        emit MetadataChanged(m_metadata);
    });
}

QString MprisPlayerAdaptor::playbackStatus() const { return QStringLiteral("Stopped"); }
bool MprisPlayerAdaptor::canControl() const        { return true; }
bool MprisPlayerAdaptor::canPlay() const           { return false; }
bool MprisPlayerAdaptor::canPause() const          { return false; }
bool MprisPlayerAdaptor::canSeek() const           { return false; }
bool MprisPlayerAdaptor::canGoNext() const         { return false; }
bool MprisPlayerAdaptor::canGoPrevious() const     { return false; }

double MprisPlayerAdaptor::volume() const { return m_dbus->volume(); }

void MprisPlayerAdaptor::setVolume(double vol) { m_dbus->setVolume(vol); }

QVariantMap MprisPlayerAdaptor::metadata() const { return m_metadata; }
