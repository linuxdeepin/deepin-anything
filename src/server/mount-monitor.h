// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MOUNT_MONITOR_H
#define MOUNT_MONITOR_H

#include <gio/gunixmounts.h>
#include <glib.h>

/**
 * mount_monitor_init:
 * @fstypes: (array zero-terminated=1): a %NULL-terminated array of filesystem
 *   type strings to track
 *
 * Performs an initial scan of unnamed devices and creates a
 * #GUnixMountMonitor that emits "mounts-changed" whenever the mount table
 * changes, triggering a rescan.
 *
 * Returns: (transfer full): a #GUnixMountMonitor.  Unref with g_object_unref()
 *   when done.
 */
GUnixMountMonitor *mount_monitor_init(GStrv fstypes);

#endif /* MOUNT_MONITOR_H */
