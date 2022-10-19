// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef EVENT_MERGE_H
#define EVENT_MERGE_H

void *get_event_merge_entry(void *vfs_changed_func);
void clearup_event_merge(void);

#endif /* EVENT_MERGE_H */