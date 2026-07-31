// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SYSFS_H
#define SYSFS_H
#include <linux/string.h>

int vfs_init_sysfs(void);

void vfs_exit_sysfs(void);

#define MAX_MINOR 255

#define IS_INVALID_DEVICE(dev) (!MAJOR(dev) && !vfs_unnamed_devices[MINOR(dev)])

/* Transport selection — shared by module param and sysfs attribute */
enum vfs_transport_type {
    VFS_TRANSPORT_MMAP = 0,
    VFS_TRANSPORT_GENL = 1,
};

/* 0 = mmap (chrdev ring), 1 = genl; set at load, mutable via sysfs */
extern int vfs_transport;

/* Map a transport name string to its enum value; -1 if unknown. */
static inline int vfs_transport_from_str(const char *s)
{
    if (sysfs_streq(s, "mmap"))
        return VFS_TRANSPORT_MMAP;
    if (sysfs_streq(s, "genl"))
        return VFS_TRANSPORT_GENL;
    return -1;
}

/* Map a transport enum value to its name string. */
static inline const char *vfs_transport_to_str(int val)
{
    return val == VFS_TRANSPORT_GENL ? "genl" : "mmap";
}

#endif /* SYSFS_H */
