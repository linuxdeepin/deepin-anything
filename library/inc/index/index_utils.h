// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

typedef struct __attribute__((__packed__)) __inkw_count_off__ {
	uint32_t len;
	uint64_t off;
} inkw_count_off;

uint32_t hash(const char* name);
inkw_count_off* load_inkw_count_offs(int fd, uint32_t count);
uint32_t get_insert_pos(uint32_t value, uint32_t* sorted, uint32_t size, int favor_big);
uint32_t add_inkw_fsbuf_offsets(index_keyword* inkw, uint32_t start_off, int delta);
