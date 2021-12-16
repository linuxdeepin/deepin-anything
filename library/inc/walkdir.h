/*
 * Copyright (C) 2021 UOS Technology Co., Ltd.
 *
 * Author:     zccrs <zccrs@live.com>
 *
 * Maintainer: zccrs <zhangjide@deepin.com>
 *             yangwu <yangwu@uniontech.com>
 *             wangrong <wangrong@uniontech.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
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
