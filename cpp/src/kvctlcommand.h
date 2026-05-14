#pragma once

#include <QString>
#include <QStringList>

struct KvCtlCommand
{
    enum class Action
    {
        VolumeUp,
        VolumeDown,
        ToggleMute,
        ToggleDucking,
        ApplyScene,
        Refresh,
        Get,
        Set,
        Show
    };

    enum class Field
    {
        None,
        Volume,
        Muted,
        ActiveApp,
        Apps,
        Step,
        Profiles,
        Scenes,
        ProgressEnabled
    };

    Action action = Action::Refresh;
    Field field = Field::None;
    QString profile;
    QString scene;
    QString value;
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
