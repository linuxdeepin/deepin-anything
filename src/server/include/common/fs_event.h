// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_FS_EVENT_H_
#define ANYTHING_FS_EVENT_H_

#include <string>

#include "common/anything_fwd.hpp"

ANYTHING_NAMESPACE_BEGIN

#define MAX_PATH_LEN 4096

struct fs_event {
    uint8_t     act;
    uint32_t    cookie;
    uint16_t    major;
    uint8_t     minor;
    char        src[MAX_PATH_LEN];
    char        dst[MAX_PATH_LEN];
};

ANYTHING_NAMESPACE_END

#endif // ANYTHING_FS_EVENT_H_