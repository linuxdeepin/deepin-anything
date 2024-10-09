// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 - 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server.h"

#include "vfs_change_consts.h"
#include "logdefine.h"

#include <QCoreApplication>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

DAS_BEGIN_NAMESPACE

static const char* act_names[] = {"file_created", "link_created", "symlink_created", "dir_created", "file_deleted", "dir_deleted", "file_renamed", "dir_renamed"};

Q_LOGGING_CATEGORY(logC, "anything.changes.server", DEFAULT_MSG_TYPE)

Server::Server(EventSource *eventsrc, QObject *parent)
    : QThread(parent)
    , eventsrc(eventsrc)
{
    qRegisterMetaType<QList<QPair<QByteArray, QByteArray>>>();
    qDebug() << "dxnu-test-log server ctor";
}

void Server::setEventAdaptor(EventAdaptor *adaptor)
{
    eventAdaptor = adaptor;
}

void Server::run()
{
    unsigned char action;
    char *src, *dst;
    bool end;
    cDebug("dxnu-test-log server run()");
    qDebug() << "dxnu-test-log server run()";

    while (true) {
        QPair<QByteArray, QByteArray> actionPair;
        if (eventsrc->getEvent(&action, &src, &dst, &end)) {
            // 如果处理耗时，可能导致部分事件（极短时间大量事件）丢失，因为netlink丢掉
            // 所以这里只做事件入队列
            cDebug("dxnu-test-log information, action:%s", act_names[action]);
            // qDebug() << "dxnu-test-log information, action: " << act_names[action];

            switch(action) {
            case ACT_NEW_FILE:
            case ACT_NEW_SYMLINK:
            case ACT_NEW_LINK:
            case ACT_NEW_FOLDER:
                cDebug("%s: %s", act_names[action], src);
                qDebug() << "dxnu-test-log information: [action:" << act_names[action] << ", src:" << src << "]";
                actionPair = qMakePair(QByteArray(INSERT_ACTION), QByteArray(src));
                break;
            case ACT_DEL_FILE:
            case ACT_DEL_FOLDER:
                cDebug("%s: %s", act_names[action], src);
                qDebug() << "dxnu-test-log information: [action:" << act_names[action] << ", src:" << src << "]";
                actionPair = qMakePair(QByteArray(REMOVE_ACTION), QByteArray(src));
                break;
            case ACT_RENAME_FILE:
            case ACT_RENAME_FOLDER:
                cDebug("%s: %s, %s", act_names[action], src, dst);
                qDebug() << "dxnu-test-log information: [action:" << act_names[action] << ", src:" << src << ", dst: " << dst << "]";
                actionPair = qMakePair(QByteArray(src), QByteArray(dst));
                break;
            default:
                nWarning("Unknow file action: %d", int(action));
                qDebug() << "dxnu-test-log information: [action:" << act_names[action] << "]";
                break;
            }

            if (eventAdaptor) {
                eventAdaptor->pushEvent(actionPair);
            }
        }
    }
}

DAS_END_NAMESPACE
