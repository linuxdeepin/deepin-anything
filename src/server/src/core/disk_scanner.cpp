// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/disk_scanner.h"

#include <string>

#include <mntent.h>

#include "utils/log.h"
#include "core/config.h"

ANYTHING_NAMESPACE_BEGIN

std::vector<std::string> disk_scanner::scan(const fs::path& root, const std::vector<std::string>& blacklist_paths) {
    spdlog::info("Scanning {}...", root.string());
    std::vector<std::string> records;
    fs::recursive_directory_iterator dirpos{ root, fs::directory_options::skip_permission_denied };
    std::error_code ec;
    for (auto it = begin(dirpos); it != end(dirpos); ++it) {
        if (is_path_in_blacklist(it->path().string(), blacklist_paths) ||
            !std::filesystem::exists(it->path(), ec)) {
            it.disable_recursion_pending();
            continue;
        }

        records.push_back(it->path().string());

        if (disk_scanner::stop_scanning) {
            spdlog::info("Scanning interrupted");
            return records;
        }
    }

    spdlog::info("Scanning {} completed", root.string());
    return records;
}

bool disk_scanner::is_hidden(const fs::path& p) {
    auto filename = p.filename().string();
    return filename == ".." || filename == "." || filename[0] == '.';
}

ANYTHING_NAMESPACE_END
