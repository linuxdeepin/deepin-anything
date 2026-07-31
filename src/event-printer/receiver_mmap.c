// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "transport.h"

#include <glib.h>
#include <glib-unix.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "vfs_ringbuf_uapi.h"

#define DEFAULT_DENTRY_DEV "/dev/vfs_monitor"
#define DEFAULT_PROC_DEV   "/dev/vfs_monitor_proc"

/* Per-channel state for one ring-buffer char device. */
typedef struct {
    int     fd;            /* -1 when inactive */
    size_t  record_size;
    guint   source_id;     /* g_unix_fd_add source */
    guint64 count;
    /* Shared with the receiver — the callback to invoke per event. */
    void  (*on_event)(const EpEvent *ev, gpointer user_data);
    gpointer user_data;
} MmapChannel;

/* Read one record from the device. Returns FALSE if no full record
 * is available right now (EAGAIN) or on error; TRUE with *out filled. */
static gboolean mmap_channel_read_one(MmapChannel *ch, EpEvent *out)
{
    union {
        struct vfs_ringbuf_dentry_rec dentry;
        struct vfs_ringbuf_proc_rec   proc;
    } buf;

    ssize_t n = read(ch->fd, &buf, ch->record_size);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return FALSE;
        g_warning("read: %s", g_strerror(errno));
        return FALSE;
    }
    if (n == 0 || (size_t)n != ch->record_size) {
        if (n != 0)
            g_warning("short read: %zd (expected %zu)", n, ch->record_size);
        return FALSE;
    }

    ch->count++;
    memset(out, 0, sizeof(*out));

    if (ch->record_size == sizeof(struct vfs_ringbuf_dentry_rec)) {
        out->is_proc = FALSE;
        out->action = buf.dentry.action;
        out->cookie = buf.dentry.cookie;
        out->seq    = buf.dentry.seq;
        out->major  = buf.dentry.major;
        out->minor  = buf.dentry.minor;
        memcpy(out->path, buf.dentry.path, buf.dentry.path_len + 1);
    } else {
        out->is_proc = TRUE;
        out->cookie = 0;
        out->seq    = buf.proc.seq;
        out->uid    = buf.proc.uid;
        out->tgid   = buf.proc.tgid;
        memcpy(out->path, buf.proc.path, buf.proc.path_len + 1);
    }
    out->path[sizeof(out->path) - 1] = '\0';
    return TRUE;
}

static gboolean on_fd_ready(G_GNUC_UNUSED gint fd,
                            GIOCondition condition,
                            gpointer data)
{
    MmapChannel *ch = data;

    if ((condition & (G_IO_HUP | G_IO_ERR)) != 0) {
        g_message("mmap channel fd=%d closed", fd);
        ch->source_id = 0;
        return G_SOURCE_REMOVE;
    }

    /* Drain all records available in this dispatch cycle. */
    for (;;) {
        EpEvent ev;
        if (!mmap_channel_read_one(ch, &ev))
            break;
        if (ch->on_event)
            ch->on_event(&ev, ch->user_data);
    }
    return G_SOURCE_CONTINUE;
}

static gboolean mmap_channel_open(MmapChannel *ch, const char *path,
                                  size_t record_size)
{
    ch->fd = open(path, O_RDONLY | O_NONBLOCK);
    if (ch->fd < 0) {
        g_warning("open(%s): %s", path, g_strerror(errno));
        return FALSE;
    }
    ch->record_size = record_size;
    ch->source_id = g_unix_fd_add(ch->fd,
                                  G_IO_IN | G_IO_HUP | G_IO_ERR,
                                  on_fd_ready, ch);
    g_message("opened %s (fd=%d)", path, ch->fd);
    return TRUE;
}

static void mmap_channel_close(MmapChannel *ch)
{
    if (ch->source_id > 0) {
        g_source_remove(ch->source_id);
        ch->source_id = 0;
    }
    if (ch->fd >= 0) {
        close(ch->fd);
        ch->fd = -1;
    }
}

/* Public receiver handle — owns both channels. */
struct EpMmapReceiver {
    MmapChannel dentry;
    MmapChannel proc;
};

typedef struct EpMmapReceiver EpMmapReceiver;

EpMmapReceiver *ep_mmap_receiver_new(const char *dentry_dev,
                                     const char *proc_dev,
                                     void (*on_event)(const EpEvent *, gpointer),
                                     gpointer user_data)
{
    EpMmapReceiver *r = g_new0(EpMmapReceiver, 1);
    r->dentry.on_event = on_event;
    r->dentry.user_data = user_data;
    r->proc.on_event = on_event;
    r->proc.user_data = user_data;

    gboolean ok = FALSE;
    if (mmap_channel_open(&r->dentry, dentry_dev ? dentry_dev : DEFAULT_DENTRY_DEV,
                          sizeof(struct vfs_ringbuf_dentry_rec)))
        ok = TRUE;
    else
        r->dentry.fd = -1;

    if (mmap_channel_open(&r->proc, proc_dev ? proc_dev : DEFAULT_PROC_DEV,
                          sizeof(struct vfs_ringbuf_proc_rec)))
        ok = TRUE;
    else
        r->proc.fd = -1;

    if (!ok) {
        g_critical("No mmap channels could be opened. "
                   "Is the vfs_monitor module loaded with transport=mmap?");
        mmap_channel_close(&r->dentry);
        mmap_channel_close(&r->proc);
        g_free(r);
        return NULL;
    }
    return r;
}

void ep_mmap_receiver_free(EpMmapReceiver *r)
{
    if (!r)
        return;
    if (r->dentry.count > 0)
        g_message("dentry events: %" G_GUINT64_FORMAT, r->dentry.count);
    if (r->proc.count > 0)
        g_message("proc events:   %" G_GUINT64_FORMAT, r->proc.count);
    mmap_channel_close(&r->dentry);
    mmap_channel_close(&r->proc);
    g_free(r);
}
