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

#include <stdlib.h>
#include <string.h>

#include "composite_str.h"

#define KW_WORD_TAG		0x75

char* get_cs_string(composite_str* cs)
{
	return cs->short_str.flag == KW_WORD_TAG ? cs->short_str.s : cs->p;
}

int set_cs_string(composite_str* cs, const char* s)
{
	int long_str = strlen(s) >= sizeof(cs->short_str.s);
	cs->p = 0;
	if (long_str)
		cs->p = strdup(s);
	else {
		cs->short_str.flag = KW_WORD_TAG;
		strcpy(cs->short_str.s, s);
	}
	
	if (cs->p == 0)
		return CS_SET_STR_FAIL;

	return long_str ? CS_LONG_STR : CS_SHORT_STR;
}

void free_composite_str(composite_str *cs)
{
	if (cs->p != 0 && cs->short_str.flag != KW_WORD_TAG)
		free(cs->p);
}

