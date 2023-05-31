// SPDX-FileCopyrightText: 2022 - 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <sched.h>
#include <time.h>
#include <bits/time.h>
#include <inttypes.h>

#include "utils.h"
#include "resourceutil .h"

#ifdef ENABLE_CGROUP
#define CGROUP_NAME "dfmgroup"
#endif

#ifdef CLOCK_MONOTONIC_RAW
#define CPU_USAGE_CLOCK CLOCK_MONOTONIC_RAW
#else
#define CPU_USAGE_CLOCK CLOCK_MONOTONIC
#endif

#define pid_path_size 128

struct process_cpu_usage {
	// obtained from /proc/pid/stat
	double user_usage;   // Seconds
	double kernel_usage; // Seconds

	// calculated
	double total_usage;  // sum of usages
	struct timespec measured_at;
};

extern bool get_process_info(pid_t pid, struct process_cpu_usage *usage);
extern double get_time_diff(struct timespec t0, struct timespec t1);

inline bool get_process_info(pid_t pid, struct process_cpu_usage *usage) {
	char pid_path[pid_path_size];
	double clock_ticks_per_second = (double) sysconf(_SC_CLK_TCK);
	int written = snprintf(pid_path, pid_path_size, "/proc/%" PRIdMAX "/stat",
			(intmax_t)pid);
	if (written == pid_path_size) {
		return false;
	}
	FILE *stat_file = fopen(pid_path, "r");
	if (!stat_file) {
		return false;
	}
	clock_gettime(CPU_USAGE_CLOCK, &usage->measured_at);
	unsigned long total_user_time;   // in clock_ticks
	unsigned long total_kernel_time; // in clock_ticks

	int return_value = fscanf(stat_file,
								"%*d %*[^)]) %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u "
								"%*u %lu %lu",
								&total_user_time, &total_kernel_time);
	fclose(stat_file);
	if (return_value != 2)
		return false;
	usage->user_usage = (double) total_user_time / clock_ticks_per_second;
	usage->kernel_usage = (double) total_kernel_time / clock_ticks_per_second;
	usage->total_usage = usage->user_usage + usage->kernel_usage;
	return true;
}

inline double get_time_diff(struct timespec t0, struct timespec t1) {
	double diff = difftime(t1.tv_sec, t0.tv_sec);
	if (t1.tv_nsec < t0.tv_nsec) {
		long val = 1000000000l - t0.tv_nsec + t1.tv_nsec;
		diff += (double)val / 1e9 - 1.;
	} else {
		long val = t1.tv_nsec - t0.tv_nsec;
		diff += (double)val / 1e9;
	}
	return diff;
}

struct timespec last_measured_at;
double last_measured_total_usage;
__attribute__((visibility("default"))) double get_pid_cpupercent(int pid)
{
	// declarations
	struct process_cpu_usage cpu_usage;
	double cpu_percent;

	// Calculate CPU Usage
	get_process_info(pid, &cpu_usage);

	cpu_percent = 100. * (cpu_usage.total_usage - last_measured_total_usage) /
			get_time_diff(last_measured_at, cpu_usage.measured_at);
	last_measured_total_usage = cpu_usage.total_usage;
	last_measured_at = cpu_usage.measured_at;

	dbg_msg("pid %d use CPU: %.1f%% \n", pid, cpu_percent);

	return cpu_percent;
}

#ifdef ENABLE_CGROUP
__attribute__((visibility("default"))) int limit_cpu(int taskpid, int percent, struct cgroup **cg_ret)
{
	if (percent < 0 || percent > 100) {
		dbg_msg("wrong percent set: %d\n", percent);
		return -1;
	}

	// init
	cgroup_init();
	struct cgroup *cg = cgroup_new_cgroup(CGROUP_NAME);
	if (cg == NULL)
	{
		dbg_msg("Failed to new cgroup\n");
		return -1;
	}

	// NOTE: Looks like if this is not done, then libcgroup will chown everything as root.
	cgroup_set_uid_gid(cg, getuid(), getgid(), getuid(), getgid());

	struct cgroup_controller *controller = cgroup_add_controller(cg, "cpu");
	if (controller == NULL)
	{
		dbg_msg("Failed to add controller\n");
		return -1;
	}

	// Create new cgroup
	int ret = cgroup_create_cgroup(cg, 0);
	if (ret != 0)
	{
		dbg_msg("Failed to create cgroup\n");
		return -1;
	}

	// Set CPU limit for cgroup
	uint64_t period = 10000; // 10ms
	ret = cgroup_set_value_uint64(controller, "cpu.cfs_period_us", period);
	if (ret != 0) {
		dbg_msg("Failed to set cpu.cfs_period_us value\n");
		return -1;
	}

	// percent = quota / period * 100
	uint64_t quota = percent * period / 100; // e.g 5ms = 50%
	ret = cgroup_set_value_uint64(controller, "cpu.cfs_quota_us", quota);
	if (ret != 0) {
		dbg_msg("Failed to set cpu.cfs_quota_us value\n");
		return -1;
	}

	/* update controller into kernel */
	if (0 != (ret = cgroup_modify_cgroup(cg))) {
		dbg_msg("ERROR: failed to modify cgroup for %s when modifying values!\n", cgroup_strerror(ret));
		cgroup_free_controllers(cg);
		cgroup_free(&cg);
		return -1;
	}

	// Get CPU limit for cgroup
	uint64_t period_read, quota_read;
	ret = cgroup_get_value_uint64(controller, "cpu.cfs_period_us", &period_read);
	if (ret != 0) {
		dbg_msg("Failed to get cpu.cfs_period_us value\n");
		return -1;
	}
	ret = cgroup_get_value_uint64(controller, "cpu.cfs_quota_us", &quota_read);
	if (ret != 0) {
		dbg_msg("Failed to get cpu.cfs_quota_us value\n");
		return -1;
	}
	dbg_msg("CPU limit set to:\n");
	dbg_msg("cpu.cfs_period_us = %lu, cpu.cfs_quota_us = %lu\n", period_read, quota_read);

	// Final, set pid into cgroup.
	if (taskpid <= 0) {
		ret = cgroup_attach_task_pid(cg, taskpid);
	} else {
		// Move process to cgroup
		ret = cgroup_attach_task(cg);
	}
	if (ret != 0) {
		dbg_msg("Failed to move process to cgroup\n");
		return -1;
	}

	*cg_ret = cg;
	return 0;
}

__attribute__((visibility("default"))) int free_cg_cpu(struct cgroup *cg)
{
	if (NULL == cg)
		return -1;
	cgroup_delete_cgroup(cg, 0);
	cgroup_free_controllers(cg);
	cgroup_free(&cg);
	return 0;
}
#endif
