#pragma once

#include "index.h"

typedef void (*get_statistics_fn)(fs_index*, uint64_t*, uint32_t*, uint32_t*);
typedef int (*get_load_policy_fn)();
typedef void (*free_fs_index_fn)(fs_index*);
typedef index_keyword* (*get_index_keyword_fn)(fs_index*, const char*);
typedef void (*add_index_fn)(fs_index*, const char*, uint32_t);
typedef void (*add_fsbuf_offsets_fn)(fs_index*, uint32_t, int);

struct __fs_index__ {
	uint32_t count;
	get_statistics_fn get_statistics;
	get_load_policy_fn get_load_policy;
	get_index_keyword_fn get_index_keyword;
	add_index_fn add_index;
	add_fsbuf_offsets_fn add_fsbuf_offsets;
	free_fs_index_fn free_fs_index;
};

int load_index_keyword(int fd, index_keyword* inkw, int load_policy, const char* query);
uint64_t save_index_keyword(int fd, index_keyword* inkw);
