// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unistd.h>
#include <pwd.h>
#include <QCoreApplication>
#include <QTimer>

#include "anything.hpp"
#include "core/config.h"

using namespace anything;

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

void setup_kernel_module_alive_check(QTimer &timer) {
    // 创建 qtimer 定时检查 /sys/kernel/vfs_monitor 的 inode 是否发生变化
    std::string path = "/sys/kernel/vfs_monitor";
    struct stat st_begin;
    if (lstat(path.c_str(), &st_begin) != 0) {
        spdlog::error("Check {} failed: {}", path, strerror(errno));
        exit(APP_QUIT_CODE);
    }

    QObject::connect(&timer, &QTimer::timeout, [path, st_begin]() {
        struct stat st_current;
        // when system reboot, the file may be deleted before we quit
        // so we not quit or restart, we wait stop command from systemd or the file appear again
        if (lstat(path.c_str(), &st_current) != 0) {
            return;
        }

        if (st_current.st_ino != st_begin.st_ino) {
            spdlog::info("File {} inode changed, restart", path);
            set_app_restart(true);
            qApp->quit();
        }
    });
    timer.setInterval(3000);
    timer.start();
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    if (!can_user_login())
        exit(APP_QUIT_CODE);

    spdlog::info("Anything daemon starting...");
    spdlog::info("Qt version: {}", qVersion());

    Config config;

    spdlog::set_level(spdlog::level::from_str(config.get_log_level()));
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [thread %t] %v");

    event_listenser listenser;
    auto event_handler_config = config.make_event_handler_config();
    print_event_handler_config(*event_handler_config);
    default_event_handler handler(event_handler_config);
    listenser.set_handler([&handler](fs_event *event) {
        handler.handle(event);
    });
    config.set_config_change_handler([&handler, &config](std::string key) {
        spdlog::info("Config changed: {}", key);
        if (key == LOG_LEVEL_KEY) {
            spdlog::set_level(spdlog::level::from_str(config.get_log_level()));
        } else {
            handler.notify_config_changed();
        }
    });

    // Process the interrupt signal
    auto signalHandler = [&app](int sig) {
        spdlog::info("Interrupt signal ({}) received, quit", sig);
        set_app_restart(false);
        app.quit();
    };
    set_signal_handler(SIGINT, signalHandler);
    set_signal_handler(SIGTERM, signalHandler);

    QTimer timer;
    setup_kernel_module_alive_check(timer);

    listenser.async_listen();
    app.exec();

    spdlog::info("Performing cleanup tasks...");
    listenser.stop_listening();
    handler.terminate_filter();
    handler.terminate_processing();

    spdlog::info("Anything daemon stopped.");
    return get_app_ret_code();
}
