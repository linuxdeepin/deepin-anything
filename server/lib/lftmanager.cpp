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
#include <dfmdiskdevice.h>

#include <QtConcurrent>
#include <QFutureWatcher>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QTimer>

#include <unistd.h>

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
Q_GLOBAL_STATIC_WITH_ARGS(QSettings, _global_settings, (getCacheDir() + "/config.ini", QSettings::IniFormat))

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
    _global_fsBufToFileMap->clear();
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

    if (lft_file.isEmpty())
        return false;

    return QFile::remove(lft_file);
}

static void cleanDirtyLFTFiles()
{
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

        qWarning() << "Failed on build fs buffer of path: " << path;

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

    const QString &cache_path = getCacheDir();

    if (cache_path.isEmpty())
        return QString();

    return cache_path + "/" + QString::fromLocal8Bit(lft_file_name.toPercentEncoding(":", "/"));
}

static bool allowableBuf(LFTManager *manager, fs_buf *buf)
{
    // 手动创建的buf不受属性autoIndexExternal/autoIndexInternal的控制
    if (_global_fsBufToFileMap->value(buf).endsWith(".lft"))
        return true;

    const char *root = get_root_path(buf);
    QStorageInfo info(QString::fromLocal8Bit(root));

    if (!info.isValid()) {
        return true;
    }

    QScopedPointer<DFMBlockPartition> device(LFTDiskTool::diskManager()->createBlockPartition(info, nullptr));

    if (!device) {
        return true;
    }

    QScopedPointer<DFMDiskDevice> disk(LFTDiskTool::diskManager()->createDiskDevice(device->drive()));

    if (disk->removable()) {
        return manager->autoIndexExternal();
    } else {
        return manager->autoIndexInternal();
    }
}

bool LFTManager::addPath(QString path, bool autoIndex)
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
    const QByteArrayList &path_list = LFTDiskTool::fromSerialUri(serial_uri);

    // 将路径改为相对于第一个挂载点的路径，vfs_monitor中所有文件的改动都是以设备第一个挂载点通知的
    path = path_list.first();

    // 保存信息，用于判断索引是否正在构建
    for (const QByteArray &path_raw : path_list) {
        const QString &path = QString::fromLocal8Bit(path_raw);

        (*_global_fsBufMap)[path] = nullptr;
        (*_global_fsWatcherMap)[path] = watcher;
    }

    connect(watcher, &QFutureWatcher<fs_buf*>::finished, this, [this, path_list, path, watcher, autoIndex] {
        fs_buf *buf = watcher->result();

        if (autoIndex && !allowableBuf(this, buf)) {
            qWarning() << "Discarded index data of path:" << path;

            free_fs_buf(buf);
            buf = nullptr;
        }

        for (const QByteArray &path_raw : path_list) {
            const QString &path = QString::fromLocal8Bit(path_raw);

            if (buf) {
                (*_global_fsBufMap)[path] = buf;
            } else {
                _global_fsBufMap->remove(path);
            }

            _global_fsWatcherMap->remove(path);

            Q_EMIT addPathFinished(path, buf);
        }

        if (buf) {
            _global_fsBufToFileMap->insert(buf, getLFTFileByPath(path, autoIndex));
        }

        watcher->deleteLater();
    });

    QFuture<fs_buf*> result = QtConcurrent::run(buildFSBuf, path.endsWith('/') ? path : path + "/");

    watcher->setFuture(result);

    return true;
}

static void removeBuf(fs_buf *buf, bool removeLFTFile = true)
{
    for (const QString &other_key : _global_fsBufMap->keys(buf)) {
        _global_fsBufMap->remove(other_key);
    }

    if (removeLFTFile)
        doLFTFileToDirty(buf);

    _global_fsBufDirtyList->remove(buf);
    _global_fsBufToFileMap->remove(buf);
    free_fs_buf(buf);
}

bool LFTManager::removePath(const QString &path)
{
    if (fs_buf *buf = _global_fsBufMap->take(path)) {
        if (_global_fsBufToFileMap->value(buf).endsWith(".LFT")) {
            // 不允许通过此接口删除由自动索引创建的数据
            sendErrorReply(QDBusError::NotSupported, "Deleting data created by automatic indexing is not supported");

            return false;
        }

        removeBuf(buf);

        //此处被删除的是手动添加的索引数据, 之后应该更新自动生成的索引数据, 因为此目录之前一直
        //使用手动生成的数据,现在数据被删了,但目录本身有可能是满足为其自动生成索引数据的要求
        QStorageInfo info(path);

        if (info.isValid())
            onMountAdded(QString(), info.rootPath().toLocal8Bit());
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
        fs_buf *buf = _global_fsBufMap->value(result_path, (fs_buf*)0x01);

        if (buf != (fs_buf*)0x01) {
            if (!buf) {
                buf_list << qMakePair(path, buf);
            } else {
                // path相对于此fs_buf root_path的路径
                QString new_path = path.mid(result_path.size());

                // 移除多余的 / 字符
                if (new_path.startsWith("/"))
                    new_path = new_path.mid(1);

                // fs_buf中的root_path以/结尾，所以此处直接拼接
                new_path.prepend(QString::fromLocal8Bit(get_root_path(buf)));

                // 移除多余的 / 字符
                if (new_path.endsWith("/"))
                    new_path.chop(1);

                buf_list << qMakePair(new_path, buf);
            }
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

    return !list.isEmpty() && !list.first().second;
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
        if (i.value() && (i.key() + "/").startsWith(path))
            list << i.key();
    }

    return list;
}

// 重新从磁盘加载lft文件
QStringList LFTManager::refresh(const QByteArray &serialUriFilter)
{
    const QString &cache_path = getCacheDir();
    QDirIterator dir_iterator(cache_path, {"*.lft"});
    QStringList path_list;

    while (dir_iterator.hasNext()) {
        const QString &lft_file = dir_iterator.next();

        // 根据地址过滤，只加载此挂载路径或其子路径对应的索引文件
        if (!serialUriFilter.isEmpty() && !dir_iterator.fileName().startsWith(serialUriFilter))
            continue;

        const QByteArrayList pathList = LFTDiskTool::fromSerialUri(QByteArray::fromPercentEncoding(dir_iterator.fileName().toLocal8Bit()));

        if (pathList.isEmpty()) {
            continue;
        }

        fs_buf *buf = nullptr;

        if (load_fs_buf(&buf, lft_file.toLocal8Bit().constData()) != 0)
            continue;

        if (!buf) {
            qWarning() << "Failed on load:" << lft_file;
            continue;
        }

        for (const QByteArray &path_raw : pathList) {
            QString path = QString::fromLocal8Bit(path_raw);

            path.chop(4);// 去除 .lft 后缀
            path_list << path;

            // 清理旧的buf
            if (fs_buf *buf = _global_fsBufMap->value(path)) {
                removeBuf(buf, false);
            }

            (*_global_fsBufMap)[path] = buf;
        }

        _global_fsBufToFileMap->insert(buf, dir_iterator.fileName());
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
            if (!(path + "/").startsWith(mountPoint + '/')) {
                continue;
            }
        }

        if (saved_buf_list.contains(buf)) {
            path_list << buf_begin.key();
            continue;
        }

        const QString &lft_file = _global_fsBufToFileMap->value(buf);

        if (lft_file.isEmpty()) {
            qWarning() << "Can't get the LFT file path of the fs_buf:" << get_root_path(buf);

            continue;
        }

        if (save_fs_buf(buf, lft_file.toLocal8Bit().constData()) == 0) {
            saved_buf_list.append(buf);
            path_list << buf_begin.key();
            // 从脏列表中移除
            _global_fsBufDirtyList->remove(buf);
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
    auto match = static_cast<QRegularExpression*>(re)->match(QString::fromLocal8Bit(string));

    return match.hasMatch();
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

    // new_path 为path在fs_buf中对应的路径
    const QString &new_path = buf_list.first().first;
    uint32_t path_offset = 0, start_offset = 0, end_offset = 0;
    get_path_range(buf, new_path.toLocal8Bit().constData(), &path_offset, &start_offset, &end_offset);

    // 说明目录为空
    if (start_offset == 0)
        return QStringList();

    QRegularExpression re(keyword);

    void *compare_param = nullptr;
    int (*compare)(const char *, void *) = nullptr;

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

#define MAX_RESULT_COUNT 1000

    uint32_t name_offsets[MAX_RESULT_COUNT];
    uint32_t count = MAX_RESULT_COUNT;

    QStringList list;
    char tmp_path[PATH_MAX];
    // root_path 以/结尾，所以此处需要多忽略一个字符
    bool need_reset_root_path = path != new_path;

    do {
        search_files(buf, &start_offset, end_offset, compare_param, compare, name_offsets, &count);

        for (uint32_t i = 0; i < count; ++i) {
            const char *result = get_path_by_name_off(buf, name_offsets[i], tmp_path, sizeof(tmp_path));
            const QString &origin_path = QString::fromLocal8Bit(result);

            if (need_reset_root_path) {
                list << path + origin_path.mid(new_path.size());
             } else {
                list << origin_path;
            }
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
        int r = insert_path(buf, i.first.toLocal8Bit().constData(), is_dir, &change);

        if (r == 0) {
            // buf内容已改动，标记删除对应的lft文件
            markLFTFileToDirty(buf);
        }
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

        fs_change changes[10];
        uint32_t count = 10;
        int r = remove_path(buf, i.first.toLocal8Bit().constData(), changes, &count);

        if (r == 0) {
            // buf内容已改动，标记删除对应的lft文件
            markLFTFileToDirty(buf);
        }
    }
}

void LFTManager::renameFileOfLFTBuf(const QByteArray &oldFile, const QByteArray &newFile)
{
    auto list = getFsBufByPath(QString::fromLocal8Bit(newFile));

    if (list.isEmpty())
        return;

    for (int i = 0; i < list.count(); ++i) {
        fs_buf *buf = list.at(i).second;

        // 有可能索引正在构建
        if (!buf) {
            // 正在构建索引时需要等待
            if (QFutureWatcher<fs_buf*> *watcher = _global_fsWatcherMap->value(list.at(i).first)) {
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

        int r = rename_path(buf, old_file_new_path.constData(), new_file_new_path.constData(), changes, &change_count);

        if (r == 0) {
            // buf内容已改动，标记删除对应的lft文件
            markLFTFileToDirty(buf);
        }
    }
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

void LFTManager::setAutoIndexExternal(bool autoIndexExternal)
{
    if (this->autoIndexExternal() == autoIndexExternal)
        return;

    _global_settings->setValue("autoIndexExternal", autoIndexExternal);

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

    if (autoIndexInternal) {
        _indexAll();
    } else {
        _cleanAllIndex();
    }

    emit autoIndexInternalChanged(autoIndexInternal);
}

static void cleanLFTManager()
{
    LFTManager::instance()->sync();
    clearFsBufMap();
    cleanDirtyLFTFiles();
}

LFTManager::LFTManager(QObject *parent)
    : QObject(parent)
{
    qAddPostRoutine(cleanLFTManager);
    refresh();
    // 可能会加载到一些自动生成的未被允许的索引文件, 此处应该清理一遍
    _cleanAllIndex();

    if (_isAutoIndexPartition())
        _indexAll();

    connect(LFTDiskTool::diskManager(), &DFMDiskManager::mountAdded,
            this, &LFTManager::onMountAdded);
    connect(LFTDiskTool::diskManager(), &DFMDiskManager::mountRemoved,
            this, &LFTManager::onMountRemoved);

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
        qWarning() << type << msg;
    }
}

bool LFTManager::_isAutoIndexPartition() const
{
    return autoIndexExternal() || autoIndexInternal();
}

void LFTManager::_syncAll()
{
    sync();
    // 清理sync失败的脏文件
    cleanDirtyLFTFiles();
}

void LFTManager::_indexAll()
{
    // 遍历已挂载分区, 看是否需要为其建立索引数据
    for (const QString &block : LFTDiskTool::diskManager()->blockDevices()) {
        if (!DFMBlockDevice::hasFileSystem(block))
            continue;

        DFMBlockDevice *device = DFMDiskManager::createBlockDevice(block);

        if (device->isLoopDevice())
            continue;

        if (device->mountPoints().isEmpty())
            continue;

        if (!hasLFT(QString::fromLocal8Bit(device->mountPoints().first())))
            _addPathByPartition(device);
    }
}

void LFTManager::_cleanAllIndex()
{
    for (fs_buf *buf : fsBufList()) {
        if (!buf)
            continue;

        if (!allowableBuf(this, buf)) {
            removeBuf(buf);
        }
    }
}

void LFTManager::_addPathByPartition(const DFMBlockDevice *block)
{
    if (DFMDiskDevice *device = LFTDiskTool::diskManager()->createDiskDevice(block->drive())) {
        bool index = false;

        if (device->removable()) {
            index = autoIndexExternal();
        } else {
            index = autoIndexInternal();
        }

        if (index) // 建立索引时一切以第一个挂载点为准
            addPath(QString::fromLocal8Bit(block->mountPoints().first()), true);

        device->deleteLater();
    }
}

void LFTManager::onMountAdded(const QString &blockDevicePath, const QByteArray &mountPoint)
{
    Q_UNUSED(blockDevicePath)

    const QString &mount_root = QString::fromLocal8Bit(mountPoint);
    const QByteArray &serial_uri = LFTDiskTool::pathToSerialUri(mount_root);

    if (!_isAutoIndexPartition())
        return;

    // 尝试加载此挂载点下的lft文件
    const QStringList &list = refresh(serial_uri.toPercentEncoding(":", "/"));

    if (list.contains(QString::fromLocal8Bit(mountPoint))) {
        return;
    }

    if (DFMBlockDevice *block = LFTDiskTool::diskManager()->createBlockPartitionByMountPoint(mountPoint)) {
        if (!block->isLoopDevice()) {
            _addPathByPartition(block);
        }

        block->deleteLater();
    }
}

void LFTManager::onMountRemoved(const QString &blockDevicePath, const QByteArray &mountPoint)
{
    Q_UNUSED(blockDevicePath)

    const QString &mount_root = QString::fromLocal8Bit(mountPoint);
//    const QByteArray &serial_uri = LFTDiskTool::pathToSerialUri(mount_root);

    sync(mount_root);
}
