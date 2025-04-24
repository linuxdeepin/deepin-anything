// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CORE_CONFIG_H
#define CORE_CONFIG_H

#include "common/anything_fwd.hpp"
#include <string>
#include <vector>

ANYTHING_NAMESPACE_BEGIN

class Config {
public:
    static Config& instance();

    // Delete copy constructor and assignment operator
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    bool isPathInBlacklist(const std::string& path) const;
    const std::vector<std::string>& get_path_blocked_list() const;

private:
    Config();
    ~Config() = default;

private:
    std::vector<std::string> path_blacklist_;

};

ANYTHING_NAMESPACE_END

#endif // CORE_CONFIG_H 