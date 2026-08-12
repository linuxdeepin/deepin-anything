// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VFS_RINGBUF_UAPI_H
#define VFS_RINGBUF_UAPI_H

#include <linux/types.h>

#define VFS_RINGBUF_MAGIC   0x52425546  /* "RBUF" */
#define VFS_RINGBUF_VERSION 1

/*
 * Capacity must be a power of two. Each record embeds a PATH_MAX path,
 * so the total ring size is capacity * record_size.
 */
#define VFS_RINGBUF_CAPACITY 1024

/* Channel identifiers (used as array indices; do not reorder) */
enum vfs_ringbuf_channel {
    VFS_RINGBUF_CH_DENTRY = 0,
    VFS_RINGBUF_CH_PROC   = 1,
    VFS_RINGBUF_CH_MAX,
};

/*
 * Shared header — lives at the start of the mmap region.
 * All fields are 64-bit aligned.
 *
 * SPSC contract: the ring is Single-Producer / Single-Consumer.
 * The kernel is the sole producer; the process that opened the
 * device is the sole consumer. consumer_tail MUST be advanced by
 * exactly one consumer (via smp_store_release). To enforce this,
 * the device refuses a second open() with -EBUSY — only one fd per
 * channel may be open at a time. read() and mmap() are mutually
 * exclusive on the same channel for the same reason.
 */
struct vfs_ringbuf_header {
    __u32 magic;
    __u32 version;
    __u32 capacity;
    __u32 record_size;
    __u64 producer_head;
    __u64 consumer_tail;
    __u64 dropped_count;
    __u8  pad[4056]; /* pad to one page (4096) */
};

/*
 * Dentry event record — carries the same fields as the genl
 * VFSMONITOR_C_NOTIFY message.
 */
struct vfs_ringbuf_dentry_rec {
    __u8  action;
    __u8  minor;
    __u16 major;
    __u32 cookie;
    __u32 seq;
    __u32 path_len;
    char  path[4096]; /* PATH_MAX, NUL-terminated */
};

/*
 * Process info record — carries the same fields as the genl
 * VFSMONITOR_C_NOTIFY_PROCESS_INFO message.
 */
struct vfs_ringbuf_proc_rec {
    __u32 uid;
    __s32 tgid;
    __u32 seq;
    __u32 path_len;
    char  path[4096]; /* PATH_MAX, NUL-terminated */
};

#endif /* VFS_RINGBUF_UAPI_H */
