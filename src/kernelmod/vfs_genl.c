// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <linux/kernel.h>
#include <linux/module.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <linux/skbuff.h>
#include <linux/kdev_t.h>

#include "vfs_genl.h"
#include "vfs_kgenl.h"
#include "event.h"

/* multicast group */
enum vfsmonitor_multicast_groups {
    VFSMONITOR_MCG_DENTRY,
    VFSMONITOR_MCG_PROCESS_INFO,
};
static const struct genl_multicast_group vfsmonitor_mcgs[] = {
    [VFSMONITOR_MCG_DENTRY] = { .name = VFSMONITOR_MCG_DENTRY_NAME, },
    [VFSMONITOR_MCG_PROCESS_INFO] = { .name = VFSMONITOR_MCG_PROCESS_INFO_NAME, },
};

/* family definition */
static struct genl_family vfsmonitor_gnl_family = {
    .name = VFSMONITOR_FAMILY_NAME,
    .version = 1,
    .module = THIS_MODULE,
    .maxattr = VFSMONITOR_A_MAX,
    .netnsok = false,
    .mcgrps = vfsmonitor_mcgs,
    .n_mcgrps = ARRAY_SIZE(vfsmonitor_mcgs),
};

// static const char* action_names[] = {"file-created", "link-created", "symlink-created", "dir-created", "file-deleted", "dir-deleted",
//     "file-renamed", "dir-renamed", "file-renamed-from", "file-renamed-to", "dir-renamed-from", "dir-renamed-to"};

int vfs_notify_dentry_event(struct vfs_event *event)
{
    int rc;
    struct sk_buff *msg;
    void *msg_head;

    /* alloc msg */
    msg = genlmsg_new(NLMSG_GOODSIZE, GFP_ATOMIC);
    if (!msg)
        return -ENOMEM;

    /* construct msg */
    /* create the message headers */
    msg_head = genlmsg_put(msg, 0, 0, &vfsmonitor_gnl_family, GFP_ATOMIC, VFSMONITOR_C_NOTIFY);
    if (!msg_head) {
        rc = -ENOMEM;
        goto failure;
    }
    /* add attributes */
    rc = nla_put_u8(msg, VFSMONITOR_A_ACT, event->action);
    if (rc != 0)
        goto failure;
    rc = nla_put_u32(msg, VFSMONITOR_A_COOKIE, event->cookie);
    if (rc != 0)
        goto failure;
    rc = nla_put_u16(msg, VFSMONITOR_A_MAJOR, MAJOR(event->dev));
    if (rc != 0)
        goto failure;
    rc = nla_put_u8(msg, VFSMONITOR_A_MINOR, MINOR(event->dev));
    if (rc != 0)
        goto failure;
    rc = nla_put_string(msg, VFSMONITOR_A_PATH, event->path);
    if (rc != 0)
        goto failure;
    /* finalize the message */
    genlmsg_end(msg, msg_head);

    /* send msg */
    genlmsg_multicast(&vfsmonitor_gnl_family, msg, 0, VFSMONITOR_MCG_DENTRY, GFP_ATOMIC);

    return 0;

failure:
    kfree_skb(msg);
    return rc;
}

int vfs_notify_proc_info(struct proc_info *info)
{
    int rc;
    struct sk_buff *msg;
    void *msg_head;

    /* alloc msg */
    msg = genlmsg_new(NLMSG_GOODSIZE, GFP_ATOMIC);
    if (!msg)
        return -ENOMEM;

    /* construct msg */
    /* create the message headers */
    msg_head = genlmsg_put(msg, 0, 0, &vfsmonitor_gnl_family, GFP_ATOMIC, VFSMONITOR_C_NOTIFY_PROCESS_INFO);
    if (!msg_head) {
        rc = -ENOMEM;
        goto failure;
    }
    /* add attributes */
    rc = nla_put_u32(msg, VFSMONITOR_A_UID, info->uid);
    if (rc != 0)
        goto failure;
    rc = nla_put_s32(msg, VFSMONITOR_A_TGID, info->tgid);
    if (rc != 0)
        goto failure;
    rc = nla_put_string(msg, VFSMONITOR_A_PATH, info->path);
    if (rc != 0)
        goto failure;

    /* finalize the message */
    genlmsg_end(msg, msg_head);

    /* send msg */
    genlmsg_multicast(&vfsmonitor_gnl_family, msg, 0, VFSMONITOR_MCG_PROCESS_INFO, GFP_ATOMIC);

    return 0;

failure:
    kfree_skb(msg);
    return rc;
}

int vfs_notify_vfs_event(struct vfs_event *event)
{
    int rc;

    rc = vfs_notify_dentry_event(event);
    if (rc)
        return rc;

    if (event->proc_info && event->proc_info->tgid != 0)
        return vfs_notify_proc_info(event->proc_info);

    return 0;
}

int init_vfs_genl(void)
{
    int ret = genl_register_family(&vfsmonitor_gnl_family);
    if (ret)
        mpr_err("init_vfs_genl fail: %d\n", ret);
    return ret;
}

void cleanup_vfs_genl(void)
{
    genl_unregister_family(&vfsmonitor_gnl_family);
}
