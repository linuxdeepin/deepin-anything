// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "utils/sys.h"

#include <filesystem>

#include <pwd.h>
#include <unistd.h>
#include <sys/stat.h>
#include <glib.h>
#include <mutex>

ANYTHING_NAMESPACE_BEGIN

const std::string get_home_directory() {
    static std::mutex mtx;
    static std::string home_dir;

    // Use double-checked locking pattern
    if (home_dir.empty()) {
        std::lock_guard<std::mutex> lock(mtx);
        if (home_dir.empty()) {
            // Check for home directory in different locations
            const char* prefix = nullptr;
            struct stat statbuf;

            if (stat("/data/home", &statbuf) == 0) {
                prefix = "/data";
            } else if (stat("/persistent/home", &statbuf) == 0) {
                prefix = "/persistent";
            }

            // Build full home path
            std::string full_path;
            if (prefix) {
                full_path = prefix;
            }
            full_path += g_get_home_dir();

            home_dir = std::move(full_path);
        }
    }

    return home_dir;
}

ANYTHING_NAMESPACE_END
