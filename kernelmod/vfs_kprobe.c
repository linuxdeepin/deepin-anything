// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <linux/dcache.h>
#include <linux/file.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#include "arg_extractor.h"
#include "vfs_change.h"
#include "vfs_log.h"
#include "vfs_partition.h"

#if 0
#define trace_vfs_kprobe mpr_info
#else
#define trace_vfs_kprobe(...)
#endif

struct _vfs_op_args {
  dev_t dev;
  int dir;
  char *path;
  char *new_path;
  char *ext_buf;
#ifdef KRETPROBE_MAX_DATA_SIZE
  char buf[KRETPROBE_MAX_DATA_SIZE - 6 * sizeof(void *)];
#else
  char buf[PATH_MAX];
#endif
};
#define _vfs_rename_args _vfs_op_args
#ifdef KRETPROBE_MAX_DATA_SIZE
static_assert(sizeof(struct _vfs_op_args) <= KRETPROBE_MAX_DATA_SIZE);
#endif

static inline void _vfs_op_args_init(struct _vfs_op_args *args) {
  args->path = NULL;
  args->new_path = NULL;
  args->ext_buf = NULL;
}
static inline int _vfs_op_args_alloc(struct _vfs_op_args *args) {
  args->ext_buf = kmalloc(PATH_MAX * 2, GFP_ATOMIC);
  if (unlikely(args->ext_buf == NULL)) {
    mpr_err("kmalloc");
    return 0;
  }
  return 1;
}
static inline void _vfs_op_args_clean(struct _vfs_op_args *args) {
  if (unlikely(args->ext_buf != NULL)) {
    kfree(args->ext_buf);
    args->ext_buf = NULL;
  }
}

///////////////////////////////////////////////////////////////////////////////
static int _vfs_common_enter(struct _vfs_op_args *args, struct dentry *de) {
  char *path;
  _vfs_op_args_init(args);

  if (unlikely(de == 0 || de->d_sb == 0)) return 1;

  path = dentry_path_raw(de, args->buf, sizeof(args->buf));
  if (unlikely(IS_ERR(path))) {
    trace_vfs_kprobe("kmalloc long path");
    if (_vfs_op_args_alloc(args)) {
      path = dentry_path_raw(de, args->ext_buf, PATH_MAX);
      if (IS_ERR(path)) {
        _vfs_op_args_clean(args);
        return 1;
      }
    } else {
      return 1;
    }
  }

  args->dev = de->d_sb->s_dev;
  args->path = path;
  return 0;
}

static int _vfs_common_return(struct _vfs_op_args *args, struct pt_regs *regs,
                              int action) {
  char root[NAME_MAX];
  if (unlikely(regs_return_value(regs) != 0 || args == NULL ||
               args->path == NULL))
    goto out;

  if (vfs_lookup_partition(args->dev, root, sizeof(root))) {
    vfs_put_change(action, strlen(root) == 1 ? 0 : root, args->path, 0);
  }
out:
  if (args) _vfs_op_args_clean(args);
  return 0;
}

///////////////////////////////////////////////////////////////////////////////
static int on_vfs_rename_ent(struct kretprobe_instance *ri,
                             struct pt_regs *regs) {
  struct _vfs_op_args *args = (struct _vfs_op_args *)ri->data;
  char *old_path, *new_path;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 12, 0)
  // vfs-rename: struct inode*, struct dentry*, struct inode*, struct dentry*,
  // struct inode**, unsigned int
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

  _vfs_op_args_init(args);

  if (unlikely(de_old == 0 || de_old->d_sb == 0 || de_new == 0)) return 1;
  old_path = dentry_path_raw(de_old, args->buf, sizeof(args->buf));
  if (unlikely(IS_ERR(old_path))) {
    trace_vfs_kprobe("kmalloc long path");
    if (_vfs_op_args_alloc(args)) {
      old_path = dentry_path_raw(de_old, args->ext_buf, PATH_MAX);
      if (IS_ERR(old_path)) goto err_out;
    } else {
      return 1;
    }
  }

  if (args->ext_buf == NULL) {
    new_path = dentry_path_raw(de_new, args->buf,
                               sizeof(args->buf) - strlen(old_path) - 1);
    if (unlikely(IS_ERR(new_path))) {
      if (!_vfs_op_args_alloc(args)) goto err_out;
      new_path = NULL;
    }
  }
  if (new_path == NULL) {
    new_path = dentry_path_raw(de_new, args->ext_buf + PATH_MAX, PATH_MAX);
    if (unlikely(IS_ERR(new_path))) goto err_out;
  }

  args->dev = de_old->d_sb->s_dev;
  args->dir = d_is_dir(de_old);
  args->path = old_path;
  args->new_path = new_path;
  return 0;
err_out:
  _vfs_op_args_clean(args);
  return 1;
}

static int on_vfs_rename_ret(struct kretprobe_instance *ri,
                             struct pt_regs *regs) {
  char root[NAME_MAX];
  struct _vfs_op_args *args = (struct _vfs_op_args *)ri->data;

  if (unlikely(regs_return_value(regs) != 0 || args == 0 || args->path == 0))
    goto quit;

  if (vfs_lookup_partition(args->dev, root, sizeof(root))) {
    vfs_put_change(args->dir ? ACT_RENAME_FOLDER : ACT_RENAME_FILE,
                   strlen(root) == 1 ? 0 : root, args->path, args->new_path);
  }

quit:
  if (args) _vfs_op_args_clean(args);
  return 0;
}

///////////////////////////////////////////////////////////////////////////////
struct _do_mount_args {
  char dir_name[NAME_MAX];
};
#define _sys_umount_args _do_mount_args
struct _do_mount_work {
  struct work_struct work;
  int is_umount;
  char dir_name[NAME_MAX];
};

static void _do_mount_work_handle(struct work_struct *work) {
  struct _do_mount_work *wk = (struct _do_mount_work *)work;
  struct path path;
  dev_t dev;
  char root[NAME_MAX];

  if (kern_path(wk->dir_name, LOOKUP_FOLLOW, &path) || path.dentry == NULL ||
      path.dentry->d_sb == NULL) {
    mpr_err("kern_path err %s", wk->dir_name);
    goto out;
  }
  dev = path.dentry->d_sb->s_dev;
  path_put(&path);

  if (wk->is_umount) {
    vfs_del_partition(dev);
  } else {
    if (vfs_lookup_partition(dev, root, sizeof(root))) {
      mpr_err("dev(%d) %s:%s", dev, root, wk->dir_name);
      goto out;
    } else {
      vfs_add_partition(wk->dir_name, dev);
    }
  }
out:
  kfree(work);
}

static void _do_mount_queue(int is_umount, const char *dir_name) {
  struct _do_mount_work *wk;

  if (unlikely(strlen(dir_name) >= NAME_MAX)) {
    mpr_err("dir_name is too long\n");
    return;
  }
  wk = kmalloc(sizeof(struct _do_mount_work), GFP_ATOMIC);
  if (unlikely(0 == wk)) {
    mpr_err("kmalloc failed");
    return;
  }
  strcpy(wk->dir_name, dir_name);
  wk->is_umount = is_umount;
  INIT_WORK(&(wk->work), _do_mount_work_handle);
  if (!schedule_work(&wk->work)) {
    mpr_err("schedule_work");
  }
}

static int on_do_mount_ent(struct kretprobe_instance *ri,
                           struct pt_regs *regs) {
  /*
   * >= 5.9
   * int path_mount(const char *dev_name, struct path *path,
   *  const char *type_page, unsigned long flags, void *data_page)
   *
   * < 5.9
   * long do_mount(const char *dev_name, const char __user *dir_name,
   *  const char *type_page, unsigned long flags, void *data_page)
   */
  char *type_name, *dir_name;
  struct _do_mount_args *args = (struct _do_mount_args *)ri->data;
  args->dir_name[0] = 0;

  /* 存储mount的时候的type值，在后面on_do_mount_ret 进行使用 */
  type_name = (char *)get_arg(regs, 3);
  /* 解决bug69979，升级过程中type_name为空导致内核崩溃 */
  if (type_name == NULL) return 1;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0)
  dir_name = (char *)get_arg(regs, 2);
  if (unlikely(dir_name == NULL)) {
    mpr_err("dir_name is null");
    return 1;
  }
  if (unlikely(strlen(dir_name) >= sizeof(args->dir_name))) {
    mpr_err("dir_name too long");
    return 1;
  }
  strcpy(args->dir_name, dir_name);
#else
  dir_name = d_path((struct path *)get_arg(regs, 2), args->dir_name,
                    sizeof(args->dir_name));
  if (IS_ERR(dir_name)) {
    mpr_err("d_path fail");
    args->dir_name[0] = '\0';
    return 1;
  }
#endif
  if (!vfs_pass_partition(dir_name)) {
    mpr_info("ignore %s", dir_name);
    args->dir_name[0] = '\0';
    return 1;
  }
  return 0;
}

static int on_do_mount_ret(struct kretprobe_instance *ri,
                           struct pt_regs *regs) {
  struct _do_mount_args *args = (struct _do_mount_args *)ri->data;

  if (regs_return_value(regs) != 0 || args == 0 || args->dir_name[0] == 0)
    return 0;

  mpr_info("mount %s", args->dir_name);
  _do_mount_queue(0, args->dir_name);
  return 0;
}

static int on_sys_umount_ent(struct kretprobe_instance *ri,
                             struct pt_regs *regs) {
  char *dir_name;
  struct _do_mount_args *args = (struct _do_mount_args *)ri->data;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0)
  // char __user* dir_name, int flags
  dir_name = (char *)get_arg(regs, 1);
  if (unlikely(dir_name == NULL)) {
    mpr_err("dir_name is null");
    return 1;
  }
  if (unlikely(strlen(dir_name) >= sizeof(args->dir_name))) {
    mpr_err("dir_name too long");
    return 1;
  }
  strcpy(args->dir_name, dir_name);
#else
  /* int path_umount(struct path *path, int flags) */
  dir_name = d_path((struct path *)get_arg(regs, 1), args->dir_name,
                    sizeof(args->dir_name));
  if (IS_ERR(dir_name)) {
    mpr_err("d_path fail\n");
    args->dir_name[0] = '\0';
    return 1;
  }
#endif
  if (!vfs_pass_partition(dir_name)) {
    mpr_info("ignore %s", dir_name);
    args->dir_name[0] = '\0';
    return 1;
  }
  return 0;
}

static int on_sys_umount_ret(struct kretprobe_instance *ri,
                             struct pt_regs *regs) {
  struct _do_mount_args *args = (struct _do_mount_args *)ri->data;

  if (regs_return_value(regs) != 0 || args == 0 || args->dir_name[0] == 0)
    return 0;

  /*增加工作队列处理方式*/
  mpr_info("umount %s", args->dir_name);
  _do_mount_queue(1, args->dir_name);
  return 0;
}

///////////////////////////////////////////////////////////////////////////////
#define _DECL_CMN_KRP(fn, symbol)               \
  static struct kretprobe fn##_krp = {          \
      .entry_handler = on_##fn##_ent,           \
      .handler = on_##fn##_ret,                 \
      .data_size = sizeof(struct _##fn##_args), \
      .maxactive = 64,                          \
      .kp.symbol_name = "" #symbol "",          \
  };

#define DECL_CMN_KRP(fn) _DECL_CMN_KRP(fn, fn)

#define DECL_VFS_KRP(fn, act, de_i)                                        \
  static int on_##fn##_ent(struct kretprobe_instance *ri,                  \
                           struct pt_regs *regs) {                         \
    return _vfs_common_enter((struct _vfs_op_args *)ri->data,              \
                             (struct dentry *)get_arg(regs, de_i));        \
  }                                                                        \
  static int on_##fn##_ret(struct kretprobe_instance *ri,                  \
                           struct pt_regs *regs) {                         \
    return _vfs_common_return((struct _vfs_op_args *)ri->data, regs, act); \
  }                                                                        \
  static struct kretprobe fn##_krp = {                                     \
      .entry_handler = on_##fn##_ent,                                      \
      .handler = on_##fn##_ret,                                            \
      .data_size = sizeof(struct _vfs_op_args),                            \
      .maxactive = 64,                                                     \
      .kp.symbol_name = "" #fn "",                                         \
  };

// clang-format off
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
DECL_CMN_KRP(vfs_rename);
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
// clang-format on

static struct kretprobe *vfs_krps[] = {
    &do_mount_krp,   &vfs_create_krp,
    &vfs_unlink_krp, &vfs_mkdir_krp,
    &vfs_rmdir_krp,  &vfs_symlink_krp,
    &vfs_link_krp,   &vfs_rename_krp,
    &sys_umount_krp, &security_inode_create_krp};

static atomic_t vfs_kprobe_counter = {0};

int vfs_kprobe_register(void) {
  int r;
  if (atomic_cmpxchg(&vfs_kprobe_counter, 0, 1) == 1) {
    mpr_info("already registed");
    return 0;
  }
  r = register_kretprobes(vfs_krps, sizeof(vfs_krps) / sizeof(void *));
  trace_vfs_kprobe("vfs_kprobe_register: %d", r);
  if (r < 0) {
    atomic_set(&vfs_kprobe_counter, 0);
    mpr_err("register_kretprobes %d", r);
    return 0;
  }
  return 1;
}
void vfs_kprobe_unregister(void) {
  if (atomic_cmpxchg(&vfs_kprobe_counter, 1, 0) == 0) {
    mpr_info("no registed");
    return;
  }
  unregister_kretprobes(vfs_krps, sizeof(vfs_krps) / sizeof(void *));
  trace_vfs_kprobe("unregister_kretprobes");
}
