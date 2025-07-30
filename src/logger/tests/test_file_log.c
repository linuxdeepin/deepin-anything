// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "file_log.h"
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define TEST_LOG_DIR "/tmp/file_log_test"
#define TEST_LOG_FILE TEST_LOG_DIR "/test.log"
#define TEST_MAX_SIZE 1024
#define TEST_MAX_COUNT 3

static void setup_test_environment(void)
{
    // Remove test directory if it exists
    g_autofree gchar *cmd = g_strdup_printf("rm -rf %s", TEST_LOG_DIR);
    g_spawn_command_line_sync(cmd, NULL, NULL, NULL, NULL);

    // Create test directory
    g_mkdir_with_parents(TEST_LOG_DIR, 0755);
}

static void cleanup_test_environment(void)
{
    g_autofree gchar *cmd = g_strdup_printf("rm -rf %s", TEST_LOG_DIR);
    g_spawn_command_line_sync(cmd, NULL, NULL, NULL, NULL);
}

static void test_file_logger_new_valid(void)
{
    setup_test_environment();

    g_autoptr(FileLogger) logger = file_logger_new(TEST_LOG_FILE, TEST_MAX_SIZE, TEST_MAX_COUNT);
    g_assert_nonnull(logger);

    // Check that the log file path is correctly set
    const char *path = file_logger_get_log_path(logger);
    g_assert_cmpstr(path, ==, TEST_LOG_FILE);

    // Check initial size
    gsize size = file_logger_get_current_size(logger);
    g_assert_cmpuint(size, ==, 0);

    cleanup_test_environment();
}

static void test_file_logger_basic_logging(void)
{
    setup_test_environment();

    g_autoptr(FileLogger) logger = file_logger_new(TEST_LOG_FILE, TEST_MAX_SIZE, TEST_MAX_COUNT);
    g_assert_nonnull(logger);

    const char *test_content = "Test log message\n";
    file_logger_log(logger, test_content);

    // Verify the content was written
    gsize expected_size = strlen(test_content);
    gsize actual_size = file_logger_get_current_size(logger);
    g_assert_cmpuint(actual_size, ==, expected_size);

    // Verify file exists and has content
    g_assert_true(g_file_test(TEST_LOG_FILE, G_FILE_TEST_EXISTS));

    g_autofree gchar *file_content = NULL;
    gsize file_length = 0;
    gboolean read_success = g_file_get_contents(TEST_LOG_FILE, &file_content, &file_length, NULL);
    g_assert_true(read_success);
    g_assert_cmpstr(file_content, ==, test_content);

    cleanup_test_environment();
}

static void test_file_logger_log_rotation(void)
{
    setup_test_environment();

    // Use small size to trigger rotation
    const gsize small_size = 50;
    g_autoptr(FileLogger) logger = file_logger_new(TEST_LOG_FILE, small_size, TEST_MAX_COUNT);
    g_assert_nonnull(logger);

    // Write enough content to trigger rotation
    const char *long_message = "This is a long test message that should trigger log rotation when written multiple times.\n";

    // Write messages until rotation occurs
    gsize total_written = 0;
    for (int i = 0; i < 10; i++) {
        file_logger_log(logger, long_message);
        total_written += strlen(long_message);

        // Check if rotation occurred
        if (g_file_test(TEST_LOG_FILE ".0.gz", G_FILE_TEST_EXISTS)) {
            break;
        }
    }

    // Verify that rotation occurred
    g_assert_true(g_file_test(TEST_LOG_FILE ".0.gz", G_FILE_TEST_EXISTS));

    cleanup_test_environment();
}

static void test_file_logger_null_safety(void)
{
    // Test file_logger_free with NULL
    file_logger_free(NULL); // Should not crash
}

static void test_file_logger_getter_functions(void)
{
    setup_test_environment();

    g_autoptr(FileLogger) logger = file_logger_new(TEST_LOG_FILE, TEST_MAX_SIZE, TEST_MAX_COUNT);
    g_assert_nonnull(logger);

    // Test get_log_path
    const char *path = file_logger_get_log_path(logger);
    g_assert_cmpstr(path, ==, TEST_LOG_FILE);

    // Test get_current_size
    gsize initial_size = file_logger_get_current_size(logger);
    g_assert_cmpuint(initial_size, ==, 0);

    // Write some content and check size
    const char *content = "test content\n";
    file_logger_log(logger, content);

    gsize new_size = file_logger_get_current_size(logger);
    g_assert_cmpuint(new_size, ==, strlen(content));

    cleanup_test_environment();
}

static void test_file_logger_directory_creation(void)
{
    const char *nested_path = TEST_LOG_DIR "/nested/deep/test.log";

    // Ensure the nested directory doesn't exist
    g_autofree gchar *cmd = g_strdup_printf("rm -rf %s/nested", TEST_LOG_DIR);
    g_spawn_command_line_sync(cmd, NULL, NULL, NULL, NULL);

    // Create logger with nested path
    g_autoptr(FileLogger) logger = file_logger_new(nested_path, TEST_MAX_SIZE, TEST_MAX_COUNT);
    g_assert_nonnull(logger);

    // Verify directory was created
    g_assert_true(g_file_test(TEST_LOG_DIR "/nested/deep", G_FILE_TEST_IS_DIR));

    cleanup_test_environment();
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    // Add test cases
    g_test_add_func("/file_log/new_valid", test_file_logger_new_valid);
    g_test_add_func("/file_log/basic_logging", test_file_logger_basic_logging);
    g_test_add_func("/file_log/log_rotation", test_file_logger_log_rotation);
    g_test_add_func("/file_log/null_safety", test_file_logger_null_safety);
    g_test_add_func("/file_log/getter_functions", test_file_logger_getter_functions);
    g_test_add_func("/file_log/directory_creation", test_file_logger_directory_creation);

    return g_test_run();
}