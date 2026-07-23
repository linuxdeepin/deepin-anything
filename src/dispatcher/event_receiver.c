// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#define G_LOG_USE_STRUCTURED
#include "event_dispatcher.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

struct EventReceiver {
    int sock_fd;            /* connected socket (SOCK_SEQPACKET, blocking) */
    char *address;          /* socket path (owned copy)                     */
};

EventReceiver *event_receiver_new(const char *address)
{
    g_return_val_if_fail(address != NULL, NULL);

    EventReceiver *r = g_new0(EventReceiver, 1);
    r->sock_fd = -1;
    r->address = g_strdup(address);

    r->sock_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (r->sock_fd < 0) {
        g_warning("socket() failed: %s", strerror(errno));
        goto fail;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    g_strlcpy(addr.sun_path, r->address, sizeof(addr.sun_path));

    if (connect(r->sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        g_warning("connect(%s) failed: %s", r->address, strerror(errno));
        goto fail;
    }

    return r;

fail:
    event_receiver_free(r);
    return NULL;
}

void event_receiver_free(EventReceiver *receiver)
{
    if (receiver == NULL)
        return;

    if (receiver->sock_fd >= 0)
        close(receiver->sock_fd);

    g_free(receiver->address);
    g_free(receiver);
}

int event_receiver_get_socket(EventReceiver *receiver)
{
    g_return_val_if_fail(receiver != NULL, -1);
    return receiver->sock_fd;
}

EventReceiveResult event_receiver_receive(EventReceiver *receiver,
                                          dispatch_event_t *event)
{
    g_return_val_if_fail(receiver != NULL, EVENT_RECEIVE_ERROR);
    g_return_val_if_fail(event != NULL, EVENT_RECEIVE_ERROR);

    memset(event, 0, sizeof(*event));

    ssize_t ret = recv(receiver->sock_fd, event, sizeof(*event), 0);
    if (ret < 0) {
        if (errno == EINTR)
            return EVENT_RECEIVE_INTERRUPTED;
        g_debug("recv() failed: %s", strerror(errno));
        return EVENT_RECEIVE_ERROR;
    }

    if (ret == 0)
        return EVENT_RECEIVE_DISCONNECTED;

    /* Validate minimum size: offsetof(event_path) + at least 1 byte for '\0' */
    size_t min_size = offsetof(dispatch_event_t, event_path) + 1;
    if ((size_t)ret < min_size) {
        g_warning("received short message (%zd bytes)", ret);
        return EVENT_RECEIVE_ERROR;
    }

    /* event_path is guaranteed null-terminated:
     * either from the sent data or from zero-initialization above */
    return EVENT_RECEIVE_OK;
}
