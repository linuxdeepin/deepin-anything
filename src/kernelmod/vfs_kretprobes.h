// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VFS_KRETPROBES_H
#define VFS_KRETPROBES_H

int init_vfs_kretprobes(void *vfs_changed_func);
void cleanup_vfs_kretprobes(void);

#endif