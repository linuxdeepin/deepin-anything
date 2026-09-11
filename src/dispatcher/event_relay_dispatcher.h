// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef EVENT_RELAY_DISPATCHER_H
#define EVENT_RELAY_DISPATCHER_H

#include <glib.h>
#include <sys/types.h>

G_BEGIN_DECLS

/**
 * EventRelayDispatcher:
 *
 * An opaque relay event dispatcher. Unlike #EventDispatcher (which filters
 * events by per-UID subscription prefixes), the relay dispatcher broadcasts
 * every event to every connected channel without filtering. The downstream
 * relay agent is responsible for its own filtering.
 *
 * Communication is via pre-established #socketpair endpoints using
 * %SOCK_SEQPACKET (preserves message boundaries). Channel establishment is
 * driven by a D-Bus <literal>GetEventChannel</literal> method call: the
 * server-side handler calls event_relay_dispatcher_get_event_channel() to
 * create a socketpair, stores one end, and returns the other end to the
 * client via D-Bus fd passing.
 */
typedef struct EventRelayDispatcher EventRelayDispatcher;

/**
 * event_relay_dispatcher_new: (constructor)
 * @max_event_channel: Maximum number of concurrent event channels (0 = unlimited)
 *
 * Creates a new relay event dispatcher. The dispatcher maintains a list of
 * socketpair endpoints (one per channel). When a client requests a channel
 * via event_relay_dispatcher_get_event_channel(), a new socketpair is created
 * and one end is stored in the list while the other is returned to the caller.
 *
 * Returns: (transfer full) (nullable): a new #EventRelayDispatcher, or %NULL
 *     on failure
 */
EventRelayDispatcher *event_relay_dispatcher_new(guint max_event_channel);

/**
 * event_relay_dispatcher_free: (skip)
 * @dispatcher: (nullable)
 *
 * Frees the relay dispatcher and all associated resources. Closes every
 * socketpair endpoint in the list. Safe to call on %NULL.
 *
 * Not thread-safe relative to get_event_channel/send — the caller must
 * ensure no concurrent calls.
 */
void event_relay_dispatcher_free(EventRelayDispatcher *dispatcher);

/**
 * event_relay_dispatcher_get_event_channel:
 * @dispatcher: an #EventRelayDispatcher
 *
 * Creates a new event channel by calling socketpair(%AF_UNIX,
 * %SOCK_SEQPACKET | %SOCK_NONBLOCK, 0). One end of the pair is stored
 * in the dispatcher's internal socket list; the other end is returned
 * as a file descriptor.
 *
 * If @max_event_channel is non-zero and the number of active channels
 * has already reached the limit, this function returns -1.
 *
 * The D-Bus service framework calls this function to respond to a
 * <literal>GetEventChannel</literal> method call. The returned fd is
 * passed back to the client via D-Bus fd passing. The service framework
 * also returns an @event_protocol_id that identifies the data structure
 * of the buffer used in send() — the current protocol ID is 0, meaning
 * #dispatch_event_t.
 *
 * Returns: a file descriptor (the client end of the socketpair), or -1
 *     on failure or if the channel limit has been reached
 */
int event_relay_dispatcher_get_event_channel(EventRelayDispatcher *dispatcher);

/**
 * event_relay_dispatcher_send:
 * @dispatcher: an #EventRelayDispatcher
 * @buf: the data to send
 * @len: the length of @buf in bytes
 *
 * Sends @buf to every active channel in the dispatcher's socket list.
 * Disconnected or slow clients (EAGAIN/EPIPE/ECONNRESET on send) are
 * automatically cleaned up — their fd is closed and removed from the list.
 *
 * Returns: %TRUE if the data was processed (even if no clients received it),
 *          %FALSE on critical error
 */
gboolean event_relay_dispatcher_send(EventRelayDispatcher *dispatcher,
                                     const void *buf, size_t len);

G_END_DECLS

#endif /* EVENT_RELAY_DISPATCHER_H */
