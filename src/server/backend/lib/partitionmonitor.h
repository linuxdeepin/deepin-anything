// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PARTITIONMONITOR_H
#define PARTITIONMONITOR_H

#include "dasdefine.h"

#include <QByteArray>
#include <QMap>
#include <QSet>

DAS_BEGIN_NAMESPACE

/**
 * Encodes a (major, minor) device pair into the key used by the partition map.
 * Keep this in sync with the kernel's encoding of dev_t in mountinfo (the
 * value read from /proc/self/mountinfo is already in the major:minor form
 * shown there, but the map collapses the pair into a single unsigned int).
 */
static inline unsigned int partition_mkdev(unsigned int major, unsigned int minor)
{
    return (major << 8) | minor;
}

/**
 * PartitionMonitor owns the partition map derived from /proc/self/mountinfo
 * and the unnamed-device list kept in sync with the kernel via
 * /sys/kernel/vfs_monitor/vfs_unnamed_devices.
 *
 * These duties were previously embedded in EventSource_GENL.  Lifting them
 * into a standalone class lets any EventSource implementation (genl, fanotify,
 * ...) share the same partition resolution and unnamed-device bookkeeping.
 */
class PartitionMonitor
{
public:
    PartitionMonitor();

    /** Re-read /proc/self/mountinfo and rebuild the partition map. */
    void updatePartitions();

    /** Whether (major,minor) is tracked as a watched partition. */
    bool contains(unsigned int major, unsigned int minor) const;

    /** Root mount point for the given device key, or nullptr if unknown. */
    const char *rootFor(unsigned int major, unsigned int minor) const;

private:
    QMap<unsigned int, QByteArray> partitions;
};

/* --- unnamed-device helpers (used by updatePartitions) --- */

void write_vfs_unnamed_device(const char *str);
void read_vfs_unnamed_device(QSet<QByteArray> &devices);
void update_vfs_unnamed_device(const QSet<QByteArray> &news);

DAS_END_NAMESPACE

#endif // PARTITIONMONITOR_H
