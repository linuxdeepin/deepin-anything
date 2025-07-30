// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DCONFIG_H
#define DCONFIG_H

#define G_LOG_USE_STRUCTURED
#include <glib.h>

G_BEGIN_DECLS

/**
 * DConfig:
 * 
 * An opaque structure representing the configuration manager for the logger service.
 * This structure manages D-Bus connections to the dconfig system and handles
 * real-time configuration updates.
 * 
 * Since: 1.0
 * Stability: Stable
 */
typedef struct _DConfig DConfig;

/**
 * DConfigError:
 * @DCONFIG_ERROR_DBUS_CONNECTION: Failed to connect to D-Bus
 * @DCONFIG_ERROR_RESOURCE_PATH: Failed to get dconfig resource path
 * @DCONFIG_ERROR_INVALID_KEY: Invalid configuration key
 * @DCONFIG_ERROR_TYPE_MISMATCH: Configuration value type mismatch
 * @DCONFIG_ERROR_DCONFIG_SERVICE: dconfig service error
 * 
 * Error codes for configuration operations.
 * 
 * Since: 1.0
 */
typedef enum {
    DCONFIG_ERROR_DBUS_CONNECTION,
    DCONFIG_ERROR_RESOURCE_PATH,
    DCONFIG_ERROR_INVALID_KEY,
    DCONFIG_ERROR_TYPE_MISMATCH,
    DCONFIG_ERROR_DCONFIG_SERVICE
} DConfigError;

#define DCONFIG_ERROR (dconfig_error_quark())

/**
 * dconfig_error_quark:
 * 
 * Gets the error domain for DConfig errors.
 * 
 * Returns: the error domain quark for DConfig errors
 * 
 * Since: 1.0
 */
GQuark dconfig_error_quark(void);

/**
 * DConfigChangeHandler:
 * @dconfig: The configuration instance
 * @key: The configuration key that changed
 * @user_data: User data passed to the handler
 * 
 * Callback function type for handling configuration changes.
 * This callback is invoked whenever a configuration value changes
 * in the dconfig system.
 * 
 * Note: The callback should be lightweight and not block, as it
 * runs on the main thread.
 * 
 * Since: 1.0
 */
typedef void (*DConfigChangeHandler)(DConfig *dconfig, const gchar *key, gpointer user_data);

/**
 * dconfig_new:
 * @app_id: The application ID for dconfig
 * @config_id: The configuration ID for dconfig
 * @error: (nullable): Return location for error, or %NULL
 * 
 * Creates a new DConfig instance and establishes connection to the dconfig service.
 * 
 * The @app_id and @config_id parameters must match those used in the dconfig
 * configuration schema files.
 * 
 * Returns: (transfer full) (nullable): A new DConfig instance, or %NULL on error
 * 
 * Since: 1.0
 */
DConfig *dconfig_new(const gchar *app_id, const gchar *config_id, GError **error);

/**
 * dconfig_free:
 * @dconfig: (nullable): A DConfig instance
 * 
 * Frees the DConfig instance and releases all associated resources.
 * This function is safe to call with %NULL.
 * 
 * Since: 1.0
 */
void dconfig_free(DConfig *dconfig);

/**
 * dconfig_get_boolean:
 * @dconfig: A DConfig instance
 * @key: Configuration key name
 * @error: (nullable): Return location for error, or %NULL
 * 
 * Gets a boolean configuration value from the dconfig system.
 * 
 * Returns: The configuration value, or %FALSE on error
 * 
 * Since: 1.0
 */
gboolean dconfig_get_boolean(DConfig *dconfig, const gchar *key, GError **error);

/**
 * dconfig_get_int:
 * @dconfig: A DConfig instance
 * @key: Configuration key name
 * @error: (nullable): Return location for error, or %NULL
 * 
 * Gets an integer configuration value from the dconfig system.
 * The function can handle int32, int64, and double types from dconfig,
 * converting them to int with appropriate range checking.
 * 
 * Returns: The configuration value, or 0 on error
 * 
 * Since: 1.0
 */
gint dconfig_get_int(DConfig *dconfig, const gchar *key, GError **error);

/**
 * dconfig_get_string_array:
 * @dconfig: A DConfig instance
 * @key: Configuration key name
 * @error: (nullable): Return location for error, or %NULL
 * 
 * Gets a string array configuration value from the dconfig system.
 * 
 * Returns: (transfer full) (nullable): The configuration value as a %NULL-terminated
 *          string array, or %NULL on error. Free with g_strfreev() when no longer needed.
 * 
 * Since: 1.0
 */
gchar **dconfig_get_string_array(DConfig *dconfig, const gchar *key, GError **error);

/**
 * dconfig_set_change_handler:
 * @dconfig: A DConfig instance
 * @handler: (nullable): Callback function for configuration changes, or %NULL to remove
 * @user_data: (nullable): User data to pass to the handler
 * 
 * Sets a callback function to be called when configuration values change.
 * Only one handler can be set at a time; setting a new handler replaces
 * the previous one.
 * 
 * Since: 1.0
 */
void dconfig_set_change_handler(DConfig *dconfig, DConfigChangeHandler handler, gpointer user_data);

/**
 * dconfig_is_valid:
 * @dconfig: (nullable): A DConfig instance
 * 
 * Checks if the configuration instance is valid and connected to the dconfig service.
 * 
 * Returns: %TRUE if the configuration is valid and connected, %FALSE otherwise
 * 
 * Since: 1.0
 */
gboolean dconfig_is_valid(DConfig *dconfig);

G_END_DECLS

#endif // DCONFIG_H