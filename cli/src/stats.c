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
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <limits.h>

#include "fs_buf.h"
#include "index.h"
#include "stats.h"
#include "utils.h"

static uint64_t print_file_info(fs_buf* fsbuf)
{
	uint64_t files_count = 0;
	uint32_t filename_lens[NAME_MAX] = {0};

	uint32_t name_off = first_name(fsbuf);
	while (name_off < get_tail(fsbuf)) {
		char* s = get_name(fsbuf, name_off);
		if (strlen(s) > 0)
			files_count++;
		wchar_t converted[NAME_MAX];
		if (utf8_to_wchar_t(s, converted, sizeof(converted) - sizeof(wchar_t)) == 0)
			filename_lens[wcslen(converted)-1]++;
		name_off = next_name(fsbuf, name_off);
	}

	for (int i = 0; i < NAME_MAX/16; i++) {
		printf("%03d:%03d: ", i*16+1, i*16+16);
		for (int j=0; j<16; j++)
			if (filename_lens[i*16 + j] == 0)
				printf("..... ");
			else
				printf("%5u ", filename_lens[i*16 + j]);
		printf("\n");
	}
	return files_count;
}

void collect_print_statistics(fs_buf* fsbuf, fs_index* fsi)
{
	// since fs_buf type is hidden, so we will miss its size here, which is minor
	uint64_t total_alloced = get_capacity(fsbuf);
	uint64_t files_count = print_file_info(fsbuf);

	uint64_t mem = 0;
	uint32_t keywords = 0, offsets = 0;
	if (fsi)
		get_stats(fsi, &mem, &keywords, &offsets);

	total_alloced += mem;
	printf("file-count: %'lu, mem: %'lu (%'lu KB), fs-buf-off: %'u, keywords: %'u, indice: %'u\n", 
		files_count, total_alloced, total_alloced >> 10, get_tail(fsbuf), keywords, offsets);
}
