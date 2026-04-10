// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef RUNNING_FLAG_H_
#define RUNNING_FLAG_H_

/**
 * @file running_flag.h
 * @brief 守护进程运行状态标志管理模块
 *
 * 本模块通过文件标志机制检测守护进程的上一次退出状态：
 * - 正常退出：运行标志文件被移除
 * - 异常退出（崩溃/被终止）：运行标志文件仍然存在
 *
 * 设计原理：
 * - 守护进程启动时调用 set_volatile_index_dir() 设置易失性索引目录路径
 * - 守护进程随后调用 detect_last_time_quit_status() 检测并上次的退出状态
 * - 守护进程然后调用 set_running_flag() 创建运行标志文件
 * - 守护进程运行中通过 is_last_time_normal_quit() 获取上次的退出状态
 * - 守护进程正常退出前调用 remove_running_flag() 移除运行标志文件
 */

#include <string>

/**
 * @brief 设置易失性索引目录路径
 * @param volatile_index_dir 易失性索引目录的完整路径
 *
 * 设置运行标志文件的目标目录。如果目录不存在，后续操作会自动创建。
 *
 * @note 必须在其他任何函数之前调用
 *
 * @param volatile_index_dir 目录路径，不能为空字符串
 */
void set_volatile_index_dir(const std::string &volatile_index_dir);

/**
 * @brief 检测守护进程上一次退出的状态
 *
 * 通过检查运行标志文件是否存在来判断上一次退出是否正常：
 * - 标志文件存在：上一次异常退出（崩溃或被终止）
 * - 标志文件不存在：上一次正常退出
 *
 * 检测结果缓存到内部状态，可通过 is_last_time_normal_quit() 获取。
 *
 * @note 必须先调用 set_volatile_index_dir() 设置目录路径
 */
void detect_last_time_quit_status(void);

/**
 * @brief 设置运行标志文件
 *
 * 在易失性索引目录中创建 "running" 文件，表示守护进程正在运行。
 * 如果目录不存在，会自动创建（包括父目录）。
 *
 * @note 必须先调用 set_volatile_index_dir() 设置目录路径
 */
void set_running_flag(void);

/**
 * @brief 获取上一次退出状态的检测结果
 * @return true 上一次正常退出，false 上一次异常退出
 *
 * 返回 detect_last_time_quit_status() 的检测结果。
 *
 * @note 必须先调用 detect_last_time_quit_status() 执行检测
 */
bool is_last_time_normal_quit(void);

/**
 * @brief 移除运行标志文件
 *
 * 删除 "running" 标志文件，表示守护进程正常退出。
 * 如果文件不存在，静默跳过不做任何操作。
 *
 * @note 应在守护进程正常退出前调用，而不是在异常处理中调用
 */
void remove_running_flag(void);

#endif // RUNNING_FLAG_H_
