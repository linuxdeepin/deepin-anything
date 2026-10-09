// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "serviceentry.h"
#include "utils/trustclient.h"

#include <QGuiApplication>
#include <QSocketNotifier>

#include <sys/socket.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <QDebug>
#include <systemd/sd-journal.h>

// Write end of a pipe used to turn SIGTERM/SIGINT into Qt events.
static int g_signalWriteFd = -1;

static void signalHandler(int sig)
{
    (void)sig;
    const char byte = 1;
    ssize_t written = write(g_signalWriteFd, &byte, sizeof(byte));
    (void)written;   // async-signal-safe best effort
}

// Redirect every Qt log message (qDebug/qInfo/qWarning/qCritical) to the
// systemd journal so it can be inspected with:
//   journalctl --user -t deepin-anything-index
// or filtered by priority (journalctl --user -p). Messages keep their source
// location as structured fields (CODE_FILE/CODE_LINE/CODE_FUNC).
static int qtMsgTypeToJournalPriority(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return LOG_DEBUG;
    case QtInfoMsg:     return LOG_INFO;
    case QtWarningMsg:  return LOG_WARNING;
    case QtCriticalMsg: return LOG_CRIT;
    case QtFatalMsg:    return LOG_ALERT;
    default:            return LOG_INFO;
    }
}

static void journalMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    const QByteArray message = msg.toUtf8();
    const int priority = qtMsgTypeToJournalPriority(type);

    // Build the structured fields for the journal entry. sd_journal_send is a
    // macro that auto-injects CODE_FILE/CODE_LINE/CODE_FUNC from the call site
    // (this handler), which would shadow the real source location. Call the
    // underlying sd_journal_send_with_location directly so the location fields
    // point at the original qDebug/qWarning/... call site instead.
    const QByteArray msgField = QByteArrayLiteral("MESSAGE=") + message;
    char prioField[32];
    snprintf(prioField, sizeof(prioField), "PRIORITY=%d", priority);

    QByteArray fileField = QByteArrayLiteral("CODE_FILE=");
    if (context.file)
        fileField.append(context.file);
    char lineField[32];
    snprintf(lineField, sizeof(lineField), "CODE_LINE=%d", context.line);
    QByteArray funcField = QByteArrayLiteral("CODE_FUNC=");
    if (context.function)
        funcField.append(context.function);

    if (sd_journal_send_with_location(fileField.constData(), lineField,
                                      funcField.constData(), msgField.constData(),
                                      prioField, "SYSLOG_IDENTIFIER=deepin-anything-index",
                                      nullptr) < 0) {
        // Fallback for non-systemd environments (manual runs, tests).
        fprintf(stderr, "deepin-anything-index[%d]: %s\n", priority, message.constData());
        fflush(stderr);
    }
}

int main(int argc, char *argv[])
{
    qInstallMessageHandler(journalMessageHandler);

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

    // Trust negotiation with deepin-security-loader (--fd1/--fd2). Ask the
    // loader to whitelist our unique bus name for org.deepin.Anything access;
    // quit on rejection before owning any bus names.
    int request_fd = -1, response_fd = -1;
    if (anything_index::parse_trust_fds(argc, argv, request_fd, response_fd)) {
        if (!anything_index::request_dbus_trust("org.deepin.Anything",
                                                "/org/deepin/Anything",
                                                "org.deepin.Anything",
                                                request_fd, response_fd)) {
            qCritical() << "deepin-anything-index: DBus trust request rejected by security loader, quit";
            return 1;
        }
    }

    if (anything_index::registerIndexServices() != 0) {
        qWarning() << "deepin-anything-index: failed to register DBus services, exiting";
        return 1;
    }

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
