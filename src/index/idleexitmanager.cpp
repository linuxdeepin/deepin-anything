// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "idleexitmanager.h"
#include "serviceentry.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDBusMessage>
#include <QTimer>
#include <DConfig>

// qDBusAddSpyHook is a Qt DBus internal function exported from libQt6DBus.
// It registers a callback invoked for every D-Bus message that flows
// through the connection (both incoming and outgoing).  The callback
// runs on the main thread, so touching the QTimer directly is safe.
//
// The symbol is not declared in any public Qt6 header, so we declare
// it here.  It is a C++ function (not extern "C"), so the linker must
// resolve the mangled symbol _Z15qDBusAddSpyHookPFvRK12QDBusMessageE
// that libQt6DBus exports.  Using extern "C" would search for the
// unmangled name and fail at link time.
Q_DECL_IMPORT void qDBusAddSpyHook(void (*)(const QDBusMessage &));

namespace anything_index {

static constexpr char kAnythingAppId[] = "org.deepin.anything";
static constexpr char kAnythingSchema[] = "org.deepin.anything";
static constexpr char kIdleExitTimeoutKey[] = "idle_exit_timeout_minutes";
static constexpr int kDefaultIdleExitTimeoutMinutes = 10;

static QTimer *g_idleTimer = nullptr;

static void idleSpyHook(const QDBusMessage &)
{
    if (g_idleTimer)
        g_idleTimer->start();
}

void startIdleExitManager()
{
    int timeoutMinutes = kDefaultIdleExitTimeoutMinutes;

    const std::unique_ptr<Dtk::Core::DConfig> dconfig(
        Dtk::Core::DConfig::create(QString::fromLatin1(kAnythingAppId),
                                   QString::fromLatin1(kAnythingSchema)));
    if (dconfig && dconfig->isValid()) {
        timeoutMinutes = dconfig->value(QString::fromLatin1(kIdleExitTimeoutKey),
                                        kDefaultIdleExitTimeoutMinutes).toInt();
        if (timeoutMinutes < 0) {
            qWarning() << "deepin-anything-index: invalid idle_exit_timeout_minutes"
                       << timeoutMinutes << ", using default"
                       << kDefaultIdleExitTimeoutMinutes;
            timeoutMinutes = kDefaultIdleExitTimeoutMinutes;
        }
    } else {
        qWarning() << "deepin-anything-index: failed to load dconfig for idle exit"
                      "timeout, using default" << kDefaultIdleExitTimeoutMinutes
                   << "minutes";
    }

    if (timeoutMinutes == 0) {
        qInfo() << "deepin-anything-index: idle exit disabled (timeout = 0)";
        return;
    }

    g_idleTimer = new QTimer;
    g_idleTimer->setSingleShot(true);
    g_idleTimer->setInterval(timeoutMinutes * 60 * 1000);

    QObject::connect(g_idleTimer, &QTimer::timeout, []() {
        qInfo() << "deepin-anything-index: idle timeout reached,"
                   "exiting to release resources (D-Bus activation will"
                   "restart on next request)";
        unregisterIndexServices();
        QCoreApplication::quit();
    });

    qDBusAddSpyHook(idleSpyHook);
    g_idleTimer->start();
}

}   // namespace anything_index
