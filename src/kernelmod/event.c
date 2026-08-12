// SPDX-FileCopyrightText: 2021 - 2026 UnionTech Software Technology Co., Ltd.
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
    // event->proc_info 目前未使用, 始终为 NULL, 不需要释放

    kmem_cache_free(vfs_event_cachep, event);
}
