// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VFS_GENL_H
#define VFS_GENL_H

/* protocol family */
#define VFSMONITOR_FAMILY_NAME "vfsmonitor"

/* attributes */
enum {
  VFSMONITOR_A_UNSPEC,
  VFSMONITOR_A_ACT,
  VFSMONITOR_A_COOKIE,
  VFSMONITOR_A_MAJOR,
  VFSMONITOR_A_MINOR,
  VFSMONITOR_A_PATH,
  __VFSMONITOR_A_MAX,
};
#define VFSMONITOR_A_MAX (__VFSMONITOR_A_MAX - 1)

/* commands */
enum {
  VFSMONITOR_C_UNSPEC,
  VFSMONITOR_C_NOTIFY,
  __VFSMONITOR_C_MAX,
};
#define VFSMONITOR_C_MAX (__VFSMONITOR_C_MAX - 1)

/* multicast group */
#define VFSMONITOR_MCG_DENTRY_NAME VFSMONITOR_FAMILY_NAME "_de"

#endif