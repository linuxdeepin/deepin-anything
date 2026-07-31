// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "event-listener-backend.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <glib-unix.h>

#include "vfs_ringbuf_uapi.h"

#define DENTRY_DEV_PATH "/dev/vfs_monitor"

typedef struct {
    ServerEventListenerBackend parent;

    int                       fd;
    guint                     source_id;
    void                     *mem;
    size_t                    map_size;
    struct vfs_ringbuf_header *hdr;
    const char               *slots;
    guint32                   record_size;
    guint32                   capacity;
    __u64                     last_dropped;

    GMainContext             *context;
    GMainLoop                *loop;
    GThread                  *thread;

    FileEventHandler          handler;
    gpointer                  user_data;
} MmapRingBackend;

static void ring_rec_to_fs_event(const struct vfs_ringbuf_dentry_rec *rec,
                                  fs_event *event)
{
    event->act = rec->action;
    event->cookie = rec->cookie;
    event->seq = rec->seq;
    event->major = rec->major;
    event->minor = rec->minor;

    size_t copy_len = rec->path_len;
    if (copy_len >= sizeof(event->src))
        copy_len = sizeof(event->src) - 1;
    memcpy(event->src, rec->path, copy_len);
    event->src[copy_len] = '\0';
    event->dst[0] = '\0';
}

static void ring_drain(MmapRingBackend *backend)
{
    /* Consume the records already in the ring first — these are older
     * than any drops. Dropped events are the newest (producer discarded
     * them when the ring was full), so report after consuming. */
    for (;;) {
        __u64 head = __atomic_load_n(&backend->hdr->producer_head,
                                      __ATOMIC_ACQUIRE);
        __u64 tail = __atomic_load_n(&backend->hdr->consumer_tail,
                                      __ATOMIC_ACQUIRE);

        if (head == tail)
            break;

        __u32 slot = (__u32)(tail & (backend->capacity - 1));
        const struct vfs_ringbuf_dentry_rec *rec =
            (const struct vfs_ringbuf_dentry_rec *)
            (backend->slots + (size_t)slot * backend->record_size);

        fs_event *event = g_slice_new0(fs_event);
        if (event) {
            ring_rec_to_fs_event(rec, event);

            if (backend->handler)
                backend->handler(backend->user_data, event);
            else
                g_slice_free(fs_event, event);
        }

        __atomic_store_n(&backend->hdr->consumer_tail, tail + 1,
                         __ATOMIC_RELEASE);
    }

    /* Report dropped events — these are the newest, discarded by the
     * producer after the records we just consumed. */
    __u64 dropped = __atomic_load_n(&backend->hdr->dropped_count,
                                     __ATOMIC_ACQUIRE);
    if (dropped != backend->last_dropped) {
        __u64 delta = dropped - backend->last_dropped;
        backend->last_dropped = dropped;
        g_warning("mmap-ring: dropped %llu events (ring full)",
                  (unsigned long long)delta);
    }
}

static gboolean on_ring_fd_ready(G_GNUC_UNUSED gint fd,
                                 G_GNUC_UNUSED GIOCondition condition,
                                 gpointer data)
{
    MmapRingBackend *backend = (MmapRingBackend *)data;

    if ((condition & (G_IO_HUP | G_IO_ERR)) != 0) {
        g_message("mmap-ring channel closed (fd=%d)", fd);
        backend->source_id = 0;
        return G_SOURCE_REMOVE;
    }

    ring_drain(backend);
    return G_SOURCE_CONTINUE;
}

static gpointer mmap_ring_thread_func(gpointer data)
{
    MmapRingBackend *backend = (MmapRingBackend *)data;

    backend->context = g_main_context_new();
    backend->loop = g_main_loop_new(backend->context, FALSE);
    g_main_context_push_thread_default(backend->context);

    backend->source_id = g_unix_fd_add(backend->fd,
                                       G_IO_IN | G_IO_HUP | G_IO_ERR,
                                       on_ring_fd_ready, backend);

    g_message("Mmap-ring event listener thread started");
    g_main_loop_run(backend->loop);

    if (backend->source_id > 0) {
        g_source_remove(backend->source_id);
        backend->source_id = 0;
    }

    g_main_context_pop_thread_default(backend->context);
    g_main_context_unref(backend->context);
    g_main_loop_unref(backend->loop);
    backend->context = NULL;
    backend->loop = NULL;

    g_message("Mmap-ring event listener thread stopped");
    return NULL;
}

static gboolean mmap_ring_backend_start(ServerEventListenerBackend *self)
{
    MmapRingBackend *backend = (MmapRingBackend *)self;

    backend->thread = g_thread_new("mmap_listener",
                                    mmap_ring_thread_func, backend);
    if (!backend->thread) {
        g_critical("Failed to create mmap-ring listener thread");
        return FALSE;
    }
    return TRUE;
}

static void mmap_ring_backend_stop(ServerEventListenerBackend *self)
{
    MmapRingBackend *backend = (MmapRingBackend *)self;

    if (backend->loop)
        g_main_loop_quit(backend->loop);

    if (backend->thread) {
        g_thread_join(backend->thread);
        backend->thread = NULL;
    }
}

static void mmap_ring_backend_free(ServerEventListenerBackend *self)
{
    MmapRingBackend *backend = (MmapRingBackend *)self;

    if (backend->mem) {
        munmap(backend->mem, backend->map_size);
        backend->mem = NULL;
        backend->hdr = NULL;
        backend->slots = NULL;
    }

    if (backend->fd >= 0) {
        close(backend->fd);
        backend->fd = -1;
    }

    g_free(backend);
}

ServerEventListenerBackend *mmap_ring_backend_new(
    FileEventHandler handler, gpointer user_data)
{
    MmapRingBackend *backend = g_new0(MmapRingBackend, 1);
    backend->handler = handler;
    backend->user_data = user_data;
    backend->fd = -1;

    backend->parent.start = mmap_ring_backend_start;
    backend->parent.stop = mmap_ring_backend_stop;
    backend->parent.free = mmap_ring_backend_free;

    backend->fd = open(DENTRY_DEV_PATH, O_RDWR | O_NONBLOCK);
    if (backend->fd < 0) {
        g_critical("Failed to open %s: %s", DENTRY_DEV_PATH,
                   g_strerror(errno));
        g_free(backend);
        return NULL;
    }

    backend->record_size = sizeof(struct vfs_ringbuf_dentry_rec);
    size_t header_size = sizeof(struct vfs_ringbuf_header);
    backend->map_size = header_size +
                        (size_t)VFS_RINGBUF_CAPACITY * backend->record_size;

    backend->mem = mmap(NULL, backend->map_size,
                        PROT_READ | PROT_WRITE, MAP_SHARED,
                        backend->fd, 0);
    if (backend->mem == MAP_FAILED) {
        g_critical("mmap(%s): %s", DENTRY_DEV_PATH, g_strerror(errno));
        backend->mem = NULL;
        close(backend->fd);
        backend->fd = -1;
        g_free(backend);
        return NULL;
    }

    backend->hdr = (struct vfs_ringbuf_header *)backend->mem;
    backend->slots = (const char *)backend->mem + header_size;
    backend->capacity = VFS_RINGBUF_CAPACITY;

    if (backend->hdr->magic != VFS_RINGBUF_MAGIC) {
        g_critical("mmap(%s): bad magic 0x%08x", DENTRY_DEV_PATH,
                   backend->hdr->magic);
        munmap(backend->mem, backend->map_size);
        backend->mem = NULL;
        close(backend->fd);
        backend->fd = -1;
        g_free(backend);
        return NULL;
    }

    if (backend->hdr->record_size != backend->record_size) {
        g_critical("mmap(%s): record_size mismatch (kernel=%u, user=%u)",
                   DENTRY_DEV_PATH, backend->hdr->record_size,
                   backend->record_size);
        munmap(backend->mem, backend->map_size);
        backend->mem = NULL;
        close(backend->fd);
        backend->fd = -1;
        g_free(backend);
        return NULL;
    }

    /* Use the kernel-reported capacity/mask in case it differs. */
    backend->capacity = backend->hdr->capacity;
    backend->last_dropped = __atomic_load_n(&backend->hdr->dropped_count,
                                             __ATOMIC_ACQUIRE);

    g_message("Mmap-ring event listener created (fd=%d, map=%zu, capacity=%u)",
              backend->fd, backend->map_size, backend->capacity);
    return (ServerEventListenerBackend *)backend;
}
