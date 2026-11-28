#pragma once

#include <QString>
#include <QStringList>
#include <cstdint>

struct KvCtlCommand
{
    enum class Action : std::uint8_t
    {
        VolumeUp,
        VolumeDown,
        ToggleMute,
        SetMute,
        ToggleDucking,
        ApplyScene,
        Refresh,
        Get,
        Set,
        Show,
        NextActiveApp,
        PreviousActiveApp,
        MediaPlayPause,
        MediaNext,
        MediaPrevious,
        MediaStop
    };

    enum class Field : std::uint8_t
    {
        None,
        Volume,
        Muted,
        ActiveApp,
        Apps,
        Step,
        Profiles,
        Scenes,
        ProgressEnabled,
        AutoProfileSwitch,
        Sinks,
        Sink
    };

    Action action = Action::Refresh;
    Field field = Field::None;
    QString profile;
    QString scene;
    QString value;
    // `set sink APP DEVICE` carries the audio app name in target, the sink in value.
    QString target;
};

struct KvCtlParseResult
{
    bool ok = false;
    KvCtlCommand command;
    QString error;
};

KvCtlParseResult parseKvCtlCommand(const QStringList& positionalArgs, const QString& profile,
                                   bool profileSet);

QString kvCtlUsageText();
