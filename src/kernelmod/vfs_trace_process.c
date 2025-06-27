// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/cred.h>
#include <linux/slab.h>
#include <linux/file.h>
#include "vfs_trace_process.h"
#include "event.h"
#include "vfs_change_consts.h"

#include <linux/version.h>
// For kernel >= 5.8, mmap_lock APIs are in <linux/mmap_lock.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
#include <linux/mmap_lock.h>
#endif

extern unsigned int trace_event_mask;

static int (*vfs_changed_entry)(struct vfs_event *event);

/**
 * @brief get_task_exe_file_for_module - Safely get a task's executable file from a module.
 *
 * This function provides a compatible and robust way for kernel modules to get the
 * executable file (`struct file`) of a task. It is designed to work across a
 * wide range of kernel versions by handling the change of the memory map locking
 * mechanism that occurred in kernel v5.8.
 *
 * In kernels < 5.8, it uses `down_read(&mm->mmap_sem)`.
 * In kernels >= 5.8, it uses `mmap_read_lock(mm)`.
 *
 * @param task The task to query.
 * @return A `struct file` pointer with its reference count incremented, or NULL on failure.
 *         The caller is RESPONSIBLE for calling fput() on the returned struct file
 *         to release the reference.
 */
static struct file *get_task_exe_file_for_module(struct task_struct *task)
{
    struct file *exe_file = NULL;
    struct mm_struct *mm;

    // 1. Safely get the process's memory descriptor (mm_struct).
    // get_task_mm() increments the mm's reference count.
    mm = get_task_mm(task);
    if (!mm) {
        return NULL;
    }

    // 2. Acquire the read lock for the memory map.
    // THIS IS THE VERSION COMPATIBILITY HANDLING BLOCK.
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
    // Newer kernels (>= 5.8) use the specialized mmap_lock API.
    mmap_read_lock(mm);
#else
    // Older kernels (< 5.8) use the generic rw_semaphore on mmap_sem.
    down_read(&mm->mmap_sem);
#endif

    if (mm->exe_file) {
        // 3. Increment the file's reference count because we are returning it.
        exe_file = get_file(mm->exe_file);
    }

    // 4. Release the read lock.
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
    mmap_read_unlock(mm);
#else
    up_read(&mm->mmap_sem);
#endif

    // 5. Decrement the mm's reference count, paired with get_task_mm().
    mmput(mm);

    return exe_file;
}

static int do_trace_process(struct vfs_event *event)
{
    struct file *exe_file;

    if (trace_event_mask & (1 << event->action))
    {
        /* event->proc_info will be freed together with event  */
        if (vfs_event_alloc_proc_info_atomic(event))
            goto quit;

        /* set tgid to 0 to indicate that the proc_info is invalid */
        event->proc_info->tgid = 0;

        exe_file = get_task_exe_file_for_module(current);
        if (NULL == exe_file)
            goto quit;

        event->proc_info->path = file_path(exe_file, event->proc_info->buf, sizeof(event->proc_info->buf));
        if (!IS_ERR(event->proc_info->path)) {
            event->proc_info->uid = from_kuid(&init_user_ns, task_uid(current));
            event->proc_info->tgid = current->tgid;
        }

        fput(exe_file);
    }

quit:
    return vfs_changed_entry(event);
}

void *vfs_get_trace_process_entry(void *vfs_changed_func)
{
    vfs_changed_entry = vfs_changed_func;

    return do_trace_process;
}
