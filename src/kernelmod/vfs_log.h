// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#define mpr_info(fmt, ...) \
    pr_info("vfs_monitor: " fmt, ##__VA_ARGS__)

#define mpr_err(fmt, ...) \
    pr_err("vfs_monitor: " fmt, ##__VA_ARGS__)
