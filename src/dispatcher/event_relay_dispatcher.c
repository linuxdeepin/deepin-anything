// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#define _GNU_SOURCE
#define G_LOG_USE_STRUCTURED
#include "event_relay_dispatcher.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* ── Data structure ─────────────────────────────────────────────── */

struct EventRelayDispatcher {
    GPtrArray *socket_list;       /* sender-side fds from socketpair()       */
    guint      max_event_channel; /* 0 = unlimited                           */
};

/* ── Helpers ────────────────────────────────────────────────────── */

/**
 * close_fd:
 * @ptr: a #GINT_TO_POINTER-encoded fd
 *
 * GDestroyNotify-compatible wrapper that closes the fd stored as @ptr.
 */
static void close_fd(gpointer ptr)
{
    int fd = GPOINTER_TO_INT(ptr);
    if (fd >= 0)
        close(fd);
}

/* ── Public API ─────────────────────────────────────────────────── */

EventRelayDispatcher *event_relay_dispatcher_new(guint max_event_channel)
{
    EventRelayDispatcher *d = g_new0(EventRelayDispatcher, 1);
    d->socket_list = g_ptr_array_new_full(8, close_fd);
    d->max_event_channel = max_event_channel;
    return d;
}

void event_relay_dispatcher_free(EventRelayDispatcher *dispatcher)
{
    if (dispatcher == NULL)
        return;

    /* Closes every fd via the close_fd destroy notify. */
    if (dispatcher->socket_list != NULL)
        g_ptr_array_free(dispatcher->socket_list, TRUE);

    g_free(dispatcher);
}

int event_relay_dispatcher_get_event_channel(EventRelayDispatcher *dispatcher)
{
    g_return_val_if_fail(dispatcher != NULL, -1);

    /* Check channel limit. */
    if (dispatcher->max_event_channel > 0 &&
        dispatcher->socket_list->len >= dispatcher->max_event_channel) {
        g_debug("relay: max_event_channel limit (%u) reached",
                dispatcher->max_event_channel);
        return -1;
    }

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0, fds) < 0) {
        g_warning("relay: socketpair() failed: %s", strerror(errno));
        return -1;
    }

    /* Enlarge send buffer so bursts of events don't cause EAGAIN. */
    int buf_size = 1 << 20;  /* 1 MiB */
    if (setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size)) < 0)
        g_debug("relay: setsockopt(SO_SNDBUF) on sender failed: %s",
                 strerror(errno));

    /* Store sender end (fds[0]) in the list, return client end (fds[1]). */
    g_ptr_array_add(dispatcher->socket_list, GINT_TO_POINTER(fds[0]));

    g_debug("relay: created event channel (sender fd=%d, client fd=%d, total=%u)",
            fds[0], fds[1], dispatcher->socket_list->len);

    return fds[1];
}

gboolean event_relay_dispatcher_send(EventRelayDispatcher *dispatcher,
                                     const void *buf, size_t len)
{
    g_return_val_if_fail(dispatcher != NULL, FALSE);
    g_return_val_if_fail(buf != NULL, FALSE);
    g_return_val_if_fail(len > 0, FALSE);

    /* Iterate in reverse so we can safely remove during iteration. */
    gboolean ok = TRUE;

    for (gint i = dispatcher->socket_list->len - 1; i >= 0; i--) {
        int fd = GPOINTER_TO_INT(g_ptr_array_index(dispatcher->socket_list, i));

        ssize_t ret;
        do {
            ret = send(fd, buf, len, MSG_NOSIGNAL);
        } while (ret < 0 && errno == EINTR);

        if (ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                g_debug("relay: slow client fd %d, kicking", fd);
                /* Remove from array first so the destroy notify owns the
                 * close — no double-close, no fd-reuse hazard. */
                g_ptr_array_remove_index_fast(dispatcher->socket_list, i);
            } else if (errno == EPIPE || errno == ECONNRESET) {
                g_debug("relay: broken connection fd %d", fd);
                g_ptr_array_remove_index_fast(dispatcher->socket_list, i);
            } else {
                g_warning("relay: send() to fd %d failed: %s",
                          fd, strerror(errno));
                ok = FALSE;
                /* Continue to remaining channels — broadcast semantics
                 * mean one client's error must not starve the others. */
            }
        }
    }

    return ok;
}
