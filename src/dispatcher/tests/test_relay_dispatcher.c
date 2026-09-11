// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#define _GNU_SOURCE
#define G_LOG_USE_STRUCTURED
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "event_dispatcher.h"
#include "event_relay_dispatcher.h"
#include "event_relay_receiver.h"

/* ── Sender tests (no D-Bus needed) ────────────────────────────── */

static void test_relay_new_free(void)
{
    EventRelayDispatcher *d = event_relay_dispatcher_new(0);
    g_assert_nonnull(d);
    event_relay_dispatcher_free(d);
}

static void test_relay_new_free_null(void)
{
    event_relay_dispatcher_free(NULL);
}

static void test_relay_get_event_channel(void)
{
    EventRelayDispatcher *d = event_relay_dispatcher_new(0);
    g_assert_nonnull(d);

    int fd = event_relay_dispatcher_get_event_channel(d);
    g_assert_cmpint(fd, >=, 0);

    /* fd should be non-blocking */
    int flags = fcntl(fd, F_GETFL, 0);
    g_assert_true(flags & O_NONBLOCK);

    close(fd);
    event_relay_dispatcher_free(d);
}

static void test_relay_max_channel_limit(void)
{
    EventRelayDispatcher *d = event_relay_dispatcher_new(2);
    g_assert_nonnull(d);

    int fd1 = event_relay_dispatcher_get_event_channel(d);
    g_assert_cmpint(fd1, >=, 0);

    int fd2 = event_relay_dispatcher_get_event_channel(d);
    g_assert_cmpint(fd2, >=, 0);

    /* Third channel should be rejected (limit = 2) */
    int fd3 = event_relay_dispatcher_get_event_channel(d);
    g_assert_cmpint(fd3, ==, -1);

    close(fd1);
    close(fd2);
    event_relay_dispatcher_free(d);
}

static void test_relay_send_no_clients(void)
{
    EventRelayDispatcher *d = event_relay_dispatcher_new(0);
    g_assert_nonnull(d);

    dispatch_event_t event = {0};
    event.event_action = 1;
    g_strlcpy(event.event_path, "/test", sizeof(event.event_path));

    gboolean ret = event_relay_dispatcher_send(d, &event, sizeof(event));
    g_assert_true(ret);

    event_relay_dispatcher_free(d);
}

static void test_relay_send_receive(void)
{
    EventRelayDispatcher *d = event_relay_dispatcher_new(0);
    g_assert_nonnull(d);

    int client_fd = event_relay_dispatcher_get_event_channel(d);
    g_assert_cmpint(client_fd, >=, 0);

    /* Small delay to ensure socketpair is fully set up */
    usleep(10000);

    dispatch_event_t send_event = {0};
    send_event.event_action = 42;
    g_strlcpy(send_event.event_path, "/home/user/relay_test.txt",
              sizeof(send_event.event_path));

    gboolean ret = event_relay_dispatcher_send(d, &send_event, sizeof(send_event));
    g_assert_true(ret);

    /* Receive on the client end — use poll for non-blocking wait */
    struct pollfd pfd;
    pfd.fd = client_fd;
    pfd.events = POLLIN;
    g_assert_cmpint(poll(&pfd, 1, 5000), >, 0);
    g_assert_true(pfd.revents & POLLIN);

    dispatch_event_t recv_event = {0};
    ssize_t n = recv(client_fd, &recv_event, sizeof(recv_event), 0);
    g_assert_cmpint(n, ==, sizeof(recv_event));
    g_assert_cmpint(recv_event.event_action, ==, 42);
    g_assert_cmpstr(recv_event.event_path, ==, "/home/user/relay_test.txt");

    close(client_fd);
    event_relay_dispatcher_free(d);
}

static void test_relay_send_multiple_clients(void)
{
    EventRelayDispatcher *d = event_relay_dispatcher_new(0);
    g_assert_nonnull(d);

    int fd1 = event_relay_dispatcher_get_event_channel(d);
    g_assert_cmpint(fd1, >=, 0);
    int fd2 = event_relay_dispatcher_get_event_channel(d);
    g_assert_cmpint(fd2, >=, 0);

    usleep(10000);

    dispatch_event_t event = {0};
    event.event_action = 7;
    g_strlcpy(event.event_path, "/tmp/multi.txt", sizeof(event.event_path));

    gboolean ret = event_relay_dispatcher_send(d, &event, sizeof(event));
    g_assert_true(ret);

    /* Both clients should receive the event */
    for (int i = 0; i < 2; i++) {
        int fd = (i == 0) ? fd1 : fd2;
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        g_assert_cmpint(poll(&pfd, 1, 5000), >, 0);

        dispatch_event_t recv_event = {0};
        ssize_t n = recv(fd, &recv_event, sizeof(recv_event), 0);
        g_assert_cmpint(n, ==, sizeof(recv_event));
        g_assert_cmpint(recv_event.event_action, ==, 7);
        g_assert_cmpstr(recv_event.event_path, ==, "/tmp/multi.txt");
    }

    close(fd1);
    close(fd2);
    event_relay_dispatcher_free(d);
}

static void test_relay_cleanup_disconnected(void)
{
    EventRelayDispatcher *d = event_relay_dispatcher_new(0);
    g_assert_nonnull(d);

    int client_fd = event_relay_dispatcher_get_event_channel(d);
    g_assert_cmpint(client_fd, >=, 0);

    /* Close the client end — simulates disconnection */
    close(client_fd);

    /* Send should clean up the dead channel without crashing */
    dispatch_event_t event = {0};
    event.event_action = 1;
    g_strlcpy(event.event_path, "/test", sizeof(event.event_path));

    gboolean ret = event_relay_dispatcher_send(d, &event, sizeof(event));
    g_assert_true(ret);

    /* After cleanup, send again — should still work (empty list) */
    ret = event_relay_dispatcher_send(d, &event, sizeof(event));
    g_assert_true(ret);

    event_relay_dispatcher_free(d);
}

static void test_relay_send_partial_msg(void)
{
    /* Test sending a partial dispatch_event_t (only the header portion).
     * SOCK_SEQPACKET preserves message boundaries so recv gets exactly
     * what was sent. */
    EventRelayDispatcher *d = event_relay_dispatcher_new(0);
    g_assert_nonnull(d);

    int client_fd = event_relay_dispatcher_get_event_channel(d);
    g_assert_cmpint(client_fd, >=, 0);

    usleep(10000);

    /* Send only the first 8 bytes (event_action + cookie, no path) */
    struct {
        gint32 action;
        guint32 cookie;
    } partial = { .action = 99, .cookie = 42 };

    gboolean ret = event_relay_dispatcher_send(d, &partial, sizeof(partial));
    g_assert_true(ret);

    struct pollfd pfd;
    pfd.fd = client_fd;
    pfd.events = POLLIN;
    g_assert_cmpint(poll(&pfd, 1, 5000), >, 0);

    char buf[sizeof(partial)];
    ssize_t n = recv(client_fd, buf, sizeof(buf), 0);
    g_assert_cmpint(n, ==, sizeof(partial));

    gint32 *action = (gint32 *)buf;
    guint32 *cookie = (guint32 *)(buf + 4);
    g_assert_cmpint(*action, ==, 99);
    g_assert_cmpuint(*cookie, ==, 42);

    close(client_fd);
    event_relay_dispatcher_free(d);
}

/* ── Receiver tests (no D-Bus — test receive logic directly) ────── */

/* The receiver's new() requires a D-Bus service, which is not available
 * in unit tests. However, we can test the receive logic by creating a
 * socketpair, pretending one end is the receiver's fd, and calling
 * event_relay_receiver_receive() — but that requires constructing the
 * opaque struct. Since the struct is opaque, we test the receive logic
 * indirectly via the sender tests (which already exercise send→recv
 * on the socketpair). For the receiver D-Bus path, we add a test that
 * verifies event_relay_receiver_new returns NULL when no D-Bus service
 * is available. */

static void test_relay_receiver_new_no_dbus(void)
{
    /* With a non-existent bus name, the D-Bus call should fail and the
     * constructor should return NULL. We fork so that the g_warning
     * (which would be fatal in the parent under
     * g_log_set_always_fatal) only affects the child. */
    pid_t pid = fork();
    g_assert_cmpint(pid, >=, 0);

    if (pid == 0) {
        /* Child: disable fatal warnings so we can reach the return path
         * and verify it directly instead of relying on a signal abort. */
        g_log_set_always_fatal(0);

        EventRelayReceiver *r = event_relay_receiver_new(
            "com.test.NonExistentService",
            "/com/test/NonExistent",
            "com.test.NonExistentInterface");

        /* Constructor must return NULL on D-Bus failure. */
        event_relay_receiver_free(r);
        _exit(r == NULL ? 0 : 1);
    }

    /* Parent: require a normal exit with status 0. We do NOT accept
     * arbitrary signal termination as success — a crash (segfault,
     * abort from an unrelated cause) is a real bug and must fail the
     * test rather than being masked as a "expected fatal warning". */
    int status;
    waitpid(pid, &status, 0);

    g_assert_true(WIFEXITED(status));
    g_assert_cmpint(WEXITSTATUS(status), ==, 0);
}

/* ── End-to-end fork test ───────────────────────────────────────── */

static void test_relay_end_to_end_fork(void)
{
    EventRelayDispatcher *d = event_relay_dispatcher_new(0);
    g_assert_nonnull(d);

    int client_fd = event_relay_dispatcher_get_event_channel(d);
    g_assert_cmpint(client_fd, >=, 0);

    pid_t pid = fork();
    g_assert_cmpint(pid, >=, 0);

    if (pid == 0) {
        /* Child: receive the event */
        dispatch_event_t event = {0};

        struct pollfd pfd;
        pfd.fd = client_fd;
        pfd.events = POLLIN;

        int nfds = poll(&pfd, 1, 5000);
        if (nfds <= 0)
            _exit(1);

        ssize_t n = recv(client_fd, &event, sizeof(event), 0);
        if (n != sizeof(event))
            _exit(1);
        if (event.event_action != 55)
            _exit(2);
        if (strcmp(event.event_path, "/e2e/fork/test") != 0)
            _exit(3);

        close(client_fd);
        _exit(0);
    }

    /* Parent: send the event */
    usleep(10000);

    dispatch_event_t event = {0};
    event.event_action = 55;
    g_strlcpy(event.event_path, "/e2e/fork/test", sizeof(event.event_path));

    gboolean ret = event_relay_dispatcher_send(d, &event, sizeof(event));
    g_assert_true(ret);

    int status;
    waitpid(pid, &status, 0);
    g_assert_true(WIFEXITED(status));
    g_assert_cmpint(WEXITSTATUS(status), ==, 0);

    close(client_fd);
    event_relay_dispatcher_free(d);
}

/* ── Main ──────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING);

    g_test_add_func("/relay/new_free", test_relay_new_free);
    g_test_add_func("/relay/new_free_null", test_relay_new_free_null);
    g_test_add_func("/relay/get_event_channel", test_relay_get_event_channel);
    g_test_add_func("/relay/max_channel_limit", test_relay_max_channel_limit);
    g_test_add_func("/relay/send_no_clients", test_relay_send_no_clients);
    g_test_add_func("/relay/send_receive", test_relay_send_receive);
    g_test_add_func("/relay/send_multiple_clients", test_relay_send_multiple_clients);
    g_test_add_func("/relay/cleanup_disconnected", test_relay_cleanup_disconnected);
    g_test_add_func("/relay/send_partial_msg", test_relay_send_partial_msg);
    g_test_add_func("/relay/receiver_new_no_dbus", test_relay_receiver_new_no_dbus);
    g_test_add_func("/relay/end_to_end_fork", test_relay_end_to_end_fork);

    return g_test_run();
}
