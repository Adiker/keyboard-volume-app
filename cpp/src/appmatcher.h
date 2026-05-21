#pragma once
#include "audioapp.h"

#include <QList>
#include <QString>

// Match a focused-window binary name against the PipeWire app cache.
// Returns the AudioApp::name of the first app whose name or binary is a
// case-insensitive substring of the focused binary (or vice versa), or an
// empty string when nothing matches.
//
// Empty candidate fields (AudioApp::name or AudioApp::binary may be empty for
// sink inputs that lack application.process.binary) are skipped explicitly,
// because QString::contains("") returns true and would otherwise make every
// focused binary match such an entry.
inline QString matchBinaryToApp(const QString& binary, const QList<AudioApp>& apps)
{
    if (binary.isEmpty()) return {};
    const QString lower = binary.toLower();

    auto candidateMatches = [&lower](const QString& candidate)
    {
        if (candidate.isEmpty()) return false;
        const QString cand = candidate.toLower();
        return cand.contains(lower) || lower.contains(cand);
    };

    for (const AudioApp& app : apps)
    {
        if (candidateMatches(app.name) || candidateMatches(app.binary)) return app.name;
    }
    return {};
}
