// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "event_listener.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include "../kernelmod/vfs_genl.h"

/**
 * ACT_INVALID:
 * 
 * Sentinel value to indicate an invalid or uninitialized action.
 * Value 100 is chosen to be outside the normal range of VFS actions
 * which typically use lower values (0-20).
 */
#define ACT_INVALID 100

/**
 * EventListener:
 * 
 * Internal structure for the event listener implementation.
 */
struct EventListener {
    struct nl_sock *sock;           /**< Netlink socket for communication */
    GIOChannel *channel;            /**< GIO channel for event loop integration */
    gint source_id;                 /**< GSource ID for the watch */
    guint event_mask;               /**< Bitmask of monitored events */
    FileEventHandler handler;       /**< User callback function */
    gpointer user_data;             /**< User data for callback */
    FileEvent *event;               /**< Current event being processed */
};

/**
 * file_event_new:
 * 
 * Creates a new FileEvent structure with initialized values.
 * 
 * Returns: A newly allocated FileEvent, or NULL on failure
 */
static FileEvent *file_event_new(void)
{
    FileEvent *event = g_slice_new0(FileEvent);
    if (event) {
        event->action = ACT_INVALID;
    }
    return event;
}

/**
 * file_event_free:
 * @event: FileEvent to free
 * 
 * Frees a FileEvent structure.
 */
static void file_event_free(FileEvent *event)
{
    if (event) {
        g_slice_free(FileEvent, event);
    }
}

/**
 * safe_string_copy:
 * @dest: Destination buffer
 * @src: Source string
 * @dest_size: Size of destination buffer
 * 
 * Safely copies a string to a fixed-size buffer with proper null termination.
 */
static void safe_string_copy(char *dest, const char *src, size_t dest_size)
{
    g_return_if_fail(dest != NULL);
    g_return_if_fail(src != NULL);
    g_return_if_fail(dest_size > 0);

    size_t src_len = strlen(src);
    size_t copy_len = MIN(src_len, dest_size - 1);
    
    memcpy(dest, src, copy_len);
    dest[copy_len] = '\0';
    
    if (src_len >= dest_size) {
        g_warning("String truncated: source length %zu exceeds buffer size %zu", 
                  src_len, dest_size);
    }
}

// static const char* action_names[] = {"file-created", "link-created", "symlink-created", "dir-created", "file-deleted", "dir-deleted", 
//     "file-renamed", "dir-renamed", "file-renamed-from", "file-renamed-to", "dir-renamed-from", "dir-renamed-to", "fs-mount", "fs-unmount"};

// static int print_dentry_msg(struct nlattr *attr[])
// {
//     unsigned char act = 0;
//     unsigned int cookie = 0;
//     unsigned int major = 0;
//     unsigned int minor = 0;
//     char *path = "";

//     if (attr[VFSMONITOR_A_ACT])
//         act = nla_get_u8(attr[VFSMONITOR_A_ACT]);
//     if (attr[VFSMONITOR_A_COOKIE])
//         cookie = nla_get_u32(attr[VFSMONITOR_A_COOKIE]);
//     if (attr[VFSMONITOR_A_MAJOR])
//         major = nla_get_u16(attr[VFSMONITOR_A_MAJOR]);
//     if (attr[VFSMONITOR_A_MINOR])
//         minor = nla_get_u8(attr[VFSMONITOR_A_MINOR]);
//     if (attr[VFSMONITOR_A_PATH])
//         path = nla_get_string(attr[VFSMONITOR_A_PATH]);
    
//     if (act < sizeof(action_names))
//         g_message("vfs_changed: %s, dev: %u:%u, path: %s, cookie: %u", action_names[act], major, minor, path, cookie);

//     return 0;
// }

// static int print_proc_info_msg(struct nlattr *attr[])
// {
//     unsigned int uid = 0;
//     int tgid = 0;
//     char *path = "";

//     if (attr[VFSMONITOR_A_UID])
//         uid = nla_get_u32(attr[VFSMONITOR_A_UID]);
//     if (attr[VFSMONITOR_A_TGID])
//         tgid = nla_get_s32(attr[VFSMONITOR_A_TGID]);
//     if (attr[VFSMONITOR_A_PATH])
//         path = nla_get_string(attr[VFSMONITOR_A_PATH]);
    
//     g_message("proc_info: %s, uid: %u, pid: %d", path, uid, tgid);

//     return 0;
// }

/**
 * event_handler:
 * @msg: Netlink message
 * @arg: EventListener instance
 * 
 * Callback for processing netlink messages from the kernel module.
 * 
 * Returns: NL_OK on success, NL_SKIP on recoverable errors
 */
static int event_handler(struct nl_msg *msg, void *arg)
{
    struct nlattr *attrs[VFSMONITOR_A_MAX+1];
    struct nlmsghdr *nlhdr;
    struct genlmsghdr *genlhdr;
    char *path;
    guint8 act;
    
    g_return_val_if_fail(msg != NULL, NL_SKIP);
    g_return_val_if_fail(arg != NULL, NL_SKIP);
    
    nlhdr = nlmsg_hdr(msg);
    genlhdr = genlmsg_hdr(nlhdr);
    
    int ret = genlmsg_parse(nlhdr, 0, attrs, VFSMONITOR_A_MAX, vfsmonitor_genl_policy);
    if (ret < 0) {
        g_warning("Failed to parse netlink message: %s", strerror(-ret));
        return NL_SKIP;
    }

    EventListener *listener = (EventListener *)arg;
    
    // Ensure we have a current event structure
    if (!listener->event) {
        listener->event = file_event_new();
        if (!listener->event) {
            g_warning("Failed to allocate memory for FileEvent");
            return NL_SKIP;
        }
    }

    switch (genlhdr->cmd) {
        case VFSMONITOR_C_NOTIFY:
            // print_dentry_msg(attrs);
            // Extract and validate action
            g_return_val_if_fail(attrs[VFSMONITOR_A_ACT] != NULL, NL_SKIP);
            act = nla_get_u8(attrs[VFSMONITOR_A_ACT]);
            
            // Check if this event type is in our mask
            if (!((1 << act) & listener->event_mask)) {
                return NL_OK; // Not an error, just filtered out
            }
            
            // Warn if we're getting events out of order
            if (listener->event->action != ACT_INVALID) {
                // Maybe the kernel module not support process info event
                // Maybe some events are lost for socket receive buffer overflow
                g_debug("Expected a process info event, but received a new notify event");
                // Reset the event to handle the new one
                listener->event->action = ACT_INVALID;
            }
            
            // Extract all required attributes
            listener->event->action = act;            
            g_return_val_if_fail(attrs[VFSMONITOR_A_COOKIE] != NULL, NL_SKIP);
            g_return_val_if_fail(attrs[VFSMONITOR_A_MAJOR] != NULL, NL_SKIP);
            g_return_val_if_fail(attrs[VFSMONITOR_A_MINOR] != NULL, NL_SKIP);
            g_return_val_if_fail(attrs[VFSMONITOR_A_PATH] != NULL, NL_SKIP);
            listener->event->cookie = nla_get_u32(attrs[VFSMONITOR_A_COOKIE]);
            listener->event->major = nla_get_u16(attrs[VFSMONITOR_A_MAJOR]);
            listener->event->minor = nla_get_u8(attrs[VFSMONITOR_A_MINOR]);
            path = nla_get_string(attrs[VFSMONITOR_A_PATH]);
            safe_string_copy(listener->event->event_path, path, sizeof(listener->event->event_path));
            break;
            
        case VFSMONITOR_C_NOTIFY_PROCESS_INFO:
            // print_proc_info_msg(attrs);
            // Ensure we have a pending event
            if (listener->event->action == ACT_INVALID) {
                // After the events are merged, some unattended notify events carry the process info event
                g_debug("Expected a new notify event, but received a process info event");
                return NL_OK;
            }
            
            // Extract process information
            g_return_val_if_fail(attrs[VFSMONITOR_A_UID] != NULL, NL_SKIP);
            g_return_val_if_fail(attrs[VFSMONITOR_A_TGID] != NULL, NL_SKIP);
            g_return_val_if_fail(attrs[VFSMONITOR_A_PATH] != NULL, NL_SKIP);
            listener->event->uid = nla_get_u32(attrs[VFSMONITOR_A_UID]);
            listener->event->pid = nla_get_s32(attrs[VFSMONITOR_A_TGID]);
            path = nla_get_string(attrs[VFSMONITOR_A_PATH]);
            safe_string_copy(listener->event->process_path, path, sizeof(listener->event->process_path));
            
            // Event is now complete - dispatch to handler
            if (listener->handler) {
                listener->handler(listener->user_data, listener->event);
            } else {
                g_warning("No event handler registered, freeing event");
                file_event_free(listener->event);
            }
            
            // Clear the current event
            listener->event = NULL;
            break;
            
        default:
            g_warning("Unknown netlink command: %d", genlhdr->cmd);
            return NL_SKIP;
    }

    return NL_OK;
}

// Callback for GIOChannel
static gboolean on_netlink_event(G_GNUC_UNUSED GIOChannel *source, 
                                 G_GNUC_UNUSED GIOCondition condition, 
                                 gpointer data)
{
    struct nl_sock *sk = (struct nl_sock *)data;
    int ret = nl_recvmsgs_default(sk);
    
    if (ret < 0) {
        g_warning("Failed to receive netlink messages: %s", nl_geterror(ret));
    }
    
    return G_SOURCE_CONTINUE;
}

/**
 * set_max_socket_receive_buffer_size:
 * @sk: Netlink socket
 * 
 * Attempts to set the socket receive buffer to the system maximum.
 * 
 * Returns: TRUE on success, FALSE on failure
 */
static gboolean set_max_socket_receive_buffer_size(struct nl_sock *sk)
{
    g_autoptr(GError) error = NULL;
    g_autofree char *contents = NULL;
    
    if (!g_file_get_contents("/proc/sys/net/core/rmem_max", &contents, NULL, &error)) {
        g_warning("Failed to read /proc/sys/net/core/rmem_max: %s", error->message);
        return FALSE;
    }
    
    int max_rcvbuf = atoi(contents);
    if (max_rcvbuf <= 0) {
        g_warning("Invalid rmem_max value: %s", contents);
        return FALSE;
    }

    int ret = nl_socket_set_buffer_size(sk, max_rcvbuf, 0);
    if (ret < 0) {
        g_warning("Failed to set socket receive buffer size: %s", strerror(-ret));
        return FALSE;
    }

    g_autofree char *size_str = g_format_size_full(max_rcvbuf, G_FORMAT_SIZE_IEC_UNITS);
    g_message("Set max socket receive buffer size: %s", size_str);
    return TRUE;
}

/**
 * event_listener_join_multicast_group:
 * @listener: EventListener instance
 * @group_name: Name of the multicast group to join
 * 
 * Joins a netlink multicast group.
 * 
 * Returns: TRUE on success, FALSE on failure
 */
static gboolean event_listener_join_multicast_group(EventListener *listener, const char* group_name)
{
    g_return_val_if_fail(listener != NULL, FALSE);
    g_return_val_if_fail(group_name != NULL, FALSE);
    
    int mcgrp = genl_ctrl_resolve_grp(listener->sock, VFSMONITOR_FAMILY_NAME, group_name);
    if (mcgrp < 0) {
        g_warning("Failed to resolve multicast group '%s': %s", 
                  group_name, strerror(-mcgrp));
        return FALSE;
    }

    int ret = nl_socket_add_membership(listener->sock, mcgrp);
    if (ret < 0) {
        g_warning("Failed to join multicast group '%s': %s", 
                  group_name, strerror(-ret));
        return FALSE;
    }

    g_debug("Successfully joined multicast group: %s", group_name);
    return TRUE;
}

EventListener *event_listener_new(FileEventHandler handler, gpointer user_data)
{
    g_return_val_if_fail(handler != NULL, NULL);
    
    EventListener *listener = g_new0(EventListener, 1);
    listener->handler = handler;
    listener->user_data = user_data;
    listener->event_mask = 0; // No events monitored by default
    
    // Allocate netlink socket
    listener->sock = nl_socket_alloc();
    if (!listener->sock) {
        g_warning("Failed to allocate netlink socket");
        event_listener_free(listener);
        return NULL;
    }

    // Connect to generic netlink
    int ret = genl_connect(listener->sock);
    if (ret < 0) {
        g_warning("Failed to connect to generic netlink: %s", strerror(-ret));
        event_listener_free(listener);
        return NULL;
    }

    set_max_socket_receive_buffer_size(listener->sock);

    nl_socket_disable_seq_check(listener->sock);
    nl_socket_disable_auto_ack(listener->sock);

    // Join required multicast groups
    if (!event_listener_join_multicast_group(listener, VFSMONITOR_MCG_DENTRY_NAME)) {
        event_listener_free(listener);
        return NULL;
    }
    
    if (!event_listener_join_multicast_group(listener, VFSMONITOR_MCG_PROCESS_INFO_NAME)) {
        event_listener_free(listener);
        return NULL;
    }

    // Set up message handler
    ret = nl_socket_modify_cb(listener->sock, NL_CB_VALID, NL_CB_CUSTOM, event_handler, listener);
    if (ret < 0) {
        g_warning("Failed to set netlink callback: %s", strerror(-ret));
        event_listener_free(listener);
        return NULL;
    }

    g_debug("EventListener created successfully");
    return listener;
}

gboolean event_listener_set_event_mask(EventListener *listener, guint mask)
{
    g_return_val_if_fail(listener != NULL, FALSE);

    const char *mask_file = "/sys/kernel/vfs_monitor/trace_event_mask";
    FILE *fp = fopen(mask_file, "w");
    if (!fp) {
        g_warning("Failed to open %s for writing: %s", mask_file, strerror(errno));
        return FALSE;
    }
    
    int ret = fprintf(fp, "%u\n", mask);
    int close_ret = fclose(fp);
    
    if (ret < 0) {
        g_warning("Failed to write event mask to %s: %s", mask_file, strerror(errno));
        return FALSE;
    }
    
    if (close_ret != 0) {
        g_warning("Failed to close %s: %s", mask_file, strerror(errno));
        return FALSE;
    }

    listener->event_mask = mask;
    g_message("Set event mask: 0x%x", mask);
    return TRUE;
}

gboolean event_listener_set_disable_event_merge(EventListener *listener, gboolean disable_event_merge)
{
    g_return_val_if_fail(listener != NULL, FALSE);

    const char *file_path = "/sys/kernel/vfs_monitor/disable_event_merge";
    FILE *fp = fopen(file_path, "w");
    if (!fp) {
        g_warning("Failed to open %s for writing: %s", file_path, strerror(errno));
        return FALSE;
    }
    
    int ret = fprintf(fp, "%u\n", disable_event_merge ? 1 : 0);
    int close_ret = fclose(fp);
    
    if (ret < 0) {
        g_warning("Failed to write event mask to %s: %s", file_path, strerror(errno));
        return FALSE;
    }
    
    if (close_ret != 0) {
        g_warning("Failed to close %s: %s", file_path, strerror(errno));
        return FALSE;
    }

    g_message("Set disable_event_merge: %s", disable_event_merge ? "true" : "false");
    return TRUE;
}

gboolean event_listener_start(EventListener *listener)
{
    g_return_val_if_fail(listener != NULL, FALSE);
    g_return_val_if_fail(listener->sock != NULL, FALSE);
    
    // Check if already started
    if (listener->source_id > 0) {
        g_warning("EventListener is already started");
        return FALSE;
    }

    int fd = nl_socket_get_fd(listener->sock);
    if (fd < 0) {
        g_warning("Failed to get file descriptor from netlink socket");
        return FALSE;
    }
    
    listener->channel = g_io_channel_unix_new(fd);
    if (!listener->channel) {
        g_warning("Failed to create GIOChannel from netlink fd %d", fd);
        return FALSE;
    }
    
    // Set up the watch for incoming data
    listener->source_id = g_io_add_watch(listener->channel, G_IO_IN | G_IO_ERR | G_IO_HUP, 
                                         on_netlink_event, listener->sock);
    if (listener->source_id == 0) {
        g_warning("Failed to add IO watch for netlink channel");
        g_io_channel_unref(listener->channel);
        listener->channel = NULL;
        return FALSE;
    }

    g_debug("EventListener started successfully (fd=%d, source_id=%d)", fd, listener->source_id);
    return TRUE;
}

void event_listener_stop(EventListener *listener)
{
    g_return_if_fail(listener != NULL);

    if (listener->source_id > 0) {
        g_source_remove(listener->source_id);
        listener->source_id = 0;
        g_debug("Removed IO watch source");
    }
    
    if (listener->channel) {
        g_io_channel_unref(listener->channel);
        listener->channel = NULL;
        g_debug("Unreferenced IO channel");
    }
    
    g_debug("EventListener stopped successfully");
}

void event_listener_free(EventListener *listener)
{
    if (!listener) {
        return;
    }

    // Stop monitoring first
    event_listener_stop(listener);

    // Clean up any pending event
    if (listener->event) {
        if (listener->event->action != ACT_INVALID) {
            g_warning("Freeing EventListener with pending event (act=%d)", 
                     listener->event->action);
        }
        file_event_free(listener->event);
        listener->event = NULL;
    }

    // Close netlink socket
    if (listener->sock) {
        nl_socket_free(listener->sock);
        listener->sock = NULL;
    }
    
    // Free the listener structure
    g_free(listener);
}

static const char *kenel_module_check_path = "/sys/kernel/vfs_monitor";
static struct stat kenel_module_check_path_stat;

gboolean is_kernel_module_available(void)
{
    return lstat(kenel_module_check_path, &kenel_module_check_path_stat) == 0;
}

gboolean is_kernel_module_reload(void)
{
    struct stat st;
    // when system reboot, the file may be deleted before we quit
    // so we not quit or restart, we wait stop command from systemd or the file appear again
    if (lstat(kenel_module_check_path, &st) != 0) {
        return FALSE;
    }

    return st.st_ino != kenel_module_check_path_stat.st_ino;
}
