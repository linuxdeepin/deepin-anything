// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "eventsource_mmap.h"
#include "vfs_change_consts.h"
#include "vfs_ringbuf_uapi.h"
#include "logdefine.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

DAS_BEGIN_NAMESPACE

Q_LOGGING_CATEGORY(logM, "anything.normal.mmap", DEFAULT_MSG_TYPE)

#define DENTRY_DEV_PATH "/dev/vfs_monitor"

EventSource_MMAP::EventSource_MMAP()
{
    inited = false;

    fd = -1;
    mem = nullptr;
    map_size = 0;
    hdr = nullptr;
    ring_slots = nullptr;
    record_size = 0;
    capacity = 0;
    last_dropped = 0;

    buf[0] = 0;
    act = (unsigned char)-1;
    dst = nullptr;
}

EventSource_MMAP::~EventSource_MMAP()
{
    if (mem) {
        munmap(mem, map_size);
        mem = nullptr;
        hdr = nullptr;
        ring_slots = nullptr;
    }

    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

bool EventSource_MMAP::init()
{
    if (inited)
        return true;

    fd = open(DENTRY_DEV_PATH, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        nWarning("open %s fail: %s.", DENTRY_DEV_PATH, strerror(errno));
        return false;
    }

    record_size = sizeof(struct vfs_ringbuf_dentry_rec);
    size_t header_size = sizeof(struct vfs_ringbuf_header);
    map_size = header_size + (size_t)VFS_RINGBUF_CAPACITY * record_size;

    mem = mmap(nullptr, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED) {
        nWarning("mmap %s fail: %s.", DENTRY_DEV_PATH, strerror(errno));
        mem = nullptr;
        close(fd);
        fd = -1;
        return false;
    }

    hdr = static_cast<struct vfs_ringbuf_header *>(mem);
    ring_slots = static_cast<const char *>(mem) + header_size;
    capacity = VFS_RINGBUF_CAPACITY;

    if (hdr->magic != VFS_RINGBUF_MAGIC) {
        nWarning("mmap %s: bad magic 0x%08x.", DENTRY_DEV_PATH, hdr->magic);
        munmap(mem, map_size);
        mem = nullptr;
        hdr = nullptr;
        ring_slots = nullptr;
        close(fd);
        fd = -1;
        return false;
    }

    if (hdr->record_size != record_size) {
        nWarning("mmap %s: record_size mismatch (kernel=%u, user=%u).",
                 DENTRY_DEV_PATH, hdr->record_size, record_size);
        munmap(mem, map_size);
        mem = nullptr;
        hdr = nullptr;
        ring_slots = nullptr;
        close(fd);
        fd = -1;
        return false;
    }

    /* Trust the kernel-reported capacity (power of two). */
    capacity = hdr->capacity;
    last_dropped = hdr->dropped_count;

    inited = true;
    nInfo("mmap event source inited (fd=%d, map=%zu, capacity=%u).",
          fd, map_size, capacity);
    return true;
}

bool EventSource_MMAP::isInited()
{
    return inited;
}

bool EventSource_MMAP::getEvent(unsigned char *type, char **src, char **dst, bool *end)
{
    while (true) {
        switch (drainOne()) {
        case DrainGotEvent:
            *type = act;
            *src = buf;
            *dst = this->dst;
            *end = true;
            return true;
        case DrainSkipped:
            /* Record consumed but no deliverable event produced — the
             * ring may still hold more, loop and try the next one. */
            continue;
        case DrainEmpty:
            break; /* fall through to poll() */
        }

        /* Ring empty — block on poll() until the kernel wakes us. */
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int ret = poll(&pfd, 1, -1);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            nWarning("poll fail: %s.", strerror(errno));
            return false;
        }
        if (pfd.revents & (POLLHUP | POLLERR)) {
            nWarning("mmap channel closed (revents=0x%x).", pfd.revents);
            return false;
        }
        /* POLLIN: the kernel published new records — loop and drain. */
    }
}

EventSource_MMAP::DrainResult EventSource_MMAP::drainOne()
{
    __u64 head = __atomic_load_n(&hdr->producer_head, __ATOMIC_ACQUIRE);
    __u64 tail = __atomic_load_n(&hdr->consumer_tail, __ATOMIC_ACQUIRE);

    if (head == tail) {
        /* Report dropped events (newest, discarded while the ring was full). */
        __u64 dropped = __atomic_load_n(&hdr->dropped_count, __ATOMIC_ACQUIRE);
        if (dropped != last_dropped) {
            __u64 delta = dropped - last_dropped;
            last_dropped = dropped;
            nWarning("mmap-ring dropped %llu events (ring full).",
                     (unsigned long long)delta);
        }
        return DrainEmpty;
    }

    __u32 slot = (__u32)(tail & (capacity - 1));
    const struct vfs_ringbuf_dentry_rec *rec =
        reinterpret_cast<const struct vfs_ringbuf_dentry_rec *>(
            ring_slots + (size_t)slot * record_size);

    unsigned char _act = rec->action;
    unsigned int _cookie = rec->cookie;
    unsigned short major = rec->major;
    unsigned char minor = rec->minor;

    /* Copy the path out of the ring slot BEFORE advancing consumer_tail.
     * Once the tail is released the kernel producer may immediately
     * reclaim this slot and overwrite rec->path — any subsequent use of
     * a pointer into the slot (partitions check, rename stash, saveData)
     * would race with the producer and could observe a half-written path.
     * Scalar fields above are already safe: they were read before the
     * release and stored in locals. */
    char _path_buf[sizeof(rec->path)];
    __u32 _path_len = rec->path_len;
    if (_path_len >= sizeof(_path_buf))
        _path_len = sizeof(_path_buf) - 1;
    memcpy(_path_buf, rec->path, _path_len);
    _path_buf[_path_len] = '\0';
    char *_src = _path_buf;

    /* Advance the tail — the slot is now reclaimable by the producer. */
    __atomic_store_n(&hdr->consumer_tail, tail + 1, __ATOMIC_RELEASE);

    char *_root = nullptr;

    if (_act < ACT_MOUNT) {
        if (!partitions.contains(major, minor)) {
            nWarning("unknown device, %u, dev: %u:%u, path: %s, cookie: %u.",
                     _act, major, minor, _src, _cookie);
            return DrainSkipped;
        }
        _root = const_cast<char *>(partitions.rootFor(major, minor));
        if (strcmp(_root, "/") == 0)
            _root = nullptr;
    }

    char *_dst = nullptr;
    switch (_act) {
    case ACT_NEW_FILE:
    case ACT_NEW_SYMLINK:
    case ACT_NEW_LINK:
    case ACT_NEW_FOLDER:
    case ACT_DEL_FILE:
    case ACT_DEL_FOLDER:
        _dst = nullptr;
        break;
    case ACT_RENAME_FROM_FILE:
    case ACT_RENAME_FROM_FOLDER:
        rename_from.insert(_cookie, QByteArray(_src));
        return DrainSkipped;
    case ACT_RENAME_TO_FILE:
    case ACT_RENAME_TO_FOLDER:
        if (rename_from.contains(_cookie)) {
            _act = _act == ACT_RENAME_TO_FILE ? ACT_RENAME_FILE : ACT_RENAME_FOLDER;
            _dst = _src;
            _src = rename_from[_cookie].data();
        }
        break;
    case ACT_MOUNT:
    case ACT_UNMOUNT:
        partitions.updatePartitions();
        return DrainSkipped;
    case ACT_RENAME_FILE:
    case ACT_RENAME_FOLDER:
        nWarning("not support file action: %d.", int(_act));
        return DrainSkipped;
    default:
        nWarning("unknow file action: %d.", int(_act));
        return DrainSkipped;
    }

    bool ok = saveData(_act, _root, _src, _dst);

    if (_act == ACT_RENAME_FILE || _act == ACT_RENAME_FOLDER)
        rename_from.remove(_cookie);

    return ok ? DrainGotEvent : DrainSkipped;
}

bool EventSource_MMAP::saveData(unsigned char _act, char *_root, char *_src, char *_dst)
{
    size_t root_size = _root ? strlen(_root) : 0;
    size_t src_size = strlen(_src);

    if (_dst) {
        size_t dst_size = strlen(_dst);
        if (root_size * 2 + src_size + dst_size + 2 > sizeof(buf)) {
            nCritical("the msg buf is too small to cache msg.");
            return false;
        }
    } else {
        if (root_size + src_size + 1 > sizeof(buf)) {
            nCritical("the msg buf is too small to cache msg.");
            return false;
        }
    }

    act = _act;

    /* save src */
    if (_root)
        strcpy(buf, _root);
    strcpy(buf + root_size, _src);

    /* save dst */
    if (_dst) {
        dst = buf + root_size + src_size + 1;
        if (_root)
            strcpy(dst, _root);
        strcpy(dst + root_size, _dst);
    } else {
        dst = nullptr;
    }

    return true;
}

DAS_END_NAMESPACE
