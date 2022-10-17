// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vfs_change.h"

#include <linux/errno.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/spinlock_types.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#include "vfs_change_uapi.h"
#include "vfs_log.h"

#if 0
#define trace_change mpr_info
#else
#define trace_change(...)
#endif
#if 0
#define trace_change_node mpr_info
#else
#define trace_change_node(...)
#endif

static int hour_shift = 8;
module_param(hour_shift, int, 0644);
MODULE_PARM_DESC(hour_shift,
                 "hour shift for displaying /proc/vfs_changes line item time");

static int merge_actions = 1;
module_param(merge_actions, int, 0644);
MODULE_PARM_DESC(merge_actions,
                 "merge actions to minimize data transfer and fs tree change");

struct krp_change {
  struct list_head node;
  struct list_head type_node;

  unsigned long timestamp;

  unsigned char action;
  unsigned char padding;
  unsigned short size;

  unsigned short src_len;
  unsigned short dst_len;

  char src[0];
};

static inline char *_krp_change_dst(struct krp_change *change) {
  return change->src + change->src_len + 1;
}

static inline struct krp_change *_krp_change_new(int act, const char *root,
                                                 const char *src,
                                                 const char *dst) {
  size_t root_len = root ? strlen(root) : 0;
  size_t src_len = strlen(src) + root_len;
  size_t dst_len = dst ? strlen(dst) + root_len : 0;
  //            |struct krp_change          |src          |dst
  size_t size = sizeof(struct krp_change) + src_len + 1 + dst_len + 1;
  struct krp_change *change;

  if (unlikely(size > 65535)) {
    mpr_err("path overflow %zd %zd", src_len, dst_len);
    return NULL;
  }

  change = kmalloc(size, GFP_ATOMIC);
  if (unlikely(change == NULL)) {
    mpr_err("kmalloc(%zd) failed", size);
    return NULL;
  }
  change->size = size;
  change->action = act;
  change->src_len = src_len;
  change->dst_len = dst_len;
  change->timestamp = jiffies;
  strcpy(change->src, root ? root : "");
  strcat(change->src, src);
  if (unlikely(dst)) {
    strcpy(_krp_change_dst(change), root ? root : "");
    strcat(_krp_change_dst(change), dst);
  }
  return change;
}

static inline void _krp_change_free(struct krp_change *change) {
  kfree(change);
}

static struct krp_change *_krp_change_renew(struct krp_change *change, int act,
                                            const char *src, const char *dst) {
  size_t src_len = strlen(src);
  size_t dst_len = dst ? strlen(dst) : 0;
  unsigned long ts = change->timestamp;
  //            |struct krp_change          |src          |dst
  size_t size = sizeof(struct krp_change) + src_len + 1 + dst_len + 1;

  if (unlikely(size > 65535)) {
    mpr_err("path overflow %zd %zd", src_len, dst_len);
    return NULL;
  }

  if (change->size < size) {
    _krp_change_free(change);
    change = kmalloc(size, GFP_ATOMIC);
    if (unlikely(change == NULL)) {
      mpr_err("kmalloc(%zd) failed", size);
      return NULL;
    }
    change->size = size;
    change->timestamp = ts;
  }
  change->action = act;
  change->src_len = src_len;
  change->dst_len = dst_len;
  strcpy(change->src, src);
  strcpy(_krp_change_dst(change), dst ? dst : "");
  return change;
}

static int change_total_count = 0;
static int change_current_count = 0;
static int change_discarded_count = 0;
static int change_memory_size = 0;

static atomic_t vfs_changes_is_open;
static atomic_t vfs_changes_is_busy;
static wait_queue_head_t wq_vfs_changes;
// 数量唤醒条件 达到这个数量则唤醒读取并返回
static int wq_vfs_number = 0;
static int wq_vfs_timer_expires = 0;

static LIST_HEAD(change_head);
static DEFINE_RWLOCK(change_lock);
// 例如 连续新建1000个文件 会造成后面的操作对整个链表完全遍历
// 新建合并时 只需要遍历 删除和重命名
// 删除合并时 只需要遍历 新建和重命名
// ACT_NEW_FILE ACT_NEW_LINK ACT_NEW_SYMLINK
static LIST_HEAD(new_change_head);
// ACT_DEL_FILE
static LIST_HEAD(del_change_head);
// ACT_RENAME_FILE
static LIST_HEAD(rename_change_head);

static inline void _krp_change_destroy(struct krp_change *change) {
  change_current_count--;
  change_memory_size -= change->size;

  list_del(&change->node);
  switch (change->action) {
    case ACT_NEW_FILE:
    case ACT_NEW_LINK:
    case ACT_NEW_SYMLINK:
    case ACT_DEL_FILE:
    case ACT_RENAME_FILE:
      list_del(&change->type_node);
      break;
    default:  // ACT_NEW_FOLDER ACT_DEL_FOLDER ACT_RENAME_FOLDER
      break;
  }
  _krp_change_free(change);
}

static void _vfs_setup_timeout(void);
static inline void _krp_change_insert(struct krp_change *change) {
  trace_change_node("vfs_change insert %d %s %s", change->action, change->src,
               _krp_change_dst(change));
  list_add_tail(&change->node, &change_head);
  switch (change->action) {
    case ACT_NEW_FILE:
    case ACT_NEW_LINK:
    case ACT_NEW_SYMLINK:
      list_add_tail(&change->type_node, &new_change_head);
      break;
    case ACT_DEL_FILE:
      list_add_tail(&change->type_node, &del_change_head);
      break;
    case ACT_RENAME_FILE:
      list_add_tail(&change->type_node, &rename_change_head);
      break;
    default:
      break;
  }
  change_total_count++;
  change_current_count++;
  change_memory_size += change->size;

  while (change_memory_size > VFS_CHANGE_MEMORY_LIMIT) {
    change_discarded_count++;
    _krp_change_destroy(
        list_first_entry(&change_head, struct krp_change, node));
  }

  if (wq_vfs_number > 0 && change_current_count >= wq_vfs_number) {
    wake_up_interruptible(&wq_vfs_changes);
  }
  if (change_current_count == 1) {
    _vfs_setup_timeout();
  }
}

enum vfs_merge_lock {
  vfs_merge_lock_read_try,
  vfs_merge_lock_write,
  vfs_merge_lock_free,
};
static int _vfs_merge(struct krp_change *change, enum vfs_merge_lock type) {
  struct krp_change *cur;
  int ret = 0;

  switch (type) {
    case vfs_merge_lock_read_try:
      read_lock(&change_lock);
      break;
    case vfs_merge_lock_write:
      write_lock(&change_lock);
      break;
    default:  // vfs_merge_lock_free
      break;
  }

  switch (change->action) {
    case ACT_NEW_FILE:
    case ACT_NEW_LINK:
    case ACT_NEW_SYMLINK:
      // 1. 删除列表 如果 change->src == cur->src 则两个都需要销毁
      list_for_each_entry_reverse(cur, &del_change_head, type_node) {
        if (unlikely(strcmp(change->src, cur->src) == 0)) {
          if (type != vfs_merge_lock_read_try) {
            trace_change_node("vfs_change merge del-new %s", cur->src);
            _krp_change_destroy(cur);
            _krp_change_free(change);
          }
          ret = 1;
          goto out;
        }
      }
      // 2. 重命名列表 如果 change->src == cur->src 则 创建rename目标文件
      list_for_each_entry_reverse(cur, &rename_change_head, type_node) {
        if (unlikely(strcmp(change->src, cur->src) == 0)) {
          if (type != vfs_merge_lock_read_try) {
            trace_change_node("vfs_change merge rename-new %s -> %s", cur->src,
                         _krp_change_dst(cur));
            change = _krp_change_renew(change, change->action,
                                       _krp_change_dst(cur), NULL);
            if (change) _krp_change_insert(change);
            _krp_change_destroy(cur);
          }
          ret = 1;
          goto out;
        }
      }
      break;
    case ACT_DEL_FILE:
      // 1. 创建列表 如果 change->src == cur->src 则两个都需要销毁
      list_for_each_entry_reverse(cur, &new_change_head, type_node) {
        if (unlikely(strcmp(change->src, cur->src) == 0)) {
          if (type != vfs_merge_lock_read_try) {
            trace_change_node("vfs_change merge new-del %s", cur->src);
            _krp_change_destroy(cur);
            _krp_change_free(change);
          }
          ret = 1;
          goto out;
        }
      }
      // 2. 重命名列表 如果 change->src == cur->dst 则 删除rename源文件
      list_for_each_entry_reverse(cur, &rename_change_head, type_node) {
        if (unlikely(strcmp(change->src, _krp_change_dst(cur)) == 0)) {
          if (type != vfs_merge_lock_read_try) {
            trace_change_node("vfs_change merge rename-del %s -> %s", cur->src,
                         _krp_change_dst(cur));
            change = _krp_change_renew(change, change->action, cur->src, NULL);
            if (change) _krp_change_insert(change);
            _krp_change_destroy(cur);
          }
          ret = 1;
          goto out;
        }
      }
      break;
    case ACT_RENAME_FILE:
      // 1. 创建列表 如果 change->src == cur->src 则 创建rename目标文件
      list_for_each_entry_reverse(cur, &new_change_head, type_node) {
        if (unlikely(strcmp(change->src, cur->src) == 0)) {
          if (type != vfs_merge_lock_read_try) {
            trace_change_node("vfs_change merge new-rename %s -> %s", change->src,
                         _krp_change_dst(change));
            change = _krp_change_renew(change, cur->action,
                                       _krp_change_dst(change), NULL);
            if (change) {
              if (!_vfs_merge(change, vfs_merge_lock_free)) {
                _krp_change_insert(change);
              }
            }
            _krp_change_destroy(cur);
          }
          ret = 1;
          goto out;
        }
      }
      // 2. 删除列表 如果 change->dst == cur->src 则 删除rename源文件
      list_for_each_entry_reverse(cur, &del_change_head, type_node) {
        if (unlikely(strcmp(_krp_change_dst(change), cur->src) == 0)) {
          if (type != vfs_merge_lock_read_try) {
            trace_change_node("vfs_change merge del-rename %s -> %s", change->src,
                         _krp_change_dst(change));
            change = _krp_change_renew(change, cur->action, change->src, NULL);
            if (change) {
              if (!_vfs_merge(change, vfs_merge_lock_free)) {
                _krp_change_insert(change);
              }
            }
            _krp_change_destroy(cur);
          }
          ret = 1;
          goto out;
        }
      }
      // 3. 重命名列表 如果 change->src == cur->dst 则 变为rename cur->src ->
      // change->dst
      list_for_each_entry_reverse(cur, &rename_change_head, type_node) {
        // 1. A-B B-C => A-C  2. B-C A-B => A-C 3. A-B B-A => 0
        struct krp_change *src, *dst;
        if (unlikely(strcmp(_krp_change_dst(cur), change->src) == 0)) {
          src = cur;
          dst = change;
        } else if (unlikely(strcmp(_krp_change_dst(change), cur->src) == 0)) {
          src = change;
          dst = cur;
        } else {
          continue;
        }
        if (type != vfs_merge_lock_read_try) {
          trace_change_node("vfs_change merge rename-rename %s -> %s ignore(%s)",
                       src->src, _krp_change_dst(dst), _krp_change_dst(src));
          if (strcmp(src->src, _krp_change_dst(dst)) == 0) {
            _krp_change_destroy(cur);
            _krp_change_free(change);
          } else {
            change = _krp_change_renew(change, cur->action, src->src,
                                       _krp_change_dst(dst));
            if (change) _krp_change_insert(change);
            _krp_change_destroy(cur);
          }
        }
        ret = 1;
        goto out;
      }
      break;
    default:
      break;
  }

out:
  switch (type) {
    case vfs_merge_lock_read_try:
      read_unlock(&change_lock);
      break;
    case vfs_merge_lock_write:
      write_unlock(&change_lock);
      break;
    default:  // vfs_merge_lock_free
      break;
  }
  return ret;
}

void vfs_put_change(int act, const char *root, const char *src,
                    const char *dst) {
  struct krp_change *change = _krp_change_new(act, root, src, dst);
  if (unlikely(change == NULL)) {
    return;
  }

  if (merge_actions) {
    // 合并成功占的比例应该不会高 所以先用读锁尝试可能后再执行
    if (_vfs_merge(change, vfs_merge_lock_read_try)) {
      if (_vfs_merge(change, vfs_merge_lock_write)) {
        return;
      }
    }
  }
  write_lock(&change_lock);
  _krp_change_insert(change);
  write_unlock(&change_lock);
}

int vfs_get_change_user(char __user *data, int size, int *count) {
  LIST_HEAD(read_changes);
  LIST_HEAD(list);
  int writted = 0;
  struct list_head *p, *next;
  struct krp_change *change;
  struct krp_change *last_new = NULL, *last_del = NULL, *last_rename = NULL,
                    *last_unread = NULL;

  if (unlikely(data == NULL || size <= 0)) {
    return -EINVAL;
  }
  trace_change("total(%d) current(%d)", change_total_count, change_current_count);

  write_lock(&change_lock);

  list_for_each_entry(change, &change_head, node) {
    int ser_size = 1 + change->src_len + 1 + change->dst_len + 1;
    if (writted + ser_size > size) {
      last_unread = change;
      break;
    }
    writted += ser_size;
    switch (change->action) {
      case ACT_NEW_FILE:
      case ACT_NEW_LINK:
      case ACT_NEW_SYMLINK:
        last_new = change;
        break;
      case ACT_DEL_FILE:
        last_del = change;
        break;
      case ACT_RENAME_FILE:
        last_rename = change;
        break;
      default:
        break;
    }
    change_current_count--;
    change_memory_size -= change->size;
  }

  if (last_unread)
    list_cut_before(&read_changes, &change_head, &last_unread->node);
  else
    list_cut_position(&read_changes, &change_head, change_head.prev);

  if (last_new)
    list_cut_position(&list, &new_change_head, &last_new->type_node);
  if (last_del)
    list_cut_position(&list, &del_change_head, &last_del->type_node);
  if (last_rename)
    list_cut_position(&list, &rename_change_head, &last_rename->type_node);

  write_unlock(&change_lock);

  (*count) = 0;
  list_for_each_entry(change, &read_changes, node) {
    int size = change->src_len + 1;
    if (change->dst_len) size += change->dst_len + 1;
    if (unlikely(copy_to_user(data, &change->action, 1))) {
      mpr_err("copy_to_user");
      writted = -EINVAL;
      break;
    }
    data++;
    if (unlikely(copy_to_user(data, change->src, size))) {
      mpr_err("copy_to_user");
      writted = -EINVAL;
      break;
    }
    (*count)++;
    data += size;
  }
  trace_change("total(%d) current(%d) count(%d)", change_total_count, change_current_count, *count);
  list_for_each_safe(p, next, &read_changes) {
    change = list_entry(p, struct krp_change, node);
    _krp_change_free(change);
  }

  return writted;
}

///////////////////////////////////////////////////////////////////////////////

static void wait_vfs_changes_timer_callback(
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
    unsigned long data
#else
    struct timer_list *t
#endif
) {
  wake_up_interruptible(&wq_vfs_changes);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
static DEFINE_TIMER(wq_vfs_timer, wait_vfs_changes_timer_callback, 0, 0);
#else
static DEFINE_TIMER(wq_vfs_timer, wait_vfs_changes_timer_callback);
#endif

static inline int _vfs_change_timeout(void) {
  if (wq_vfs_timer_expires != 0 && !list_empty(&change_head)) {
    struct krp_change *change =
        list_first_entry(&change_head, struct krp_change, node);
    trace_change("current: %lu, timeout: %lu", jiffies, change->timestamp + wq_vfs_timer_expires);
    if (time_before_eq(change->timestamp + wq_vfs_timer_expires, jiffies)) {
      return 1;
    }
  }
  return 0;
}

static void _vfs_setup_timeout(void) {
  if (wq_vfs_timer_expires != 0 && !list_empty(&change_head)) {
    struct krp_change *change =
        list_first_entry(&change_head, struct krp_change, node);
    mod_timer(&wq_vfs_timer, change->timestamp + wq_vfs_timer_expires);
    trace_change("make timer: %lu", change->timestamp + wq_vfs_timer_expires);
  }
}

static inline int _vfs_wait_condition(void) {
  int r = 0;
  read_lock(&change_lock);
  if (change_current_count >= wq_vfs_number) {
    r = 1;
    trace_change("current(%d) limit(%d)", change_current_count, wq_vfs_number);
  } else {
    r = _vfs_change_timeout();
  }
  read_unlock(&change_lock);
  return r;
}

static inline int _vfs_wait(int count, int first_timeout, int total_timeout) {
  unsigned long timeout = total_timeout * HZ / 1000;
  unsigned long expires = first_timeout * HZ / 1000;
  int r;

  write_lock(&change_lock);
  wq_vfs_number = count;
  wq_vfs_timer_expires = expires;
  _vfs_setup_timeout();
  write_unlock(&change_lock);

  if (timeout == 0) {
    r = wait_event_interruptible(wq_vfs_changes, _vfs_wait_condition() != 0);
  } else {
    r = wait_event_interruptible_timeout(wq_vfs_changes,
                                         _vfs_wait_condition() != 0, timeout);
  }

  trace_change("%d %d", r, ERESTARTSYS);
  write_lock(&change_lock);
  wq_vfs_number = 0;
  wq_vfs_timer_expires = 0;
  write_unlock(&change_lock);
  del_timer(&wq_vfs_timer);

  if (r < 0) {
    return r;
  }

  return 0;
}

static inline void _vfs_format_time(unsigned long jiff, char *buf, size_t sz) {
  struct tm ts;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 0, 0)
  struct timeval tv;
  jiffies_to_timeval(jiff, &tv);
#else
  struct timespec64 tv;
  jiffies_to_timespec64(jiff, &tv);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
  time_t shifted_secs = tv.tv_sec + hour_shift * 3600;
#else
  time64_t shifted_secs = tv.tv_sec + hour_shift * 3600;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0)
  time_to_tm(shifted_secs, 0, &ts);
#else
  time64_to_tm(shifted_secs, 0, &ts);
#endif

  snprintf(buf, sz, "%04ld-%02d-%02d %02d:%02d:%02d.%03ld", 1900 + ts.tm_year,
           1 + ts.tm_mon, ts.tm_mday, ts.tm_hour, ts.tm_min, ts.tm_sec,
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 0, 0)
           tv.tv_usec / 1000
#else
           tv.tv_nsec
#endif
  );
}

static int open_vfs_changes(struct inode *si, struct file *filp) {
  if (atomic_cmpxchg(&vfs_changes_is_open, 0, 1) == 1) return -EBUSY;

  filp->private_data = (void *)jiffies;
  return 0;
}
static int release_vfs_changes(struct inode *si, struct file *filp) {
  atomic_set(&vfs_changes_is_open, 0);
  atomic_set(&vfs_changes_is_busy, 0);
  return 0;
}
// give string format output for debugging purpose (cat /proc/vfs_changes)
// format: YYYY-MM-DD hh:mm:ss.SSS $action $src $dst, let's require at least 50
// bytes
/*
 * read_vfs_changes can only be called by one thread at the same time, otherwise
 * the result is undefined.
 *
 * This module transfer event data by copy_to_user and data buffer.
 * If the buffer is dynamically allocated when the function is called, the
 * allocation may fail. If the buffer is allocated before the function call, the
 * buffer will be locked and protected, but this may lead to a deadlock, which
 * will cause the system to freeze.
 *
 * The current method is to declare that the function does not support
 * multi-threaded access, which avoids locking the buffer. At the same time,
 * since the module is currently only accessed by a single thread, it will not
 * affect system functions.
 */
static ssize_t read_vfs_changes(struct file *filp, char __user *buf,
                                size_t size, loff_t *offset) {
  unsigned long last = (unsigned long)filp->private_data;
  struct krp_change *change;
  ssize_t readed = 0, line_len;
  char *line;
  int r;
  static const char *action_names[] = {
      "file-created", "link-created", "symlink-created", "dir-created",
      "file-deleted", "dir-deleted",  "file-renamed",    "dir-renamed"};

  trace_change("start");
  if (atomic_cmpxchg(&vfs_changes_is_busy, 0, 1) == 1) return -EBUSY;

  r = _vfs_wait(1, 0, 0);
  if (r) {
    atomic_set(&vfs_changes_is_busy, 0);
    return r;
  }

  line = kmalloc(PATH_MAX * 2 + 64, GFP_KERNEL);

  read_lock(&change_lock);

  list_for_each_entry(change, &change_head, node) {
    if (time_before_eq(change->timestamp, last)) {
      continue;
    }

    // YYYY-MM-DD hh:mm:ss.SSS $action src dst\n
    _vfs_format_time(change->timestamp, line, 64);
    strcat(line, " ");
    strcat(line, action_names[change->action]);
    strcat(line, " ");
    strcat(line, change->src);
    if (change->dst_len) {
      strcat(line, " ");
      strcat(line, _krp_change_dst(change));
    }
    strcat(line, "\n");
    line_len = strlen(line);

    if (readed + line_len > size) break;
    trace_change("copy: sz(%zd) readed(%zd) line(%zd)", size, readed, line_len);
    if (copy_to_user(buf + readed, line, line_len)) {
      mpr_err("copy_to_user");
      break;
    }

    readed += line_len;
    last = change->timestamp;
  }
  read_unlock(&change_lock);

  kfree(line);

  trace_change("end %zd", readed);
  filp->private_data = (void *)last;

  atomic_set(&vfs_changes_is_busy, 0);
  return readed;
}

/*
 * move_vfs_changes can only be called by one thread at the same time, otherwise
 * the result is undefined.
 *
 * This module transfer event data by copy_to_user and data buffer.
 * If the buffer is dynamically allocated when the function is called, the
 * allocation may fail. If the buffer is allocated before the function call, the
 * buffer will be locked and protected, but this may lead to a deadlock, which
 * will cause the system to freeze.
 *
 * The current method is to declare that the function does not support
 * multi-threaded access, which avoids locking the buffer. At the same time,
 * since the module is currently only accessed by a single thread, it will not
 * affect system functions.
 */
static long move_vfs_changes(ioctl_rd_args __user *ira) {
  ioctl_rd_args kira;
  if (copy_from_user(&kira, ira, sizeof(kira)) != 0) {
    return -EFAULT;
  }

  if (vfs_get_change_user(kira.data, kira.size, &kira.size) < 0) {
    return -EINVAL;
  }

	if (copy_to_user(ira, &kira, sizeof(kira)) != 0) {
		return -EFAULT;
	}

  return 0;
}

static long read_stats(ioctl_rs_args __user *irsa) {
  ioctl_rs_args kirsa = {
      .total_changes = change_total_count,
      .cur_changes = change_current_count,
      .discarded = change_discarded_count,
      .cur_memory = change_memory_size,
  };
  if (copy_to_user(irsa, &kirsa, sizeof(kirsa)) != 0) return -EFAULT;
  return 0;
}

static long wait_vfs_changes(ioctl_wd_args __user *ira) {
  ioctl_wd_args kira;
  int r;
  if (copy_from_user(&kira, ira, sizeof(kira)) != 0) return -EFAULT;

  r = _vfs_wait(kira.condition_count, kira.condition_timeout, kira.timeout);
  if (r) return r;

  return 0;
}

static long ioctl_vfs_changes(struct file *filp, unsigned int cmd,
                              unsigned long arg) {
  int r = -EINVAL;
  if (atomic_cmpxchg(&vfs_changes_is_busy, 0, 1) == 1) return -EBUSY;
  switch (cmd) {
    case VC_IOCTL_READDATA:
      r = move_vfs_changes((ioctl_rd_args *)arg);
      break;
    case VC_IOCTL_READSTAT:
      r = read_stats((ioctl_rs_args *)arg);
      break;
    case VC_IOCTL_WAITDATA:
      r = wait_vfs_changes((ioctl_wd_args *)arg);
      break;
  }
  atomic_set(&vfs_changes_is_busy, 0);
  return r;
}
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static struct file_operations procfs_ops = {
    .owner = THIS_MODULE,
    .open = open_vfs_changes,
    .read = read_vfs_changes,
    .unlocked_ioctl = ioctl_vfs_changes,
    .llseek = no_llseek,
    //.llseek = generic_file_llseek,
    .release = release_vfs_changes,
};
#else
static struct proc_ops procfs_ops = {
    .proc_open = open_vfs_changes,
    .proc_read = read_vfs_changes,
    .proc_ioctl = ioctl_vfs_changes,
    .proc_lseek = no_llseek,
    .proc_release = release_vfs_changes,
};
#endif

int vfs_init_change(void) {
  struct proc_dir_entry *procfs_entry =
      proc_create(PROCFS_NAME, 0666, 0, &procfs_ops);
  if (procfs_entry == 0) {
    mpr_err("%s already exists?\n", PROCFS_NAME);
    return 0;
  }

  init_waitqueue_head(&wq_vfs_changes);
  atomic_set(&vfs_changes_is_open, 0);
  atomic_set(&vfs_changes_is_busy, 0);

  return 1;
}

void vfs_clean_change(void) {
  struct krp_change *cur;
  struct list_head *p, *next;

  remove_proc_entry(PROCFS_NAME, 0);

  write_lock(&change_lock);
  list_for_each_safe(p, next, &change_head) {
    cur = list_entry(p, struct krp_change, node);
    _krp_change_free(cur);
  }

  INIT_LIST_HEAD(&change_head);
  INIT_LIST_HEAD(&new_change_head);
  INIT_LIST_HEAD(&del_change_head);
  INIT_LIST_HEAD(&rename_change_head);

  change_total_count = 0;
  change_current_count = 0;
  change_discarded_count = 0;
  change_memory_size = 0;
  write_unlock(&change_lock);
}
