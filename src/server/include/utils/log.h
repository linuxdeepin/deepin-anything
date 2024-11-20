#ifndef ANYTHING_LOG_H_
#define ANYTHING_LOG_H_

// #include <iomanip> // std::put_time
// #include <sstream> // std::ostringstream
#include <iostream>
#include <thread>

#include <fmt/chrono.h>
#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/ostream.h> // for setting output stream
#include <fmt/ranges.h>  // Formatted ranges
#include <fmt/std.h>     // for printing std::this_thread::get_id()
#include <fmt/xchar.h>

#include "common/anything_fwd.hpp"
#include "utils/enum_helper.h"

ANYTHING_NAMESPACE_BEGIN

namespace log {

#if defined(__cplusplus) && __cplusplus >= 202002L
    #define ANYTHING_CONSTINIT constinit
#else
    #define ANYTHING_CONSTINIT
#endif

enum class level : uint8_t {
    debug = 1 << 0,
    info = 1 << 1,
    warning = 1 << 2,
    success = 1 << 3,
    error = 1 << 4,
    all = debug | info | warning | success | error
};

ANYTHING_CONSTINIT inline auto level_debug_enabled   = false;
ANYTHING_CONSTINIT inline auto level_info_enabled    = false;
ANYTHING_CONSTINIT inline auto level_warning_enabled = false;
ANYTHING_CONSTINIT inline auto level_success_enabled = false;
ANYTHING_CONSTINIT inline auto level_error_enabled   = false;

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

namespace detail {

inline void print_pattern_info(std::string_view level_info) {
    using namespace std::chrono;
    fmt::print("[{:%Y-%m-%d %H:%M:%S}] [{}] [Thread {}] ", system_clock::now(), level_info, std::this_thread::get_id());
    // auto now = std::chrono::system_clock::now();
    // std::time_t t = std::chrono::system_clock::to_time_t(now);
    // std::tm tm = *std::localtime(&t);
    // std::ostringstream ss;
    // ss << std::this_thread::get_id();
    // fmt::print("[{}] [{}] [Thread {}] ", std::put_time(&tm, "%Y-%m-%d %H:%M:%S"), level_info, ss.str());
}

} // namespace detail

template <typename... Args>
inline auto debug(fmt::format_string<Args...>&& fmt, Args&&... args)
    -> void {
    if (level_debug_enabled) {
        detail::print_pattern_info("DEBUG");
        fmt::println(std::forward<fmt::format_string<Args...>>(fmt), std::forward<Args>(args)...);
    }
}

template <typename... Args>
inline auto debug(fmt::wformat_string<Args...>&& fmt, Args&&... args)
    -> void {
    if (level_debug_enabled) {
        detail::print_pattern_info("DEBUG");
        std::wcout << fmt::format(std::forward<fmt::wformat_string<Args...>>(fmt), std::forward<Args>(args)...);
        std::wcout << L"\n";
    }
}

template <typename... Args>
inline auto info(fmt::format_string<Args...>&& fmt, Args&&... args)
    -> void {
    if (level_info_enabled) {
        detail::print_pattern_info("INFO");
        fmt::println(std::forward<fmt::format_string<Args...>>(fmt), std::forward<Args>(args)...);
    }
}

template <typename... Args>
inline auto warning(fmt::format_string<Args...>&& fmt, Args&&... args)
    -> void {
    if (level_warning_enabled) {
        detail::print_pattern_info("WARNING");
        fmt::println(std::forward<fmt::format_string<Args...>>(fmt), std::forward<Args>(args)...);
    }
}

template <typename... Args>
inline auto success(fmt::format_string<Args...>&& fmt, Args&&... args)
    -> void {
    if (level_success_enabled) {
        detail::print_pattern_info("SUCCESS");
        fmt::println(std::forward<fmt::format_string<Args...>>(fmt), std::forward<Args>(args)...);
    }
}

template <typename... Args>
inline auto error(fmt::format_string<Args...>&& fmt, Args&&... args)
    -> void {
    if (level_error_enabled) {
        detail::print_pattern_info("ERROR");
        fmt::println(std::forward<fmt::format_string<Args...>>(fmt), std::forward<Args>(args)...);
    }
}

} // namespace log

ANYTHING_NAMESPACE_END

#endif // ANYTHING_LOG_H_