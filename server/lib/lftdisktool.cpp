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
#include "lftdisktool.h"

#include <dfmblockpartition.h>
#include <dfmdiskmanager.h>

#include <QStorageInfo>
#include <QScopedPointer>
#include <QDebug>

namespace LFTDiskTool {
Q_GLOBAL_STATIC(DFMDiskManager, _global_diskManager)

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

    QScopedPointer<DFMBlockDevice> block_obj(_global_diskManager->createBlockDeviceByDevicePath(device));

    if (!block_obj || block_obj->isLoopDevice())
        return QByteArray();

    const QString block_id = block_obj->id();

    if (block_id.isEmpty())
        return QByteArray();

    const QString &absolute_path = dir.absolutePath();
    const QString &root_path = storage_info.rootPath();
    int right_length = absolute_path.length() - root_path.length();

    const QByteArray &uri = QByteArrayLiteral("serial:") + block_id.toLocal8Bit()
                            + "/" + absolute_path.right(right_length).toLocal8Bit();

    return uri;
}

QStringList fromSerialUri(const QByteArray &uri)
{
    if (!uri.startsWith("serial:"))
        return QStringList();

    int path_start_pos = uri.indexOf('/', 7);

    if (path_start_pos < 0)
        return QStringList();

    const QString block_id = QString::fromLocal8Bit(uri.mid(7, path_start_pos - 7));

    if (block_id.isEmpty())
        return QStringList();

    QString path = QString::fromLocal8Bit(uri.mid(path_start_pos));

    for (const QString &block : _global_diskManager->blockDevices()) {
        QScopedPointer<DFMBlockDevice> block_obj(_global_diskManager->createBlockPartition(block));
        const QString _block_id = block_obj->id();

        if (_block_id == block_id) {
            const QByteArrayList &mount_points = block_obj->mountPoints();
            QStringList pathList;

            path = path.mid(1);

            for (const QByteArray &mount_point : mount_points) {
                pathList << QString::fromLocal8Bit(mount_point) + path;
            }

            return pathList;
        }
    }

    return QStringList();
}

DFMDiskManager *diskManager()
{
    return _global_diskManager;
}

}
