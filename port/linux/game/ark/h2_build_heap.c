/*
H2_BUILD_HEAP.C

The Halo 2 level builder's heap (h2_build_heap.h): TLSF over memory blocks
of user RAM for its small allocations, a memory block of its own for each
large one (given back as soon as it is freed: the level's tag data, grown a
quarter at a time to 20 MB, would otherwise leave every size it passed
through in the heap's blocks).
*/

/* ---------- headers */

#include "stall_trace.h"
#include "h2_build_heap.h"

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if H2_BUILD_HEAP
#include "../../../third_party/tlsf/tlsf.h"
#ifdef HALO_VITA
/* (port/vita/host/vita_main.c) */
void *vita_host_block_alloc(const char *name, unsigned long size, int *uid);
void vita_host_block_free(int uid);
unsigned long vita_host_free_user_bytes(void);
void *vita_host_phycont_block_alloc(const char *name, unsigned long size, int *uid);
void *vita_host_cdram_block_alloc(const char *name, unsigned long size, int *uid);
unsigned long vita_host_free_phycont_bytes(void);
unsigned long vita_host_free_cdram_bytes(void);
#else
#include <sys/mman.h>
#ifdef _WIN32
/* (the Windows build of the gxm-null level builder, tools/windows_build.py:
no execinfo there - the trace keeps no frames) */
static int backtrace(void **frames, int size)
{
	(void)frames;
	(void)size;
	return 0;
}
#else
#include <execinfo.h>
#endif
#endif
#endif
#ifdef HALO_LINUX
#include "render_epoch.h" /* HALO_ENV_ONCE */
#endif

void platform_log(char const *format, ...);

#if H2_BUILD_HEAP

/* ---------- constants */

enum
{
	/* the small allocations' blocks: each this size */
	BUILD_HEAP_BLOCK_BYTES = 2 * 1024 * 1024,
	BUILD_HEAP_MAXIMUM_BLOCKS = 64,
	/* an allocation this large or larger: a block of its own */
	BUILD_HEAP_LARGE_BYTES = 128 * 1024,
	BUILD_HEAP_MAXIMUM_LARGE = 512,
	/* the most its blocks may hold: on the Vita the system's free user RAM
less a margin when a build begins (60-70 MB free with the menus up), on
the harness 64 MB (HALO_H2_BUILD_HEAP_MB either way) */
	BUILD_HEAP_DEFAULT_MB = 64,
	/* the most an allocation the heap has no room for may take of the C
	heap during a build */
	BUILD_HEAP_C_HEAP_MOST = 4 * 1024 * 1024,
	BUILD_HEAP_VITA_MARGIN_MB = 8,
	/* (block_get) the user RAM a block never takes the system below */
	BUILD_HEAP_VITA_FLOOR_MB = 8,
	/* (block_get, h2_build_heap_video_memory) the video memory a block
	never takes the system below */
	BUILD_HEAP_CDRAM_MARGIN_MB = 8,
	/* (the derive arena) the user RAM left free for the play beside its
	room, and beside what it reserves at the load; from this size an
	allocation is a block of its own */
	DERIVE_HEAP_PLAY_MB = 4,
	DERIVE_HEAP_SPARE_MB = 12,
	DERIVE_HEAP_LARGE_BYTES = 2 * 1024 * 1024,
	BUILD_HEAP_PAGE = 4096,
	/* (every allocation aligned as the C library's on these targets, and
	for the builder's SSE/NEON moves) */
	BUILD_HEAP_ALIGNMENT = 16,
	/* (harness, HALO_H2_BUILD_HEAP_TRACE=1) the live allocations' callers */
	BUILD_HEAP_TRACE_SLOTS = 1 << 18,
	BUILD_HEAP_TRACE_FRAMES = 5,
};

/* ---------- structures */

struct heap_block
{
	unsigned char *base;
	unsigned long bytes;
	int uid;
	/* (the small blocks) its pool in the heap, and the bytes given out in it */
	pool_t pool;
	unsigned long in_use;
	/* (the large ones) the bytes asked for */
	unsigned long size;
};

/* ---------- globals */

struct user_heap
{
	pthread_mutex_t lock;
	/* a build runs: allocations are the heap's */
	int active;
	tlsf_t heap;
	struct heap_block blocks[BUILD_HEAP_MAXIMUM_BLOCKS];
	long block_count;
	struct heap_block large[BUILD_HEAP_MAXIMUM_LARGE];
	long large_count;
	/* the blocks' bytes, now and at most */
	unsigned long block_bytes, block_peak;
	/* bytes given out now and at most since the build began; allocations
	the heap had no room for (the C heap's) */
	unsigned long in_use, peak, refused;
	long limit_mb;
	int trace;
	/* memory blocks made since the build began */
	unsigned long blocks_made;
	/* large allocations refused outright since the build began (no room in
	the heap, too large for the C heap): h2_build_heap_refused_large */
	unsigned long refused_large;
	/* (the instance's own: the builder's or the derive arena's,
	h2_derive_heap_*) its name for the blocks, its small blocks' bytes and
	the size from which an allocation has a block of its own (0: none, all
	in the small blocks); the arena's: the threads whose allocations are
	its, its fixed room (MB), allocations it had no room for */
	char const *name;
	unsigned long small_block_bytes, large_from;
	int derive;
	pthread_t owners[4];
	int owner_count;
	unsigned long failures;
	/* (h3vita, h2_build_heap_video_memory) video memory (CDRAM) as a block's
	source once user RAM is at its floor - a Halo 3 level's make, whose peak
	the Vita's user RAM has no room for -; the bytes of such blocks now and
	at most; their uids (block_put tells them by it) */
	int video_memory;
	unsigned long cdram_bytes, cdram_peak;
	int cdram_uids[BUILD_HEAP_MAXIMUM_BLOCKS + BUILD_HEAP_MAXIMUM_LARGE];
	long cdram_count;
};

static struct user_heap build_heap_state = { PTHREAD_MUTEX_INITIALIZER, 0, NULL, { { NULL } }, 0, { { NULL } }, 0, 0, 0, 0, 0, 0,
	-1, -1, 0, 0, "halo_h2_build_heap", BUILD_HEAP_BLOCK_BYTES, BUILD_HEAP_LARGE_BYTES, 0, { 0 }, 0, 0 };
static struct user_heap derive_heap_state = { PTHREAD_MUTEX_INITIALIZER, 0, NULL, { { NULL } }, 0, { { NULL } }, 0, 0, 0, 0, 0, 0,
	-1, -1, 0, 0, "halo_h2_derive_heap", 0, 0, 1, { 0 }, 0, 0 };

#ifndef HALO_VITA
static struct
{
	void *pointer;
	void *frames[BUILD_HEAP_TRACE_FRAMES];
} *build_heap_trace;
#endif

/* ---------- private code */

static void *block_get(struct user_heap *h, unsigned long bytes, int *uid)
{
	h->blocks_made++;
	if (h->derive)
		halo_stall_note("derive arena: a block made (bytes)", bytes, 0);
#ifdef HALO_VITA
	/* (never the system's last user RAM: a block that would leave less than
	BUILD_HEAP_VITA_FLOOR_MB free is refused - the allocation then the C
	heap's, else refused, and the make fails with its reason - not taken,
	leaving the rest of the game (the GPU's, the sound's, the threads') none:
	an insider.5 tester's Vita died mid-make of Zanzibar after Turf at 6 MB
	free, with no dump) */
	{
		unsigned long free_bytes = vita_host_free_user_bytes();
		static unsigned long said;

		/* (the contiguous main memory first - cached, a pool of its own
		beside the 256 MB of user RAM: the derive arena's room is then not
		the play's user RAM, and a level's make, which runs before its arena
		takes that memory and gives its blocks back at its end, has 26 MB
		more; a BSP's world read during the play finds it the arena's. Whole
		megabytes: a block of 1 MB or more that wastes no more than an
		eighth rounded up) */
		unsigned long rounded = (bytes + 0xFFFFFUL) & ~0xFFFFFUL;

		if (bytes >= 0x100000UL && rounded - bytes <= bytes / 8)
		{
			void *base = vita_host_phycont_block_alloc(h->name, bytes, uid);

			if (base)
				return base;
		}
		/* (then user RAM: never below DERIVE_HEAP_PLAY_MB free for the
		arena, BUILD_HEAP_VITA_FLOOR_MB for the builder) */
		if (free_bytes < bytes + (unsigned long)(h->derive ? DERIVE_HEAP_PLAY_MB : BUILD_HEAP_VITA_FLOOR_MB) * 1024UL * 1024UL)
		{
			/* (video memory, when the build may: CDRAM the CPU reads
			uncached - slower - but the make goes on where it would have
			stopped; never below BUILD_HEAP_CDRAM_MARGIN_MB of it) */
			if (h->video_memory && h->cdram_count < (long)(sizeof(h->cdram_uids) / sizeof(h->cdram_uids[0])) &&
				vita_host_free_cdram_bytes() >= bytes + (unsigned long)BUILD_HEAP_CDRAM_MARGIN_MB * 1024UL * 1024UL)
			{
				void *base = vita_host_cdram_block_alloc(h->name, bytes, uid);

				if (base)
				{
					if (!h->cdram_count)
						platform_log("%s: user RAM at its floor (%lu KB free): blocks from video memory (CDRAM) from here, %lu KB of it free",
							h->name, free_bytes / 1024, vita_host_free_cdram_bytes() / 1024);
					h->cdram_uids[h->cdram_count++] = *uid;
					h->cdram_bytes += bytes;
					if (h->cdram_bytes > h->cdram_peak)
						h->cdram_peak = h->cdram_bytes;
					return base;
				}
			}
			if (said++ < 4)
				platform_log("%s: a %lu KB block refused: no contiguous main memory for it, %lu KB of user RAM free, %d MB kept "
					"for the rest of the game", h->name, bytes / 1024, free_bytes / 1024,
					(int)(h->derive ? DERIVE_HEAP_PLAY_MB : BUILD_HEAP_VITA_FLOOR_MB));
			*uid = -1;
			return NULL;
		}
	}
	return vita_host_block_alloc(h->name, bytes, uid);
#else
	void *p = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

	*uid = -1;
	return p == MAP_FAILED ? NULL : p;
#endif
}

static void block_put(struct user_heap *h, void *base, unsigned long bytes, int uid)
{
	if (h->derive)
		halo_stall_note("derive arena: a block given back (bytes)", bytes, 0);
#ifdef HALO_VITA
	{
		long k;

		(void)base;
		for (k = 0; k < h->cdram_count; k++)
		{
			if (h->cdram_uids[k] == uid)
			{
				h->cdram_uids[k] = h->cdram_uids[--h->cdram_count];
				h->cdram_bytes = h->cdram_bytes > bytes ? h->cdram_bytes - bytes : 0;
				break;
			}
		}
	}
	vita_host_block_free(uid);
#else
	(void)uid;
	munmap(base, bytes);
#endif
}

static unsigned long limit_bytes(struct user_heap *h)
{
	if (h->limit_mb < 0)
	{
		char const *setting = getenv("HALO_H2_BUILD_HEAP_MB");

		h->limit_mb = setting ? atol(setting) : BUILD_HEAP_DEFAULT_MB;
#ifdef HALO_VITA
		if (!setting)
		{
			/* (what this build may have of what is free now, with what the
			heap already holds) */
			long free_mb = (long)(vita_host_free_user_bytes() / (1024 * 1024)) + (long)(h->block_bytes / (1024 * 1024));

			h->limit_mb = free_mb > BUILD_HEAP_VITA_MARGIN_MB ? free_mb - BUILD_HEAP_VITA_MARGIN_MB : 0;
			/* (at least 48 MB tried: Vita3K's free figure does not follow
			blocks given back, and a block the system has no room for is only
			refused - its allocations the C heap's) */
			if (h->limit_mb < 48)
				h->limit_mb = 48;
		}
		/* (and the contiguous main memory free now, block_get's first) */
		h->limit_mb += (long)(vita_host_free_phycont_bytes() / (1024 * 1024));
		/* (and the video memory, when the build may take blocks of it) */
		if (h->video_memory)
		{
			long cdram_mb = (long)(vita_host_free_cdram_bytes() / (1024 * 1024));

			h->limit_mb += cdram_mb > BUILD_HEAP_CDRAM_MARGIN_MB ? cdram_mb - BUILD_HEAP_CDRAM_MARGIN_MB : 0;
		}
#else
		/* (harness) HALO_PHYCONT_MB=n: the Vita's contiguous main memory,
		26 MB free on the hardware, beside the room set - the arena takes
		it first at a level's load, so only the builder's make counts it */
		if (!h->derive && getenv("HALO_PHYCONT_MB"))
			h->limit_mb += atol(getenv("HALO_PHYCONT_MB"));
		/* (harness) HALO_CDRAM_MB=n: the Vita's video memory the build may
		take (h2_build_heap_video_memory), beside the room set */
		if (!h->derive && h->video_memory && getenv("HALO_CDRAM_MB"))
			h->limit_mb += atol(getenv("HALO_CDRAM_MB"));
#endif
		if (h->limit_mb < 0)
			h->limit_mb = 0;
	}
	return (unsigned long)h->limit_mb * 1024 * 1024;
}

static void blocks_added(struct user_heap *h, unsigned long bytes)
{
	h->block_bytes += bytes;
	if (h->block_bytes > h->block_peak)
		h->block_peak = h->block_bytes;
}

/* the small block pointer lies in (-1: none) */
static long small_block_of(struct user_heap *h, void const *pointer)
{
	long i;

	for (i = 0; i < h->block_count; i++)
	{
		if ((unsigned char const *)pointer >= h->blocks[i].base &&
			(unsigned char const *)pointer < h->blocks[i].base + h->blocks[i].bytes)
			return i;
	}
	return -1;
}

/* the large allocation pointer is (-1: none) */
static long large_of(struct user_heap *h, void const *pointer)
{
	long i;

	for (i = 0; i < h->large_count; i++)
	{
		if (h->large[i].base == pointer)
			return i;
	}
	return -1;
}

static int owns_unlocked(struct user_heap *h, void const *pointer)
{
	return small_block_of(h, pointer) >= 0 || large_of(h, pointer) >= 0;
}

/* a small block more (the heap made in the first); FALSE at the limit or
with no memory */
static int grow_unlocked(struct user_heap *h)
{
	unsigned long bytes = h->small_block_bytes;
	unsigned char *base;
	pool_t pool;
	int uid = -1;

	if (h->block_count >= BUILD_HEAP_MAXIMUM_BLOCKS || h->block_bytes + bytes > limit_bytes(h))
		return 0;
	base = block_get(h, bytes, &uid);
	if (!base)
		return 0;
	if (!h->heap)
	{
		h->heap = tlsf_create_with_pool(base, bytes);
		pool = h->heap ? tlsf_get_pool(h->heap) : NULL;
	}
	else
		pool = tlsf_add_pool(h->heap, base, bytes);
	if (!pool)
	{
		block_put(h, base, bytes, uid);
		return 0;
	}
	memset(&h->blocks[h->block_count], 0, sizeof(h->blocks[0]));
	h->blocks[h->block_count].base = base;
	h->blocks[h->block_count].bytes = bytes;
	h->blocks[h->block_count].uid = uid;
	h->blocks[h->block_count].pool = pool;
	h->block_count++;
	blocks_added(h, bytes);
	return 1;
}

/* every small block given back (nothing of them in use) */
static void small_blocks_release_unlocked(struct user_heap *h)
{
	long i;

	h->heap = NULL;
	for (i = h->block_count - 1; i >= 0; i--)
	{
		block_put(h, h->blocks[i].base, h->blocks[i].bytes, h->blocks[i].uid);
		h->block_bytes -= h->blocks[i].bytes;
	}
	h->block_count = 0;
}

/* (harness) a live allocation's callers, or none */
static void trace_note(void *pointer, int add)
{
	struct user_heap *h = &build_heap_state;

#ifndef HALO_VITA
	unsigned long slot, probe;

	if (h->trace < 0)
		h->trace = getenv("HALO_H2_BUILD_HEAP_TRACE") && atoi(getenv("HALO_H2_BUILD_HEAP_TRACE"));
	if (!h->trace)
		return;
	if (!build_heap_trace)
	{
		build_heap_trace = mmap(NULL, sizeof(*build_heap_trace) * BUILD_HEAP_TRACE_SLOTS, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (build_heap_trace == MAP_FAILED)
		{
			build_heap_trace = NULL;
			h->trace = 0;
			return;
		}
	}
	slot = ((unsigned long)pointer >> 4) * 2654435761UL % BUILD_HEAP_TRACE_SLOTS;
	for (probe = 0; probe < BUILD_HEAP_TRACE_SLOTS; probe++, slot = (slot + 1) % BUILD_HEAP_TRACE_SLOTS)
	{
		if (add && (!build_heap_trace[slot].pointer || build_heap_trace[slot].pointer == (void *)1))
		{
			void *frames[BUILD_HEAP_TRACE_FRAMES + 2];
			int count = backtrace(frames, BUILD_HEAP_TRACE_FRAMES + 2), i;

			build_heap_trace[slot].pointer = pointer;
			for (i = 0; i < BUILD_HEAP_TRACE_FRAMES; i++)
				build_heap_trace[slot].frames[i] = i + 2 < count ? frames[i + 2] : NULL;
			return;
		}
		if (!add && build_heap_trace[slot].pointer == pointer)
		{
			/* (a tombstone: the probes past it go on) */
			build_heap_trace[slot].pointer = (void *)1;
			return;
		}
		if (!build_heap_trace[slot].pointer)
			return;
	}
#else
	(void)pointer;
	(void)add;
#endif
}

static void trace_log_live(void)
{
	struct user_heap *h = &build_heap_state;

#ifndef HALO_VITA
	unsigned long slot;
	long logged = 0;

	if (!build_heap_trace)
		return;
	for (slot = 0; slot < BUILD_HEAP_TRACE_SLOTS && logged < 24; slot++)
	{
		if (build_heap_trace[slot].pointer && build_heap_trace[slot].pointer != (void *)1)
		{
			platform_log("h2 build heap: still held: %p (%lu bytes) from %p %p %p %p %p", build_heap_trace[slot].pointer,
				large_of(h, build_heap_trace[slot].pointer) >= 0 ? h->large[large_of(h, build_heap_trace[slot].pointer)].size :
				(unsigned long)tlsf_block_size(build_heap_trace[slot].pointer),
				build_heap_trace[slot].frames[0], build_heap_trace[slot].frames[1], build_heap_trace[slot].frames[2],
				build_heap_trace[slot].frames[3], build_heap_trace[slot].frames[4]);
			logged++;
		}
	}
#endif
}

/* (harness, debug) the live allocations' bytes by their two nearest callers,
the most first */
void h2_build_heap_trace_log(char const *when)
{
#ifndef HALO_VITA
	enum { SITES = 4096, TOP = 20 };
	static struct { void *a, *b; unsigned long bytes, count; } sites[SITES];
	unsigned long slot, used = 0, i, j;
	struct user_heap *h = &build_heap_state;   /* (the build's: the trace is its) */

	pthread_mutex_lock(&h->lock);
	if (!build_heap_trace)
	{
		pthread_mutex_unlock(&h->lock);
		return;
	}
	memset(sites, 0, sizeof(sites));
	for (slot = 0; slot < BUILD_HEAP_TRACE_SLOTS; slot++)
	{
		void *pointer = build_heap_trace[slot].pointer;
		unsigned long bytes;

		if (!pointer || pointer == (void *)1)
			continue;
		bytes = large_of(h, pointer) >= 0 ? h->large[large_of(h, pointer)].size : (unsigned long)tlsf_block_size(pointer);
		for (i = 0; i < used; i++)
			if (sites[i].a == build_heap_trace[slot].frames[1] && sites[i].b == build_heap_trace[slot].frames[2])
				break;
		if (i == used)
		{
			if (used == SITES)
				continue;
			sites[used].a = build_heap_trace[slot].frames[1];
			sites[used].b = build_heap_trace[slot].frames[2];
			used++;
		}
		sites[i].bytes += bytes;
		sites[i].count++;
	}
	for (j = 0; j < TOP && j < used; j++)
	{
		unsigned long best = j;

		for (i = j + 1; i < used; i++)
			if (sites[i].bytes > sites[best].bytes)
				best = i;
		if (best != j)
		{
			sites[SITES - 1] = sites[j];
			sites[j] = sites[best];
			sites[best] = sites[SITES - 1];
		}
		platform_log("h2 build heap at %s: %lu KB in %lu from %p %p", when, sites[j].bytes / 1024, sites[j].count,
			sites[j].a, sites[j].b);
	}
	pthread_mutex_unlock(&h->lock);
#else
	(void)when;
#endif
}

static void *allocate_unlocked(struct user_heap *h, size_t size)
{
	void *p = NULL;

	if (!size)
		size = 1;
	if (h->large_from && size >= h->large_from)
	{
		/* (a block of its own, whole pages) */
		unsigned long bytes = ((unsigned long)size + BUILD_HEAP_PAGE - 1) & ~(unsigned long)(BUILD_HEAP_PAGE - 1);
		int uid = -1;

		if (h->large_count < BUILD_HEAP_MAXIMUM_LARGE && h->block_bytes + bytes <= limit_bytes(h))
			p = block_get(h, bytes, &uid);
		if (p)
		{
			struct heap_block *large = &h->large[h->large_count++];

			memset(large, 0, sizeof(*large));
			large->base = p;
			large->bytes = bytes;
			large->uid = uid;
			large->size = (unsigned long)size;
			blocks_added(h, bytes);
			h->in_use += bytes;
		}
		/* (no room for a block of its own: the small blocks' free room, if
		any is that long - what smaller ones let go of there; up to a whole
		small block: the derive arena's 5.5 MB earth_matte on the dev Vita's
		01b, refused every second with 18 MB of its 24 MB free in its 8 MB
		blocks) */
		else if (h->heap && size < h->small_block_bytes &&
			(p = tlsf_memalign(h->heap, BUILD_HEAP_ALIGNMENT, size)) != NULL)
		{
			long block = small_block_of(h, p);
			unsigned long bytes = (unsigned long)tlsf_block_size(p);

			if (block >= 0)
				h->blocks[block].in_use += bytes;
			h->in_use += bytes;
		}
	}
	else
	{
		long block;

		if (h->heap)
			p = tlsf_memalign(h->heap, BUILD_HEAP_ALIGNMENT, size);
		if (!p && grow_unlocked(h))
			p = tlsf_memalign(h->heap, BUILD_HEAP_ALIGNMENT, size);
		if (p && (block = small_block_of(h, p)) >= 0)
		{
			unsigned long bytes = (unsigned long)tlsf_block_size(p);

			h->blocks[block].in_use += bytes;
			h->in_use += bytes;
		}
	}
	if (p)
	{
		if (h->in_use > h->peak)
			h->peak = h->in_use;
		if (h == &build_heap_state)
			trace_note(p, 1);
	}
	else
	{
		h->refused++;
		if (getenv("HALO_H2_DEBUG"))
		{
			unsigned long small = 0, small_used = 0, large = 0;
			long i;

			for (i = 0; i < h->block_count; i++)
				small += h->blocks[i].bytes, small_used += h->blocks[i].in_use;
			for (i = 0; i < h->large_count; i++)
				large += h->large[i].bytes;
			platform_log("h2 build heap: %lu bytes refused (%lu KB in use, %lu KB of blocks: %ld small %lu KB holding %lu KB, %ld large %lu KB; "
				"room %ld MB)", (unsigned long)size, h->in_use / 1024, h->block_bytes / 1024, h->block_count,
				small / 1024, small_used / 1024, h->large_count, large / 1024, h->limit_mb);
		}
	}
	return p;
}

/* the bytes of the heap's allocation at pointer that may be read */
static unsigned long size_unlocked(struct user_heap *h, void *pointer)
{
	long large = large_of(h, pointer);

	return large >= 0 ? h->large[large].size : (unsigned long)tlsf_block_size(pointer);
}

static void release_unlocked(struct user_heap *h, void *pointer)
{
	long large = large_of(h, pointer);

	if (h == &build_heap_state)
		trace_note(pointer, 0);
	if (large >= 0)
	{
		struct heap_block block = h->large[large];

		h->large[large] = h->large[--h->large_count];
		h->in_use = h->in_use > block.bytes ? h->in_use - block.bytes : 0;
		h->block_bytes -= block.bytes;
		block_put(h, block.base, block.bytes, block.uid);
	}
	else
	{
		long block = small_block_of(h, pointer);
		unsigned long bytes = (unsigned long)tlsf_block_size(pointer);

		if (block >= 0)
			h->blocks[block].in_use = h->blocks[block].in_use > bytes ? h->blocks[block].in_use - bytes : 0;
		h->in_use = h->in_use > bytes ? h->in_use - bytes : 0;
		tlsf_free(h->heap, pointer);
		/* (an empty block past the first, which holds the heap's own
		structure, given back while a build runs on) */
		if (!h->derive && block > 0 && !h->blocks[block].in_use)
		{
			struct heap_block empty = h->blocks[block];

			tlsf_remove_pool(h->heap, empty.pool);
			h->blocks[block] = h->blocks[--h->block_count];
			h->block_bytes -= empty.bytes;
			block_put(h, empty.base, empty.bytes, empty.uid);
		}
	}
	/* (the last of what a build kept past its end: the blocks back) */
	if (!h->derive && !h->active && !h->in_use && h->heap)
		small_blocks_release_unlocked(h);
}

/* ---------- public code */

/* (a build's allocations that got no memory at all - neither the heap's
nor the C heap's - but those of 4 MB or more its large buffers try less
for: h2_build_heap_failed) */
static unsigned long build_failed_count, build_failed_first;
/* (h2_build_heap_unit) the part each thread makes now, and the one the first
of those allocations was made for: what a failed make names */
static __thread char build_unit_now[96];
static char build_failed_unit[96];

/* ---------- (harness) the make's memory peak, sampled (piece H)

HALO_H2_PEAK_SAMPLE=1: while a build is on, a thread of its own samples
every millisecond what the build takes together - the build's heap, the
derive arena and the C heap in use - and logs the most it held, of each,
when and in what phase of the make (h2_level.c's phase marks); and the most
of the make's own: less what it gives back when the heap has none (the
kept chunks, h2_cache.c; the sections made ahead, h2_level.c). One
instrument for every lane's peak. HALO_H2_PEAK_SAMPLE=2: and at each new
most of the make's own (1 MB past the last), who holds it - the build's heap's live
allocations by their callers (with HALO_H2_BUILD_HEAP_TRACE=1) and the C
heap's (with HALO_HEAP_CENSUS=1): the last pair logged is the peak's. The
harness only (the Vita's own figures: platform_memory_log). */
#if !defined(HALO_VITA)
#include <unistd.h>
void platform_heap_usage(unsigned long *in_use, unsigned long *capacity);
void platform_heap_census_log(const char *when);
extern char const *volatile h2_level_phase_last;
unsigned long h2_cache_chunks_kept_bytes(void);
unsigned long h2_level_world_ahead_bytes(void);
unsigned long long vita_host_time_us(void);

static struct
{
	volatile int run;
	int detail;
	pthread_t thread;
	unsigned long long started, at;
	unsigned long most, build, derive, c_heap, snapshot;
	char const *phase;
	/* (the most less what the make gives back when the heap has none: the
	kept chunks, the sections made ahead - its own, inelastic) */
	unsigned long own_most, own_build, own_c_heap;
	unsigned long long own_at;
	char const *own_phase;
} peak_sample;

static void *peak_sample_loop(void *argument)
{
	(void)argument;
	while (__atomic_load_n(&peak_sample.run, __ATOMIC_ACQUIRE))
	{
		unsigned long c_heap = 0, capacity = 0, build = build_heap_state.in_use, derive = derive_heap_state.in_use, total;

		platform_heap_usage(&c_heap, &capacity);
		total = build + derive + c_heap;
		{
			unsigned long elastic = h2_cache_chunks_kept_bytes() + h2_level_world_ahead_bytes();
			unsigned long own = total > elastic ? total - elastic : 0;

			if (own > peak_sample.own_most)
			{
				peak_sample.own_most = own;
				peak_sample.own_build = build > elastic ? build - elastic : 0;
				peak_sample.own_c_heap = c_heap;
				peak_sample.own_at = vita_host_time_us();
				peak_sample.own_phase = h2_level_phase_last;
				/* (=2: who holds it, at each new most of the make's own) */
				if (peak_sample.detail && own > peak_sample.snapshot + 1024UL * 1024UL)
				{
					char when[160];

					peak_sample.snapshot = own;
					snprintf(when, sizeof(when), "the make's own most so far (%lu KB, after %s)", own / 1024, peak_sample.own_phase);
					platform_log("h2 peak: %s - who holds it:", when);
					h2_build_heap_trace_log(when);
					platform_heap_census_log(when);
				}
			}
		}
		if (total > peak_sample.most)
		{
			peak_sample.most = total;
			peak_sample.build = build;
			peak_sample.derive = derive;
			peak_sample.c_heap = c_heap;
			peak_sample.at = vita_host_time_us();
			peak_sample.phase = h2_level_phase_last;
		}
		usleep(1000);
	}
	return NULL;
}

static void peak_sample_begin(void)
{
	char const *setting = getenv("HALO_H2_PEAK_SAMPLE");

	if (!setting || atoi(setting) <= 0 || peak_sample.run)
		return;
	memset(&peak_sample, 0, sizeof(peak_sample));
	peak_sample.detail = atoi(setting) >= 2;
	peak_sample.started = vita_host_time_us();
	peak_sample.run = 1;
	if (pthread_create(&peak_sample.thread, NULL, peak_sample_loop, NULL) != 0)
		peak_sample.run = 0;
}

static void peak_sample_end(void)
{
	if (!peak_sample.run)
		return;
	__atomic_store_n(&peak_sample.run, 0, __ATOMIC_RELEASE);
	pthread_join(peak_sample.thread, NULL);
	platform_log("h2 peak: %lu KB at most together (the build's heap %lu KB, the derive arena %lu KB, the C heap %lu KB), %.0f ms into "
		"the build, after %s", peak_sample.most / 1024, peak_sample.build / 1024, peak_sample.derive / 1024, peak_sample.c_heap / 1024,
		(peak_sample.at - peak_sample.started) / 1000.0, peak_sample.phase ? peak_sample.phase : "?");
	platform_log("h2 peak: %lu KB at most without the kept chunks and the sections made ahead (given back when the heap has "
		"none): the build's heap %lu KB, the C heap %lu KB, %.0f ms into the build, after %s", peak_sample.own_most / 1024,
		peak_sample.own_build / 1024, peak_sample.own_c_heap / 1024, (peak_sample.own_at - peak_sample.started) / 1000.0,
		peak_sample.own_phase ? peak_sample.own_phase : "?");
}
#else
static void peak_sample_begin(void)
{
}

static void peak_sample_end(void)
{
}
#endif

/* (h2_build_heap_thread_plays) a thread that plays - the tick's
(tick_thread.c) -, and the thread that began the build on now */
static __thread int build_heap_thread_playing;
static pthread_t build_heap_beginner;
static int build_heap_beginner_set;

void h2_build_heap_thread_plays(void)
{
	build_heap_thread_playing = 1;
}

/* whether the calling thread's allocations stay out of the build's heap: one
that plays, while a build it did not begin is on (a make on another thread) */
static int build_heap_thread_out(void)
{
	int out;

	if (!build_heap_thread_playing)
		return 0;
	pthread_mutex_lock(&build_heap_state.lock);
	out = !build_heap_beginner_set || !pthread_equal(build_heap_beginner, pthread_self());
	pthread_mutex_unlock(&build_heap_state.lock);
	return out;
}

void h2_build_heap_begin(void)
{
	peak_sample_begin();
	pthread_mutex_lock(&build_heap_state.lock);
	build_heap_beginner = pthread_self();
	build_heap_beginner_set = 1;
	build_failed_count = build_failed_first = 0;
	build_failed_unit[0] = 0;
	build_heap_state.active = 1;
	build_heap_state.blocks_made = 0;
	/* (HALO_H2_BUILD_HEAP_CDRAM=0/1: video memory never / for every build,
	whatever h2_build_heap_video_memory said) */
	if (getenv("HALO_H2_BUILD_HEAP_CDRAM"))
		build_heap_state.video_memory = atoi(getenv("HALO_H2_BUILD_HEAP_CDRAM")) != 0;
	build_heap_state.cdram_peak = build_heap_state.cdram_bytes;
	/* (the limit taken again: what is free now) */
	build_heap_state.limit_mb = -1;
	limit_bytes(&build_heap_state);
	build_heap_state.peak = build_heap_state.in_use;
	build_heap_state.block_peak = build_heap_state.block_bytes;
	build_heap_state.refused = 0;
	build_heap_state.refused_large = 0;
	pthread_mutex_unlock(&build_heap_state.lock);
}

void h2_build_heap_end(void)
{
	unsigned long in_use, peak, blocks, blocks_now, refused;

	peak_sample_end();

	pthread_mutex_lock(&build_heap_state.lock);
	build_heap_state.active = 0;
	build_heap_beginner_set = 0;
	in_use = build_heap_state.in_use;
	peak = build_heap_state.peak;
	blocks = build_heap_state.block_peak;
	refused = build_heap_state.refused;
	if (in_use)
		trace_log_live();
	if (!in_use && build_heap_state.heap)
		small_blocks_release_unlocked(&build_heap_state);
	blocks_now = build_heap_state.block_bytes;
	pthread_mutex_unlock(&build_heap_state.lock);
	platform_log("h2 level: the build's own heap (user RAM): %lu KB at most in %lu KB of blocks at most (room %ld MB, %lu "
		"blocks made, %lu KB of video memory at most), %lu KB still held (%lu KB of blocks kept), %lu allocations it had no room for (the C heap's)",
		peak / 1024, blocks / 1024, build_heap_state.limit_mb, build_heap_state.blocks_made, build_heap_state.cdram_peak / 1024, in_use / 1024,
		blocks_now / 1024, refused);
}

/* whether the builds from here may take blocks of video memory (CDRAM) once
user RAM is at its floor (block_get): a Halo 3 level's make says so around
itself; HALO_H2_BUILD_HEAP_CDRAM=0/1 overrides at each build's begin */
void h2_build_heap_video_memory(int allowed)
{
	pthread_mutex_lock(&build_heap_state.lock);
	build_heap_state.video_memory = allowed != 0;
	pthread_mutex_unlock(&build_heap_state.lock);
}

/* (the derive arena) whether the calling thread's allocations are its */
static int derive_thread(void)
{
	pthread_t self;
	int k;

	if (!__atomic_load_n(&derive_heap_state.active, __ATOMIC_ACQUIRE))
		return 0;
	self = pthread_self();
	for (k = 0; k < derive_heap_state.owner_count; k++)
		if (pthread_equal(derive_heap_state.owners[k], self))
			return 1;
	return 0;
}

/* (the derive arena, h2_derive_heap_make_begin/figures) the calling
thread's make: its largest allocation, and the arena's bytes in use at most
while it ran */
static __thread unsigned long derive_make_largest, derive_make_peak;

void h2_derive_heap_make_begin(void)
{
	derive_make_largest = derive_make_peak = 0;
}

void h2_derive_heap_make_figures(unsigned long *largest, unsigned long *peak)
{
	*largest = derive_make_largest;
	*peak = derive_make_peak;
}

/* (the derive arena) an allocation of its thread's: its, or none - never
the C heap's (the play's free C heap must not move) - and said loudly: a
sizing bug */
static void *derive_allocate(size_t size)
{
	void *p;

	pthread_mutex_lock(&derive_heap_state.lock);
	p = allocate_unlocked(&derive_heap_state, size);
	if (!p)
		derive_heap_state.failures++;
	if ((unsigned long)size > derive_make_largest)
		derive_make_largest = (unsigned long)size;
	if (derive_heap_state.in_use > derive_make_peak)
		derive_make_peak = derive_heap_state.in_use;
	pthread_mutex_unlock(&derive_heap_state.lock);
	if (!p)
		platform_log("h2 derive heap: NO ROOM for %lu bytes (%lu KB in use of %ld MB, %lu KB of blocks): the arena is too small "
			"- a sizing bug", (unsigned long)size, derive_heap_state.in_use / 1024, derive_heap_state.limit_mb,
			derive_heap_state.block_bytes / 1024);
#ifndef HALO_VITA
	/* (harness, HALO_H2_DERIVED_DEBUG=1: an allocation of 4 MB or more, its callers) */
	if (size >= 4UL * 1024 * 1024 && getenv("HALO_H2_DERIVED_DEBUG"))
	{
		void *frames[8];
		int count = backtrace(frames, 8);

		platform_log("h2 derive heap: %lu bytes %s, from %p %p %p %p %p %p", (unsigned long)size, p ? "given" : "REFUSED",
			count > 1 ? frames[1] : NULL, count > 2 ? frames[2] : NULL, count > 3 ? frames[3] : NULL, count > 4 ? frames[4] : NULL,
			count > 5 ? frames[5] : NULL, count > 6 ? frames[6] : NULL);
	}
#endif
	return p;
}

static int derive_owns(void const *pointer)
{
	int owns;

	if (!pointer || (!derive_heap_state.block_count && !derive_heap_state.large_count))
		return 0;
	pthread_mutex_lock(&derive_heap_state.lock);
	owns = owns_unlocked(&derive_heap_state, pointer);
	pthread_mutex_unlock(&derive_heap_state.lock);
	return owns;
}

/* (h2_build_heap_pressure_add) what lets memory go when the heap has no
room: a few, each called in turn */
#define BUILD_HEAP_PRESSURE_MOST 4
static void (*build_heap_pressure[BUILD_HEAP_PRESSURE_MOST])(void);

void h2_build_heap_pressure_add(void (*release)(void))
{
	int k;

	for (k = 0; k < BUILD_HEAP_PRESSURE_MOST; k++)
	{
		void (*none)(void) = NULL;

		if (__atomic_load_n(&build_heap_pressure[k], __ATOMIC_ACQUIRE) == release ||
			__atomic_compare_exchange_n(&build_heap_pressure[k], &none, release, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		{
			return;
		}
	}
}

void h2_build_heap_pressure_remove(void (*release)(void))
{
	int k;

	for (k = 0; k < BUILD_HEAP_PRESSURE_MOST; k++)
	{
		void (*mine)(void) = release;

		__atomic_compare_exchange_n(&build_heap_pressure[k], &mine, NULL, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
	}
}

/* (no room: what may go let go, then once more - FALSE if nothing to let go) */
static int build_heap_pressure_release(void)
{
	int k, any = 0;

	for (k = 0; k < BUILD_HEAP_PRESSURE_MOST; k++)
	{
		void (*release)(void) = __atomic_load_n(&build_heap_pressure[k], __ATOMIC_ACQUIRE);

		if (release)
			release(), any = 1;
	}
	return any;
}


static void *failed_note(void *p, size_t size)
{
	if (!p && __atomic_load_n(&build_heap_state.active, __ATOMIC_ACQUIRE) && size < BUILD_HEAP_C_HEAP_MOST)
	{
		pthread_mutex_lock(&build_heap_state.lock);
		if (!build_failed_count++)
		{
			build_failed_first = (unsigned long)size;
			snprintf(build_failed_unit, sizeof(build_failed_unit), "%s", build_unit_now);
		}
		pthread_mutex_unlock(&build_heap_state.lock);
	}
	return p;
}

void h2_build_heap_unit(char const *what)
{
	snprintf(build_unit_now, sizeof(build_unit_now), "%s", what ? what : "");
}

char const *h2_build_heap_failed_unit(void)
{
	return build_failed_unit;
}

unsigned long h2_build_heap_failed(unsigned long *first_size)
{
	unsigned long count;

	pthread_mutex_lock(&build_heap_state.lock);
	count = build_failed_count;
	if (first_size)
		*first_size = build_failed_first;
	pthread_mutex_unlock(&build_heap_state.lock);
	return count;
}

void *h2_build_heap_malloc(size_t size)
{
	void *p = NULL;

	int active;

	if (derive_thread())
		return derive_allocate(size);
	/* (the tick's while another thread makes - the HUD's make at load on the
	game thread -: the C heap's. Its play memory - a face's phonemes, decoded
	in h2_animation.c - is freed by files outside the wrap, with the C
	library's free: on the Vita newlib's free of a heap's pointer corrupted
	the C heap, the dev Vita's 03a intro halt at tick ~80) */
	if (build_heap_thread_out())
		return malloc(size);
#ifndef HALO_VITA
	/* (harness, tests: HALO_H2_BUILD_HEAP_FAIL=<unit text>,<n> - the nth
	allocation made for a part whose name holds that text gets no memory, as
	a full heap would give it: a maker's failure, and the make's, seen by
	name) */
	{
		static __thread long seen;
		char const *fail = HALO_ENV_ONCE("HALO_H2_BUILD_HEAP_FAIL");
		char const *comma = fail ? strrchr(fail, ',') : NULL;

		char text[96];

		if (comma && (size_t)(comma - fail) < sizeof(text))
		{
			memcpy(text, fail, (size_t)(comma - fail));
			text[comma - fail] = 0;
		}
		if (comma && build_heap_state.active && size < BUILD_HEAP_C_HEAP_MOST && (size_t)(comma - fail) < sizeof(text) &&
			strstr(build_unit_now, text) && ++seen == atol(comma + 1))
		{
			return failed_note(NULL, size);
		}
	}
#endif

	pthread_mutex_lock(&build_heap_state.lock);
	active = build_heap_state.active;
	if (active)
		p = allocate_unlocked(&build_heap_state, size);
	pthread_mutex_unlock(&build_heap_state.lock);
	if (!p && active && build_heap_pressure_release())
	{
		pthread_mutex_lock(&build_heap_state.lock);
		p = build_heap_state.active ? allocate_unlocked(&build_heap_state, size) : NULL;
		pthread_mutex_unlock(&build_heap_state.lock);
	}
#ifndef HALO_VITA
	/* (harness, debug) HALO_H2_BUILD_HEAP_TRACE_AT_KB: the live allocations
	by caller once, when the heap first holds that much, or (0) first has no
	room for one of 4 MB or more (with HALO_H2_BUILD_HEAP_TRACE=1) */
	{
		static int traced;
		char const *at = HALO_ENV_ONCE("HALO_H2_BUILD_HEAP_TRACE_AT_KB");

		if (!traced && at && active && ((p && atol(at) > 0 && build_heap_state.in_use / 1024 >= (unsigned long)atol(at)) ||
			(!p && atol(at) == 0 && size >= BUILD_HEAP_C_HEAP_MOST)))
		{
			traced = 1;
			h2_build_heap_trace_log("its most");
		}
	}
#endif
	/* (a large one the heap had no room for is refused, not the C heap's:
	the Vita's has 14-21 MB free, and the builder's large buffers - the tag
	data's room, a BSP's - try less when refused (bytes_reserve). The
	harness's C heap took a campaign level's 28 MB tag data whole and hid
	it) */
	if (!p && active && size >= BUILD_HEAP_C_HEAP_MOST)
	{
		pthread_mutex_lock(&build_heap_state.lock);
		build_heap_state.refused_large++;
		pthread_mutex_unlock(&build_heap_state.lock);
		return NULL;
	}
	return p ? p : failed_note(malloc(size), size);
}

void *h2_build_heap_calloc(size_t count, size_t size)
{
	void *p;

	if (count && size > (size_t)-1 / count)
		return NULL;
	p = h2_build_heap_malloc(count * size);
	if (p)
		memset(p, 0, count * size);
	return p;
}

void *h2_build_heap_keep(void *pointer)
{
	void *copy;
	unsigned long size = 0;

	if (!pointer)
		return pointer;
	pthread_mutex_lock(&build_heap_state.lock);
	if (small_block_of(&build_heap_state, pointer) >= 0)
		size = size_unlocked(&build_heap_state, pointer);
	pthread_mutex_unlock(&build_heap_state.lock);
	if (!size || !(copy = malloc(size)))
		return pointer;
	memcpy(copy, pointer, size);
	h2_build_heap_free(pointer);
	return copy;
}

void h2_build_heap_free(void *pointer)
{
	if (!pointer)
		return;
	if (derive_owns(pointer))
	{
		pthread_mutex_lock(&derive_heap_state.lock);
		release_unlocked(&derive_heap_state, pointer);
		pthread_mutex_unlock(&derive_heap_state.lock);
		return;
	}
	pthread_mutex_lock(&build_heap_state.lock);
	if (owns_unlocked(&build_heap_state, pointer))
	{
		release_unlocked(&build_heap_state, pointer);
		pthread_mutex_unlock(&build_heap_state.lock);
		return;
	}
	pthread_mutex_unlock(&build_heap_state.lock);
	free(pointer);
}

void *h2_build_heap_realloc(void *pointer, size_t size)
{
	void *moved = NULL;
	unsigned long old_size;

	if (!pointer)
		return h2_build_heap_malloc(size);
	if (!size)
	{
		h2_build_heap_free(pointer);
		return NULL;
	}
	/* (the derive arena's, or one its thread grows: moved within it) */
	if (derive_owns(pointer) || derive_thread())
	{
		unsigned long old_size = 0;
		void *grown;

		if (derive_owns(pointer))
		{
			pthread_mutex_lock(&derive_heap_state.lock);
			old_size = size_unlocked(&derive_heap_state, pointer);
			pthread_mutex_unlock(&derive_heap_state.lock);
			if ((size_t)old_size >= size)
				return pointer;
			grown = derive_thread() ? derive_allocate(size) : malloc(size);
		}
		else
		{
			/* (a C heap one its thread grows - made before it was the
			arena's: grown where it is) */
			pthread_mutex_lock(&build_heap_state.lock);
			if (owns_unlocked(&build_heap_state, pointer))
			{
				pthread_mutex_unlock(&build_heap_state.lock);
				grown = derive_allocate(size);
				if (grown)
				{
					unsigned long old_bytes;

					pthread_mutex_lock(&build_heap_state.lock);
					old_bytes = size_unlocked(&build_heap_state, pointer);
					pthread_mutex_unlock(&build_heap_state.lock);
					memcpy(grown, pointer, old_bytes < size ? old_bytes : size);
					h2_build_heap_free(pointer);
				}
				return grown;
			}
			pthread_mutex_unlock(&build_heap_state.lock);
			return realloc(pointer, size);
		}
		if (grown)
		{
			memcpy(grown, pointer, old_size < size ? old_size : size);
			h2_build_heap_free(pointer);
		}
		return grown;
	}
	pthread_mutex_lock(&build_heap_state.lock);
	if (!owns_unlocked(&build_heap_state, pointer))
	{
		pthread_mutex_unlock(&build_heap_state.lock);
		/* (the C heap's: grown there) */
		return failed_note(realloc(pointer, size), size);
	}
	old_size = size_unlocked(&build_heap_state, pointer);
	/* (one that still fits stays; else moved, within the heap if it has
	room) */
	if ((size_t)old_size >= size)
	{
		pthread_mutex_unlock(&build_heap_state.lock);
		return pointer;
	}
	moved = allocate_unlocked(&build_heap_state, size);
	if (moved)
	{
		memcpy(moved, pointer, old_size < size ? old_size : size);
		release_unlocked(&build_heap_state, pointer);
		pthread_mutex_unlock(&build_heap_state.lock);
		return moved;
	}
	pthread_mutex_unlock(&build_heap_state.lock);
	/* (what may go let go, then once more) */
	if (build_heap_pressure_release())
	{
		pthread_mutex_lock(&build_heap_state.lock);
		moved = allocate_unlocked(&build_heap_state, size);
		if (moved)
		{
			memcpy(moved, pointer, old_size < size ? old_size : size);
			release_unlocked(&build_heap_state, pointer);
			pthread_mutex_unlock(&build_heap_state.lock);
			return moved;
		}
		pthread_mutex_unlock(&build_heap_state.lock);
	}
	/* (no room in the heap: the C heap's, a small one) */
	if (size >= BUILD_HEAP_C_HEAP_MOST)
		return NULL;
	moved = failed_note(malloc(size), size);
	if (moved)
	{
		memcpy(moved, pointer, old_size < size ? old_size : size);
		h2_build_heap_free(pointer);
	}
	return moved;
}

/* (piece H) what the build's heap has left of its room now: 0 if none or no
build on - what work made ahead may take without the make running short */
unsigned long h2_build_heap_room_left(void)
{
	unsigned long room, left = 0;

	pthread_mutex_lock(&build_heap_state.lock);
	if (build_heap_state.active)
	{
		room = limit_bytes(&build_heap_state);
		left = room > build_heap_state.in_use ? room - build_heap_state.in_use : 0;
	}
	pthread_mutex_unlock(&build_heap_state.lock);
	return left;
}

int h2_build_heap_active(void)
{
	int active;

	pthread_mutex_lock(&build_heap_state.lock);
	active = build_heap_state.active;
	pthread_mutex_unlock(&build_heap_state.lock);
	return active;
}

int h2_build_heap_owns(void const *pointer)
{
	int owns;

	if (!pointer || (!build_heap_state.block_count && !build_heap_state.large_count))
		return 0;
	pthread_mutex_lock(&build_heap_state.lock);
	owns = owns_unlocked(&build_heap_state, pointer);
	pthread_mutex_unlock(&build_heap_state.lock);
	return owns;
}

void h2_build_heap_figures(unsigned long *in_use, unsigned long *peak, unsigned long *blocks)
{
	pthread_mutex_lock(&build_heap_state.lock);
	*in_use = build_heap_state.in_use;
	*peak = build_heap_state.peak;
	*blocks = build_heap_state.block_bytes;
	pthread_mutex_unlock(&build_heap_state.lock);
}

unsigned long h2_build_heap_refused_large(void)
{
	unsigned long count;

	pthread_mutex_lock(&build_heap_state.lock);
	count = build_heap_state.refused_large;
	pthread_mutex_unlock(&build_heap_state.lock);
	return count;
}

/* ---------- user RAM blocks, one an allocation (an MCC cache's inflated
piece when the window and the C heap have no room: h2_cache.c) */

enum
{
	/* (the MCC caches' pieces made in user RAM ahead of the C heap outside a
	build - a level's AI step's 9-23 MB of them, h2_cache.c - each one) */
	USER_BLOCKS_MAXIMUM = 512,
	/* (the user RAM a block must leave free) */
	USER_BLOCKS_SPARE_MB = 4
};

static struct
{
	pthread_mutex_t lock;
	struct { void *base; unsigned long bytes; int uid; } blocks[USER_BLOCKS_MAXIMUM];
	long count;
} user_blocks = { PTHREAD_MUTEX_INITIALIZER };

void *h2_user_block_alloc(unsigned long size)
{
	return h2_user_block_alloc_named("halo_h2_cache_pieces", size);
}

/* (kind 0 user RAM - never below USER_BLOCKS_SPARE_MB free -, 1 contiguous
main memory, 2 video memory) */
static void *user_block_alloc_kind(char const *name, unsigned long size, int kind)
{
	unsigned long bytes = (size + BUILD_HEAP_PAGE - 1) & ~(unsigned long)(BUILD_HEAP_PAGE - 1);
	void *base = NULL;
	int uid = -1, k;

	if (!bytes)
		bytes = BUILD_HEAP_PAGE;
#ifdef HALO_VITA
	if (kind == 0 && vita_host_free_user_bytes() < bytes + USER_BLOCKS_SPARE_MB * 1024UL * 1024UL)
		return NULL;
#else
	if (kind != 0)
		return NULL;
#endif
	pthread_mutex_lock(&user_blocks.lock);
	for (k = 0; k < USER_BLOCKS_MAXIMUM && user_blocks.blocks[k].base; k++)
		;
	if (k < USER_BLOCKS_MAXIMUM)
	{
#ifdef HALO_VITA
		if (kind == 0)
			base = vita_host_block_alloc(name, bytes, &uid);
		else if (kind == 1)
			base = vita_host_phycont_block_alloc(name, bytes, &uid);
		else
			base = vita_host_cdram_block_alloc(name, bytes, &uid);
#else
		base = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
		if (base == MAP_FAILED)
			base = NULL;
#endif
		if (base)
		{
			user_blocks.blocks[k].base = base;
			user_blocks.blocks[k].bytes = bytes;
			user_blocks.blocks[k].uid = uid;
			user_blocks.count++;
		}
	}
	pthread_mutex_unlock(&user_blocks.lock);
	return base;
}

void *h2_user_block_alloc_named(char const *name, unsigned long size)
{
	return user_block_alloc_kind(name, size, 0);
}

void *h2_phycont_block_alloc_named(char const *name, unsigned long size)
{
	return user_block_alloc_kind(name, size, 1);
}

void *h2_video_block_alloc_named(char const *name, unsigned long size)
{
	return user_block_alloc_kind(name, size, 2);
}

/* a block of its own from any memory the system has: user RAM (never its
last USER_BLOCKS_SPARE_MB), else the contiguous main memory, else video
memory (CDRAM, which the CPU reads slower: only what nothing else could
give - Jolt's, physics_jolt.cpp, rather than the game stopping); given back
by h2_user_block_free. *where: 0, 1 or 2 as found (NULL: not said) */
void *h2_any_block_alloc_named(char const *name, unsigned long size, int *where)
{
	int kind;

	for (kind = 0; kind < 3; kind++)
	{
		void *base = user_block_alloc_kind(name, size, kind);

		if (base)
		{
			if (where)
				*where = kind;
			return base;
		}
	}
	return NULL;
}

int h2_user_block_free(void *data)
{
	int k, found = 0;

	if (!data || !__atomic_load_n(&user_blocks.count, __ATOMIC_ACQUIRE))
		return 0;
	pthread_mutex_lock(&user_blocks.lock);
	for (k = 0; k < USER_BLOCKS_MAXIMUM && user_blocks.blocks[k].base != data; k++)
		;
	if (k < USER_BLOCKS_MAXIMUM)
	{
#ifdef HALO_VITA
		vita_host_block_free(user_blocks.blocks[k].uid);
#else
		munmap(user_blocks.blocks[k].base, user_blocks.blocks[k].bytes);
#endif
		user_blocks.blocks[k].base = NULL;
		user_blocks.count--;
		found = 1;
	}
	pthread_mutex_unlock(&user_blocks.lock);
	return found;
}

/* ---------- (piece J) the derive arena: a second instance of the heap,
for the derived bitmaps' worker threads during the play (h2_texture_source.c):
TLSF over user RAM blocks reserved at the level's load, never the C heap */

void h2_derive_heap_begin(long room_mb, long reserve_mb, long block_mb)
{
	struct user_heap *h = &derive_heap_state;

#ifdef HALO_VITA
	/* (one budget: what the system has free now, the build's blocks given
	back - the arena takes no more than leaves DERIVE_HEAP_PLAY_MB free for
	the play, and reserves no more than leaves DERIVE_HEAP_SPARE_MB; on the
	owner's Vita, Zanzibar's 24 MB reserve left the kernel 0 KB free) */
	/* (the contiguous main memory - 26 MB the play never takes, block_get's
	first - counts in full beside it, its blocks given whole megabytes) */
	{
		long free_mb = (long)(vita_host_free_user_bytes() / (1024 * 1024));
		long phycont_mb = (long)(vita_host_free_phycont_bytes() / (1024 * 1024));

		if (room_mb > phycont_mb + free_mb - DERIVE_HEAP_PLAY_MB)
			room_mb = phycont_mb + free_mb - DERIVE_HEAP_PLAY_MB;
		if (reserve_mb > phycont_mb + free_mb - DERIVE_HEAP_SPARE_MB)
			reserve_mb = phycont_mb + free_mb - DERIVE_HEAP_SPARE_MB;
		if (reserve_mb < block_mb)
			reserve_mb = block_mb;
		platform_log("h2 derive heap: user RAM free %ld MB, contiguous main memory free %ld MB at the level's load: room %ld MB, "
			"%ld MB reserved", free_mb, phycont_mb, room_mb, reserve_mb);
	}
#endif
	pthread_mutex_lock(&h->lock);
	h->limit_mb = room_mb > 0 ? room_mb : 0;
	h->small_block_bytes = (unsigned long)(block_mb > 0 ? block_mb : 8) * 1024UL * 1024UL;
	/* (an allocation of DERIVE_HEAP_LARGE_BYTES or more a block of its own:
	an 8 MB one fit no 8 MB block - NO ROOM on the owner's Vita, Zanzibar) */
	h->large_from = DERIVE_HEAP_LARGE_BYTES;
	h->owner_count = 0;
	h->failures = 0;
	h->peak = h->in_use;
	h->block_peak = h->block_bytes;
	h->blocks_made = 0;
	while (h->block_bytes < (unsigned long)reserve_mb * 1024UL * 1024UL && grow_unlocked(h))
		;
	__atomic_store_n(&h->active, 1, __ATOMIC_RELEASE);
	pthread_mutex_unlock(&h->lock);
	platform_log("h2 derive heap: %lu KB reserved (contiguous main memory first, then user RAM; room %ld MB, blocks of %ld MB)", h->block_bytes / 1024, h->limit_mb,
		block_mb);
}

/* the calling thread's allocations the arena's (a worker's, at its start) */
void h2_derive_heap_thread(void)
{
	struct user_heap *h = &derive_heap_state;

	pthread_mutex_lock(&h->lock);
	if (h->owner_count < (int)(sizeof(h->owners) / sizeof(h->owners[0])))
		h->owners[h->owner_count++] = pthread_self();
	pthread_mutex_unlock(&h->lock);
}

void h2_derive_heap_end(void)
{
	struct user_heap *h = &derive_heap_state;
	unsigned long peak, blocks, in_use, failures;

	pthread_mutex_lock(&h->lock);
	__atomic_store_n(&h->active, 0, __ATOMIC_RELEASE);
	h->owner_count = 0;
	peak = h->peak;
	blocks = h->block_peak;
	in_use = h->in_use;
	failures = h->failures;
	if (!in_use && h->heap)
		small_blocks_release_unlocked(h);
	pthread_mutex_unlock(&h->lock);
	platform_log("h2 derive heap: %lu KB at most in %lu KB of blocks at most, %lu KB still held, %lu allocations it had no room "
		"for%s", peak / 1024, blocks / 1024, in_use / 1024, failures, failures ? " - A SIZING BUG" : "");
}

void h2_derive_heap_figures(unsigned long *in_use, unsigned long *peak, unsigned long *blocks, unsigned long *failures)
{
	struct user_heap *h = &derive_heap_state;

	pthread_mutex_lock(&h->lock);
	*in_use = h->in_use;
	*peak = h->peak;
	*blocks = h->block_bytes;
	*failures = h->failures;
	pthread_mutex_unlock(&h->lock);
}

#else

void h2_build_heap_thread_plays(void)
{
}

void h2_build_heap_trace_log(char const *when)
{
	(void)when;
}

void h2_build_heap_pressure_add(void (*release)(void))
{
	(void)release;
}

void h2_build_heap_pressure_remove(void (*release)(void))
{
	(void)release;
}

void h2_build_heap_begin(void)
{
}

void h2_build_heap_video_memory(int allowed)
{
	(void)allowed;
}

void h2_build_heap_end(void)
{
}

void *h2_build_heap_malloc(size_t size)
{
	return malloc(size);
}

void *h2_build_heap_calloc(size_t count, size_t size)
{
	return calloc(count, size);
}

void *h2_build_heap_realloc(void *pointer, size_t size)
{
	return realloc(pointer, size);
}

void *h2_user_block_alloc(unsigned long size)
{
	(void)size;
	return NULL;
}

void *h2_user_block_alloc_named(char const *name, unsigned long size)
{
	(void)name;
	(void)size;
	return NULL;
}

void *h2_phycont_block_alloc_named(char const *name, unsigned long size)
{
	(void)name;
	(void)size;
	return NULL;
}

void *h2_video_block_alloc_named(char const *name, unsigned long size)
{
	(void)name;
	(void)size;
	return NULL;
}

void *h2_any_block_alloc_named(char const *name, unsigned long size, int *where)
{
	(void)name;
	(void)size;
	(void)where;
	return NULL;
}

int h2_user_block_free(void *data)
{
	(void)data;
	return 0;
}

void *h2_build_heap_keep(void *pointer)
{
	return pointer;
}

void h2_build_heap_free(void *pointer)
{
	free(pointer);
}

unsigned long h2_build_heap_room_left(void)
{
	return 0xFFFFFFFFUL;
}

unsigned long h2_build_heap_failed(unsigned long *first_size)
{
	if (first_size)
		*first_size = 0;
	return 0;
}

void h2_build_heap_unit(char const *what)
{
	(void)what;
}

char const *h2_build_heap_failed_unit(void)
{
	return "";
}

int h2_build_heap_active(void)
{
	return 0;
}

int h2_build_heap_owns(void const *pointer)
{
	(void)pointer;
	return 0;
}

void h2_build_heap_figures(unsigned long *in_use, unsigned long *peak, unsigned long *blocks)
{
	*in_use = *peak = *blocks = 0;
}

unsigned long h2_build_heap_refused_large(void)
{
	return 0;
}

void h2_derive_heap_begin(long room_mb, long reserve_mb, long block_mb)
{
	(void)room_mb;
	(void)reserve_mb;
	(void)block_mb;
}

void h2_derive_heap_thread(void)
{
}

void h2_derive_heap_end(void)
{
}

void h2_derive_heap_make_begin(void)
{
}

void h2_derive_heap_make_figures(unsigned long *largest, unsigned long *peak)
{
	*largest = *peak = 0;
}

void h2_derive_heap_figures(unsigned long *in_use, unsigned long *peak, unsigned long *blocks, unsigned long *failures)
{
	*in_use = *peak = *blocks = *failures = 0;
}

#endif
