#pragma once

#include "audioapp.h"
#include "config.h"

#include <QList>
#include <QRegularExpression>
#include <QString>

// Match a focused-window binary/app id against the PipeWire app cache using a
// case-insensitive substring check against both AudioApp::name and
// AudioApp::binary. Also compares normalized identifiers so desktop app ids
// like "YouTube Music" match PipeWire targets like "youtube-music".
// Returns the matched AudioApp::binary when present (stable volume target),
// otherwise AudioApp::name, or an empty string when no match is found.
//
// Empty fields are explicitly rejected: QString::contains("") returns true
// for any haystack, so without the guard any AudioApp with an empty name or
// binary would match every focused window and break auto-profile switching.
inline QString normalizedAppId(QString value)
{
    value = value.toLower();
    QString out;
    out.reserve(value.size());
    for (const QChar ch : value)
    {
        if (ch.isLetterOrNumber()) out.append(ch);
    }
    return out;
}

inline bool appIdMatches(const QString& candidate, const QString& needle)
{
    if (candidate.isEmpty()) return false;
    const QString lowerCandidate = candidate.toLower();
    if (lowerCandidate.contains(needle) || needle.contains(lowerCandidate)) return true;

    const QString normalizedCandidate = normalizedAppId(candidate);
    const QString normalizedNeedle = normalizedAppId(needle);
    return !normalizedCandidate.isEmpty() && !normalizedNeedle.isEmpty() &&
           (normalizedCandidate.contains(normalizedNeedle) ||
            normalizedNeedle.contains(normalizedCandidate));
}

inline bool appRegexMatches(const QString& appName, const QString& pattern)
{
    if (appName.isEmpty() || pattern.isEmpty()) return false;
    const QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
    return re.isValid() && re.match(appName).hasMatch();
}

inline bool profileListsApp(const Profile& profile, const QString& appName)
{
    if (appName.isEmpty()) return false;
    const QString lower = appName.toLower();
    for (const QString& app : profile.apps)
    {
        if (appIdMatches(app, lower)) return true;
    }
    return appRegexMatches(appName, profile.appRegex);
}

inline QString matchBinaryToApp(const QString& binary, const QList<AudioApp>& cache)
{
    if (binary.isEmpty()) return {};
    const QString lower = binary.toLower();
    for (const AudioApp& app : cache)
    {
        if (appIdMatches(app.name, lower) || appIdMatches(app.binary, lower))
            return app.binary.isEmpty() ? app.name : app.binary;
    }
    return {};
}

inline Profile findAutoSwitchProfileForApp(const QString& appName, const QList<Profile>& profiles)
{
    if (appName.isEmpty()) return {};

    for (const Profile& profile : profiles)
    {
        if (!profile.autoSwitch) continue;
        if (profileListsApp(profile, appName)) return profile;
    }
    return {};
}

inline QString validateStickyAutoProfileTarget(const QString& currentTarget,
                                               const QList<Profile>& profiles)
{
    if (currentTarget.isEmpty()) return {};
    return findAutoSwitchProfileForApp(currentTarget, profiles).id.isEmpty() ? QString{}
                                                                             : currentTarget;
}

inline QString resolveStickyAutoProfileTarget(const QString& focusedBinary,
                                              const QList<AudioApp>& cache,
                                              const QList<Profile>& profiles,
                                              const QString& currentTarget)
{
    if (focusedBinary.isEmpty()) return currentTarget;

    const QString matchedApp = matchBinaryToApp(focusedBinary, cache);
    if (matchedApp.isEmpty()) return currentTarget;

    const Profile matchedProfile = findAutoSwitchProfileForApp(matchedApp, profiles);
    return matchedProfile.id.isEmpty() ? currentTarget : matchedApp;
}

// Apply the first matching alias to a detected client. UI shows `display`;
// volume control uses `target` when set, otherwise the original binary.
inline bool aliasMatchesNameOrBinary(const QString& name, const QString& binary,
                                     const AppAlias& alias)
{
    if (alias.match.isEmpty()) return false;
    const QString needle = alias.match.toLower();
    return appIdMatches(name, needle) || appIdMatches(binary, needle);
}

inline void applyAppAliasToNames(QString& displayName, QString& targetName,
                                 const QList<AppAlias>& aliases)
{
    for (const AppAlias& alias : aliases)
    {
        if (!aliasMatchesNameOrBinary(displayName, targetName, alias)) continue;
        if (!alias.display.isEmpty()) displayName = alias.display;
        if (!alias.target.isEmpty()) targetName = alias.target;
        return;
    }
}

template <typename ClientT>
inline ClientT applyAppAlias(ClientT client, const QList<AppAlias>& aliases)
{
    applyAppAliasToNames(client.name, client.binary, aliases);
    return client;
}

template <typename ClientT>
inline QList<ClientT> applyAppAliases(QList<ClientT> clients, const QList<AppAlias>& aliases)
{
    if (aliases.isEmpty()) return clients;
    for (ClientT& client : clients) client = applyAppAlias(client, aliases);
    return clients;
}
