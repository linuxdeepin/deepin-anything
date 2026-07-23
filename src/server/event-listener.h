// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SERVER_EVENT_LISTENER_H
#define SERVER_EVENT_LISTENER_H

#define G_LOG_USE_STRUCTURED
#include <glib.h>

#include "fs-event.h"

G_BEGIN_DECLS

typedef struct ServerEventListener ServerEventListener;

typedef void (*FileEventHandler)(gpointer user_data, fs_event *event);

ServerEventListener *server_event_listener_new(FileEventHandler handler,
                                                 gpointer user_data);

gboolean server_event_listener_start(ServerEventListener *listener);

void server_event_listener_stop(ServerEventListener *listener);

void server_event_listener_free(ServerEventListener *listener);

G_END_DECLS

#endif /* SERVER_EVENT_LISTENER_H */
