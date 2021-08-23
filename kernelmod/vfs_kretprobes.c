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
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
#include <linux/cdev.h>
#include <linux/device.h>
#define CHRDEV_NAME      "driver_set_info"
#define CLASS_NAME       "class_set_info"    /* 表示在/system/class目录下创建的设备类别目录 */
#define DEVICE_NAME      "driver_set_info"   /* 在/dev/目录和/sys/class/class_wrbuff目录下分别创建设备文件driver_wrbuff */
static dev_t         devt_wrbuffer;  /* alloc_chrdev_region函数向内核申请下来的设备号 */
static struct cdev  *cdev_wrbuffer;  /* 注册到驱动的字符设备 */
static struct class *class_wrbuffer; /* 字符设备创建的设备节点 */
int my_open(struct inode *inode, struct file *file); /* 字符设备打开函数 */
int my_release(struct inode *inode, struct file *file); /* 字符设备释放函数 */
ssize_t my_read(struct file *file, char __user *user, size_t t, loff_t *f); /* 字符设备读函数 */
ssize_t my_write(struct file *file, const char __user *user, size_t t, loff_t *f); /* 字符设备写函数 */

int init_vfs_flag = 0; /* vfs初始化函数 */
struct file_operations fops_chrdriver = {
open: my_open,
release: my_release,
read: my_read,
write: my_write,
};

#endif

#include "arg_extractor.h"
#include "vfs_change_consts.h"
#include "vfs_change.h"
#include "vfs_utils.h"

typedef struct __do_mount_args__ {
    char dir_name[NAME_MAX];
    char dir_type[NAME_MAX];
} do_mount_args;

#define _DECL_CMN_KRP(fn, symbol) static struct kretprobe fn##_krp = {\
        .entry_handler  = on_##fn##_ent,\
                          .handler        = on_##fn##_ret,\
                                            .data_size      = sizeof(fn##_args),\
                                                              .maxactive      = 64,\
                                                                                .kp.symbol_name = ""#symbol"",\
    };

#define DECL_CMN_KRP(fn) _DECL_CMN_KRP(fn, fn)

static DEFINE_SPINLOCK(sl_parts);
static LIST_HEAD(partitions);


static void get_root(char *root, unsigned char major, unsigned char minor)
{
    *root = 0;
    krp_partition *part;

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
    static struct mnt_namespace *init_mnt_ns = 0;
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
    do_mount_args *args = (do_mount_args *)ri->data;
    if (!is_mnt_ns_valid()) {
        args->dir_name[0] = 0;
        return 1;
    }

    /* 存储mount的时候的type值，在后面on_do_mount_ret 进行使用 */
    const char __user *type_name = (const char __user *)get_arg(regs, 3);
    /* 解决bug69979，升级过程中type_name为空导致内核崩溃 */
    if (type_name == NULL) {
        pr_err("on_do_mount_ent type_name is null\n");
        args->dir_name[0] = 0;
        return 1;
    }
    if (strlen(type_name) <= 0 || strlen(type_name) >= sizeof(args->dir_type)) {
        pr_err("on_do_mount_ent type is empty or too long\n");
        args->dir_name[0] = 0;
        return 1;
    } else {
        if (unlikely(strncpy(args->dir_type, type_name, sizeof(args->dir_type)-1) < 0)) {
            pr_err("on_do_mount_ent strncpy failed\n");
            args->dir_name[0] = 0;
            return 1;
        }
        args->dir_type[sizeof(args->dir_type)-1] = 0;
    }

    const char __user *dir_name = (const char __user *)get_arg(regs, 2);
    if (dir_name == NULL) {
        pr_err("on_do_mount_ent dir_name is null\n");
        args->dir_name[0] = 0;
        return 1;
    }
    if (unlikely(strncpy_from_user(args->dir_name, dir_name, NAME_MAX) < 0)) {
        args->dir_name[0] = 0;
        return 1;
    }

    if (is_special_mp(args->dir_name)) {
        args->dir_name[0] = 0;
        return 1;
    }

    return 0;
}

static void add_partition(const char *dir_name, int major, int minor)
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

static int get_major_minor(const char *dir_name, unsigned char *major, unsigned char *minor)
{
    struct path path;
    if (kern_path(dir_name, LOOKUP_FOLLOW, &path))
        return 1;
    *major = MAJOR(path.dentry->d_sb->s_dev);
    *minor = MINOR(path.dentry->d_sb->s_dev);
    path_put(&path);
    return 0;
}

typedef struct __do_mount_work_stuct__ {
    struct work_struct work;
    char dir_name[NAME_MAX];
} do_mount_work_stuct;

void do_mount_work_handle(struct work_struct *work)
{
    do_mount_work_stuct *do_mount_work = (do_mount_work_stuct*)work;

    unsigned char major, minor;
    if (get_major_minor(do_mount_work->dir_name, &major, &minor)) {
        pr_err("do_mount_work_handle get_major_minor failed for %s\n", do_mount_work->dir_name);
        kfree(do_mount_work);
        return;
    }

    char root[NAME_MAX];
    get_root(root, major, minor);
    if (*root != 0 && strcmp(root, do_mount_work->dir_name) == 0) {
        pr_info("do_mount_work_handle partition(%s) alread exist\n", do_mount_work->dir_name);
        kfree(do_mount_work);
        return;
    }

    spin_lock(&sl_parts);
    add_partition(do_mount_work->dir_name, major, minor);
    spin_unlock(&sl_parts);

    pr_info("do_mount_work_handle quit for %s\n", do_mount_work->dir_name);
    kfree(do_mount_work);
}

static int on_do_mount_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    do_mount_args *args = (do_mount_args *)ri->data;
    if (args == 0 || args->dir_name[0] == 0)
        return 0;

    /* 解决ntfs文件系统挂载的时候开启了auditd以后，mount的时候会导致系统卡死问题 */
    /* 对挂载的ntfs等fuse类型的文件系统进行判断，如果是这类文件系统则退出 */
    if (args->dir_type[0] == 0) {
        pr_err("dir_type is null");
        return 0;
    }

    if (strstr(args->dir_type, "fuse")) {
        pr_info("This is the fuse filesytem，so return\n");
        return 0;
    }

    unsigned long retval = regs_return_value(regs);
    if (retval != 0)
        return 0;
    /*增加工作队列处理*/
    do_mount_work_stuct *do_mount_work = kmalloc(sizeof(do_mount_work_stuct), GFP_ATOMIC);
    if (unlikely(0 == do_mount_work)) {
        pr_err("do_mount_work kmalloc failed\n");
        return 0;
    }

    if (unlikely(strncpy(do_mount_work->dir_name, args->dir_name, sizeof(do_mount_work->dir_name)-1) < 0)) {
        pr_err("on_do_mount_ret: strncpy failed\n");
        kfree(do_mount_work);
        return 0;
    }
    do_mount_work->dir_name[sizeof(do_mount_work->dir_name)]= 0;
    INIT_WORK(&(do_mount_work->work), do_mount_work_handle);
    schedule_work(&do_mount_work->work);

    pr_info("on_do_mount_ret: %s\n", args->dir_name);
    return 0;
}

typedef struct __sys_umount_args__ {
    char dir_name[NAME_MAX];
    unsigned char major, minor;
} sys_umount_args;

static int on_sys_umount_ent(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    sys_umount_args *args = (sys_umount_args *)ri->data;
    if (!is_mnt_ns_valid()) {
        args->dir_name[0] = 0;
        return 1;
    }

    //char __user* dir_name, int flags
    const char __user *dir_name = (const char __user *)get_arg(regs, 1);
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

    pr_info("sys_umount: %s, %d, %d\n", args->dir_name, args->major, args->minor);
    return 0;
}

typedef struct __sys_umount_work_stuct__ {
    struct work_struct work;
    sys_umount_args args;
} sys_umount_work_stuct;

void sys_umount_work_handle(struct work_struct *work)
{
    sys_umount_work_stuct *sys_umount_work = (sys_umount_work_stuct*)work;

    spin_lock(&sl_parts);
    struct list_head *p, *next;
    struct list_head *target = NULL;
    list_for_each_safe(p, next, &partitions) {
        krp_partition *part = list_entry(p, krp_partition, list);
        if (strcmp(part->root, sys_umount_work->args.dir_name))
            continue;
        target = p;
    }
    if (target)
    {
        krp_partition *part = list_entry(target, krp_partition, list);
        pr_info("partition %s [%d, %d] umounted\n", part->root, part->major, part->minor);
        list_del(target);
        kfree(part);
    }
    spin_unlock(&sl_parts);
    pr_info("sys_umount_work_handle quit for %s\n", sys_umount_work->args.dir_name);
    kfree(sys_umount_work);
}

static int on_sys_umount_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    sys_umount_args *args = (sys_umount_args *)ri->data;
    if (args == 0 || args->dir_name[0] == 0)
        return 0;

    unsigned long retval = regs_return_value(regs);
    if (retval != 0)
        return 0;

    /*增加工作队列处理方式*/
    sys_umount_work_stuct *sys_umount_work = kmalloc(sizeof(sys_umount_work_stuct), GFP_ATOMIC);
    if (unlikely(0 == sys_umount_work)) {
        pr_err("on_sys_umount_ret kmalloc failed\n");
        return 0;
    }

    if (unlikely(strncpy(sys_umount_work->args.dir_name, args->dir_name, sizeof(sys_umount_work->args.dir_name)-1) < 0)) {
        pr_err("on_sys_umount_ret strncpy failed\n");
        kfree(sys_umount_work);
        return 0;
    }
    sys_umount_work->args.dir_name[sizeof(sys_umount_work->args.dir_name)] = 0;
    sys_umount_work->args.major = args->major;
    sys_umount_work->args.minor = args->minor;

    INIT_WORK(&(sys_umount_work->work), sys_umount_work_handle);
    schedule_work(&sys_umount_work->work);

    pr_info("on_sys_umount_ret: %s\n", args->dir_name);
    return 0;
}

DECL_CMN_KRP(do_mount);
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 17, 0)
DECL_CMN_KRP(sys_umount);
#else
_DECL_CMN_KRP(sys_umount, ksys_umount);
#endif

typedef struct __vfs_op_args__ {
    unsigned char major, minor;
    char *path;
    char buf[PATH_MAX];
} vfs_op_args, vfs_link_args;

#define DECL_VFS_KRP(fn, act) static int on_##fn##_ret(struct kretprobe_instance *ri, struct pt_regs *regs)\
    {\
        return common_vfs_ret(ri, regs, act);\
    }\
    \
    static struct kretprobe fn##_krp = {\
        .entry_handler  = on_vfs_op_ent,\
                          .handler        = on_##fn##_ret,\
                                            .data_size      = sizeof(vfs_op_args),\
                                                              .maxactive      = 64,\
                                                                                .kp.symbol_name = ""#fn"",\
    };

static int common_vfs_ent(vfs_op_args *args, struct dentry *de)
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
    struct dentry *de = (struct dentry *)get_arg(regs, 2);
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
    struct dentry *de = (struct dentry *)get_arg(regs, 1);
    return common_vfs_ent((vfs_op_args *)ri->data, de);
}

static int on_vfs_link_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    return common_vfs_ret(ri, regs, ACT_NEW_LINK);
}

DECL_CMN_KRP(vfs_link);

typedef struct __vfs_rename_args__ {
    char *old_path;
    char *new_path;
    char buf[PATH_MAX];
    unsigned char major, minor, is_dir;
} vfs_rename_args;

static int on_vfs_rename_ent(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    // vfs-rename: struct inode*, struct dentry*, struct inode*, struct dentry*, struct inode**, unsigned int
    vfs_rename_args *args = (vfs_rename_args *)ri->data;

    struct dentry *de_old = (struct dentry *)get_arg(regs, 2);
    struct dentry *de_new = (struct dentry *)get_arg(regs, 4);
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
    args->new_path = dentry_path_raw(de_new, args->buf, sizeof(args->buf) - strlen(args->old_path) - 1);
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

static struct kretprobe *vfs_krps[] = {&do_mount_krp, &vfs_create_krp, &vfs_unlink_krp,
           &vfs_mkdir_krp, &vfs_rmdir_krp, &vfs_symlink_krp, &vfs_link_krp, &vfs_rename_krp,
           &sys_umount_krp, &security_inode_create_krp
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
/* 打开 */
int my_open(struct inode *inode, struct file *file)
{
    if (!try_module_get(THIS_MODULE)) {
        return -ENODEV;
    }
    return 0;
}
/* 关闭 */
int my_release(struct inode *inode, struct file *file)
{
    module_put(THIS_MODULE);
    return 0;
}
/* 读设备里的信息 */
ssize_t my_read(struct file *file, char __user *user, size_t t, loff_t *f)
{
    return 0;
}
/* 向设备里写信息 */
ssize_t my_write(struct file *file, const char __user *user, size_t t, loff_t *f)
{
    char buff[4096 * 2] = {0};
    if (copy_from_user(buff, user, t)) {
        return -1;
    } else {
        f += t;
        buff[t] = 0;
        if (!is_mnt_ns_valid()) {
            pr_err("is_mnt_ns_valid failed\n");
            return -1;
        }
        int size = 0;

        size = 0;
        int parts_count = 0;
        if (init_vfs_flag) {
            return 1;
        }
        /* init vfs_change */
        krp_partition *part;
        unsigned int major, minor;
        char mp[NAME_MAX], *line = buff;
        while (sscanf(line, "%*d %*d %d:%d %*s %250s %*s %*s %*s %*s %*s %*s\n", &major, &minor, mp) == 3) {
            line = strchr(line, '\n') + 1;

            if (is_special_mp(mp))
                continue;

            krp_partition *part = kmalloc(sizeof(krp_partition) + strlen(mp) + 1, GFP_KERNEL);
            if (unlikely(part == 0)) {
                pr_err("krp-partition kmalloc failed for %s\n", mp);
                continue;
            }
            part->major = major;
            part->minor = minor;
            strcpy(part->root, mp);
            list_add_tail(&part->list, &partitions);
        }

        list_for_each_entry(part, &partitions, list) {
            parts_count++;
            pr_info("mp: %s, major: %d, minor: %d\n", part->root, part->major, part->minor);
        }

        int ret = register_kretprobes(vfs_krps, sizeof(vfs_krps) / sizeof(void *));
        if (ret < 0) {
            pr_err("register_kretprobes failed, returned %d\n", ret);
            cleanup_vfs_changes();
            return -1;
        }
        init_vfs_flag = 1;
        pr_info("register_kretprobes %ld ok\n", sizeof(vfs_krps) / sizeof(void *));
    }
    return 0;
}
#else
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
            if (cmdline[i + 1] == 0) break;
            cmdline[i] = ' ';
        }

    size = 0;
    char *buf = read_file_content("/proc/self/mountinfo", &size);

    int parts_count = 0;

    /* get mounts info */
    krp_partition *part;
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
#endif
int __init init_module()
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
    int ret = 0;
    init_vfs_flag = 0;

    /* 申请设备号 */
    ret = alloc_chrdev_region(&devt_wrbuffer, 0, 1, CHRDEV_NAME);
    if (ret) {
        pr_err("alloc char driver error!\n");
        return ret;
    }

    /* 注册字符设备 */
    cdev_wrbuffer = cdev_alloc();
    if (!cdev_wrbuffer) {
        return ENOMEM;
    }
    cdev_init(cdev_wrbuffer, &fops_chrdriver);
    cdev_wrbuffer->owner = THIS_MODULE;
    ret = cdev_add(cdev_wrbuffer, devt_wrbuffer, 1);
    if (ret) {
        pr_err("cdev create error!\n");
        unregister_chrdev_region(devt_wrbuffer, 1);
        cdev_del(cdev_wrbuffer);
        return ret;
    }

    /* 创建设备节点 */
    class_wrbuffer = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(class_wrbuffer)) {
        cdev_del(cdev_wrbuffer);
        return ENOMEM;
    }
    struct device *dev=device_create(class_wrbuffer, NULL, devt_wrbuffer, NULL, DEVICE_NAME);
    if (IS_ERR(dev)) {
        /* 删除设备节点 */
        device_destroy(class_wrbuffer, devt_wrbuffer);
        class_destroy(class_wrbuffer);
        return ENOMEM;
    }

    ret = init_vfs_changes();
    if (ret != 0) {
        pr_err("init_vfs_changes failed, returned %d\n", ret);
        return ret;
    }
#else
    init_mounts_info();
    int ret = init_vfs_changes();
    if (ret != 0) {
        pr_err("init_vfs_changes failed, returned %d\n", ret);
        return ret;
    }

    ret = register_kretprobes(vfs_krps, sizeof(vfs_krps) / sizeof(void *));
    if (ret < 0) {
        pr_err("register_kretprobes failed, returned %d\n", ret);
        cleanup_vfs_changes();
        return ret;
    }
#endif
    pr_info("register_kretprobes %ld ok\n", sizeof(vfs_krps) / sizeof(void *));
    return 0;
}

void __exit cleanup_module()
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
    /* 删除设备节点 */
    device_destroy(class_wrbuffer, devt_wrbuffer);
    class_destroy(class_wrbuffer);
    /* 取消字符设备注册 */
    cdev_del(cdev_wrbuffer);
    /* 释放设备号 */
    unregister_chrdev_region(devt_wrbuffer, 1);
#endif
    unregister_kretprobes(vfs_krps, sizeof(vfs_krps) / sizeof(void *));
    cleanup_vfs_changes();
    pr_info("unregister_kretprobes %ld ok\n", sizeof(vfs_krps) / sizeof(void *));
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Raphael");
MODULE_DESCRIPTION("VFS change monitor");
