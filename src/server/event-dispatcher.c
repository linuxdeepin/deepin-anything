// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "event-dispatcher.h"

#include <errno.h>
#include <pwd.h>
#include <string.h>
#include <sys/sysmacros.h>

#include <glib-unix.h>
#include <glib/gstdio.h>

#include "vfs_change_consts.h"
#include "event_dispatcher.h"
#include "core/mount_info.h"
#include "utils/tools.h"

#define ACT_TERMINATE 100
#define ACT_WAKEUP    101

typedef struct {
    guint8   act;
    dev_t    device_id;
    guint32  cookie;
    gchar   *src;
    gchar   *dst;
} dispatch_fs_event_t;

struct ServerEventDispatcher {
    EventDispatcher *dispatcher;
    MountInfo       *mount_info;
    GAsyncQueue     *event_queue;
    GThread         *thread;
    guint            socket_source_id;
    volatile gint    accept_pending;
    GHashTable      *rename_from;
    gchar           *socket_path;
    gchar          **default_events;  /* GStrv of default path prefixes (owned) */
    GMutex           startup_mutex;
    GCond            startup_cond;
    volatile gint    started;  /* 0 = pending, 1 = success, -1 = failure */
};

static void dispatch_fs_event_init(dispatch_fs_event_t *ev)
{
    ev->act = 0;
    ev->device_id = 0;
    ev->cookie = 0;
    ev->src = NULL;
    ev->dst = NULL;
}

static void dispatch_fs_event_clear(dispatch_fs_event_t *ev)
{
    g_free(ev->src);
    g_free(ev->dst);
    ev->src = NULL;
    ev->dst = NULL;
}

/**
 * get_user_subscribed_cb:
 * @uid: the client's real UID (from SO_PEERCRED)
 * @user_data: the ServerEventDispatcher pointer
 *
 * Callback for the dispatcher library. Expands $HOME prefixes via
 * getpwuid_r and resolves all paths through get_full_path so that
 * subscription prefixes match event paths produced by convert_fs_event
 * (which uses device mount points). This handles bind mounts (e.g.
 * /home/user → /data/home/user) correctly.
 *
 * Returns: (transfer full) (nullable): NULL-terminated array of
 *     fully-resolved path prefixes, or NULL if the user has no
 *     subscriptions. Caller must free with g_strfreev().
 */
static gchar **get_user_subscribed_cb(uid_t uid, gpointer user_data)
{
    ServerEventDispatcher *dispatcher = (ServerEventDispatcher *)user_data;
    if (!dispatcher->default_events)
        return NULL;

    /* Resolve home directory for $HOME expansion */
    struct passwd pwd;
    struct passwd *result = NULL;
    gchar buf[4096];
    if (getpwuid_r(uid, &pwd, buf, sizeof(buf), &result) != 0 || !result)
        return NULL;

    const char *home = pwd.pw_dir;
    if (home == NULL) {
        g_warning("User %u has no home directory, skipping subscription", uid);
        return NULL;
    }

    GPtrArray *out = g_ptr_array_new();

    for (guint i = 0; dispatcher->default_events[i]; i++) {
        const char *prefix = dispatcher->default_events[i];

        /* Step 1: Expand $HOME if present */
        gchar *expanded = NULL;
        if (g_str_has_prefix(prefix, "$HOME")) {
            const char *rest = prefix + 5;  /* skip "$HOME" */
            if (*rest == '\0')
                expanded = g_strdup(home);
            else if (*rest == '/')
                expanded = g_strconcat(home, rest, NULL);
            else
                expanded = g_strdup(prefix);  /* $HOMExyz — not a valid expansion */
        } else {
            expanded = g_strdup(prefix);
        }

        /* Step 2: Resolve through get_full_path for bind-mount awareness.
         * All paths go through get_full_path so subscription prefixes
         * match the event paths produced by convert_fs_event. */
        gchar *full = get_full_path(dispatcher->mount_info, expanded);

        /* Step 3: Ensure trailing slash for prefix matching. All
         * subscription prefixes must end with '/' so that only children
         * match (not the directory itself). get_full_path /
         * find_dir_full_path strips trailing slashes, so we always
         * re-add one. */
        if (full && !g_str_has_suffix(full, "/")) {
            gchar *with_slash = g_strconcat(full, "/", NULL);
            g_free(full);
            full = with_slash;
        }

        g_free(expanded);

        if (full)
            g_ptr_array_add(out, full);
    }

    if (out->len == 0) {
        g_ptr_array_free(out, TRUE);
        return NULL;
    }

    g_ptr_array_add(out, NULL);
    return (gchar **)g_ptr_array_free(out, FALSE);
}

static gboolean convert_fs_event(ServerEventDispatcher *dispatcher,
                                 fs_event *event,
                                 dispatch_fs_event_t *out)
{
    dispatch_fs_event_init(out);

    if (event->act == ACT_MOUNT || event->act == ACT_UNMOUNT) {
        g_debug("%s: %s",
                event->act == ACT_MOUNT ? "Mount a device" : "Unmount a device",
                event->src);
        mount_info_update(dispatcher->mount_info);
        return TRUE;
    }

    gchar *root = NULL;
    if (event->act < ACT_MOUNT) {
        out->device_id = makedev(event->major, event->minor);

        const gchar *mount_point = mount_info_get_device_mount_point(
            dispatcher->mount_info, out->device_id);
        if (!mount_point) {
            g_debug("Unknown device: %u, dev: %u:%u, path: %s, cookie: %u",
                    +event->act, event->major, +event->minor,
                    event->src, event->cookie);
            return TRUE;
        }
        root = g_strdup(mount_point);
        if (g_strcmp0(root, "/") == 0) {
            g_free(root);
            root = NULL;
        }
    }

    switch (event->act) {
    case ACT_NEW_FILE:
    case ACT_NEW_SYMLINK:
    case ACT_NEW_LINK:
    case ACT_NEW_FOLDER:
    case ACT_DEL_FILE:
    case ACT_DEL_FOLDER:
        out->act = event->act;
        out->src = g_strdup(event->src);
        out->dst = g_strdup("");
        break;
    case ACT_RENAME_FROM_FILE:
    case ACT_RENAME_FROM_FOLDER:
        g_hash_table_insert(dispatcher->rename_from,
                            GUINT_TO_POINTER(event->cookie),
                            g_strdup(event->src));
        g_free(root);
        return TRUE;
    case ACT_RENAME_TO_FILE:
    case ACT_RENAME_TO_FOLDER: {
        gpointer from_src = g_hash_table_lookup(dispatcher->rename_from,
                                                GUINT_TO_POINTER(event->cookie));
        if (from_src) {
            out->act = (event->act == ACT_RENAME_TO_FILE)
                           ? ACT_RENAME_FILE : ACT_RENAME_FOLDER;
            out->cookie = event->cookie;
            out->dst = g_strdup(event->src);
            out->src = g_strdup((const gchar *)from_src);
            g_hash_table_remove(dispatcher->rename_from,
                                GUINT_TO_POINTER(event->cookie));
        } else {
            g_debug("Rename-to without matching rename-from (cookie=%u)",
                    event->cookie);
            g_free(root);
            return TRUE;
        }
        break;
    }
    case ACT_RENAME_FILE:
    case ACT_RENAME_FOLDER:
        g_warning("Unsupported file action: %u", +event->act);
        g_free(root);
        return TRUE;
    default:
        g_warning("Unknown file action: %u", +event->act);
        g_free(root);
        return TRUE;
    }

    if (root) {
        if (out->src) {
            gchar *new_src = g_strconcat(root, out->src, NULL);
            g_free(out->src);
            out->src = new_src;
        }
        if (out->dst && out->dst[0] != '\0') {
            gchar *new_dst = g_strconcat(root, out->dst, NULL);
            g_free(out->dst);
            out->dst = new_dst;
        }
    }

    g_free(root);
    return FALSE;
}

static gboolean is_lowerfs_event(MountInfo *mount_info,
                                 dispatch_fs_event_t *event)
{
    if (!mount_info_exist_lowerfs(mount_info)) {
        return FALSE;
    }

    const gchar *event_file_path = (event->dst && event->dst[0] != '\0')
                                       ? event->dst : event->src;
    if (!event_file_path) {
        return FALSE;
    }

    const GList *child_mount_points = mount_info_get_child_mount_points(
        mount_info, event->device_id);
    if (child_mount_points) {
        for (const GList *iter = child_mount_points; iter != NULL;
             iter = iter->next) {
            if (g_str_has_prefix(event_file_path, (const gchar *)iter->data)) {
                g_debug("%s is under child mount point: %s",
                        event_file_path, (gchar *)iter->data);
                return TRUE;
            }
        }
    }

    return FALSE;
}

static void send_dispatch_event(ServerEventDispatcher *dispatcher,
                                gint32 action, guint32 cookie,
                                const gchar *path)
{
    dispatch_event_t evt;
    evt.event_action = action;
    evt.cookie = cookie;
    g_strlcpy(evt.event_path, path ? path : "",
              sizeof(evt.event_path));

    event_dispatcher_send(dispatcher->dispatcher, &evt);
}

static void process_event(ServerEventDispatcher *dispatcher, fs_event *event)
{
    dispatch_fs_event_t ev;
    if (convert_fs_event(dispatcher, event, &ev)) {
        dispatch_fs_event_clear(&ev);
        return;
    }

    if (is_lowerfs_event(dispatcher->mount_info, &ev)) {
        dispatch_fs_event_clear(&ev);
        return;
    }

    if (ev.act == ACT_RENAME_FILE || ev.act == ACT_RENAME_FOLDER) {
        send_dispatch_event(dispatcher,
                            ev.act == ACT_RENAME_FILE
                                ? ACT_RENAME_FROM_FILE
                                : ACT_RENAME_FROM_FOLDER,
                            ev.cookie, ev.src);
        send_dispatch_event(dispatcher,
                            ev.act == ACT_RENAME_FILE
                                ? ACT_RENAME_TO_FILE
                                : ACT_RENAME_TO_FOLDER,
                            ev.cookie, ev.dst);
    } else {
        send_dispatch_event(dispatcher, ev.act, ev.cookie, ev.src);
    }

    dispatch_fs_event_clear(&ev);
}

static gboolean on_socket_readable(G_GNUC_UNUSED gint fd,
                                   G_GNUC_UNUSED GIOCondition condition,
                                   gpointer data)
{
    ServerEventDispatcher *dispatcher = (ServerEventDispatcher *)data;

    g_atomic_int_set(&dispatcher->accept_pending, TRUE);
    fs_event *wakeup = g_slice_new0(fs_event);
    wakeup->act = ACT_WAKEUP;
    g_async_queue_push(dispatcher->event_queue, wakeup);

    return G_SOURCE_CONTINUE;
}

static void signal_startup(ServerEventDispatcher *dispatcher, gboolean success)
{
    g_mutex_lock(&dispatcher->startup_mutex);
    g_atomic_int_set(&dispatcher->started, success ? 1 : -1);
    g_cond_signal(&dispatcher->startup_cond);
    g_mutex_unlock(&dispatcher->startup_mutex);
}

static gpointer event_dispatcher_thread_func(gpointer data)
{
    ServerEventDispatcher *dispatcher = (ServerEventDispatcher *)data;

    dispatch_config_t config = {
        .get_user_subscribed     = get_user_subscribed_cb,
        .user_data               = dispatcher,
        .max_users               = 10,
        .max_connections_per_user = 10,
    };

    gchar *socket_dir = g_path_get_dirname(dispatcher->socket_path);
    if (g_mkdir_with_parents(socket_dir, 0755) < 0) {
        g_critical("Failed to create socket directory '%s': %s",
                   socket_dir, strerror(errno));
        g_free(socket_dir);
        signal_startup(dispatcher, FALSE);
        return NULL;
    }
    g_free(socket_dir);

    dispatcher->dispatcher = event_dispatcher_new(dispatcher->socket_path,
                                                   &config);
    if (!dispatcher->dispatcher) {
        g_critical("Failed to create event dispatcher");
        signal_startup(dispatcher, FALSE);
        return NULL;
    }

    int listen_fd = event_dispatcher_get_socket(dispatcher->dispatcher);
    if (listen_fd < 0) {
        g_critical("Failed to get dispatcher socket fd");
        event_dispatcher_free(dispatcher->dispatcher);
        dispatcher->dispatcher = NULL;
        signal_startup(dispatcher, FALSE);
        return NULL;
    }

    /* Attach a socket readability source to the main thread's default
     * GMainContext. g_unix_fd_add_full uses g_main_context_get_thread_default()
     * which is NULL in this thread (no thread-default pushed), so the source
     * attaches to the global default context — the main thread's GMainLoop.
     * The callback only sets the atomic accept_pending flag and pushes an
     * empty event to wake up this dispatch thread; accept() is called in
     * the dispatch thread to satisfy the dispatcher's thread-safety
     * requirement. */
    dispatcher->socket_source_id = g_unix_fd_add_full(
        G_PRIORITY_DEFAULT, listen_fd, G_IO_IN,
        on_socket_readable, dispatcher, NULL);
    if (dispatcher->socket_source_id == 0) {
        g_critical("Failed to add socket fd source");
        event_dispatcher_free(dispatcher->dispatcher);
        dispatcher->dispatcher = NULL;
        signal_startup(dispatcher, FALSE);
        return NULL;
    }

    g_message("Event dispatcher thread started (socket: %s)",
              dispatcher->socket_path);
    signal_startup(dispatcher, TRUE);

    while (TRUE) {
        fs_event *event = (fs_event *)g_async_queue_pop(
            dispatcher->event_queue);

        if (g_atomic_int_get(&dispatcher->accept_pending)) {
            g_atomic_int_set(&dispatcher->accept_pending, FALSE);
            event_dispatcher_accept(dispatcher->dispatcher);
        }

        if (event->act == ACT_WAKEUP) {
            g_slice_free(fs_event, event);
            continue;
        }

        if (event->act == ACT_TERMINATE) {
            g_slice_free(fs_event, event);
            break;
        }

        process_event(dispatcher, event);
        g_slice_free(fs_event, event);
    }

    if (dispatcher->socket_source_id > 0) {
        g_source_remove(dispatcher->socket_source_id);
        dispatcher->socket_source_id = 0;
    }

    if (dispatcher->dispatcher) {
        event_dispatcher_free(dispatcher->dispatcher);
        dispatcher->dispatcher = NULL;
    }

    g_message("Event dispatcher thread stopped");
    return NULL;
}

ServerEventDispatcher *server_event_dispatcher_new(const char *socket_path)
{
    g_return_val_if_fail(socket_path != NULL, NULL);

    ServerEventDispatcher *dispatcher = g_new0(ServerEventDispatcher, 1);
    dispatcher->socket_path = g_strdup(socket_path);
    dispatcher->event_queue = g_async_queue_new();
    dispatcher->mount_info = mount_info_new();
    dispatcher->rename_from = g_hash_table_new_full(g_direct_hash,
                                                       g_direct_equal,
                                                       NULL, g_free);
    g_atomic_int_set(&dispatcher->accept_pending, FALSE);
    g_mutex_init(&dispatcher->startup_mutex);
    g_cond_init(&dispatcher->startup_cond);
    g_atomic_int_set(&dispatcher->started, 0);

    static const gchar * const default_prefixes[] = { "$HOME/", NULL };
    dispatcher->default_events = g_strdupv((gchar **)default_prefixes);

    return dispatcher;
}

gboolean server_event_dispatcher_start(ServerEventDispatcher *dispatcher)
{
    g_return_val_if_fail(dispatcher != NULL, FALSE);

    if (dispatcher->thread) {
        g_warning("Event dispatcher is already started");
        return FALSE;
    }

    dispatcher->thread = g_thread_new("event_dispatcher",
                                      event_dispatcher_thread_func,
                                      dispatcher);
    if (!dispatcher->thread) {
        g_critical("Failed to create event dispatcher thread");
        return FALSE;
    }

    g_mutex_lock(&dispatcher->startup_mutex);
    while (g_atomic_int_get(&dispatcher->started) == 0)
        g_cond_wait(&dispatcher->startup_cond, &dispatcher->startup_mutex);
    gboolean ok = (g_atomic_int_get(&dispatcher->started) == 1);
    g_mutex_unlock(&dispatcher->startup_mutex);

    if (!ok) {
        g_thread_join(dispatcher->thread);
        dispatcher->thread = NULL;
        g_critical("Event dispatcher thread failed during startup");
        return FALSE;
    }

    return TRUE;
}

void server_event_dispatcher_stop(ServerEventDispatcher *dispatcher)
{
    g_return_if_fail(dispatcher != NULL);

    /* Only push the terminate sentinel if the thread is still running,
     * so calling stop() twice (e.g. from main + free) doesn't leak. */
    if (dispatcher->thread) {
        if (dispatcher->event_queue) {
            fs_event *terminate = g_slice_new0(fs_event);
            terminate->act = ACT_TERMINATE;
            g_async_queue_push(dispatcher->event_queue, terminate);
        }

        g_thread_join(dispatcher->thread);
        dispatcher->thread = NULL;
    }
}

void server_event_dispatcher_free(ServerEventDispatcher *dispatcher)
{
    if (!dispatcher) {
        return;
    }

    server_event_dispatcher_stop(dispatcher);

    if (dispatcher->rename_from) {
        g_hash_table_destroy(dispatcher->rename_from);
        dispatcher->rename_from = NULL;
    }

    if (dispatcher->mount_info) {
        mount_info_free(dispatcher->mount_info);
        dispatcher->mount_info = NULL;
    }

    if (dispatcher->event_queue) {
        /* Drain any events left in the queue (e.g. if the thread exited
         * early due to an error, the terminate sentinel pushed by stop()
         * was never consumed). */
        fs_event *remaining;
        while ((remaining = g_async_queue_try_pop(dispatcher->event_queue)) != NULL) {
            g_slice_free(fs_event, remaining);
        }
        g_async_queue_unref(dispatcher->event_queue);
        dispatcher->event_queue = NULL;
    }

    g_free(dispatcher->socket_path);
    g_strfreev(dispatcher->default_events);
    g_mutex_clear(&dispatcher->startup_mutex);
    g_cond_clear(&dispatcher->startup_cond);
    g_free(dispatcher);
}

void server_event_dispatcher_push_event(ServerEventDispatcher *dispatcher,
                                        fs_event *event)
{
    g_return_if_fail(dispatcher != NULL);
    g_return_if_fail(event != NULL);

    if (dispatcher->event_queue) {
        g_async_queue_push(dispatcher->event_queue, event);
    } else {
        /* Queue unavailable — free the event to avoid a leak. */
        g_slice_free(fs_event, event);
    }
}
