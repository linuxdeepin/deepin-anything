// SPDX-FileCopyrightText: 2025 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#define G_LOG_USE_STRUCTURED
#include <glib.h>
#include <glib-unix.h>
#include <locale.h>

#include "mount-monitor.h"
#include "event-listener.h"
#include "event-relay-dispatcher.h"
#include "dbus-service.h"

static GMainLoop *loop = NULL;

static void on_file_event(gpointer user_data, fs_event *event)
{
    ServerEventRelayDispatcher *dispatcher = (ServerEventRelayDispatcher *)user_data;
    server_event_relay_dispatcher_push_event(dispatcher, event);
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
    ServerEventRelayDispatcher *dispatcher = NULL;
    ServerEventListener *listener = NULL;
    AnythingDBusService *dbus_service = NULL;

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

    dispatcher = server_event_relay_dispatcher_new(30);
    if (!dispatcher) {
        g_critical("Failed to create server event relay dispatcher");
        ret = 1;
        goto quit;
    }

    if (!server_event_relay_dispatcher_start(dispatcher)) {
        g_critical("Failed to start server event relay dispatcher");
        ret = 1;
        goto quit;
    }

    dbus_service = anything_dbus_service_new(dispatcher, loop);
    if (!dbus_service) {
        g_critical("Failed to create D-Bus service");
        ret = 1;
        goto quit;
    }
    if (!anything_dbus_service_start(dbus_service)) {
        g_critical("Failed to start D-Bus service");
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
    if (dbus_service) {
        anything_dbus_service_stop(dbus_service);
        anything_dbus_service_free(dbus_service);
    }
    if (dispatcher) {
        server_event_relay_dispatcher_stop(dispatcher);
        server_event_relay_dispatcher_free(dispatcher);
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
