// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <linux/kernel.h>
#include <linux/kdev_t.h>
#include <linux/string.h>
#include <linux/types.h>

void vfs_init_partition(const char *mounts);
void vfs_add_partition(const char *pathname, dev_t dev);
void vfs_del_partition(dev_t dev);
int vfs_lookup_partition(dev_t dev, char *path, size_t size);
void vfs_clean_partition(void);

static inline int mounted_at(const char *mp, const char *root) {
  return strcmp(mp, root) == 0 ||
         (strlen(mp) > strlen(root) && strstr(mp, root) == mp &&
          mp[strlen(root)] == '/');
}

// 允许通过的分区
static inline int vfs_pass_partition(const char *mp) {
  if (unlikely(mounted_at(mp, "/sys") || mounted_at(mp, "/run") ||
               mounted_at(mp, "/dev") || mounted_at(mp, "/proc") ||
               strncmp("/", mp, 1) != 0)) {
    return 0;
  }
  return 1;
}