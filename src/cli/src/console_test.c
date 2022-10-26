// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <limits.h>
#include <regex.h>

#include "fs_buf.h"
#include "index.h"
#include "utils.h"

#ifndef MAX_RESULTS
#define MAX_RESULTS 100
#endif


int match_str(const char *name, void *query)
{
	return !strstr(name, (char *)query);
}

int match_regex(const char *name, void *query)
{
	regmatch_t subs;
	regex_t *compiled = (regex_t *)query;
	return (regexec(compiled, name, 1, &subs, 0) == REG_NOERROR) ? 0 : 1;
}

int progress_function(uint32_t count, const char* cur_file, void* param) {
	// printf("progress: %d file(s) founded, current file: %s\n", count, cur_file);
	return 0;
}

static uint32_t search_by_fsbuf(fs_buf *fsbuf, const char *query)
{
	uint32_t name_offs[MAX_RESULTS], end_off = get_tail(fsbuf);
	uint32_t count = MAX_RESULTS, start_off = first_name(fsbuf);
	regex_t compiled;
	int err = regcomp(&compiled, query, REG_ICASE | REG_EXTENDED);

	if (!err)
	{
		search_files(fsbuf, &start_off, end_off, name_offs, &count, match_regex, &compiled, progress_function, NULL);
		char path[PATH_MAX] = {'0'};
		for (uint32_t i = 0; i < count; i++)
		{
			char *p = get_path_by_name_off(fsbuf, name_offs[i], path, sizeof(path));
			printf("\t%'u: %c %'u %s\n", i + 1, is_file(fsbuf, name_offs[i]) ? 'F' : 'D', name_offs[i], p);
		}
		uint32_t total = count;
		while (count == MAX_RESULTS)
		{
			search_files(fsbuf, &start_off, end_off, name_offs, &count, match_regex, &compiled, progress_function, NULL);
			total += count;
		}
		regfree(&compiled);
		return total;
	}
	else
	{
		search_files(fsbuf, &start_off, end_off, name_offs, &count, match_str, query, progress_function, NULL);
		char path[PATH_MAX];
		for (uint32_t i = 0; i < count; i++)
		{
			char *p = get_path_by_name_off(fsbuf, name_offs[i], path, sizeof(path));
			printf("\t%'u: %c %'u %s\n", i + 1, is_file(fsbuf, name_offs[i]) ? 'F' : 'D', name_offs[i], p);
		}
		uint32_t total = count;
		while (count == MAX_RESULTS)
		{
			search_files(fsbuf, &start_off, end_off, name_offs, &count, match_str, query, progress_function, NULL);
			// search_files(fsbuf, &start_off, end_off, query, name_offs, &count, comparison_by_regex);
			total += count;
		}
		return total;
	}
}

static uint32_t search_by_fsbuf_parallel(fs_buf *fsbuf, const char *query, const char *filter)
{
	uint32_t name_offs[MAX_RESULTS], end_off = get_tail(fsbuf);
	uint32_t count = MAX_RESULTS, start_off = first_name(fsbuf);
	search_rule *s_rule = NULL;
	search_rule *rule_tmp = NULL;
	uint32_t total = 0;
	uint32_t reqcount = count;

	if (filter != NULL) {
		char *rulestr;
		char *buff;
		buff = filter;
		rulestr = strsep(&buff, " ");
		while(rulestr!=NULL)
		{
			// parse the rule's 'flag' and 'target' from string, i.e. 0x10.git > flag=16 target=.git
			if (strstr(rulestr, "0x") == rulestr) {
				char tag[4] = {0};
				strncpy(tag, rulestr, 4); //获取规则前4字符

				search_rule *irule = (search_rule *)malloc(sizeof(search_rule));
				irule->next = NULL;
				irule->flag = (int)strtol(tag, NULL, 0); //转换为flag, 如 0x10 -> 16
				strcpy(irule->target, rulestr + 4);

				// append new rule to tmp rule link.
				if (rule_tmp != NULL)
					rule_tmp->next = irule;
				rule_tmp = irule;

				//save the tmp link head to real rule link.
				if (s_rule == NULL)
					s_rule = rule_tmp;
			}

			//check next rule
			rulestr = strsep(&buff, " ");
		}
	}

	parallelsearch_files(fsbuf, &start_off, end_off, name_offs, &count, s_rule, query);
	{
		char path[PATH_MAX] = {'0'};
		uint32_t min_count = reqcount < count ? reqcount : count;
		for (uint32_t i = 0; i < min_count; i++)
		{
			char *p = get_path_by_name_off(fsbuf, name_offs[i], path, sizeof(path));
			printf("\t%'u: %c %'u %s\n", i + 1, is_file(fsbuf, name_offs[i]) ? 'F' : 'D', name_offs[i], p);
		}
		total += count;
	}
	return total;
}

static int get_short_query(char *cmd, char *short_cmd)
{
	if (strlen(cmd) == 0)
		return -1;

	if (cmd[strlen(cmd) - 1] == '\n')
		cmd[strlen(cmd) - 1] = 0;

	if (strlen(cmd) == 0)
		return 1;

	wchar_t wquery[NAME_MAX], wshort_query[MAX_KW_LEN + 1];
	wchar_t *ws = 0;

	if (utf8_to_wchar_t(cmd, wquery, sizeof(wquery) - sizeof(wchar_t)) != 0)
	{
		printf("utf8 conversion failed for %s\n", cmd);
		return 2;
	}

	if (wcslen(wquery) > MAX_KW_LEN)
	{
		wcsncpy(wshort_query, wquery, MAX_KW_LEN);
		wshort_query[MAX_KW_LEN] = 0;
		ws = wshort_query;
	}
	else
		ws = wquery;

	if (wchar_t_to_utf8(ws, short_cmd, NAME_MAX) != 0)
	{
		printf("wchar_t conversion failed for %S\n", ws);
		return 3;
	}
	return 0;
}

static uint32_t search_by_index(fs_index *fsi, fs_buf *fsbuf, char *query)
{
	if (fsi == 0)
	{
		printf("    index empty :P\n");
		return 0;
	}

	char short_query[NAME_MAX + 1] = {'0'};
	int ret = get_short_query(query, short_query);
	if (ret != 0)
		return 0;

	int truncated = strlen(short_query) < strlen(query);
	char path[PATH_MAX] = {'0'};
	index_keyword *inkw = get_index_keyword(fsi, short_query);
	uint32_t n = 1;
	for (uint32_t i = 0; inkw && i < inkw->len; i++)
	{
		if (n <= MAX_RESULTS)
		{
			if (truncated && strstr(get_name(fsbuf, inkw->fsbuf_offsets[i]), query) == 0)
				continue;
			char *p = get_path_by_name_off(fsbuf, inkw->fsbuf_offsets[i], path, sizeof(path));
			printf("\t%'u: %c %'u %s\n", n, is_file(fsbuf, inkw->fsbuf_offsets[i]) ? 'F' : 'D', inkw->fsbuf_offsets[i], p);
		}
		n++;
	}
	if (get_load_policy(fsi) != LOAD_ALL)
		free_index_keyword(inkw, 1);
	return n - 1;
}

void console_test(fs_buf *fsbuf, fs_index *fsi)
{
	char cmd[1024];
	struct timeval s, e;
	printf("*** input any string to query, or s/XXX to search XXX with index, if/XXX to insert file /XXX, id/XXX to insert directory /XXX, d/XXX to remove path /XXX, r/XXX /YYY to rename path /XXX to /YYY ***\n");
	printf("*** search by multi thread:m/XXX 0x*** 0x**** to search XXX with filter rule(detail rule in fs_buf.h) ***\n");
	while (1)
	{
		printf(" $ ");
		char *r = fgets(cmd, sizeof(cmd) - 1, stdin);
		if (r == 0 || *r == 0)
		{
			printf("\n");
			return;
		}
		if (r[strlen(r) - 1] == '\n')
			r[strlen(r) - 1] = 0;
		if (*r == 0)
			continue;

		int cmd_type = 0;
		char *src = 0, *dst = 0;
		char *filter = 0;
		if (strstr(r, "s/") == r)
		{
			cmd_type = 1;
		}
		else if (strstr(r, "if/") == r || strstr(r, "id/") == r)
		{
			cmd_type = 2;
		}
		else if (strstr(r, "d/") == r)
		{
			cmd_type = 3;
		}
		else if (strstr(r, "r/") == r)
		{
			src = r + 1;
			dst = strstr(src, " /");
			if (dst == 0)
			{
				printf("rename requires a dst-path\n");
				continue;
			}
			*dst = 0;
			dst += 1;
			cmd_type = 4;
		}
		else if (strstr(r, "p/") == r)
		{
			cmd_type = 5;
		}
		else if (strstr(r, "m/") == r)
		{
			src = r + 1;
			filter = strstr(src, " 0x");
			if (filter != 0)
			{
				*filter = 0;
				filter += 1;
				printf("search with some searching rules!\n");
			}
			cmd_type = 6;
		}
		else
		{
			cmd_type = 0;
		}

		fs_change changes[10];
		uint32_t n = 0, change_count = sizeof(changes) / sizeof(fs_change), path_off = 0, start_off = 0, end_off = 0;
		int result = 0;

		gettimeofday(&s, 0);
		switch (cmd_type)
		{
		case 0:
			n = search_by_fsbuf(fsbuf, r);
			break;
		case 1:
			n = search_by_index(fsi, fsbuf, r + 2);
			break;
		case 2:
			result = insert_path(fsbuf, r + 2, r[1] == 'd', changes);
			break;
		case 3:
			result = remove_path(fsbuf, r + 1, changes, &change_count);
			break;
		case 4:
			result = rename_path(fsbuf, src, dst, changes, &change_count);
			break;
		case 5:
			get_path_range(fsbuf, r + 1, &path_off, &start_off, &end_off);
			break;
		case 6:
			n = search_by_fsbuf_parallel(fsbuf, r + 2, filter);
			break;
		}
		gettimeofday(&e, 0);
		uint64_t dur = (e.tv_usec + e.tv_sec * 1000000) - (s.tv_usec + s.tv_sec * 1000000);

		switch (cmd_type)
		{
		case 0:
			printf("    found %'u entries for %s in %'lu us\n", n, r, dur);
			break;
		case 1:
			printf("    found %'u entries for %s in %'lu us\n", n, r + 2, dur);
			break;
		case 2:
			printf("    %s insertion for %s in %'lu us with result: %d, start: %'u, delta: %'d\n",
				   r[1] == 'd' ? "dir" : "file", r + 2, dur, result, changes[0].start_off, changes[0].delta);
			break;
		case 3:
			printf("    path %s removed in %'lu us with result %d, changes:\n", r + 1, dur, result);
			if (result == 0)
			{
				for (int i = 0; i < change_count; i++)
					printf("\tstart: %'u, delta: %'d\n", changes[i].start_off, changes[i].delta);
			}
			break;
		case 4:
			printf("    rename %s to %s result: %d\n", src, dst, result);
			if (result == 0)
			{
				for (int i = 0; i < change_count; i++)
					printf("\tstart: %'u, delta: %'d\n", changes[i].start_off, changes[i].delta);
			}
			break;
		case 5:
			printf("    path %s info: start %'u, kids-start %'u, kids-end %'u\n", r + 1,
				   path_off, start_off, end_off);
			break;
		case 6:
			printf("  found %'u entries for %s in %'lu us\n", n, r + 2, dur);
			break;
		}
	}
}
