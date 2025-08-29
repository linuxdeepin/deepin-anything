// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gio/gunixmounts.h>
#include <glib.h>
#include <libmount/libmount.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysmacros.h>
#include <locale.h>

#define MAX_MINOR 255

#define VFS_UNNAMED_DEVICE_FILE "/sys/kernel/vfs_monitor/vfs_unnamed_devices"

static GList *get_unnamed_device_by_fstype (GStrv fstypes)
{
    GList *devices = NULL;
    struct libmnt_table *table;
    struct libmnt_iter* iter;
    struct libmnt_fs *fs;
    unsigned int major_num, minor_num;
    gchar **fstype;
    GHashTable *minor_set = NULL;

    table = mnt_new_table ();
    if (mnt_table_parse_mtab (table, NULL) < 0)
        goto out;

    minor_set = g_hash_table_new (g_direct_hash, g_direct_equal);
    iter = mnt_new_iter (MNT_ITER_FORWARD);
    while (mnt_table_next_fs (table, iter, &fs) == 0) {
        major_num = major(mnt_fs_get_devno (fs));
        minor_num = minor(mnt_fs_get_devno (fs));
        if (major_num != 0 || g_hash_table_contains (minor_set, GUINT_TO_POINTER(minor_num)))
            continue;
        if (minor_num > MAX_MINOR) {
            g_warning("minor %u is out of range", minor_num);
            continue;
        }

        for (fstype = fstypes; *fstype; fstype++) {
            if (g_strcmp0 (mnt_fs_get_fstype (fs), *fstype) == 0) {
                devices = g_list_append (devices, g_strdup_printf("%u", minor_num));
                g_hash_table_insert (minor_set, GUINT_TO_POINTER(minor_num), GUINT_TO_POINTER(1));
                break;
            }
        }
    }
    mnt_free_iter (iter);

out:
    if (minor_set)
        g_hash_table_unref (minor_set);
    mnt_free_table (table);
    return devices;
}

static void write_vfs_unnamed_device(const char *str)
{
    FILE *file = fopen(VFS_UNNAMED_DEVICE_FILE, "w");
    if (!file)
        return;

    fwrite(str, strlen(str), 1, file);
    fclose(file);
}

static GList *read_vfs_unnamed_device(GError **error)
{
    GList *devices = NULL;

    g_autofree char *content = NULL;
    if (!g_file_get_contents(VFS_UNNAMED_DEVICE_FILE, &content, NULL, error)) {
        return NULL;
    }

    /* remove last \n */
    size_t len = strlen(content);
    if (content[len - 1] == '\n')
        content[len - 1] = '\0';

    char **list = g_strsplit(content, ",", 0);
    for (char **p = list; *p; p++) {
        devices = g_list_append(devices, *p);
    }
    /* free list only, devices own the data */
    g_free(list);

    return devices;
}

static void diff_sorted_lists (GList *list1, GList *list2, GCompareFunc compare, GList **added, GList **removed)
{
    int order;

    *added = *removed = NULL;

    while (list1 != NULL && list2 != NULL)
    {
        order = (*compare) (list1->data, list2->data);
        if (order < 0)
        {
            *removed = g_list_prepend (*removed, list1->data);
            list1 = list1->next;
        }
        else if (order > 0)
        {
            *added = g_list_prepend (*added, list2->data);
            list2 = list2->next;
        }
        else
        {
            /* same item */
            list1 = list1->next;
            list2 = list2->next;
        }
    }

    while (list1 != NULL)
    {
        *removed = g_list_prepend (*removed, list1->data);
        list1 = list1->next;
    }
    while (list2 != NULL)
    {
        *added = g_list_prepend (*added, list2->data);
        list2 = list2->next;
    }
}

static void update_vfs_unnamed_device(GList *news)
{
    char buf[32];
    GList *olds, *removed, *added;
    GError *error = NULL;
    GList *iter;

    olds = read_vfs_unnamed_device(&error);
    if (error) {
        g_warning("Failed to read vfs_unnamed_devices: %s", error->message);
        g_error_free(error);
        return;
    }

    olds = g_list_sort(olds, (GCompareFunc) g_strcmp0);
    news = g_list_sort(news, (GCompareFunc) g_strcmp0);

    diff_sorted_lists(olds, news, (GCompareFunc) g_strcmp0, &added, &removed);

    for (iter = removed; iter; iter = iter->next) {
        snprintf(buf, sizeof(buf), "r%s", (char *) iter->data);
        write_vfs_unnamed_device(buf);
    }

    for (iter = added; iter; iter = iter->next) {
        snprintf(buf, sizeof(buf), "a%s", (char *) iter->data);
        write_vfs_unnamed_device(buf);
    }

    /* free list only for added and removed, because news and olds own the data */
    g_list_free(added);
    g_list_free(removed);
    g_list_free_full(olds, g_free);
}

static void mounts_changed (G_GNUC_UNUSED GUnixMountMonitor *mount_monitor, gpointer user_data)
{
    GStrv fstypes = user_data;

    GList *devices = get_unnamed_device_by_fstype (fstypes);
    update_vfs_unnamed_device(devices);
    g_list_free_full(devices, g_free);
}

int main (G_GNUC_UNUSED int argc, G_GNUC_UNUSED char *argv[])
{
    setlocale (LC_ALL, "");

    g_auto(GStrv) fstypes = g_strsplit("overlay,btrfs,fuse.dlnfs,ulnfs", ",", 0);
    mounts_changed(NULL, fstypes);

    GMainLoop *loop = g_main_loop_new (NULL, FALSE);
    GUnixMountMonitor *monitor = g_unix_mount_monitor_get ();
    g_signal_connect (monitor,
                      "mounts-changed",
                      G_CALLBACK (mounts_changed),
                      fstypes);
    g_main_loop_run (loop);
    g_object_unref (monitor);
    g_main_loop_unref (loop);

    return 0;
}
