// SPDX-FileCopyrightText: 2024-2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/event_listener.h"

#include <string.h>

#include <glib-unix.h>

#include "event_dispatcher.h"
#include "utils/log.h"

#define DISPATCHER_SOCKET_PATH "/run/deepin-anything/event-dispatcher.sock"
#define RECONNECT_DELAY_MS     10000   /* 10s delay to ride out event bursts */
#define MAX_RECONNECT_ATTEMPTS 30      /* give up and quit after this many fails */

struct EventListener {
    EventReceiver           *receiver;
    GMainLoop               *loop;
    GMainContext            *context;
    GThread                 *thread;
    EventListenerCallback    callback;
    EventListenerQuitCallback quit_callback;
    gpointer                 user_data;
    guint                    fd_source_id;
    guint                    reconnect_timer_id;
    gint                     reconnect_attempts;

    GMutex                   startup_mutex;
    GCond                    startup_cond;
    volatile gint            started;  /* 0 = pending, 1 = success, -1 = failure */
};

static void signal_startup(EventListener *listener, gboolean success)
{
    g_mutex_lock(&listener->startup_mutex);
    g_atomic_int_set(&listener->started, success ? 1 : -1);
    g_cond_signal(&listener->startup_cond);
    g_mutex_unlock(&listener->startup_mutex);
}

static void notify_quit(EventListener *listener)
{
    if (listener->quit_callback)
        listener->quit_callback(listener->user_data);
}

static gboolean on_fd_readable(gint fd, GIOCondition condition, gpointer data);

/*
 * Reconnect state machine — why one-shot timers instead of a single
 * repeating GSource:
 *
 * A one-shot timer per attempt (create → fire → destroy → create next)
 * trades a tiny per-10s allocation for three simplifications:
 *
 *   1. Success path needs no cleanup: returning G_SOURCE_REMOVE auto-
 *      destroys the timer, so reconnection back to fd-driven mode is a
 *      single return with no g_source_remove() branch.
 *   2. Single reschedule point: the failure-count check, the new-timer
 *      creation, and the id storage all live in one place (schedule_retry
 *      below). A repeating source would split "keep going" (return value)
 *      from "hit the cap" (in-body check), scattering the state machine.
 *   3. stop() is trivial: at most one pending source id
 *      (reconnect_timer_id) to remove in the cleanup block; the callback
 *      entry immediately clears it, so the state space is minimal.
 *
 * The allocation cost (g_timeout_source_new + attach + unref) is paid at
 * most once per RECONNECT_DELAY_MS (10s) — negligible on this low-frequency
 * path.
 */
static gboolean on_reconnect_attempt(gpointer data)
{
    EventListener *listener = (EventListener *)data;

    listener->reconnect_timer_id = 0;

    EventReceiver *new_receiver = event_receiver_new(DISPATCHER_SOCKET_PATH);
    if (new_receiver != NULL) {
        listener->receiver = new_receiver;

        int sock_fd = event_receiver_get_socket(listener->receiver);
        if (sock_fd < 0) {
            spdlog::error("Invalid receiver socket fd after reconnect");
            event_receiver_free(listener->receiver);
            listener->receiver = NULL;
            listener->reconnect_attempts++;
            goto schedule_retry;
        }

        GSource *fd_source = g_unix_fd_source_new(sock_fd,
            (GIOCondition)(G_IO_IN | G_IO_HUP | G_IO_ERR));
        if (fd_source == NULL) {
            spdlog::error("Failed to create fd watch source for receiver socket");
            event_receiver_free(listener->receiver);
            listener->receiver = NULL;
            listener->reconnect_attempts++;
            goto schedule_retry;
        }
        g_source_set_priority(fd_source, G_PRIORITY_DEFAULT);
        g_source_set_callback(fd_source,
            (GSourceFunc)(void (*)(void))on_fd_readable, listener, NULL);
        listener->fd_source_id = g_source_attach(fd_source, listener->context);
        g_source_unref(fd_source);
        if (listener->fd_source_id == 0) {
            spdlog::error("Failed to attach fd watch to listener context");
            event_receiver_free(listener->receiver);
            listener->receiver = NULL;
            listener->reconnect_attempts++;
            goto schedule_retry;
        }

        spdlog::info("Reconnected to dispatcher: {}", DISPATCHER_SOCKET_PATH);
        listener->reconnect_attempts = 0;
        return G_SOURCE_REMOVE;
    }

    listener->reconnect_attempts++;
    spdlog::warn("Reconnect attempt {}/{} failed",
                 listener->reconnect_attempts, MAX_RECONNECT_ATTEMPTS);

schedule_retry:
    if (listener->reconnect_attempts >= MAX_RECONNECT_ATTEMPTS) {
        spdlog::error("Giving up after {} reconnect attempts, requesting quit",
                      MAX_RECONNECT_ATTEMPTS);
        notify_quit(listener);
        return G_SOURCE_REMOVE;
    }

    GSource *timer_source = g_timeout_source_new(RECONNECT_DELAY_MS);
    if (timer_source == NULL) {
        spdlog::warn("Failed to create reconnect timer, quitting immediately");
        notify_quit(listener);
        return G_SOURCE_REMOVE;
    }
    g_source_set_callback(timer_source, on_reconnect_attempt, listener, NULL);
    listener->reconnect_timer_id = g_source_attach(timer_source, listener->context);
    g_source_unref(timer_source);

    if (listener->reconnect_timer_id == 0) {
        spdlog::warn("Failed to attach reconnect timer, quitting immediately");
        notify_quit(listener);
    }

    return G_SOURCE_REMOVE;
}

static void start_reconnect(EventListener *listener)
{
    /* The old fd source has already returned G_SOURCE_REMOVE, so it is
     * destroyed by the main loop.  Free the stale receiver and schedule a
     * delayed reconnect to avoid hammering the dispatcher during an event
     * burst. */
    if (listener->receiver != NULL) {
        event_receiver_free(listener->receiver);
        listener->receiver = NULL;
    }

    listener->reconnect_attempts = 0;

    GSource *timer_source = g_timeout_source_new(RECONNECT_DELAY_MS);
    if (timer_source == NULL) {
        spdlog::warn("Failed to create reconnect timer, quitting immediately");
        notify_quit(listener);
        return;
    }
    g_source_set_callback(timer_source, on_reconnect_attempt, listener, NULL);
    listener->reconnect_timer_id = g_source_attach(timer_source, listener->context);
    g_source_unref(timer_source);

    if (listener->reconnect_timer_id == 0) {
        spdlog::warn("Failed to attach reconnect timer, quitting immediately");
        notify_quit(listener);
    } else {
        spdlog::info("Connection lost, will reconnect in {}ms",
                     RECONNECT_DELAY_MS);
    }
}

static gboolean on_fd_readable(G_GNUC_UNUSED gint fd,
                               GIOCondition condition,
                               gpointer data)
{
    EventListener *listener = (EventListener *)data;

    if ((condition & (G_IO_HUP | G_IO_ERR)) != 0) {
        spdlog::info("Dispatcher connection lost");
        listener->fd_source_id = 0;
        start_reconnect(listener);
        return G_SOURCE_REMOVE;
    }

    /* Drain all pending events in one callback to avoid GMainContext
     * re-dispatch overhead per event under high throughput. The fd source
     * is level-triggered, so it will fire again if the kernel buffer still
     * has data after we return. */
    for (;;) {
        dispatch_event_t dispatch_evt;
        memset(&dispatch_evt, 0, sizeof(dispatch_evt));

        EventReceiveResult result = event_receiver_receive_nonblock(
            listener->receiver, &dispatch_evt);

        switch (result) {
        case EVENT_RECEIVE_OK: {
            fs_event *evt = g_slice_new(fs_event);
            evt->act = (uint8_t)dispatch_evt.event_action;
            evt->cookie = dispatch_evt.cookie;
            g_strlcpy(evt->src, dispatch_evt.event_path, MAX_PATH_LEN);
            evt->dst[0] = '\0';

            if (listener->callback)
                listener->callback(listener->user_data, evt);

            continue;
        }
        case EVENT_RECEIVE_WOULD_BLOCK:
            return G_SOURCE_CONTINUE;
        case EVENT_RECEIVE_INTERRUPTED:
            continue;
        case EVENT_RECEIVE_DISCONNECTED:
            spdlog::info("Dispatcher closed the connection");
            listener->fd_source_id = 0;
            start_reconnect(listener);
            return G_SOURCE_REMOVE;
        case EVENT_RECEIVE_ERROR:
            spdlog::warn("Failed to receive event");
            listener->fd_source_id = 0;
            start_reconnect(listener);
            return G_SOURCE_REMOVE;
        default:
            continue;
        }
    }
}

static gpointer event_listener_thread_func(gpointer data)
{
    EventListener *listener = (EventListener *)data;

    listener->context = g_main_context_new();
    if (listener->context == NULL) {
        spdlog::error("Failed to create event listener GMainContext");
        signal_startup(listener, FALSE);
        return NULL;
    }

    listener->loop = g_main_loop_new(listener->context, FALSE);
    g_main_context_push_thread_default(listener->context);

    int sock_fd = -1;

    listener->receiver = event_receiver_new(DISPATCHER_SOCKET_PATH);
    if (listener->receiver == NULL) {
        spdlog::error("Failed to connect to dispatcher at {}",
                      DISPATCHER_SOCKET_PATH);
        spdlog::info("Is deepin-anything-server running and reachable?");
        signal_startup(listener, FALSE);
        goto cleanup;
    }

    spdlog::info("Connected to dispatcher: {}", DISPATCHER_SOCKET_PATH);

    sock_fd = event_receiver_get_socket(listener->receiver);
    if (sock_fd < 0) {
        spdlog::error("Invalid receiver socket fd");
        signal_startup(listener, FALSE);
        goto cleanup;
    }

    {
        GSource *fd_source = g_unix_fd_source_new(sock_fd,
            (GIOCondition)(G_IO_IN | G_IO_HUP | G_IO_ERR));
        if (fd_source == NULL) {
            spdlog::error("Failed to create fd watch source for receiver socket");
            signal_startup(listener, FALSE);
            goto cleanup;
        }
        g_source_set_priority(fd_source, G_PRIORITY_DEFAULT);
        g_source_set_callback(fd_source,
            (GSourceFunc)(void (*)(void))on_fd_readable, listener, NULL);
        listener->fd_source_id = g_source_attach(fd_source, listener->context);
        g_source_unref(fd_source);
        if (listener->fd_source_id == 0) {
            spdlog::error("Failed to attach fd watch to listener context");
            signal_startup(listener, FALSE);
            goto cleanup;
        }
    }

    spdlog::info("Event listener thread started");
    signal_startup(listener, TRUE);
    g_main_loop_run(listener->loop);

cleanup:
    if (listener->fd_source_id > 0) {
        g_source_remove(listener->fd_source_id);
        listener->fd_source_id = 0;
    }
    if (listener->reconnect_timer_id > 0) {
        g_source_remove(listener->reconnect_timer_id);
        listener->reconnect_timer_id = 0;
    }
    if (listener->receiver != NULL) {
        event_receiver_free(listener->receiver);
        listener->receiver = NULL;
    }
    g_main_context_pop_thread_default(listener->context);
    if (listener->loop != NULL) {
        g_main_loop_unref(listener->loop);
        listener->loop = NULL;
    }
    if (listener->context != NULL) {
        g_main_context_unref(listener->context);
        listener->context = NULL;
    }

    spdlog::info("Event listener thread stopped");
    return NULL;
}

EventListener *event_listener_new(EventListenerCallback callback,
                                   EventListenerQuitCallback quit_callback,
                                   gpointer user_data)
{
    g_return_val_if_fail(callback != NULL, NULL);

    EventListener *listener = g_new0(EventListener, 1);
    listener->callback = callback;
    listener->user_data = user_data;
    listener->quit_callback = quit_callback;
    listener->fd_source_id = 0;
    listener->reconnect_timer_id = 0;
    listener->reconnect_attempts = 0;
    g_atomic_int_set(&listener->started, 0);
    g_mutex_init(&listener->startup_mutex);
    g_cond_init(&listener->startup_cond);

    return listener;
}

gboolean event_listener_start(EventListener *listener)
{
    g_return_val_if_fail(listener != NULL, FALSE);

    if (listener->thread) {
        spdlog::warn("Event listener is already started");
        return FALSE;
    }

    listener->thread = g_thread_new("event_listener",
                                     event_listener_thread_func, listener);
    if (listener->thread == NULL) {
        spdlog::error("Failed to create event listener thread");
        return FALSE;
    }

    g_mutex_lock(&listener->startup_mutex);
    while (g_atomic_int_get(&listener->started) == 0)
        g_cond_wait(&listener->startup_cond, &listener->startup_mutex);
    gboolean ok = (g_atomic_int_get(&listener->started) == 1);
    g_mutex_unlock(&listener->startup_mutex);

    if (!ok) {
        g_thread_join(listener->thread);
        listener->thread = NULL;
        spdlog::error("Event listener thread failed during startup");
    }

    return ok;
}

void event_listener_stop(EventListener *listener)
{
    g_return_if_fail(listener != NULL);

    if (listener->loop != NULL)
        g_main_loop_quit(listener->loop);

    if (listener->thread) {
        g_thread_join(listener->thread);
        listener->thread = NULL;
    }
}

void event_listener_free(EventListener *listener)
{
    if (listener == NULL)
        return;

    event_listener_stop(listener);

    g_mutex_clear(&listener->startup_mutex);
    g_cond_clear(&listener->startup_cond);
    g_free(listener);
}
