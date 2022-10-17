// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vfs_partition.h"

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/spinlock_types.h>
#include <linux/string.h>

#include "vfs_log.h"

struct krp_partition {
  struct rb_node node;
  dev_t dev;
  char path[0];
};

static struct rb_root partitions_root = RB_ROOT;
static DEFINE_RWLOCK(partitions_lock);

#if 0
#define trace_partition mpr_info
#else
#define trace_partition(...)
#endif

void vfs_init_partition(const char *mounts) {
  dev_t major, minor;
  char path[NAME_MAX];
  const char *line = mounts;
  const char *end = mounts + strlen(mounts);

  while (line < end) {
    if (sscanf(line, "%*d %*d %d:%d %*s %250s %*s %*s %*s %*s %*s %*s\n",
               &major, &minor, path) == 3) {
      if (vfs_pass_partition(path)) {
        vfs_add_partition(path, MKDEV(major, minor));
      } else {
        mpr_info("ignore: %s", path);
      }
    }
    line = strchr(line, '\n');
    if (line == NULL) break;
    line++;
  }
}

void vfs_add_partition(const char *pathname, dev_t dev) {
  struct rb_node **new = &(partitions_root.rb_node), *parent = NULL;
  struct krp_partition *partition;
  size_t pathLen = strlen(pathname) + 1;
  if (pathLen >= NAME_MAX) {
    mpr_err("path len: %s", pathname);
  }

  partition = kmalloc(sizeof(struct krp_partition) + pathLen, GFP_ATOMIC);
  if (unlikely(partition == NULL)) {
    mpr_err("kmalloc");
    return;
  }

  trace_partition("vfs_add_partition: %d:%d %s", MAJOR(dev), MINOR(dev),
                  pathname);

  strcpy(partition->path, pathname);
  partition->dev = dev;

  write_lock(&partitions_lock);
  while (*new) {
    struct krp_partition *cur = rb_entry(*new, struct krp_partition, node);
    parent = *new;
    if (dev > cur->dev) {
      new = &((*new)->rb_right);
    } else if (dev < cur->dev) {
      new = &((*new)->rb_left);
    } else {
      write_unlock(&partitions_lock);
      mpr_info("%s %d:%d duplicate(%s)", pathname, MAJOR(dev), MINOR(dev),
               cur->path);
      kfree(partition);
      return;
    }
  }
  rb_link_node(&partition->node, parent, new);
  rb_insert_color(&partition->node, &partitions_root);
  write_unlock(&partitions_lock);
}

static struct krp_partition *_vfs_search(dev_t dev) {
  struct rb_node *node = partitions_root.rb_node;
  while (node) {
    struct krp_partition *partition =
        rb_entry(node, struct krp_partition, node);
    if (dev < partition->dev) {
      node = node->rb_left;
    } else if (dev > partition->dev) {
      node = node->rb_right;
    } else {
      return partition;
    }
  }
  return NULL;
}

int vfs_lookup_partition(dev_t dev, char *path, size_t size) {
  int ret = 0;
  struct krp_partition *partition;

  read_lock(&partitions_lock);
  partition = _vfs_search(dev);
  if (partition != NULL) {
    ret = 1;
    // BUG_ON(size < NAMEMAX)
    strcpy(path, partition->path);
  }
  read_unlock(&partitions_lock);

  return ret;
}

void vfs_del_partition(dev_t dev) {
  struct krp_partition *partition;

  write_lock(&partitions_lock);
  partition = _vfs_search(dev);
  if (partition != NULL) {
    rb_erase(&partition->node, &partitions_root);
    write_unlock(&partitions_lock);
    trace_partition("vfs_del_partition: %s %d:%d", partition->path,
                    MAJOR(partition->dev), MINOR(partition->dev));
    kfree(partition);
  } else {
    write_unlock(&partitions_lock);
  }
}

void vfs_clean_partition(void) {
  struct rb_node *node = rb_first(&partitions_root);
  write_lock(&partitions_lock);
  while (node != NULL) {
    struct krp_partition *partition =
        rb_entry(node, struct krp_partition, node);
    node = rb_next(node);
    trace_partition("vfs_clean_partition: %s %d:%d", partition->path,
                    MAJOR(partition->dev), MINOR(partition->dev));
    kfree(partition);
  }
  trace_partition("cleanup");
  partitions_root.rb_node = NULL;
  write_unlock(&partitions_lock);
}
