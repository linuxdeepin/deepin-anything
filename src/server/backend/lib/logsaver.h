// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LOGSAVER_H
#define LOGSAVER_H

#include "dasdefine.h"

#include <iostream>
#include <QDebug>
#include <QDateTime>
#include <QMutexLocker>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QTextStream>
#include <QObject>
#include <QScopedPointer>

DAS_BEGIN_NAMESPACE

class LogSaverPrivate;

class LogSaver : public QObject
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(LogSaver)
    Q_DISABLE_COPY(LogSaver)

public:
    static LogSaver *instance();

    void installMessageHandler();
    void uninstallMessageHandler();

    void setlogFilePath(const QString &logFilePath);

private:
    explicit LogSaver(QObject *parent = nullptr);
    ~LogSaver();

private:
    friend LogSaverPrivate;
    QScopedPointer<LogSaverPrivate> d;
};

DAS_END_NAMESPACE

#endif // LOGSAVER_H
