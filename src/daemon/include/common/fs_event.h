// SPDX-FileCopyrightText: 2024 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_FS_EVENT_H_
#define ANYTHING_FS_EVENT_H_

#define G_LOG_USE_STRUCTURED
#include <glib.h>

#define MAX_PATH_LEN 4096

G_BEGIN_DECLS

typedef struct {
    guint8      act;
    guint32     cookie;
    guint32     seq;
    guint16     major;
    guint32     minor;
    gchar       path[MAX_PATH_LEN];
} fs_event;

G_END_DECLS

#endif // ANYTHING_FS_EVENT_H_
