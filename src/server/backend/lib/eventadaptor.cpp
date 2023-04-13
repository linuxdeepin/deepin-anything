// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "eventadaptor.h"

#include <QDebug>
#include <QDir>
#include <QStorageInfo>

EventAdaptor::EventAdaptor(QObject *parent)
    : QObject(parent)
{
    action_buffers.clear();

    connect(&handle_timer, &QTimer::timeout, this, &EventAdaptor::onHandleEvent, Qt::QueuedConnection);
    // 每500ms 触发一次事件
    handle_timer.setInterval(500);
    handle_timer.start();
}

EventAdaptor::~EventAdaptor()
{
    handle_timer.stop();
}

void EventAdaptor::pushEvent(QPair<QByteArray, QByteArray> &action)
{
    action_buffers.enqueue(action);
}

bool EventAdaptor::popEvent(QPair<QByteArray, QByteArray> *action)
{
    if (action_buffers.isEmpty()) {
        return false;
    }

    *action = action_buffers.dequeue();
    return true;
}

void EventAdaptor::onHandleEvent()
{
    bool pop = false;
    QList<QPair<QByteArray, QByteArray>> tmpActions;
    bool ignored = false;
    do {
        QPair<QByteArray, QByteArray> topAction;
        pop = popEvent(&topAction);
        if (pop) {
            // 在支持长文件名的目录，一个动作底层会发出两次事件：一次原有文件系统的变更；一次长文件名系统dlnfs的变更。
            // 忽略第一次原有文件系统事件
            ignored = ignoreAction(topAction.second, ignored);
            if (!ignored) {
                // 不应该被忽略或前一条已被忽略
                tmpActions.append(topAction);
            }
        }
    } while (pop);

    if (!tmpActions.isEmpty()) {
        onHandler(tmpActions);
    }
}


bool EventAdaptor::ignoreAction(QByteArray &strArr, bool ignored)
{
    QString strPath = QString::fromUtf8(strArr);
    if (strPath.endsWith(".longname")) {
        // 长文件名记录文件，直接忽略
        return true;
    }

    //没有标记忽略前一条，则检查是否长文件目录
    if (!ignored) {
        QDir path_dir(strPath);

        // 向上找到一个当前文件存在的路径
        while (!path_dir.exists()) {
            if (!path_dir.cdUp())
                break;
        }

        QStorageInfo storage_info(path_dir);
        if (storage_info.fileSystemType().startsWith("fuse.dlnfs")) {
            // 长文件目录，标记此条事件被忽略
            return true;;
        }
    }
    return false;
}
