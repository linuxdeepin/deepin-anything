// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SERVER_EVENT_DISPATCHER_H
#define SERVER_EVENT_DISPATCHER_H

#define G_LOG_USE_STRUCTURED
#include <glib.h>

#include "fs-event.h"

G_BEGIN_DECLS

typedef struct ServerEventDispatcher ServerEventDispatcher;

ServerEventDispatcher *server_event_dispatcher_new(const char *socket_path);

gboolean server_event_dispatcher_start(ServerEventDispatcher *dispatcher);

void server_event_dispatcher_stop(ServerEventDispatcher *dispatcher);

void server_event_dispatcher_free(ServerEventDispatcher *dispatcher);

void server_event_dispatcher_push_event(ServerEventDispatcher *dispatcher,
                                         fs_event *event);

G_END_DECLS

#endif /* SERVER_EVENT_DISPATCHER_H */
