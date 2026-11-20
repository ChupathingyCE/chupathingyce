/*
H2_CACHE.C

A retail Xbox Halo 2 cache file read for the game (h2_cache.h).

The header (0x800 bytes, little endian): 'head' (stored "daeh"), version 8,
the file's length, the tag index's offset and size at 0x10/0x14, the tag
data's size and the resident size at 0x18/0x1C, the build at 0x120, the
cache's role at 0x140, the name at 0x198, the scenario's path at 0x1BC, the
tag names' count, data and offsets at 0x2C0/0x2C4/0x2CC, 'foot' at 0x7FC.
The index is read to H2_CACHE_BASE_ADDRESS; the tag data to the top of the
resident span (base + resident - tag data size), the bytes between being the
structure BSP's room. The index's first words: the groups, their count, the
tag entries, the scenario's and globals' handles, the tag count and 'tags'.
A tag entry is 16 bytes: group, salted handle, address, size. A structure
descriptor (the scenario's block at 0x210, 0x44 bytes each) gives the BSP
block's file offset, size and load address and the structure BSP and
lightmap tags; the block's own header has the structure BSP's address at +4,
the lightmap's at +8 and 'sbsp' at +0xC. A resource reference's top two
bits pick the file: 0 this map, 1 mainmenu.map, 2 shared.map,
3 single_player_shared.map; its low 30 bits are the offset.
(halo2-decomp: include/halo2/cache_layout.h, docs/CACHE-MAPS.md,
docs/TAGS-OBJECTS.md.)
*/

/* ---------- headers */

#include "h2_cache.h"
#include "h2_tag_dump.h"
#include "../../../third_party/zlib/zlib_prefixed.h"
/* (the inflates whose sizes are known: libdeflate's decompressor, inflate_whole) */
/* (port) Project Ark inflates these with libdeflate when it has it; Halo 2
mode has zlib alone for now (HALO_H2_LIBDEFLATE) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdarg.h>
#include <errno.h>

/* (the C library's allocator, not cseries.h's debug one, whose free takes
no NULL and which calloc is not: this file's memory is all its own) */
#undef malloc
#undef free
#undef realloc

/* (the Vita tier: what a level's build allocates is the builder's own
heap's, not the C heap's - h2_build_heap.h) */
#define H2_BUILD_HEAP_WRAP
#include "h2_build_heap.h"

int halo_thread_index(void);
int halo_game_thread_index(void);

/* (the game's callers' stacks are aligned to 4 bytes, as the Xbox's were;
this file's SSE moves want 16: every function realigns its own) */
#define H2_ALIGN __attribute__((force_align_arg_pointer))

/* ---------- constants */

enum
{
	H2_HEADER_SIGNATURE = 0x68656164, /* 'head' */
	H2_FOOTER_SIGNATURE = 0x666f6f74, /* 'foot' */
	H2_TAGS_SIGNATURE = 0x74616773,   /* 'tags' */
	H2_SBSP_SIGNATURE = 0x73627370,   /* 'sbsp' */
	H2_SCENARIO_STRUCTURES_OFFSET = 0x210,
	H2_SCENARIO_STRUCTURE_SIZE = 0x44,
	/* the most a map's index, tag data, names or BSP block may be (the
	largest retail map's are a fraction of these) */
	H2_MAXIMUM_INDEX_BYTES = 4 * 1024 * 1024,
	H2_MAXIMUM_TAG_DATA_BYTES = 64 * 1024 * 1024,
	H2_MAXIMUM_NAME_BYTES = 8 * 1024 * 1024,
	H2_MAXIMUM_BSP_BYTES = 64 * 1024 * 1024,
	H2_MAXIMUM_TAGS = 0x10000,
	H2_RESOURCE_FILES = 4,
	H2_MCC_CACHE_VERSION = 13,
	H2_MCC_HEADER_SIZE = 0x380,
	H2_MCC_CHUNK_SIZE = 0x40000,
	/* (a piece this large or more a user RAM block ahead of the C heap
	outside a build: mcc_bytes) */
	H2_MCC_USER_BLOCK_PIECE = 0x20000,
	H2_TEXTURE_CHUNK_BYTES = 0x40000,   /* (textures.dat: each chunk's inflated bytes, the last chunk's the rest) */
	H2_CACHE_CHUNKS_KEPT = 8,         /* (the most; chunks_kept_count: 2 on a small C heap) */
	H2_MCC_MAXIMUM_BYTES = 256 * 1024 * 1024,
	H2_MCC_SEGMENTS_KEPT = 8,
};

/* ---------- structures */

struct h2_cache
{
	char directory[H2_CACHE_NAME_SIZE];
	char name[32];
	char scenario[H2_CACHE_NAME_SIZE];
	FILE *files[H2_RESOURCE_FILES];
	unsigned long file_sizes[H2_RESOURCE_FILES];
	boolean file_tried[H2_RESOURCE_FILES];

	unsigned char *index;
	unsigned long index_size;
	unsigned char *tag_data;
	unsigned long tag_data_size;
	unsigned long tag_data_address;

	unsigned char *names;
	unsigned long names_size;
	unsigned long *name_offsets;
	/* (h2_cache_tag_find) the tags by name: a hash of each name, its
	chain's first tag (the lowest index) and each tag's next of the same
	hash - made at the first find; NULL none yet */
	long *name_heads, *name_next;
	unsigned long name_hash_mask;
	boolean name_index_tried;

	long tag_count;
	unsigned long tags_address;
	unsigned long scenario_handle;

	unsigned char *bsp;
	unsigned long bsp_size;
	unsigned long bsp_address;

	/* (MCC's Halo 2: the map decompressed whole, its index and tags in it;
	the shared maps' tags, which this map's table lists without data;
	textures.dat, where the bitmaps' pixels are) */
	boolean mcc;
	unsigned char *whole;
	unsigned long whole_size;
	struct h2_cache *shared[H2_RESOURCE_FILES];
	boolean shared_tried[H2_RESOURCE_FILES];
	FILE *textures;
	unsigned char *touched;     /* (HALO_H2_DEBUG: the chunks read, a byte each) */
	/* (MCC: the decompressed file read lazily - its header, its chunks'
	table; segments of whole chunks inflated when first touched, kept for
	the cache's life so what h2_cache_at gave stays) */
	unsigned char header_bytes[H2_MCC_HEADER_SIZE];
	unsigned long chunk_count;
	unsigned long *chunk_sizes, *chunk_offsets;
	struct mcc_segment { unsigned long start, size; unsigned char *data; boolean stress; } *segments;
	unsigned long index_offset;
	long segment_count, segment_capacity, segment_last;
	long globals_index;
	boolean globals_found;
	unsigned char *compressed;  /* a chunk's compressed bytes, read */
	/* (h2_cache_resource_read) the last chunks inflated for raw resources
	and what each gave: the next resource in one of them is copied out of
	it - a BSP's sections lie side by side, and each read inflated its
	chunk again (and took a scratch chunk from the C heap each time); the
	models made at load (h2_models_load.inc) come back to a chunk after
	others - Cairo's gathers inflated 417 chunks where 160 were distinct -,
	so H2_CACHE_CHUNKS_KEPT of them are kept, the least recently used given
	up, each 256 KB of the C heap only while the cache is open */
	struct
	{
		unsigned char *data;
		unsigned long index, got, used;
	} chunks_kept[H2_CACHE_CHUNKS_KEPT];
	unsigned long chunk_use_stamp;
	/* (h2_cache_segment_memory) where the segments come from: NULL the C heap */
	void *(*segment_alloc)(unsigned long size);
	void (*segment_free)(void *data, unsigned long size);
	/* (h2_cache_segment_budget) the segments' bytes past which the oldest
	are let go (0: kept for the cache's life), what they hold now, and how
	many chunks were inflated (again) */
	unsigned long segment_budget, segment_bytes, inflated, evicted;
	/* (h2_cache_memory_failed) a segment or its table not made for want of
	memory: a read of the tags silently gave nothing */
	boolean memory_failed;
	/* (h2_cache_open_view) a view of another's open: its names, index
	offsets and chunk tables are that one's, read only; its files, scratch
	and segments its own */
	boolean view;
	char path[H2_CACHE_NAME_SIZE + 64];   /* (the map's file, as opened) */
};

/* ---------- private code */

H2_ALIGN static void segments_release_all(struct h2_cache *cache);

/* (texture_read) textures.dat's reads, one thread at a time */
static pthread_mutex_t textures_lock = PTHREAD_MUTEX_INITIALIZER;

H2_ALIGN static unsigned long read_le32(
	unsigned char const *p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

/* (piece H) the maps' and textures.dat's reads and inflates, every thread's
(h2_cache_io_counts): what a load waits on in the card's reads, logged by
the load's steps and the make's profile */
unsigned long long vita_host_time_us(void);
static struct
{
	unsigned long reads, read_bytes, read_us;
	unsigned long inflates, inflate_us;
} io;

H2_ALIGN static void io_inflated(unsigned long long since)
{
	__atomic_add_fetch(&io.inflates, 1UL, __ATOMIC_RELAXED);
	__atomic_add_fetch(&io.inflate_us, (unsigned long)(vita_host_time_us() - since), __ATOMIC_RELAXED);
}

H2_ALIGN void h2_cache_io_counts(
	unsigned long *reads,
	unsigned long *read_bytes,
	unsigned long *read_us,
	unsigned long *inflates,
	unsigned long *inflate_us)
{
	*reads = __atomic_load_n(&io.reads, __ATOMIC_RELAXED);
	*read_bytes = __atomic_load_n(&io.read_bytes, __ATOMIC_RELAXED);
	*read_us = __atomic_load_n(&io.read_us, __ATOMIC_RELAXED);
	*inflates = __atomic_load_n(&io.inflates, __ATOMIC_RELAXED);
	*inflate_us = __atomic_load_n(&io.inflate_us, __ATOMIC_RELAXED);
}

/* (h2_texture_source.c's own textures.dat reads: counted with these) */
H2_ALIGN void h2_cache_io_read_noted(
	unsigned long bytes,
	unsigned long microseconds)
{
	__atomic_add_fetch(&io.reads, 1UL, __ATOMIC_RELAXED);
	__atomic_add_fetch(&io.read_bytes, bytes, __ATOMIC_RELAXED);
	__atomic_add_fetch(&io.read_us, microseconds, __ATOMIC_RELAXED);
}

/* (a map's or a .dat's file, read at offsets) its reads straight from the
card: newlib's stdio on the Vita buffers 1 KB at a time (BUFSIZ, no block
size from the card's stat), so a 36 KB read was 36 reads of the card -
the make of 01b read its 138 MB at 1.7 MB/s on the dev Vita, 21 ms a read,
and 05a's load took 334 s on the owner's. Unbuffered, a read is one */
H2_ALIGN void h2_file_unbuffered(
	FILE *file)
{
	if (file)
		setvbuf(file, NULL, _IONBF, 0);
}

H2_ALIGN static boolean file_read_at(
	FILE *file,
	unsigned long offset,
	void *out,
	unsigned long size)
{
	unsigned long long started = vita_host_time_us();
	boolean ok = fseek(file, (long)offset, SEEK_SET) == 0 && fread(out, 1, size, file) == size;

	__atomic_add_fetch(&io.reads, 1UL, __ATOMIC_RELAXED);
	__atomic_add_fetch(&io.read_bytes, size, __ATOMIC_RELAXED);
	__atomic_add_fetch(&io.read_us, (unsigned long)(vita_host_time_us() - started), __ATOMIC_RELAXED);
	return ok;
}

/* (the lock held) a textures.dat entry's head - its chunk count, then each
chunk's size - read at `reference` in one read with what follows it: a
small entry's whole (TEXTURE_ENTRY_WHOLE bytes or fewer), else room for
TEXTURE_HEAD_GUESS chunks' sizes; a second read only for a head longer than
that. Two reads an entry were one (the count's 4 bytes, then the head) and
a small entry's chunk a third: each a card access on the Vita, thousands a
level's load. NULL when the head cannot be read or is not an entry's (a
chunk count of 0 or 4096 and over, a head past `size`); *chunks the count,
*held the bytes of the entry held from its start (the head's at least) */
enum
{
	TEXTURE_ENTRY_WHOLE = 32 * 1024,
	TEXTURE_HEAD_GUESS = 64,
};

H2_ALIGN static unsigned char *texture_head_read(
	FILE *file,
	unsigned long reference,
	unsigned long size,
	unsigned long *chunks,
	unsigned long *held)
{
	unsigned long want = size <= TEXTURE_ENTRY_WHOLE ? size : 4 + 4 * TEXTURE_HEAD_GUESS, count;
	unsigned char *head;

	*chunks = *held = 0;
	if (want > size)
		want = size;
	if (want < 4 || !(head = malloc(want)))
		return NULL;
	if (!file_read_at(file, reference, head, want))
	{
		free(head);
		return NULL;
	}
	count = read_le32(head);
	if (!count || count >= 4096 || 4 + 4 * count > size)
	{
		free(head);
		return NULL;
	}
	if (4 + 4 * count > want)
	{
		unsigned char *whole = realloc(head, 4 + 4 * count);

		if (!whole || !file_read_at(file, reference + want, whole + want, 4 + 4 * count - want))
		{
			free(whole ? whole : head);
			return NULL;
		}
		head = whole;
		want = 4 + 4 * count;
	}
	*chunks = count;
	*held = want;
	return head;
}

H2_ALIGN static void why_set(
	char *why,
	long why_size,
	char const *text)
{
	if (why && why_size > 0)
	{
		strncpy(why, text, why_size - 1);
		why[why_size - 1] = 0;
	}
}

H2_ALIGN static FILE *resource_file(
	struct h2_cache *cache,
	unsigned long selector)
{
	static char const *const names[H2_RESOURCE_FILES] = { NULL, "mainmenu", "shared", "single_player_shared" };

	if (selector >= H2_RESOURCE_FILES)
		return NULL;
	if (!cache->files[selector] && !cache->file_tried[selector] && names[selector])
	{
		char path[H2_CACHE_NAME_SIZE + 64];

		cache->file_tried[selector] = TRUE;
		snprintf(path, sizeof(path), "%s%s.map", cache->directory, names[selector]);
		/* (a shared map is used only if it is one) */
		if (h2_cache_file_is_halo2(path))
		{
			cache->files[selector] = fopen(path, "rb");
			h2_file_unbuffered(cache->files[selector]);
			if (cache->files[selector] && fseek(cache->files[selector], 0, SEEK_END) == 0)
				cache->file_sizes[selector] = (unsigned long)ftell(cache->files[selector]);
		}
	}

	return cache->files[selector];
}

/* ---------- public code */

H2_ALIGN boolean h2_cache_file_is_halo2(
	char const *path)
{
	unsigned char header[8];
	FILE *file = fopen(path, "rb");
	boolean result = FALSE;

	if (file)
	{
		unsigned char more[0x100];

		result = fread(header, 1, sizeof(header), file) == sizeof(header) &&
			read_le32(header) == H2_HEADER_SIGNATURE &&
			(read_le32(header + 4) == H2_CACHE_VERSION ||
				/* (MCC's Halo 2, version 13: its scenario's path at 0xD0, as
				Halo 3's version 13 maps have theirs elsewhere) */
				(read_le32(header + 4) == H2_MCC_CACHE_VERSION &&
					fseek(file, 0xD0, SEEK_SET) == 0 && fread(more, 1, 10, file) == 10 &&
					!memcmp(more, "scenarios\\", 10)));
		fclose(file);
	}

	return result;
}

H2_ALIGN boolean h2_cache_file_is_mcc(
	char const *path)
{
	unsigned char header[8];
	FILE *file = fopen(path, "rb");
	boolean result = FALSE;

	if (file)
	{
		result = fread(header, 1, sizeof(header), file) == sizeof(header) &&
			read_le32(header) == H2_HEADER_SIGNATURE && read_le32(header + 4) == H2_MCC_CACHE_VERSION;
		fclose(file);
	}

	return result;
}

/* MCC's Halo 2 map (version 13): its header (0x380 bytes) as it is, then
its chunks - each a 2-byte zlib header and a raw deflate stream of 0x40000
bytes - listed (compressed size, offset) by the chunk table at +0x310,
their count at +0x314; decompressed, the index at +0x10, a tag at its
address less the mask at +0x2D0 plus the index's offset, the tag names'
count, data, size and offsets at +0x20..+0x2C, the name at +0xB0, the
scenario's path at +0xD0. (Read from the owner's MCC files; Assembly's
Blamite description of the format as a guide, no code of it.) */

/* (MCC's chunk table) a chunk stored, not deflated: its size negative, the
bytes it holds -size (0x40000, a whole chunk) */
H2_ALIGN static boolean mcc_chunk_stored(unsigned long size)
{
	return (size & 0x80000000UL) && (unsigned long)-(long)size <= H2_MCC_CHUNK_SIZE;
}

H2_ALIGN static unsigned long mcc_chunk_stored_size(unsigned long size)
{
	return (unsigned long)-(long)size;
}

/* the bytes a chunk's entry says it has in the file */
H2_ALIGN static unsigned long mcc_chunk_file_bytes(unsigned long size)
{
	return mcc_chunk_stored(size) ? mcc_chunk_stored_size(size) : size;
}

H2_ALIGN static unsigned char *mcc_decompress(FILE *file, unsigned long file_size, unsigned long *size)
{
	unsigned char header[H2_MCC_HEADER_SIZE];
	unsigned long table, count, total, done, i;
	unsigned char *out = NULL, *chunk = NULL;

	*size = 0;
	if (!file_read_at(file, 0, header, sizeof(header)))
		return NULL;
	total = read_le32(header + 8);
	table = read_le32(header + 0x310);
	count = read_le32(header + 0x314);
	if (total < H2_MCC_HEADER_SIZE || total > H2_MCC_MAXIMUM_BYTES || table > file_size || count > file_size / 8 ||
		count * 8 > file_size - table)
	{
		return NULL;
	}
	out = malloc(total);
	chunk = malloc(H2_MCC_CHUNK_SIZE * 2);
	if (!out || !chunk)
		goto failed;
	memcpy(out, header, sizeof(header));
	done = H2_MCC_HEADER_SIZE;
	for (i = 0; i < count; i++)
	{
		unsigned char entry[8];
		unsigned long csize, offset;
		z_stream stream;
		int status;

		if (!file_read_at(file, table + 8 * i, entry, 8))
			goto failed;
		csize = read_le32(entry);
		offset = read_le32(entry + 4);
		if (mcc_chunk_stored(csize))
		{
			unsigned long n = mcc_chunk_stored_size(csize);

			if (offset > file_size || n > file_size - offset || n > total - done || !file_read_at(file, offset, out + done, n))
				goto failed;
			done += n;
			continue;
		}
		if (csize < 2 || csize > H2_MCC_CHUNK_SIZE * 2 || offset > file_size || csize > file_size - offset ||
			!file_read_at(file, offset, chunk, csize))
		{
			goto failed;
		}
		memset(&stream, 0, sizeof(stream));
		if (inflateInit2(&stream, -15) != Z_OK)
			goto failed;
		stream.next_in = chunk + 2;
		stream.avail_in = (uInt)(csize - 2);
		stream.next_out = out + done;
		stream.avail_out = (uInt)(total - done);
		status = inflate(&stream, Z_FINISH);
		done += stream.total_out;
		inflateEnd(&stream);
		if (status != Z_STREAM_END && status != Z_BUF_ERROR)
			goto failed;
	}
	free(chunk);
	if (done != total)
	{
		free(out);
		return NULL;
	}
	*size = total;
	return out;

failed:
	free(chunk);
	free(out);
	return NULL;
}

H2_ALIGN static struct h2_cache *mcc_open(char const *maps_directory, char const *name, boolean shared_maps,
	char *why, long why_size);

/* ---------- (port) the chunks inflated, kept across trims (piece H)

A level's make reads an MCC map's chunks through segments it lets go
between its gathers (h2_cache_trim: the memory window's room), and the next
gather inflates many of them again - Lockout's make inflated 251 chunks, 113
of them distinct. Between h2_cache_chunks_begin and h2_cache_chunks_end (the
make) every chunk inflated whole is kept as well, in the build's heap (user
RAM on the Vita, not the C heap nor the window), the least recently used let
go past the budget; an inflate of a chunk kept is a copy of it. Keyed by the
map's name and size and the chunk's number, any open or view of the map, on
any thread, finds it. The same bytes: a chunk is kept only once inflated to
its stream's end. HALO_H2_CHUNKS_KEPT_MB: the budget (16 MB; 0 none). */
void platform_log(const char *format, ...);
void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size);

/* textures.dat opened for a cache (the folder its map is in, else Halo 2's
own, else the maps folder: h2_level.c h2_resource_directory): the first
open that works and the first that fails said in the log - the path tried,
the card's path and errno -, once a run each (a failure was only "not open",
at every texture: the tester's card of insider.5) */
H2_ALIGN static FILE *textures_dat_open(char const *directory)
{
	static int said_open, said_failed;
	char const *h2_resource_directory(char const *preferred, char const *file);
	char path[H2_CACHE_NAME_SIZE + 64], host[1024];
	FILE *file;
	int code;

	snprintf(path, sizeof(path), "%stextures.dat", h2_resource_directory(directory, "textures.dat"));
	errno = 0;
	file = fopen(path, "rb");
	code = errno;
	h2_file_unbuffered(file);
	if (file ? !said_open : !said_failed)
	{
		host[0] = 0;
		platform_translate_path(path, host, sizeof(host));
		if (file)
		{
			said_open = 1;
			platform_log("h2 cache: textures.dat open: %s (%s)", path, host);
		}
		else
		{
			said_failed = 1;
			platform_log("h2 cache: textures.dat NOT opened: %s (%s): errno %d (%s)%s", path, host, code, strerror(code),
				code == ENOENT ? " - no such file there: Setup copies it beside the maps" : "");
		}
	}
	return file;
}

struct kept_chunk
{
	char map[32];
	unsigned long whole_size, k, size, used;
	unsigned char *data;
};

static struct
{
	pthread_mutex_t lock;
	boolean on;
	struct kept_chunk *chunks;
	long count, capacity;
	unsigned long bytes, budget, stamp;
	unsigned long hits, stored, let_go, pressed;
} chunks_kept = { PTHREAD_MUTEX_INITIALIZER };

/* (the build's heap had no room: h2_build_heap_pressure) every chunk kept let
go, and the budget halved - what the make needs comes first; nothing if
another thread is at the chunks (it lets go of its own room) */
H2_ALIGN static void chunks_kept_release(void)
{
	long i;

	if (pthread_mutex_trylock(&chunks_kept.lock) != 0)
		return;
	for (i = 0; i < chunks_kept.count; i++)
		free(chunks_kept.chunks[i].data);
	chunks_kept.let_go += (unsigned long)chunks_kept.count;
	chunks_kept.count = 0;
	chunks_kept.bytes = 0;
	chunks_kept.budget /= 2;
	chunks_kept.pressed++;
	pthread_mutex_unlock(&chunks_kept.lock);
}

/* (h2_build_heap.c's peak sampler) the kept chunks' bytes now: memory the
make gives back when the heap has none - elastic, not the make's own */
unsigned long h2_cache_chunks_kept_bytes(void)
{
	return __atomic_load_n(&chunks_kept.bytes, __ATOMIC_RELAXED);
}

H2_ALIGN void h2_cache_chunks_begin(
	void)
{
	char const *setting = getenv("HALO_H2_CHUNKS_KEPT_MB");
	unsigned long budget = (setting ? (unsigned long)atol(setting) : 16UL) * 1024UL * 1024UL;

	h2_cache_chunks_end();
	pthread_mutex_lock(&chunks_kept.lock);
	chunks_kept.capacity = (long)(budget / H2_MCC_CHUNK_SIZE) + 1;
	chunks_kept.chunks = budget ? calloc((size_t)chunks_kept.capacity, sizeof(*chunks_kept.chunks)) : NULL;
	chunks_kept.budget = budget;
	chunks_kept.on = chunks_kept.chunks != NULL;
	pthread_mutex_unlock(&chunks_kept.lock);
	/* (let go when the build's heap has no room for what the make asks) */
	if (chunks_kept.on)
		h2_build_heap_pressure_add(chunks_kept_release);
}

H2_ALIGN void h2_cache_chunks_end(
	void)
{
	long i;

	h2_build_heap_pressure_remove(chunks_kept_release);
	pthread_mutex_lock(&chunks_kept.lock);
	if (chunks_kept.on)
		platform_log("h2 cache: the chunks kept through the make: %lu stored, %lu found again, %lu let go (%lu times all for the build's "
			"heap)", chunks_kept.stored, chunks_kept.hits, chunks_kept.let_go, chunks_kept.pressed);
	for (i = 0; i < chunks_kept.count; i++)
		free(chunks_kept.chunks[i].data);
	free(chunks_kept.chunks);
	chunks_kept.chunks = NULL;
	chunks_kept.count = chunks_kept.capacity = 0;
	chunks_kept.bytes = chunks_kept.budget = chunks_kept.stamp = 0;
	chunks_kept.hits = chunks_kept.stored = chunks_kept.let_go = chunks_kept.pressed = 0;
	chunks_kept.on = FALSE;
	pthread_mutex_unlock(&chunks_kept.lock);
}

/* chunk k of cache's map, if kept: room bytes of it at most into out, how
many (0: not kept) */
H2_ALIGN static unsigned long chunk_kept_copy(struct h2_cache *cache, unsigned long k, unsigned char *out, unsigned long room)
{
	unsigned long got = 0;
	long i;

	if (!__atomic_load_n(&chunks_kept.on, __ATOMIC_ACQUIRE))
		return 0;
	pthread_mutex_lock(&chunks_kept.lock);
	for (i = 0; chunks_kept.on && i < chunks_kept.count; i++)
	{
		struct kept_chunk *c = &chunks_kept.chunks[i];

		if (c->k == k && c->whole_size == cache->whole_size && !strcmp(c->map, cache->name))
		{
			got = c->size < room ? c->size : room;
			memcpy(out, c->data, got);
			c->used = ++chunks_kept.stamp;
			chunks_kept.hits++;
			break;
		}
	}
	pthread_mutex_unlock(&chunks_kept.lock);
	return got;
}

/* chunk k of cache's map, inflated whole (size bytes at data), kept */
H2_ALIGN static void chunk_keep(struct h2_cache *cache, unsigned long k, unsigned char const *data, unsigned long size)
{
	long i, slot = NONE;
	unsigned char *copy;

	if (!__atomic_load_n(&chunks_kept.on, __ATOMIC_ACQUIRE) || !size || size > chunks_kept.budget)
		return;
	copy = malloc(size);
	if (!copy)
		return;
	memcpy(copy, data, size);
	pthread_mutex_lock(&chunks_kept.lock);
	for (i = 0; chunks_kept.on && i < chunks_kept.count; i++)
	{
		if (chunks_kept.chunks[i].k == k && chunks_kept.chunks[i].whole_size == cache->whole_size &&
			!strcmp(chunks_kept.chunks[i].map, cache->name))
		{
			break;
		}
	}
	if (!chunks_kept.on || i < chunks_kept.count)
	{
		pthread_mutex_unlock(&chunks_kept.lock);
		free(copy);
		return;
	}
	/* (room: the least recently used let go) */
	while (chunks_kept.count && (chunks_kept.bytes + size > chunks_kept.budget || chunks_kept.count >= chunks_kept.capacity))
	{
		long oldest = 0;

		for (i = 1; i < chunks_kept.count; i++)
			if (chunks_kept.chunks[i].used < chunks_kept.chunks[oldest].used)
				oldest = i;
		chunks_kept.bytes -= chunks_kept.chunks[oldest].size;
		free(chunks_kept.chunks[oldest].data);
		chunks_kept.chunks[oldest] = chunks_kept.chunks[--chunks_kept.count];
		chunks_kept.let_go++;
	}
	slot = chunks_kept.count++;
	snprintf(chunks_kept.chunks[slot].map, sizeof(chunks_kept.chunks[slot].map), "%s", cache->name);
	chunks_kept.chunks[slot].whole_size = cache->whole_size;
	chunks_kept.chunks[slot].k = k;
	chunks_kept.chunks[slot].size = size;
	chunks_kept.chunks[slot].data = copy;
	chunks_kept.chunks[slot].used = ++chunks_kept.stamp;
	chunks_kept.bytes += size;
	chunks_kept.stored++;
	pthread_mutex_unlock(&chunks_kept.lock);
}

/* ---------- (port) inflates by libdeflate (piece H)

An MCC map's chunks and textures.dat's are each one deflate stream whose
compressed bytes are read whole and whose inflated size is known or bounded:
libdeflate inflates such a buffer in one call, faster than zlib's streaming
inflate (MIT, port/third_party/libdeflate, v1.25, its decompressor alone).
The same stream gives the same bytes: a stream libdeflate does not inflate
whole into the room given (too small, damaged) is inflated by zlib, as
before, so what zlib gave in every case is what the caller gets.
HALO_LIBDEFLATE=0: zlib alone. */
H2_ALIGN static boolean libdeflate_on(void)
{
	static int on = -1;

	if (on < 0)
		on = !getenv("HALO_LIBDEFLATE") || atoi(getenv("HALO_LIBDEFLATE")) != 0;
	return on;
}

/* in (bytes) inflated whole into out (room bytes at most): a raw deflate
stream (zlib FALSE) or a zlib one (TRUE: its header and Adler-32 checked);
the bytes made, or -1 if libdeflate did not (then zlib's to do) */
H2_ALIGN static long inflate_whole(unsigned char const *in, unsigned long bytes, unsigned char *out, unsigned long room, boolean zlib)
{
	(void)in; (void)bytes; (void)out; (void)room; (void)zlib;
	(void)libdeflate_on;
	return -1;
}

/* (HALO_INFLATE_BENCH=n: the first n chunks of a map - shared.map - inflated
by zlib and by libdeflate, timed and compared, logged: the two on this
machine's processor) */
unsigned long long vita_host_time_us(void);
void platform_log(const char *format, ...);

H2_ALIGN void h2_cache_inflate_bench(
	char const *maps_directory,
	long count)
{
	char path[H2_CACHE_NAME_SIZE + 64];
	unsigned char header[H2_MCC_HEADER_SIZE];
	unsigned char *in = malloc(H2_MCC_CHUNK_SIZE * 2), *a = malloc(H2_MCC_CHUNK_SIZE), *b = malloc(H2_MCC_CHUNK_SIZE);
	unsigned long long zlib_us = 0, libdeflate_us = 0;
	unsigned long table, chunks, bytes = 0, k;
	long done = 0, differ = 0;
	FILE *file;

	snprintf(path, sizeof(path), "%sshared.map", maps_directory);
	file = fopen(path, "rb");
	if (!file || !in || !a || !b || !file_read_at(file, 0, header, sizeof(header)))
		goto out;
	table = read_le32(header + 0x310);
	chunks = read_le32(header + 0x314);
	for (k = 0; k < chunks && done < count; k++)
	{
		unsigned char entry[8];
		unsigned long csize, offset;
		unsigned long long t;
		z_stream stream;
		long made;

		if (!file_read_at(file, table + 8 * k, entry, 8))
			break;
		csize = read_le32(entry);
		offset = read_le32(entry + 4);
		if (mcc_chunk_stored(csize) || csize < 2 || csize > H2_MCC_CHUNK_SIZE * 2 || !file_read_at(file, offset, in, csize))
			continue;
		t = vita_host_time_us();
		memset(&stream, 0, sizeof(stream));
		inflateInit2(&stream, -15);
		stream.next_in = in + 2;
		stream.avail_in = (uInt)(csize - 2);
		stream.next_out = a;
		stream.avail_out = H2_MCC_CHUNK_SIZE;
		inflate(&stream, Z_FINISH);
		inflateEnd(&stream);
		zlib_us += vita_host_time_us() - t;
		t = vita_host_time_us();
		made = inflate_whole(in + 2, csize - 2, b, H2_MCC_CHUNK_SIZE, FALSE);
		libdeflate_us += vita_host_time_us() - t;
		if (made != (long)stream.total_out || memcmp(a, b, stream.total_out))
			differ++;
		bytes += stream.total_out;
		done++;
	}
	platform_log("h2 cache: inflate bench: %ld chunks of shared.map, %lu KB: zlib %.1f ms, libdeflate %.1f ms (x%.2f), %ld differ",
		done, bytes / 1024, zlib_us / 1000.0, libdeflate_us / 1000.0, libdeflate_us ? (double)zlib_us / (double)libdeflate_us : 0.0,
		differ);
out:
	if (file)
		fclose(file);
	free(in);
	free(a);
	free(b);
}

/* chunk k inflated into out (H2_MCC_CHUNK_SIZE bytes at most), how many */
H2_ALIGN static unsigned long mcc_inflate_chunk(struct h2_cache *cache, unsigned long k, unsigned char *out, unsigned long room)
{
	unsigned long csize = cache->chunk_sizes[k];
	z_stream stream;
	int status;
	unsigned long done;
	unsigned long long started;

	/* (a chunk stored as it is: its size negative, -0x40000 - deflate would
	not have made it smaller. 06b and 07a have one each) */
	if (mcc_chunk_stored(csize))
	{
		unsigned long n = mcc_chunk_stored_size(csize) < room ? mcc_chunk_stored_size(csize) : room;

		return file_read_at(cache->files[0], cache->chunk_offsets[k], out, n) ? n : 0;
	}
	/* (kept from an earlier inflate of it: copied, chunk_keep) */
	{
		unsigned long kept = chunk_kept_copy(cache, k, out, room);

		if (kept)
			return kept;
	}
	/* (the compressed chunk's buffer: made again after a trim let it go) */
	if (!cache->compressed)
		cache->compressed = malloc(H2_MCC_CHUNK_SIZE * 2);
	if (!cache->compressed || csize < 2 || csize > H2_MCC_CHUNK_SIZE * 2 ||
		!file_read_at(cache->files[0], cache->chunk_offsets[k], cache->compressed, csize))
	{
		return 0;
	}
	/* (libdeflate's: the stream inflated whole into the room - zlib's
	Z_STREAM_END -, else zlib's as before) */
	started = vita_host_time_us();
	{
		long made = inflate_whole(cache->compressed + 2, csize - 2, out, room, FALSE);

		if (made >= 0)
		{
			io_inflated(started);
			chunk_keep(cache, k, out, (unsigned long)made);
			return (unsigned long)made;
		}
	}
	memset(&stream, 0, sizeof(stream));
	if (inflateInit2(&stream, -15) != Z_OK)
		return 0;
	stream.next_in = cache->compressed + 2;
	stream.avail_in = (uInt)(csize - 2);
	stream.next_out = out;
	stream.avail_out = (uInt)room;
	status = inflate(&stream, Z_FINISH);
	done = stream.total_out;
	inflateEnd(&stream);
	io_inflated(started);
	/* (inflated whole - to its stream's end -: kept, chunk_keep) */
	if (status == Z_STREAM_END)
		chunk_keep(cache, k, out, done);
	return status == Z_STREAM_END || status == Z_BUF_ERROR ? done : 0;
}

/* ---------- HALO_H2_CACHE_STRESS=1 (a test build's, never a release's nor
the Vita's): every reader's pointer into an inflated segment checked by
force. The budget 1 byte, so each new segment lets go every one but the
newest H2_MCC_SEGMENTS_KEPT (the promise mcc_bytes makes); each segment its
own pages, and a segment let go is filled with 0xDEADBEEF and its pages
made PROT_NONE - a pointer kept past it faults at once, under gdb or ASan, at
the line that read it. The pages are unmapped only after 256 MB of others
were let go since (a 32-bit address space) */
#if !defined(HALO_RELEASE) && !defined(HALO_VITA) && !defined(_WIN32)
#include <sys/mman.h>
#include <unistd.h>
#define H2_CACHE_STRESS_BUILD 1
enum { CACHE_STRESS_QUARANTINE = 256 * 1024 * 1024, CACHE_STRESS_RING = 4096 };
static struct
{
	pthread_mutex_t lock;
	void *data[CACHE_STRESS_RING];
	unsigned long size[CACHE_STRESS_RING];
	long first, count;
	unsigned long bytes, released;
} cache_stress_ring = {PTHREAD_MUTEX_INITIALIZER};

H2_ALIGN static boolean cache_stress(void)
{
	static int on = -1;

	if (on < 0)
	{
		on = getenv("HALO_H2_CACHE_STRESS") && atoi(getenv("HALO_H2_CACHE_STRESS")) != 0;
		if (on)
			platform_log("h2 cache: HALO_H2_CACHE_STRESS: segments budgeted to 1 byte, the newest %d kept, the rest "
				"poisoned and made unreadable", (int)H2_MCC_SEGMENTS_KEPT);
	}
	return on;
}

H2_ALIGN static unsigned long cache_stress_pages(unsigned long size)
{
	unsigned long page = (unsigned long)sysconf(_SC_PAGESIZE);

	return (size + page - 1) / page * page;
}

H2_ALIGN static unsigned char *cache_stress_alloc(unsigned long size)
{
	void *p = mmap(NULL, cache_stress_pages(size), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	return p == MAP_FAILED ? NULL : (unsigned char *)p;
}

H2_ALIGN static void cache_stress_free(unsigned char *data, unsigned long size)
{
	unsigned long pages = cache_stress_pages(size), i;

	/* (poisoned first - a reader racing the release reads 0xDEADBEEF -, then
	the pages replaced by unreadable ones: their memory given back, the
	addresses kept) */
	for (i = 0; i + 4 <= pages; i += 4)
	{
		data[i] = 0xEF, data[i + 1] = 0xBE, data[i + 2] = 0xAD, data[i + 3] = 0xDE;
	}
	if (mmap(data, pages, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED)
		mprotect(data, pages, PROT_NONE);
	pthread_mutex_lock(&cache_stress_ring.lock);
	/* (the count of segments let go, now and then: the mode's proof it bites) */
	if (!(++cache_stress_ring.released & (cache_stress_ring.released - 1)) || !(cache_stress_ring.released % 20000))
		platform_log("h2 cache: HALO_H2_CACHE_STRESS: %lu segments let go", cache_stress_ring.released);
	while (cache_stress_ring.count &&
		(cache_stress_ring.count >= CACHE_STRESS_RING || cache_stress_ring.bytes + pages > CACHE_STRESS_QUARANTINE))
	{
		long k = cache_stress_ring.first;

		munmap(cache_stress_ring.data[k], cache_stress_ring.size[k]);
		cache_stress_ring.bytes -= cache_stress_ring.size[k];
		cache_stress_ring.first = (k + 1) % CACHE_STRESS_RING;
		cache_stress_ring.count--;
	}
	{
		long k = (cache_stress_ring.first + cache_stress_ring.count) % CACHE_STRESS_RING;

		cache_stress_ring.data[k] = data;
		cache_stress_ring.size[k] = pages;
		cache_stress_ring.bytes += pages;
		cache_stress_ring.count++;
	}
	pthread_mutex_unlock(&cache_stress_ring.lock);
}
#else
#define cache_stress() FALSE
#define cache_stress_alloc(size) NULL
#define cache_stress_free(data, size) ((void)0)
#endif

/* the segments' budget: h2_cache_segment_budget's, 1 byte under HALO_H2_CACHE_STRESS */
H2_ALIGN static unsigned long segment_budget_of(struct h2_cache const *cache)
{
	return cache_stress() ? 1UL : cache->segment_budget;
}

H2_ALIGN static void segment_release(struct h2_cache *cache, struct mcc_segment *segment)
{
	static int poison = -1;

	cache->segment_bytes = cache->segment_bytes > segment->size ? cache->segment_bytes - segment->size : 0;
	/* (harness, HALO_H2_SEGMENT_POISON=1: a segment let go is overwritten
	first, so a reader still holding a pointer into it reads garbage - a
	budget's test) */
	if (poison < 0)
		poison = getenv("HALO_H2_SEGMENT_POISON") && atoi(getenv("HALO_H2_SEGMENT_POISON"));
	if (segment->stress)
	{
		cache_stress_free(segment->data, segment->size ? segment->size : 1);
		segment->data = NULL;
		return;
	}
	if (poison && segment->data)
		memset(segment->data, 0xA5, segment->size ? segment->size : 1);
	if (h2_user_block_free(segment->data))
		;
	else if (cache->segment_free)
		cache->segment_free(segment->data, segment->size ? segment->size : 1);
	else
		free(segment->data);
	segment->data = NULL;
}

/* size bytes of the decompressed file at offset, where they lie in a
segment (made now - whole chunks - if none holds them); NULL if not */
/* (a read served by segment i: under a budget, made the newest - the
budget lets go the oldest first and keeps the newest H2_MCC_SEGMENTS_KEPT,
so a piece just read stays through the next reads; first in, first out,
a reader's pointer into an old piece went with the next read's new one: a
placement's position, an animation's sizes) its data; segment_last it */
H2_ALIGN static unsigned char *segment_used(struct h2_cache *cache, long i)
{
	/* (every hit made the newest, not only one past the kept: a piece
	found among the kept but the oldest of them went with the very next new
	piece - a reader's node record let go by its name's read, h2_objanim.c.
	So a pointer a read gave holds through the next H2_MCC_SEGMENTS_KEPT - 1
	new pieces at least) */
	if (segment_budget_of(cache) && i != cache->segment_count - 1)
	{
		struct mcc_segment used = cache->segments[i];

		memmove(cache->segments + i, cache->segments + i + 1, (size_t)(cache->segment_count - i - 1) * sizeof(*cache->segments));
		i = cache->segment_count - 1;
		cache->segments[i] = used;
	}
	cache->segment_last = i;
	return cache->segments[i].data;
}

/* (h2_cache_memory_failures_count) the segments not made for want of memory,
every open's, all told */
static unsigned long h2_cache_memory_failures;

unsigned long h2_cache_memory_failures_count(void)
{
	return __atomic_load_n(&h2_cache_memory_failures, __ATOMIC_RELAXED);
}

H2_ALIGN static unsigned char *mcc_bytes(struct h2_cache *cache, unsigned long offset, unsigned long size)
{
	long i;
	unsigned long start, end, at;
	struct mcc_segment *segment;

	if (offset > cache->whole_size || size > cache->whole_size - offset)
		return NULL;
	/* (the piece the last read found first: a walk over the tags reads the
	same piece thousands of times, each past every piece read since - 0.8 s
	of each game's start in the HUD's events, on the game thread) */
	i = cache->segment_last;
	if (i >= 0 && i < cache->segment_count)
	{
		segment = &cache->segments[i];
		if (offset >= segment->start && offset + size <= segment->start + segment->size)
		{
			unsigned long from = offset - segment->start;

			return segment_used(cache, i) + from;
		}
	}
	for (i = cache->segment_count - 1; i >= 0; i--)
	{
		segment = &cache->segments[i];
		if (offset >= segment->start && offset + size <= segment->start + segment->size)
		{
			unsigned long from = offset - segment->start;

			return segment_used(cache, i) + from;
		}
	}
	/* (whole chunks: the header its own piece before them) */
	start = offset < H2_MCC_HEADER_SIZE ? 0 :
		H2_MCC_HEADER_SIZE + (offset - H2_MCC_HEADER_SIZE) / H2_MCC_CHUNK_SIZE * H2_MCC_CHUNK_SIZE;
	end = offset + (size ? size : 1);
	end = end <= H2_MCC_HEADER_SIZE ? H2_MCC_HEADER_SIZE :
		H2_MCC_HEADER_SIZE + (end - H2_MCC_HEADER_SIZE + H2_MCC_CHUNK_SIZE - 1) / H2_MCC_CHUNK_SIZE * H2_MCC_CHUNK_SIZE;
	if (end > cache->whole_size)
		end = cache->whole_size;
	/* (a budget: the oldest segments let go first, the newest
	H2_MCC_SEGMENTS_KEPT always kept - what the last reads gave stays) */
	if (segment_budget_of(cache) && cache->segment_bytes + (end - start) > segment_budget_of(cache) &&
		cache->segment_count > H2_MCC_SEGMENTS_KEPT)
	{
		long released = 0;

		while (released < cache->segment_count - H2_MCC_SEGMENTS_KEPT &&
			cache->segment_bytes + (end - start) > segment_budget_of(cache))
		{
			segment_release(cache, &cache->segments[released]);
			released++;
		}
		memmove(cache->segments, cache->segments + released, (size_t)(cache->segment_count - released) * sizeof(*cache->segments));
		cache->segment_count -= released;
		cache->evicted += (unsigned long)released;
	}
	if (cache->segment_count >= cache->segment_capacity)
	{
		long grown_capacity = cache->segment_capacity ? cache->segment_capacity * 2 : 64;
		struct mcc_segment *grown = realloc(cache->segments, grown_capacity * sizeof(*grown));

		if (!grown)
		{
			cache->memory_failed = TRUE;
			__atomic_add_fetch(&h2_cache_memory_failures, 1, __ATOMIC_RELAXED);
			return NULL;
		}
		cache->segments = grown;
		cache->segment_capacity = grown_capacity;
	}
	segment = &cache->segments[cache->segment_count];
	segment->start = start;
	segment->size = end - start;
	segment->stress = FALSE;
	segment->data = NULL;
	if (cache_stress())
		segment->stress = (segment->data = cache_stress_alloc(segment->size ? segment->size : 1)) != NULL;
	if (!segment->data)
		segment->data = cache->segment_alloc ? cache->segment_alloc(segment->size ? segment->size : 1) : NULL;
#if H2_BUILD_HEAP
	/* (no room in the window and no build on - the load's steps after the
	make, the play's reads -: a large piece a user RAM block of its own
	before the C heap. The AI's step of a level's load inflated 9-23 MB of
	pieces into the Vita's 48 MB C heap with the window full: 04a after 03b
	"out of memory" at shared.map, 04b's AI step 2.5 MB short of it. The
	game thread's alone: the derive workers' pieces are their arena's) */
	if (!segment->data && segment->size >= H2_MCC_USER_BLOCK_PIECE && !h2_build_heap_active() &&
		halo_thread_index() == halo_game_thread_index())
		segment->data = h2_user_block_alloc(segment->size);
#endif
	if (!segment->data)
		segment->data = malloc(segment->size ? segment->size : 1);
	/* (neither the window nor the C heap had room - the owner's Vita after
	03a's make: shared.map's sounds "named no scenario", its index's piece
	not inflated -: a user RAM block of its own) */
	if (!segment->data)
	{
		segment->data = h2_user_block_alloc(segment->size ? segment->size : 1);
		platform_log("h2 cache: %s: a piece of %lu KB had no room in the window or the C heap: %s", cache->name,
			segment->size / 1024, segment->data ? "a user RAM block of its own" : "none in user RAM either");
	}
	if (!segment->data)
	{
		cache->memory_failed = TRUE;
		__atomic_add_fetch(&h2_cache_memory_failures, 1, __ATOMIC_RELAXED);
		return NULL;
	}
	for (at = start; at < end; )
	{
		if (at < H2_MCC_HEADER_SIZE)
		{
			unsigned long n = (end < H2_MCC_HEADER_SIZE ? end : H2_MCC_HEADER_SIZE) - at;

			memcpy(segment->data + (at - start), cache->header_bytes + at, n);
			at += n;
		}
		else
		{
			unsigned long k = (at - H2_MCC_HEADER_SIZE) / H2_MCC_CHUNK_SIZE;
			unsigned long room = end - at < H2_MCC_CHUNK_SIZE ? end - at : H2_MCC_CHUNK_SIZE;
			long j;
			boolean copied = FALSE;

			/* (a chunk another segment holds whole: copied, not inflated again) */
			for (j = 0; j < cache->segment_count && !copied; j++)
			{
				struct mcc_segment const *other = &cache->segments[j];

				if (at >= other->start && at + room <= other->start + other->size)
				{
					memcpy(segment->data + (at - start), other->data + (at - other->start), room);
					copied = TRUE;
				}
			}
			if (!copied && (k >= cache->chunk_count || mcc_inflate_chunk(cache, k, segment->data + (at - start), room) != room))
			{
				cache->segment_bytes += segment->size;
				segment_release(cache, segment);
				return NULL;
			}
			if (!copied)
				cache->inflated++;
			if (cache->touched)
				cache->touched[k] = 1;
			at += room;
		}
	}
	cache->segment_count++;
	cache->segment_bytes += segment->size;
	return segment->data + (offset - start);
}

H2_ALIGN static boolean mcc_load(struct h2_cache *cache, boolean shared_maps, char *why, long why_size)
{
	unsigned char *p;
	unsigned long n, index_offset, mask, name_count, name_data, name_bytes, name_offsets, tags, count, i, table;

	cache->mcc = TRUE;
	if (!file_read_at(cache->files[0], 0, cache->header_bytes, H2_MCC_HEADER_SIZE))
	{
		why_set(why, why_size, "its header cannot be read");
		return FALSE;
	}
	n = cache->whole_size = read_le32(cache->header_bytes + 8);
	table = read_le32(cache->header_bytes + 0x310);
	cache->chunk_count = read_le32(cache->header_bytes + 0x314);
	if (n < H2_MCC_HEADER_SIZE || n > H2_MCC_MAXIMUM_BYTES || table > cache->file_sizes[0] ||
		cache->chunk_count > cache->file_sizes[0] / 8 || cache->chunk_count * 8 > cache->file_sizes[0] - table)
	{
		why_set(why, why_size, "its compressed data cannot be read");
		return FALSE;
	}
	cache->chunk_sizes = malloc((cache->chunk_count + 1) * sizeof(unsigned long));
	cache->chunk_offsets = malloc((cache->chunk_count + 1) * sizeof(unsigned long));
	cache->compressed = malloc(H2_MCC_CHUNK_SIZE * 2);
	if (!cache->chunk_sizes || !cache->chunk_offsets || !cache->compressed)
	{
		why_set(why, why_size, "out of memory");
		return FALSE;
	}
	/* (the chunk table in one read: 8 bytes a read were some 200 reads a
	map's open, each a card access on the Vita) */
	{
		unsigned char *entries = malloc(cache->chunk_count * 8 + 1);

		if (!entries)
		{
			why_set(why, why_size, "out of memory");
			return FALSE;
		}
		if (cache->chunk_count && !file_read_at(cache->files[0], table, entries, cache->chunk_count * 8))
		{
			free(entries);
			why_set(why, why_size, "its compressed data cannot be read");
			return FALSE;
		}
		for (i = 0; i < cache->chunk_count; i++)
		{
			cache->chunk_sizes[i] = read_le32(entries + 8 * i);
			cache->chunk_offsets[i] = read_le32(entries + 8 * i + 4);
		}
		free(entries);
	}
	for (i = 0; i < cache->chunk_count; i++)
	{
		if (cache->chunk_offsets[i] > cache->file_sizes[0] ||
			mcc_chunk_file_bytes(cache->chunk_sizes[i]) > cache->file_sizes[0] - cache->chunk_offsets[i])
		{
			why_set(why, why_size, "its compressed data cannot be read");
			return FALSE;
		}
	}
	if (getenv("HALO_H2_DEBUG"))
		cache->touched = calloc(n / H2_MCC_CHUNK_SIZE + 2, 1);
	p = cache->header_bytes;
	memcpy(cache->name, p + 0xB0, sizeof(cache->name) - 1);
	memcpy(cache->scenario, p + 0xD0, sizeof(cache->scenario) - 1);
	index_offset = read_le32(p + 0x10);
	mask = read_le32(p + 0x2D0);
	name_count = read_le32(p + 0x20);
	name_data = read_le32(p + 0x24);
	name_bytes = read_le32(p + 0x28);
	name_offsets = read_le32(p + 0x2C);
	if (index_offset > n || n - index_offset < 32 || name_count > H2_MAXIMUM_TAGS || name_data > n ||
		name_bytes > n - name_data || name_offsets > n || name_count * 4 > n - name_offsets)
	{
		why_set(why, why_size, "its header's sizes are not the file's");
		return FALSE;
	}
	cache->index_offset = index_offset;
	cache->tag_data_size = n;
	cache->tag_data_address = mask - index_offset;
	cache->names = malloc(name_bytes + 1);
	cache->name_offsets = malloc((name_count ? name_count : 1) * sizeof(unsigned long));
	if (!cache->names || !cache->name_offsets || !(p = mcc_bytes(cache, name_data, name_bytes)))
	{
		why_set(why, why_size, "out of memory");
		return FALSE;
	}
	memcpy(cache->names, p, name_bytes);
	cache->names[name_bytes] = 0;
	cache->names_size = name_bytes;
	if (!(p = mcc_bytes(cache, name_offsets, name_count * 4)))
	{
		why_set(why, why_size, "its names cannot be read");
		return FALSE;
	}
	for (i = 0; i < name_count; i++)
		cache->name_offsets[i] = read_le32(p + 4 * i);
	if (!(p = mcc_bytes(cache, index_offset, 0x20)))
	{
		why_set(why, why_size, "its tag index cannot be read");
		return FALSE;
	}
	tags = read_le32(p + 8);
	cache->scenario_handle = read_le32(p + 12);
	count = read_le32(p + 0x18);
	if (read_le32(p + 0x1C) != H2_TAGS_SIGNATURE || count > H2_MAXIMUM_TAGS || count != name_count ||
		tags > n - index_offset || count * 16 > n - index_offset - tags)
	{
		why_set(why, why_size, "its tag index is damaged");
		return FALSE;
	}
	cache->tag_count = (long)count;
	cache->tags_address = H2_CACHE_BASE_ADDRESS + tags;
	/* the shared map's tags (those this one lists without data) */
	if (shared_maps)
	{
		cache->shared[2] = mcc_open(cache->directory, "shared", FALSE, NULL, 0);
		cache->shared_tried[2] = TRUE;
		if (cache->shared[2])
			cache->shared[2]->segment_alloc = cache->segment_alloc, cache->shared[2]->segment_free = cache->segment_free;
	}
	{
		char path[H2_CACHE_NAME_SIZE + 64];

		/* (beside the map, else Halo 2's own folder or the maps folder:
		h2_resource_directory) */
		char const *h2_resource_directory(char const *preferred, char const *file);

		(void)path;
		cache->textures = textures_dat_open(cache->directory);
	}

	return TRUE;
}

H2_ALIGN static struct h2_cache *mcc_open(char const *maps_directory, char const *name, boolean shared_maps,
	char *why, long why_size)
{
	struct h2_cache *cache = calloc(1, sizeof(*cache));
	char path[H2_CACHE_NAME_SIZE + 64];

	if (!cache)
		return NULL;
	snprintf(cache->directory, sizeof(cache->directory), "%s", maps_directory);
	snprintf(path, sizeof(path), "%s%s.map", maps_directory, name);
	snprintf(cache->path, sizeof(cache->path), "%s", path);
	cache->files[0] = fopen(path, "rb");
	h2_file_unbuffered(cache->files[0]);
	cache->file_tried[0] = TRUE;
	if (!cache->files[0] || fseek(cache->files[0], 0, SEEK_END) != 0)
	{
		h2_cache_close(cache);
		return NULL;
	}
	cache->file_sizes[0] = (unsigned long)ftell(cache->files[0]);
	if (!mcc_load(cache, shared_maps, why, why_size))
	{
		h2_cache_close(cache);
		return NULL;
	}

	return cache;
}

H2_ALIGN static struct h2_cache *cache_open_fresh(
	char const *maps_directory,
	char const *name,
	char *why,
	long why_size)
{
	struct h2_cache *cache = calloc(1, sizeof(*cache));
	unsigned char header[H2_CACHE_HEADER_SIZE];
	char path[H2_CACHE_NAME_SIZE + 64];
	unsigned long index_offset, tag_data_size, resident, name_count, name_data, name_bytes, name_offsets;
	unsigned long groups, tags, count;
	long i;

	if (!cache)
	{
		why_set(why, why_size, "out of memory");
		return NULL;
	}
	snprintf(cache->directory, sizeof(cache->directory), "%s", maps_directory);
	snprintf(path, sizeof(path), "%s%s.map", maps_directory, name);
	snprintf(cache->path, sizeof(cache->path), "%s", path);
	cache->files[0] = fopen(path, "rb");
	h2_file_unbuffered(cache->files[0]);
	cache->file_tried[0] = TRUE;
	if (!cache->files[0] || fseek(cache->files[0], 0, SEEK_END) != 0)
	{
		why_set(why, why_size, "the map file cannot be opened");
		goto failed;
	}
	cache->file_sizes[0] = (unsigned long)ftell(cache->files[0]);
	if (file_read_at(cache->files[0], 0, header, 8) && read_le32(header) == H2_HEADER_SIGNATURE &&
		read_le32(header + 4) == H2_MCC_CACHE_VERSION)
	{
		if (!mcc_load(cache, TRUE, why, why_size))
			goto failed;
		if (h2_cache_scenario_tag(cache) == NONE)
		{
			/* (its tag table's read had no memory: said so, not taken for
			a map with no scenario) */
			why_set(why, why_size, h2_cache_memory_failed(cache) ? "out of memory (its tag table's read)" : "it names no scenario");
			goto failed;
		}
		/* (the file's name is the level's: a map renamed to sit beside
		another of its name - Halo 2's beavercreek.map as h2_beavercreek.map,
		beside Halo CE's Battle Creek - makes its level, HUD, menu and scripts
		under that name, and the game reads them back by it) */
		if (name && name[0] && !strchr(name, '\\') && !strchr(name, '/') && strlen(name) < sizeof(cache->name))
			snprintf(cache->name, sizeof(cache->name), "%s", name);
		return cache;
	}
	if (!file_read_at(cache->files[0], 0, header, sizeof(header)) ||
		read_le32(header) != H2_HEADER_SIGNATURE || read_le32(header + 4) != H2_CACHE_VERSION ||
		read_le32(header + 0x7FC) != H2_FOOTER_SIGNATURE)
	{
		why_set(why, why_size, "not a Halo 2 Xbox map");
		goto failed;
	}
	memcpy(cache->name, header + 0x198, sizeof(cache->name) - 1);
	memcpy(cache->scenario, header + 0x1BC, sizeof(cache->scenario) - 1);
	index_offset = read_le32(header + 0x10);
	cache->index_size = read_le32(header + 0x14);
	tag_data_size = read_le32(header + 0x18);
	resident = read_le32(header + 0x1C);
	name_count = read_le32(header + 0x2C0);
	name_data = read_le32(header + 0x2C4);
	name_bytes = read_le32(header + 0x2C8);
	name_offsets = read_le32(header + 0x2CC);
	if (cache->index_size < 32 || cache->index_size > H2_MAXIMUM_INDEX_BYTES ||
		tag_data_size > H2_MAXIMUM_TAG_DATA_BYTES || resident < cache->index_size + tag_data_size ||
		index_offset > cache->file_sizes[0] ||
		cache->index_size + tag_data_size > cache->file_sizes[0] - index_offset ||
		name_bytes > H2_MAXIMUM_NAME_BYTES || name_count > H2_MAXIMUM_TAGS ||
		name_data > cache->file_sizes[0] || name_bytes > cache->file_sizes[0] - name_data ||
		name_offsets > cache->file_sizes[0] || name_count * 4 > cache->file_sizes[0] - name_offsets)
	{
		why_set(why, why_size, "its header's sizes are not the file's");
		goto failed;
	}

	/* the index and the tag data, where their addresses say */
	cache->index = malloc(cache->index_size);
	cache->tag_data_size = tag_data_size;
	cache->tag_data = malloc(tag_data_size ? tag_data_size : 1);
	cache->tag_data_address = H2_CACHE_BASE_ADDRESS + resident - tag_data_size;
	cache->names_size = name_bytes;
	cache->names = malloc(name_bytes + 1);
	cache->name_offsets = malloc((name_count ? name_count : 1) * sizeof(unsigned long));
	if (!cache->index || !cache->tag_data || !cache->names || !cache->name_offsets)
	{
		why_set(why, why_size, "out of memory");
		goto failed;
	}
	if (!file_read_at(cache->files[0], index_offset, cache->index, cache->index_size) ||
		!file_read_at(cache->files[0], index_offset + cache->index_size, cache->tag_data, tag_data_size) ||
		!file_read_at(cache->files[0], name_data, cache->names, name_bytes))
	{
		why_set(why, why_size, "the map file could not be read");
		goto failed;
	}
	cache->names[name_bytes] = 0;
	for (i = 0; i < (long)name_count; i++)
	{
		unsigned char word[4];

		if (!file_read_at(cache->files[0], name_offsets + 4 * i, word, 4))
		{
			why_set(why, why_size, "the map file could not be read");
			goto failed;
		}
		cache->name_offsets[i] = read_le32(word);
	}

	groups = read_le32(cache->index);
	(void)groups;
	tags = read_le32(cache->index + 8);
	cache->scenario_handle = read_le32(cache->index + 12);
	count = read_le32(cache->index + 0x18);
	if (read_le32(cache->index + 0x1C) != H2_TAGS_SIGNATURE || count > H2_MAXIMUM_TAGS || count != name_count ||
		tags < H2_CACHE_BASE_ADDRESS || tags - H2_CACHE_BASE_ADDRESS > cache->index_size ||
		count * 16 > cache->index_size - (tags - H2_CACHE_BASE_ADDRESS))
	{
		why_set(why, why_size, "its tag index is damaged");
		goto failed;
	}
	cache->tag_count = (long)count;
	cache->tags_address = tags;
	if (h2_cache_scenario_tag(cache) == NONE)
	{
		why_set(why, why_size, "it names no scenario");
		goto failed;
	}

	return cache;

failed:
	h2_cache_close(cache);
	return NULL;
}

H2_ALIGN static void cache_close_real(
	struct h2_cache *cache)
{
	short i;

	if (!cache)
		return;
	for (i = 0; i < H2_RESOURCE_FILES; i++)
	{
		if (cache->files[i])
			fclose(cache->files[i]);
	}
	if (cache->touched)
	{
		unsigned long k, touched = 0, chunks = cache->whole_size / H2_MCC_CHUNK_SIZE + 1;

		for (k = 0; k < chunks; k++)
			touched += cache->touched[k];
		fprintf(stderr, "h2 cache: %s: %lu of %lu chunks of 256 KB read\n", cache->name, touched, chunks);
		free(cache->touched);
	}
	for (i = 0; i < cache->segment_count; i++)
		segment_release(cache, &cache->segments[i]);
	free(cache->segments);
	if (!cache->view)
	{
		free(cache->chunk_sizes);
		free(cache->chunk_offsets);
	}
	free(cache->compressed);
	{
		int k;

		for (k = 0; k < H2_CACHE_CHUNKS_KEPT; k++)
			free(cache->chunks_kept[k].data);
	}
	if (cache->whole)
		free(cache->whole);
	else
	{
		if (!cache->mcc)
		{
			free(cache->index);
			free(cache->tag_data);
		}
	}
	for (i = 0; i < H2_RESOURCE_FILES; i++)
	{
		if (cache->shared[i])
			h2_cache_close(cache->shared[i]);
	}
	if (cache->textures)
		fclose(cache->textures);
	if (!cache->view)
	{
		free(cache->names);
		free(cache->name_offsets);
	}
	/* (a view's names index is its own, made at its first find) */
	free(cache->name_heads);
	free(cache->name_next);
	free(cache->bsp);
	free(cache);
}

/* (port) a view of an MCC map's open for another thread: what the open
read once and never changes - its header, names, chunk tables - shared,
read only; its own file handles, chunk scratch and segments (to `budget`
bytes, 0 none), and views of the shared maps base has open. A fraction of
an open's memory (no names, no tables); base must stay open while it is */
H2_ALIGN struct h2_cache *h2_cache_open_view(
	struct h2_cache *base,
	unsigned long budget)
{
	struct h2_cache *view;
	char path[H2_CACHE_NAME_SIZE + 64];
	short i;

	if (!base || !base->mcc || base->whole)
		return NULL;
	view = calloc(1, sizeof(*view));
	if (!view)
		return NULL;
	memcpy(view->directory, base->directory, sizeof(view->directory));
	memcpy(view->name, base->name, sizeof(view->name));
	memcpy(view->scenario, base->scenario, sizeof(view->scenario));
	memcpy(view->header_bytes, base->header_bytes, sizeof(view->header_bytes));
	/* (its tables copied - its own memory, the caller's thread's heap: base
	need not stay open once the view is made, and is not held through the
	play - a hold kept the load's open's segments in the C heap) */
	view->view = FALSE;
	view->mcc = TRUE;
	view->file_sizes[0] = base->file_sizes[0];
	view->whole_size = base->whole_size;
	view->chunk_count = base->chunk_count;
	view->chunk_sizes = malloc((base->chunk_count + 1) * sizeof(unsigned long));
	view->chunk_offsets = malloc((base->chunk_count + 1) * sizeof(unsigned long));
	view->index_offset = base->index_offset;
	view->tag_data_size = base->tag_data_size;
	view->tag_data_address = base->tag_data_address;
	view->names = malloc(base->names_size + 1);
	view->names_size = base->names_size;
	view->name_offsets = malloc((base->tag_count ? base->tag_count : 1) * sizeof(unsigned long));
	if (!view->chunk_sizes || !view->chunk_offsets || !view->names || !view->name_offsets)
	{
		h2_cache_close(view);
		return NULL;
	}
	memcpy(view->chunk_sizes, base->chunk_sizes, (base->chunk_count + 1) * sizeof(unsigned long));
	memcpy(view->chunk_offsets, base->chunk_offsets, (base->chunk_count + 1) * sizeof(unsigned long));
	memcpy(view->names, base->names, base->names_size + 1);
	memcpy(view->name_offsets, base->name_offsets, (base->tag_count ? base->tag_count : 1) * sizeof(unsigned long));
	view->tag_count = base->tag_count;
	view->tags_address = base->tags_address;
	view->scenario_handle = base->scenario_handle;
	view->globals_index = base->globals_index;
	view->globals_found = base->globals_found;
	view->segment_last = -1;
	view->segment_budget = budget;
	snprintf(view->path, sizeof(view->path), "%s", base->path);
	view->files[0] = fopen(base->path, "rb");
	h2_file_unbuffered(view->files[0]);
	view->file_tried[0] = TRUE;
	view->compressed = malloc(H2_MCC_CHUNK_SIZE * 2);
	if (!view->files[0] || !view->compressed)
	{
		h2_cache_close(view);
		return NULL;
	}
	{
		char const *h2_resource_directory(char const *preferred, char const *file);

		view->textures = textures_dat_open(view->directory);
	}
	for (i = 0; i < H2_RESOURCE_FILES; i++)
	{
		struct h2_cache *shared = __atomic_load_n(&base->shared[i], __ATOMIC_ACQUIRE);

		if (shared)
		{
			view->shared[i] = h2_cache_open_view(shared, budget);
			view->shared_tried[i] = TRUE;
		}
	}
	return view;
}

/* ---------- (port) one open shared through a level's load

A Halo 2 level's load opens its MCC map once per module - the AI, the
sounds, the phantoms, the rules, the props, the HUD, the scripts, the
emblems, the menu, the models, the world: 23 opens of Cairo Station - and
each open reads the names and the index and inflates afresh every chunk
the module touches (the sounds alone inflated 292). On the Vita that was
38 s of the sounds' step and 17 s of the phantoms' (5ackwood's Cairo, Nov
2026). Between h2_cache_keep_begin and h2_cache_keep_end (cache_files.c:
the level's load) the first open of a map is kept: a later open of the
same map by the game thread gets it, a close by one of its users leaves it
open, and the end of the load (or an open of another map) closes it. Its
segment budget is the most any user asked for, so what one module
inflated serves the next. Another thread gets its own open, as before.
HALO_H2_CACHE_SHARE=0: every open its own, as before. */
int halo_thread_index(void);
int halo_game_thread_index(void);
unsigned long system_milliseconds(void);
void platform_log(const char *format, ...);
int strcasecmp(const char *a, const char *b);

static struct
{
	boolean window;           /* (between keep_begin and keep_end: a level's load) */
	boolean keeping;          /* (and the sharing on) */
	struct h2_cache *cache;   /* the kept one, NULL none */
	char directory[H2_CACHE_NAME_SIZE];
	char name[64];
	long users;               /* opens of it not closed yet */
	long served, opened;      /* (the load's opens served by the kept one, and fresh ones) */
	unsigned long open_ms;    /* (fresh opens' time) */
	unsigned long inflated, evicted;   /* (the load's chunks inflated and let go, every open's) */
} kept;

static boolean cache_share_enabled(void)
{
	static int enabled = -1;

	if (enabled < 0)
		enabled = !getenv("HALO_H2_CACHE_SHARE") || atoi(getenv("HALO_H2_CACHE_SHARE")) != 0;
	return enabled;
}

static void kept_release(void)
{
	if (kept.cache)
	{
		kept.inflated += kept.cache->inflated;
		kept.evicted += kept.cache->evicted;
		cache_close_real(kept.cache);
	}
	kept.cache = NULL;
	kept.users = 0;
}

void h2_cache_keep_begin(void)
{
	kept_release();
	kept.window = TRUE;
	kept.keeping = cache_share_enabled();
	kept.served = kept.opened = 0;
	kept.open_ms = kept.inflated = kept.evicted = 0;
}

void h2_cache_keep_end(void)
{
	boolean window = kept.window;

	kept.keeping = FALSE;
	kept.window = FALSE;
	/* (kept through the play, trimmed to its tables and the newest chunks
	its users may still read (h2_cache_trim) - not the load's chunks sat in
	the C heap -, until the next load's begin or another map's open: the
	game thread's later opens of the map are served it - the world's reads
	at a BSP switch during play (h2_world_load.inc), which opened afresh and
	took the C heap the play had little of; its file handles stay open, the
	Vita's few) */
	if (kept.cache && !cache_share_enabled())
		kept_release();
	else if (kept.cache)
		h2_cache_trim(kept.cache);
	if (window)
	{
		platform_log("h2 cache: the load's MCC map opens: %ld fresh (%lu ms) and %ld served by the one kept, %lu chunks inflated, "
			"%lu let go%s", kept.opened, kept.open_ms, kept.served, kept.inflated, kept.evicted,
			cache_share_enabled() ? "" : " (HALO_H2_CACHE_SHARE=0: every open its own)");
	}
}

H2_ALIGN struct h2_cache *h2_cache_open(
	char const *maps_directory,
	char const *name,
	char *why,
	long why_size)
{
	struct h2_cache *cache;
	unsigned long started;
	boolean game_thread = halo_thread_index() == halo_game_thread_index();

	if (kept.cache && game_thread && cache_share_enabled() && !strcasecmp(kept.name, name) && !strcmp(kept.directory, maps_directory))
	{
		kept.users++;
		kept.served++;
		return kept.cache;
	}
	started = system_milliseconds();
	cache = cache_open_fresh(maps_directory, name, why, why_size);
	/* (test builds: HALO_H2_TAG_DUMP, the tag fields this port reads
	written for h2ekref's verify.py; nothing in release builds) */
	if (cache)
		h2_tag_dump_cache(cache);
	if (cache && kept.window && game_thread)
	{
		kept.opened++;
		kept.open_ms += system_milliseconds() - started;
	}
	if (!cache || !kept.keeping || !game_thread)
		return cache;
	/* (another map's open while one is kept: the kept one goes if no one
	holds it, else this open is its caller's own) */
	if (kept.cache && kept.users > 0)
		return cache;
	kept_release();
	kept.cache = cache;
	kept.users = 1;
	snprintf(kept.directory, sizeof(kept.directory), "%s", maps_directory);
	snprintf(kept.name, sizeof(kept.name), "%s", name);
	return cache;
}

/* (port) an open of its own, never the load's kept one, whatever the
thread: a user that holds it on another thread (h2_texture_source.c's
derived bitmaps, made on the cache file thread) */
H2_ALIGN struct h2_cache *h2_cache_open_own(
	char const *maps_directory,
	char const *name,
	char *why,
	long why_size)
{
	return cache_open_fresh(maps_directory, name, why, why_size);
}

H2_ALIGN void h2_cache_close(
	struct h2_cache *cache)
{
	if (cache && cache == kept.cache)
	{
		if (kept.users > 0)
			kept.users--;
		/* (sharing on: kept through the play with no user too, h2_cache_keep_end) */
		if (kept.users <= 0 && !kept.keeping && !cache_share_enabled())
			kept_release();
		/* (a user closing it while a module's memory is installed: its
		segments let go with that memory's hooks, which go with it - the
		C heap's from here, the module's window not read or freed after it) */
		else if (cache->segment_alloc || cache->segment_free)
			h2_cache_segment_memory(cache, NULL, NULL);
		return;
	}
	if (cache && kept.window)
	{
		kept.inflated += cache->inflated;
		kept.evicted += cache->evicted;
	}
	cache_close_real(cache);
}

H2_ALIGN char const *h2_cache_name(
	struct h2_cache const *cache)
{
	return cache->name;
}

H2_ALIGN char const *h2_cache_scenario_path(
	struct h2_cache const *cache)
{
	return cache->scenario;
}

H2_ALIGN long h2_cache_tag_count(
	struct h2_cache const *cache)
{
	return cache->tag_count;
}

H2_ALIGN boolean h2_cache_tag_get(
	struct h2_cache const *cache,
	long index,
	struct h2_cache_tag *tag)
{
	unsigned char const *entry;

	if (index < 0 || index >= cache->tag_count)
		return FALSE;
	entry = cache->mcc ?
		mcc_bytes((struct h2_cache *)cache, cache->index_offset + (cache->tags_address - H2_CACHE_BASE_ADDRESS) + 16 * index, 16) :
		cache->index + (cache->tags_address - H2_CACHE_BASE_ADDRESS) + 16 * index;
	if (!entry)
		return FALSE;
	tag->group_tag = read_le32(entry);
	tag->handle = read_le32(entry + 4);
	tag->address = read_le32(entry + 8);
	tag->size = read_le32(entry + 12);
	/* (MCC: a tag listed here and kept in a shared map: the shared map's
	entry for it, the same index and handle) */
	if (cache->mcc && !tag->address)
	{
		short k;

		for (k = 0; k < H2_RESOURCE_FILES; k++)
		{
			struct h2_cache_tag shared;

			if (cache->shared[k] && h2_cache_tag_get(cache->shared[k], index, &shared) &&
				shared.handle == tag->handle && shared.address)
			{
				*tag = shared;
				break;
			}
		}
	}

	return TRUE;
}

H2_ALIGN char const *h2_cache_tag_name(
	struct h2_cache const *cache,
	long index)
{
	if (index < 0 || index >= cache->tag_count || cache->name_offsets[index] >= cache->names_size)
		return "";

	return (char const *)cache->names + cache->name_offsets[index];
}

H2_ALIGN long h2_cache_tag_from_handle(
	struct h2_cache const *cache,
	unsigned long handle)
{
	struct h2_cache_tag tag;
	long index = (long)(handle & 0xFFFF);

	if (handle == 0xFFFFFFFFUL || !h2_cache_tag_get(cache, index, &tag) || tag.handle != handle)
		return NONE;

	return index;
}

H2_ALIGN long h2_cache_scenario_tag(
	struct h2_cache const *cache)
{
	return h2_cache_tag_from_handle(cache, cache->scenario_handle);
}

H2_ALIGN void const *h2_cache_at(
	struct h2_cache const *cache,
	unsigned long address,
	unsigned long size)
{
	if (cache->bsp && address >= cache->bsp_address && address - cache->bsp_address <= cache->bsp_size &&
		size <= cache->bsp_size - (address - cache->bsp_address))
	{
		return cache->bsp + (address - cache->bsp_address);
	}
	/* (MCC's base, its mask less the index's offset, may be below zero -
the shared map's mask is 0: the difference taken modulo 2^32; its bytes
inflated as they are first wanted) */
	if (cache->mcc && address - cache->tag_data_address <= cache->tag_data_size &&
		size <= cache->tag_data_size - (address - cache->tag_data_address))
	{
		void const *p = mcc_bytes((struct h2_cache *)cache, address - cache->tag_data_address, size);

		if (p)
			return p;
	}
	if (!cache->mcc && address >= cache->tag_data_address && address - cache->tag_data_address <= cache->tag_data_size &&
		size <= cache->tag_data_size - (address - cache->tag_data_address))
	{
		if (cache->touched)
		{
			unsigned long first = (address - cache->tag_data_address) / H2_MCC_CHUNK_SIZE;
			unsigned long last = (address - cache->tag_data_address + (size ? size - 1 : 0)) / H2_MCC_CHUNK_SIZE;

			for (; first <= last; first++)
				((struct h2_cache *)cache)->touched[first] = 1;
		}
		return cache->tag_data + (address - cache->tag_data_address);
	}
	if (address >= H2_CACHE_BASE_ADDRESS && address - H2_CACHE_BASE_ADDRESS <= cache->index_size &&
		size <= cache->index_size - (address - H2_CACHE_BASE_ADDRESS))
	{
		return cache->index + (address - H2_CACHE_BASE_ADDRESS);
	}
	/* (MCC: a shared map's tag, at its addresses) */
	if (cache->mcc)
	{
		short i;

		for (i = 0; i < H2_RESOURCE_FILES; i++)
		{
			if (cache->shared[i])
			{
				void const *p = h2_cache_at(cache->shared[i], address, size);

				if (p)
					return p;
			}
		}
	}

	return NULL;
}

H2_ALIGN unsigned long h2_cache_read32(
	struct h2_cache const *cache,
	unsigned long address)
{
	unsigned char const *p = h2_cache_at(cache, address, 4);

	return p ? read_le32(p) : 0;
}

H2_ALIGN boolean h2_cache_block(
	struct h2_cache const *cache,
	unsigned long address,
	unsigned long element_size,
	long *count,
	unsigned long *elements)
{
	unsigned char const *p = h2_cache_at(cache, address, 8);
	long n;

	*count = 0;
	*elements = 0;
	if (!p)
		return FALSE;
	n = (long)read_le32(p);
	*elements = read_le32(p + 4);
	if (n < 0 || n > 0x1000000 || (n && !h2_cache_at(cache, *elements, (unsigned long)n * element_size)))
	{
		*elements = 0;
		return FALSE;
	}
	*count = n;

	return TRUE;
}

H2_ALIGN long h2_cache_reference(
	struct h2_cache const *cache,
	unsigned long address)
{
	unsigned char const *p = h2_cache_at(cache, address, 8);

	return p ? h2_cache_tag_from_handle(cache, read_le32(p + 4)) : NONE;
}

H2_ALIGN long h2_cache_structure_count(
	struct h2_cache const *cache)
{
	struct h2_cache_tag scenario;
	long count;
	unsigned long elements;

	if (!h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario) ||
		!h2_cache_block(cache, scenario.address + H2_SCENARIO_STRUCTURES_OFFSET, H2_SCENARIO_STRUCTURE_SIZE,
			&count, &elements))
	{
		return 0;
	}

	return count;
}

/* structure BSP index's tag (its reference in the scenario's block), NONE
if there is none */
H2_ALIGN long h2_cache_structure_tag(
	struct h2_cache const *cache,
	long index)
{
	struct h2_cache_tag scenario;
	long count;
	unsigned long elements;

	if (!h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario) ||
		!h2_cache_block(cache, scenario.address + H2_SCENARIO_STRUCTURES_OFFSET, H2_SCENARIO_STRUCTURE_SIZE,
			&count, &elements) ||
		index < 0 || index >= count)
	{
		return NONE;
	}

	return h2_cache_reference(cache, elements + H2_SCENARIO_STRUCTURE_SIZE * index + 0x10);
}

H2_ALIGN boolean h2_cache_structure_load(
	struct h2_cache *cache,
	long index,
	unsigned long *structure_bsp,
	unsigned long *lightmap,
	char *why,
	long why_size)
{
	struct h2_cache_tag scenario;
	long count;
	unsigned long elements;
	unsigned char const *descriptor;
	unsigned long offset, size, address;

	free(cache->bsp);
	cache->bsp = NULL;
	cache->bsp_size = 0;
	if (!h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario) ||
		!h2_cache_block(cache, scenario.address + H2_SCENARIO_STRUCTURES_OFFSET, H2_SCENARIO_STRUCTURE_SIZE,
			&count, &elements) ||
		index < 0 || index >= count)
	{
		why_set(why, why_size, "the scenario has no such structure BSP");
		return FALSE;
	}
	descriptor = h2_cache_at(cache, elements + H2_SCENARIO_STRUCTURE_SIZE * index, H2_SCENARIO_STRUCTURE_SIZE);
	/* (MCC: the structure BSP and its lightmap are tags as any, where their
	references say) */
	if (cache->mcc)
	{
		struct h2_cache_tag bsp, lightmap_tag;

		if (!h2_cache_tag_get(cache, h2_cache_reference(cache, elements + H2_SCENARIO_STRUCTURE_SIZE * index + 0x10), &bsp) ||
			!h2_cache_at(cache, bsp.address, 0x23C))
		{
			why_set(why, why_size, "its structure BSP is not there");
			return FALSE;
		}
		*structure_bsp = bsp.address;
		*lightmap = h2_cache_tag_get(cache, h2_cache_reference(cache, elements + H2_SCENARIO_STRUCTURE_SIZE * index + 0x18),
			&lightmap_tag) && h2_cache_at(cache, lightmap_tag.address, 0x104) ? lightmap_tag.address : 0;
		return TRUE;
	}
	offset = read_le32(descriptor);
	size = read_le32(descriptor + 4);
	address = read_le32(descriptor + 8);
	if (size < 16 || size > H2_MAXIMUM_BSP_BYTES || offset > cache->file_sizes[0] ||
		size > cache->file_sizes[0] - offset)
	{
		why_set(why, why_size, "its structure BSP's range is not in the file");
		return FALSE;
	}
	cache->bsp = malloc(size);
	if (!cache->bsp)
	{
		why_set(why, why_size, "out of memory");
		return FALSE;
	}
	if (!file_read_at(cache->files[0], offset, cache->bsp, size) ||
		read_le32(cache->bsp + 12) != H2_SBSP_SIGNATURE)
	{
		free(cache->bsp);
		cache->bsp = NULL;
		why_set(why, why_size, "its structure BSP could not be read");
		return FALSE;
	}
	cache->bsp_size = size;
	cache->bsp_address = address;
	*structure_bsp = read_le32(cache->bsp + 4);
	*lightmap = read_le32(cache->bsp + 8);
	if (!h2_cache_at(cache, *structure_bsp, 0x23C))
	{
		why_set(why, why_size, "its structure BSP's header is damaged");
		return FALSE;
	}
	if (*lightmap && !h2_cache_at(cache, *lightmap, 0x104))
		*lightmap = 0;

	return TRUE;
}

/* (a raw resource that could not be read: why, the first few times) */
H2_ALIGN static void resource_read_complaint(char const *format, ...)
{
	static int complaints;
	char text[160];
	va_list arguments;

	if (complaints >= 8)
		return;
	complaints++;
	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	platform_log("h2 cache: %s", text);
}

static unsigned long resource_reads, resource_chunks;
static unsigned long long resource_chunk_bytes;

void platform_heap_usage(unsigned long *in_use, unsigned long *capacity);

/* how many inflated chunks a source keeps: 8 while the C heap has 16 MB
free (the makers at load: the Vita's 48 MB heap has about 30 free then,
and Cairo's gathers inflate a quarter of the chunks they would with 2), 2
when it has less (Old Mombasa's play-time BSP switch peaks about 40 of
it; h2_world_load.inc's trims free the kept chunks there anyway), asked
again every 64 inflations; HALO_H2_CHUNKS_KEPT=N a test's */
H2_ALIGN static int chunks_kept_count(void)
{
	static int count = -1, asked = 0;

	if (count < 0 || (asked++ & 63) == 0)
	{
		unsigned long in_use = 0, capacity = 0;
		char const *setting = getenv("HALO_H2_CHUNKS_KEPT");

		platform_heap_usage(&in_use, &capacity);
		count = !capacity || (capacity > in_use && capacity - in_use >= 16UL * 1024 * 1024) ? H2_CACHE_CHUNKS_KEPT : 2;
		if (setting && atoi(setting) >= 1 && atoi(setting) <= H2_CACHE_CHUNKS_KEPT)
			count = atoi(setting);
	}
	return count;
}

H2_ALIGN void h2_cache_resource_stats(
	unsigned long *reads,
	unsigned long *chunks,
	unsigned long long *bytes)
{
	*reads = resource_reads;
	*chunks = resource_chunks;
	*bytes = resource_chunk_bytes;
}

H2_ALIGN boolean h2_cache_resource_read(
	struct h2_cache *cache,
	unsigned long reference,
	void *out,
	unsigned long size)
{
	unsigned long selector = (reference >> 30) & 3;
	unsigned long offset = reference & 0x3FFFFFFFUL;
	FILE *file;

	if (reference == 0xFFFFFFFFUL)
		return FALSE;
	resource_reads++;
	/* (MCC: in the map decompressed, or a shared map's) */
	if (cache->mcc)
	{
		struct h2_cache *source = selector == 0 ? cache : cache->shared[selector];

		if (selector && !source && !cache->shared_tried[selector] && selector >= 2)
		{
			cache->shared_tried[selector] = TRUE;
			source = cache->shared[selector] = mcc_open(cache->directory,
				selector == 2 ? "shared" : "single_player_shared", FALSE, NULL, 0);
			if (source)
				source->segment_alloc = cache->segment_alloc, source->segment_free = cache->segment_free;
		}
		/* (copied out: a segment holding it used, else its chunks inflated
		one at a time into a scratch chunk, not kept - raw resources are read
		once, so they do not hold memory for the build) */
		if (!source || offset > source->whole_size || size > source->whole_size - offset)
		{
			resource_read_complaint(!source ? "a raw resource not read: the shared map %lu is not open" :
				"a raw resource not read: %lu bytes at %lu are past the map's %lu",
				!source ? selector : size, offset, source ? source->whole_size : 0);
			return FALSE;
		}
		{
			long i;

			for (i = source->segment_count - 1; i >= 0; i--)
			{
				struct mcc_segment const *segment = &source->segments[i];

				if (offset >= segment->start && offset + size <= segment->start + segment->size)
				{
					memcpy(out, segment->data + (offset - segment->start), size);
					return TRUE;
				}
			}
		}
		{
			unsigned char *chunk;
			unsigned long at = offset, done = 0;

			while (done < size)
			{
				unsigned long piece;

				if (at < H2_MCC_HEADER_SIZE)
				{
					piece = H2_MCC_HEADER_SIZE - at < size - done ? H2_MCC_HEADER_SIZE - at : size - done;
					memcpy((unsigned char *)out + done, source->header_bytes + at, piece);
				}
				else
				{
					unsigned long k = (at - H2_MCC_HEADER_SIZE) / H2_MCC_CHUNK_SIZE;
					unsigned long within = (at - H2_MCC_HEADER_SIZE) % H2_MCC_CHUNK_SIZE;
					unsigned long got;
					int e, kept = -1, oldest = 0, keep = chunks_kept_count();

					/* (a chunk inflated lately: as it was; else the least
					recently used one's room, inflated again) */
					for (e = 0; e < keep; e++)
					{
						if (source->chunks_kept[e].data && source->chunks_kept[e].index == k)
							kept = e;
						if (!source->chunks_kept[e].data || source->chunks_kept[e].used < source->chunks_kept[oldest].used)
							oldest = source->chunks_kept[e].data || !source->chunks_kept[oldest].data ? e : oldest;
					}
					if (kept < 0)
					{
						kept = oldest;
						if (!source->chunks_kept[kept].data)
						{
							source->chunks_kept[kept].data = malloc(H2_MCC_CHUNK_SIZE);
							source->chunks_kept[kept].index = (unsigned long)-1;
						}
						if (!source->chunks_kept[kept].data)
						{
							resource_read_complaint("a raw resource not read: no chunk of %lu KB to inflate into", (unsigned long)H2_MCC_CHUNK_SIZE / 1024);
							return FALSE;
						}
						got = k < source->chunk_count ? mcc_inflate_chunk(source, k, source->chunks_kept[kept].data, H2_MCC_CHUNK_SIZE) : 0;
						source->chunks_kept[kept].index = got ? k : (unsigned long)-1;
						source->chunks_kept[kept].got = got;
						resource_chunks++;
						resource_chunk_bytes += got;
					}
					else
						got = source->chunks_kept[kept].got;
					source->chunks_kept[kept].used = ++source->chunk_use_stamp;
					chunk = source->chunks_kept[kept].data;
					if (got <= within)
					{
						resource_read_complaint("a raw resource not read: chunk %lu of %lu gave %lu bytes, %lu wanted", k, source->chunk_count, got, within + 1);
						return FALSE;
					}
					piece = got - within < size - done ? got - within : size - done;
					memcpy((unsigned char *)out + done, chunk + within, piece);
					if (source->touched)
						source->touched[k] = 1;
				}
				at += piece;
				done += piece;
			}
		}
		if (source->touched)
		{
			unsigned long first = offset / H2_MCC_CHUNK_SIZE, last = (offset + (size ? size - 1 : 0)) / H2_MCC_CHUNK_SIZE;

			for (; first <= last; first++)
				source->touched[first] = 1;
		}
		return TRUE;
	}
	file = resource_file(cache, selector);
	if (!file || offset > cache->file_sizes[selector] || size > cache->file_sizes[selector] - offset)
		return FALSE;

	return file_read_at(file, offset, out, size);
}

H2_ALIGN void const *h2_cache_file_bytes(
	struct h2_cache *cache,
	unsigned long offset,
	unsigned long size)
{
	return cache && cache->mcc ? mcc_bytes(cache, offset, size) : NULL;
}

H2_ALIGN boolean h2_cache_is_mcc(
	struct h2_cache const *cache)
{
	return cache->mcc;
}

/* (a texture's buffers) where the segments come from (h2_cache_segment_memory) */
H2_ALIGN static void *buffer_alloc(struct h2_cache *cache, boolean window, unsigned long size)
{
	void *p = window && cache->segment_alloc ? cache->segment_alloc(size ? size : 1) : NULL;

	return p ? p : malloc(size ? size : 1);
}

H2_ALIGN static void buffer_free(struct h2_cache *cache, boolean window, void *p)
{
	if (window && cache->segment_free)
		cache->segment_free(p, 0);
	else
		free(p);
}

/* the buffer made capacity bytes, its first used kept */
H2_ALIGN static unsigned char *buffer_grow(struct h2_cache *cache, boolean window, unsigned char *p, unsigned long used,
	unsigned long capacity)
{
	unsigned char *grown;

	if (!window || !cache->segment_alloc)
		return realloc(p, capacity);
	grown = buffer_alloc(cache, window, capacity);
	if (grown && p)
	{
		memcpy(grown, p, used);
		buffer_free(cache, window, p);
	}
	return grown;
}

H2_ALIGN void h2_cache_texture_free(
	struct h2_cache *cache,
	void *pixels)
{
	if (pixels)
		buffer_free(cache, TRUE, pixels);
}

/* where a texture's decompressed bytes go: kept (grown as they come, or
sized once by expected), dropped (the first skip), or - a single level of
4 bytes a texel, reduce_log2 > 0 - averaged 2^k x 2^k as they come, so a
level too big to be kept is never whole in memory */
struct texture_sink
{
	struct h2_cache *cache;
	boolean window;
	unsigned char *out;
	unsigned long done, capacity;
	unsigned long skip, skipped;
	/* (reduce) */
	int reduce_log2;
	unsigned long width, row_bytes, band_rows, band_fill;   /* band_fill: bytes of the band */
	unsigned long *sums;
	boolean failed;
};

H2_ALIGN static boolean sink_room(struct texture_sink *k, unsigned long n)
{
	if (k->done + n <= k->capacity)
		return TRUE;
	{
		unsigned long capacity = k->capacity ? k->capacity : (k->done + n > 0x10000 ? k->done + n : 0x10000);
		unsigned char *grown;

		while (capacity < k->done + n)
			capacity *= 2;
		grown = buffer_grow(k->cache, k->window, k->out, k->done, capacity);
		if (!grown)
			return k->failed = TRUE, FALSE;
		k->out = grown;
		k->capacity = capacity;
	}
	return TRUE;
}

H2_ALIGN static void sink_put(struct texture_sink *k, unsigned char const *data, unsigned long n)
{
	if (k->failed)
		return;
	if (k->skipped < k->skip)
	{
		unsigned long dropped = k->skip - k->skipped < n ? k->skip - k->skipped : n;

		k->skipped += dropped;
		data += dropped;
		n -= dropped;
	}
	if (!n)
		return;
	if (!k->reduce_log2)
	{
		if (sink_room(k, n))
		{
			memcpy(k->out + k->done, data, n);
			k->done += n;
		}
		return;
	}
	while (n && !k->failed)
	{
		/* (the band's texels summed per output texel: rows of the band
		added as they come) */
		unsigned long band_bytes = k->band_rows * k->row_bytes;
		unsigned long take = band_bytes - k->band_fill < n ? band_bytes - k->band_fill : n, j;
		unsigned long factor = 1UL << k->reduce_log2, out_width = k->width >> k->reduce_log2;
		/* (each byte's place in its row, kept as it goes rather than divided
		out of the band's: a row is 4 bytes a texel, so its channel is the
		place's low two bits and its output texel the place over 4 * factor) */
		unsigned long place = k->band_fill % k->row_bytes, shift = 2 + (unsigned long)k->reduce_log2;

		for (j = 0; j < take; j++)
		{
			k->sums[((place >> shift) << 2) | (place & 3)] += data[j];
			if (++place == k->row_bytes)
				place = 0;
		}
		k->band_fill += take;
		data += take;
		n -= take;
		if (k->band_fill == band_bytes)
		{
			unsigned long x, c, count = factor * factor;

			if (!sink_room(k, out_width * 4))
				return;
			for (x = 0; x < out_width; x++)
				for (c = 0; c < 4; c++)
					k->out[k->done + 4 * x + c] = (unsigned char)((k->sums[4 * x + c] + count / 2) / count);
			k->done += out_width * 4;
			memset(k->sums, 0, sizeof(*k->sums) * 4 * out_width);
			k->band_fill = 0;
		}
	}
}

H2_ALIGN static void *texture_read(
	struct h2_cache *cache,
	unsigned long reference,
	unsigned long size,
	unsigned long *out_size,
	boolean window,
	struct h2_cache_texture_options const *options,
	unsigned long most)
{
	boolean stopped = FALSE;
	unsigned char *out;

	*out_size = 0;
	if (!cache->mcc)
	{
		out = buffer_alloc(cache, window, size);
		if (!out || !h2_cache_resource_read(cache, reference, out, size))
		{
			if (out)
				buffer_free(cache, window, out);
			return NULL;
		}
		*out_size = size;
		return out;
	}
	/* (MCC: textures.dat, an entry of size bytes at the reference: its
	chunk count, each chunk's size, then the chunks: a zlib stream each, or
	- its size negative - that many bytes stored as they are) */
	{
		unsigned char *entry, *scratch = NULL;
		unsigned long chunks, i, at, sum = 0, entry_held = 0;
		int status = Z_OK;
		struct texture_sink k;

		/* (textures.dat not open - an open that found no file handle free,
		the Vita's few: tried again) */
		pthread_mutex_lock(&textures_lock);
		if (!cache->textures)
		{
			char path[H2_CACHE_NAME_SIZE + 64];
			char const *h2_resource_directory(char const *preferred, char const *file);

			(void)path;
			cache->textures = textures_dat_open(cache->directory);
		}
		pthread_mutex_unlock(&textures_lock);
		if (!cache->textures || reference == 0xFFFFFFFFUL || size < 8 || size > 64 * 1024 * 1024)
		{
			resource_read_complaint(!cache->textures ? "a texture not read: textures.dat is not open" :
				"a texture not read: %lu bytes at %lu", size, reference);
			return NULL;
		}
		memset(&k, 0, sizeof(k));
		k.cache = cache;
		k.window = window;
		if (options)
		{
			k.skip = options->skip;
			if (options->reduce_log2 > 0 && options->width >> options->reduce_log2 && options->height >> options->reduce_log2)
			{
				k.reduce_log2 = options->reduce_log2;
				k.width = options->width;
				k.row_bytes = 4 * options->width;
				k.band_rows = 1UL << options->reduce_log2;
				k.sums = calloc(4 * (options->width >> options->reduce_log2), sizeof(*k.sums));
				if (!k.sums)
					return NULL;
			}
			if (options->expected && !sink_room(&k, options->expected))
			{
				resource_read_complaint("a texture not read: no memory for %lu KB of pixels", options->expected / 1024);
				free(k.sums);
				return NULL;
			}
		}
		/* (the entry's head read - its chunk count and sizes - then each
		chunk wanted read and inflated in turn: no more of it in memory than
		a chunk, its compressed bytes - a 7.5 MB entry held whole stopped
		03a's direct make on the Vita's builder heap; the chunks before the
		first byte wanted not read at all. The file reads under a lock: the
		lightmap pages are decoded on the worker threads, h2_level.c's page
		tiles; the inflate outside it) */
		pthread_mutex_lock(&textures_lock);
		entry = texture_head_read(cache->textures, reference, size, &chunks, &entry_held);
		pthread_mutex_unlock(&textures_lock);
		if (!entry)
		{
			resource_read_complaint("a texture not read: %lu bytes at %lu: its head not read", size, reference);
			if (k.out)
				buffer_free(cache, window, k.out);
			free(k.sums);
			return NULL;
		}
		for (i = 0; i < chunks; i++)
		{
			long csize = (long)read_le32(entry + 4 + 4 * i);

			sum += (unsigned long)(csize < 0 ? -csize : csize);
		}
		scratch = malloc(0x10000);
		if (!scratch || sum != size - 4 * (chunks + 1))
			i = 0, chunks = 1, k.failed = TRUE;
		at = 4 + 4 * chunks;
		/* (an entry's chunks inflate to H2_TEXTURE_CHUNK_BYTES each, the last
		to the rest: the chunks before the first byte wanted are passed over
		by arithmetic, not read - a level of a texture costs its own chunks
		alone, however large the levels above it) */
		if (k.skip >= H2_TEXTURE_CHUNK_BYTES && !k.reduce_log2 && !getenv("HALO_H2_NO_CHUNK_SKIP") && !k.failed)
		{
			unsigned long passed = k.skip / H2_TEXTURE_CHUNK_BYTES;

			if (passed > chunks)
				passed = chunks;
			for (i = 0; i < (long)passed; i++)
			{
				long csize = (long)read_le32(entry + 4 + 4 * i);

				at += (unsigned long)(csize < 0 ? -csize : csize);
			}
			/* (the rest of the skip falls in the first chunk inflated: the
			sink drops it - k.skipped stays 0. HALO_H2_NO_CHUNK_SKIP=1: every
			chunk inflated, the reference) */
			k.skip -= passed * H2_TEXTURE_CHUNK_BYTES;
		}
		else if (!k.failed)
			i = 0;
		/* (its room made once, for the chunks still to come - each inflates
		to H2_TEXTURE_CHUNK_BYTES at most -, not doubled as they land: a
		water ripple sheet just over 8 MB asked a derive worker for 16 MB, the
		owner's Vita had no room) */
		if (!k.failed && !k.reduce_log2 && !k.capacity && i < chunks)
		{
			unsigned long most_bytes = most ? most + k.skip : 0;
			unsigned long most = (unsigned long)(chunks - i) * H2_TEXTURE_CHUNK_BYTES;

			if (most_bytes && most_bytes > k.skip && most > most_bytes)
				most = most_bytes;
			if (most > k.skip && !sink_room(&k, most - k.skip))
				k.failed = TRUE;
		}
		{
			unsigned char *chunk = NULL, *whole = NULL;
			unsigned long chunk_capacity = 0;

			for (; i < chunks && !k.failed; i++)
			{
				long csize = (long)read_le32(entry + 4 + 4 * i);
				unsigned long bytes = (unsigned long)(csize < 0 ? -csize : csize);
				z_stream stream;
				boolean read;
				unsigned long long inflate_started;

				/* (h2_cache_texture_read_most: what was wanted is in) */
				if (most && k.done >= most)
				{
					stopped = TRUE;
					break;
				}

				if (bytes > chunk_capacity)
				{
					free(chunk);
					chunk = malloc(bytes ? bytes : 1);
					chunk_capacity = chunk ? bytes : 0;
					if (!chunk)
					{
						resource_read_complaint("a texture not read: no memory for a %lu KB chunk of its entry", bytes / 1024);
						break;
					}
				}
				/* (a chunk the head's read held - a small entry's: copied) */
				if (at + bytes <= entry_held)
				{
					memcpy(chunk, entry + at, bytes);
					read = TRUE;
				}
				else
				{
					pthread_mutex_lock(&textures_lock);
					read = file_read_at(cache->textures, reference + at, chunk, bytes);
					pthread_mutex_unlock(&textures_lock);
				}
				if (!read)
				{
					resource_read_complaint("a texture not read: %lu bytes at %lu not read from textures.dat", size, reference);
					break;
				}
				at += bytes;
				if (csize < 0)
				{
					sink_put(&k, chunk, bytes);
					continue;
				}
				/* (libdeflate's: the chunk's zlib stream inflated whole - it
				inflates to H2_TEXTURE_CHUNK_BYTES at most - and given the sink
				at once, else zlib's as before) */
				if (!whole && libdeflate_on())
					whole = malloc(H2_TEXTURE_CHUNK_BYTES);
				inflate_started = vita_host_time_us();
				if (whole)
				{
					long made = inflate_whole(chunk, bytes, whole, H2_TEXTURE_CHUNK_BYTES, TRUE);

					if (made >= 0)
					{
						io_inflated(inflate_started);
						sink_put(&k, whole, (unsigned long)made);
						continue;
					}
				}
				memset(&stream, 0, sizeof(stream));
				if (inflateInit(&stream) != Z_OK)
					break;
				stream.next_in = chunk;
				stream.avail_in = (uInt)bytes;
				do
				{
					stream.next_out = scratch;
					stream.avail_out = 0x10000;
					status = inflate(&stream, Z_NO_FLUSH);
					sink_put(&k, scratch, 0x10000 - stream.avail_out);
				} while (status == Z_OK && !k.failed);
				/* (HALO_H2_DEBUG_CHUNKS=1: each chunk's bytes, compressed and
				inflated - whether a level can be reached by chunk arithmetic) */
				if (getenv("HALO_H2_DEBUG_CHUNKS"))
					platform_log("h2 cache: textures.dat entry %lu (%lu bytes, %lu chunks): chunk %lu %ld -> %lu", reference, size,
						chunks, i, csize, stream.total_out);
				inflateEnd(&stream);
				io_inflated(inflate_started);
				if (status != Z_STREAM_END)
					break;
			}
			free(chunk);
			free(whole);
		}
		free(entry);
		free(scratch);
		free(k.sums);
		if ((i < chunks && !stopped) || k.failed || !k.done)
		{
			if (getenv("HALO_H2_TEXTURE_READ_LOG"))
				platform_log("h2 cache: texture at %08lx (%lu bytes) not read: %ld of %lu chunks, %s, %lu bytes out (skip %lu)", reference, size, i,
					chunks, k.failed ? "the sink failed" : "the sink is whole", k.done, options ? options->skip : 0UL);
			if (k.out)
				buffer_free(cache, window, k.out);
			return NULL;
		}
		*out_size = most && k.done > most ? most : k.done;
		return k.out;
	}
}

H2_ALIGN void *h2_cache_texture_read_most(
	struct h2_cache *cache,
	unsigned long reference,
	unsigned long size,
	unsigned long most,
	unsigned long *out_size)
{
	return texture_read(cache, reference, size, out_size, FALSE, NULL, most);
}

H2_ALIGN void *h2_cache_texture_read(
	struct h2_cache *cache,
	unsigned long reference,
	unsigned long size,
	unsigned long *out_size)
{
	return texture_read(cache, reference, size, out_size, FALSE, NULL, 0);
}

H2_ALIGN void *h2_cache_texture_read_options(
	struct h2_cache *cache,
	unsigned long reference,
	unsigned long size,
	struct h2_cache_texture_options const *options,
	unsigned long *out_size)
{
	return texture_read(cache, reference, size, out_size, FALSE, options, 0);
}

/* (as h2_cache_texture_read) bytes skip .. skip + most of the entry's
inflated bytes alone: the chunks before them passed over by arithmetic, the
ones past them not inflated - a band of a big bitmap's rows, in the heap
(never the memory window) */
H2_ALIGN void *h2_cache_texture_read_span(
	struct h2_cache *cache,
	unsigned long reference,
	unsigned long size,
	unsigned long skip,
	unsigned long most,
	unsigned long *out_size)
{
	struct h2_cache_texture_options options;

	memset(&options, 0, sizeof(options));
	options.skip = skip;
	return texture_read(cache, reference, size, out_size, FALSE, &options, most);
}

H2_ALIGN void *h2_cache_texture_read_window(
	struct h2_cache *cache,
	unsigned long reference,
	unsigned long size,
	struct h2_cache_texture_options const *options,
	unsigned long *out_size)
{
	return texture_read(cache, reference, size, out_size, TRUE, options, 0);
}

H2_ALIGN short h2_cache_address_source(
	struct h2_cache const *cache,
	unsigned long address)
{
	short i;

	if (!cache->mcc || address - cache->tag_data_address < cache->tag_data_size)
		return 0;
	for (i = 1; i < H2_RESOURCE_FILES; i++)
	{
		if (cache->shared[i] && address - cache->shared[i]->tag_data_address < cache->shared[i]->tag_data_size)
			return i;
	}
	return 0;
}

/* every segment of this cache let go (not the shared maps': theirs
through h2_cache_segment_memory's own recursion) */
H2_ALIGN static void segments_release_all(struct h2_cache *cache)
{
	long i;

	for (i = 0; i < cache->segment_count; i++)
		segment_release(cache, &cache->segments[i]);
	cache->segment_count = 0;
	cache->segment_last = -1;
}

H2_ALIGN void h2_cache_trim(
	struct h2_cache *cache)
{
	long i;
	short k;

	long let_go;

	if (!cache || !cache->mcc)
		return;
	/* (the kept one, shared: its newest H2_MCC_SEGMENTS_KEPT stay, as its
	budget's evictions leave them - what a user read last) */
	let_go = cache->segment_count;
	if (cache == kept.cache && kept.users > 0)
		let_go = cache->segment_count > H2_MCC_SEGMENTS_KEPT ? cache->segment_count - H2_MCC_SEGMENTS_KEPT : 0;
	if (!let_go)
		return;
	for (i = 0; i < let_go; i++)
		segment_release(cache, &cache->segments[i]);
	memmove(cache->segments, cache->segments + let_go, (size_t)(cache->segment_count - let_go) * sizeof(*cache->segments));
	cache->segment_count -= let_go;
	cache->segment_last = -1;
	if (cache->segment_count)
		return;
	/* (and the chunk buffers, made again when next needed: a cache kept
	open between a level's BSPs holds the C heap only for its tables) */
	free(cache->compressed);
	cache->compressed = NULL;
	{
		int k;

		for (k = 0; k < H2_CACHE_CHUNKS_KEPT; k++)
		{
			free(cache->chunks_kept[k].data);
			cache->chunks_kept[k].data = NULL;
			cache->chunks_kept[k].index = (unsigned long)-1;
		}
	}
	for (k = 0; k < H2_RESOURCE_FILES; k++)
	{
		if (cache->shared[k])
			h2_cache_trim(cache->shared[k]);
	}
}

H2_ALIGN boolean h2_cache_trim_past(
	struct h2_cache *cache,
	unsigned long bytes)
{
	unsigned long held;
	short k;

	if (!cache || !cache->mcc)
		return FALSE;
	held = cache->segment_bytes;
	for (k = 0; k < H2_RESOURCE_FILES; k++)
	{
		if (cache->shared[k])
			held += cache->shared[k]->segment_bytes;
	}
	if (held <= bytes)
		return FALSE;
	h2_cache_trim(cache);
	return TRUE;
}






/* (player models, h2_biped.c) a string id's text: MCC's string table, its
count, data, size and offsets at the header's +0x30..+0x3C (the low 24 bits
of an id its index); FALSE and "" if there is none */
H2_ALIGN boolean h2_cache_string(
	struct h2_cache *cache,
	unsigned long id,
	char *out,
	long out_size)
{
	unsigned long count, data, size, offsets, index = id & 0xFFFFFFUL, at, n;
	unsigned char const *p;

	if (out_size < 1)
		return FALSE;
	out[0] = 0;
	if (!cache->mcc || !id || id == 0xFFFFFFFFUL)
		return FALSE;
	count = read_le32(cache->header_bytes + 0x30);
	data = read_le32(cache->header_bytes + 0x34);
	size = read_le32(cache->header_bytes + 0x38);
	offsets = read_le32(cache->header_bytes + 0x3C);
	if (index >= count || !(p = mcc_bytes(cache, offsets + 4 * index, 4)))
		return FALSE;
	at = read_le32(p);
	if (at >= size)
		return FALSE;
	n = size - at < (unsigned long)out_size - 1 ? size - at : (unsigned long)out_size - 1;
	if (!(p = mcc_bytes(cache, data + at, n)))
		return FALSE;
	memcpy(out, p, n);
	out[n] = 0;
	return out[0] != 0;
}

/* (the menus, h2_menu_make.c) a string of the globals' language pack (MCC:
the globals at 0x190, 0x1C a language - its count, its text's size, the
file offsets of its index, 8 bytes a string: string id and offset into the
text, and of its UTF-8 text) */
H2_ALIGN boolean h2_cache_localized_string(
	struct h2_cache *cache,
	long language,
	long index,
	unsigned long *string_id,
	char *out,
	long out_size)
{
	struct h2_cache_tag globals;
	unsigned char const *pack, *p;
	unsigned long count, size, offsets, data, at, n;
	long i;

	if (out_size < 1)
		return FALSE;
	out[0] = 0;
	if (!cache->mcc || language < 0 || language >= 9 || index < 0)
		return FALSE;
	/* (the scan's answer kept: a walk over the strings asks each time) */
	i = cache->globals_found ? cache->globals_index : 0;
	for (; i < cache->tag_count; i++)
	{
		if (h2_cache_tag_get(cache, i, &globals) && globals.group_tag == 'matg')
			break;
	}
	cache->globals_index = i;
	cache->globals_found = TRUE;
	if (i == cache->tag_count || globals.size < 0x190 + 0x1C * 9UL ||
		!(pack = h2_cache_at(cache, globals.address + 0x190 + 0x1C * (unsigned long)language, 16)))
	{
		return FALSE;
	}
	count = read_le32(pack);
	size = read_le32(pack + 4);
	/* (shared.map's - a multiplayer map's - have their top bit set: the
	offset is the rest) */
	offsets = read_le32(pack + 8) & 0x7FFFFFFFUL;
	data = read_le32(pack + 12) & 0x7FFFFFFFUL;
	/* (a multiplayer map's globals are shared.map's: its pack's offsets are
	that file's) */
	{
		short source = h2_cache_address_source(cache, globals.address);

		if (source > 0 && cache->shared[source])
			cache = cache->shared[source];
	}
	if ((unsigned long)index >= count || !(p = mcc_bytes(cache, offsets + 8 * (unsigned long)index, 8)))
		return FALSE;
	at = read_le32(p + 4);
	if (string_id)
		*string_id = read_le32(p);
	if (at >= size)
		return FALSE;
	n = size - at < (unsigned long)out_size - 1 ? size - at : (unsigned long)out_size - 1;
	if (!(p = mcc_bytes(cache, data + at, n)))
		return FALSE;
	memcpy(out, p, n);
	out[n] = 0;
	return out[0] != 0;
}

H2_ALIGN static unsigned long name_hash(char const *name)
{
	unsigned long hash = 2166136261UL;

	while (*name)
		hash = (hash ^ (unsigned char)*name++) * 16777619UL;
	return hash;
}

/* (h2_cache_tag_find) the names' index: every tag on the chain of its name's
hash, in index order - the names are the cache's own memory, read once at
its open, so nothing of the map is read to make it */
H2_ALIGN static boolean name_index_make(struct h2_cache *cache)
{
	unsigned long buckets = 1024;
	long i;
	long *tails;

	if (cache->name_heads)
		return TRUE;
	if (cache->name_index_tried || cache->tag_count <= 0)
		return FALSE;
	cache->name_index_tried = TRUE;
	while (buckets < 2UL * (unsigned long)cache->tag_count)
		buckets *= 2;
	cache->name_heads = malloc(buckets * sizeof(long));
	cache->name_next = malloc((size_t)cache->tag_count * sizeof(long));
	tails = malloc(buckets * sizeof(long));
	if (!cache->name_heads || !cache->name_next || !tails)
	{
		free(cache->name_heads);
		free(cache->name_next);
		free(tails);
		cache->name_heads = cache->name_next = NULL;
		return FALSE;
	}
	for (i = 0; i < (long)buckets; i++)
		cache->name_heads[i] = tails[i] = NONE;
	cache->name_hash_mask = buckets - 1;
	for (i = 0; i < cache->tag_count; i++)
	{
		unsigned long b = name_hash(h2_cache_tag_name(cache, i)) & cache->name_hash_mask;

		cache->name_next[i] = NONE;
		if (tails[b] == NONE)
			cache->name_heads[b] = i;
		else
			cache->name_next[tails[b]] = i;
		tails[b] = i;
	}
	free(tails);
	return TRUE;
}

/* (player models) a tag by its group and name, NONE if the map has none: the
first (lowest index) of that name whose entry reads and is of that group -
by the names' index, the tags of other names not read (each read of an MCC
map's entry finds its piece of the inflated file: a walk over them all was
half of a direct level's make, Nov 4 2026); the walk where no index can be
made */
H2_ALIGN long h2_cache_tag_find(
	struct h2_cache const *cache,
	unsigned long group_tag,
	char const *name)
{
	long i;

	if (name_index_make((struct h2_cache *)cache))
	{
		for (i = cache->name_heads[name_hash(name) & cache->name_hash_mask]; i != NONE; i = cache->name_next[i])
		{
			struct h2_cache_tag tag;

			if (!strcmp(h2_cache_tag_name(cache, i), name) && h2_cache_tag_get(cache, i, &tag) && tag.group_tag == group_tag)
				return i;
		}
		return NONE;
	}
	for (i = 0; i < cache->tag_count; i++)
	{
		struct h2_cache_tag tag;

		if (h2_cache_tag_get(cache, i, &tag) && tag.group_tag == group_tag && !strcmp(h2_cache_tag_name(cache, i), name))
			return i;
	}
	return NONE;
}

H2_ALIGN void h2_cache_segment_memory(
	struct h2_cache *cache,
	void *(*segment_alloc)(unsigned long size),
	void (*segment_free)(void *data, unsigned long size))
{
	short k;

	if (!cache)
		return;
	/* (its segments were made by the hooks it had - a kept cache's by a
	module's, the HUD's or the sounds' memory in the level's window -: let
	go before the hooks change, not freed by the wrong one later, nor read
	after the window they lie in is reused) */
	if ((cache->segment_alloc != segment_alloc || cache->segment_free != segment_free) && cache->segment_count)
		segments_release_all(cache);
	cache->segment_alloc = segment_alloc;
	cache->segment_free = segment_free;
	for (k = 0; k < H2_RESOURCE_FILES; k++)
	{
		if (cache->shared[k])
			h2_cache_segment_memory(cache->shared[k], segment_alloc, segment_free);
	}
}

H2_ALIGN void h2_cache_segment_budget(
	struct h2_cache *cache,
	unsigned long bytes)
{
	short k;

	if (!cache)
		return;
	/* (the kept one's: the most any of its users asked for, so one module's
	inflated chunks serve the next) */
	if (cache == kept.cache && bytes < cache->segment_budget)
		return;
	cache->segment_budget = bytes;
	for (k = 0; k < H2_RESOURCE_FILES; k++)
	{
		if (cache->shared[k])
			h2_cache_segment_budget(cache->shared[k], bytes);
	}
}

H2_ALIGN boolean h2_cache_memory_failed(
	struct h2_cache *cache)
{
	boolean failed;
	short k;

	if (!cache)
		return FALSE;
	failed = cache->memory_failed;
	cache->memory_failed = FALSE;
	for (k = 0; k < H2_RESOURCE_FILES; k++)
	{
		if (cache->shared[k] && h2_cache_memory_failed(cache->shared[k]))
			failed = TRUE;
	}
	return failed;
}

/* (port) the bytes its segments and its shared maps' hold */
H2_ALIGN unsigned long h2_cache_segment_bytes(
	struct h2_cache const *cache)
{
	unsigned long bytes;
	short k;

	if (!cache)
		return 0;
	bytes = cache->segment_bytes;
	for (k = 0; k < H2_RESOURCE_FILES; k++)
		if (cache->shared[k])
			bytes += h2_cache_segment_bytes(cache->shared[k]);
	return bytes;
}

H2_ALIGN void h2_cache_segment_counts(
	struct h2_cache const *cache,
	unsigned long *inflated,
	unsigned long *evicted)
{
	short k;

	if (!cache)
		return;
	*inflated += cache->inflated;
	*evicted += cache->evicted;
	for (k = 0; k < H2_RESOURCE_FILES; k++)
	{
		if (cache->shared[k])
			h2_cache_segment_counts(cache->shared[k], inflated, evicted);
	}
}

/* (feat/h2-level-at-load: a direct level made without its checks) a
textures.dat entry's bytes once inflated, without inflating it: every chunk
but the last inflates to H2_TEXTURE_CHUNK_BYTES (texture_read's own rule),
the last one - stored as it is (its size negative), or inflated here alone,
counted - to the rest. FALSE if the entry cannot be read */
boolean h2_cache_texture_inflated_size(
	struct h2_cache *cache,
	unsigned long reference,
	unsigned long size,
	unsigned long *inflated)
{
	unsigned char *head = NULL, *last = NULL, out[0x4000];
	unsigned long chunks = 0, i, at, sum = 0, last_size = 0, total = 0, held = 0;
	long csize;
	boolean ok = FALSE;

	*inflated = 0;
	if (!cache || !cache->mcc || reference == 0xFFFFFFFFUL || size < 8 || size > 64 * 1024 * 1024)
		return FALSE;
	pthread_mutex_lock(&textures_lock);
	if (!cache->textures)
	{
		char path[H2_CACHE_NAME_SIZE + 64];
		char const *h2_resource_directory(char const *preferred, char const *file);

		(void)path;
		cache->textures = textures_dat_open(cache->directory);
	}
	head = cache->textures ? texture_head_read(cache->textures, reference, size, &chunks, &held) : NULL;
	if (head)
	{
		for (i = 0; i < chunks; i++)
		{
			csize = (long)read_le32(head + 4 + 4 * i);
			sum += (unsigned long)(csize < 0 ? -csize : csize);
		}
		csize = (long)read_le32(head + 4 + 4 * (chunks - 1));
		at = reference + 4 + 4 * chunks + sum - (unsigned long)(csize < 0 ? -csize : csize);
		if (sum == size - 4 * (chunks + 1))
		{
			if (csize < 0)
				last_size = (unsigned long)-csize, ok = TRUE;
			else if ((last = malloc((size_t)csize ? (size_t)csize : 1)) != NULL)
			{
				/* (the last chunk in the head's read - a small entry's: copied) */
				if (at - reference + (unsigned long)csize <= held)
				{
					memcpy(last, head + (at - reference), (size_t)csize);
					ok = TRUE;
				}
				else if (file_read_at(cache->textures, at, last, (unsigned long)csize))
					ok = TRUE;
			}
		}
	}
	pthread_mutex_unlock(&textures_lock);
	/* (the last chunk inflated outside the lock, its bytes counted) */
	if (ok && last)
	{
		z_stream z;
		int status;

		memset(&z, 0, sizeof(z));
		ok = inflateInit(&z) == Z_OK;
		if (ok)
		{
			z.next_in = last;
			z.avail_in = (uInt)csize;
			do
			{
				z.next_out = out;
				z.avail_out = sizeof(out);
				status = inflate(&z, Z_NO_FLUSH);
				last_size += sizeof(out) - z.avail_out;
			}
			while (status == Z_OK);
			ok = status == Z_STREAM_END;
			inflateEnd(&z);
		}
	}
	if (ok)
	{
		total = (chunks - 1) * (unsigned long)H2_TEXTURE_CHUNK_BYTES + last_size;
		*inflated = total;
	}
	free(head);
	free(last);
	return ok;
}
