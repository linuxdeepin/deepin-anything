// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "file_log.h"
#include <gio/gio.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <string.h>

/**
 * struct FileLogger:
 * @log_file_path: The path to the log file.
 * @max_file_size: The maximum size of a single log file in bytes.
 * @max_file_count: The maximum number of rotated log files to keep.
 * @out_stream: The output stream for writing to the log file.
 *
 * The FileLogger struct is used to manage the state of a file logger.
 */
struct FileLogger {
    gchar *log_file_path;
    gsize max_file_size;
    gsize max_file_count;

    GFileOutputStream *out_stream;
    gsize current_file_size;
};

static gboolean rotate_logs(FileLogger *logger);
static gboolean compress_file(const gchar *path);

static gboolean open_log_file(FileLogger *logger)
{
    g_return_val_if_fail(logger != NULL, FALSE);

    GError *error = NULL;
    logger->current_file_size = 0;
    logger->out_stream = NULL;

    g_autoptr(GFile) log_file = g_file_new_for_path(logger->log_file_path);
    logger->out_stream = g_file_append_to(log_file, G_FILE_CREATE_NONE, NULL, &error);
    if (error) {
        g_warning("Failed to open log file: %s, %s", logger->log_file_path, error->message);
        g_error_free(error);
        return FALSE;
    }

    // check the log file size
    g_autoptr(GFileInfo) info = g_file_query_info(log_file, G_FILE_ATTRIBUTE_STANDARD_SIZE, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, &error);
    if (error) {
        g_warning("Failed to get file info: %s, %s", logger->log_file_path, error->message);
        g_error_free(error);
    } else {
        logger->current_file_size = g_file_info_get_size(info);
    }

    g_debug("Log file opened: %s", logger->log_file_path);
    return TRUE;
}

static void close_log_file(FileLogger *logger)
{
    if (!logger) {
        return;
    }

    if (logger->out_stream) {
        // g_output_stream_close is better for flushing buffers before unref
        g_output_stream_close(G_OUTPUT_STREAM(logger->out_stream), NULL, NULL);
        g_object_unref(logger->out_stream);
        logger->out_stream = NULL;
    }
}

/**
 * file_logger_new:
 * @log_file_path: The path to the log file.
 * @max_file_size: The maximum size of a single log file in bytes.
 * @max_file_count: The maximum number of old log files to keep.
 *
 * Creates a new #FileLogger logger instance. Archived log files will always be compressed.
 *
 * Returns: (transfer full): A new #FileLogger instance, or %NULL on failure.
 */
FileLogger *file_logger_new(const char *log_file_path, gsize max_file_size, gsize max_file_count)
{
    g_return_val_if_fail(log_file_path != NULL, NULL);
    g_return_val_if_fail(max_file_count > 0, NULL);
    g_return_val_if_fail(max_file_size > 0, NULL);

    // Create the log directory if it doesn't exist
    g_autofree gchar *log_dir = g_path_get_dirname(log_file_path);
    if (g_mkdir_with_parents(log_dir, 0755) != 0) {
        g_warning("Failed to create log directory: %s", log_dir);
        return NULL;
    }

    FileLogger *logger = g_new(FileLogger, 1);
    logger->log_file_path = g_strdup(log_file_path);
    logger->max_file_size = max_file_size;
    logger->max_file_count = max_file_count;

    if (!open_log_file(logger)) {
        file_logger_free(logger);
        return NULL;
    }

    return logger;
}

/**
 * file_logger_free:
 * @logger: A #FileLogger instance.
 *
 * Frees the #FileLogger instance and all its associated resources.
 */
void file_logger_free(FileLogger *logger)
{
    if (logger == NULL) {
        return;
    }
    close_log_file(logger);
    g_free(logger->log_file_path);
    g_free(logger);
}

/**
 * file_logger_log:
 * @logger: A #FileLogger instance.
 * @content: The text content to write to the log.
 *
 * Writes a line of text content to the log file.
 * If the current log file size exceeds `max_file_size`, this function will trigger a log rotation before writing new content.
 */
void file_logger_log(FileLogger *logger, const char *content)
{
    g_return_if_fail(logger != NULL);
    g_return_if_fail(content != NULL);
    g_return_if_fail(logger->log_file_path != NULL);

    if (logger->out_stream == NULL) {
        return;
    }

    // Check file size and rotate if necessary
    if (logger->current_file_size > logger->max_file_size) {
        if (!rotate_logs(logger)) {
            g_critical("Failed to rotate logs: %s", logger->log_file_path);
            return;
        }
    }

    GError *error = NULL;
    gsize content_len = strlen(content);
    gsize bytes_written = 0;
    if (!g_output_stream_write_all(G_OUTPUT_STREAM(logger->out_stream), content, content_len, &bytes_written, NULL, &error)) {
        g_warning("Failed to write to log file: %s, %s", logger->log_file_path, error->message);
        g_error_free(error);
        return;
    }

    // Force flush to ensure data is written to disk immediately for logging
    if (!g_output_stream_flush(G_OUTPUT_STREAM(logger->out_stream), NULL, &error)) {
        g_warning("Failed to flush log file: %s, %s", logger->log_file_path, error->message);
        g_error_free(error);
    }

    logger->current_file_size += bytes_written;
}

static gboolean compress_file(const gchar *path)
{
    g_return_val_if_fail(path != NULL, FALSE);

    g_autofree gchar *compressed_path = g_strdup_printf("%s.gz", path);
    GError *error = NULL;

    g_autoptr(GFile) src_file = g_file_new_for_path(path);
    g_autoptr(GFile) dest_file = g_file_new_for_path(compressed_path);

    g_autoptr(GFileInputStream) in_stream = g_file_read(src_file, NULL, &error);
    if (error) {
        g_warning("Failed to open source file for compression '%s': %s", path, error->message);
        g_error_free(error);
        return FALSE;
    }

    g_autoptr(GFileOutputStream) out_stream = g_file_replace(dest_file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, &error);
    if (error) {
        g_warning("Failed to create compressed file '%s': %s", compressed_path, error->message);
        g_error_free(error);
        return FALSE;
    }

    g_autoptr(GConverter) compressor = G_CONVERTER(g_zlib_compressor_new(G_ZLIB_COMPRESSOR_FORMAT_GZIP, -1));
    g_autoptr(GOutputStream) converting_stream = g_converter_output_stream_new(G_OUTPUT_STREAM(out_stream), compressor);

    if (g_output_stream_splice(G_OUTPUT_STREAM(converting_stream), G_INPUT_STREAM(in_stream), G_OUTPUT_STREAM_SPLICE_NONE, NULL, &error) == -1) {
        g_warning("Failed to compress file '%s': %s", path, error->message);
        g_error_free(error);
        g_output_stream_close(converting_stream, NULL, NULL);
        g_file_delete(dest_file, NULL, NULL);
        return FALSE;
    }

    g_output_stream_close(converting_stream, NULL, NULL);

    if (g_unlink(path) != 0) {
        g_warning("Failed to delete original log file '%s' after compression.", path);
    } else {
        g_debug("Log file compressed: %s", compressed_path);
    }

    return TRUE;
}

/**
 * rotate_logs:
 * @logger: A #FileLogger instance.
 *
 * Performs log rotation. This includes renaming old log files and creating space for new log messages.
 *
 * The rotation process is as follows (e.g., max_file_count = 3):
 * 1. Delete `log.2.gz`
 * 2. Rename `log.1.gz` to `log.2.gz`
 * 3. Rename `log.0.gz` to `log.1.gz`
 * 4. Rename `log` to `log.0`
 * 5. Compress `log.0` to `log.0.gz` and delete the original file
 */
static gboolean rotate_logs(FileLogger *logger)
{
    g_return_val_if_fail(logger != NULL, FALSE);

    g_message("Logs rotating...");

    close_log_file(logger);

    // Delete the old files left behind, Check up to 100 files
    for (gsize i = logger->max_file_count; i < 100; ++i) {
        g_autofree gchar *path = g_strdup_printf("%s.%" G_GSIZE_FORMAT ".gz", logger->log_file_path, i);
        if (g_file_test(path, G_FILE_TEST_EXISTS)) {
            if (g_unlink(path) != 0) {
                g_warning("Failed to delete old log file: %s", path);
            }
        } else {
            break;
        }
    }

    // Delete the oldest log file
    g_autofree gchar *oldest_path = g_strdup_printf("%s.%" G_GSIZE_FORMAT ".gz", logger->log_file_path, logger->max_file_count - 1);
    if (g_file_test(oldest_path, G_FILE_TEST_EXISTS)) {
        if (g_unlink(oldest_path) != 0) {
            g_warning("Failed to delete oldest compressed log file: %s", oldest_path);
            return FALSE;
        }
        g_debug("Oldest log file deleted successfully");
    }

    // Rotate intermediate compressed files
    for (gint i = logger->max_file_count - 2; i >= 0; --i) {
        g_autofree gchar *src_path = g_strdup_printf("%s.%d.gz", logger->log_file_path, i);
        if (g_file_test(src_path, G_FILE_TEST_EXISTS)) {
            g_autofree gchar *dest_path = g_strdup_printf("%s.%d.gz", logger->log_file_path, i + 1);
            g_debug("Rotating compressed log file: %s -> %s", src_path, dest_path);
            if (g_rename(src_path, dest_path) != 0) {
                g_warning("Failed to rename log file: %s to %s", src_path, dest_path);
                return FALSE;
            }
        }
    }

    // Rename current log file to the first rotated file
    if (g_file_test(logger->log_file_path, G_FILE_TEST_EXISTS)) {
        g_autofree gchar *dest_path = g_strdup_printf("%s.%d", logger->log_file_path, 0);
        g_debug("Rotating current log file: %s -> %s", logger->log_file_path, dest_path);
        if (g_rename(logger->log_file_path, dest_path) != 0) {
            g_warning("Failed to rename current log file: %s to %s", logger->log_file_path, dest_path);
            return FALSE;
        }
        if (!compress_file(dest_path)) {
            g_warning("Failed to compress log file: %s", dest_path);
            return FALSE;
        }
    }

    // Open a new log file for writing
    if (!open_log_file(logger)) {
        g_critical("Failed to open new log file: %s", logger->log_file_path);
        return FALSE;
    }

    return TRUE;
}

/**
 * file_logger_get_log_path:
 * @logger: A #FileLogger instance.
 *
 * Gets the current log file path.
 *
 * Returns: (transfer none): The log file path, or %NULL if logger is invalid
 */
const char *file_logger_get_log_path(FileLogger *logger)
{
    g_return_val_if_fail(logger != NULL, NULL);

    return logger->log_file_path;
}

/**
 * file_logger_get_current_size:
 * @logger: A #FileLogger instance.
 *
 * Gets the current size of the active log file in bytes.
 *
 * Returns: The current file size in bytes, or 0 if logger is invalid
 */
gsize file_logger_get_current_size(FileLogger *logger)
{
    g_return_val_if_fail(logger != NULL, 0);

    return logger->current_file_size;
}

