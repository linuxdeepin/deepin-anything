#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>

#include "fs_buf.h"
#include "walkdir.h"

#define EMPTY_DIR		0
#define NONEMPTY_DIR	1
#define CANCELLED		2

#ifndef MAX_PARTS
#define MAX_PARTS		256
#endif

typedef struct __progress_report__ {
	uint32_t file_count;
	uint32_t dir_count;
	progress_callback_fn pcf;
	void* param;
} progress_report;

typedef struct __partition_filter__ {
	int selected_partition;
	int merge_partition;
	int partition_count;
	partition* partitions;
} partition_filter;

static int mounted_at(const char* mp, const char* root)
{
	return strcmp(mp, root) == 0 || (strlen(mp) > strlen(root) && strstr(mp, root) == mp && mp[strlen(root)] == '/');
}

static int is_special_mount_point(const char* mp, const char* fs_type)
{
	return (mounted_at(mp, "/sys") || mounted_at(mp, "/proc")) ||
			((mounted_at(mp, "/dev") || mounted_at(mp, "/run")) && strcmp(fs_type, "tmpfs") != 0);
}

static int compare_partition(const void *p1, const void *p2)
{
	partition* part1 = (partition*)p1, *part2 = (partition*)p2;
	return strcmp(part1->mount_point, part2->mount_point);
}

__attribute__((visibility("default"))) int get_partitions(int* part_count, partition* parts)
{
	*part_count = 0;
	FILE* fp = fopen("/proc/mounts", "r");
	if (fp == 0)
		return 1;

	char dev[PART_NAME_MAX], mp[PART_NAME_MAX], fs_type[FS_TYPE_MAX];
	while (fscanf(fp, "%s %s %s %*s %*d %*d\n", dev, mp, fs_type) == 3) {
		if (is_special_mount_point(mp, fs_type))
			continue;
		struct stat st = {0};
		if (stat(mp, &st) != 0)
			continue;

		parts[*part_count].major = major(st.st_dev);
		parts[*part_count].minor = minor(st.st_dev);
		strcpy(parts[*part_count].dev, dev);
		strcpy(parts[*part_count].mount_point, mp);
		strcpy(parts[*part_count].fs_type, fs_type);
		*part_count = *part_count + 1;
	}

	fclose(fp);
	qsort(parts, *part_count, sizeof(partition), compare_partition);
	return 0;
}

static int get_path_partition(const char* path, int part_count, partition* sorted_parts)
{
	for (int i = part_count-1; i >= 0; i--)
		if (strstr(path, sorted_parts[i].mount_point) == path)
			return i;
	return -1;
}

static int should_skip_path(const char* path, partition_filter *pf)
{
	if (is_special_mount_point(path, pf->partitions[pf->selected_partition].fs_type))
		return 1;

	if (pf->merge_partition)
		return 0;

	for (int i = pf->selected_partition+1; i < pf->partition_count; i++)
		if (strstr(path, pf->partitions[i].mount_point) == path)
			return 1;

	return 0;
}

// name should be absolute path, i.e., it should start with / so that we can compare path with special path
static int walkdir(const char* name, fs_buf* fsbuf, uint32_t parent_off, progress_report *pr, partition_filter *pf)
{
	if (should_skip_path(name, pf))
		return EMPTY_DIR;

	if (pr->pcf && pr->pcf(pr->file_count, pr->dir_count, name, pr->param))
		return CANCELLED;

	DIR* dir = opendir(name);
	if (0 == dir)
		return EMPTY_DIR;

	uint32_t start = get_tail(fsbuf);

	struct dirent* de = 0;
	while ((de = readdir(dir)) != 0) {
		if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
			continue;

		// DT_REG: regular file/hardlinks
		// DT_LNK: softlinks
		if (de->d_type != DT_DIR && de->d_type != DT_REG && de->d_type != DT_LNK)
			continue;

		append_new_name(fsbuf, de->d_name, de->d_type == DT_DIR);
		if (de->d_type == DT_DIR)
			pr->dir_count++;
		else
			pr->file_count++;
	}
	closedir(dir);

	// empty folder
	if (start == get_tail(fsbuf))
		return EMPTY_DIR;

	// set parent offset
	uint32_t end = get_tail(fsbuf);
	append_parent(fsbuf, parent_off);

	// loop thru siblings
	uint32_t off = start;
	while (off < end) {
		if (is_file(fsbuf, off)) {
			off = next_name(fsbuf, off);
			continue;
		}

		// set kid offset
		set_kids_off(fsbuf, off, get_tail(fsbuf));
		char path[PATH_MAX];
		snprintf(path, sizeof(path)-1, name[strlen(name)-1] == '/' ? "%s%s" : "%s/%s", name, get_name(fsbuf, off));
		int result = walkdir(path, fsbuf, off, pr, pf);
		if (result == EMPTY_DIR)
			set_kids_off(fsbuf, off, 0);
		else if (result == CANCELLED)
			return CANCELLED;
		off = next_name(fsbuf, off);
	}
	return NONEMPTY_DIR;
}

__attribute__((visibility("default"))) int build_fstree(fs_buf* fsbuf, int merge_partition, progress_callback_fn pcf, void* param)
{
	char* root = get_root_path(fsbuf);
	partition parts[MAX_PARTS];
	partition_filter pf = {
		.selected_partition = -1,
		.merge_partition = merge_partition,
		.partition_count = 0,
		.partitions = parts
	};
	progress_report pr = {
		.file_count = 0,
		.dir_count = 0,
		.pcf = pcf,
		.param = param
	};

	get_partitions(&pf.partition_count, parts);

	if (pf.partition_count > MAX_PARTS) {
		fprintf(stderr, "The number of partitions exceeds the upper limit: %d\n", MAX_PARTS);
		abort();
	}

	pf.selected_partition = get_path_partition(root, pf.partition_count, parts);

	return walkdir(root, fsbuf, 0, &pr, &pf) == CANCELLED;
}
