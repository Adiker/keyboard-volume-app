#pragma once

#include <QString>
#include <QVariant>

// Render a value returned by org.freedesktop.DBus.Properties.Get. Qt keeps
// nested arrays/maps as QDBusArgument values, so this also normalizes those
// wire containers before producing the script-friendly text output.
QString formatKvCtlValue(const QVariant& value);
