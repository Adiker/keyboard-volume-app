#pragma once
#include <QString>

// Token bundle for OSD label rendering.
//   {app}    — audio app name from VolumeController (e.g. "spotify")
//   {player} — MPRIS player display name (e.g. "Spotify")
//   {title}  — xesam:title
//   {artist} — xesam:artist (first element if a list)
//   {album}  — xesam:album
// Empty tokens collapse to "" inside the template, then orphan separators
// (whitespace + any of `-—:|/`) at the start and end of each line are trimmed.
struct LabelTokens
{
    QString app;
    QString player;
    QString title;
    QString artist;
    QString album;
};

// Substitute {app}, {player}, {title}, {artist}, {album} in `tpl` with values
// from `tokens`. Unknown tokens are left literal (debuggable). After
// substitution, leading/trailing whitespace and orphan separators caused by
// empty tokens are stripped. Pure function — no Qt threads, no I/O.
QString formatOsdLabelTemplate(const QString& tpl, const LabelTokens& tokens);
