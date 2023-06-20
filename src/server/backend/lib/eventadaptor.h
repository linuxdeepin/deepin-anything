// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef EVENTADAPTOR_H
#define EVENTADAPTOR_H

#include "dasdefine.h"

#include <QObject>
#include <QTimer>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <QThread>

DAS_BEGIN_NAMESPACE

//定义一个函数指针
typedef void(*OnHandleEvent)(QList<QPair<QByteArray, QByteArray>> &actionList);

#define INSERT_ACTION "insert:"
#define REMOVE_ACTION "remove:"

class EventAdaptor : public QObject
{
    Q_OBJECT
public:
    explicit EventAdaptor(QObject *parent = nullptr);
    ~EventAdaptor();
    void pushEvent(QPair<QByteArray, QByteArray> &action);
    bool popEvent(QPair<QByteArray, QByteArray> *action);

public slots:
    void startWork();
    void handleTaskFinish();

public:
    OnHandleEvent onHandler;

private slots:
    void onHandleEvent();

private:
    bool ignoreAction(QByteArray &strArr, bool ignored);

private:
    QMutex mutex;
    QWaitCondition waitCondition;
    QQueue<QPair<QByteArray, QByteArray>> action_buffers;
    QTimer handle_timer;
    // 用于事件更新，串行进行
    bool jobFinished = true;
};

class TaskThread : public QThread
{
    Q_OBJECT
public:
    explicit TaskThread(QObject *parent = nullptr) : QThread(parent) {}
    ~TaskThread() override {
        handleFunc = nullptr;
        actionList.clear();
        deleteLater();
    }
    void run() override
    {
        // 后台回调处理事件，更新索引
        if (handleFunc)
            handleFunc(actionList);

        emit workFinished();
    }

public slots:
    void setData(OnHandleEvent callbackFunc, QList<QPair<QByteArray, QByteArray>> &actions)
    {
        handleFunc = callbackFunc;
        actionList = actions;
    }

signals:
    void workFinished();

private:
    OnHandleEvent handleFunc = nullptr;
    QList<QPair<QByteArray, QByteArray>> actionList;
};

DAS_END_NAMESPACE

#endif // EVENTADAPTOR_H
