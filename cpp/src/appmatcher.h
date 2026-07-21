#pragma once

#include "audioapp.h"
#include "config.h"

#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

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

// Expand an app name used for volume/mute/sink ops (and Follow Focus) to every
// string that may appear in PulseAudio / PipeWire fields after aliases remap
// the control target (e.g. alias chromium → youtube-music must still match PA
// binary "chromium").
inline QStringList appMatchCandidates(const QString& appName, const QList<AppAlias>& aliases)
{
    QStringList out;
    auto add = [&](const QString& candidate)
    {
        const QString trimmed = candidate.trimmed();
        if (trimmed.isEmpty()) return;
        for (const QString& existing : std::as_const(out))
        {
            if (existing.compare(trimmed, Qt::CaseInsensitive) == 0) return;
        }
        out.append(trimmed);
    };

    add(appName);
    if (aliases.isEmpty() || appName.isEmpty()) return out;

    const QString lower = appName.toLower();
    for (const AppAlias& alias : aliases)
    {
        const bool hitMatch = appIdMatches(alias.match, lower);
        const bool hitDisplay = !alias.display.isEmpty() && appIdMatches(alias.display, lower);
        const bool hitTarget = !alias.target.isEmpty() && appIdMatches(alias.target, lower);
        if (!hitMatch && !hitDisplay && !hitTarget) continue;

        add(alias.match);
        add(alias.display);
        add(alias.target);
    }
    return out;
}

inline bool appNameMatchesFields(const QString& appName, const QString& name, const QString& binary,
                                 const QString& mediaName = {}, const QList<AppAlias>& aliases = {})
{
    for (const QString& candidate : appMatchCandidates(appName, aliases))
    {
        if (name.compare(candidate, Qt::CaseInsensitive) == 0 ||
            binary.compare(candidate, Qt::CaseInsensitive) == 0 ||
            mediaName.compare(candidate, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

inline QString matchBinaryToApp(const QString& binary, const QList<AudioApp>& cache,
                                const QList<AppAlias>& aliases = {})
{
    if (binary.isEmpty()) return {};
    const QString lower = binary.toLower();
    for (const AudioApp& app : cache)
    {
        if (appIdMatches(app.name, lower) || appIdMatches(app.binary, lower))
            return app.binary.isEmpty() ? app.name : app.binary;

        // Cache may already hold the remapped alias target (youtube-music) while
        // WindowTracker still reports the source binary (chromium). Expand through
        // aliases so Follow Focus can resolve the control target.
        const QString control = app.binary.isEmpty() ? app.name : app.binary;
        if (control.isEmpty() && app.name.isEmpty()) continue;
        for (const QString& candidate : appMatchCandidates(control, aliases))
        {
            if (appIdMatches(candidate, lower)) return control;
        }
        if (!app.name.isEmpty())
        {
            for (const QString& candidate : appMatchCandidates(app.name, aliases))
            {
                if (appIdMatches(candidate, lower)) return control.isEmpty() ? app.name : control;
            }
        }
    }

    // No live cache entry — still map a focused alias source to its control target
    // so a profile can activate before the stream appears in the app list.
    for (const AppAlias& alias : aliases)
    {
        if (!appIdMatches(alias.match, lower) &&
            (alias.display.isEmpty() || !appIdMatches(alias.display, lower)))
            continue;
        if (!alias.target.isEmpty()) return alias.target;
        if (!alias.display.isEmpty()) return alias.display;
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
                                              const QString& currentTarget,
                                              const QList<AppAlias>& aliases = {})
{
    if (focusedBinary.isEmpty()) return currentTarget;

    const QString matchedApp = matchBinaryToApp(focusedBinary, cache, aliases);
    const QString preferredTarget = matchedApp.isEmpty() ? focusedBinary : matchedApp;

    // Collect focused binary + remapped target + alias siblings so regex profiles
    // that list the source name (chromium) still fire when cache only has the
    // aliased control target (youtube-music).
    QStringList tryApps = appMatchCandidates(preferredTarget, aliases);
    for (const QString& candidate : appMatchCandidates(focusedBinary, aliases))
    {
        bool exists = false;
        for (const QString& existing : std::as_const(tryApps))
        {
            if (existing.compare(candidate, Qt::CaseInsensitive) == 0)
            {
                exists = true;
                break;
            }
        }
        if (!exists) tryApps.append(candidate);
    }

    for (const QString& candidate : tryApps)
    {
        const Profile matchedProfile = findAutoSwitchProfileForApp(candidate, profiles);
        if (matchedProfile.id.isEmpty()) continue;
        return preferredTarget;
    }
    return currentTarget;
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
