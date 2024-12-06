// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/disk_scanner.h"

#include <string>

#include <mntent.h>

#include "utils/log.h"

ANYTHING_NAMESPACE_BEGIN

std::vector<std::string> disk_scanner::scan(const fs::path& root) {
    log::info() << "Scanning " << root.string() << "...\n";
    std::vector<std::string> records;
    fs::recursive_directory_iterator dirpos{ root, fs::directory_options::skip_permission_denied };
    for (auto it = begin(dirpos); it != end(dirpos); ++it) {
        // Skip hidden files and folders
        if (disk_scanner::is_hidden(it->path())) {
            // Prevent recursion into hidden folders
            if (fs::is_directory(it->path())) {
                it.disable_recursion_pending();
            }
            continue;
        }

        if (std::filesystem::exists(it->path())) {
            records.push_back(it->path().string());
        }

        if (disk_scanner::stop_scanning) {
            log::info() << "Scanning interrupted\n";
            return records;
        }
    }

    log::info() << "Scanning " << root.string() << " completed\n";
    return records;
}

bool disk_scanner::is_hidden(const fs::path& p) {
    auto filename = p.filename().string();
    return filename == ".." || filename == "." || filename[0] == '.';
}

ANYTHING_NAMESPACE_END