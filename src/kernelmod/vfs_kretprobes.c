// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 - 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/dcache.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/namei.h>
#include <linux/version.h>
#include <linux/atomic.h>

#include "arg_extractor.h"
#include "vfs_change_consts.h"
#include "event.h"
#include "vfs_sysfs.h"


extern char vfs_unnamed_devices[MAX_MINOR+1];
static int (*vfs_changed_entry)(struct vfs_event *event);
static atomic_t event_sync_cookie = ATOMIC_INIT(0);

#define _DECL_CMN_KRP(fn, symbol) static struct kretprobe fn##_krp = {\
        .entry_handler  = on_##fn##_ent,\
        .handler        = on_##fn##_ret,\
        .data_size      = sizeof(struct fn##_args),\
        .maxactive      = 64,\
        .kp.symbol_name = ""#symbol"",\
    };

#define DECL_CMN_KRP(fn) _DECL_CMN_KRP(fn, fn)

static struct mnt_namespace *target_mnt_ns;

static inline int init_mnt_ns_data(void)
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
        mpr_err("init_mnt_ns_data fail\n");
        return -EINVAL;
    }

    target_mnt_ns = current->nsproxy->mnt_ns;
    return 0;
}

static inline int is_mnt_ns_valid(void)
{
    return target_mnt_ns && current->nsproxy && current->nsproxy->mnt_ns == target_mnt_ns;
}

struct do_mount_args {
    char dir_name[NAME_MAX];
};

static int on_do_mount_ent(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    const char *dir_name;
    /*
     * >= 5.9
     * int path_mount(const char *dev_name, struct path *path,
     *  const char *type_page, unsigned long flags, void *data_page)
     *
     * < 5.9
     * long do_mount(const char *dev_name, const char __user *dir_name,
     *  const char *type_page, unsigned long flags, void *data_page)
     */
    struct do_mount_args *args = (struct do_mount_args *)ri->data;
    if (!is_mnt_ns_valid())
        return 1;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0)
    dir_name = (const char *)get_arg(regs, 2);
    if (dir_name == NULL) {
        mpr_info("on_do_mount_ent dir_name is null\n");
        return 1;
    }
#ifdef CONFIG_ARCH_HISI
    // kernel will halt if set_fs func on kunpen SOC
    if (unlikely(strncpy_from_user(args->dir_name, dir_name, sizeof(args->dir_name)) < 0))
        return 1;
#else
    mm_segment_t org_fs = get_fs();
    set_fs(KERNEL_DS);

    if (unlikely(strncpy_from_user(args->dir_name, dir_name, sizeof(args->dir_name)) < 0)) {
        mpr_info("on_do_mount_ent strncpy dir_name failed: %s\n", dir_name);
        set_fs(org_fs);
        return 1;
    }
    set_fs(org_fs);
#endif
#else
    dir_name = d_path((struct path *)get_arg(regs, 2), args->dir_name, sizeof(args->dir_name));
    if (IS_ERR(dir_name)) {
        mpr_info("on_do_mount_ent get mount dir fail\n");
        return 1;
    }
    strcpy(args->dir_name, dir_name);
#endif

    return 0;
}

struct do_mount_work_stuct {
    struct work_struct work;
    struct do_mount_args args;
};

void do_mount_work_handle(struct work_struct *work)
{
#ifdef QUERY_DEV_ON_MOUNT
    struct path path;
#endif
    dev_t dev = 0;
    struct do_mount_work_stuct *do_mount_work = (struct do_mount_work_stuct *)work;
    struct vfs_event *event;

#ifdef QUERY_DEV_ON_MOUNT
    if (unlikely(kern_path(do_mount_work->args.dir_name, LOOKUP_FOLLOW, &path))) {
        mpr_info("do_mount_work_handle, kern_path fail for %s\n", do_mount_work->args.dir_name);
        goto quit;
    }
    dev = path.dentry->d_sb->s_dev;
    path_put(&path);
#endif

    if (unlikely(strlen(do_mount_work->args.dir_name) >= sizeof(event->buf))){
        mpr_info("do_mount_work_handle, mountpoint is too long, %s\n", do_mount_work->args.dir_name);
        goto quit;
    }

    event = vfs_event_alloc_atomic();
    if (unlikely(!event)) {
        mpr_info("do_mount_work_handle, vfs_event_alloc_atomic fail\n");
        goto quit;
    }

    strcpy(event->buf, do_mount_work->args.dir_name);
    event->path = event->buf;
    event->action = ACT_MOUNT;
    event->cookie = 0;
    event->dev = dev;

    vfs_changed_entry(event);

quit:
    kfree(do_mount_work);
}

static int on_do_mount_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    struct do_mount_work_stuct *do_mount_work;
    struct do_mount_args *args = (struct do_mount_args *)ri->data;
    if (regs_return_value(regs))
        return 0;

    do_mount_work = kmalloc(sizeof(struct do_mount_work_stuct), GFP_ATOMIC);
    if (unlikely(0 == do_mount_work)) {
        mpr_info("do_mount_work kmalloc failed\n");
        return 0;
    }
    strcpy(do_mount_work->args.dir_name, args->dir_name);
    INIT_WORK(&(do_mount_work->work), do_mount_work_handle);
    schedule_work(&do_mount_work->work);

    return 0;
}

struct sys_umount_args {
    char dir_name[NAME_MAX];
};

static int on_sys_umount_ent(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    const char *dir_name;
    struct sys_umount_args *args = (struct sys_umount_args *)ri->data;
    if (!is_mnt_ns_valid())
        return 1;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0)
    //char __user* dir_name, int flags
    dir_name = (const char *)get_arg(regs, 1);
    if (dir_name == NULL) {
        mpr_info("on_sys_umount_ent dir_name is null\n");
        return 1;
    }

#ifdef CONFIG_ARCH_HISI
    // kernel will halt if set_fs func on kunpen SOC
    if (unlikely(strncpy_from_user(args->dir_name, dir_name, sizeof(args->dir_name)) < 0))
        return 1;
#else
    mm_segment_t org_fs = get_fs();
    set_fs(KERNEL_DS);
    if (unlikely(strncpy_from_user(args->dir_name, dir_name, sizeof(args->dir_name)) < 0)) {
        mpr_info("on_sys_umount_ent strncpy dir_name failed: %s\n", dir_name);
        set_fs(org_fs);
        return 1;
    }
    set_fs(org_fs);
#endif
#else
    /* int path_umount(struct path *path, int flags) */
    dir_name = d_path((struct path *)get_arg(regs, 1), args->dir_name, sizeof(args->dir_name));
    if (IS_ERR(dir_name)) {
        mpr_info("on_sys_umount_ent get umount dir fail\n");
        return 1;
    }
    strcpy(args->dir_name, dir_name);
#endif

    return 0;
}

struct sys_umount_work_stuct {
    struct work_struct work;
    struct sys_umount_args args;
};

void sys_umount_work_handle(struct work_struct *work)
{
    struct sys_umount_work_stuct *sys_umount_work = (struct sys_umount_work_stuct *)work;
    struct vfs_event *event;

    if (unlikely(strlen(sys_umount_work->args.dir_name) >= sizeof(event->buf))){
        mpr_info("on_mount, mountpoint is too long, %s\n", sys_umount_work->args.dir_name);
        goto quit;
    }

    event = vfs_event_alloc_atomic();
    if (unlikely(!event)) {
        mpr_info("on_mount, vfs_event_alloc_atomic fail\n");
        goto quit;
    }

    strcpy(event->buf, sys_umount_work->args.dir_name);
    event->path = event->buf;
    event->action = ACT_UNMOUNT;
    event->cookie = 0;
    event->dev = 0;

    vfs_changed_entry(event);

quit:
    kfree(sys_umount_work);
}

static int on_sys_umount_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    struct sys_umount_work_stuct *sys_umount_work;
    struct sys_umount_args *args = (struct sys_umount_args *)ri->data;
    if (regs_return_value(regs))
        return 0;

    sys_umount_work = kmalloc(sizeof(struct sys_umount_work_stuct), GFP_ATOMIC);
    if (unlikely(0 == sys_umount_work)) {
        mpr_info("on_sys_umount_ret kmalloc failed\n");
        return 0;
    }
    strcpy(sys_umount_work->args.dir_name, args->dir_name);
    INIT_WORK(&(sys_umount_work->work), sys_umount_work_handle);
    schedule_work(&sys_umount_work->work);

    return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0)
DECL_CMN_KRP(do_mount);
#else
_DECL_CMN_KRP(do_mount, path_mount);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 17, 0)
DECL_CMN_KRP(sys_umount);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0)
_DECL_CMN_KRP(sys_umount, ksys_umount);
#else
_DECL_CMN_KRP(sys_umount, path_umount);
#endif



#define DECL_VFS_KRP(fn, act, de_i) static int on_##fn##_ent(struct kretprobe_instance *ri, struct pt_regs *regs)\
    {\
        return common_vfs_ent((struct vfs_event **)&(ri->data), (struct dentry *)get_arg(regs, de_i));\
    }\
    \
    static int on_##fn##_ret(struct kretprobe_instance *ri, struct pt_regs *regs)\
    {\
        return common_vfs_ret((struct vfs_event **)&(ri->data), regs, act);\
    }\
    \
    static struct kretprobe fn##_krp = {\
        .entry_handler  = on_##fn##_ent,\
        .handler        = on_##fn##_ret,\
        .data_size      = sizeof(struct vfs_event *),\
        .maxactive      = 64,\
        .kp.symbol_name = ""#fn"",\
    };

/*
 * if kretprobe entry-handler returns a non-zero error,
 * then the handler will not be called.
 *
 * https://www.kernel.org/doc/Documentation/kprobes.txt
 */
static int common_vfs_ent(struct vfs_event **event, struct dentry *de)
{
    if (de == 0 || de->d_sb == 0)
        return 1;
    if (IS_INVALID_DEVICE(de->d_sb->s_dev) || !is_mnt_ns_valid())
        return 1;

    *event = vfs_event_alloc_atomic();
    if (unlikely(!*event)) {
        mpr_info("vfs_event_alloc_atomic fail\n");
        return 1;
    }

    (*event)->dev = de->d_sb->s_dev;
    (*event)->path = dentry_path_raw(de, (*event)->buf, sizeof((*event)->buf));
    if (IS_ERR((*event)->path))
        goto fail;
    (*event)->cookie = 0;

    return 0;

fail:
    if (*event)
        vfs_event_free(*event);
    return 1;
}

static int common_vfs_ret(struct vfs_event **event, struct pt_regs *regs, int action)
{
    if (regs_return_value(regs))
        goto fail;

    (*event)->action = action;
    vfs_changed_entry(*event);
    return 0;

fail:
    if (*event)
        vfs_event_free(*event);
    return 0;
}

// select dentry from vfs api by different kernel
// If the vfs api in the subsequent kernel changes, please define a new macro branch.
// The main work of the definition is to specify the position number (starting from 1) of
// the dentry parameter according to the definition of vfs api.
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 12, 0)
// vfs-create: struct inode*, struct dentry*, umode_t, bool
// vfs-unlink: struct inode*, struct dentry*, struct inode**
// vfs-mkdir: struct inode*, struct dentry*, umode_t
// vfs-rmdir: struct inode*, struct dentry*
// vfs-symlink: struct inode*, struct dentry*, const char*
// security-inode-create: struct inode*, struct dentry*, umode_t
// vfs-link: struct dentry*, struct inode*, struct dentry*, struct inode**
DECL_VFS_KRP(vfs_create, ACT_NEW_FILE, 2);
DECL_VFS_KRP(vfs_unlink, ACT_DEL_FILE, 2);
DECL_VFS_KRP(vfs_mkdir, ACT_NEW_FOLDER, 2);
DECL_VFS_KRP(vfs_rmdir, ACT_DEL_FOLDER, 2);
DECL_VFS_KRP(vfs_symlink, ACT_NEW_SYMLINK, 2);
DECL_VFS_KRP(security_inode_create, ACT_NEW_FILE, 2);
// select dest(3) dentry, not src(1) dentry
DECL_VFS_KRP(vfs_link, ACT_NEW_LINK, 3);
#else
// int vfs_create(struct user_namespace *, struct inode *, struct dentry *, umode_t, bool);
// int vfs_unlink(struct user_namespace *, struct inode *, struct dentry *, struct inode **);
// int vfs_mkdir(struct user_namespace *, struct inode *, struct dentry *, umode_t);
// int vfs_rmdir(struct user_namespace *, struct inode *, struct dentry *);
// int vfs_symlink(struct user_namespace *, struct inode *, struct dentry *, const char *);
// int security_inode_create(struct inode *dir, struct dentry *dentry, umode_t mode);
// int vfs_link(struct dentry *, struct user_namespace *, struct inode *, struct dentry *, struct inode **);
DECL_VFS_KRP(vfs_create, ACT_NEW_FILE, 3);
DECL_VFS_KRP(vfs_unlink, ACT_DEL_FILE, 3);
DECL_VFS_KRP(vfs_mkdir, ACT_NEW_FOLDER, 3);
DECL_VFS_KRP(vfs_rmdir, ACT_DEL_FOLDER, 3);
DECL_VFS_KRP(vfs_symlink, ACT_NEW_SYMLINK, 3);
DECL_VFS_KRP(security_inode_create, ACT_NEW_FILE, 2);
// select dest(4) dentry, not src(1) dentry
DECL_VFS_KRP(vfs_link, ACT_NEW_LINK, 4);
#endif

struct vfs_rename_args {
    struct vfs_event *fe;
    struct vfs_event *te;
};

static int on_vfs_rename_ent(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    unsigned char is_dir;
    struct vfs_event **fe, **te;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 12, 0)
    // vfs-rename: struct inode*, struct dentry*, struct inode*, struct dentry*, struct inode**, unsigned int
    struct dentry *de_old = (struct dentry *)get_arg(regs, 2);
    struct dentry *de_new = (struct dentry *)get_arg(regs, 4);
#else
    // int vfs_rename(struct renamedata *);
    // struct renamedata {
    //     struct user_namespace *old_mnt_userns;
    //     struct inode *old_dir;
    //     struct dentry *old_dentry;
    //     struct user_namespace *new_mnt_userns;
    //     struct inode *new_dir;
    //     struct dentry *new_dentry;
    //     struct inode **delegated_inode;
    //     unsigned int flags;
    // } __randomize_layout;
    struct renamedata *renamedata = (struct renamedata *)get_arg(regs, 1);
    struct dentry *de_old = renamedata->old_dentry;
    struct dentry *de_new = renamedata->new_dentry;
#endif

    if (de_old == 0 || de_old->d_sb == 0 || de_new == 0)
        return 1;
    if (IS_INVALID_DEVICE(de_old->d_sb->s_dev) || !is_mnt_ns_valid())
        return 1;

    fe = &((struct vfs_rename_args *)ri->data)->fe;
    te = &((struct vfs_rename_args *)ri->data)->te;
    *fe = 0;
    *te = 0;

    *fe = vfs_event_alloc_atomic();
    if (unlikely(!*fe)) {
        mpr_info("vfs_event_alloc_atomic fail\n");
        return 1;
    }
    *te = vfs_event_alloc_atomic();
    if (unlikely(!*te)) {
        mpr_info("vfs_event_alloc_atomic fail\n");
        goto fail;
    }

    (*fe)->path = dentry_path_raw(de_old, (*fe)->buf, sizeof((*fe)->buf));
    if (IS_ERR((*fe)->path))
        goto fail;
    (*fe)->dev = de_old->d_sb->s_dev;
    is_dir = d_is_dir(de_old);
    (*fe)->action = is_dir ? ACT_RENAME_FROM_FOLDER : ACT_RENAME_FROM_FILE;

    (*te)->path = dentry_path_raw(de_new, (*te)->buf, sizeof((*te)->buf));
    if (IS_ERR((*te)->path))
        goto fail;
    (*te)->dev = (*fe)->dev;
    (*te)->action = is_dir ? ACT_RENAME_TO_FOLDER : ACT_RENAME_TO_FILE;

    return 0;

fail:
    if (*fe)
        vfs_event_free(*fe);
    if (*te)
        vfs_event_free(*te);
    return 1;
}

static int on_vfs_rename_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    struct vfs_event **fe = &((struct vfs_rename_args *)ri->data)->fe;
    struct vfs_event **te = &((struct vfs_rename_args *)ri->data)->te;

    if (regs_return_value(regs))
        goto fail;

    (*fe)->cookie = atomic_inc_return(&event_sync_cookie);
    (*te)->cookie = (*fe)->cookie;

    vfs_changed_entry(*fe);
    vfs_changed_entry(*te);

    return 0;

fail:
    if (*fe)
        vfs_event_free(*fe);
    if (*te)
        vfs_event_free(*te);
    return 0;
}

DECL_CMN_KRP(vfs_rename);

static struct kretprobe *vfs_krps[] = {&do_mount_krp, &sys_umount_krp, &vfs_create_krp,
    &vfs_unlink_krp, &vfs_mkdir_krp, &vfs_rmdir_krp, &vfs_symlink_krp, &vfs_link_krp,
    &vfs_rename_krp, &security_inode_create_krp
};

int init_vfs_kretprobes(void *vfs_changed_func)
{
    int ret;

    ret = init_mnt_ns_data();
    if (ret)
        return ret;

    vfs_changed_entry = vfs_changed_func;

    ret = register_kretprobes(vfs_krps, sizeof(vfs_krps) / sizeof(void *));
    if (ret < 0) {
        mpr_info("register_kretprobes failed, returned %d\n", ret);
        return ret;
    }
    mpr_info("register_kretprobes %ld ok\n", sizeof(vfs_krps) / sizeof(void *));

    return 0;
}

void cleanup_vfs_kretprobes(void)
{
    unregister_kretprobes(vfs_krps, sizeof(vfs_krps) / sizeof(void *));
}
