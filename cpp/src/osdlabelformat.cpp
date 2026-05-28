#include "osdlabelformat.h"

#include <QChar>
#include <QString>

namespace
{

bool isOrphanSeparator(QChar c)
{
    static const QString kSeps = QStringLiteral(" \t-:|/–—");
    return kSeps.contains(c);
}

void trimOrphans(QString& s)
{
    while (!s.isEmpty() && isOrphanSeparator(s.front())) s.remove(0, 1);
    while (!s.isEmpty() && isOrphanSeparator(s.back())) s.chop(1);
}

} // namespace

QString formatOsdLabelTemplate(const QString& tpl, const LabelTokens& tokens)
{
    // Use a private-use sentinel (U+0001) to mark positions of empty tokens.
    // Adjacent whitespace + separator characters on the RIGHT of each sentinel
    // are then eaten, preserving any separator on the LEFT — this collapses
    // "Spotify —  — Artist" into "Spotify — Artist" without disturbing a
    // legitimate "Spotify: Title" between two non-empty tokens.
    const QChar kMark(0x0001);
    QString s = tpl;

    auto sub = [&](QLatin1String key, const QString& value)
    { s.replace(key, value.isEmpty() ? QString(kMark) : value); };

    sub(QLatin1String("{app}"), tokens.app);
    sub(QLatin1String("{player}"), tokens.player);
    sub(QLatin1String("{title}"), tokens.title);
    sub(QLatin1String("{artist}"), tokens.artist);
    sub(QLatin1String("{album}"), tokens.album);

    int idx = 0;
    while ((idx = s.indexOf(kMark, idx)) >= 0)
    {
        int end = idx + 1;
        while (end < s.size() && (s[end] == kMark || isOrphanSeparator(s[end]))) ++end;
        s.remove(idx, end - idx);
        // Continue from idx — the position now holds the first kept character
        // (which may itself need processing if multiple sentinels chain).
    }

    trimOrphans(s);
    return s;
}
