// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mount-monitor.h"
#include "vfs-unnamed-device.h"

static void mounts_changed(G_GNUC_UNUSED GUnixMountMonitor *mount_monitor,
                           gpointer user_data)
{
    GStrv fstypes = user_data;

    GList *devices = get_unnamed_device_by_fstype(fstypes);
    update_vfs_unnamed_device(devices);
    g_list_free_full(devices, g_free);
}

GUnixMountMonitor *mount_monitor_init(GStrv fstypes)
{
    /* initial scan */
    mounts_changed(NULL, fstypes);

    GUnixMountMonitor *monitor = g_unix_mount_monitor_get();
    g_signal_connect(monitor,
                     "mounts-changed",
                     G_CALLBACK(mounts_changed),
                     fstypes);
    return monitor;
}
