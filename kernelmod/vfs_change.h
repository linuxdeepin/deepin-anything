// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

int init_vfs_changes(void) __init;
void cleanup_vfs_changes(void);

void vfs_changed(int act, const char* root, const char* src, const char* dst);
