// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

/* Zero-copy mmap ring-buffer receiver.
 *
 * Unlike receiver_mmap.c (which uses read()), this receiver mmaps the
 * kernel's ring-buffer pages directly and consumes records from the
 * mapped memory without any copy_to_user. The kernel is the sole
 * producer (serialized by producer_lock); we are the sole consumer
 * (enforced by vfs_chrdev_open returning -EBUSY on a second open).
 *
 * Memory layout (single contiguous region returned by one mmap call):
 *   [ struct vfs_ringbuf_header | slot[0] ... slot[capacity-1] ]
 *
 * Consumer protocol (SPSC, lock-free):
 *   1. load producer_head with acquire semantics
 *   2. compare with consumer_tail — if equal, ring is empty
 *   3. read slot at (tail & mask) directly from mapped memory
 *   4. advance consumer_tail with release semantics
 *
 * We use __atomic_load_n/__atomic_store_n with the appropriate memory
 * models to mirror the kernel's smp_load_acquire / smp_store_release.
 */

#include "transport.h"

#include <glib.h>
#include <glib-unix.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>

#include "vfs_ringbuf_uapi.h"

#define DEFAULT_DENTRY_DEV "/dev/vfs_monitor"
#define DEFAULT_PROC_DEV   "/dev/vfs_monitor_proc"

/* Per-channel state for one mmap'd ring buffer. */
typedef struct {
    int    fd;            /* -1 when inactive */
    guint  source_id;     /* g_unix_fd_add source */
    guint64 count;

    /* Mapped region — header lives at base, slots after. */
    void  *mem;
    size_t map_size;

    struct vfs_ringbuf_header *hdr;
    const char  *slots;     /* pointer to slot area (hdr + 1) */
    size_t       record_size;
    gboolean     is_dentry; /* TRUE = dentry rec, FALSE = proc rec */

    /* Last seen dropped_count from the kernel. A non-zero delta means the
     * ring was full and the kernel dropped events; we synthesize an
     * ACT_EVENT_LOSS event so the loss is observable in the event stream. */
    __u64  last_dropped;

    void  (*on_event)(const EpEvent *ev, gpointer user_data);
    gpointer user_data;
} RingChannel;

/* Translate a raw record at @rec into a normalized EpEvent. */
static void ring_channel_fill_event(RingChannel *ch, const void *rec, EpEvent *out)
{
    memset(out, 0, sizeof(*out));

    if (ch->is_dentry) {
        const struct vfs_ringbuf_dentry_rec *d = rec;
        out->is_proc = FALSE;
        out->action = d->action;
        out->cookie = d->cookie;
        out->seq    = d->seq;
        out->major  = d->major;
        out->minor  = d->minor;
        memcpy(out->path, d->path, d->path_len + 1);
    } else {
        const struct vfs_ringbuf_proc_rec *p = rec;
        out->is_proc = TRUE;
        out->cookie  = 0;  /* proc records have no cookie field */
        out->seq     = p->seq;
        out->uid     = p->uid;
        out->tgid    = p->tgid;
        memcpy(out->path, p->path, p->path_len + 1);
    }
    out->path[sizeof(out->path) - 1] = '\0';
}

/* Drain all available records from the ring. Called when poll fires. */
static gboolean ring_channel_drain(RingChannel *ch)
{
    /* Check for dropped events before consuming. The kernel increments
     * hdr->dropped_count when the ring is full; a rising delta means we
     * lost events. We synthesize an ACT_EVENT_LOSS event so the loss is
     * visible in the event stream alongside real events. */
    __u64 dropped = __atomic_load_n(&ch->hdr->dropped_count,
                                    __ATOMIC_ACQUIRE);
    if (dropped != ch->last_dropped) {
        __u64 delta = dropped - ch->last_dropped;
        ch->last_dropped = dropped;

        EpEvent loss;
        memset(&loss, 0, sizeof(loss));
        loss.is_proc = FALSE;
        loss.action  = ACT_EVENT_LOSS;
        loss.seq     = (guint32)delta;  /* dropped count, not a real seq */
        if (ch->on_event)
            ch->on_event(&loss, ch->user_data);
    }

    for (;;) {
        __u64 head = __atomic_load_n(&ch->hdr->producer_head,
                                     __ATOMIC_ACQUIRE);
        __u64 tail = __atomic_load_n(&ch->hdr->consumer_tail,
                                     __ATOMIC_ACQUIRE);

        if (head == tail)
            break;  /* ring empty */

        __u32 slot = tail & (ch->hdr->capacity - 1);
        const void *rec = ch->slots + (size_t)slot * ch->record_size;

        EpEvent ev;
        ring_channel_fill_event(ch, rec, &ev);
        ch->count++;
        if (ch->on_event)
            ch->on_event(&ev, ch->user_data);

        /* Advance consumer_tail — release pairs with kernel's acquire. */
        __atomic_store_n(&ch->hdr->consumer_tail, tail + 1,
                         __ATOMIC_RELEASE);
    }
    return TRUE;
}

static gboolean on_fd_ready(G_GNUC_UNUSED gint fd,
                           GIOCondition condition,
                           gpointer data)
{
    RingChannel *ch = data;

    if ((condition & (G_IO_HUP | G_IO_ERR)) != 0) {
        g_message("mmap-ring channel fd=%d closed", fd);
        ch->source_id = 0;
        return G_SOURCE_REMOVE;
    }

    ring_channel_drain(ch);
    return G_SOURCE_CONTINUE;
}

static gboolean ring_channel_open(RingChannel *ch, const char *path,
                                 gboolean is_dentry)
{
    ch->fd = open(path, O_RDWR | O_NONBLOCK);
    if (ch->fd < 0) {
        g_warning("open(%s): %s", path, g_strerror(errno));
        return FALSE;
    }

    /* Determine record size from the uapi struct. */
    ch->is_dentry = is_dentry;
    ch->record_size = is_dentry
        ? sizeof(struct vfs_ringbuf_dentry_rec)
        : sizeof(struct vfs_ringbuf_proc_rec);

    /* Map enough pages for header + all slots. The kernel rounds up
     * to page granularity; we map the exact logical size and rely on
     * the kernel's remap_pfn_range to provide the backing pages. */
    size_t header_size = sizeof(struct vfs_ringbuf_header);
    size_t data_size = (size_t)VFS_RINGBUF_CAPACITY * ch->record_size;
    ch->map_size = header_size + data_size;

    ch->mem = mmap(NULL, ch->map_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                   ch->fd, 0);
    if (ch->mem == MAP_FAILED) {
        g_warning("mmap(%s): %s", path, g_strerror(errno));
        ch->mem = NULL;
        close(ch->fd);
        ch->fd = -1;
        return FALSE;
    }

    ch->hdr   = (struct vfs_ringbuf_header *)ch->mem;
    ch->slots = (const char *)ch->mem + header_size;

    /* Sanity-check the header. */
    if (ch->hdr->magic != VFS_RINGBUF_MAGIC) {
        g_critical("mmap(%s): bad magic 0x%08x", path, ch->hdr->magic);
        munmap(ch->mem, ch->map_size);
        ch->mem = NULL;
        close(ch->fd);
        ch->fd = -1;
        return FALSE;
    }
    if (ch->hdr->record_size != ch->record_size) {
        g_critical("mmap(%s): record_size mismatch (kernel=%u, user=%zu)",
                   path, ch->hdr->record_size, ch->record_size);
        munmap(ch->mem, ch->map_size);
        ch->mem = NULL;
        close(ch->fd);
        ch->fd = -1;
        return FALSE;
    }

    ch->source_id = g_unix_fd_add(ch->fd,
                                  G_IO_IN | G_IO_HUP | G_IO_ERR,
                                  on_fd_ready, ch);
    g_message("mmap-ring: opened %s (fd=%d, map=%zu bytes, capacity=%u)",
              path, ch->fd, ch->map_size, ch->hdr->capacity);
    return TRUE;
}

static void ring_channel_close(RingChannel *ch)
{
    if (ch->source_id > 0) {
        g_source_remove(ch->source_id);
        ch->source_id = 0;
    }
    if (ch->mem) {
        munmap(ch->mem, ch->map_size);
        ch->mem = NULL;
        ch->hdr = NULL;
        ch->slots = NULL;
    }
    if (ch->fd >= 0) {
        close(ch->fd);
        ch->fd = -1;
    }
}

/* Public receiver handle — owns both channels. */
struct EpMmapRingReceiver {
    RingChannel dentry;
    RingChannel proc;
};

typedef struct EpMmapRingReceiver EpMmapRingReceiver;

EpMmapRingReceiver *ep_mmap_ring_receiver_new(const char *dentry_dev,
                                              const char *proc_dev,
                                              void (*on_event)(const EpEvent *, gpointer),
                                              gpointer user_data)
{
    EpMmapRingReceiver *r = g_new0(EpMmapRingReceiver, 1);
    r->dentry.on_event = on_event;
    r->dentry.user_data = user_data;
    r->proc.on_event = on_event;
    r->proc.user_data = user_data;

    gboolean ok = FALSE;
    if (ring_channel_open(&r->dentry,
                          dentry_dev ? dentry_dev : DEFAULT_DENTRY_DEV,
                          TRUE))
        ok = TRUE;
    else
        r->dentry.fd = -1;

    if (ring_channel_open(&r->proc,
                          proc_dev ? proc_dev : DEFAULT_PROC_DEV,
                          FALSE))
        ok = TRUE;
    else
        r->proc.fd = -1;

    if (!ok) {
        g_critical("No mmap-ring channels could be opened. "
                   "Is the vfs_monitor module loaded with transport=mmap "
                   "and no other consumer holding the device?");
        ring_channel_close(&r->dentry);
        ring_channel_close(&r->proc);
        g_free(r);
        return NULL;
    }

    /* Drain any events already in the rings at startup. */
    ring_channel_drain(&r->dentry);
    ring_channel_drain(&r->proc);
    return r;
}

void ep_mmap_ring_receiver_free(EpMmapRingReceiver *r)
{
    if (!r)
        return;
    if (r->dentry.count > 0)
        g_message("dentry events: %" G_GUINT64_FORMAT, r->dentry.count);
    if (r->proc.count > 0)
        g_message("proc events:   %" G_GUINT64_FORMAT, r->proc.count);
    ring_channel_close(&r->dentry);
    ring_channel_close(&r->proc);
    g_free(r);
}
