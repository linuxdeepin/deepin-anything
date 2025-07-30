// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_event_logger.c
 * @brief Comprehensive unit tests for EventLogger component
 *
 * This test suite provides complete coverage of the EventLogger API,
 * including normal operations, error conditions, and edge cases.
 * Uses GLib test framework for consistent test execution.
 */

#include "event_logger.h"
#include "datatype.h"
#include "vfs_change_consts.h"
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define TEST_OUTPUT_DIR "/tmp/event_logger_test"
#define TEST_TIMEOUT_MS 1000

/**
 * Test context structure for storing test state
 */
typedef struct {
    EventLogger *logger;
    GString *captured_output;
    GMutex output_mutex;
    GCond output_cond;
    gint expected_events;
    gint received_events;
    gboolean handler_called;
    gchar *last_log_content;
} TestContext;

/**
 * Test log handler that captures output for verification
 */
static void test_log_handler(gpointer user_data, const gchar *content)
{
    TestContext *ctx = (TestContext *)user_data;

    g_mutex_lock(&ctx->output_mutex);

    g_string_append(ctx->captured_output, content);
    ctx->handler_called = TRUE;
    ctx->received_events++;

    g_free(ctx->last_log_content);
    ctx->last_log_content = g_strdup(content);

    g_cond_signal(&ctx->output_cond);
    g_mutex_unlock(&ctx->output_mutex);
}

/**
 * Test log handler that always fails (for error testing)
 */
static void failing_log_handler(gpointer user_data, const gchar *content)
{
    g_test_message("Failing handler called with: %s", content);
    // Intentionally do nothing to simulate handler failure
}

/**
 * Setup function called before each test
 */
static void setup_test_context(TestContext *ctx, gconstpointer test_data)
{
    memset(ctx, 0, sizeof(TestContext));
    ctx->captured_output = g_string_new("");
    g_mutex_init(&ctx->output_mutex);
    g_cond_init(&ctx->output_cond);
    ctx->expected_events = 0;
    ctx->received_events = 0;
    ctx->handler_called = FALSE;
    ctx->last_log_content = NULL;

    // Create test directory
    g_mkdir_with_parents(TEST_OUTPUT_DIR, 0755);
}

/**
 * Teardown function called after each test
 */
static void teardown_test_context(TestContext *ctx, gconstpointer test_data)
{
    if (ctx->logger) {
        event_logger_stop(ctx->logger);
        event_logger_free(ctx->logger);
        ctx->logger = NULL;
    }

    g_string_free(ctx->captured_output, TRUE);
    g_mutex_clear(&ctx->output_mutex);
    g_cond_clear(&ctx->output_cond);
    g_free(ctx->last_log_content);

    // Cleanup test directory
    g_autofree gchar *cmd = g_strdup_printf("rm -rf %s", TEST_OUTPUT_DIR);
    g_spawn_command_line_sync(cmd, NULL, NULL, NULL, NULL);
}

/**
 * Helper function to create a test FileEvent
 */
static FileEvent *create_test_event(guint8 action, const gchar *event_path,
                                   const gchar *process_path, guint32 pid,
                                   guint32 uid, guint32 cookie)
{
    FileEvent *event = g_slice_new0(FileEvent);
    event->action = action;
    event->pid = pid;
    event->uid = uid;
    event->cookie = cookie;

    g_strlcpy(event->event_path, event_path, MAX_PATH_LEN);
    g_strlcpy(event->process_path, process_path, MAX_PATH_LEN);

    return event;
}

/**
 * Helper function to wait for expected events with timeout
 */
static gboolean wait_for_events(TestContext *ctx, gint expected_count, guint timeout_ms)
{
    gint64 end_time = g_get_monotonic_time() + timeout_ms * 1000;

    g_mutex_lock(&ctx->output_mutex);

    while (ctx->received_events < expected_count) {
        if (!g_cond_wait_until(&ctx->output_cond, &ctx->output_mutex, end_time)) {
            g_mutex_unlock(&ctx->output_mutex);
            return FALSE; // Timeout
        }
    }

    g_mutex_unlock(&ctx->output_mutex);
    return TRUE;
}

/**
 * Test: EventLogger creation and destruction
 */
static void test_event_logger_new_free(TestContext *ctx, gconstpointer test_data)
{
    // Test successful creation
    ctx->logger = event_logger_new(test_log_handler, ctx);
    g_assert_nonnull(ctx->logger);

    // Test free with valid logger
    event_logger_free(ctx->logger);
    ctx->logger = NULL;

    // Test free with NULL (should not crash)
    event_logger_free(NULL);
}

/**
 * Test: EventLogger start and stop functionality
 */
static void test_event_logger_start_stop(TestContext *ctx, gconstpointer test_data)
{
    ctx->logger = event_logger_new(test_log_handler, ctx);
    g_assert_nonnull(ctx->logger);

    // Test start
    gboolean started = event_logger_start(ctx->logger);
    g_assert_true(started);

    // Test double start (should fail)
    gboolean double_start = event_logger_start(ctx->logger);
    g_assert_false(double_start);

    // Test stop
    event_logger_stop(ctx->logger);

    // Test double stop (should be safe)
    event_logger_stop(ctx->logger);
}

/**
 * Test: Basic event logging functionality
 */
static void test_event_logger_basic_logging(TestContext *ctx, gconstpointer test_data)
{
    ctx->logger = event_logger_new(test_log_handler, ctx);
    g_assert_nonnull(ctx->logger);

    gboolean started = event_logger_start(ctx->logger);
    g_assert_true(started);

    // Create and log a simple event
    FileEvent *event = create_test_event(ACT_NEW_FILE, "/tmp/test.txt",
                                        "/usr/bin/touch", 1234, 1000, 0);

    event_logger_log_event(ctx->logger, event);

    // Wait for event to be processed
    gboolean received = wait_for_events(ctx, 1, TEST_TIMEOUT_MS);
    g_assert_true(received);

    // Verify output format
    g_assert_true(ctx->handler_called);
    g_assert_cmpint(ctx->received_events, ==, 1);

    // Check that output contains expected fields
    const gchar *output = ctx->captured_output->str;
    g_assert_nonnull(strstr(output, "/tmp/test.txt"));
    g_assert_nonnull(strstr(output, "/usr/bin/touch"));
    g_assert_nonnull(strstr(output, "1234"));
    g_assert_nonnull(strstr(output, "1000"));
}

/**
 * Test: CSV field escaping functionality
 */
static void test_csv_field_escaping(TestContext *ctx, gconstpointer test_data)
{
    ctx->logger = event_logger_new(test_log_handler, ctx);
    g_assert_nonnull(ctx->logger);

    gboolean started = event_logger_start(ctx->logger);
    g_assert_true(started);

    // Test event with characters that need escaping
    FileEvent *event = create_test_event(ACT_NEW_FILE, "/tmp/file,with\"commas.txt",
                                        "/usr/bin/test,\"process", 1234, 1000, 0);

    event_logger_log_event(ctx->logger, event);

    gboolean received = wait_for_events(ctx, 1, TEST_TIMEOUT_MS);
    g_assert_true(received);

    // Verify that fields with special characters are properly quoted
    const gchar *output = ctx->last_log_content;
    g_assert_nonnull(strstr(output, "\"/tmp/file,with\"\"commas.txt\""));
    g_assert_nonnull(strstr(output, "\"/usr/bin/test,\"\"process\""));
}

/**
 * Test: Rename event pairing functionality
 */
static void test_rename_event_pairing(TestContext *ctx, gconstpointer test_data)
{
    ctx->logger = event_logger_new(test_log_handler, ctx);
    g_assert_nonnull(ctx->logger);

    gboolean started = event_logger_start(ctx->logger);
    g_assert_true(started);

    // Create rename event pair with same cookie
    guint32 cookie = 12345;

    FileEvent *from_event = create_test_event(ACT_RENAME_FROM_FILE, "/tmp/old.txt",
                                            "/usr/bin/mv", 1234, 1000, cookie);
    FileEvent *to_event = create_test_event(ACT_RENAME_TO_FILE, "/tmp/new.txt",
                                          "/usr/bin/mv", 1234, 1000, cookie);

    // Submit both events
    event_logger_log_event(ctx->logger, from_event);
    event_logger_log_event(ctx->logger, to_event);

    // Should get exactly one output (paired event)
    gboolean received = wait_for_events(ctx, 1, TEST_TIMEOUT_MS);
    g_assert_true(received);
    g_assert_cmpint(ctx->received_events, ==, 1);

    // Verify output contains both paths
    const gchar *output = ctx->last_log_content;
    g_assert_nonnull(strstr(output, "/tmp/old.txt"));
    g_assert_nonnull(strstr(output, "/tmp/new.txt"));
}

/**
 * Test: Unpaired rename event handling
 */
static void test_unpaired_rename_events(TestContext *ctx, gconstpointer test_data)
{
    ctx->logger = event_logger_new(test_log_handler, ctx);
    g_assert_nonnull(ctx->logger);

    gboolean started = event_logger_start(ctx->logger);
    g_assert_true(started);

    // Submit only "to" event without matching "from" event
    FileEvent *to_event = create_test_event(ACT_RENAME_TO_FILE, "/tmp/orphan.txt",
                                          "/usr/bin/mv", 1234, 1000, 99999);

    event_logger_log_event(ctx->logger, to_event);

    // Wait a bit to ensure no output is generated
    g_usleep(100000); // 100ms
    g_assert_cmpint(ctx->received_events, ==, 0);

    // Now submit "from" event only
    FileEvent *from_event = create_test_event(ACT_RENAME_FROM_FILE, "/tmp/source.txt",
                                            "/usr/bin/mv", 1234, 1000, 88888);

    event_logger_log_event(ctx->logger, from_event);

    // Still should be no output
    g_usleep(100000); // 100ms
    g_assert_cmpint(ctx->received_events, ==, 0);
}

/**
 * Test: Multiple events processing
 */
static void test_multiple_events(TestContext *ctx, gconstpointer test_data)
{
    ctx->logger = event_logger_new(test_log_handler, ctx);
    g_assert_nonnull(ctx->logger);

    gboolean started = event_logger_start(ctx->logger);
    g_assert_true(started);

    const gint num_events = 10;

    // Submit multiple different events
    for (gint i = 0; i < num_events; i++) {
        g_autofree gchar *path = g_strdup_printf("/tmp/test_%d.txt", i);
        g_autofree gchar *process = g_strdup_printf("/usr/bin/test_%d", i);

        FileEvent *event = create_test_event(ACT_NEW_FILE, path, process,
                                           1000 + i, 500 + i, 0);
        event_logger_log_event(ctx->logger, event);
    }

    // Wait for all events to be processed
    gboolean received = wait_for_events(ctx, num_events, TEST_TIMEOUT_MS * 2);
    g_assert_true(received);
    g_assert_cmpint(ctx->received_events, ==, num_events);

    // Verify all events are in output
    for (gint i = 0; i < num_events; i++) {
        g_autofree gchar *expected_path = g_strdup_printf("/tmp/test_%d.txt", i);
        g_assert_nonnull(strstr(ctx->captured_output->str, expected_path));
    }
}

/**
 * Test: Event logging when logger is stopped
 */
static void test_logging_when_stopped(TestContext *ctx, gconstpointer test_data)
{
    ctx->logger = event_logger_new(test_log_handler, ctx);
    g_assert_nonnull(ctx->logger);

    // Don't start the logger

    FileEvent *event = create_test_event(ACT_NEW_FILE, "/tmp/stopped.txt",
                                        "/usr/bin/touch", 1234, 1000, 0);

    // This should not crash but event should be discarded
    event_logger_log_event(ctx->logger, event);

    // Verify no output was generated
    g_usleep(100000); // 100ms
    g_assert_false(ctx->handler_called);
    g_assert_cmpint(ctx->received_events, ==, 0);
}

/**
 * Test: Handler error resilience
 */
static void test_handler_error_resilience(TestContext *ctx, gconstpointer test_data)
{
    // Use a failing handler
    ctx->logger = event_logger_new(failing_log_handler, ctx);
    g_assert_nonnull(ctx->logger);

    gboolean started = event_logger_start(ctx->logger);
    g_assert_true(started);

    FileEvent *event = create_test_event(ACT_NEW_FILE, "/tmp/failing.txt",
                                        "/usr/bin/touch", 1234, 1000, 0);

    // This should not crash even if handler fails
    event_logger_log_event(ctx->logger, event);

    g_usleep(100000); // 100ms

    // Logger should still be functional
    event_logger_stop(ctx->logger);
}

/**
 * Test: Performance under load
 */
static void test_performance_under_load(TestContext *ctx, gconstpointer test_data)
{
    if (!g_test_perf()) {
        g_test_skip("Performance test skipped (not in performance mode)");
        return;
    }

    ctx->logger = event_logger_new(test_log_handler, ctx);
    g_assert_nonnull(ctx->logger);

    gboolean started = event_logger_start(ctx->logger);
    g_assert_true(started);

    const gint num_events = 1000;

    g_test_timer_start();

    // Submit many events quickly
    for (gint i = 0; i < num_events; i++) {
        g_autofree gchar *path = g_strdup_printf("/tmp/perf_%d.txt", i);

        FileEvent *event = create_test_event(ACT_NEW_FILE, path,
                                           "/usr/bin/test", 1000, 500, 0);
        event_logger_log_event(ctx->logger, event);
    }

    // Wait for all events to be processed
    gboolean received = wait_for_events(ctx, num_events, TEST_TIMEOUT_MS * 10);

    gdouble elapsed = g_test_timer_elapsed();

    g_assert_true(received);
    g_assert_cmpint(ctx->received_events, ==, num_events);

    g_test_message("Processed %d events in %.3f seconds (%.1f events/sec)",
                   num_events, elapsed, num_events / elapsed);

    // Performance should be reasonable
    g_assert_cmpfloat(elapsed, <, 5.0); // Should complete within 5 seconds
}

/**
 * Main test runner
 */
int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    // Set up test logging
    g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING);

    // Basic functionality tests
    g_test_add("/event_logger/new_free", TestContext, NULL,
               setup_test_context, test_event_logger_new_free, teardown_test_context);

    g_test_add("/event_logger/start_stop", TestContext, NULL,
               setup_test_context, test_event_logger_start_stop, teardown_test_context);

    g_test_add("/event_logger/basic_logging", TestContext, NULL,
               setup_test_context, test_event_logger_basic_logging, teardown_test_context);

    g_test_add("/event_logger/csv_escaping", TestContext, NULL,
               setup_test_context, test_csv_field_escaping, teardown_test_context);

    // Advanced functionality tests
    g_test_add("/event_logger/rename_pairing", TestContext, NULL,
               setup_test_context, test_rename_event_pairing, teardown_test_context);

    g_test_add("/event_logger/unpaired_rename", TestContext, NULL,
               setup_test_context, test_unpaired_rename_events, teardown_test_context);

    g_test_add("/event_logger/multiple_events", TestContext, NULL,
               setup_test_context, test_multiple_events, teardown_test_context);

    // Error handling tests
    g_test_add("/event_logger/logging_when_stopped", TestContext, NULL,
               setup_test_context, test_logging_when_stopped, teardown_test_context);

    g_test_add("/event_logger/handler_error_resilience", TestContext, NULL,
               setup_test_context, test_handler_error_resilience, teardown_test_context);

    // Performance tests
    g_test_add("/event_logger/performance", TestContext, NULL,
               setup_test_context, test_performance_under_load, teardown_test_context);

    return g_test_run();
}