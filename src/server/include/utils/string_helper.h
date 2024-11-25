// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_STRING_HELPER_H_
#define ANYTHING_STRING_HELPER_H_

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

#include "common/anything_fwd.hpp"

#if defined(__cplusplus) && __cplusplus >= 202002L
    #define ANYTHING_CONSTEXPR constexpr
#else
    #define ANYTHING_CONSTEXPR
#endif

ANYTHING_NAMESPACE_BEGIN

namespace string_helper {

ANYTHING_CONSTEXPR inline auto split(std::string s, const std::string& delimiter) {
    std::vector<std::string> tokens;
    size_t pos = 0;
    std::string token;
    while ((pos = s.find(delimiter)) != std::string::npos) {
        token = s.substr(0, pos);
        tokens.push_back(token);
        s.erase(0, pos + delimiter.length());
    }

    tokens.push_back(s);
    return tokens;
}

ANYTHING_CONSTEXPR inline auto starts_with(const std::string& str, const std::string& prefix) {
    return str.rfind(prefix, 0) == 0;
}

ANYTHING_CONSTEXPR inline auto ends_with(const std::string& path, const std::string& ending) {
    if (path.length() >= ending.length())
        return 0 == path.compare(path.length() - ending.length(), ending.length(), ending);
    
    return false;
}

ANYTHING_CONSTEXPR inline auto contains(const std::string& str, const std::string& substr) {
    return str.find(substr) != std::string::npos;
}

inline auto contains_invalid_chars(const std::string& path) {
    // Set of characters that are not allowed in file paths
    std::unordered_set<char> invalid_chars = { ':', '[', ']', '{', '}' };
    return std::any_of(path.begin(), path.end(), [&invalid_chars](char c) {
        return invalid_chars.count(c);
    });
}

} // namespace string_helper

ANYTHING_NAMESPACE_END

#endif // ANYTHING_STRING_HELPER_H_