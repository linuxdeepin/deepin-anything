// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "utils/sys.h"

#include <filesystem>

#include <pwd.h>
#include <unistd.h>

ANYTHING_NAMESPACE_BEGIN

std::string get_home_directory() {
    if (const char* sudo_user = std::getenv("SUDO_USER")) {
        struct passwd* pw = getpwnam(sudo_user);
        if (pw && pw->pw_dir) {
            return pw->pw_dir;
        }
    } else if (const char* home_dir = std::getenv("HOME")) {
        return home_dir;
    }

    return {};
}

std::string get_user_cache_directory() {
    if (const char* cache_home = std::getenv("XDG_CACHE_HOME")) {
        return cache_home;
    } else {
        auto home_dir = get_home_directory();
        if (!home_dir.empty()) {
            return home_dir + "/.cache";
        }
    }

    return {};
}

std::string get_sys_cache_directory() {
    std::filesystem::path cache_dir = "/var/cache";
    if (std::filesystem::exists(cache_dir) && std::filesystem::is_directory(cache_dir)) {
        return cache_dir.string();
    }

    return {};
}

ANYTHING_NAMESPACE_END