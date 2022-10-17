
// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#include "vfs_change.h"
#include "vfs_kprobe.h"
#include "vfs_partition.h"

#define ALLOC_UNIT (1 << 12)

/*
        kernel_read_file_from_path cant be used here because:
        1. procfs doesnt have i_size, which is used by kernel_read_file, which
   is in turn called by k_r_f_f_p
        2. we want to avoid security_XXX calls
*/
char* __init read_file_content(const char* filename, int* real_size) {
  struct file* filp = filp_open(filename, O_RDONLY, 0);
  if (IS_ERR(filp)) {
    pr_err("error opening %s\n", filename);
    return 0;
  }
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
  mm_segment_t old_fs = get_fs();
  set_fs(KERNEL_DS);
#endif

  // i_size_read is useless here because procfs does not have i_size
  // loff_t size = i_size_read(file_inode(filp));
  char* buf = 0;
  int size = ALLOC_UNIT;
  while (1) {
    buf = kmalloc(size, GFP_KERNEL);
    if (unlikely(buf == 0)) break;

    loff_t off = 0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
    *real_size = kernel_read(filp, buf, size, &off);
#else
    char __user* user_buf = (char __user*)buf;
    *real_size = kernel_read(filp, user_buf, size, &off);
#endif
    if (*real_size > 0 && *real_size < size) {
      buf[*real_size] = 0;
      break;
    }

    size += ALLOC_UNIT;
    kfree(buf);
  }
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
  set_fs(old_fs);
#endif
  filp_close(filp, 0);
  // pr_info("%s size: %d\n", filename, *real_size);
  return buf;
}

static void __init init_mounts_info(void) {
  int size;
  char* buf = read_file_content("/proc/self/mountinfo", &size);
  if (buf) {
    vfs_init_partition(buf);
  }
}

int __init init_module() {
  init_mounts_info();
  if (!vfs_init_change()) {
    vfs_clean_partition();
    return ENOMEM;
  }
  if (!vfs_kprobe_register()) {
    vfs_clean_change();
    vfs_clean_partition();
  }
  return 0;
}

void __exit cleanup_module() {
  vfs_kprobe_unregister();
  vfs_clean_change();
  vfs_clean_partition();
}
