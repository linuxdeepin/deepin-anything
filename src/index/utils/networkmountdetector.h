// SPDX-FileCopyrightText: 2026 Uniontech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NETWORKMOUNTDETECTOR_H
#define NETWORKMOUNTDETECTOR_H

#include "index_global.h"

#include <QString>
#include <QSet>
#include <QReadWriteLock>

ANYTHING_INDEX_BEGIN_NAMESPACE

class NetworkMountDetector
{
public:
    static NetworkMountDetector &instance();

    bool isNetworkPath(const QString &path) const;

    void refresh();

private:
    NetworkMountDetector();

    void parseMountInfo();

    static bool isNetworkFilesystem(const char *fstype, const char *source);

    QSet<QString> m_networkMountPoints;
    mutable QReadWriteLock m_lock;
};

ANYTHING_INDEX_END_NAMESPACE

#endif   // NETWORKMOUNTDETECTOR_H
