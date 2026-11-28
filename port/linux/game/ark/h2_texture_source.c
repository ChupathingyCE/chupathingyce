/*
H2_TEXTURE_SOURCE.C

(port) A Halo 2 level's bitmaps read from MCC's textures.dat at load
(h2_texture_source.h).

A textures.dat entry is a chunk count, each chunk's compressed size (a
negative size: stored as it is), then the chunks, a zlib stream each; every
chunk inflates to H2_TEXTURE_SOURCE_CHUNK_BYTES but the last. So the bytes a
read wants lie in chunks found by arithmetic, and those alone are read from
the file and inflated: a level of a texture costs its own chunks, whatever
the levels above it. The level's record (h2_level.c) says, for each bitmap
tag so read, the entry, how many bytes of it the level passes over (the
levels bigger than its size limit) and how many it keeps, and the pixels
offset its bitmap_data carries: a range of its own above any level file's
length, by which the cache file's reads name it.
*/

#include "stall_trace.h"
#include "cseries.h"
#include "h2_texture_source.h"
#include "h2_level.h"
#include "h2_cache.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <time.h>
#include <unistd.h>

/* (the C library's allocator, as h2_level.c's and h2_cache.c's: a derived
bitmap's pixels are made there and freed here) */
#undef malloc
#undef free
#undef realloc
/* (the Vita tier: a derived bitmap's pixels made by the worker are the
derive arena's - h2_build_heap.h - and freed there, wherever freed) */
#define H2_BUILD_HEAP_WRAP
#include "h2_build_heap.h"

void platform_log(const char *format, ...);
void platform_memory_log(const char *when, const char *map_name);
int halo_thread_index(void);
int halo_game_thread_index(void);
boolean h2_level_derived_pixels(struct h2_cache *cache, unsigned char const *params, unsigned long bytes, int tail,
	unsigned char **pixels, unsigned long *size);
boolean h2_level_derived_banded(struct h2_cache *cache, unsigned char const *params, unsigned long bytes, unsigned long *total);
int h2_level_derived_range(struct h2_cache *cache, unsigned char const *params, unsigned long bytes, unsigned long chain,
	unsigned long wanted, unsigned char *out);
unsigned long long vita_host_time_us(void);
/* (platform.h) the memory a read is about to write marked written - as
xbox_files.c's reads do - so that a texture decoded from those addresses
before is decoded again (vita_memory_watch.c: the Vita has no page
protection; the host says so before it writes) */
void memory_watch_prepare_write(void *address, unsigned long size);

enum
{
	MAXIMUM_CHUNKS = 4096
};

struct dat_entry
{
	long tag_index;
	long kind;                        /* _h2_texture_source_dat, ... */
	unsigned long offset;             /* the pixels offset the bitmap's record carries */
	unsigned long kept;               /* the pixels' bytes */
	unsigned long sum;                /* their FNV-1a sum, as the builder saw them */
	unsigned long reference, bytes;   /* (a textures.dat copy) the entry in textures.dat; (a derived one)
	                                  its parameters' offset in params and their bytes */
	unsigned long skip;               /* (a textures.dat copy) the bytes of it passed over */
};

/* (a derived bitmap made: kept while there is room, the least recently
read let go first) */
enum
{
	/* (a characters' map made ahead counts as read this many reads ahead) */
	DERIVED_AHEAD_READS = 4096,
	DERIVED_SLOTS = 64,
	/* (a derived bitmap this big or more, made by bands where it can be: a 16 MB sky dome made whole took a block of the
	derive arena that the play's user RAM sometimes could not give - the read then tried again for ever) */
	DERIVED_BAND_MINIMUM = 1024 * 1024,
	/* (a read of a request that made nothing: tried again this many times - half a second apart - then failed) */
	DERIVED_TRIES_REQUEST = 24,
	WORKERS_MAXIMUM = 4
};

/* (a derived bitmap's read from the cache file thread, parked for the game
thread: h2_texture_source_park, h2_texture_source_pump) */
enum
{
	PARKED_MAXIMUM = 512   /* (every request slot: a derived read is never made on the cache file thread, whose stack is 16 KB) */
};

struct parked_read
{
	long tag_index, offset, size;
	void *buffer, *request;           /* (request: the cache file's, finished; NULL a blocking read's) */
	volatile int *done;               /* (a blocking read's: 1 read, -1 not) */
	long tries;                       /* (made and not: tried again, the memory let go first) */
	unsigned long long not_before_us; /* (tried again no sooner) */
	boolean ahead;                    /* (derived_characters_ahead's: kept past the LRU's next turns) */
	boolean band;                     /* (the whole make failed: made by bands, h2_level_derived_range) */
};

struct derived_slot
{
	struct dat_entry const *entry;
	unsigned char *pixels;
	unsigned long used;               /* (the read count when last read) */
};

static struct
{
	FILE *file;                       /* textures.dat */
	struct dat_entry *entries;
	long count;
	unsigned long reads, read_bytes, chunks_inflated, failures;
	unsigned long long read_us;
	/* (the derived ones) their parameters; an open of the map of this
	source's own (h2_cache_open_own: made on the cache file thread);
	those made, kept to kept_limit bytes */
	unsigned char *params;
	unsigned long params_bytes;
	struct h2_cache *base;            /* (the load's open of the map, held through the play: the game thread's) */
	struct h2_cache *view;            /* (the worker's view of it: h2_cache_open_view) */
	unsigned long long view_used_us;
	pthread_t workers[WORKERS_MAXIMUM];
	long worker_count, workers_ready;
	boolean stopping;
	struct dat_entry const *making[WORKERS_MAXIMUM];   /* (each worker's entry being made) */
	unsigned long prefetched, tails_made, tails_held_reads;
	unsigned long view_opens, view_peak;
	unsigned long long worst_make_us;
	unsigned long retries;
	struct derived_slot slots[DERIVED_SLOTS];
	unsigned long kept_bytes, kept_limit, kept_peak;
	unsigned long derived_made, derived_hits, derived_failed, derived_banded, derived_banded_failed;
	unsigned long long derived_us;
	struct parked_read parked[PARKED_MAXIMUM];
	long parked_count;
	unsigned long parked_total;
} h2_texture_source = { NULL, NULL, 0, 0, 0, 0, 0, 0, 0 };

static pthread_mutex_t *file_lock(void)
{
	static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

	return &lock;
}

static pthread_mutex_t *source_lock(void)
{
	static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

	return &lock;
}

/* (the worker's: a job parked, a blocking read's done) */
static pthread_cond_t source_wake = PTHREAD_COND_INITIALIZER, source_done = PTHREAD_COND_INITIALIZER;
static boolean verify_on_worker;

static int entry_compare(void const *a, void const *b);
static void worker_stop(void);
static void worker_start(void);
static void derived_prefetch(struct dat_entry const *e);
static void derived_characters_ahead(void);
static unsigned long pixels_sum(unsigned char const *p, unsigned long size);
static unsigned long derived_tail_bytes(struct dat_entry const *e);

static unsigned long read_u32(unsigned char const *p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static boolean file_read_at(FILE *file, unsigned long offset, void *out, unsigned long size)
{
	unsigned long long started = vita_host_time_us();
	boolean ok = fseek(file, (long)offset, SEEK_SET) == 0 && fread(out, 1, size, file) == size;

	/* (the load's steps' read counts: h2_cache's) */
	h2_cache_io_read_noted(size, (unsigned long)(vita_host_time_us() - started));
	return ok;
}

/* the level file's bytes at a cache file offset: past its 0x800 header the
builder writes the file as a zlib stream of stored blocks, 0xFFFF bytes each
behind a 5-byte block header, 2 bytes of zlib header first (h2_level.c
stream_flush) - what the header's offsets count is the stored bytes */
static boolean level_read_at(FILE *file, unsigned long offset, unsigned char *out, unsigned long size)
{
	enum { HEADER = 0x800, BLOCK = 0xFFFF };

	while (size)
	{
		unsigned long block = (offset - HEADER) / BLOCK, in_block = (offset - HEADER) % BLOCK;
		unsigned long n = BLOCK - in_block < size ? BLOCK - in_block : size;
		unsigned long at = HEADER + 2 + 5 * (block + 1) + (offset - HEADER);

		if (offset < HEADER || !file_read_at(file, at, out, n))
			return FALSE;
		out += n;
		offset += n;
		size -= n;
	}
	return TRUE;
}

void h2_texture_source_report(void);
void cache_file_request_finish(void *request, int ok);

/* (piece J) the smallest levels of a direct level's derived bitmaps -
those the texture streamer keeps resident, 64 px and under - held from its
make (h2_texture_source_tail_hold): the streamer's base reads served from
them, no whole made for a base. Held by the make before the level loads
(pending), the level's while it is loaded (current). */
struct held_tail
{
	unsigned long offset;           /* (its entry's pixels offset) */
	unsigned long bytes;
	unsigned char *pixels;
};

static struct
{
	struct held_tail *list;
	long count, capacity;
	unsigned long bytes;
} tails_pending, tails_current;

static void tails_free(struct held_tail *list, long count)
{
	long k;

	/* (the engine's free, debug_memory.c: not of NULL) */
	for (k = 0; k < count; k++)
		if (list[k].pixels)
			free(list[k].pixels);
	if (list)
		free(list);
}

void h2_texture_source_tails_drop_pending(void)
{
	pthread_mutex_lock(source_lock());
	tails_free(tails_pending.list, tails_pending.count);
	memset(&tails_pending, 0, sizeof(tails_pending));
	pthread_mutex_unlock(source_lock());
}

void h2_texture_source_tail_hold(unsigned long entry_offset, unsigned char *pixels, unsigned long bytes)
{
	pthread_mutex_lock(source_lock());
	if (tails_pending.count == tails_pending.capacity)
	{
		long capacity = tails_pending.capacity ? 2 * tails_pending.capacity : 256;
		/* (the C heap's: the list lasts the level, not its make) */
		struct held_tail *grown = (realloc)(tails_pending.list, (size_t)capacity * sizeof(*grown));

		if (!grown)
		{
			pthread_mutex_unlock(source_lock());
			free(pixels);
			return;
		}
		tails_pending.list = grown;
		tails_pending.capacity = capacity;
	}
	tails_pending.list[tails_pending.count].offset = entry_offset;
	tails_pending.list[tails_pending.count].bytes = bytes;
	tails_pending.list[tails_pending.count].pixels = pixels;
	tails_pending.count++;
	tails_pending.bytes += bytes;
	pthread_mutex_unlock(source_lock());
}

static int held_tail_compare(void const *a, void const *b)
{
	unsigned long x = ((struct held_tail const *)a)->offset, y = ((struct held_tail const *)b)->offset;

	return x < y ? -1 : x > y;
}

/* (the lock held) the tail held for e, if a read at chain lies in it */
static struct held_tail const *held_tail_for(struct dat_entry const *e, unsigned long chain)
{
	struct held_tail key, *found;

	if (!tails_current.count)
		return NULL;
	key.offset = e->offset;
	found = bsearch(&key, tails_current.list, (size_t)tails_current.count, sizeof(key), held_tail_compare);
	return found && found->bytes <= e->kept && chain >= e->kept - found->bytes ? found : NULL;
}

void h2_texture_source_unloaded(void)
{
	/* (the worker stopped, the reads it never made failed) */
	worker_stop();
	/* (the level's reads, at its end: the derived ones' turnover) */
	h2_texture_source_report();
	pthread_mutex_lock(source_lock());
	tails_free(tails_current.list, tails_current.count);
	memset(&tails_current, 0, sizeof(tails_current));
	if (h2_texture_source.file)
		fclose(h2_texture_source.file);
	h2_texture_source.file = NULL;
	/* (the engine's free, debug_memory.c: not of NULL) */
	if (h2_texture_source.entries)
		free(h2_texture_source.entries);
	h2_texture_source.entries = NULL;
	h2_texture_source.count = 0;
	{
		long k;

		for (k = 0; k < DERIVED_SLOTS; k++)
			if (h2_texture_source.slots[k].pixels)
				free(h2_texture_source.slots[k].pixels);
		memset(h2_texture_source.slots, 0, sizeof(h2_texture_source.slots));
	}
	if (h2_texture_source.params)
		free(h2_texture_source.params);
	h2_texture_source.params = NULL;
	h2_texture_source.params_bytes = 0;
	if (h2_texture_source.base)
		h2_cache_close(h2_texture_source.base);
	h2_texture_source.base = NULL;
	h2_texture_source.view_opens = h2_texture_source.view_peak = 0;
	h2_texture_source.retries = 0;
	h2_texture_source.worst_make_us = 0;
	h2_texture_source.kept_bytes = h2_texture_source.kept_peak = 0;
	h2_texture_source.derived_made = h2_texture_source.derived_hits = h2_texture_source.derived_failed = 0;
	h2_texture_source.derived_banded = h2_texture_source.derived_banded_failed = 0;
	h2_texture_source.parked_total = 0;
	h2_texture_source.tails_held_reads = 0;
	h2_texture_source.derived_us = 0;
	h2_texture_source.reads = h2_texture_source.read_bytes = h2_texture_source.chunks_inflated = h2_texture_source.failures = 0;
	h2_texture_source.read_us = 0;
	pthread_mutex_unlock(source_lock());
}

void h2_texture_source_map_loaded(char const *map_name)
{
	char level_path[512], file_name[96], dat_path[512];
	unsigned char header[0x800];
	unsigned char *table = NULL;
	FILE *level;
	boolean direct;
	unsigned long offset, count, bytes, i, pixel_bytes = 0, dat_count = 0, derived_count = 0, derived_bytes = 0;

	h2_texture_source_unloaded();
	/* (the make's held tails the level's now) */
	pthread_mutex_lock(source_lock());
	tails_current = tails_pending;
	memset(&tails_pending, 0, sizeof(tails_pending));
	if (tails_current.count)
		qsort(tails_current.list, (size_t)tails_current.count, sizeof(*tails_current.list), held_tail_compare);
	pthread_mutex_unlock(source_lock());
	if (!map_name)
		return;
	if (getenv("HALO_H2_DERIVED_MEMORY_LOG"))
		platform_memory_log("h2 textures: begun", map_name);
	/* (a direct level - no file, HALO_H2_DIRECT: its header and record in
	memory, h2_level.h) */
	direct = h2_level_direct_loaded() && h2_level_direct_header(map_name, header);
	level = NULL;
	if (!direct)
	{
		snprintf(file_name, sizeof(file_name), "%s.map", map_name);
		h2_level_folder_file(file_name, level_path, (long)sizeof(level_path));
		level = fopen(level_path, "rb");
		if (!level)
			return;
		if (fread(header, 1, sizeof(header), level) != sizeof(header))
		{
			fclose(level);
			return;
		}
	}
	if (read_u32(header + 0x700) != 0x68326C76UL /* 'h2lv' */ || read_u32(header + H2_TEXTURE_SOURCE_HEADER) != H2_TEXTURE_SOURCE_SIGNATURE)
	{
		if (level)
			fclose(level);
		return;
	}
	offset = read_u32(header + H2_TEXTURE_SOURCE_HEADER_OFFSET);
	count = read_u32(header + H2_TEXTURE_SOURCE_HEADER_COUNT);
	bytes = read_u32(header + H2_TEXTURE_SOURCE_HEADER_BYTES);
	if (!count || count > 65535 || bytes < count * H2_TEXTURE_SOURCE_ENTRY_SIZE || (table = malloc(bytes)) == NULL ||
		!(direct ? h2_level_direct_read(offset, bytes, table) : level_read_at(level, offset, table, bytes)))
	{
		platform_log("h2 textures: %s: its textures.dat record (%lu entries at %lu) cannot be read", map_name, count, offset);
		if (table)
			free(table);
		if (level)
			fclose(level);
		return;
	}
	if (level)
		fclose(level);
	pthread_mutex_lock(source_lock());
	h2_texture_source.entries = calloc(count, sizeof(*h2_texture_source.entries));
	if (h2_texture_source.entries)
	{
		for (i = 0; i < count; i++)
		{
			struct dat_entry *e = &h2_texture_source.entries[i];
			unsigned char const *p = table + i * H2_TEXTURE_SOURCE_ENTRY_SIZE;

			e->tag_index = (long)read_u32(p);
			e->kind = (long)read_u32(p + 4);
			e->offset = read_u32(p + 8);
			e->kept = read_u32(p + 12);
			e->sum = read_u32(p + 16);
			e->reference = read_u32(p + 20);
			e->bytes = read_u32(p + 24);
			e->skip = read_u32(p + 28);
			pixel_bytes += e->kept;
			if (e->kind == _h2_texture_source_dat)
				dat_count++;
			/* (its parameters: from the table's offset to the parameters') */
			else if (e->kind == _h2_texture_source_derived)
			{
				derived_count++;
				derived_bytes += e->kept;
				e->reference -= count * H2_TEXTURE_SOURCE_ENTRY_SIZE;
				if (e->reference + e->bytes > bytes - count * H2_TEXTURE_SOURCE_ENTRY_SIZE ||
					e->bytes <= H2_TEXTURE_SOURCE_DERIVED_PATH)
				{
					e->kind = NUMBER_OF_H2_TEXTURE_SOURCE_KINDS;   /* (not read: a damaged record) */
				}
			}
		}
		/* (the derived ones' parameters, after the entries) */
		h2_texture_source.params_bytes = bytes - count * H2_TEXTURE_SOURCE_ENTRY_SIZE;
		if (h2_texture_source.params_bytes && (h2_texture_source.params = malloc(h2_texture_source.params_bytes)) != NULL)
			memcpy(h2_texture_source.params, table + count * H2_TEXTURE_SOURCE_ENTRY_SIZE, h2_texture_source.params_bytes);
		else
			h2_texture_source.params_bytes = 0;
		{
			char const *setting = getenv("HALO_H2_DERIVED_KEPT_KB");
			long kb = setting ? atol(setting) : 4096;

			h2_texture_source.kept_limit = (unsigned long)(kb > 0 ? kb : 0) * 1024UL;
		}
		/* (by their pixels offsets: a read names its bitmap by offset alone -
		a tag of several bitmaps, an effect's frames, has one entry each) */
		qsort(h2_texture_source.entries, count, sizeof(*h2_texture_source.entries), entry_compare);
		h2_texture_source.count = (long)count;
	}
	free(table);
	snprintf(dat_path, sizeof(dat_path), "%stextures.dat", h2_resource_directory(H2_MAPS_FOLDER, "textures.dat"));
	h2_texture_source.file = dat_count ? fopen(dat_path, "rb") : NULL;
	{
		void h2_file_unbuffered(FILE *file);

		h2_file_unbuffered(h2_texture_source.file);
	}
	if ((dat_count && !h2_texture_source.file) || !h2_texture_source.count)
	{
		platform_log("h2 textures: %s: %lu bitmaps are textures.dat's, which %s", map_name, dat_count,
			h2_texture_source.count ? "cannot be opened: they will not be drawn" : "has no record of them here");
		pthread_mutex_unlock(source_lock());
		h2_texture_source_unloaded();
		return;
	}
	pthread_mutex_unlock(source_lock());
	/* (the derived ones: the load's open of the map held through the play -
	served the load's own, on the game thread now - and the worker begun) */
	if (derived_count)
	{
		struct dat_entry const *first = NULL;

		for (i = 0; i < count && !first; i++)
			if (h2_texture_source.entries[i].kind == _h2_texture_source_derived)
				first = &h2_texture_source.entries[i];
		if (first)
		{
			char name[H2_TEXTURE_SOURCE_DERIVED_PATH - H2_TEXTURE_SOURCE_DERIVED_MAP + 8], file[96], why[256];

			snprintf(name, sizeof(name), "%.*s", (int)(H2_TEXTURE_SOURCE_DERIVED_PATH - H2_TEXTURE_SOURCE_DERIVED_MAP),
				(char const *)h2_texture_source.params + first->reference + H2_TEXTURE_SOURCE_DERIVED_MAP);
			snprintf(file, sizeof(file), "%s.map", name);
			why[0] = 0;
			h2_texture_source.base = h2_cache_open(h2_resource_directory(H2_MAPS_FOLDER, file), name, why, (long)sizeof(why));
			if (!h2_texture_source.base)
				platform_log("h2 textures: %s.map cannot be read for its derived bitmaps: %s", name, why);
			else
			{
				worker_start();
				derived_characters_ahead();
			}
			if (getenv("HALO_H2_DERIVED_MEMORY_LOG"))
				platform_memory_log("h2 textures: the workers begun", map_name);
		}
	}
	platform_log("h2 textures: %s: %lu bitmaps (%lu KB of pixels) not in the level: %lu read from textures.dat as they are drawn, "
		"%lu (%lu KB) made again from the map on demand (%lu KB kept at most)", map_name, count, pixel_bytes / 1024, dat_count,
		derived_count, derived_bytes / 1024, h2_texture_source.kept_limit / 1024);
	/* (a test, HALO_H2_DAT_VERIFY=1: every entry's kept bytes read now and
	summed against the builder's sum of what it would have written) */
	if (getenv("HALO_H2_DAT_VERIFY") && atoi(getenv("HALO_H2_DAT_VERIFY")) != 0)
	{
		/* (=2: the derived ones made by the worker, over its view, as in the play) */
		verify_on_worker = atoi(getenv("HALO_H2_DAT_VERIFY")) == 2;
		unsigned long long started = vita_host_time_us();
		long mismatches = 0, unread = 0, k, tails_verified = 0;
		unsigned long total = 0;

		for (k = 0; k < h2_texture_source.count; k++)
		{
			struct dat_entry const *e = &h2_texture_source.entries[k];
			unsigned char *buffer = malloc(e->kept ? e->kept : 1);
			unsigned long j, sum = 2166136261UL;

			/* (a derived one whose smallest levels are made alone: those read
			first - before the whole is kept - and summed against the build's) */
			if (buffer && e->kind == _h2_texture_source_derived && derived_tail_bytes(e))
			{
				unsigned long tail = derived_tail_bytes(e);

				if (!h2_texture_source_read(e->tag_index, (long)(e->offset + e->kept - tail), (long)tail, buffer) ||
					pixels_sum(buffer, tail) != e->skip)
				{
					mismatches++;
					platform_log("h2 textures: tag %08lx: its smallest levels made alone not the build's", (unsigned long)e->tag_index);
				}
				tails_verified++;
			}

			if (!buffer || !h2_texture_source_read(e->tag_index, (long)e->offset, (long)e->kept, buffer))
			{
				unread++;
				if (buffer)
					free(buffer);
				continue;
			}
			for (j = 0; j < e->kept; j++)
				sum = (sum ^ buffer[j]) * 16777619UL;
			if (sum != e->sum)
			{
				mismatches++;
				if (mismatches <= 5)
					platform_log("h2 textures: tag %08lx: its %lu bytes read sum to %08lx, the builder's %08lx", (unsigned long)e->tag_index,
						e->kept, sum, e->sum);
			}
			total += e->kept;
			free(buffer);
		}
		platform_log("h2 textures: verified: %ld entries (%lu KB) read whole in %lu ms (and %ld bases made alone), %ld sums differ, "
			"%ld unread", h2_texture_source.count, total / 1024, (unsigned long)((vita_host_time_us() - started) / 1000), tails_verified,
			mismatches, unread);
		verify_on_worker = FALSE;
	}
}

static int entry_compare(void const *a, void const *b)
{
	unsigned long x = ((struct dat_entry const *)a)->offset, y = ((struct dat_entry const *)b)->offset;

	return x < y ? -1 : x > y;
}

static int dat_debug(void)
{
	static int on = -1;

	if (on < 0)
		on = getenv("HALO_H2_DAT_DEBUG") != NULL;
	return on;
}

int h2_texture_source_owns(long tag_index, long offset)
{
	(void)tag_index;
	return h2_texture_source.count && (unsigned long)offset >= H2_TEXTURE_SOURCE_OFFSET_BASE &&
		(unsigned long)offset - H2_TEXTURE_SOURCE_OFFSET_BASE < H2_TEXTURE_SOURCE_OFFSET_RANGE;
}

/* the entry whose pixels (to their 512-byte end) hold a pixels offset; NULL none */
static struct dat_entry const *entry_at(unsigned long offset)
{
	long low = 0, high = h2_texture_source.count - 1;
	struct dat_entry const *e;

	while (low < high)
	{
		long middle = (low + high + 1) / 2;

		if (h2_texture_source.entries[middle].offset <= offset)
			low = middle;
		else
			high = middle - 1;
	}
	if (high < 0)
		return NULL;
	e = &h2_texture_source.entries[low];
	return offset >= e->offset && offset - e->offset < ((e->kept + 511) & ~511UL) ? e : NULL;
}

/* the entry's chunks from first to last (inclusive) inflated, the bytes
from `from` (an offset in the inflated entry) for `size` into out */
enum
{
	ENTRY_WHOLE = 32 * 1024,   /* (an entry this small read whole with its head) */
	HEAD_GUESS = 64            /* (else the head's read takes this many chunks' sizes) */
};

static boolean entry_read_as(struct dat_entry const *e, unsigned long from, unsigned long size, unsigned char *out,
	boolean through_scratch)
{
	/* (not under the source's lock - its file's reads under a lock of
	their own: a long inflate held the lock against the reads parked for
	the workers, the render worker's among them; the chunk table not on
	the stack, the cache file thread's is 16 KB) */
	unsigned char *head;
	unsigned long chunks, first, last, c, at, done = 0, held;
	unsigned char *scratch, *compressed = NULL;
	unsigned long compressed_capacity = 0;
	boolean ok = TRUE, read;

	/* (the head - the chunk count, each chunk's size - in one read with
	what follows it: a small entry's whole, else room for HEAD_GUESS chunks'
	sizes; a second read only for a longer head. The count's 4 bytes, the
	head, then the chunk were three reads an entry: each a card access on the
	Vita, the bases' step's thousands) */
	held = e->bytes <= ENTRY_WHOLE ? e->bytes : 4 + 4 * HEAD_GUESS;
	if (held > e->bytes)
		held = e->bytes;
	head = held >= 4 ? malloc(held) : NULL;
	pthread_mutex_lock(file_lock());
	read = head && file_read_at(h2_texture_source.file, e->reference, head, held);
	chunks = read ? read_u32(head) : 0;
	read = read && chunks && chunks <= MAXIMUM_CHUNKS && 4 + 4 * chunks <= e->bytes;
	if (read && 4 + 4 * chunks > held)
	{
		unsigned char *whole = realloc(head, 4 + 4 * chunks);

		read = whole && file_read_at(h2_texture_source.file, e->reference + held, whole + held, 4 + 4 * chunks - held);
		head = whole ? whole : head;
		held = 4 + 4 * chunks;
	}
	pthread_mutex_unlock(file_lock());
	if (!read)
	{
		if (head)
			free(head);
		return FALSE;
	}
	first = from / H2_TEXTURE_SOURCE_CHUNK_BYTES;
	last = (from + size - 1) / H2_TEXTURE_SOURCE_CHUNK_BYTES;
	if (first >= chunks)
	{
		free(head);
		return FALSE;
	}
	if (last >= chunks)
		last = chunks - 1;
	/* (a chunk the read takes from its start - every one but a read's
	first, when the read begins inside it - is inflated straight into out,
	only as far as the read wants: the 256 KB scratch only for a first chunk
	read from inside. The C heap has a few MB free in a Quality game on the
	Vita, and a refused scratch failed the read - a Halo 2 texture's detail
	not drawn) */
	scratch = NULL;
	at = e->reference + 4 + 4 * chunks;
	for (c = 0; c < first; c++)
	{
		long csize = (long)read_u32(head + 4 + 4 * c);

		at += (unsigned long)(csize < 0 ? -csize : csize);
	}
	for (c = first; c <= last && ok; c++)
	{
		long csize = (long)read_u32(head + 4 + 4 * c);
		unsigned long csize_abs = (unsigned long)(csize < 0 ? -csize : csize);
		unsigned long chunk_start = c * H2_TEXTURE_SOURCE_CHUNK_BYTES, got = 0;
		unsigned long take_from, take;

		if (csize_abs > compressed_capacity)
		{
			unsigned char *grown = compressed ? realloc(compressed, csize_abs) : malloc(csize_abs);

			if (!grown)
			{
				ok = FALSE;
				break;
			}
			compressed = grown;
			compressed_capacity = csize_abs;
		}
		/* (in the head's read - a small entry's: copied) */
		if (at - e->reference + csize_abs <= held)
		{
			memcpy(compressed, head + (at - e->reference), csize_abs);
			read = TRUE;
		}
		else
		{
			pthread_mutex_lock(file_lock());
			read = file_read_at(h2_texture_source.file, at, compressed, csize_abs);
			pthread_mutex_unlock(file_lock());
		}
		if (!read)
		{
			ok = FALSE;
			break;
		}
		at += csize_abs;
		/* the part of this chunk the read wants: from take_from on */
		take_from = from + done > chunk_start ? from + done - chunk_start : 0;
		{
			/* (straight into out: the chunk's first size - done bytes at most) */
			boolean direct = take_from == 0 && !through_scratch;
			unsigned char *into;
			unsigned long room = H2_TEXTURE_SOURCE_CHUNK_BYTES;

			if (direct)
			{
				into = out + done;
				if (size - done < room)
					room = size - done;
			}
			else
			{
				if (!scratch && (scratch = malloc(H2_TEXTURE_SOURCE_CHUNK_BYTES)) == NULL)
				{
					ok = FALSE;
					break;
				}
				into = scratch;
			}
			if (csize < 0)
			{
				got = csize_abs < room ? csize_abs : room;
				memcpy(into, compressed, got);
			}
			else
			{
				z_stream stream;
				int status;

				memset(&stream, 0, sizeof(stream));
				if (inflateInit(&stream) != Z_OK)
				{
					ok = FALSE;
					break;
				}
				stream.next_in = compressed;
				stream.avail_in = (uInt)csize_abs;
				stream.next_out = into;
				stream.avail_out = (uInt)room;
				status = inflate(&stream, Z_FINISH);
				got = stream.total_out;
				inflateEnd(&stream);
				/* (Z_BUF_ERROR: out full - the read had what it wanted - or
				the chunk short, as before) */
				if (status != Z_STREAM_END && status != Z_BUF_ERROR)
				{
					ok = FALSE;
					break;
				}
			}
			h2_texture_source.chunks_inflated++;
			if (direct)
			{
				if (!got)
					break;
				done += got;
			}
			else
			{
				if (take_from >= got)
					break;
				take = got - take_from < size - done ? got - take_from : size - done;
				memcpy(out + done, scratch + take_from, take);
				done += take;
			}
		}
		if (done >= size)
			break;
	}
	if (compressed)
		free(compressed);
	if (scratch)
		free(scratch);
	free(head);
	/* (past the entry's end - the read's sector rounding: zero) */
	if (ok && done < size)
		memset(out + done, 0, size - done);
	return ok;
}

/* (debug) HALO_H2_DAT_VERIFY=1: each read made again through the scratch,
as before the reads went straight into out, and compared ("h2 textures:
read check" every 256 reads) */
static unsigned long dat_verify_reads, dat_verify_mismatches;

static boolean entry_read(struct dat_entry const *e, unsigned long from, unsigned long size, unsigned char *out)
{
	static int verify = -1;
	boolean ok = entry_read_as(e, from, size, out, FALSE);

	if (verify < 0)
		verify = getenv("HALO_H2_DAT_VERIFY") && atoi(getenv("HALO_H2_DAT_VERIFY")) > 0;
	if (verify && ok)
	{
		unsigned char *again = malloc(size);

		if (again)
		{
			if (!entry_read_as(e, from, size, again, TRUE) || memcmp(again, out, size))
				__atomic_add_fetch(&dat_verify_mismatches, 1, __ATOMIC_RELAXED);
			free(again);
			if ((__atomic_add_fetch(&dat_verify_reads, 1, __ATOMIC_RELAXED) & 255) == 0)
				platform_log("h2 textures: read check: %lu reads made again through the scratch, %lu not alike",
					dat_verify_reads, dat_verify_mismatches);
		}
	}
	return ok;
}

static unsigned long pixels_sum(unsigned char const *p, unsigned long size)
{
	unsigned long sum = 2166136261UL, k;

	for (k = 0; k < size; k++)
		sum = (sum ^ p[k]) * 16777619UL;
	return sum;
}

/* ---------- the derived bitmaps: made on demand

A derived bitmap is made again from the map by the builder's own code
(h2_level_derived_pixels) and summed against the builder's sum: its very
bytes or none. The made ones are kept in a small LRU (kept_limit). The
map: the load's own open, held through the play (h2_cache.c keeps it): on
the game thread (the load's reads, HALO_H2_DAT_VERIFY) it is used as it
is; any other read is made by a worker thread of this source's (a big
stack: the cache file thread's is 16 KB) over a view of it
(h2_cache_open_view: the open's names and tables shared, read only; its
own files and segments, let go when idle) - a view costs the segments it
inflates, not an open's MBs, and nothing is made on the game thread
during the play (Vita3K Old Mombasa: 8-14 ms a bitmap there). A read from
the cache file thread is parked for the worker, which finishes the
request; the texture cache draws the stream stand-in meanwhile, as for a
streamed level not in yet. A blocking read from another thread waits. */

static char const *derived_path(struct dat_entry const *e)
{
	return (char const *)h2_texture_source.params + e->reference + H2_TEXTURE_SOURCE_DERIVED_PATH;
}

/* made over cache (no lock: the entry and its parameters stay while the
level is loaded, the worker stopped before they go); NULL not made */
/* (a plain one) the bytes of its smallest levels made alone, 0 none */
static unsigned long derived_tail_bytes(struct dat_entry const *e)
{
	return read_u32(h2_texture_source.params + e->reference) >> 16;
}

/* its smallest levels alone (derived_tail_bytes of them, summed against the
build's sum of them in the entry's last word); NULL not made */
static unsigned char *derived_make_tail(struct dat_entry const *e, struct h2_cache *cache)
{
	unsigned char *pixels = NULL;
	unsigned long size = 0, tail = derived_tail_bytes(e);

	if (!cache || !tail)
		return NULL;
	if (!h2_level_derived_pixels(cache, h2_texture_source.params + e->reference, e->bytes, 1, &pixels, &size) || size != tail ||
		pixels_sum(pixels, size) != e->skip)
	{
		platform_log("h2 textures: derived %s's smallest levels not made (%lu bytes of %lu)", derived_path(e), size, tail);
		if (pixels)
			free(pixels);
		return NULL;
	}
	return pixels;
}

static unsigned char *derived_make(struct dat_entry const *e, struct h2_cache *cache, unsigned long long *us)
{
	unsigned char *pixels = NULL;
	unsigned long size = 0;
	unsigned long long started = vita_host_time_us();

	if (!cache)
		return NULL;
	h2_derive_heap_make_begin();
	/* (a record with no sum - a direct level made without its checks,
	h2_level.c's direct_unverified - is taken by its size alone) */
	if (!h2_level_derived_pixels(cache, h2_texture_source.params + e->reference, e->bytes, 0, &pixels, &size) ||
		size != e->kept || (e->sum && pixels_sum(pixels, size) != e->sum))
	{
		if (h2_texture_source.derived_failed < 20)
			platform_log("h2 textures: derived %s not made (%lu bytes of %lu%s; the map %s)", derived_path(e), size, e->kept,
				pixels && size == e->kept ? ", their sum not the builder's" : "",
				h2_cache_memory_failed(cache) ? "short of memory" : "read");
		if (pixels)
			free(pixels);
		pixels = NULL;
	}
	*us = vita_host_time_us() - started;
	if (pixels && getenv("HALO_H2_DERIVED_DEBUG"))
		platform_log("h2 textures: derived %s made on thread %d (the game's %d) in %lu us", derived_path(e), halo_thread_index(),
			halo_game_thread_index(), (unsigned long)*us);
	/* (HALO_H2_DERIVED_MEMORY_LOG=2: each make's memory - its largest
	allocation, the arena's most in use while it ran) */
	if (getenv("HALO_H2_DERIVED_MEMORY_LOG") && atoi(getenv("HALO_H2_DERIVED_MEMORY_LOG")) >= 2)
	{
		unsigned long largest = 0, peak = 0;

		h2_derive_heap_make_figures(&largest, &peak);
		platform_log("h2 textures: derived %s %s: %lu KB, its largest allocation %lu KB, the arena %lu KB in use at most, %lu us",
			derived_path(e), pixels ? "made" : "NOT made", e->kept / 1024, largest / 1024, peak / 1024, (unsigned long)*us);
	}
	return pixels;
}

/* (the lock held) a kept one's pixels, NULL none */
static unsigned char const *derived_kept_pixels(struct dat_entry const *e)
{
	long k;

	for (k = 0; k < DERIVED_SLOTS; k++)
	{
		if (h2_texture_source.slots[k].entry == e)
		{
			h2_texture_source.slots[k].used = h2_texture_source.reads;
			h2_texture_source.derived_hits++;
			return h2_texture_source.slots[k].pixels;
		}
	}
	return NULL;
}

static boolean derived_kept_pixels_quiet(struct dat_entry const *e)
{
	long k;

	for (k = 0; k < DERIVED_SLOTS; k++)
		if (h2_texture_source.slots[k].entry == e)
			return TRUE;
	return FALSE;
}

/* (the lock held) one made: counted, kept if there is room (the least
recently read let go); FALSE not kept - the caller frees it */
static boolean derived_keep(struct dat_entry const *e, unsigned char *pixels, unsigned long long us)
{
	struct derived_slot *slot = NULL;
	long k;

	h2_texture_source.derived_made++;
	h2_texture_source.derived_us += us;
	if (us > h2_texture_source.worst_make_us)
		h2_texture_source.worst_make_us = us;
	for (k = 0; k < DERIVED_SLOTS; k++)
		if (h2_texture_source.slots[k].entry == e)
			return FALSE;
	for (;;)
	{
		struct derived_slot *oldest = NULL, *empty = NULL;

		for (k = 0; k < DERIVED_SLOTS; k++)
		{
			struct derived_slot *candidate = &h2_texture_source.slots[k];

			if (!candidate->entry)
			{
				if (!empty)
					empty = candidate;
			}
			else if (!oldest || candidate->used < oldest->used)
				oldest = candidate;
		}
		if (empty && h2_texture_source.kept_bytes + e->kept <= h2_texture_source.kept_limit)
		{
			slot = empty;
			break;
		}
		if (!oldest)
			break;
		h2_texture_source.kept_bytes -= oldest->entry->kept;
		halo_stall_note("derived: the LRU let one go (bytes, kept bytes)", oldest->entry->kept, h2_texture_source.kept_bytes);
		free(oldest->pixels);
		memset(oldest, 0, sizeof(*oldest));
	}
	if (!slot)
		return FALSE;
	slot->entry = e;
	slot->pixels = pixels;
	slot->used = h2_texture_source.reads;
	h2_texture_source.kept_bytes += e->kept;
	if (h2_texture_source.kept_bytes > h2_texture_source.kept_peak)
		h2_texture_source.kept_peak = h2_texture_source.kept_bytes;
	return TRUE;
}

static void derived_copy(struct dat_entry const *e, unsigned char const *pixels, unsigned long chain, unsigned long size,
	void *buffer)
{
	unsigned long n = size < e->kept - chain ? size : e->kept - chain;

	memcpy(buffer, pixels + chain, n);
	if (n < size)
		memset((unsigned char *)buffer + n, 0, size - n);
}

/* (the lock held) a job for the worker; FALSE no room */
static boolean derived_park(long tag_index, long offset, long size, void *buffer, void *request, volatile int *done)
{
	struct parked_read *p;

	if (!h2_texture_source.worker_count || h2_texture_source.stopping || h2_texture_source.parked_count >= PARKED_MAXIMUM)
		return FALSE;
	p = &h2_texture_source.parked[h2_texture_source.parked_count++];
	p->tag_index = tag_index;
	p->offset = offset;
	p->size = size;
	p->buffer = buffer;
	p->request = request;
	p->done = done;
	p->tries = 0;
	p->not_before_us = 0;
	p->ahead = FALSE;
	p->band = FALSE;
	h2_texture_source.parked_total++;
	pthread_cond_signal(&source_wake);
	return TRUE;
}

/* (the lock held) size bytes of a derived entry's pixels from chain on:
kept; else made here on the game thread over the load's open; else made
by the worker, waited for */
static boolean derived_read(struct dat_entry const *e, long tag_index, long offset, unsigned long chain, unsigned long size,
	void *buffer)
{
	unsigned char const *kept = derived_kept_pixels(e);
	unsigned char *made;
	unsigned long long us = 0;
	struct held_tail const *tail = held_tail_for(e, chain);

	if (kept)
	{
		derived_copy(e, kept, chain, size, buffer);
		return TRUE;
	}
	/* (its smallest levels held from the make: the streamer's base) */
	if (tail)
	{
		unsigned long from = chain - (e->kept - tail->bytes), n = size < tail->bytes - from ? size : tail->bytes - from;

		memcpy(buffer, tail->pixels + from, n);
		if (n < size)
			memset((unsigned char *)buffer + n, 0, size - n);
		h2_texture_source.tails_held_reads++;
		return TRUE;
	}
	/* (made by the worker, over its view, waited for - on the game thread
too: the load's open is the game thread's own, and a make over it left its
"memory failed" flag for the world's read, which then refused the BSP on
Vita3K; the worker's view takes the C heap, never the memory window) */
	if (!h2_texture_source.worker_count)
	{
		made = derived_make(e, h2_texture_source.base, &us);
		if (!made)
		{
			h2_texture_source.derived_failed++;
			memset(buffer, 0, size);
			return FALSE;
		}
		derived_copy(e, made, chain, size, buffer);
		if (!derived_keep(e, made, us))
			free(made);
		return TRUE;
	}
	{
		volatile int done = 0;

		if (!derived_park(tag_index, offset, (long)size, buffer, NULL, &done))
		{
			memset(buffer, 0, size);
			return FALSE;
		}
		derived_prefetch(e);
		while (!done)
			pthread_cond_wait(&source_done, source_lock());
		return done > 0;
	}
}

/* (the lock held) whether an entry is waiting or being made */
static boolean derived_pending(struct dat_entry const *e)
{
	long k;

	for (k = 0; k < WORKERS_MAXIMUM; k++)
		if (h2_texture_source.making[k] == e)
			return TRUE;
	for (k = 0; k < h2_texture_source.parked_count; k++)
		if (entry_at((unsigned long)h2_texture_source.parked[k].offset) == e)
			return TRUE;
	return FALSE;
}

/* (the lock held) the derived entries after e in their order - the order
the level's tags, so the stream's base reads at load, take them - made
ahead, one a worker: the workers busy while the game thread waits on one */
/* (made ahead at load) whether the texture streamer reads this one's base
at the load (h2_texture_stream.c source_make): a plain one with its mips (its
recipe's description), or one small enough to be resident whole */
static boolean derived_streamed(struct dat_entry const *e)
{
	unsigned char const *p = h2_texture_source.params + e->reference;
	unsigned long kind = (read_u32(p) >> 8) & 0xFF;

	if (derived_tail_bytes(e))
		return FALSE;   /* (its base made alone, quickly, when read) */
	if (e->kept <= 16 * 1024)
		return TRUE;
	if (kind != 1 /* (_derived_recipe_plain, h2_level.c) */)
		return FALSE;
	/* (its mips: the low half of its third argument, at ARGS + 4; its type
	the high half of its fourth, at ARGS + 8) */
	return (read_u32(p + H2_TEXTURE_SOURCE_DERIVED_ARGS + 4) & 0xFFFF) != 0 &&
		(read_u32(p + H2_TEXTURE_SOURCE_DERIVED_ARGS + 8) >> 16) == 0;
}

static void derived_prefetch(struct dat_entry const *e)
{
	long i = e ? (long)(e - h2_texture_source.entries) + 1 : 0, queued = 0;

	static long ahead = -1;

	if (ahead < 0)
		ahead = getenv("HALO_H2_DERIVED_AHEAD") ? atol(getenv("HALO_H2_DERIVED_AHEAD")) : 2;
	for (; i < h2_texture_source.count && queued < ahead * h2_texture_source.worker_count && h2_texture_source.parked_count < PARKED_MAXIMUM / 2; i++)
	{
		struct dat_entry const *next = &h2_texture_source.entries[i];

		if (next->kind != _h2_texture_source_derived || !derived_streamed(next))
			continue;
		if (derived_kept_pixels_quiet(next) || derived_pending(next))
		{
			queued++;
			continue;
		}
		if (derived_park(next->tag_index, (long)next->offset, 0, NULL, NULL, NULL))
		{
			h2_texture_source.prefetched++;
			queued++;
		}
	}
}

/* the characters' multipurpose maps (their change colours' masks: the
Spartans' and Elites' armour, the first-person arms) made ahead by the
workers from the level's load on, kept: every multiplayer view draws them
first. Made when first drawn, the insider.5 tester's Vita made them after
the loading screen went - "derived: 72 made in 31016 ms (the slowest
4281 ms)" - the armour drawn from the white stand-in meanwhile; the workers
sat idle through the load's last steps. A dozen of 32 KB on Zanzibar.
HALO_H2_DERIVED_CHARACTERS_AHEAD=0: made when drawn, as before */
static void derived_characters_ahead(void)
{
	char const *setting = getenv("HALO_H2_DERIVED_CHARACTERS_AHEAD");
	long i, queued = 0;

	if (setting && !strcmp(setting, "0"))
		return;
	pthread_mutex_lock(source_lock());
	for (i = 0; i < h2_texture_source.count && h2_texture_source.parked_count < PARKED_MAXIMUM / 2; i++)
	{
		struct dat_entry const *e = &h2_texture_source.entries[i];
		char const *path;

		if (e->kind != _h2_texture_source_derived)
			continue;
		path = derived_path(e);
		if (!strstr(path, "\\characters\\") || !strstr(path, " multipurpose"))
			continue;
		if (derived_kept_pixels_quiet(e) || derived_pending(e))
			continue;
		if (derived_park(e->tag_index, (long)e->offset, 0, NULL, NULL, NULL))
		{
			h2_texture_source.parked[h2_texture_source.parked_count - 1].ahead = TRUE;
			h2_texture_source.prefetched++;
			queued++;
		}
	}
	pthread_mutex_unlock(source_lock());
	if (queued)
		platform_log("h2 textures: %ld characters' multipurpose maps made ahead from the load on", queued);
}

/* (the lock not held) bytes chain .. chain + size of a derived entry made by bands into buffer: 1 made, 0 not a bitmap that
can be, -1 not made (no room, the entry short) */
static int derived_band_into(struct dat_entry const *e, struct h2_cache *cache, unsigned long chain, unsigned long size, void *buffer)
{
	unsigned long total = 0;
	int r;

	if (!cache || !buffer || e->kept < DERIVED_BAND_MINIMUM ||
		!h2_level_derived_banded(cache, h2_texture_source.params + e->reference, e->bytes, &total) || total != e->kept)
		return 0;
	r = h2_level_derived_range(cache, h2_texture_source.params + e->reference, e->bytes, chain, size, buffer);
	/* (HALO_H2_BAND_CHECK=1: what was made, against the whole make's) */
	if (r > 0 && getenv("HALO_H2_BAND_CHECK") && atoi(getenv("HALO_H2_BAND_CHECK")) > 0)
	{
		unsigned char *pixels = NULL;
		unsigned long made = 0;
		boolean same = FALSE;

		if (h2_level_derived_pixels(cache, h2_texture_source.params + e->reference, e->bytes, 0, &pixels, &made) && made == e->kept)
		{
			unsigned long n = size < made - chain ? size : made - chain;

			same = !memcmp(pixels + chain, buffer, n);
		}
		platform_log("h2 textures: band check %s: %lu bytes at %lu of %lu: %s", derived_path(e), size, chain, e->kept,
			same ? "the same as the whole make" : (pixels ? "NOT THE SAME" : "the whole make not made"));
		/* (=2: and ranges of other shapes - unaligned, a chunk's, across tiles', past the end - against it) */
		if (pixels && made == e->kept && atoi(getenv("HALO_H2_BAND_CHECK")) >= 2)
		{
			static unsigned long const at[] = { 0, 1, 3, 4093, 16384, 16385, 262144, 524288 - 7, 4000000, 8388608 + 5, 16777216 - 100, 16777216 - 4 };
			static unsigned long const length[] = { 1, 2, 4, 5, 16384, 20000, 262144, 600000, 1048576, 3000000 };
			unsigned long i, j, bad = 0, tried = 0;
			unsigned char *again;

			for (i = 0; i < sizeof(at) / sizeof(*at); i++)
				for (j = 0; j < sizeof(length) / sizeof(*length); j++)
				{
					unsigned long n = length[j], want = n < made - at[i] ? n : made - at[i];

					again = malloc(n);
					if (!again)
						continue;
					memset(again, 0x5A, n);
					if (h2_level_derived_range(cache, h2_texture_source.params + e->reference, e->bytes, at[i], n, again) <= 0 ||
						memcmp(again, pixels + at[i], want) || (want < n && again[want] != 0))
						bad++;
					tried++;
					free(again);
				}
			platform_log("h2 textures: band check %s: %lu ranges of other shapes, %lu not the same", derived_path(e), tried, bad);
		}
		if (pixels)
			free(pixels);
	}
	return r;
}

static void *worker_main(void *argument)
{
	static long idle_ms = -1, view_limit_kb = -1;
	void h2_level_window_refuse_this_thread(void);
	long index = (long)(size_t)argument;
	struct h2_cache *view = NULL;
	unsigned long long view_used_us = 0;

	if (idle_ms < 0)
		idle_ms = getenv("HALO_H2_DERIVED_IDLE_MS") ? atol(getenv("HALO_H2_DERIVED_IDLE_MS")) : 0;
	if (view_limit_kb < 0)
		view_limit_kb = getenv("HALO_H2_DERIVED_VIEW_KB") ? atol(getenv("HALO_H2_DERIVED_VIEW_KB")) : 4096;
	/* (its memory the derive arena's - user RAM -, never the memory window
	or the C heap) */
	h2_level_window_refuse_this_thread();
	h2_derive_heap_thread();
	/* (its view made now, while the load's open is there - its tables
	copied into the arena: the load's open is let go once every worker has
	one, worker_start) */
	view = h2_texture_source.base ? h2_cache_open_view(h2_texture_source.base, 0) : NULL;
	pthread_mutex_lock(source_lock());
	h2_texture_source.workers_ready++;
	pthread_cond_broadcast(&source_done);
	for (;;)
	{
		struct parked_read p;
		struct dat_entry const *e = NULL;
		unsigned char const *pixels;
		unsigned char *made = NULL;
		unsigned long long us = 0;
		boolean ok = FALSE, kept = FALSE;
		long ready;

		/* (nothing waiting: the view let go - HALO_H2_DERIVED_IDLE_MS, 0 by
		default) */
		if (!h2_texture_source.parked_count && view && idle_ms == 0 && h2_cache_segment_bytes(view))
		{
			pthread_mutex_unlock(source_lock());
			h2_cache_trim(view);
			pthread_mutex_lock(source_lock());
		}
		/* (the first job ready - not waiting to be tried again, its entry
		not another worker's now) */
		for (;;)
		{
			unsigned long long now = vita_host_time_us();

			if (h2_texture_source.stopping)
				break;
			for (ready = 0; ready < h2_texture_source.parked_count; ready++)
			{
				struct dat_entry const *candidate = entry_at((unsigned long)h2_texture_source.parked[ready].offset);
				long k;

				if (h2_texture_source.parked[ready].not_before_us > now)
					continue;
				for (k = 0; k < WORKERS_MAXIMUM && h2_texture_source.making[k] != candidate; k++)
					;
				if (k == WORKERS_MAXIMUM || !candidate)
					break;
			}
			if (ready < h2_texture_source.parked_count)
				break;
			{
				struct timespec until;

				until.tv_sec = time(NULL) + 1;
				until.tv_nsec = 0;
				pthread_cond_timedwait(&source_wake, source_lock(), &until);
			}
			if (view && !h2_texture_source.parked_count && h2_cache_segment_bytes(view) &&
				vita_host_time_us() - view_used_us >= (unsigned long long)idle_ms * 1000ULL)
			{
				pthread_mutex_unlock(source_lock());
				h2_cache_trim(view);
				pthread_mutex_lock(source_lock());
			}
		}
		if (h2_texture_source.stopping)
			break;
		p = h2_texture_source.parked[ready];
		memmove(h2_texture_source.parked + ready, h2_texture_source.parked + ready + 1,
			(size_t)(--h2_texture_source.parked_count - ready) * sizeof(*h2_texture_source.parked));
		e = entry_at((unsigned long)p.offset);
		if (e && e->kind == _h2_texture_source_derived)
		{
			pixels = derived_kept_pixels(e);
			/* (a read of its smallest levels alone - the streamer's base:
			those made alone, not the whole) */
			if (!pixels && p.buffer && derived_tail_bytes(e) && (unsigned long)p.offset - e->offset >= e->kept - derived_tail_bytes(e))
			{
				unsigned long tail = derived_tail_bytes(e), from = (unsigned long)p.offset - e->offset - (e->kept - tail);
				unsigned char *small;

				pthread_mutex_unlock(source_lock());
				small = derived_make_tail(e, view);
				pthread_mutex_lock(source_lock());
				if (small)
				{
					unsigned long n = (unsigned long)p.size < tail - from ? (unsigned long)p.size : tail - from;

					memcpy(p.buffer, small + from, n);
					if (n < (unsigned long)p.size)
						memset((unsigned char *)p.buffer + n, 0, (unsigned long)p.size - n);
					free(small);
					h2_texture_source.tails_made++;
					memory_watch_prepare_write(p.buffer, (unsigned long)p.size);
					if (p.request)
						cache_file_request_finish(p.request, TRUE);
					if (p.done)
						*p.done = 1;
					pthread_cond_broadcast(&source_done);
					continue;
				}
			}
			/* (a big plain bitmap: made by bands into the reader's buffer, no block of its size; one with a sum - checked
			against the builder's - only after the whole make failed) */
			if (!pixels && p.buffer && p.size > 0 && (!e->sum || p.band) && e->kept >= DERIVED_BAND_MINIMUM)
			{
				int band;

				h2_texture_source.making[index] = e;
				pthread_mutex_unlock(source_lock());
				if (!view && h2_texture_source.base)
					view = h2_cache_open_view(h2_texture_source.base, 0);
				band = derived_band_into(e, view, (unsigned long)p.offset - e->offset, (unsigned long)p.size, p.buffer);
				pthread_mutex_lock(source_lock());
				h2_texture_source.making[index] = NULL;
				if (band > 0)
				{
					ok = TRUE;
					h2_texture_source.derived_banded++;
				}
				else if (band < 0)
					h2_texture_source.derived_banded_failed++;
			}
			if (!pixels && !ok)
			{
				h2_texture_source.making[index] = e;
				pthread_mutex_unlock(source_lock());
				if (!view && h2_texture_source.base)
					view = h2_cache_open_view(h2_texture_source.base, 0);
				made = derived_make(e, view, &us);
				halo_stall_note(made ? "derived: made (us, bytes)" : "derived: NOT made (us, bytes)", (unsigned long)us, e->kept);
				view_used_us = vita_host_time_us();
				{
					unsigned long bytes = h2_cache_segment_bytes(view);

					pthread_mutex_lock(source_lock());
					h2_texture_source.view_opens += 0;
					if (bytes > h2_texture_source.view_peak)
						h2_texture_source.view_peak = bytes;
					pthread_mutex_unlock(source_lock());
					/* (its segments past a limit: let go now, made again at the next) */
					if (bytes > (unsigned long)view_limit_kb * 1024UL || !made)
						h2_cache_trim(view);
				}
				pthread_mutex_lock(source_lock());
				h2_texture_source.making[index] = NULL;
				if (made)
				{
					kept = derived_keep(e, made, us);
					/* (one made ahead: not the LRU's first to go while the
					load's and the first frames' reads go by - it is made
					for them; read, it is the LRU's as any other) */
					if (kept && p.ahead)
					{
						long s;

						for (s = 0; s < DERIVED_SLOTS; s++)
							if (h2_texture_source.slots[s].entry == e)
								h2_texture_source.slots[s].used = h2_texture_source.reads + DERIVED_AHEAD_READS;
					}
					pixels = made;
				}
				else
				{
					/* (not made: the arena's room too small - a sizing bug,
					h2_build_heap.c says so - or a read that failed: tried
					again later, the read not ready meanwhile; a blocking one
					fails after a few tries) */
					h2_texture_source.derived_failed++;
					p.tries++;
					p.band = TRUE;
					if ((p.request && p.tries < DERIVED_TRIES_REQUEST) || (p.done && p.tries < 5) || (!p.request && !p.done && p.tries < 3))
					{
						p.not_before_us = vita_host_time_us() + (p.done ? 100000ULL : 500000ULL);
						h2_texture_source.parked[h2_texture_source.parked_count++] = p;
						h2_texture_source.retries++;
						pthread_cond_broadcast(&source_done);
						continue;
					}
					platform_log("h2 textures: derived %s given up after %ld tries (the read fails: the texture is drawn without it)",
						derived_path(e), p.tries);
				}
			}
			if (pixels && p.buffer && (unsigned long)p.offset - e->offset < e->kept)
			{
				derived_copy(e, pixels, (unsigned long)p.offset - e->offset, (unsigned long)p.size, p.buffer);
				ok = TRUE;
			}
			if (made && !kept)
				free(made);
		}
		/* (written now: marked again, after the decoder may have looked) */
		if (p.buffer && p.size > 0)
			memory_watch_prepare_write(p.buffer, (unsigned long)p.size);
		if (p.request)
			cache_file_request_finish(p.request, ok);
		if (p.done)
			*p.done = ok ? 1 : -1;
		halo_stall_note("derived: a parked read finished (ok, bytes)", (unsigned long)ok, (unsigned long)p.size);
		pthread_cond_broadcast(&source_done);
	}
	pthread_mutex_unlock(source_lock());
	if (view)
		h2_cache_close(view);
	return NULL;
}

/* (the game thread, the level's load: the map held, the derive arena
reserved, the workers begun - HALO_H2_DERIVED_WORKERS, 2) */
static void worker_start(void)
{
	pthread_attr_t attributes;
	long count = getenv("HALO_H2_DERIVED_WORKERS") ? atol(getenv("HALO_H2_DERIVED_WORKERS")) : 2, k;

	if (h2_texture_source.worker_count)
		return;
	if (count < 1)
		count = 1;
	if (count > WORKERS_MAXIMUM)
		count = WORKERS_MAXIMUM;
	h2_derive_heap_begin(getenv("HALO_H2_DERIVE_HEAP_MB") ? atol(getenv("HALO_H2_DERIVE_HEAP_MB")) : 48,
		getenv("HALO_H2_DERIVE_HEAP_RESERVE_MB") ? atol(getenv("HALO_H2_DERIVE_HEAP_RESERVE_MB")) : 12 * count, 8);
	h2_texture_source.stopping = FALSE;
	pthread_attr_init(&attributes);
	pthread_attr_setstacksize(&attributes, 512 * 1024);
	for (k = 0; k < count; k++)
	{
		if (pthread_create(&h2_texture_source.workers[h2_texture_source.worker_count], &attributes, worker_main, (void *)(size_t)k) == 0)
			h2_texture_source.worker_count++;
	}
	pthread_attr_destroy(&attributes);
	if (!h2_texture_source.worker_count)
		platform_log("h2 textures: the derived bitmaps' workers not begun: made where asked");
	/* (each worker's view made: the load's open let go - not held through
	the play, where it kept its segments in the C heap) */
	else
	{
		pthread_mutex_lock(source_lock());
		while (h2_texture_source.workers_ready < h2_texture_source.worker_count)
			pthread_cond_wait(&source_done, source_lock());
		pthread_mutex_unlock(source_lock());
		if (h2_texture_source.base)
			h2_cache_close(h2_texture_source.base);
		h2_texture_source.base = NULL;
		/* (the first ones made ahead at once: the stream's base reads, in
		the tags' order, find them made) */
		pthread_mutex_lock(source_lock());
		derived_prefetch(NULL);
		pthread_mutex_unlock(source_lock());
	}
}

static void worker_stop(void)
{
	long k, count;

	pthread_mutex_lock(source_lock());
	count = h2_texture_source.worker_count;
	if (!count)
	{
		pthread_mutex_unlock(source_lock());
		return;
	}
	h2_texture_source.stopping = TRUE;
	pthread_cond_broadcast(&source_wake);
	pthread_mutex_unlock(source_lock());
	for (k = 0; k < count; k++)
		pthread_join(h2_texture_source.workers[k], NULL);
	pthread_mutex_lock(source_lock());
	for (k = 0; k < h2_texture_source.parked_count; k++)
	{
		if (h2_texture_source.parked[k].request)
			cache_file_request_finish(h2_texture_source.parked[k].request, FALSE);
		if (h2_texture_source.parked[k].done)
			*h2_texture_source.parked[k].done = -1;
	}
	h2_texture_source.parked_count = 0;
	pthread_cond_broadcast(&source_done);
	h2_texture_source.worker_count = 0;
	h2_texture_source.workers_ready = 0;
	/* (the kept ones are the arena's: freed before it goes) */
	for (k = 0; k < DERIVED_SLOTS; k++)
		if (h2_texture_source.slots[k].pixels)
			free(h2_texture_source.slots[k].pixels);
	memset(h2_texture_source.slots, 0, sizeof(h2_texture_source.slots));
	h2_texture_source.kept_bytes = 0;
	pthread_mutex_unlock(source_lock());
	h2_derive_heap_end();
}

int h2_texture_source_read(long tag_index, long offset, long size, void *buffer)
{
	int ok = FALSE;
	unsigned long long started = vita_host_time_us();

	if (size > 0)
		memory_watch_prepare_write(buffer, (unsigned long)size);
	pthread_mutex_lock(source_lock());
	struct dat_entry const *e = h2_texture_source.count && size > 0 ? entry_at((unsigned long)offset) : NULL;

	if (e)
	{
		unsigned long chain = (unsigned long)offset - e->offset;

		/* (the read's sector rounding past the pixels: zero) */
		if (chain >= e->kept)
			memset(buffer, 0, (size_t)size), ok = TRUE;
		else if (e->kind == _h2_texture_source_derived)
			ok = derived_read(e, tag_index, offset, chain, (unsigned long)size, buffer);
		else if (e->kind != _h2_texture_source_dat || !h2_texture_source.file)
			memset(buffer, 0, (size_t)size), ok = FALSE;
		else
		{
			unsigned long n = (unsigned long)size < e->kept - chain ? (unsigned long)size : e->kept - chain;

			/* (the inflate outside the source's lock: the entry and its
			file stay while the level is loaded) */
			pthread_mutex_unlock(source_lock());
			{
				unsigned long long read_from = HALO_STALL_ON() ? vita_host_time_us() : 0;

				ok = entry_read(e, e->skip + chain, n, buffer);
				if (read_from && vita_host_time_us() - read_from > 5000ULL)
					halo_stall_note("textures.dat: a slow read (us, bytes)", (unsigned long)(vita_host_time_us() - read_from), n);
			}
			pthread_mutex_lock(source_lock());
			if (ok && n < (unsigned long)size)
				memset((unsigned char *)buffer + n, 0, (size_t)size - n);
		}
		h2_texture_source.reads++;
		h2_texture_source.read_bytes += (unsigned long)size;
		if ((dat_debug() && h2_texture_source.reads <= 40) || (!ok && ++h2_texture_source.failures <= 20))
			platform_log("h2 textures: read tag %08lx at %08lx, %ld bytes (entry: offset %08lx, %lu kept, %lu skipped): %s",
				(unsigned long)tag_index, (unsigned long)offset, size, e->offset, e->kept, e->skip, ok ? "read" : "FAILED");
	}
	else if (dat_debug())
		platform_log("h2 textures: read tag %08lx at %08lx, %ld bytes: no entry there (file %d, %ld entries)", (unsigned long)tag_index,
			(unsigned long)offset, size, h2_texture_source.file != NULL, h2_texture_source.count);
	h2_texture_source.read_us += vita_host_time_us() - started;
	pthread_mutex_unlock(source_lock());
	return ok;
}

/* (the cache file thread) a read of a derived bitmap not kept: parked for
the worker, which finishes the request (cache_file_request_finish); TRUE
if parked. HALO_H2_DERIVED_PARK=0: made where asked */
int h2_texture_source_park(long tag_index, long offset, long size, void *buffer, void *request)
{
	static int park = -1;
	struct dat_entry const *e;
	int parked = FALSE;

	if (park < 0)
		park = !getenv("HALO_H2_DERIVED_PARK") || atoi(getenv("HALO_H2_DERIVED_PARK")) != 0;
	if (!park || size <= 0 || halo_thread_index() == halo_game_thread_index())
		return FALSE;
	pthread_mutex_lock(source_lock());
	e = h2_texture_source.count ? entry_at((unsigned long)offset) : NULL;
	if (e && e->kind == _h2_texture_source_derived && (unsigned long)offset - e->offset < e->kept && !derived_kept_pixels(e) &&
		!held_tail_for(e, (unsigned long)offset - e->offset))
		parked = derived_park(tag_index, offset, size, buffer, request, NULL);
	pthread_mutex_unlock(source_lock());
	return parked;
}

/* (kept for the frame's end and the cache file's waits, main.c and
cache_files_windows.c: the worker makes them now) the turnover logged
every 300 frames when it changed */
void h2_texture_source_pump(void)
{
	/* (the cache file's waits for every read - a load, a map's unload: the
	parked ones count, their slots let go when parked - waited for here) */
	long k;
	boolean busy;
	unsigned long long started = 0;

	if (!h2_texture_source.worker_count)
		return;
	pthread_mutex_lock(source_lock());
	for (;;)
	{
		busy = h2_texture_source.parked_count > 0;
		for (k = 0; k < WORKERS_MAXIMUM && !busy; k++)
			busy = h2_texture_source.making[k] != NULL;
		if (!busy || h2_texture_source.stopping)
			break;
		/* (one tried again and again - not made: not waited for past 10 s) */
		if (!started)
			started = vita_host_time_us();
		else if (vita_host_time_us() - started > 10000000ULL)
		{
			platform_log("h2 textures: %ld derived reads still waiting after 10 s at the cache file's wait", h2_texture_source.parked_count);
			break;
		}
		{
			struct timespec until;

			until.tv_sec = time(NULL) + 1;
			until.tv_nsec = 0;
			pthread_cond_timedwait(&source_done, source_lock(), &until);
		}
	}
	pthread_mutex_unlock(source_lock());
}

void h2_texture_source_pump_tick(void)
{
	static unsigned long ticks, last_made = 0, last_hits = 0;

	if (++ticks % 300 == 0 && h2_texture_source.count &&
		(h2_texture_source.derived_made != last_made || h2_texture_source.derived_hits != last_hits))
	{
		last_made = h2_texture_source.derived_made;
		last_hits = h2_texture_source.derived_hits;
		h2_texture_source_report();
	}
}

long h2_texture_source_reads_pending(unsigned long *made)
{
	long k, pending = 0;

	pthread_mutex_lock(source_lock());
	for (k = 0; k < h2_texture_source.parked_count; k++)
		pending += h2_texture_source.parked[k].request || h2_texture_source.parked[k].done;
	/* (and those being made: a read's, or one made ahead - a few, each soon done) */
	for (k = 0; k < WORKERS_MAXIMUM; k++)
		pending += h2_texture_source.making[k] != NULL;
	if (made)
		*made = h2_texture_source.derived_made;
	pthread_mutex_unlock(source_lock());
	return pending;
}

void h2_texture_source_report(void)
{
	if (h2_texture_source.count && getenv("HALO_H2_DERIVED_MEMORY_LOG"))
		platform_memory_log("h2 textures: report", "");
	if (h2_texture_source.count)
	{
		unsigned long arena_in_use = 0, arena_peak = 0, arena_blocks = 0, arena_failures = 0;

		h2_derive_heap_figures(&arena_in_use, &arena_peak, &arena_blocks, &arena_failures);
		if (h2_texture_source.worker_count)
			platform_log("h2 textures: derive arena (user RAM): %lu KB in use, %lu KB at most, %lu KB of blocks, %lu allocations it had no room "
				"for%s; %lu made ahead, %lu bases made alone, %lu read from %ld held (%lu KB)", arena_in_use / 1024, arena_peak / 1024,
				arena_blocks / 1024, arena_failures, arena_failures ? " - A SIZING BUG" : "", h2_texture_source.prefetched,
				h2_texture_source.tails_made, h2_texture_source.tails_held_reads, tails_current.count, tails_current.bytes / 1024);
		platform_log("h2 textures: textures.dat: %lu reads, %lu KB, %lu chunks inflated, %lu ms", h2_texture_source.reads,
			h2_texture_source.read_bytes / 1024, h2_texture_source.chunks_inflated, (unsigned long)(h2_texture_source.read_us / 1000));
		if (h2_texture_source.derived_banded || h2_texture_source.derived_banded_failed)
			platform_log("h2 textures: derived by bands (no block of the bitmap's size): %lu reads made, %lu not",
				h2_texture_source.derived_banded, h2_texture_source.derived_banded_failed);
		if (h2_texture_source.derived_made || h2_texture_source.derived_failed)
			platform_log("h2 textures: derived: %lu made in %lu ms (the slowest %lu ms), %lu read kept, %lu not made; %lu KB kept "
				"(%lu KB at most); %lu reads parked for the worker (%lu tried again), its view of the map opened %lu times "
				"(%lu KB of segments at most)",
				h2_texture_source.derived_made, (unsigned long)(h2_texture_source.derived_us / 1000),
				(unsigned long)(h2_texture_source.worst_make_us / 1000), h2_texture_source.derived_hits, h2_texture_source.derived_failed,
				h2_texture_source.kept_bytes / 1024, h2_texture_source.kept_peak / 1024, h2_texture_source.parked_total,
				h2_texture_source.retries,
				h2_texture_source.view_opens, h2_texture_source.view_peak / 1024);
	}
}
