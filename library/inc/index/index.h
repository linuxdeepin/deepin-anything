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

#include <stdint.h>

#include "composite_str.h"

// you can set MAX-KW-LEN when compiling
#ifndef MAX_KW_LEN
#define MAX_KW_LEN		8
#endif

#define LOAD_ALL		0
#define LOAD_NONE		1

#pragma pack(push, 4)

typedef struct __index_keyword__ {
	composite_str keyword;
	uint32_t* fsbuf_offsets;
	uint32_t len:28;
	uint32_t empty:4;
} index_keyword;

typedef struct __index_hash__ {
	index_keyword* keywords;
	uint32_t len:28;
	uint32_t empty:4;
} index_hash;

#pragma pack(pop)

typedef struct __fs_index__ fs_index;

void free_index_keyword(index_keyword* inkw, int free_all);
void get_stats(fs_index* fsi, uint64_t *memory, uint32_t* keywords, uint32_t* fsbuf_offsets);

int load_fs_index(fs_index** pfsi, const char* filename, int load_policy);
int get_load_policy(fs_index* fsi);
void free_fs_index(fs_index* fsi);
index_keyword* get_index_keyword(fs_index* fsi, const char* query_utf8);
void add_index(fs_index* fsi, char* name, uint32_t fsbuf_offset);
void add_fsbuf_offsets(fs_index* fsi, uint32_t start_off, int delta);
