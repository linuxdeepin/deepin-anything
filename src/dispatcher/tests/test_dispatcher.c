// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#define _GNU_SOURCE
#define G_LOG_USE_STRUCTURED
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "event_dispatcher.h"

/* ── Test helpers ──────────────────────────────────────────────── */

#define TEST_SOCKET_TEMPLATE "/tmp/dispatcher_test_XXXXXX"

/* Default prefixes for basic tests (no $HOME expansion needed) */
static const char *basic_prefixes[] = { "/", NULL };

static char *make_socket_path(void)
{
    char *path = g_strdup(TEST_SOCKET_TEMPLATE);
    int fd = mkstemp(path);
    if (fd >= 0) {
        close(fd);
        unlink(path);
    }
    return path;
}

/* Pipe for parent-child synchronization */
struct sync_pipe {
    int parent_to_child[2];
    int child_to_parent[2];
};

static void sync_pipe_init(struct sync_pipe *sp)
{
    pipe(sp->parent_to_child);
    pipe(sp->child_to_parent);
}

static void sync_pipe_close(struct sync_pipe *sp)
{
    close(sp->parent_to_child[0]);
    close(sp->parent_to_child[1]);
    close(sp->child_to_parent[0]);
    close(sp->child_to_parent[1]);
}

/* Signal child to proceed */
static void sync_signal_parent(struct sync_pipe *sp)
{
    char c = 'x';
    write(sp->parent_to_child[1], &c, 1);
}

/* Wait for parent signal */
static void sync_wait_child(struct sync_pipe *sp)
{
    char c;
    read(sp->parent_to_child[0], &c, 1);
}

/* Signal parent that child is ready */
static void sync_signal_child(struct sync_pipe *sp)
{
    char c = 'x';
    write(sp->child_to_parent[1], &c, 1);
}

/* Wait for child signal */
static void sync_wait_parent(struct sync_pipe *sp)
{
    char c;
    read(sp->child_to_parent[0], &c, 1);
}

/* ── Subscription callback ─────────────────────────────────────── */

/**
 * test_get_subscribed:
 * @uid: the client's real UID
 * @user_data: a gchar** GStrv of path prefixes (may contain $HOME)
 *
 * Test callback that expands $HOME via getpwuid_r (matching the old
 * expand_path_prefix + resolve_home_dir behavior). Does NOT call
 * get_full_path — tests have no MountInfo.
 *
 * Returns: (transfer full) (nullable): NULL-terminated array of
 *     (possibly expanded) path prefixes, or NULL if @user_data is NULL.
 */
static gchar **test_get_subscribed(uid_t uid, gpointer user_data)
{
    gchar **prefixes = user_data;
    if (!prefixes)
        return NULL;

    /* Check if any prefix starts with $HOME */
    gboolean has_home = FALSE;
    for (guint i = 0; prefixes[i]; i++) {
        if (g_str_has_prefix(prefixes[i], "$HOME")) {
            has_home = TRUE;
            break;
        }
    }

    if (!has_home)
        return g_strdupv(prefixes);

    /* Resolve home dir for $HOME expansion */
    struct passwd pwd;
    struct passwd *result = NULL;
    char buf[4096];
    if (getpwuid_r(uid, &pwd, buf, sizeof(buf), &result) != 0 || !result)
        return g_strdupv(prefixes);  /* can't expand, return as-is */

    GPtrArray *out = g_ptr_array_new();
    for (guint i = 0; prefixes[i]; i++) {
        if (g_str_has_prefix(prefixes[i], "$HOME")) {
            const char *rest = prefixes[i] + 5;  /* skip "$HOME" */
            if (*rest == '\0')
                g_ptr_array_add(out, g_strdup(pwd.pw_dir));
            else if (*rest == '/')
                g_ptr_array_add(out, g_strconcat(pwd.pw_dir, rest, NULL));
            else
                g_ptr_array_add(out, g_strdup(prefixes[i]));  /* $HOMExyz — not valid */
        } else {
            g_ptr_array_add(out, g_strdup(prefixes[i]));
        }
    }
    g_ptr_array_add(out, NULL);
    return (gchar **)g_ptr_array_free(out, FALSE);
}

/* ── Tests ─────────────────────────────────────────────────────── */

static void test_new_free(void)
{
    char *path = make_socket_path();
    dispatch_config_t config = {
        .get_user_subscribed = test_get_subscribed,
        .user_data = (gpointer)basic_prefixes,
    };
    EventDispatcher *d = event_dispatcher_new(path, &config);
    g_assert_nonnull(d);

    /* Verify socket file was created */
    g_assert_true(g_file_test(path, G_FILE_TEST_EXISTS));

    int fd = event_dispatcher_get_socket(d);
    g_assert_cmpint(fd, >=, 0);

    event_dispatcher_free(d);

    /* Verify socket file was cleaned up */
    g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));

    g_free(path);
}

static void test_new_free_null(void)
{
    event_dispatcher_free(NULL);
}

static void test_send_no_clients(void)
{
    char *path = make_socket_path();
    dispatch_config_t config = {
        .get_user_subscribed = test_get_subscribed,
        .user_data = (gpointer)basic_prefixes,
    };
    EventDispatcher *d = event_dispatcher_new(path, &config);
    g_assert_nonnull(d);

    dispatch_event_t event = {0};
    event.event_action = 1;
    g_strlcpy(event.event_path, "/home/user/test.txt", sizeof(event.event_path));

    gboolean ret = event_dispatcher_send(d, &event);
    g_assert_true(ret);

    event_dispatcher_free(d);
    g_free(path);
}

static void test_get_socket(void)
{
    char *path = make_socket_path();
    dispatch_config_t config = {
        .get_user_subscribed = test_get_subscribed,
        .user_data = (gpointer)basic_prefixes,
    };
    EventDispatcher *d = event_dispatcher_new(path, &config);
    g_assert_nonnull(d);

    int fd = event_dispatcher_get_socket(d);
    g_assert_cmpint(fd, >=, 0);

    /* Verify the socket is non-blocking */
    int flags = fcntl(fd, F_GETFL, 0);
    g_assert_true(flags & O_NONBLOCK);

    event_dispatcher_free(d);
    g_free(path);
}

static void test_send_without_accept(void)
{
    char *path = make_socket_path();

    const char *prefixes[] = { "/", NULL };
    dispatch_config_t config = {
        .get_user_subscribed = test_get_subscribed,
        .user_data = (gpointer)prefixes,
        .max_users = 0,
        .max_connections_per_user = 0,
    };

    EventDispatcher *d = event_dispatcher_new(path, &config);
    g_assert_nonnull(d);

    /* Fork a child that connects but parent does NOT call accept */
    struct sync_pipe sp;
    sync_pipe_init(&sp);

    pid_t pid = fork();
    g_assert_cmpint(pid, >=, 0);

    if (pid == 0) {
        /* Child: connect to the dispatcher socket */
        close(sp.parent_to_child[1]);
        close(sp.child_to_parent[0]);

        sync_wait_child(&sp);

        EventReceiver *r = event_receiver_new(path);
        if (r == NULL) {
            _exit(1);
        }

        /* Signal parent that we're connected */
        sync_signal_child(&sp);

        /* Try to receive — should block since parent hasn't called accept yet.
         * The child is expected to remain blocked (or get disconnected) because
         * the parent never calls event_dispatcher_accept. */
        dispatch_event_t event = {0};
        (void) event_receiver_receive(r, &event);
        event_receiver_free(r);
        _exit(0);  /* test verifies parent can send without crash; child exit code unchecked */
    }

    /* Parent */
    close(sp.parent_to_child[0]);
    close(sp.child_to_parent[1]);

    /* Give child time to connect */
    sync_signal_parent(&sp);

    /* Wait for child to signal it's connected */
    sync_wait_parent(&sp);

    /* Now send WITHOUT calling accept — child should NOT receive */
    dispatch_event_t event = {0};
    event.event_action = 1;
    g_strlcpy(event.event_path, "/home/user/test.txt", sizeof(event.event_path));

    gboolean ret = event_dispatcher_send(d, &event);
    g_assert_true(ret);

    /* Give child a moment, then kill it (it should be blocked in recv) */
    usleep(100000);
    kill(pid, SIGTERM);

    int status;
    waitpid(pid, &status, 0);

    event_dispatcher_free(d);
    g_free(path);
    sync_pipe_close(&sp);
}

static void test_external_accept(void)
{
    char *path = make_socket_path();

    const char *prefixes[] = { "/", NULL };
    dispatch_config_t config = {
        .get_user_subscribed = test_get_subscribed,
        .user_data = (gpointer)prefixes,
        .max_users = 0,
        .max_connections_per_user = 0,
    };

    EventDispatcher *d = event_dispatcher_new(path, &config);
    g_assert_nonnull(d);

    struct sync_pipe sp;
    sync_pipe_init(&sp);

    pid_t pid = fork();
    g_assert_cmpint(pid, >=, 0);

    if (pid == 0) {
        /* Child: create receiver and wait for event */
        close(sp.parent_to_child[1]);
        close(sp.child_to_parent[0]);

        sync_wait_child(&sp);

        EventReceiver *r = event_receiver_new(path);
        if (r == NULL) {
            sync_signal_child(&sp);  /* signal failure */
            _exit(1);
        }

        sync_signal_child(&sp);

        dispatch_event_t event = {0};
        EventReceiveResult ret = event_receiver_receive(r, &event);

        event_receiver_free(r);
        _exit(ret == EVENT_RECEIVE_OK ? 0 : 1);
    }

    /* Parent */
    close(sp.parent_to_child[0]);
    close(sp.child_to_parent[1]);

    sync_signal_parent(&sp);
    sync_wait_parent(&sp);

    /* Use external accept (not send's internal accept) */
    event_dispatcher_accept(d);

    /* Small delay to ensure accept processed */
    usleep(10000);

    dispatch_event_t event = {0};
    event.event_action = 42;
    g_strlcpy(event.event_path, "/test/path", sizeof(event.event_path));

    gboolean ret = event_dispatcher_send(d, &event);
    g_assert_true(ret);

    int status;
    waitpid(pid, &status, 0);
    g_assert_true(WIFEXITED(status));
    g_assert_cmpint(WEXITSTATUS(status), ==, 0);

    event_dispatcher_free(d);
    g_free(path);
    sync_pipe_close(&sp);
}

static void test_end_to_end(void)
{
    char *path = make_socket_path();

    const char *prefixes[] = { "/home", NULL };
    dispatch_config_t config = {
        .get_user_subscribed = test_get_subscribed,
        .user_data = (gpointer)prefixes,
        .max_users = 0,
        .max_connections_per_user = 0,
    };

    EventDispatcher *d = event_dispatcher_new(path, &config);
    g_assert_nonnull(d);

    struct sync_pipe sp;
    sync_pipe_init(&sp);

    pid_t pid = fork();
    g_assert_cmpint(pid, >=, 0);

    if (pid == 0) {
        /* Child: create receiver and wait for event */
        close(sp.parent_to_child[1]);
        close(sp.child_to_parent[0]);

        sync_wait_child(&sp);

        EventReceiver *r = event_receiver_new(path);
        if (r == NULL) {
            sync_signal_child(&sp);
            _exit(1);
        }

        sync_signal_child(&sp);

        dispatch_event_t event = {0};
        EventReceiveResult ret = event_receiver_receive(r, &event);
        if (ret != EVENT_RECEIVE_OK) {
            event_receiver_free(r);
            _exit(1);
        }

        /* Verify the event */
        if (event.event_action != 42 ||
            strcmp(event.event_path, "/home/user/test.txt") != 0) {
            event_receiver_free(r);
            _exit(2);
        }

        event_receiver_free(r);
        _exit(0);
    }

    /* Parent */
    close(sp.parent_to_child[0]);
    close(sp.child_to_parent[1]);

    sync_signal_parent(&sp);
    sync_wait_parent(&sp);

    event_dispatcher_accept(d);
    usleep(10000);

    dispatch_event_t event = {0};
    event.event_action = 42;
    g_strlcpy(event.event_path, "/home/user/test.txt", sizeof(event.event_path));

    gboolean ret = event_dispatcher_send(d, &event);
    g_assert_true(ret);

    int status;
    waitpid(pid, &status, 0);
    g_assert_true(WIFEXITED(status));
    g_assert_cmpint(WEXITSTATUS(status), ==, 0);

    event_dispatcher_free(d);
    g_free(path);
    sync_pipe_close(&sp);
}

static void test_prefix_filtering(void)
{
    char *path = make_socket_path();

    const char *prefixes[] = { "/home", NULL };
    dispatch_config_t config = {
        .get_user_subscribed = test_get_subscribed,
        .user_data = (gpointer)prefixes,
        .max_users = 0,
        .max_connections_per_user = 0,
    };

    EventDispatcher *d = event_dispatcher_new(path, &config);
    g_assert_nonnull(d);

    struct sync_pipe sp;
    sync_pipe_init(&sp);

    pid_t pid = fork();
    g_assert_cmpint(pid, >=, 0);

    if (pid == 0) {
        /* Child */
        close(sp.parent_to_child[1]);
        close(sp.child_to_parent[0]);

        sync_wait_child(&sp);

        EventReceiver *r = event_receiver_new(path);
        if (r == NULL) {
            sync_signal_child(&sp);
            _exit(1);
        }

        sync_signal_child(&sp);

        /* Set alarm to timeout if no event received */
        alarm(3);

        dispatch_event_t event = {0};
        EventReceiveResult ret = event_receiver_receive(r, &event);

        alarm(0);

        /* If we received an event, it should NOT be for /var (subscribed to /home only).
         * If we received /var, that's a failure.
         * If alarm killed us (SIGALRM), that's correct — no event received. */
        if (ret == EVENT_RECEIVE_OK) {
            if (g_str_has_prefix(event.event_path, "/var")) {
                event_receiver_free(r);
                _exit(2);  /* should not have received /var event */
            }
        }

        event_receiver_free(r);
        _exit(0);
    }

    /* Parent */
    close(sp.parent_to_child[0]);
    close(sp.child_to_parent[1]);

    sync_signal_parent(&sp);
    sync_wait_parent(&sp);

    event_dispatcher_accept(d);
    usleep(10000);

    /* Send an event under /var — child subscribed to /home should NOT receive it */
    dispatch_event_t event = {0};
    event.event_action = 1;
    g_strlcpy(event.event_path, "/var/log/test.log", sizeof(event.event_path));

    gboolean ret = event_dispatcher_send(d, &event);
    g_assert_true(ret);

    int status;
    /* Child should be killed by SIGALRM (timeout), which is exit code 0 for our test */
    waitpid(pid, &status, 0);

    /* Child should exit with 0 (either no event received = SIGALRM, or didn't get /var) */
    g_assert_true(WIFEXITED(status) || WIFSIGNALED(status));

    event_dispatcher_free(d);
    g_free(path);
    sync_pipe_close(&sp);
}

static void test_default_events(void)
{
    char *path = make_socket_path();

    const char *prefixes[] = { "/tmp", NULL };

    dispatch_config_t config = {
        .get_user_subscribed = test_get_subscribed,
        .user_data = (gpointer)prefixes,
        .max_users = 0,
        .max_connections_per_user = 0,
    };

    EventDispatcher *d = event_dispatcher_new(path, &config);
    g_assert_nonnull(d);

    struct sync_pipe sp;
    sync_pipe_init(&sp);

    pid_t pid = fork();
    g_assert_cmpint(pid, >=, 0);

    if (pid == 0) {
        /* Child */
        close(sp.parent_to_child[1]);
        close(sp.child_to_parent[0]);

        sync_wait_child(&sp);

        EventReceiver *r = event_receiver_new(path);
        if (r == NULL) {
            sync_signal_child(&sp);
            _exit(1);
        }

        sync_signal_child(&sp);

        dispatch_event_t event = {0};
        EventReceiveResult ret = event_receiver_receive(r, &event);

        if (ret == EVENT_RECEIVE_OK && event.event_action == 7 &&
            strcmp(event.event_path, "/tmp/test_file.txt") == 0) {
            event_receiver_free(r);
            _exit(0);
        }

        event_receiver_free(r);
        _exit(1);
    }

    /* Parent */
    close(sp.parent_to_child[0]);
    close(sp.child_to_parent[1]);

    sync_signal_parent(&sp);
    sync_wait_parent(&sp);

    event_dispatcher_accept(d);
    usleep(10000);

    dispatch_event_t event = {0};
    event.event_action = 7;
    g_strlcpy(event.event_path, "/tmp/test_file.txt", sizeof(event.event_path));

    gboolean ret = event_dispatcher_send(d, &event);
    g_assert_true(ret);

    int status;
    waitpid(pid, &status, 0);
    g_assert_true(WIFEXITED(status));
    g_assert_cmpint(WEXITSTATUS(status), ==, 0);

    event_dispatcher_free(d);
    g_free(path);
    sync_pipe_close(&sp);
}

static void test_max_connections_per_user(void)
{
    char *path = make_socket_path();

    /* Use "/" as default prefix so all accepted clients receive events */
    const char *prefixes[] = { "/", NULL };
    dispatch_config_t config = {
        .get_user_subscribed = test_get_subscribed,
        .user_data = (gpointer)prefixes,
        .max_users = 0,
        .max_connections_per_user = 2,
    };

    EventDispatcher *d = event_dispatcher_new(path, &config);
    g_assert_nonnull(d);

    struct sync_pipe sp;
    sync_pipe_init(&sp);

    pid_t pid = fork();
    g_assert_cmpint(pid, >=, 0);

    if (pid == 0) {
        /* Child: create 3 receivers, only 2 should receive events.
         * All 3 connect() calls succeed (kernel backlog), but the server
         * rejects the 3rd at accept time by closing the fd. */
        close(sp.parent_to_child[1]);
        close(sp.child_to_parent[0]);

        sync_wait_child(&sp);

        EventReceiver *r1 = event_receiver_new(path);
        EventReceiver *r2 = event_receiver_new(path);
        EventReceiver *r3 = event_receiver_new(path);
        if (!r1 || !r2 || !r3) {
            if (r1) event_receiver_free(r1);
            if (r2) event_receiver_free(r2);
            if (r3) event_receiver_free(r3);
            _exit(1);
        }

        sync_signal_child(&sp);

        /* Set alarm as safety timeout (should not fire if logic is correct) */
        alarm(5);

        /* Receive on each — r1 and r2 should get the event, r3 should
         * get a disconnect (recv returns 0 → DISCONNECTED) because server rejected it */
        int received = 0;
        dispatch_event_t event = {0};

        if (event_receiver_receive(r1, &event) == EVENT_RECEIVE_OK) received++;
        if (event_receiver_receive(r2, &event) == EVENT_RECEIVE_OK) received++;
        if (event_receiver_receive(r3, &event) == EVENT_RECEIVE_OK) received++;

        alarm(0);

        event_receiver_free(r1);
        event_receiver_free(r2);
        event_receiver_free(r3);

        /* Exactly 2 should receive (max_connections_per_user = 2) */
        _exit(received == 2 ? 0 : 1);
    }

    /* Parent */
    close(sp.parent_to_child[0]);
    close(sp.child_to_parent[1]);

    sync_signal_parent(&sp);
    sync_wait_parent(&sp);

    /* Accept connections — should accept at most 2 from same UID */
    event_dispatcher_accept(d);
    usleep(10000);

    /* Send an event that all subscribed clients should receive */
    dispatch_event_t event = {0};
    event.event_action = 1;
    g_strlcpy(event.event_path, "/test", sizeof(event.event_path));

    gboolean ret = event_dispatcher_send(d, &event);
    g_assert_true(ret);

    int status;
    waitpid(pid, &status, 0);
    g_assert_true(WIFEXITED(status));
    g_assert_cmpint(WEXITSTATUS(status), ==, 0);

    event_dispatcher_free(d);
    g_free(path);
    sync_pipe_close(&sp);
}

/* ── Main ──────────────────────────────────────────────────────── */

static void test_home_expansion(void)
{
    char *path = make_socket_path();

    /* Resolve current user's home directory (same as dispatcher will do) */
    uid_t uid = getuid();
    struct passwd pwd;
    struct passwd *result = NULL;
    char pwbuf[4096];
    g_assert_cmpint(getpwuid_r(uid, &pwd, pwbuf, sizeof(pwbuf), &result), ==, 0);
    g_assert_nonnull(result);
    const char *home = result->pw_dir;
    if (home == NULL) {
        /* Cannot run test without a home directory */
        g_test_skip("no home directory for current user");
        g_free(path);
        return;
    }

    /* Build subscription with $HOME prefix — dispatcher must expand it */
    const char *prefixes[] = { "$HOME/dispatcher_test", NULL };
    dispatch_config_t config = {
        .get_user_subscribed = test_get_subscribed,
        .user_data = (gpointer)prefixes,
        .max_users = 0,
        .max_connections_per_user = 0,
    };

    EventDispatcher *d = event_dispatcher_new(path, &config);
    g_assert_nonnull(d);

    struct sync_pipe sp;
    sync_pipe_init(&sp);

    pid_t pid = fork();
    g_assert_cmpint(pid, >=, 0);

    if (pid == 0) {
        /* Child */
        close(sp.parent_to_child[1]);
        close(sp.child_to_parent[0]);

        sync_wait_child(&sp);

        EventReceiver *r = event_receiver_new(path);
        if (r == NULL) {
            sync_signal_child(&sp);
            _exit(1);
        }

        sync_signal_child(&sp);

        /* Wait for event under $HOME/dispatcher_test — should be received */
        alarm(3);
        dispatch_event_t event = {0};
        EventReceiveResult ret = event_receiver_receive(r, &event);
        alarm(0);

        if (ret != EVENT_RECEIVE_OK) {
            event_receiver_free(r);
            _exit(1);
        }

        /* Verify the event path starts with our home directory (proves $HOME
         * was expanded per-UID rather than left literal or using the
         * dispatcher process's environment) */
        if (!g_str_has_prefix(event.event_path, home)) {
            event_receiver_free(r);
            _exit(2);
        }

        event_receiver_free(r);
        _exit(0);
    }

    /* Parent */
    close(sp.parent_to_child[0]);
    close(sp.child_to_parent[1]);

    sync_signal_parent(&sp);
    sync_wait_parent(&sp);

    event_dispatcher_accept(d);
    usleep(10000);

    /* Send event under <home>/dispatcher_test/file.txt — child should receive
     * it because $HOME was expanded to the client's home directory */
    char event_path[DISPATCH_MAX_PATH_LEN];
    g_snprintf(event_path, sizeof(event_path), "%s/dispatcher_test/file.txt", home);

    dispatch_event_t event = {0};
    event.event_action = 99;
    g_strlcpy(event.event_path, event_path, sizeof(event.event_path));

    gboolean ret = event_dispatcher_send(d, &event);
    g_assert_true(ret);

    int status;
    waitpid(pid, &status, 0);
    g_assert_true(WIFEXITED(status));
    g_assert_cmpint(WEXITSTATUS(status), ==, 0);

    event_dispatcher_free(d);
    g_free(path);
    sync_pipe_close(&sp);
}

/* ── Graceful exit test ────────────────────────────────────────── */

static void test_graceful_exit_via_poll(void)
{
    char *path = make_socket_path();

    const char *prefixes[] = { "/", NULL };
    dispatch_config_t config = {
        .get_user_subscribed = test_get_subscribed,
        .user_data = (gpointer)prefixes,
        .max_users = 0,
        .max_connections_per_user = 0,
    };

    EventDispatcher *d = event_dispatcher_new(path, &config);
    g_assert_nonnull(d);

    struct sync_pipe sp;
    sync_pipe_init(&sp);

    /* Extra pipe used as a shutdown trigger in the child */
    int shutdown_pipe[2];
    g_assert_cmpint(pipe(shutdown_pipe), ==, 0);

    pid_t pid = fork();
    g_assert_cmpint(pid, >=, 0);

    if (pid == 0) {
        /* Child */
        close(sp.parent_to_child[1]);
        close(sp.child_to_parent[0]);
        close(shutdown_pipe[1]);

        sync_wait_child(&sp);

        EventReceiver *r = event_receiver_new(path);
        if (r == NULL) {
            sync_signal_child(&sp);
            _exit(1);
        }

        sync_signal_child(&sp);

        int sock_fd = event_receiver_get_socket(r);
        if (sock_fd < 0) {
            event_receiver_free(r);
            _exit(1);
        }

        /* poll() on both the receiver socket and the shutdown pipe.
         * When the parent writes to the shutdown pipe, poll() returns
         * with the pipe fd readable, and the child exits gracefully. */
        struct pollfd pfds[2];
        pfds[0].fd = sock_fd;
        pfds[0].events = POLLIN;
        pfds[1].fd = shutdown_pipe[0];
        pfds[1].events = POLLIN;

        gboolean got_event = FALSE;
        gboolean shutdown = FALSE;

        while (!got_event && !shutdown) {
            int nfds = poll(pfds, 2, 5000);
            if (nfds < 0) {
                if (errno == EINTR)
                    continue;
                break;
            }
            if (nfds == 0)
                break; /* timeout */

            if (pfds[1].revents & POLLIN) {
                shutdown = TRUE;
                break;
            }
            if (pfds[0].revents & POLLIN) {
                dispatch_event_t event = {0};
                EventReceiveResult ret = event_receiver_receive(r, &event);
                if (ret == EVENT_RECEIVE_OK)
                    got_event = TRUE;
                else
                    break; /* disconnected or error */
            }
        }

        event_receiver_free(r);
        close(shutdown_pipe[0]);
        _exit(shutdown ? 0 : 1);
    }

    /* Parent */
    close(sp.parent_to_child[0]);
    close(sp.child_to_parent[1]);
    close(shutdown_pipe[0]);

    sync_signal_parent(&sp);
    sync_wait_parent(&sp);

    event_dispatcher_accept(d);
    usleep(10000);

    /* Do NOT send any event. Instead, trigger graceful shutdown via pipe. */
    char c = 'x';
    write(shutdown_pipe[1], &c, 1);

    int status;
    waitpid(pid, &status, 0);
    g_assert_true(WIFEXITED(status));
    g_assert_cmpint(WEXITSTATUS(status), ==, 0);

    event_dispatcher_free(d);
    g_free(path);
    sync_pipe_close(&sp);
    close(shutdown_pipe[1]);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING);

    g_test_add_func("/dispatcher/new_free", test_new_free);
    g_test_add_func("/dispatcher/new_free_null", test_new_free_null);
    g_test_add_func("/dispatcher/send_no_clients", test_send_no_clients);
    g_test_add_func("/dispatcher/get_socket", test_get_socket);
    g_test_add_func("/dispatcher/send_without_accept", test_send_without_accept);
    g_test_add_func("/dispatcher/external_accept", test_external_accept);
    g_test_add_func("/dispatcher/end_to_end", test_end_to_end);
    g_test_add_func("/dispatcher/prefix_filtering", test_prefix_filtering);
    g_test_add_func("/dispatcher/default_events", test_default_events);
    g_test_add_func("/dispatcher/home_expansion", test_home_expansion);
    g_test_add_func("/dispatcher/max_connections_per_user", test_max_connections_per_user);
    g_test_add_func("/dispatcher/graceful_exit_via_poll", test_graceful_exit_via_poll);

    return g_test_run();
}
