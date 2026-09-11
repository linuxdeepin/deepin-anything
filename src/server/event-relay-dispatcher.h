// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SERVER_EVENT_RELAY_DISPATCHER_H
#define SERVER_EVENT_RELAY_DISPATCHER_H

#define G_LOG_USE_STRUCTURED
#include <glib.h>

#include "fs-event.h"

G_BEGIN_DECLS

/**
 * SERVER_RELAY_EVENT_PROTOCOL_ID:
 *
 * Protocol identifier for the raw #fs_event wire format used by
 * ServerEventRelayDispatcher. The value is sent to the client in the
 * #ServerEventRelayChannelFunc completion callback so the receiver knows
 * which struct to expect on the channel.
 *
 * Protocol id 0 is the legacy #dispatch_event_t format used by the
 * user-filter EventDispatcher. This id must be incremented only when the
 * event struct type on the wire changes.
 */
#define SERVER_RELAY_EVENT_PROTOCOL_ID 1u

/**
 * ServerEventRelayDispatcher:
 *
 * An opaque relay event dispatcher for the server. Unlike
 * #ServerEventDispatcher (which converts fs events to #dispatch_event_t
 * and filters by per-UID subscription prefixes), the relay dispatcher
 * forwards raw #fs_event structs to every connected channel without
 * conversion or filtering. The downstream relay agent is responsible for
 * its own conversion and filtering.
 *
 * Internally the dispatcher uses a dedicated worker thread so that every
 * call to the underlying #EventRelayDispatcher (get_event_channel, send)
 * happens on a single thread — the library is not thread-safe relative
 * to those operations.
 */

typedef struct ServerEventRelayDispatcher ServerEventRelayDispatcher;

/**
 * ServerEventRelayChannelFunc:
 * @dispatcher: the #ServerEventRelayDispatcher
 * @fd: the client end of the new event channel socketpair, or -1 on
 *     failure (e.g. the channel limit was reached, or the dispatcher is
 *     shutting down)
 * @event_protocol_id: the wire protocol id identifying the event struct
 *     the client will receive on the channel (currently
 *     %SERVER_RELAY_EVENT_PROTOCOL_ID)
 * @user_data: the user_data passed to
 *     server_event_relay_dispatcher_get_event_channel()
 *
 * Callback invoked by the relay dispatcher worker thread when a
 * get_event_channel request has been processed. The callback runs on the
 * worker thread; GDBus method-invocation returns are thread-safe, so a
 * future D-Bus GetEventChannel handler can complete its method call
 * directly from here.
 *
 * If @fd is -1 the channel could not be created (limit reached or
 * shutdown). The callback is still invoked so the caller can clean up
 * or return an error to its client.
 */
typedef void (*ServerEventRelayChannelFunc)(ServerEventRelayDispatcher *dispatcher,
                                             int fd,
                                             guint32 event_protocol_id,
                                             gpointer user_data);

/**
 * server_event_relay_dispatcher_new: (constructor)
 * @max_event_channel: Maximum number of concurrent event channels
 *     (0 = unlimited)
 *
 * Creates a new relay event dispatcher. The dispatcher must be started
 * with server_event_relay_dispatcher_start() before it can process
 * events or channel requests.
 *
 * Returns: (transfer full) (nullable): a new #ServerEventRelayDispatcher,
 *     or %NULL on failure
 */
ServerEventRelayDispatcher *server_event_relay_dispatcher_new(guint max_event_channel);

/**
 * server_event_relay_dispatcher_start:
 * @dispatcher: a #ServerEventRelayDispatcher
 *
 * Starts the worker thread. This function blocks until the worker thread
 * has completed its initialisation and is ready to process events.
 *
 * Returns: %TRUE if the worker thread started successfully, %FALSE on
 *     failure (in which case the dispatcher should be freed)
 */
gboolean server_event_relay_dispatcher_start(ServerEventRelayDispatcher *dispatcher);

/**
 * server_event_relay_dispatcher_stop:
 * @dispatcher: a #ServerEventRelayDispatcher
 *
 * Signals the worker thread to stop and blocks until it has exited.
 * Pending get_event_channel requests are completed with fd = -1.
 * Safe to call multiple times (idempotent).
 */
void server_event_relay_dispatcher_stop(ServerEventRelayDispatcher *dispatcher);

/**
 * server_event_relay_dispatcher_free: (skip)
 * @dispatcher: (nullable)
 *
 * Stops the dispatcher if running, drains all pending events and
 * requests, frees all resources. Safe to call on %NULL.
 */
void server_event_relay_dispatcher_free(ServerEventRelayDispatcher *dispatcher);

/**
 * server_event_relay_dispatcher_push_event:
 * @dispatcher: a #ServerEventRelayDispatcher
 * @event: (transfer full): the fs event to forward
 *
 * Enqueues @event for broadcast to all connected relay channels. This
 * function takes ownership of @event (which must have been allocated
 * with g_slice_new0) and frees it after the worker thread has processed
 * it. This function is cheap and thread-safe — it only pushes the
 * pointer onto a #GAsyncQueue. If the event queue is unavailable the
 * event is freed immediately to avoid a leak.
 */
void server_event_relay_dispatcher_push_event(ServerEventRelayDispatcher *dispatcher,
                                               fs_event *event);

/**
 * server_event_relay_dispatcher_get_event_channel:
 * @dispatcher: a #ServerEventRelayDispatcher
 * @callback: (nullable): function to call when the channel has been
 *     created, or %NULL to ignore the result
 * @user_data: data to pass to @callback
 *
 * Asynchronously requests a new event channel. The request is enqueued
 * and processed by the worker thread (which calls
 * event_relay_dispatcher_get_event_channel() on the library), ensuring
 * all #EventRelayDispatcher calls happen on a single thread. The
 * @callback is invoked on the worker thread with the resulting fd and
 * %SERVER_RELAY_EVENT_PROTOCOL_ID.
 *
 * Returns: %TRUE if the request was enqueued, %FALSE if the worker
 *     thread is not running or allocation failed (in which case
 *     @callback is never invoked)
 */
gboolean server_event_relay_dispatcher_get_event_channel(ServerEventRelayDispatcher *dispatcher,
                                                          ServerEventRelayChannelFunc callback,
                                                          gpointer user_data);

G_END_DECLS

#endif /* SERVER_EVENT_RELAY_DISPATCHER_H */
