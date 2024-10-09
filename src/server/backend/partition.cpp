#include "partition.h"
#include "mountcacher.h"
#include "logdefine.h"
#include <QFile>


namespace anything {


bool partition::update()
{
    /* Invokes updateMountPoints in the event loop of MountCacher to avoid multi-threaded access */
    QMetaObject::invokeMethod(deepin_anything_server::MountCacher::instance(), "updateMountPoints", Qt::QueuedConnection);

    /*
     * No use`MountCacher::instance()->getMountPointsByRoot("/")` to get mount list.
     * This is to avoid multi-threaded access, and locks may cause dead locks.
     */
    QString file_mountinfo_path("/proc/self/mountinfo");
    QFile file_mountinfo(file_mountinfo_path);
    if (!file_mountinfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray ba = file_mountinfo_path.toLatin1();
        nWarning("open file failed: %s.", ba.data());
        return false;
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
    while (sscanf(line, "%*d %*d %u:%u %250s %250s %*s %*s %*s %250s %*s %*s\n", &major, &minor, root, mp, type) == 5) {
        line = strchr(line, '\n') + 1;

        if (!major && strcmp(type, "fuse.dlnfs"))
            continue;

        if (!strcmp(root, "/")) {
            partitions.insert(MKDEV(major, minor), QByteArray(mp));
            nInfo("%u:%u, %s", major, minor, mp);
            /* add monitoring for dlnfs device */
            if (!major && !strcmp(type, "fuse.dlnfs")) {
                ba.setNum(minor);
                dlnfs_devs.insert(ba);
            }
        }
    }
    
    return update_vfs_unnamed_device(dlnfs_devs);
}

bool partition::path_match_type(std::string path, std::string type)
{
    return deepin_anything_server::MountCacher::instance()->pathMatchType(
        QString::fromStdString(std::move(path)),
        QString::fromStdString(std::move(type)));
}

bool partition::write_vfs_unnamed_device(const char *str)
{
    QString path("/sys/kernel/vfs_monitor/vfs_unnamed_devices");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QByteArray ba = path.toLatin1();
        nWarning("open file failed: %s.(write_vfs_unnamed_device)", ba.data());
        return false;
    }
    file.write(str, strlen(str));
    file.close();

    return true;
}

bool partition::read_vfs_unnamed_device(QSet<QByteArray>& devices)
{
    QString path("/sys/kernel/vfs_monitor/vfs_unnamed_devices");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QByteArray ba = path.toLatin1();
        nWarning("open file failed: %s.(read_vfs_unnamed_device)", ba.data());
        return false;
    }
    QByteArray line = file.readLine();
    file.close();
    
    /* remove last \n */
    line.chop(1);
    QList<QByteArray> list = line.split(',');
    foreach (const QByteArray &minor, list) {
        devices.insert(minor);
    }

    return true;
}

bool partition::update_vfs_unnamed_device(const QSet<QByteArray>& news)
{
    char buf[32];
    QSet<QByteArray> olds;
    
    if (!read_vfs_unnamed_device(olds))
        return false;

    QSet<QByteArray> removes(olds);
    removes.subtract(news);
    foreach (const QByteArray &minor, removes) {
        snprintf(buf, sizeof(buf), "r%s", minor.data());
        if (!write_vfs_unnamed_device(buf))
            return false;
    }

    QSet<QByteArray> adds(news);
    adds.subtract(olds);
    foreach (const QByteArray &minor, adds) {
        snprintf(buf, sizeof(buf), "a%s", minor.data());
        if (!write_vfs_unnamed_device(buf))
            return false;
    }

    return true;
}

} // namespace anything