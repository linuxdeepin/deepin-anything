// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_FS_EVENT_H_
#define ANYTHING_FS_EVENT_H_

#include <stdint.h>

#define MAX_PATH_LEN 4096

G_BEGIN_DECLS

struct fs_event {
    uint8_t     act;
    uint32_t    cookie;
    char        src[MAX_PATH_LEN];
    char        dst[MAX_PATH_LEN];
};

typedef struct fs_event fs_event;

G_END_DECLS

#endif // ANYTHING_FS_EVENT_H_
