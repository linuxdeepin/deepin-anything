// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "event-listener.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib-unix.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>

#include "vfs_genl.h"

struct ServerEventListener {
    struct nl_sock *sock;
    GMainLoop      *loop;
    GThread        *thread;
    GMutex          startup_mutex;
    GCond           startup_cond;
    volatile gint   started;  /* 0 = pending, 1 = success, -1 = failure */
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
    safe_string_copy(event->path, path, sizeof(event->path));

    if (listener->handler) {
        listener->handler(listener->user_data, event);
    } else {
        g_slice_free(fs_event, event);
    }

    return NL_OK;
}

static gboolean on_netlink_readable(G_GNUC_UNUSED gint fd,
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

static void signal_startup(ServerEventListener *listener, gboolean success)
{
    g_mutex_lock(&listener->startup_mutex);
    g_atomic_int_set(&listener->started, success ? 1 : -1);
    g_cond_signal(&listener->startup_cond);
    g_mutex_unlock(&listener->startup_mutex);
}

static gpointer event_listener_thread_func(gpointer data)
{
    ServerEventListener *listener = (ServerEventListener *)data;
    gboolean startup_signaled = FALSE;
    GMainLoop *loop = NULL;

    GMainContext *context = g_main_context_new();
    if (!context) {
        g_critical("Failed to create main context for event listener");
        signal_startup(listener, FALSE);
        return NULL;
    }

    g_main_context_push_thread_default(context);

    loop = g_main_loop_new(context, FALSE);
    if (!loop) {
        g_critical("Failed to create main loop for event listener");
        goto cleanup;
    }

    int fd = nl_socket_get_fd(listener->sock);
    if (fd < 0) {
        g_critical("Failed to get file descriptor from netlink socket");
        goto cleanup;
    }

    GSource *fd_source = g_unix_fd_source_new(fd,
                                               G_IO_IN | G_IO_ERR | G_IO_HUP);
    if (!fd_source) {
        g_critical("Failed to create unix fd source for netlink fd %d", fd);
        goto cleanup;
    }

    g_source_set_callback(fd_source,
                          G_SOURCE_FUNC(on_netlink_readable),
                          listener->sock, NULL);

    /* Attach the source to the dedicated context; ownership transfers to the
     * context and the source is destroyed when the context is finalized. */
    guint id = g_source_attach(fd_source, context);
    g_source_unref(fd_source);
    if (id == 0) {
        g_critical("Failed to attach unix fd source for netlink channel");
        goto cleanup;
    }

    /* Publish the loop so stop() can quit it; take an extra reference so
     * this thread owns its own copy and can run/unref independently. */
    g_main_loop_ref(loop);
    listener->loop = loop;
    signal_startup(listener, TRUE);
    startup_signaled = TRUE;
    g_message("Event listener thread started");
    g_main_loop_run(loop);

cleanup:
    if (!startup_signaled) {
        signal_startup(listener, FALSE);
    }

    g_main_context_pop_thread_default(context);

    if (loop) {
        g_main_loop_unref(loop);
    }
    if (context) {
        g_main_context_unref(context);
    }

    g_message("Event listener thread stopped");
    return NULL;
}

ServerEventListener *server_event_listener_new(FileEventHandler handler,
                                                gpointer user_data)
{
    g_return_val_if_fail(handler != NULL, NULL);

    ServerEventListener *listener = g_new0(ServerEventListener, 1);
    g_mutex_init(&listener->startup_mutex);
    g_cond_init(&listener->startup_cond);
    g_atomic_int_set(&listener->started, 0);
    listener->handler = handler;
    listener->user_data = user_data;

    listener->sock = nl_socket_alloc();
    if (!listener->sock) {
        g_critical("Failed to allocate netlink socket");
        goto fail;
    }

    int ret = genl_connect(listener->sock);
    if (ret < 0) {
        g_critical("Failed to connect to generic netlink: %s", strerror(-ret));
        nl_socket_free(listener->sock);
        goto fail;
    }

    set_max_socket_receive_buffer_size(listener->sock);

    nl_socket_disable_seq_check(listener->sock);
    nl_socket_disable_auto_ack(listener->sock);

    if (!join_multicast_group(listener->sock, VFSMONITOR_MCG_DENTRY_NAME)) {
        g_critical("Failed to join dentry multicast group");
        nl_socket_free(listener->sock);
        goto fail;
    }

    ret = nl_socket_modify_cb(listener->sock, NL_CB_VALID, NL_CB_CUSTOM,
                              netlink_event_handler, listener);
    if (ret < 0) {
        g_critical("Failed to set netlink callback: %s", strerror(-ret));
        nl_socket_free(listener->sock);
        goto fail;
    }

    g_message("Event listener created successfully");
    return listener;

fail:
    g_mutex_clear(&listener->startup_mutex);
    g_cond_clear(&listener->startup_cond);
    g_free(listener);
    return NULL;
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

    g_mutex_lock(&listener->startup_mutex);
    while (g_atomic_int_get(&listener->started) == 0)
        g_cond_wait(&listener->startup_cond, &listener->startup_mutex);
    gboolean ok = (g_atomic_int_get(&listener->started) == 1);
    g_mutex_unlock(&listener->startup_mutex);

    if (!ok) {
        g_thread_join(listener->thread);
        listener->thread = NULL;
        g_critical("Event listener thread failed during startup");
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

    g_mutex_clear(&listener->startup_mutex);
    g_cond_clear(&listener->startup_cond);

    g_free(listener);
}
