
// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <linux/module.h>
#include <linux/version.h>
#include <linux/slab.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
#include "vfs_module_510.c"
#else
#include "vfs_module_old.c"
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Raphael");
MODULE_DESCRIPTION("VFS change monitor");
