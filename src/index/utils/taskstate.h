// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TASKSTATE_H
#define TASKSTATE_H

#include "index_global.h"

#include <QMutex>
#include <QMutexLocker>
#include <QAtomicInteger>

ANYTHING_INDEX_BEGIN_NAMESPACE

class TaskState
{
public:
    TaskState() : m_running(false), m_pauseRequested(false) { }

    bool isRunning() const
    {
        return m_running.loadAcquire();
    }

    void start()
    {
        m_running.storeRelease(true);
    }

    void stop()
    {
        m_running.storeRelease(false);
    }

    void requestPause()
    {
        m_pauseRequested.storeRelease(true);
    }

    bool isPauseRequested() const
    {
        return m_pauseRequested.loadAcquire();
    }

    void clearPauseRequest()
    {
        m_pauseRequested.storeRelease(false);
    }

private:
    QAtomicInteger<bool> m_running;
    QAtomicInteger<bool> m_pauseRequested;
};

ANYTHING_INDEX_END_NAMESPACE

#endif   // TASKSTATE_H
