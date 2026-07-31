// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 - 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fsnotify_backend.h>
#include <linux/delay.h>

#include "vfs_sysfs.h"
#include "event.h"
#include "vfs_kgenl.h"
#include "vfs_fsnotify.h"
#include "vfs_kretprobes.h"
#include "event_merge.h"
#include "vfs_trace_process.h"
#include "vfs_chrdev.h"

static char *transport = "mmap";
module_param(transport, charp, 0644);
MODULE_PARM_DESC(transport, "Event transport: \"mmap\" (default) or \"genl\"");

/* 0 = mmap (chrdev ring), 1 = genl — set at load, mutable via sysfs */
int vfs_transport;

/*
 * Monotonic per-event sequence number for cross-channel correlation.
 * Assigned at the dispatch entry so both transports (mmap rings and genl
 * messages) carry the same seq, letting userspace pair a dentry record
 * with its proc record regardless of transport.
 */
static atomic_t event_seq = ATOMIC_INIT(0);

/*
 * Dispatch entry — the pipeline tail. Selects between the two exclusive
 * transports on every event so the choice can be flipped at runtime via
 * /sys/kernel/vfs_monitor/transport.
 */
static int vfs_dispatch_notify_event(struct vfs_event *event)
{
    event->seq = atomic_inc_return(&event_seq);

    if (vfs_transport == VFS_TRANSPORT_MMAP)
        return vfs_chrdev_notify_event(event);
    return vfs_notify_vfs_event(event);
}

int __init vfs_monitor_init_module(void)
{
    int ret;
    char *events_source;
    void *vfs_changed_func;

    vfs_transport = vfs_transport_from_str(transport);
    if (vfs_transport < 0)
        vfs_transport = VFS_TRANSPORT_MMAP;
#ifdef CONFIG_FSNOTIFY_BROADCAST
    events_source = "fsnotify_broadcast";
#else
    events_source = "kretprobes";
#endif

    ret = vfs_init_sysfs();
    if (ret)
        goto vfs_init_sysfs_quit;

    ret = init_vfs_event_cache();
    if (ret)
        goto init_vfs_event_cache_quit;

    ret = init_vfs_genl();
    if (ret)
        goto init_vfs_genl_fail;

    ret = init_vfs_chrdev();
    if (ret)
        goto init_vfs_chrdev_fail;

    vfs_changed_func = vfs_get_trace_process_entry(
        get_event_merge_entry(vfs_dispatch_notify_event));
#ifdef CONFIG_FSNOTIFY_BROADCAST
    ret = init_vfs_fsnotify(vfs_changed_func);
    if (ret)
        goto init_event_source_fail;
#else
    ret = init_vfs_kretprobes(vfs_changed_func);
    if (ret)
        goto init_event_source_fail;
#endif

    mpr_info("init ok, %s, %s, trace_process\n", events_source,
             vfs_transport_to_str(vfs_transport));
    return 0;

init_event_source_fail:
    cleanup_vfs_chrdev();
init_vfs_chrdev_fail:
    cleanup_vfs_genl();
init_vfs_genl_fail:
    cleanup_vfs_event_cache();
init_vfs_event_cache_quit:
    vfs_exit_sysfs();
vfs_init_sysfs_quit:
    mpr_info("init fail\n");
    return ret;
}

void __exit vfs_monitor_cleanup_module(void)
{
#ifdef CONFIG_FSNOTIFY_BROADCAST
    cleanup_vfs_fsnotify();
#else
    cleanup_vfs_kretprobes();
#endif

    /* Wait for no events to come in and send all events in the buffer */
    msleep(150);

    clearup_event_merge();
    cleanup_vfs_chrdev();
    cleanup_vfs_genl();
    cleanup_vfs_event_cache();
    vfs_exit_sysfs();

    mpr_info("clearup ok\n");
}


module_init(vfs_monitor_init_module);
module_exit(vfs_monitor_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("wangrong@uniontech.com");
MODULE_DESCRIPTION("VFS change monitor");
