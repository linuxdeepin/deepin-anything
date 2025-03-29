// Copyright (C) 2024 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "utils/sys.h"

#include <filesystem>

#include <pwd.h>
#include <unistd.h>
#include <sys/stat.h>
#include <glib.h>

ANYTHING_NAMESPACE_BEGIN

std::string get_home_directory()
{
    GString *home = g_string_new(NULL);

    struct stat statbuf;
    if (stat("/data/home", &statbuf) == 0)
        g_string_assign(home, "/data");
    else if (stat("/persistent/home", &statbuf) == 0)
        g_string_assign(home, "/persistent");

    g_string_append(home, g_get_home_dir());

    std::string ret(home->str);
    g_string_free(home, TRUE);

    return ret;
}

ANYTHING_NAMESPACE_END
