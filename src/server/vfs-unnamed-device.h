// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VFS_UNNAMED_DEVICE_H
#define VFS_UNNAMED_DEVICE_H

#include <glib.h>

/**
 * get_unnamed_device_by_fstype:
 * @fstypes: (array zero-terminated=1): a %NULL-terminated array of filesystem
 *   type strings
 *
 * Scans the mount table for unnamed devices (major 0) whose filesystem type
 * matches one of @fstypes.  Each matching device is represented by its minor
 * number as a newly-allocated string.
 *
 * Returns: (transfer full) (element-type gchar*): a newly-allocated list of
 *   minor-number strings.  Free with g_list_free_full() and g_free() when done.
 */
GList *get_unnamed_device_by_fstype(GStrv fstypes);

/**
 * update_vfs_unnamed_device:
 * @news: (transfer full) (element-type gchar*): the new set of device minor
 *   numbers, as returned by get_unnamed_device_by_fstype().  The list and its
 *   data are consumed by this function.
 *
 * Compares @news against the previously recorded set stored in the kernel
 * VFS monitor sysfs file and writes "a<minor>" / "r<minor>" entries for
 * added / removed devices respectively.
 */
void update_vfs_unnamed_device(GList *news);

#endif /* VFS_UNNAMED_DEVICE_H */
