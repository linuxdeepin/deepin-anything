// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef EVENT_LISTENER_H
#define EVENT_LISTENER_H

#include "datatype.h"

#define G_LOG_USE_STRUCTURED
#include <glib.h>

/**
 * EventListener:
 * 
 * An opaque structure representing a file system event listener.
 * This listener monitors VFS (Virtual File System) events through
 * netlink sockets and forwards them to registered handlers.
 */
typedef struct EventListener EventListener;

/**
 * FileEventHandler:
 * @user_data: User data passed to the handler
 * @event: The file system event that occurred
 * 
 * Callback function type for handling file system events.
 * The handler is responsible for processing the event and
 * freeing the event structure when done.
 * 
 * Note: The @event parameter must be freed by the handler
 * using the appropriate cleanup function.
 */
typedef void (*FileEventHandler)(gpointer user_data, FileEvent *event);

/**
 * event_listener_new:
 * @handler: The callback function to handle events
 * @user_data: User data to pass to the handler
 * 
 * Creates a new event listener instance.
 * 
 * Returns: A new #EventListener instance, or %NULL on failure
 */
EventListener *event_listener_new(FileEventHandler handler, gpointer user_data);

/**
 * event_listener_set_event_mask:
 * @listener: An #EventListener instance
 * @mask: Bitmask of events to monitor
 * 
 * Sets the event mask to filter which types of file system
 * events should be monitored.
 * 
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean event_listener_set_event_mask(EventListener *listener, guint mask);

/**
 * event_listener_set_disable_event_merge:
 * @listener: An #EventListener instance
 * @disable_event_merge: Whether to disable event merge
 * 
 * Sets whether to disable event merge.
 * 
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean event_listener_set_disable_event_merge(EventListener *listener, gboolean disable_event_merge);

/**
 * event_listener_start:
 * @listener: An #EventListener instance
 * 
 * Starts the event listener. After calling this function,
 * the listener will begin monitoring file system events
 * and calling the registered handler.
 * 
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean event_listener_start(EventListener *listener);

/**
 * event_listener_stop:
 * @listener: An #EventListener instance
 * 
 * Stops the event listener. After calling this function,
 * the listener will stop monitoring events.
 */
void event_listener_stop(EventListener *listener);

/**
 * event_listener_free:
 * @listener: (nullable): An #EventListener instance
 * 
 * Frees an event listener instance and all associated resources.
 * If @listener is %NULL, this function does nothing.
 */
void event_listener_free(EventListener *listener);


gboolean is_kernel_module_available(void);
gboolean is_kernel_module_reload(void);

#endif // EVENT_LISTENER_H


