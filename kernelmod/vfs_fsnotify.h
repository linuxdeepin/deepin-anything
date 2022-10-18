// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VFS_FSNOTIFY_H
#define VFS_FSNOTIFY_H

#ifdef CONFIG_FSNOTIFY_BROADCAST

int init_vfs_fsnotify(void *vfs_changed_func);
void cleanup_vfs_fsnotify(void);

#endif

#endif