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
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "event-relay-dispatcher.h"
#include "fs-event.h"

/* ── Test fixtures ──────────────────────────────────────────────── */

typedef struct {
    ServerEventRelayDispatcher *dispatcher;
} Fixture;

static void setup(Fixture *f, gconstpointer unused)
{
    (void)unused;
    f->dispatcher = server_event_relay_dispatcher_new(0);
    g_assert_nonnull(f->dispatcher);
}

static void teardown(Fixture *f, gconstpointer unused)
{
    (void)unused;
    server_event_relay_dispatcher_free(f->dispatcher);
}

/* ── Basic lifecycle ────────────────────────────────────────────── */

static void test_new_free(void)
{
    ServerEventRelayDispatcher *d = server_event_relay_dispatcher_new(0);
    g_assert_nonnull(d);
    server_event_relay_dispatcher_free(d);
}

static void test_free_null(void)
{
    server_event_relay_dispatcher_free(NULL);
}

static void test_start_stop(void)
{
    ServerEventRelayDispatcher *d = server_event_relay_dispatcher_new(0);
    g_assert_nonnull(d);

    g_assert_true(server_event_relay_dispatcher_start(d));
    server_event_relay_dispatcher_stop(d);

    /* Double stop is safe. */
    server_event_relay_dispatcher_stop(d);

    server_event_relay_dispatcher_free(d);
}

static void test_push_before_start(Fixture *f, gconstpointer unused)
{
    (void)unused;
    /* push_event takes ownership (g_slice-allocated); the worker or
     * free() will g_slice_free it. */
    fs_event *ev = g_slice_new0(fs_event);
    ev->act = 1;
    g_strlcpy(ev->path, "/tmp/test", sizeof(ev->path));
    server_event_relay_dispatcher_push_event(f->dispatcher, ev);
}

static void test_get_channel_before_start(Fixture *f, gconstpointer unused)
{
    (void)unused;
    g_assert_false(server_event_relay_dispatcher_get_event_channel(
        f->dispatcher, NULL, NULL));
}

/* ── Async get_event_channel round-trip ────────────────────────── */

typedef struct {
    int      fd;
    guint32  protocol_id;
    gboolean called;
} ChannelResult;

static void channel_cb(ServerEventRelayDispatcher *dispatcher, int fd,
                       guint32 protocol_id, gpointer user_data)
{
    (void)dispatcher;
    ChannelResult *r = (ChannelResult *)user_data;
    r->fd = fd;
    r->protocol_id = protocol_id;
    r->called = TRUE;
}

static void test_get_channel_async(Fixture *f, gconstpointer unused)
{
    (void)unused;
    g_assert_true(server_event_relay_dispatcher_start(f->dispatcher));

    ChannelResult r = { .fd = -2, .protocol_id = 0, .called = FALSE };
    g_assert_true(server_event_relay_dispatcher_get_event_channel(
        f->dispatcher, channel_cb, &r));

    /* Wait for the callback to fire on the worker thread. */
    for (int i = 0; i < 500 && !r.called; i++)
        g_usleep(10000);  /* 10 ms */
    g_assert_true(r.called);
    g_assert_cmpint(r.fd, >=, 0);
    g_assert_cmpuint(r.protocol_id, ==, SERVER_RELAY_EVENT_PROTOCOL_ID);

    close(r.fd);
}

static void test_get_channel_null_callback(Fixture *f, gconstpointer unused)
{
    (void)unused;
    g_assert_true(server_event_relay_dispatcher_start(f->dispatcher));

    /* NULL callback should not crash; the worker closes the fd itself. */
    g_assert_true(server_event_relay_dispatcher_get_event_channel(
        f->dispatcher, NULL, NULL));

    /* Give the worker time to process. */
    g_usleep(50000);
}

static void test_get_channel_limit(Fixture *f, gconstpointer unused)
{
    (void)unused;
    /* Create a dispatcher with max 2 channels. */
    server_event_relay_dispatcher_free(f->dispatcher);
    f->dispatcher = server_event_relay_dispatcher_new(2);
    g_assert_nonnull(f->dispatcher);
    g_assert_true(server_event_relay_dispatcher_start(f->dispatcher));

    ChannelResult r1 = {0}, r2 = {0}, r3 = {0};
    g_assert_true(server_event_relay_dispatcher_get_event_channel(
        f->dispatcher, channel_cb, &r1));
    g_assert_true(server_event_relay_dispatcher_get_event_channel(
        f->dispatcher, channel_cb, &r2));

    for (int i = 0; i < 500 && (!r1.called || !r2.called); i++)
        g_usleep(10000);
    g_assert_true(r1.called);
    g_assert_true(r2.called);
    g_assert_cmpint(r1.fd, >=, 0);
    g_assert_cmpint(r2.fd, >=, 0);

    /* Third channel should fail (limit = 2). */
    g_assert_true(server_event_relay_dispatcher_get_event_channel(
        f->dispatcher, channel_cb, &r3));
    for (int i = 0; i < 500 && !r3.called; i++)
        g_usleep(10000);
    g_assert_true(r3.called);
    g_assert_cmpint(r3.fd, ==, -1);

    close(r1.fd);
    close(r2.fd);
}

/* ── Raw fs_event broadcast ─────────────────────────────────────── */

static void test_event_broadcast(Fixture *f, gconstpointer unused)
{
    (void)unused;
    g_assert_true(server_event_relay_dispatcher_start(f->dispatcher));

    /* Get a channel. */
    ChannelResult r = {0};
    g_assert_true(server_event_relay_dispatcher_get_event_channel(
        f->dispatcher, channel_cb, &r));
    for (int i = 0; i < 500 && !r.called; i++)
        g_usleep(10000);
    g_assert_true(r.called);
    g_assert_cmpint(r.fd, >=, 0);

    /* Push an event (ownership transferred to the dispatcher). */
    fs_event *ev = g_slice_new0(fs_event);
    ev->act = 2;
    ev->cookie = 42;
    ev->seq = 99;
    ev->major = 8;
    ev->minor = 5;
    g_strlcpy(ev->path, "/home/user/test.txt", sizeof(ev->path));

    server_event_relay_dispatcher_push_event(f->dispatcher, ev);

    /* Receive on the channel. */
    struct pollfd pfd = { .fd = r.fd, .events = POLLIN };
    g_assert_cmpint(poll(&pfd, 1, 5000), >, 0);
    g_assert_true(pfd.revents & POLLIN);

    fs_event recv_ev = {0};
    ssize_t n = recv(r.fd, &recv_ev, sizeof(recv_ev), 0);
    g_assert_cmpint(n, ==, sizeof(recv_ev));
    g_assert_cmpint(recv_ev.act, ==, 2);
    g_assert_cmpuint(recv_ev.cookie, ==, 42);
    g_assert_cmpuint(recv_ev.seq, ==, 99);
    g_assert_cmpuint(recv_ev.major, ==, 8);
    g_assert_cmpuint(recv_ev.minor, ==, 5);
    g_assert_cmpstr(recv_ev.path, ==, "/home/user/test.txt");

    close(r.fd);
}

static void test_multiple_channels_broadcast(Fixture *f, gconstpointer unused)
{
    (void)unused;
    g_assert_true(server_event_relay_dispatcher_start(f->dispatcher));

    ChannelResult r1 = {0}, r2 = {0};
    g_assert_true(server_event_relay_dispatcher_get_event_channel(
        f->dispatcher, channel_cb, &r1));
    g_assert_true(server_event_relay_dispatcher_get_event_channel(
        f->dispatcher, channel_cb, &r2));

    for (int i = 0; i < 500 && (!r1.called || !r2.called); i++)
        g_usleep(10000);
    g_assert_true(r1.called);
    g_assert_true(r2.called);

    fs_event *ev = g_slice_new0(fs_event);
    ev->act = 7;
    ev->cookie = 123;
    g_strlcpy(ev->path, "/tmp/multi.txt", sizeof(ev->path));

    server_event_relay_dispatcher_push_event(f->dispatcher, ev);

    for (int i = 0; i < 2; i++) {
        int fd = (i == 0) ? r1.fd : r2.fd;
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        g_assert_cmpint(poll(&pfd, 1, 5000), >, 0);

        fs_event recv_ev = {0};
        ssize_t n = recv(fd, &recv_ev, sizeof(recv_ev), 0);
        g_assert_cmpint(n, ==, sizeof(recv_ev));
        g_assert_cmpint(recv_ev.act, ==, 7);
        g_assert_cmpuint(recv_ev.cookie, ==, 123);
        g_assert_cmpstr(recv_ev.path, ==, "/tmp/multi.txt");
    }

    close(r1.fd);
    close(r2.fd);
}

/* ── Pending request at shutdown ────────────────────────────────── */

static void test_pending_request_shutdown(Fixture *f, gconstpointer unused)
{
    (void)unused;
    g_assert_true(server_event_relay_dispatcher_start(f->dispatcher));

    ChannelResult r = { .called = FALSE, .fd = -2 };
    g_assert_true(server_event_relay_dispatcher_get_event_channel(
        f->dispatcher, channel_cb, &r));

    /* Stop before the request is necessarily processed. */
    server_event_relay_dispatcher_stop(f->dispatcher);

    /* The request may or may not have been completed with a real fd
     * before stop() drained it. If called with fd >= 0, close it. */
    if (r.called && r.fd >= 0)
        close(r.fd);
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    /* Standalone tests (no fixture needed). */
    g_test_add_func("/relay/new_free", test_new_free);
    g_test_add_func("/relay/free_null", test_free_null);
    g_test_add_func("/relay/start_stop", test_start_stop);

    /* Fixture-based tests. */
    g_test_add("/relay/push_before_start", Fixture, NULL,
               setup, test_push_before_start, teardown);
    g_test_add("/relay/get_channel_before_start", Fixture, NULL,
               setup, test_get_channel_before_start, teardown);
    g_test_add("/relay/get_channel_async", Fixture, NULL,
               setup, test_get_channel_async, teardown);
    g_test_add("/relay/get_channel_null_callback", Fixture, NULL,
               setup, test_get_channel_null_callback, teardown);
    g_test_add("/relay/get_channel_limit", Fixture, NULL,
               setup, test_get_channel_limit, teardown);
    g_test_add("/relay/event_broadcast", Fixture, NULL,
               setup, test_event_broadcast, teardown);
    g_test_add("/relay/multiple_channels_broadcast", Fixture, NULL,
               setup, test_multiple_channels_broadcast, teardown);
    g_test_add("/relay/pending_request_shutdown", Fixture, NULL,
               setup, test_pending_request_shutdown, teardown);

    return g_test_run();
}
