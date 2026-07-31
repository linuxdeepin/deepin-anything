// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SERVER_EVENT_LISTENER_BACKEND_H
#define SERVER_EVENT_LISTENER_BACKEND_H

#include <glib.h>

#include "event-listener.h"

G_BEGIN_DECLS

/* Polymorphic backend: genl or mmap-ring. Each implementation owns its
 * own thread and GLib main loop; events are delivered via the handler
 * passed to its _new function. */
typedef struct ServerEventListenerBackend ServerEventListenerBackend;

struct ServerEventListenerBackend {
    gboolean (*start)(ServerEventListenerBackend *self);
    void     (*stop)(ServerEventListenerBackend *self);
    void     (*free)(ServerEventListenerBackend *self);
};

/* Construct a genl backend. Returns NULL on failure. */
ServerEventListenerBackend *genl_backend_new(FileEventHandler handler,
                                              gpointer user_data);

/* Construct a mmap-ring backend. Returns NULL on failure. */
ServerEventListenerBackend *mmap_ring_backend_new(FileEventHandler handler,
                                                   gpointer user_data);

G_END_DECLS

#endif /* SERVER_EVENT_LISTENER_BACKEND_H */
