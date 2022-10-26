// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#define PART_NAME_MAX	128
#define FS_TYPE_MAX		32

typedef struct __partition__ {
	char dev[PART_NAME_MAX];
	char mount_point[PART_NAME_MAX];
	char fs_type[FS_TYPE_MAX];
	char major, minor;
} partition;

typedef int (*progress_callback_fn)(uint32_t file_count, uint32_t dir_count, const char* cur_dir, const char *cur_file, void* param);

int get_partitions(int* part_count, partition* parts);
int build_fstree(fs_buf* fsbuf, int merge_partition, progress_callback_fn pcf, void *param);
