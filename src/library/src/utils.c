// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unistd.h>
#include <string.h>
#include <iconv.h>
#include <wchar.h>
#include <libmount.h>

#include "utils.h"

#define IO_BLK_SIZE		(1<<14)

__attribute__((visibility("default"))) int utf8_to_wchar_t(char* input, wchar_t* output, size_t output_bytes)
{
	char *pinput = input;
	char *poutput = (char *)output;
	size_t inbytes = strlen(input), outbytes = output_bytes;

	iconv_t icd = iconv_open("WCHAR_T", "UTF-8");
	size_t chars = iconv(icd, &pinput, &inbytes, &poutput, &outbytes);
	iconv_close(icd);

	if (chars == -1)
		return 1;

	wchar_t *p = (wchar_t*)poutput;
	*p = 0;
	return 0;
}

__attribute__((visibility("default"))) int wchar_t_to_utf8(const wchar_t* input, char* output, size_t output_bytes)
{
	char *pinput = (char *)input;
	char *poutput = output;
	size_t inbytes = wcslen(input)*sizeof(wchar_t), outbytes = output_bytes;

	iconv_t icd = iconv_open("UTF-8", "WCHAR_T");
	size_t chars = iconv(icd, &pinput, &inbytes, &poutput, &outbytes);
	iconv_close(icd);

	if (chars == -1)
		return 1;

	*poutput = 0;
	return 0;
}

int read_file(int fd, char* head, uint32_t size)
{
	uint32_t left = size;
	char* p = head;
	while (left > 0) {
		uint32_t to_read = left > IO_BLK_SIZE ? IO_BLK_SIZE : left;
		if (read(fd, p, to_read) != to_read)
			return 1;
		p += to_read;
		left -= to_read;
	}
	return 0;
}

int write_file(int fd, char* head, uint32_t size)
{
	uint32_t left = size;
	char* p = head;
	while (left > 0) {
		uint32_t to_write = left > IO_BLK_SIZE ? IO_BLK_SIZE : left;
		if (write(fd, p, to_write) != to_write)
			return 1;
		p += to_write;
		left -= to_write;
	}
	return 0;
}

// 将搜索路径转换为挂载路径

GList *get_bind_mountpoints(const dev_t target_dev)
{
    // 初始化 libmount 上下文
    struct libmnt_table *tb = mnt_new_table();
    if (!tb)
        return NULL;

    // 加载内核挂载信息
    if (mnt_table_parse_mtab(tb, "/proc/self/mountinfo") != 0) {
        mnt_free_table(tb);
        return NULL;
    }

    GList *mount_list = NULL;
    struct libmnt_iter *itr = mnt_new_iter(MNT_ITER_FORWARD);
    struct libmnt_fs *fs;

    // 遍历所有挂载项
    while (mnt_table_next_fs(tb, itr, &fs) == 0) {
        if (mnt_fs_get_devno(fs) != target_dev)
            continue;
        if (g_strcmp0(mnt_fs_get_root(fs), "/") == 0)
            continue;

        // 使用 GLib 内存分配并添加到链表
        gchar *path = g_strdup(mnt_fs_get_target(fs));
        if (path) {
            mount_list = g_list_prepend(mount_list, path);
        }
    }

    // 清理 libmount 资源
    mnt_free_iter(itr);
    mnt_free_table(tb);

    // 反转链表恢复原始顺序
    return g_list_reverse(mount_list);
}

gboolean is_mount_point(GList *mounts, const gchar *dir)
{
    GList *iter;

    for (iter = mounts; iter != NULL; iter = iter->next) {
        if (g_strcmp0(dir, (char*)iter->data) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

char* bfs_search(GList *mounts, const char *start_path, dev_t target_dev, ino_t target_ino)
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

        while (entry = g_dir_read_name(dir)) {
            g_autofree char *full_path = g_build_filename(current_path, entry, NULL);
            struct stat entry_st;

            // 队列仅保存同一挂载中的目录, 且忽略绑定挂载
            if (lstat(full_path, &entry_st) == 0 &&
                S_ISDIR(entry_st.st_mode) &&
                entry_st.st_dev == target_dev &&
                !is_mount_point(mounts, full_path)) {
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

// 将搜索路径转换为挂载路径
// 在形参 mount_dir 目录下查找一个目录, 该目录的设备号和 inode 号与形参 search_dir 目录的相同
// 查找目录时, 不跟随符号链接，不跨越挂载; 使用广度优先搜索
char* find_matching_dir(const char *mount_dir, const char *search_dir)
{
    struct stat search_st, mount_st;
    GList *mounts;
    char *ret;

    // 获取目标目录的元数据
    if (lstat(search_dir, &search_st) != 0 ||
        lstat(mount_dir, &mount_st) != 0) {
        return NULL;
    }

    // 两个目录位于不同挂载, 返回
    if (search_st.st_dev != mount_st.st_dev)
        return NULL;

    // 找到结果, 返回
    if (search_st.st_ino == mount_st.st_ino)
        return g_strdup(mount_dir);

    mounts = get_bind_mountpoints(mount_st.st_dev);
    ret = bfs_search(mounts, mount_dir, search_st.st_dev, search_st.st_ino);
    g_list_free_full(mounts, (GDestroyNotify)g_free);

    return ret;
}

static GHashTable *find_matching_dir_cache = NULL;

// 生成缓存键：mount_dir + "|" + search_dir 的规范路径
static char* generate_cache_key(const char *mount, const char *search) {
    g_autofree char *canon_mount = g_canonicalize_filename(mount, NULL);
    g_autofree char *canon_search = g_canonicalize_filename(search, NULL);
    return canon_mount && canon_search ?
        g_strdup_printf("%s|%s", canon_mount, canon_search) : NULL;
}

char* find_matching_dir_by_cache(const char *mount_dir, const char *search_dir)
{
    /* 初始化缓存 */
    if (!find_matching_dir_cache)
        find_matching_dir_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    /* 生成缓存键 */
    g_autofree char *cache_key = generate_cache_key(mount_dir, search_dir);
    if (!cache_key) return NULL;

    /* 检查缓存 */
    char *cached = g_hash_table_lookup(find_matching_dir_cache, cache_key);
    if (cached) {
        return g_strdup(cached);
    }

    /* 执行实际查找 */
    g_autofree char *ret = find_matching_dir(mount_dir, search_dir);

    /* 更新缓存 */
    if (ret)
        g_hash_table_insert(find_matching_dir_cache, g_steal_pointer(&cache_key), g_strdup(ret));

    return g_steal_pointer(&ret);
}
