// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#define PROCFS_NAME			"vfs_changes"

#define VC_IOCTL_MAGIC		0x81
#define VC_IOCTL_READDATA	_IOR(VC_IOCTL_MAGIC, 0, long)
#define VC_IOCTL_READSTAT	_IOR(VC_IOCTL_MAGIC, 1, long)
#define VC_IOCTL_WAITDATA	_IOR(VC_IOCTL_MAGIC, 2, long)

// on input, size means total size of data, on output, it means actual data item count
// data format: 1 byte of action, 1 byte of major, 1 byte of minor, then src, then dst (if applicable)
typedef struct __vc_ioctl_readdata_args__ {
	int size;
	char* data;
} ioctl_rd_args;

typedef struct __vc_ioctl_readstat_args__ {
	int total_changes;
	int cur_changes;
	int discarded;
	int cur_memory;
} ioctl_rs_args;

typedef struct __vc_ioctl_waitdata_args__ {
	// 当前改变数量大于等于此值时返回等待(取值范围：0-INT_MAX，取0时表示忽略此参数)
	int condition_count;
	// 从等到第一条数据时开始计时，超时后返回(单位：毫秒，取值范围：0-INT_MAX，取0时表示忽略此参数)
	int condition_timeout;
	// 从调用时开始计时，超时后返回-ETIME(单位：毫秒，值小于等于0时表示无超时)
	int timeout;
	// condition_count和condition_timeout不可同时为0
} ioctl_wd_args;