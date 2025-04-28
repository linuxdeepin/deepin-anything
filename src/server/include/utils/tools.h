// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_TOOLS_H_
#define ANYTHING_TOOLS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int get_file_info(const char *file_path, const char **file_type, int64_t *modify_time, int64_t *file_size);

char *format_time(int64_t time);

char *format_size(int64_t size);

char *get_full_path(const char *path);

unsigned int get_thread_pool_size_from_env(unsigned int default_size);

#ifdef __cplusplus
}
#endif


#endif // ANYTHING_TOOLS_H_