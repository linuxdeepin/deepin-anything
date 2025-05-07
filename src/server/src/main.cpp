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
        QString uidStr;
        uidStr.setNum(uid);
        spdlog::warn("User can not login: {}", uidStr.toStdString());
        return false;
    }

    return true;
}

void setup_kernel_module_alive_check(QTimer &timer) {
    // 创建 qtimer 定时检查 /sys/kernel/vfs_monitor 的 inode 是否发生变化
    std::string path = "/sys/kernel/vfs_monitor";
    struct stat st_begin;
    if (stat(path.c_str(), &st_begin) != 0) {
        spdlog::error("Check {} failed: {}", path, strerror(errno));
        exit(1);
    }

    QObject::connect(&timer, &QTimer::timeout, [path, st_begin]() {
        struct stat st_current;
        if (stat(path.c_str(), &st_current) != 0) {
            spdlog::error("Check {} failed: {}", path, strerror(errno));
            qApp->quit();
        }

        if (st_current.st_ino != st_begin.st_ino) {
            spdlog::info("File {} inode changed", path);
            qApp->quit();
        }
    });
    timer.setInterval(3000);
    timer.start();
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    if (!can_user_login())
        exit(0);

    // spdlog::set_default_logger(spdlog::basic_logger_mt("file_logger", "/var/cache/deepin/deepin-anything/app.log"));
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [thread %t] %v");
    spdlog::info("Anything daemon starting...");
    spdlog::info("Qt version: {}", qVersion());

    Config config;
    event_listenser listenser;
    default_event_handler handler(config.make_event_handler_config());
    listenser.set_handler([&handler](fs_event event) {
        handler.handle(std::move(event));
    });

    // Process the interrupt signal
    auto signalHandler = [&listenser, &handler, &app](int sig) {
        spdlog::info("Interrupt signal ({}) received.", sig);
        app.quit();
    };
    set_signal_handler(SIGINT, signalHandler);
    set_signal_handler(SIGTERM, signalHandler);

    QTimer timer;
    setup_kernel_module_alive_check(timer);

    listenser.async_listen();
    int ret = app.exec();

    spdlog::info("Performing cleanup tasks...");
    listenser.stop_listening();
    handler.terminate_processing();

    spdlog::info("Anything daemon stopped.");
    return ret;
}
