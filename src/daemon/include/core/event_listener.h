// SPDX-FileCopyrightText: 2024-2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ANYTHING_EVENT_LISTENER_H_
#define ANYTHING_EVENT_LISTENER_H_

#define G_LOG_USE_STRUCTURED
#include <glib.h>

#include "common/fs_event.h"

G_BEGIN_DECLS

typedef struct EventListener EventListener;

typedef void (*EventListenerCallback)(gpointer user_data, fs_event *event);

typedef void (*EventListenerQuitCallback)(gpointer user_data);

EventListener *event_listener_new(EventListenerCallback callback,
                                   EventListenerQuitCallback quit_callback,
                                   gpointer user_data);

gboolean event_listener_start(EventListener *listener);

void event_listener_stop(EventListener *listener);

void event_listener_free(EventListener *listener);

G_END_DECLS

#endif // ANYTHING_EVENT_LISTENER_H_
