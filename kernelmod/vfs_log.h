// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#define mpr_info(fmt, ...) \
  pr_info("vfs_monitor(%s): " fmt "\n", __func__, ##__VA_ARGS__)
#define mpr_err(fmt, ...) \
  pr_err("vfs_monitor(%s): " fmt "\n", __func__, ##__VA_ARGS__)
