// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <linux/slab.h>
#include "event.h"


struct kmem_cache *vfs_event_cachep __read_mostly;

int init_vfs_event_cache(void)
{
    vfs_event_cachep = KMEM_CACHE(vfs_event, 0);
    if (unlikely(!vfs_event_cachep))
        return -ENOMEM;

    return 0;
}

void cleanup_vfs_event_cache(void)
{
    kmem_cache_destroy(vfs_event_cachep);
}

struct vfs_event *vfs_event_alloc(void)
{
    struct vfs_event *event;

    event = kmem_cache_alloc(vfs_event_cachep, GFP_KERNEL);
    if (event != NULL)
        event->proc_info = NULL;

    return event;
}

struct vfs_event *vfs_event_alloc_atomic(void)
{
    struct vfs_event *event;

    event = kmem_cache_alloc(vfs_event_cachep, GFP_ATOMIC);
    if (event != NULL)
        event->proc_info = NULL;

    return event;
}

void vfs_event_free(struct vfs_event *event)
{
    if (event->proc_info != NULL)
        kmem_cache_free(vfs_event_cachep, event->proc_info);

    kmem_cache_free(vfs_event_cachep, event);
}

#define CONCAT_(A,B) A##B
#define CONCAT(A,B) CONCAT_(A,B)
#define STATIC_ASSERT(p, msg) typedef char CONCAT(dummy__,__LINE__) [(p) ? 1 : -1]

STATIC_ASSERT(sizeof(struct proc_info) == sizeof(struct vfs_event), "struct size not match");

int vfs_event_alloc_proc_info_atomic(struct vfs_event *event)
{
    if (event == NULL || event->proc_info != NULL)
        return -EINVAL;

    event->proc_info = kmem_cache_alloc(vfs_event_cachep, GFP_ATOMIC);

    return event->proc_info ? 0 : -ENOMEM;
}
