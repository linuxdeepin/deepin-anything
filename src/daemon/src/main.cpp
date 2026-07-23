// SPDX-FileCopyrightText: 2024 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unistd.h>
#include <pwd.h>

#include "anything.hpp"
#include "core/config.h"
#include "utils/running_flag.h"

#include <QCoreApplication>
#include <glib-unix.h>

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

    if (!can_user_login())
        exit(APP_QUIT_CODE);

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

    Config config;
    event_handler_config event_handler_config = config.make_event_handler_config();

    spdlog::set_level(spdlog::level::from_str(config.get_log_level()));
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [thread %t] %v");

    set_volatile_index_dir(event_handler_config.volatile_index_dir);
    detect_last_time_quit_status();
    set_running_flag();

    print_event_handler_config(event_handler_config);
    default_event_handler handler(event_handler_config);
    // default_event_handler 实例化时, 可能会清空索引目录, 这里重新设置 running 标志
    set_running_flag();
    EventListener *listener = event_listener_new(on_event_received,
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

    if (!event_listener_start(listener)) {
        spdlog::error("Failed to start event listener");
        event_listener_free(listener);
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
    event_listener_stop(listener);
    event_listener_free(listener);
    handler.terminate_filter();
    handler.terminate_processing();

    spdlog::info("Anything daemon stopped.");
    return get_app_ret_code();
}
