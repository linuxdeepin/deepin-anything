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

#define ERR_NO_MEM		1
#define ERR_NO_PATH		2
#define ERR_PATH_EXISTS	3
#define ERR_NESTED		4
#define ERR_PATH_DIFFER	5
#define ERR_NOTEMPTY	6

typedef struct __fs_buf__ fs_buf;

typedef struct __fs_change__ {
	uint32_t start_off;
	// delta < 0 means some file/folder has been removed, delta > 0 means some file/folder has been added
	int delta;
} fs_change;

/* the search rule defines */
#define RULE_NONE 0x00 //not use
/* 0x01-0x3F: search related defines */
// search with RegExp, don't use regexp if without this rule in search API.
#define RULE_SEARCH_REGX 0x01
// search with request max count.
#define RULE_SEARCH_MAX_COUNT 0x02
// ignore case while compare string.
#define RULE_SEARCH_ICASE 0x03
// search from the start offset in index.
#define RULE_SEARCH_STARTOFF 0x04
// search with a end offset in index.
#define RULE_SEARCH_ENDOFF 0x05
// search pinyin index.
#define RULE_SEARCH_PINYIN 0x06

/* 0x40-0x7F: exclude these results */
// exclude the substring in the name of the directory or file: SUB_S startwith; SUB_D endwith.
#define RULE_EXCLUDE_SUB_S 0x40
#define RULE_EXCLUDE_SUB_D 0x41
// ignore sepcial absolute path or file for match
#define RULE_EXCLUDE_PATH 0x42

/* 0x80-0xAF: include these results */
// include the substring in the name of the directory or file: SUB_S startwith; SUB_D endwith.
#define RULE_INCLUDE_SUB_S 0x80
#define RULE_INCLUDE_SUB_D 0x81


// define the search rule link to record the rules.
typedef struct __search_rule__
{
	uint8_t flag;
	char target[255]; //Linux max name length
	struct __search_rule__ *next;
} search_rule;

uint32_t get_capacity(fs_buf* fsbuf);
uint32_t first_name(fs_buf* fsbuf);
const char* get_root_path(fs_buf* fsbuf);

uint32_t get_tail(fs_buf* fsbuf);
// thread-unsafe
char* get_name(fs_buf* fsbuf, uint32_t name_off);
fs_buf* new_fs_buf(uint32_t capacity, const char* root_path);
void free_fs_buf(fs_buf* fsbuf);

int is_file(fs_buf* fsbuf, uint32_t name_off);
// thread-unsafe
uint32_t next_name(fs_buf* fsbuf, uint32_t name_off);

char* get_path_by_name_off(fs_buf* fsbuf, uint32_t name_off, char *path, uint32_t path_size);

int save_fs_buf(fs_buf* fsbuf, const char* filename);
int load_fs_buf(fs_buf** pfsbuf, const char* filename);

int insert_path(fs_buf* fsbuf, const char *path, int is_dir, fs_change* change);
int remove_path(fs_buf* fsbuf, const char *path, fs_change* changes, uint32_t* change_count);
int rename_path(fs_buf* fsbuf, const char* src_path, const char* dst_path, fs_change* changes, uint32_t* change_count);

void get_path_range(fs_buf *fsbuf, const char *path, uint32_t *path_off, uint32_t *start_off, uint32_t *end_off);

// do not check null pointer.
typedef int (*progress_fn)(uint32_t count, const char* cur_file, void* param);
typedef int (*comparator_fn)(const char *file_name, void* param);
void search_files(fs_buf* fsbuf, uint32_t* start_off, uint32_t end_off, uint32_t* results, uint32_t* count,
		comparator_fn comparator, void *comparator_param, progress_fn pcf, void *pcf_param);
void parallelsearch_files(fs_buf* fsbuf, uint32_t* start_off, uint32_t end_off, uint32_t* results, uint32_t* count,
		search_rule *rule, const char *query);

// functions below are used internally
void set_kids_off(fs_buf* fsbuf, uint32_t name_off, uint32_t kids_off);
int append_new_name(fs_buf* fsbuf, char* name, int is_dir);
int append_parent(fs_buf* fsbuf, uint32_t parent_off);
