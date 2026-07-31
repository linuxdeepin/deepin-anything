// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

/* Test receiver for events produced by src/kernelmod.
 *
 * At startup reads /sys/kernel/vfs_monitor/transport to select the
 * active transport:
 *   - "mmap": open /dev/vfs_monitor + /dev/vfs_monitor_proc and read
 *     ring-buffer records via poll/read.
 *   - "genl": join the vfsmonitor generic netlink multicast groups and
 *     parse VFSMONITOR_C_NOTIFY / VFSMONITOR_C_NOTIFY_PROCESS_INFO.
 *
 * Both paths feed a normalized EpEvent to the same printer. Built on
 * GLib: GMainLoop for the event loop, g_unix_fd_add for fd readiness,
 * g_unix_signal_add for graceful SIGINT/SIGTERM.
 */

#define G_LOG_USE_STRUCTURED
#define _GNU_SOURCE
#include <glib.h>
#include <glib-unix.h>
#include <signal.h>
#include <string.h>

#include "transport.h"

/* Forward declarations of receiver factories. */
typedef struct EpMmapReceiver EpMmapReceiver;
typedef struct EpMmapRingReceiver EpMmapRingReceiver;
typedef struct GenlReceiver GenlReceiver;
EpMmapReceiver *ep_mmap_receiver_new(const char *dentry_dev,
                                     const char *proc_dev,
                                     void (*on_event)(const EpEvent *, gpointer),
                                     gpointer user_data);
void ep_mmap_receiver_free(EpMmapReceiver *r);
EpMmapRingReceiver *ep_mmap_ring_receiver_new(const char *dentry_dev,
                                               const char *proc_dev,
                                               void (*on_event)(const EpEvent *, gpointer),
                                               gpointer user_data);
void ep_mmap_ring_receiver_free(EpMmapRingReceiver *r);
GenlReceiver *ep_genl_receiver_new(void (*on_event)(const EpEvent *, gpointer),
                                   gpointer user_data);
void ep_genl_receiver_free(GenlReceiver *r);

typedef struct {
    GMainLoop *loop;
    gpointer   receiver;          /* EpMmapReceiver* or GenlReceiver* */
    EpTransport transport;
    gint       event_count;       /* atomic */
} App;

static void on_event(const EpEvent *ev, gpointer data)
{
    App *app = data;
    g_atomic_int_inc(&app->event_count);
    ep_event_print(ev);
}

static gboolean on_term_signal(gpointer data)
{
    App *app = data;
    g_message("Received termination signal, shutting down...");
    g_main_loop_quit(app->loop);
    return G_SOURCE_REMOVE;
}

static void print_help(const char *prog)
{
    g_print("Usage: %s [OPTIONS]\n\n", prog);
    g_print("Receive and print VFS events from the kernel module.\n");
    g_print("Transport is auto-detected from /sys/kernel/vfs_monitor/transport.\n\n");
    g_print("Options:\n");
    g_print("  --dentry=PATH   Dentry device (mmap only, default: /dev/vfs_monitor)\n");
    g_print("  --proc=PATH     Process device (mmap only, default: /dev/vfs_monitor_proc)\n");
    g_print("  --transport=T   Force transport: mmap, mmap-ring, or genl (overrides sysfs)\n");
    g_print("  -h, --help      Show this help and exit\n");
}

int main(int argc, char **argv)
{
    const char *dentry_dev = NULL;
    const char *proc_dev = NULL;
    const char *force_transport = NULL;

    for (int i = 1; i < argc; i++) {
        if (g_str_has_prefix(argv[i], "--dentry="))
            dentry_dev = argv[i] + strlen("--dentry=");
        else if (g_str_has_prefix(argv[i], "--proc="))
            proc_dev = argv[i] + strlen("--proc=");
        else if (g_str_has_prefix(argv[i], "--transport="))
            force_transport = argv[i] + strlen("--transport=");
        else if (g_strcmp0(argv[i], "-h") == 0 ||
                 g_strcmp0(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        } else {
            g_printerr("Unknown option: %s\n", argv[i]);
            print_help(argv[0]);
            return 2;
        }
    }

    App app = {0};
    app.loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGINT, on_term_signal, &app);
    g_unix_signal_add(SIGTERM, on_term_signal, &app);

    /* Determine transport. */
    if (force_transport != NULL) {
        if (g_str_equal(force_transport, "genl"))
            app.transport = EP_TRANSPORT_GENL;
        else if (g_str_equal(force_transport, "mmap"))
            app.transport = EP_TRANSPORT_MMAP;
        else if (g_str_equal(force_transport, "mmap-ring"))
            app.transport = EP_TRANSPORT_MMAP_RING;
        else {
            g_printerr("Invalid --transport=%s (expected mmap, mmap-ring, or genl)\n",
                       force_transport);
            return 2;
        }
    } else {
        app.transport = ep_transport_detect();
    }

    const char *tname = "mmap";
    if (app.transport == EP_TRANSPORT_GENL)
        tname = "genl";
    else if (app.transport == EP_TRANSPORT_MMAP_RING)
        tname = "mmap-ring";
    g_message("event-printer started (transport: %s)", tname);

    /* Create the appropriate receiver. Both feed on_event(). */
    if (app.transport == EP_TRANSPORT_GENL) {
        app.receiver = ep_genl_receiver_new(on_event, &app);
        if (!app.receiver) {
            g_critical("Failed to create genl receiver");
            g_main_loop_unref(app.loop);
            return 1;
        }
    } else if (app.transport == EP_TRANSPORT_MMAP_RING) {
        app.receiver = ep_mmap_ring_receiver_new(dentry_dev, proc_dev,
                                                 on_event, &app);
        if (!app.receiver) {
            g_main_loop_unref(app.loop);
            return 1;
        }
    } else {
        app.receiver = ep_mmap_receiver_new(dentry_dev, proc_dev,
                                             on_event, &app);
        if (!app.receiver) {
            g_main_loop_unref(app.loop);
            return 1;
        }
    }

    g_main_loop_run(app.loop);

    /* Cleanup + stats. */
    gint total = g_atomic_int_get(&app.event_count);
    if (total > 0)
        g_message("Total events received: %d", total);

    if (app.transport == EP_TRANSPORT_GENL)
        ep_genl_receiver_free(app.receiver);
    else if (app.transport == EP_TRANSPORT_MMAP_RING)
        ep_mmap_ring_receiver_free(app.receiver);
    else
        ep_mmap_receiver_free(app.receiver);

    g_main_loop_unref(app.loop);
    g_message("event-printer shutdown complete");
    return 0;
}
