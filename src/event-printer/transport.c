// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "transport.h"

#include <glib.h>
#include <glib-unix.h>
#include <string.h>

#include "vfs_change_consts.h"

#define SYSFS_TRANSPORT_PATH "/sys/kernel/vfs_monitor/transport"

EpTransport ep_transport_detect(void)
{
    gchar *contents = NULL;
    gsize length = 0;
    GError *error = NULL;

    if (!g_file_get_contents(SYSFS_TRANSPORT_PATH, &contents, &length, &error)) {
        g_warning("Cannot read %s: %s — assuming genl", SYSFS_TRANSPORT_PATH,
                  error->message);
        g_error_free(error);
        return EP_TRANSPORT_GENL;
    }

    g_strstrip(contents);
    EpTransport t;
    if (g_str_equal(contents, "genl"))
        t = EP_TRANSPORT_GENL;
    else if (g_str_equal(contents, "mmap"))
        t = EP_TRANSPORT_MMAP;
    else {
        g_warning("Unknown transport '%s' — assuming mmap", contents);
        t = EP_TRANSPORT_MMAP;
    }

    g_free(contents);
    return t;
}

void ep_event_print(const EpEvent *ev)
{
    g_return_if_fail(ev != NULL);

    if (ev->is_proc) {
        if (ev->cookie != 0)
            g_print("%-18s seq=%-10u cookie=%-10u uid=%u tgid=%d %s\n",
                    "PROC", ev->seq, ev->cookie, ev->uid, ev->tgid, ev->path);
        else
            g_print("%-18s seq=%-10u uid=%u tgid=%d %s\n",
                    "PROC", ev->seq, ev->uid, ev->tgid, ev->path);
        return;
    }

    if (ev->action == ACT_EVENT_LOSS) {
        g_print("%-18s seq=%-10u (dropped=%u)\n",
                "EVENT_LOSS", ev->seq, ev->seq);
        return;
    }

    const char *name = NULL;
    switch (ev->action) {
    case ACT_NEW_FILE:           name = "NEW_FILE"; break;
    case ACT_NEW_LINK:           name = "NEW_LINK"; break;
    case ACT_NEW_SYMLINK:        name = "NEW_SYMLINK"; break;
    case ACT_NEW_FOLDER:         name = "NEW_FOLDER"; break;
    case ACT_DEL_FILE:           name = "DEL_FILE"; break;
    case ACT_DEL_FOLDER:         name = "DEL_FOLDER"; break;
    case ACT_RENAME_FILE:        name = "RENAME_FILE"; break;
    case ACT_RENAME_FOLDER:      name = "RENAME_FOLDER"; break;
    case ACT_RENAME_FROM_FILE:   name = "RENAME_FROM_FILE"; break;
    case ACT_RENAME_TO_FILE:     name = "RENAME_TO_FILE"; break;
    case ACT_RENAME_FROM_FOLDER: name = "RENAME_FROM_FOLDER"; break;
    case ACT_RENAME_TO_FOLDER:   name = "RENAME_TO_FOLDER"; break;
    case ACT_MOUNT:              name = "MOUNT"; break;
    case ACT_UNMOUNT:            name = "UNMOUNT"; break;
    default:                     name = NULL; break;
    }

    if (name != NULL) {
        if (ev->cookie != 0)
            g_print("%-18s seq=%-10u cookie=%-10u %u:%u %s\n", name,
                    ev->seq, ev->cookie, ev->major, ev->minor, ev->path);
        else
            g_print("%-18s seq=%-10u %u:%u %s\n", name,
                    ev->seq, ev->major, ev->minor, ev->path);
    } else {
        g_print("UNKNOWN(%-3u)     seq=%-10u %u:%u %s\n", ev->action,
                ev->seq, ev->major, ev->minor, ev->path);
    }
}
