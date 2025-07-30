// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "event_logger.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "vfs_change_consts.h"
#include "datatype.h"

/**
 * Special action code used internally to signal worker thread termination.
 * This value should not conflict with any valid file system event actions.
 */
#define ACT_TERMINATE 100

/**
 * Maximum size for timestamp string buffer.
 */
#define TIMESTAMP_BUFFER_SIZE 128

/**
 * EventLogger:
 * @event_queue: Thread-safe queue for pending file events
 * @worker_thread: Background thread that processes events
 * @log_handler: User-provided callback for handling formatted log output
 * @user_data: User data passed to the log handler
 * @is_running: Atomic flag indicating if the logger is currently active
 * @rename_events: Hash table storing unpaired rename events by cookie
 * @last_cleanup_time: Timestamp of last rename event cleanup operation
 *
 * Internal structure representing an event logger instance.
 * All fields are private and should not be accessed directly.
 */
struct EventLogger
{
    GAsyncQueue *event_queue;
    GThread *worker_thread;
    LogHandler log_handler;
    gpointer user_data;
    volatile gboolean is_running;

    // Temporary storage for handling rename events
    GHashTable *rename_events; // key: cookie, value: FileEvent*
};

/**
 * get_timestamp_string:
 *
 * Generates a timestamp string in ISO 8601 format with millisecond precision.
 * The returned string is stored in a static buffer and will be overwritten
 * on subsequent calls. This function is not thread-safe.
 *
 * Returns: A pointer to a static timestamp string
 */
static const gchar *get_timestamp_string(void)
{
    static gchar timestamp[TIMESTAMP_BUFFER_SIZE];
    struct timeval tv;
    struct tm *tm_info;

    if (gettimeofday(&tv, NULL) != 0) {
        g_warning("Failed to get current time, using fallback");
        g_strlcpy(timestamp, "1970-01-01 00:00:00.000", sizeof(timestamp));
        return timestamp;
    }

    tm_info = localtime(&tv.tv_sec);
    if (tm_info == NULL) {
        g_warning("Failed to convert timestamp to local time");
        g_strlcpy(timestamp, "1970-01-01 00:00:00.000", sizeof(timestamp));
        return timestamp;
    }

    snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
             tv.tv_usec / 1000);

    return timestamp;
}

/**
 * escape_csv_field:
 * @field: The original field string to be escaped
 *
 * Escapes a string field for CSV output according to RFC 4180 rules:
 * 1. If the field contains commas, double quotes, or newlines,
 *    the entire field must be surrounded by double quotes.
 * 2. Double quotes within the field must be escaped as two double quotes.
 *
 * Returns: (transfer full) (nullable): A newly allocated escaped string,
 *          or %NULL if no escaping is needed. Use g_free() to release.
 */
static gchar* escape_csv_field(const gchar *field)
{
    g_return_val_if_fail(field != NULL, NULL);

    // Quick check for special characters that require escaping
    if (strpbrk(field, ",\"\n\r") == NULL) {
        return NULL; // No escaping needed
    }

    // Build escaped field with proper quoting
    GString *escaped_field = g_string_sized_new(strlen(field) * 2 + 2);
    g_string_append_c(escaped_field, '"'); // Opening quote

    for (const gchar *p = field; *p; p++) {
        if (*p == '"') {
            g_string_append(escaped_field, "\"\""); // Escape internal quotes
        } else {
            g_string_append_c(escaped_field, *p);
        }
    }

    g_string_append_c(escaped_field, '"'); // Closing quote

    return g_string_free(escaped_field, FALSE);
}

/**
 * validate_file_event:
 * @event: FileEvent to validate
 *
 * Validates that a FileEvent contains all required fields and has
 * reasonable values.
 *
 * Returns: %TRUE if the event is valid, %FALSE otherwise
 */
static gboolean validate_file_event(const FileEvent *event)
{
    if (event == NULL) {
        g_warning("FileEvent is NULL");
        return FALSE;
    }

    if (strlen(event->process_path) == 0) {
        g_warning("FileEvent has invalid process_path");
        return FALSE;
    }

    if (strlen(event->event_path) == 0) {
        g_warning("FileEvent has invalid event_path");
        return FALSE;
    }

    if (event->pid <= 0) {
        g_warning("FileEvent has invalid PID: %d", event->pid);
        return FALSE;
    }

    return TRUE;
}

/**
 * format_single_event_csv:
 * @event: FileEvent to format
 *
 * Formats a single file system event as a CSV line.
 * The output format is: timestamp,process_path,uid,pid,action,event_path
 *
 * Returns: (transfer full): A newly allocated CSV string with trailing newline
 */
static gchar *format_single_event_csv(const FileEvent *event)
{
    const gchar *timestamp = get_timestamp_string();
    g_autofree gchar *escaped_process_path = escape_csv_field(event->process_path);
    g_autofree gchar *escaped_event_path = escape_csv_field(event->event_path);

    gchar *csv_line = g_strdup_printf("%s,%s,%u,%d,%s,%s\n",
                                      timestamp,
                                      escaped_process_path ? escaped_process_path : event->process_path,
                                      event->uid,
                                      event->pid,
                                      event_action_to_string(event->action),
                                      escaped_event_path ? escaped_event_path : event->event_path);
    return csv_line;
}

/**
 * format_rename_event_csv:
 * @from_event: The "rename from" event
 * @to_event: The "rename to" event
 *
 * Formats a rename operation as a CSV line with both source and destination paths.
 * The output format is: timestamp,process_path,uid,pid,action,from_path,to_path
 *
 * Returns: (transfer full): A newly allocated CSV string with trailing newline
 */
static gchar *format_rename_event_csv(const FileEvent *from_event, const FileEvent *to_event)
{
    const gchar *timestamp = get_timestamp_string();
    g_autofree gchar *escaped_process_path = escape_csv_field(from_event->process_path);
    g_autofree gchar *escaped_from_path = escape_csv_field(from_event->event_path);
    g_autofree gchar *escaped_to_path = escape_csv_field(to_event->event_path);

    gchar *csv_line = g_strdup_printf("%s,%s,%u,%d,%s,%s,%s\n",
                                      timestamp,
                                      escaped_process_path ? escaped_process_path : from_event->process_path,
                                      from_event->uid,
                                      from_event->pid,
                                      event_action_to_string(from_event->action),
                                      escaped_from_path ? escaped_from_path : from_event->event_path,
                                      escaped_to_path ? escaped_to_path : to_event->event_path);
    return csv_line;
}

/**
 * handle_rename_event:
 * @logger: EventLogger instance
 * @event: Rename event to process
 *
 * Handles rename events by pairing "from" and "to" events using cookies.
 * Rename operations generate two separate events that must be matched
 * to produce a complete rename log entry.
 */
static void handle_rename_event(EventLogger *logger, FileEvent *event)
{
    g_return_if_fail(logger != NULL);
    g_return_if_fail(event != NULL);

    FileEvent *from_event = g_hash_table_lookup(logger->rename_events, GUINT_TO_POINTER(event->cookie));

    // Note that 'from' event is received before 'to' event
    if (from_event == NULL) {
        // First time encountering this cookie, create new pairing
        if (event->action == ACT_RENAME_FROM_FILE || event->action == ACT_RENAME_FROM_FOLDER) {
            g_hash_table_insert(logger->rename_events, GUINT_TO_POINTER(event->cookie), event);
        } else {
            // If event is a 'to' event, discard it because there's no longer a matching 'from' event
            g_slice_free(FileEvent, event);
        }
    } else {
        // Found pairing, complete rename event handling
        if ((from_event->action == ACT_RENAME_FROM_FILE || from_event->action == ACT_RENAME_FROM_FOLDER) &&
            (event->action == ACT_RENAME_TO_FILE || event->action == ACT_RENAME_TO_FOLDER)) {
            g_autofree gchar *csv_line = format_rename_event_csv(from_event, event);
            logger->log_handler(logger->user_data, csv_line);
        }

        g_hash_table_remove(logger->rename_events, GUINT_TO_POINTER(event->cookie));
        g_slice_free(FileEvent, from_event);
        g_slice_free(FileEvent, event);
    }
}

/**
 * worker_thread_func:
 * @data: EventLogger instance cast to gpointer
 *
 * Main worker thread function that processes events from the queue.
 * This function runs in a separate thread and handles all event processing
 * asynchronously to avoid blocking the main thread.
 *
 * Returns: Always returns %NULL
 */
static gpointer worker_thread_func(gpointer data)
{
    g_return_val_if_fail(data != NULL, NULL);

    EventLogger *logger = (EventLogger *)data;

    g_message("Event logger worker thread started (thread ID: %p)", (void*)g_thread_self());

    while (logger->is_running) {
        // Get event from queue (blocks until event available)
        FileEvent *event = g_async_queue_pop(logger->event_queue);

        if (G_UNLIKELY(event == NULL)) {
            g_warning("Received NULL event from queue, continuing");
            continue;
        }

        if (event->action == ACT_TERMINATE) {
            g_message("Event logger worker thread received termination event");
            g_slice_free(FileEvent, event);
            break;
        }

        // Validate event before processing
        if (!validate_file_event(event)) {
            g_warning("Discarding invalid event");
            g_slice_free(FileEvent, event);
            continue;
        }

        // Process according to event type
        if (event->action == ACT_RENAME_FROM_FILE || event->action == ACT_RENAME_TO_FILE ||
            event->action == ACT_RENAME_FROM_FOLDER || event->action == ACT_RENAME_TO_FOLDER) {
            // Rename events need special handling for pairing
            handle_rename_event(logger, event);
        } else {
            // Regular events are directly formatted and output
            g_autofree gchar *csv_line = format_single_event_csv(event);
            logger->log_handler(logger->user_data, csv_line);
            g_slice_free(FileEvent, event);
        }
    }

    g_message("Event logger worker thread stopped");
    return NULL;
}

/**
 * event_logger_new:
 * @handler: Log handler callback function
 * @user_data: User data to pass to the handler
 *
 * Creates a new EventLogger instance with the specified handler.
 * The logger is initially in a stopped state.
 *
 * Returns: (transfer full): A new EventLogger instance, or %NULL on failure
 */
EventLogger *event_logger_new(LogHandler handler, gpointer user_data)
{
    g_return_val_if_fail(handler != NULL, NULL);

    EventLogger *logger = g_new0(EventLogger, 1);
    if (!logger) {
        g_critical("Failed to allocate memory for EventLogger");
        return NULL;
    }

    logger->event_queue = g_async_queue_new();
    if (!logger->event_queue) {
        g_critical("Failed to create event queue");
        g_free(logger);
        return NULL;
    }

    logger->rename_events = g_hash_table_new(g_direct_hash, g_direct_equal);
    if (!logger->rename_events) {
        g_critical("Failed to create rename events hash table");
        g_async_queue_unref(logger->event_queue);
        g_free(logger);
        return NULL;
    }

    logger->log_handler = handler;
    logger->user_data = user_data;
    logger->is_running = FALSE;
    logger->worker_thread = NULL;

    return logger;
}

/**
 * event_logger_free:
 * @logger: EventLogger instance to free
 *
 * Frees an EventLogger instance and all associated resources.
 * If the logger is currently running, it will be stopped first.
 * This function is safe to call with %NULL.
 */
void event_logger_free(EventLogger *logger)
{
    if (!logger) {
        return;
    }

    // Ensure thread has stopped
    event_logger_stop(logger);

    // Clean up remaining events in queue
    FileEvent *event;
    guint remaining_events = 0;
    while ((event = g_async_queue_try_pop(logger->event_queue)) != NULL) {
        g_slice_free(FileEvent, event);
        remaining_events++;
    }
    if (remaining_events > 0) {
        g_message("Cleaned up %d remaining events from queue", remaining_events);
    }

    // Clean up rename events table
    GHashTableIter iter;
    gpointer key, value;
    guint remaining_rename_events = 0;
    g_hash_table_iter_init(&iter, logger->rename_events);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        g_slice_free(FileEvent, value);
        remaining_rename_events++;
    }
    if (remaining_rename_events > 0) {
        g_message("Cleaned up %d unpaired rename events", remaining_rename_events);
    }
    g_hash_table_destroy(logger->rename_events);

    g_async_queue_unref(logger->event_queue);
    g_free(logger);
}

/**
 * event_logger_start:
 * @logger: EventLogger instance to start
 *
 * Starts the event logger worker thread. Once started, the logger
 * will process events submitted via event_logger_log_event().
 *
 * Returns: %TRUE if the logger was started successfully, %FALSE otherwise
 */
gboolean event_logger_start(EventLogger *logger)
{
    g_return_val_if_fail(logger != NULL, FALSE);

    if (logger->is_running) {
        return FALSE;
    }

    GError *error = NULL;
    logger->worker_thread = g_thread_try_new("event-logger-worker",
                                           worker_thread_func,
                                           logger,
                                           &error);

    if (logger->worker_thread == NULL) {
        logger->is_running = FALSE;
        g_critical("Failed to create worker thread: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        return FALSE;
    }

    logger->is_running = TRUE;
    g_debug("EventLogger started successfully");
    return TRUE;
}

/**
 * event_logger_stop:
 * @logger: EventLogger instance to stop
 *
 * Stops the event logger worker thread and waits for it to complete.
 * Any remaining events in the queue will be processed before stopping.
 * This function is safe to call multiple times or on an already stopped logger.
 */
void event_logger_stop(EventLogger *logger)
{
    g_return_if_fail(logger != NULL);

    if (!logger->is_running) {
        return;
    }

    // Signal the worker thread to stop
    logger->is_running = FALSE;

    // Send termination event to wake up the worker thread
    FileEvent *termination_event = g_slice_new0(FileEvent);
    if (termination_event) {
        termination_event->action = ACT_TERMINATE;
        g_async_queue_push(logger->event_queue, termination_event);
    } else {
        g_critical("Failed to allocate termination event");
    }

    // Wait for worker thread to complete
    if (logger->worker_thread) {
        g_debug("Waiting for worker thread to join...");
        g_thread_join(logger->worker_thread);
        logger->worker_thread = NULL;
    }

    g_debug("EventLogger stopped successfully");
}

/**
 * event_logger_log_event:
 * @logger: EventLogger instance
 * @event: FileEvent to be logged (ownership transferred)
 *
 * Submits a file system event for logging. The event will be processed
 * asynchronously by the worker thread. The logger takes ownership of
 * the event and will free it when processing is complete.
 *
 * The logger must be in a running state for this function to succeed.
 */
void event_logger_log_event(EventLogger *logger, FileEvent *event)
{
    g_return_if_fail(logger != NULL);
    g_return_if_fail(event != NULL);

    if (G_UNLIKELY(!logger->is_running)) {
        g_message("Attempted to log event on stopped logger, discarding event");
        g_slice_free(FileEvent, event);
        return;
    }

    g_async_queue_push(logger->event_queue, event);
}
