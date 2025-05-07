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

struct event_handler_config {
    std::string persistent_index_dir;
    std::string volatile_index_dir;
    std::size_t thread_pool_size;
    std::vector<std::string> blacklist_paths;
    std::vector<std::string> indexing_paths;
    std::map<std::string, std::string> file_type_mapping;
};

class Config {
public:
    Config();
    ~Config() = default;

    // Delete copy constructor and assignment operator
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    std::shared_ptr<event_handler_config> make_event_handler_config();

private:
    std::vector<std::string> blacklist_paths_;
    std::vector<std::string> indexing_paths_;
    std::map<std::string, std::string> file_type_mapping_;

    std::shared_ptr<void> dbus_connection_;
};

bool is_path_in_blacklist(const std::string& path, const std::vector<std::string>& blacklist_paths);

#endif // CORE_CONFIG_H 