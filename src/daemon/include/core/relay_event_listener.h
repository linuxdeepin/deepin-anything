// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_RELAY_EVENT_LISTENER_H_
#define ANYTHING_RELAY_EVENT_LISTENER_H_

#define G_LOG_USE_STRUCTURED
#include <glib.h>

#include "common/fs_event.h"

G_BEGIN_DECLS

typedef struct RelayEventListener RelayEventListener;

typedef void (*RelayEventListenerCallback)(gpointer user_data, fs_event *event);

typedef void (*RelayEventListenerQuitCallback)(gpointer user_data);

RelayEventListener *relay_event_listener_new(RelayEventListenerCallback callback,
                                               RelayEventListenerQuitCallback quit_callback,
                                               gpointer user_data);

gboolean relay_event_listener_start(RelayEventListener *listener);

void relay_event_listener_stop(RelayEventListener *listener);

void relay_event_listener_free(RelayEventListener *listener);

G_END_DECLS

#endif // ANYTHING_RELAY_EVENT_LISTENER_H_
