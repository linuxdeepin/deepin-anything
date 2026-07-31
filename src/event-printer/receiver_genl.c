// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "transport.h"

#include <glib.h>
#include <glib-unix.h>
#include <string.h>
#include <errno.h>

#include <netlink/socket.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <netlink/attr.h>

#include "vfs_genl.h"

/* Per-socket state. One socket joins both multicast groups. */
typedef struct {
    struct nl_sock *sock;
    int             fd;
    guint           source_id;
    guint64         dentry_count;
    guint64         proc_count;
    void          (*on_event)(const EpEvent *ev, gpointer user_data);
    gpointer        user_data;
} GenlReceiver;

/* Parse a VFSMONITOR_C_NOTIFY message into a dentry EpEvent. */
static gboolean parse_dentry_msg(struct nlmsghdr *nlhdr, EpEvent *out)
{
    struct nlattr *attrs[VFSMONITOR_A_MAX + 1];

    if (genlmsg_parse(nlhdr, 0, attrs, VFSMONITOR_A_MAX, vfsmonitor_genl_policy) < 0)
        return FALSE;

    if (!attrs[VFSMONITOR_A_ACT] || !attrs[VFSMONITOR_A_PATH])
        return FALSE;

    memset(out, 0, sizeof(*out));
    out->is_proc = FALSE;
    out->action = nla_get_u8(attrs[VFSMONITOR_A_ACT]);
    out->cookie = attrs[VFSMONITOR_A_COOKIE] ? nla_get_u32(attrs[VFSMONITOR_A_COOKIE]) : 0;
    out->seq    = attrs[VFSMONITOR_A_SEQ]    ? nla_get_u32(attrs[VFSMONITOR_A_SEQ])    : 0;
    out->major  = attrs[VFSMONITOR_A_MAJOR]  ? nla_get_u16(attrs[VFSMONITOR_A_MAJOR])  : 0;
    out->minor  = attrs[VFSMONITOR_A_MINOR]  ? nla_get_u8(attrs[VFSMONITOR_A_MINOR])  : 0;

    const char *path = nla_get_string(attrs[VFSMONITOR_A_PATH]);
    g_strlcpy(out->path, path ? path : "", sizeof(out->path));
    return TRUE;
}

/* Parse a VFSMONITOR_C_NOTIFY_PROCESS_INFO message into a proc EpEvent. */
static gboolean parse_proc_msg(struct nlmsghdr *nlhdr, EpEvent *out)
{
    struct nlattr *attrs[VFSMONITOR_A_MAX + 1];

    if (genlmsg_parse(nlhdr, 0, attrs, VFSMONITOR_A_MAX, vfsmonitor_genl_policy) < 0)
        return FALSE;

    if (!attrs[VFSMONITOR_A_UID] || !attrs[VFSMONITOR_A_TGID] || !attrs[VFSMONITOR_A_PATH])
        return FALSE;

    memset(out, 0, sizeof(*out));
    out->is_proc = TRUE;
    out->uid  = nla_get_u32(attrs[VFSMONITOR_A_UID]);
    out->tgid = nla_get_s32(attrs[VFSMONITOR_A_TGID]);
    out->seq = attrs[VFSMONITOR_A_SEQ] ? nla_get_u32(attrs[VFSMONITOR_A_SEQ]) : 0;
    const char *path = nla_get_string(attrs[VFSMONITOR_A_PATH]);
    g_strlcpy(out->path, path ? path : "", sizeof(out->path));
    return TRUE;
}

/* libnl callback for each valid netlink message. */
static int genl_msg_cb(struct nl_msg *msg, void *arg)
{
    GenlReceiver *r = arg;
    struct nlmsghdr *nlhdr = nlmsg_hdr(msg);
    struct genlmsghdr *genlhdr = genlmsg_hdr(nlhdr);
    EpEvent ev;

    switch (genlhdr->cmd) {
    case VFSMONITOR_C_NOTIFY:
        if (parse_dentry_msg(nlhdr, &ev)) {
            r->dentry_count++;
            if (r->on_event)
                r->on_event(&ev, r->user_data);
        }
        break;
    case VFSMONITOR_C_NOTIFY_PROCESS_INFO:
        if (parse_proc_msg(nlhdr, &ev)) {
            r->proc_count++;
            if (r->on_event)
                r->on_event(&ev, r->user_data);
        }
        break;
    default:
        break;
    }
    return NL_OK;
}

static gboolean on_nl_ready(G_GNUC_UNUSED gint fd,
                            G_GNUC_UNUSED GIOCondition condition,
                            gpointer data)
{
    GenlReceiver *r = data;
    int ret = nl_recvmsgs_default(r->sock);
    if (ret < 0)
        g_warning("nl_recvmsgs: %s", nl_geterror(ret));
    return G_SOURCE_CONTINUE;
}

GenlReceiver *ep_genl_receiver_new(void (*on_event)(const EpEvent *, gpointer),
                                   gpointer user_data)
{
    GenlReceiver *r = g_new0(GenlReceiver, 1);
    r->on_event = on_event;
    r->user_data = user_data;

    r->sock = nl_socket_alloc();
    if (!r->sock) {
        g_critical("nl_socket_alloc failed");
        g_free(r);
        return NULL;
    }

    /* Disable sequence checking / auto-ack — we only receive multicast. */
    nl_socket_disable_seq_check(r->sock);
    nl_socket_disable_auto_ack(r->sock);

    int ret = genl_connect(r->sock);
    if (ret < 0) {
        g_critical("genl_connect: %s", nl_geterror(ret));
        nl_socket_free(r->sock);
        g_free(r);
        return NULL;
    }

    /* Join both multicast groups. */
    int mcgrp_de = genl_ctrl_resolve_grp(r->sock, VFSMONITOR_FAMILY_NAME,
                                         VFSMONITOR_MCG_DENTRY_NAME);
    if (mcgrp_de < 0) {
        g_critical("resolve group %s: %s", VFSMONITOR_MCG_DENTRY_NAME,
                   nl_geterror(mcgrp_de));
        nl_socket_free(r->sock);
        g_free(r);
        return NULL;
    }
    int mcgrp_pi = genl_ctrl_resolve_grp(r->sock, VFSMONITOR_FAMILY_NAME,
                                         VFSMONITOR_MCG_PROCESS_INFO_NAME);
    if (mcgrp_pi < 0) {
        g_critical("resolve group %s: %s", VFSMONITOR_MCG_PROCESS_INFO_NAME,
                   nl_geterror(mcgrp_pi));
        nl_socket_free(r->sock);
        g_free(r);
        return NULL;
    }

    ret = nl_socket_add_membership(r->sock, mcgrp_de);
    if (ret < 0) {
        g_critical("join %s: %s", VFSMONITOR_MCG_DENTRY_NAME, nl_geterror(ret));
        nl_socket_free(r->sock);
        g_free(r);
        return NULL;
    }
    ret = nl_socket_add_membership(r->sock, mcgrp_pi);
    if (ret < 0) {
        g_critical("join %s: %s", VFSMONITOR_MCG_PROCESS_INFO_NAME, nl_geterror(ret));
        nl_socket_free(r->sock);
        g_free(r);
        return NULL;
    }

    ret = nl_socket_modify_cb(r->sock, NL_CB_VALID, NL_CB_CUSTOM,
                              genl_msg_cb, r);
    if (ret < 0) {
        g_critical("modify_cb: %s", nl_geterror(ret));
        nl_socket_free(r->sock);
        g_free(r);
        return NULL;
    }

    r->fd = nl_socket_get_fd(r->sock);
    r->source_id = g_unix_fd_add(r->fd,
                                 G_IO_IN | G_IO_HUP | G_IO_ERR,
                                 on_nl_ready, r);

    g_message("connected to genl family '%s' (fd=%d)", VFSMONITOR_FAMILY_NAME, r->fd);
    return r;
}

void ep_genl_receiver_free(GenlReceiver *r)
{
    if (!r)
        return;
    if (r->dentry_count > 0)
        g_message("dentry events: %" G_GUINT64_FORMAT, r->dentry_count);
    if (r->proc_count > 0)
        g_message("proc events:   %" G_GUINT64_FORMAT, r->proc_count);
    if (r->source_id > 0)
        g_source_remove(r->source_id);
    if (r->sock)
        nl_socket_free(r->sock);
    g_free(r);
}
