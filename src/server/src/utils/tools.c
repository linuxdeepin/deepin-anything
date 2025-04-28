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

#define MAX_EXTENSION   10

int get_file_info(const char *file_path, const char **file_type, int64_t *modify_time, int64_t *file_size)
{
    static const char *file_type_maps[] = {
        "app",      ";desktop;",
        "archive",  ";7z;ace;arj;bz2;cab;gz;gzip;jar;r00;r01;r02;r03;r04;r05;r06;r07;r08;r09;"
                    ";r10;r11;r12;r13;r14;r15;r16;r17;r18;r19;r20;r21;r22;r23;r24;r25;r26;r27;"
                    ";r28;r29;rar;tar;tgz;z;zip;",
        "audio",    ";aac;ac3;aif;aifc;aiff;au;cda;dts;fla;flac;it;m1a;m2a;m3u;m4a;mid;midi;"
                    ";mka;mod;mp2;mp3;mpa;ogg;opus;ra;rmi;spc;rmi;snd;umx;voc;wav;wma;xm;ape;",
        "doc",      ";c;chm;cpp;csv;cxx;doc;docm;docx;dot;dotm;dotx;h;hpp;htm;html;hxx;ini;java;"
                    ";lua;mht;mhtml;ods;odt;odp;pdf;potx;potm;ppam;ppsm;ppsx;pps;ppt;pptm;pptx;rtf;"
                    ";sldm;sldx;thmx;txt;vsd;wpd;wps;wri;xlam;xls;xlsb;xlsm;xlsx;xltm;xltx;xml;latex;"
                    ";wpt;md;odg;dps;sh;xhtml;dhtml;shtm;shtml;json;css;yaml;bat;js;sql;uof;ofd;log;tex;",
        "pic",      ";ani;bmp;gif;ico;jpe;jpeg;jpg;pcx;png;psd;tga;tif;tiff;webp;wmf;svg;",
        "video",    ";3g2;3gp;3gp2;3gpp;amr;amv;asf;asx;avi;bdmv;bik;d2v;divx;drc;dsa;dsm;dss;dsv;"
                    ";evo;f4v;flc;fli;flic;flv;hdmov;ifo;ivf;m1v;m2p;m2t;m2ts;m2v;m4b;m4p;m4v;mkv;"
                    ";mp2v;mp4;mp4v;mpe;mpeg;mpg;mpls;mpv2;mpv4;mov;mts;ogm;ogv;pss;pva;qt;ram;"
                    ";ratdvd;rm;rmm;rmvb;roq;rpm;smil;smk;swf;tp;tpr;ts;vob;vp6;webm;wm;wmp;wmv;",
        NULL,
    };

    *file_type = "other";
    *modify_time = *file_size = 0;

    struct stat statbuf;
    if (stat(file_path, &statbuf) != 0)
        return 1;

    *modify_time = statbuf.st_mtim.tv_sec;
    *file_size = statbuf.st_size;

    if (S_ISDIR(statbuf.st_mode)) {
        *file_type = "dir";
    } else if (S_ISREG(statbuf.st_mode)) {
        // 处理文件后缀
        const char *dot = strrchr(file_path, '.');
        if (!dot) { // 没有后缀
            *file_type = "other";
            return 0;
        }

        int ext_len = strlen(file_path) - (dot-file_path); /* include . */
        if (ext_len > MAX_EXTENSION) { // 无效后缀
            *file_type = "other";
            return 0;
        }

        g_autofree char *ext = g_strconcat(";", dot+1, ";", NULL);
        g_autofree char *ext_lower = g_ascii_strdown(ext, -1);
        for (const char **p = file_type_maps; *p; p+=2) {
            if (strstr(*(p+1), ext_lower)) {
                *file_type = *p;
                return 0;
            }
        }

        *file_type = "other";
    } else {
        *file_type = "other";
    }

    return 0;
}

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
        return g_strdup_printf("%ld B", size);

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
