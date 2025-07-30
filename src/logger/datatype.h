// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DATATYPE_H
#define DATATYPE_H

#define G_LOG_USE_STRUCTURED
#include <glib.h>

G_BEGIN_DECLS

/**
 * MAX_PATH_LEN:
 * 
 * Maximum length for file paths in file events.
 * This should be sufficient for most file systems.
 */
#define MAX_PATH_LEN 4096

/**
 * FileEvent:
 * @action: The type of file system operation (see vfs_change_consts.h)
 * @cookie: Unique identifier for related events (e.g., rename operations)
 * @major: Major device number
 * @minor: Minor device number
 * @event_path: Path of the file/directory affected by the event
 * @uid: User ID of the process that triggered the event
 * @pid: Process ID that triggered the event
 * @process_path: Path of the executable that triggered the event
 *
 * Structure representing a file system event captured by the deepin-anything system.
 * This structure is used to communicate file system changes from the kernel module
 * to user space applications.
 */
typedef struct _FileEvent {
    guint8      action;
    guint32     cookie;
    guint16     major;
    guint8      minor;
    gchar       event_path[MAX_PATH_LEN];
    guint32     uid;
    gint32      pid;
    gchar       process_path[MAX_PATH_LEN];
} FileEvent;

/**
 * event_action_to_string:
 * @action: The action code to convert
 *
 * Converts a file event action code to its string representation.
 * The returned string is statically allocated and should not be freed.
 *
 * Returns: (transfer none): String representation of the action, or "unknown" for invalid actions
 */
const gchar *event_action_to_string(guint8 action);

/**
 * event_string_to_action_mask:
 * @action_str: String representation of the action
 *
 * Converts a string representation of a file event action to its numeric code.
 * This function performs case-sensitive string matching.
 *
 * Returns: Action mask, or G_MAXUINT32 for invalid/unknown strings
 */
guint32 event_string_to_action_mask(const gchar *action_str);

G_END_DECLS

#endif // DATATYPE_H


