// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "event-listener.h"

#include "event-listener-backend.h"

struct ServerEventListener {
    FileEventHandler            handler;
    gpointer                    user_data;
    ServerEventListenerBackend *backend;
};

/* ------------------------------------------------------------------ */
/* Transport detection                                                */
/* ------------------------------------------------------------------ */

#define SYSFS_TRANSPORT_PATH "/sys/kernel/vfs_monitor/transport"

typedef enum {
    LISTENER_TRANSPORT_MMAP,
    LISTENER_TRANSPORT_GENL,
} ListenerTransport;

static ListenerTransport detect_transport(void)
{
    g_autofree gchar *contents = NULL;
    g_autoptr(GError) error = NULL;

    if (!g_file_get_contents(SYSFS_TRANSPORT_PATH, &contents, NULL, &error)) {
        g_warning("Cannot read %s: %s — assuming genl",
                  SYSFS_TRANSPORT_PATH, error->message);
        return LISTENER_TRANSPORT_GENL;
    }

    g_strstrip(contents);
    ListenerTransport t;
    if (g_str_equal(contents, "mmap"))
        t = LISTENER_TRANSPORT_MMAP;
    else
        t = LISTENER_TRANSPORT_GENL;

    return t;
}

/* ------------------------------------------------------------------ */
/* Public API — delegates to the selected backend                     */
/* ------------------------------------------------------------------ */

ServerEventListener *server_event_listener_new(FileEventHandler handler,
                                                gpointer user_data)
{
    g_return_val_if_fail(handler != NULL, NULL);

    ServerEventListener *listener = g_new0(ServerEventListener, 1);
    listener->handler = handler;
    listener->user_data = user_data;

    ListenerTransport transport = detect_transport();

    ServerEventListenerBackend *backend = NULL;
    if (transport == LISTENER_TRANSPORT_MMAP)
        backend = mmap_ring_backend_new(handler, user_data);
    else
        backend = genl_backend_new(handler, user_data);

    if (!backend) {
        g_free(listener);
        return NULL;
    }

    listener->backend = backend;
    return listener;
}

gboolean server_event_listener_start(ServerEventListener *listener)
{
    g_return_val_if_fail(listener != NULL, FALSE);
    g_return_val_if_fail(listener->backend != NULL, FALSE);
    return listener->backend->start(listener->backend);
}

void server_event_listener_stop(ServerEventListener *listener)
{
    g_return_if_fail(listener != NULL);
    g_return_if_fail(listener->backend != NULL);
    listener->backend->stop(listener->backend);
}

void server_event_listener_free(ServerEventListener *listener)
{
    if (!listener)
        return;

    if (listener->backend) {
        listener->backend->free(listener->backend);
        listener->backend = NULL;
    }

    g_free(listener);
}
