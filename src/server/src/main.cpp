// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unistd.h>
#include <pwd.h>
#include <QCoreApplication>

#include "anything.hpp"

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

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    if (!can_user_login())
        exit(0);

    // spdlog::set_default_logger(spdlog::basic_logger_mt("file_logger", "/var/cache/deepin/deepin-anything/app.log"));
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [thread %t] %v");
    spdlog::info("Qt version: {}", qVersion());

    event_listenser listenser;
    default_event_handler handler;
    listenser.set_handler([&handler](fs_event event) {
        handler.handle(std::move(event));
    });

    // Process the interrupt signal
    auto signalHandler = [&listenser, &handler, &app](int sig) {
        spdlog::info("Interrupt signal ({}) received.", sig);
        spdlog::info("Performing cleanup tasks...");
        listenser.stop_listening();
        handler.terminate_processing();
        app.exit();
    };
    set_signal_handler(SIGINT, signalHandler);
    set_signal_handler(SIGTERM, signalHandler);

    listenser.async_listen();
    app.exec();
}
