// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 - 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server.h"

#include "vfs_change_consts.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QDebug>

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

    QByteArrayList create_list;
    QByteArrayList delete_list;
    QList<QPair<QByteArray, QByteArray>> rename_list;

    while (true) {
        if (eventsrc->getEvent(&action, &src, &dst, &end)) {
            switch(action) {
            case ACT_NEW_FILE:
            case ACT_NEW_SYMLINK:
            case ACT_NEW_LINK:
            case ACT_NEW_FOLDER:
                serverDebug("%s: %s", act_names[action], src);

                create_list << src;

                if (!delete_list.isEmpty()) {
                    emit fileDeleted(delete_list);
                    delete_list.clear();
                } else if (!rename_list.isEmpty()) {
                    emit fileRenamed(rename_list);
                    rename_list.clear();
                }

                break;
            case ACT_DEL_FILE:
            case ACT_DEL_FOLDER:
                serverDebug("%s: %s", act_names[action], src);

                delete_list << src;

                if (!create_list.isEmpty()) {
                    emit fileCreated(create_list);
                    create_list.clear();
                } else if (!rename_list.isEmpty()) {
                    emit fileRenamed(rename_list);
                    rename_list.clear();
                }

                break;
            case ACT_RENAME_FILE:
            case ACT_RENAME_FOLDER:
                serverDebug("%s: %s, %s", act_names[action], src, dst);

                rename_list << qMakePair(QByteArray(src), QByteArray(dst));

                if (!delete_list.isEmpty()) {
                    emit fileDeleted(delete_list);
                    delete_list.clear();
                } else if (!create_list.isEmpty()) {
                    emit fileCreated(create_list);
                    create_list.clear();
                }

                break;
            default:
                serverWarning("Unknow file action: %d", int(action));
                break;
            }

            if (end) {
                if (!delete_list.isEmpty()) {
                    emit fileDeleted(delete_list);
                    delete_list.clear();
                } else if (!create_list.isEmpty()) {
                    emit fileCreated(create_list);
                    create_list.clear();
                } else if (!rename_list.isEmpty()) {
                    emit fileRenamed(rename_list);
                    rename_list.clear();
                }
            }
        }
    }
}

DAS_END_NAMESPACE
