#pragma once

#include "audioapp.h"

#include <QList>
#include <QString>

// Match a focused-window binary name against the PipeWire app cache using a
// case-insensitive substring check against both AudioApp::name and
// AudioApp::binary. Returns the matched AudioApp::name, or an empty string
// when no match is found.
//
// Empty fields are explicitly rejected: QString::contains("") returns true
// for any haystack, so without the guard any AudioApp with an empty name or
// binary would match every focused window and break auto-profile switching.
inline QString matchBinaryToApp(const QString& binary, const QList<AudioApp>& cache)
{
    if (binary.isEmpty()) return {};
    const QString lower = binary.toLower();
    for (const AudioApp& app : cache)
    {
        const QString appName = app.name.toLower();
        if (!appName.isEmpty() && (appName.contains(lower) || lower.contains(appName)))
            return app.name;
        const QString appBinary = app.binary.toLower();
        if (!appBinary.isEmpty() && (appBinary.contains(lower) || lower.contains(appBinary)))
            return app.name;
    }
    return {};
}
