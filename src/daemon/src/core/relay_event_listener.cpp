// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/relay_event_listener.h"

#include <string.h>

#include <glib-unix.h>
#include <gio/gio.h>

#include "event_relay_receiver.h"
#include "utils/log.h"

#define ANYTHING_BUS_NAME      "org.deepin.Anything"
#define ANYTHING_OBJECT_PATH   "/org/deepin/Anything"
#define ANYTHING_INTERFACE     "org.deepin.Anything"

#define RESTART_CHECK_INTERVAL_MS 3000
#define MAX_RESTART_CHECKS 10

struct RelayEventListener {
    EventRelayReceiver           *receiver;
    GMainLoop                     *loop;
    GMainContext                  *context;
    GThread                       *thread;
    RelayEventListenerCallback    callback;
    RelayEventListenerQuitCallback quit_callback;
    gpointer                      user_data;
    guint                         fd_source_id;
    guint                         timer_source_id;
    gchar                         *initial_bus_owner;
    gint                          timer_check_count;

    GMutex                        startup_mutex;
    GCond                         startup_cond;
    volatile gint                 started;  /* 0 = pending, 1 = success, -1 = failure */
};

static void signal_startup(RelayEventListener *listener, gboolean success)
{
    g_mutex_lock(&listener->startup_mutex);
    g_atomic_int_set(&listener->started, success ? 1 : -1);
    g_cond_signal(&listener->startup_cond);
    g_mutex_unlock(&listener->startup_mutex);
}

static void notify_quit(RelayEventListener *listener)
{
    if (listener->quit_callback)
        listener->quit_callback(listener->user_data);
}

/* Query the unique bus name that currently owns @well_known_name.
 * Returns: (transfer full) (nullable): the unique name (e.g. ":1.42"),
 *          or NULL on failure. */
static gchar *get_bus_name_owner(const char *well_known_name)
{
    GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    if (connection == NULL)
        return NULL;

    GError *error = NULL;
    GVariant *result = g_dbus_connection_call_sync(
        connection,
        "org.freedesktop.DBus",       /* bus name */
        "/org/freedesktop/DBus",      /* object path */
        "org.freedesktop.DBus",       /* interface */
        "GetNameOwner",
        g_variant_new("(s)", well_known_name),
        G_VARIANT_TYPE("(s)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error);

    g_object_unref(connection);

    if (result == NULL) {
        spdlog::debug("GetNameOwner('{}') failed: {}",
                      well_known_name,
                      error ? error->message : "unknown error");
        g_clear_error(&error);
        return NULL;
    }

    const gchar *owner = NULL;
    g_variant_get(result, "(&s)", &owner);
    gchar *ret = g_strdup(owner);
    g_variant_unref(result);

    return ret;
}

static gboolean on_restart_check(gpointer data)
{
    RelayEventListener *listener = (RelayEventListener *)data;

    listener->timer_check_count++;

    gchar *current_owner = get_bus_name_owner(ANYTHING_BUS_NAME);
    if (current_owner != NULL && listener->initial_bus_owner != NULL &&
        strcmp(current_owner, listener->initial_bus_owner) != 0) {
        spdlog::info("D-Bus bus owner changed ({} -> {}), server has restarted",
                     listener->initial_bus_owner, current_owner);
        g_free(current_owner);
        listener->timer_source_id = 0;
        notify_quit(listener);
        return G_SOURCE_REMOVE;
    }
    g_free(current_owner);

    if (listener->timer_check_count >= MAX_RESTART_CHECKS) {
        spdlog::info("Server did not restart after {} checks, requesting quit",
                     MAX_RESTART_CHECKS);
        listener->timer_source_id = 0;
        notify_quit(listener);
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

static void start_restart_check(RelayEventListener *listener)
{
    listener->timer_check_count = 0;

    GSource *timer_source = g_timeout_source_new(RESTART_CHECK_INTERVAL_MS);
    if (timer_source == NULL) {
        spdlog::warn("Failed to create restart check timer, quitting immediately");
        notify_quit(listener);
        return;
    }
    g_source_set_callback(timer_source, on_restart_check, listener, NULL);
    listener->timer_source_id = g_source_attach(timer_source, listener->context);
    g_source_unref(timer_source);

    if (listener->timer_source_id == 0) {
        spdlog::warn("Failed to attach restart check timer, quitting immediately");
        notify_quit(listener);
    } else {
        spdlog::info("Waiting for server restart (checking every {}ms, "
                     "up to {} times)", RESTART_CHECK_INTERVAL_MS, MAX_RESTART_CHECKS);
    }
}

static gboolean on_fd_readable(G_GNUC_UNUSED gint fd,
                               GIOCondition condition,
                               gpointer data)
{
    RelayEventListener *listener = (RelayEventListener *)data;

    if ((condition & (G_IO_HUP | G_IO_ERR)) != 0) {
        spdlog::info("Server connection lost");
        listener->fd_source_id = 0;
        start_restart_check(listener);
        return G_SOURCE_REMOVE;
    }

    for (;;) {
        fs_event evt;
        memset(&evt, 0, sizeof(evt));

        EventReceiveResult result = event_relay_receiver_receive(
            listener->receiver, &evt, sizeof(evt));

        switch (result) {
        case EVENT_RECEIVE_OK: {
            fs_event *heap_evt = g_slice_new(fs_event);
            *heap_evt = evt;

            if (listener->callback)
                listener->callback(listener->user_data, heap_evt);

            continue;
        }
        case EVENT_RECEIVE_WOULD_BLOCK:
            return G_SOURCE_CONTINUE;
        case EVENT_RECEIVE_INTERRUPTED:
            continue;
        case EVENT_RECEIVE_DISCONNECTED:
            spdlog::info("Server closed the connection");
            listener->fd_source_id = 0;
            start_restart_check(listener);
            return G_SOURCE_REMOVE;
        case EVENT_RECEIVE_ERROR:
            spdlog::warn("Failed to receive event");
            listener->fd_source_id = 0;
            start_restart_check(listener);
            return G_SOURCE_REMOVE;
        default:
            continue;
        }
    }
}

static gpointer relay_event_listener_thread_func(gpointer data)
{
    RelayEventListener *listener = (RelayEventListener *)data;

    listener->context = g_main_context_new();
    if (listener->context == NULL) {
        spdlog::error("Failed to create relay event listener GMainContext");
        signal_startup(listener, FALSE);
        return NULL;
    }

    listener->loop = g_main_loop_new(listener->context, FALSE);
    g_main_context_push_thread_default(listener->context);

    int sock_fd = -1;
    guint32 protocol_id = 0;

    listener->receiver = event_relay_receiver_new(ANYTHING_BUS_NAME,
                                                   ANYTHING_OBJECT_PATH,
                                                   ANYTHING_INTERFACE);
    if (listener->receiver == NULL) {
        spdlog::error("Failed to connect to server via D-Bus GetEventChannel");
        spdlog::info("Is deepin-anything-server running and reachable?");
        signal_startup(listener, FALSE);
        goto cleanup;
    }

    spdlog::info("Connected to server via D-Bus relay channel");

    /* Record the initial unique bus owner so we can detect server restarts. */
    listener->initial_bus_owner = get_bus_name_owner(ANYTHING_BUS_NAME);
    if (listener->initial_bus_owner != NULL) {
        spdlog::info("Server bus owner: {}", listener->initial_bus_owner);
    } else {
        spdlog::warn("Could not determine initial server bus owner");
    }

    if (!event_relay_receiver_get_fd(listener->receiver, &sock_fd, &protocol_id)) {
        spdlog::error("Failed to get receiver fd");
        signal_startup(listener, FALSE);
        goto cleanup;
    }

    if (sock_fd < 0) {
        spdlog::error("Invalid receiver socket fd");
        signal_startup(listener, FALSE);
        goto cleanup;
    }

    {
        GSource *fd_source = g_unix_fd_source_new(sock_fd,
            (GIOCondition)(G_IO_IN | G_IO_HUP | G_IO_ERR));
        if (fd_source == NULL) {
            spdlog::error("Failed to create fd source for receiver socket");
            signal_startup(listener, FALSE);
            goto cleanup;
        }
        g_source_set_priority(fd_source, G_PRIORITY_DEFAULT);
        g_source_set_callback(fd_source,
            (GSourceFunc)(void(*)(void))on_fd_readable, listener, NULL);
        listener->fd_source_id = g_source_attach(fd_source, listener->context);
        g_source_unref(fd_source);
        if (listener->fd_source_id == 0) {
            spdlog::error("Failed to attach fd watch to listener context");
            signal_startup(listener, FALSE);
            goto cleanup;
        }
    }

    spdlog::info("Relay event listener thread started");
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
        event_relay_receiver_free(listener->receiver);
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

    spdlog::info("Relay event listener thread stopped");
    return NULL;
}

RelayEventListener *relay_event_listener_new(RelayEventListenerCallback callback,
                                            RelayEventListenerQuitCallback quit_callback,
                                            gpointer user_data)
{
    g_return_val_if_fail(callback != NULL, NULL);

    RelayEventListener *listener = g_new0(RelayEventListener, 1);
    listener->callback = callback;
    listener->quit_callback = quit_callback;
    listener->user_data = user_data;
    listener->fd_source_id = 0;
    listener->timer_source_id = 0;
    listener->initial_bus_owner = NULL;
    listener->timer_check_count = 0;
    g_atomic_int_set(&listener->started, 0);
    g_mutex_init(&listener->startup_mutex);
    g_cond_init(&listener->startup_cond);

    return listener;
}

gboolean relay_event_listener_start(RelayEventListener *listener)
{
    g_return_val_if_fail(listener != NULL, FALSE);

    if (listener->thread) {
        spdlog::warn("Relay event listener is already started");
        return FALSE;
    }

    listener->thread = g_thread_new("relay_event_listener",
                                     relay_event_listener_thread_func, listener);
    if (listener->thread == NULL) {
        spdlog::error("Failed to create relay event listener thread");
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
        spdlog::error("Relay event listener thread failed during startup");
    }

    return ok;
}

void relay_event_listener_stop(RelayEventListener *listener)
{
    g_return_if_fail(listener != NULL);

    if (listener->loop != NULL)
        g_main_loop_quit(listener->loop);

    if (listener->thread) {
        g_thread_join(listener->thread);
        listener->thread = NULL;
    }
}

void relay_event_listener_free(RelayEventListener *listener)
{
    if (listener == NULL)
        return;

    relay_event_listener_stop(listener);

    g_free(listener->initial_bus_owner);
    g_mutex_clear(&listener->startup_mutex);
    g_cond_clear(&listener->startup_cond);
    g_free(listener);
}
