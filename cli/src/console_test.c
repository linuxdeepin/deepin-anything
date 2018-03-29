#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <limits.h>

#include "fs_buf.h"
#include "index.h"
#include "utils.h"

#ifndef MAX_RESULTS
#define MAX_RESULTS		100
#endif

static uint32_t search_by_fsbuf(fs_buf* fsbuf, const char* query)
{
	uint32_t name_offs[MAX_RESULTS], end_off = get_tail(fsbuf);
	uint32_t count = MAX_RESULTS, start_off = first_name(fsbuf);
	search_files(fsbuf, &start_off, end_off, query, name_offs, &count);

	char path[PATH_MAX];
	for (uint32_t i = 0; i < count; i++) {
		char *p = get_path_by_name_off(fsbuf, name_offs[i], path, sizeof(path));
		printf("\t%'u: %c %'u %s\n", i+1, is_file(fsbuf, name_offs[i]) ? 'F' : 'D', name_offs[i], p);
	}
	uint32_t total = count;
	while(count == MAX_RESULTS) {
		search_files(fsbuf, &start_off, end_off, query, name_offs, &count);
		total += count;
	}
	return total;
}

static int get_short_query(char* cmd, char* short_cmd)
{
	if (strlen(cmd) == 0)
		return -1;

	if (cmd[strlen(cmd)-1] == '\n')
		cmd[strlen(cmd)-1] = 0;

	if (strlen(cmd) == 0)
		return 1;

	wchar_t wquery[NAME_MAX], wshort_query[MAX_KW_LEN+1];
	wchar_t* ws = 0;

	if (utf8_to_wchar_t(cmd, wquery, sizeof(wquery) - sizeof(wchar_t)) != 0) {
		printf("utf8 conversion failed for %s\n", cmd);
		return 2;
	}

	if (wcslen(wquery) > MAX_KW_LEN) {
		wcsncpy(wshort_query, wquery, MAX_KW_LEN);
		wshort_query[MAX_KW_LEN] = 0;
		ws = wshort_query;
	} else
		ws = wquery;

	if (wchar_t_to_utf8(ws, short_cmd, NAME_MAX) != 0) {
		printf("wchar_t conversion failed for %S\n", ws);
		return 3;
	}
	return 0;
}

static uint32_t search_by_index(fs_index* fsi, fs_buf* fsbuf, char* query)
{
	if (fsi == 0) {
		printf("    index empty :P\n");
		return 0;
	}

	char short_query[NAME_MAX+1];
	int ret = get_short_query(query, short_query);
	if (ret != 0)
		return 0;

	int truncated = strlen(short_query) < strlen(query);
	char path[PATH_MAX];
	index_keyword* inkw = get_index_keyword(fsi, short_query);
	uint32_t n = 1;
	for (uint32_t i = 0; inkw && i < inkw->len; i++) {
		if (n <= MAX_RESULTS) {
			if (truncated && strstr(get_name(fsbuf, inkw->fsbuf_offsets[i]), query) == 0)
				continue;
			char *p = get_path_by_name_off(fsbuf, inkw->fsbuf_offsets[i], path, sizeof(path));
			printf("\t%'d: %c %'u %s\n", n, is_file(fsbuf, inkw->fsbuf_offsets[i]) ? 'F' : 'D', inkw->fsbuf_offsets[i], p);
		}
		n++;
	}
	if (get_load_policy(fsi) != LOAD_ALL)
		free_index_keyword(inkw, 1);
	return n-1;
}

void console_test(fs_buf* fsbuf, fs_index* fsi)
{
	char cmd[1024];
	struct timeval s, e;
	printf("*** input any string to query, or s/XXX to search XXX with index, if/XXX to insert file /XXX, id/XXX to insert directory /XXX, d/XXX to remove path /XXX, r/XXX /YYY to rename path /XXX to /YYY ***\n");
	while (1) {
		printf(" $ ");
		char* r = fgets(cmd, sizeof(cmd)-1, stdin);
		if (r == 0 || *r == 0) {
			printf("\n");
			return;
		}
		if (r[strlen(r)-1] == '\n')
			r[strlen(r)-1] = 0;
		if (*r == 0)
			continue;

		int cmd_type = 0;
		char *src = 0, *dst = 0;
		if (strstr(r, "s/") == r) {
			cmd_type = 1;
		} else if (strstr(r, "if/") == r || strstr(r, "id/") == r) {
			cmd_type = 2;
		} else if (strstr(r, "d/") == r) {
			cmd_type = 3;
		} else if (strstr(r, "r/") == r) {
			src = r+1;
			dst = strstr(src, " /");
			if (dst == 0) {
				printf("rename requires a dst-path\n");
				continue;
			}
			*dst = 0;
			dst += 1;
			cmd_type = 4;
		} else if (strstr(r, "p/") == r) {
			cmd_type = 5;
		} else {
			cmd_type = 0;
		}

		fs_change changes[10];
		uint32_t n = 0, change_count = sizeof(changes)/sizeof(fs_change), path_off = 0, start_off = 0, end_off = 0;
		int result = 0;

		gettimeofday(&s, 0);
		switch(cmd_type) {
		case 0:
			n = search_by_fsbuf(fsbuf, r);
			break;
		case 1:
			n = search_by_index(fsi, fsbuf, r+2);
			break;
		case 2:
			result = insert_path(fsbuf, r+2, r[1] == 'd', changes);
			break;
		case 3:
			result = remove_path(fsbuf, r+1, changes, &change_count);
			break;
		case 4:
			result = rename_path(fsbuf, src, dst, changes, &change_count);
			break;
		case 5:
			get_path_range(fsbuf, r+1, &path_off, &start_off, &end_off);
			break;
		}
		gettimeofday(&e, 0);
		uint64_t dur = (e.tv_usec + e.tv_sec*1000000) - (s.tv_usec + s.tv_sec*1000000);

		switch (cmd_type) {
		case 0:
			printf("    found %'u entries for %s in %'lu ms\n", n, r, dur/1000);
			break;
		case 1:
			printf("    found %'u entries for %s in %'lu ms\n", n, r+2, dur/1000);
			break;
		case 2:
			printf("    %s insertion for %s in %'lu ms with result: %d, start: %'u, delta: %'d\n",
				r[1] == 'd' ? "dir" : "file", r+2, dur/1000, result, changes[0].start_off, changes[0].delta);
			break;
		case 3:
			printf("    path %s removed in %'lu ms with result %d, changes:\n", r+1, dur/1000, result);
			if (result == 0) {
				for (int i = 0; i < change_count; i++)
					printf("\tstart: %'u, delta: %'d\n", changes[i].start_off, changes[i].delta);
			}
			break;
		case 4:
			printf("    rename %s to %s result: %d\n", src, dst, result);
			if (result == 0) {
				for (int i = 0; i < change_count; i++)
					printf("\tstart: %'u, delta: %'d\n", changes[i].start_off, changes[i].delta);
			}
			break;
		case 5:
			printf("    path %s info: start %'u, kids-start %'u, kids-end %'u\n", r+1,
				path_off, start_off, end_off);
			break;
		}
	}
}
