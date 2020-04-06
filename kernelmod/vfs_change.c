#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/version.h>

#include "vfs_change_consts.h"
#include "vfs_change_uapi.h"

#ifndef MAX_VFS_CHANGE_MEM
#define MAX_VFS_CHANGE_MEM	(1<<20)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 0, 0)
#define TIMESTRUCT timeval
#else
#define TIMESTRUCT timespec64
#endif

typedef struct __vfs_change__ {
	struct TIMESTRUCT ts;
	char *src, *dst;
	unsigned char action;
	unsigned short size;
	struct list_head list;
} vfs_change;

#define REMOVE_ENTRY(p, vc) {\
	list_del(p);\
	total_memory -= vc->size;\
	kfree(vc);\
	cur_changes--;\
}

static const char* action_names[] = {"file-created", "link-created", "symlink-created", "dir-created", "file-deleted", "dir-deleted", "file-renamed", "dir-renamed"};

static LIST_HEAD(vfs_changes);
static int discarded = 0, total_changes = 0, cur_changes = 0, total_memory = 0;
static DEFINE_SPINLOCK(sl_changes);

static wait_queue_head_t wq_vfs_changes;
static atomic_t wait_vfs_changes_count;
static atomic_t vfs_changes_is_open;

static void wait_vfs_changes_timer_callback(
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
unsigned long data
#else
struct timer_list *t
#endif
)
{
	atomic_set(&wait_vfs_changes_count, 1);
	wake_up_interruptible(&wq_vfs_changes);
}

static void wait_vfs_changes_timeout_timer_callback(
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
unsigned long data
#else
struct timer_list *t
#endif
)
{
	atomic_set(&wait_vfs_changes_count, 0);
	wake_up_interruptible(&wq_vfs_changes);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
static DEFINE_TIMER(wait_vfs_changes_timer, wait_vfs_changes_timer_callback, 0, 0);
static DEFINE_TIMER(wait_vfs_changes_timeout_timer, wait_vfs_changes_timeout_timer_callback, 0, 0);
#else
static DEFINE_TIMER(wait_vfs_changes_timer, wait_vfs_changes_timer_callback);
static DEFINE_TIMER(wait_vfs_changes_timeout_timer, wait_vfs_changes_timeout_timer_callback);
#endif

static int open_vfs_changes(struct inode* si, struct file* filp)
{
	if (atomic_cmpxchg(&vfs_changes_is_open, 0, 1) == 1) {
		return -EBUSY;
	}

	struct TIMESTRUCT* tv = kzalloc(sizeof(struct TIMESTRUCT), GFP_KERNEL);
	if (unlikely(tv == 0)) {
		atomic_set(&vfs_changes_is_open, 0);
		return -ENOMEM;
	}

	filp->private_data = tv;
	return 0;
}

static int release_vfs_changes(struct inode* si, struct file* filp)
{
	kfree(filp->private_data);
	atomic_set(&vfs_changes_is_open, 0);
	atomic_set(&wait_vfs_changes_count, -1);;
	del_timer(&wait_vfs_changes_timer);
	del_timer(&wait_vfs_changes_timeout_timer);

	return 0;
}

static int hour_shift = 8;
module_param(hour_shift, int, 0644);
MODULE_PARM_DESC(hour_shift, "hour shift for displaying /proc/vfs_changes line item time");

#define MIN_LINE_SIZE	50

static ssize_t copy_vfs_changes(struct TIMESTRUCT *last, char* buf, size_t size)
{
	size_t total = 0;
	vfs_change* vc;
	list_for_each_entry(vc, &vfs_changes, list) {
		if (vc->ts.tv_sec < last->tv_sec || 
			(vc->ts.tv_sec == last->tv_sec && 
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 0, 0)
			vc->ts.tv_usec <= last->tv_usec
#else
			vc->ts.tv_nsec <= last->tv_nsec
#endif
			))
			continue;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0))
		time_t shifted_secs = vc->ts.tv_sec + hour_shift*3600;
#else
		ktime_t shifted_secs = vc->ts.tv_sec + hour_shift*3600;
#endif
		struct tm ts;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0)
		time_to_tm(shifted_secs, 0, &ts);
#else
		time64_to_tm(shifted_secs, 0, &ts);
#endif
		char temp[MIN_LINE_SIZE];
		snprintf(temp, sizeof(temp), "%04ld-%02d-%02d %02d:%02d:%02d.%03ld %s ",
			1900+ts.tm_year, 1+ts.tm_mon, ts.tm_mday, ts.tm_hour, ts.tm_min, ts.tm_sec,
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 0, 0)
			vc->ts.tv_usec/1000
#else
			vc->ts.tv_nsec
#endif
			, action_names[vc->action]);
		size_t line_len = strlen(temp) + strlen(vc->src) + 1;  //+1 for \n
		if (vc->dst)
			line_len += strlen(vc->dst) + 1;  //+1 for space
		if (line_len + total > size)
			break;

		memcpy(buf+total, temp, strlen(temp));
		total += strlen(temp);
		memcpy(buf+total, vc->src, strlen(vc->src));
		total += strlen(vc->src);

		if (vc->dst) {
			buf[total] = ' ';
			memcpy(buf+total+1, vc->dst, strlen(vc->dst));
			total += 1+strlen(vc->dst);
		}
		buf[total] = '\n';
		total++;
		*last = vc->ts;
	}
	return total;
}

// give string format output for debugging purpose (cat /proc/vfs_changes)
// format: YYYY-MM-DD hh:mm:ss.SSS $action $src $dst, let's require at least 50 bytes
static ssize_t read_vfs_changes(struct file* filp, char __user* buf, size_t size, loff_t* offset)
{
	if (size < MIN_LINE_SIZE)
		return -EINVAL;

	char* kbuf = kmalloc(size, GFP_KERNEL);
	if (kbuf == 0)
		return -ENOMEM;

	struct TIMESTRUCT *last = (struct TIMESTRUCT*)filp->private_data;
	spin_lock(&sl_changes);
	ssize_t r = copy_vfs_changes(last, kbuf, size);
	spin_unlock(&sl_changes);
	if (r > 0 && copy_to_user(buf, kbuf, r))
		r = -EFAULT;

	kfree(kbuf);
	return r;
}

static long move_vfs_changes(ioctl_rd_args __user* ira)
{
	if (atomic_cmpxchg(&wait_vfs_changes_count, -1, INT_MAX) >= 0) {
		return -EBUSY;
	}

	ioctl_rd_args kira;
	if (copy_from_user(&kira, ira, sizeof(kira)) != 0) {
		atomic_set(&wait_vfs_changes_count, -1);
		return -EFAULT;
	}

	char *kbuf = kmalloc(kira.size, GFP_KERNEL);
	if (kbuf == 0) {
		atomic_set(&wait_vfs_changes_count, -1);
		return -ENOMEM;
	}

	int total_bytes = 0, total_items = 0;

	spin_lock(&sl_changes);
	struct list_head *p, *next;
	list_for_each_safe(p, next, &vfs_changes) {
		vfs_change* vc = list_entry(p, vfs_change, list);
		char head = vc->action;
		int this_len = sizeof(head) + strlen(vc->src) + 1;
		if (vc->dst)
			this_len += strlen(vc->dst) + 1;
		if (this_len + total_bytes > kira.size)
			break;

		memcpy(kbuf + total_bytes, &head, sizeof(head));
		total_bytes += sizeof(head);
		// src and dst are adjacent when allocated
		memcpy(kbuf + total_bytes, vc->src, this_len - sizeof(head));
		total_bytes += this_len-sizeof(head);

		total_items++;
		REMOVE_ENTRY(p, vc);
	}
	spin_unlock(&sl_changes);

	if (total_bytes && copy_to_user(kira.data, kbuf, total_bytes)) {
		kfree(kbuf);
		atomic_set(&wait_vfs_changes_count, -1);
		return -EFAULT;
	}
	kfree(kbuf);

	kira.size = total_items;
	if (copy_to_user(ira, &kira, sizeof(kira)) != 0) {
		atomic_set(&wait_vfs_changes_count, -1);
		return -EFAULT;
	}

	atomic_set(&wait_vfs_changes_count, -1);

	return 0;
}

static long read_stats(ioctl_rs_args __user* irsa)
{
	ioctl_rs_args kirsa = {
		.total_changes = total_changes,
		.cur_changes = cur_changes,
		.discarded = discarded,
		.cur_memory = total_memory,
	};
	if (copy_to_user(irsa, &kirsa, sizeof(kirsa)) != 0)
		return -EFAULT;
	return 0;
}

static long wait_vfs_changes(ioctl_wd_args __user* ira)
{
	if (atomic_cmpxchg(&wait_vfs_changes_count, -1, 0) >= 0)
		return -EBUSY;

	ioctl_wd_args kira;
	if (copy_from_user(&kira, ira, sizeof(kira)) != 0)
		return -EFAULT;

	if (kira.condition_count < 0 || kira.condition_timeout < 0 || (kira.condition_count == 0 && kira.condition_timeout))
		return -EINVAL;

	if (kira.timeout > 0) {
		mod_timer(&wait_vfs_changes_timeout_timer, jiffies + HZ * kira.timeout / 1000);
	}

	int wait_vfs_changes_timer_runing = 0;

	if (kira.condition_count > 0)
		atomic_set(&wait_vfs_changes_count, kira.condition_count);
	else
		atomic_set(&wait_vfs_changes_count, INT_MAX);

	while (cur_changes < atomic_read(&wait_vfs_changes_count)) {
		if (kira.condition_timeout > 0) {
			if (wait_vfs_changes_timer_runing == 1) {
				if (timer_pending(&wait_vfs_changes_timer) == 0)
					break;
			} else if (cur_changes > 0) {
				wait_vfs_changes_timer_runing = 1;
				mod_timer(&wait_vfs_changes_timer, jiffies + HZ * kira.condition_timeout / 1000);
			}
		}

		if (kira.condition_timeout > 0 && wait_vfs_changes_timer_runing == 0) {
			int old = atomic_read(&wait_vfs_changes_count);

			atomic_set(&wait_vfs_changes_count, 1);

			if (wait_event_interruptible(wq_vfs_changes, cur_changes >= atomic_read(&wait_vfs_changes_count)) != 0)
				return -EAGAIN;

			atomic_set(&wait_vfs_changes_count, old);
		} else {
			if (wait_event_interruptible(wq_vfs_changes, cur_changes >= atomic_read(&wait_vfs_changes_count)) != 0)
				return -EAGAIN;
		}

		if (kira.timeout > 0 && timer_pending(&wait_vfs_changes_timeout_timer) == 0) {
			return -ETIME;
		}
	}

	atomic_set(&wait_vfs_changes_count, -1);
	del_timer(&wait_vfs_changes_timer);
	del_timer(&wait_vfs_changes_timeout_timer);

	return 0;
}

static long ioctl_vfs_changes(struct file* filp, unsigned int cmd, unsigned long arg)
{
	switch(cmd) {
	case VC_IOCTL_READDATA:
		return move_vfs_changes((ioctl_rd_args*)arg);
	case VC_IOCTL_READSTAT:
		return read_stats((ioctl_rs_args*)arg);
	case VC_IOCTL_WAITDATA:
		return wait_vfs_changes((ioctl_wd_args*)arg);
	default:
		return -EINVAL;
	}
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0))
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
	.proc_compat_ioctl = ioctl_vfs_changes,
	.proc_lseek = no_llseek,
	//proc_lseek = generic_file_llseek,
	.proc_release = release_vfs_changes,
};
#endif

int __init init_vfs_changes(void)
{
	struct proc_dir_entry* procfs_entry = proc_create(PROCFS_NAME, 0666, 0, &procfs_ops);
	if (procfs_entry == 0) {
		pr_warn("%s already exists?\n", PROCFS_NAME);
		return -1;
	}

	init_waitqueue_head(&wq_vfs_changes);
	atomic_set(&vfs_changes_is_open, 0);
	atomic_set(&wait_vfs_changes_count, -1);

	return 0;
}

// c_v_c is also called when doing rollback in module-init phase, so it cant be marked as __exit
void cleanup_vfs_changes(void)
{
	remove_proc_entry(PROCFS_NAME, 0);
	// remove all dynamically allocated memory
	struct list_head *p, *next;
	spin_lock(&sl_changes);
	list_for_each_safe(p, next, &vfs_changes) {
		vfs_change* vc = list_entry(p, vfs_change, list);
		REMOVE_ENTRY(p, vc);
	}
	spin_unlock(&sl_changes);
}

static void remove_oldest(void)
{
	spin_lock(&sl_changes);
	struct list_head *p, *next;
	while (total_memory > MAX_VFS_CHANGE_MEM && !list_empty(&vfs_changes)) {
		list_for_each_safe(p, next, &vfs_changes) {
			vfs_change* vc = list_entry(p, vfs_change, list);
			// if (printk_ratelimit())
			// 	pr_warn("vfs-change discarded[%d]: %lu %s, %s, %s\n",
			// 		vc->size, vc->ts.tv_sec, action_names[vc->action], vc->src, vc->dst);
			REMOVE_ENTRY(p, vc);
			discarded++;
			break;
		}
	}
	spin_unlock(&sl_changes);
}

/*
curr: new-dir(a)
prev:
	* new-file(X): continue
	* new-dir(X): continue [if a == X, break and warn]
	* del-file(X): continue
	* del-dir(X): continue //even if a == X, a might contain files and so we cant simply remove prev del-dir(a) action
	* rename-file(X, Y): continue
	* rename-dir(X, Y): continue //even if a == Y, a might contain files...

curr: del-dir(a)
prev:
	* new-file(X): if X is sub-path of a, remove prev and continue
	* new-dir(a): remove prev, break
	* new-dir(X): if X is sub-path of a, remove prev and continue
	* del-file(X): if X is sub-path of a, remove prev and continue
	* del-dir(X): if X is sub-path of a, remove prev and continue, elif a == X, break and warn
	* rename-file(X, Y): if X/Y is sub-path of a, remove prev and continue, elif X is sub-path of a, change prev into new-file(Y) and continue, elif Y is sub-path of a, change prev into del-file(X) and continue, else continue
	* rename-dir(X, a): prev -> del-dir(X), break // we could do better to remember the cur-pos and use del-dir(X) to continue...
	* rename-dir(X, Y): same as rename-file(X, Y)...

curr: rename-dir(a, b)
	* new-file(X): continue
	* new-dir(a): continue //a may have files after creation, so we cant prev -> new-dir(b), ideally, we can check to see if there's any file/dir created in a after new-dir(a), let's postpone the work later (if will be done)
	* new-dir(X): continue [if X == b, break and warn]
	* del-file(X): continue
	* del-dir(X): continue //even if X == b, b may contain files so we cant remove prev and curr -> del-dir(a), as in del-file(b)
	* rename-file(X, Y): continue
	* rename-dir(X, a): continue //if a is dir and there's any operations for a in-between the 2 actions, combine them into rename(X, b) will lost all the changes
	* rename-dir(X, Y): continue
*/

#define MERGE_CON	0
#define MERGE_BRK	1

typedef int (*merge_action_fn)(struct list_head* entry, vfs_change* cur);

/*
curr: new-file(a)
prev:
	* new-file(X): continue [if a == X, break and warn]
	* new-dir(X): continue
	* del-file(a): remove prev, break
	* del-file(X): continue
	* del-dir(X): continue [if a is sub-path of X, break and warn]
	* rename-file(a, X): prev -> new-file(X), break
	* rename-file(X, Y): continue [if a == Y, break and warn]
	* rename-dir(X, Y): continue
*/
static int merge_new_file(struct list_head* p, vfs_change* cur)
{
	vfs_change* vc = list_entry(p, vfs_change, list);
	if (vc->action == ACT_DEL_FILE) {
		if (strcmp(vc->src, cur->src) != 0)
			return MERGE_CON;
		REMOVE_ENTRY(p, vc);
		return MERGE_BRK;
	} else if (vc->action == ACT_RENAME_FILE) {
		if (strcmp(vc->src, cur->src) != 0)
			return MERGE_CON;
		vc->action = cur->action;
		vc->src = vc->dst;
		vc->dst = 0;
		return MERGE_BRK;
	}
	return MERGE_CON;
}

/*
curr: del-file(a)
prev:
	* new-file(a): remove prev, break
	* new-file(X): continue
	* new-dir(X): continue
	* del-file(X): continue [if a == X, break and warn]
	* del-dir(X): continue [if a is sub-path of X, break and warn]
	* rename-file(X, a): prev -> del-file(X), break
	* rename-file(X, Y): continue [if a == X, break and warn]
	* rename-dir(X, Y): continue
*/
static int merge_del_file(struct list_head* p, vfs_change* cur)
{
	vfs_change* vc = list_entry(p, vfs_change, list);
	if (vc->action < ACT_NEW_FOLDER) {
		if (strcmp(vc->src, cur->src) != 0)
			return MERGE_CON;
		REMOVE_ENTRY(p, vc);
		return MERGE_BRK;
	} else if (vc->action == ACT_RENAME_FILE) {
		if (strcmp(vc->dst, cur->src) != 0)
			return MERGE_CON;
		vc->action = ACT_DEL_FILE;
		vc->dst = 0;
		return MERGE_BRK;
	}
	return MERGE_CON;
}

/*
curr: rename-file(a, b)
prev:
	* new-file(a): remove prev, curr -> new-file(b), continue
	* new-file(X): continue [if X == b, break and warn]
	* new-dir(X): continue
	* del-file(b): remove prev, curr -> del-file(a), continue
	* del-file(X): continue [if a == X, break and warn]
	* rename-file(X, a): remove prev, if X != b, curr -> rename-file(X, b) continue, else break
	* rename-file(X, Y): continue
	* rename-dir(X, Y): continue
*/
static int merge_rename_file(struct list_head* p, vfs_change* cur)
{
	vfs_change* vc = list_entry(p, vfs_change, list);
	if (vc->action <= ACT_NEW_SYMLINK) {
		if (strcmp(vc->src, cur->src) != 0)
			return MERGE_CON;
		cur->action = vc->action;
		REMOVE_ENTRY(p, vc);
		cur->src = cur->dst;
		cur->dst = 0;
		return MERGE_CON;
	} else if (vc->action == ACT_DEL_FILE) {
		if (strcmp(vc->src, cur->dst) != 0)
			return MERGE_CON;
		REMOVE_ENTRY(p, vc);
		cur->action = ACT_DEL_FILE;
		cur->dst = 0;
		return MERGE_CON;
	} else if (vc->action == ACT_RENAME_FILE) {
		if (strcmp(vc->dst, cur->src) != 0 || strcmp(vc->src, cur->dst) != 0)
			return MERGE_CON;
		REMOVE_ENTRY(p, vc);
		return MERGE_CON;
	}
	return MERGE_CON;
}

static merge_action_fn action_merge_fns[] = {merge_new_file, merge_new_file, merge_new_file, 0, merge_del_file, 0, merge_rename_file, 0};

static int merge_actions = 1;
module_param(merge_actions, int, 0644);
MODULE_PARM_DESC(merge_actions, "merge actions to minimize data transfer and fs tree change");

static vfs_change* merge_action(vfs_change* cur)
{
	if (action_merge_fns[cur->action] == 0)
		return cur;

	struct list_head *p, *next;
	list_for_each_prev_safe(p, next, &vfs_changes) {
		merge_action_fn maf = action_merge_fns[cur->action];
		if (maf(p, cur) == MERGE_CON)
			continue;

		kfree(cur);
		cur = 0;
		break;
	}

	//remove duplicate entries
	vfs_change* last = cur;
	list_for_each_prev_safe(p, next, &vfs_changes) {
		vfs_change* vc = list_entry(p, vfs_change, list);
		if (last == 0) {
			last = vc;
			continue;
		}
		if (last->action != vc->action || strcmp(last->src, vc->src) != 0)
			break;

		if (last->dst && strcmp(last->dst, vc->dst) != 0)
			break;

		if (last == cur) {
			kfree(cur);
			cur = 0;
			last = vc;
		} else
			REMOVE_ENTRY(p, vc);
	}
	return cur;
}

void vfs_changed(int act, const char* root, const char* src, const char* dst)
{
	remove_oldest();
	int extra_bytes = root ? strlen(root) : 0;
	size_t size = sizeof(vfs_change) + strlen(src) + extra_bytes + 1;
	if (dst != 0)
		size += strlen(dst) + extra_bytes + 1;
	char* p = kmalloc(size, GFP_ATOMIC);
	if (unlikely(p == 0)) {
		pr_info("vfs_changed_1: %s, src: %s, dst: %s, proc: %s[%d]\n",
			action_names[act], src, dst, current->comm, current->pid);
		return;
	}

	char* full_src = p + sizeof(vfs_change);
	if (extra_bytes > 0)
		strcpy(full_src, root);
	else
		full_src[0] = 0;
	strcat(full_src, src);
	char* full_dst = dst == 0 ? 0 : full_src + strlen(src) + 1 + extra_bytes;
	if (dst) {
		if (extra_bytes > 0)
			strcpy(full_dst, root);
		else
			full_dst[0] = 0;
		strcat(full_dst, dst);
	}
	vfs_change* vc = (vfs_change*)p;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 0, 0)
	do_gettimeofday(&vc->ts);
#else
	ktime_get_real_ts64(&vc->ts);
#endif
	vc->size = size;
	vc->action = act;
	vc->src = full_src;
	vc->dst = full_dst;
	spin_lock(&sl_changes);
	if (merge_actions) {
		vc = merge_action(vc);
		if (!vc) {
			spin_unlock(&sl_changes);
			return;
		}
	}
	list_add_tail(&vc->list, &vfs_changes);
	total_changes++;
	cur_changes++;
	total_memory += vc->size;
	spin_unlock(&sl_changes);

	int wvcc = atomic_read(&wait_vfs_changes_count);

	if (wvcc > 0 && cur_changes >= wvcc) {
		wake_up_interruptible(&wq_vfs_changes);
	}
}
