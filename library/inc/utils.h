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
#include <wchar.h>

#ifdef IDX_DEBUG

#include <stdio.h>

#define dbg_msg(fmt, ...) do {\
	fprintf(stderr, "[idx] "fmt, ##__VA_ARGS__);\
} while(0)

#else

#define dbg_msg(fmt, ...) do{} while(0)

#endif

int utf8_to_wchar_t(char* input, wchar_t* output, size_t output_bytes);
int wchar_t_to_utf8(const wchar_t* input, char* output, size_t output_bytes);

int read_file(int fd, char* head, uint32_t size);
int write_file(int fd, char* head, uint32_t size);
