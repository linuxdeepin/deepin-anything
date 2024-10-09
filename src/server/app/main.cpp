// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "service_manager.h"
#include "event_listenser.h"
#include "lib/logsaver.h"
#include "lib/lftmanager.h"


int main(int argc, char *argv[]) {
    anything::service_manager manager;
    auto ret = manager.register_service("com.deepin.anything");

    using namespace deepin_anything_server;
    LogSaver::instance()->setlogFilePath(LFTManager::cacheDir());
    LogSaver::instance()->installMessageHandler();

    anything::event_listenser listenser;
    listenser.listen();
}