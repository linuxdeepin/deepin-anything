#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <regex.h>

#include "fs_buf.h"
#include "utils.h"

#define DATA_START 8
#define FS_NEW_BLK_SIZE (1 << 20)

// fs-tags below are all for little endian archs, such as x86/loongson

#define FS_TAG_BITS 2
#define FS_TAG_MASK ((1 << FS_TAG_BITS) - 1)
#define MAX_FSBUF_SIZE (1 << (8 * sizeof(uint32_t) - FS_TAG_BITS))

#define FS_TAG_FILE 0
#define FS_TAG_DIR 1

struct __fs_buf__
{
	char *head;
	uint32_t capacity;
	uint32_t tail;
	uint32_t first_name_off;
	pthread_rwlock_t lock;
};

// Linear File Tree
static const char fsbuf_magic[] = "LFT";

__attribute__((visibility("default"))) fs_buf *new_fs_buf(uint32_t capacity, const char *root_path)
{
	if (capacity > MAX_FSBUF_SIZE || root_path == 0)
		return 0;

	if (strlen(root_path) + FS_NEW_BLK_SIZE > capacity)
		return 0;

	if (root_path[0] != '/' || root_path[strlen(root_path) - 1] != '/')
		return 0;

	fs_buf *fsbuf = malloc(sizeof(fs_buf));
	if (fsbuf == 0)
		return 0;

	if (pthread_rwlock_init(&fsbuf->lock, 0) != 0)
	{
		free(fsbuf);
		return 0;
	}

	fsbuf->capacity = capacity;
	fsbuf->head = malloc(capacity);
	if (fsbuf->head == 0)
	{
		pthread_rwlock_destroy(&fsbuf->lock);
		free(fsbuf);
		return 0;
	}

	// first DATA_START bytes left for serialization magic & size
	strcpy(fsbuf->head + DATA_START, root_path);
	fsbuf->first_name_off = fsbuf->tail = DATA_START + strlen(root_path) + 1;
	return fsbuf;
}

__attribute__((visibility("default"))) void free_fs_buf(fs_buf *fsbuf)
{
	if (0 == fsbuf)
		return;

	if (fsbuf->head)
		free(fsbuf->head);

	pthread_rwlock_destroy(&fsbuf->lock);
	free(fsbuf);
}

__attribute__((visibility("default"))) uint32_t get_capacity(fs_buf *fsbuf)
{
	return fsbuf->capacity;
}

__attribute__((visibility("default"))) char *get_root_path(fs_buf *fsbuf)
{
	return fsbuf->head + DATA_START;
}

__attribute__((visibility("default"))) uint32_t get_tail(fs_buf *fsbuf)
{
	return fsbuf->tail;
}

__attribute__((visibility("default"))) uint32_t first_name(fs_buf *fsbuf)
{
	return fsbuf->first_name_off;
}

__attribute__((visibility("default"))) char *get_name(fs_buf *fsbuf, uint32_t name_off)
{
	return fsbuf->head + name_off;
}

static int add_capacity(fs_buf *fsbuf, uint32_t size)
{
	uint32_t alloc_size = size / FS_NEW_BLK_SIZE;
	alloc_size = alloc_size * FS_NEW_BLK_SIZE;
	if (alloc_size < size)
		alloc_size += FS_NEW_BLK_SIZE;
	if (fsbuf->capacity + alloc_size > MAX_FSBUF_SIZE)
		return 1;

	char *p = realloc(fsbuf->head, fsbuf->capacity + alloc_size);
	if (p == 0) // TODO alert here
		return 1;

	fsbuf->head = p;
	fsbuf->capacity += alloc_size;
	return 0;
}

static void set_parent_offset(fs_buf *fsbuf, uint32_t name_off, uint32_t parent_off)
{
	// set empty string
	*(fsbuf->head + name_off) = 0;
	// set parent tag
	uint32_t *tag = (uint32_t *)(fsbuf->head + name_off + 1);
	// internally we use relative offset w.r.t. to the tag (not the name)
	// and note that parent is always ahead
	// 0 means root
	if (parent_off > 0)
		parent_off = name_off + 1 - parent_off;
	*tag = (parent_off << FS_TAG_BITS) + FS_TAG_DIR;
}

static int insert_new_name(fs_buf *fsbuf, uint32_t off, char *name, int is_dir, int create_parent_tag)
{
	uint32_t tag_size = is_dir ? sizeof(uint32_t) : 1;
	uint32_t extra_size = strlen(name) + 1 + tag_size + (create_parent_tag ? 1 + sizeof(uint32_t) : 0);

	if (extra_size + fsbuf->tail >= fsbuf->capacity)
		if (add_capacity(fsbuf, extra_size) != 0)
			return ERR_NO_MEM;

	if (fsbuf->tail > off)
		memmove(fsbuf->head + off + extra_size, fsbuf->head + off, fsbuf->tail - off);

	strcpy(fsbuf->head + off, name);
	off += strlen(name) + 1;

	if (is_dir)
	{
		uint32_t *tag = (uint32_t *)(fsbuf->head + off);
		*tag = FS_TAG_DIR;
	}
	else
	{
		*(fsbuf->head + off) = FS_TAG_FILE;
	}

	fsbuf->tail += extra_size;
	return 0;
}

int append_new_name(fs_buf *fsbuf, char *name, int is_dir)
{
	pthread_rwlock_wrlock(&fsbuf->lock);
	int r = insert_new_name(fsbuf, fsbuf->tail, name, is_dir, 0);
	pthread_rwlock_unlock(&fsbuf->lock);
	return r;
}

int append_parent(fs_buf *fsbuf, uint32_t parent_off)
{
	pthread_rwlock_wrlock(&fsbuf->lock);
	if (sizeof(uint32_t) + 1 + fsbuf->tail >= fsbuf->capacity)
		if (add_capacity(fsbuf, 1 + sizeof(uint32_t)) != 0)
		{
			pthread_rwlock_unlock(&fsbuf->lock);
			return ERR_NO_MEM;
		}

	set_parent_offset(fsbuf, fsbuf->tail, parent_off);
	fsbuf->tail += 1 + sizeof(uint32_t);
	pthread_rwlock_unlock(&fsbuf->lock);
	return 0;
}

static int do_is_file(fs_buf *fsbuf, uint32_t name_off)
{
	return *(fsbuf->head + name_off + strlen(fsbuf->head + name_off) + 1) == FS_TAG_FILE;
}

__attribute__((visibility("default"))) int is_file(fs_buf *fsbuf, uint32_t name_off)
{
	pthread_rwlock_rdlock(&fsbuf->lock);
	int r = do_is_file(fsbuf, name_off);
	pthread_rwlock_unlock(&fsbuf->lock);
	return r;
}

__attribute__((visibility("default"))) uint32_t next_name(fs_buf *fsbuf, uint32_t name_off)
{
	uint32_t tag_off = name_off + strlen(fsbuf->head + name_off) + 1;
	return *(fsbuf->head + tag_off) == FS_TAG_FILE ? tag_off + 1 : tag_off + sizeof(uint32_t);
}

static void do_set_kids_off(fs_buf *fsbuf, uint32_t name_off, uint32_t kids_off)
{
	// we don't check if the name is a dir here
	uint32_t *tag = (uint32_t *)(fsbuf->head + name_off + strlen(fsbuf->head + name_off) + 1);
	// internally we use relative offset w.r.t. to the tag (not the name)
	// and note that kid is always after parent
	if (kids_off != 0)
		kids_off = kids_off - (name_off + strlen(fsbuf->head + name_off) + 1);
	*tag = (kids_off << FS_TAG_BITS) + FS_TAG_DIR;
}

void set_kids_off(fs_buf *fsbuf, uint32_t name_off, uint32_t kids_off)
{
	pthread_rwlock_wrlock(&fsbuf->lock);
	do_set_kids_off(fsbuf, name_off, kids_off);
	pthread_rwlock_unlock(&fsbuf->lock);
}

static uint32_t get_reloff_by_tag(fs_buf *fsbuf, uint32_t tag_off)
{
	uint32_t *tag = (uint32_t *)(fsbuf->head + tag_off);
	return (*tag - (*tag & FS_TAG_MASK)) >> FS_TAG_BITS;
}

static char *do_get_path_by_name_off(fs_buf *fsbuf, uint32_t name_off, char *path, uint32_t path_size)
{
	// dst用于存储文件路径，从后往前写入整个文件全路径，-1是为了保证末尾存在'\0'字符
	char *src = fsbuf->head + name_off, *dst = path + path_size - strlen(src) - 1;
	strcpy(dst, src);
	while (1)
	{
		if (*src != 0)
		{
			src = fsbuf->head + next_name(fsbuf, src - fsbuf->head);
			continue;
		}

		src++;
		uint32_t rel_off = get_reloff_by_tag(fsbuf, src - fsbuf->head);
		// we have reached the root
		if (rel_off == 0)
			break;

		src = src - rel_off;
		dbg_msg("name: %s, offset: %'ld\n", src, src - fsbuf->head);
		dst--;
		*dst = '/';
		dst -= strlen(src);
		memcpy(dst, src, strlen(src));
	}

	dst -= (fsbuf->first_name_off - DATA_START - 1);
	memcpy(dst, fsbuf->head + DATA_START, fsbuf->first_name_off - DATA_START - 1);
	return dst;
}

__attribute__((visibility("default"))) char *get_path_by_name_off(fs_buf *fsbuf, uint32_t name_off, char *path, uint32_t path_size)
{
	pthread_rwlock_rdlock(&fsbuf->lock);
	char *dst = do_get_path_by_name_off(fsbuf, name_off, path, path_size);
	pthread_rwlock_unlock(&fsbuf->lock);
	return dst;
}

__attribute__((visibility("default"))) int save_fs_buf(fs_buf *fsbuf, const char *filename)
{
	int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return 1;

	pthread_rwlock_rdlock(&fsbuf->lock);
	memcpy(fsbuf->head, fsbuf_magic, strlen(fsbuf_magic) + 1);
	memcpy(fsbuf->head + strlen(fsbuf_magic) + 1, &fsbuf->tail, sizeof(fsbuf->tail));

	if (write_file(fd, fsbuf->head, fsbuf->tail) != 0)
	{
		pthread_rwlock_unlock(&fsbuf->lock);
		close(fd);
		return 2;
	}
	pthread_rwlock_unlock(&fsbuf->lock);

	close(fd);
	return 0;
}

__attribute__((visibility("default"))) int load_fs_buf(fs_buf **pfsbuf, const char *filename)
{
	int fd = open(filename, O_RDONLY);
	if (fd < 0)
		return 1;

	char magic[4];
	if (read(fd, magic, sizeof(magic)) != sizeof(magic) || memcmp(magic, fsbuf_magic, sizeof(magic)) != 0)
	{
		close(fd);
		return 2;
	}

	uint32_t size;
	if (read(fd, &size, sizeof(size)) != sizeof(size) || size < sizeof(uint32_t) * 2 + 5)
	{
		close(fd);
		return 3;
	}

	fs_buf *fsbuf = malloc(sizeof(fs_buf));
	if (fsbuf == 0)
	{
		close(fd);
		return 4;
	}

	if (pthread_rwlock_init(&fsbuf->lock, 0) != 0)
	{
		free(fsbuf);
		close(fd);
		return 5;
	}

	fsbuf->head = malloc(size);
	if (fsbuf->head == 0)
	{
		pthread_rwlock_destroy(&fsbuf->lock);
		free(fsbuf);
		close(fd);
		return 6;
	}

	posix_fadvise(fd, sizeof(uint32_t) * 2, 0, POSIX_FADV_SEQUENTIAL);

	if (read_file(fd, fsbuf->head + sizeof(uint32_t) * 2, size - sizeof(uint32_t) * 2) != 0)
	{
		free(fsbuf->head);
		pthread_rwlock_destroy(&fsbuf->lock);
		free(fsbuf);
		close(fd);
		return 7;
	}

	close(fd);

	fsbuf->capacity = fsbuf->tail = size;
	fsbuf->first_name_off = DATA_START + strlen(fsbuf->head + DATA_START) + 1;
	*pfsbuf = fsbuf;
	return 0;
}

// return 0 means file, no-kid or parent node
static uint32_t get_kids_offset(fs_buf *fsbuf, uint32_t name_off)
{
	if (do_is_file(fsbuf, name_off))
		return 0;

	uint32_t tag_off = name_off + strlen(fsbuf->head + name_off) + 1;
	uint32_t rel_off = get_reloff_by_tag(fsbuf, tag_off);
	if (rel_off == 0)
		return 0;

	return tag_off + rel_off;
}

// return 0 means not-found, DATA_START means root
static uint32_t get_path_offset(fs_buf *fsbuf, char *path)
{
	if (*path == 0 && fsbuf->first_name_off == DATA_START + 2)
		return DATA_START;

	if (strstr(path, fsbuf->head + DATA_START) != path)
		return 0;

	char *p = path + (fsbuf->first_name_off - DATA_START - 1);
	if (*p == 0)
		return DATA_START;

	uint32_t offset = fsbuf->first_name_off;
	while (offset < fsbuf->tail)
	{
		char *name = fsbuf->head + offset;
		if (*name == 0) // parent-tag met, not found
			return 0;

		size_t name_len = strlen(name);
		if (strstr(p, name) == p && (p[name_len] == 0 || p[name_len] == '/'))
		{
			if (p[name_len] == 0)
				return offset;
			// p[name_len] == '/'
			uint32_t kids_off = get_kids_offset(fsbuf, offset);
			if (kids_off == 0)
				return 0;
			p += name_len + 1;
			offset = kids_off;
		}
		else
			offset = next_name(fsbuf, offset);
	}
	return 0;
}

// recursively get last-kids-off
static uint32_t get_tree_end_offset(fs_buf *fsbuf, uint32_t start_off)
{
	uint32_t name_off = start_off, last_kids_off = 0;
	while (name_off < fsbuf->tail)
	{
		if (*(fsbuf->head + name_off))
		{
			uint32_t kids_off = get_kids_offset(fsbuf, name_off);
			if (kids_off)
				last_kids_off = kids_off;
			name_off = next_name(fsbuf, name_off);
			continue;
		}

		if (last_kids_off)
		{
			name_off = last_kids_off;
			last_kids_off = 0;
			continue;
		}

		return name_off + 1 + sizeof(uint32_t);
	}

	return fsbuf->tail;
}

// any silbing folder's kids which(the silbing folder) is after the empty-folder
// or any ancestor's silbing folder's kids which(the ancestor's silbling folder) is after the ancestor folder
static uint32_t get_insert_offset(fs_buf *fsbuf, uint32_t empty_folder_off)
{
	uint32_t name_off = empty_folder_off, off = name_off;
	while (off < fsbuf->tail)
	{
		if (*(fsbuf->head + off) == 0)
		{
			uint32_t rel_off = get_reloff_by_tag(fsbuf, off + 1);
			if (rel_off == 0) // root met
				return fsbuf->tail;
			name_off = off + 1 - rel_off;
			off = name_off;
			continue;
		}

		if (off > name_off)
		{
			uint32_t kids_off = get_kids_offset(fsbuf, off);
			if (kids_off)
				return kids_off;
		}
		off = next_name(fsbuf, off);
	}
	return fsbuf->tail;
}

static uint32_t get_folder_tail_offset(fs_buf *fsbuf, uint32_t name_off)
{
	while (name_off < fsbuf->tail)
	{
		if (*(fsbuf->head + name_off))
		{
			name_off = next_name(fsbuf, name_off);
			continue;
		}

		return name_off;
	}
	return 0;
}

static uint32_t get_parent_offset(fs_buf *fsbuf, uint32_t name_off)
{
	uint32_t tail = get_folder_tail_offset(fsbuf, name_off);
	if (tail == 0)
		return 0;

	uint32_t rel_off = get_reloff_by_tag(fsbuf, tail + 1);
	if (rel_off == 0) // root met
		return 0;

	return tail + 1 - rel_off;
}

static uint32_t get_1st_sibling_offset(fs_buf *fsbuf, uint32_t name_off)
{
	uint32_t parent_off = get_parent_offset(fsbuf, name_off);
	if (parent_off == 0)
		return fsbuf->first_name_off;

	return get_kids_offset(fsbuf, parent_off);
}

#define UPDATE_KIDS_OFF(fsbuf, off, delta)                                                       \
	do                                                                                           \
	{                                                                                            \
		uint32_t kids_off = get_kids_offset(fsbuf, off);                                         \
		if (kids_off)                                                                            \
		{                                                                                        \
			dbg_msg("update offset: delta: %'d, kid-off: %'u -> %'u, parent: [%'u] %s\n", delta, \
					kids_off, kids_off + delta, off, fsbuf->head + off);                         \
			kids_off += delta;                                                                   \
			do_set_kids_off(fsbuf, off, kids_off);                                               \
			set_parent_offset(fsbuf, get_folder_tail_offset(fsbuf, kids_off), off);              \
		}                                                                                        \
	} while (0);

static void update_offsets(fs_buf *fsbuf, uint32_t start_off, int delta, int update_siblings)
{
	// first update prev sibling dirs' offsets, and then
	uint32_t off = start_off;
	if (update_siblings)
	{
		off = get_1st_sibling_offset(fsbuf, start_off);
		if (off == 0)
			return;

		while (off < start_off)
		{
			UPDATE_KIDS_OFF(fsbuf, off, delta);
			off = next_name(fsbuf, off);
		}
		off = get_parent_offset(fsbuf, start_off);
		start_off = off;
	}

	// recursively update parent's post sibling dirs' offsets
	while (off && off < fsbuf->tail)
	{
		if (*(fsbuf->head + off) == 0)
		{
			uint32_t rel_off = get_reloff_by_tag(fsbuf, off + 1);
			if (rel_off == 0) // root met
				return;
			off = off + 1 - rel_off;
			start_off = off;
			continue;
		}

		if (off > start_off)
			UPDATE_KIDS_OFF(fsbuf, off, delta);
		off = next_name(fsbuf, off);
	}
}

static int do_insert_path(fs_buf *fsbuf, char *path, int is_dir, fs_change *change)
{
	char *last_slash = strrchr(path, '/');
	if (last_slash == 0 || strlen(last_slash) == 1)
		return ERR_NO_PATH;

	*last_slash = 0;
	uint32_t parent_off = get_path_offset(fsbuf, path);
	*last_slash = '/';
	if (parent_off == 0 || (DATA_START != parent_off && do_is_file(fsbuf, parent_off)))
		return ERR_NO_PATH;

	uint32_t kids_off = DATA_START == parent_off ? fsbuf->first_name_off : get_kids_offset(fsbuf, parent_off);
	dbg_msg("parent-off: %u, parent-path: %s, kids-off: %u\n", parent_off, fsbuf->head + parent_off, kids_off);
	// kids_off might be 0 because parent might be an empty folder
	int empty_folder = kids_off == 0;
	if (kids_off)
	{
		while (kids_off < fsbuf->tail && *(fsbuf->head + kids_off))
		{
			if (strcmp(fsbuf->head + kids_off, last_slash + 1) == 0)
				return ERR_PATH_EXISTS;
			kids_off = next_name(fsbuf, kids_off);
		}
	}
	else
	{
		kids_off = get_insert_offset(fsbuf, parent_off);
	}

	// kids_off points to parent-tag of parent node (empty_folder == 0) or a new node (empty_folder == 1)
	int result = insert_new_name(fsbuf, kids_off, last_slash + 1, is_dir, empty_folder);
	if (result)
		return result;

	change->delta = strlen(last_slash + 1) + 1 + (is_dir ? sizeof(uint32_t) : 1);
	if (empty_folder)
	{
		set_parent_offset(fsbuf, kids_off + change->delta, parent_off);
		change->delta += 1 + sizeof(uint32_t);
		do_set_kids_off(fsbuf, parent_off, kids_off);
	}
	else if (DATA_START != parent_off)
	{
		uint32_t tail = get_folder_tail_offset(fsbuf, kids_off);
		set_parent_offset(fsbuf, tail, parent_off);
	}
	change->start_off = kids_off;
	update_offsets(fsbuf, kids_off, change->delta, 1);
	return 0;
}

__attribute__((visibility("default"))) int insert_path(fs_buf *fsbuf, char *path, int is_dir, fs_change *change)
{
	pthread_rwlock_wrlock(&fsbuf->lock);
	int r = do_insert_path(fsbuf, path, is_dir, change);
	pthread_rwlock_unlock(&fsbuf->lock);
	return r;
}

#define COPY_TREE(kids_tree, kids_tree_size, tree_start_off, tree_end_off)     \
	do                                                                         \
	{                                                                          \
		if (kids_tree != 0 && kids_tree_size != 0)                             \
		{                                                                      \
			*kids_tree_size = tree_end_off - tree_start_off;                   \
			*kids_tree = malloc(*kids_tree_size);                              \
			if (*kids_tree == 0)                                               \
				return ERR_NO_MEM;                                             \
			memcpy(*kids_tree, fsbuf->head + tree_start_off, *kids_tree_size); \
		}                                                                      \
	} while (0);

static int do_remove_path(fs_buf *fsbuf, char *path, fs_change *changes, uint32_t *change_count, char **kids_tree, uint32_t *kids_tree_size)
{
	uint32_t name_off = get_path_offset(fsbuf, path);
	if (name_off == 0)
		return ERR_NO_PATH;

	if (name_off == DATA_START)
	{ // remove whole tree
		dbg_msg("remove whole tree: [%d] %s, [%d, %d]\n", name_off, path, fsbuf->first_name_off, fsbuf->tail);
		COPY_TREE(kids_tree, kids_tree_size, fsbuf->first_name_off, fsbuf->tail);
		changes[0].start_off = fsbuf->first_name_off;
		changes[0].delta = fsbuf->first_name_off - fsbuf->tail;
		*change_count = 1;
		fsbuf->tail = fsbuf->first_name_off;
		return 0;
	}

	*change_count = 0;
	uint32_t kids_off = get_kids_offset(fsbuf, name_off);
	// folder with kids, remove its children first
	if (kids_off != 0)
	{
		uint32_t tree_end_off = get_tree_end_offset(fsbuf, kids_off);
		dbg_msg("kids-off: %'u, kids-name: %s, tree-end: %'u, next-name: %s\n",
				kids_off, fsbuf->head + kids_off, tree_end_off, fsbuf->head + tree_end_off);

		COPY_TREE(kids_tree, kids_tree_size, kids_off, tree_end_off);

		do_set_kids_off(fsbuf, name_off, 0);
		if (tree_end_off < fsbuf->tail)
			memmove(fsbuf->head + kids_off, fsbuf->head + tree_end_off, fsbuf->tail - tree_end_off);
		fsbuf->tail -= (tree_end_off - kids_off);
		update_offsets(fsbuf, name_off, kids_off - tree_end_off, 0);
		changes[0].start_off = kids_off;
		changes[0].delta = kids_off - tree_end_off;
		*change_count = 1;
	}

	// remove name_off node itself
	uint32_t parent_off = get_parent_offset(fsbuf, name_off), sibling1 = get_1st_sibling_offset(fsbuf, name_off);
	uint32_t size = next_name(fsbuf, name_off) - name_off;
	char *name = fsbuf->head + name_off + size;
	int only_kid = (*name == 0 && sibling1 == name_off);
	if (only_kid)
	{
		size += 1 + sizeof(uint32_t);
		if (parent_off)
			do_set_kids_off(fsbuf, parent_off, 0);
	}
	else if (parent_off)
	{
		uint32_t tail = get_folder_tail_offset(fsbuf, name_off);
		set_parent_offset(fsbuf, tail, parent_off + size);
	}

	if (name_off + size < fsbuf->tail)
		memmove(fsbuf->head + name_off, fsbuf->head + name_off + size, fsbuf->tail - name_off - size);
	fsbuf->tail -= size;
	if (only_kid)
	{
		if (parent_off)
			update_offsets(fsbuf, parent_off, -size, 0);
	}
	else
	{
		update_offsets(fsbuf, name_off, -size, 1);
	}
	changes[*change_count].start_off = name_off;
	changes[*change_count].delta = -size;
	*change_count = *change_count + 1;
	return 0;
}

__attribute__((visibility("default"))) int remove_path(fs_buf *fsbuf, char *path, fs_change *changes, uint32_t *change_count)
{
	pthread_rwlock_wrlock(&fsbuf->lock);
	int r = do_remove_path(fsbuf, path, changes, change_count, 0, 0);
	pthread_rwlock_unlock(&fsbuf->lock);
	return r;
}

// NOTE: Linux rename syscall for folder can only succeed if dst_path doesnt exist or is empty
// so dst_path (if a folder) MUST be empty here
static int do_rename_path(fs_buf *fsbuf, char *src_path, char *dst_path, fs_change *changes, uint32_t *change_count)
{
	if (strstr(dst_path, src_path) == dst_path)
		return ERR_NESTED;

	uint32_t src_off = get_path_offset(fsbuf, src_path);
	if (src_off == 0)
		return ERR_NO_PATH;

	uint32_t dst_off = get_path_offset(fsbuf, dst_path);
	if (dst_off == DATA_START) // we DONT support rename to root
		return ERR_NESTED;

	int src_is_file = do_is_file(fsbuf, src_off);
	if (dst_off != 0 && do_is_file(fsbuf, dst_off) != src_is_file)
		return ERR_PATH_DIFFER;

	if (dst_off != 0 && !src_is_file && get_kids_offset(fsbuf, dst_off) != 0)
		return ERR_NOTEMPTY;

	char *last_slash = strrchr(dst_path, '/');
	if (last_slash == 0 || strlen(last_slash) == 1)
		return ERR_NO_PATH;

	*last_slash = 0;
	uint32_t dst_parent_off = get_path_offset(fsbuf, dst_path);
	*last_slash = '/';
	if (dst_parent_off == 0 || (dst_parent_off == DATA_START && do_is_file(fsbuf, dst_parent_off)))
		return ERR_NO_PATH;

	// folder with kids, backup its kids first
	char *old_kids_tree = 0;
	uint32_t tree_size = 0;
	int result = do_remove_path(fsbuf, src_path, changes, change_count, &old_kids_tree, &tree_size);
	if (result != 0)
		return result;

	if (dst_off == 0)
	{
		// we assume insert_path will always succeed
		do_insert_path(fsbuf, dst_path, !src_is_file, changes + *change_count);
		*change_count = *change_count + 1;
	}
	// do_remove_path might change dst_off, so we must get it again
	dst_off = get_path_offset(fsbuf, dst_path);

	// copy back kids tree, dst_off must point to a folder
	if (old_kids_tree)
	{
		dbg_msg("old-kids-tree: %p (%s), size: %'u\n", old_kids_tree, old_kids_tree, tree_size);
		if (tree_size + fsbuf->tail >= fsbuf->capacity)
			if (add_capacity(fsbuf, tree_size) != 0)
			{
				free(old_kids_tree);
				return ERR_NO_MEM;
			}

		uint32_t kids_off = get_insert_offset(fsbuf, dst_off);
		if (fsbuf->tail > kids_off)
			memmove(fsbuf->head + kids_off + tree_size, fsbuf->head + kids_off, fsbuf->tail - kids_off);
		memcpy(fsbuf->head + kids_off, old_kids_tree, tree_size);
		free(old_kids_tree);
		fsbuf->tail += tree_size;
		// set kids-off, parent-off & update-offsets
		do_set_kids_off(fsbuf, dst_off, kids_off);
		set_parent_offset(fsbuf, get_folder_tail_offset(fsbuf, kids_off), dst_off);
		update_offsets(fsbuf, kids_off, tree_size, 1);

		changes[*change_count].start_off = kids_off;
		changes[*change_count].delta = tree_size;
		*change_count = *change_count + 1;
	}

	return 0;
}

__attribute__((visibility("default"))) void get_path_range(fs_buf *fsbuf, char *path, uint32_t *path_off, uint32_t *start_off, uint32_t *end_off)
{
	pthread_rwlock_rdlock(&fsbuf->lock);
	*path_off = get_path_offset(fsbuf, path);
	if (*path_off != 0)
	{
		// 对于此索引数据的根目录没有保存它的目录文件起始信息
		if (*path_off == DATA_START) {
			*start_off = first_name(fsbuf);
			*end_off = get_tail(fsbuf);
		} else {
			*start_off = get_kids_offset(fsbuf, *path_off);
			*end_off = get_tree_end_offset(fsbuf, *start_off);
		}
	}
	pthread_rwlock_unlock(&fsbuf->lock);
}

__attribute__((visibility("default"))) int rename_path(fs_buf *fsbuf, char *src_path, char *dst_path, fs_change *changes, uint32_t *change_count)
{
	pthread_rwlock_wrlock(&fsbuf->lock);
	int r = do_rename_path(fsbuf, src_path, dst_path, changes, change_count);
	pthread_rwlock_unlock(&fsbuf->lock);
	return r;
}

__attribute__((visibility("default"))) void search_files(fs_buf *fsbuf, uint32_t *start_off, uint32_t end_off, void *param, int (*comparator)(const char *, void *), uint32_t *results, uint32_t *count)
{
	uint32_t size = *count;
	*count = 0;
	pthread_rwlock_rdlock(&fsbuf->lock);
	uint32_t name_off = *start_off, min_off = fsbuf->tail > end_off ? end_off : fsbuf->tail;

	while (name_off < min_off && *count < size)
	{
		char *name = fsbuf->head + name_off;
		if (*name != 0 && (*comparator)(name, param) != 0)
		{
			results[*count] = name_off;
			*count = *count + 1;
		}
		name_off = next_name(fsbuf, name_off);
	}
	pthread_rwlock_unlock(&fsbuf->lock);
	*start_off = name_off;
}
