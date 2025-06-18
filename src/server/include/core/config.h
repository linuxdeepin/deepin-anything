// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CORE_CONFIG_H
#define CORE_CONFIG_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>

struct event_handler_config {
    std::string persistent_index_dir;
    std::string volatile_index_dir;
    std::size_t thread_pool_size;
    std::vector<std::string> blacklist_paths;
    std::vector<std::string> indexing_paths;
    std::map<std::string, std::string> file_type_mapping;
    std::map<std::string, std::string> file_type_mapping_original;
    int commit_volatile_index_timeout;
    int commit_persistent_index_timeout;
};

void print_event_handler_config(const event_handler_config &config);


#define LOG_LEVEL_KEY "log_level"

class Config {
public:
    Config();
    ~Config();

    // Delete copy constructor and assignment operator
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    std::shared_ptr<event_handler_config> make_event_handler_config();

    void set_config_change_handler(std::function<void(std::string)> config_change_handler);
    void notify_config_changed(const std::string &key);

    std::string get_log_level();

private:
    std::vector<std::string> blacklist_paths_;
    std::vector<std::string> indexing_paths_;
    std::map<std::string, std::string> file_type_mapping_;
    std::string log_level_;
    int commit_volatile_index_timeout_;
    int commit_persistent_index_timeout_;

    void* dbus_connection_;
    std::string resource_path_;
    std::function<void(std::string)> config_change_handler_;
    int subscription_id_;
};

bool is_path_in_blacklist(const std::string& path, const std::vector<std::string>& blacklist_paths);

#define APP_RESTART_CODE 1
#define APP_QUIT_CODE 0

void set_app_restart(bool restart);

int get_app_ret_code();

#endif // CORE_CONFIG_H 