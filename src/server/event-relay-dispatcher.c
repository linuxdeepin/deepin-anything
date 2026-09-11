// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "event-relay-dispatcher.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "event_relay_dispatcher.h"

/* Server-local sentinel action code to wake the worker thread.
 * Distinct from all kernel vfs_change_consts.h action codes. */
#define ACT_WAKEUP 101

/* ── Internal types ─────────────────────────────────────────────── */

typedef struct {
    ServerEventRelayChannelFunc callback;
    gpointer                   user_data;
} ChannelRequest;

struct ServerEventRelayDispatcher {
    EventRelayDispatcher *relay;        /* owned library object            */
    GAsyncQueue          *event_queue;   /* fs_event by value              */
    GAsyncQueue          *request_queue; /* ChannelRequest items           */
    GThread              *thread;
    volatile gint         quit;          /* set by stop(), checked by worker */
    GMutex                startup_mutex;
    GCond                 startup_cond;
    volatile gint         started;       /* 0 pending / 1 ok / -1 fail     */
    guint                 max_event_channel;
};

/* ── Helpers ────────────────────────────────────────────────────── */

static void signal_startup(ServerEventRelayDispatcher *d, gboolean success)
{
    g_mutex_lock(&d->startup_mutex);
    g_atomic_int_set(&d->started, success ? 1 : -1);
    g_cond_signal(&d->startup_cond);
    g_mutex_unlock(&d->startup_mutex);
}

static void complete_request(ChannelRequest *req,
                             ServerEventRelayDispatcher *d, int fd)
{
    if (req->callback)
        req->callback(d, fd, SERVER_RELAY_EVENT_PROTOCOL_ID, req->user_data);
    else if (fd >= 0)
        close(fd);
}

static void drain_requests(ServerEventRelayDispatcher *d)
{
    ChannelRequest *req;
    while ((req = g_async_queue_try_pop(d->request_queue)) != NULL) {
        complete_request(req, d, -1);
        g_slice_free(ChannelRequest, req);
    }
}

/* ── Worker thread ──────────────────────────────────────────────── */

static gpointer relay_thread_func(gpointer data)
{
    ServerEventRelayDispatcher *d = (ServerEventRelayDispatcher *)data;

    d->relay = event_relay_dispatcher_new(d->max_event_channel);
    if (!d->relay) {
        g_critical("Failed to create relay dispatcher");
        signal_startup(d, FALSE);
        return NULL;
    }

    g_message("Event relay dispatcher thread started");
    signal_startup(d, TRUE);

    while (TRUE) {
        fs_event *ev = (fs_event *)g_async_queue_pop(d->event_queue);

        if (ev->act == ACT_WAKEUP) {
            g_slice_free(fs_event, ev);

            /* Quit flag is only set by stop() which also pushes
             * ACT_WAKEUP, so the quit check belongs here. */
            if (g_atomic_int_get(&d->quit))
                break;

            /* Drain pending channel requests only on wakeup. */
            ChannelRequest *req;
            while ((req = g_async_queue_try_pop(d->request_queue)) != NULL) {
                int fd = event_relay_dispatcher_get_event_channel(d->relay);
                complete_request(req, d, fd);
                g_slice_free(ChannelRequest, req);
            }
            continue;
        }

        event_relay_dispatcher_send(d->relay, ev, sizeof(fs_event));
        g_slice_free(fs_event, ev);
    }

    /* Complete any requests that arrived after the last drain. */
    drain_requests(d);

    if (d->relay) {
        event_relay_dispatcher_free(d->relay);
        d->relay = NULL;
    }

    g_message("Event relay dispatcher thread stopped");
    return NULL;
}

/* ── Public API ─────────────────────────────────────────────────── */

ServerEventRelayDispatcher *server_event_relay_dispatcher_new(guint max_event_channel)
{
    ServerEventRelayDispatcher *d = g_new0(ServerEventRelayDispatcher, 1);
    d->event_queue = g_async_queue_new();
    d->request_queue = g_async_queue_new();
    d->max_event_channel = max_event_channel;
    g_mutex_init(&d->startup_mutex);
    g_cond_init(&d->startup_cond);
    g_atomic_int_set(&d->started, 0);
    return d;
}

gboolean server_event_relay_dispatcher_start(ServerEventRelayDispatcher *d)
{
    g_return_val_if_fail(d != NULL, FALSE);

    if (d->thread) {
        g_warning("Event relay dispatcher is already started");
        return FALSE;
    }

    d->thread = g_thread_new("event_relay_dispatcher", relay_thread_func, d);
    if (!d->thread) {
        g_critical("Failed to create event relay dispatcher thread");
        return FALSE;
    }

    g_mutex_lock(&d->startup_mutex);
    while (g_atomic_int_get(&d->started) == 0)
        g_cond_wait(&d->startup_cond, &d->startup_mutex);
    gboolean ok = (g_atomic_int_get(&d->started) == 1);
    g_mutex_unlock(&d->startup_mutex);

    if (!ok) {
        g_thread_join(d->thread);
        d->thread = NULL;
        g_critical("Event relay dispatcher thread failed during startup");
        return FALSE;
    }

    return TRUE;
}

void server_event_relay_dispatcher_stop(ServerEventRelayDispatcher *d)
{
    g_return_if_fail(d != NULL);

    if (d->thread) {
        g_atomic_int_set(&d->quit, TRUE);

        /* Push a wakeup event to unblock the worker. */
        if (d->event_queue) {
            fs_event *wakeup = g_slice_new0(fs_event);
            wakeup->act = ACT_WAKEUP;
            g_async_queue_push(d->event_queue, wakeup);
        }

        g_thread_join(d->thread);
        d->thread = NULL;
    }
}

void server_event_relay_dispatcher_free(ServerEventRelayDispatcher *d)
{
    if (!d)
        return;

    server_event_relay_dispatcher_stop(d);

    /* Drain leftover events. */
    if (d->event_queue) {
        fs_event *ev;
        while ((ev = g_async_queue_try_pop(d->event_queue)) != NULL)
            g_slice_free(fs_event, ev);
        g_async_queue_unref(d->event_queue);
        d->event_queue = NULL;
    }

    /* Drain leftover requests (worker never ran or already exited). */
    if (d->request_queue) {
        drain_requests(d);
        g_async_queue_unref(d->request_queue);
        d->request_queue = NULL;
    }

    if (d->relay) {
        event_relay_dispatcher_free(d->relay);
        d->relay = NULL;
    }

    g_mutex_clear(&d->startup_mutex);
    g_cond_clear(&d->startup_cond);
    g_free(d);
}

void server_event_relay_dispatcher_push_event(ServerEventRelayDispatcher *d,
                                               fs_event *event)
{
    g_return_if_fail(d != NULL);
    g_return_if_fail(event != NULL);

    if (!d->event_queue) {
        g_warning("relay: event queue unavailable, dropping event");
        g_slice_free(fs_event, event);
        return;
    }

    g_async_queue_push(d->event_queue, event);
}

gboolean server_event_relay_dispatcher_get_event_channel(ServerEventRelayDispatcher *d,
                                                          ServerEventRelayChannelFunc callback,
                                                          gpointer user_data)
{
    g_return_val_if_fail(d != NULL, FALSE);

    if (!d->thread || !d->request_queue) {
        g_debug("relay: get_event_channel called but worker not running");
        return FALSE;
    }

    ChannelRequest *req = g_slice_new0(ChannelRequest);
    req->callback = callback;
    req->user_data = user_data;

    g_async_queue_push(d->request_queue, req);

    /* Wake the worker so it drains the request promptly. */
    fs_event *wakeup = g_slice_new0(fs_event);
    wakeup->act = ACT_WAKEUP;
    g_async_queue_push(d->event_queue, wakeup);

    return TRUE;
}
