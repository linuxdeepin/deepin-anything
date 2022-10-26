// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/ioctl.h>

#include "utils.h"
#include "fs_buf.h"
#include "walkdir.h"
#include "vfs_change_consts.h"
#include "vfs_change_uapi.h"

#define PROCFS_PATH		"/proc/"PROCFS_NAME

#define MONITOR_NOT_STARTED		0
#define MONITOR_FAILED			1
#define MONITOR_RUNNING			2
#define MONITOR_QUIT_NOW		3

#ifndef MAX_PARTS
#define MAX_PARTS		32
#endif

static pthread_t monitor_thread = 0;
static int monitor_state = MONITOR_NOT_STARTED;
static const char* act_names[] = {"file_created", "link_created", "symlink_created", "dir_created", "file_deleted", "dir_deleted", "file_renamed", "dir_renamed"};

static void poll_vfs_change(fs_buf* fsbuf)
{
	int fd = open(PROCFS_PATH, O_RDONLY);
	if (fd < 0)
		return;

	ioctl_rs_args irsa;
	if (ioctl(fd, VC_IOCTL_READSTAT, &irsa) != 0) {
		close(fd);
		return;
	}

	if (irsa.cur_changes == 0) {
		close(fd);
		return;
	}

	printf("    vfs-changes cur-changes: %d, total-changes: %'d, discarded: %d, cur-memory: %'d\n",
		irsa.cur_changes, irsa.total_changes, irsa.discarded, irsa.cur_memory);

	char buf[1<<20];
	ioctl_rd_args ira = {.data = buf};
	while (1) {
		ira.size = sizeof(buf);
		if (ioctl(fd, VC_IOCTL_READDATA, &ira) != 0)
			break;

		// no more changes
		if (ira.size == 0)
			break;

		fs_change changes[10];
		int off = 0, result;
		for (int i = 0; i < ira.size; i++) {
			unsigned char action = *(ira.data + off);
			off++;
			char* src = ira.data + off, *dst = 0;
			off += strlen(src) + 1;

			uint32_t change_count = sizeof(changes)/sizeof(fs_change);
			switch(action) {
			case ACT_NEW_FILE:
			case ACT_NEW_SYMLINK:
			case ACT_NEW_LINK:
			case ACT_NEW_FOLDER:
				printf("    %s: %s\n", act_names[action], src);
				result = insert_path(fsbuf, src, action == ACT_NEW_FOLDER, changes);
				printf("    insert_path result: %d\n", result);
				break;
			case ACT_DEL_FILE:
			case ACT_DEL_FOLDER:
				printf("    %s: %s\n", act_names[action], src);
				result = remove_path(fsbuf, src, changes, &change_count);
				printf("    remove_path result: %d\n", result);
				break;
			case ACT_RENAME_FILE:
			case ACT_RENAME_FOLDER:
				dst = ira.data + off;
				off += strlen(dst) + 1;
				printf("    %s: %s -> %s\n", act_names[action], src, dst);
				result = rename_path(fsbuf, src, dst, changes, &change_count);
				printf("    rename_path result: %d\n", result);
				break;
			}
		}
	}
	close(fd);
}

static void* monitor_vfs(void *arg)
{
	fs_buf* fsbuf = (fs_buf*)arg;
	while (1) {
		poll_vfs_change(fsbuf);
		sleep(1);
		if (monitor_state == MONITOR_QUIT_NOW)
			break;
	}
	monitor_state = MONITOR_NOT_STARTED;
	return 0;
}

int start_vfs_monitor(fs_buf* fsbuf)
{
	if (monitor_state != MONITOR_NOT_STARTED && monitor_state != MONITOR_FAILED)
		return 1;

	if (pthread_create(&monitor_thread, 0, monitor_vfs, fsbuf) != 0) {
		dbg_msg("vfs-monitor not started...\n");
		monitor_state = MONITOR_FAILED;
		return 2;
	}
	monitor_state = MONITOR_RUNNING;
	return 0;
}

void stop_vfs_monitor()
{
	if (monitor_state != MONITOR_RUNNING)
		return;

	monitor_state = MONITOR_QUIT_NOW;
	pthread_join(monitor_thread, 0);
}
