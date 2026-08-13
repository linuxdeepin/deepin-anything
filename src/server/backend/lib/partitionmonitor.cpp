// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "partitionmonitor.h"
#include "logdefine.h"
#include "mountcacher.h"

#include <QFile>
#include <QString>

DAS_BEGIN_NAMESPACE

PartitionMonitor::PartitionMonitor()
{
    updatePartitions();
}

void PartitionMonitor::updatePartitions()
{
    /* Invokes updateMountPoints in the event loop of MountCacher to avoid multi-threaded access */
    QMetaObject::invokeMethod(MountCacher::instance(), "updateMountPoints", Qt::QueuedConnection);

    /*
     * No use`MountCacher::instance()->getMountPointsByRoot("/")` to get mount list.
     * This is to avoid multi-threaded access, and locks may cause dead locks.
     */
    QString file_mountinfo_path("/proc/self/mountinfo");
    QFile file_mountinfo(file_mountinfo_path);
    if (!file_mountinfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray ba = file_mountinfo_path.toLatin1();
        nWarning("open file failed: %s.", ba.data());
        return;
    }
    QByteArray mount_info;
    mount_info = file_mountinfo.readAll();
    file_mountinfo.close();

    unsigned int major, minor;
    char mp[256], root[256], type[256], *line = mount_info.data();
    QSet<QByteArray> dlnfs_devs;
    QByteArray ba;
    partitions.clear();
    nInfo("updatePartitions start.");
    while (line && sscanf(line, "%*d %*d %u:%u %250s %250s %*s %*s %*s %250s %*s %*s\n", &major, &minor, root, mp, type) == 5) {
        line = strchr(line, '\n');
        if (line)
            ++line;

        if (!major && strcmp(type, "fuse.dlnfs"))
            continue;

        if (!strcmp(root, "/")) {
            partitions.insert(partition_mkdev(major, minor), QByteArray(mp));
            nInfo("%u:%u, %s", major, minor, mp);
            /* add monitoring for dlnfs device */
            if (!major && !strcmp(type, "fuse.dlnfs")) {
                ba.setNum(minor);
                dlnfs_devs.insert(ba);
            }
        }
    }
    update_vfs_unnamed_device(dlnfs_devs);
    nInfo("updatePartitions end.");
}

bool PartitionMonitor::contains(unsigned int major, unsigned int minor) const
{
    return partitions.contains(partition_mkdev(major, minor));
}

const char *PartitionMonitor::rootFor(unsigned int major, unsigned int minor) const
{
    auto it = partitions.find(partition_mkdev(major, minor));
    return it != partitions.end() ? it->constData() : nullptr;
}

void write_vfs_unnamed_device(const char *str)
{
    QString path("/sys/kernel/vfs_monitor/vfs_unnamed_devices");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QByteArray ba = path.toLatin1();
        nWarning("open file failed: %s.", ba.data());
        return;
    }
    file.write(str, strlen(str));
    file.close();
}

void read_vfs_unnamed_device(QSet<QByteArray> &devices)
{
    QString path("/sys/kernel/vfs_monitor/vfs_unnamed_devices");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QByteArray ba = path.toLatin1();
        nWarning("open file failed: %s.", ba.data());
        return;
    }
    QByteArray line = file.readLine();
    file.close();

    /* remove last \n */
    line.chop(1);
    QList<QByteArray> list = line.split(',');
    foreach (const QByteArray &minor, list) {
        devices.insert(minor);
    }
}

void update_vfs_unnamed_device(const QSet<QByteArray> &news)
{
    char buf[32];
    QSet<QByteArray> olds;
    read_vfs_unnamed_device(olds);

    QSet<QByteArray> removes(olds);
    removes.subtract(news);
    foreach (const QByteArray &minor, removes) {
        snprintf(buf, sizeof(buf), "r%s", minor.data());
        write_vfs_unnamed_device(buf);
    }

    QSet<QByteArray> adds(news);
    adds.subtract(olds);
    foreach (const QByteArray &minor, adds) {
        snprintf(buf, sizeof(buf), "a%s", minor.data());
        write_vfs_unnamed_device(buf);
    }
}

DAS_END_NAMESPACE
