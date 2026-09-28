// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TASKQUEUEUTILS_H
#define TASKQUEUEUTILS_H

#include "index_global.h"
#include "taskmanager.h"

#include <QQueue>

ANYTHING_INDEX_BEGIN_NAMESPACE

namespace TaskQueueUtils {

struct DirectoryMovePlan
{
    QString fromPath;
    QString toPath;
};

QList<DirectoryMovePlan> buildDirectoryMovePlans(const QHash<QString, QString> &movedFiles);
bool rewriteQueuedTasksForDirectoryMove(QQueue<TaskQueueItem> &taskQueue,
                                        const QString &fromPath,
                                        const QString &toPath);

}   // namespace TaskQueueUtils

ANYTHING_INDEX_END_NAMESPACE

#endif   // TASKQUEUEUTILS_H
