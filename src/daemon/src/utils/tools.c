// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#define _GNU_SOURCE
#include "utils/tools.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <glib.h>
#include <libmount.h>
#include <fcntl.h>
#include <errno.h>

char *format_time(int64_t modify_time)
{
    // 将 UNIX 时间戳转换为 GDateTime 对象（本地时区）
    g_autoptr(GDateTime) datetime = g_date_time_new_from_unix_local(modify_time);
    if (!datetime) {
        return g_strdup("Invalid time");
    }

    // 格式化时间字符串
    return g_date_time_format(datetime, "%Y/%m/%d %H:%M:%S");
}

char *format_size(int64_t size)
{
    if (size < 1000)
        return g_strdup_printf("%" G_GINT64_FORMAT " B", size);

    return g_format_size(size);
}


// 获取设备根目录挂载路径
char *get_device_root_mountpoint(const dev_t target_dev)
{
    char *mountpoint = NULL;

    // 初始化 libmount 上下文
    struct libmnt_table *tb = mnt_new_table();
    if (!tb)
        return NULL;

    // 加载内核挂载信息
    if (mnt_table_parse_mtab(tb, "/proc/self/mountinfo") != 0) {
        mnt_free_table(tb);
        return NULL;
    }

    struct libmnt_iter *itr = mnt_new_iter(MNT_ITER_FORWARD);
    struct libmnt_fs *fs;

    // 遍历所有挂载项
    while (mnt_table_next_fs(tb, itr, &fs) == 0) {
        if (mnt_fs_get_devno(fs) != target_dev)
            continue;
        if (g_strcmp0(mnt_fs_get_root(fs), "/") == 0) {
            mountpoint = g_strdup(mnt_fs_get_target(fs));
            break;
        }
    }

    // 清理 libmount 资源
    mnt_free_iter(itr);
    mnt_free_table(tb);

    return mountpoint;
}

struct file_handle_with_bytes {
    struct file_handle fh;
    char bytes[MAX_HANDLE_SZ];
};

int get_mount_id(const char *path, int *mount_id)
{
    struct file_handle_with_bytes fh;
    fh.fh.handle_bytes = MAX_HANDLE_SZ;

    return name_to_handle_at(AT_FDCWD, path, &(fh.fh), mount_id, 0);
}

char* bfs_search(const char *start_path, int target_mount_id, ino_t target_ino)
{
    char *ret = NULL;
    // 保存了同一挂载中的目录
    GQueue *queue = g_queue_new();
    g_queue_push_tail(queue, g_strdup(start_path));

    while (!g_queue_is_empty(queue)) {
        const gchar *entry = NULL;
        g_autofree char *current_path = g_queue_pop_head(queue);
        GDir *dir = g_dir_open(current_path, 0, NULL);
        if (!dir) continue;

        while ((entry = g_dir_read_name(dir))) {
            g_autofree char *full_path = g_build_filename(current_path, entry, NULL);
            struct stat entry_st;

            // 队列仅保存同一挂载中的目录, 且忽略绑定挂载
            if (lstat(full_path, &entry_st) != 0 ||
                !S_ISDIR(entry_st.st_mode))
                continue;

            int mount_id;
            if (get_mount_id(full_path, &mount_id) != 0)
                continue;

            if (mount_id == target_mount_id) {
                // 找到匹配项立即返回
                if (entry_st.st_ino == target_ino) {
                    ret = g_steal_pointer(&full_path);
                    break;
                } else {
                    g_queue_push_tail(queue, g_steal_pointer(&full_path));
                }
            }
        }
        g_dir_close(dir);
        if (ret)
            break;
    }

    // 自动清理队列内存
    g_queue_free_full(queue, g_free);
    return ret;
}

// 在形参 mount_dir 目录下查找一个目录, 该目录的设备号和 inode 号与形参 target_dir 目录的相同
// 查找目录时, 不跟随符号链接，不跨越挂载; 使用广度优先搜索
char* find_dir_full_path(const char *mount_dir, const char *target_dir)
{
    struct stat target_st, mount_st;

    // 获取目标目录的元数据
    if (lstat(target_dir, &target_st) != 0 ||
        lstat(mount_dir, &mount_st) != 0) {
        return NULL;
    }

    // 两个目录位于不同挂载, 返回
    if (target_st.st_dev != mount_st.st_dev)
        return NULL;

    // 找到结果, 返回
    if (target_st.st_ino == mount_st.st_ino)
        return g_strdup(mount_dir);

    int mount_id;
    if (get_mount_id(mount_dir, &mount_id) != 0)
        return NULL;

    return bfs_search(mount_dir, mount_id, target_st.st_ino);
}

// 获取一个路径的的完整路径
// 完整路径表示为 设备根目录挂载路径 + 设备内路径
// 例如: /home -> /persist/home/
char *get_full_path(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0)
        return NULL;

    g_autofree char *mountpoint = get_device_root_mountpoint(st.st_dev);
    if (!mountpoint)
        return NULL;

    char *full_path = find_dir_full_path(mountpoint, path);
    return full_path;
}

// return 1 if env is not set
unsigned int get_thread_pool_size_from_env(unsigned int default_size)
{
    const char *env_thread_pool_size = getenv("ANYTHING_THREAD_POOL_SIZE");
    if (!env_thread_pool_size)
        return default_size;

    char *end;
    errno = 0;
    unsigned int size = g_ascii_strtoull(env_thread_pool_size, &end, 10);
    if (errno != 0 || *end != '\0')
        return default_size;

    if (size < 1)
        size = 1;
    if (size > 128)
        size = 128;

    return size;
}
