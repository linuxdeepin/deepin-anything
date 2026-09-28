// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PROGRESSNOTIFIER_H
#define PROGRESSNOTIFIER_H

#include "index_global.h"

#include <QObject>

ANYTHING_INDEX_BEGIN_NAMESPACE

class ProgressNotifier : public QObject
{
    Q_OBJECT
public:
    static ProgressNotifier *instance();

Q_SIGNALS:
    void progressChanged(qint64 count, qint64 total);

private:
    explicit ProgressNotifier(QObject *parent = nullptr)
        : QObject(parent) { }
};

ANYTHING_INDEX_END_NAMESPACE
#endif   // PROGRESSNOTIFIER_H
