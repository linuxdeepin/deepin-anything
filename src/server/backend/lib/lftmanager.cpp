// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lftmanager.h"
#include "lftdisktool.h"

extern "C" {
#include "fs_buf.h"
#include "walkdir.h"
}

#include <ddiskmanager.h>
#include <dblockpartition.h>
#include <ddiskdevice.h>

#include <QtConcurrent>
#include <QFutureWatcher>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QTimer>
#include <QLoggingCategory>

#include <unistd.h>
#include <sys/time.h>

Q_GLOBAL_STATIC_WITH_ARGS(QLoggingCategory, normalLog, ("manager.normal"))
Q_GLOBAL_STATIC_WITH_ARGS(QLoggingCategory, changesLog, ("manager.changes", QtWarningMsg))

#define nDebug(...) qCDebug((*normalLog), __VA_ARGS__)
#define nInfo(...) qCInfo((*normalLog), __VA_ARGS__)
#define nWarning(...) qCWarning((*normalLog), __VA_ARGS__)
#define cDebug(...) qCDebug((*changesLog), __VA_ARGS__)
#define cWarning(...) qCWarning((*changesLog), __VA_ARGS__)
#define DEFAULT_RESULT_COUNT 100
// set default timeout(ms) for search function return.
#define DEFAULT_TIMEOUT 200

static QString _getCacheDir()
{
    QString cachePath = QString("/var/cache/%1/deepin-anything").arg(qApp->organizationName());

    if (getuid() != 0 && !QFileInfo(cachePath).isWritable()) {
        cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);

        if (cachePath.isEmpty() || cachePath == "/") {
            cachePath = QString("/tmp/%1/deepin-anything").arg(qApp->organizationName());
        }
    }

    nInfo() << "Cache Dir:" << cachePath;

    if (!QDir::home().mkpath(cachePath)) {
        nWarning() << "Failed on create chache path";
    }

    return cachePath;
}

class _LFTManager : public LFTManager {};
Q_GLOBAL_STATIC(_LFTManager, _global_lftmanager)
typedef QMap<QString, fs_buf*> FSBufMap;
Q_GLOBAL_STATIC(FSBufMap, _global_fsBufMap)
typedef QMap<fs_buf*, QString> FSBufToFileMap;
Q_GLOBAL_STATIC(FSBufToFileMap, _global_fsBufToFileMap)
typedef QMap<QString, QFutureWatcher<fs_buf*>*> FSJobWatcherMap;
Q_GLOBAL_STATIC(FSJobWatcherMap, _global_fsWatcherMap)
typedef QSet<fs_buf*> FSBufList;
Q_GLOBAL_STATIC(FSBufList, _global_fsBufDirtyList)
Q_GLOBAL_STATIC_WITH_ARGS(QSettings, _global_settings, (_getCacheDir() + "/config.ini", QSettings::IniFormat))

static QSet<fs_buf*> fsBufList()
{
    if (!_global_fsBufMap.exists())
        return QSet<fs_buf*>();

    return _global_fsBufMap->values().toSet();
}

static void clearFsBufMap()
{
    for (fs_buf *buf : fsBufList()) {
        if (buf)
            free_fs_buf(buf);
    }

    if (_global_fsBufMap.exists())
        _global_fsBufMap->clear();

    if (_global_fsBufToFileMap)
        _global_fsBufToFileMap->clear();

    if (_global_fsWatcherMap.exists()) {
        for (const QString &path : _global_fsWatcherMap->keys()) {
            LFTManager::instance()->cancelBuild(path);
        }
    }
}

// 标记为脏文件, 定时清理(对象销毁时也会清理)
static void markLFTFileToDirty(fs_buf *buf)
{
    _global_fsBufDirtyList->insert(buf);
}

// 删除脏文件
static bool doLFTFileToDirty(fs_buf *buf)
{
    const QString &lft_file = _global_fsBufToFileMap->value(buf);

    nDebug() << lft_file;

    if (lft_file.isEmpty())
        return false;

    return QFile::remove(lft_file);
}

static void cleanDirtyLFTFiles()
{
    if (!_global_fsBufDirtyList.exists())
        return;

    for (fs_buf *buf : _global_fsBufDirtyList.operator *()) {
        doLFTFileToDirty(buf);
    }

    _global_fsBufDirtyList->clear();
}

LFTManager::~LFTManager()
{
    sync();
    clearFsBufMap();
    // 删除剩余脏文件(可能是sync失败)
    cleanDirtyLFTFiles();
}

LFTManager *LFTManager::instance()
{
    return _global_lftmanager;
}

QString LFTManager::cacheDir()
{
    static QString dir = _getCacheDir();

    return dir;
}

QStringList LFTManager::logCategoryList()
{
    QStringList list;

    list << normalLog->categoryName()
         << changesLog->categoryName();

    return list;
}

QByteArray LFTManager::setCodecNameForLocale(const QByteArray &codecName)
{
    const QTextCodec *old_codec = QTextCodec::codecForLocale();

    if (codecName.isEmpty())
        QTextCodec::setCodecForLocale(nullptr);
    else
        QTextCodec::setCodecForLocale(QTextCodec::codecForName(codecName));

    nDebug() << codecName << "old:" << old_codec->name();

    return old_codec->name();
}

struct FSBufDeleter
{
    static inline void cleanup(fs_buf *pointer)
    {
        free_fs_buf(pointer);
    }
};

static int handle_build_fs_buf_progress(uint32_t file_count, uint32_t dir_count, const char* cur_dir, const char* cur_file, void* param)
{
    Q_UNUSED(file_count)
    Q_UNUSED(dir_count)
    Q_UNUSED(cur_dir)
    Q_UNUSED(cur_file)

    QFutureWatcherBase *futureWatcher = static_cast<QFutureWatcherBase*>(param);
    // 在并行里立即检测watcher, 它的状态可能未更新(isStarted=true 但是isRunning=false && isFinished=true)
    // BUG93215，导致索引建立被取消
    if (!futureWatcher || !futureWatcher->isRunning()) {
        return 0;
    }

    if (futureWatcher->isCanceled()) {
        return 1;
    }

    return 0;
}

// TODO(zccrs): 检查buf是否为空，在某些情况下，创建或加载的buf为会为空，导致无法进行文件搜索
static bool checkFSBuf(fs_buf *buf)
{
    return get_tail(buf) != first_name(buf);
}

static fs_buf *buildFSBuf(QFutureWatcherBase *futureWatcher, const QString &path)
{
    fs_buf *buf = new_fs_buf(1 << 24, path.toLocal8Bit().constData());

    if (!buf)
        return buf;

    if (build_fstree(buf, false, handle_build_fs_buf_progress, futureWatcher) != 0) {
        free_fs_buf(buf);

        nWarning() << "[LFT] Failed on build fs buffer of path: " << path;

        return nullptr;
    }

    if (!checkFSBuf(buf)) {
        free_fs_buf(buf);

        nWarning() << "[LFT] Failed on check fs buffer of path: " << path;

        return nullptr;
    }

    return buf;
}

static QString getLFTFileByPath(const QString &path, bool autoIndex)
{
    QByteArray lft_file_name = LFTDiskTool::pathToSerialUri(path);

    if (lft_file_name.isEmpty())
        return QString();

    //由自动索引机制创建的文件后缀名为LFT，否则为lft
    lft_file_name += autoIndex ? ".LFT" : ".lft";

    const QString &cache_path = LFTManager::cacheDir();

    if (cache_path.isEmpty())
        return QString();

    return cache_path + "/" + QString::fromLocal8Bit(lft_file_name.toPercentEncoding(":", "/"));
}

static bool allowablePath(LFTManager *manager, const QString &path)
{
    QStorageInfo info(path);

    if (!info.isValid()) {
        return true;
    }

    QScopedPointer<DBlockPartition> device(LFTDiskTool::diskManager()->createBlockPartition(info, nullptr));

    if (!device) {
        return true;
    }

    QScopedPointer<DDiskDevice> disk(LFTDiskTool::diskManager()->createDiskDevice(device->drive()));

    if (disk->removable()) {
        return manager->autoIndexExternal();
    } else {
        return manager->autoIndexInternal();
    }
}

static bool allowableBuf(LFTManager *manager, fs_buf *buf)
{
    // 手动创建的buf不受属性autoIndexExternal/autoIndexInternal的控制
    if (_global_fsBufToFileMap->value(buf).endsWith(".lft"))
        return true;

    const char *root = get_root_path(buf);

    return allowablePath(manager, QString::fromLocal8Bit(root));
}

static void removeBuf(fs_buf *buf, bool &removeLFTFile)
{
    nDebug() << get_root_path(buf) << removeLFTFile;

    for (const QString &other_key : _global_fsBufMap->keys(buf)) {
        nDebug() << "do remove:" << other_key;

        _global_fsBufMap->remove(other_key);
    }

    if (removeLFTFile) {
        removeLFTFile = doLFTFileToDirty(buf);
    }

    _global_fsBufDirtyList->remove(buf);
    _global_fsBufToFileMap->remove(buf);
    free_fs_buf(buf);
}

bool LFTManager::addPath(QString path, bool autoIndex)
{
    nDebug() << path << autoIndex;

    if (!path.startsWith("/")) {
        sendErrorReply(QDBusError::InvalidArgs, "The path must start with '/'");

        return false;
    }

    if (_global_fsWatcherMap->contains(path)) {
        sendErrorReply(QDBusError::InternalError, "Index data building for this path");

        return false;
    }

    const QByteArray &serial_uri = LFTDiskTool::pathToSerialUri(path);

    if (serial_uri.isEmpty()) {
        sendErrorReply(QDBusError::InvalidArgs, "Unable to convert to serial uri");

        return false;
    }

    QFutureWatcher<fs_buf*> *watcher = new QFutureWatcher<fs_buf*>(this);
    // 保存任务是否由自动索引触发的
    watcher->setProperty("_d_autoIndex", autoIndex);

    // 此路径对应的设备可能被挂载到多个位置
    const QByteArrayList &path_list = LFTDiskTool::fromSerialUri(serial_uri);

    nDebug() << "Equivalent paths:" << path_list;

    // 将路径改为相对于第一个挂载点的路径，vfs_monitor中所有文件的改动都是以设备第一个挂载点通知的
    path = path_list.first();

    // 保存信息，用于判断索引是否正在构建
    for (const QByteArray &path_raw : path_list) {
        const QString &path = QString::fromLocal8Bit(path_raw);

        (*_global_fsWatcherMap)[path] = watcher;
    }

    connect(watcher, &QFutureWatcher<fs_buf*>::finished, this, [this, path_list, path, watcher, autoIndex] {
        fs_buf *buf = !watcher->isCanceled() ? watcher->result() : nullptr;

        // 已被取消构建或构建的结果不再需要时则忽略生成结果
        if (!_global_fsWatcherMap->contains(path) || (autoIndex && buf && !allowableBuf(this, buf))) {
            nWarning() << "[LFT] Discarded index data of path:" << path;

            free_fs_buf(buf);
            buf = nullptr;
        }

        for (const QByteArray &path_raw : path_list) {
            const QString &path = QString::fromLocal8Bit(path_raw);

            if (buf) {
                // 清理旧数据
                if (fs_buf *old_buf = _global_fsBufMap->value(path)) {
                    bool removeFile = false;
                    removeBuf(old_buf, removeFile);
                }

                (*_global_fsBufMap)[path] = buf;
                // 新加入的buf要添加到脏列表
                markLFTFileToDirty(buf);
            }

            _global_fsWatcherMap->remove(path);

            Q_EMIT addPathFinished(path, buf);
        }

        if (buf) {
            _global_fsBufToFileMap->insert(buf, getLFTFileByPath(path, autoIndex));
        }

        watcher->deleteLater();
    });

    QFuture<fs_buf*> result = QtConcurrent::run(buildFSBuf, watcher, path.endsWith('/') ? path : path + "/");

    watcher->setFuture(result);

    return true;
}

bool LFTManager::removePath(const QString &path)
{
    nDebug() << path;

    if (fs_buf *buf = _global_fsBufMap->take(path)) {
        if (_global_fsBufToFileMap->value(buf).endsWith(".LFT")) {
            // 不允许通过此接口删除由自动索引创建的数据
            sendErrorReply(QDBusError::NotSupported, "Deleting data created by automatic indexing is not supported");

            return false;
        }

        bool removeLFTFile = true;
        removeBuf(buf, removeLFTFile);

        if (removeLFTFile) {
            //此处被删除的是手动添加的索引数据, 之后应该更新自动生成的索引数据, 因为此目录之前一直
            //使用手动生成的数据,现在数据被删了,但目录本身有可能是满足为其自动生成索引数据的要求
            QStorageInfo info(path);

            if (info.isValid()) {
                nDebug() << "will process mount point(do build lft data for it):" << info.rootPath();

                onMountAdded(QString(), info.rootPath().toLocal8Bit());
            }
        }
    }

    sendErrorReply(QDBusError::InvalidArgs, "Not found the index data");

    return false;
}

// 返回path对应的fs_buf对象，且将path转成为相对于fs_buf root_path的路径
static QList<QPair<QString, fs_buf*>> getFsBufByPath(const QString &path, bool onlyFirst = true)
{
    if (!_global_fsBufMap.exists())
        return QList<QPair<QString, fs_buf*>>();

    if (!path.startsWith("/"))
        return QList<QPair<QString, fs_buf*>>();

    QDir path_dir(path);

    // 找到一个存在的路径
    while (!path_dir.exists()) {
        if (!path_dir.cdUp())
            break;
    }

    QStorageInfo storage_info(path_dir);
    QString result_path = path;
    QList<QPair<QString, fs_buf*>> buf_list;

    Q_FOREVER {
        fs_buf *buf = _global_fsBufMap->value(result_path);

        if (buf) {
            // path相对于此fs_buf root_path的路径
            QString new_path = path.mid(result_path.size());

            // 移除多余的 / 字符
            if (new_path.startsWith("/"))
                new_path = new_path.mid(1);

            // fs_buf中的root_path以/结尾，所以此处直接拼接
            new_path.prepend(QString::fromLocal8Bit(get_root_path(buf)));

            // 移除多余的 / 字符
            if (new_path.size() > 1 && new_path.endsWith("/"))
                new_path.chop(1);

            buf_list << qMakePair(new_path, buf);

            if (onlyFirst)
                break;
        }

        if (result_path == "/" || result_path == storage_info.rootPath())
            break;

        int last_dir_split_pos = result_path.lastIndexOf('/');

        if (last_dir_split_pos < 0)
            break;

        result_path = result_path.left(last_dir_split_pos);

        if (result_path.isEmpty())
            result_path = "/";
    };

    return buf_list;
}

bool LFTManager::hasLFT(const QString &path) const
{
    auto list = getFsBufByPath(path);

    return !list.isEmpty();
}

bool LFTManager::lftBuinding(const QString &path) const
{
    return _global_fsWatcherMap->contains(path);
}

bool LFTManager::cancelBuild(const QString &path)
{
    nDebug() << path;

    if (QFutureWatcher<fs_buf*> *watcher = _global_fsWatcherMap->take(path)) {
        watcher->cancel();
        nDebug() << "will wait for finished";
        watcher->waitForFinished();

        // 清理其它等价的路径
        for (const QString &path : _global_fsWatcherMap->keys(watcher)) {
            qDebug() << "do remove:" << path;

            _global_fsWatcherMap->remove(path);
        }

        return true;
    }

    return false;
}

QStringList LFTManager::allPath() const
{
    if (!_global_fsBufMap.exists())
        return QStringList();

    QStringList list;

    for (auto i = _global_fsBufMap->constBegin(); i != _global_fsBufMap->constEnd(); ++i) {
        list << i.key();
    }

    return list;
}

QStringList LFTManager::hasLFTSubdirectories(QString path) const
{
    if (!path.endsWith("/"))
        path.append('/');

    QStringList list;

    for (auto i = _global_fsBufMap->constBegin(); i != _global_fsBufMap->constEnd(); ++i) {
        if ((i.key() + "/").startsWith(path))
            list << i.key();
    }

    return list;
}

// 重新从磁盘加载lft文件
QStringList LFTManager::refresh(const QByteArray &serialUriFilter)
{
    nDebug() << serialUriFilter;

    const QString &cache_path = cacheDir();
    QDirIterator dir_iterator(cache_path, {"*.lft", "*.LFT"});
    QStringList path_list;

    while (dir_iterator.hasNext()) {
        const QString &lft_file = dir_iterator.next();

        nDebug() << "found file:" << lft_file;

        // 根据地址过滤，只加载此挂载路径或其子路径对应的索引文件
        if (!serialUriFilter.isEmpty() && !dir_iterator.fileName().startsWith(serialUriFilter))
            continue;

        nDebug() << "will load:" << dir_iterator.fileName();

        QByteArray file_name = dir_iterator.fileName().toLocal8Bit();
        file_name.chop(4); // 去除 .lft 后缀
        const QByteArrayList pathList = LFTDiskTool::fromSerialUri(QByteArray::fromPercentEncoding(file_name));

        nDebug() << "path list of serial uri:" << pathList;

        if (pathList.isEmpty()) {
            continue;
        }

        fs_buf *buf = nullptr;

        if (load_fs_buf(&buf, lft_file.toLocal8Bit().constData()) != 0) {
            nWarning() << "[LFT] Failed on load:" << lft_file;
            continue;
        }

        if (!buf) {
            nWarning() << "[LFT] Failed on load:" << lft_file;
            continue;
        }

        if (!checkFSBuf(buf)) {
            // 重新生成fs buf
            addPath(QString::fromLocal8Bit(get_root_path(buf)), dir_iterator.fileName().endsWith(".lft"));
            free_fs_buf(buf);

            nWarning() << "[LFT] Failed on check fs buf of: " << lft_file;
            continue;
        }

        for (const QByteArray &path_raw : pathList) {
            const QString path = QString::fromLocal8Bit(path_raw);

            path_list << path;

            // 清理旧的buf
            if (fs_buf *buf = _global_fsBufMap->value(path)) {
                bool removeFile = false;
                removeBuf(buf, removeFile);
            }

            (*_global_fsBufMap)[path] = buf;
        }

        _global_fsBufToFileMap->insert(buf, lft_file);
    }

    return path_list;
}

QStringList LFTManager::sync(const QString &mountPoint)
{
    nDebug() << mountPoint;

    QStringList path_list;

    if (!_global_fsBufMap.exists()) {
        return path_list;
    }

    if (!QDir::home().mkpath(cacheDir())) {
        sendErrorReply(QDBusError::AccessDenied, "Failed on create path: " + cacheDir());

        return path_list;
    }

    QList<fs_buf*> saved_buf_list;

    for (auto buf_begin = _global_fsBufMap->constBegin(); buf_begin != _global_fsBufMap->constEnd(); ++buf_begin) {
        fs_buf *buf = buf_begin.value();
        const QString &path = buf_begin.key();

        nDebug() << "found buf, path:" << path;

        // 只同步此挂载点的数据
        if (!mountPoint.isEmpty()) {
            if (!(path + "/").startsWith(mountPoint + '/')) {
                continue;
            }
        }

        if (saved_buf_list.contains(buf)) {
            nDebug() << "buf is saved";

            path_list << buf_begin.key();
            continue;
        }

        const QString &lft_file = _global_fsBufToFileMap->value(buf);

        nDebug() << "lft file:" << lft_file;

        if (lft_file.isEmpty()) {
            nWarning() << "[LFT] Can't get the LFT file path of the fs_buf:" << get_root_path(buf);

            continue;
        }

        if (save_fs_buf(buf, lft_file.toLocal8Bit().constData()) == 0) {
            saved_buf_list.append(buf);
            path_list << buf_begin.key();
            // 从脏列表中移除
            _global_fsBufDirtyList->remove(buf);
        } else {
            path_list << QString("Failed: \"%1\"->\"%2\"").arg(buf_begin.key()).arg(lft_file);

            nWarning() << "[LFT] " << path_list.last();
        }
    }

    return path_list;
}

QStringList LFTManager::search(const QString &path, const QString &keyword, bool useRegExp) const
{
    quint32 start, end;

    return search(-1, -1, 0, 0, path, keyword, useRegExp, start, end);
}

QStringList LFTManager::search(int maxCount, qint64 icase, quint32 startOffset, quint32 endOffset,
                               const QString &path, const QString &keyword, bool useRegExp,
                               quint32 &startOffsetReturn, quint32 &endOffsetReturn) const
{
    QStringList rules; //append some old args in the rule list.
    rules.append(QString("0x%1%2").arg(RULE_SEARCH_REGX, 2, 16, QLatin1Char('0')).arg(useRegExp? 1 : 0));
    rules.append(QString("0x%1%2").arg(RULE_SEARCH_MAX_COUNT, 2, 16, QLatin1Char('0')).arg(maxCount));
    rules.append(QString("0x%1%2").arg(RULE_SEARCH_ICASE, 2, 16, QLatin1Char('0')).arg(icase));
    rules.append(QString("0x%1%2").arg(RULE_SEARCH_STARTOFF, 2, 16, QLatin1Char('0')).arg(startOffset));
    rules.append(QString("0x%1%2").arg(RULE_SEARCH_ENDOFF, 2, 16, QLatin1Char('0')).arg(endOffset));

    return _enterSearch(path, keyword, rules, startOffsetReturn, endOffsetReturn);
}

enum SearchError
{
    SUCCESS = 0,
    OK_RULE,
    NOFOUND_INDEX,
    BUILDING_INDEX,
    EMPTY_DIR,
    INVALID_RE
};

QStringList LFTManager::insertFileToLFTBuf(const QByteArray &file)
{
    cDebug() << file;

    auto list = getFsBufByPath(QString::fromLocal8Bit(file), false);
    QStringList root_path_list;

    if (list.isEmpty())
        return root_path_list;

    QFileInfo info(QString::fromLocal8Bit(file));
    bool is_dir = info.isDir();

    for (auto i : list) {
        fs_buf *buf = i.second;

        // 有可能索引正在构建
        if (!buf) {
            cDebug() << "index buinding";

            // 正在构建索引时需要等待
            if (QFutureWatcher<fs_buf*> *watcher = _global_fsWatcherMap->value(i.first)) {
                cDebug() << "will be wait build finished";

                watcher->waitForFinished();
                buf = watcher->result();
            }

            if (!buf)
                continue;
        }

        cDebug() << "do insert:" << i.first;

        fs_change change;
        int r = insert_path(buf, i.first.toLocal8Bit().constData(), is_dir, &change);

        if (r == 0) {
            // buf内容已改动，标记删除对应的lft文件
            markLFTFileToDirty(buf);
            root_path_list << QString::fromLocal8Bit(get_root_path(buf));
        } else {
            if (r == ERR_NO_MEM) {
                cWarning() << "Failed(No Memory):" << i.first;
            } else {
                cDebug() << "Failed:" << i.first << ", result:" << r;
            }
        }
    }

    return root_path_list;
}

QStringList LFTManager::removeFileFromLFTBuf(const QByteArray &file)
{
    cDebug() << file;

    auto list = getFsBufByPath(QString::fromLocal8Bit(file), false);
    QStringList root_path_list;

    if (list.isEmpty())
        return root_path_list;

    for (auto i : list) {
        fs_buf *buf = i.second;

        // 有可能索引正在构建
        if (!buf) {
            cDebug() << "index buinding";

            // 正在构建索引时需要等待
            if (QFutureWatcher<fs_buf*> *watcher = _global_fsWatcherMap->value(i.first)) {
                cDebug() << "will be wait build finished";

                watcher->waitForFinished();
                buf = watcher->result();
            }

            if (!buf)
                continue;
        }

        cDebug() << "do remove:" << i.first;

        fs_change changes[10];
        uint32_t count = 10;
        int r = remove_path(buf, i.first.toLocal8Bit().constData(), changes, &count);

        if (r == 0) {
            // buf内容已改动，标记删除对应的lft文件
            markLFTFileToDirty(buf);
            root_path_list << QString::fromLocal8Bit(get_root_path(buf));
        } else {
            if (r == ERR_NO_MEM) {
                cWarning() << "Failed(No Memory):" << i.first;
            } else {
                cDebug() << "Failed:" << i.first << ", result:" << r;
            }
        }
    }

    return root_path_list;
}

QStringList LFTManager::renameFileOfLFTBuf(const QByteArray &oldFile, const QByteArray &newFile)
{
    cDebug() << oldFile << newFile;

    auto list = getFsBufByPath(QString::fromLocal8Bit(newFile), false);
    QStringList root_path_list;

    if (list.isEmpty())
        return root_path_list;

    for (int i = 0; i < list.count(); ++i) {
        fs_buf *buf = list.at(i).second;

        // 有可能索引正在构建
        if (!buf) {
            cDebug() << "index buinding";

            // 正在构建索引时需要等待
            if (QFutureWatcher<fs_buf*> *watcher = _global_fsWatcherMap->value(list.at(i).first)) {
                cDebug() << "will be wait build finished";

                watcher->waitForFinished();
                buf = watcher->result();
            }

            if (!buf)
                continue;
        }

        fs_change changes[10];
        uint32_t change_count = 10;

        // newFile相对于此buf的路径
        const QByteArray &new_file_new_path = list.at(i).first.toLocal8Bit();
        int valid_suffix_size = new_file_new_path.size() - strlen(get_root_path(buf));
        int invalid_prefix_size = newFile.size() - valid_suffix_size;

        QByteArray old_file_new_path = QByteArray(get_root_path(buf)).append(oldFile.mid(invalid_prefix_size));

        cDebug() << "do rename:" << old_file_new_path << new_file_new_path;

        int r = rename_path(buf, old_file_new_path.constData(), new_file_new_path.constData(), changes, &change_count);

        if (r == 0) {
            // buf内容已改动，标记删除对应的lft文件
            markLFTFileToDirty(buf);
            root_path_list << QString::fromLocal8Bit(get_root_path(buf));
        } else {
            if (r == ERR_NO_MEM) {
                cWarning() << "Failed(No Memory)";
            } else {
                cDebug() << "Failed: result=" << r;
            }
        }
    }

    return root_path_list;
}

void LFTManager::quit()
{
    qApp->quit();
}

bool LFTManager::autoIndexExternal() const
{
    return _global_settings->value("autoIndexExternal", false).toBool();
}

bool LFTManager::autoIndexInternal() const
{
    return _global_settings->value("autoIndexInternal", true).toBool();
}

int LFTManager::logLevel() const
{
    if (normalLog->isEnabled(QtDebugMsg)) {
        if (changesLog->isEnabled(QtDebugMsg)) {
            return 2;
        } else {
            return 1;
        }
    }

    return 0;
}

QStringList LFTManager::parallelsearch(const QString &path, const QString &keyword, const QStringList &rules) const
{
    quint32 start, end;
    QStringList nRules = _setRulesByDefault(rules, 0, 0);
    return _enterSearch(path, keyword, nRules, start, end);
}

QStringList LFTManager::parallelsearch(const QString &path, quint32 startOffset, quint32 endOffset,
                                       const QString &keyword, const QStringList &rules,
                                       quint32 &startOffsetReturn, quint32 &endOffsetReturn) const
{
    QStringList nRules = _setRulesByDefault(rules, startOffset, endOffset);
    return _enterSearch(path, keyword, nRules, startOffsetReturn, endOffsetReturn);
}

void LFTManager::setAutoIndexExternal(bool autoIndexExternal)
{
    if (this->autoIndexExternal() == autoIndexExternal)
        return;

    _global_settings->setValue("autoIndexExternal", autoIndexExternal);
    nDebug() << autoIndexExternal;

    if (autoIndexExternal) {
        _indexAll();
    } else {
        _cleanAllIndex();
    }

    emit autoIndexExternalChanged(autoIndexExternal);
}

void LFTManager::setAutoIndexInternal(bool autoIndexInternal)
{
    if (this->autoIndexInternal() == autoIndexInternal)
        return;

    _global_settings->setValue("autoIndexInternal", autoIndexInternal);
    nDebug() << autoIndexInternal;

    if (autoIndexInternal) {
        _indexAll();
    } else {
        _cleanAllIndex();
    }

    emit autoIndexInternalChanged(autoIndexInternal);
}

void LFTManager::setLogLevel(int logLevel)
{
    qDebug() << logLevel;

    normalLog->setEnabled(QtDebugMsg, logLevel > 0);
    normalLog->setEnabled(QtWarningMsg, logLevel > 0);
    normalLog->setEnabled(QtInfoMsg, logLevel > 0);

    changesLog->setEnabled(QtDebugMsg, logLevel > 1);
    changesLog->setEnabled(QtWarningMsg, logLevel > 0);
    changesLog->setEnabled(QtInfoMsg, logLevel > 1);
}

inline static QString getAppRungingFile()
{
    return LFTManager::cacheDir() + "/app.runing";
}

static void cleanLFTManager()
{
    nDebug() << "clean at application exit";

    LFTManager::instance()->sync();
    clearFsBufMap();
    cleanDirtyLFTFiles();

    QFile::remove(getAppRungingFile());
}

static QStringList removeLFTFiles(const QByteArray &serialUriFilter = QByteArray())
{
    nDebug() << serialUriFilter;

    const QString &cache_path = LFTManager::cacheDir();
    //只处理自动生成的索引文件
    QDirIterator dir_iterator(cache_path, {"*.LFT"});
    QStringList path_list;

    while (dir_iterator.hasNext()) {
        const QString &lft_file = dir_iterator.next();

        nDebug() << "found lft file:" << lft_file;

        // 根据地址过滤，只加载此挂载路径或其子路径对应的索引文件
        if (!serialUriFilter.isEmpty() && !dir_iterator.fileName().startsWith(serialUriFilter))
            continue;

        nDebug() << "remove:" << lft_file;

        if (QFile::remove(lft_file)) {
            path_list << lft_file;
        } else {
            nWarning() << "[LFT] Failed on remove:" << lft_file;
        }
    }

    return path_list;
}

LFTManager::LFTManager(QObject *parent)
    : QObject(parent)
{
    // ascii编码支持内容太少, 此处改为兼容它的utf8编码
    if (QTextCodec::codecForLocale() == QTextCodec::codecForName("ASCII")) {
        QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
        nDebug() << "reset the locale codec to UTF-8";
    }

    { // 创建一个普通文件, 在程序正常退出时删除, 用于识别进程未正常退出
        QFile file(getAppRungingFile());

        nDebug() << "app.runing:" << getAppRungingFile();

        if (file.exists()) {
            nWarning() << "[LFT] Last time not exiting normally";

#ifdef QT_NO_DEBUG
            // 说明进程上次未正常退出, 无法保证这些lft文件是正常的, 此处需要清理它们
            removeLFTFiles();
#endif
        }

        if (file.open(QIODevice::WriteOnly)) {
            file.close();
        }
    }

    qAddPostRoutine(cleanLFTManager);
    /*解决在最开始的10分钟内搜索不到问题 92168*/
   connect(&refresh_timer,&QTimer::timeout,this,[this](){
     const QStringList &list = this->refresh();
     if(!list.isEmpty()){
        refresh_timer.stop();
     } 
       
   });
   refresh_timer.setInterval(10*1000);
   refresh_timer.start();
#ifdef QT_NO_DEBUG
    // 可能会加载到一些自动生成的未被允许的索引文件, 此处应该清理一遍
    _cleanAllIndex();

    if (_isAutoIndexPartition())
        _indexAllDelay();
#endif

    connect(LFTDiskTool::diskManager(), &DDiskManager::mountAdded,
            this, &LFTManager::onMountAdded);
    connect(LFTDiskTool::diskManager(), &DDiskManager::mountRemoved,
            this, &LFTManager::onMountRemoved);
    connect(LFTDiskTool::diskManager(), &DDiskManager::fileSystemAdded,
            this, &LFTManager::onFSAdded);
    connect(LFTDiskTool::diskManager(), &DDiskManager::fileSystemRemoved,
            this, &LFTManager::onFSRemoved);

    // 监听设备信号
    LFTDiskTool::diskManager()->setWatchChanges(true);

    QTimer *sync_timer = new QTimer(this);

    connect(sync_timer, &QTimer::timeout, this, &LFTManager::_syncAll);

    // 每10分钟将fs_buf回写到硬盘一次
    sync_timer->setInterval(10 * 60 * 1000);
    sync_timer->start();
}

void LFTManager::sendErrorReply(QDBusError::ErrorType type, const QString &msg) const
{
    if (calledFromDBus()) {
        QDBusContext::sendErrorReply(type, msg);
    } else {
        nWarning() << type << msg;
    }
}

bool LFTManager::_isAutoIndexPartition() const
{
    return autoIndexExternal() || autoIndexInternal();
}

void LFTManager::_syncAll()
{
    nDebug() << "Timing synchronization data";

    sync();
    // 清理sync失败的脏文件
    cleanDirtyLFTFiles();
}

void LFTManager::_indexAll()
{
    // 遍历已挂载分区, 看是否需要为其建立索引数据
    for (const QString &block : LFTDiskTool::diskManager()->blockDevices()) {
        if (!DBlockDevice::hasFileSystem(block))
            continue;

        DBlockDevice *device = DDiskManager::createBlockDevice(block);

        if (device->isLoopDevice())
            continue;

        if (device->mountPoints().isEmpty())
            continue;

        if (!hasLFT(QString::fromLocal8Bit(device->mountPoints().first())))
            _addPathByPartition(device);
        else
            nDebug() << "Exist index data:" << device->mountPoints().first() << ", block:" << block;
    }
}

// 不要立马自动生成索引，防止刚开机时和其它进程抢占io
void LFTManager::_indexAllDelay(int time)
{
    QTimer::singleShot(time, this, &LFTManager::_indexAll);
}

void LFTManager::_cleanAllIndex()
{
    // 清理无效的路径
    for (fs_buf *buf : fsBufList()) {
        if (!allowableBuf(this, buf)) {
            bool removeFile = true;
            removeBuf(buf, removeFile);
        }
    }

    // 取消无效的构建
    for (const QString &path : _global_fsWatcherMap->keys()) {
        // 只清理由自动索引触发的构建
        if (_global_fsWatcherMap->value(path)->property("_d_autoIndex").toBool()
                && !allowablePath(this, path)) {
            cancelBuild(path);
        }
    }
}

static QString getRootMountPoint(const DBlockDevice *block)
{
    const QByteArrayList &mount_points = block->mountPoints();
    if(mount_points.size() == 1) {
        return QString::fromLocal8Bit(mount_points.first());
    }

    const auto mount_point_infos = LFTDiskTool::getMountPointsInfos(mount_points);
    for (QByteArray mount_point : mount_points) {
        const LFTDiskTool::MountPointInfo info = mount_point_infos.value(mount_point);
        if (info.sourcePath == "/") {
            mount_point.chop(1);
            return QString::fromLocal8Bit(mount_point);
        }
    }

    return QString::fromLocal8Bit(mount_points.first());
}

void LFTManager::_addPathByPartition(const DBlockDevice *block)
{
    nDebug() << block->device() << block->id() << block->drive();

    if (DDiskDevice *device = LFTDiskTool::diskManager()->createDiskDevice(block->drive())) {
        bool index = false;

        if (device->removable()) {
            index = autoIndexExternal();
            nDebug() << "removable device:" << device->path();
        } else {
            index = autoIndexInternal();
            nDebug() << "internal device:" << device->path();
        }

        nDebug() << "can index:" << index;

        if (index) // 建立索引时尽量使用根挂载点，这样索引的范围最大，同时与内核模块发出的事件的文件路径一致
            addPath(getRootMountPoint(block), true);

        device->deleteLater();
    }
}

void LFTManager::onMountAdded(const QString &blockDevicePath, const QByteArray &mountPoint)
{
    nInfo() << blockDevicePath << mountPoint;

    const QString &mount_root = QString::fromLocal8Bit(mountPoint);
    const QByteArray &serial_uri = LFTDiskTool::pathToSerialUri(mount_root);

    // 尝试加载此挂载点下的lft文件
    const QStringList &list = refresh(serial_uri.toPercentEncoding(":", "/"));

    if (list.contains(QString::fromLocal8Bit(mountPoint))) {
        return;
    }

    // 只在开启了自动建立索引的情况下进行下面的操作
    if (!_isAutoIndexPartition())
        return;

    if (DBlockDevice *block = LFTDiskTool::diskManager()->createBlockPartitionByMountPoint(mountPoint)) {
        if (!block->isLoopDevice()) {
            _addPathByPartition(block);
        }

        block->deleteLater();
    }
}

void LFTManager::onMountRemoved(const QString &blockDevicePath, const QByteArray &mountPoint)
{
    nInfo() << blockDevicePath << mountPoint;

    const QString &mount_root = QString::fromLocal8Bit(mountPoint);
//    const QByteArray &serial_uri = LFTDiskTool::pathToSerialUri(mount_root);

    for (const QString &path : hasLFTSubdirectories(mount_root)) {
        auto index = _global_fsBufMap->find(path);

        if (index != _global_fsBufMap->constEnd()) {
            if (lftBuinding(path)) {
                cancelBuild(path);
            } else {
                if (_global_fsBufDirtyList->contains(index.value()))
                    sync(path);
                bool removeFile = false;
                removeBuf(index.value(), removeFile);
            }
        }
    }
}

typedef QMap<QString, QString> BlockIDMap;
Q_GLOBAL_STATIC(BlockIDMap, _global_blockIdMap)

// 当文件系统变动时, 删除已存在的索引数据
void LFTManager::onFSAdded(const QString &blockDevicePath)
{
    QScopedPointer<DBlockDevice> device(DDiskManager::createBlockDevice(blockDevicePath));
    const QString &id = device->id();

    nInfo() << blockDevicePath << "id:" << id;

    if (!id.isEmpty()) {
        // 保存设备id，当设备被移除时以无法获取到id信息
        _global_blockIdMap->insert(blockDevicePath, id);
        removeLFTFiles("serial:" + id.toLocal8Bit());
    }
}

void LFTManager::onFSRemoved(const QString &blockDevicePath)
{
    const QString &id = _global_blockIdMap->take(blockDevicePath);

    nInfo() << blockDevicePath << "id:" << id;

    if (!id.isEmpty()) {
        removeLFTFiles("serial:" + id.toLocal8Bit());
    }
}

int LFTManager::_prepareBuf(quint32 *startOffset, quint32 *endOffset, const QString &path, void **buf, QString *newpath) const
{
    auto buf_list = getFsBufByPath(path);

    if (buf_list.isEmpty())
        return NOFOUND_INDEX;

    fs_buf *fs_buf = buf_list.first().second;

    if (!fs_buf)
        return BUILDING_INDEX;

    // new_path 为path在fs_buf中对应的路径
    *newpath = QString(buf_list.first().first);

    if (*startOffset == 0 || *endOffset == 0) {
        // 未指定有效的搜索区间时, 根据路径获取
        uint32_t path_offset = 0;
        uint32_t start_off, end_off = 0;
        get_path_range(fs_buf, newpath->toLocal8Bit().constData(), &path_offset, &start_off, &end_off);
        nDebug() << "get_path_range:" << start_off << end_off;
        *startOffset = start_off;
        *endOffset = end_off;
    }
    nDebug() << *startOffset << *endOffset;

    // 说明目录为空
    if (*startOffset == 0)
        return EMPTY_DIR;

    *buf = fs_buf;

    return SUCCESS;
}

bool LFTManager::_getRuleArgs(const QStringList &rules, int searchFlag, quint32 &valueReturn) const
{
    if (searchFlag >= RULE_EXCLUDE_SUB_S) {
        nDebug() << "this rule value will return a string!";
        return false;
    }
    for (const QString &rule : rules) {
        if (rule.size() < 4 || !rule.startsWith("0x")) //incorrect format rule, not start with "0x01" "0x10" e.
            continue;

        int flag = 0;
        bool ok;
        flag = rule.left(4).toInt(&ok, 0);
        if (ok && flag == searchFlag) {
            quint32 value = rule.mid(4).toUInt(&ok, 0);
            valueReturn = value;
            return true;
        }
    }

    // not find the special flag in rules
    return false;
}

bool LFTManager::_getRuleStrings(const QStringList &rules, int searchFlag, QStringList &valuesReturn) const
{
    if (searchFlag < RULE_EXCLUDE_SUB_S) {
        nDebug() << "this rule value is not a string!";
        return false;
    }
    bool ret = false;
    for (const QString &rule : rules) {
        if (rule.size() < 4 || !rule.startsWith("0x")) //incorrect format rule, not start with "0x40" e.
            continue;

        int flag = 0;
        bool ok;
        flag = rule.left(4).toInt(&ok, 0);
        if (ok && flag == searchFlag) {
            QString value = rule.mid(4);
            valuesReturn << value;
            ret = true;
        }
    }

    // not find the special flag in rules
    return ret;
}

bool LFTManager::_parseRules(void **prules, const QStringList &rules) const
{
    search_rule *p = nullptr; //the header of filter rule list;
    search_rule *temp = nullptr; //temp list for rule list building.

    for (const QString &rule : rules) {
        if (rule.size() < 4 || !rule.startsWith("0x")) //incorrect format rule, not start with "0x01" "0x10" e.
            continue;

        uint8_t flag = 0;
        bool ok;
        flag = (uint8_t)(rule.left(4).toInt(&ok, 0));
        QByteArray tmp = rule.mid(4).toLatin1();
        char *rule_content = tmp.data();

        search_rule *irule = reinterpret_cast<search_rule *>(malloc(sizeof(search_rule)));
        if (irule == nullptr) {
            nDebug() << "Failed to malloc search_rule.";
            break;
        }

        irule->flag = flag;
        strcpy(irule->target, rule_content);
        irule->next = nullptr;

        if (temp)
            temp->next =irule;
        temp = irule;

        // record the rule list header
        if (p == nullptr)
            p = temp;
    }
    *prules = p;
    return p && (p->flag != RULE_NONE);
}

QStringList LFTManager::_setRulesByDefault(const QStringList &rules, quint32 startOffset, quint32 endOffset) const
{
    QStringList nRules;
    //check the all search rules (0x01 - 0x05), set them with default value if isn't setted by user.
    quint32 orgval = 0;
    if (!_getRuleArgs(rules, RULE_SEARCH_REGX, orgval)) {
        nRules.append(QString("0x%1%2").arg(RULE_SEARCH_REGX, 2, 16, QLatin1Char('0')).arg(0));
    }
    if (!_getRuleArgs(rules, RULE_SEARCH_MAX_COUNT, orgval)) {
        //default not set request max count, it will search all index and return DEFAULT_RESULT_COUNT results.
        nRules.append(QString("0x%1%2").arg(RULE_SEARCH_MAX_COUNT, 2, 16, QLatin1Char('0')).arg(-1));
    }
    if (!_getRuleArgs(rules, RULE_SEARCH_ICASE, orgval)) {
        // default not ignoring the case(UP and LOW) of the characters in this API
        nRules.append(QString("0x%1%2").arg(RULE_SEARCH_ICASE, 2, 16, QLatin1Char('0')).arg(0));
    }
    if (!_getRuleArgs(rules, RULE_SEARCH_STARTOFF, orgval)) {
        nRules.append(QString("0x%1%2").arg(RULE_SEARCH_STARTOFF, 2, 16, QLatin1Char('0')).arg(startOffset));
    }
    if (!_getRuleArgs(rules, RULE_SEARCH_ENDOFF, orgval)) {
        nRules.append(QString("0x%1%2").arg(RULE_SEARCH_ENDOFF, 2, 16, QLatin1Char('0')).arg(endOffset));
    }

    nRules.append(rules);
    return nRules;
}

QStringList LFTManager::_enterSearch(const QString &opath, const QString &keyword, const QStringList &rules,
                   quint32 &startOffsetReturn, quint32 &endOffsetReturn) const
{
    quint32 maxCount = 0;
    quint32 startOffset = 0;
    quint32 endOffset = 0;

    // get the setting values: 0x02, 0x04, 0x05, which will be returned.
    _getRuleArgs(rules, RULE_SEARCH_MAX_COUNT, maxCount);
    _getRuleArgs(rules, RULE_SEARCH_STARTOFF, startOffset);
    _getRuleArgs(rules, RULE_SEARCH_ENDOFF, endOffset);

    QString path = opath;
    if (path.length() > 1 && path.endsWith("/")) {
        // make sure this search path not end with '/' if it's not the root /
        path.chop(1);
    }
    nInfo() << maxCount << startOffset << endOffset << path << keyword << rules;

    void *buf = nullptr;
    QString newpath;
    int buf_ok = _prepareBuf(&startOffset, &endOffset, path, &buf, &newpath);
    if (buf_ok != 0) {
        if (buf_ok == NOFOUND_INDEX)
            sendErrorReply(QDBusError::InvalidArgs, "Not found the index data");
        if (buf_ok == BUILDING_INDEX)
            sendErrorReply(QDBusError::InternalError, "Index is being generated");
        if (buf_ok == EMPTY_DIR) // 说明目录为空
            nDebug() << "Empty directory:" << newpath;
        return QStringList();
    }

    QStringList list;
    QList<uint32_t> offset_results;

    struct timeval s, e;
    gettimeofday(&s, nullptr);

    int total = 0;
    // get the search result, note the @startOffset and @endOffset both are in and out, which will record the real searching offsets.
    total += _doSearch(buf, maxCount, path, keyword, &startOffset, &endOffset, offset_results, rules);

    fs_buf *fsbuf = static_cast<fs_buf*>(buf);
    if (fsbuf) {
        char tmp_path[PATH_MAX] = {0};
        bool reset_path = path != newpath;
        // get the full path by the file/dir name, and append to list.
        for(uint32_t offset : offset_results) {
            const char *name_path = get_path_by_name_off(fsbuf, offset, tmp_path, sizeof(tmp_path));
            const QString &origin_path = QString::fromLocal8Bit(name_path);
            list.append(reset_path ? path + origin_path.mid(newpath.size()) : origin_path);
        }
    }

    gettimeofday(&e, nullptr);
    long dur = (e.tv_usec + e.tv_sec * 1000000) - (s.tv_usec + s.tv_sec * 1000000);
    // set this log as special start, it may be used to take result from log.
    nInfo() << "anything-GOOD: found " << total << " entries for " << keyword << "in " << dur << " us\n";

    startOffsetReturn = startOffset;
    endOffsetReturn = endOffset;
    return list;
}

int LFTManager::_doSearch(void *vbuf, quint32 maxCount, const QString &path, const QString &keyword,
                          quint32 *startOffset, quint32 *endOffset, QList<uint32_t> &results, const QStringList &rules) const
{
    fs_buf *buf = static_cast<fs_buf*>(vbuf);
    if (buf == nullptr)
        return 0;

    int total = 0;
    uint32_t start = *startOffset; //the search start offset in data index
    uint32_t end = *endOffset;  //the search end offset in data index
    uint32_t count = DEFAULT_RESULT_COUNT;
    uint32_t maxTimeout = DEFAULT_TIMEOUT;

    search_rule *searc_rule = nullptr;
    void *p = nullptr;
    if (!rules.isEmpty() && _parseRules(&p, rules))
        searc_rule = static_cast<search_rule*>(p);

    //if unlimit count (maxCount = -1 or 0), only append first DEFAULT_RESULT_COUNT results.
    uint32_t req_count = maxCount > 0 ? maxCount : count;
    uint32_t req_number = req_count;
    uint32_t *name_offsets = reinterpret_cast<uint32_t *>(malloc(req_count * sizeof(uint32_t)));
    if (name_offsets == nullptr) {
        nDebug() << "try malloc name_offsets to save result FAILED, count:" << req_count;
        return 0;
    }

    // 获取“以什么开头”的所有过滤条件，可能是多种条件！
    QStringList excludeStartStrs;
    bool hasExclude = _getRuleStrings(rules, RULE_EXCLUDE_SUB_S, excludeStartStrs);

    // 开始计时，默认超时200ms，防止搜索出错进入无限循环或过长时间无返回。
    QElapsedTimer et;
    et.start();

    bool next = false; // 标记是否需要进行再次搜索
    QByteArray keyArray = keyword.toLocal8Bit();
    const char *queryword = keyArray.data();
    do {
        // 搜索 -> 过滤 (1.结果数不满足或小于默认100个； 2.区间未搜索到任何结果) -> 循环搜索
        parallelsearch_files(buf, &start, end, name_offsets, &req_count, searc_rule, queryword);
        // save request count of result.
        uint32_t mincount = qMin(req_number, req_count);

        // 计算总是，如果有过滤则减除。然后重置请求数，防止用返回的值去下次请求，但保存结果的数组过小导致崩溃。
        total += req_count; // append the match number in this range, it allways more than the size of name_offsets.
        req_count = maxCount > 0 ? maxCount : count; // reset the request count, the name_offsets has been malloced with it.

        // append the offset values
        char tmp_path[PATH_MAX] = {0};
        for (uint32_t i = 0; i < mincount; ++i) {
            if (name_offsets[i] >= end) {
                // 搜索结果偏移量超出索引区间范围
                total--;
                continue;
            }
            if (uint32_t(results.count()) >= req_number) {
                start = name_offsets[i];
                if (maxCount > 0) //如果设置请求数，返回的应该是返回数据的实际数量
                    total = req_number;
                break;
            }

            // 从结果中排除过滤。
            // check exclude path which start with a filter setting string.
            bool should_filter = false;
            if (hasExclude) {
                const char *name_path = get_path_by_name_off(buf, name_offsets[i], tmp_path, sizeof(tmp_path));
                //should cut off the searching path start string.
                const QString &origin_path = QString::fromLocal8Bit(name_path).mid(path.length());
                for (QString &startStr : excludeStartStrs) {
                    QString subStr("/" + startStr);
                    if (origin_path.indexOf(subStr, 0, Qt::CaseSensitive) >= 0) {
                        total--;
                        should_filter = true;
                        break;
                    }
                }
            }
            if (!should_filter)
                results << name_offsets[i];
        }

        if (mincount > 0) {
            // 此次搜索有结果
            if (req_number > uint32_t(results.count())) {
                // 结果数未达到请求数，无论搜索是否已经结束（start=end)，都需设置下次起点，进入下一次搜索
                start = next_name(buf, name_offsets[mincount - 1]);
                next = true;
            } else {
                // 结果数已达到请求数，但一次搜索完成，由于多线程切片，最后一个结果并未是真正的结束点，重置起点，以方便用户进行下次搜索
                if (start == end) {
                    // search once and there are request number, need to return the last pos for next requestion.
                    start = next_name(buf, name_offsets[mincount - 1]);
                }
                // 搜索满足，结束
                next = false;
            }
        } else {
            // 此次搜索无结果，进入下一个区间，或已搜索完则结束
            next = start < end ? true : false;
        }

        if (next && et.elapsed() >= maxTimeout) {
            nInfo() << "break loop search by timeout! " << maxTimeout;
            // 本来是需要进入下一次循环请求，但由于超时，所以返回的结果数应该是实际结果数。
            total = results.count(); //set the actual result number.
            break;
        }

    } while (next);

    *startOffset = start;
    *endOffset = end;
    if (name_offsets)
        free(name_offsets);

    return total;
}
