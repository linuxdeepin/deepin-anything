// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "eventadaptor.h"
#include "mountcacher.h"

DAS_BEGIN_NAMESPACE

#define MAX_EVENT_HANDLE_COUNT_PER_CYCLE 100
#define EVENT_HANDLE_CYCLE 500

EventAdaptor::EventAdaptor(QObject *parent)
    : QObject(parent)
{
    connect(&handle_timer, &QTimer::timeout, this, &EventAdaptor::onHandleEvent);
    handle_timer.setInterval(EVENT_HANDLE_CYCLE);
    handle_timer.start();
}

EventAdaptor::~EventAdaptor()
{
    handle_timer.stop();
}

void EventAdaptor::pushEvent(QPair<QByteArray, QByteArray> &action)
{
    QMutexLocker locker(&mutex);
    action_buffers.enqueue(action);
}

bool EventAdaptor::popEvent(QPair<QByteArray, QByteArray> *action)
{
    QMutexLocker locker(&mutex);
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
    int event_count = 0;
    int new_interval;

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
    } while (pop && ++event_count <= MAX_EVENT_HANDLE_COUNT_PER_CYCLE);

    new_interval = pop ? 0 : EVENT_HANDLE_CYCLE;
    if (new_interval != handle_timer.interval()) {
        handle_timer.setInterval(new_interval);
    }

    if (!tmpActions.isEmpty()) {
        onHandler(tmpActions);
    }
}

bool EventAdaptor::ignoreAction(QByteArray &strArr, bool ignored)
{
    QString strPath = QString::fromLocal8Bit(strArr);
    if (strPath.endsWith(".longname")) {
        // 长文件名记录文件，直接忽略
        return true;
    }

    //没有标记忽略前一条，则检查是否长文件目录
    if (!ignored) {
        // 向上找到一个当前文件的挂载点且匹配文件系统类型
        if (MountCacher::instance()->pathMatchType(strPath, "fuse.dlnfs")) {
            // 长文件目录，标记此条事件被忽略
            return true;
        }
    }
    return false;
}

DAS_END_NAMESPACE
