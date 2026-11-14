/* ---------- MOD_SOUND_STUBS.C (appended to mods.c by the mod_sound fuzz target)

(The 64-bit builds compile the game's units with their longs made ints,
tools/lp64_build.py; this file is appended after that, so it spells those
types int.) What mods.c and mod_sounds.c need of the rest of the game: one sound tag in
the map (its name sound\fuzz, its definition a struct of zeros), one mod
with one file (the fuzzer's input), the game's allocator in the Xbox
address window (an arena taken back whole after each input), and the
console, the log and the sound system as nothing. */

#include "cache/cache_files.h"
#include "sound/sound_definitions.h"

#include <sys/mman.h>
#include <stdarg.h>
/* (the game is built as strict C, which hides it on Linux: world_setup.c) */
#if defined(__linux__) && !defined(MAP_ANONYMOUS)
#define MAP_ANONYMOUS 0x20
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
	FUZZ_ARENA_BASE = 0x10000000,
	FUZZ_ARENA_SIZE = 0x04000000,
};

static const uint8_t *fuzz_data;
static size_t fuzz_size;
static unsigned int fuzz_arena_next;
static int fuzz_arena_mapped;
static unsigned int fuzz_scans;
static struct sound_definition fuzz_definition;
static char fuzz_name[] = "sound\\fuzz";

void mod_sound_fuzz_input(const uint8_t *data, size_t size)
{
	fuzz_data = data;
	fuzz_size = size;
	/* (the last input's memory taken back whole: the game's free is nothing
	here, the overrides and sounds are let go by mods_tags_unloaded) */
	fuzz_arena_next = FUZZ_ARENA_BASE;
	memset(&fuzz_definition, 0, sizeof(fuzz_definition));
}

void *debug_malloc(unsigned int size, boolean clear, const char *file, int line)
{
	byte *pointer;

	(void)file;
	(void)line;
	if (!fuzz_arena_mapped)
	{
		void *wanted = xbox_pointer(FUZZ_ARENA_BASE);

		if (mmap(wanted, FUZZ_ARENA_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) !=
			wanted)
		{
			abort();
		}
		fuzz_arena_mapped = 1;
		fuzz_arena_next = FUZZ_ARENA_BASE;
	}
	size = (size + 15) & ~15U;
	if (size > FUZZ_ARENA_SIZE - (fuzz_arena_next - FUZZ_ARENA_BASE))
		return NULL;
	pointer = (byte *)xbox_pointer(fuzz_arena_next);
	fuzz_arena_next += size;
	if (clear)
	{
		unsigned int index;

		for (index = 0; index < size; index++)
			pointer[index] = 0;
	}
	return pointer;
}

void debug_free(void *pointer, const char *file, int line)
{
	(void)pointer;
	(void)file;
	(void)line;
}

void xbox_address_out_of_range(void const *pointer)
{
	fprintf(stderr, "a pointer outside the Xbox address space: %p\n", pointer);
	abort();
}

/* the map: one sound tag, index 0 */
char *tag_get_name(int tag_index)
{
	return tag_index == 0 ? fuzz_name : NULL;
}

int tag_loaded(int group_tag, const char *name)
{
	(void)group_tag;
	(void)name;
	return NONE;
}

void *tag_get(int group_tag, int tag_index)
{
	(void)group_tag;
	return tag_index == 0 ? &fuzz_definition : NULL;
}

void tag_iterator_new(struct tag_iterator *iterator, int group_tag)
{
	(void)group_tag;
	memset(iterator, 0, sizeof(*iterator));
}

int tag_iterator_next(struct tag_iterator *iterator)
{
	int *state = (int *)iterator;

	return (*state)++ == 0 ? 0 : NONE;
}

/* the sound system */
void sound_stop_all(void) {}
void sound_cache_flush(void) {}

/* the console and the log */
void console_printf(boolean clear, const char *format, ...) { (void)clear; (void)format; }
void console_warning(const char *format, ...) { (void)format; }
void error(int priority, const char *format, ...) { (void)priority; (void)format; }
void platform_log(char const *format, ...) { (void)format; }
int config_boolean(const char *name) { (void)name; return 1; }
short game_connection(void) { return 0; }
/* (by hand: cseries.h makes strncpy this function) */
char *csstrncpy(char *destination, const char *source, unsigned int count)
{
	unsigned int index = 0;

	while (index < count && source[index])
	{
		destination[index] = source[index];
		index++;
	}
	while (index < count)
		destination[index++] = 0;
	return destination;
}

/* the mods folder: one mod, one file, the input */
int port_mods_scan(void) { fuzz_scans++; return 1; }
unsigned int port_mods_scans(void) { return fuzz_scans; }
int port_mods_played(void) { return 1; }
int port_mods_file_find(char const *tag_file, char const *map_name) { (void)tag_file; (void)map_name; return 0; }
int port_mods_file_stamp(int file, unsigned int *size, unsigned int stamp[2])
{
	if (file != 0 || fuzz_size > 0x7fffffff)
		return 0;
	*size = (unsigned int)fuzz_size;
	stamp[0] = 1;
	stamp[1] = 0;
	return 1;
}
int port_mods_file_read(int file, unsigned int offset, void *buffer, unsigned int size)
{
	if (file != 0 || offset > fuzz_size || size > fuzz_size - offset)
		return 0;
	memcpy(buffer, fuzz_data + offset, size);
	return 1;
}
char const *port_mods_file_mod(int file) { (void)file; return "fuzz"; }
char const *port_mods_file_path(int file) { (void)file; return "mods/fuzz/tags/sound/fuzz.sound"; }
int port_mods_count(void) { return 1; }
int port_mods_at(int position) { return position == 0 ? 0 : -1; }
int port_mods_describe(int index, char *text, unsigned int size, char const *map_name)
{
	(void)index;
	(void)map_name;
	if (size)
		text[0] = 0;
	return 0;
}
