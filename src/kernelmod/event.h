// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef EVENT_H
#define EVENT_H

#include <uapi/linux/limits.h>

#define VFS_EVENT_PART struct list_head list; \
    unsigned char action; \
    u32 cookie; \
    dev_t dev; \
    char *path; \
    void *pair;

struct __vfs_event_part {
	VFS_EVENT_PART
};

#define VFS_EVENT_PATH_LEN (PATH_MAX - sizeof(struct __vfs_event_part))

struct vfs_event {
	VFS_EVENT_PART
    char buf[VFS_EVENT_PATH_LEN];
};

int init_vfs_event_cache(void);
void cleanup_vfs_event_cache(void);
struct vfs_event *vfs_event_alloc(void);
struct vfs_event *vfs_event_alloc_atomic(void);
void vfs_event_free(struct vfs_event *event);

#define mpr_info(fmt, ...) \
    pr_info("vfs_monitor: " fmt, ##__VA_ARGS__)

#define mpr_err(fmt, ...) \
    pr_err("vfs_monitor: " fmt, ##__VA_ARGS__)

#endif /* EVENT_H */
