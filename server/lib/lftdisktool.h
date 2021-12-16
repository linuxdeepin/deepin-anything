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
#ifndef LFTDISKTOOL_H
#define LFTDISKTOOL_H

#include <QString>
#include <QByteArrayList>

class DDiskManager;
namespace LFTDiskTool
{
struct MountPointInfo {
    QByteArray sourceDevice;
    QByteArray sourcePath;
};

QMap<QByteArray, MountPointInfo> getMountPointsInfos(const QByteArrayList &mountPointList);

QByteArray pathToSerialUri(const QString &path);
QByteArrayList fromSerialUri(const QByteArray &uri);

DDiskManager *diskManager();
}

QDebug &operator <<(QDebug &deg, const LFTDiskTool::MountPointInfo &info);

#endif // LFTDISKTOOL_H
