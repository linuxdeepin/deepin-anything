// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef FILE_LOG_H
#define FILE_LOG_H

#define G_LOG_USE_STRUCTURED
#include <glib.h>

G_BEGIN_DECLS

/**
 * FileLogger:
 *
 * An opaque structure representing a file logging system with automatic
 * rotation and compression capabilities.
 *
 * The FileLogger provides:
 * - Automatic log file rotation based on size limits
 * - Compression of archived log files using gzip
 * - Thread-safe logging operations
 * - Configurable retention policy for old log files
 *
 * Since: 1.0
 */
typedef struct FileLogger FileLogger;

/**
 * file_logger_new:
 * @log_file_path: (type filename): The path to the main log file
 * @max_file_size: Maximum size of a single log file in bytes (must be > 0)
 * @max_file_count: Maximum number of archived log files to keep (must be > 0)
 *
 * Creates a new #FileLogger instance with the specified configuration.
 * The log directory will be created if it doesn't exist.
 * Archived log files are automatically compressed using gzip compression.
 *
 * Returns: (transfer full) (nullable): A new #FileLogger instance, or %NULL on failure
 *
 * Since: 1.0
 */
FileLogger *file_logger_new(const char *log_file_path, gsize max_file_size, gsize max_file_count);

/**
 * file_logger_free:
 * @logger: (nullable): A #FileLogger instance
 *
 * Frees the #FileLogger instance and all its associated resources.
 * This function safely handles %NULL input.
 *
 * Since: 1.0
 */
void file_logger_free(FileLogger *logger);

/**
 * file_logger_log:
 * @logger: A #FileLogger instance
 * @content: (type utf8): The text content to write to the log
 *
 * Writes a line of text content to the log file. The content is immediately
 * flushed to disk to ensure reliability for logging purposes.
 *
 * If the current log file size exceeds the configured maximum size,
 * this function will automatically trigger log rotation before writing
 * the new content.
 *
 * Since: 1.0
 */
void file_logger_log(FileLogger *logger, const char *content);

/**
 * file_logger_get_log_path:
 * @logger: A #FileLogger instance
 *
 * Gets the current log file path.
 *
 * Returns: (transfer none): The log file path, or %NULL if logger is invalid
 *
 * Since: 1.0
 */
const char *file_logger_get_log_path(FileLogger *logger);

/**
 * file_logger_get_current_size:
 * @logger: A #FileLogger instance
 *
 * Gets the current size of the active log file in bytes.
 *
 * Returns: The current file size in bytes, or 0 if logger is invalid
 *
 * Since: 1.0
 */
gsize file_logger_get_current_size(FileLogger *logger);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FileLogger, file_logger_free)

G_END_DECLS

#endif // FILE_LOG_H
