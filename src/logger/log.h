// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LOG_H
#define LOG_H

#define G_LOG_USE_STRUCTURED
#include <glib.h>

/**
 * init_log:
 *
 * Initializes the custom logging system. This function should be called
 * once at application startup before any logging operations.
 *
 * The function sets up a custom log writer that formats messages with
 * timestamps, thread IDs, and source code location information.
 *
 * Thread Safety: This function is not thread-safe and should only be
 * called from the main thread during application initialization.
 */
void init_log(void);

/**
 * enable_debug_log:
 * @enable: %TRUE to enable debug logging, %FALSE to disable
 *
 * Controls whether DEBUG and INFO level messages are output.
 * When disabled, only WARNING, CRITICAL, and ERROR messages are shown.
 *
 * Thread Safety: This function is not currently thread-safe.
 * Consider using atomic operations for thread safety.
 */
void enable_debug_log(gboolean enable);

#endif // LOG_H