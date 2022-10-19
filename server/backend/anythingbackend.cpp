// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <signal.h>
#include <sys/utsname.h>

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusConnectionInterface>

#include <DLog>

#include "anythingbackend.h"
#include "anythingexport.h"
#include "server.h"
#include "dasfactory.h"
#include "dasinterface.h"
#include "daspluginloader.h"
#include "eventsource_genl.h"

#include "lftmanager.h"
#include "anything_adaptor.h"

DCORE_USE_NAMESPACE

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


static QList<QPair<QString, DASInterface*>> interfaceList;
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

void AnythingBackend::addPlugin(const QString &key, Server *server)
{
    DASInterface *interface = DASFactory::create(key);

    if (!interface) {
        qWarning() << "interface is null, key:" << key;
        return;
    }

    QThread *t = new QThread(interface);

    interface->moveToThread(t);
    t->start();

    interfaceList << qMakePair(key, interface);

    QObject::connect(server, &Server::fileCreated, interface, &DASInterface::onFileCreate);
    QObject::connect(server, &Server::fileDeleted, interface, &DASInterface::onFileDelete);
    QObject::connect(server, &Server::fileRenamed, interface, &DASInterface::onFileRename);
}

void AnythingBackend::removePlugins(const QStringList &keys, Server *server)
{
    for (int i = 0; i < interfaceList.count(); ++i) {
        const QPair<QString, DASInterface*> &value = interfaceList.at(i);

        if (!keys.contains(value.first))
            continue;

        QThread *t = value.second->thread();

        t->quit();

        if (!t->wait()) {
            qWarning() << "failed on wait thread to quit, key:" << value.first;
            continue;
        }

        interfaceList.removeAt(i);
        --i;
        server->disconnect(value.second);
        value.second->deleteLater();
    }
}

int AnythingBackend::init_connection()noexcept
{
    if (hasconnected)
        return 0;
    if (backendRun() == 0 && monitorStart() == 0) {
        hasconnected = true;
        return 0;
    }
    return -1;
}

int AnythingBackend::monitorStart()
{
    qSetMessagePattern("[%{time yyyy-MM-dd, HH:mm:ss.zzz}] [%{category}-%{type}] [%{function}: %{line}]: %{message}");

#ifdef QT_NO_DEBUG
    QLoggingCategory::setFilterRules("vfs.info=false");
#endif

    if (!eventsrc)
        eventsrc = new EventSource_GENL();

    if (!eventsrc->isInited() && !eventsrc->init())
        return -1;

    if (!server)
        server = new Server(eventsrc);

    if (server && !server->isRunning()) {
        // init plugins
        for (const QString &key : DASFactory::keys()) {
            addPlugin(key, server);
        }

        QObject::connect(DASFactory::loader(), &DASPluginLoader::pluginRemoved, [this] (QPluginLoader *loader, const QStringList &keys) {
            removePlugins(keys, server);
            DASFactory::loader()->removeLoader(loader);
        });

        QObject::connect(DASFactory::loader(), &DASPluginLoader::pluginModified, [this] (QPluginLoader *loader, const QStringList &keys) {
            removePlugins(keys, server);
            loader = DASFactory::loader()->reloadLoader(loader);

            if (loader) {
                for (const QString &key : DASFactory::loader()->getKeysByLoader(loader)) {
                    addPlugin(key, server);
                }
            }
        });

        QObject::connect(DASFactory::loader(), &DASPluginLoader::pluginAdded, server, [this] (const QString &key) {
            addPlugin(key, server);
        });

        server->start();
    }
    return 0;
}

int AnythingBackend::backendRun()
{
    const QString anythingServicePath = "com.deepin.anything";
    // init log
    ConsoleAppender *consoleAppender = new ConsoleAppender;
    consoleAppender->setFormat(logFormat);

    RollingFileAppender *rollingFileAppender = new RollingFileAppender(LFTManager::cacheDir() + "/app.log");
    rollingFileAppender->setFormat(logFormat);
    rollingFileAppender->setLogFilesLimit(5);
    rollingFileAppender->setDatePattern(RollingFileAppender::DailyRollover);

    logger->registerAppender(consoleAppender);
    logger->registerAppender(rollingFileAppender);

    for (const QString &c : LFTManager::logCategoryList()) {
        logger->registerCategoryAppender(c, consoleAppender);
        logger->registerCategoryAppender(c, rollingFileAppender);
    }

     QDBusConnection connection = QDBusConnection::systemBus();
     if (!connection.interface()->isServiceRegistered(anythingServicePath)) {
         bool reg_result = connection.registerService(anythingServicePath);
         if (!reg_result) {
             qWarning("Cannot register the \"com.deepin.anything\" service.\n");
             return 2;
         }
         Q_UNUSED(new AnythingAdaptor(LFTManager::instance()));
         if (!connection.registerObject("/com/deepin/anything", LFTManager::instance())) {
             qWarning("Cannot register to the D-Bus object: \"/com/deepin/anything\"\n");
             return 3;
         }
     }else{
         qDebug() << "deepin-anything-backend is running";
     }

    return 0;
}

DAS_END_NAMESPACE
