// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fsnotify_backend.h>
#include <linux/delay.h>

#include "event.h"
#include "vfs_kgenl.h"
#include "vfs_fsnotify.h"
#include "vfs_kretprobes.h"
#include "event_merge.h"

int __init init_module()
{
    int ret;
    char *notify_solution;
    char *events_source;
    void *vfs_changed_func;

    notify_solution = "genl";
#ifdef CONFIG_FSNOTIFY_BROADCAST
    events_source = "fsnotify_broadcast";
#else
    events_source = "kretprobes";
#endif

    ret = init_vfs_event_cache();
    if (ret)
        goto quit;

    vfs_changed_func = vfs_notify_dentry_event;
    ret = init_vfs_genl();
    if (ret)
        goto init_vfs_genl_fail;

#ifdef CONFIG_FSNOTIFY_BROADCAST
    ret = init_vfs_fsnotify(get_event_merge_entry(vfs_changed_func));
    if (ret)
        goto init_event_source_fail;
#else
    ret = init_vfs_kretprobes(get_event_merge_entry(vfs_changed_func));
    if (ret)
        goto init_event_source_fail;
#endif

    mpr_info("init ok, %s, %s\n", events_source, notify_solution);
	return 0;

init_event_source_fail:
    cleanup_vfs_genl();
init_vfs_genl_fail:
    cleanup_vfs_event_cache();
quit:
    mpr_info("init fail, %s, %s\n", events_source, notify_solution);
	return ret;
}

void __exit cleanup_module()
{
#ifdef CONFIG_FSNOTIFY_BROADCAST
    cleanup_vfs_fsnotify();
#else
    cleanup_vfs_kretprobes();
#endif

    /* Wait for no events to come in and send all events in the buffer */
    msleep(150);

    clearup_event_merge();
    cleanup_vfs_genl();
    cleanup_vfs_event_cache();

    mpr_info("clearup ok\n");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("wangrong@uniontech.com");
MODULE_DESCRIPTION("VFS change monitor");
