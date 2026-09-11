// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/trust_client.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <unistd.h>

#include <gio/gio.h>

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include <spdlog/spdlog.h>

// response_fd 读取缓冲上限；回复 JSON 为单对象，正常远小于该值
static constexpr size_t TRUST_RESPONSE_MAX_LEN = 4096;

ANYTHING_NAMESPACE_BEGIN

bool parse_trust_fds(int argc, char *argv[], int &request_fd, int &response_fd)
{
    int fd1 = -1;
    int fd2 = -1;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--fd1") == 0 && i + 1 < argc) {
            char *end = nullptr;
            const long value = std::strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || value < 0) {
                spdlog::warn("Invalid --fd1 value: {}", argv[i]);
                return false;
            }
            fd1 = static_cast<int>(value);
        } else if (std::strcmp(argv[i], "--fd2") == 0 && i + 1 < argc) {
            char *end = nullptr;
            const long value = std::strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || value < 0) {
                spdlog::warn("Invalid --fd2 value: {}", argv[i]);
                return false;
            }
            fd2 = static_cast<int>(value);
        }
    }

    if (fd1 < 0 || fd2 < 0) {
        spdlog::warn("Trust fds not provided (--fd1/--fd2), skip trust negotiation");
        return false;
    }

    request_fd = fd1;
    response_fd = fd2;
    return true;
}

static bool write_all(int fd, const std::string &data)
{
    size_t written = 0;
    while (written < data.size()) {
        const ssize_t n = ::write(fd, data.data() + written, data.size() - written);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            spdlog::error("Failed to write trust request: {}", std::strerror(errno));
            return false;
        }
        written += static_cast<size_t>(n);
    }
    return true;
}

/// 逐字节读取直到字符串外的 {} 配平（或 EOF / 缓冲上限）。
/// 跟踪是否处于 JSON 字符串内部及转义状态，避免字符串内的括号干扰配平。
static bool read_balanced_json(int fd, std::string &buffer)
{
    int depth = 0;
    bool in_string = false;
    bool escaped = false;

    buffer.clear();
    buffer.reserve(256);

    char ch;
    while (buffer.size() < TRUST_RESPONSE_MAX_LEN) {
        const ssize_t n = ::read(fd, &ch, 1);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            spdlog::error("Failed to read trust response: {}", std::strerror(errno));
            return false;
        }
        if (n == 0) // EOF
            break;

        buffer.push_back(ch);

        if (in_string) {
            if (escaped)
                escaped = false;
            else if (ch == '\\')
                escaped = true;
            else if (ch == '"')
                in_string = false;
            continue;
        }

        switch (ch) {
        case '"':
            in_string = true;
            break;
        case '{':
            ++depth;
            break;
        case '}':
            if (--depth == 0)
                return true; // 顶层对象闭合，JSON 完整
            break;
        default:
            break;
        }
    }

    if (depth == 0 && buffer.empty()) {
        spdlog::error("Trust response: connection closed without data");
        return false;
    }

    spdlog::error("Trust response: incomplete JSON (depth {} after {} bytes)",
                  depth, buffer.size());
    return false;
}

bool request_dbus_trust(const std::string &bus_name,
                        const std::string &object_path,
                        const std::string &interface_name,
                        int request_fd,
                        int response_fd)
{
    // 连接系统总线并取本连接的唯一总线名称（loader 期望值，如 ":1.234"）
    GError *error = nullptr;
    GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (connection == nullptr) {
        spdlog::error("Failed to connect to system bus: {}",
                      error ? error->message : "unknown error");
        g_clear_error(&error);
        return false;
    }

    const gchar *unique_name_c = g_dbus_connection_get_unique_name(connection);
    if (unique_name_c == nullptr || *unique_name_c == '\0') {
        spdlog::error("Failed to get unique bus name");
        g_object_unref(connection);
        return false;
    }
    const std::string unique_name(unique_name_c);

    // 构造请求 JSON：
    // {"UniqueName":"...","DestList":[{"DbusName":"...","DbusPath":"...","DbusInterface":"..."}]}
    QJsonObject dest;
    dest.insert("DbusName", QString::fromStdString(bus_name));
    dest.insert("DbusPath", QString::fromStdString(object_path));
    dest.insert("DbusInterface", QString::fromStdString(interface_name));

    QJsonObject request;
    request.insert("UniqueName", QString::fromStdString(unique_name));
    QJsonArray dest_list;
    dest_list.append(dest);
    request.insert("DestList", dest_list);

    const QByteArray payload =
        QJsonDocument(request).toJson(QJsonDocument::Compact);

    g_object_unref(connection);

    const std::string request_json(payload.constData(), static_cast<size_t>(payload.size()));
    spdlog::debug("Trust request: {}", request_json);

    // 写请求（阻塞式一次性写出，处理 EINTR 与部分写）
    if (!write_all(request_fd, request_json)) {
        close(request_fd);
        close(response_fd);
        return false;
    }

    // 读回复（括号配平终止，不依赖 loader 关闭写端）
    std::string response;
    const bool read_ok = read_balanced_json(response_fd, response);

    // fd 由 loader 在 exec 时注入，用完即关
    close(request_fd);
    close(response_fd);

    if (!read_ok)
        return false;

    spdlog::debug("Trust response: {}", response);

    // 解析回复 JSON：{"Result":true,"Message":"..."}
    const QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray(response.data(), static_cast<int>(response.size())));
    if (doc.isNull() || !doc.isObject()) {
        spdlog::error("Failed to parse trust response as JSON object: {}", response);
        return false;
    }

    const QJsonObject reply = doc.object();
    if (!reply.contains("Result") || !reply.value("Result").isBool()) {
        spdlog::error("Trust response missing boolean 'Result' field: {}", response);
        return false;
    }

    const bool result = reply.value("Result").toBool();
    const QString message = reply.value("Message").toString();

    if (!result) {
        spdlog::error("DBus trust request rejected by security loader: {}",
                      message.isEmpty() ? std::string("no message") : message.toStdString());
        return false;
    }

    spdlog::info("DBus trust granted: {}", message.toStdString());
    return true;
}

ANYTHING_NAMESPACE_END
