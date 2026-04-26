#pragma once
#include <QDBusAbstractAdaptor>
#include <QString>
#include <QVariantMap>

class DbusInterface;

class MprisRootAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")

    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)

public:
    explicit MprisRootAdaptor(DbusInterface *dbus, QObject *parent);

    QString identity() const;
    bool canQuit() const;
    bool canRaise() const;

public slots:
    Q_SCRIPTABLE void Quit();
    Q_SCRIPTABLE void Raise();
};

class MprisPlayerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")

    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(bool CanControl READ canControl)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPause)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(double Volume READ volume WRITE setVolume NOTIFY VolumeChanged)
    Q_PROPERTY(QVariantMap Metadata READ metadata NOTIFY MetadataChanged)

public:
    explicit MprisPlayerAdaptor(DbusInterface *dbus, QObject *parent);

    QString    playbackStatus() const;
    bool       canControl() const;
    bool       canPlay() const;
    bool       canPause() const;
    bool       canSeek() const;
    bool       canGoNext() const;
    bool       canGoPrevious() const;
    double     volume() const;
    void       setVolume(double vol);
    QVariantMap metadata() const;

public slots:
    Q_SCRIPTABLE void Play() {}
    Q_SCRIPTABLE void Pause() {}
    Q_SCRIPTABLE void PlayPause() {}
    Q_SCRIPTABLE void Stop() {}
    Q_SCRIPTABLE void Next() {}
    Q_SCRIPTABLE void Previous() {}

signals:
    void VolumeChanged(double vol);
    void MetadataChanged(const QVariantMap &meta);

private:
    DbusInterface *m_dbus = nullptr;
    QVariantMap    m_metadata;
};
