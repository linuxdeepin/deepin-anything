// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "vfs_change.h"
#include "vfs_kprobe.h"
#include "vfs_log.h"
#include "vfs_partition.h"

#define CHRDEV_NAME "driver_set_info"
// 表示在/system/class目录下创建的设备类别目录
#define CLASS_NAME "class_set_info"
// 在/dev/目录和/sys/class/class_wrbuff目录下分别创建设备文件driver_wrbuff
#define DEVICE_NAME "driver_set_info"

static dev_t devt_wrbuffer; /* alloc_chrdev_region函数向内核申请下来的设备号 */
static struct cdev *cdev_wrbuffer;   /* 注册到驱动的字符设备 */
static struct class *class_wrbuffer; /* 字符设备创建的设备节点 */
/* 打开 */
static int my_open(struct inode *inode, struct file *file) {
  if (!try_module_get(THIS_MODULE)) {
    return -ENODEV;
  }
  return 0;
}
/* 关闭 */
static int my_release(struct inode *inode, struct file *file) {
  module_put(THIS_MODULE);
  return 0;
}
/* 读设备里的信息 */
static ssize_t my_read(struct file *file, char __user *user, size_t t,
                       loff_t *f) {
  return 0;
}
static ssize_t my_write(struct file *file, const char __user *user, size_t t,
                        loff_t *f) {
  char *mounts = NULL;
  if (vfs_kprobe_register()) {
    mounts = kmalloc(t + 1, GFP_KERNEL);
    if (!mounts) {
      mpr_err("kmalloc");
      return 0;
    }
    if (copy_from_user(mounts, user, t)) {
      mpr_err("copy_from_user failed");
    } else {
      mounts[t] = '\0';
      vfs_init_partition(mounts);
    }
  }
  if (mounts) kfree(mounts);
  return t;
}

static struct file_operations fops_chrdriver = {
  open : my_open,
  release : my_release,
  read : my_read,
  write : my_write,
};

int __init init_module() {
  int ret = 0;

  /* 申请设备号 */
  ret = alloc_chrdev_region(&devt_wrbuffer, 0, 1, CHRDEV_NAME);
  if (ret) {
    mpr_err("alloc char driver error!\n");
    return ret;
  }

  /* 注册字符设备 */
  cdev_wrbuffer = cdev_alloc();
  if (!cdev_wrbuffer) {
    mpr_err("cdev_alloc");
    unregister_chrdev_region(devt_wrbuffer, 1);
    return ENOMEM;
  }
  cdev_init(cdev_wrbuffer, &fops_chrdriver);
  cdev_wrbuffer->owner = THIS_MODULE;
  ret = cdev_add(cdev_wrbuffer, devt_wrbuffer, 1);
  if (ret) {
    mpr_err("cdev_add");
    cdev_del(cdev_wrbuffer);
    unregister_chrdev_region(devt_wrbuffer, 1);
    return ret;
  }

  /* 创建设备节点 */
  class_wrbuffer = class_create(THIS_MODULE, CLASS_NAME);
  if (IS_ERR(class_wrbuffer)) {
    mpr_err("class_create");
    cdev_del(cdev_wrbuffer);
    unregister_chrdev_region(devt_wrbuffer, 1);
    return ENOMEM;
  }
  struct device *dev =
      device_create(class_wrbuffer, NULL, devt_wrbuffer, NULL, DEVICE_NAME);
  if (IS_ERR(dev)) {
    /* 删除设备节点 */
    mpr_err("device_create");
    class_destroy(class_wrbuffer);
    cdev_del(cdev_wrbuffer);
    unregister_chrdev_region(devt_wrbuffer, 1);
    return ENOMEM;
  }
  if (!vfs_init_change()) {
    /* 删除设备节点 */
    device_destroy(class_wrbuffer, devt_wrbuffer);
    class_destroy(class_wrbuffer);
    /* 取消字符设备注册 */
    cdev_del(cdev_wrbuffer);
    /* 释放设备号 */
    unregister_chrdev_region(devt_wrbuffer, 1);

    return ENOMEM;
  }
  return 0;
}

void __exit cleanup_module() {
  vfs_kprobe_unregister();
  vfs_clean_change();
  vfs_clean_partition();
  /* 删除设备节点 */
  device_destroy(class_wrbuffer, devt_wrbuffer);
  class_destroy(class_wrbuffer);
  /* 取消字符设备注册 */
  cdev_del(cdev_wrbuffer);
  /* 释放设备号 */
  unregister_chrdev_region(devt_wrbuffer, 1);
}
