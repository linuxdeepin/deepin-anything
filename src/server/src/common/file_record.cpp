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
    // extension_mapper_ = {
    //     // Programming Language Files
    //     { ".cpp", "text/c++" },
    //     { ".cxx", "text/c++" },
    //     { ".cc", "text/c++" },
    //     { ".c", "text/c" }, // 需要优化单个字母的情况
    //     { ".h", "text/c" },
    //     { ".hpp", "text/c++" },
    //     { ".cs", "text/csharp" },
    //     { ".java", "text/java" },
    //     { ".py", "text/python" },
    //     { ".js", "application/javascript" },
    //     { ".ts", "application/typescript" },
    //     { ".html", "text/html" },
    //     { ".css", "text/css" },
    //     { ".php", "application/php" },
    //     { ".rb", "text/ruby" },
    //     { ".go", "text/go" },
    //     { ".swift", "text/swift" },
    //     { ".kt", "text/kotlin" },
    //     { ".rs", "text/rust" },
    //     { ".lua", "text/lua" },
    //     { ".sh", "application/shellscript" },
    //     { ".bash", "application/shellscript/shell/bash" },
    //     { ".pl", "text/perl" },
    //     { ".sql", "application/sql" },
    //     { ".json", "application/json" },
    //     { ".xml", "application/xml" },
    //     { ".yaml", "text/yaml" },
    //     { ".toml", "text/toml" },
    //     { ".md", "text/markdown" },
    //     { ".r", "text/r" },
    //     { ".jl", "text/julia/jl" },
    //     { ".dart", "text/dart" },
    //     { ".scala", "text/scala" },
    //     { ".vb", "text/vb" },
    //     { ".asm", "text/assembly/asm" },
    //     { ".bat", "application/batch" },
    //     { ".make", "text/makefile" },
    //     { ".cmake", "text/cmake" },
    //     { ".gradle", "text/gradle" },
    //     { ".dockerfile", "text/dockerfile" },
    //     // Document Files
    //     { ".txt", "text/plain/txt" },
    //     { ".pdf", "application/pdf" },
    //     { ".doc", "application/doc" },
    //     { ".docx", "application/docx" },
    //     { ".xls", "application/xls" },
    //     { ".xlsx", "application/xlsx" },
    //     { ".ppt", "application/ppt" },
    //     { ".pptx", "application/pptx" },
    //     // Image Files
    //     { ".jpg", "image/jpeg/jpg" },
    //     { ".jpeg", "image/jpeg" },
    //     { ".png", "image/png" },
    //     { ".gif", "image/gif" },
    //     { ".bmp", "image/bmp" },
    //     { ".svg", "image/svg+xml" },
    //     { ".ico", "image/vnd.microsoft.icon" },
    //     // Audio Files
    //     { ".mp3", "audio/mpeg" },
    //     { ".wav", "audio/wav" },
    //     { ".ogg", "audio/ogg" },
    //     { ".flac", "audio/flac" },
    //     // Video Files
    //     { ".mp4", "video/mp4" },
    //     { ".avi", "video/avi" },
    //     { ".mov", "video/mov" },
    //     { ".mkv", "video/mkv" },
    //     // Compressed Files
    //     { ".zip", "application/zip" },
    //     { ".tar", "application/tar" },
    //     { ".gz", "application/gzip/gz" },
    //     { ".rar", "application/rar" },
    //     { ".7z", "application/7z" },
    //     // Other Files
    //     { ".iso", "application/iso" },
    //     { ".csv", "text/csv" },
    //     { ".epub", "application/epub" },
    //     { ".apk", "application/apk" },
    //     { ".exe", "application/exe" },
    //     { ".deb", "package/deb" },
    //     { ".a", "library/a"},
    //     { ".so", "library/so" },
    //     { ".dll", "library/dll" }
    // };
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
    } catch(const std::exception& e) {
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