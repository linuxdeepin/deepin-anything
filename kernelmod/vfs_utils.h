#pragma once

typedef struct __krp_partition__ {
	unsigned char major, minor;
	struct list_head list;
	char root[0];
} krp_partition;

char* read_file_content(const char* filename, int *real_size);
int is_special_mp(const char* mp);
void parse_mounts_info(char* buf, struct list_head* parts) __init;
