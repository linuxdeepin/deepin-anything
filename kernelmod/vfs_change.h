// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "vfs_change_uapi.h"

// 节点占用的内存不能大于1M
#define VFS_CHANGE_MEMORY_LIMIT (1 << 20)

void vfs_clean_change(void);
int vfs_init_change(void);

void vfs_put_change(int act, const char *root, const char *src,
                    const char *dst);
int vfs_get_change_user(char __user *data, int size, int *count);
