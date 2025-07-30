// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "dconfig.h"
#include "datatype.h"
#include <gio/gio.h>
#include <string.h>


/* Configuration constants */
#define DCONFIG_APP_ID "org.deepin.anything"
#define DCONFIG_CONFIG_ID "org.deepin.anything.logger"

/* Default configuration values */
#define LOG_EVENTS_DEFAULT TRUE
#define LOG_EVENTS_TYPE_DEFAULT "file-deleted folder-deleted"
#define LOG_FILE_COUNT_DEFAULT 10
#define LOG_FILE_SIZE_DEFAULT 50
#define PRINT_DEBUG_LOG_DEFAULT FALSE
#define DISABLE_EVENT_MERGE_DEFAULT FALSE

#define LOG_FILE_COUNT_MAX 20
#define LOG_FILE_SIZE_MAX 100

/**
 * Config:
 * @dconfig: DConfig instance
 * @change_handler: Callback function for configuration changes
 * @user_data: User data for the change handler
 * @log_events: Cached value for log_events configuration
 * @log_events_type: Cached value for log_events_type configuration (as bitmask)
 * @log_file_count: Cached value for log_file_count configuration
 * @log_file_size: Cached value for log_file_size configuration
 * @print_debug_log: Cached value for print_debug_log configuration
 * @disable_event_merge: Cached value for disable_event_merge configuration
 * 
 * The configuration manager structure that maintains DConfig instance
 * and cached configuration values.
 * 
 * Thread Safety: This structure is NOT thread-safe. All access should
 * be performed from the main thread context. The change handler callbacks
 * are invoked on the main thread.
 */
struct _Config {
    DConfig *dconfig;
    ConfigChangeHandler change_handler;
    gpointer user_data;

    /* Cached configuration values */
    gboolean log_events;
    guint log_events_type;
    guint log_file_count;
    guint log_file_size;
    gboolean print_debug_log;
    gboolean disable_event_merge;
};

/* Forward declarations */
static guint config_log_events_type_from_string(const GStrv event_types);
static void config_load_all_values(Config *config);
static void config_dconfig_changed_handler(DConfig *dconfig, const gchar *key, gpointer user_data);

/**
 * config_log_events_type_from_string:
 * @event_types: (nullable): Array of event type strings
 * 
 * Converts an array of event type strings to a bitmask.
 * 
 * Returns: Bitmask representing the event types
 */
static guint32
config_log_events_type_from_string(const GStrv event_types)
{
    guint32 log_events_type = 0;
    
    if (event_types == NULL) {
        return log_events_type;
    }
    
    for (guint i = 0; event_types[i] != NULL; i++) {
        guint32 action_mask = event_string_to_action_mask(event_types[i]);
        if (action_mask != G_MAXUINT32) {
            log_events_type |= action_mask;
        } else {
            g_warning("Unknown event type: %s", event_types[i]);
        }
    }
    
    return log_events_type;
}

/**
 * config_load_all_values:
 * @config: Configuration instance
 * 
 * Loads all configuration values from dconfig and caches them.
 */
static void
config_load_all_values(Config *config)
{
    g_return_if_fail(config != NULL);
    
    GError *error = NULL;
    
    /* Load boolean values */
    config->log_events = dconfig_get_boolean(config->dconfig, "log_events", &error);
    if (error != NULL) {
        g_debug("Failed to load log_events: %s, using default value", error->message);
        g_clear_error(&error);
        config->log_events = LOG_EVENTS_DEFAULT;
    }
    
    config->print_debug_log = dconfig_get_boolean(config->dconfig, "print_debug_log", &error);
    if (error != NULL) {
        g_debug("Failed to load print_debug_log: %s, using default value", error->message);
        g_clear_error(&error);
        config->print_debug_log = PRINT_DEBUG_LOG_DEFAULT;
    }
    
    config->disable_event_merge = dconfig_get_boolean(config->dconfig, "disable_event_merge", &error);
    if (error != NULL) {
        g_debug("Failed to load disable_event_merge: %s, using default value", error->message);
        g_clear_error(&error);
        config->disable_event_merge = DISABLE_EVENT_MERGE_DEFAULT;
    }

    /* Load integer values */
    config->log_file_count = dconfig_get_int(config->dconfig, "log_file_count", &error);
    if (error != NULL) {
        g_debug("Failed to load log_file_count: %s, using default value", error->message);
        g_clear_error(&error);
        config->log_file_count = LOG_FILE_COUNT_DEFAULT;
    }
    /* Validate and clamp log_file_count to maximum allowed value */
    if (config->log_file_count > LOG_FILE_COUNT_MAX) {
        g_warning("log_file_count value %u exceeds maximum %u, clamping to maximum", 
                  config->log_file_count, LOG_FILE_COUNT_MAX);
        config->log_file_count = LOG_FILE_COUNT_MAX;
    }
    
    config->log_file_size = dconfig_get_int(config->dconfig, "log_file_size", &error);
    if (error != NULL) {
        g_debug("Failed to load log_file_size: %s, using default value", error->message);
        g_clear_error(&error);
        config->log_file_size = LOG_FILE_SIZE_DEFAULT;
    }
    /* Validate and clamp log_file_size to maximum allowed value */
    if (config->log_file_size > LOG_FILE_SIZE_MAX) {
        g_warning("log_file_size value %u exceeds maximum %u, clamping to maximum", 
                  config->log_file_size, LOG_FILE_SIZE_MAX);
        config->log_file_size = LOG_FILE_SIZE_MAX;
    }

    /* Load string array values */
    g_auto(GStrv) event_types = dconfig_get_string_array(config->dconfig, "log_events_type", &error);
    if (error != NULL) {
        g_debug("Failed to load log_events_type: %s, using default value", error->message);
        g_clear_error(&error);
        event_types = g_strsplit(LOG_EVENTS_TYPE_DEFAULT, " ", 0);
    }
    config->log_events_type = config_log_events_type_from_string(event_types);
    
    /* Log loaded configuration */
    g_message("Configuration loaded successfully:");
    g_message("  log_events: %s", config->log_events ? "true" : "false");
    g_autofree gchar *event_types_str = g_strjoinv(" ", event_types);
    g_message("  log_events_type: 0x%08x, %s", config->log_events_type, event_types_str);
    g_message("  log_file_count: %u", config->log_file_count);
    g_message("  log_file_size: %u", config->log_file_size);
    g_message("  print_debug_log: %s", config->print_debug_log ? "true" : "false");
    g_message("  disable_event_merge: %s", config->disable_event_merge ? "true" : "false");
}

static void
config_dconfig_changed_handler(DConfig *dconfig, const gchar *key, gpointer user_data)
{
    Config *config = (Config *)user_data;
    GError *error = NULL;
    
    g_return_if_fail(dconfig != NULL);
    g_return_if_fail(config != NULL);
    g_return_if_fail(key != NULL);
    
    g_debug("Configuration changed: %s", key);
    
    /* Update the cached value for the changed key */
    if (g_strcmp0(key, "log_events") == 0) {
        config->log_events = dconfig_get_boolean(config->dconfig, key, &error);
        if (error == NULL) {
            g_message("log_events changed to: %s", config->log_events ? "true" : "false");
        } else {
            g_warning("Failed to reload log_events: %s, keeping previous value", error->message);
            g_clear_error(&error);
            return;
        }
    } else if (g_strcmp0(key, "log_events_type") == 0) {
        g_auto(GStrv) event_types = dconfig_get_string_array(config->dconfig, key, &error);
        if (error == NULL && event_types != NULL) {
            config->log_events_type = config_log_events_type_from_string(event_types);
            g_autofree gchar *event_types_str = g_strjoinv(" ", event_types);
            g_message("log_events_type changed to: %s (0x%08x)", event_types_str, config->log_events_type);
        } else {
            g_warning("Failed to reload log_events_type: %s, keeping previous value", 
                      error ? error->message : "Unknown error");
            g_clear_error(&error);
            return;
        }
    } else if (g_strcmp0(key, "log_file_count") == 0) {
        guint new_count = dconfig_get_int(config->dconfig, key, &error);
        if (error == NULL) {
            /* Validate and clamp the new value */
            if (new_count > LOG_FILE_COUNT_MAX) {
                g_warning("log_file_count value %u exceeds maximum %u, clamping to maximum", 
                          new_count, LOG_FILE_COUNT_MAX);
                new_count = LOG_FILE_COUNT_MAX;
            }
            if (config->log_file_count == new_count)
                return;
            config->log_file_count = new_count;
            g_message("log_file_count changed to: %u", config->log_file_count);
        } else {
            g_warning("Failed to reload log_file_count: %s, keeping previous value", error->message);
            g_clear_error(&error);
            return;
        }
    } else if (g_strcmp0(key, "log_file_size") == 0) {
        guint new_size = dconfig_get_int(config->dconfig, key, &error);
        if (error == NULL) {
            /* Validate and clamp the new value */
            if (new_size > LOG_FILE_SIZE_MAX) {
                g_warning("log_file_size value %u exceeds maximum %u, clamping to maximum", 
                          new_size, LOG_FILE_SIZE_MAX);
                new_size = LOG_FILE_SIZE_MAX;
            }
            if (config->log_file_size == new_size)
                return;
            config->log_file_size = new_size;
            g_message("log_file_size changed to: %u", config->log_file_size);
        } else {
            g_warning("Failed to reload log_file_size: %s, keeping previous value", error->message);
            g_clear_error(&error);
            return;
        }
    } else if (g_strcmp0(key, "print_debug_log") == 0) {
        config->print_debug_log = dconfig_get_boolean(config->dconfig, key, &error);
        if (error == NULL) {
            g_message("print_debug_log changed to: %s", config->print_debug_log ? "true" : "false");
        } else {
            g_warning("Failed to reload print_debug_log: %s, keeping previous value", error->message);
            g_clear_error(&error);
            return;
        }
    } else if (g_strcmp0(key, "disable_event_merge") == 0) {
        config->disable_event_merge = dconfig_get_boolean(config->dconfig, key, &error);
        if (error == NULL) {
            g_message("disable_event_merge changed to: %s", config->disable_event_merge ? "true" : "false");
        } else {
            g_warning("Failed to reload disable_event_merge: %s, keeping previous value", error->message);
            g_clear_error(&error);
            return;
        }
    } else {
        g_warning("Unknown configuration key changed: %s", key);
        return;
    }
    
    /* Notify change handler if set */
    if (config->change_handler != NULL) {
        config->change_handler(config, key, config->user_data);
    }
}

/* Public API implementations */

/**
 * config_new:
 * @error: Return location for error
 * 
 * Creates a new configuration manager instance.
 * 
 * Returns: (transfer full) (nullable): A new Config instance, or %NULL on error
 */
Config *
config_new(GError **error)
{
    Config *config = NULL;
    
    g_debug("Creating new configuration manager");
    
    config = g_new0(Config, 1);
    config->dconfig = dconfig_new(DCONFIG_APP_ID, DCONFIG_CONFIG_ID, error);
    if (config->dconfig == NULL) {
        g_free(config);
        g_debug("Failed to create dconfig instance");
        return NULL;
    }
    dconfig_set_change_handler(config->dconfig, config_dconfig_changed_handler, config);
    
    /* Load initial configuration values */
    config_load_all_values(config);
    
    g_debug("Configuration manager created successfully");
    return config;
}

/**
 * config_free:
 * @config: A Config instance
 * 
 * Frees the configuration manager and releases all associated resources.
 */
void
config_free(Config *config)
{
    if (config == NULL) {
        return;
    }
    
    g_debug("Freeing configuration manager");
    
    /* Free resources */
    g_clear_pointer(&config->dconfig, dconfig_free);
    g_free(config);
    
    g_debug("Configuration manager freed");
}

/**
 * config_get_boolean:
 * @config: A Config instance
 * @key: Configuration key name
 * 
 * Gets a boolean configuration value.
 * 
 * Returns: The configuration value or default_value on error
 */
gboolean
config_get_boolean(Config *config, const gchar *key)
{
    g_return_val_if_fail(config != NULL, FALSE);
    g_return_val_if_fail(key != NULL, FALSE);
    
    if (!dconfig_is_valid(config->dconfig)) {
        g_warning("DConfig instance is invalid");
        return FALSE;
    }
    
    /* Return cached values for known keys */
    if (g_strcmp0(key, "log_events") == 0) {
        return config->log_events;
    } else if (g_strcmp0(key, "print_debug_log") == 0) {
        return config->print_debug_log;
    } else if (g_strcmp0(key, "disable_event_merge") == 0) {
        return config->disable_event_merge;
    } else {
        g_warning("Unknown boolean configuration key: %s", key);
        return FALSE;
    }
}

/**
 * config_get_uint:
 * @config: A Config instance
 * @key: Configuration key name
 * 
 * Gets an unsigned integer configuration value.
 * 
 * Returns: The configuration value
 */
guint
config_get_uint(Config *config, const gchar *key)
{
    g_return_val_if_fail(config != NULL, 0);
    g_return_val_if_fail(key != NULL, 0);
    
    if (!dconfig_is_valid(config->dconfig)) {
        g_warning("DConfig instance is invalid");
        return 0;
    }
    
    /* Return cached values for known keys */
    if (g_strcmp0(key, "log_events_type") == 0) {
        return config->log_events_type;
    } else if (g_strcmp0(key, "log_file_count") == 0) {
        return config->log_file_count;
    } else if (g_strcmp0(key, "log_file_size") == 0) {
        return config->log_file_size;
    } else {
        g_warning("Unknown uint configuration key: %s", key);
        return 0;
    }
}

/**
 * config_set_change_handler:
 * @config: A Config instance
 * @handler: Callback function for configuration changes
 * @user_data: User data to pass to the handler
 * 
 * Sets a callback function to be called when configuration values change.
 */
void
config_set_change_handler(Config *config, ConfigChangeHandler handler, gpointer user_data)
{
    g_return_if_fail(config != NULL);
    
    config->change_handler = handler;
    config->user_data = user_data;
    
    g_debug("Configuration change handler %s", handler ? "set" : "cleared");
}
