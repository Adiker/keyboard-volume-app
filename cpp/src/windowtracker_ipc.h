#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QtGlobal>

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>

namespace windowtracker_ipc
{

// ─── Hyprland IPC helpers ────────────────────────────────────────────────────
QString hyprlandSocketPath(const QString& signature, const QString& runtimeDir, bool eventSocket);
std::optional<QString> parseHyprlandEventLine(const QByteArray& line);
std::optional<QString> parseHyprlandActiveWindowJson(const QByteArray& json);

// ─── Niri IPC helpers ────────────────────────────────────────────────────────
struct NiriFocusState
{
    QHash<qint64, QString> appIdByWindowId;
    std::optional<qint64> focusedId;
};

// Applies one JSON line from Niri's EventStream. Returns the new focused app_id
// (possibly empty string when focus clears) if focus changed, or std::nullopt
// if the event did not affect focus.
std::optional<QString> applyNiriEventLine(const QByteArray& line, NiriFocusState& state);

// ─── Sway / i3 IPC helpers ───────────────────────────────────────────────────
constexpr uint32_t kSwayMsgSubscribe = 2;
constexpr uint32_t kSwayMsgGetTree = 4;
constexpr uint32_t kSwayEventWindow = 0x80000003U;

QByteArray encodeSwayIpcMessage(uint32_t type, const QByteArray& payload);
std::optional<QString> extractFocusedAppFromSwayTree(const QByteArray& json);
std::optional<QString> parseSwayWindowEvent(const QByteArray& json);

// ─── UNIX socket probes & event loops ────────────────────────────────────────
bool probeUnixSocket(const QString& path);

bool runHyprlandIpc(const QString& cmdSocketPath, const QString& eventSocketPath,
                    const std::atomic<bool>& running,
                    const std::function<void(const QString&)>& emitFocus,
                    const std::function<void(const QString&)>& emitError);

bool runNiriIpc(const QString& socketPath, const std::atomic<bool>& running,
                const std::function<void(const QString&)>& emitFocus,
                const std::function<void(const QString&)>& emitError);

bool runSwayIpc(const QString& socketPath, const std::atomic<bool>& running,
                const std::function<void(const QString&)>& emitFocus,
                const std::function<void(const QString&)>& emitError);

} // namespace windowtracker_ipc
