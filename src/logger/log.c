// Copyright (C) 2025 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <glib/gstdio.h>
#include "log.h"

#define INFO_LEVELS (G_LOG_LEVEL_INFO | G_LOG_LEVEL_DEBUG)

static gboolean g_log_debug = FALSE;
static GLogLevelFlags g_fatal_mask = G_LOG_LEVEL_ERROR;

static const char *
log_level_str (GLogLevelFlags log_level)
{
    switch (log_level & G_LOG_LEVEL_MASK) {
        case G_LOG_LEVEL_ERROR:
            return "ERROR";
            break;
        case G_LOG_LEVEL_CRITICAL:
            return "CRITICAL";
            break;
        case G_LOG_LEVEL_WARNING:
            return "WARNING";
            break;
        case G_LOG_LEVEL_MESSAGE:
            return "MESSAGE";
            break;
        case G_LOG_LEVEL_INFO:
            return "INFO";
            break;
        case G_LOG_LEVEL_DEBUG:
            return "DEBUG";
            break;
        default:
            return "LOG";
            break;
    }
}

static gchar *
log_format_fields (GLogLevelFlags   log_level,
                   const GLogField *fields,
                   gsize            n_fields)
{
    gsize i;
    const gchar *message = NULL;
    const gchar *log_domain = "";
    const gchar *log_file = NULL;
    const gchar *log_function = NULL;
    const gchar *log_line = NULL;
    gint64 now;
    time_t now_secs;
    struct tm *now_tm;
    gchar time_buf[128];

    for (i = 0; i < n_fields; ++i) {
        const GLogField *field = &fields[i];

        if (g_strcmp0 (field->key, "MESSAGE") == 0)
            message = field->value;
        else if (g_strcmp0 (field->key, "GLIB_DOMAIN") == 0)
            log_domain = field->value;
        else if (g_strcmp0 (field->key, "CODE_FILE") == 0)
            log_file = field->value;
        else if (g_strcmp0 (field->key, "CODE_FUNC") == 0)
            log_function = field->value;
        else if (g_strcmp0 (field->key, "CODE_LINE") == 0)
            log_line = field->value;
    }

    now = g_get_real_time ();
    now_secs = (time_t) (now / 1000000);
    now_tm = localtime (&now_secs);
    strftime (time_buf, sizeof (time_buf), "%Y-%m-%d %H:%M:%S", now_tm);

    return g_strdup_printf("[%s.%" G_GINT64_FORMAT "] [%s-%s] [%p-%s@%s:%s] %s\n",
                           time_buf,
                           now % 1000000,
                           log_domain,
                           log_level_str(log_level),
                           (void*)g_thread_self(),
                           log_function,
                           log_file,
                           log_line,
                           message);
}

static GLogWriterOutput
log_writer (GLogLevelFlags   log_level,
            const GLogField *fields,
            gsize            n_fields,
            G_GNUC_UNUSED gpointer         user_data)
{
    g_return_val_if_fail (fields != NULL, G_LOG_WRITER_UNHANDLED);
    g_return_val_if_fail (n_fields > 0, G_LOG_WRITER_UNHANDLED);

    if ((log_level & INFO_LEVELS) && !g_log_debug)
        return G_LOG_WRITER_HANDLED;

    g_autofree gchar *out = log_format_fields (log_level, fields, n_fields);
    g_print("%s", out);

    if (log_level & g_fatal_mask)
        G_BREAKPOINT ();

    return G_LOG_WRITER_HANDLED;
}

void 
init_log()
{
    g_log_set_writer_func (log_writer, NULL, NULL);
    /* get fatal_mask setting */
    g_fatal_mask = g_log_set_always_fatal(g_fatal_mask);
    g_log_set_always_fatal(g_fatal_mask);
}

void
enable_debug_log(gboolean enable)
{
    g_log_debug = enable;
}
