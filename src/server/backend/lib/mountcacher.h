// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MOUNTCACHER_H
#define MOUNTCACHER_H

#include "dasdefine.h"

#include <QDebug>
#include <QObject>

DAS_BEGIN_NAMESPACE

class MountPoint
{
public:
    QString mountedSource;
    QString realDevice;
    QString mountTarget;
    QString mountRoot;
    QString mountType;
    dev_t deviceId = 0;
};

class MountCacher : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(MountCacher)

public:
    ~MountCacher();
    static MountCacher *instance();
    QString findMountPointByPath(const QString &path, bool hardreal = false);
    bool pathMatchType(const QString &path, const QString &type);

    // 块设备存在多个挂载点，获取他们的根
    QMap<QByteArray, QString> getRootsByPoints(const QByteArrayList &pointList);
    QMap<QString, QString> getRootsByStrPoints(const QStringList &pointList);

    // 获取挂载点的设备（source）, 必须传入真实挂载点
    QString getDeviceByPoint(const QString &point);

    // 获取所有根为指定的挂载点, 不指定则返回全部挂载点信息
    QList<MountPoint> getMountPointsByRoot(const QString &root = nullptr);

public slots:
    bool updateMountPoints();

protected:
    explicit MountCacher(QObject *parent = nullptr);

private:
    QList<MountPoint> mountPointList;
};

QDebug operator<<(QDebug debug, const MountPoint &mp);

DAS_END_NAMESPACE

#endif // MOUNTCACHER_H
