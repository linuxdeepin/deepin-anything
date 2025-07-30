// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef EVENT_LOGGER_H
#define EVENT_LOGGER_H

#define G_LOG_USE_STRUCTURED
#include <glib.h>
#include "datatype.h"

G_BEGIN_DECLS

/**
 * EventLogger:
 *
 * An opaque structure representing an asynchronous file system event logger.
 * EventLogger processes file system events in a separate worker thread and
 * outputs them in CSV format through a configurable log handler.
 *
 * Since: 1.0
 */
typedef struct EventLogger EventLogger;

/**
 * LogHandler:
 * @user_data: User data passed to the handler
 * @content: CSV-formatted log content to be processed
 *
 * A callback function type for handling formatted log output.
 * This function will be called from the worker thread context,
 * so implementations must be thread-safe.
 *
 * The @content parameter contains a single CSV line with a trailing newline.
 * The handler should not modify or free the @content string.
 *
 * Since: 1.0
 */
typedef void (*LogHandler)(gpointer user_data, const gchar *content);

/**
 * event_logger_new:
 * @handler: (not nullable): A #LogHandler callback function
 * @user_data: (nullable): User data to pass to the handler
 *
 * Creates a new #EventLogger instance with the specified log handler.
 * The logger is created in a stopped state and must be started with
 * event_logger_start() before it can process events.
 *
 * Returns: (transfer full): A new #EventLogger instance, or %NULL on failure
 * Since: 1.0
 */
EventLogger *event_logger_new(LogHandler handler, gpointer user_data);

/**
 * event_logger_free:
 * @logger: (nullable): An #EventLogger instance
 *
 * Frees an #EventLogger instance and all associated resources.
 * If the logger is currently running, it will be stopped first.
 * This function is safe to call with %NULL.
 *
 * Since: 1.0
 */
void event_logger_free(EventLogger *logger);

/**
 * event_logger_start:
 * @logger: An #EventLogger instance
 *
 * Starts the event logger worker thread. Once started, the logger
 * will process events submitted via event_logger_log_event().
 *
 * Returns: %TRUE if the logger was started successfully, %FALSE otherwise
 * Since: 1.0
 */
gboolean event_logger_start(EventLogger *logger);

/**
 * event_logger_stop:
 * @logger: An #EventLogger instance
 *
 * Stops the event logger worker thread and waits for it to complete.
 * Any remaining events in the queue will be processed before stopping.
 * This function is safe to call multiple times or on an already stopped logger.
 *
 * Since: 1.0
 */
void event_logger_stop(EventLogger *logger);

/**
 * event_logger_log_event:
 * @logger: An #EventLogger instance
 * @event: (transfer full): A #FileEvent to be logged
 *
 * Submits a file system event for logging. The event will be processed
 * asynchronously by the worker thread. The logger takes ownership of
 * the @event and will free it when processing is complete.
 *
 * The logger must be in a running state (started with event_logger_start())
 * for this function to succeed.
 *
 * Since: 1.0
 */
void event_logger_log_event(EventLogger *logger, FileEvent *event);

G_END_DECLS

#endif // EVENT_LOGGER_H
