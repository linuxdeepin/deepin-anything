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
#include "lftdisktool.h"

#include <dblockpartition.h>
#include <ddiskmanager.h>

#include <QStorageInfo>
#include <QScopedPointer>
#include <QDebug>

extern "C" {
#include <libmount.h>
}

namespace LFTDiskTool {
Q_GLOBAL_STATIC(DDiskManager, _global_diskManager)

QByteArray pathToSerialUri(const QString &path)
{
    QDir dir(path);
    QStorageInfo storage_info(dir);

    if (!storage_info.isValid()) {
        return QByteArray();
    }

    // 为了和udisks2返回的device做比较，故加上\0结尾
    const QByteArray &device = storage_info.device() + '\0';

    if (!device.startsWith("/dev/"))
        return QByteArray();

    if (device.startsWith("/dev/loop"))
        return QByteArray();

    if (device == QByteArrayLiteral("/dev/fuse\0"))
        return QByteArray();

    // 为了和udisks2返回的device做比较，故加上\0结尾
    QByteArray mount_partition = QByteArray(path.toLocal8Bit()).append('\0');
    // 分区为加密盘时（TYPE：lvm），QStorageInfo::device() 返回“/dev/mapper/xxx”，与udisks2返回的“/dev/dm-x\0x00” 不匹配。
    // 使用挂载点匹配获取块设备。
    QScopedPointer<DBlockDevice> block_obj(_global_diskManager->createBlockPartitionByMountPoint(mount_partition));

    if (!block_obj || block_obj->isLoopDevice())
        return QByteArray();

    const QString block_id = block_obj->id();

    if (block_id.isEmpty())
        return QByteArray();

    const QString &absolute_path = dir.absolutePath();
    const QString &root_path = storage_info.rootPath();
    const auto mount_info_map = getMountPointsInfos({root_path.toLocal8Bit()});
    QByteArray mount_root_path;

    if (!mount_info_map.isEmpty()) {
        mount_root_path = mount_info_map.first().sourcePath;
    }

    if (mount_root_path.isEmpty())
        mount_root_path = "/";

    int right_length = absolute_path.length() - root_path.length();

    const QByteArray &uri = QByteArrayLiteral("serial:") + block_id.toLocal8Bit()
                            + mount_root_path + absolute_path.right(right_length).toLocal8Bit();

    return uri;
}

QByteArrayList fromSerialUri(const QByteArray &uri)
{
    if (!uri.startsWith("serial:"))
        return QByteArrayList();

    int path_start_pos = uri.indexOf('/', 7);

    if (path_start_pos < 0)
        return QByteArrayList();

    const QString block_id = QString::fromLocal8Bit(uri.mid(7, path_start_pos - 7));

    if (block_id.isEmpty())
        return QByteArrayList();

    const QByteArray path = uri.mid(path_start_pos);

    for (const QString &block : _global_diskManager->blockDevices()) {
        QScopedPointer<DBlockDevice> block_obj(_global_diskManager->createBlockPartition(block));
        const QString _block_id = block_obj->id();

        if (_block_id == block_id) {
            const QByteArrayList &mount_points = block_obj->mountPoints();
            const auto mount_point_infos = getMountPointsInfos(mount_points);
            QByteArrayList pathList;

            for (QByteArray mount_point : mount_points) {
                const MountPointInfo info = mount_point_infos.value(mount_point);
                QByteArray new_path = path;

                // 因为挂载点是以 '\0' 结尾的, 所以此处必须要把结尾的一个字符排除;
                mount_point.chop(1);

                if (!info.sourcePath.isEmpty()) {
                    if (!path.startsWith(info.sourcePath))
                        continue;

                    new_path = path.mid(info.sourcePath.size());
                }

                // 因为挂载点是以 '\0' 结尾的, 所以此处必须要分开转成QString;
                pathList << mount_point.append(new_path);
            }

            return pathList;
        }
    }

    return QByteArrayList();
}

DDiskManager *diskManager()
{
    return _global_diskManager;
}

/* error callback */
static int parser_errcb(libmnt_table *tb, const char *filename, int line)
{
    Q_UNUSED(tb)

    qWarning("%s: parse error at line %d -- ignored", filename, line);

    return 1;
}

struct SPMntTableDeleter
{
    static inline void cleanup(libmnt_table *pointer)
    {
        mnt_free_table(pointer);
    }
};

struct SPMntIterDeleter
{
    static inline void cleanup(libmnt_table *pointer)
    {
        mnt_free_table(pointer);
    }
};

QMap<QByteArray, MountPointInfo> getMountPointsInfos(const QByteArrayList &mountPointList)
{
    QMap<QByteArray, MountPointInfo> map;

    mnt_init_debug(0);

    QScopedPointer<libmnt_table, SPMntTableDeleter> tb(mnt_new_table());

    if (!tb) {
        return map;
    }

    mnt_table_set_parser_errcb(tb.data(), parser_errcb);

    int rc = mnt_table_parse_mtab(tb.data(), "/proc/self/mountinfo");

    if (rc) {
        qWarning("can't read /proc/self/mountinfo");

        return map;
    }

    for (const QByteArray &mount_point : mountPointList) {
        libmnt_fs *fs = mnt_table_find_mountpoint(tb.data(), mount_point.constData(), MNT_ITER_BACKWARD);

        if (fs) {
            MountPointInfo info;

            info.sourceDevice = QByteArray(mnt_fs_get_source(fs));
            info.sourcePath = QByteArray(mnt_fs_get_root(fs));

            map[mount_point] = info;
        } else {
            qWarning("can't find mountpoint \"%s\"", mount_point.constData());
        }
    }

    return map;
}

}


QDebug &operator <<(QDebug &deg, const LFTDiskTool::MountPointInfo &info)
{
    deg << "device:" << info.sourceDevice << ", path:" << info.sourcePath;

    return deg;
}
