// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MOUNT_INFO_H
#define MOUNT_INFO_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct _MountInfo MountInfo;

MountInfo *mount_info_new();

void mount_info_free(MountInfo *mount_info);

void mount_info_update(MountInfo *mount_info);

const gchar *mount_info_get_device_mount_point(MountInfo *mount_info, dev_t device_id);

const GList *mount_info_get_child_mount_points(MountInfo *mount_info, dev_t device_id);

gchar *mount_info_dump(MountInfo *mount_info);

gboolean mount_info_exist_lowerfs(MountInfo *mount_info);

G_END_DECLS

#endif /* MOUNT_INFO_H */