// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#define _GNU_SOURCE
#define G_LOG_USE_STRUCTURED
#include "event_relay_receiver.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gio/gio.h>
#include <gio/gunixfdlist.h>

/* 1 MiB receive buffer — same rationale as the existing EventReceiver. */
#define RELAY_RECEIVER_SOCKET_BUF_SIZE (1 << 20)

/* ── Data structure ─────────────────────────────────────────────── */

struct EventRelayReceiver {
    int    sock_fd;            /* socket fd from GetEventChannel            */
    guint32 event_protocol_id; /* protocol ID returned by GetEventChannel    */
};

/* ── Public API ─────────────────────────────────────────────────── */

EventRelayReceiver *event_relay_receiver_new(const char *bus_name,
                                             const char *object_path,
                                             const char *interface_name)
{
    g_return_val_if_fail(bus_name != NULL, NULL);
    g_return_val_if_fail(object_path != NULL, NULL);
    g_return_val_if_fail(interface_name != NULL, NULL);

    EventRelayReceiver *r = g_new0(EventRelayReceiver, 1);
    r->sock_fd = -1;
    r->event_protocol_id = 0;

    GError *error = NULL;

    /* Connect to the system bus (server is a root system service). */
    GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (connection == NULL) {
        g_warning("relay: failed to connect to system bus: %s",
                  error ? error->message : "unknown error");
        g_clear_error(&error);
        goto fail;
    }

    /* Call GetEventChannel on the specified D-Bus service. The method
     * returns (h fd, u event_protocol_id) with a GUnixFDList for fd passing. */
    GUnixFDList *fd_list = NULL;
    GVariant *result = g_dbus_connection_call_with_unix_fd_list_sync(
        connection,
        bus_name,
        object_path,
        interface_name,
        "GetEventChannel",
        NULL,               /* parameters — none                        */
        G_VARIANT_TYPE("(hu)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,                 /* timeout: default                          */
        NULL,               /* fd_list to send — none                    */
        &fd_list,
        NULL,               /* cancellable                               */
        &error);

    g_object_unref(connection);

    if (result == NULL) {
        g_warning("relay: GetEventChannel call failed: %s",
                  error ? error->message : "unknown error");
        g_clear_error(&error);
        goto fail;
    }

    /* Extract the fd handle and event_protocol_id from the return value.
     * The D-Bus signature is (h u): h is the index into the GUnixFDList
     * for the passed fd, u is the event_protocol_id. */
    guint32 protocol_id = 0;
    guint32 fd_handle = 0;
    g_variant_get(result, "(hu)", &fd_handle, &protocol_id);
    g_variant_unref(result);

    if (fd_list == NULL || g_unix_fd_list_get_length(fd_list) < 1) {
        g_warning("relay: GetEventChannel returned no file descriptor");
        g_clear_object(&fd_list);
        goto fail;
    }

    if (fd_handle >= (guint32)g_unix_fd_list_get_length(fd_list)) {
        g_warning("relay: GetEventChannel fd handle %u out of range (len=%d)",
                  fd_handle, g_unix_fd_list_get_length(fd_list));
        g_clear_object(&fd_list);
        goto fail;
    }

    g_clear_error(&error);
    gint fd = g_unix_fd_list_get(fd_list, (gint)fd_handle, &error);
    g_object_unref(fd_list);

    if (fd < 0) {
        g_warning("relay: GetEventChannel returned invalid fd: %s",
                  error ? error->message : "unknown error");
        g_clear_error(&error);
        goto fail;
    }

    r->sock_fd = fd;
    r->event_protocol_id = protocol_id;

    /* Enlarge the receive buffer so bursts don't cause the server to kick us. */
    int buf_size = RELAY_RECEIVER_SOCKET_BUF_SIZE;
    if (setsockopt(r->sock_fd, SOL_SOCKET, SO_RCVBUF, &buf_size,
                   sizeof(buf_size)) < 0)
        g_debug("relay: setsockopt(SO_RCVBUF) failed: %s", strerror(errno));

    g_debug("relay: receiver created (fd=%d, protocol_id=%u)",
            r->sock_fd, r->event_protocol_id);

    return r;

fail:
    event_relay_receiver_free(r);
    return NULL;
}

void event_relay_receiver_free(EventRelayReceiver *receiver)
{
    if (receiver == NULL)
        return;

    if (receiver->sock_fd >= 0)
        close(receiver->sock_fd);

    g_free(receiver);
}

gboolean event_relay_receiver_get_fd(EventRelayReceiver *receiver,
                                     int *fd, guint32 *event_protocol_id)
{
    g_return_val_if_fail(receiver != NULL, FALSE);

    if (fd != NULL)
        *fd = receiver->sock_fd;
    if (event_protocol_id != NULL)
        *event_protocol_id = receiver->event_protocol_id;

    return TRUE;
}

EventReceiveResult event_relay_receiver_receive(EventRelayReceiver *receiver,
                                                void *buf, size_t len)
{
    g_return_val_if_fail(receiver != NULL, EVENT_RECEIVE_ERROR);
    g_return_val_if_fail(buf != NULL, EVENT_RECEIVE_ERROR);

    ssize_t ret = recv(receiver->sock_fd, buf, len, MSG_DONTWAIT);
    if (ret < 0) {
        if (errno == EINTR)
            return EVENT_RECEIVE_INTERRUPTED;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return EVENT_RECEIVE_WOULD_BLOCK;
        g_debug("relay: recv() failed: %s", strerror(errno));
        return EVENT_RECEIVE_ERROR;
    }

    if (ret == 0)
        return EVENT_RECEIVE_DISCONNECTED;

    return EVENT_RECEIVE_OK;
}
