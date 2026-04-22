#pragma once
#include <QString>
#include <optional>

// Represents an audio application — may or may not have an active PA stream.
struct AudioApp {
    std::optional<uint32_t> sinkInputIndex;  // nullopt if app has no active stream
    QString  name;
    QString  binary;
    double   volume = 1.0;    // 0.0 – 1.0
    bool     muted  = false;
    bool     active = false;  // true when sinkInputIndex has a value
};
