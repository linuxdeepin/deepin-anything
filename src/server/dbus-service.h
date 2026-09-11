// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SERVER_DBUS_SERVICE_H
#define SERVER_DBUS_SERVICE_H

#define G_LOG_USE_STRUCTURED
#include <glib.h>

#include "event-relay-dispatcher.h"

G_BEGIN_DECLS

/**
 * AnythingDBusService:
 *
 * An opaque D-Bus service object that exports the org.deepin.Anything
 * interface on the system bus. The service exposes two methods:
 * SetAllowCaller and GetEventChannel. Method handlers run on the main
 * thread's #GMainLoop (GDBus dispatch context).
 */
typedef struct AnythingDBusService AnythingDBusService;

/**
 * anything_dbus_service_new: (constructor)
 * @dispatcher: the #ServerEventRelayDispatcher (not owned)
 * @loop: the main #GMainLoop (not owned) — quit on bus name loss
 *
 * Creates a new D-Bus service. Does NOT register on the bus yet — call
 * anything_dbus_service_start() to acquire the bus name and register the
 * object.
 *
 * If the bus name cannot be acquired (or is lost while running),
 * the main loop is quit so the server exits.
 *
 * Returns: (transfer full) (nullable): a new #AnythingDBusService, or
 *     %NULL on failure
 */
AnythingDBusService *anything_dbus_service_new(ServerEventRelayDispatcher *dispatcher,
                                                GMainLoop *loop);

/**
 * anything_dbus_service_start:
 * @service: an #AnythingDBusService
 *
 * Connects to the system bus and acquires the org.deepin.Anything bus
 * name. The actual name acquisition and object registration happen
 * asynchronously via callbacks fired on the main #GMainLoop.
 *
 * Returns: %TRUE if the bus name request was submitted successfully,
 *     %FALSE on failure
 */
gboolean anything_dbus_service_start(AnythingDBusService *service);

/**
 * anything_dbus_service_stop:
 * @service: (nullable): an #AnythingDBusService
 *
 * Unregisters the object and releases the bus name. Safe to call on
 * %NULL.
 */
void anything_dbus_service_stop(AnythingDBusService *service);

/**
 * anything_dbus_service_free: (skip)
 * @service: (nullable)
 *
 * Stops and frees the service. Safe to call on %NULL.
 */
void anything_dbus_service_free(AnythingDBusService *service);

G_END_DECLS

#endif /* SERVER_DBUS_SERVICE_H */
