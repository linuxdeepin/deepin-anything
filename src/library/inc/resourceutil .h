// SPDX-FileCopyrightText: 2022 - 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

// get this pid cpu usage percent
double get_pid_cpupercent(int pid);

// taskpid = 0, means self task pid; 
// percent = cpu.cfs_quota_us/cpu.cfs_period_us
int limit_cpu(int taskpid, int percent, struct cgroup **cg_ret);
int free_cg_cpu(struct cgroup *cg);
