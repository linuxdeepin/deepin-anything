// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

/* Receiver-side example for deepin-anything-server.
 *
 * Connects to the server's event-dispatcher Unix Domain Socket and prints
 * VFS file events as they arrive. It demonstrates the real-world
 * integration pattern a downstream component would use:
 *
 *   - A dedicated worker thread owns a private GMainContext / GMainLoop so
 *     its fd source does not attach to the main thread's default context
 *     (see the note in src/server/event-dispatcher.c about
 *     g_unix_fd_add_full and thread-default contexts).
 *   - The receiver socket fd is watched non-blockingly via
 *     g_unix_fd_add_full(); when readable, event_receiver_receive() reads
 *     one complete message (SOCK_SEQPACKET preserves boundaries).
 *   - The main thread runs its own GMainLoop only to catch SIGINT/SIGTERM
 *     for graceful shutdown.
 *
 * Lifecycle messages go through GLib structured logging (g_message, ...) so
 * they reach the journal when run as a service; the actual file events are
 * written to stdout so they can be piped or grepped.
 */

#define G_LOG_USE_STRUCTURED
#define _GNU_SOURCE
#include <glib.h>
#include <glib-unix.h>
#include <signal.h>
#include <string.h>

#include "event_dispatcher.h"
#include "vfs_change_consts.h"

#define DEFAULT_SOCKET_PATH "/run/deepin-anything/event-dispatcher.sock"

typedef struct {
    const char     *socket_path;
    GMainLoop      *main_loop;   /* main thread's loop (default context)  */
    GMainLoop      *loop;        /* worker's own loop (private context)   */
    GMainContext   *context;     /* worker's dedicated GMainContext       */
    GThread         *thread;
    EventReceiver  *receiver;
    guint           fd_source_id;
    volatile gint   event_count; /* total events received (atomic)        */
    volatile gint   connected;   /* 1 once connected, 0 otherwise         */
    volatile gint   quitting;    /* 1 once shutdown has been requested    */
} ReceiverWorker;

/* Maps a VFS action code to a stable, human-readable label.
 * Returns NULL for unknown codes so the caller can fall back to a
 * numeric representation. */
static const char *action_to_string(gint32 action)
{
    switch (action) {
    case ACT_NEW_FILE:           return "NEW_FILE";
    case ACT_NEW_LINK:           return "NEW_LINK";
    case ACT_NEW_SYMLINK:        return "NEW_SYMLINK";
    case ACT_NEW_FOLDER:         return "NEW_FOLDER";
    case ACT_DEL_FILE:           return "DEL_FILE";
    case ACT_DEL_FOLDER:         return "DEL_FOLDER";
    case ACT_RENAME_FILE:        return "RENAME_FILE";
    case ACT_RENAME_FOLDER:      return "RENAME_FOLDER";
    case ACT_RENAME_FROM_FILE:   return "RENAME_FROM";
    case ACT_RENAME_TO_FILE:     return "RENAME_TO";
    case ACT_RENAME_FROM_FOLDER: return "RENAME_FROM";
    case ACT_RENAME_TO_FOLDER:   return "RENAME_TO";
    case ACT_MOUNT:              return "MOUNT";
    case ACT_UNMOUNT:            return "UNMOUNT";
    default:                     return NULL;
    }
}

static void print_event(const dispatch_event_t *event)
{
    const char *name = action_to_string(event->event_action);
    if (name != NULL) {
        if (event->cookie != 0)
            g_print("%-12s cookie=%-10u %s\n", name,
                    event->cookie, event->event_path);
        else
            g_print("%-12s %s\n", name, event->event_path);
    } else {
        g_print("UNKNOWN(%-4d) %s\n", event->event_action, event->event_path);
    }
}

static void worker_request_shutdown(ReceiverWorker *w)
{
    g_atomic_int_set(&w->quitting, TRUE);
    if (w->loop != NULL)
        g_main_loop_quit(w->loop);
    if (w->main_loop != NULL)
        g_main_loop_quit(w->main_loop);
}

static gboolean on_fd_readable(G_GNUC_UNUSED gint fd,
                               GIOCondition condition,
                               gpointer data)
{
    ReceiverWorker *w = (ReceiverWorker *)data;

    /* Socket error / remote closed: stop watching and tear everything down. */
    if ((condition & (G_IO_HUP | G_IO_ERR)) != 0) {
        g_message("Dispatcher connection lost");
        w->fd_source_id = 0;
        worker_request_shutdown(w);
        return G_SOURCE_REMOVE;
    }

    dispatch_event_t event = {0};
    EventReceiveResult result = event_receiver_receive(w->receiver, &event);

    switch (result) {
    case EVENT_RECEIVE_OK:
        g_atomic_int_inc(&w->event_count);
        print_event(&event);
        return G_SOURCE_CONTINUE;
    case EVENT_RECEIVE_INTERRUPTED:
        /* recv() was interrupted by a signal; retry on next readiness. */
        return G_SOURCE_CONTINUE;
    case EVENT_RECEIVE_DISCONNECTED:
        g_message("Dispatcher closed the connection");
        w->fd_source_id = 0;
        worker_request_shutdown(w);
        return G_SOURCE_REMOVE;
    case EVENT_RECEIVE_ERROR:
        g_warning("Failed to receive event");
        w->fd_source_id = 0;
        worker_request_shutdown(w);
        return G_SOURCE_REMOVE;
    default:
        return G_SOURCE_CONTINUE;
    }
}

static gpointer worker_thread_func(gpointer data)
{
    ReceiverWorker *w = (ReceiverWorker *)data;

    /* Private context + pushed as thread-default so the fd source added
     * below attaches here, not to the main thread's default context. */
    w->context = g_main_context_new();
    if (w->context == NULL) {
        g_critical("Failed to create worker GMainContext");
        worker_request_shutdown(w);
        return NULL;
    }
    w->loop = g_main_loop_new(w->context, FALSE);
    g_main_context_push_thread_default(w->context);

    w->receiver = event_receiver_new(w->socket_path);
    if (w->receiver == NULL) {
        g_critical("Failed to connect to dispatcher at %s", w->socket_path);
        g_message("Is deepin-anything-server running and reachable?");
        goto cleanup;
    }
    g_atomic_int_set(&w->connected, 1);
    g_message("Connected to dispatcher: %s", w->socket_path);

    int sock_fd = event_receiver_get_socket(w->receiver);
    if (sock_fd < 0) {
        g_critical("Invalid receiver socket fd");
        goto cleanup;
    }

    w->fd_source_id = g_unix_fd_add_full(G_PRIORITY_DEFAULT, sock_fd,
                                         G_IO_IN | G_IO_HUP | G_IO_ERR,
                                         on_fd_readable, w, NULL);
    if (w->fd_source_id == 0) {
        g_critical("Failed to add fd watch");
        goto cleanup;
    }

    /* If a shutdown signal arrived while we were connecting (which can
     * block inside event_receiver_new), honor it before entering the loop. */
    if (g_atomic_int_get(&w->quitting)) {
        g_message("Shutdown requested before entering event loop");
        goto cleanup;
    }

    g_main_loop_run(w->loop);

cleanup:
    if (w->fd_source_id > 0) {
        g_source_remove(w->fd_source_id);
        w->fd_source_id = 0;
    }
    if (w->receiver != NULL) {
        event_receiver_free(w->receiver);
        w->receiver = NULL;
    }
    g_main_context_pop_thread_default(w->context);
    if (w->loop != NULL) {
        g_main_loop_unref(w->loop);
        w->loop = NULL;
    }
    if (w->context != NULL) {
        g_main_context_unref(w->context);
        w->context = NULL;
    }
    /* Ensure the main thread wakes up even on early-exit error paths. */
    if (w->main_loop != NULL)
        g_main_loop_quit(w->main_loop);
    return NULL;
}

static gboolean on_termination_signal(gpointer data)
{
    GMainLoop *loop = (GMainLoop *)data;
    g_message("Received termination signal, shutting down...");
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

static void print_help(const char *prog)
{
    g_print("Usage: %s [OPTIONS]\n\n", prog);
    g_print("Connect to deepin-anything-server and print VFS file events.\n\n");
    g_print("Options:\n");
    g_print("  --socket=PATH   Event dispatcher socket path\n");
    g_print("                  (default: %s)\n", DEFAULT_SOCKET_PATH);
    g_print("  --socket PATH    Same as --socket=PATH\n");
    g_print("  -h, --help       Show this help and exit\n");
}

int main(int argc, char **argv)
{
    const char *socket_path = DEFAULT_SOCKET_PATH;
    int ret = 1;

    for (int i = 1; i < argc; i++) {
        if (g_str_has_prefix(argv[i], "--socket=")) {
            socket_path = argv[i] + strlen("--socket=");
        } else if (g_strcmp0(argv[i], "--socket") == 0) {
            if (++i >= argc) {
                g_printerr("--socket requires a value\n");
                return 2;
            }
            socket_path = argv[i];
        } else if (g_strcmp0(argv[i], "-h") == 0 ||
                   g_strcmp0(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        } else {
            g_printerr("Unknown option: %s\n", argv[i]);
            print_help(argv[0]);
            return 2;
        }
    }

    ReceiverWorker w;
    memset(&w, 0, sizeof(w));
    w.socket_path = socket_path;
    w.fd_source_id = 0;
    g_atomic_int_set(&w.event_count, 0);
    g_atomic_int_set(&w.connected, 0);
    g_atomic_int_set(&w.quitting, 0);

    w.main_loop = g_main_loop_new(NULL, FALSE);
    if (w.main_loop == NULL) {
        g_critical("Failed to create main loop");
        return 1;
    }

    g_unix_signal_add(SIGINT, on_termination_signal, w.main_loop);
    g_unix_signal_add(SIGTERM, on_termination_signal, w.main_loop);

    w.thread = g_thread_new("event_receiver", worker_thread_func, &w);
    if (w.thread == NULL) {
        g_critical("Failed to create worker thread");
        goto quit;
    }

    g_message("example_receiver started (socket: %s)", socket_path);
    g_main_loop_run(w.main_loop);

quit:
    /* Ask the worker to stop (idempotent) and wait for it. */
    g_atomic_int_set(&w.quitting, TRUE);
    if (w.loop != NULL)
        g_main_loop_quit(w.loop);
    if (w.thread != NULL) {
        g_thread_join(w.thread);
        w.thread = NULL;
    }

    if (g_atomic_int_get(&w.connected) &&
        g_atomic_int_get(&w.event_count) > 0) {
        g_message("Total events received: %d",
                  g_atomic_int_get(&w.event_count));
    }
    ret = g_atomic_int_get(&w.connected) ? 0 : 1;

    if (w.main_loop != NULL)
        g_main_loop_unref(w.main_loop);

    g_message("example_receiver shutdown complete");
    return ret;
}
