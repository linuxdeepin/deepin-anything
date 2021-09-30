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

#include <sys/utsname.h>

#include <QCoreApplication>
#include <QThread>
#include <QLoggingCategory>
#include <QFile>

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

void removePlugins(const QStringList &keys, Server *server)
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

enum WriteMountInfoError
{
    Success = 0,
    UnameFail,
    UnrecognizedVersion,
    OpenSrcFileFail,
    OpenDstFileFail,
    WriteDstFileFail
};

// write mountinfo for vfs_monitor when kernel version >= 5.10
WriteMountInfoError writeMountInfo()
{
    struct utsname uts;
    if (uname(&uts) != 0) {
        qWarning() << "uname fail";
        return WriteMountInfoError::UnameFail;
    }
    qDebug() << "the kernel version: " << uts.release;

    QStringList ver_list = QString(uts.release).split(".");
    if (ver_list.size() < 3) {
        qWarning() << "unrecognized version format, expect x.y.z";
        return WriteMountInfoError::UnrecognizedVersion;
    }
    int ver_x = ver_list[0].toInt();
    int ver_y = ver_list[1].toInt();

    // write when version >= 5.10
    if (ver_x >= 6 || (5 == ver_x && ver_y >= 10)) {
        QString file_mountinfo_path("/proc/self/mountinfo");
        QFile file_mountinfo(file_mountinfo_path);
        if (!file_mountinfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "open file " << file_mountinfo_path << " failed";
            return WriteMountInfoError::OpenSrcFileFail;
        }
        QByteArray mount_info;
        mount_info = file_mountinfo.readAll();
        file_mountinfo.close();

        // driver_set_info is created by vfs_monitor and be used to receive mount information
        QString file_drv_path("/dev/driver_set_info");
        QFile file_drv(file_drv_path);
        if (!file_drv.open(QIODevice::WriteOnly | QIODevice::Text)) {
            qWarning() << "open file " << file_drv_path << " failed";
            return WriteMountInfoError::OpenDstFileFail;
        }
        if (file_drv.write(mount_info.data(), mount_info.size()) != mount_info.size()) {
            qWarning() << "write file " << file_drv_path << " failed";
            return WriteMountInfoError::WriteDstFileFail;
        }
        file_drv.close();

        qDebug() << "write mountinfo success";
    }

    return WriteMountInfoError::Success;
}

int main(int argc, char *argv[])
{
    qSetMessagePattern("[%{time yyyy-MM-dd, HH:mm:ss.zzz}] [%{category}-%{type}] [%{function}: %{line}]: %{message}");

    writeMountInfo();

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
        removePlugins(keys, server);
        DASFactory::loader()->removeLoader(loader);
    });

    QObject::connect(DASFactory::loader(), &DASPluginLoader::pluginModified, [server] (QPluginLoader *loader, const QStringList &keys) {
        removePlugins(keys, server);
        loader = DASFactory::loader()->reloadLoader(loader);

        if (loader) {
            for (const QString &key : DASFactory::loader()->getKeysByLoader(loader)) {
                addPlugin(key, server);
            }
        }
    });

    QObject::connect(DASFactory::loader(), &DASPluginLoader::pluginAdded, server, [server] (const QString &key) {
        addPlugin(key, server);
    });

    server->start();

    return app.exec();
}
