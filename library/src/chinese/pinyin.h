// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PINYIN_H_INCLUDED
#define PINYIN_H_INCLUDED

#pragma once

// first chinese word's unicode is 19968 in of basic map
#define PINYIN_UNICODE_START 0x4E00
// last chinese word's unicode is 40896 of basic map
#define PINYIN_UNICODE_END 0x9FA5
#define MAX_PINYIN_WORD 20902
#define MAX_PINYIN_LEN 6

#define DICT_MAX_LEN (MAX_PINYIN_WORD * MAX_PINYIN_LEN + 1)

void utf8_to_pinyin(const char *in, char *out);
void convert_all_pinyin(const char *in, char *first, char *full);
// cat first and full words with '|' and return, need be freed in its invoker
char* cat_pinyin(const char *in);
int is_text_utf8(const char* str, long length);

#endif // PINYIN_H_INCLUDED
