// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_FILE_RECORD_H_
#define ANYTHING_FILE_RECORD_H_

#include <filesystem>
#include <string>
#include <unordered_map>

#include "common/anything_fwd.hpp"

ANYTHING_NAMESPACE_BEGIN

struct file_record {
    std::string file_name;
    std::string full_path;
    std::string file_type;
    std::string file_ext;
    int64_t modify_time; // milliseconds time since epoch
    int64_t file_size;
    bool is_hidden;
};

class file_helper {
public:
    file_helper();

    file_record make_file_record(const std::filesystem::path& p);

    int64_t get_file_creation_time(const std::filesystem::path& file_path);
    int64_t to_milliseconds_since_epoch(std::string date_time);

private:
    bool is_valid_date_format(std::string& date_str);
};

ANYTHING_NAMESPACE_END

#endif // ANYTHING_FILE_RECORD_H_
