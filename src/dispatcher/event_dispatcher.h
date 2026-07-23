// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef EVENT_DISPATCHER_H
#define EVENT_DISPATCHER_H

#define G_LOG_USE_STRUCTURED
#include <glib.h>
#include <sys/types.h>

G_BEGIN_DECLS

/**
 * DISPATCH_MAX_PATH_LEN:
 *
 * Maximum length for file paths in dispatched events.
 */
#define DISPATCH_MAX_PATH_LEN 4096

/**
 * dispatch_event_t:
 * @event_action: VFS action code
 * @cookie: Unique identifier for pairing related events (e.g. rename
 *     from/to). Zero for events that have no related counterpart.
 * @event_path: Absolute path of the affected file/directory
 *
 * A file event dispatched from sender to receiver.
 * Sent as variable-length: only offsetof(dispatch_event_t, event_path)
 * + strlen(event_path) + 1 bytes are transmitted.
 */
typedef struct {
    gint32   event_action;
    guint32  cookie;
    gchar    event_path[DISPATCH_MAX_PATH_LEN];
} dispatch_event_t;

/**
 * GetUserSubscribedFunc:
 * @uid: the client's real UID (from SO_PEERCRED)
 * @user_data: user data passed via dispatch_config_t
 *
 * Returns: (transfer full) (nullable): a %NULL-terminated string array
 *     of fully-resolved path prefixes for @uid, or %NULL if the user
 *     has no subscriptions. The caller must free the result with
 *     g_strfreev().
 */
typedef gchar **(*GetUserSubscribedFunc)(uid_t uid, gpointer user_data);

/**
 * dispatch_config_t:
 * @get_user_subscribed: callback that returns a %NULL-terminated array of
 *     fully-resolved path prefixes for a given UID. Must not be %NULL.
 *     Invoked once per UID — only for the first client of that UID at
 *     accept time. If the callback returns %NULL, the client connection
 *     is rejected entirely.
 * @user_data: opaque pointer passed to @get_user_subscribed.
 * @max_users: Maximum number of distinct UIDs (0 = unlimited).
 * @max_connections_per_user: Maximum connections per UID (0 = unlimited).
 *
 * Configuration for an EventDispatcher. Passed to event_dispatcher_new().
 * Both @get_user_subscribed and @config itself must be non-%NULL (caller
 * bug otherwise). The dispatcher does not own @user_data; the caller
 * retains ownership.
 */
typedef struct {
    GetUserSubscribedFunc get_user_subscribed;
    gpointer              user_data;
    guint                 max_users;
    guint                 max_connections_per_user;
} dispatch_config_t;

/* ── Sender (server) API ────────────────────────────────────────── */

typedef struct EventDispatcher EventDispatcher;

/**
 * event_dispatcher_new: (constructor)
 * @address: Unix socket path to listen on (will be unlinked if stale)
 * @config: configuration including subscription callback and connection
 *     limits. Must not be %NULL; @config->get_user_subscribed must also
 *     be non-%NULL (programmer error otherwise). The dispatcher does not
 *     own the callback's user_data; it must remain valid for the
 *     dispatcher's lifetime.
 *
 * Creates a new event dispatcher that listens on a Unix Domain Socket
 * (SOCK_SEQPACKET). The caller retains ownership of @config; the
 * dispatcher stores the callback and user_data pointer directly.
 *
 * Returns: (transfer full) (nullable): a new EventDispatcher, or NULL on failure
 */
EventDispatcher *event_dispatcher_new(const char *address,
                                      const dispatch_config_t *config);

/**
 * event_dispatcher_free: (skip)
 * @dispatcher: (nullable)
 *
 * Frees the dispatcher and all associated resources. Closes the listening
 * socket and all accepted client connections. Safe to call on NULL.
 *
 * Not thread-safe relative to send/accept — the caller must ensure no
 * concurrent calls.
 */
void event_dispatcher_free(EventDispatcher *dispatcher);

/**
 * event_dispatcher_send:
 * @dispatcher: an EventDispatcher
 * @event: the event to dispatch
 *
 * Dispatches @event to all connected clients whose subscription rules
 * match @event->event_path. This function does not accept pending
 * connections — the caller must call event_dispatcher_accept() (typically
 * driven by monitoring the socket fd from event_dispatcher_get_socket())
 * to accept new connections before sending.
 *
 * Client fds are non-blocking. If send() returns EAGAIN/EWOULDBLOCK,
 * the client is too slow and its fd is closed and removed to prevent
 * memory exhaustion.
 *
 * Returns: %TRUE if the event was processed (even if no clients received it),
 *          %FALSE on critical error
 */
gboolean event_dispatcher_send(EventDispatcher *dispatcher,
                               const dispatch_event_t *event);

/**
 * event_dispatcher_get_socket:
 * @dispatcher: an EventDispatcher
 *
 * Returns the listening socket file descriptor. The caller can monitor
 * this fd for readability (e.g., via poll/epoll) and call
 * event_dispatcher_accept() when readable to accept pending connections.
 *
 * Returns: the listening socket fd, or -1 on error
 */
int event_dispatcher_get_socket(EventDispatcher *dispatcher);

/**
 * event_dispatcher_accept:
 * @dispatcher: an EventDispatcher
 *
 * Accepts all pending client connections in a non-blocking loop.
 * Before accepting, cleans up disconnected existing clients. For each
 * new connection: authenticates via SO_PEERCRED, checks user and
 * connection limits, and registers the client. Over-limit clients and
 * clients with no subscriptions (callback returns %NULL) are rejected
 * (fd closed).
 *
 * Returns: %TRUE if at least one connection was accepted, %FALSE otherwise
 */
gboolean event_dispatcher_accept(EventDispatcher *dispatcher);

/* ── Receiver (client) API ──────────────────────────────────────── */

typedef struct EventReceiver EventReceiver;

/**
 * EventReceiveResult:
 * @EVENT_RECEIVE_OK: Event received successfully
 * @EVENT_RECEIVE_DISCONNECTED: Server closed the connection
 * @EVENT_RECEIVE_INTERRUPTED: recv() was interrupted by a signal (EINTR)
 * @EVENT_RECEIVE_ERROR: Other error occurred
 *
 * Result codes for event_receiver_receive().
 */
typedef enum {
    EVENT_RECEIVE_OK,
    EVENT_RECEIVE_DISCONNECTED,
    EVENT_RECEIVE_INTERRUPTED,
    EVENT_RECEIVE_ERROR
} EventReceiveResult;

/**
 * event_receiver_new: (constructor)
 * @address: Unix socket path to connect to
 *
 * Creates a new event receiver that connects to a dispatcher.
 *
 * Returns: (transfer full) (nullable): a new EventReceiver, or NULL on failure
 */
EventReceiver *event_receiver_new(const char *address);

/**
 * event_receiver_free: (skip)
 * @receiver: (nullable)
 *
 * Frees the receiver and closes the connection. Safe to call on NULL.
 */
void event_receiver_free(EventReceiver *receiver);

/**
 * event_receiver_get_socket:
 * @receiver: an EventReceiver
 *
 * Returns the connected socket file descriptor. The caller can monitor
 * this fd for readability (e.g., via poll/epoll) to implement non-blocking
 * event loops with timeout and graceful shutdown.
 *
 * Returns: the connected socket fd, or -1 on error
 */
int event_receiver_get_socket(EventReceiver *receiver);

/**
 * event_receiver_receive:
 * @receiver: an EventReceiver
 * @event: (out caller-allocates): output event buffer
 *
 * Blocks until an event is received from the dispatcher or the
 * connection is lost. SOCK_SEQPACKET preserves message boundaries,
 * so a single recv() returns the complete event.
 *
 * Returns: #EventReceiveResult indicating the outcome
 */
EventReceiveResult event_receiver_receive(EventReceiver *receiver,
                                          dispatch_event_t *event);

G_END_DECLS

#endif /* EVENT_DISPATCHER_H */
