// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/config.h"
#include "spdlog/spdlog.h"
#include "utils/tools.h"
#include "utils/string_helper.h"

#include <glib.h>
#include <sstream>
#include <gio/gio.h>

#define COMMIT_VOLATILE_INDEX_TIMEOUT_DEFAULT 2
#define COMMIT_VOLATILE_INDEX_TIMEOUT_MIN 1
#define COMMIT_VOLATILE_INDEX_TIMEOUT_MAX 60
#define COMMIT_PERSISTENT_INDEX_TIMEOUT_DEFAULT 600
#define COMMIT_PERSISTENT_INDEX_TIMEOUT_MIN 60
#define COMMIT_PERSISTENT_INDEX_TIMEOUT_MAX 3600

void print_event_handler_config(const event_handler_config &config) {
    spdlog::info("Persistent index dir: {}", config.persistent_index_dir);
    spdlog::info("Volatile index dir: {}", config.volatile_index_dir);
    spdlog::info("Thread pool size: {}", config.thread_pool_size);
    spdlog::info("Blacklist paths:");
    for (const auto& path : config.blacklist_paths) {
        spdlog::info("  {}", path);
    }
    spdlog::info("Indexing paths:");
    for (const auto& path : config.indexing_paths) {
        spdlog::info("  {}", path);
    }
    spdlog::info("File type mapping:");
    for (const auto& [file_type, file_exts] : config.file_type_mapping_original) {
        spdlog::info("  {} : {}", file_type, file_exts);
    }
    spdlog::info("Commit volatile index timeout: {}", config.commit_volatile_index_timeout);
    spdlog::info("Commit persistent index timeout: {}", config.commit_persistent_index_timeout);
}

// 获取dconfig资源路径
std::string get_dconfig_resource_path(GDBusConnection *connection) {
    GError *error = nullptr;
    GVariant *result = nullptr;
    std::string resource_path;

    // Call the D-Bus method
    result = g_dbus_connection_call_sync(
        connection,
        "org.desktopspec.ConfigManager",    // destination
        "/",                                // object path
        "org.desktopspec.ConfigManager",    // interface
        "acquireManager",                   // method
        g_variant_new("(sss)",              // parameters
            "org.deepin.anything",
            "org.deepin.anything",
            ""),
        G_VARIANT_TYPE("(o)"),             // reply type
        G_DBUS_CALL_FLAGS_NONE,
        1000,                              // timeout is 1s
        nullptr,
        &error
    );

    if (error) {
        spdlog::error("D-Bus call failed: {}", error->message);
        g_error_free(error);
    } else if (result) {
        gchar *path = nullptr;
        g_variant_get(result, "(o)", &path);
        if (path) {
            resource_path = path;
            g_free(path);
        }
        g_variant_unref(result);
    }

    return resource_path;
}

// 获取dconfig配置
GVariant *get_config_value(GDBusConnection *connection, const std::string& resource_path, const std::string& key) {
    GError *error = nullptr;
    GVariant *result = nullptr;

    // Call the D-Bus method to get config value
    result = g_dbus_connection_call_sync(
        connection,
        "org.desktopspec.ConfigManager",    // destination
        resource_path.c_str(),              // object path
        "org.desktopspec.ConfigManager.Manager", // interface
        "value",                            // method
        g_variant_new("(s)", key.c_str()),  // parameters
        G_VARIANT_TYPE("(v)"),              // reply type
        G_DBUS_CALL_FLAGS_NONE,
        1000,                               // timeout is 1s
        nullptr,
        &error
    );

    if (error) {
        spdlog::error("D-Bus call failed: {}", error->message);
        g_error_free(error);
    }

    return result;
}

std::vector<std::string> get_config_string_list(GDBusConnection *connection, const std::string& resource_path, const std::string& key) {
    GVariant *result = nullptr;
    std::vector<std::string> config_list;

    result = get_config_value(connection, resource_path, key);
    if (result) {
        GVariant *val = nullptr;
        GVariantIter iter;
        GVariant *item = nullptr;
        gchar *str;
        g_variant_get(result, "(v)", &val);
        g_variant_iter_init(&iter, val);
        while (g_variant_iter_loop(&iter, "v", &item)) {
            g_variant_get(item, "s", &str);
            config_list.push_back(str);
            g_free(str);
        }
        g_variant_unref(val);
        g_variant_unref(result);
    }

    return config_list;
}

std::string get_config_string(GDBusConnection *connection, const std::string& resource_path, const std::string& key) {
    GVariant *result = nullptr;
    std::string config_value;

    result = get_config_value(connection, resource_path, key);
    if (result) {
        GVariant *val = nullptr;
        gchar *str;
        g_variant_get(result, "(v)", &val);
        g_variant_get(val, "s", &str);
        if (str) {
            config_value = str;
            g_free(str);
        }
        g_variant_unref(val);
        g_variant_unref(result);
    }

    return config_value;
}

int64_t get_config_int64(GDBusConnection *connection, const std::string& resource_path, const std::string& key) {
    GVariant *result = nullptr;
    int64_t config_value = 0;

    result = get_config_value(connection, resource_path, key);
    if (result) {
        GVariant *val = nullptr;
        g_variant_get(result, "(v)", &val);
        g_variant_get(val, "x", &config_value);
        g_variant_unref(val);
        g_variant_unref(result);
    }

    return config_value;
}


bool replace_home_dir(std::string& path) {
    auto home = g_get_home_dir();
    size_t pos = path.find("$HOME");
    if (pos != std::string::npos) {
        path.replace(pos, strlen("$HOME"), home);
        return true;
    }
    return false;
}

void dconfig_changed (G_GNUC_UNUSED GDBusConnection *connection,
                    G_GNUC_UNUSED const gchar *sender_name,
                    G_GNUC_UNUSED const gchar *object_path,
                    G_GNUC_UNUSED const gchar *interface_name,
                    G_GNUC_UNUSED const gchar *signal_name,
                    GVariant *parameters,
                    gpointer user_data) {
    gchar *str;
    std::string key;
    Config* config = (Config*)user_data;

    g_variant_get(parameters, "(s)", &str);
    if (str) {
        key = str;
        g_free(str);
    }

    if (!key.empty())
        config->notify_config_changed(key);
}

Config::Config()
{
    // 获取dconfig配置
    dbus_connection_ = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
    if (!dbus_connection_) {
        spdlog::error("Failed to connect to system bus");
        exit(APP_QUIT_CODE);
    }

    resource_path_ = get_dconfig_resource_path((GDBusConnection*)dbus_connection_);
    if(resource_path_.empty()) {
        spdlog::error("Failed to get dconfig resource path");
        exit(APP_QUIT_CODE);
    }

    indexing_paths_ = get_config_string_list((GDBusConnection*)dbus_connection_, resource_path_, "indexing_paths");
    blacklist_paths_ = get_config_string_list((GDBusConnection*)dbus_connection_, resource_path_, "blacklist_paths");
    std::string app_file_suffix, archive_file_suffix, audio_file_suffix, doc_file_suffix, pic_file_suffix, video_file_suffix;
    app_file_suffix = get_config_string((GDBusConnection*)dbus_connection_, resource_path_, "app_file_suffix");
    archive_file_suffix = get_config_string((GDBusConnection*)dbus_connection_, resource_path_, "archive_file_suffix");
    audio_file_suffix = get_config_string((GDBusConnection*)dbus_connection_, resource_path_, "audio_file_suffix");
    doc_file_suffix = get_config_string((GDBusConnection*)dbus_connection_, resource_path_, "doc_file_suffix");
    pic_file_suffix = get_config_string((GDBusConnection*)dbus_connection_, resource_path_, "pic_file_suffix");
    video_file_suffix = get_config_string((GDBusConnection*)dbus_connection_, resource_path_, "video_file_suffix");
    file_type_mapping_ = {
        {"app",     app_file_suffix},
        {"archive", archive_file_suffix},
        {"audio",   audio_file_suffix},
        {"doc",     doc_file_suffix},
        {"pic",     pic_file_suffix},
        {"video",   video_file_suffix},
    };
    if(indexing_paths_.empty() ||
        blacklist_paths_.empty() ||
        app_file_suffix.empty() ||
        archive_file_suffix.empty() ||
        audio_file_suffix.empty() ||
        doc_file_suffix.empty() ||
        pic_file_suffix.empty() ||
        video_file_suffix.empty()) {
        spdlog::error("Failed to get dconfig config");
        exit(APP_QUIT_CODE);
    }

    log_level_ = get_config_string((GDBusConnection*)dbus_connection_, resource_path_, LOG_LEVEL_KEY);

    commit_volatile_index_timeout_ = get_config_int64((GDBusConnection*)dbus_connection_, resource_path_, "commit_volatile_index_timeout");
    commit_persistent_index_timeout_ = get_config_int64((GDBusConnection*)dbus_connection_, resource_path_, "commit_persistent_index_timeout");
    if (commit_volatile_index_timeout_ < COMMIT_VOLATILE_INDEX_TIMEOUT_MIN ||
        commit_volatile_index_timeout_ > COMMIT_VOLATILE_INDEX_TIMEOUT_MAX) {
        commit_volatile_index_timeout_ = COMMIT_VOLATILE_INDEX_TIMEOUT_DEFAULT;
    }
    if (commit_persistent_index_timeout_ < COMMIT_PERSISTENT_INDEX_TIMEOUT_MIN ||
        commit_persistent_index_timeout_ > COMMIT_PERSISTENT_INDEX_TIMEOUT_MAX) {
        commit_persistent_index_timeout_ = COMMIT_PERSISTENT_INDEX_TIMEOUT_DEFAULT;
    }

    // Replace $HOME with actual home directory path
    for (auto& path : blacklist_paths_) {
        replace_home_dir(path);
    }

    for (auto& path : indexing_paths_) {
        if (replace_home_dir(path)) {
            continue;
        }
        // not allow relative path
        if (!anything::string_helper::starts_with(path, "/")) {
            path.insert(0, "/");
            continue;
        }
    }

    subscription_id_ = g_dbus_connection_signal_subscribe((GDBusConnection*)dbus_connection_,
                                                        "org.desktopspec.ConfigManager",            // sender
                                                        "org.desktopspec.ConfigManager.Manager",    // interface
                                                        "valueChanged",                             // signal
                                                        resource_path_.c_str(),                     // object path
                                                        NULL,
                                                        G_DBUS_SIGNAL_FLAGS_NONE,
                                                        dconfig_changed,
                                                        this,
                                                        NULL);
}

Config::~Config() {
    g_dbus_connection_signal_unsubscribe((GDBusConnection*)dbus_connection_, subscription_id_);
    g_object_unref(dbus_connection_);
}

std::shared_ptr<event_handler_config> Config::make_event_handler_config()
{
    auto config = std::make_shared<event_handler_config>();
    config->persistent_index_dir = std::string(g_get_user_cache_dir()) + "/deepin-anything-server";
    config->volatile_index_dir = std::string(g_get_user_runtime_dir()) + "/deepin-anything-server";
    // 3 primary threads: event receiving thread, event filter thread, timer thread
    std::size_t free_threads = std::max(std::thread::hardware_concurrency() - 3, 1U);
    config->thread_pool_size = get_thread_pool_size_from_env(free_threads);
    config->blacklist_paths = blacklist_paths_;
    config->indexing_paths = indexing_paths_;
    config->file_type_mapping_original = file_type_mapping_;
    for (const auto& [file_type, file_exts] : file_type_mapping_) {
        std::stringstream ss(file_exts);
        std::string item;
        while (std::getline(ss, item, ';')) {
            config->file_type_mapping[item] = file_type;
        }
    }
    config->commit_volatile_index_timeout = commit_volatile_index_timeout_;
    config->commit_persistent_index_timeout = commit_persistent_index_timeout_;

    return config;
}

void Config::set_config_change_handler(std::function<void(std::string)> config_change_handler)
{
    config_change_handler_ = config_change_handler;
}

void Config::notify_config_changed(const std::string &key)
{
    if (key == LOG_LEVEL_KEY) {
        log_level_ = get_config_string((GDBusConnection*)dbus_connection_, resource_path_, LOG_LEVEL_KEY);
    }

    if (config_change_handler_)
        config_change_handler_(key);
}

std::string Config::get_log_level()
{
    return log_level_;
}


bool is_path_in_blacklist(const std::string& path, const std::vector<std::string>& blacklist_paths)
{
    for (const auto& blacklisted_path : blacklist_paths) {
        if (path.find(blacklisted_path) != std::string::npos) {
            return true;
        }
    }
    return false;
}

static int app_ret_code = 0;

void set_app_restart(bool restart)
{
    app_ret_code = restart ? APP_RESTART_CODE : APP_QUIT_CODE;
}

int get_app_ret_code()
{
    return app_ret_code;
}
