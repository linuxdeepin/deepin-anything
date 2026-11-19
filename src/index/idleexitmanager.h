// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IDLEEXITMANAGER_H
#define IDLEEXITMANAGER_H

namespace anything_index {

// Registers a D-Bus spy hook that restarts an idle timer on every D-Bus
// message. When the timer expires (no D-Bus activity for the configured
// number of minutes), unregisters index services and quits the application.
//
// The idle timeout is read from the dconfig key "idle_exit_timeout_minutes"
// in the org.deepin.anything schema (default: 10 minutes). A value of 0
// disables the auto-exit feature entirely.
//
// The service is D-Bus-activated (its .service file points at
// deepin-anything-index), so exiting when idle is safe: the next D-Bus
// call to any index service name will automatically restart the process.
//
// dde-file-manager's AbstractIndexController::keepBackendAlive polls
// IsEnabled every 5 minutes while indexes are enabled, which resets the
// timer and keeps the service alive. When all indexes are disabled the
// keepalive stops, and the service exits after the idle timeout.
void startIdleExitManager();

}   // namespace anything_index

#endif   // IDLEEXITMANAGER_H
