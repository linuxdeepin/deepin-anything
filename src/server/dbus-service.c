// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#define G_LOG_USE_STRUCTURED
#include "dbus-service.h"

#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <unistd.h>

#include "event-relay-dispatcher.h"

/* ── D-Bus constants ────────────────────────────────────────────── */

#define ANYTHING_BUS_NAME      "org.deepin.Anything"
#define ANYTHING_OBJECT_PATH   "/org/deepin/Anything"
#define ANYTHING_INTERFACE     "org.deepin.Anything"

static const gchar introspection_xml[] =
    "<node>"
    "  <interface name='" ANYTHING_INTERFACE "'>"
    "    <method name='SetAllowCaller'>"
    "      <arg type='s' name='unique_name' direction='in'/>"
    "    </method>"
    "    <method name='GetEventChannel'>"
    "      <arg type='h' name='fd' direction='out'/>"
    "      <arg type='u' name='event_protocol_id' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

/* ── Data structure ──────────────────────────────────────────────── */

struct AnythingDBusService {
    GDBusConnection          *connection;     /* system bus connection (owned)             */
    guint                     bus_name_id;    /* g_bus_own_name() registration ID           */
    guint                     object_id;      /* g_dbus_connection_register_object() ID    */
    ServerEventRelayDispatcher *dispatcher;   /* not owned — lives in main()               */
    GMainLoop               *loop;           /* main loop (not owned) — for fatal quit    */
    GHashTable               *allow_callers;  /* set of unique bus names (owned strings)   */
    GCancellable             *list_names_cancellable; /* cancels pending ListNames call   */
};

/* ── Method handlers ────────────────────────────────────────────── */

/* Forward declarations — defined after the method handlers. */
static void get_event_channel_callback(ServerEventRelayDispatcher *dispatcher,
                                        int fd,
                                        guint32 event_protocol_id,
                                        gpointer user_data);

static void list_names_callback(GObject *source_object,
                                GAsyncResult *res,
                                gpointer user_data);

static void handle_set_allow_caller(AnythingDBusService *service,
                                    GVariant *parameters,
                                    GDBusMethodInvocation *invocation)
{
    const gchar *unique_name = NULL;
    g_variant_get(parameters, "(s)", &unique_name);

    /* g_variant_get returns a borrowed pointer; g_strdup before inserting
     * into the hash table so it owns the key. g_hash_table_insert is
     * idempotent — if the key already exists the new key copy is freed. */
    g_hash_table_insert(service->allow_callers, g_strdup(unique_name), NULL);

    g_message("SetAllowCaller: registered %s", unique_name);

    /* g_variant_new("()") is floating; return_value takes ownership. */
    g_dbus_method_invocation_return_value(invocation, g_variant_new("()"));
    /* Lazy cleanup: asynchronously query the current set of unique bus
     * names on the system bus and evict any allow_callers entries that
     * are no longer connected. This bounds the set size — without it,
     * disconnected callers' unique names accumulate forever.
     *
     * Unique bus names are monotonically increasing and never reused,
     * so stale entries pose no impersonation risk; this is purely a
     * memory hygiene measure. The call is async (non-blocking) and the
     * callback runs on the main thread (GDBus dispatch), so no locking
     * is needed for allow_callers. */
    if (service->connection != NULL) {
        /* Reset the cancellable so a previously cancelled stop() doesn't
         * prevent new cleanup calls. g_cancellable_reset is safe here
         * because we're on the main thread, the same thread where the
         * callback fires. */
        g_cancellable_reset(service->list_names_cancellable);
        g_dbus_connection_call(
            service->connection,
            "org.freedesktop.DBus",   /* bus name of the dbus daemon  */
            "/org/freedesktop/DBus",  /* well-known object path        */
            "org.freedesktop.DBus",   /* interface                     */
            "ListNames",              /* method                        */
            NULL,                     /* parameters — no input args    */
            G_VARIANT_TYPE("(as)"),   /* expected reply type           */
            G_DBUS_CALL_FLAGS_NONE,
            -1,                       /* timeout — use default         */
            service->list_names_cancellable,
            list_names_callback,
            service);                 /* user_data — borrowed service  */
    }

}

/* ── Lazy cleanup callback for allow_callers ─────────────────────── */

static void list_names_callback(GObject *source_object,
                                GAsyncResult *res,
                                gpointer user_data)
{
    GDBusConnection *connection = G_DBUS_CONNECTION(source_object);
    AnythingDBusService *service = (AnythingDBusService *)user_data;

    g_autoptr(GVariant) result = NULL;
    g_autoptr(GError) error = NULL;

    result = g_dbus_connection_call_finish(connection, res, &error);
    if (result == NULL) {
        /* Cancellation happens during shutdown — not a warning-worthy
         * condition. Other errors are unexpected. */
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            g_warning("ListNames cleanup failed: %s",
                      error ? error->message : "unknown");
        return;
    }

    /* Build a temporary lookup set from the ListNames snapshot.
     * Keys are borrowed from the GVariant (not g_strdup'd) — the
     * GVariant is held alive for the duration of this function via
     * the g_autoptr above, so the borrowed pointers are valid. The
     * lookup set must NOT use a key-destroy function. */
    g_autoptr(GHashTable) live_names =
        g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);

    GVariantIter iter;
    g_variant_iter_init(&iter, g_variant_get_child_value(result, 0));
    const gchar *name;
    while (g_variant_iter_next(&iter, "&s", &name)) {
        /* &s: borrowed pointer, valid while @result is alive.
         * Insert into lookup set with NULL key-destroy (no ownership). */
        g_hash_table_insert(live_names, (gpointer)name, NULL);
    }

    /* Evict allow_callers entries not in the live snapshot.
     * Iterator-safe removal — g_hash_table_iter_remove is safe during
     * iteration, unlike g_hash_table_remove. */
    GHashTableIter ht_iter;
    gpointer key;
    g_hash_table_iter_init(&ht_iter, service->allow_callers);
    while (g_hash_table_iter_next(&ht_iter, &key, NULL)) {
        if (!g_hash_table_contains(live_names, key)) {
            g_message("allow_callers: evicted stale entry %s", (gchar *)key);
            g_hash_table_iter_remove(&ht_iter);
        }
    }
}

static void handle_get_event_channel(AnythingDBusService *service,
                                     GDBusMethodInvocation *invocation)
{
    const gchar *sender = g_dbus_method_invocation_get_sender(invocation);

    if (!g_hash_table_contains(service->allow_callers, sender)) {
        g_dbus_method_invocation_return_dbus_error(
            invocation,
            "org.deepin.Anything.Error.NotAllowed",
            "Caller not in allow_callers");
        g_message("GetEventChannel: rejected caller %s (not allow-listed)", sender);
        return;
    }

    /* Enqueue the async request. The callback completes the invocation
     * on the worker thread — GDBus method-invocation returns are
     * thread-safe. Do NOT return the invocation here. */
    gboolean enqueued = server_event_relay_dispatcher_get_event_channel(
        service->dispatcher, get_event_channel_callback, invocation);

    if (!enqueued) {
        g_dbus_method_invocation_return_dbus_error(
            invocation,
            "org.deepin.Anything.Error.ChannelFailed",
            "Failed to enqueue channel request");
    }
}

static void handle_method_call(GDBusConnection *connection,
                               const gchar *sender,
                               const gchar *object_path,
                               const gchar *interface_name,
                               const gchar *method_name,
                               GVariant *parameters,
                               GDBusMethodInvocation *invocation,
                               gpointer user_data)
{
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)interface_name;

    AnythingDBusService *service = (AnythingDBusService *)user_data;

    if (g_strcmp0(method_name, "SetAllowCaller") == 0) {
        handle_set_allow_caller(service, parameters, invocation);
    } else if (g_strcmp0(method_name, "GetEventChannel") == 0) {
        handle_get_event_channel(service, invocation);
    } else {
        g_dbus_method_invocation_return_dbus_error(
            invocation,
            "org.freedesktop.DBus.Error.UnknownMethod",
            "Unknown method");
    }
}

/* ── Callback for async get_event_channel ───────────────────────── */

static void get_event_channel_callback(ServerEventRelayDispatcher *dispatcher,
                                       int fd,
                                       guint32 event_protocol_id,
                                       gpointer user_data)
{
    (void)dispatcher;

    GDBusMethodInvocation *invocation = G_DBUS_METHOD_INVOCATION(user_data);

    if (fd < 0) {
        g_dbus_method_invocation_return_dbus_error(
            invocation,
            "org.deepin.Anything.Error.ChannelFailed",
            "Failed to create event channel");
        return;
    }

    g_autoptr(GUnixFDList) fd_list = g_unix_fd_list_new();
    g_autoptr(GError) error = NULL;

    /* g_unix_fd_list_append calls dup() on the fd; the original must
     * still be closed by us. */
    gint handle = g_unix_fd_list_append(fd_list, fd, &error);
    if (handle < 0) {
        g_warning("Failed to append fd to FDList: %s",
                  error ? error->message : "unknown");
        close(fd);
        g_dbus_method_invocation_return_dbus_error(
            invocation,
            "org.deepin.Anything.Error.ChannelFailed",
            "Failed to package fd for D-Bus transfer");
        return;
    }

    /* Both the GVariant and fd_list are consumed (floating → owned). */
    GVariant *result = g_variant_new("(hu)", (gint32)handle, event_protocol_id);
    g_dbus_method_invocation_return_value_with_unix_fd_list(
        invocation, result, fd_list);

    close(fd);  /* GUnixFDList dup'd the fd; our copy must be closed */
}

/* ── GDBus interface vtable ─────────────────────────────────────── */

static const GDBusInterfaceVTable interface_vtable = {
    .method_call  = handle_method_call,
    .get_property = NULL,
    .set_property = NULL,
};

/* ── Bus lifecycle callbacks ────────────────────────────────────── */

static void on_bus_acquired(GDBusConnection *connection,
                            const gchar *name,
                            gpointer user_data)
{
    (void)name;

    AnythingDBusService *service = (AnythingDBusService *)user_data;
    service->connection = g_object_ref(connection);

    g_autoptr(GError) error = NULL;
    g_autoptr(GDBusNodeInfo) node_info = g_dbus_node_info_new_for_xml(introspection_xml, &error);
    if (node_info == NULL) {
        g_critical("Failed to parse introspection XML: %s",
                   error ? error->message : "unknown");
        return;
    }

    GDBusInterfaceInfo *iface_info =
        g_dbus_node_info_lookup_interface(node_info, ANYTHING_INTERFACE);
    if (iface_info == NULL) {
        g_critical("Failed to find interface '%s' in introspection XML",
                   ANYTHING_INTERFACE);
        return;
    }

    service->object_id = g_dbus_connection_register_object(
        connection,
        ANYTHING_OBJECT_PATH,
        iface_info,
        &interface_vtable,
        service,
        NULL,
        &error);

    if (service->object_id == 0) {
        g_critical("Failed to register object: %s",
                   error ? error->message : "unknown");
        return;
    }


    g_debug("D-Bus object registered at %s", ANYTHING_OBJECT_PATH);
}

static void on_name_acquired(GDBusConnection *connection,
                             const gchar *name,
                             gpointer user_data)
{
    (void)connection;
    (void)user_data;

    g_message("D-Bus name acquired: %s", name);
}

static void on_name_lost(GDBusConnection *connection,
                         const gchar *name,
                         gpointer user_data)
{
    (void)connection;

    AnythingDBusService *service = (AnythingDBusService *)user_data;

    g_critical("D-Bus name lost: %s — shutting down", name);

    /* Name acquisition failed or was lost — quit the main loop so the
     * server exits with an error. This satisfies PRD R1: bus name
     * acquisition failure is fatal. */
    if (service->loop)
        g_main_loop_quit(service->loop);
}

/* ── Public API ─────────────────────────────────────────────────── */

AnythingDBusService *anything_dbus_service_new(ServerEventRelayDispatcher *dispatcher,
                                                GMainLoop *loop)
{
    g_return_val_if_fail(dispatcher != NULL, NULL);
    g_return_val_if_fail(loop != NULL, NULL);

    AnythingDBusService *service = g_new0(AnythingDBusService, 1);
    service->dispatcher = dispatcher;
    service->loop = loop;
    /* Keys are owned strings (g_free'd on removal); values are NULL
     * (set semantics). */
    service->allow_callers = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                    g_free, NULL);
    service->list_names_cancellable = g_cancellable_new();
    return service;
}

gboolean anything_dbus_service_start(AnythingDBusService *service)
{
    g_return_val_if_fail(service != NULL, FALSE);

    service->bus_name_id = g_bus_own_name(
        G_BUS_TYPE_SYSTEM,
        ANYTHING_BUS_NAME,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired,
        on_name_acquired,
        on_name_lost,
        service,
        NULL);

    if (service->bus_name_id == 0) {
        g_critical("Failed to acquire bus name");
        return FALSE;
    }

    /* g_bus_own_name is async — on_bus_acquired, on_name_acquired, and
     * on_name_lost fire on the main GMainLoop. Since this runs before
     * g_main_loop_run(), the callbacks fire once the loop starts. */
    return TRUE;
}

void anything_dbus_service_stop(AnythingDBusService *service)
{
    if (service == NULL)
        return;
    if (service->object_id > 0 && service->connection != NULL) {
        g_dbus_connection_unregister_object(service->connection,
                                            service->object_id);
        service->object_id = 0;
    }

    /* Cancel any in-flight ListNames async call so its callback fires
     * with G_IO_ERROR_CANCELLED and does not dereference @service after
     * it is freed. The callback handles cancellation silently. */
    g_cancellable_cancel(service->list_names_cancellable);

    if (service->bus_name_id > 0) {
        g_bus_unown_name(service->bus_name_id);
        service->bus_name_id = 0;
    }

    g_clear_object(&service->connection);
}

void anything_dbus_service_free(AnythingDBusService *service)
{
    if (service == NULL)
        return;

    anything_dbus_service_stop(service);
    g_clear_pointer(&service->allow_callers, g_hash_table_unref);
    g_clear_object(&service->list_names_cancellable);
    g_free(service);
}
