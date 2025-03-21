// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 - 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lftdisktool.h"
#include "logdefine.h"
#include "mountcacher.h"

#include <dblockpartition.h>
#include <ddiskmanager.h>

extern "C" {
#include <libmount.h>
}


namespace LFTDiskTool {
Q_GLOBAL_STATIC(DDiskManager, _global_diskManager)
Q_LOGGING_CATEGORY(logN, "anything.normal.disktool", DEFAULT_MSG_TYPE)

QByteArray pathToSerialUri(const QString &path)
{
    // 避免使用QDir/QStorageInfo, 如果IO被阻塞（U盘正在被扫描或网络盘断网），则导致卡顿或假死
    // 向上找到路径的真实挂载点
    const QString mountPoint = deepin_anything_server::MountCacher::instance()->findMountPointByPath(path);
    if (mountPoint.isEmpty()) {
        nWarning() << "pathToSerialUri findMountPointByPath NULL for:" << path;
        return QByteArray();
    }

    const QString device = deepin_anything_server::MountCacher::instance()->getDeviceByPoint(mountPoint);
    if (!device.startsWith("/dev/") || device.startsWith("/dev/loop") || device.compare("/dev/fuse") == 0) {
        nWarning() << "ingore device:" << device;
        return QByteArray();
    }

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

    const auto mount_info_map = deepin_anything_server::MountCacher::instance()->getRootsByPoints({mountPoint.toLocal8Bit()});
    QByteArray mount_root_path;

    // 只获取一个，返回就取第一个值
    if (!mount_info_map.isEmpty()) { 
        mount_root_path = mount_info_map.first().toLocal8Bit();
    }

    if (mount_root_path.isEmpty())
        mount_root_path = "/";

    int right_length = path.length() - mountPoint.length();

    const QByteArray &uri = QByteArrayLiteral("serial:") + block_id.toLocal8Bit()
                            + mount_root_path + path.right(right_length).toLocal8Bit();

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

    QVariantMap option;
    for (const QString &block : _global_diskManager->blockDevices(option)) {
        QScopedPointer<DBlockDevice> block_obj(_global_diskManager->createBlockPartition(block));
        const QString _block_id = block_obj->id();

        if (_block_id == block_id) {
            const QByteArrayList &mount_points = block_obj->mountPoints();
            const auto mount_point_infos = deepin_anything_server::MountCacher::instance()->getRootsByPoints(mount_points);
            QByteArrayList pathList;

            for (QByteArray mount_point : mount_points) {
                const QString root = mount_point_infos.value(mount_point);
                QByteArray new_path = path;

                // 因为挂载点是以 '\0' 结尾的, 所以此处必须要把结尾的一个字符排除;
                mount_point.chop(1);

                if (!root.isEmpty()) {
                    if (!path.startsWith(root.toLocal8Bit()))
                        continue;

                    new_path = path.mid(root.size());
                }

                if (new_path.isEmpty()) {
                    pathList << mount_point;
                } else {
                    if (!mount_point.endsWith('/'))
                        mount_point.append("/");
                    pathList << mount_point.append(new_path);
                }
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

}
