// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SERVER_FS_EVENT_H
#define SERVER_FS_EVENT_H

#define G_LOG_USE_STRUCTURED
#include <glib.h>

G_BEGIN_DECLS

#define MAX_PATH_LEN 4096

typedef struct {
    guint8      act;
    guint32     cookie;
    guint16     major;
    guint8      minor;
    gchar       src[MAX_PATH_LEN];
    gchar       dst[MAX_PATH_LEN];
} fs_event;

G_END_DECLS

#endif /* SERVER_FS_EVENT_H */
