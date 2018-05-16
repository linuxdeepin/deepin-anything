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

int main(int argc, char *argv[])
{
    qSetMessagePattern("[%{time yyyy-MM-dd, HH:mm:ss.zzz}] [%{category}-%{type}] [%{function}: %{line}]: %{message}");

    QCoreApplication app(argc, argv);

#ifdef QT_NO_DEBUG
    QLoggingCategory::setFilterRules("vfs.info=false");
#endif

    DAS_NAMESPACE::Server *server = new DAS_NAMESPACE::Server();

    // init plugins
    for (const QString &key : DAS_NAMESPACE::DASFactory::keys()) {
        DAS_NAMESPACE::DASInterface *interface = DAS_NAMESPACE::DASFactory::create(key);

        if (!interface)
            continue;

        QThread *t = new QThread(interface);

        interface->moveToThread(t);
        t->start();

        QObject::connect(server, &DAS_NAMESPACE::Server::fileCreated, interface, &DAS_NAMESPACE::DASInterface::onFileCreate);
        QObject::connect(server, &DAS_NAMESPACE::Server::fileDeleted, interface, &DAS_NAMESPACE::DASInterface::onFileDelete);
        QObject::connect(server, &DAS_NAMESPACE::Server::fileRenamed, interface, &DAS_NAMESPACE::DASInterface::onFileRename);
    }

    server->start();

    return app.exec();
}
