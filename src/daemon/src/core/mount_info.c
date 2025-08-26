// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/mount_info.h"
#include <libmount/libmount.h>
#include <sys/sysmacros.h>

typedef struct _MountRecord {
    dev_t device_id;
    int parent_mount_id;
    gchar *mount_point;
} MountRecord;

struct _MountInfo {
    /* key: device_id, value: MountRecord */
    GHashTable *device_mount_points;
    /* key: device_id, value: GList<mount_point> */
    /* the mount_point owned by device_mount_points */
    GHashTable *child_mount_points;
    gboolean exist_lowerfs;
};

static void
free_mount_record(gpointer mount_record)
{
    MountRecord *record = (MountRecord *)mount_record;
    g_free(record->mount_point);
    g_free(record);
}

static void
mount_info_clear(MountInfo *mount_info)
{
    if (mount_info->device_mount_points) {
        g_hash_table_destroy(mount_info->device_mount_points);
        mount_info->device_mount_points = NULL;
    }

    if (mount_info->child_mount_points) {
        g_hash_table_destroy(mount_info->child_mount_points);
        mount_info->child_mount_points = NULL;
    }
}

MountInfo *
mount_info_new()
{
    MountInfo *mount_info = g_new0(MountInfo, 1);
    mount_info_update(mount_info);

    return mount_info;
}

void
mount_info_free(MountInfo *mount_info)
{
    if (!mount_info)
        return;

    mount_info_clear(mount_info);
    g_free(mount_info);
}

gboolean
is_mount_chain_all_root(GHashTable *root_mount_tree, struct libmnt_fs *fs)
{
    // skip non-root mount point
    if (g_strcmp0(mnt_fs_get_root(fs), "/") != 0) {
        return FALSE;
    }

    // check if the mount point is root
    if (g_strcmp0(mnt_fs_get_target(fs), "/") == 0) {
        return TRUE;
    }

    // check if all parent mount points are root
    int parent_mount_id = mnt_fs_get_parent_id(fs);
    while (TRUE) {
        MountRecord *record = g_hash_table_lookup(root_mount_tree, GINT_TO_POINTER(parent_mount_id));
        if (!record) {
            return FALSE;
        }
        if (g_strcmp0(record->mount_point, "/") == 0) {
            return TRUE;
        }
        parent_mount_id = record->parent_mount_id;
    }
}

static void 
mount_info_update_child_mount_points(MountInfo *mount_info, GHashTable *root_mount_tree)
{
    g_return_if_fail(mount_info != NULL);
    g_return_if_fail(root_mount_tree != NULL);

    GList *keys = g_hash_table_get_keys(root_mount_tree);
    GList *values = g_hash_table_get_values(root_mount_tree);
    for (GList *key = keys; key != NULL; key = key->next) {
        GList *child_mount_points = NULL;
        for (GList *value = values; value != NULL; value = value->next) {
            MountRecord *record = (MountRecord *)value->data;
            if (record->parent_mount_id == GPOINTER_TO_INT(key->data)) {
                child_mount_points = g_list_append(child_mount_points, record->mount_point);
            }
        }
        if (child_mount_points) {
            MountRecord *record = g_hash_table_lookup(root_mount_tree, key->data);
            g_hash_table_insert(mount_info->child_mount_points, GINT_TO_POINTER(record->device_id), child_mount_points);
        }
    }
    g_list_free(values);
    g_list_free(keys);
}

void
mount_info_update(MountInfo *mount_info)
{
    g_return_if_fail(mount_info != NULL);

    mount_info_clear(mount_info);
    mount_info->device_mount_points = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, free_mount_record);
    mount_info->child_mount_points = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)g_list_free);

    struct libmnt_table *mtab = mnt_new_table();
    if (mnt_table_parse_mtab (mtab, NULL) < 0){
        mnt_free_table(mtab);
        return;
    }

    struct libmnt_iter *iter = mnt_new_iter(MNT_ITER_FORWARD);
    struct libmnt_fs *fs;
    g_autoptr(GHashTable) root_mount_tree = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);
    while (mnt_table_next_fs(mtab, iter, &fs) == 0) {
        if (!is_mount_chain_all_root(root_mount_tree, fs)) {
            continue;
        }

        dev_t device_id = mnt_fs_get_devno(fs);
        if (g_hash_table_contains(mount_info->device_mount_points, GINT_TO_POINTER(device_id))) {
            g_warning("device %d is already mounted", (int)device_id);
            continue;
        }

        const char *target = mnt_fs_get_target(fs);
        if (!target) {
            continue;
        }

        MountRecord *record = g_new0(MountRecord, 1);
        record->device_id = device_id;
        record->parent_mount_id = mnt_fs_get_parent_id(fs);
        record->mount_point = g_strdup(target);
        g_hash_table_insert(mount_info->device_mount_points, GINT_TO_POINTER(device_id), record);
        g_hash_table_insert(root_mount_tree, GINT_TO_POINTER(mnt_fs_get_id(fs)), record);

        if (g_strcmp0(mnt_fs_get_fstype(fs), "fuse.dlnfs") == 0 ||
            g_strcmp0(mnt_fs_get_fstype(fs), "ulnfs") == 0) {
            mount_info->exist_lowerfs = TRUE;
        }
    }
    mnt_free_iter(iter);

    mnt_free_table(mtab);

    mount_info_update_child_mount_points(mount_info, root_mount_tree);
}

const gchar *
mount_info_get_device_mount_point(MountInfo *mount_info, dev_t device_id)
{
    g_return_val_if_fail(mount_info != NULL, NULL);
    MountRecord *record = g_hash_table_lookup(mount_info->device_mount_points, GINT_TO_POINTER(device_id));
    if (record) {
        return record->mount_point;
    }
    return NULL;
}

const GList *
mount_info_get_child_mount_points(MountInfo *mount_info, dev_t device_id)
{
    g_return_val_if_fail(mount_info != NULL, NULL);
    return g_hash_table_lookup(mount_info->child_mount_points, GINT_TO_POINTER(device_id));
}

static void
dump_mount_info(G_GNUC_UNUSED gpointer key, gpointer value, gpointer user_data)
{
    MountRecord *record = (MountRecord *)value;
    GString *buf = (GString *)user_data;
    unsigned int major_num, minor_num;

    major_num = major(record->device_id);
    minor_num = minor(record->device_id);
    g_string_append_printf(buf, "%d:%d -> %s\n", major_num, minor_num, record->mount_point);
}

static void
dump_child_mount_points(gpointer key, gpointer value, gpointer user_data)
{
    dev_t device_id = GPOINTER_TO_INT(key);
    GList *child_mount_points = (GList *)value;
    GString *buf = (GString *)user_data;
    unsigned int major_num, minor_num;

    major_num = major(device_id);
    minor_num = minor(device_id);
    g_string_append_printf(buf, "%d:%d:\n", major_num, minor_num);

    for (GList *iter = child_mount_points; iter != NULL; iter = iter->next) {
        g_string_append_printf(buf, "  %s\n", (gchar *)iter->data);
    }
}

gchar *
mount_info_dump(MountInfo *mount_info)
{
    GString *buf = g_string_new("");

    g_string_append_printf(buf, "device mount points:\n");
    g_hash_table_foreach(mount_info->device_mount_points, (GHFunc)dump_mount_info, buf);
    g_string_append_printf(buf, "child mount points:\n");
    g_hash_table_foreach(mount_info->child_mount_points, (GHFunc)dump_child_mount_points, buf);
    g_string_append_printf(buf, "exist lowerfs: %s\n", mount_info->exist_lowerfs ? "true" : "false");

    return g_string_free(buf, FALSE);
}

gboolean
mount_info_exist_lowerfs(MountInfo *mount_info)
{
    g_return_val_if_fail(mount_info != NULL, FALSE);
    return mount_info->exist_lowerfs;
}