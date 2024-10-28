#ifndef ANYTHING_LOG_H_
#define ANYTHING_LOG_H_

#include <iostream>

#include <fmt/chrono.h>
#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/xchar.h>

#include "anything_fwd.hpp"
#include "enum_helper.h"

ANYTHING_NAMESPACE_BEGIN

namespace log {

#if defined(__cplusplus) and __cplusplus >= 202002L
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

inline ANYTHING_CONSTINIT bool level_debug_enabled   = false;
inline ANYTHING_CONSTINIT bool level_info_enabled    = false;
inline ANYTHING_CONSTINIT bool level_warning_enabled = false;
inline ANYTHING_CONSTINIT bool level_success_enabled = false;
inline ANYTHING_CONSTINIT bool level_error_enabled   = false;

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
    fmt::print("[{:%Y-%m-%d %H:%M:%S}] [{}] ", system_clock::now(), level_info);
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
inline auto info(fmt::wformat_string<Args...>&& fmt, Args&&... args)
    -> void {
    if (level_info_enabled) {
        detail::print_pattern_info("INFO");
        std::wcout << fmt::format(std::forward<fmt::wformat_string<Args...>>(fmt), std::forward<Args>(args)...);
        std::wcout << L"\n";
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

inline void set_encode(const char* loc = "") {
    std::ios_base::sync_with_stdio(false);
    std::wcout.imbue(std::locale(loc));
}


} // namespace log

ANYTHING_NAMESPACE_END

#endif // ANYTHING_LOG_H_