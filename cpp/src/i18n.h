#pragma once
#include <QString>
#include <QMap>

// Available language codes → display names
const QMap<QString, QString> &languages();   // { "en" → "English", "pl" → "Polski" }

// Set the active language.  Silently ignored for unknown codes.
void setLanguage(const QString &code);

// Current language code (default "en").
QString currentLanguage();

// Look up a translation key.  Falls back to English if the key is missing in
// the current language, and returns the raw key if it is missing everywhere.
QString tr(const QString &key);
