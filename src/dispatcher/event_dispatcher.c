// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#define _GNU_SOURCE
#define G_LOG_USE_STRUCTURED
#include "event_dispatcher.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

/* ── Sender data structure ──────────────────────────────────────── */

struct EventDispatcher {
    int listen_fd;          /* listening socket (SOCK_SEQPACKET, non-blocking) */
    char *address;          /* socket path (owned copy)                        */

    /* UID → dynamic array of client fds (GINT_TO_POINTER)                  */
    GHashTable *uid_to_clients;   /* key: GUINT_TO_POINTER(uid_t)
                                     value: GPtrArray<int-as-ptr>           */

    /* path prefix → set of UIDs (GHashTable as set)                        */
    GHashTable *event_to_user;    /* key: gchar* prefix
                                     value: GHashTable<uid-as-ptr,1>        */

    /* Configuration (not owned)                                        */
    GetUserSubscribedFunc get_user_subscribed; /* callback for per-UID prefixes */
    gpointer              user_data;           /* opaque data for callback     */
    guint max_users;              /* max distinct UIDs (0 = unlimited)      */
    guint max_connections_per_user; /* max connections per UID (0 = unlim)   */
};

/* ── Helpers ────────────────────────────────────────────────────── */

/**
 * ptr_array_destroy:
 * @array: (nullable): a GPtrArray
 *
 * GDestroyNotify-compatible wrapper for g_ptr_array_free(array, TRUE).
 */
static void ptr_array_destroy(gpointer array)
{
    if (array != NULL)
        g_ptr_array_free((GPtrArray *)array, TRUE);
}

/**
 * add_uid_to_prefix:
 * @event_to_user: the path prefix → UID set hash table
 * @prefix: the expanded path prefix (owned by caller)
 * @uid: the UID to add
 *
 * Adds @uid to the UID set for @prefix in @event_to_user.
 * The UID set is a GHashTable used as a set (key = GUINT_TO_POINTER(uid),
 * value = GUINT_TO_POINTER(1)).
 */
static void add_uid_to_prefix(GHashTable *event_to_user,
                              const gchar *prefix, uid_t uid)
{
    GHashTable *uid_set = g_hash_table_lookup(event_to_user, prefix);
    if (uid_set == NULL) {
        uid_set = g_hash_table_new(g_direct_hash, g_direct_equal);
        gchar *prefix_copy = g_strdup(prefix);
        g_hash_table_insert(event_to_user, prefix_copy, uid_set);
    }
    g_hash_table_insert(uid_set, GUINT_TO_POINTER(uid), GUINT_TO_POINTER(1));
}

/**
 * remove_client_fd:
 * @dispatcher: an EventDispatcher
 * @fd: the client fd to remove
 * @uid: the UID that owns this fd
 *
 * Removes @fd from uid_to_clients[@uid] and closes it.
 * If the UID has no more fds, removes the UID entry from uid_to_clients.
 */
static void remove_client_fd(EventDispatcher *dispatcher, int fd, uid_t uid)
{
    GPtrArray *clients = g_hash_table_lookup(dispatcher->uid_to_clients,
                                             GUINT_TO_POINTER(uid));
    if (clients == NULL)
        return;

    for (guint i = 0; i < clients->len; i++) {
        if (GPOINTER_TO_INT(g_ptr_array_index(clients, i)) == fd) {
            g_ptr_array_remove_index_fast(clients, i);
            break;
        }
    }

    close(fd);

    if (clients->len == 0) {
        /* g_hash_table_remove invokes ptr_array_destroy (the value destroy
         * func) which frees the GPtrArray — do not free it manually. */
        g_hash_table_remove(dispatcher->uid_to_clients, GUINT_TO_POINTER(uid));
    }
}

/**
 * cleanup_disconnected_clients:
 * @dispatcher: an EventDispatcher
 *
 * Iterates all existing client fds and checks for disconnection using
 * recv(MSG_PEEK | MSG_DONTWAIT). If a client has disconnected (recv
 * returns 0 or EPIPE/ECONNRESET), closes fd and removes it.
 */
static void cleanup_disconnected_clients(EventDispatcher *dispatcher)
{
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, dispatcher->uid_to_clients);

    while (g_hash_table_iter_next(&iter, &key, &value)) {
        uid_t uid = GPOINTER_TO_UINT(key);
        GPtrArray *clients = (GPtrArray *)value;

        for (gint i = clients->len - 1; i >= 0; i--) {
            int fd = GPOINTER_TO_INT(g_ptr_array_index(clients, i));
            char buf[1];
            ssize_t ret = recv(fd, buf, 1, MSG_PEEK | MSG_DONTWAIT);
            if (ret == 0 || (ret < 0 && (errno == EPIPE || errno == ECONNRESET))) {
                g_debug("client fd %d (uid %u) disconnected", fd, uid);
                close(fd);
                g_ptr_array_remove_index_fast(clients, i);
            }
        }

        /* If no more clients for this UID, remove from hash table via iterator */
        if (clients->len == 0) {
            g_hash_table_iter_remove(&iter);
        }
    }
}

/**
 * accept_one:
 * @dispatcher: an EventDispatcher
 *
 * Accepts one pending connection. Authenticates via SO_PEERCRED,
 * checks max_users and max_connections_per_user limits, registers
 * the client in uid_to_clients and queries the subscription callback
 * for fully-resolved path prefixes before adding to event_to_user.
 *
 * The callback is invoked only for the first client of a given UID;
 * subsequent connections from the same UID reuse the prefixes already
 * registered. If the callback returns %NULL (no subscriptions), the
 * client connection is rejected entirely.
 *
 * Returns: %TRUE if a connection was accepted, %FALSE on EAGAIN or error
 */
static gboolean accept_one(EventDispatcher *dispatcher)
{
    int fd;
    do {
        fd = accept4(dispatcher->listen_fd, NULL, NULL, SOCK_NONBLOCK);
    } while (fd < 0 && errno == EINTR);

    if (fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return FALSE;
        g_warning("accept4 failed: %s", strerror(errno));
        return FALSE;
    }

    /* Enlarge send buffer so bursts of 4 KB events don't cause EAGAIN
     * (which currently results in kicking the client). */
    int buf_size = 1 << 20;  /* 1 MiB */
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size)) < 0)
        g_debug("setsockopt(SO_SNDBUF) failed: %s", strerror(errno));
    struct ucred cred;
    socklen_t cred_len = sizeof(cred);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) < 0) {
        g_warning("getsockopt(SO_PEERCRED) failed: %s", strerror(errno));
        close(fd);
        return FALSE;
    }

    uid_t uid = cred.uid;

    /* Check max_users limit */
    if (dispatcher->max_users > 0 &&
        !g_hash_table_contains(dispatcher->uid_to_clients, GUINT_TO_POINTER(uid)) &&
        g_hash_table_size(dispatcher->uid_to_clients) >= dispatcher->max_users) {
        g_debug("max_users limit reached, rejecting uid %u", uid);
        close(fd);
        return FALSE;
    }

    /* Check max_connections_per_user limit */
    GPtrArray *clients = g_hash_table_lookup(dispatcher->uid_to_clients,
                                              GUINT_TO_POINTER(uid));
    if (dispatcher->max_connections_per_user > 0 && clients != NULL &&
        clients->len >= dispatcher->max_connections_per_user) {
        g_debug("max_connections_per_user limit reached for uid %u", uid);
        close(fd);
        return FALSE;
    }

    /* Resolve subscriptions for the first client of this UID.
     * If the callback returns NULL (no subscriptions), reject the
     * connection entirely — the client would receive no events. */
    gchar **prefixes = NULL;
    if (clients == NULL) {
        prefixes = dispatcher->get_user_subscribed(uid, dispatcher->user_data);
        if (prefixes == NULL) {
            g_debug("uid %u has no subscriptions, rejecting", uid);
            close(fd);
            return FALSE;
        }
    }

    /* Register client */
    if (clients == NULL) {
        clients = g_ptr_array_new();
        g_hash_table_insert(dispatcher->uid_to_clients,
                            GUINT_TO_POINTER(uid), clients);
    }
    g_ptr_array_add(clients, GINT_TO_POINTER(fd));

    /* Add subscription prefixes (first client only) */
    if (prefixes != NULL) {
        for (guint i = 0; prefixes[i] != NULL; i++) {
            add_uid_to_prefix(dispatcher->event_to_user, prefixes[i], uid);
        }
        g_strfreev(prefixes);
    }

    g_debug("accepted client fd %d, uid %u", fd, uid);
    return TRUE;
}

/**
 * accept_pending:
 * @dispatcher: an EventDispatcher
 *
 * Cleans up disconnected clients, then accepts all pending connections
 * in a non-blocking loop.
 *
 * Returns: %TRUE if at least one connection was accepted
 */
static gboolean accept_pending(EventDispatcher *dispatcher)
{
    cleanup_disconnected_clients(dispatcher);

    gboolean any_accepted = FALSE;
    while (accept_one(dispatcher))
        any_accepted = TRUE;

    return any_accepted;
}

/* ── Public API ─────────────────────────────────────────────────── */

EventDispatcher *event_dispatcher_new(const char *address,
                                      const dispatch_config_t *config)
{
    g_return_val_if_fail(address != NULL, NULL);
    g_return_val_if_fail(config != NULL, NULL);
    g_return_val_if_fail(config->get_user_subscribed != NULL, NULL);

    EventDispatcher *d = g_new0(EventDispatcher, 1);

    d->listen_fd = -1;
    d->address = g_strdup(address);
    d->uid_to_clients = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                              NULL, (GDestroyNotify)ptr_array_destroy);
    d->event_to_user = g_hash_table_new_full(g_str_hash, g_str_equal,
                                             g_free, (GDestroyNotify)g_hash_table_destroy);

    d->get_user_subscribed = config->get_user_subscribed;
    d->user_data = config->user_data;
    d->max_users = config->max_users;
    d->max_connections_per_user = config->max_connections_per_user;

    /* Create SOCK_SEQPACKET socket (preserves message boundaries) */
    d->listen_fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0);
    if (d->listen_fd < 0) {
        g_critical("socket() failed: %s", strerror(errno));
        goto fail;
    }

    /* Unlink stale socket path if it exists */
    unlink(d->address);

    /* Bind */
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    g_strlcpy(addr.sun_path, d->address, sizeof(addr.sun_path));

    if (bind(d->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        g_critical("bind(%s) failed: %s", d->address, strerror(errno));
        goto fail;
    }

    /* chmod 0666 so non-root users can connect (connect requires write
     * permission on the socket file; default umask 022 yields 0755 which
     * excludes write for "other"). */
    if (chmod(d->address, 0666) < 0) {
        g_critical("chmod(%s, 0666) failed: %s", d->address, strerror(errno));
        goto fail;
    }

    if (listen(d->listen_fd, 16) < 0) {
        g_critical("listen() failed: %s", strerror(errno));
        goto fail;
    }

    return d;

fail:
    event_dispatcher_free(d);
    return NULL;
}

void event_dispatcher_free(EventDispatcher *dispatcher)
{
    if (dispatcher == NULL)
        return;

    if (dispatcher->listen_fd >= 0) {
        close(dispatcher->listen_fd);
    }

    /* Only unlink the socket path if we successfully created the socket
     * (which implies we also called unlink + bind in _new). If socket()
     * failed, the path may belong to another process and must not be
     * removed. */
    if (dispatcher->listen_fd >= 0 && dispatcher->address != NULL) {
        unlink(dispatcher->address);
    }

    if (dispatcher->address != NULL) {
        g_free(dispatcher->address);
    }

    /* Close all client fds */
    if (dispatcher->uid_to_clients != NULL) {
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, dispatcher->uid_to_clients);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            GPtrArray *clients = (GPtrArray *)value;
            for (guint i = 0; i < clients->len; i++) {
                int fd = GPOINTER_TO_INT(g_ptr_array_index(clients, i));
                close(fd);
            }
        }
        g_hash_table_destroy(dispatcher->uid_to_clients);
    }

    if (dispatcher->event_to_user != NULL)
        g_hash_table_destroy(dispatcher->event_to_user);

    g_free(dispatcher);
}

gboolean event_dispatcher_send(EventDispatcher *dispatcher,
                               const dispatch_event_t *event)
{
    g_return_val_if_fail(dispatcher != NULL, FALSE);
    g_return_val_if_fail(event != NULL, FALSE);

    /* Reject if event_path is not null-terminated within bounds (overrun) */
    size_t path_len = strnlen(event->event_path, DISPATCH_MAX_PATH_LEN);
    if (path_len == DISPATCH_MAX_PATH_LEN) {
        g_warning("event_path not null-terminated, dropping event");
        return FALSE;
    }

    size_t send_size = offsetof(dispatch_event_t, event_path) + path_len + 1;

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, dispatcher->event_to_user);

    while (g_hash_table_iter_next(&iter, &key, &value)) {
        const gchar *prefix = (const gchar *)key;
        GHashTable *uid_set = (GHashTable *)value;

        if (!g_str_has_prefix(event->event_path, prefix))
            continue;

        GHashTableIter uid_iter;
        gpointer uid_key, uid_val;
        g_hash_table_iter_init(&uid_iter, uid_set);

        while (g_hash_table_iter_next(&uid_iter, &uid_key, &uid_val)) {
            uid_t uid = GPOINTER_TO_UINT(uid_key);
            GPtrArray *clients = g_hash_table_lookup(dispatcher->uid_to_clients,
                                                      GUINT_TO_POINTER(uid));
            if (clients == NULL)
                continue;

            for (gint i = clients->len - 1; i >= 0; i--) {
                int fd = GPOINTER_TO_INT(g_ptr_array_index(clients, i));
                ssize_t ret = send(fd, event, send_size, MSG_NOSIGNAL);
                if (ret < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        g_debug("slow client fd %d (uid %u), kicking", fd, uid);
                        remove_client_fd(dispatcher, fd, uid);
                    } else if (errno == EPIPE || errno == ECONNRESET) {
                        g_debug("broken connection fd %d (uid %u)", fd, uid);
                        remove_client_fd(dispatcher, fd, uid);
                    } else {
                        g_warning("send() to fd %d failed: %s",
                                  fd, strerror(errno));
                    }
                }
            }
            /* remove_client_fd already handles empty-array cleanup when the
             * last fd for a UID is removed, so no post-loop action needed. */
        }
    }

    return TRUE;
}

int event_dispatcher_get_socket(EventDispatcher *dispatcher)
{
    g_return_val_if_fail(dispatcher != NULL, -1);
    return dispatcher->listen_fd;
}

gboolean event_dispatcher_accept(EventDispatcher *dispatcher)
{
    g_return_val_if_fail(dispatcher != NULL, FALSE);
    return accept_pending(dispatcher);
}
