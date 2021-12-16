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

#define CS_LONG_STR		0
#define CS_SHORT_STR	1
#define CS_SET_STR_FAIL	2

// memory optimization for 64bit system
typedef union __composite_str__ {
	struct {
		char s[7];
		unsigned char flag;
	} short_str;
	char* p;
} composite_str;

char* get_cs_string(composite_str* cs);
int set_cs_string(composite_str* cs, const char* s);
void free_composite_str(composite_str *cs);
