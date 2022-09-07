// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#define _DEFAULT_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <locale.h>
#include <limits.h>

#include "fs_buf.h"
#include "index.h"
#include "index_allmem.h"
#include "walkdir.h"
#include "monitor_vfs.h"
#include "stats.h"
#include "console_test.h"

#define FSBUF_SIZE		(1<<24)
#define FSBUF_FILE		".lft"
#define INDEX_COUNT		131071
#define INDEX_FILE		".fsi"

#ifndef MAX_PARTS
#define MAX_PARTS		32
#endif

static int report_progress(uint32_t file_count, uint32_t dir_count, const char* cur_dir, const char* cur_file, void* param)
{
	int *n10k = (int *)param;
	if (file_count + dir_count >= (*n10k)*10000) {
		printf("files: %'u, dirs: %'u, current dir: %s, current file: %s\n", file_count, dir_count, cur_dir, cur_file);
		*n10k = *n10k + 1;
	}
	return 0;
}

static int scan(int argc, char* argv[])
{
	char dir[NAME_MAX] = ".";
	int opt, use_index = 0, merge_partition = 0;
	while ((opt = getopt(argc, argv, "d:im")) != -1) {
		switch(opt) {
		case 'd':
			strcpy(dir, optarg);
			break;
		case 'i':
			use_index = 1;
			break;
		case 'm':
			merge_partition = 1;
			break;
		default:
			printf("unknown options: %c\n", opt);
			return 1;
		}
	}

	char path[PATH_MAX];
	if (argc > optind) {
		realpath(argv[optind], path);
		if (path[strlen(path)-1] != '/')
			path[strlen(path)] = '/';
	}

	fs_buf* fsbuf = new_fs_buf(FSBUF_SIZE, argc <= optind ? "/" : path);

	// walk dir to make indice
	struct timeval s, e;
	gettimeofday(&s, 0);
	int n10k = 0;
	build_fstree(fsbuf, merge_partition, report_progress, &n10k);
	gettimeofday(&e, 0);
	uint64_t dur = (e.tv_usec + e.tv_sec*1000000) - (s.tv_usec + s.tv_sec*1000000);
	printf("scan dur: %'lu ms\n", dur/1000);

	gettimeofday(&s, 0);
	char fullpath[PATH_MAX];
	sprintf(fullpath, "%s/%s", dir, FSBUF_FILE);
	printf("save fsbuf %s: %d\n", fullpath, save_fs_buf(fsbuf, fullpath));
	gettimeofday(&e, 0);
	dur = (e.tv_usec + e.tv_sec*1000000) - (s.tv_usec + s.tv_sec*1000000);
	printf("save fsbuf dur: %'lu ms\n", dur/1000);

	fs_index* fsi = 0;
	fs_allmem_index* ami = 0;
	if (use_index) {
		gettimeofday(&s, 0);
		ami = new_allmem_index(INDEX_COUNT);
		fsi = (fs_index*)ami;
		uint32_t name_off = first_name(fsbuf);
		while (name_off < get_tail(fsbuf)) {
			char* s = get_name(fsbuf, name_off);
			if (strlen(s) > 0)
				add_index(fsi, s, name_off);
			name_off = next_name(fsbuf, name_off);
		}
		gettimeofday(&e, 0);
		dur = (e.tv_usec + e.tv_sec*1000000) - (s.tv_usec + s.tv_sec*1000000);
		printf("indexing dur: %'lu ms\n", dur/1000);

		gettimeofday(&s, 0);
		sprintf(fullpath, "%s/%s", dir, INDEX_FILE);
		printf("save index %s: %d\n", fullpath, save_allmem_index(ami, fullpath));
		gettimeofday(&e, 0);
		dur = (e.tv_usec + e.tv_sec*1000000) - (s.tv_usec + s.tv_sec*1000000);
		printf("save index dur: %'lu ms\n", dur/1000);
	}

	collect_print_statistics(fsbuf, fsi);

	start_vfs_monitor(fsbuf);
	console_test(fsbuf, fsi);
	stop_vfs_monitor();

	free_fs_buf(fsbuf);
	return 0;
}

static int load(int argc, char* argv[])
{
	char dir[NAME_MAX] = ".";
	int load_policy = LOAD_NONE, opt;
	char fullpath[PATH_MAX];

	while ((opt = getopt(argc, argv, "d:l:f:")) != -1) {
		switch(opt) {
		case 'f':
			strcpy(fullpath, optarg);
			break;
		case 'd':
			strcpy(dir, optarg);
			sprintf(fullpath, "%s/%s", dir, FSBUF_FILE);
			break;
		case 'l':
			load_policy = atoi(optarg);
			if (load_policy < LOAD_ALL || load_policy > LOAD_NONE) {
				printf("load policy should be in range of [%d..%d] but %d is given\n", LOAD_ALL, LOAD_NONE, load_policy);
				return 1;
			}
			break;
		default:
			printf("unknown options: %c\n", opt);
			return 2;
		}
	}

	fs_buf* fsbuf = 0;

	struct timeval s, e;
	gettimeofday(&s, 0);

	int r = load_fs_buf(&fsbuf, fullpath);
	if (r != 0) {
		printf("load linear file tree file %s failed: %d\n", fullpath, r);
		return 4;
	}
	printf("load linear file tree file %s done, root: %s\n", fullpath, get_root_path(fsbuf));

	fs_index* fsi = 0;
	sprintf(fullpath, "%s/%s", dir, INDEX_FILE);
	if (load_fs_index(&fsi, fullpath, load_policy) != 0)
		printf("load index file %s failed\n", fullpath);
	else
		printf("load index file %s done\n", fullpath);

	gettimeofday(&e, 0);
	uint64_t dur = (e.tv_usec + e.tv_sec*1000000) - (s.tv_usec + s.tv_sec*1000000);
	printf("load dur: %'lu ms\n", dur/1000);

	collect_print_statistics(fsbuf, fsi);

	start_vfs_monitor(fsbuf);
	console_test(fsbuf, fsi);
	stop_vfs_monitor();

	free_fs_buf(fsbuf);
	return 0;
}

static int get_parts(int argc, char*argv[])
{
	int part_count;
	partition parts[MAX_PARTS];
	if (get_partitions(&part_count, parts) != 0) {
		printf("getting partitions error\n");
		return 1;
	}

	for (int i = 0; i < part_count; i++)
		printf("dev: %s, mount-point: %s, fs-type: %s, major: %d, minor: %d\n",
			parts[i].dev, parts[i].mount_point, parts[i].fs_type, parts[i].major, parts[i].minor);

	return 0;
}

static int help(int argc, char* argv[]);

typedef int (*cmd_fn)(int argc, char* argv[]);

struct {
	char* cmd;
	cmd_fn fp;
	const char* options;
	const char* desc;
} commands[] = {
	{"help", help, 0, "Print this help information"},
	{"scan", scan, "[-d $dir] [-i] [-m] [$root]", "Scan directories $root (default to /), merge all partitions (if -m), make indice(if -i), save data to $dir and test search"},
	{"load", load, "[-d $dir] [-f $lftfile] [-l #load_policy]", "Load previously saved indice from $dir all into memory or load xx.lft index file if -l 0 or none into memory if -l 1 and test search"},
	{"partitions", get_parts, 0, "Get partitions"},
	{0, 0, 0, 0}
};

static int help(int argc, char* argv[])
{
	char path[PATH_MAX];
	readlink("/proc/self/exe", path, sizeof(path));
	printf("Usage: %s commands [options]\n", path);
	printf("command:\n");
	for (int i = 0; commands[i].fp; i++) {
		printf("    %s:\t%s\n", commands[i].cmd, commands[i].desc);
		if (commands[i].options)
			printf("\toptions: %s\n", commands[i].options);
	}
	return 0;
}

int main(int argc, char* argv[])
{
	// for wchar_t and thousands separator printf
	setlocale(LC_ALL, "");

	if (argc <= 1)
		return help(argc-1, argv+1);

	for (int i = 0; commands[i].fp; i++)
		if (strcmp(commands[i].cmd, argv[1]) == 0)
			return (*commands[i].fp)(argc-1, argv+1);

	return help(argc-1, argv+1);
}
