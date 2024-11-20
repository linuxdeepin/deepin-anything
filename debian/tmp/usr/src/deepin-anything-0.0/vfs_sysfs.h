// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SYSFS_H
#define SYSFS_H

int vfs_init_sysfs(void);

void vfs_exit_sysfs(void);

#define MAX_MINOR 255

#define IS_INVALID_DEVICE(dev) (!MAJOR(dev) && !vfs_unnamed_devices[MINOR(dev)])

#endif /* SYSFS_H */
