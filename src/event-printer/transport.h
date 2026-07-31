// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef EVENT_PRINTER_TRANSPORT_H
#define EVENT_PRINTER_TRANSPORT_H

#include <glib.h>

G_BEGIN_DECLS

/* Transport reported by /sys/kernel/vfs_monitor/transport */
typedef enum {
    EP_TRANSPORT_UNKNOWN = 0,
    EP_TRANSPORT_MMAP,
    EP_TRANSPORT_MMAP_RING,
    EP_TRANSPORT_GENL,
} EpTransport;
/* Local synthetic action: indicates events were dropped by the ring buffer.
 * Not a real kernel action; only produced by the mmap-ring receiver when it
 * observes a non-zero dropped_count delta. Carried in EpEvent.action; the
 * dropped count is reported via EpEvent.seq. */
#define ACT_EVENT_LOSS 200

/* Normalized event — produced by whichever receiver is active.
 * For dentry events proc_* fields are zero; for proc events the
 * dentry-specific fields (action/major/minor) are zero. */
typedef struct {
    gboolean   is_proc;
    guint8     action;
    guint32    cookie;
    guint32    seq;
    guint16    major;
    guint8     minor;
    guint32    uid;
    gint32     tgid;
    gchar      path[4096];
} EpEvent;

/* A receiver exposes a single fd to poll and a callback to drain one
 * event. Returns FALSE when the channel is exhausted or in error. */
typedef struct {
    int         fd;          /* -1 if inactive */
    gboolean    (*drain)(EpEvent *out, gpointer user_data);
    gpointer    user_data;
    GDestroyNotify destroy;
} EpChannel;

/* Read /sys/kernel/vfs_monitor/transport. Falls back to MMAP. */
EpTransport ep_transport_detect(void);

/* Pretty-print an event to stdout. */
void ep_event_print(const EpEvent *ev);

G_END_DECLS

#endif /* EVENT_PRINTER_TRANSPORT_H */
