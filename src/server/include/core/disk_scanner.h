// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_DISK_SCANNER_H_
#define ANYTHING_DISK_SCANNER_H_

#include <atomic>
#include <filesystem>
#include <vector>

#include "common/anything_fwd.hpp"

ANYTHING_NAMESPACE_BEGIN

namespace fs = std::filesystem;

/// A class to scan mounted disks and perform operations on their files and directories.
struct disk_scanner {
    static std::vector<std::string> scan(const fs::path& root);
    static bool is_hidden(const fs::path& p);
    inline static std::atomic<bool> stop_scanning = false;
};

ANYTHING_NAMESPACE_END

#endif // ANYTHING_DISK_SCANNER_H_