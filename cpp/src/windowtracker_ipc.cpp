#include "windowtracker_ipc.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace windowtracker_ipc
{

namespace
{

constexpr char kSwayMagic[6] = {'i', '3', '-', 'i', 'p', 'c'};
constexpr qsizetype kSwayHeaderSize = 14;
constexpr uint32_t kMaxSwayPayloadBytes = 16U * 1024U * 1024U;

int connectUnixSocket(const QString& path)
{
    const QByteArray utf8 = path.toUtf8();
    sockaddr_un addr{};
    if (utf8.isEmpty() || static_cast<size_t>(utf8.size()) >= sizeof(addr.sun_path)) return -1;

    const int fd = ::socket(AF_UNIX,
                            static_cast<int>(static_cast<unsigned int>(SOCK_STREAM) |
                                             static_cast<unsigned int>(SOCK_CLOEXEC)),
                            0);
    if (fd < 0) return -1;

    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, utf8.constData(), static_cast<size_t>(utf8.size()) + 1);
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        ::close(fd);
        return -1;
    }
    return fd;
}

bool writeAll(int fd, const char* data, size_t len)
{
    size_t written = 0;
    while (written < len)
    {
        const ssize_t n = ::write(fd, data + written, len - written);
        if (n < 0)
        {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        written += static_cast<size_t>(n);
    }
    return true;
}

bool readExact(int fd, char* dst, size_t len, const std::atomic<bool>& running, int timeoutMs = -1)
{
    size_t got = 0;
    int waited = 0;
    while (got < len && running.load(std::memory_order_acquire))
    {
        pollfd pfd{fd, POLLIN, 0};
        const int rc = ::poll(&pfd, 1, 50);
        if (rc < 0)
        {
            if (errno == EINTR) continue;
            return false;
        }
        if (rc == 0)
        {
            if (timeoutMs >= 0)
            {
                waited += 50;
                if (waited >= timeoutMs) return false;
            }
            continue;
        }
        if ((static_cast<unsigned int>(pfd.revents) &
             (static_cast<unsigned int>(POLLERR) | static_cast<unsigned int>(POLLNVAL))) != 0U)
            return false;

        const ssize_t n = ::read(fd, dst + got, len - got);
        if (n < 0)
        {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        got += static_cast<size_t>(n);
    }
    return got == len;
}

bool readSwayMessage(int fd, uint32_t& outType, QByteArray& outPayload,
                     const std::atomic<bool>& running, int timeoutMs = -1)
{
    char header[kSwayHeaderSize];
    if (!readExact(fd, header, sizeof(header), running, timeoutMs)) return false;
    if (std::memcmp(header, kSwayMagic, sizeof(kSwayMagic)) != 0) return false;

    uint32_t payloadLen = 0;
    uint32_t msgType = 0;
    std::memcpy(&payloadLen, header + 6, sizeof(payloadLen));
    std::memcpy(&msgType, header + 10, sizeof(msgType));
    if (payloadLen > kMaxSwayPayloadBytes) return false;

    outPayload.resize(static_cast<qsizetype>(payloadLen));
    if (payloadLen > 0 && !readExact(fd, outPayload.data(), payloadLen, running, timeoutMs))
        return false;
    outType = msgType;
    return true;
}

QString swayNodeAppName(const QJsonObject& node)
{
    QString appId = node[QStringLiteral("app_id")].toString().trimmed();
    if (!appId.isEmpty()) return appId;

    const QJsonObject props = node[QStringLiteral("window_properties")].toObject();
    QString winClass = props[QStringLiteral("class")].toString().trimmed();
    if (!winClass.isEmpty()) return winClass;
    return props[QStringLiteral("instance")].toString().trimmed();
}

std::optional<QString> findFocusedInSwayNode(const QJsonObject& node)
{
    if (node[QStringLiteral("focused")].toBool(false))
    {
        QString app = swayNodeAppName(node);
        if (!app.isEmpty()) return app;
    }

    const QJsonArray nodes = node[QStringLiteral("nodes")].toArray();
    for (const auto& child : nodes)
    {
        if (child.isObject())
        {
            if (auto found = findFocusedInSwayNode(child.toObject())) return found;
        }
    }
    const QJsonArray floating = node[QStringLiteral("floating_nodes")].toArray();
    for (const auto& child : floating)
    {
        if (child.isObject())
        {
            if (auto found = findFocusedInSwayNode(child.toObject())) return found;
        }
    }
    return std::nullopt;
}

} // namespace

QString hyprlandSocketPath(const QString& signature, const QString& runtimeDir, bool eventSocket)
{
    if (signature.isEmpty() || runtimeDir.isEmpty()) return {};
    return QDir(runtimeDir)
        .filePath(QStringLiteral("hypr/%1/%2")
                      .arg(signature, eventSocket ? QStringLiteral(".socket2.sock")
                                                  : QStringLiteral(".socket.sock")));
}

std::optional<QString> parseHyprlandEventLine(const QByteArray& line)
{
    static constexpr const char kPrefix[] = "activewindow>>";
    static constexpr qsizetype kPrefixLen = sizeof(kPrefix) - 1;
    if (!line.startsWith(kPrefix)) return std::nullopt;

    QByteArray payload = line.mid(kPrefixLen);
    while (payload.endsWith('\n') || payload.endsWith('\r')) payload.chop(1);
    const qsizetype comma = payload.indexOf(',');
    const QByteArray windowClass = comma >= 0 ? payload.left(comma) : payload;
    return QString::fromUtf8(windowClass).trimmed();
}

std::optional<QString> parseHyprlandActiveWindowJson(const QByteArray& json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) return std::nullopt;
    const QJsonObject obj = doc.object();
    if (!obj.contains(QStringLiteral("class"))) return std::nullopt;
    return obj[QStringLiteral("class")].toString().trimmed();
}

std::optional<QString> applyNiriEventLine(const QByteArray& line, NiriFocusState& state)
{
    const QJsonDocument doc = QJsonDocument::fromJson(line);
    if (!doc.isObject()) return std::nullopt;
    const QJsonObject root = doc.object();

    if (root.contains(QStringLiteral("WindowsChanged")))
    {
        const QJsonObject payload = root[QStringLiteral("WindowsChanged")].toObject();
        state.appIdByWindowId.clear();
        state.focusedId.reset();
        QString focusedApp;
        const QJsonArray windows = payload[QStringLiteral("windows")].toArray();
        for (const auto& val : windows)
        {
            if (!val.isObject()) continue;
            const QJsonObject win = val.toObject();
            const qint64 id = win[QStringLiteral("id")].toInteger(-1);
            if (id < 0) continue;
            const QString appId = win[QStringLiteral("app_id")].toString().trimmed();
            state.appIdByWindowId.insert(id, appId);
            if (win[QStringLiteral("is_focused")].toBool(false))
            {
                state.focusedId = id;
                focusedApp = appId;
            }
        }
        return focusedApp;
    }

    if (root.contains(QStringLiteral("WindowOpenedOrChanged")))
    {
        const QJsonObject payload = root[QStringLiteral("WindowOpenedOrChanged")].toObject();
        const QJsonObject win = payload[QStringLiteral("window")].toObject();
        const qint64 id = win[QStringLiteral("id")].toInteger(-1);
        if (id < 0) return std::nullopt;
        QString appId = win[QStringLiteral("app_id")].toString().trimmed();
        state.appIdByWindowId.insert(id, appId);
        if (win[QStringLiteral("is_focused")].toBool(false))
        {
            state.focusedId = id;
            return appId;
        }
        if (state.focusedId && *state.focusedId == id) return appId;
        return std::nullopt;
    }

    if (root.contains(QStringLiteral("WindowClosed")))
    {
        const QJsonObject payload = root[QStringLiteral("WindowClosed")].toObject();
        const qint64 id = payload[QStringLiteral("id")].toInteger(-1);
        if (id >= 0) state.appIdByWindowId.remove(id);
        if (state.focusedId && *state.focusedId == id)
        {
            state.focusedId.reset();
            return QString{};
        }
        return std::nullopt;
    }

    if (root.contains(QStringLiteral("WindowFocusChanged")))
    {
        const QJsonObject payload = root[QStringLiteral("WindowFocusChanged")].toObject();
        const QJsonValue idVal = payload[QStringLiteral("id")];
        if (idVal.isNull() || idVal.isUndefined())
        {
            state.focusedId.reset();
            return QString{};
        }
        const qint64 id = idVal.toInteger(-1);
        if (id < 0)
        {
            state.focusedId.reset();
            return QString{};
        }
        state.focusedId = id;
        return state.appIdByWindowId.value(id);
    }

    return std::nullopt;
}

QByteArray encodeSwayIpcMessage(uint32_t type, const QByteArray& payload)
{
    QByteArray out(kSwayHeaderSize + payload.size(), '\0');
    std::memcpy(out.data(), kSwayMagic, sizeof(kSwayMagic));
    const auto len = static_cast<uint32_t>(payload.size());
    std::memcpy(out.data() + 6, &len, sizeof(len));
    std::memcpy(out.data() + 10, &type, sizeof(type));
    if (!payload.isEmpty())
        std::memcpy(out.data() + kSwayHeaderSize, payload.constData(),
                    static_cast<size_t>(payload.size()));
    return out;
}

std::optional<QString> extractFocusedAppFromSwayTree(const QByteArray& json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) return std::nullopt;
    return findFocusedInSwayNode(doc.object());
}

std::optional<QString> parseSwayWindowEvent(const QByteArray& json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) return std::nullopt;
    const QJsonObject obj = doc.object();
    if (obj[QStringLiteral("change")].toString() != QLatin1String("focus")) return std::nullopt;
    const QJsonObject container = obj[QStringLiteral("container")].toObject();
    return swayNodeAppName(container);
}

bool probeUnixSocket(const QString& path)
{
    if (path.isEmpty()) return false;
    struct stat st = {};
    const QByteArray utf8 = path.toUtf8();
    return ::stat(utf8.constData(), &st) == 0 && S_ISSOCK(st.st_mode);
}

bool runHyprlandIpc(const QString& cmdSocketPath, const QString& eventSocketPath,
                    const std::atomic<bool>& running,
                    const std::function<void(const QString&)>& emitFocus,
                    const std::function<void(const QString&)>& emitError)
{
    QString lastApp;
    auto report = [&](const QString& app)
    {
        if (app == lastApp) return;
        lastApp = app;
        emitFocus(app);
    };

    // Best-effort initial focused window query via command socket.
    if (!cmdSocketPath.isEmpty())
    {
        const int cmdFd = connectUnixSocket(cmdSocketPath);
        if (cmdFd >= 0)
        {
            static constexpr const char kCmd[] = "j/activewindow";
            if (writeAll(cmdFd, kCmd, sizeof(kCmd) - 1))
            {
                QByteArray resp;
                char buf[1024];
                while (running.load(std::memory_order_acquire))
                {
                    pollfd pfd{cmdFd, POLLIN, 0};
                    const int rc = ::poll(&pfd, 1, 200);
                    if (rc <= 0) break;
                    const ssize_t n = ::read(cmdFd, buf, sizeof(buf));
                    if (n <= 0) break;
                    resp.append(buf, static_cast<qsizetype>(n));
                    if (resp.size() > 65536) break;
                }
                if (const auto initial = parseHyprlandActiveWindowJson(resp)) report(*initial);
            }
            ::close(cmdFd);
        }
    }

    const int evFd = connectUnixSocket(eventSocketPath);
    if (evFd < 0)
    {
        emitError(QStringLiteral("Cannot connect to Hyprland event socket"));
        return false;
    }

    QByteArray buffer;
    char chunk[1024];
    while (running.load(std::memory_order_acquire))
    {
        pollfd pfd{evFd, POLLIN, 0};
        const int rc = ::poll(&pfd, 1, 50);
        if (rc < 0)
        {
            if (errno == EINTR) continue;
            break;
        }
        if (rc == 0) continue;
        if ((static_cast<unsigned int>(pfd.revents) &
             (static_cast<unsigned int>(POLLERR) | static_cast<unsigned int>(POLLHUP) |
              static_cast<unsigned int>(POLLNVAL))) != 0U)
        {
            const ssize_t n = ::read(evFd, chunk, sizeof(chunk));
            if (n <= 0) break;
            buffer.append(chunk, static_cast<qsizetype>(n));
        }
        else if ((static_cast<unsigned int>(pfd.revents) & static_cast<unsigned int>(POLLIN)) != 0U)
        {
            const ssize_t n = ::read(evFd, chunk, sizeof(chunk));
            if (n <= 0) break;
            buffer.append(chunk, static_cast<qsizetype>(n));
        }

        qsizetype nl = buffer.indexOf('\n');
        while (nl >= 0)
        {
            const QByteArray line = buffer.left(nl);
            buffer.remove(0, nl + 1);
            if (const auto app = parseHyprlandEventLine(line)) report(*app);
            nl = buffer.indexOf('\n');
        }
    }

    ::close(evFd);
    return true;
}

bool runNiriIpc(const QString& socketPath, const std::atomic<bool>& running,
                const std::function<void(const QString&)>& emitFocus,
                const std::function<void(const QString&)>& emitError)
{
    const int fd = connectUnixSocket(socketPath);
    if (fd < 0)
    {
        emitError(QStringLiteral("Cannot connect to Niri IPC socket"));
        return false;
    }

    static constexpr const char kRequest[] = "\"EventStream\"\n";
    if (!writeAll(fd, kRequest, sizeof(kRequest) - 1))
    {
        ::close(fd);
        emitError(QStringLiteral("Cannot subscribe to Niri EventStream"));
        return false;
    }

    NiriFocusState state;
    QString lastApp;
    bool hasEmitted = false;
    QByteArray buffer;
    char chunk[2048];

    while (running.load(std::memory_order_acquire))
    {
        pollfd pfd{fd, POLLIN, 0};
        const int rc = ::poll(&pfd, 1, 50);
        if (rc < 0)
        {
            if (errno == EINTR) continue;
            break;
        }
        if (rc == 0) continue;
        const ssize_t n = ::read(fd, chunk, sizeof(chunk));
        if (n <= 0) break;
        buffer.append(chunk, static_cast<qsizetype>(n));

        qsizetype nl = buffer.indexOf('\n');
        while (nl >= 0)
        {
            const QByteArray line = buffer.left(nl).trimmed();
            buffer.remove(0, nl + 1);
            if (!line.isEmpty())
            {
                if (const auto app = applyNiriEventLine(line, state))
                {
                    if (!hasEmitted || *app != lastApp)
                    {
                        hasEmitted = true;
                        lastApp = *app;
                        emitFocus(*app);
                    }
                }
            }
            nl = buffer.indexOf('\n');
        }
    }

    ::close(fd);
    return true;
}

bool runSwayIpc(const QString& socketPath, const std::atomic<bool>& running,
                const std::function<void(const QString&)>& emitFocus,
                const std::function<void(const QString&)>& emitError)
{
    const int fd = connectUnixSocket(socketPath);
    if (fd < 0)
    {
        emitError(QStringLiteral("Cannot connect to Sway/i3 IPC socket"));
        return false;
    }

    QString lastApp;
    auto report = [&](const QString& app)
    {
        if (app == lastApp) return;
        lastApp = app;
        emitFocus(app);
    };

    // 1. Initial focused window from GET_TREE.
    const QByteArray treeReq = encodeSwayIpcMessage(kSwayMsgGetTree, {});
    if (writeAll(fd, treeReq.constData(), static_cast<size_t>(treeReq.size())))
    {
        uint32_t type = 0;
        QByteArray payload;
        if (readSwayMessage(fd, type, payload, running, 1000) && type == kSwayMsgGetTree)
        {
            if (const auto initial = extractFocusedAppFromSwayTree(payload)) report(*initial);
        }
    }

    // 2. Subscribe to window focus events.
    const QByteArray subReq =
        encodeSwayIpcMessage(kSwayMsgSubscribe, QByteArrayLiteral("[\"window\"]"));
    if (!writeAll(fd, subReq.constData(), static_cast<size_t>(subReq.size())))
    {
        ::close(fd);
        emitError(QStringLiteral("Cannot subscribe to Sway/i3 window events"));
        return false;
    }

    uint32_t subReplyType = 0;
    QByteArray subReply;
    if (!readSwayMessage(fd, subReplyType, subReply, running, 1000))
    {
        ::close(fd);
        emitError(QStringLiteral("Sway/i3 subscribe handshake failed"));
        return false;
    }

    while (running.load(std::memory_order_acquire))
    {
        uint32_t msgType = 0;
        QByteArray payload;
        if (!readSwayMessage(fd, msgType, payload, running)) break;
        if (msgType == kSwayEventWindow)
        {
            if (const auto app = parseSwayWindowEvent(payload)) report(*app);
        }
    }

    ::close(fd);
    return true;
}

} // namespace windowtracker_ipc
