#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/dcache.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/namei.h>

#include "arg_extractor.h"
#include "vfs_change_consts.h"
#include "vfs_change.h"
#include "vfs_utils.h"

typedef struct __do_mount_args__ {
	char dir_name[NAME_MAX];
} do_mount_args;

#define DECL_CMN_KRP(fn) static struct kretprobe fn##_krp = {\
	.entry_handler	= on_##fn##_ent,\
	.handler		= on_##fn##_ret,\
	.data_size		= sizeof(fn##_args),\
	.maxactive		= 64,\
	.kp.symbol_name = ""#fn"",\
};

static DEFINE_SPINLOCK(sl_parts);
static LIST_HEAD(partitions);

static void get_root(char* root, unsigned char major, unsigned char minor)
{
	*root = 0;
	krp_partition* part;

	spin_lock(&sl_parts);
	list_for_each_entry(part, &partitions, list) {
		if (part->major == major && part->minor == minor) {
			strcpy(root, part->root);
			break;
		}
	}
	spin_unlock(&sl_parts);
}

static int is_mnt_ns_valid(void)
{
	static struct mnt_namespace* init_mnt_ns = 0;
	if (init_mnt_ns == 0) {
		if (current->nsproxy)
			init_mnt_ns = current->nsproxy->mnt_ns;
		return init_mnt_ns != 0;
	}

	if (current->nsproxy && current->nsproxy->mnt_ns != init_mnt_ns)
		return 0;

	return 1;
}

static int on_do_mount_ent(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	// const char*, const char __user*, ...
	do_mount_args* args = (do_mount_args*)ri->data;
	if (!is_mnt_ns_valid()) {
		args->dir_name[0] = 0;
		return 1;
	}

	const char __user* dir_name = (const char __user*)get_arg(regs, 2);
	if (unlikely(strncpy_from_user(args->dir_name, dir_name, NAME_MAX) < 0)) {
		args->dir_name[0] = 0;
		return 1;
	}

	if (is_special_mp(args->dir_name)) {
		args->dir_name[0] = 0;
		return 1;
	}

	//const char* dev_name = (const char*)get_arg(regs, 1);
	//pr_info("do_mount_ent dev: %s, dir: %s\n", dev_name, args->dir_name);
	return 0;
}

static void add_partition(const char* dir_name, int major, int minor)
{
	krp_partition *part = kmalloc(sizeof(krp_partition) + strlen(dir_name) + 1, GFP_ATOMIC);
	if (unlikely(part == 0)) {
		pr_err("kmalloc failed and thus cant add %s [%d, %d] to partitions\n",
			dir_name, major, minor);
		return;
	}

	part->major = major;
	part->minor = minor;
	strcpy(part->root, dir_name);
	pr_info("partition %s [%d, %d] added, comm[%d]: %s\n",
		part->root, major, minor, current->pid, current->comm);
	list_add_tail(&part->list, &partitions);
}

static int get_major_minor(const char* dir_name, unsigned char* major, unsigned char* minor)
{
	struct path path;
	if (kern_path(dir_name, LOOKUP_FOLLOW, &path))
		return 1;
	*major = MAJOR(path.dentry->d_sb->s_dev);
	*minor = MINOR(path.dentry->d_sb->s_dev);
	path_put(&path);
	return 0;
}

static int on_do_mount_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	do_mount_args* args = (do_mount_args*)ri->data;
	if (args == 0 || args->dir_name[0] == 0)
		return 0;

	unsigned long retval = regs_return_value(regs);
	if (retval != 0)
		return 0;

	unsigned char major, minor;
	if (get_major_minor(args->dir_name, &major, &minor)) {
		pr_err("get_mj_mn failed for %s\n", args->dir_name);
		return 0;
	}

	char root[NAME_MAX];
	get_root(root, major, minor);
	if (*root != 0 && strcmp(root, args->dir_name) == 0)
		return 0;

	spin_lock(&sl_parts);
	add_partition(args->dir_name, major, minor);
	spin_unlock(&sl_parts);
	return 0;
}

typedef struct __ksys_umount_args__ {
	char dir_name[NAME_MAX];
	unsigned char major, minor;
} ksys_umount_args;

static int on_ksys_umount_ent(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	ksys_umount_args* args = (ksys_umount_args*)ri->data;
	if (!is_mnt_ns_valid()) {
		args->dir_name[0] = 0;
		return 1;
	}

	//char __user* dir_name, int flags
	const char __user* dir_name = (const char __user*)get_arg(regs, 1);
	if (unlikely(strncpy_from_user(args->dir_name, dir_name, sizeof(args->dir_name)) < 0)) {
		args->dir_name[0] = 0;
		return 1;
	}

	// we must get all the info before umount, otherwise, they will be lost after umount returns
	if (is_special_mp(args->dir_name)) {
		args->dir_name[0] = 0;
		return 1;
	}

	if (get_major_minor(args->dir_name, &args->major, &args->minor)) {
		args->dir_name[0] = 0;
		return 1;
	}

	pr_info("ksys_umount: %s, %d, %d\n", args->dir_name, args->major, args->minor);
	return 0;
}

static void drop_partition(ksys_umount_args* args)
{
	struct list_head *p, *next;
	list_for_each_safe(p, next, &partitions) {
		krp_partition* part = list_entry(p, krp_partition, list);
		if (part->major != args->major || part->minor != args->minor ||
			strcmp(part->root, args->dir_name))
			continue;

		pr_info("partition %s [%d, %d] umounted\n", part->root, part->major, part->minor);
		list_del(p);
		kfree(part);
		break;
	}
}

static int on_ksys_umount_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	ksys_umount_args* args = (ksys_umount_args*)ri->data;
	if (args == 0 || args->dir_name[0] == 0)
		return 0;

	unsigned long retval = regs_return_value(regs);
	if (retval != 0)
		return 0;

	spin_lock(&sl_parts);
	drop_partition(args);
	spin_unlock(&sl_parts);
	return 0;
}

DECL_CMN_KRP(do_mount);
DECL_CMN_KRP(ksys_umount);

typedef struct __vfs_op_args__ {
	unsigned char major, minor;
	char* path;
	char buf[PATH_MAX];
} vfs_op_args, vfs_link_args;

#define DECL_VFS_KRP(fn, act) static int on_##fn##_ret(struct kretprobe_instance *ri, struct pt_regs *regs)\
{\
	return common_vfs_ret(ri, regs, act);\
}\
\
static struct kretprobe fn##_krp = {\
	.entry_handler	= on_vfs_op_ent,\
	.handler		= on_##fn##_ret,\
	.data_size		= sizeof(vfs_op_args),\
	.maxactive		= 64,\
	.kp.symbol_name = ""#fn"",\
};

static int common_vfs_ent(vfs_op_args* args, struct dentry* de)
{
	args->path = 0;
	if (de == 0 || de->d_sb == 0)
		return 1;

	args->major = MAJOR(de->d_sb->s_dev);
	args->minor = MINOR(de->d_sb->s_dev);
	char *path = dentry_path_raw(de, args->buf, sizeof(args->buf));
	if (IS_ERR(path))
		return 1;

	args->path = path;
	return 0;
}

static int on_vfs_op_ent(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	// vfs-create: struct inode*, struct dentry*, umode_t, bool
	// vfs-unlink: struct inode*, struct dentry*, struct inode**
	// vfs-mkdir: struct inode*, struct dentry*, umode_t
	// vfs-rmdir: struct inode*, struct dentry*
	// vfs-symlink: struct inode*, struct dentry*, const char*
	// security-inode-create: struct inode*, struct dentry*, umode_t
	struct dentry* de = (struct dentry*)get_arg(regs, 2);
	return common_vfs_ent((vfs_op_args *)ri->data, de);
}

static int common_vfs_ret(struct kretprobe_instance *ri, struct pt_regs *regs, int action)
{
	unsigned long retval = regs_return_value(regs);
	if (retval != 0)
		return 0;

	vfs_op_args *args = (vfs_op_args *)ri->data;
	if (args == 0 || args->path == 0) {
		pr_info("action %d args->path null? in proc[%d]: %s\n", action, current->pid, current->comm);
		return 0;
	}

	char root[NAME_MAX];
	get_root(root, args->major, args->minor);
	if (*root == 0)
		return 0;

	vfs_changed(action, strlen(root) == 1 ? 0 : root, args->path, 0);
	return 0;
}

DECL_VFS_KRP(vfs_create, ACT_NEW_FILE);
DECL_VFS_KRP(vfs_unlink, ACT_DEL_FILE);
DECL_VFS_KRP(vfs_mkdir, ACT_NEW_FOLDER);
DECL_VFS_KRP(vfs_rmdir, ACT_DEL_FOLDER);
DECL_VFS_KRP(vfs_symlink, ACT_NEW_SYMLINK);
// newer kernel rarely calls vfs_create... so we have to rely on the not-so-reliable security_inode_create
DECL_VFS_KRP(security_inode_create, ACT_NEW_FILE);

static int on_vfs_link_ent(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	// vfs-link: struct dentry*, struct inode*, struct dentry*, struct inode**
	struct dentry* de = (struct dentry*)get_arg(regs, 1);
	return common_vfs_ent((vfs_op_args*)ri->data, de);
}

static int on_vfs_link_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	return common_vfs_ret(ri, regs, ACT_NEW_LINK);
}

DECL_CMN_KRP(vfs_link);

typedef struct __vfs_rename_args__ {
	char* old_path;
	char* new_path;
	char buf[PATH_MAX];
	unsigned char major, minor, is_dir;
} vfs_rename_args;

static int on_vfs_rename_ent(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	// vfs-rename: struct inode*, struct dentry*, struct inode*, struct dentry*, struct inode**, unsigned int
	vfs_rename_args *args = (vfs_rename_args *)ri->data;

	struct dentry* de_old = (struct dentry*)get_arg(regs, 2);
	struct dentry* de_new = (struct dentry*)get_arg(regs, 4);
	if (de_old == 0 || de_old->d_sb == 0 || de_new == 0) {
		args->old_path = 0;
		return 1;
	}
	args->major = MAJOR(de_old->d_sb->s_dev);
	args->minor = MINOR(de_old->d_sb->s_dev);
	args->old_path = dentry_path_raw(de_old, args->buf, sizeof(args->buf));
	if (IS_ERR(args->old_path)) {
		args->old_path = 0;
		return 1;
	}
	args->new_path = dentry_path_raw(de_new, args->buf, sizeof(args->buf)-strlen(args->old_path)-1);
	if (IS_ERR(args->new_path)) {
		args->old_path = 0;
		return 1;
	}
	args->is_dir = d_is_dir(de_old);
	return 0;
}

static int on_vfs_rename_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	unsigned long retval = regs_return_value(regs);
	if (retval != 0)
		return 0;

	vfs_rename_args *args = (vfs_rename_args *)ri->data;
	if (args == 0 || args->old_path == 0)
		return 0;

	char root[NAME_MAX];
	get_root(root, args->major, args->minor);
	if (*root != 0)
		vfs_changed(args->is_dir ? ACT_RENAME_FOLDER : ACT_RENAME_FILE,
			strlen(root) == 1 ? 0 : root, args->old_path, args->new_path);
	return 0;
}

DECL_CMN_KRP(vfs_rename);

static struct kretprobe* vfs_krps[] = {&do_mount_krp, &vfs_create_krp, &vfs_unlink_krp,
	&vfs_mkdir_krp, &vfs_rmdir_krp, &vfs_symlink_krp, &vfs_link_krp, &vfs_rename_krp,
	&ksys_umount_krp, &security_inode_create_krp
};

static void __init init_mounts_info(void)
{
	if (!is_mnt_ns_valid())
		return;

	char *cur_path = 0, *path_buf = kmalloc(PATH_MAX, GFP_KERNEL);
	if (path_buf)
		cur_path = file_path(get_task_exe_file(current), path_buf, PATH_MAX);
	int size = 0;
	char *cmdline = read_file_content("/proc/self/cmdline", &size);
	if (cmdline)
	for (int i = 0; i < size; i++) {
		if (cmdline[i] != 0) continue;
		if (cmdline[i+1] == 0) break;
		cmdline[i] = ' ';
	}

	size = 0;
	char* buf = read_file_content("/proc/self/mountinfo", &size);

	int parts_count = 0;

	// __init section doesnt need lock
	krp_partition* part;
	parse_mounts_info(buf, &partitions);
	list_for_each_entry(part, &partitions, list) {
		parts_count++;
		pr_info("mp: %s, major: %d, minor: %d\n", part->root, part->major, part->minor);
	}

	if (buf)
		kfree(buf);

	pr_info("partition count: %d, comm[%d]: %s, path: %s, cmdline: %s\n",
		parts_count, current->pid, current->comm, cur_path, cmdline);
	if (path_buf)
		kfree(path_buf);
	if (cmdline)
		kfree(cmdline);
}

int __init init_module()
{
	init_mounts_info();
	int ret = init_vfs_changes();
	if (ret != 0) {
		pr_err("init_vfs_changes failed, returned %d\n", ret);
		return ret;
	}

	ret = register_kretprobes(vfs_krps, sizeof(vfs_krps)/sizeof(void *));
	if (ret < 0) {
		pr_err("register_kretprobes failed, returned %d\n", ret);
		cleanup_vfs_changes();
		return ret;
	}
	pr_info("register_kretprobes %ld ok\n", sizeof(vfs_krps)/sizeof(void *));
	return 0;
}

void __exit cleanup_module()
{
	unregister_kretprobes(vfs_krps, sizeof(vfs_krps)/sizeof(void *));
	cleanup_vfs_changes();
	pr_info("unregister_kretprobes %ld ok\n", sizeof(vfs_krps)/sizeof(void *));
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Raphael");
MODULE_DESCRIPTION("VFS change monitor");
