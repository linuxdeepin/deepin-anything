// SPDX-FileCopyrightText: 2026 Uniontech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "networkmountdetector.h"

#include <QDebug>
#include <QFileInfo>
#include <QReadLocker>
#include <QWriteLocker>

#include <libmount.h>

#include <memory>

ANYTHING_INDEX_BEGIN_NAMESPACE

namespace {

// Non-FUSE filesystems that are always network-backed.
const QSet<QString> &networkFsTypes()
{
    static const QSet<QString> set = {
        QStringLiteral("nfs"),      QStringLiteral("nfs4"),
        QStringLiteral("cifs"),     QStringLiteral("smb3"),
        QStringLiteral("sshfs"),    QStringLiteral("davfs"),
        QStringLiteral("davfs2"),   QStringLiteral("9p"),
        QStringLiteral("ceph"),     QStringLiteral("glusterfs"),
        QStringLiteral("lustre"),   QStringLiteral("ocfs2"),
        QStringLiteral("gfs2"),
    };
    return set;
}

// FUSE subtypes that are known to be local.
// Everything else FUSE is conservatively treated as network.
const QSet<QString> &localFuseSubtypes()
{
    static const QSet<QString> set = {
        QStringLiteral("dlnfs"),    QStringLiteral("ulnfs"),
        QStringLiteral("ntfs"),     QStringLiteral("exfat"),
        QStringLiteral("vfat"),     QStringLiteral("iso9660"),
        QStringLiteral("bind"),     QStringLiteral("mergerfs"),
        QStringLiteral("unionfs"),
    };
    return set;
}

// RAII wrapper for libmount table.
struct MntTableDeleter {
    void operator()(libmnt_table *t) const { mnt_free_table(t); }
};
using MntTablePtr = std::unique_ptr<libmnt_table, MntTableDeleter>;

struct MntIterDeleter {
    void operator()(libmnt_iter *it) const { mnt_free_iter(it); }
};
using MntIterPtr = std::unique_ptr<libmnt_iter, MntIterDeleter>;

}   // namespace

NetworkMountDetector &NetworkMountDetector::instance()
{
    static NetworkMountDetector detector;
    return detector;
}

NetworkMountDetector::NetworkMountDetector()
{
    parseMountInfo();
}

bool NetworkMountDetector::isNetworkPath(const QString &path) const
{
    if (path.isEmpty())
        return false;

    QReadLocker locker(&m_lock);

    // Resolve symlinks so that a symlink into a network mount is caught.
    // canonicalFilePath() returns empty if the target doesn't exist (e.g.
    // network mount is down); fall back to the raw path in that case.
    QString resolved = QFileInfo(path).canonicalFilePath();
    if (resolved.isEmpty())
        resolved = path;

    for (const QString &mountPoint : m_networkMountPoints) {
        if (resolved == mountPoint || resolved.startsWith(mountPoint + QLatin1Char('/')))
            return true;
    }
    return false;
}

void NetworkMountDetector::refresh()
{
    parseMountInfo();
}

void NetworkMountDetector::parseMountInfo()
{
    QWriteLocker locker(&m_lock);
    QSet<QString> mounts;

    MntTablePtr table(mnt_new_table());
    if (!table || mnt_table_parse_mtab(table.get(), nullptr) < 0) {
        qWarning() << "NetworkMountDetector: failed to parse mount table";
        return;
    }

    MntIterPtr iter(mnt_new_iter(MNT_ITER_FORWARD));
    if (!iter)
        return;

    libmnt_fs *fs = nullptr;
    while (mnt_table_next_fs(table.get(), iter.get(), &fs) == 0) {
        const char *target = mnt_fs_get_target(fs);
        if (!target || !*target)
            continue;

        if (isNetworkFilesystem(mnt_fs_get_fstype(fs), mnt_fs_get_source(fs)))
            mounts.insert(QString::fromUtf8(target));
    }

    m_networkMountPoints = std::move(mounts);
    if (!m_networkMountPoints.isEmpty())
        qDebug() << "NetworkMountDetector: detected network mount points:" << m_networkMountPoints;
}

bool NetworkMountDetector::isNetworkFilesystem(const char *fstype, const char *source)
{
    if (!fstype || !*fstype)
        return false;

    QString fs = QString::fromUtf8(fstype);

    // Non-FUSE: network type → network; everything else → local.
    if (!fs.startsWith(QLatin1String("fuse")))
        return networkFsTypes().contains(fs);

    // FUSE: known local subtype → local.
    QString subtype = fs.startsWith(QLatin1String("fuse.")) ? fs.mid(5) : QString();
    if (localFuseSubtypes().contains(subtype))
        return false;

    // FUSE with a URL scheme in source (ftp://, smb://, …) → network.
    if (source && *source && QString::fromUtf8(source).contains(QLatin1String("://")))
        return true;

    // Unknown FUSE: conservatively treat as network.
    return true;
}

ANYTHING_INDEX_END_NAMESPACE
