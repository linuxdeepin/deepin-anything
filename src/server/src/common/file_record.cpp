// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/file_record.h"

#include <sys/stat.h>  // For statx and struct statx
#include <fcntl.h>     // For AT_FDCWD

#include "utils/log.h"
#include "utils/config.h"

ANYTHING_NAMESPACE_BEGIN

file_helper::file_helper() {
    spdlog::info("current dir: {}", std::filesystem::current_path().string());

    load_config_file("config/filetypes.cfg",
        [this](std::string&& suffix, std::string&& filetype) {
        extension_mapper_.emplace(suffix, filetype);
    });
}

file_record file_helper::make_file_record(
    const std::filesystem::path& p) {
    namespace fs = std::filesystem;
    std::string type;
    if (fs::is_regular_file(p)) {
        auto it = extension_mapper_.find(p.extension().string());
        type = it != extension_mapper_.end() ? it->second : "regular/file";
    } else if (fs::is_directory(p)) {
        type = "directory";
    } else if (fs::is_symlink(p)) {
        type = "symlink";
    } else {
        type = "unknown";
    }

    try {
        return file_record {
            .file_name     = p.filename().string(),
            .full_path     = p.string(),
            .file_type     = type,
            .creation_time = get_file_creation_time(p)
        };
    } catch (const std::exception& e) {
        throw;
    }
}

bool file_helper::is_valid_date_format(std::string& date_time) {
    switch (date_time.length()) {
    case 19:
        if (date_time[4] != '-'  || date_time[7] != '-' || date_time[10] != ' ' ||
            date_time[13] != ':' || date_time[16] != ':') {
            return false;
        }
        for (size_t i = 0; i < date_time.size(); ++i) {
            if ((i == 4 || i == 7 || i == 10 || i == 13 || i == 16)) continue;
            if (!std::isdigit(date_time[i])) return false;
        }

        return true;
    case 10:
        if (date_time[4] != '-' || date_time[7] != '-') return false;
        for (size_t i = 0; i < date_time.size(); ++i) {
            if (i == 4 || i == 7) continue;
            if (!std::isdigit(date_time[i])) return false;
        }

        date_time += " 00:00:00";
        return true;
    case 4:
        if (std::find_if(date_time.begin(), date_time.end(),
            [](unsigned char c) { return !std::isdigit(c); }) == date_time.end()) {
            date_time += "-01-01 00:00:00";
            return true;
        }
        [[fallthrough]];
    default:
        return false;
    }
}

int64_t file_helper::to_milliseconds_since_epoch(std::string date_time) {
    std::tm tm = {};
    if (is_valid_date_format(date_time)) {
        strptime(date_time.c_str(), "%Y-%m-%d %H:%M:%S", &tm);
        std::time_t time_t_epoch = std::mktime(&tm);
        return time_t_epoch;
    }

    // spdlog::error("Failed to match any date format: " + date_time);
    throw std::runtime_error("Failed to match any date format: " + date_time);
}

int64_t file_helper::get_file_creation_time(const std::filesystem::path& file_path) {
    struct statx statxbuf;
    // Use statx to retrieve file state; STATX_BTIME is used to obtain the creation time.
    if (statx(AT_FDCWD, file_path.c_str(), AT_STATX_SYNC_AS_STAT, STATX_BTIME, &statxbuf) != 0) {
        throw std::runtime_error("Failed to get file creation time: " + file_path.string());
    }

    time_t file_time = statxbuf.stx_btime.tv_sec;
    return file_time;
}

ANYTHING_NAMESPACE_END