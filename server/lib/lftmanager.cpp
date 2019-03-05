/*
 * Copyright (C) 2017 ~ 2019 Deepin Technology Co., Ltd.
 *
 * Author:     zccrs <zccrs@live.com>
 *
 * Maintainer: zccrs <zhangjide@deepin.com>
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

#include <dfmdiskmanager.h>
#include <dfmblockpartition.h>

#include <QtConcurrent>
#include <QFutureWatcher>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QTimer>

#include <unistd.h>

class _LFTManager : public LFTManager {};
Q_GLOBAL_STATIC(_LFTManager, _global_lftmanager)
typedef QMap<QString, fs_buf*> FSBufMap;
Q_GLOBAL_STATIC(FSBufMap, _global_fsBufMap)
typedef QMap<QString, QFutureWatcher<fs_buf*>*> FSJobWatcherMap;
Q_GLOBAL_STATIC(FSJobWatcherMap, _global_fsWatcherMap)

static QSet<fs_buf*> fsBufList()
{
    return _global_fsBufMap->values().toSet();
}

static void clearFsBufMap()
{
    for (fs_buf *buf : fsBufList()) {
        if (buf)
            free_fs_buf(buf);
    }

    _global_fsBufMap->clear();
}

LFTManager::~LFTManager()
{
    sync();
    clearFsBufMap();
}

LFTManager *LFTManager::instance()
{
    return _global_lftmanager;
}

struct FSBufDeleter
{
    static inline void cleanup(fs_buf *pointer)
    {
        free_fs_buf(pointer);
    }
};

static fs_buf *buildFSBuf(const QString &path)
{
    fs_buf *buf = new_fs_buf(1 << 24, path.toLocal8Bit().constData());

    if (!buf)
        return buf;

    if (build_fstree(buf, false, nullptr, nullptr) != 0) {
        free_fs_buf(buf);

        return nullptr;
    }

    return buf;
}

static QString _getCacheDir()
{
    const QString cachePath = QString("/var/cache/%2/deepin-anything").arg(qApp->organizationName());

    if (getuid() == 0)
        return cachePath;

    if (QFileInfo(cachePath).isWritable())
        return cachePath;

    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
}

static QString getCacheDir()
{
    static QString dir = _getCacheDir();

    return dir;
}

static QString getLFTFileByPath(const QString &path)
{
    QString lft_file_name = LFTDiskTool::pathToSerialUri(path);

    if (lft_file_name.isEmpty())
        return QString();

    lft_file_name += ".lft";

    const QString &cache_path = getCacheDir();

    if (cache_path.isEmpty())
        return QString();

    return cache_path + "/" + lft_file_name.toUtf8().toPercentEncoding(":", "/");
}

bool LFTManager::addPath(QString path)
{
    if (!path.startsWith("/")) {
        sendErrorReply(QDBusError::InvalidArgs, "The path must start with '/'");

        return false;
    }

    if (_global_fsBufMap->contains(path)) {
        sendErrorReply(QDBusError::InternalError, "Index data already exists for this path");

        return false;
    }

    const QByteArray &serial_uri = LFTDiskTool::pathToSerialUri(path);

    if (serial_uri.isEmpty()) {
        sendErrorReply(QDBusError::InvalidArgs, "Unable to convert to serial uri");

        return false;
    }

    QFutureWatcher<fs_buf*> *watcher = new QFutureWatcher<fs_buf*>(this);
    // 此路径对应的设备可能被挂载到多个位置
    const QStringList &path_list = LFTDiskTool::fromSerialUri(serial_uri);

    // 将路径改为相对于第一个挂载点的路径，vfs_monitor中所有文件的改动都是以设备第一个挂载点通知的
    path = path_list.first();

    // 保存信息，用于判断索引是否正在构建
    for (const QString &path : path_list) {
        (*_global_fsBufMap)[path] = nullptr;
        (*_global_fsWatcherMap)[path] = watcher;
    }

    connect(watcher, &QFutureWatcher<fs_buf*>::finished, this, [this, path_list, watcher] {
        fs_buf *buf = watcher->result();

        for (const QString &path : path_list) {
            if (buf) {
                (*_global_fsBufMap)[path] = buf;
            } else {
                _global_fsBufMap->remove(path);
            }

            _global_fsWatcherMap->remove(path);

            Q_EMIT addPathFinished(path, buf);
        }

        watcher->deleteLater();
    });

    QFuture<fs_buf*> result = QtConcurrent::run(buildFSBuf, path.endsWith('/') ? path : path + "/");

    watcher->setFuture(result);

    return true;
}

static bool markLFTFileToDirty(fs_buf *buf)
{
    const char *root = get_root_path(buf);
    const QString &lft_file = getLFTFileByPath(QString::fromLocal8Bit(root));

    return QFile::remove(lft_file);
}

bool LFTManager::removePath(const QString &path)
{
    if (fs_buf *buf = _global_fsBufMap->take(path)) {
        markLFTFileToDirty(buf);
        free_fs_buf(buf);

        return true;
    }

    sendErrorReply(QDBusError::InvalidArgs, "Not found the index data");

    return false;
}

// 返回path对应的fs_buf对象，且将path转成为相对于fs_buf root_path的路径
static QList<QPair<QString, fs_buf*>> getFsBufByPath(const QString &path)
{
    if (!_global_fsBufMap.exists())
        return QList<QPair<QString, fs_buf*>>();

    if (!path.startsWith("/"))
        return QList<QPair<QString, fs_buf*>>();

    QStorageInfo storage_info(path);

    if (!storage_info.isValid())
        return QList<QPair<QString, fs_buf*>>();;

    QString result_path = path;
    QList<QPair<QString, fs_buf*>> buf_list;

    Q_FOREVER {
        fs_buf *buf = _global_fsBufMap->value(result_path, (fs_buf*)0x01);

        if (buf != (fs_buf*)0x01) {
            buf_list << qMakePair(result_path, buf);
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

    return !list.isEmpty() && list.first().second;
}

bool LFTManager::lftBuinding(const QString &path) const
{
    // 对应fs_buf存在且为nullptr认为正在构建
    auto list = getFsBufByPath(path);

    return !list.isEmpty() && list.first().second;
}

QStringList LFTManager::allPath() const
{
    if (!_global_fsBufMap.exists())
        return QStringList();

    QStringList list;

    for (auto i = _global_fsBufMap->constBegin(); i != _global_fsBufMap->constEnd(); ++i) {
        if (i.value()) { // 排除正在构建的目录
            list << i.key();
        }
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
    clearFsBufMap();

    const QString &cache_path = getCacheDir();
    QDirIterator dir_iterator(cache_path, {"*.lft"});
    QStringList path_list;

    while (dir_iterator.hasNext()) {
        const QString &lft_file = dir_iterator.next();

        // 根据地址过滤，只加载此挂载路径或其子路径对应的索引文件
        if (!serialUriFilter.isEmpty() && !dir_iterator.fileName().startsWith(serialUriFilter))
            continue;

        fs_buf *buf = nullptr;

        if (load_fs_buf(&buf, lft_file.toLocal8Bit().constData()) != 0)
            continue;

        const  QStringList pathList = LFTDiskTool::fromSerialUri(QByteArray::fromPercentEncoding(dir_iterator.fileName().toLocal8Bit()));

        for (QString path : pathList) {
            path.chop(4);// 去除 .lft 后缀
            path_list << path;
            (*_global_fsBufMap)[path] = buf;
        }

        if (pathList.isEmpty()) {
            free_fs_buf(buf);
        }
    }

    return path_list;
}

QStringList LFTManager::sync(const QString &mountPoint)
{
    QStringList path_list;

    if (!QDir::home().mkpath(getCacheDir())) {
        sendErrorReply(QDBusError::AccessDenied, "Failed on create path: " + getCacheDir());

        return path_list;
    }

    QList<fs_buf*> saved_buf_list;

    for (auto buf_begin = _global_fsBufMap->constBegin(); buf_begin != _global_fsBufMap->constEnd(); ++buf_begin) {
        fs_buf *buf = buf_begin.value();

        if (!buf)
            continue;

        const QString &path = buf_begin.key();

        // 只同步此挂载点的数据
        if (!mountPoint.isEmpty()) {
            QStorageInfo info(path);

            if (info.rootPath() != mountPoint) {
                continue;
            }
        }

        if (saved_buf_list.contains(buf)) {
            path_list << buf_begin.key();
            continue;
        }

        const QString &lft_file = getLFTFileByPath(path);

        if (save_fs_buf(buf, lft_file.toLocal8Bit().constData()) == 0) {
            saved_buf_list.append(buf);
            path_list << buf_begin.key();
        } else {
            path_list << QString("Failed: \"%1\"->\"%2\"").arg(buf_begin.key()).arg(lft_file);
        }
    }

    return path_list;
}

static int compareString(const char *string, void *keyword)
{
    return QString::fromLocal8Bit(string).indexOf(*static_cast<const QString*>(keyword), 0, Qt::CaseInsensitive) >= 0;
}

static int compareStringRegExp(const char *string, void *re)
{
    return static_cast<QRegularExpression*>(re)->match(QString::fromLocal8Bit(string)).hasMatch();
}

QStringList LFTManager::search(const QString &path, const QString keyword, bool useRegExp) const
{
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

    QString new_path = path.mid(buf_list.first().first.size());
    // fs_buf中的root_path以/结尾，所以此处直接拼接
    new_path.prepend(QString::fromLocal8Bit(get_root_path(buf)));

    uint32_t path_offset, start_offset, end_offset;
    get_path_range(buf, new_path.toLocal8Bit().constData(), &path_offset, &start_offset, &end_offset);

    QRegularExpression re(keyword);

    re.setPatternOptions(QRegularExpression::CaseInsensitiveOption
                         | QRegularExpression::DotMatchesEverythingOption
                         | QRegularExpression::OptimizeOnFirstUsageOption);

    void *compare_param = nullptr;
    int (*compare)(const char *, void *) = nullptr;

    if (useRegExp) {
        if (!re.isValid()) {
            sendErrorReply(QDBusError::InvalidArgs, "Invalid regular expression: " + re.errorString());

            return QStringList();
        }

        compare_param = &re;
        compare = compareStringRegExp;
    } else {
        compare_param = const_cast<QString*>(&keyword);
        compare = compareString;
    }

#define MAX_RESULT_COUNT 1000

    uint32_t name_offsets[MAX_RESULT_COUNT];
    uint32_t count = MAX_RESULT_COUNT;

    QStringList list;
    char tmp_path[PATH_MAX];
    // root_path 以/结尾，所以此处需要多忽略一个字符
    int buf_root_path_length = strlen(get_root_path(buf)) - 1;
    bool need_reset_root_path = path != new_path;

    do {
        search_files(buf, &start_offset, end_offset, compare_param, compare, name_offsets, &count);

        for (uint32_t i = 0; i < count; ++i) {
            const char *result = get_path_by_name_off(buf, name_offsets[i], tmp_path, sizeof(tmp_path));
            const QString &origin_path = QString::fromLocal8Bit(result);

            if (need_reset_root_path)
                list << path + origin_path.mid(buf_root_path_length);
            else
                list << origin_path;
        }
    } while (count == MAX_RESULT_COUNT);

    return list;
}

void LFTManager::insertFileToLFTBuf(const QByteArray &file)
{
    auto list = getFsBufByPath(QString::fromLocal8Bit(file));

    if (list.isEmpty())
        return;

    QFileInfo info(QString::fromLocal8Bit(file));
    bool is_dir = info.isDir();

    for (auto i : list) {
        fs_buf *buf = i.second;

        // 有可能索引正在构建
        if (!buf) {
            // 正在构建索引时需要等待
            if (QFutureWatcher<fs_buf*> *watcher = _global_fsWatcherMap->value(i.first)) {
                watcher->waitForFinished();
                buf = watcher->result();
            }

            if (!buf)
                continue;
        }

        fs_change change;
        insert_path(buf, file.constData(), is_dir, &change);

        // buf内容已改动，删除对应的lft文件
        markLFTFileToDirty(buf);
    }
}

void LFTManager::removeFileFromLFTBuf(const QByteArray &file)
{
    auto list = getFsBufByPath(QString::fromLocal8Bit(file));

    if (list.isEmpty())
        return;

    for (auto i : list) {
        fs_buf *buf = i.second;

        // 有可能索引正在构建
        if (!buf) {
            // 正在构建索引时需要等待
            if (QFutureWatcher<fs_buf*> *watcher = _global_fsWatcherMap->value(i.first)) {
                watcher->waitForFinished();
                buf = watcher->result();
            }

            if (!buf)
                continue;
        }

        fs_change change;
        uint32_t count;
        remove_path(buf, file.constData(), &change, &count);

        // buf内容已改动，删除对应的lft文件
        markLFTFileToDirty(buf);
    }
}

void LFTManager::renameFileOfLFTBuf(const QByteArray &oldFile, const QByteArray &newFile)
{
    // 此处期望oldFile是fs_buf的子文件（未处理同一设备不同挂载点的问题）
    auto list = getFsBufByPath(QString::fromLocal8Bit(oldFile));

    if (list.isEmpty())
        return;

    for (auto i : list) {
        fs_buf *buf = i.second;

        // 有可能索引正在构建
        if (!buf) {
            // 正在构建索引时需要等待
            if (QFutureWatcher<fs_buf*> *watcher = _global_fsWatcherMap->value(i.first)) {
                watcher->waitForFinished();
                buf = watcher->result();
            }

            if (!buf)
                continue;
        }

        fs_change change;
        uint32_t change_count;
        rename_path(buf, oldFile.constData(), newFile.constData(), &change, &change_count);

        // buf内容已改动，删除对应的lft文件
        markLFTFileToDirty(buf);
    }
}

void LFTManager::quit()
{
    qApp->quit();
}

static void cleanLFTManager()
{
    LFTManager::instance()->sync();
    clearFsBufMap();
}

LFTManager::LFTManager(QObject *parent)
    : QObject(parent)
{
    qAddPostRoutine(cleanLFTManager);
    refresh();

    connect(LFTDiskTool::diskManager(), &DFMDiskManager::mountAdded,
            this, &LFTManager::onMountAdded);
    connect(LFTDiskTool::diskManager(), &DFMDiskManager::mountRemoved,
            this, &LFTManager::onMountRemoved);

    QTimer *sync_timer = new QTimer(this);

    connect(sync_timer, &QTimer::timeout, this, &LFTManager::_syncAll);

    // 每30分钟将fs_buf回写到硬盘一次
    sync_timer->setInterval(30 * 60 * 1000);
    sync_timer->start();
}

void LFTManager::_syncAll()
{
    sync();
}

void LFTManager::onMountAdded(const QString &blockDevicePath, const QByteArray &mountPoint)
{
    Q_UNUSED(blockDevicePath)

    const QString &mount_root = QString::fromLocal8Bit(mountPoint);
    const QByteArray &serial_uri = LFTDiskTool::pathToSerialUri(mount_root);

    // 尝试加载此挂载点下的lft文件
    refresh(serial_uri.toPercentEncoding(":", "/"));
}

void LFTManager::onMountRemoved(const QString &blockDevicePath, const QByteArray &mountPoint)
{
    Q_UNUSED(blockDevicePath)

    const QString &mount_root = QString::fromLocal8Bit(mountPoint);
//    const QByteArray &serial_uri = LFTDiskTool::pathToSerialUri(mount_root);

    sync(mount_root);
}
