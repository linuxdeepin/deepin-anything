// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/file_record.h"

#include <regex>

#include <sys/stat.h>  // For statx and struct statx
#include <fcntl.h>     // For AT_FDCWD

#include "utils/log.h"

ANYTHING_NAMESPACE_BEGIN

file_helper::file_helper()
    : handle_(::magic_open(MAGIC_MIME_TYPE)) {
    ::magic_load(handle_, NULL);
}

file_helper::~file_helper() {
    ::magic_close(handle_);
}

file_record file_helper::make_file_record(
    const std::filesystem::path& p) {
    std::lock_guard<std::mutex> lg(mtx_);
    auto file_type = ::magic_file(handle_, p.native().c_str());
    std::string type(file_type ? file_type : "unknown");
    auto creation_time = get_file_creation_time(p);
    auto milliseconds = to_milliseconds_since_epoch(parse_datetime(creation_time));

    spdlog::info("creation_time: {}, milliseconds: {}", creation_time, milliseconds);
    // spdlog::debug("---type: {}", type);

    return file_record {
        .file_name     = p.filename().string(),
        .full_path     = p.string(),
        .file_type     = std::move(type),
        .creation_time = milliseconds
    };
}

std::chrono::system_clock::time_point file_helper::parse_datetime(const std::string& datetime) {
    std::tm tm = {};
    
    // Regular expression patterns for different formats
    std::regex full_format(R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$)");   // Full datetime format
    std::regex date_only_format(R"(^\d{4}-\d{2}-\d{2}$)");                   // Date only format
    std::regex year_only_format(R"(^\d{4}$)");                                // Year only format

    // Match the input string with different formats
    if (std::regex_match(datetime, full_format)) {
        std::istringstream ss(datetime);
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    } else if (std::regex_match(datetime, date_only_format)) {
        std::istringstream ss(datetime);
        ss >> std::get_time(&tm, "%Y-%m-%d");
        tm.tm_hour = 0;
        tm.tm_min  = 0;
        tm.tm_sec  = 0;
    } else if (std::regex_match(datetime, year_only_format)) {
        std::istringstream ss(datetime);
        ss >> std::get_time(&tm, "%Y");
        tm.tm_mon  = 0; // January
        tm.tm_mday = 1; // First day of the month
        tm.tm_hour = 0;
        tm.tm_min  = 0;
        tm.tm_sec  = 0;
    } else {
        throw std::runtime_error("Failed to match any date format");
    }

    std::time_t time_t_epoch = std::mktime(&tm);
    return std::chrono::system_clock::from_time_t(time_t_epoch);
}

int64_t file_helper::to_milliseconds_since_epoch(const std::chrono::system_clock::time_point &tp) {
    auto duration = tp.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

std::string file_helper::get_file_creation_time(const std::filesystem::path& file_path) {
    struct statx statxbuf;
    // 使用 statx 获取文件状态，STATX_BTIME 用于获取创建时间
    if (statx(AT_FDCWD, file_path.c_str(), AT_STATX_SYNC_AS_STAT, STATX_BTIME, &statxbuf) != 0) {
        throw std::runtime_error("Failed to get file creation time");
    }

    time_t file_time = statxbuf.stx_btime.tv_sec;
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&file_time), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

ANYTHING_NAMESPACE_END