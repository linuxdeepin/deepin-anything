// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file config.h
 * @brief Configuration management interface for the deepin-anything logger service
 * 
 * This module provides a configuration management system that integrates with
 * the dconfig service to handle runtime configuration changes. It maintains
 * cached configuration values for performance and provides change notification
 * callbacks for dynamic configuration updates.
 */

#ifndef CONFIG_H
#define CONFIG_H

#define G_LOG_USE_STRUCTURED
#include <glib.h>

G_BEGIN_DECLS

/**
 * Config:
 * 
 * An opaque structure representing the configuration manager for the logger service.
 * This structure manages connections to the dconfig system and handles
 * real-time configuration updates with caching for improved performance.
 * 
 * The configuration manager handles the following configuration keys:
 * - log_events: Whether to log file system events (boolean)
 * - log_events_type: Types of events to log (string array converted to bitmask)
 * - log_file_count: Maximum number of log files to keep (unsigned integer)
 * - log_file_size: Maximum size of each log file in MB (unsigned integer)  
 * - print_debug_log: Whether to print debug messages (boolean)
 */
typedef struct _Config Config;

/**
 * ConfigChangeHandler:
 * @config: The configuration instance that changed
 * @key: The configuration key that was modified
 * @user_data: User data passed when setting up the handler
 * 
 * Callback function type for handling configuration changes.
 * This callback is invoked whenever a configuration value changes
 * through the dconfig system, allowing applications to respond
 * dynamically to configuration updates.
 * 
 * The callback is called after the internal cached values have
 * been updated, so calling config_get_* functions will return
 * the new values.
 */
typedef void (*ConfigChangeHandler)(Config *config, const gchar *key, gpointer user_data);

/**
 * config_new:
 * @error: (out) (optional): Return location for error information
 * 
 * Creates a new configuration manager instance and connects to the dconfig service.
 * This function initializes the configuration system, loads all configuration
 * values into cache, and sets up change monitoring.
 * 
 * Returns: (transfer full) (nullable): A new Config instance, or %NULL on error.
 *          Use config_free() to release the returned instance.
 */
Config *config_new(GError **error);

/**
 * config_free:
 * @config: (nullable): A Config instance to free
 * 
 * Frees the configuration manager and releases all associated resources.
 * This function safely disconnects from the dconfig service and cleans up
 * all internal state. It is safe to call with %NULL.
 * 
 * After calling this function, the Config instance should not be used.
 */
void config_free(Config *config);

/**
 * config_get_boolean:
 * @config: A valid Config instance
 * @key: Configuration key name (must be a known boolean key)
 * 
 * Gets a boolean configuration value from the cached configuration.
 * This function provides fast access to boolean configuration values
 * without requiring D-Bus communication.
 * 
 * Supported keys:
 * - "log_events": Whether to enable event logging
 * - "print_debug_log": Whether to print debug messages
 * 
 * Returns: The cached configuration value, or %FALSE if the key is unknown
 *          or the config instance is invalid.
 */
gboolean config_get_boolean(Config *config, const gchar *key);

/**
 * config_get_uint:
 * @config: A valid Config instance  
 * @key: Configuration key name (must be a known unsigned integer key)
 * 
 * Gets an unsigned integer configuration value from the cached configuration.
 * This function provides fast access to numeric configuration values
 * without requiring D-Bus communication.
 * 
 * Supported keys:
 * - "log_events_type": Bitmask of event types to log
 * - "log_file_count": Maximum number of log files
 * - "log_file_size": Maximum log file size in MB
 * 
 * Returns: The cached configuration value, or 0 if the key is unknown
 *          or the config instance is invalid.
 */
guint config_get_uint(Config *config, const gchar *key);

/**
 * config_set_change_handler:
 * @config: A valid Config instance
 * @handler: (nullable): Callback function for configuration changes, or %NULL to clear
 * @user_data: (nullable): User data to pass to the handler
 * 
 * Sets a callback function to be invoked when configuration values change.
 * Only one change handler can be active at a time; setting a new handler
 * replaces any previously set handler.
 * 
 * The handler will be called on the main thread context whenever the dconfig
 * service notifies of configuration changes. The handler receives the
 * configuration instance, the key that changed, and the provided user data.
 */
void config_set_change_handler(Config *config, ConfigChangeHandler handler, gpointer user_data);

G_END_DECLS

#endif /* CONFIG_H */