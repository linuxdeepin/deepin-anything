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
    void autoDeleteLog(); // 自动删除日志

    QDir   logDir;              // 日志文件夹
    QTimer renameLogFileTimer;  // 重命名日志文件使用的定时器
    QDate  logFileCreatedDate;  // 日志文件创建的时间

    static QFile *logFile;      // 日志文件
    static QTextStream *logOut; // 输出日志的 QTextStream，使用静态对象就是为了减少函数调用的开销
    static QMutex logMutex;     // 同步使用的 mutex

    int logLimitSize = 10 * 1024 * 1024; // 10M
    qsizetype logMaxFiles = 10; // 最多保存 10 个最新的备份日志文件
};

QMutex LogSaverPrivate::logMutex;
QFile* LogSaverPrivate::logFile = nullptr;
QTextStream* LogSaverPrivate::logOut = nullptr;

LogSaverPrivate::LogSaverPrivate(LogSaver *qq)
    : q(qq)
{
    QString logPath = logDir.absoluteFilePath("app.log"); // 获取日志的路径
    logFileCreatedDate = QFileInfo(logPath).lastModified().date(); // 若日志文件不存在，返回nullptr

    // 3分钟检查一次日志文件创建时间
    renameLogFileTimer.setInterval(1000 * 60 * 3);
    QObject::connect(&renameLogFileTimer, &QTimer::timeout, [this] {
        QMutexLocker locker(&LogSaverPrivate::logMutex);
        backupLog();
        autoDeleteLog();
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
    if (logFileCreatedDate != QDate::currentDate() || logFile->size() > logLimitSize) {
        resetFile();

        QString logPath = logDir.absoluteFilePath("app.log");
        QTime lastTime = QFileInfo(logPath).lastModified().time();
        //重命名日志文件为后缀是创建时间的文件（例如 app.log > app.log.2023-04-14-00-04-12）
        QString newLogPath = logDir.absoluteFilePath(logFileCreatedDate.toString("app.log.yyyy-MM-dd") + lastTime.toString("-hh-mm-ss"));
        QFile::rename(logPath, newLogPath);

        // 重新创建 app.log
        initLogFile();
    }
}

// 自动删除过期日志
void LogSaverPrivate::autoDeleteLog()
{
    if (logDir.isEmpty()) {
        return;
    }

    QFileInfoList fileList = logDir.entryInfoList(QStringList() << "app.log.*", QDir::Files, QDir::Time|QDir::Reversed);
    int delCount = fileList.count() - logMaxFiles;
    if (delCount <= 0) {
        return;
    }

    for (auto it = fileList.begin(); it != fileList.end() && --delCount >= 0; ++it) {
        logDir.remove(it->absoluteFilePath());
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
