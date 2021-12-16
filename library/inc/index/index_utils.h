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

typedef struct __attribute__((__packed__)) __inkw_count_off__ {
	uint32_t len;
	uint64_t off;
} inkw_count_off;

uint32_t hash(const char* name);
inkw_count_off* load_inkw_count_offs(int fd, uint32_t count);
uint32_t get_insert_pos(uint32_t value, uint32_t* sorted, uint32_t size, int favor_big);
uint32_t add_inkw_fsbuf_offsets(index_keyword* inkw, uint32_t start_off, int delta);
