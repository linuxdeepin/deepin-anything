#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

#include "index.h"
#include "index_base.h"
#include "index_allfile.h"
#include "index_utils.h"

typedef struct __fs_allfile_index__ {
	fs_index base;
	int fd;
} fs_allfile_index;

static int get_load_policy_allfile()
{
	return LOAD_NONE;
}

static void get_stats_allfile(fs_index* fsi, uint64_t* memory, uint32_t* keywords, uint32_t* fsbuf_offsets)
{
	*memory = 0;
	*keywords = 0;
	*fsbuf_offsets = 0;
}

static void free_fs_index_allfile(fs_index* fsi)
{
	fs_allfile_index* afi = (fs_allfile_index*)fsi;
	close(afi->fd);
	free(afi);
}

static index_keyword* get_index_keyword_allfile(fs_index* fsi, const char* query)
{
	fs_allfile_index* afi = (fs_allfile_index*)fsi;
	uint32_t ih = hash(query) % fsi->count;
	uint64_t off = 2*sizeof(uint32_t) + ih * sizeof(inkw_count_off);
	if (lseek(afi->fd, off, SEEK_SET) == -1)
		return 0;

	inkw_count_off ico;
	if (read(afi->fd, &ico, sizeof(ico)) != sizeof(ico))
		return 0;

	if (ico.len == 0)
		return 0;

	if (lseek(afi->fd, ico.off, SEEK_SET) == -1)
		return 0;

	index_keyword* inkw = malloc(sizeof(index_keyword));
	if (inkw == 0)
		return 0;

	for (uint32_t i = 0; i < ico.len; i++) {
		int r = load_index_keyword(afi->fd, inkw, LOAD_NONE, query);
		if (r == 0)
			return inkw;
		if (r > 0) {
			free_index_keyword(inkw, 1);
			return 0;
		}
	}
	return 0;
}

//TODO
static void add_index_allfile(fs_index* fsi, const char* index_utf8, uint32_t fsbuf_offset)
{
}

//TODO
static void add_fsbuf_offsets_allfile(fs_index* fsi, uint32_t start_off, int delta)
{
}

int load_allfile_index(fs_index** pfsi, int fd, uint32_t count)
{
	fs_allfile_index *afi = malloc(sizeof(fs_allfile_index));
	if (0 == afi) {
		close(fd);
		return 10;
	}
	afi->base.count = count;
	afi->base.get_statistics = get_stats_allfile;
	afi->base.get_load_policy = get_load_policy_allfile;
	afi->base.get_index_keyword = get_index_keyword_allfile;
	afi->base.add_index = add_index_allfile;
	afi->base.add_fsbuf_offsets = add_fsbuf_offsets_allfile;
	afi->base.free_fs_index = free_fs_index_allfile;
	afi->fd = fd;

	*pfsi = &afi->base;
	return 0;
}
