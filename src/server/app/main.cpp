// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "service_manager.h"
#include "event_listenser.h"
#include "lib/logsaver.h"
#include "lib/lftmanager.h"
#include <QCoreApplication>


int main() {
    anything::service_manager manager;
    auto ret = manager.register_service("com.deepin.anything");
    if (!ret) {
        std::cerr << "Failed to register service\n";
        return -1;
    }

    std::cout << "register service succeed\n";

    using namespace deepin_anything_server;
    LogSaver::instance()->setlogFilePath(LFTManager::cacheDir());
    LogSaver::instance()->installMessageHandler();

    anything::event_listenser listenser;
    listenser.listen();
}