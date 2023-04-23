// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "logsaver.h"

DAS_BEGIN_NAMESPACE

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

QMutex LogSaverPrivate::logMutex;
QFile* LogSaverPrivate::logFile = nullptr;
QTextStream* LogSaverPrivate::logOut = nullptr;

LogSaverPrivate::LogSaverPrivate(LogSaver *qq)
    : q(qq)
{
    QString logPath = logDir.absoluteFilePath("app.log"); // 获取日志的路径
    logFileCreatedDate = QFileInfo(logPath).lastModified().date(); // 若日志文件不存在，返回nullptr

    // 10分钟检查一次日志文件创建时间
    renameLogFileTimer.setInterval(1000 * 60 * 10);
    QObject::connect(&renameLogFileTimer, &QTimer::timeout, [this] {
        QMutexLocker locker(&LogSaverPrivate::logMutex);
        backupLog();
        autoDeleteLog(); // 自动删除7天前的日志
    });
}

LogSaverPrivate::~LogSaverPrivate() {
    resetFile();
}

void LogSaverPrivate::resetFile()
{
    if (nullptr != logFile && nullptr != logOut) {
        logFile->flush();
        logFile->close();
        delete logOut;
        delete logFile;
        logOut  = nullptr;
        logFile = nullptr;
    }
}

void LogSaverPrivate::startSaveDir(const QString &logPath)
{
    logDir.setPath(logPath);
    if (initLogFile()) {
        backupLog(); // 检测一次当前是否需要备份
        renameLogFileTimer.start();
    }
}

bool LogSaverPrivate::initLogFile()
{
    if (logDir.isEmpty()) {
        return false;
    }

    // 程序每次启动时 logFile 为 nullptr
    if (logFile == nullptr) {
        QString logPath = logDir.absoluteFilePath("app.log");
        logFileCreatedDate = QFileInfo(logPath).lastModified().date(); // 若日志文件不存在，返回nullptr
        // 文件是第一次创建，则创建日期是无效的，把其设置为当前日期
        if (logFileCreatedDate.isNull()) {
            logFileCreatedDate = QDate::currentDate();
        }

        logFile = new QFile(logPath);
        logOut = (logFile->open(QIODevice::WriteOnly | QIODevice::Append)) ? new QTextStream(logFile) : nullptr;
        if (logOut != nullptr) {
            logOut->setCodec("UTF-8");
        }
    }
    return logOut != nullptr;
}

void LogSaverPrivate::backupLog()
{
    if (nullptr == logFile || nullptr == logOut)
        return;

    // 程序运行时如果创建日期不是当前日期，则使用创建日期重命名，并生成一个新的 *.log
    // 如果 app.log 文件大小超过10M，重新创建一个日志文件，原文件存档为yyyy-MM-dd-hh-mm-ss.log
    if (logFileCreatedDate != QDate::currentDate() || logFile->size() > g_logLimitSize) {
        resetFile();

        QString logPath = logDir.absoluteFilePath("app.log");
        QTime lastTime = QFileInfo(logPath).lastModified().time();
        QString newLogPath = logDir.absoluteFilePath(logFileCreatedDate.toString("app.log.yyyy-MM-dd") + lastTime.toString("-hh-mm-ss"));
        QFile::rename(logPath, newLogPath);

        // 重新创建 app.log
        initLogFile();
    }
}

// 自动删除7天前的日志
void LogSaverPrivate::autoDeleteLog()
{
    QDateTime now = QDateTime::currentDateTime();

    // 前7天
    QDateTime dateTime1 = now.addDays(-7);
    QDateTime dateTime2;

    QString logPath = logDir.absoluteFilePath("app.log"); // 日志的路径
    QDir dir(logPath);
    QFileInfoList fileList = dir.entryInfoList();
    foreach (QFileInfo f, fileList ) {
        // "."和".."跳过
        if (f.baseName() == "")
            continue;

        dateTime2 = QDateTime::fromString(f.baseName(), "yyyy-MM-dd");
        if (dateTime2 < dateTime1) { // 只要日志时间小于前x天的时间就删除
            dir.remove(f.absoluteFilePath());
        }
    }
}

// 消息处理函数
void LogSaverPrivate::messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    QMutexLocker locker(&logMutex);

    if (nullptr == logOut) {
        return;
    }

    QString formatMsg = qFormatLogMessage(type, context, msg);
    *logOut << formatMsg << endl;
}

LogSaver *LogSaver::instance()
{
    static LogSaver ins;
    return &ins;
}

LogSaver::LogSaver(QObject *parent)
    : QObject(parent),
      d(new LogSaverPrivate(this))
{
}

LogSaver::~LogSaver()
{
}

void LogSaver::installMessageHandler() {
    QString logFormat = "[%{time yyyy-MM-dd, HH:mm:ss.zzz}] [%{type}] [%{function}: %{line}] %{message}";
    qSetMessagePattern(logFormat);
    qInstallMessageHandler(LogSaverPrivate::messageHandler);
}

void LogSaver::uninstallMessageHandler() {
    qSetMessagePattern(nullptr);
    qInstallMessageHandler(nullptr);
}

void LogSaver::setlogFilePath(const QString &logFilePath)
{
    d->startSaveDir(logFilePath);
}

DAS_END_NAMESPACE
