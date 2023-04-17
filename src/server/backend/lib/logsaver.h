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
#include <QTextCodec>
#include <QObject>
#include <QScopedPointer>

DAS_BEGIN_NAMESPACE

class LogSaver;
class LogSaverPrivate
{
public:
    explicit LogSaverPrivate(LogSaver *qq);
    ~LogSaverPrivate();

    LogSaver *const q;

public:
    void startSaveDir(const QString &logPath);

    // 消息处理函数
    static void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg);

private:
    void resetFile();
    bool initLogFile();
    void backupLog();
    void autoDeleteLog(); // 自动删除30天前的日志

    QDir   logDir;              // 日志文件夹
    QTimer renameLogFileTimer;  // 重命名日志文件使用的定时器
    QDate  logFileCreatedDate;  // 日志文件创建的时间

    static QFile *logFile;      // 日志文件
    static QTextStream *logOut; // 输出日志的 QTextStream，使用静态对象就是为了减少函数调用的开销
    static QMutex logMutex;     // 同步使用的 mutex

    int g_logLimitSize = 10 * 1024 * 1024; // 10M
};


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
    QScopedPointer<LogSaverPrivate> d;
};

DAS_END_NAMESPACE

#endif // LOGSAVER_H
