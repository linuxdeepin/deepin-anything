// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef EVENT_RELAY_RECEIVER_H
#define EVENT_RELAY_RECEIVER_H

#include <glib.h>
#include <sys/types.h>

#include "event_dispatcher.h"

G_BEGIN_DECLS

/**
 * EventRelayReceiver:
 *
 * An opaque relay event receiver. It obtains a communication socket from a
 * D-Bus service's <literal>GetEventChannel</literal> method and reads events
 * from it. Unlike #EventReceiver (which connects to a Unix Domain Socket
 * path), the relay receiver gets its fd via D-Bus fd passing.
 */

typedef struct EventRelayReceiver EventRelayReceiver;

/**
 * event_relay_receiver_new: (constructor)
 * @bus_name: the D-Bus bus name of the service to connect to
 * @object_path: the D-Bus object path of the service
 * @interface_name: the D-Bus interface name providing GetEventChannel
 *
 * Creates a new relay event receiver by calling the specified D-Bus
 * service's <literal>GetEventChannel</literal> method. The method returns
 * a file descriptor (via D-Bus fd passing) and an @event_protocol_id
 * (uint32) that identifies the data structure of the event buffer.
 * Both are stored in the receiver for later use.
 *
 * Returns: (transfer full) (nullable): a new #EventRelayReceiver, or %NULL
 *     on failure
 */
EventRelayReceiver *event_relay_receiver_new(const char *bus_name,
                                             const char *object_path,
                                             const char *interface_name);

/**
 * event_relay_receiver_free: (skip)
 * @receiver: (nullable)
 *
 * Frees the relay receiver and closes the socket fd. Safe to call on %NULL.
 */
void event_relay_receiver_free(EventRelayReceiver *receiver);

/**
 * event_relay_receiver_get_fd:
 * @receiver: an #EventRelayReceiver
 * @fd: (out) (optional): location to store the socket fd, or %NULL
 * @event_protocol_id: (out) (optional): location to store the protocol ID, or %NULL
 *
 * Retrieves the socket fd and @event_protocol_id from the receiver. The
 * caller can monitor the fd for readability (e.g., via poll/epoll) and
 * call event_relay_receiver_receive() when data is available.
 *
 * Returns: %TRUE on success, %FALSE if @receiver is %NULL
 */
gboolean event_relay_receiver_get_fd(EventRelayReceiver *receiver,
                                     int *fd, guint32 *event_protocol_id);

/**
 * event_relay_receiver_receive:
 * @receiver: an #EventRelayReceiver
 * @buf: (out caller-allocates): output buffer
 * @len: the maximum length of @buf in bytes
 *
 * Non-blocking variant of receive. Uses %MSG_DONTWAIT so the call returns
 * immediately when no data is available. Designed for use in fd-readable
 * callbacks that drain the socket in a loop. %SOCK_SEQPACKET preserves
 * message boundaries, so a single recv() returns one complete message.
 *
 * Returns: #EventReceiveResult indicating the outcome:
 *     %EVENT_RECEIVE_OK on success,
 *     %EVENT_RECEIVE_WOULD_BLOCK when no data is available,
 *     %EVENT_RECEIVE_DISCONNECTED when the server closed the connection,
 *     %EVENT_RECEIVE_INTERRUPTED when recv() was interrupted by a signal,
 *     %EVENT_RECEIVE_ERROR on other errors
 */
EventReceiveResult event_relay_receiver_receive(EventRelayReceiver *receiver,
                                                void *buf, size_t len);

G_END_DECLS

#endif /* EVENT_RELAY_RECEIVER_H */
