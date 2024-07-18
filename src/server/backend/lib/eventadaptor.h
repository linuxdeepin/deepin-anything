// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef EVENTADAPTOR_H
#define EVENTADAPTOR_H

#include "dasdefine.h"

#include <QObject>
#include <QTimer>
#include <QMutex>
#include <QQueue>

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

public:
    OnHandleEvent onHandler;

private slots:
    void onHandleEvent();

private:
    bool ignoreAction(QByteArray &strArr, bool ignored);

private:
    QMutex mutex;
    QQueue<QPair<QByteArray, QByteArray>> action_buffers;
    QTimer handle_timer;
};

DAS_END_NAMESPACE

#endif // EVENTADAPTOR_H
