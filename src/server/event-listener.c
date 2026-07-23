// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "event-listener.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>

#include "vfs_genl.h"

struct ServerEventListener {
    struct nl_sock *sock;
    GIOChannel     *channel;
    gint            source_id;
    GMainLoop      *loop;
    GThread        *thread;
    FileEventHandler handler;
    gpointer         user_data;
};

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

static int netlink_event_handler(struct nl_msg *msg, void *arg)
{
    struct nlattr *attrs[VFSMONITOR_A_MAX + 1];
    struct nlmsghdr *nlhdr;
    struct genlmsghdr *genlhdr;
    char *path;

    g_return_val_if_fail(msg != NULL, NL_SKIP);
    g_return_val_if_fail(arg != NULL, NL_SKIP);

    nlhdr = nlmsg_hdr(msg);
    genlhdr = genlmsg_hdr(nlhdr);

    int ret = genlmsg_parse(nlhdr, 0, attrs, VFSMONITOR_A_MAX, vfsmonitor_genl_policy);
    if (ret < 0) {
        g_warning("Failed to parse netlink message: %s", strerror(-ret));
        return NL_SKIP;
    }

    ServerEventListener *listener = (ServerEventListener *)arg;

    if (genlhdr->cmd != VFSMONITOR_C_NOTIFY) {
        g_debug("Ignoring netlink command: %d", genlhdr->cmd);
        return NL_OK;
    }

    g_return_val_if_fail(attrs[VFSMONITOR_A_ACT] != NULL, NL_SKIP);
    g_return_val_if_fail(attrs[VFSMONITOR_A_COOKIE] != NULL, NL_SKIP);
    g_return_val_if_fail(attrs[VFSMONITOR_A_MAJOR] != NULL, NL_SKIP);
    g_return_val_if_fail(attrs[VFSMONITOR_A_MINOR] != NULL, NL_SKIP);
    g_return_val_if_fail(attrs[VFSMONITOR_A_PATH] != NULL, NL_SKIP);

    fs_event *event = g_slice_new0(fs_event);
    if (!event) {
        g_warning("Failed to allocate memory for fs_event");
        return NL_SKIP;
    }

    event->act = nla_get_u8(attrs[VFSMONITOR_A_ACT]);
    event->cookie = nla_get_u32(attrs[VFSMONITOR_A_COOKIE]);
    event->major = nla_get_u16(attrs[VFSMONITOR_A_MAJOR]);
    event->minor = nla_get_u8(attrs[VFSMONITOR_A_MINOR]);

    path = nla_get_string(attrs[VFSMONITOR_A_PATH]);
    safe_string_copy(event->src, path, sizeof(event->src));
    event->dst[0] = '\0';

    if (listener->handler) {
        listener->handler(listener->user_data, event);
    } else {
        g_slice_free(fs_event, event);
    }

    return NL_OK;
}

static gboolean on_netlink_readable(G_GNUC_UNUSED GIOChannel *source,
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

static gboolean set_max_socket_receive_buffer_size(struct nl_sock *sk)
{
    g_autofree char *contents = NULL;
    g_autoptr(GError) error = NULL;

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

static gboolean join_multicast_group(struct nl_sock *sk, const char *group_name)
{
    g_return_val_if_fail(sk != NULL, FALSE);
    g_return_val_if_fail(group_name != NULL, FALSE);

    int mcgrp = genl_ctrl_resolve_grp(sk, VFSMONITOR_FAMILY_NAME, group_name);
    if (mcgrp < 0) {
        g_warning("Failed to resolve multicast group '%s': %s",
                  group_name, strerror(-mcgrp));
        return FALSE;
    }

    int ret = nl_socket_add_membership(sk, mcgrp);
    if (ret < 0) {
        g_warning("Failed to join multicast group '%s': %s",
                  group_name, strerror(-ret));
        return FALSE;
    }

    g_debug("Successfully joined multicast group: %s", group_name);
    return TRUE;
}

static gpointer event_listener_thread_func(gpointer data)
{
    ServerEventListener *listener = (ServerEventListener *)data;

    listener->loop = g_main_loop_new(NULL, FALSE);

    int fd = nl_socket_get_fd(listener->sock);
    if (fd < 0) {
        g_critical("Failed to get file descriptor from netlink socket");
        return NULL;
    }

    listener->channel = g_io_channel_unix_new(fd);
    if (!listener->channel) {
        g_critical("Failed to create GIOChannel from netlink fd %d", fd);
        return NULL;
    }

    listener->source_id = g_io_add_watch(listener->channel,
                                          G_IO_IN | G_IO_ERR | G_IO_HUP,
                                          on_netlink_readable, listener->sock);
    if (listener->source_id == 0) {
        g_critical("Failed to add IO watch for netlink channel");
        g_io_channel_unref(listener->channel);
        listener->channel = NULL;
        return NULL;
    }

    g_message("Event listener thread started");
    g_main_loop_run(listener->loop);

    if (listener->source_id > 0) {
        g_source_remove(listener->source_id);
        listener->source_id = 0;
    }

    if (listener->channel) {
        g_io_channel_unref(listener->channel);
        listener->channel = NULL;
    }

    g_message("Event listener thread stopped");
    return NULL;
}

ServerEventListener *server_event_listener_new(FileEventHandler handler,
                                                gpointer user_data)
{
    g_return_val_if_fail(handler != NULL, NULL);

    ServerEventListener *listener = g_new0(ServerEventListener, 1);
    listener->handler = handler;
    listener->user_data = user_data;

    listener->sock = nl_socket_alloc();
    if (!listener->sock) {
        g_critical("Failed to allocate netlink socket");
        g_free(listener);
        return NULL;
    }

    int ret = genl_connect(listener->sock);
    if (ret < 0) {
        g_critical("Failed to connect to generic netlink: %s", strerror(-ret));
        nl_socket_free(listener->sock);
        g_free(listener);
        return NULL;
    }

    set_max_socket_receive_buffer_size(listener->sock);

    nl_socket_disable_seq_check(listener->sock);
    nl_socket_disable_auto_ack(listener->sock);

    if (!join_multicast_group(listener->sock, VFSMONITOR_MCG_DENTRY_NAME)) {
        g_critical("Failed to join dentry multicast group");
        nl_socket_free(listener->sock);
        g_free(listener);
        return NULL;
    }

    ret = nl_socket_modify_cb(listener->sock, NL_CB_VALID, NL_CB_CUSTOM,
                              netlink_event_handler, listener);
    if (ret < 0) {
        g_critical("Failed to set netlink callback: %s", strerror(-ret));
        nl_socket_free(listener->sock);
        g_free(listener);
        return NULL;
    }

    g_message("Event listener created successfully");
    return listener;
}

gboolean server_event_listener_start(ServerEventListener *listener)
{
    g_return_val_if_fail(listener != NULL, FALSE);
    g_return_val_if_fail(listener->sock != NULL, FALSE);

    if (listener->thread) {
        g_warning("Event listener is already started");
        return FALSE;
    }

    listener->thread = g_thread_new("event_listener",
                                     event_listener_thread_func, listener);
    if (!listener->thread) {
        g_critical("Failed to create event listener thread");
        return FALSE;
    }

    return TRUE;
}

void server_event_listener_stop(ServerEventListener *listener)
{
    g_return_if_fail(listener != NULL);

    if (listener->loop) {
        g_main_loop_quit(listener->loop);
    }

    if (listener->thread) {
        g_thread_join(listener->thread);
        listener->thread = NULL;
    }

    if (listener->loop) {
        g_main_loop_unref(listener->loop);
        listener->loop = NULL;
    }
}

void server_event_listener_free(ServerEventListener *listener)
{
    if (!listener) {
        return;
    }

    server_event_listener_stop(listener);

    if (listener->sock) {
        nl_socket_free(listener->sock);
        listener->sock = NULL;
    }

    g_free(listener);
}
