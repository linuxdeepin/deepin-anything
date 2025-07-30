// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dconfig.h"
#include "datatype.h"
#include <gio/gio.h>
#include <string.h>

/**
 * SECTION:dconfig
 * @title: DConfig Manager
 * @short_description: Manages dconfig-based configuration for the logger service
 * @include: dconfig.h
 * 
 * The DConfig module provides a high-level interface for managing configuration
 * values through the dconfig system. It supports real-time configuration updates
 * and provides type-safe accessors for different configuration value types.
 */

/* Configuration constants */
#define DCONFIG_SERVICE "org.desktopspec.ConfigManager"
#define DCONFIG_PATH "/"
#define DCONFIG_INTERFACE "org.desktopspec.ConfigManager"
#define DCONFIG_MANAGER_INTERFACE "org.desktopspec.ConfigManager.Manager"

/* Timeout for D-Bus operations (in milliseconds) */
#define DBUS_CALL_TIMEOUT_MS 1000

/**
 * DConfig:
 * @dbus_connection: Connection to the system D-Bus
 * @resource_path: D-Bus object path for the dconfig resource
 * @subscription_id: Signal subscription ID for configuration changes
 * @change_handler: User-provided callback for configuration changes
 * @user_data: User data passed to the change handler
 * @is_valid: Whether the configuration instance is properly initialized
 * 
 * The configuration manager structure that maintains D-Bus connections
 * and cached configuration values.
 */
struct _DConfig {
    GDBusConnection *dbus_connection;
    gchar *resource_path;
    guint subscription_id;
    DConfigChangeHandler change_handler;
    gpointer user_data;
    gboolean is_valid;
    gchar *app_id;
    gchar *config_id;
};

/* Forward declarations */
static gchar *dconfig_get_dconfig_resource_path(DConfig *dconfig, GError **error);
static GVariant *dconfig_get_dconfig_value(DConfig *dconfig, 
                                           const gchar *key,
                                           GError **error);
static void dconfig_changed_signal(GDBusConnection *connection,
                                   const gchar *sender_name,
                                   const gchar *object_path,
                                   const gchar *interface_name,
                                   const gchar *signal_name,
                                   GVariant *parameters,
                                   gpointer user_data);

/**
 * dconfig_error_quark:
 * 
 * Gets the error quark for configuration errors.
 * 
 * Returns: The error quark for DCONFIG_ERROR
 */
GQuark
dconfig_error_quark(void)
{
    return g_quark_from_static_string("dconfig-error-quark");
}

/**
 * dconfig_get_dconfig_resource_path:
 * @dconfig: DConfig instance
 * @error: Return location for error
 * 
 * Gets the D-Bus object path for the dconfig resource.
 * 
 * Returns: (transfer full) (nullable): The resource path, or %NULL on error
 */
static gchar *
dconfig_get_dconfig_resource_path(DConfig *dconfig, GError **error)
{
    g_return_val_if_fail(dconfig != NULL, NULL);
    g_return_val_if_fail(dconfig->dbus_connection != NULL, NULL);
    g_return_val_if_fail(dconfig->app_id != NULL && *dconfig->app_id != '\0', NULL);
    g_return_val_if_fail(dconfig->config_id != NULL && *dconfig->config_id != '\0', NULL);
    
    GVariant *result = NULL;
    gchar *resource_path = NULL;
    
    g_debug("Acquiring dconfig manager for app_id=%s, config_id=%s", 
            dconfig->app_id, dconfig->config_id);
    
    result = g_dbus_connection_call_sync(
        dconfig->dbus_connection,
        DCONFIG_SERVICE,
        DCONFIG_PATH,
        DCONFIG_INTERFACE,
        "acquireManager",
        g_variant_new("(sss)", dconfig->app_id, dconfig->config_id, ""),
        G_VARIANT_TYPE("(o)"),
        G_DBUS_CALL_FLAGS_NONE,
        DBUS_CALL_TIMEOUT_MS,
        NULL,
        error
    );
    
    if (result == NULL) {
        g_debug("Failed to acquire dconfig manager: %s", 
                error && *error ? (*error)->message : "Unknown error");
        return NULL;
    }
    
    g_variant_get(result, "(o)", &resource_path);
    g_variant_unref(result);
    
    g_debug("Acquired dconfig resource path: %s", resource_path);
    return resource_path;
}

/**
 * dconfig_get_dconfig_value:
 * @dconfig: DConfig instance
 * @key: Configuration key
 * @error: Return location for error
 * 
 * Gets a configuration value from dconfig.
 * 
 * Returns: (transfer full) (nullable): The configuration value, or %NULL on error
 */
static GVariant *
dconfig_get_dconfig_value(DConfig *dconfig, 
                          const gchar *key,
                          GError **error)
{
    g_return_val_if_fail(dconfig != NULL, NULL);
    g_return_val_if_fail(key != NULL && *key != '\0', NULL);
    g_return_val_if_fail(dconfig->dbus_connection != NULL, NULL);
    g_return_val_if_fail(dconfig->resource_path != NULL, NULL);
    
    GVariant *result = NULL;
    
    g_debug("Getting config value for key: %s", key);
    
    result = g_dbus_connection_call_sync(
        dconfig->dbus_connection,
        DCONFIG_SERVICE,
        dconfig->resource_path,
        DCONFIG_MANAGER_INTERFACE,
        "value",
        g_variant_new("(s)", key),
        G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE,
        DBUS_CALL_TIMEOUT_MS,
        NULL,
        error
    );
    
    if (result == NULL) {
        g_debug("Failed to get config value for key '%s': %s", 
                key, error && *error ? (*error)->message : "Unknown error");
        return NULL;
    }
    
    g_debug("Successfully retrieved config value for key: %s", key);
    return result;
}

/**
 * dconfig_changed_signal:
 * @connection: D-Bus connection
 * @sender_name: Sender name
 * @object_path: Object path
 * @interface_name: Interface name
 * @signal_name: Signal name
 * @parameters: Signal parameters
 * @user_data: User data (DConfig instance)
 * 
 * Signal handler for configuration changes from dconfig.
 */
static void
dconfig_changed_signal(G_GNUC_UNUSED GDBusConnection *connection,
                       G_GNUC_UNUSED const gchar *sender_name,
                       G_GNUC_UNUSED const gchar *object_path,
                       G_GNUC_UNUSED const gchar *interface_name,
                       G_GNUC_UNUSED const gchar *signal_name,
                       GVariant *parameters,
                       gpointer user_data)
{
    DConfig *dconfig = (DConfig *)user_data;
    g_autofree gchar *key = NULL;
    
    g_return_if_fail(dconfig != NULL);
    g_return_if_fail(dconfig->is_valid);
    g_return_if_fail(parameters != NULL);
    
    /* Validate parameter format */
    if (!g_variant_is_of_type(parameters, G_VARIANT_TYPE("(s)"))) {
        g_warning("Received configuration change signal with invalid parameter type: %s",
                  g_variant_get_type_string(parameters));
        return;
    }
    
    g_variant_get(parameters, "(s)", &key);
    if (key == NULL || *key == '\0') {
        g_warning("Received configuration change signal with NULL or empty key");
        return;
    }
    
    if (dconfig->change_handler != NULL) {
        /* Call user handler in a safe manner */
        dconfig->change_handler(dconfig, key, dconfig->user_data);
    } else {
        g_debug("No change handler set, ignoring configuration change for key: %s", key);
    }
}

/* Public API implementations */

/**
 * dconfig_new:
 * @app_id: The application ID for dconfig
 * @config_id: The configuration ID for dconfig  
 * @error: Return location for error
 * 
 * Creates a new configuration manager instance and establishes connection to dconfig.
 * 
 * Returns: (transfer full) (nullable): A new DConfig instance, or %NULL on error
 */
DConfig *
dconfig_new(const gchar *app_id, const gchar *config_id, GError **error)
{
    DConfig *dconfig = NULL;
    GError *local_error = NULL;
    
    g_return_val_if_fail(app_id != NULL && *app_id != '\0', NULL);
    g_return_val_if_fail(config_id != NULL && *config_id != '\0', NULL);
    
    g_debug("Creating new DConfig instance for app_id='%s', config_id='%s'", 
            app_id, config_id);
    
    dconfig = g_new0(DConfig, 1);
    dconfig->is_valid = FALSE;
    dconfig->subscription_id = 0;
    dconfig->change_handler = NULL;
    dconfig->user_data = NULL;
    dconfig->app_id = g_strdup(app_id);
    dconfig->config_id = g_strdup(config_id);

    /* Validate that app_id and config_id were successfully allocated */
    if (dconfig->app_id == NULL || dconfig->config_id == NULL) {
        g_set_error(error, DCONFIG_ERROR, DCONFIG_ERROR_DBUS_CONNECTION,
                   "Failed to allocate memory for app_id or config_id");
        dconfig_free(dconfig);
        return NULL;
    }

    /* Connect to system D-Bus */
    g_debug("Connecting to system D-Bus");
    dconfig->dbus_connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &local_error);
    if (dconfig->dbus_connection == NULL) {
        g_set_error(error, DCONFIG_ERROR, DCONFIG_ERROR_DBUS_CONNECTION,
                   "Failed to connect to system bus: %s", 
                   local_error ? local_error->message : "Unknown error");
        g_clear_error(&local_error);
        dconfig_free(dconfig);
        return NULL;
    }
    
    /* Get dconfig resource path */
    g_debug("Acquiring dconfig resource path");
    dconfig->resource_path = dconfig_get_dconfig_resource_path(dconfig, &local_error);
    if (dconfig->resource_path == NULL) {
        g_set_error(error, DCONFIG_ERROR, DCONFIG_ERROR_RESOURCE_PATH,
                   "Failed to get dconfig resource path: %s",
                   local_error ? local_error->message : "Unknown error");
        g_clear_error(&local_error);
        dconfig_free(dconfig);
        return NULL;
    }
    
    /* Subscribe to configuration changes */
    g_debug("Subscribing to configuration change signals");
    dconfig->subscription_id = g_dbus_connection_signal_subscribe(
        dconfig->dbus_connection,
        DCONFIG_SERVICE,
        DCONFIG_MANAGER_INTERFACE,
        "valueChanged",
        dconfig->resource_path,
        NULL,
        G_DBUS_SIGNAL_FLAGS_NONE,
        dconfig_changed_signal,
        dconfig,
        NULL
    );
    if (dconfig->subscription_id == 0) {
        g_set_error(error, DCONFIG_ERROR, DCONFIG_ERROR_DBUS_CONNECTION,
                   "Failed to subscribe to configuration change signals");
        dconfig_free(dconfig);
        return NULL;
    }

    dconfig->is_valid = TRUE;
    g_debug("DConfig instance created successfully");
    return dconfig;
}

/**
 * dconfig_free:
 * @dconfig: A DConfig instance
 * 
 * Frees the configuration manager and releases all associated resources.
 */
void
dconfig_free(DConfig *dconfig)
{
    if (dconfig == NULL) {
        return;
    }
    
    /* Unsubscribe from signals */
    if (dconfig->subscription_id > 0 && dconfig->dbus_connection != NULL) {
        g_dbus_connection_signal_unsubscribe(dconfig->dbus_connection, dconfig->subscription_id);
        dconfig->subscription_id = 0;
    }
    
    /* Free resources */
    g_clear_object(&dconfig->dbus_connection);
    g_clear_pointer(&dconfig->resource_path, g_free);
    g_clear_pointer(&dconfig->app_id, g_free);
    g_clear_pointer(&dconfig->config_id, g_free);

    dconfig->is_valid = FALSE;
    g_free(dconfig);
}

/**
 * dconfig_get_boolean:
 * @dconfig: A DConfig instance
 * @key: Configuration key name
 * @error: Return location for error
 * 
 * Gets a boolean configuration value.
 * 
 * Returns: The configuration value or %FALSE on error
 */
gboolean
dconfig_get_boolean(DConfig *dconfig, const gchar *key, GError **error)
{
    g_return_val_if_fail(dconfig != NULL, FALSE);
    g_return_val_if_fail(key != NULL, FALSE);
    
    if (!dconfig->is_valid) {
        g_set_error(error, DCONFIG_ERROR, DCONFIG_ERROR_DBUS_CONNECTION,
                   "Configuration manager is not valid");
        return FALSE;
    }
    
    GVariant *result = dconfig_get_dconfig_value(dconfig, key, error);
    if (result == NULL) {
        return FALSE;
    }
    
    GVariant *value = NULL;
    g_variant_get(result, "(v)", &value);
    
    gboolean ret_value = FALSE;
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)) {
        ret_value = g_variant_get_boolean(value);
        g_debug("Retrieved boolean value %s for key '%s'", ret_value ? "TRUE" : "FALSE", key);
    } else {
        g_set_error(error, DCONFIG_ERROR, DCONFIG_ERROR_TYPE_MISMATCH,
                   "Configuration key '%s' is not a boolean", key);
        g_debug("Type mismatch for key '%s': expected boolean, got %s", 
                key, g_variant_get_type_string(value));
    }
    
    g_variant_unref(value);
    g_variant_unref(result);
    
    return ret_value;
}

/**
 * dconfig_get_int:
 * @dconfig: A DConfig instance
 * @key: Configuration key name
 * @error: Return location for error
 * 
 * Gets an integer configuration value.
 * 
 * Returns: The configuration value or 0 on error
 */
gint
dconfig_get_int(DConfig *dconfig, const gchar *key, GError **error)
{
    g_return_val_if_fail(dconfig != NULL, 0);
    g_return_val_if_fail(key != NULL, 0);
    
    if (!dconfig->is_valid) {
        g_set_error(error, DCONFIG_ERROR, DCONFIG_ERROR_DBUS_CONNECTION,
                   "Configuration manager is not valid");
        return 0;
    }
    
    GVariant *result = dconfig_get_dconfig_value(dconfig, key, error);
    if (result == NULL) {
        return 0;
    }
    
    GVariant *value = NULL;
    g_variant_get(result, "(v)", &value);
    
    gint ret_value = 0;
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT32)) {
        ret_value = g_variant_get_int32(value);
        g_debug("Retrieved int32 value %d for key '%s'", ret_value, key);
    } else if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT64)) {
        gint64 int64_value = g_variant_get_int64(value);
        if (int64_value >= G_MININT && int64_value <= G_MAXINT) {
            ret_value = (gint)int64_value;
            g_debug("Retrieved int64 value %d for key '%s' (converted from %" G_GINT64_FORMAT ")", 
                    ret_value, key, int64_value);
        } else {
            g_set_error(error, DCONFIG_ERROR, DCONFIG_ERROR_TYPE_MISMATCH,
                       "Configuration key '%s' value is out of range for int", key);
            g_debug("Int64 value %" G_GINT64_FORMAT " for key '%s' is out of range for int", 
                    int64_value, key);
        }
    } else if (g_variant_is_of_type(value, G_VARIANT_TYPE_DOUBLE)) {
        gdouble double_value = g_variant_get_double(value);
        if (double_value >= G_MININT && double_value <= G_MAXINT) {
            ret_value = (gint)double_value;
            g_debug("Retrieved double value %d for key '%s' (converted from %f)", 
                    ret_value, key, double_value);
        } else {
            g_set_error(error, DCONFIG_ERROR, DCONFIG_ERROR_TYPE_MISMATCH,
                       "Configuration key '%s' value is out of range for int", key);
            g_debug("Double value %f for key '%s' is out of range for int", 
                    double_value, key);
        }
    } else {
        g_set_error(error, DCONFIG_ERROR, DCONFIG_ERROR_TYPE_MISMATCH,
                   "Configuration key '%s' is not a numeric type", key);
        g_debug("Type mismatch for key '%s': expected numeric type, got %s", 
                key, g_variant_get_type_string(value));
    }
    
    g_variant_unref(value);
    g_variant_unref(result);
    
    return ret_value;
}

/**
 * dconfig_get_string_array:
 * @dconfig: A DConfig instance
 * @key: Configuration key name
 * @error: Return location for error
 * 
 * Gets a string array configuration value.
 * 
 * Returns: (transfer full) (nullable): The configuration value as a string array, or %NULL on error
 */
gchar **
dconfig_get_string_array(DConfig *dconfig, const gchar *key, GError **error)
{
    g_return_val_if_fail(dconfig != NULL, NULL);
    g_return_val_if_fail(key != NULL, NULL);
    
    if (!dconfig->is_valid) {
        g_set_error(error, DCONFIG_ERROR, DCONFIG_ERROR_DBUS_CONNECTION,
                   "Configuration manager is not valid");
        return NULL;
    }
    
    GVariant *result = dconfig_get_dconfig_value(dconfig, key, error);
    if (result == NULL) {
        return NULL;
    }
    
    GVariant *value = NULL;
    g_variant_get(result, "(v)", &value);
    
    gchar **array = NULL;
    
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING_ARRAY)) {
        array = g_variant_dup_strv(value, NULL);
        g_debug("Retrieved string array for key '%s' with %d elements", 
                key, array ? g_strv_length(array) : 0);
    } else if (g_variant_is_of_type(value, G_VARIANT_TYPE_ARRAY)) {
        /* Handle array of variants containing strings */
        GVariantIter iter;
        GVariant *item;
        GPtrArray *ptr_array = g_ptr_array_new();
        
        g_variant_iter_init(&iter, value);
        while (g_variant_iter_loop(&iter, "v", &item)) {
            if (g_variant_is_of_type(item, G_VARIANT_TYPE_STRING)) {
                gchar *str = g_variant_dup_string(item, NULL);
                if (str != NULL) {
                    g_ptr_array_add(ptr_array, str);
                }
            } else {
                g_warning("Skipping non-string element in array for key '%s'", key);
            }
        }
        
        g_ptr_array_add(ptr_array, NULL); /* NULL-terminate */
        array = (gchar **)g_ptr_array_free(ptr_array, FALSE);
        g_debug("Converted variant array for key '%s' with %d elements", 
                key, array ? g_strv_length(array) : 0);
    } else {
        g_set_error(error, DCONFIG_ERROR, DCONFIG_ERROR_TYPE_MISMATCH,
                   "Configuration key '%s' is not a string array (got type '%s')", 
                   key, g_variant_get_type_string(value));
        g_debug("Type mismatch for key '%s': expected string array, got %s", 
                key, g_variant_get_type_string(value));
    }
    
    g_variant_unref(value);
    g_variant_unref(result);
    
    return array;
}

/**
 * dconfig_set_change_handler:
 * @dconfig: A DConfig instance
 * @handler: Callback function for configuration changes
 * @user_data: User data to pass to the handler
 * 
 * Sets a callback function to be called when configuration values change.
 */
void
dconfig_set_change_handler(DConfig *dconfig, DConfigChangeHandler handler, gpointer user_data)
{
    g_return_if_fail(dconfig != NULL);
    
    dconfig->change_handler = handler;
    dconfig->user_data = user_data;
    
    g_debug("DConfig change handler %s", handler ? "set" : "cleared");
}

/**
 * dconfig_is_valid:
 * @dconfig: A DConfig instance
 * 
 * Checks if the configuration instance is valid and connected.
 * 
 * Returns: %TRUE if the configuration is valid and connected
 */
gboolean
dconfig_is_valid(DConfig *dconfig)
{
    return dconfig != NULL && dconfig->is_valid && 
           dconfig->dbus_connection != NULL && 
           dconfig->resource_path != NULL;
}
