// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "event-listener-backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>

#include "vfs_genl.h"

typedef struct {
    ServerEventListenerBackend parent;
    struct nl_sock            *sock;
    GIOChannel                *channel;
    gint                       source_id;
    GMainLoop                 *loop;
    GThread                   *thread;
    FileEventHandler           handler;
    gpointer                   user_data;
} GenlBackend;

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

    GenlBackend *backend = (GenlBackend *)arg;

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
    event->seq = attrs[VFSMONITOR_A_SEQ] ? nla_get_u32(attrs[VFSMONITOR_A_SEQ]) : 0;
    event->major = nla_get_u16(attrs[VFSMONITOR_A_MAJOR]);
    event->minor = nla_get_u8(attrs[VFSMONITOR_A_MINOR]);

    path = nla_get_string(attrs[VFSMONITOR_A_PATH]);
    safe_string_copy(event->src, path, sizeof(event->src));
    event->dst[0] = '\0';

    if (backend->handler)
        backend->handler(backend->user_data, event);
    else
        g_slice_free(fs_event, event);

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

static gpointer genl_backend_thread_func(gpointer data)
{
    GenlBackend *backend = (GenlBackend *)data;

    GMainContext *ctx = g_main_context_new();
    backend->loop = g_main_loop_new(ctx, FALSE);
    g_main_context_push_thread_default(ctx);

    int fd = nl_socket_get_fd(backend->sock);
    backend->channel = g_io_channel_unix_new(fd);
    backend->source_id = g_io_add_watch(backend->channel,
                                        G_IO_IN | G_IO_ERR | G_IO_HUP,
                                        on_netlink_readable, backend->sock);

    g_message("Genl event listener thread started");
    g_main_loop_run(backend->loop);

    if (backend->source_id > 0) {
        g_source_remove(backend->source_id);
        backend->source_id = 0;
    }
    if (backend->channel) {
        g_io_channel_unref(backend->channel);
        backend->channel = NULL;
    }

    g_main_context_pop_thread_default(ctx);
    g_main_context_unref(ctx);
    g_main_loop_unref(backend->loop);
    backend->loop = NULL;

    g_message("Genl event listener thread stopped");
    return NULL;
}

static gboolean genl_backend_start(ServerEventListenerBackend *self)
{
    GenlBackend *backend = (GenlBackend *)self;

    backend->thread = g_thread_new("genl_listener",
                                    genl_backend_thread_func, backend);
    if (!backend->thread) {
        g_critical("Failed to create genl listener thread");
        return FALSE;
    }
    return TRUE;
}

static void genl_backend_stop(ServerEventListenerBackend *self)
{
    GenlBackend *backend = (GenlBackend *)self;

    if (backend->loop)
        g_main_loop_quit(backend->loop);

    if (backend->thread) {
        g_thread_join(backend->thread);
        backend->thread = NULL;
    }
}

static void genl_backend_free(ServerEventListenerBackend *self)
{
    GenlBackend *backend = (GenlBackend *)self;

    if (backend->sock) {
        nl_socket_free(backend->sock);
        backend->sock = NULL;
    }

    g_free(backend);
}

ServerEventListenerBackend *genl_backend_new(FileEventHandler handler,
                                              gpointer user_data)
{
    GenlBackend *backend = g_new0(GenlBackend, 1);
    backend->handler = handler;
    backend->user_data = user_data;

    backend->parent.start = genl_backend_start;
    backend->parent.stop = genl_backend_stop;
    backend->parent.free = genl_backend_free;

    backend->sock = nl_socket_alloc();
    if (!backend->sock) {
        g_critical("Failed to allocate netlink socket");
        g_free(backend);
        return NULL;
    }

    int ret = genl_connect(backend->sock);
    if (ret < 0) {
        g_critical("Failed to connect to generic netlink: %s", strerror(-ret));
        nl_socket_free(backend->sock);
        g_free(backend);
        return NULL;
    }

    set_max_socket_receive_buffer_size(backend->sock);

    nl_socket_disable_seq_check(backend->sock);
    nl_socket_disable_auto_ack(backend->sock);

    if (!join_multicast_group(backend->sock, VFSMONITOR_MCG_DENTRY_NAME)) {
        g_critical("Failed to join dentry multicast group");
        nl_socket_free(backend->sock);
        g_free(backend);
        return NULL;
    }

    ret = nl_socket_modify_cb(backend->sock, NL_CB_VALID, NL_CB_CUSTOM,
                              netlink_event_handler, backend);
    if (ret < 0) {
        g_critical("Failed to set netlink callback: %s", strerror(-ret));
        nl_socket_free(backend->sock);
        g_free(backend);
        return NULL;
    }

    g_message("Genl event listener created");
    return (ServerEventListenerBackend *)backend;
}
