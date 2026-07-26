#pragma once
#include <QSet>
#include <QString>

// Built-in audio-app filter defaults shared by Config (effective* helpers),
// pwutils listing, and VolumeController. User overrides live in
// Config::audioAppFilters() and are applied on top via extra_*/remove_*.
extern const QSet<QString> SYSTEM_BINARIES;
extern const QSet<QString> SKIP_APP_NAMES;
