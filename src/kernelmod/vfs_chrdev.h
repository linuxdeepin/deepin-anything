// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VFS_CHRDEV_H
#define VFS_CHRDEV_H

#include "event.h"

/**
 * init_vfs_chrdev - register the char devices and ring buffers.
 * Returns 0 on success, negative errno on failure.
 */
int init_vfs_chrdev(void);

/**
 * cleanup_vfs_chrdev - unregister devices and free ring memory.
 */
void cleanup_vfs_chrdev(void);

/**
 * vfs_chrdev_notify_event - write event to the mmap ring buffers.
 *
 * Pure mmap transport. Drops the event when the ring is full; no
 * fallback. Returns 0 on success, non-zero on drop.
 */
int vfs_chrdev_notify_event(struct vfs_event *event);

#endif /* VFS_CHRDEV_H */
