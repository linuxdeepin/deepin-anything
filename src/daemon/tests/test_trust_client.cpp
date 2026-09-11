// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for the daemon trust client (parse_trust_fds / request_dbus_trust).
//
// parse_trust_fds is pure command-line parsing and is exercised directly.
// request_dbus_trust first connects to the system bus, so the request tests
// boot a private dbus-daemon and point DBUS_SYSTEM_BUS_ADDRESS at it (GLib
// honours that variable for G_BUS_TYPE_SYSTEM, and caches the connection
// per process). The security-loader side of the protocol is mocked by a
// forked child process that validates the request JSON and writes a canned
// reply over a socketpair — the same fork pattern as
// src/dispatcher/tests/test_relay_dispatcher.c.

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

#include <glib.h>

#include "core/trust_client.h"

namespace {

constexpr size_t MOCK_MAX_LEN = 4096;
constexpr int MOCK_TIMEOUT_MS = 10000;

const char kBusName[] = "org.deepin.Anything";
const char kBusPath[] = "/org/deepin/Anything";
const char kBusIface[] = "org.deepin.Anything";

/* ── parse_trust_fds tests (pure logic, no external dependencies) ── */

// Runs parse_trust_fds over the given argument vector (args[0] is the
// program name, as in a real argv) and asserts the outcome.
void run_parse(std::initializer_list<const char *> args, bool expect_ok,
               int expect_request_fd = -1, int expect_response_fd = -1)
{
    char *argv[16] = { nullptr };
    int argc = 0;
    for (const char *arg : args) {
        g_assert_cmpint(argc, <, (int)(sizeof(argv) / sizeof(argv[0])));
        argv[argc++] = const_cast<char *>(arg);
    }

    int request_fd = -1;
    int response_fd = -1;
    const bool ok = anything::parse_trust_fds(argc, argv, request_fd, response_fd);
    g_assert_true(ok == expect_ok);
    if (expect_ok) {
        g_assert_cmpint(request_fd, ==, expect_request_fd);
        g_assert_cmpint(response_fd, ==, expect_response_fd);
    }
}

void test_parse_valid(void)
{
    run_parse({ "prog", "--fd1", "3", "--fd2", "4" }, true, 3, 4);
}

void test_parse_valid_reversed_order(void)
{
    run_parse({ "prog", "--fd2", "4", "--fd1", "3" }, true, 3, 4);
}

void test_parse_valid_with_extra_args(void)
{
    run_parse({ "prog", "--verbose", "--fd1", "3", "--other", "value", "--fd2", "4" },
              true, 3, 4);
}

void test_parse_missing_fd1(void)
{
    run_parse({ "prog", "--fd2", "4" }, false);
}

void test_parse_missing_fd2(void)
{
    run_parse({ "prog", "--fd1", "3" }, false);
}

void test_parse_none(void)
{
    run_parse({ "prog" }, false);
}

void test_parse_invalid_value(void)
{
    // non-numeric / negative / trailing garbage all fail
    run_parse({ "prog", "--fd1", "abc", "--fd2", "4" }, false);
    run_parse({ "prog", "--fd1", "3", "--fd2", "-1" }, false);
    run_parse({ "prog", "--fd1", "3x", "--fd2", "4" }, false);
}

/* ── Private system-bus fixture ─────────────────────────────────── */

pid_t bus_pid = -1;
bool bus_ready = false;
bool bus_tried = false;

void stop_bus_at_exit(void)
{
    if (bus_pid > 0) {
        // GLib's D-Bus worker may log when the peer goes away; never let
        // that abort the process while it is already exiting.
        g_log_set_always_fatal(static_cast<GLogLevelFlags>(G_LOG_FATAL_MASK));
        kill(bus_pid, SIGTERM);
        waitpid(bus_pid, nullptr, 0);
        bus_pid = -1;
    }
}

// Boots a private dbus-daemon and points DBUS_SYSTEM_BUS_ADDRESS at it.
// Idempotent: the first caller starts the daemon, later callers reuse it
// (GLib caches the G_BUS_TYPE_SYSTEM connection per process anyway, so the
// whole test binary shares one connection and one unique bus name).
// Returns false (once) when dbus-daemon is not installed — callers skip.
bool ensure_private_bus(void)
{
    if (bus_tried)
        return bus_ready;
    bus_tried = true;

    gchar *daemon_path = g_find_program_in_path("dbus-daemon");
    if (daemon_path == nullptr)
        return false;
    g_free(daemon_path);

    int out_pipe[2];
    if (pipe(out_pipe) != 0)
        return false;

    const pid_t pid = fork();
    if (pid < 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        return false;
    }

    if (pid == 0) {
        // dbus-daemon --print-address=1 --session stays in the foreground
        // and prints the (abstract) socket address once it is listening.
        dup2(out_pipe[1], STDOUT_FILENO);
        close(out_pipe[0]);
        close(out_pipe[1]);
        execlp("dbus-daemon", "dbus-daemon", "--print-address=1", "--session",
               static_cast<char *>(nullptr));
        _exit(127);
    }

    close(out_pipe[1]);

    // Read the first line printed by the daemon (its bus address).
    std::string address;
    char buf[256];
    while (address.find('\n') == std::string::npos) {
        const ssize_t n = read(out_pipe[0], buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (n == 0)
            break;
        address.append(buf, static_cast<size_t>(n));
    }
    close(out_pipe[0]);

    const size_t eol = address.find('\n');
    if (eol != std::string::npos)
        address.resize(eol);
    while (!address.empty() && address.back() == '\r')
        address.pop_back();

    if (address.rfind("unix:", 0) != 0 && address.rfind("tcp:", 0) != 0) {
        fprintf(stderr, "test_trust_client: unexpected bus address: '%s'\n",
                address.c_str());
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        return false;
    }

    g_setenv("DBUS_SYSTEM_BUS_ADDRESS", address.c_str(), TRUE);

    bus_pid = pid;
    atexit(stop_bus_at_exit);
    bus_ready = true;
    return true;
}

/* ── Mock security loader (fork child) ──────────────────────────── */

// Reads the request until the top-level JSON object closes (or EOF /
// timeout). Returns the length, or -1 on error. Not string-aware — the
// request produced by trust_client never contains braces inside strings.
int read_request(int fd, char *buffer)
{
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    size_t len = 0;
    int depth = 0;
    while (len < MOCK_MAX_LEN) {
        const int ready = poll(&pfd, 1, MOCK_TIMEOUT_MS);
        if (ready <= 0)
            return -1;

        char ch;
        const ssize_t n = read(fd, &ch, 1);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            break;

        buffer[len++] = ch;
        if (ch == '{') {
            ++depth;
        } else if (ch == '}') {
            if (--depth == 0)
                return static_cast<int>(len);
        }
    }
    return (depth == 0 && len > 0) ? static_cast<int>(len) : -1;
}

bool write_all_bytes(int fd, const char *data, size_t size)
{
    size_t written = 0;
    while (written < size) {
        const ssize_t n = write(fd, data + written, size - written);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        written += static_cast<size_t>(n);
    }
    return true;
}

// The request must carry our own unique bus name (":1.x", hence the ':'
// right after the value's opening quote) and the destination triple
// exactly as passed to request_dbus_trust.
bool request_looks_valid(const char *request)
{
    return strstr(request, "\"UniqueName\":\":") != nullptr
        && strstr(request, "\"DbusName\":\"org.deepin.Anything\"") != nullptr
        && strstr(request, "\"DbusPath\":\"/org/deepin/Anything\"") != nullptr
        && strstr(request, "\"DbusInterface\":\"org.deepin.Anything\"") != nullptr;
}

// Mock loader child: reads and validates the request, then optionally
// writes the canned reply and exits. Exit codes: 0 ok, 1 bad request,
// 2 read failure, 3 write failure. Diagnostics go to stderr.
[[noreturn]] void mock_loader_child(int request_fd, int response_fd,
                                    const char *reply, bool write_reply)
{
    char request[MOCK_MAX_LEN + 1] = { 0 };
    const int len = read_request(request_fd, request);
    if (len <= 0) {
        fprintf(stderr, "mock loader: failed to read trust request\n");
        _exit(2);
    }

    if (!request_looks_valid(request)) {
        fprintf(stderr, "mock loader: unexpected trust request: %s\n", request);
        _exit(1);
    }

    if (write_reply && !write_all_bytes(response_fd, reply, strlen(reply))) {
        fprintf(stderr, "mock loader: failed to write trust reply\n");
        _exit(3);
    }

    close(request_fd);
    close(response_fd);
    _exit(0);
}

/* ── request_dbus_trust tests ───────────────────────────────────── */

// Drives one request_dbus_trust call against a freshly forked mock loader
// over dedicated socketpairs. write_reply=false simulates a loader that
// closes the channel without answering (immediate EOF). check_child
// additionally requires the mock child to exit cleanly (request validated
// and reply written) — used by the grant case.
void run_request_case(const char *reply, bool write_reply, bool expect_ok,
                      bool check_child)
{
    if (!ensure_private_bus()) {
        g_test_skip("dbus-daemon not available");
        return;
    }

    int request_pair[2];
    int response_pair[2];
    g_assert_cmpint(socketpair(AF_UNIX, SOCK_STREAM, 0, request_pair), ==, 0);
    g_assert_cmpint(socketpair(AF_UNIX, SOCK_STREAM, 0, response_pair), ==, 0);

    const pid_t pid = fork();
    g_assert_cmpint(pid, >=, 0);

    if (pid == 0)
        mock_loader_child(request_pair[1], response_pair[1], reply, write_reply);

    close(request_pair[1]);
    close(response_pair[1]);

    const bool ok = anything::request_dbus_trust(
        kBusName, kBusPath, kBusIface, request_pair[0], response_pair[0]);
    g_assert_true(ok == expect_ok);

    int status = 0;
    g_assert_cmpint(waitpid(pid, &status, 0), ==, pid);
    if (check_child) {
        g_assert_true(WIFEXITED(status));
        g_assert_cmpint(WEXITSTATUS(status), ==, 0);
    }
}

void test_request_grant(void)
{
    run_request_case("{\"Result\":true,\"Message\":\"granted\"}", true, true, true);
}

void test_request_deny(void)
{
    run_request_case("{\"Result\":false,\"Message\":\"denied\"}", true, false, false);
}

void test_request_message_with_braces(void)
{
    // Braces and an escaped quote inside the Message string must not make
    // the brace-balanced read stop early or run past the object end.
    run_request_case("{\"Result\":true,\"Message\":\"a{b}c{}\\\"q\"}", true, true, false);
}

void test_request_invalid_json(void)
{
    // Plain non-JSON text followed by EOF must be rejected.
    run_request_case("not json", true, false, false);
}

void test_request_incomplete_then_eof(void)
{
    // Half a JSON object, then the loader closes the channel.
    run_request_case("{\"Result\":tr", true, false, false);
}

void test_request_immediate_eof(void)
{
    // The loader reads the request and closes without answering.
    run_request_case("", false, false, false);
}

} // namespace

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, nullptr);
    g_log_set_always_fatal(static_cast<GLogLevelFlags>(G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING));

    // The EOF cases make request_dbus_trust write to a socket whose peer
    // may already have closed — fail with EPIPE, don't die on SIGPIPE.
    signal(SIGPIPE, SIG_IGN);

    g_test_add_func("/trust/parse/valid", test_parse_valid);
    g_test_add_func("/trust/parse/valid_reversed_order", test_parse_valid_reversed_order);
    g_test_add_func("/trust/parse/valid_with_extra_args", test_parse_valid_with_extra_args);
    g_test_add_func("/trust/parse/missing_fd1", test_parse_missing_fd1);
    g_test_add_func("/trust/parse/missing_fd2", test_parse_missing_fd2);
    g_test_add_func("/trust/parse/none", test_parse_none);
    g_test_add_func("/trust/parse/invalid_value", test_parse_invalid_value);
    g_test_add_func("/trust/request/grant", test_request_grant);
    g_test_add_func("/trust/request/deny", test_request_deny);
    g_test_add_func("/trust/request/message_with_braces", test_request_message_with_braces);
    g_test_add_func("/trust/request/invalid_json", test_request_invalid_json);
    g_test_add_func("/trust/request/incomplete_then_eof", test_request_incomplete_then_eof);
    g_test_add_func("/trust/request/immediate_eof", test_request_immediate_eof);

    return g_test_run();
}
