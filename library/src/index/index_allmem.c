#define _DEFAULT_SOURCE

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "index.h"
#include "index_base.h"
#include "index_allmem.h"
#include "index_utils.h"
#include "utils.h"

#define IDX_KW_BLK	4
#define FSBUF_BLK	4

extern const char index_magic[];

struct __fs_allmem_index__ {
	fs_index base;
	index_hash* indice;
};

static int get_load_policy_allmem()
{
	return LOAD_ALL;
}

static void get_stats_allmem(fs_index* fsi, uint64_t *memory, uint32_t* keywords, uint32_t* fsbuf_offsets)
{
	fs_allmem_index* ami = (fs_allmem_index*)fsi;

	*memory = sizeof(index_hash)*fsi->count;
	*keywords = 0;
	*fsbuf_offsets = 0;
	for (int i = 0; i < fsi->count; i++) {
		if (ami->indice[i].len == 0)
			continue;

		*keywords = *keywords + ami->indice[i].len;
		*memory = *memory + sizeof(index_keyword)*(ami->indice[i].len + ami->indice[i].empty);
		for (int j = 0; j < ami->indice[i].len; j++) {
			index_keyword* inkw = &ami->indice[i].keywords[j];
			*fsbuf_offsets = *fsbuf_offsets + inkw->len;
			*memory = *memory + sizeof(uint32_t)*(inkw->len + inkw->empty);
			char *s = get_cs_string(&inkw->keyword);
			if (strlen(s) >= 7)
				*memory = *memory + strlen(s) + 1;
		}
	}
}

static index_keyword* get_index_keyword_allmem(fs_index* fsi, const char* query_utf8)
{
	fs_allmem_index* ami = (fs_allmem_index*)fsi;
	uint32_t ih = hash(query_utf8) % fsi->count;

	uint32_t inkw_pos = ami->indice[ih].len;
	for (uint32_t i = 0; i < ami->indice[ih].len; i++) {
		index_keyword* inkw = &ami->indice[ih].keywords[i];
		if (strcmp(get_cs_string(&inkw->keyword), query_utf8) == 0) {
			inkw_pos = i;
			break;
		}
	}

	return inkw_pos == ami->indice[ih].len ? 0 : &ami->indice[ih].keywords[inkw_pos];
}

static void free_fs_index_allmem(fs_index* fsi)
{
	fs_allmem_index* ami = (fs_allmem_index*)fsi;
	for (uint32_t i = 0; i < fsi->count; i++) {
		if (ami->indice[i].keywords == 0)
			continue;

		for (uint32_t j = 0; j < ami->indice[i].len; j++)
			free_index_keyword(&ami->indice[i].keywords[j], 0);
		free(ami->indice[i].keywords);
	}
	
	free(ami->indice);
	free(ami);
}

static index_keyword* init_index_keyword(index_keyword* inkw, const char* index_utf8)
{
	int ret = set_cs_string(&inkw->keyword, index_utf8);
	if (ret == CS_SET_STR_FAIL)
		return 0;
	int long_str = CS_LONG_STR == ret;

	inkw->fsbuf_offsets = malloc(FSBUF_BLK * sizeof(uint32_t));
	if (inkw->fsbuf_offsets == 0) {
		if (long_str)
			free(inkw->keyword.p);
		return 0;
	}

	inkw->empty = FSBUF_BLK;
	inkw->len = 0;
	return inkw;
}

static index_keyword* get_index_keyword_for_append(fs_allmem_index* ami, const char* query_utf8)
{
	index_keyword* inkw = get_index_keyword_allmem(&ami->base, query_utf8);
	if (inkw != 0)
		return inkw;

	uint32_t ih = hash(query_utf8) % ami->base.count;
	if (ami->indice[ih].empty == 0) {
		void* p = realloc(ami->indice[ih].keywords, sizeof(index_keyword) * (ami->indice[ih].len + IDX_KW_BLK));
		if (p == 0)
			return 0;
		ami->indice[ih].keywords = p;
		ami->indice[ih].empty = IDX_KW_BLK;
	}

	inkw = init_index_keyword(&ami->indice[ih].keywords[ami->indice[ih].len], query_utf8);
	if (inkw) {
		ami->indice[ih].len++;
		ami->indice[ih].empty--;
	}
	return inkw;
}

static void add_index_allmem(fs_index* fsi, const char* index_utf8, uint32_t fsbuf_offset)
{
	fs_allmem_index* ami = (fs_allmem_index*)fsi;

	index_keyword* inkw = get_index_keyword_for_append(ami, index_utf8);
	if (inkw == 0)
		return;

	uint32_t pos = get_insert_pos(fsbuf_offset, inkw->fsbuf_offsets, inkw->len, 1);
	if (inkw->len > pos && inkw->fsbuf_offsets[pos] == fsbuf_offset) {
		return;
	}

	if (inkw->empty == 0) {
		void *p = realloc(inkw->fsbuf_offsets, (inkw->len + FSBUF_BLK)*sizeof(uint32_t));
		if (p) {
			inkw->fsbuf_offsets = p;
			inkw->empty = FSBUF_BLK;
		} else
			return;
	}
	if (inkw->len > pos)
		memmove(inkw->fsbuf_offsets + pos + 1, inkw->fsbuf_offsets + pos, sizeof(uint32_t)*(inkw->len-pos));
	inkw->fsbuf_offsets[pos] = fsbuf_offset;
	inkw->len++;
	inkw->empty--;
}

static void add_fsbuf_offsets_allmem(fs_index* fsi, uint32_t start_off, int delta)
{
	fs_allmem_index* ami = (fs_allmem_index*)fsi;
	for (uint32_t i = 0; i < fsi->count; i++) {
		for (uint32_t j = 0; j < ami->indice[i].len; j++) {
			index_keyword* inkw = &ami->indice[i].keywords[j];
			add_inkw_fsbuf_offsets(inkw, start_off, delta);
		}
	}
}

static void init_allmem_base(fs_index* fsi, uint32_t count)
{
	fsi->count = count;
	fsi->get_statistics = get_stats_allmem;
	fsi->get_load_policy = get_load_policy_allmem;
	fsi->get_index_keyword = get_index_keyword_allmem;
	fsi->add_index = add_index_allmem;
	fsi->add_fsbuf_offsets = add_fsbuf_offsets_allmem;
	fsi->free_fs_index = free_fs_index_allmem;
}

int load_allmem_index(fs_index** pfsi, int fd, uint32_t count)
{
	fs_allmem_index* ami = malloc(sizeof(fs_allmem_index));
	if (0 == ami) {
		close(fd);
		return 10;
	}
	init_allmem_base(&ami->base, count);

	posix_fadvise(fd, sizeof(uint32_t)*2, 0, POSIX_FADV_SEQUENTIAL);

	ami->indice = calloc(sizeof(index_hash), count);
	if (ami->indice == 0) {
		free(ami);
		close(fd);
		return 11; 
	}

	inkw_count_off* icos = load_inkw_count_offs(fd, count);
	if (icos == 0) {
		free_fs_index_allmem(&ami->base);
		close(fd);
		return 12;
	}

	for (uint32_t i = 0; i < count; i++)
		ami->indice[i].len = icos[i].len;
	free(icos);

	for (uint32_t i = 0; i < count; i++) {
		ami->indice[i].keywords = calloc(sizeof(index_keyword), ami->indice[i].len);
		if (ami->indice[i].keywords == 0) {
			free_fs_index_allmem(&ami->base);
			close(fd);
			return 13;
		}
		for (uint32_t j = 0; j < ami->indice[i].len; j++) {
			if (load_index_keyword(fd, &ami->indice[i].keywords[j], LOAD_ALL, 0) != 0) {
				free_fs_index_allmem(&ami->base);
				close(fd);
				return 14;
			}
		}
	}

	*pfsi = &ami->base;
	close(fd);
	return 0;
}

__attribute__((visibility("default"))) fs_allmem_index* new_allmem_index(uint32_t count)
{
	fs_allmem_index* ami = malloc(sizeof(fs_allmem_index));
	if (0 == ami)
		return 0;

	ami->indice = calloc(sizeof(index_hash), count);
	if (0 == ami->indice) {
		free(ami);
		return 0;
	}
	init_allmem_base(&ami->base, count);

	return ami;
}

__attribute__((visibility("default"))) int save_allmem_index(fs_allmem_index* ami, const char* filename)
{
	int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return 1;

	if (write(fd, index_magic, strlen(index_magic)+1) != strlen(index_magic)+1) {
		close(fd);
		return 2;
	}

	if (write(fd, &ami->base.count, sizeof(ami->base.count)) != sizeof(ami->base.count)) {
		close(fd);
		return 3;
	}

	inkw_count_off* icos = malloc(sizeof(inkw_count_off) * ami->base.count);
	if (icos == 0) {
		close(fd);
		return 4;
	}

	uint64_t offset = strlen(index_magic) + 1 + sizeof(uint32_t) + sizeof(inkw_count_off)*ami->base.count;
	for (uint32_t i = 0; i < ami->base.count; i++) {
		icos[i].len = ami->indice[i].len;
		icos[i].off = offset;
		uint64_t inkw_size = 0;
		for (uint32_t j = 0; j < ami->indice[i].len; j++) {
			index_keyword* inkw = &ami->indice[i].keywords[j];
			char* s = get_cs_string(&inkw->keyword);
			inkw_size += sizeof(uint32_t)*2 + strlen(s) + 1 + sizeof(uint32_t)*inkw->len;
		}
		offset += inkw_size;
	}

	if (write_file(fd, (char *)icos, sizeof(inkw_count_off) * ami->base.count) != 0) {
		free(icos);
		close(fd);
		return 5;
	}
	free(icos);

	for (uint32_t i = 0; i < ami->base.count; i++) {
		for (uint32_t j = 0; j < ami->indice[i].len; j++) {
			index_keyword* inkw = &ami->indice[i].keywords[j];
			if (save_index_keyword(fd, inkw) == 0) {
				close(fd);
				return 6;
			}
		}
	}
	close(fd);
	return 0;
}
