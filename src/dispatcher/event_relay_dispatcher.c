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

/**
 * prune_disconnected_channels:
 * @dispatcher: an #EventRelayDispatcher
 *
 * Remove sender fds whose peer (client end) has closed.  A non-blocking
 * MSG_PEEK recv returning 0 signals EOF — the client disconnected without
 * a send() having noticed yet.  Without this, stale fds occupy channel
 * slots and cause get_event_channel() to reject replacements despite free
 * capacity.  Iterates in reverse so remove_index_fast (which swaps the
 * last element into the freed slot) stays safe during iteration.
 */
static void prune_disconnected_channels(EventRelayDispatcher *dispatcher)
{
    for (gint i = dispatcher->socket_list->len - 1; i >= 0; i--) {
        int fd = GPOINTER_TO_INT(g_ptr_array_index(dispatcher->socket_list, i));

        char dummy;
        ssize_t ret = recv(fd, &dummy, 1, MSG_PEEK | MSG_DONTWAIT);
        if (ret == 0) {
            g_message("relay: pruning disconnected sender fd %d, total after: %u",
                      fd, dispatcher->socket_list->len - 1);
            g_ptr_array_remove_index_fast(dispatcher->socket_list, i);
        }
    }
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

    /* Prune channels whose client end has disconnected so the limit
     * reflects live connections, not stale fds left by closed peers. */
    prune_disconnected_channels(dispatcher);

    /* Check channel limit. */
    if (dispatcher->max_event_channel > 0 &&
        dispatcher->socket_list->len >= dispatcher->max_event_channel) {
        g_message("relay: max_event_channel limit (%u) reached",
                  dispatcher->max_event_channel);
        return -1;
    }

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, fds) < 0) {
        g_critical("relay: socketpair() failed: %s", strerror(errno));
        return -1;
    }

    /* Enlarge receive buffer so bursts of events don't cause packet drops.
     * SO_RCVBUFFORCE bypasses the system rmem_max limit (requires CAP_NET_ADMIN
     * or root). 8 MiB absorbs large bursts of small event packets. */
    int rcv_buf_size = 8 << 20;  /* 8 MiB */
    if (setsockopt(fds[1], SOL_SOCKET, SO_RCVBUFFORCE, &rcv_buf_size, sizeof(rcv_buf_size)) < 0) {
        g_warning("relay: setsockopt(SO_RCVBUFFORCE) on client failed: %s, falling back to SO_RCVBUF",
                  strerror(errno));
        /* SO_RCVBUFFORCE requires CAP_NET_ADMIN; fall back to SO_RCVBUF
         * which respects rmem_max but needs no special capability. */
        if (setsockopt(fds[1], SOL_SOCKET, SO_RCVBUF, &rcv_buf_size, sizeof(rcv_buf_size)) < 0)
            g_warning("relay: setsockopt(SO_RCVBUF) on client failed: %s", strerror(errno));
    }

    /* Enlarge send buffer on the sender end so bursts of events don't
     * exhaust the kernel send queue and trigger EAGAIN/EWOULDBLOCK, which
     * would cause the slow-client kick.  SO_SNDBUFFORCE bypasses wmem_max
     * (requires CAP_NET_ADMIN, already granted via the systemd unit).
     * Falls back to SO_SNDBUF which respects wmem_max. */
    int snd_buf_size = 8 << 20;  /* 8 MiB */
    if (setsockopt(fds[0], SOL_SOCKET, SO_SNDBUFFORCE, &snd_buf_size, sizeof(snd_buf_size)) < 0) {
        g_warning("relay: setsockopt(SO_SNDBUFFORCE) on sender failed: %s, falling back to SO_SNDBUF",
                  strerror(errno));
        if (setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &snd_buf_size, sizeof(snd_buf_size)) < 0)
            g_warning("relay: setsockopt(SO_SNDBUF) on sender failed: %s", strerror(errno));
    }

    /* Store sender end (fds[0]) in the list, return client end (fds[1]). */
    g_ptr_array_add(dispatcher->socket_list, GINT_TO_POINTER(fds[0]));

    g_message("relay: created event channel (sender fd=%d, client fd=%d, total=%u)",
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
                g_message("relay: slow client fd %d, kicking", fd);
                /* Remove from array first so the destroy notify owns the
                 * close — no double-close, no fd-reuse hazard. */
                g_ptr_array_remove_index_fast(dispatcher->socket_list, i);
            } else if (errno == EPIPE || errno == ECONNRESET) {
                g_message("relay: broken connection fd %d, removing", fd);
                g_ptr_array_remove_index_fast(dispatcher->socket_list, i);
            } else {
                g_critical("relay: send() to fd %d failed: %s",
                          fd, strerror(errno));
                ok = FALSE;
                /* Continue to remaining channels — broadcast semantics
                 * mean one client's error must not starve the others. */
            }
        }
    }

    return ok;
}
