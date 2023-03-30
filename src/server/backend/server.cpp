// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 - 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server.h"

#include "vfs_change_consts.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QDebug>
#include <QDir>
#include <QStorageInfo>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

DAS_BEGIN_NAMESPACE

static const char* act_names[] = {"file_created", "link_created", "symlink_created", "dir_created", "file_deleted", "dir_deleted", "file_renamed", "dir_renamed"};

Q_LOGGING_CATEGORY(lcServer, "anything.monitor.server", DEFAULT_MSG_TYPE)
#define serverWarning(...) qCWarning(lcServer, __VA_ARGS__)
#define serverDebug(...) qCDebug(lcServer, __VA_ARGS__)

Server::Server(EventSource *eventsrc, QObject *parent)
    : QThread(parent)
    , eventsrc(eventsrc)
{
    qRegisterMetaType<QList<QPair<QByteArray, QByteArray>>>();
}

QStringList Server::logCategoryList()
{
    QStringList list;

    list << lcServer().categoryName();

    return list;
}

void Server::run()
{
    unsigned char action;
    char *src, *dst;
    bool end;

    // 初始化事件列表和忽略标记
    delete_list.clear();
    rename_list.clear();
    create_list.clear();
    mark_ignore = false;

    while (true) {
        if (eventsrc->getEvent(&action, &src, &dst, &end)) {
            // 在支持长文件名的目录，一个动作底层会发出两次事件：一次原有文件系统的变更；一次长文件名系统dlnfs的变更。
            // 忽略第一次原有文件系统事件
            QString strSrc = QString(src);
            if (ignoreAction(strSrc))
                continue;

            switch(action) {
            case ACT_NEW_FILE:
            case ACT_NEW_SYMLINK:
            case ACT_NEW_LINK:
            case ACT_NEW_FOLDER:
                serverDebug("%s: %s", act_names[action], src);

                create_list << src;
                break;
            case ACT_DEL_FILE:
            case ACT_DEL_FOLDER:
                serverDebug("%s: %s", act_names[action], src);

                delete_list << src;
                break;
            case ACT_RENAME_FILE:
            case ACT_RENAME_FOLDER:
                serverDebug("%s: %s, %s", act_names[action], src, dst);

                rename_list << qMakePair(QByteArray(src), QByteArray(dst));
                break;
            default:
                serverWarning("Unknow file action: %d", int(action));
                break;
            }

            if (end) {
                notifyChanged();
            }
        }
    }
}

void Server::notifyChanged()
{
    if (!delete_list.isEmpty()) {
        emit fileDeleted(delete_list);
        delete_list.clear();
    }
    if (!create_list.isEmpty()) {
        emit fileCreated(create_list);
        create_list.clear();
    }
    if (!rename_list.isEmpty()) {
        emit fileRenamed(rename_list);
        rename_list.clear();
    }
    mark_ignore = false;
}

bool Server::ignoreAction(QString &strSrc)
{
    if (strSrc.endsWith(".longname")) {
        serverDebug() << "this is longname file, ignore. " << strSrc;
        return true;
    }

    //没有标记忽略前一条，则检查是否长文件目录
    if (!mark_ignore) {
        QDir path_dir(strSrc);

        // 向上找到一个当前文件存在的路径
        while (!path_dir.exists()) {
            if (!path_dir.cdUp())
                break;
        }

        QStorageInfo storage_info(path_dir);
        if (storage_info.fileSystemType().startsWith("fuse.dlnfs")) {
            // 长文件目录，标记此条事件被忽略
            serverDebug() << "ignore first action in dlnfs dir:" << strSrc;
            mark_ignore = true;
            return true;;
        }
    }
    return false;
}

DAS_END_NAMESPACE
