// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdlib.h>
#include <string.h>

#include "index.h"
#include "index_utils.h"
#include "utils.h"

uint32_t hash(const char* name)
{
	uint32_t result = 0;
	while (*name) {
		result = result*31 + *name;
		name++;
	}
	return result;
}

uint32_t get_insert_pos(uint32_t value, uint32_t* sorted, uint32_t size, int favor_big)
{
	if (size == 0 || sorted[0] >= value)
		return 0;

	if (sorted[size-1] == value)
		return size-1;

	if (sorted[size-1] < value)
		return favor_big ? size : size-1;

	for (uint32_t i = 0; i < size-1; i++) {
		if (sorted[i] == value)
			return i;

		if (sorted[i] < value && value < sorted[i+1])
			return favor_big ? i+1 : i;
	}

	// this should never happen
	return favor_big ? size : size-1;
}

inkw_count_off* load_inkw_count_offs(int fd, uint32_t count)
{
	inkw_count_off* icos = malloc(sizeof(inkw_count_off) * count);
	if (icos == 0)
		return 0;

	if (read_file(fd, (char *)icos, sizeof(inkw_count_off) * count) != 0) {
		free(icos);
		return 0;
	}

	return icos;
}

uint32_t add_inkw_fsbuf_offsets(index_keyword* inkw, uint32_t start_off, int delta)
{
	if (inkw->len == 0)
		return 0;

	// fsbuf-offsets are sorted from small to big
	if (inkw->fsbuf_offsets[inkw->len-1] < start_off)
		return 0;

	uint32_t total = 0;
	for (uint32_t n = 0; n < inkw->len; n++)
		if (inkw->fsbuf_offsets[n] >= start_off) {
			inkw->fsbuf_offsets[n] += delta;
			total++;
		}
	return total;
}
