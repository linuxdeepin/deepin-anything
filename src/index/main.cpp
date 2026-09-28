// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "serviceentry.h"

#include <QGuiApplication>
#include <QSocketNotifier>

#include <sys/socket.h>
#include <signal.h>
#include <unistd.h>
#include <QDebug>

// Write end of a pipe used to turn SIGTERM/SIGINT into Qt events.
static int g_signalWriteFd = -1;

static void signalHandler(int sig)
{
    (void)sig;
    const char byte = 1;
    ssize_t written = write(g_signalWriteFd, &byte, sizeof(byte));
    (void)written;   // async-signal-safe best effort
}

int main(int argc, char *argv[])
{
    // IdleMonitor relies on KIdleTime, whose poller plugins are tied to a QPA
    // platform integration (a QCoreApplication finds none). Run with the real
    // session platform when a display exists, offscreen otherwise (same
    // pattern as deepin-anything-extractor).
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")
        && qEnvironmentVariableIsEmpty("DISPLAY")
        && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("deepin-anything-index");
    QGuiApplication::setApplicationVersion("1.0.0");

    anything_index::registerIndexServices();

    // Turn SIGTERM/SIGINT into a graceful shutdown (stop monitoring, stop the
    // running task, mark unfinished indexes dirty, unregister bus names).
    int signalFds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, signalFds) == 0) {
        g_signalWriteFd = signalFds[1];
        auto *notifier = new QSocketNotifier(signalFds[0], QSocketNotifier::Read, &app);
        QObject::connect(notifier, &QSocketNotifier::activated, &app, []() {
            anything_index::unregisterIndexServices();
            QCoreApplication::exit(0);
        });
        struct sigaction sa;
        sa.sa_handler = signalHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGINT, &sa, nullptr);
    } else {
        qWarning() << "deepin-anything-index: failed to create signal pipe, graceful shutdown disabled";
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, []() {
        anything_index::unregisterIndexServices();
    });

    return app.exec();
}
