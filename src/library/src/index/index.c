// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#define _DEFAULT_SOURCE

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <wchar.h>

#include "index.h"
#include "index_base.h"
#include "index_allfile.h"
#include "index_allmem.h"
#include "utils.h"

// File System Indice
const char index_magic[] = "FSI";

int load_index_keyword(int fd, index_keyword* inkw, int load_policy, const char* query)
{
	uint32_t sizes[2];
	if (read(fd, sizes, sizeof(sizes)) != sizeof(sizes))
		return 1;

	sizes[0] -= sizeof(uint32_t);
	sizes[0] -= sizes[1]*sizeof(uint32_t);

	// size includes the last \0
	char s[sizes[0]];
	if (read(fd, s, sizes[0]) != sizes[0])
		return 2;

	if (query && strcmp(query, s) != 0) {
		if (lseek(fd, sizeof(uint32_t)*sizes[1], SEEK_CUR) == -1)
			return 3;
		return -1;
	}

	int ret = set_cs_string(&inkw->keyword, s);
	if (ret == CS_SET_STR_FAIL)
		return 4;

	// for now we dont vailidate sizes[1]
	inkw->fsbuf_offsets = malloc(sizeof(uint32_t) * sizes[1]);
	if (inkw->fsbuf_offsets == 0)
		return 6;
	inkw->len = sizes[1];

	if (read_file(fd, (char *)inkw->fsbuf_offsets, sizes[1]*sizeof(uint32_t)) != 0)
		return 7;

	return 0;
}

uint64_t save_index_keyword(int fd, index_keyword* inkw)
{
	char *s = get_cs_string(&inkw->keyword);

	uint32_t inkw_size = sizeof(uint32_t);  // count of fs_buf pointers
	inkw_size += strlen(s) + 1;
	inkw_size += sizeof(uint32_t)*inkw->len;  // fs_buf pointers

	// s should be small enough, after all, index cant be that long
	char buf[NAME_MAX];
	uint32_t len = inkw->len;
	memcpy(buf, &inkw_size, sizeof(inkw_size));
	memcpy(buf + sizeof(inkw_size), &len, sizeof(len));
	memcpy(buf + sizeof(inkw_size) + sizeof(len), s, strlen(s) + 1);

	len = sizeof(uint32_t)*2 + strlen(s) + 1;
	if (write(fd, buf, len) != len)
		return 0;

	if (write_file(fd, (char*)inkw->fsbuf_offsets, inkw->len*sizeof(uint32_t)) != 0)
		return 0;

	return inkw_size + sizeof(uint32_t);
}

__attribute__((visibility("default"))) void free_index_keyword(index_keyword* inkw, int free_all)
{
	if (0 == inkw)
		return;

	if (inkw->fsbuf_offsets)
		free(inkw->fsbuf_offsets);

	free_composite_str(&inkw->keyword);

	if (free_all)
		free(inkw);
}

__attribute__((visibility("default"))) void get_stats(fs_index* fsi, uint64_t *memory, uint32_t* keywords, uint32_t* fsbuf_offsets)
{
	return fsi->get_statistics(fsi, memory, keywords, fsbuf_offsets);
}

__attribute__((visibility("default"))) int get_load_policy(fs_index* fsi)
{
	return fsi->get_load_policy();
}

__attribute__((visibility("default"))) index_keyword* get_index_keyword(fs_index* fsi, const char* query_utf8)
{
	return fsi->get_index_keyword(fsi, query_utf8);
}

__attribute__((visibility("default"))) void free_fs_index(fs_index* fsi)
{
	fsi->free_fs_index(fsi);
}

__attribute__((visibility("default"))) int load_fs_index(fs_index** pfsi, const char* filename, int load_policy)
{
	int fd = open(filename, O_RDWR);
	if (fd < 0)
		return 1;

	char magic[4];
	if (read(fd, magic, sizeof(magic)) != sizeof(magic) || strcmp(magic, index_magic) != 0) {
		close(fd);
		return 2;
	}

	// we won't verify len here
	uint32_t len;
	if (read(fd, &len, sizeof(len)) != sizeof(len)) {
		close(fd);
		return 3;
	}

	switch (load_policy) {
	case LOAD_ALL:
		return load_allmem_index(pfsi, fd, len);
	case LOAD_NONE:
		return load_allfile_index(pfsi, fd, len);
	default:
		close(fd);
		return -1;
	}
}

__attribute__((visibility("default"))) void add_index(fs_index* fsi, char* name, uint32_t fsbuf_offset)
{
	// from utf8 to wchar_t
	wchar_t converted[NAME_MAX];
	if (utf8_to_wchar_t(name, converted, sizeof(converted) - sizeof(wchar_t)) != 0)
		return;

	wchar_t index[NAME_MAX];
	for (int i = 0; i < wcslen(converted); i++)
		for (int j = i+1; j <= wcslen(converted) && j <= i+MAX_KW_LEN; j++) {
			wcsncpy(index, converted+i, j-i);
			index[j-i] = 0;
			char index_utf8[NAME_MAX];
			if (wchar_t_to_utf8(index, index_utf8, sizeof(index_utf8) - sizeof(char)) != 0)
				continue;

			fsi->add_index(fsi, index_utf8, fsbuf_offset);
		}
}

void add_fsbuf_offsets(fs_index* fsi, uint32_t start_off, int delta)
{
	return fsi->add_fsbuf_offsets(fsi, start_off, delta);
}

