// SPDX-FileCopyrightText: 2025 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#define G_LOG_USE_STRUCTURED
#include <glib.h>
#include <glib-unix.h>
#include <locale.h>

#include "mount-monitor.h"
#include "event-listener.h"
#include "event-dispatcher.h"

#define DISPATCHER_SOCKET_PATH "/run/deepin-anything/event-dispatcher.sock"

static GMainLoop *loop = NULL;

static void on_file_event(gpointer user_data, fs_event *event)
{
    ServerEventDispatcher *dispatcher = (ServerEventDispatcher *)user_data;
    server_event_dispatcher_push_event(dispatcher, event);
}

static gboolean on_signal(gpointer user_data)
{
    (void)user_data;
    g_message("Received termination signal, shutting down...");
    if (loop) {
        g_main_loop_quit(loop);
    }
    return G_SOURCE_REMOVE;
}

int main(G_GNUC_UNUSED int argc, G_GNUC_UNUSED char *argv[])
{
    int ret = 0;
    ServerEventDispatcher *dispatcher = NULL;
    ServerEventListener *listener = NULL;

    setlocale(LC_ALL, "");

    g_auto(GStrv) fstypes = g_strsplit("overlay,btrfs,fuse.dlnfs,ulnfs", ",", 0);
    GUnixMountMonitor *monitor = mount_monitor_init(fstypes);

    loop = g_main_loop_new(NULL, FALSE);
    if (!loop) {
        g_critical("Failed to initialize main event loop");
        ret = 1;
        goto quit;
    }

    g_unix_signal_add(SIGTERM, on_signal, NULL);
    g_unix_signal_add(SIGINT, on_signal, NULL);

    dispatcher = server_event_dispatcher_new(DISPATCHER_SOCKET_PATH);
    if (!dispatcher) {
        g_critical("Failed to create server event dispatcher");
        ret = 1;
        goto quit;
    }

    if (!server_event_dispatcher_start(dispatcher)) {
        g_critical("Failed to start server event dispatcher");
        ret = 1;
        goto quit;
    }

    listener = server_event_listener_new(on_file_event, dispatcher);
    if (!listener) {
        g_critical("Failed to create event listener");
        ret = 1;
        goto quit;
    }

    if (!server_event_listener_start(listener)) {
        g_critical("Failed to start event listener");
        ret = 1;
        goto quit;
    }

    g_message("deepin-anything-server started");
    g_main_loop_run(loop);
    g_message("deepin-anything-server stopping");

quit:
    if (listener) {
        server_event_listener_stop(listener);
        server_event_listener_free(listener);
    }
    if (dispatcher) {
        server_event_dispatcher_stop(dispatcher);
        server_event_dispatcher_free(dispatcher);
    }
    if (monitor) {
        g_object_unref(monitor);
    }
    if (loop) {
        g_main_loop_unref(loop);
    }

    g_message("deepin-anything-server shutdown complete");
    return ret;
}
