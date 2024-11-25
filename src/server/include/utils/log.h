// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_LOG_H_
#define ANYTHING_LOG_H_

#include <iomanip> // std::put_time
#include <iostream>
#include <streambuf>
#include <thread>

#include "common/anything_fwd.hpp"
#include "utils/enum_helper.h"

ANYTHING_NAMESPACE_BEGIN

namespace log {

enum class level : uint8_t {
    debug = 1 << 0,
    info = 1 << 1,
    warning = 1 << 2,
    success = 1 << 3,
    error = 1 << 4,
    all = debug | info | warning | success | error
};

inline auto level_debug_enabled   = false;
inline auto level_info_enabled    = false;
inline auto level_warning_enabled = false;
inline auto level_success_enabled = false;
inline auto level_error_enabled   = false;

inline auto set_level(level log_level, bool enabled = true) -> void {
    if (enabled) {
        level_debug_enabled   |= to_underlying(log_level) & to_underlying(level::debug);
        level_info_enabled    |= to_underlying(log_level) & to_underlying(level::info);
        level_warning_enabled |= to_underlying(log_level) & to_underlying(level::warning);
        level_success_enabled |= to_underlying(log_level) & to_underlying(level::success);
        level_error_enabled   |= to_underlying(log_level) & to_underlying(level::error);
    } else {
        level_debug_enabled   &= ~(to_underlying(log_level) & to_underlying(level::debug));
        level_info_enabled    &= ~(to_underlying(log_level) & to_underlying(level::info));
        level_warning_enabled &= ~(to_underlying(log_level) & to_underlying(level::warning));
        level_success_enabled &= ~(to_underlying(log_level) & to_underlying(level::success));
        level_error_enabled   &= ~(to_underlying(log_level) & to_underlying(level::error));
    }
}

inline auto operator|(level lhs, level rhs) -> level {
    return static_cast<level>(to_underlying(lhs) | to_underlying(rhs));
}

struct null_buffer : ::std::streambuf {
    int overflow(int c) override { return c; }
};

class null_stream : public ::std::ostream {
    null_buffer null_buf;
public:
    null_stream() : ::std::ostream(&null_buf) {}
};

inline ::std::ostream& log_os = ::std::cout;
inline null_stream null_os;

namespace detail {

inline void print_pattern_info(std::string_view level_info) {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&t);
    log_os << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "]"
           << " [" << level_info << "]" << " [Thread " << std::this_thread::get_id() << "] ";
}

} // namespace detail

inline ::std::ostream& debug() {
    if (level_debug_enabled) {
        detail::print_pattern_info("DEBUG");
        return log_os;
    }

    return null_os;
}

inline ::std::ostream& info() {
    if (level_info_enabled) {
        detail::print_pattern_info("INFO");
        return log_os;
    }

    return null_os;
}

inline ::std::ostream& warning() {
    if (level_warning_enabled) {
        detail::print_pattern_info("WARNING");
        return log_os;
    }

    return null_os;
}

inline ::std::ostream& success() {
    if (level_success_enabled) {
        detail::print_pattern_info("SUCCESS");
        return log_os;
    }

    return null_os;
}

inline ::std::ostream& error() {
    if (level_error_enabled) {
        detail::print_pattern_info("ERROR");
        return log_os;
    }

    return null_os;
}

} // namespace log

ANYTHING_NAMESPACE_END

#endif // ANYTHING_LOG_H_