// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VFS_KGENL_H
#define VFS_KGENL_H

#include "event.h"

int init_vfs_genl(void);
void cleanup_vfs_genl(void);
int vfs_notify_dentry_event(struct vfs_event *event);

#endif