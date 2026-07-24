// SPDX-FileCopyrightText: 2024-2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/event_listener.h"

#include <string.h>
#include <sys/stat.h>

#include <glib-unix.h>

#include "event_dispatcher.h"
#include "utils/log.h"

#define DISPATCHER_SOCKET_PATH "/run/deepin-anything/event-dispatcher.sock"
#define RESTART_CHECK_INTERVAL_MS 3000
#define MAX_RESTART_CHECKS 10

struct EventListener {
    EventReceiver           *receiver;
    GMainLoop               *loop;
    GMainContext            *context;
    GThread                 *thread;
    EventListenerCallback    callback;
    EventListenerQuitCallback quit_callback;
    gpointer                 user_data;
    guint                    fd_source_id;
    guint                    timer_source_id;
    ino_t                    initial_sock_inode;
    gint                     timer_check_count;

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

static gboolean on_restart_check(gpointer data)
{
    EventListener *listener = (EventListener *)data;

    listener->timer_check_count++;

    struct stat st;
    ino_t current_inode = 0;
    if (stat(DISPATCHER_SOCKET_PATH, &st) == 0)
        current_inode = st.st_ino;

    if (current_inode != 0 && current_inode != listener->initial_sock_inode) {
        spdlog::info("Dispatcher socket inode changed, server has restarted");
        listener->timer_source_id = 0;
        notify_quit(listener);
        return G_SOURCE_REMOVE;
    }

    if (listener->timer_check_count >= MAX_RESTART_CHECKS) {
        spdlog::info("Dispatcher did not restart after {} checks, requesting quit",
                     MAX_RESTART_CHECKS);
        listener->timer_source_id = 0;
        notify_quit(listener);
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

static void start_restart_check(EventListener *listener)
{
    struct stat st;
    listener->initial_sock_inode = 0;
    if (stat(DISPATCHER_SOCKET_PATH, &st) == 0)
        listener->initial_sock_inode = st.st_ino;

    listener->timer_check_count = 0;
    listener->timer_source_id = g_timeout_add(RESTART_CHECK_INTERVAL_MS,
                                              on_restart_check, listener);
    if (listener->timer_source_id == 0) {
        spdlog::warn("Failed to create restart check timer, quitting immediately");
        notify_quit(listener);
    } else {
        spdlog::info("Waiting for dispatcher restart (checking every {}ms, "
                     "up to {} times)", RESTART_CHECK_INTERVAL_MS, MAX_RESTART_CHECKS);
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
        start_restart_check(listener);
        return G_SOURCE_REMOVE;
    }

    dispatch_event_t dispatch_evt;
    memset(&dispatch_evt, 0, sizeof(dispatch_evt));

    EventReceiveResult result = event_receiver_receive(listener->receiver,
                                                        &dispatch_evt);

    switch (result) {
    case EVENT_RECEIVE_OK: {
        fs_event *evt = g_slice_new(fs_event);
        evt->act = (uint8_t)dispatch_evt.event_action;
        evt->cookie = dispatch_evt.cookie;
        g_strlcpy(evt->src, dispatch_evt.event_path, MAX_PATH_LEN);
        evt->dst[0] = '\0';

        if (listener->callback)
            listener->callback(listener->user_data, evt);

        return G_SOURCE_CONTINUE;
    }
    case EVENT_RECEIVE_INTERRUPTED:
        return G_SOURCE_CONTINUE;
    case EVENT_RECEIVE_DISCONNECTED:
        spdlog::info("Dispatcher closed the connection");
        listener->fd_source_id = 0;
        start_restart_check(listener);
        return G_SOURCE_REMOVE;
    case EVENT_RECEIVE_ERROR:
        spdlog::warn("Failed to receive event");
        listener->fd_source_id = 0;
        start_restart_check(listener);
        return G_SOURCE_REMOVE;
    default:
        return G_SOURCE_CONTINUE;
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

    listener->fd_source_id = g_unix_fd_add_full(
        G_PRIORITY_DEFAULT, sock_fd,
        (GIOCondition)(G_IO_IN | G_IO_HUP | G_IO_ERR),
        on_fd_readable, listener, NULL);
    if (listener->fd_source_id == 0) {
        spdlog::error("Failed to add fd watch for receiver socket");
        signal_startup(listener, FALSE);
        goto cleanup;
    }

    spdlog::info("Event listener thread started");
    signal_startup(listener, TRUE);
    g_main_loop_run(listener->loop);

cleanup:
    if (listener->fd_source_id > 0) {
        g_source_remove(listener->fd_source_id);
        listener->fd_source_id = 0;
    }
    if (listener->timer_source_id > 0) {
        g_source_remove(listener->timer_source_id);
        listener->timer_source_id = 0;
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
    listener->quit_callback = quit_callback;
    listener->user_data = user_data;
    listener->fd_source_id = 0;
    listener->timer_source_id = 0;
    listener->initial_sock_inode = 0;
    listener->timer_check_count = 0;
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
