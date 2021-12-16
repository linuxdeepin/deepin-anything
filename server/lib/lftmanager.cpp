/*
 * Copyright (C) 2021 UOS Technology Co., Ltd.
 *
 * Author:     zccrs <zccrs@live.com>
 *
 * Maintainer: zccrs <zhangjide@deepin.com>
 *             yangwu <yangwu@uniontech.com>
 *             wangrong <wangrong@uniontech.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
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

Q_GLOBAL_STATIC_WITH_ARGS(QLoggingCategory, normalLog, ("manager.normal"))
Q_GLOBAL_STATIC_WITH_ARGS(QLoggingCategory, changesLog, ("manager.changes", QtWarningMsg))

#define nDebug(...) qCDebug((*normalLog), __VA_ARGS__)
#define nInfo(...) qCInfo((*normalLog), __VA_ARGS__)
#define nWarning(...) qCWarning((*normalLog), __VA_ARGS__)
#define cDebug(...) qCDebug((*changesLog), __VA_ARGS__)
#define cWarning(...) qCWarning((*changesLog), __VA_ARGS__)

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

        nWarning() << "Failed on build fs buffer of path: " << path;

        return nullptr;
    }

    if (!checkFSBuf(buf)) {
        free_fs_buf(buf);

        nWarning() << "Failed on check fs buffer of path: " << path;

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
            nWarning() << "Discarded index data of path:" << path;

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
            nWarning() << "Failed on load:" << lft_file;
            continue;
        }

        if (!buf) {
            nWarning() << "Failed on load:" << lft_file;
            continue;
        }

        if (!checkFSBuf(buf)) {
            // 重新生成fs buf
            addPath(QString::fromLocal8Bit(get_root_path(buf)), dir_iterator.fileName().endsWith(".lft"));
            free_fs_buf(buf);

            nWarning() << "Failed on check fs buf of: " << lft_file;
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
            nWarning() << "Can't get the LFT file path of the fs_buf:" << get_root_path(buf);

            continue;
        }

        if (save_fs_buf(buf, lft_file.toLocal8Bit().constData()) == 0) {
            saved_buf_list.append(buf);
            path_list << buf_begin.key();
            // 从脏列表中移除
            _global_fsBufDirtyList->remove(buf);
        } else {
            path_list << QString("Failed: \"%1\"->\"%2\"").arg(buf_begin.key()).arg(lft_file);

            nWarning() << path_list.last();
        }
    }

    return path_list;
}

static int compareString(const char *string, void *keyword)
{
    return QString::fromLocal8Bit(string).indexOf(*static_cast<const QString*>(keyword), 0, Qt::CaseInsensitive) < 0;
}

static int compareStringRegExp(const char *string, void *re)
{
    auto match = static_cast<QRegularExpression*>(re)->match(QString::fromLocal8Bit(string));

    return !match.hasMatch();
}

static int timeoutGuard(uint32_t count, const char* cur_file, void* param)
{
    Q_UNUSED(count)
    Q_UNUSED(cur_file)

    QPair<QElapsedTimer*, qint64> *data = static_cast<QPair<QElapsedTimer*, qint64>*>(param);

    return data->first->elapsed() >= data->second; // 返回false时表示不终端搜索
}

QStringList LFTManager::search(const QString &path, const QString &keyword, bool useRegExp) const
{
    quint32 start, end;

    return search(-1, -1, 0, 0, path, keyword, useRegExp, start, end);
}

QStringList LFTManager::search(int maxCount, qint64 maxTime, quint32 startOffset, quint32 endOffset,
                               const QString &path, const QString &keyword, bool useRegExp,
                               quint32 &startOffsetReturn, quint32 &endOffsetReturn) const
{
    nDebug() << maxCount << maxTime << startOffset << endOffset << path << keyword << useRegExp;

    auto buf_list = getFsBufByPath(path);

    if (buf_list.isEmpty()) {
        sendErrorReply(QDBusError::InvalidArgs, "Not found the index data");

        return QStringList();
    }

    fs_buf *buf = buf_list.first().second;

    if (!buf) {
        sendErrorReply(QDBusError::InternalError, "Index is being generated");

        return QStringList();
    }

    // new_path 为path在fs_buf中对应的路径
    const QString &new_path = buf_list.first().first;

    if (startOffset == 0 || endOffset == 0) {
        // 未指定有效的搜索区间时, 根据路径获取
        uint32_t path_offset = 0;
        get_path_range(buf, new_path.toLocal8Bit().constData(), &path_offset, &startOffset, &endOffset);
    }

    // 说明目录为空
    if (startOffset == 0) {
        nDebug() << "Empty directory:" << new_path;

        return QStringList();
    }

    QRegularExpression re(keyword);

    void *compare_param = nullptr;
    comparator_fn compare = nullptr;

    if (useRegExp) {
        if (!re.isValid()) {
            sendErrorReply(QDBusError::InvalidArgs, "Invalid regular expression: " + re.errorString());

            return QStringList();
        }

        re.setPatternOptions(QRegularExpression::CaseInsensitiveOption
                             | QRegularExpression::DotMatchesEverythingOption
                             | QRegularExpression::OptimizeOnFirstUsageOption);

        compare_param = &re;
        compare = compareStringRegExp;
    } else {
        compare_param = const_cast<QString*>(&keyword);
        compare = compareString;
    }

#define MAX_RESULT_COUNT 100

    uint32_t name_offsets[MAX_RESULT_COUNT];
    uint32_t count = MAX_RESULT_COUNT;

    QStringList list;
    char tmp_path[PATH_MAX];
    // root_path 以/结尾，所以此处需要多忽略一个字符
    bool need_reset_root_path = path != new_path;

    QElapsedTimer et;
    progress_fn progress = nullptr;
    QPair<QElapsedTimer*, qint64> progress_param;

    if (maxTime >= 0) {
        et.start();
        progress = timeoutGuard;
        progress_param.first = &et;
        progress_param.second = maxTime;
    }

    do {
        count = qMin(uint32_t(MAX_RESULT_COUNT), uint32_t(maxCount - list.count()));
        search_files(buf, &startOffset, endOffset, name_offsets, &count, compare, compare_param, progress, &progress_param);

        for (uint32_t i = 0; i < count; ++i) {
            const char *result = get_path_by_name_off(buf, name_offsets[i], tmp_path, sizeof(tmp_path));
            const QString &origin_path = QString::fromLocal8Bit(result);

            if (need_reset_root_path) {
                list << path + origin_path.mid(new_path.size());
                nDebug() << "need reset root path:" << origin_path << ", to:" << list.last();
             } else {
                list << origin_path;
            }
        }

        if (maxTime >= 0 && et.elapsed() >= maxTime) {
            break;
        }
    } while (count == MAX_RESULT_COUNT);

    startOffsetReturn = startOffset;
    endOffsetReturn = endOffset;

    return list;
}

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
            nWarning() << "Failed on remove:" << lft_file;
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
            nWarning() << "Last time not exiting normally";

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

        if (index) // 建立索引时一切以第一个挂载点为准
            addPath(QString::fromLocal8Bit(block->mountPoints().first()), true);

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
        bool removeFile = false;
        auto index = _global_fsBufMap->find(path);

        if (index != _global_fsBufMap->constEnd()) {
            if (lftBuinding(path)) {
                cancelBuild(path);
            } else {
                if (_global_fsBufDirtyList->contains(index.value()))
                    sync(path);

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
