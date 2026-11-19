"""The Xbox heap (port/linux/src/xbox_heap.c) commits its range as it grows
where a read-write mapping is committed memory (64-bit Windows, the
translated-window consoles), in large steps, and never over an MCC map's
window; elsewhere it maps the whole range once, as before (#1462).

The heap is built on its own over a real reservation of the host, with mmap
counted and, on request, refused."""
from pathlib import Path
import shutil
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[1]

def implementation():
	text = (ROOT / "port/linux/src/xbox_heap.c").read_text()
	return "\n".join(line for line in text.splitlines() if not line.startswith("#include"))


PRELUDE = r'''
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define HALO_64BIT 1
#define PLATFORM_CE_TAG_CACHE_BASE 0x40000000U
#define PLATFORM_CE_TAG_CACHE_SIZE 0x01000000U
#define PLATFORM_CE_CONVERSION_ROOM_SIZE 0x01000000U

/* the Xbox address space: the heap's part of it, reserved for real */
static unsigned char *space;
#define XBOX_ADDRESS_SPACE_BASE ((unsigned long long)(uintptr_t)space)
static void *xbox_pointer(unsigned int address)
{
	return address ? space + address : NULL;
}

static int commit_logged;
static void platform_log(const char *format, ...)
{
	char line[512];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	if (strstr(line, "cannot commit"))
		commit_logged++;
}

/* each mapping the heap makes, by Xbox address */
static int maps, refuse;
static unsigned long long mapped_begin[256], mapped_end[256];
static void *counted_mmap(void *address, size_t length, int protection, int flags, int descriptor, off_t offset)
{
	unsigned long long begin = (unsigned long long)((unsigned char *)address - space);

	if (maps < 256)
	{
		mapped_begin[maps] = begin;
		mapped_end[maps] = begin + length;
	}
	maps++;
	if (!(flags & MAP_FIXED) || !(protection & PROT_WRITE))
		return MAP_FAILED;
	if (refuse)
	{
		errno = ENOMEM;
		return MAP_FAILED;
	}
	return mmap(address, length, protection, flags, descriptor, offset);
}
#define mmap counted_mmap
'''

MAIN = r'''
#undef mmap
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); return 1; } } while (0)
#define WINDOW 0x50800000U

int main(void)
{
	unsigned char *reservation;
	unsigned char *pointers[64];
	unsigned char *window, *large, *after;
	int index, before;

	/* the heap's range and the tag cache below it, address space only */
	reservation = mmap(NULL, 0x7f000000U - 0x40000000U, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	CHECK(reservation != MAP_FAILED);
	space = reservation - 0x40000000U;

	pointers[0] = xbox_heap_allocate(100, TRUE);
	CHECK(pointers[0]);
	CHECK(xbox_heap_contains(pointers[0]));
#ifndef XBOX_HEAP_COMMIT_AS_USED
	/* the whole range at once, as ever */
	CHECK(maps == 1);
	CHECK(mapped_begin[0] == 0x42000000U && mapped_end[0] == 0x7f000000U);
	return 0;
#else
	/* one step, not the range */
	CHECK(maps == 1);
	CHECK(mapped_begin[0] == 0x42000000U && mapped_end[0] == 0x42000000U + HEAP_COMMIT_GRANULARITY);
	/* many small blocks inside it: no more mappings */
	for (index = 1; index < 64; index++)
	{
		pointers[index] = xbox_heap_allocate(4096, FALSE);
		CHECK(pointers[index]);
		memset(pointers[index], index, 4096);
	}
	CHECK(maps == 1);

	/* an MCC map's window, taken as the map does it (off the steps' bounds,
	as a map linked elsewhere would be): the heap commits up to it and steps
	over it, and never maps a page of it again */
	CHECK(xbox_heap_reserve_window(WINDOW, 0x04000000U));
	window = xbox_pointer(WINDOW);
	CHECK(mmap(window, 0x04000000U, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == window);
	window[0] = 0x5a;
	window[0x03ffffff] = 0xa5;
	before = maps;
	large = NULL;
	while ((unsigned long long)(after = xbox_heap_allocate(0x10000, FALSE)) - (unsigned long long)(uintptr_t)space
		< WINDOW)
	{
		CHECK(after);
		after[0] = after[0xffff] = 1;
		large = after;
	}
	CHECK(large && after);
	/* (the last block before the window ends close to it) */
	CHECK((unsigned long long)(large - space) + 2 * 0x10010 > WINDOW);
	CHECK((unsigned long long)(after - space) >= WINDOW + 0x04000000U);
	after[0] = after[0xffff] = 2;
	for (index = 0; index < maps && index < 256; index++)
		CHECK(mapped_end[index] <= WINDOW || mapped_begin[index] >= WINDOW + 0x04000000U);
	CHECK(maps > before);
	CHECK(window[0] == 0x5a && window[0x03ffffff] == 0xa5);
	/* the first blocks kept what they held */
	for (index = 1; index < 64; index++)
		CHECK(pointers[index][0] == index && pointers[index][4095] == index);

	/* a refused commit: no block and a reason, then on again */
	refuse = 1;
	before = maps;
	CHECK(!xbox_heap_allocate(64U << 20, FALSE));
	CHECK(maps == before + 1);
	CHECK(commit_logged == 1);
	refuse = 0;
	after = xbox_heap_allocate(64U << 20, FALSE);
	CHECK(after);
	memset(after, 3, 64U << 20);
	/* up to the end of the range, and never past it */
	while (xbox_heap_allocate(64U << 20, FALSE))
		;
	while (xbox_heap_allocate(0x10000, FALSE))
		;
	CHECK(heap_next <= heap_committed && heap_committed == heap_end);
	CHECK(heap_end == (unsigned char *)xbox_pointer(0x7f000000U));
	return 0;
#endif
}
'''


def build(tmp_path, defines):
	clang = shutil.which("clang") or shutil.which("cc")
	if not clang:
		pytest.skip("a C compiler is required")
	source = tmp_path / "heap.c"
	source.write_text(PRELUDE + "\n".join(f"#define {d}" for d in defines) + "\n" + implementation() + MAIN)
	program = tmp_path / "heap"
	result = subprocess.run([clang, "-std=gnu11", "-D_DEFAULT_SOURCE", "-D_DARWIN_C_SOURCE", "-Wall", "-Werror",
		"-Wno-unused-function", "-Wno-unused-variable", "-g", str(source), "-lpthread", "-o", str(program)],
		capture_output=True, text=True)
	assert result.returncode == 0, result.stderr
	return program


@pytest.mark.skipif(sys.platform == "win32", reason="the host's mmap")
@pytest.mark.parametrize("defines", [[], ["XBOX_HEAP_COMMIT_AS_USED", "HEAP_COMMIT_GRANULARITY 0x1000000U"]],
	ids=["mapped-once", "commit-as-used"])
def test_heap_mapping(tmp_path, defines):
	program = build(tmp_path, defines)
	result = subprocess.run([str(program)], capture_output=True, text=True)
	assert result.returncode == 0, result.stderr


def test_windows_commits_as_used():
	"""The 64-bit Windows build takes the commit-as-used path, and says what
	to do about the page file when Windows refuses."""
	text = (ROOT / "port/linux/src/xbox_heap.c").read_text()
	windows = text.index("#ifdef _WIN32")
	assert text.index("#define XBOX_HEAP_COMMIT_AS_USED", windows) < text.index("#endif", windows)
	assert "page file" in text
	assert "ulimit" in text[text.index("#else", text.index("heap_committed = base;")):]
