// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mountcacher.h"
#include "logdefine.h"

#include <QFile>

extern "C" {
#include <libmount.h>
#include <sys/sysmacros.h>
}

DAS_BEGIN_NAMESPACE

class _MountCacher : public MountCacher {};
Q_GLOBAL_STATIC(_MountCacher, mountCacherGlobal)

/* error callback */
static int parser_errcb(libmnt_table *tb, const char *filename, int line)
{
    Q_UNUSED(tb)

    nWarning("%s: parse error at line %d -- ignored", filename, line);

    return 1;
}

struct SPMntTableDeleter
{
    static inline void cleanup(libmnt_table *pointer)
    {
        mnt_free_table(pointer);
    }
};

MountCacher *MountCacher::instance()
{
    return mountCacherGlobal;
}

MountCacher::MountCacher(QObject *parent)
    : QObject(parent)
{
    updateMountPoints();
}

MountCacher::~MountCacher()
{
    mountPointList.clear();
}

bool MountCacher::updateMountPoints()
{
    mnt_init_debug(0);

    QScopedPointer<libmnt_table, SPMntTableDeleter> tb(mnt_new_table());

    if (!tb) {
        return false;
    }

    mnt_table_set_parser_errcb(tb.data(), parser_errcb);

    // 使用"/proc/self/mountinfo" 否则导致NTFS挂载点被隐藏
    int rc = mnt_table_parse_mtab(tb.data(), "/proc/self/mountinfo");
    if (rc) {
        nWarning("can't read /proc/self/mountinfo");
        return false;
    }

    // 解析成功，清除之前的mount信息
    mountPointList.clear();

    // 向前查找，保存信息与"cat /proc/self/mountinfo"得到信息一致
    struct libmnt_iter *itr = mnt_new_iter(MNT_ITER_FORWARD);
    struct libmnt_fs *fs;

    while (mnt_table_next_fs(tb.data(), itr, &fs) == 0) {
        MountPoint info;
        info.deviceId = mnt_fs_get_devno(fs);
        info.mountType = QFile::decodeName(mnt_fs_get_fstype(fs));
        // major=0为非真实设备，且不为长文件名系统（dlnfs）的虚拟文件系统设备，比如：tmpfs bpf sysfs等，跳过。
        if (!major(info.deviceId) && info.mountType != "fuse.dlnfs") {
            //nDebug() << "ingore the virtual:" << info.mountType;
            continue;
        }
        info.mountedSource = QFile::decodeName(mnt_fs_get_source(fs));
        info.mountTarget = QFile::decodeName(mnt_fs_get_target(fs));
        info.mountRoot = QFile::decodeName(mnt_fs_get_root(fs));
        info.realDevice = info.mountedSource;

        //nDebug() << info;

        mountPointList << info;
    }

    mnt_free_iter(itr);

    return true;
}

QString MountCacher::findMountPointByPath(const QString &path, bool hardreal)
{
    QString result;
    QString result_path = path;

    Q_FOREVER {
        QByteArray checkpath = QFile::encodeName(result_path);
        char *mount_point = mnt_get_mountpoint(checkpath.data());
        if (nullptr != mount_point) {
            // nDebug() << path << " mountpoint: " << mount_point;
            result = QString(mount_point);
            free(mount_point);
            if (hardreal) {
                bool find_virtual = false;
                for (MountPoint info: mountPointList) {
                    // 找到挂载点相同，但是虚拟设备（major=0），向上一级找到真实设备挂载点
                    if (result == info.mountTarget && !major(info.deviceId)) {
                        // 赋值当前挂载点，进入向上一级目录
                        result_path = result;
                        find_virtual = true;
                        break;
                    }
                }
                if (!find_virtual) {
                    // 遍历完但是没有找到虚拟设备，返回当前挂载点
                    break;
                }
            } else {
                // 不需要向上找到真实设备挂载，直接返回
                break;
            }
        }

        // 已经向上找到根'/', 返回
        if (result_path == "/") {
            result = QString(result_path);
            break;
        }

        int last_dir_split_pos = result_path.lastIndexOf('/');
        if (last_dir_split_pos < 0) {
            break;
        }

        result_path = result_path.left(last_dir_split_pos);
        if (result_path.isEmpty()) {
            result_path = "/";
        }
    };
    return result;
}

bool MountCacher::pathMatchType(const QString &path, const QString &type)
{
    bool result= false;
    QString point = findMountPointByPath(path);

    for (MountPoint info: mountPointList) {
        if (point == info.mountTarget && type == info.mountType) {
            result = true;
            break;
        }
    }
    return result;
}

// 块设备存在多个挂载点，获取他们的根
QMap<QByteArray, QString> MountCacher::getRootsByPoints(const QByteArrayList &pointList)
{
    QMap<QByteArray, QString> map;

    for (const QByteArray &point : pointList) {
        const QString target = QString(point);
        for (MountPoint info: mountPointList) {
            if (target == info.mountTarget) {
                map[point] = QString(info.mountRoot);
            }
        }
    }

    return map;
}

QMap<QString, QString> MountCacher::getRootsByStrPoints(const QStringList &pointList)
{
    QMap<QString, QString> map;

    for (const QString &point : pointList) {
        for (const MountPoint &info: mountPointList) {
            if (point == info.mountTarget) {
                map[point] = info.mountRoot;
            }
        }
    }

    return map;
}

// 获取挂载点的设备（source）, 必须传入真实挂载点
QString MountCacher::getDeviceByPoint(const QString &point)
{
    QString device;

    for (MountPoint info: mountPointList) {
        if (point == info.mountTarget) {
            device = QString(info.realDevice);
            break;
        }
    }
    return device;
}

// 获取所有根为指定的挂载点, 不指定则返回全部挂载点信息
QList<MountPoint> MountCacher::getMountPointsByRoot(const QString &root)
{
    if (root == nullptr || !root.startsWith('/')) {
        return mountPointList;
    }

    QList<MountPoint> list;
    for (MountPoint info: mountPointList) {
        if (root == info.mountRoot) {
            list << info;
        }
    }
    return list;
}

QDebug operator<<(QDebug debug, const MountPoint &mp)
{
    QDebugStateSaver saver(debug);

    // clang-format off
    debug.nospace() << "MountPoint ["
                    << "id<major,minor>: "  << mp.deviceId
                    << ", from: "  << mp.mountedSource
                    << ", point: " << mp.mountTarget
                    << ", root: " << mp.mountRoot
                    << ", type: "  << mp.mountType
                    <<']';

    // clang-format on
    return debug;
}

DAS_END_NAMESPACE
