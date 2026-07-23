// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/stddef.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <linux/skbuff.h>
#include <linux/kdev_t.h>

#include "vfs_genl.h"
#include "vfs_kgenl.h"
#include "event.h"

/*
 * Detect multicast group binding restriction capability without relying on
 * LINUX_VERSION_CODE, because this security feature may be backported.
 *
 * struct genl_multicast_group layout across kernel versions:
 *   Case 1: { char name[GENL_NAMSIZ]; }
 *       No flags field — binding cannot be restricted.
 *   Case 2: { char name[GENL_NAMSIZ]; u8 flags; }
 *       2.1: GENL_MCAST_CAP_SYS_ADMIN defined — set .flags = GENL_MCAST_CAP_SYS_ADMIN.
 *       2.2: GENL_MCAST_CAP_SYS_ADMIN undefined — binding cannot be restricted.
 *   Case 3: { char name[GENL_NAMSIZ]; u8 flags; u8 cap_sys_admin:1; }
 *       Set .cap_sys_admin = 1.
 *
 * GENL_ADMIN_PERM governs genl_ops (operation) permissions, not multicast
 * group binding, and cannot restrict group joins — it is not used here.
 *
 * Detection strategy:
 *   - #ifdef GENL_MCAST_CAP_SYS_ADMIN tells us the flags value is available
 *     (Case 2.1). The macro is defined iff the kernel headers support it,
 *     regardless of kernel version.
 *   - sizeof(struct genl_multicast_group) distinguishes the three layouts:
 *       == GENL_NAMSIZ       → Case 1 (name only)
 *       >  GENL_NAMSIZ       → has flags field (Case 2 or 3)
 *       >  GENL_NAMSIZ + 1   → has cap_sys_admin bitfield (Case 3)
 *   - All checks are compile-time constants; the compiler folds and
 *     dead-code-eliminates accordingly.
 *   - The mcgs array is defined without .flags/.cap_sys_admin (always
 *     compiles). init_vfs_genl() patches the fields via pointer offsets
 *     before registration, avoiding member-name references that don't
 *     exist on all kernel versions.
 *   - Event functions check vfsmonitor_mcg_restricted (a static const bool
 *     initialized from the sizeof and #ifdef checks) so the compiler
 *     eliminates the unreachable branch at -O1+.
 */

#ifdef GENL_MCAST_CAP_SYS_ADMIN
#define VFSMONITOR_MCG_HAS_SYS_ADMIN_MACRO  1
#else
#define VFSMONITOR_MCG_HAS_SYS_ADMIN_MACRO  0
#endif

/* multicast group */
enum vfsmonitor_multicast_groups {
    VFSMONITOR_MCG_DENTRY,
    VFSMONITOR_MCG_PROCESS_INFO,
};

/* Defined without .flags/.cap_sys_admin — always compiles regardless of
 * struct layout. init_vfs_genl() patches the fields in before registration
 * if the struct supports them. */
static struct genl_multicast_group vfsmonitor_mcgs[] = {
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

/* Compile-time struct layout detection. */
#define VFSMONITOR_MCG_HAS_FLAGS  (sizeof(struct genl_multicast_group) > GENL_NAMSIZ)
#define VFSMONITOR_MCG_HAS_CAP    (sizeof(struct genl_multicast_group) > (GENL_NAMSIZ + 1))

/* True iff binding can actually be enforced:
 *   Case 2.1 (flags + GENL_MCAST_CAP_SYS_ADMIN) or Case 3 (cap_sys_admin).
 * The compiler folds this into constant propagation, so the event functions'
 * early-return branch is eliminated when false. */
static const bool vfsmonitor_mcg_restricted =
    VFSMONITOR_MCG_HAS_CAP ||
    (VFSMONITOR_MCG_HAS_FLAGS && VFSMONITOR_MCG_HAS_SYS_ADMIN_MACRO);

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

    if (!vfsmonitor_mcg_restricted)
        return 0;

    rc = vfs_notify_dentry_event(event);
    if (rc)
        return rc;

    if (event->proc_info && event->proc_info->tgid != 0)
        return vfs_notify_proc_info(event->proc_info);

    return 0;
}

int init_vfs_genl(void)
{
    int ret;
    unsigned int i;

    /* Verify offset assumptions: when the flags field exists it must be
     * at offset GENL_NAMSIZ (right after name[GENL_NAMSIZ], no padding
     * because u8 has alignment 1). */
#ifdef GENL_MCAST_CAP_SYS_ADMIN
    BUILD_BUG_ON(offsetof(struct genl_multicast_group, flags) != GENL_NAMSIZ);
#endif
    BUILD_BUG_ON(sizeof(struct genl_multicast_group) < GENL_NAMSIZ);

    /* Skip registration when binding cannot be restricted, so the family
     * is never exposed to unprivileged listeners. vfsmonitor_mcg_restricted
     * is a compile-time constant, so this branch is eliminated when false. */
    if (!vfsmonitor_mcg_restricted) {
        mpr_info("kernel lacks multicast group binding restriction support, skip genl registration\n");
        return 0;
    }

    /* Patch the restriction fields via pointer offsets (avoiding member-name
     * references that don't exist on all kernel versions) before registering
     * the family. The BUILD_BUG_ON above validates the flags offset.
     *
     *   Case 3: cap_sys_admin bitfield at offset GENL_NAMSIZ + 1.
     *   Case 2.1: flags field at offset GENL_NAMSIZ.
     * Both offsets are safe from padding: name (char) and u8 fields all have
     * alignment 1. */
    for (i = 0; i < ARRAY_SIZE(vfsmonitor_mcgs); i++) {
        if (VFSMONITOR_MCG_HAS_CAP) {
            /* Case 3: set cap_sys_admin = 1 */
            *((u8 *)((char *)&vfsmonitor_mcgs[i] + GENL_NAMSIZ + 1)) = 1;
        }
#ifdef GENL_MCAST_CAP_SYS_ADMIN
        else {
            /* Case 2.1: set flags = GENL_MCAST_CAP_SYS_ADMIN */
            *((u8 *)((char *)&vfsmonitor_mcgs[i] + GENL_NAMSIZ)) =
                GENL_MCAST_CAP_SYS_ADMIN;
        }
#endif
    }

    ret = genl_register_family(&vfsmonitor_gnl_family);
    if (ret)
        mpr_err("init_vfs_genl fail: %d\n", ret);
    return ret;
}

void cleanup_vfs_genl(void)
{
    if (!vfsmonitor_mcg_restricted)
        return;

    genl_unregister_family(&vfsmonitor_gnl_family);
}
