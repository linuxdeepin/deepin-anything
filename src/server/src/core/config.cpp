// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/config.h"

#include <glib.h>

ANYTHING_NAMESPACE_BEGIN

Config& Config::instance() {
    static Config instance;
    return instance;
}

Config::Config()
{
    // init path_blacklist_
    path_blacklist_ = {
        "/.git",
        "/.svn",
        "$HOME/.cache",
        "$HOME/.config",
        "$HOME/.local/share/Trash",
    };
    // Replace $HOME with actual home directory path
    auto home = g_get_home_dir();
    for (auto& path : path_blacklist_) {
        size_t pos = path.find("$HOME");
        if (pos != std::string::npos) {
            path.replace(pos, 5, home);
        }
    }

}

bool Config::isPathInBlacklist(const std::string& path) const
{
    for (const auto& blacklisted_path : path_blacklist_) {
        if (path.find(blacklisted_path) != std::string::npos) {
            return true;
        }
    }
    return false;
}

const std::vector<std::string>& Config::get_path_blocked_list() const
{
    return path_blacklist_;
}

ANYTHING_NAMESPACE_END
