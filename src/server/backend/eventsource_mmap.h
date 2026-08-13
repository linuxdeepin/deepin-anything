// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef EVENTSOURCE_MMAP_H
#define EVENTSOURCE_MMAP_H

#include <QMap>
#include <QByteArray>
#include "dasdefine.h"
#include "eventsource.h"
#include "partitionmonitor.h"

struct vfs_ringbuf_header;
struct vfs_ringbuf_dentry_rec;

DAS_BEGIN_NAMESPACE

/*
 * EventSource_MMAP — mmap-ring based VFS event source.
 *
 * Functionally equivalent to EventSource_GENL: it consumes VFS events
 * produced by the kernel module through a shared, lock-free ring buffer
 * exposed via /dev/vfs_monitor. The ring layout and SPSC contract are
 * defined in vfs_ringbuf_uapi.h.
 *
 * Unlike the standalone C backend (event-listener-mmap-ring.c) which
 * spawns a dedicated thread driven by a GLib main loop, this class
 * implements the synchronous EventSource interface: getEvent() blocks
 * on poll() and drains records one at a time until a deliverable event
 * is produced. No extra thread, no GLib event loop, no g_unix_fd_add.
 */
class EventSource_MMAP : public EventSource
{
public:
    EventSource_MMAP();
    ~EventSource_MMAP() override;

    bool init() override;
    bool isInited() override;
    bool getEvent(unsigned char *type, char **src, char **dst, bool *end) override;

private:
    /* Outcome of draining a single record from the ring. */
    enum DrainResult {
        DrainGotEvent,   /* a deliverable event is ready in buf/act/dst */
        DrainSkipped,     /* record consumed but no event produced
                           * (mount/unmount, rename_from stash, …) —
                           * the ring may still hold more, try again. */
        DrainEmpty,       /* ring is empty — block on poll(). */
    };

    /* Drain one record from the ring and apply the same post-processing
     * (partition root prefix, rename_from matching, mount/unmount
     * handling) as EventSource_GENL::handleMsg. */
    DrainResult drainOne();

    bool saveData(unsigned char _act, char *_root, char *_src, char *_dst);

private:
    bool inited;

    int fd;
    void *mem;
    size_t map_size;
    struct vfs_ringbuf_header *hdr;
    const char *ring_slots;
    quint32 record_size;
    quint32 capacity;
    quint64 last_dropped;

    PartitionMonitor partitions;
    QMap<unsigned int, QByteArray> rename_from;

    char buf[4096 * 2];
    unsigned char act;
    char *dst;
};

DAS_END_NAMESPACE

#endif // EVENTSOURCE_MMAP_H
