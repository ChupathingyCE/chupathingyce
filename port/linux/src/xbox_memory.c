/*
XBOX_MEMORY.C

#ifdef HALO_64BIT
The Xbox address space, Xbox contiguous memory (XPhysicalAlloc) and page
protection for the modern 64-bit build.
#else
Xbox contiguous memory (XPhysicalAlloc) and page protection for the Linux
build.
#endif

#ifdef HALO_64BIT
At start-up the layer reserves 4 GB at XBOX_ADDRESS_SPACE_BASE
(xbox_address.h; on Android, wherever the system has room), so Xbox address X is host address base + X. On the
Xbox, physical memory at address P is visible at virtual address
0x80000000 + P; the game asks for its game state and tag cache at fixed
#else
On the Xbox, physical memory at address P is visible at virtual address
0x80000000 + P. The game asks for its game state and tag cache at fixed
#endif
addresses that way (physical_memory_map.c), and Direct3D resources carry
#ifdef HALO_64BIT
physical addresses in their Data fields. That contiguous window is committed
read-write up front and handed out in 4 KB Xbox pages: placed requests at
exactly the address asked for, the rest top-down as the Xbox kernel does.

Host pages may be larger than the Xbox's (16 KB on Apple silicon), so page
protection is applied to the host pages a range covers completely, and a
freshly allocated block is remapped where it covers host pages completely
and cleared where it shares them.
#else
physical addresses in their Data fields. A 32-bit Linux process on a 64-bit
kernel owns the whole 4 GB address space, so the layer reserves the same
virtual window at start-up and allocates page-granular blocks inside it:
placed requests at exactly the address asked for, the rest top-down as the
Xbox kernel does.
#endif
*/

#include "platform.h"

#include <errno.h>
#ifdef HALO_ANDROID
#include <stdio.h>
#endif
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifdef HALO_64BIT
#define PAGE_SIZE_BYTES 0x1000U
#else
#define PAGE_SIZE_BYTES 0x1000UL
#endif
#define CONTIGUOUS_PAGE_COUNT (PLATFORM_CONTIGUOUS_SIZE / PAGE_SIZE_BYTES)

/* per page: 0 free, otherwise the protection of the block (PAGE_*); the
first page of a block also records the block length. As many as the largest
window has (platform.h); CONTIGUOUS_PAGE_COUNT is this one's */
static DWORD page_protection[PLATFORM_CONTIGUOUS_MAXIMUM / PAGE_SIZE_BYTES];
static unsigned long block_page_count[PLATFORM_CONTIGUOUS_MAXIMUM / PAGE_SIZE_BYTES];
static BOOL arena_reserved = FALSE;
static pthread_mutex_t arena_lock = PTHREAD_MUTEX_INITIALIZER;
#ifdef HALO_CUSTOM_EDITION
int platform_ce_tag_cache_ready = FALSE;
unsigned int platform_ce_conversion_room = 0;
#endif
#ifdef HALO_WINDOW_REBASE
/* where the window is and how large (cseries/xbox_address.h): the linked
address until the guest's runtime is told the host's, before any
constructor runs (port/android/guest/runtime/guest_start.c), or the Linux
build reads HALO_WINDOW_BASE (below) */
unsigned long platform_contiguous_base = 0x80000000UL;
unsigned long platform_contiguous_size = PLATFORM_CONTIGUOUS_MINIMUM;
#endif
#ifdef HALO_64BIT

unsigned int platform_host_page_size = PAGE_SIZE_BYTES;
#endif
#if defined(HALO_64BIT) && defined(HALO_ANDROID)
/* where the Xbox address space is (xbox_address.h) */
unsigned long long xbox_address_space_base;
/* why the Xbox address space could not be reserved, or empty: an Android
app's library is loaded into a process the system has laid out already,
and the app tells the player (port/android/native64/android64_main.c) */
char platform_xbox_address_space_error[160];
#endif

static int protection_to_host(DWORD protect)
{
	switch (protect & 0xff)
	{
	case PAGE_NOACCESS: return PROT_NONE;
	case PAGE_READONLY: return PROT_READ;
	case PAGE_EXECUTE: return PROT_EXEC;
	case PAGE_EXECUTE_READ: return PROT_READ | PROT_EXEC;
	case PAGE_EXECUTE_READWRITE: return PROT_READ | PROT_WRITE | PROT_EXEC;
	default: return PROT_READ | PROT_WRITE;
	}
}

/* Reserve the window before anything else can map into it. */
#ifdef HALO_WINDOW_TRANSLATED
/* (the console's window is wherever it has room: platform.h) */
__attribute__((constructor(101)))
static void xbox_address_space_reserve(void)
{
	void *window;

	if (arena_reserved)
		return;
	window = platform_xbox_window_reserve(XBOX_WINDOW_SIZE);
	if (!window)
	{
		platform_log("cannot reserve the Xbox window (%u MB)", (unsigned int)(XBOX_WINDOW_SIZE >> 20));
		abort();
	}
	xbox_address_space_base = (unsigned int)(uintptr_t)window - XBOX_WINDOW_LOW;
	if (mmap(xbox_pointer(PLATFORM_CONTIGUOUS_BASE), PLATFORM_CONTIGUOUS_SIZE, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != xbox_pointer(PLATFORM_CONTIGUOUS_BASE))
	{
		platform_log("cannot commit the Xbox contiguous memory window (%s)", strerror(errno));
		abort();
	}
	platform_log("Xbox window: Xbox addresses 0x%08x to 0x%08x at host 0x%08x (contiguous memory committed: %u MB)",
		XBOX_WINDOW_LOW, XBOX_WINDOW_HIGH, (unsigned int)(uintptr_t)window,
		(unsigned int)(PLATFORM_CONTIGUOUS_SIZE >> 20));
	arena_reserved = TRUE;
}

void xbox_address_out_of_range(void const *pointer)
{
	platform_log("pointer %p is outside the Xbox window and has no Xbox address", pointer);
	abort();
}

void platform_xbox_window_ensure(void)
{
	xbox_address_space_reserve();
}
#else
__attribute__((constructor(101)))
#ifdef HALO_64BIT
static void xbox_address_space_reserve(void)
#else
static void contiguous_arena_reserve(void)
#endif
{
#ifdef HALO_64BIT
	void *wanted = (void *)XBOX_ADDRESS_SPACE_BASE;
	void *result;
	long host_page_size = sysconf(_SC_PAGESIZE);
#else
#if defined(HALO_WINDOW_REBASE) && !defined(HALO_ANDROID)
	/* the Linux build's test of a moved window: HALO_WINDOW_BASE, in hex,
	on a whole number of the window's size; unset, the linked address */
	const char *setting = getenv("HALO_WINDOW_BASE");

	if (setting && *setting)
	{
		unsigned long base = strtoul(setting, NULL, 16);

		if (base && !(base % PLATFORM_CONTIGUOUS_SIZE) && base <= 0xffffffffUL - PLATFORM_CONTIGUOUS_SIZE + 1)
			platform_contiguous_base = base;
		else
			platform_log("HALO_WINDOW_BASE=%s is not a multiple of %#lx: the window stays at %#lx", setting,
				(unsigned long)PLATFORM_CONTIGUOUS_SIZE, platform_contiguous_base);
	}
#endif
#ifdef HALO_WINDOW_REBASE
	if (platform_contiguous_size < PLATFORM_CONTIGUOUS_MINIMUM)
	{
		platform_log("the host's window is %lu MB at %#lx; this build needs %lu MB", platform_contiguous_size >> 20,
			platform_contiguous_base, (unsigned long)PLATFORM_CONTIGUOUS_MINIMUM >> 20);
		abort();
	}
	/* (a larger window than the tables are for: the rest goes unused) */
	if (platform_contiguous_size > PLATFORM_CONTIGUOUS_MAXIMUM)
	{
		platform_log("the host's window is %lu MB; this build uses %lu MB of it", platform_contiguous_size >> 20,
			(unsigned long)PLATFORM_CONTIGUOUS_MAXIMUM >> 20);
		platform_contiguous_size = PLATFORM_CONTIGUOUS_MAXIMUM;
	}
	platform_log("contiguous window at %08lx, %lu MB", platform_contiguous_base, platform_contiguous_size >> 20);
#endif
	void *wanted = (void *)PLATFORM_CONTIGUOUS_BASE;
	void *result = mmap(wanted, PLATFORM_CONTIGUOUS_SIZE, PROT_NONE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
#endif

#ifdef HALO_64BIT
	if (host_page_size > 0)
		platform_host_page_size = (unsigned int)host_page_size;
#ifdef HALO_ANDROID
	/* anywhere the system has room (xbox_address.h): twice the size,
	of which the 4 GB-aligned part is kept, so that Xbox address X is host
	address base + X with X in the low 32 bits, as at the desktop's fixed
	addresses */
	result = mmap(NULL, 2 * XBOX_ADDRESS_SPACE_SIZE, PROT_NONE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (result != MAP_FAILED)
	{
		uintptr_t start = (uintptr_t)result;
		uintptr_t end = start + 2 * XBOX_ADDRESS_SPACE_SIZE;
		uintptr_t base = (start + XBOX_ADDRESS_SPACE_SIZE - 1) & ~(uintptr_t)(XBOX_ADDRESS_SPACE_SIZE - 1);

		if (base > start)
			munmap(result, base - start);
		if (base + XBOX_ADDRESS_SPACE_SIZE < end)
			munmap((void *)(base + XBOX_ADDRESS_SPACE_SIZE), end - (base + XBOX_ADDRESS_SPACE_SIZE));
		xbox_address_space_base = base;
		wanted = result = (void *)base;
	}
#else
	result = mmap(wanted, XBOX_ADDRESS_SPACE_SIZE, PROT_NONE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
#endif
	if (result != wanted)
#else
	if (result == wanted)
	{
		arena_reserved = TRUE;
	}
	else
#endif
	{
#ifdef HALO_64BIT
		const char *reason = result == MAP_FAILED ? strerror(errno) :
			"the system put it elsewhere: its address space is smaller than this build needs";

		if (result != MAP_FAILED)
			munmap(result, XBOX_ADDRESS_SPACE_SIZE);
		platform_log("cannot reserve the Xbox address space at %p (%s)", wanted, reason);
#ifdef HALO_ANDROID
		snprintf(platform_xbox_address_space_error, sizeof(platform_xbox_address_space_error), "%s", reason);
		return;
#else
		abort();
#endif
#else
		if (result != MAP_FAILED)
			munmap(result, PLATFORM_CONTIGUOUS_SIZE);
		platform_log("cannot reserve the Xbox contiguous memory window at %p (%s)",
			wanted, strerror(errno));
#endif
	}
#if defined(HALO_CUSTOM_EDITION) && !defined(HALO_64BIT)
	/* Custom Edition maps' tag cache (platform.h). Xbox addresses are host
	addresses here, so it is the host's 0x40440000 to 0x41b40000, which a
	32-bit process leaves free: the executable is linked at 0x08048000, its
	break heap grows up from just past it, and the libraries, mmap'd blocks
	and the stack are at the top below 0xc0000000 or 0xffffffff, growing
	down. Taken here, before anything else is mapped, never in place of a
	mapping (MAP_FIXED_NOREPLACE); backed on first touch. With the room
	above it if that is free too, else without */
	result = mmap((void *)PLATFORM_CE_TAG_CACHE_BASE, PLATFORM_CE_TAG_CACHE_SIZE + PLATFORM_CE_CONVERSION_ROOM_SIZE,
		PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
	if (result == (void *)PLATFORM_CE_TAG_CACHE_BASE)
	{
		platform_ce_conversion_room = PLATFORM_CE_CONVERSION_ROOM_SIZE;
	}
	else
	{
		if (result != MAP_FAILED)
			munmap(result, PLATFORM_CE_TAG_CACHE_SIZE + PLATFORM_CE_CONVERSION_ROOM_SIZE);
		result = mmap((void *)PLATFORM_CE_TAG_CACHE_BASE, PLATFORM_CE_TAG_CACHE_SIZE, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
	}
	if (result != (void *)PLATFORM_CE_TAG_CACHE_BASE)
	{
		if (result != MAP_FAILED)
			munmap(result, PLATFORM_CE_TAG_CACHE_SIZE);
		platform_log("cannot reserve Custom Edition maps' tag cache at %p (%s): they will not load",
			(void *)PLATFORM_CE_TAG_CACHE_BASE, strerror(errno));
	}
	else
	{
		platform_ce_tag_cache_ready = TRUE;
	}
#endif
#ifdef HALO_64BIT
	/* (no swap reserved for these, as for the heap, xbox_heap.c: pages are
	backed when touched, and a small machine refuses the whole window
	otherwise) */
	if (mmap(xbox_pointer(PLATFORM_CONTIGUOUS_BASE), PLATFORM_CONTIGUOUS_SIZE, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0) != xbox_pointer(PLATFORM_CONTIGUOUS_BASE))
	{
		platform_log("cannot commit the Xbox contiguous memory window (%s)", strerror(errno));
		abort();
	}
	/* Custom Edition maps' tag cache, and the room above it (platform.h) */
	if (mmap(xbox_pointer(PLATFORM_CE_TAG_CACHE_BASE), PLATFORM_CE_TAG_CACHE_SIZE + PLATFORM_CE_CONVERSION_ROOM_SIZE,
		PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0) !=
		xbox_pointer(PLATFORM_CE_TAG_CACHE_BASE))
	{
		platform_log("cannot commit Custom Edition maps' tag cache (%s)", strerror(errno));
	}
	else
	{
		platform_ce_tag_cache_ready = TRUE;
		platform_ce_conversion_room = PLATFORM_CE_CONVERSION_ROOM_SIZE;
	}
	arena_reserved = TRUE;
}

void xbox_address_out_of_range(void const *pointer)
{
	platform_log("pointer %p is outside the Xbox address space and cannot be stored in 32 bits", pointer);
	abort();
#endif
}
#endif /* HALO_WINDOW_TRANSLATED */

BOOL platform_is_contiguous(const void *address)
{
#ifdef HALO_64BIT
	unsigned long long offset = PLATFORM_XBOX_OFFSET(address);
#else
	unsigned long value = (unsigned long)address;
#endif

#ifdef HALO_64BIT
	return offset >= PLATFORM_CONTIGUOUS_BASE && offset - PLATFORM_CONTIGUOUS_BASE < PLATFORM_CONTIGUOUS_SIZE;
#else
	return value >= PLATFORM_CONTIGUOUS_BASE && value - PLATFORM_CONTIGUOUS_BASE < PLATFORM_CONTIGUOUS_SIZE;
#endif
}

#ifdef HALO_64BIT
static unsigned int contiguous_page(const void *address)
#else
static BOOL pages_free(unsigned long first, unsigned long count)
#endif
{
#ifdef HALO_64BIT
	return (xbox_address(address) - PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES;
}

/* Change the host protection of the host pages that [address, address +
size) covers completely; returns FALSE (with errno) on failure. */
static BOOL protect_host_pages(void *address, size_t size, int protection)
{
	uintptr_t mask = platform_host_page_size - 1;
	uintptr_t start = ((uintptr_t)address + mask) & ~mask;
	uintptr_t end = ((uintptr_t)address + size) & ~mask;

	if (end <= start)
		return TRUE;
	return mprotect((void *)start, end - start, protection) == 0;
}

/* The host protection a host page should have: that of its Xbox pages when
they all agree, read-write otherwise (free pages are read-write). */
static int host_page_protection(uintptr_t host_page)
{
	unsigned int first = contiguous_page((void *)host_page);
	unsigned int count = platform_host_page_size / PAGE_SIZE_BYTES;
	DWORD protect = page_protection[first];
	unsigned int page;

	for (page = first + 1; page < first + count && page < CONTIGUOUS_PAGE_COUNT; page++)
	{
		if (page_protection[page] != protect)
			return PROT_READ | PROT_WRITE;
	}
	return protect ? protection_to_host(protect) : PROT_READ | PROT_WRITE;
}

/* Give every host page that [address, address + size) touches the
protection host_page_protection says. A block allocated or freed takes its
host pages back from the memory watch, which leaves them as it last made
them: read-only where the renderer watched a texture there, even a host page
the block only shares with its neighbour. */
static void reprotect_host_pages(void *address, size_t size)
{
	uintptr_t mask = platform_host_page_size - 1;
	uintptr_t start = (uintptr_t)address & ~mask;
	uintptr_t end = ((uintptr_t)address + size + mask) & ~mask;
	uintptr_t host_page;

	for (host_page = start; host_page < end; host_page += platform_host_page_size)
		mprotect((void *)host_page, platform_host_page_size, host_page_protection(host_page));
}

/* Zero a new block. The host pages it covers completely are mapped afresh,
which zeroes them without touching them: the memory is taken only once the
game writes there (a 128 MB texture cache that a map fills a fifth of, or a
dedicated server's, which draws nothing). Those pages are free until the
block takes them, so read-write, as a fresh mapping is. The host pages it
shares with a neighbour are cleared. */
static void clear_block(void *address, size_t size)
{
	uintptr_t mask = platform_host_page_size - 1;
	uintptr_t start = ((uintptr_t)address + mask) & ~mask;
	uintptr_t end = ((uintptr_t)address + size) & ~mask;

	if (end <= start || mmap((void *)start, end - start, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0) != (void *)start)
	{
		memset(address, 0, size);
		return;
	}
	memset(address, 0, start - (uintptr_t)address);
	memset((void *)end, 0, (uintptr_t)address + size - end);
}

static BOOL pages_free(unsigned int first, unsigned int count)
{
	unsigned int page;
#else
	unsigned long page;
#endif

	if (first + count > CONTIGUOUS_PAGE_COUNT)
		return FALSE;
	for (page = first; page < first + count; page++)
	{
		if (page_protection[page])
			return FALSE;
	}
	return TRUE;
}

void *platform_contiguous_alloc(unsigned long size, unsigned long alignment,
	unsigned long physical_address, DWORD protect)
{
	unsigned long count = (size + PAGE_SIZE_BYTES - 1) / PAGE_SIZE_BYTES;
	unsigned long alignment_pages = alignment > PAGE_SIZE_BYTES ? alignment / PAGE_SIZE_BYTES : 1;
	unsigned long first = CONTIGUOUS_PAGE_COUNT;
	unsigned long page;
	void *address;

	if (!count)
		count = 1;
	protect &= ~(PAGE_WRITECOMBINE | PAGE_NOCACHE);
	if (!protect)
		protect = PAGE_READWRITE;

	pthread_mutex_lock(&arena_lock);
	if (!arena_reserved)
	{
		pthread_mutex_unlock(&arena_lock);
		return NULL;
	}
	if (physical_address != PLATFORM_ANY_PHYSICAL_ADDRESS)
	{
		unsigned long wanted = (physical_address & ~PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES;

		if (pages_free(wanted, count))
			first = wanted;
	}
	else if (count <= CONTIGUOUS_PAGE_COUNT)
	{
		/* top-down first fit, like the Xbox contiguous allocator */
		unsigned long candidate = CONTIGUOUS_PAGE_COUNT - count;

		for (;;)
		{
			candidate -= candidate % alignment_pages;
			if (pages_free(candidate, count))
			{
				first = candidate;
				break;
			}
			if (candidate == 0)
				break;
			candidate--;
		}
	}
	if (first == CONTIGUOUS_PAGE_COUNT)
	{
		pthread_mutex_unlock(&arena_lock);
		return NULL;
	}

#ifdef HALO_64BIT
	address = xbox_pointer(PLATFORM_CONTIGUOUS_BASE + first * PAGE_SIZE_BYTES);
	/* (a host page it shares with a texture the renderer watches is
	read-only: made writable, as a write would make it, before the watch
	forgets it; else zeroing the block faults where no watch is left to
	take the fault) */
	memory_watch_prepare_write(address, count * PAGE_SIZE_BYTES);
#else
	address = (void *)(PLATFORM_CONTIGUOUS_BASE + first * PAGE_SIZE_BYTES);
#endif
	memory_watch_forget(address, count * PAGE_SIZE_BYTES);
#ifdef HALO_64BIT
	/* a block starts out zeroed (its pages still free, so read-write), then
	takes its protection */
	reprotect_host_pages(address, count * PAGE_SIZE_BYTES);
	clear_block(address, count * PAGE_SIZE_BYTES);
#else
	/* map fresh zeroed pages over the reservation */
	if (mmap(address, count * PAGE_SIZE_BYTES, protection_to_host(protect),
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != address)
	{
		pthread_mutex_unlock(&arena_lock);
		return NULL;
	}
#endif
	for (page = first; page < first + count; page++)
		page_protection[page] = protect;
	block_page_count[first] = count;
#ifdef HALO_64BIT
	if (protection_to_host(protect) != (PROT_READ | PROT_WRITE))
		reprotect_host_pages(address, count * PAGE_SIZE_BYTES);
#endif
	pthread_mutex_unlock(&arena_lock);
	return address;
}

#if defined(HALO_CONSOLE_GPU_RESOURCE_HOOKS) || defined(HALO_CONSOLE_NO_WRITE_WATCH)
/* the length of the block address is the start of, or 0 */
unsigned long platform_contiguous_block_size(const void *address)
{
	unsigned long first, count = 0;

	if (!platform_is_contiguous(address))
		return 0;
#ifdef HALO_64BIT
	first = contiguous_page(address);
#else
	first = ((unsigned long)address - PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES;
#endif
	pthread_mutex_lock(&arena_lock);
	count = block_page_count[first];
	pthread_mutex_unlock(&arena_lock);
	return count * PAGE_SIZE_BYTES;
}
#endif

void platform_contiguous_free(void *address)
{
	unsigned long first, count, page;

	if (!platform_is_contiguous(address))
		return;
#ifdef HALO_64BIT
	first = contiguous_page(address);
#else
	first = ((unsigned long)address - PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES;
#endif
	pthread_mutex_lock(&arena_lock);
	count = block_page_count[first];
	if (count)
	{
		memory_watch_forget(address, count * PAGE_SIZE_BYTES);
#ifndef HALO_64BIT
		mmap(address, count * PAGE_SIZE_BYTES, PROT_NONE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
#endif
		for (page = first; page < first + count; page++)
			page_protection[page] = 0;
		block_page_count[first] = 0;
#ifdef HALO_64BIT
		reprotect_host_pages(address, count * PAGE_SIZE_BYTES);
	}
	pthread_mutex_unlock(&arena_lock);
}

/* Write into contiguous memory the way the Xbox's DVD and hard disk do:
by DMA, regardless of the CPU's page protection (the game guards its read
buffers PAGE_READONLY while the drive fills them). */
void platform_contiguous_write(void *destination, const void *source, size_t size)
{
	uintptr_t mask = platform_host_page_size - 1;
	uintptr_t start = (uintptr_t)destination & ~mask;
	uintptr_t end = ((uintptr_t)destination + size + mask) & ~mask;
	uintptr_t page;

	if (!size)
		return;
	memory_watch_prepare_write(destination, (unsigned int)size);
	pthread_mutex_lock(&arena_lock);
	mprotect((void *)start, end - start, PROT_READ | PROT_WRITE);
	memcpy(destination, source, size);
	for (page = start; page < end; page += platform_host_page_size)
	{
		int protection = host_page_protection(page);

		if (protection != (PROT_READ | PROT_WRITE))
			mprotect((void *)page, platform_host_page_size, protection);
#endif
	}
	pthread_mutex_unlock(&arena_lock);
}

/* ---------- XAPI */

LPVOID WINAPI XPhysicalAlloc(SIZE_T size, ULONG_PTR physical_address, ULONG_PTR alignment, DWORD protect)
{
	/* A highest-acceptable address inside the window places the block
	there, which is how the game gets its fixed game state and tag cache
	addresses; anything else may go anywhere. */
	void *result = platform_contiguous_alloc(size, alignment,
		physical_address < PLATFORM_CONTIGUOUS_SIZE ? physical_address : PLATFORM_ANY_PHYSICAL_ADDRESS,
		protect);

	if (!result)
	{
#ifdef HALO_64BIT
		platform_log("XPhysicalAlloc: cannot allocate %u bytes (physical address 0x%08x)",
			(unsigned int)size, (unsigned int)physical_address);
#else
		platform_log("XPhysicalAlloc: cannot allocate %lu bytes (physical address 0x%08lx)",
			(unsigned long)size, (unsigned long)physical_address);
#endif
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
	}
	return result;
}

VOID WINAPI XPhysicalFree(LPVOID address)
{
	platform_contiguous_free(address);
}

#ifdef HALO_64BIT
/* Protection is per host page, which holds several Xbox pages on Apple
silicon (16 KB): every host page the range touches takes the protection its
Xbox pages share, or read-write where they differ. So a few bytes made
writable in a read-only block (bink_alloc_permanent) are writable. */
BOOL WINAPI VirtualProtect(LPVOID address, SIZE_T size, DWORD new_protect, PDWORD old_protect)
{
	uintptr_t mask = platform_host_page_size - 1;
	uintptr_t first = (uintptr_t)address & ~mask;
	uintptr_t end = ((uintptr_t)address + size + mask) & ~mask;
	uintptr_t host_page;
	unsigned int page, last;

	if (!platform_is_contiguous(address))
	{
		/* outside the Xbox's pages: the host's own pages */
		if (old_protect)
			*old_protect = PAGE_READWRITE;
		memory_watch_forget(address, size);
		if (!protect_host_pages(address, size, protection_to_host(new_protect)))
		{
			platform_set_last_error_from_errno(errno);
			return FALSE;
		}
		return TRUE;
	}
	pthread_mutex_lock(&arena_lock);
	if (old_protect)
		*old_protect = page_protection[contiguous_page(address)];
	last = contiguous_page((char *)address + (size ? size - 1 : 0));
	for (page = contiguous_page(address); page <= last && page < CONTIGUOUS_PAGE_COUNT; page++)
	{
		if (page_protection[page])
			page_protection[page] = new_protect & ~(PAGE_WRITECOMBINE | PAGE_NOCACHE);
	}
	/* (the whole host pages: their protection changes, and with it what the
	memory watch sees) */
	memory_watch_forget((void *)first, (unsigned int)(end - first));
	for (host_page = first; host_page < end; host_page += platform_host_page_size)
	{
		if (mprotect((void *)host_page, platform_host_page_size, host_page_protection(host_page)) != 0)
		{
			pthread_mutex_unlock(&arena_lock);
			platform_set_last_error_from_errno(errno);
			return FALSE;
		}
	}
	pthread_mutex_unlock(&arena_lock);
	return TRUE;
}
#else
BOOL WINAPI VirtualProtect(LPVOID address, SIZE_T size, DWORD new_protect, PDWORD old_protect)
{
	unsigned long start = (unsigned long)address & ~(PAGE_SIZE_BYTES - 1);
	unsigned long end = ((unsigned long)address + size + PAGE_SIZE_BYTES - 1) & ~(PAGE_SIZE_BYTES - 1);

	if (old_protect)
		*old_protect = platform_is_contiguous(address) ?
			page_protection[(start - PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES] : PAGE_READWRITE;
	memory_watch_forget((void *)start, end - start);
	if (mprotect((void *)start, end - start, protection_to_host(new_protect)) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return FALSE;
	}
	if (platform_is_contiguous((void *)start))
	{
		unsigned long page;

		pthread_mutex_lock(&arena_lock);
		for (page = (start - PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES;
			page < (end - PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES && page < CONTIGUOUS_PAGE_COUNT;
			page++)
		{
			if (page_protection[page])
				page_protection[page] = new_protect & ~(PAGE_WRITECOMBINE | PAGE_NOCACHE);
		}
		pthread_mutex_unlock(&arena_lock);
	}
	return TRUE;
}
#endif

VOID WINAPI XPhysicalProtect(LPVOID address, SIZE_T size, DWORD new_protect)
{
	VirtualProtect(address, size, new_protect, NULL);
}

DWORD WINAPI XQueryMemoryProtect(LPVOID address)
{
	DWORD protect = PAGE_READWRITE;

	if (platform_is_contiguous(address))
	{
		pthread_mutex_lock(&arena_lock);
#ifdef HALO_64BIT
		protect = page_protection[contiguous_page(address)];
#else
		protect = page_protection[((unsigned long)address - PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES];
#endif
		pthread_mutex_unlock(&arena_lock);
		if (!protect)
			protect = PAGE_NOACCESS;
	}
	return protect;
}
