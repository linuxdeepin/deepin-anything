// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_CONFIG_H_
#define ANYTHING_CONFIG_H_

#include <fstream> // ifstream
#include <codecvt>

#include "common/anything_fwd.hpp"
#include "utils/string_helper.h"

ANYTHING_NAMESPACE_BEGIN

template<typename F>
auto load_config_file(const std::string& file,
    F fn, const std::string& delimiter = "=") noexcept {
    std::ifstream data_file(file);
    if (!data_file.is_open())
        return;

    std::string line;
    while (std::getline(data_file, line)) {
        // skip empty and comment lines
        if (line.empty() || line[0] == '#') continue;

        auto result = string_helper::split(std::move(line), delimiter);
        if (result.size() == 2) {
            fn(std::move(result[0]), std::move(result[1]));
        }
    }
}

template<typename F>
auto load_dictionary(const std::string& file, F fn) {
    std::wifstream wif(file);
    wif.imbue(std::locale("zh_CN.UTF-8"));
    if (!wif.is_open())
        return;

    std::wstring line;
    while (std::getline(wif, line)) {
        if (line.empty()) continue;

        fn(std::move(line));
    }
}

ANYTHING_NAMESPACE_END

#endif // ANYTHING_CONFIG_H_