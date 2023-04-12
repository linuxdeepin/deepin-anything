// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 - 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <signal.h>
#include <sys/utsname.h>

#include <QDBusConnection>

#include <DLog>

#include "anythingbackend.h"
#include "anythingexport.h"
#include "server.h"
#include "eventsource_genl.h"

#include "lftmanager.h"
#include "anything_adaptor.h"

DCORE_USE_NAMESPACE

DAS_BEGIN_NAMESPACE

Q_LOGGING_CATEGORY(lcBackend, "anything.backend", DEFAULT_MSG_TYPE)
#define backWarning(...) qCWarning(lcBackend, __VA_ARGS__)
#define backDebug(...) qCDebug(lcBackend, __VA_ARGS__)

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


static QString logFormat = "[%{time}{yyyy-MM-dd, HH:mm:ss.zzz}] [%{type:-7}] [%{file}=>%{function}: %{line}] %{message}\n";

AnythingBackend::~AnythingBackend()
{
    if (server && server->isRunning()) {
        server->terminate();
    }
}

AnythingBackend *AnythingBackend::instance()
{
    return _global_anybackend;
}

static void initLog()
{
    static std::once_flag flag;

    std::call_once(flag, []() {
        ConsoleAppender *consoleAppender = new ConsoleAppender;
        consoleAppender->setFormat(logFormat);

        RollingFileAppender *rollingFileAppender = new RollingFileAppender(LFTManager::cacheDir() + "/app.log");
        rollingFileAppender->setFormat(logFormat);
        rollingFileAppender->setLogFilesLimit(5);
        rollingFileAppender->setDatePattern(RollingFileAppender::DailyRollover);

        QStringList logCategoryList = LFTManager::logCategoryList() +
                                      Server::logCategoryList() +
                                      EventSource_GENL::logCategoryList();
        logCategoryList << lcBackend().categoryName();
        for (const QString &c : logCategoryList) {
            logger->registerCategoryAppender(c, consoleAppender);
            logger->registerCategoryAppender(c, rollingFileAppender);
        }
    });
}

int AnythingBackend::init_connection()noexcept
{
    if (hasconnected)
        return 0;
    initLog();
    if (backendRun() == 0 && monitorStart() == 0) {
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
            backWarning("Cannot register the \"com.deepin.anything\" service.\n");
            return 2;
        }
        Q_UNUSED(new AnythingAdaptor(LFTManager::instance()));
        if (!connection.registerObject("/com/deepin/anything", LFTManager::instance())) {
            backWarning("Cannot register to the D-Bus object: \"/com/deepin/anything\"\n");
            return 3;
        }
    }else{
        backDebug() << "deepin-anything-backend is running";
    }

    return 0;
}

DAS_END_NAMESPACE
