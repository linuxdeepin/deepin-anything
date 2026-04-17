// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// 本模块实现运行标志文件管理，用于检测守护进程的退出状态
//
// 工作流程：
// 1. 启动初始化：调用 set_volatile_index_dir() 设置目录
// 2. 检测上次状态：调用 detect_last_time_quit_status() 检查标志文件
// 3. 设置运行标志：调用 set_running_flag() 创建运行标志文件
// 4. 获取上次状态：调用 is_last_time_normal_quit() 获取退出状态
// 5. 正常退出前：调用 remove_running_flag() 移除运行标志文件

#include "utils/running_flag.h"

#include <cerrno>
#include <filesystem>
#include <glib/gstdio.h>
#include <mutex>
#include <spdlog/spdlog.h>

static std::string volatile_index_dir_;
static bool is_last_time_normal_quit_ = false;
static std::mutex flag_mutex_;
static const char RUNNING_FLAG_FILE[] = "running";

static std::string get_running_flag_path()
{
    return volatile_index_dir_ + "/" + RUNNING_FLAG_FILE;
}

static bool ensure_directory_exists()
{
    std::error_code ec;
    if (!std::filesystem::exists(volatile_index_dir_, ec)) {
        if (ec) {
            spdlog::error("Failed to check directory existence {}: {}",
                          volatile_index_dir_, ec.message());
            return false;
        }

        spdlog::info("Creating volatile index directory: {}", volatile_index_dir_);

        std::filesystem::create_directories(volatile_index_dir_, ec);
        if (ec) {
            spdlog::error("Failed to create directory {}: {}", volatile_index_dir_,
                          ec.message());
            return false;
        }

        spdlog::info("Created directory: {}", volatile_index_dir_);
    }

    return true;
}

void set_volatile_index_dir(const std::string &volatile_index_dir)
{
    g_return_if_fail(!volatile_index_dir.empty());

    std::lock_guard lock(flag_mutex_);
    volatile_index_dir_ = volatile_index_dir;
}

void detect_last_time_quit_status()
{
    std::lock_guard lock(flag_mutex_);

    if (!ensure_directory_exists()) {
        spdlog::warn("Volatile index directory check failed, assuming abnormal exit");
        is_last_time_normal_quit_ = false;
        return;
    }

    std::string running_flag_path = get_running_flag_path();

    if (g_file_test(running_flag_path.c_str(), G_FILE_TEST_EXISTS)) {
        // 标志文件存在，说明上次异常退出（崩溃或被终止）
        is_last_time_normal_quit_ = false;
        spdlog::warn("Running flag exists, last quit was abnormal");
    } else {
        // 标志文件不存在，说明上次正常退出
        is_last_time_normal_quit_ = true;
        spdlog::info("Running flag does not exist, last quit was normal");
    }
}

void set_running_flag(void)
{
    std::lock_guard lock(flag_mutex_);

    if (!ensure_directory_exists()) {
        spdlog::error("Volatile index directory check failed, cannot set running flag");
        return;
    }

    std::string running_flag_path = get_running_flag_path();

    if (g_file_test(running_flag_path.c_str(), G_FILE_TEST_EXISTS)) {
        spdlog::debug("Running flag already exists at {}, skipping", running_flag_path);
        return;
    }

    if (!g_file_set_contents(running_flag_path.c_str(), "", -1, nullptr)) {
        spdlog::warn("Failed to set running flag at {}: {}", running_flag_path,
                     strerror(errno));
    } else {
        spdlog::info("Running flag set successfully at {}", running_flag_path);
    }
}

bool is_last_time_normal_quit(void)
{
    std::lock_guard lock(flag_mutex_);
    return is_last_time_normal_quit_;
}

void remove_running_flag(void)
{
    std::lock_guard lock(flag_mutex_);

    std::string running_flag_path = get_running_flag_path();

    if (g_file_test(running_flag_path.c_str(), G_FILE_TEST_EXISTS)) {
        if (g_unlink(running_flag_path.c_str()) != 0) {
            spdlog::warn("Failed to remove running flag at {}: {}", running_flag_path,
                         strerror(errno));
        } else {
            spdlog::info("Running flag removed successfully from {}", running_flag_path);
        }
    } else {
        spdlog::debug("Running flag file does not exist at {}, skipping removal",
                      running_flag_path);
    }
}
