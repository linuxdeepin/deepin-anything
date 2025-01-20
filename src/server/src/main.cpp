// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QCoreApplication>

#include "anything.hpp"

using namespace anything;

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

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
    set_signal_handler(SIGINT, [&listenser, &handler, &app](int sig) {
        spdlog::info("Interrupt signal ({}) received.", sig);
        spdlog::info("Performing cleanup tasks...");
        listenser.stop_listening();
        handler.terminate_processing();
        app.exit();
    });

    listenser.async_listen();
    app.exec();
}