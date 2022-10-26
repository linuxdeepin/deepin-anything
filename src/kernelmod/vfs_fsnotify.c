// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <linux/fsnotify_backend.h>
#ifdef CONFIG_FSNOTIFY_BROADCAST
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dcache.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/namei.h>
#include <linux/fs.h>
#include <linux/delay.h>

#include "vfs_change_consts.h"
#include "event.h"


static int (*vfs_changed_entry)(struct vfs_event *event);

static struct mnt_namespace *target_mnt_ns;

static inline int init_mnt_ns(void)
{
    /*
     * workaround the addr is 0xFFF...FF(value: -1) for this case by unkown reason: upgrade kernel
     * image to 5.10 and update this dkms module at the same time.
     * this nsproxy's addr fill 'F' while load this module at first time and then tainted. Add value
     * checking to workaround this issue.
     */
    if ((struct nsproxy*)-1 == current->nsproxy) {
        mpr_err("current->nsproxy value is -1, return\n");
        return -EINVAL;
    }

    if (0 == current->nsproxy || 0 == current->nsproxy->mnt_ns) {
        mpr_err("init_mnt_ns fail\n");
        return -EINVAL;
    }
    
    target_mnt_ns = current->nsproxy->mnt_ns;
    return 0;
}

static inline int is_mnt_ns_valid(void)
{
    return target_mnt_ns && current->nsproxy && current->nsproxy->mnt_ns == target_mnt_ns;
}

static void on_mount(const unsigned char *dir_name)
{
    struct path path;
    struct vfs_event *event = 0;
    dev_t dev;

    if (!is_mnt_ns_valid())
        return;

    if (unlikely(kern_path(dir_name, LOOKUP_FOLLOW, &path))) {
        mpr_info("on_mount, kern_path fail for %s\n", dir_name);
        return;
    }
    dev = path.dentry->d_sb->s_dev;
    path_put(&path);

    if (!MAJOR(dev))
        return;

    if (unlikely(strlen(dir_name) >= sizeof(event->buf))){
        mpr_info("on_mount, mountpoint is too long, %s\n", dir_name);
        return;
    }

    event = vfs_event_alloc();
    if (unlikely(!event)) {
        mpr_info("on_mount, vfs_event_alloc fail\n");
        return;
    }

    strcpy(event->buf, dir_name);
    event->path = event->buf;
    event->action = ACT_MOUNT;
    event->cookie = 0;
    event->dev = dev;

    vfs_changed_entry(event);
}

static void on_unmount(const unsigned char *dir_name)
{
    struct vfs_event *event;

    if (!is_mnt_ns_valid())
        return;

    if (unlikely(strlen(dir_name) >= sizeof(event->buf))){
        mpr_info("on_unmount, mountpoint is too long, %s\n", dir_name);
        return;
    }

    event = vfs_event_alloc();
    if (unlikely(!event)) {
        mpr_info("on_unmount, vfs_event_alloc fail\n");
        return;
    }

    strcpy(event->buf, dir_name);
    event->path = event->buf;
    event->action = ACT_UNMOUNT;
    event->cookie = 0;
    event->dev = 0;

    vfs_changed_entry(event);
}

static void on_dentry_op(int action, struct dentry *p_dentry, const unsigned char *file_name, u32 cookie)
{
    struct vfs_event *event;
    int file_name_len, dentry_path_size;
    char *write_pos;

    event = vfs_event_alloc();
    if (unlikely(!event)) {
        mpr_info("vfs_event_alloc fail\n");
        return;
    }

    /* 
     * write '\0' at `/` pos first, then update to '/'
     * 
     * p_dentry_str + '\0'
     * p_dentry_str + '/' + file_name + '\0'
     */

    file_name_len = strlen(file_name);
    dentry_path_size = sizeof(event->buf) - file_name_len - 1;

    event->path = dentry_path_raw(p_dentry, event->buf, dentry_path_size);
    if (IS_ERR(event->path)) {
        mpr_info("dentry_path_raw fail\n");
        goto fail;
    }

    write_pos = event->buf + dentry_path_size - 1;
    /* handle / case */
    if (0 != *(event->path+1))
        *write_pos++ = '/';
    memcpy(write_pos, file_name, file_name_len+1);


    event->action = action;
    event->cookie = cookie;
    event->dev = p_dentry->d_sb->s_dev;

    vfs_changed_entry(event);
    return;

fail:
    if (event)
        vfs_event_free(event);
}


static void on_file_op(int action, struct inode *p_inode, const unsigned char *file_name, u32 cookie)
{
    struct dentry *dentry;

    spin_lock(&p_inode->i_lock);
    dentry = hlist_entry_safe(p_inode->i_dentry.first, typeof(*dentry), d_u.d_alias);
    if (dentry)
        dget(dentry);
    spin_unlock(&p_inode->i_lock);

    if (dentry) {
        on_dentry_op(action, dentry, file_name, cookie);
        dput(dentry);
    }
}

#define TARGET_EVENT (FS_DELETE | FS_UNMOUNT_DIR | FS_MOUNT_DIR | FS_CREATE | FS_MOVED_FROM | FS_MOVED_TO)

static inline void fsnotify_event_handler(struct inode *to_tell, __u32 mask, const unsigned char *file_name, u32 cookie)
{
    switch (mask & TARGET_EVENT)
    {
    case FS_CREATE:
        on_file_op((mask & FS_ISDIR) ? ACT_NEW_FOLDER : ACT_NEW_FILE, to_tell, file_name, cookie);
        break;
    case FS_DELETE:
        on_file_op((mask & FS_ISDIR) ? ACT_DEL_FOLDER : ACT_DEL_FILE, to_tell, file_name, cookie);
        break;
    case FS_MOVED_FROM:
        on_file_op((mask & FS_ISDIR) ? ACT_RENAME_FROM_FOLDER : ACT_RENAME_FROM_FILE, to_tell, file_name, cookie);
        break;
    case FS_MOVED_TO:
        on_file_op((mask & FS_ISDIR) ? ACT_RENAME_TO_FOLDER : ACT_RENAME_TO_FILE, to_tell, file_name, cookie);
        break;
    case FS_MOUNT_DIR:
        on_mount(file_name);
        break;
    case FS_UNMOUNT_DIR:
        on_unmount(file_name);
        break;
    default:
        break;
    }
}

static void fsnotify_broadcast_listener(struct inode *to_tell, __u32 mask, const void *data, int data_is,
    const unsigned char *file_name, u32 cookie)
{
    if (!to_tell || FSNOTIFY_EVENT_INODE != data_is || !file_name 
        || !MAJOR(to_tell->i_sb->s_dev) || !(mask & TARGET_EVENT) || !is_mnt_ns_valid())
        return;

    fsnotify_event_handler(to_tell, mask, file_name, cookie);
}

static void fsnotify_parent_broadcast_listener(const struct path *path,
        struct dentry *dentry, __u32 mask, struct inode *p_inode)
{
    struct dentry *parent = NULL;
    struct name_snapshot name;

    if (!MAJOR(dentry->d_sb->s_dev) || !(mask & TARGET_EVENT) || !is_mnt_ns_valid())
        return;

    if (!p_inode) {
        parent = dget_parent(dentry);
        p_inode = parent->d_inode;
    }

    take_dentry_name_snapshot(&name, dentry);
    fsnotify_event_handler(p_inode, mask, name.name, 0);
    release_dentry_name_snapshot(&name);

    if (parent)
        dput(parent);
}

int init_vfs_fsnotify(void *vfs_changed_func)
{
    int ret;
    
    ret = init_mnt_ns();
    if (ret)
        return ret;

    vfs_changed_entry = vfs_changed_func;
    ret = fsnotify_reg_listener(fsnotify_broadcast_listener, fsnotify_parent_broadcast_listener);
    if (ret)
        mpr_info("fsnotify_reg_listener fail\n");
    
    return ret;
}

void cleanup_vfs_fsnotify(void)
{
    int ret;

    ret = fsnotify_unreg_listener(fsnotify_broadcast_listener, fsnotify_parent_broadcast_listener);
    if (ret)
        mpr_info("fsnotify_unreg_listener fail\n");

    /* wait notify threads quit current module */
    /* sleep later */
}

#endif // CONFIG_FSNOTIFY_BROADCAST
