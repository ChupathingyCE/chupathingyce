/*
XBOX_HEAP.C

The game's heap, inside the Xbox address space.

Everything the game allocates (system_malloc is GlobalAlloc) can end up in
a structure that holds 32-bit pointers: tag data built at run time, script
values, Direct3D resource headers. So the heap lives in the Xbox address
space too (cseries/xbox_address.h), below the contiguous window: Xbox addresses
XBOX_HEAP_BASE to XBOX_HEAP_END, mapped read-write at the first allocation
and backed by the host on first touch. Windows and the consoles have no
pages backed on first touch: there the heap commits its range in large
steps as it grows (heap_commit), so that memory plus the page file need
cover only what the game uses, not the whole range.

Blocks carry a 16-byte header. Requests up to 64 KB come from power-of-two
size classes with free lists; larger ones are rounded to 64 KB and reuse
freed large blocks first fit. The game allocates mostly at start-up and map
load, so this stays simple rather than clever.
*/

#include "platform.h"

#ifdef HALO_64BIT

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#ifdef HALO_WINDOW_TRANSLATED
/* the window's part below the contiguous memory (xbox_address.h), committed
as the heap grows: the console has no pages backed on first touch */
#define XBOX_HEAP_BASE XBOX_WINDOW_LOW
#define XBOX_HEAP_END PLATFORM_CONTIGUOUS_BASE
#define XBOX_HEAP_COMMIT_AS_USED
#define HEAP_COMMIT_GRANULARITY 0x100000U
#else
/* (above Custom Edition maps' tag cache and the room above it, platform.h) */
#define XBOX_HEAP_BASE 0x42000000U
#define XBOX_HEAP_END 0x7f000000U
#ifdef _WIN32
/* (a read-write mapping on Windows is committed memory, charged against
memory plus the page file at once: win32_posix.c's mmap. The range is
reserved already, with the rest of the Xbox address space, xbox_memory.c) */
#define XBOX_HEAP_COMMIT_AS_USED
#define HEAP_COMMIT_GRANULARITY 0x1000000U
#endif
#endif
#define HEAP_MAGIC 0x68656170U /* 'heap' */
#define SMALL_CLASS_COUNT 13 /* 16 bytes .. 64 KB of payload */
#define LARGE_GRANULARITY 0x10000U

typedef char verify_heap_above_ce_tag_cache[PLATFORM_CE_TAG_CACHE_BASE + PLATFORM_CE_TAG_CACHE_SIZE +
	PLATFORM_CE_CONVERSION_ROOM_SIZE <= XBOX_HEAP_BASE ? 1 : -1];

#if defined(HALO_CUSTOM_EDITION) && !defined(HALO_WINDOW_TRANSLATED)
/* port: OpenSauce's maps (SPV3's) are linked to a tag cache 1.5 times Custom
Edition's, 0x40440000 to 0x426C0000, their structure BSPs at its top. With an
SPV3 install in the data folder (maps_spv3) at the first allocation, the heap
starts above that and the room for the port's conversions above it, and the
part of the window past Custom Edition's own is mapped here; an OpenSauce
map asking for its memory upgrades is read with it
(port/linux/game/opensauce_maps.c). On the builds that read those maps, an
OpenSauce maps folder does the same: maps_os with a map in it, or an
OpenSauce mod's own (maps_<pack> with its ui.yelo). Without one, nothing moves */
#include "posix.h"
#include "opensauce_maps.h"
#include <stdio.h>
#define OPENSAUCE_TAG_CACHE_TOP (PLATFORM_CE_TAG_CACHE_BASE + 0x02280000U)
#define OPENSAUCE_HEAP_BASE 0x42C00000U
typedef char verify_heap_above_opensauce_window[OPENSAUCE_TAG_CACHE_TOP + PLATFORM_CE_CONVERSION_ROOM_SIZE <=
	OPENSAUCE_HEAP_BASE ? 1 : -1];
const char *platform_data_root(void);

/* whether name starts with prefix (count characters of it, all when
count < 0), in any case */
static int heap_name_starts(const char *name, const char *prefix, int count)
{
	for (; count && *prefix; prefix++, name++, count--)
	{
		char a = *name >= 'A' && *name <= 'Z' ? (char)(*name + 32) : *name;
		char b = *prefix >= 'A' && *prefix <= 'Z' ? (char)(*prefix + 32) : *prefix;

		if (a != b)
			return 0;
	}
	return count >= 0 || !*name;
}
#define HEAP_IS_SPV3(name) heap_name_starts((name), "maps_spv3", -1)

/* the OpenSauce maps folder in the data root that has the window mapped,
into on_disk: maps_spv3 (SPV3's install), else maps_os or an OpenSauce mod's
folder on the builds that read their maps; FALSE if there is none */
static int heap_opensauce_folder(const char *root, char *on_disk, size_t size)
{
#ifdef OPENSAUCE_MAPS_MEMORY_UPGRADES
	void *directory;
	char name[256];
	int found = 0;
#endif

	if (posix_find_entry_case_insensitive(root, "maps_spv3", on_disk, size))
		return 1;
#ifdef OPENSAUCE_MAPS_MEMORY_UPGRADES
	/* (maps_os with a map in it: the folder Make the folders makes, empty,
	maps nothing) */
	if (posix_find_entry_case_insensitive(root, OPENSAUCE_MAPS_FOLDER, on_disk, size))
	{
		char path[1100];

		snprintf(path, sizeof(path), "%s/%s", root, on_disk);
		directory = posix_directory_open(path);
		while (!found && directory && posix_directory_next(directory, name, sizeof(name)))
		{
			size_t length = strlen(name);

			found = (length > 5 && heap_name_starts(name + length - 5, ".yelo", -1)) ||
				(length > 4 && heap_name_starts(name + length - 4, ".map", -1));
		}
		if (directory)
			posix_directory_close(directory);
		if (found)
			return 1;
	}
	directory = posix_directory_open(root);
	while (!found && directory && posix_directory_next(directory, name, sizeof(name)))
	{
		char path[1100], ui[64];

		if (!heap_name_starts(name, "maps_", 5))
			continue;
		snprintf(path, sizeof(path), "%s/%s", root, name);
		if (posix_find_entry_case_insensitive(path, "ui.yelo", ui, sizeof(ui)))
		{
			snprintf(on_disk, size, "%s", name);
			found = 1;
		}
	}
	if (directory)
		posix_directory_close(directory);
	return found;
#else
	return 0;
#endif
}
#endif
/* where the heap starts: XBOX_HEAP_BASE, or above OpenSauce's window */
static unsigned int heap_base = XBOX_HEAP_BASE;

struct heap_block
{
	unsigned long long capacity; /* payload bytes */
	unsigned int magic;
	unsigned int free;
};

struct free_node
{
	struct free_node *next;
};

static pthread_mutex_t heap_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned char *heap_next;
static unsigned char *heap_end;
/* the window the heap leaves alone (xbox_heap_reserve_window); 0: none */
static unsigned int heap_hole_address, heap_hole_size;
static struct free_node *small_free[SMALL_CLASS_COUNT];
static struct free_node *large_free;
#ifdef XBOX_HEAP_COMMIT_AS_USED
/* the end of the committed part */
static unsigned char *heap_committed;
#endif

static BOOL heap_initialize(void)
{
	void *base;

	if (heap_next)
		return TRUE;
#if defined(HALO_CUSTOM_EDITION) && !defined(HALO_WINDOW_TRANSLATED)
	{
		char on_disk[64];
		const char *root = platform_data_root();

		if (root && root[0] && heap_opensauce_folder(root, on_disk, sizeof(on_disk)))
		{
			void *window = xbox_pointer(XBOX_HEAP_BASE);

			if (mmap(window, OPENSAUCE_HEAP_BASE - XBOX_HEAP_BASE, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0) == window)
			{
				heap_base = OPENSAUCE_HEAP_BASE;
				opensauce_tag_window = 1;
				platform_log("%s: %s is in the data folder: OpenSauce's tag window 0x%08x to 0x%08x is "
					"mapped, the heap starts at 0x%08x", HEAP_IS_SPV3(on_disk) ? "SPV3" : "OpenSauce",
					on_disk, PLATFORM_CE_TAG_CACHE_BASE, OPENSAUCE_TAG_CACHE_TOP, heap_base);
			}
			else
				platform_log("%s: OpenSauce's tag window could not be mapped (%s): %s maps will not load",
					HEAP_IS_SPV3(on_disk) ? "SPV3" : "OpenSauce", strerror(errno),
					HEAP_IS_SPV3(on_disk) ? "SPV3's" : "OpenSauce's larger");
		}
	}
#endif
	base = xbox_pointer(heap_base);
#ifdef XBOX_HEAP_COMMIT_AS_USED
	heap_committed = base;
#else
	/* (no swap reserved: nearly 1 GB of address space, of which the game
	touches a small part; a machine with 1 GB of memory refuses it otherwise) */
	if (mmap(base, XBOX_HEAP_END - heap_base, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0) != base)
	{
		platform_log("cannot commit the Xbox heap (%s): the system refused %u MB of address space (a memory "
			"limit such as ulimit -v, or vm.overcommit_memory = 2)", strerror(errno),
			(XBOX_HEAP_END - heap_base) >> 20);
		abort();
	}
#endif
	heap_next = base;
	heap_end = xbox_pointer(XBOX_HEAP_END);
	return TRUE;
}

static int small_class(size_t size)
{
	int index = 0;

	while (index < SMALL_CLASS_COUNT && ((size_t)16 << index) < size)
		index++;
	return index; /* SMALL_CLASS_COUNT: a large block */
}

static struct heap_block *block_of(void *payload)
{
	return (struct heap_block *)payload - 1;
}

#ifdef XBOX_HEAP_COMMIT_AS_USED
/* commits the heap up to end, for a block from start, in steps of
HEAP_COMMIT_GRANULARITY: the game carves a few hundred MB, mostly at start-up
and map load, so this runs a few dozen times a run, never per allocation */
static BOOL heap_commit(unsigned char *start, unsigned char *end)
{
	unsigned char *limit = heap_end;
	size_t size;

	if (end <= heap_committed)
		return TRUE;
	/* (a block past an MCC map's window, carve's step over it: the window's
	pages are the map's, committed by it, and the heap never goes back below) */
	if (heap_committed < start)
		heap_committed = start;
	/* (and short of the window, when it lies ahead) */
	if (heap_hole_size && heap_committed < (unsigned char *)xbox_pointer(heap_hole_address))
		limit = xbox_pointer(heap_hole_address);
	size = (size_t)(end - heap_committed);
	size = (size + HEAP_COMMIT_GRANULARITY - 1) & ~(size_t)(HEAP_COMMIT_GRANULARITY - 1);
	if (size > (size_t)(limit - heap_committed))
		size = (size_t)(limit - heap_committed);
	if (mmap(heap_committed, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) !=
		heap_committed)
	{
#if defined(_WIN32) && !defined(HALO_WINDOW_TRANSLATED)
		platform_log("cannot commit %u MB more of the Xbox heap, %u MB committed so far (%s): Windows has no "
			"memory left to back it, counting the page file (close other programs, or let Windows manage the "
			"page file size)", (unsigned int)(size >> 20),
			(unsigned int)((size_t)(heap_committed - (unsigned char *)xbox_pointer(heap_base)) >> 20),
			strerror(errno));
#else
		platform_log("cannot commit %u more bytes of the Xbox heap (%s)", (unsigned int)size, strerror(errno));
#endif
		return FALSE;
	}
	heap_committed += size;
	return TRUE;
}

#endif
static struct heap_block *carve(size_t capacity)
{
	struct heap_block *block;

	/* (over an MCC map's tag window: xbox_heap_reserve_window) */
	if (heap_hole_size)
	{
		unsigned char *hole = xbox_pointer(heap_hole_address);

		if (heap_next < hole + heap_hole_size && (size_t)(hole - heap_next) < sizeof(*block) + capacity)
			heap_next = hole + heap_hole_size;
	}
	if ((size_t)(heap_end - heap_next) < sizeof(*block) + capacity)
	{
#ifdef HALO_CONSOLE_WINDOW_LOW
		/* (a console whose heap is smaller than the Xbox 360's: halt here,
		saying why, rather than leave the game a NULL it takes for a
		damaged map or a data array's assert much later) */
		platform_log("the Xbox heap is full: %u KB asked for, %u KB of %u KB used (the window starts at 0x%08x, "
			"HALO_CONSOLE_WINDOW_LOW)", (unsigned int)((sizeof(*block) + capacity) >> 10),
			(unsigned int)((size_t)(heap_next - (unsigned char *)xbox_pointer(heap_base)) >> 10),
			(unsigned int)((size_t)(heap_end - (unsigned char *)xbox_pointer(heap_base)) >> 10), XBOX_WINDOW_LOW);
		abort();
#endif
		return NULL;
	}
#ifdef XBOX_HEAP_COMMIT_AS_USED
	if (!heap_commit(heap_next, heap_next + sizeof(*block) + capacity))
		return NULL;
#endif
	block = (struct heap_block *)heap_next;
	heap_next += sizeof(*block) + capacity;
	block->capacity = capacity;
	block->magic = HEAP_MAGIC;
	return block;
}

#ifdef HALO_WINDOW_TRANSLATED
/* the bytes carved from the heap so far (its high-water mark: freed blocks
are reused, not returned), for a console's memory report */
size_t xbox_heap_used(void)
{
	return heap_next ? (size_t)(heap_next - (unsigned char *)xbox_pointer(heap_base)) : 0;
}

#endif
#ifndef HALO_WINDOW_TRANSLATED
/* port: MCC maps are linked to their own tag addresses, 0x50000000 for
MCC's tools, inside the heap's range: the heap leaves that window to them
(port/linux/src/mcc_memory.c), stepping over it as it grows, from the first
MCC map on for the rest of the run. FALSE when the heap has used the window
already, or another is held */
BOOL xbox_heap_reserve_window(unsigned int address, unsigned int size)
{
	BOOL result = FALSE;

	pthread_mutex_lock(&heap_lock);
	heap_initialize();
	if (heap_hole_size)
		result = heap_hole_address == address && heap_hole_size == size;
	else if (address >= heap_base && address <= XBOX_HEAP_END && size <= XBOX_HEAP_END - address &&
		heap_next <= (unsigned char *)xbox_pointer(address))
	{
		heap_hole_address = address;
		heap_hole_size = size;
		result = TRUE;
	}
	pthread_mutex_unlock(&heap_lock);
	return result;
}

#endif
BOOL xbox_heap_contains(const void *pointer)
{
#ifdef HALO_WINDOW_TRANSLATED
	unsigned int offset = (unsigned int)(uintptr_t)pointer - xbox_address_space_base;
#else
	unsigned long long offset = (unsigned long long)(uintptr_t)pointer - XBOX_ADDRESS_SPACE_BASE;
#endif

	return offset >= heap_base && offset < XBOX_HEAP_END;
}

void *xbox_heap_allocate(size_t size, BOOL zero)
{
	struct heap_block *block = NULL;
	int index = small_class(size ? size : 1);

	pthread_mutex_lock(&heap_lock);
	if (heap_initialize())
	{
		if (index < SMALL_CLASS_COUNT)
		{
			if (small_free[index])
			{
				block = block_of(small_free[index]);
				small_free[index] = small_free[index]->next;
			}
			else
			{
				block = carve((size_t)16 << index);
			}
		}
		else
		{
			size_t capacity = (size + LARGE_GRANULARITY - 1) & ~(size_t)(LARGE_GRANULARITY - 1);
			struct free_node **link;

			for (link = &large_free; *link; link = &(*link)->next)
			{
				if (block_of(*link)->capacity >= capacity)
				{
					block = block_of(*link);
					*link = (*link)->next;
					break;
				}
			}
			if (!block)
				block = carve(capacity);
		}
	}
	if (block)
		block->free = FALSE;
	pthread_mutex_unlock(&heap_lock);
	if (!block)
	{
		platform_log("Xbox heap exhausted (%lu bytes requested)", (unsigned long)size);
		return NULL;
	}
	if (zero)
		memset(block + 1, 0, size);
	return block + 1;
}

void xbox_heap_free(void *pointer)
{
	struct heap_block *block;
	struct free_node *node = pointer;
	int index;

	if (!pointer)
		return;
	block = block_of(pointer);
	/* (checked under the lock: two threads freeing the same block would
	both see it in use, and list it twice) */
	pthread_mutex_lock(&heap_lock);
	if (block->magic != HEAP_MAGIC || block->free)
	{
		pthread_mutex_unlock(&heap_lock);
		platform_log("Xbox heap: bad free of %p", pointer);
		return;
	}
	index = small_class(block->capacity);
	block->free = TRUE;
	if (index < SMALL_CLASS_COUNT && block->capacity == ((size_t)16 << index))
	{
		node->next = small_free[index];
		small_free[index] = node;
	}
	else
	{
		node->next = large_free;
		large_free = node;
	}
	pthread_mutex_unlock(&heap_lock);
}

size_t xbox_heap_capacity(void *pointer)
{
	return pointer ? (size_t)block_of(pointer)->capacity : 0;
}

#endif /* HALO_64BIT */
