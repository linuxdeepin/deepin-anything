// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

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
