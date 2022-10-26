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
    return kmem_cache_alloc(vfs_event_cachep, GFP_KERNEL);
}

struct vfs_event *vfs_event_alloc_atomic(void)
{
    return kmem_cache_alloc(vfs_event_cachep, GFP_ATOMIC);
}

void vfs_event_free(struct vfs_event *event)
{
    kmem_cache_free(vfs_event_cachep, event);
}
