// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QCoreApplication>

#include "anything.hpp"

using namespace anything;

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    log::set_level(log::level::all, true);
    // log::set_to_file("/data/home/dxnu/.cache/findex/findex.log");

    event_listenser listenser;
    default_event_handler handler;
    listenser.set_handler([&handler](fs_event event) {
        handler.handle(std::move(event));
    });

    // Process the interrupt signal
    set_signal_handler(SIGINT, [&listenser, &handler, &app](int sig) {
        log::info() << "Interrupt signal (" << sig << ") received.\n";
        log::info() << "Performing cleanup tasks...\n";
        listenser.stop_listening();
        handler.terminate_processing();
        app.exit();
    });

    listenser.async_listen();
    app.exec();
}