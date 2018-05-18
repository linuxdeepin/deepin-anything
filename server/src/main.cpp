/*
 * Copyright (C) 2017 ~ 2018 Deepin Technology Co., Ltd.
 *
 * Author:     zccrs <zccrs@live.com>
 *
 * Maintainer: zccrs <zhangjide@deepin.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QCoreApplication>
#include <QThread>
#include <QLoggingCategory>

#include "server.h"
#include "dasfactory.h"
#include "dasinterface.h"
#include "daspluginloader.h"

using namespace DAS_NAMESPACE;

static QList<QPair<QString, DASInterface*>> interfaceList;

void addPlugin(const QString &key, Server *server)
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

int main(int argc, char *argv[])
{
    qSetMessagePattern("[%{time yyyy-MM-dd, HH:mm:ss.zzz}] [%{category}-%{type}] [%{function}: %{line}]: %{message}");

    QCoreApplication app(argc, argv);

#ifdef QT_NO_DEBUG
    QLoggingCategory::setFilterRules("vfs.info=false");
#endif

    Server *server = new Server();

    // init plugins
    for (const QString &key : DASFactory::keys()) {
        addPlugin(key, server);
    }

    QObject::connect(DASFactory::loader(), &DASPluginLoader::pluginRemoved, [server] (QPluginLoader *loader, const QStringList &keys) {
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

            DASFactory::loader()->removeLoader(loader);
        }
    });

    QObject::connect(DASFactory::loader(), &DASPluginLoader::pluginAdded, server, [server] (const QString &key) {
        addPlugin(key, server);
    });

    server->start();

    return app.exec();
}
