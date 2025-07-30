// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "datatype.h"
#include "vfs_change_consts.h"

const gchar *event_action_to_string(guint8 action)
{
    switch (action) {
        case ACT_NEW_FILE:
            return "file-created";
        case ACT_NEW_LINK:
            return "link-created";
        case ACT_NEW_SYMLINK:
            return "symlink-created";
        case ACT_NEW_FOLDER:
            return "folder-created";
        case ACT_DEL_FILE:
            return "file-deleted";
        case ACT_DEL_FOLDER:
            return "folder-deleted";
        case ACT_RENAME_FROM_FILE:
        case ACT_RENAME_TO_FILE:
            return "file-renamed";
        case ACT_RENAME_FROM_FOLDER:
        case ACT_RENAME_TO_FOLDER:
            return "folder-renamed";
        default:
            return "unknown";
    }
}

guint32 event_string_to_action_mask(const gchar *action_str)
{
    if (g_str_equal(action_str, "file-created")) {
        return 1 << ACT_NEW_FILE;
    } else if (g_str_equal(action_str, "link-created")) {
        return 1 << ACT_NEW_LINK;
    } else if (g_str_equal(action_str, "symlink-created")) {
        return 1 << ACT_NEW_SYMLINK;
    } else if (g_str_equal(action_str, "folder-created")) {
        return 1 << ACT_NEW_FOLDER;
    } else if (g_str_equal(action_str, "file-deleted")) {
        return 1 << ACT_DEL_FILE;
    } else if (g_str_equal(action_str, "folder-deleted")) {
        return 1 << ACT_DEL_FOLDER;
    } else if (g_str_equal(action_str, "file-renamed")) {
        return 1 << ACT_RENAME_FROM_FILE | 1 << ACT_RENAME_TO_FILE;
    } else if (g_str_equal(action_str, "folder-renamed")) {
        return 1 << ACT_RENAME_FROM_FOLDER | 1 << ACT_RENAME_TO_FOLDER;
    } else {
        return G_MAXUINT32;
    }
}
