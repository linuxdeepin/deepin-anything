// SPDX-FileCopyrightText: 2026 Uniontech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#define _GNU_SOURCE
#define G_LOG_USE_STRUCTURED
#include <glib.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "event_dispatcher.h"

#define EXAMPLE_SOCKET "/tmp/dispatcher_example.sock"
#define EXAMPLE_SYNC_READY 'R'
#define EXAMPLE_SYNC_GO 'G'

static gchar **example_get_subscribed(G_GNUC_UNUSED uid_t uid,
                                       gpointer user_data)
{
    gchar **prefixes = user_data;
    if (!prefixes)
        return NULL;
    return g_strdupv(prefixes);
}

static void sync_signal(int fd, char tag)
{
    write(fd, &tag, 1);
}

static gboolean sync_wait(int fd, char expect)
{
    char c;
    if (read(fd, &c, 1) != 1)
        return FALSE;
    return c == expect;
}

int main(void)
{
    static const char *default_prefixes[] = { "/", NULL };
    dispatch_config_t config = {
        .get_user_subscribed = example_get_subscribed,
        .user_data           = (gpointer)default_prefixes,
        .max_users           = 0,
        .max_connections_per_user = 0,
    };

    EventDispatcher *dispatcher = event_dispatcher_new(EXAMPLE_SOCKET, &config);
    if (dispatcher == NULL) {
        g_printerr("failed to create dispatcher\n");
        return 1;
    }
    g_print("[server] dispatcher listening on %s\n", EXAMPLE_SOCKET);

    int parent_to_child[2];
    int child_to_parent[2];
    if (pipe(parent_to_child) != 0 || pipe(child_to_parent) != 0) {
        g_printerr("pipe() failed\n");
        event_dispatcher_free(dispatcher);
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        g_printerr("fork() failed\n");
        event_dispatcher_free(dispatcher);
        return 1;
    }

    if (pid == 0) {
        /* ── Child: receiver (client) ─────────────────────────────── */
        close(parent_to_child[1]);
        close(child_to_parent[0]);

        sync_wait(parent_to_child[0], EXAMPLE_SYNC_GO);

        EventReceiver *receiver = event_receiver_new(EXAMPLE_SOCKET);
        if (receiver == NULL) {
            g_printerr("[client] failed to connect\n");
            _exit(1);
        }
        g_print("[client] connected to dispatcher\n");

        sync_signal(child_to_parent[1], EXAMPLE_SYNC_READY);

        alarm(5);
        dispatch_event_t event = {0};
        EventReceiveResult result = event_receiver_receive(receiver, &event);
        alarm(0);

        if (result != EVENT_RECEIVE_OK) {
            g_printerr("[client] did not receive event (result=%d)\n", result);
            event_receiver_free(receiver);
            _exit(1);
        }

        g_print("[client] received event: action=%d path=%s\n",
                event.event_action, event.event_path);

        event_receiver_free(receiver);
        _exit(0);
    }

    /* ── Parent: dispatcher (server) ──────────────────────────────── */
    close(parent_to_child[0]);
    close(child_to_parent[1]);

    sync_signal(parent_to_child[1], EXAMPLE_SYNC_GO);
    sync_wait(child_to_parent[0], EXAMPLE_SYNC_READY);

    event_dispatcher_accept(dispatcher);
    g_print("[server] accepted client connection\n");

    usleep(10000);

    dispatch_event_t event = {0};
    event.event_action = 42;
    g_strlcpy(event.event_path, "/home/user/example.txt",
              sizeof(event.event_path));

    if (!event_dispatcher_send(dispatcher, &event)) {
        g_printerr("[server] failed to send event\n");
        kill(pid, SIGTERM);
    } else {
        g_print("[server] sent event: action=%d path=%s\n",
                event.event_action, event.event_path);
    }

    int status;
    waitpid(pid, &status, 0);

    event_dispatcher_free(dispatcher);
    close(parent_to_child[1]);
    close(child_to_parent[0]);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        g_print("example completed successfully\n");
        return 0;
    }

    g_printerr("example failed (child status=%d)\n", status);
    return 1;
}
