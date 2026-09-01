// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/version.h>
#include <linux/kdev_t.h>

#include "vfs_chrdev.h"
#include "vfs_ringbuf_uapi.h"
#include "event.h"


/* Rate-limited variants for hot paths (ring full, read again, etc.) */
#define mpr_info_ratelimited(fmt, ...) \
    printk_ratelimited(KERN_INFO "vfs_monitor: " fmt, ##__VA_ARGS__)

#define mpr_err_ratelimited(fmt, ...) \
    printk_ratelimited(KERN_ERR "vfs_monitor: " fmt, ##__VA_ARGS__)

/* Pure mmap transport — no genl fallback. Drops when ring is full. */

/*
 * Per-channel ring state.
 *
 * The ring memory is a single contiguous, page-aligned allocation:
 *   [ struct vfs_ringbuf_header | slot[0] ... slot[capacity-1] ]
 *
 * The kernel is the sole producer; the process that opened the device is
 * the sole consumer. The producer side is serialized by @producer_lock
 * (MPSC), while the consumer side remains single-consumer and lock-free.
 */
struct vfs_ring {
    void           *mem;          /* base of the whole allocation */
    size_t          mem_size;     /* total allocation size */
    void           *data;         /* start of slot area (after header) */
    u32             capacity;
    u32             mask;         /* capacity - 1 */
    u32             record_size;
    atomic_t        openers;      /* number of processes with the device open */
    spinlock_t      producer_lock; /* serializes concurrent producers */
    wait_queue_head_t wq;
};

static struct vfs_ring rings[VFS_RINGBUF_CH_MAX];
struct miscdevice vfs_chrdev_devs[VFS_RINGBUF_CH_MAX];

/* ------------------------------------------------------------------ */
/* Ring buffer init / cleanup                                          */
/* ------------------------------------------------------------------ */

static int vfs_ring_init(struct vfs_ring *ring, u32 capacity, u32 record_size)
{
    struct vfs_ringbuf_header *hdr;
    size_t header_size;
    size_t data_size;
    size_t alloc_size;

    header_size = sizeof(struct vfs_ringbuf_header);
    data_size = (size_t)capacity * record_size;

    /* Align the allocation to a whole number of pages up front. On
     * kernels whose PAGE_SIZE != 4096 (e.g. Loongson 16 KiB, arm64
     * 64 KiB) the raw header+data total is not a page multiple, so
     * the consumer's mmap() — which the kernel page-aligns — would
     * be larger than the recorded size and get rejected with
     * "size > mem_size". Page-aligning here keeps the allocation
     * request, the recorded mem_size, and the consumer's mapping
     * all in sync. */
    alloc_size = PAGE_ALIGN(header_size + data_size);

    /* vmalloc gives virtually-contiguous memory without MAX_ORDER
     * limits; physical pages are discontiguous, which is fine for
     * this SPSC ring (single-record access, no DMA). */
    ring->mem = vzalloc(alloc_size);
    if (!ring->mem) {
        mpr_err("ring_init: vzalloc fail, alloc_size=%zu (capacity=%u, record_size=%u)\n",
                alloc_size, capacity, record_size);
        return -ENOMEM;
    }

    ring->mem_size = alloc_size;

    hdr = (struct vfs_ringbuf_header *)ring->mem;
    hdr->magic       = VFS_RINGBUF_MAGIC;
    hdr->version     = VFS_RINGBUF_VERSION;
    hdr->capacity    = capacity;
    hdr->record_size = record_size;
    hdr->producer_head  = 0;
    hdr->consumer_tail  = 0;
    hdr->dropped_count  = 0;

    ring->data        = (char *)ring->mem + header_size;
    ring->capacity     = capacity;
    ring->mask         = capacity - 1;
    ring->record_size  = record_size;
    atomic_set(&ring->openers, 0);
    spin_lock_init(&ring->producer_lock);
    init_waitqueue_head(&ring->wq);

    return 0;
}

static void vfs_ring_cleanup(struct vfs_ring *ring)
{
    if (ring->mem) {
        vfree(ring->mem);
        ring->mem = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Producer-serialized SPSC produce                                   */
/* ------------------------------------------------------------------ */

/*
 * Called from the pipeline tail. May be in atomic context (kretprobe
 * return handler, timer softirq, or work queue), so no sleeping
 * allocations. wake_up_interruptible is safe from atomic.
 *
 * The caller MUST hold @ring->producer_lock.
 *
 * Returns a pointer to the next slot for the caller to fill, or NULL
 * when the ring is full (dropped).
 */
static void *vfs_ring_produce_slot(struct vfs_ring *ring)
{
    struct vfs_ringbuf_header *hdr = ring->mem;
    u64 head, tail;
    u32 slot;

    head = smp_load_acquire(&hdr->producer_head);
    tail = smp_load_acquire(&hdr->consumer_tail);

    if (head - tail >= ring->capacity) {
        /* Ring full — drop */
        hdr->dropped_count++;
        return NULL;
    }

    slot = head & ring->mask;
    return (char *)ring->data + (size_t)slot * ring->record_size;
}

/*
 * Publish the produced slot and wake the consumer.
 * The caller MUST hold @ring->producer_lock.
 */
static void vfs_ring_commit(struct vfs_ring *ring)
{
    struct vfs_ringbuf_header *hdr = ring->mem;
    u64 head = smp_load_acquire(&hdr->producer_head);

    smp_store_release(&hdr->producer_head, head + 1);
    wake_up_interruptible(&ring->wq);
}

int vfs_chrdev_notify_event(struct vfs_event *event)
{
    struct vfs_ringbuf_dentry_rec *drec;
    struct vfs_ringbuf_proc_rec *prec;
    unsigned long flags;
    u32 path_len;

    /* No consumer — discard the event without touching the ring. */
    if (atomic_read(&rings[VFS_RINGBUF_CH_DENTRY].openers) == 0)
        return 0;

    /* Produce dentry record directly into the ring slot */
    spin_lock_irqsave(&rings[VFS_RINGBUF_CH_DENTRY].producer_lock, flags);
    drec = vfs_ring_produce_slot(&rings[VFS_RINGBUF_CH_DENTRY]);
    if (drec) {
        drec->action   = event->action;
        drec->minor    = MINOR(event->dev);
        drec->major    = MAJOR(event->dev);
        drec->cookie   = event->cookie;
        drec->seq     = event->seq;
        path_len = strlen(event->path);
        if (path_len >= sizeof(drec->path))
            path_len = sizeof(drec->path) - 1;
        drec->path_len = path_len;
        memcpy(drec->path, event->path, path_len);
        drec->path[path_len] = '\0';
        vfs_ring_commit(&rings[VFS_RINGBUF_CH_DENTRY]);
    } else {
        mpr_err_ratelimited("dentry channel drop: seq=%u, path=%s\n",
                            event->seq, event->path);
    }
    spin_unlock_irqrestore(&rings[VFS_RINGBUF_CH_DENTRY].producer_lock, flags);

    /* Produce proc record if process info is present */
    if (event->proc_info && event->proc_info->tgid != 0) {
        /* No consumer on the proc channel — skip it too. */
        if (atomic_read(&rings[VFS_RINGBUF_CH_PROC].openers) == 0)
            goto out;

        spin_lock_irqsave(&rings[VFS_RINGBUF_CH_PROC].producer_lock, flags);
        prec = vfs_ring_produce_slot(&rings[VFS_RINGBUF_CH_PROC]);
        if (prec) {
            prec->uid      = event->proc_info->uid;
            prec->tgid     = event->proc_info->tgid;
            prec->seq     = event->seq;
            path_len = strlen(event->proc_info->path);
            if (path_len >= sizeof(prec->path))
                path_len = sizeof(prec->path) - 1;
            prec->path_len = path_len;
            memcpy(prec->path, event->proc_info->path, path_len);
            prec->path[path_len] = '\0';
            vfs_ring_commit(&rings[VFS_RINGBUF_CH_PROC]);
        } else {
            mpr_err_ratelimited("proc channel drop: seq=%u, tgid=%d, path=%s\n",
                                event->seq, event->proc_info->tgid,
                                event->proc_info->path);
        }
        spin_unlock_irqrestore(&rings[VFS_RINGBUF_CH_PROC].producer_lock, flags);
    }

out:
    /* Drops stay in the ring's dropped_count; no genl fallback. */
    return 0;
}
/* ------------------------------------------------------------------ */
/* File operations                                                     */
/* ------------------------------------------------------------------ */

static int vfs_chrdev_open(struct inode *inode, struct file *file)
{
    struct miscdevice *mdev = file->private_data;
    struct vfs_ringbuf_header *hdr;
    unsigned long flags;
    int i;

    for (i = 0; i < VFS_RINGBUF_CH_MAX; i++) {
        if (&vfs_chrdev_devs[i] == mdev) {
            int old = atomic_cmpxchg(&rings[i].openers, 0, 1);
            if (old != 0) {
                mpr_info("channel %d already opened by another process\n", i);
                return -EBUSY;
            }

            /* Reset ring state so the new consumer starts fresh, without
             * stale events produced before it opened. Slot data is left
             * untouched — the producer overwrites it on the next produce.
             * Serialize against concurrent producers to avoid resetting
             * a slot that is mid-fill. */
            spin_lock_irqsave(&rings[i].producer_lock, flags);
            hdr = rings[i].mem;
            smp_store_release(&hdr->producer_head, 0);
            smp_store_release(&hdr->consumer_tail, 0);
            smp_store_release(&hdr->dropped_count, 0);
            spin_unlock_irqrestore(&rings[i].producer_lock, flags);

            file->private_data = &rings[i];
            return 0;
        }
    }
    mpr_err("open: no matching device for mdev=%px\n", mdev);
    return -ENODEV;
}

static int vfs_chrdev_release(struct inode *inode, struct file *file)
{
    struct vfs_ring *ring = file->private_data;

    if (ring)
        atomic_dec(&ring->openers);

    return 0;
}

static int vfs_chrdev_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct vfs_ring *ring = file->private_data;
    unsigned long size, page_count, i;
    int ret;

    if (!ring || !ring->mem) {
        mpr_err("mmap: invalid ring state (ring=%px)\n", ring);
        return -EINVAL;
    }

    size = vma->vm_end - vma->vm_start;
    if (size > ring->mem_size) {
        mpr_err("mmap: size %lu > mem_size %zu\n", size, ring->mem_size);
        return -EINVAL;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
    vm_flags_set(vma, VM_DONTEXPAND | VM_DONTCOPY);
#else
    vma->vm_flags |= VM_DONTEXPAND | VM_DONTCOPY;
#endif

    /* Map each vmalloc page individually. remap_vmalloc_range uses
     * vm_insert_page internally, which rejects VM_SHARED vmas on
     * kernels < 5.x (returns -EINVAL). remap_pfn_range works on all
     * versions. */
    page_count = size >> PAGE_SHIFT;
    for (i = 0; i < page_count; i++) {
        struct page *page = vmalloc_to_page(
            (char *)ring->mem + i * PAGE_SIZE);
        if (!page) {
            mpr_err("mmap: vmalloc_to_page fail at page %lu\n", i);
            return -ENOMEM;
        }
        ret = remap_pfn_range(vma,
                              vma->vm_start + i * PAGE_SIZE,
                              page_to_pfn(page),
                              PAGE_SIZE,
                              vma->vm_page_prot);
        if (ret) {
            mpr_err("mmap: remap_pfn_range fail at page %lu: %d\n", i, ret);
            return ret;
        }
    }

    return 0;
}

static __poll_t vfs_chrdev_poll(struct file *file, poll_table *wait)
{
    struct vfs_ring *ring = file->private_data;
    struct vfs_ringbuf_header *hdr;

    if (!ring)
        return 0;

    poll_wait(file, &ring->wq, wait);

    hdr = ring->mem;
    if (smp_load_acquire(&hdr->producer_head) !=
        smp_load_acquire(&hdr->consumer_tail))
        return POLLIN | POLLRDNORM;

    return 0;
}

static ssize_t vfs_chrdev_read(struct file *file, char __user *buf,
                              size_t count, loff_t *ppos)
{
    struct vfs_ring *ring = file->private_data;
    struct vfs_ringbuf_header *hdr;
    u64 head, tail;
    u32 slot;
    void *rec;

    if (!ring) {
        mpr_err("read: ring is NULL\n");
        return -EINVAL;
    }

    if (count < ring->record_size) {
        mpr_err_ratelimited("read: count %zu < record_size %u\n",
                            count, ring->record_size);
        return -EINVAL;
    }

    hdr = ring->mem;
    head = smp_load_acquire(&hdr->producer_head);
    tail = smp_load_acquire(&hdr->consumer_tail);

    if (head == tail) {
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;
        if (wait_event_interruptible(ring->wq,
                smp_load_acquire(&hdr->producer_head) !=
                smp_load_acquire(&hdr->consumer_tail)))
            return -ERESTARTSYS;
        head = smp_load_acquire(&hdr->producer_head);
        tail = smp_load_acquire(&hdr->consumer_tail);
    }

    slot = tail & ring->mask;
    rec = (char *)ring->data + (size_t)slot * ring->record_size;

    if (copy_to_user(buf, rec, ring->record_size)) {
        mpr_err_ratelimited("read: copy_to_user fail, seq tail=%llu\n", tail);
        return -EFAULT;
    }

    smp_store_release(&hdr->consumer_tail, tail + 1);

    return ring->record_size;
}

static const struct file_operations vfs_chrdev_fops = {
    .owner   = THIS_MODULE,
    .open    = vfs_chrdev_open,
    .release = vfs_chrdev_release,
    .mmap    = vfs_chrdev_mmap,
    .poll    = vfs_chrdev_poll,
    .read    = vfs_chrdev_read,
};

/* ------------------------------------------------------------------ */
/* Device table                                                        */
/* ------------------------------------------------------------------ */

struct miscdevice vfs_chrdev_devs[VFS_RINGBUF_CH_MAX] = {
    [VFS_RINGBUF_CH_DENTRY] = {
        .minor = MISC_DYNAMIC_MINOR,
        .name  = "vfs_monitor",
        .fops  = &vfs_chrdev_fops,
        .mode  = 0660,
    },
    [VFS_RINGBUF_CH_PROC] = {
        .minor = MISC_DYNAMIC_MINOR,
        .name  = "vfs_monitor_proc",
        .fops  = &vfs_chrdev_fops,
        .mode  = 0660,
    },
};

/* ------------------------------------------------------------------ */
/* Init / cleanup                                                      */
/* ------------------------------------------------------------------ */

int init_vfs_chrdev(void)
{
    int ret;
    int i;


    ret = vfs_ring_init(&rings[VFS_RINGBUF_CH_DENTRY],
                        VFS_RINGBUF_CAPACITY,
                        sizeof(struct vfs_ringbuf_dentry_rec));
    if (ret) {
        mpr_err("init_vfs_chrdev: dentry ring init fail: %d\n", ret);
        goto fail_dentry_ring;
    }

    ret = vfs_ring_init(&rings[VFS_RINGBUF_CH_PROC],
                        VFS_RINGBUF_CAPACITY,
                        sizeof(struct vfs_ringbuf_proc_rec));
    if (ret) {
        mpr_err("init_vfs_chrdev: proc ring init fail: %d\n", ret);
        goto fail_proc_ring;
    }

    for (i = 0; i < VFS_RINGBUF_CH_MAX; i++) {
        ret = misc_register(&vfs_chrdev_devs[i]);
        if (ret) {
            mpr_err("init_vfs_chrdev: misc_register[%d] fail: %d\n", i, ret);
            goto fail_misc;
        }
    }

    mpr_info("init_vfs_chrdev: /dev/vfs_monitor, /dev/vfs_monitor_proc ok\n");
    return 0;

fail_misc:
    for (i--; i >= 0; i--)
        misc_deregister(&vfs_chrdev_devs[i]);
    vfs_ring_cleanup(&rings[VFS_RINGBUF_CH_PROC]);
fail_proc_ring:
    vfs_ring_cleanup(&rings[VFS_RINGBUF_CH_DENTRY]);
fail_dentry_ring:
    return ret;
}

void cleanup_vfs_chrdev(void)
{
    int i;

    for (i = 0; i < VFS_RINGBUF_CH_MAX; i++) {
        /* Wake any sleeping readers so they can observe unload */
        wake_up_interruptible_all(&rings[i].wq);
        misc_deregister(&vfs_chrdev_devs[i]);
    }

    vfs_ring_cleanup(&rings[VFS_RINGBUF_CH_PROC]);
    vfs_ring_cleanup(&rings[VFS_RINGBUF_CH_DENTRY]);

    mpr_info("cleanup_vfs_chrdev ok\n");
}
