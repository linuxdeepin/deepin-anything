// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 - 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <signal.h>
#include <sys/utsname.h>

#include <QDBusConnection>

#include "anythingbackend.h"
#include "anythingexport.h"
#include "server.h"
#include "eventsource_genl.h"
#include "logdefine.h"
#include "logsaver.h"

#include "lftmanager.h"
#include "anything_adaptor.h"

DAS_BEGIN_NAMESPACE

class _AnythingBackend : public AnythingBackend {};
Q_GLOBAL_STATIC(_AnythingBackend, _global_anybackend)

extern "C" ANYTHINGBACKEND_SHARED_EXPORT int fireAnything()
{
    AnythingBackend *backend = AnythingBackend::instance();
    if (backend) {
        return backend->init_connection();
    }
    return -1;
}

AnythingBackend::~AnythingBackend()
{
    if (server && server->isRunning()) {
        server->terminate();
    }
    LogSaver::instance()->uninstallMessageHandler();
}

AnythingBackend *AnythingBackend::instance()
{
    return _global_anybackend;
}

static void initLog()
{
    static std::once_flag flag;

    std::call_once(flag, []() {
        // 设置保存路径并开始记录
        LogSaver::instance()->setlogFilePath(LFTManager::cacheDir());
        LogSaver::instance()->installMessageHandler();
    });
}

int AnythingBackend::init_connection()noexcept
{
    if (hasconnected)
        return 0;

    if (backendRun() == 0 && monitorStart() == 0) {
        initLog();
        hasconnected = true;
        return 0;
    }
    return -1;
}

int AnythingBackend::monitorStart()
{
    if (!eventsrc)
        eventsrc = new EventSource_GENL();

    if (!eventsrc->isInited() && !eventsrc->init())
        return -1;

    if (!server)
        server = new Server(eventsrc);

    if (server && !server->isRunning()) {
        EventAdaptor *adaptor = new EventAdaptor();
        adaptor->onHandler = LFTManager::onFileChanged;
        server->setEventAdaptor(adaptor);

        server->start();
    }
    return 0;
}

int AnythingBackend::backendRun()
{
    const QString anythingServicePath = "com.deepin.anything";

    QDBusConnection connection = QDBusConnection::systemBus();
    if (!connection.interface()->isServiceRegistered(anythingServicePath)) {
        bool reg_result = connection.registerService(anythingServicePath);
        if (!reg_result) {
            qWarning() << "Cannot register the \"com.deepin.anything\" service.";
            return 2;
        }
        Q_UNUSED(new AnythingAdaptor(LFTManager::instance()));
        if (!connection.registerObject("/com/deepin/anything", LFTManager::instance())) {
            qWarning() << "Cannot register to the D-Bus object: \"/com/deepin/anything\"";
            return 3;
        }
    }else{
        qDebug() << "deepin-anything-backend is running";
    }

    return 0;
}

DAS_END_NAMESPACE
