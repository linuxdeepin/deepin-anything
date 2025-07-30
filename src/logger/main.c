// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#define G_LOG_USE_STRUCTURED
#include <glib-unix.h>

#include "event_listener.h"
#include "event_logger.h"
#include "file_log.h"
#include "config.h"
#include "log.h"

#define EVENT_LOG_FILE "/var/log/deepin/deepin-anything-logger/events.csv"

static GMainLoop *loop = NULL;
static gboolean do_restart = FALSE;

// Signal handler to stop the main loop
static gboolean on_signal(G_GNUC_UNUSED gpointer user_data)
{
    g_message("Received signal, initiating graceful shutdown");
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

/**
 * @brief Get event listening type mask from configuration
 * 
 * Calculate the final event listening mask based on config items log_events 
 * and log_events_type. If log_events is false, return 0 (no events monitored).
 * 
 * @param config Configuration manager instance
 * @return Event listening mask, 0 means no events are monitored
 */
static gint get_log_events_type(Config *config)
{
    guint log_events_type = config_get_uint(config, "log_events_type");
    gboolean log_events = config_get_boolean(config, "log_events");
    return log_events ? log_events_type : 0;
}

/**
 * @brief Configuration change event handler function
 * 
 * This function is called when configuration items change to handle 
 * corresponding configuration changes. Supports dynamic adjustment of:
 * - print_debug_log: Enable/disable debug logging
 * - log_events/log_events_type: Update event listening mask
 * 
 * @param config Configuration manager instance
 * @param key Name of the configuration item that changed
 * @param user_data User data, EventListener instance here
 */
static void config_change_handler(Config *config, const gchar *key, gpointer user_data)
{
    if (g_strcmp0(key, "print_debug_log") == 0) {
        enable_debug_log(config_get_boolean(config, "print_debug_log"));
    } else if (g_strcmp0(key, "log_events_type") == 0 || g_strcmp0(key, "log_events") == 0) {
        EventListener *listener = (EventListener *)user_data;
        event_listener_set_event_mask(listener, get_log_events_type(config));
    } else if (g_strcmp0(key, "disable_event_merge") == 0) {
        EventListener *listener = (EventListener *)user_data;
        event_listener_set_disable_event_merge(listener, config_get_boolean(config, "disable_event_merge"));
    }
}

gboolean check_kernel_module_available(G_GNUC_UNUSED gpointer user_data)
{
    if (is_kernel_module_available()) {
        g_message("Kernel module available, quit");
        g_main_loop_quit(loop);
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

gboolean check_kernel_module_reload(G_GNUC_UNUSED gpointer user_data)
{
    if (is_kernel_module_reload()) {
        g_message("Kernel module reload, quit");
        do_restart = TRUE;
        g_main_loop_quit(loop);
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

/**
 * @brief deepin-anything-logger main program entry point
 * 
 * This is a system service program responsible for monitoring filesystem 
 * events and recording them to log files. The program must run with root 
 * privileges and operates as a systemd service.
 * 
 * Main functions:
 * 1. Initialize configuration management system
 * 2. Create file logger and event logger
 * 3. Start event listener to monitor kernel filesystem events
 * 4. Handle events and configuration changes in main event loop
 * 5. Respond to system signals and gracefully shutdown service
 * 
 * @param argc Number of command line arguments (unused)
 * @param argv Command line argument array (unused)
 * @return Program exit code: 0 for success, non-zero for failure
 */
int main(G_GNUC_UNUSED int argc, G_GNUC_UNUSED char *argv[])
{
    Config *config = NULL;
    EventListener *listener = NULL;
    FileLogger *file_logger = NULL;
    EventLogger *event_logger = NULL;
    int ret = 0;

    init_log();

    g_message("deepin-anything-logger started.");

    // Expect root user
    if (geteuid() != 0) {
        g_critical("deepin-anything-logger must be run as root user.");
        goto quit;
    }

    // Prepare the main loop
    loop = g_main_loop_new(NULL, FALSE);
    if (loop == NULL) {
        g_critical("Failed to initialize main event loop");
        goto quit;
    }
    
    g_unix_signal_add(SIGINT, on_signal, NULL);
    g_unix_signal_add(SIGTERM, on_signal, NULL);

    // Wait kernel module available
    if (!is_kernel_module_available()) {
        g_message("Waiting kernel module available...");
        g_timeout_add(1000, check_kernel_module_available, NULL);
        g_main_loop_run(loop);
        if (!is_kernel_module_available()) {
            g_critical("Failed to wait kernel module available.");
            goto quit;
        }
    }

    // Initialize config
    GError *error = NULL;
    config = config_new(&error);
    if (config == NULL) {
        g_critical("Failed to initialize config: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        goto quit;
    }
    enable_debug_log(config_get_boolean(config, "print_debug_log"));
    g_debug("debug log is enabled");

    // Create file log
    file_logger = file_logger_new(EVENT_LOG_FILE,
                                  config_get_uint(config, "log_file_size") * 1024 * 1024,
                                  config_get_uint(config, "log_file_count"));
    if (file_logger == NULL) {
        g_critical("Failed to initialize file logger.");
        goto quit;
    }

    // Prepare event logger
    event_logger = event_logger_new((LogHandler)file_logger_log, file_logger);
    if (event_logger == NULL) {
        g_critical("Failed to initialize event logger.");
        goto quit;
    }
    if (!event_logger_start(event_logger)) {
        g_critical("Failed to start event logger.");
        goto quit;
    }

    // Prepare event listener
    listener = event_listener_new((FileEventHandler)event_logger_log_event, event_logger);
    if (listener == NULL) {
        g_critical("Failed to initialize event listener.");
        goto quit;
    }
    if (!event_listener_set_event_mask(listener, get_log_events_type(config))) {
        g_critical("Failed to set event mask.");
        goto quit;
    }
    if (!event_listener_set_disable_event_merge(listener, config_get_boolean(config, "disable_event_merge"))) {
        g_critical("Failed to set disable event merge.");
        goto quit;
    }
    config_set_change_handler(config, config_change_handler, listener);

    // Run the main loop
    g_timeout_add(3000, check_kernel_module_reload, NULL);
    if (event_listener_start(listener)) {
        g_message("Service running...");
        g_main_loop_run(loop);
        ret = do_restart ? 1 : 0;
        g_message("Service stopping...");
    } else {
        g_critical("Failed to start event listener.");
    }

quit:
    // Cleanup
    event_listener_free(listener);
    event_logger_free(event_logger);
    file_logger_free(file_logger);
    config_free(config);
    if (loop) {
        g_main_loop_unref(loop);
    }

    g_message("deepin-anything-logger shutdown complete with exit code: %d", ret);
    return ret;
}
