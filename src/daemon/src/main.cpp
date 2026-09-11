// SPDX-FileCopyrightText: 2024 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unistd.h>
#include <pwd.h>

#include "anything.hpp"
#include "core/relay_event_listener.h"
#include "core/config.h"
#include "core/trust_client.h"
#include "utils/running_flag.h"

#include <QCoreApplication>
#include <QFile>
#include <QStandardPaths>
#include <glib-unix.h>
#include <gio/gio.h>

#include <spdlog/sinks/syslog_sink.h>

using namespace anything;

extern "C" {
static void on_event_received(gpointer user_data, fs_event *event) {
    auto handler = static_cast<default_event_handler*>(user_data);
    handler->handle(event);
}

static void on_quit_requested(gpointer user_data) {
    (void)user_data;
    spdlog::info("Event listener disconnected, requesting restart");
    set_app_restart(true);
    qApp->quit();
}
}

// 判断 uid 是否可登录
bool can_user_login() {
    struct passwd pwd, *result = NULL;
    gchar buf[1024];
    uid_t uid = getuid();

    if (getpwuid_r(uid, &pwd, buf, sizeof(buf), &result) != 0 || !result) {
        spdlog::warn("User not found");
        return false;
    }

    // 检查登录 Shell
    if (g_strcmp0(pwd.pw_shell, "/sbin/nologin") == 0 ||
        g_strcmp0(pwd.pw_shell, "/bin/false") == 0) {
        spdlog::warn("User can not login: {}", uid);
        return false;
    }

    return true;
}

// 写入 PID 文件供 systemd Type=forking 跟踪主进程。
// 路径 %t/deepin-anything-daemon.pid 即 $XDG_RUNTIME_DIR/deepin-anything-daemon.pid，
// 每用户独立，无需 root 权限。loader/loader-exec fork 链退出后 systemd
// 通过此文件定位 daemon 主 PID，避免 cgroup 被误杀。
static bool write_pid_file() {
    const QString runtime_dir =
        QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime_dir.isEmpty()) {
        spdlog::warn("XDG_RUNTIME_DIR not set, cannot write PID file");
        return false;
    }

    const QString pid_path = runtime_dir + "/deepin-anything-daemon.pid";
    QFile pid_file(pid_path);
    if (!pid_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        spdlog::warn("Failed to write PID file at {}: {}",
                     pid_path.toStdString(),
                     pid_file.errorString().toStdString());
        return false;
    }

    pid_file.write(QByteArray::number(QCoreApplication::applicationPid()));
    pid_file.close();
    spdlog::info("PID file written: {}", pid_path.toStdString());
    return true;
}

// 收到信号后调用, 运行在 Qt 事件循环中
gboolean on_sigint_sigterm(gpointer user_data) {
    spdlog::info("Interrupt signal ({}) received, quit", (const char *) user_data);
    remove_running_flag();
    set_app_restart(false);
    qApp->quit();

    return TRUE;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    // 日志必须最先接入 syslog：daemon 由 deepin-security-loader 启动时
    // stdout/stderr 会被 loader 关闭，写 stdout 的日志（默认 logger）会
    // 静默丢失。syslog 走 /dev/log unix socket，不依赖 stdout fd。作为
    // user unit 运行时 journald 会按 cgroup 打上 _SYSTEMD_USER_UNIT 字段，
    // 可用 `journalctl --user-unit deepin-anything-daemon` 过滤；级别
    // 映射到 journal 的 PRIORITY 字段。
    auto syslog_sink = std::make_shared<spdlog::sinks::syslog_sink_mt>(
        "deepin-anything-daemon", LOG_PID, LOG_DAEMON);
    spdlog::set_default_logger(
        std::make_shared<spdlog::logger>("anything", syslog_sink));

    // 尽早写入 PID 文件，通知 systemd Type=forking 主进程 PID。
    // 放在 g_bus_get_sync / request_dbus_trust 等可能阻塞的操作之前：
    // 这些操作若耗时长，systemd 会一直处于 starting 状态阻塞依赖单元。
    // PIDFile 只是告诉 systemd "主进程是这个 PID"，不等于服务已就绪
    // （那是 Type=notify + READY=1 的语义），提前写不影响状态正确性。
    write_pid_file();

    if (!can_user_login())
        exit(APP_QUIT_CODE);

    // 缓存系统总线连接：GLib 的 g_bus_get_sync() 以 singleton 方式缓存，
    // 只要存在至少一个引用，后续所有 g_bus_get_sync() 调用（trust_client、
    // relay_event_listener 等）都会复用同一连接和 unique name。
    // 这确保 SetAllowCaller 注册的 unique name 与 GetEventChannel 请求的
    // unique name 一致。连接在 main() 退出时自动释放。
    g_autoptr(GDBusConnection) system_bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
    if (system_bus == nullptr) {
        spdlog::error("Failed to connect to system bus");
        return APP_QUIT_CODE;
    }

    spdlog::info("Anything daemon starting...");
    // 打印版本号（如果在编译时定义了的话）
#ifdef DEEPIN_ANYTHING_VERSION
    spdlog::info("Deepin Anything version: {}", DEEPIN_ANYTHING_VERSION);
#endif
    // 打印commit号（如果在编译时定义了的话）
#ifdef DEEPIN_ANYTHING_COMMIT_HASH
    spdlog::info("Deepin Anything commit: {}", DEEPIN_ANYTHING_COMMIT_HASH);
#endif
    spdlog::info("Qt version: {}", qVersion());

    // 进程白名单协商：--fd1/--fd2 由 deepin-security-loader 注入（R1/R3）。
    // 非 loader 启动（无 fd 参数）时 parse_trust_fds 返回 false，行为与现状一致。
    // 信任协商先于 Config / default_event_handler 等重活。
    int request_fd = -1, response_fd = -1;
    if (parse_trust_fds(argc, argv, request_fd, response_fd)) {
        if (!request_dbus_trust("org.deepin.Anything",
                                "/org/deepin/Anything",
                                "org.deepin.Anything",
                                request_fd, response_fd)) {
            spdlog::error("DBus trust request rejected by security loader, quit");
            return APP_QUIT_CODE;
        }
    }

    Config config;
    event_handler_config event_handler_config = config.make_event_handler_config();

    spdlog::set_level(spdlog::level::from_str(config.get_log_level()));
    // 注意：syslog_sink 只输出消息本体，pattern 不生效；时间戳与级别由
    // journal 字段（__REALTIME_TIMESTAMP / PRIORITY）提供。

    set_volatile_index_dir(event_handler_config.volatile_index_dir);
    detect_last_time_quit_status();
    set_running_flag();

    print_event_handler_config(event_handler_config);
    default_event_handler handler(event_handler_config);
    // default_event_handler 实例化时, 可能会清空索引目录, 这里重新设置 running 标志
    set_running_flag();
    RelayEventListener *listener = relay_event_listener_new(on_event_received,
                                                              on_quit_requested,
                                                              &handler);
    config.set_config_change_handler([&handler, &config](std::string key) {
        spdlog::info("Config changed: {}", key);

        if (key == LOG_LEVEL_KEY) {
            spdlog::set_level(spdlog::level::from_str(config.get_log_level()));
            return;
        }

        auto new_config = config.make_event_handler_config();
        bool handled = handler.handle_config_change(key, new_config);

        if (handled) {
            spdlog::info("Config changes have been processed.");
        } else {
            handler.set_index_invalid_and_restart();
        }
    });

    if (!relay_event_listener_start(listener)) {
        spdlog::error("Failed to start relay event listener");
        relay_event_listener_free(listener);
        handler.terminate_filter();
        handler.terminate_processing();
        return APP_QUIT_CODE;
    }

    // Process the interrupt signal using g_unix_signal_add()
    guint sigint_id = g_unix_signal_add(SIGINT, on_sigint_sigterm, (gpointer)"SIGINT");
    guint sigterm_id = g_unix_signal_add(SIGTERM, on_sigint_sigterm, (gpointer)"SIGTERM");

    app.exec();

    // Clean up signal handlers
    if (sigint_id > 0)
        g_source_remove(sigint_id);
    if (sigterm_id > 0)
        g_source_remove(sigterm_id);

    spdlog::info("Performing cleanup tasks...");
    relay_event_listener_stop(listener);
    relay_event_listener_free(listener);
    handler.terminate_filter();
    handler.terminate_processing();

    spdlog::info("Anything daemon stopped.");
    return get_app_ret_code();
}
