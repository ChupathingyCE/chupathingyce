/*
WIN32_MEMORY_WATCH.C

Write tracking for guest memory that the renderer caches: the Windows version
of port/linux/src/memory_watch.c (see there for the design). The pages behind
a cached texture are made read-only; a vectored exception handler catches
the first write, records a new generation for the page and makes it
writable again.

In the 64-bit build (HALO_64BIT) the window is in the Xbox address space
(source/cseries/xbox_address.h): pages are named by Xbox address, as in
the 32-bit build, and host pointers are converted at the edges.
*/

#include <windows.h>

/* the Xbox memory window (port/linux/src/platform.h: the desktop builds') */
#define PLATFORM_CONTIGUOUS_BASE 0x80000000UL
#define PLATFORM_CONTIGUOUS_SIZE 0x20000000UL

#define WATCH_PAGE_SIZE 0x1000UL
#define WATCH_PAGE_COUNT (PLATFORM_CONTIGUOUS_SIZE / WATCH_PAGE_SIZE)

#ifdef HALO_64BIT
#include "../../../source/cseries/xbox_address.h"

/* the Xbox address of a host pointer, or 0 (outside the window) for one
outside the Xbox address space */
static unsigned long watch_address(const void *pointer)
{
	unsigned long long offset = (unsigned long long)(ULONG_PTR)pointer - XBOX_ADDRESS_SPACE_BASE;

	return offset < XBOX_ADDRESS_SPACE_SIZE ? (unsigned long)offset : 0;
}
#define WATCH_ADDRESS(pointer) watch_address(pointer)
#define WATCH_POINTER(address) xbox_pointer(address)
#else
#define WATCH_ADDRESS(pointer) ((unsigned long)(pointer))
#define WATCH_POINTER(address) ((void *)(address))
#endif

void platform_log(const char *format, ...);

static volatile unsigned char page_protected[WATCH_PAGE_COUNT];
static volatile LONG page_generation[WATCH_PAGE_COUNT];
static volatile LONG current_generation = 1;
static BOOL watch_active = FALSE;

static BOOL in_window(unsigned long address)
{
	return address >= PLATFORM_CONTIGUOUS_BASE && address - PLATFORM_CONTIGUOUS_BASE < PLATFORM_CONTIGUOUS_SIZE;
}

static unsigned long page_index(unsigned long address)
{
	return (address - PLATFORM_CONTIGUOUS_BASE) / WATCH_PAGE_SIZE;
}

static void mark_written(unsigned long page)
{
	DWORD previous;

	page_generation[page] = InterlockedIncrement(&current_generation);
	page_protected[page] = 0;
	VirtualProtect(WATCH_POINTER(PLATFORM_CONTIGUOUS_BASE + page * WATCH_PAGE_SIZE), WATCH_PAGE_SIZE,
		PAGE_READWRITE, &previous);
}

static LONG CALLBACK watch_handler(EXCEPTION_POINTERS *exception)
{
	EXCEPTION_RECORD *record = exception->ExceptionRecord;

	if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2 &&
		record->ExceptionInformation[0] == 1 /* a write */)
	{
		unsigned long address = WATCH_ADDRESS((void *)record->ExceptionInformation[1]);

		if (in_window(address) && page_protected[page_index(address)])
		{
			mark_written(page_index(address));
			return EXCEPTION_CONTINUE_EXECUTION;
		}
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

void memory_watch_initialize(void)
{
	if (watch_active)
		return;
	if (AddVectoredExceptionHandler(1, watch_handler))
		watch_active = TRUE;
}

void memory_watch_protect(unsigned long address, unsigned long size)
{
	unsigned long first, last, page;

	if (!watch_active || !size || !in_window(address))
		return;
	first = page_index(address);
	last = page_index(address + size - 1);
	if (last >= WATCH_PAGE_COUNT)
		last = WATCH_PAGE_COUNT - 1;
	for (page = first; page <= last; page++)
	{
		if (!page_protected[page])
		{
			DWORD previous;

			page_protected[page] = 1;
			VirtualProtect(WATCH_POINTER(PLATFORM_CONTIGUOUS_BASE + page * WATCH_PAGE_SIZE), WATCH_PAGE_SIZE,
				PAGE_READONLY, &previous);
		}
	}
}

unsigned long memory_watch_serial(void)
{
	return (unsigned long)current_generation;
}

unsigned long memory_watch_generation(unsigned long address, unsigned long size)
{
	unsigned long first, last, page, newest = 0;

	if (!size || !in_window(address))
		return 0;
	first = page_index(address);
	last = page_index(address + size - 1);
	if (last >= WATCH_PAGE_COUNT)
		last = WATCH_PAGE_COUNT - 1;
	for (page = first; page <= last; page++)
	{
		if ((unsigned long)page_generation[page] > newest)
			newest = (unsigned long)page_generation[page];
	}
	return newest;
}

void memory_watch_prepare_write(void *address, unsigned long size)
{
	unsigned long start = WATCH_ADDRESS(address);
	unsigned long first, last, page;

	if (!watch_active || !size)
		return;
	if (start + size <= PLATFORM_CONTIGUOUS_BASE || start >= PLATFORM_CONTIGUOUS_BASE + PLATFORM_CONTIGUOUS_SIZE)
		return;
	if (start < PLATFORM_CONTIGUOUS_BASE)
		start = PLATFORM_CONTIGUOUS_BASE;
	first = page_index(start);
	last = page_index(WATCH_ADDRESS(address) + size - 1);
	if (last >= WATCH_PAGE_COUNT)
		last = WATCH_PAGE_COUNT - 1;
	for (page = first; page <= last; page++)
	{
		if (page_protected[page])
			mark_written(page);
	}
}

void memory_watch_forget(void *address, unsigned long size)
{
	unsigned long start = WATCH_ADDRESS(address);
	unsigned long first, last, page;

	if (!size || !in_window(start))
		return;
	first = page_index(start);
	last = page_index(start + size - 1);
	if (last >= WATCH_PAGE_COUNT)
		last = WATCH_PAGE_COUNT - 1;
	for (page = first; page <= last; page++)
	{
		page_protected[page] = 0;
		page_generation[page] = InterlockedIncrement(&current_generation);
	}
}
