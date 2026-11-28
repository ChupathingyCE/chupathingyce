/*
MAP_FUZZ.C

libFuzzer targets for what the game reads of a map file before anything else
trusts it, built and run by tools/test_map_fuzz.py with AddressSanitizer and
UndefinedBehaviorSanitizer on the 64-bit build's code (rewritten for LP64 and
compiled with the macOS build's flags, as the game's own units are):

  MAP_FUZZ_XBOX     an Xbox map: its header, then its tags through the tag
                    validator (port/linux/game/tag_validate.c, tag_schema*.c),
                    then each of its structure BSPs where it loads, as
                    cache_files.c and tools/map_validate.c do; the tags it
                    lets through checked again must need no more correction.
  MAP_FUZZ_CE       a Custom Edition or HaloMD map: its header, then the
                    checks (ce_map_checks.c and the files it calls), then,
                    if it passes, what loading does with it (cache_files.c's
                    scenario_tags_load and scenario_structure_bsp_load): its
                    resources copied in, its repairs, its models, functions,
                    shaders and HUD converted, each BSP's vertices compressed
                    (each BSP as corrected, checked again, must need no more
                    correction).
                    The checks are the contract the conversions trust: a map
                    they pass must convert without touching memory it does
                    not own. What the load reads must also be found to be
                    what was checked (ce_map_loaded_*): a map that passed,
                    loaded unchanged, that is refused as changed is a crash.
                    With MAP_FUZZ_CE_CHANGE (tags, model, bsp or resources),
                    that part is changed between the checks and the load
                    (a byte in its middle; the last byte of each resource
                    map), and the load must refuse it.
                    With MAP_FUZZ_CE_RESOURCE (bitmaps, sounds, loc,
                    pc_bitmaps or pc_sounds), the input is that resource map
                    instead, and the map is the one MAP_FUZZ_CE_MAP names:
                    ce_resources.c's reader of resource maps fuzzed through
                    the same checks and load.
  MAP_FUZZ_VORBIS   an Ogg Vorbis stream through ce_vorbis_decode (stb_vorbis),
                    and mcc_vorbis_decode where the build has MCC maps,
                    as a Custom Edition sound's permutation is decoded.
  MAP_FUZZ_FORGE_AI a campaign map as Forge's AI characters read one into a
                    multiplayer map (port/linux/game/forge_ai_tags.c): the
                    player's own file, so anything; its header, its tags
                    expanded and moved, the tag validator, the characters'
                    tags reached, copied and numbered, their pixels and
                    samples written to the data file and read back.

An input is the map file itself (uncompressed: an Xbox map's tags inflated as
cache_files.c inflates them), or, with MAP_FUZZ_BASE naming a map file, a list
of changes to that map (each 8 bytes: what to change and a value), which
reaches deeper into a real map than whole-file mutations do. Custom Edition
maps' resource maps (bitmaps.map, sounds.map, loc.map) are read from the
folder MAP_FUZZ_CE_RESOURCES names, if any: without them, a map that keeps
tags there is refused as the game refuses it. Halo PC retail's own
bitmaps.map and sounds.map, which a HaloMD map's stock bitmaps and sounds are
read from when a player has them (maps_pc), are read from the folder
MAP_FUZZ_PC_RESOURCES names, if any. The Xbox's ui.map, whose button
icons a map's HUD is given (ce_hud.c), is read from MAP_FUZZ_XBOX_UI, if it
names one: without it, HUD messages are left without them, as in the game.

The Xbox address space the 64-bit build keeps its tags in (xbox_address.h) is
reserved as the game reserves it (port/linux/src/xbox_memory.c); only the
tag caches and an allocation arena are committed, so a pointer a map gives
that nothing checked faults rather than reading memory that happens to be
there. The arena (the game's system_malloc and XPhysicalAlloc, which must
give Xbox addresses) is poisoned between allocations for AddressSanitizer.
*/

#include "cseries.h"
#include "cseries_windows.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* (the game's units are built as strict ISO C, which hides these on Linux:
its values) */
#if defined(__linux__) && !defined(MAP_ANONYMOUS)
#define MAP_ANONYMOUS 0x20
#endif
#if defined(__linux__) && !defined(MAP_NORESERVE)
#define MAP_NORESERVE 0x4000
#endif

#include <sanitizer/asan_interface.h>

/* (the harness's own memory is the host's: cseries.h gives the game's units
the game's heap, debug_malloc below) */
#undef malloc
#undef free
#undef realloc

#if !defined(MAP_FUZZ_XBOX) && !defined(MAP_FUZZ_CE) && !defined(MAP_FUZZ_VORBIS) && !defined(MAP_FUZZ_FORGE_AI)
#error define MAP_FUZZ_XBOX, MAP_FUZZ_CE, MAP_FUZZ_VORBIS or MAP_FUZZ_FORGE_AI
#endif

/* ---------- constants */

enum
{
	HEADER_SIZE = 0x800,
	HEADER_SIGNATURE = 'head',
	FOOTER_SIGNATURE = 'foot',

	/* the Xbox tag cache (physical_memory_map.c) */
	XBOX_TAG_CACHE_BASE = 0x803A6000,
	XBOX_TAG_CACHE_SIZE = 0x1600000,
	/* Custom Edition maps' (platform.h) */
	CE_TAG_CACHE_BASE = 0x40440000,
	CE_TAG_CACHE_SIZE = 0x1700000,
	/* (and the room above it: platform.h) */
	CE_CONVERSION_ROOM_SIZE = 0x4c0000,
	/* (its tags' header: cache_files.c's cache_file_ce_tag_header) */
	CE_TAG_HEADER_SIZE = 0x28,
	/* the game's heap and contiguous memory, as far as these units use them
	(xbox_heap.c's range) */
	ARENA_BASE = 0x42000000,
	ARENA_SIZE = 0x20000000,
	ARENA_REDZONE = 64,

	SCENARIO_GROUP = 'scnr',
	STRUCTURE_BSP_GROUP = 'sbsp',
	SCENARIO_BSPS_OFFSET = 0x5A4,
	BSP_REFERENCE_SIZE = 0x20,
	MAXIMUM_BSPS = 32,
	SECTOR_SIZE = 512,

	CACHE_VERSION_XBOX = 5,
	CACHE_VERSION_RETAIL = 7,
	CACHE_VERSION_CUSTOM_EDITION = 609,

	/* the largest file read (the patch mode's base maps are larger) */
	MAXIMUM_INPUT = 0x7FFFFFFF,
};

/* ---------- structures */

struct map_header
{
	unsigned long header_signature;
	long version;
	long file_length;
	unsigned long checksum;
	long tag_data_offset;
	long tag_data_size;
	unsigned char reserved18[8];
	char name[0x20];
	char build[0x20];
	unsigned char reserved60[0x79C];
	unsigned long footer_signature;
};

typedef char verify_map_header_size[sizeof(struct map_header) == HEADER_SIZE ? 1 : -1];

struct tag_instance
{
	unsigned long group_tag;
	unsigned long parent_group_tags[2];
	long tag_index;
	unsigned long name;
	unsigned long base_address;
	unsigned long indexed;
	unsigned long unused;
};

struct bsp_reference
{
	long file_offset;
	long file_size;
	unsigned long base_address;
	unsigned long unused;
	unsigned long group_tag;
	unsigned long name;
	long name_length;
	long tag_index;
};

/* a file the units read through the Win32 calls (the map, a resource map)
or write (decoded sounds) */
struct fuzz_file
{
	unsigned char const *data;
	unsigned long size;
	unsigned long position;
	boolean writable;
	unsigned long written;
	/* (a resource map's: its index in fuzz_resources, plus one, whose data
	is read as it is now, as a file held open reads what is in it now) */
	long resource;
};

/* ---------- prototypes */

int LLVMFuzzerInitialize(int *argc, char ***argv);
int LLVMFuzzerTestOneInput(unsigned char const *data, size_t size);

/* (MAP_FUZZ_COPY_ROOM: fuzz_copy_room) */
static long fuzz_copy_room_limit = -1;
static unsigned long fuzz_copy_room_used;

boolean tag_validate_tags(void *tag_header, long tag_data_size, long file_length, char const *map_name);
boolean tag_validate_structure_bsp(long tag_index, void *base, long size);
long tag_validate_corrections(void);
#ifdef MAP_FUZZ_CE
struct tag_schema_layout;
extern struct tag_schema_layout const tag_schema_pc_layout;
extern struct tag_schema_layout const tag_schema_opensauce_layout;
boolean tag_validate_tags_layout(struct tag_schema_layout const *layout, void *tag_header, long tag_data_size,
	long file_length, char const *map_name);
unsigned long ce_resources_free_mark(void);
unsigned long ce_resources_allocate_tags(unsigned long size);
extern unsigned long (*tag_validate_room_proc)(unsigned long size);
#endif

boolean ce_map_check(HANDLE file, char const *map_name, long file_length, long tag_data_offset, long tag_data_size);
unsigned long ce_map_checked_tags_tail(void);
long ce_map_header_tag_data_size(long version, long file_length, long tag_data_offset, long tag_data_size);
boolean ce_map_bsp_reference_checked(void const *reference);
extern long ce_map_cache_version;
boolean ce_resources_tags_loaded(void *tag_instances, long tag_count, unsigned long first_free,
	unsigned long end_free);
void ce_repairs_scenario_loaded(void *tag_instances, long tag_count, unsigned long scenario_tag_index);
void ce_repairs_tags_loaded(void *tag_instances, long tag_count, unsigned long scenario_tag_index);
boolean ce_models_tags_loaded(void *tag_instances, long tag_count, byte const *model_data,
	unsigned long vertex_data_size, unsigned long model_data_size);
void ce_functions_tags_loaded(void *tag_instances, long tag_count);
void ce_shaders_tags_loaded(void *tag_instances, long tag_count);
void ce_hud_tags_loaded(void *tag_instances, long tag_count);
void ce_loaded_bsps_low(unsigned long bsp_low);
boolean ce_map_check_loaded(void *tag_instances, long tag_count, unsigned long scenario_tag_index,
	char const *map_name);
boolean ce_map_loaded_tags_unchanged(void const *tag_data, unsigned long size);
boolean ce_map_loaded_bsp_unchanged(void const *reference, void const *bsp, unsigned long size);
boolean ce_map_loaded_unchanged(void);
void ce_bsp_loaded(void *structure);
void ce_repairs_bsp_loaded(void *structure);
void ce_bsp_unloaded(void);
void ce_resources_tags_unloaded(void);
void ce_resources_update(void);
void ce_models_tags_unloaded(void);
void ce_hud_tags_unloaded(void);

int ce_vorbis_decode(const unsigned char *data, int size, int *channels, int *sample_rate, short **samples);
int ce_vorbis_resample_piece(short **samples, int frames, int channels, int rate, int wanted,
	const short *before, int before_frames, const short *after, int after_frames);
int ce_vorbis_edges(const unsigned char *data, int size, int count, int whole, int *channels, int *sample_rate,
	short *head, int *head_frames, short *tail, int *tail_frames);
void ce_vorbis_free(short *samples);
#ifdef MAP_FUZZ_VORBIS
#include "halo_port_features.h"
/* (MCC's decoder, over the same stb_vorbis: content_vorbis.c, where the build
has MCC maps) */
int mcc_vorbis_decode(unsigned char const *bytes, uint32_t size, uint32_t frame_limit,
	short **samples, uint32_t *frames, uint32_t *rate, int *channels);
void mcc_vorbis_free(short *samples);
void mcc_loading_pulse(void);
void mcc_loading_pulse(void)
{
}
#endif

/* ---------- globals */

static boolean fuzz_verbose;
/* whether a Custom Edition map's tags go through Halo PC's tag schema as
they load (MAP_FUZZ_CE_SCHEMA=0: not, so that a test reaches the
conversions' own guards with a map the schema would refuse) */
static boolean fuzz_ce_schema = TRUE;
/* the map being changed (MAP_FUZZ_BASE), and a copy to change */
static unsigned char *fuzz_base;
static unsigned long fuzz_base_size;
static unsigned char *fuzz_work;
/* the resource maps (MAP_FUZZ_CE_RESOURCES) */
static struct
{
	char const *name;
	unsigned char *data;
	unsigned long size;
} fuzz_resources[] = { { "bitmaps" }, { "sounds" }, { "loc" }, { "pc_bitmaps" }, { "pc_sounds" } };
/* (the first three Custom Edition's, MAP_FUZZ_CE_RESOURCES; the others Halo
PC's own, MAP_FUZZ_PC_RESOURCES) */
#define FUZZ_RESOURCE_COUNT ((long)(sizeof(fuzz_resources) / sizeof(fuzz_resources[0])))
/* the resource map that is the input (MAP_FUZZ_CE_RESOURCE: an index in
fuzz_resources, or -1), and the map read with it (MAP_FUZZ_CE_MAP) */
static long fuzz_resource_input = -1;
static unsigned char *fuzz_resource_map;
static unsigned long fuzz_resource_map_size;
/* the Xbox's ui.map (MAP_FUZZ_XBOX_UI) */
static unsigned char *fuzz_xbox_ui;
static unsigned long fuzz_xbox_ui_size;
static struct fuzz_file fuzz_map_file;
/* what is changed between the checks and the load (MAP_FUZZ_CE_CHANGE) */
static char const *fuzz_ce_change;
static struct fuzz_file fuzz_open_files[8];
#ifdef MAP_FUZZ_FORGE_AI
/* the campaign map's file as the game finds it (MAP_FUZZ_FORGE_AI) */
static unsigned char *fuzz_forge_map;
static unsigned long fuzz_forge_map_size;
#endif
static boolean fuzz_tags_are_ce;
/* the game exiting on purpose (system_exit), or asserting */
static jmp_buf fuzz_exit;
static boolean fuzz_exit_armed;
static unsigned long fuzz_arena_next;
static unsigned long fuzz_arena_end;
/* (platform.h's: the room above the tag cache is mapped) */
unsigned int platform_ce_conversion_room = CE_CONVERSION_ROOM_SIZE;

/* ---------- the Xbox address space */

static void fuzz_commit(unsigned long address, unsigned long size)
{
	/* (whole host pages: Apple silicon's are 16 KB) */
	unsigned long page = 0x10000;
	unsigned long first = address & ~(page - 1);
	void *wanted = xbox_pointer(first);

	size = (address + size - first + page - 1) & ~(page - 1);
	if (mmap(wanted, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != wanted)
	{
		fprintf(stderr, "map_fuzz: cannot commit %08lx bytes at Xbox address %08lx\n", size, address);
		abort();
	}
}

static void fuzz_arena_initialize(void);

static void fuzz_address_space_reserve(void)
{
	void *wanted = (void *)XBOX_ADDRESS_SPACE_BASE;

	if (mmap(wanted, XBOX_ADDRESS_SPACE_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0) !=
		wanted)
	{
		fprintf(stderr, "map_fuzz: cannot reserve the Xbox address space at %p\n", wanted);
		abort();
	}
	fuzz_commit(XBOX_TAG_CACHE_BASE, XBOX_TAG_CACHE_SIZE);
	fuzz_commit(CE_TAG_CACHE_BASE, CE_TAG_CACHE_SIZE + CE_CONVERSION_ROOM_SIZE);
	fuzz_commit(ARENA_BASE, ARENA_SIZE);
	fuzz_arena_initialize();
}

/* the arena: power-of-two blocks, each after a redzone that keeps its size,
reused through a free list for each size; whatever is not given out is
poisoned */
enum
{
	ARENA_SMALLEST_CLASS = 6, /* 64 bytes */
	ARENA_CLASSES = 30,
};

static void *fuzz_arena_free_lists[ARENA_CLASSES];

static void fuzz_arena_initialize(void)
{
	fuzz_arena_next = ARENA_BASE;
	fuzz_arena_end = ARENA_BASE + ARENA_SIZE;
	ASAN_POISON_MEMORY_REGION(xbox_pointer(ARENA_BASE), ARENA_SIZE);
}

static unsigned long fuzz_arena_header(void const *pointer)
{
	unsigned long value;

	ASAN_UNPOISON_MEMORY_REGION((byte *)pointer - ARENA_REDZONE, ARENA_REDZONE);
	value = *(unsigned long *)((byte *)pointer - ARENA_REDZONE);
	ASAN_POISON_MEMORY_REGION((byte *)pointer - ARENA_REDZONE, ARENA_REDZONE);
	return value;
}

static void *fuzz_arena_allocate(unsigned long size, boolean zero)
{
	long class = ARENA_SMALLEST_CLASS;
	byte *pointer;

	while (class < ARENA_CLASSES && (1UL << class) < size)
		class++;
	if (class >= ARENA_CLASSES)
		return NULL;
	if (fuzz_arena_free_lists[class])
	{
		pointer = fuzz_arena_free_lists[class];
		ASAN_UNPOISON_MEMORY_REGION(pointer, sizeof(void *));
		fuzz_arena_free_lists[class] = *(void **)pointer;
	}
	else
	{
		unsigned long block = ARENA_REDZONE + (1UL << class);

		if (block > fuzz_arena_end - fuzz_arena_next)
			return NULL;
		pointer = (byte *)xbox_pointer(fuzz_arena_next) + ARENA_REDZONE;
		fuzz_arena_next += block;
	}
	ASAN_UNPOISON_MEMORY_REGION(pointer - ARENA_REDZONE, ARENA_REDZONE);
	*(unsigned long *)(pointer - ARENA_REDZONE) = (unsigned long)class << 24 | (size & 0xFFFFFF);
	*(unsigned long *)(pointer - ARENA_REDZONE + 4) = (unsigned long)size;
	ASAN_POISON_MEMORY_REGION(pointer - ARENA_REDZONE, ARENA_REDZONE);
	ASAN_POISON_MEMORY_REGION(pointer, 1UL << class);
	ASAN_UNPOISON_MEMORY_REGION(pointer, size);
	memset(pointer, zero ? 0 : 0xBE, size);

	return pointer;
}

static unsigned long fuzz_arena_size(void const *pointer)
{
	unsigned long size;

	ASAN_UNPOISON_MEMORY_REGION((byte *)pointer - ARENA_REDZONE, ARENA_REDZONE);
	size = *(unsigned long *)((byte *)pointer - ARENA_REDZONE + 4);
	ASAN_POISON_MEMORY_REGION((byte *)pointer - ARENA_REDZONE, ARENA_REDZONE);
	return size;
}

static void fuzz_arena_free(void *pointer)
{
	long class;

	if (!pointer)
		return;
	class = (long)(fuzz_arena_header(pointer) >> 24);
	ASAN_POISON_MEMORY_REGION(pointer, 1UL << class);
	ASAN_UNPOISON_MEMORY_REGION(pointer, sizeof(void *));
	*(void **)pointer = fuzz_arena_free_lists[class];
	ASAN_POISON_MEMORY_REGION(pointer, sizeof(void *));
	fuzz_arena_free_lists[class] = pointer;
}

/* ---------- the game's functions these units call */

void *system_malloc(long size)
{
	return size < 0 ? NULL : fuzz_arena_allocate((unsigned long)size, FALSE);
}

void system_free(void *pointer)
{
	fuzz_arena_free(pointer);
}

void *XPhysicalAlloc(unsigned long size, unsigned long address, unsigned long alignment, unsigned long protect)
{
	return fuzz_arena_allocate(size, TRUE);
}

void XPhysicalFree(void *pointer)
{
	fuzz_arena_free(pointer);
}

void D3DResource_Register(struct D3DResource *resource, void *base)
{
}

/* (ce_hud.c's ui.map, for the Xbox's button icons: none here) */
char const *cache_files_map_directory(void)
{
	return "";
}

unsigned long system_milliseconds(void)
{
	return 0;
}

/* (sound_manager.c's, for ce_resources.c's decoded sounds: none plays here) */
boolean sound_definition_playing(long definition_index);
boolean sound_definition_playing(long definition_index)
{
	(void)definition_index;
	return FALSE;
}

void system_exit(long code)
{
	if (fuzz_verbose)
		fprintf(stderr, "map_fuzz: the game exits (%ld)\n", (long)code);
	if (fuzz_exit_armed)
		longjmp(fuzz_exit, 1);
	abort();
}

void display_assert(char *condition, char *file, long line, boolean fatal)
{
	/* (an assertion a map can reach halts the game: reported as a crash) */
	fprintf(stderr, "map_fuzz: assertion failed: %s (%s #%ld)\n", condition ? condition : "", file ? file : "",
		(long)line);
	abort();
}

/* (a release build's asserts, cseries.h: the same) */
void release_assert_failed(char const *information, char const *file, long line, boolean fatal)
{
	display_assert((char *)information, (char *)file, line, fatal);
}

char temporary[256];

char *csprintf(char *buffer, char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(buffer, 256, format, arguments);
	va_end(arguments);
	return buffer;
}

void error(short priority, char const *format, ...)
{
	if (fuzz_verbose)
	{
		va_list arguments;

		va_start(arguments, format);
		vfprintf(stderr, format, arguments);
		va_end(arguments);
		fputc('\n', stderr);
	}
}

void console_warning(char const *format, ...)
{
	if (fuzz_verbose)
	{
		va_list arguments;

		va_start(arguments, format);
		vfprintf(stderr, format, arguments);
		va_end(arguments);
		fputc('\n', stderr);
	}
}

void tag_validate_report(char const *message)
{
	if (fuzz_verbose)
		fprintf(stderr, "%s\n", message);
}

void xbox_address_out_of_range(void const *pointer)
{
	fprintf(stderr, "map_fuzz: pointer %p is outside the Xbox address space\n", pointer);
	abort();
}

/* (not traced for coverage: HaloMD's tags are found in the resource maps by
their paths, thousands of comparisons a run, which tracing made most of a
run's time) */
__attribute__((no_sanitize("coverage")))
int _stricmp(char const *s1, char const *s2)
{
	for (;; s1++, s2++)
	{
		int c1 = *s1 >= 'A' && *s1 <= 'Z' ? *s1 - 'A' + 'a' : (unsigned char)*s1;
		int c2 = *s2 >= 'A' && *s2 <= 'Z' ? *s2 - 'A' + 'a' : (unsigned char)*s2;

		if (c1 != c2 || !c1)
			return c1 - c2;
	}
}

/* (the Custom Edition map's tags are loaded: tag_groups.c bounds reads of
them, ce_tags_pointer) */
boolean cache_file_tags_are_ce(void)
{
	return fuzz_tags_are_ce;
}

HANDLE cache_files_ce_map_file(void)
{
	return &fuzz_map_file;
}

boolean map_family_resource(char const *name, char *path, long size)
{
	snprintf(path, (size_t)size, "%s", name);
	return TRUE;
}

/* (the game under way, a second in: ce_resources_update) */
boolean game_in_progress(void)
{
	return TRUE;
}

long game_time_get(void)
{
	return 31;
}

/* (one local player, whose HUD lines are written out: ce_resources_update) */
short local_player_get_next(short local_player_index)
{
	return local_player_index == NONE ? 0 : NONE;
}

void hud_print_message(short local_player_index, wchar_t const *message)
{
	if (fuzz_verbose)
	{
		fprintf(stderr, "HUD %d: ", local_player_index);
		for (; *message; message++)
			fputc((char)*message, stderr);
		fputc('\n', stderr);
	}
}

boolean map_family_halo_pc_resource(char const *name, char *path, long size)
{
	snprintf(path, (size_t)size, "pc_%s", name);
	return TRUE;
}

/* (tag_groups.c's, as the game reads a block's element: a Custom Edition
map's through ce_tags_pointer) */
void *ce_tags_pointer(unsigned long address, long size);

void *tag_block_get_element_with_size(void const *block, long index, long element_size)
{
	long count = *(long const *)block;
	unsigned long address = *(unsigned long const *)((byte const *)block + 4);
	static byte empty[0x400];

	if (index < 0 || index >= count || !address)
	{
		memset(empty, 0, sizeof(empty));
		return element_size <= (long)sizeof(empty) ? empty : NULL;
	}
	if (fuzz_tags_are_ce)
		return ce_tags_pointer(address + index * element_size, element_size);
	return (byte *)xbox_pointer(address) + index * element_size;
}

/* ---------- the rendering the conversions call (no device) */

DWORD D3D__RenderState[D3DRS_MAX];

#define FUZZ_RENDER_STATE(name) void D3DDevice_SetRenderState_##name(unsigned long value) {}
FUZZ_RENDER_STATE(BackFillMode)
FUZZ_RENDER_STATE(CullMode)
FUZZ_RENDER_STATE(DoNotCullUncompressed)
FUZZ_RENDER_STATE(Dxt1NoiseEnable)
FUZZ_RENDER_STATE(EdgeAntiAlias)
FUZZ_RENDER_STATE(FillMode)
FUZZ_RENDER_STATE(FogColor)
FUZZ_RENDER_STATE(FrontFace)
FUZZ_RENDER_STATE(LineWidth)
FUZZ_RENDER_STATE(LogicOp)
FUZZ_RENDER_STATE(MultiSampleAntiAlias)
FUZZ_RENDER_STATE(MultiSampleMask)
FUZZ_RENDER_STATE(MultiSampleType)
FUZZ_RENDER_STATE(NormalizeNormals)
FUZZ_RENDER_STATE(OcclusionCullEnable)
FUZZ_RENDER_STATE(PSTextureModes)
FUZZ_RENDER_STATE(RopZCmpAlwaysRead)
FUZZ_RENDER_STATE(RopZRead)
FUZZ_RENDER_STATE(ShadowFunc)
FUZZ_RENDER_STATE(StencilCullEnable)
FUZZ_RENDER_STATE(StencilEnable)
FUZZ_RENDER_STATE(StencilFail)
FUZZ_RENDER_STATE(TextureFactor)
FUZZ_RENDER_STATE(TwoSidedLighting)
FUZZ_RENDER_STATE(VertexBlend)
FUZZ_RENDER_STATE(YuvEnable)
FUZZ_RENDER_STATE(ZBias)
FUZZ_RENDER_STATE(ZEnable)
void D3DDevice_SetRenderState_Deferred(D3DRENDERSTATETYPE state, unsigned long value) {}
void D3DDevice_SetRenderState_Simple(unsigned long method, unsigned long value) {}

/* (bitmaps.c's, which ce_resources.c checks bitmaps with: no texture is
made, no compressed block decoded; swizzling is the game's own,
rasterizer_swizzle.c, which ce_hud.c reads ui.map's button icons with) */
void rasterizer_bitmap_changed(void *bitmap) {}
void rasterizer_bitmap_delete(void *bitmap) {}
boolean rasterizer_bitmap_new(void *bitmap) { return TRUE; }
void DecodeBlockAlpha3__single_pixel(void) {}
void DecodeBlockAlpha4__single_pixel(void) {}
void DecodeBlockRGB__single_pixel(void) {}

short floor_log2(unsigned long value)
{
	short result = -1;

	while (value)
	{
		value >>= 1;
		result++;
	}
	return result;
}

/* ---------- files */

HANDLE CreateFileA(char const *path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD disposition,
	DWORD flags, HANDLE template_file)
{
	long index;

	for (index = 0; index < (long)(sizeof(fuzz_open_files) / sizeof(fuzz_open_files[0])); index++)
	{
		struct fuzz_file *file = &fuzz_open_files[index];

		if (file->data || file->writable)
			continue;
		memset(file, 0, sizeof(*file));
		if (strstr(path, "ce_sounds"))
		{
			file->writable = TRUE;
			return file;
		}
#ifdef MAP_FUZZ_FORGE_AI
		/* (the characters' data file, and the campaign map, by any of
		the factions' missions' names) */
		if (strstr(path, "forge_ai_"))
		{
			file->writable = TRUE;
			return file;
		}
		if (fuzz_forge_map && strlen(path) > 4 && !strcmp(path + strlen(path) - 4, ".map"))
		{
			file->data = fuzz_forge_map;
			file->size = fuzz_forge_map_size;
			return file;
		}
#endif
		if (!strcmp(path, "ui.map"))
		{
			if (!fuzz_xbox_ui)
				return INVALID_HANDLE_VALUE;
			file->data = fuzz_xbox_ui;
			file->size = fuzz_xbox_ui_size;
			return file;
		}
		{
			long resource;

			for (resource = 0; resource < FUZZ_RESOURCE_COUNT; resource++)
			{
				if (!strcmp(path, fuzz_resources[resource].name) && fuzz_resources[resource].data)
				{
					file->data = fuzz_resources[resource].data;
					file->size = fuzz_resources[resource].size;
					file->resource = resource + 1;
					return file;
				}
			}
		}
		return INVALID_HANDLE_VALUE;
	}
	return INVALID_HANDLE_VALUE;
}

BOOL CloseHandle(HANDLE handle)
{
	struct fuzz_file *file = handle;

	if (file && file != &fuzz_map_file)
		memset(file, 0, sizeof(*file));
	return TRUE;
}

BOOL DeleteFileA(char const *path)
{
	return TRUE;
}

DWORD GetLastError(void)
{
	return 0;
}

/* a resource map's file as its data is now (MAP_FUZZ_CE_RESOURCE gives each
input its own) */
static void fuzz_file_refresh(struct fuzz_file *file)
{
	if (file->resource)
	{
		file->data = fuzz_resources[file->resource - 1].data;
		file->size = fuzz_resources[file->resource - 1].size;
	}
}

DWORD GetFileSize(HANDLE handle, LPDWORD high)
{
	struct fuzz_file *file = handle;

	fuzz_file_refresh(file);
	if (high)
		*high = 0;
	return file->writable ? file->written : file->size;
}

DWORD SetFilePointer(HANDLE handle, LONG distance, PLONG high, DWORD method)
{
	struct fuzz_file *file = handle;

	if (method != FILE_BEGIN || high)
		return INVALID_SET_FILE_POINTER;
	file->position = (unsigned long)distance;
	return file->position;
}

BOOL ReadFile(HANDLE handle, LPVOID buffer, DWORD size, LPDWORD read, LPOVERLAPPED overlapped)
{
	struct fuzz_file *file = handle;
	unsigned long available;

	fuzz_file_refresh(file);
	if (file->writable)
	{
		*read = 0;
		return FALSE;
	}
	available = file->position < file->size ? file->size - file->position : 0;
	if (size > available)
		size = available;
	memcpy(buffer, file->data + file->position, size);
	file->position += size;
	*read = size;
	return TRUE;
}

BOOL WriteFile(HANDLE handle, LPCVOID buffer, DWORD size, LPDWORD written, LPOVERLAPPED overlapped)
{
	struct fuzz_file *file = handle;
	unsigned long index;
	unsigned long sum = 0;

	if (!file->writable)
		return FALSE;
	/* (every byte read, so that a size past the buffer is found) */
	for (index = 0; index < size; index++)
		sum += ((unsigned char const *)buffer)[index];
	file->written += size;
	*written = size + (sum & 0);
	return TRUE;
}

#ifdef MAP_FUZZ_CE
/* (the sounds' Ogg Vorbis streams are MAP_FUZZ_VORBIS's: here each is
refused as the decoder refuses a damaged one, which keeps the runs fast) */
int ce_vorbis_decode(const unsigned char *data, int size, int *channels, int *sample_rate, short **samples)
{
	*samples = NULL;
	*channels = 0;
	*sample_rate = 0;
	return -1;
}

int ce_vorbis_resample(short **samples, int frames, int channels, int rate, int wanted)
{
	return -1;
}

/* (a piece of a long stream's, below, brought to another rate as the real
one gives it back: halved in place, doubled in a new allocation of twice
its frames; each sample is only repeated or dropped. Any other is refused) */
int ce_vorbis_resample_piece(short **samples, int frames, int channels, int rate, int wanted,
	const short *before, int before_frames, const short *after, int after_frames)
{
	short *in = *samples;
	short *out;
	int index;

	if (frames <= 0 || channels < 1 || channels > 2 || !in)
		return -1;
	if (rate == wanted)
		return frames;
	if (rate == wanted * 2)
	{
		if (frames / 2 <= 0)
			return -1;
		for (index = 0; index < frames / 2 * channels; index++)
			in[index] = in[(index / channels) * 2 * channels + index % channels];
		return frames / 2;
	}
	if (rate * 2 != wanted)
		return -1;
	out = malloc((size_t)frames * 2 * (size_t)channels * sizeof(*out));
	if (!out)
		return -1;
	for (index = 0; index < frames * 2 * channels; index++)
		out[index] = in[(index / channels) / 2 * channels + index % channels];
	free(in);
	*samples = out;
	return frames * 2;
}

int ce_vorbis_edges(const unsigned char *data, int size, int count, int whole, int *channels, int *sample_rate,
	short *head, int *head_frames, short *tail, int *tail_frames)
{
	*channels = 0;
	*sample_rate = 0;
	*head_frames = *tail_frames = 0;
	return -1;
}

void ce_vorbis_free(short *samples)
{
	free(samples);
}

/* (a long stream, decoded a piece at a time: a test's stand-in for one,
data beginning "MAP_FUZZ_PCM" then its channels, rate and frames, gives
that many frames of a ramp; anything else is refused, as above. Then, if
not 0, the frame a read stops short at, as stb_vorbis's does where it skips
a page it cannot read, with more after it) */
struct fuzz_vorbis_stream
{
	int channels;
	long frames_left;
	long frame;
	long short_at;
};

void *ce_vorbis_stream_open(const unsigned char *data, int size, int *channels, int *sample_rate)
{
	struct fuzz_vorbis_stream *stream;
	int header[4];

	*channels = *sample_rate = 0;
	if (size < 12 + (int)sizeof(header) || memcmp(data, "MAP_FUZZ_PCM", 12))
		return NULL;
	memcpy(header, data + 12, sizeof(header));
	if (header[0] < 1 || header[0] > 2 || header[2] < 0)
		return NULL;
	stream = malloc(sizeof(*stream));
	if (!stream)
		return NULL;
	stream->channels = header[0];
	stream->frames_left = header[2];
	stream->frame = 0;
	stream->short_at = header[3];
	*channels = header[0];
	*sample_rate = header[1];
	return stream;
}

int ce_vorbis_stream_read(void *stream, short *buffer, int frames)
{
	struct fuzz_vorbis_stream *fake = stream;
	int count, index;

	if (!fake || frames <= 0)
		return 0;
	count = fake->frames_left < frames ? (int)fake->frames_left : frames;
	if (fake->short_at > fake->frame && fake->short_at - fake->frame < count)
		count = (int)(fake->short_at - fake->frame);
	for (index = 0; index < count * fake->channels; index++)
		buffer[index] = (short)(fake->frame + index / fake->channels);
	fake->frame += count;
	fake->frames_left -= count;
	return count;
}

void ce_vorbis_stream_close(void *stream)
{
	free(stream);
}

short *ce_vorbis_samples_new(int frames, int channels)
{
	if (frames <= 0 || channels < 1 || channels > 2)
		return NULL;
	return malloc((size_t)frames * (size_t)channels * sizeof(short));
}
#endif

/* ---------- reading the inputs */

static unsigned char *fuzz_file_read(char const *path, unsigned long *size)
{
	FILE *file = fopen(path, "rb");
	unsigned char *data = NULL;
	long length;

	if (!file)
		return NULL;
	if (!fseek(file, 0, SEEK_END) && (length = ftell(file)) > 0 && !fseek(file, 0, SEEK_SET))
	{
		data = malloc((size_t)length);
		if (data && fread(data, (size_t)length, 1, file) != 1)
		{
			free(data);
			data = NULL;
		}
		*size = (unsigned long)length;
	}
	fclose(file);
	return data;
}

/* the patch mode's changes applied to a copy of the base map: each 8 bytes,
a word saying what and where, and a value */
static unsigned char const *fuzz_patch(unsigned char const *data, size_t size, size_t *patched_size)
{
	struct map_header const *header = (struct map_header const *)fuzz_base;
	unsigned long tag_data_offset = (unsigned long)header->tag_data_offset;
	unsigned long tag_data_size = (unsigned long)header->tag_data_size;
	unsigned long tag_cache_base = header->version == CACHE_VERSION_XBOX ? XBOX_TAG_CACHE_BASE : CE_TAG_CACHE_BASE;
	unsigned long words = fuzz_base_size / 4;
	unsigned long tag_words = tag_data_size / 4;
	size_t at;

	memcpy(fuzz_work, fuzz_base, fuzz_base_size);
	for (at = 0; at + 8 <= size; at += 8)
	{
		unsigned long what, value, offset, old;

		memcpy(&what, data + at, 4);
		memcpy(&value, data + at + 4, 4);
		/* (most in the tag data, the rest anywhere: the BSPs, the model
		data) */
		offset = (what & 0x40000000) || !tag_words ? (what % words) * 4 :
			tag_data_offset + ((what & 0x3FFFFFFF) % tag_words) * 4;
		if (offset + 4 > fuzz_base_size)
			continue;
		memcpy(&old, fuzz_work + offset, 4);
		switch (what >> 31 | ((what >> 28) & 2))
		{
		case 0: break; /* the value */
		case 1: value = tag_cache_base + value % (tag_data_size ? tag_data_size : 1); break; /* into the tags */
		case 2: value = old + (value % 512) - 256; break; /* near it */
		default: /* another word's */
			memcpy(&value, fuzz_work + tag_data_offset + (value % (tag_words ? tag_words : 1)) * 4, 4);
			break;
		}
		memcpy(fuzz_work + offset, &value, 4);
	}
	*patched_size = fuzz_base_size;
	return fuzz_work;
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	char const *base = getenv("MAP_FUZZ_BASE");
	char const *resources = getenv("MAP_FUZZ_CE_RESOURCES");
	char const *pc_resources = getenv("MAP_FUZZ_PC_RESOURCES");
	char const *xbox_ui = getenv("MAP_FUZZ_XBOX_UI");

	fuzz_verbose = getenv("MAP_FUZZ_VERBOSE") != NULL;
#ifdef MAP_FUZZ_CE
	fuzz_copy_room_limit = getenv("MAP_FUZZ_COPY_ROOM") ? atol(getenv("MAP_FUZZ_COPY_ROOM")) : -1;
#endif
	fuzz_ce_schema = !getenv("MAP_FUZZ_CE_SCHEMA") || strcmp(getenv("MAP_FUZZ_CE_SCHEMA"), "0");
	fuzz_ce_change = getenv("MAP_FUZZ_CE_CHANGE");
	if (fuzz_ce_change && !*fuzz_ce_change)
		fuzz_ce_change = NULL;
	fuzz_address_space_reserve();
	if (base && *base)
	{
		fuzz_base = fuzz_file_read(base, &fuzz_base_size);
		if (!fuzz_base || fuzz_base_size < HEADER_SIZE)
		{
			fprintf(stderr, "map_fuzz: cannot read the base map %s\n", base);
			abort();
		}
		fuzz_work = malloc(fuzz_base_size);
	}
	if (xbox_ui && *xbox_ui)
		fuzz_xbox_ui = fuzz_file_read(xbox_ui, &fuzz_xbox_ui_size);
	if (resources && *resources)
	{
		long index;

		for (index = 0; index < 3; index++)
		{
			char path[1024];

			snprintf(path, sizeof(path), "%s/%s.map", resources, fuzz_resources[index].name);
			fuzz_resources[index].data = fuzz_file_read(path, &fuzz_resources[index].size);
		}
	}
	if (pc_resources && *pc_resources)
	{
		long index;

		for (index = 3; index < FUZZ_RESOURCE_COUNT; index++)
		{
			char path[1024];

			snprintf(path, sizeof(path), "%s/%s.map", pc_resources, fuzz_resources[index].name + 3);
			fuzz_resources[index].data = fuzz_file_read(path, &fuzz_resources[index].size);
		}
	}
#ifdef MAP_FUZZ_CE
	{
		char const *resource = getenv("MAP_FUZZ_CE_RESOURCE");
		char const *map = getenv("MAP_FUZZ_CE_MAP");

		if (resource && *resource)
		{
			long index;

			for (index = 0; index < FUZZ_RESOURCE_COUNT; index++)
			{
				if (!strcmp(resource, fuzz_resources[index].name))
					fuzz_resource_input = index;
			}
			if (fuzz_resource_input < 0 || !map || !*map ||
				!(fuzz_resource_map = fuzz_file_read(map, &fuzz_resource_map_size)))
			{
				fprintf(stderr, "map_fuzz: MAP_FUZZ_CE_RESOURCE needs a resource map's name and MAP_FUZZ_CE_MAP a map\n");
				abort();
			}
		}
	}
#endif
	return 0;
}

/* ---------- the targets */

/* the header, as cache_file_header_verify checks it */
static boolean fuzz_header_valid(struct map_header const *header, size_t size)
{
	if (size < HEADER_SIZE || header->header_signature != HEADER_SIGNATURE ||
		header->footer_signature != FOOTER_SIGNATURE || !memchr(header->name, 0, sizeof(header->name)) ||
		header->file_length < HEADER_SIZE || (unsigned long)header->file_length > size ||
		header->tag_data_offset < 0 || header->tag_data_size < 0 ||
		header->tag_data_offset > header->file_length - header->tag_data_size)
	{
		return FALSE;
	}
	return TRUE;
}

#ifdef MAP_FUZZ_XBOX
static void fuzz_xbox(unsigned char const *data, size_t size)
{
	struct map_header const *header = (struct map_header const *)data;
	byte *tag_cache = xbox_pointer(XBOX_TAG_CACHE_BASE);
	struct tag_instance *instances;
	long tag_count, scenario_index;
	byte *scenario;
	long bsp_count, bsp_index;
	unsigned long bsps;

	if (!fuzz_header_valid(header, size) || header->version != CACHE_VERSION_XBOX ||
		header->tag_data_size > XBOX_TAG_CACHE_SIZE)
	{
		return;
	}
	memset(tag_cache, 0xCD, XBOX_TAG_CACHE_SIZE);
	memcpy(tag_cache, data + header->tag_data_offset, (size_t)header->tag_data_size);
	if (!tag_validate_tags(tag_cache, header->tag_data_size, header->file_length, "fuzz"))
		return;
	/* (the tags as corrected need no more correcting: tools/map_validate.c) */
	{
		long corrections = tag_validate_corrections();

		if (corrections)
		{
			boolean valid;

			if (fuzz_verbose)
				fprintf(stderr, "map_fuzz: %ld corrections; checked again:\n", (long)corrections);
			valid = tag_validate_tags(tag_cache, header->tag_data_size, header->file_length, "fuzz");
			if (!valid || tag_validate_corrections())
			{
				fprintf(stderr, "map_fuzz: the tags as corrected are not clean when checked again (%s, %ld more)\n",
					valid ? "accepted" : "refused", (long)tag_validate_corrections());
				abort();
			}
		}
	}

	/* each structure BSP where it loads, as scenario_structure_bsp_load
	reads and checks it (cache_file_structure_bsp_reference_verify) */
	memcpy(&tag_count, tag_cache + 0xC, 4);
	memcpy(&scenario_index, tag_cache + 4, 4);
	instances = xbox_pointer(*(unsigned long *)tag_cache);
	scenario_index &= 0xFFFF;
	if (scenario_index >= tag_count || instances[scenario_index].group_tag != SCENARIO_GROUP)
		return;
	scenario = xbox_pointer(instances[scenario_index].base_address);
	memcpy(&bsp_count, scenario + SCENARIO_BSPS_OFFSET, 4);
	memcpy(&bsps, scenario + SCENARIO_BSPS_OFFSET + 4, 4);
	if (bsp_count > MAXIMUM_BSPS)
		bsp_count = MAXIMUM_BSPS;
	for (bsp_index = 0; bsp_index < bsp_count; bsp_index++)
	{
		struct bsp_reference reference;
		unsigned long load, read_size;
		long absolute_index;

		memcpy(&reference, (byte *)xbox_pointer(bsps) + bsp_index * BSP_REFERENCE_SIZE, sizeof(reference));
		load = reference.base_address - XBOX_TAG_CACHE_BASE;
		read_size = ((unsigned long)reference.file_size + SECTOR_SIZE - 1) & ~(unsigned long)(SECTOR_SIZE - 1);
		absolute_index = reference.tag_index & 0xFFFF;
		if (reference.file_offset < 0 || reference.file_size < 0x18 || reference.file_size > XBOX_TAG_CACHE_SIZE ||
			reference.file_offset > header->file_length - reference.file_size ||
			reference.base_address < XBOX_TAG_CACHE_BASE || load < (unsigned long)header->tag_data_size ||
			load > XBOX_TAG_CACHE_SIZE || read_size > XBOX_TAG_CACHE_SIZE - load ||
			reference.tag_index == NONE || absolute_index >= tag_count ||
			instances[absolute_index].tag_index != reference.tag_index ||
			instances[absolute_index].group_tag != STRUCTURE_BSP_GROUP)
		{
			continue;
		}
		memset(tag_cache + header->tag_data_size, 0xCD, XBOX_TAG_CACHE_SIZE - header->tag_data_size);
		memcpy(xbox_pointer(reference.base_address), data + reference.file_offset, (size_t)reference.file_size);
		tag_validate_structure_bsp(reference.tag_index, xbox_pointer(reference.base_address), reference.file_size);
	}
}
#endif

#ifdef MAP_FUZZ_CE
/* the load found what it read not what was checked (part: tags, resources,
model or bsp): right only if that part was changed (MAP_FUZZ_CE_CHANGE) */
static void fuzz_ce_refused(char const *part)
{
	if (fuzz_verbose)
		fprintf(stderr, "map_fuzz: the load found its %s changed since the checks\n", part);
	if (!fuzz_ce_change)
	{
		fprintf(stderr, "map_fuzz: nothing changed, but the load found its %s changed since the checks\n", part);
		abort();
	}
}

/* the map as the load reads it (MAP_FUZZ_CE_CHANGE): a copy with a byte
changed in the middle of the part named (the tag data, the model data,
each structure BSP), or the last byte of each resource map changed; NULL
if there is nothing of it to change */
static unsigned char *fuzz_ce_changed(unsigned char const *data, size_t size, struct map_header const *header)
{
	unsigned char *changed;
	unsigned long model_data_file_offset, model_data_size;
	byte const *tags = data + header->tag_data_offset;
	boolean any = FALSE;

	if (!strcmp(fuzz_ce_change, "resources"))
	{
		long index;

		for (index = 0; index < 3; index++)
		{
			if (fuzz_resources[index].data && fuzz_resources[index].size)
			{
				fuzz_resources[index].data[fuzz_resources[index].size - 1] ^= 0x5A;
				any = TRUE;
			}
		}
		return any ? (unsigned char *)data : NULL;
	}
	changed = malloc(size);
	if (!changed)
		return NULL;
	memcpy(changed, data, size);
	memcpy(&model_data_file_offset, tags + 0x14, 4);
	memcpy(&model_data_size, tags + 0x20, 4);
	if (!strcmp(fuzz_ce_change, "tags") && header->tag_data_size > 0)
	{
		changed[header->tag_data_offset + header->tag_data_size / 2] ^= 0x5A;
		any = TRUE;
	}
	else if (!strcmp(fuzz_ce_change, "model") && model_data_size)
	{
		changed[model_data_file_offset + model_data_size / 2] ^= 0x5A;
		any = TRUE;
	}
	else if (!strcmp(fuzz_ce_change, "bsp"))
	{
		struct tag_instance const *instances;
		unsigned long tag_instances, scenario_tag_index, bsps;
		long bsp_count, bsp_index;

		/* (the checks passed: the scenario and its references are in the tags) */
		memcpy(&tag_instances, tags, 4);
		memcpy(&scenario_tag_index, tags + 4, 4);
		instances = (struct tag_instance const *)(tags + (tag_instances - CE_TAG_CACHE_BASE));
		{
			byte const *scenario = tags + (instances[scenario_tag_index & 0xFFFF].base_address - CE_TAG_CACHE_BASE);

			memcpy(&bsp_count, scenario + SCENARIO_BSPS_OFFSET, 4);
			memcpy(&bsps, scenario + SCENARIO_BSPS_OFFSET + 4, 4);
		}
		for (bsp_index = 0; bsp_index < bsp_count; bsp_index++)
		{
			struct bsp_reference reference;

			memcpy(&reference, tags + (bsps - CE_TAG_CACHE_BASE) + bsp_index * BSP_REFERENCE_SIZE, sizeof(reference));
			changed[reference.file_offset + reference.file_size / 2] ^= 0x5A;
			any = TRUE;
		}
	}
	if (!any)
	{
		free(changed);
		return NULL;
	}
	return changed;
}

/* the map loaded as scenario_tags_load and scenario_structure_bsp_load load
a Custom Edition map that passed its checks */
/* MAP_FUZZ_COPY_ROOM: the room the tag validator is given for copies, in
bytes, as a platform with less room past the tags would give it (none
given: as the game gives it, ce_resources_allocate_tags). The copies a map
gets never depend on it: a map whose copies don't fit is refused */
#ifdef MAP_FUZZ_CE
static unsigned long fuzz_copy_room(unsigned long size)
{
	unsigned long aligned = (size + 15) & ~15UL;

	if (aligned > (unsigned long)fuzz_copy_room_limit - fuzz_copy_room_used ||
		fuzz_copy_room_used > (unsigned long)fuzz_copy_room_limit)
	{
		return 0;
	}
	fuzz_copy_room_used += aligned;
	return ce_resources_allocate_tags(size);
}
#endif

static void fuzz_ce_load(unsigned char const *data, struct map_header const *header)
{
	byte *tag_cache = xbox_pointer(CE_TAG_CACHE_BASE);
	unsigned long tag_instances, scenario_tag_index, tag_count, model_data_file_offset, vertex_data_size,
		model_data_size;
	struct tag_instance *instances;
	byte *scenario;
	long bsp_count, bsp_index;
	unsigned long bsps, end_free = CE_TAG_CACHE_BASE + CE_TAG_CACHE_SIZE;
	byte *model_data;
	/* (the tag header, which no conversion writes over: ce_tags_writable) */
	byte tag_header[CE_TAG_HEADER_SIZE];
	unsigned long tags_tail;

	memset(tag_cache, 0xCD, CE_TAG_CACHE_SIZE);
	memcpy(tag_cache, data + header->tag_data_offset, (size_t)header->tag_data_size);
	/* (only the tags checked: scenario_tags_load refuses others) */
	if (!ce_map_loaded_tags_unchanged(tag_cache, (unsigned long)header->tag_data_size))
	{
		fuzz_ce_refused("tags");
		return;
	}
	memcpy(&tag_instances, tag_cache, 4);
	memcpy(&scenario_tag_index, tag_cache + 4, 4);
	memcpy(&tag_count, tag_cache + 0xC, 4);
	memcpy(&model_data_file_offset, tag_cache + 0x14, 4);
	memcpy(&vertex_data_size, tag_cache + 0x1C, 4);
	memcpy(&model_data_size, tag_cache + 0x20, 4);
	memcpy(tag_header, tag_cache, sizeof(tag_header));
	/* (a HaloMD map's last tag that begins where its tag data ends, zeroes
	after it, as it was checked: scenario_tags_load) */
	tags_tail = ce_map_checked_tags_tail();
	memset(tag_cache + header->tag_data_size, 0, tags_tail);
	instances = xbox_pointer(tag_instances);
	fuzz_tags_are_ce = TRUE;

	scenario = xbox_pointer(instances[scenario_tag_index & 0xFFFF].base_address);
	memcpy(&bsp_count, scenario + SCENARIO_BSPS_OFFSET, 4);
	memcpy(&bsps, scenario + SCENARIO_BSPS_OFFSET + 4, 4);
	for (bsp_index = 0; bsp_index < bsp_count; bsp_index++)
	{
		unsigned long base;

		memcpy(&base, (byte *)xbox_pointer(bsps) + bsp_index * BSP_REFERENCE_SIZE + 8, 4);
		if (base > CE_TAG_CACHE_BASE && base < end_free)
			end_free = base;
	}
	ce_loaded_bsps_low(end_free);
	ce_repairs_scenario_loaded(instances, (long)tag_count, scenario_tag_index);
	ce_resources_tags_loaded(instances, (long)tag_count, CE_TAG_CACHE_BASE + header->tag_data_size + tags_tail,
		end_free);
	if (!ce_map_loaded_unchanged())
	{
		fuzz_ce_refused("resources");
		return;
	}
	ce_repairs_tags_loaded(instances, (long)tag_count, scenario_tag_index);
	/* (function fields made valid ones, those in the other byte order
	turned back, before the schema checks them: scenario_tags_load) */
	ce_functions_tags_loaded(instances, (long)tag_count);
	/* (every tag, its own and its resources', through Halo PC's schema
	before the conversions read them: scenario_tags_load refuses the map if
	not; and the tags as corrected need no more correcting, as an Xbox
	map's) */
	if (fuzz_ce_schema)
	{
		unsigned long tags_end = ce_resources_free_mark();
		unsigned long own_size = (unsigned long)header->tag_data_size + tags_tail;
		long tags_size = tags_end > CE_TAG_CACHE_BASE + own_size &&
			tags_end <= CE_TAG_CACHE_BASE + CE_TAG_CACHE_SIZE ?
			(long)(tags_end - CE_TAG_CACHE_BASE) : (long)own_size;

		/* (OpenSauce's layout for a map with OpenSauce's header, as
		cache_files.c chooses it: port/linux/game/opensauce_map.c) */
		unsigned long opensauce_signature;
		struct tag_schema_layout const *layout;
		char const *layout_name;
		boolean valid_tags;

		memcpy(&opensauce_signature, data + 0x70, sizeof(opensauce_signature));
		layout = opensauce_signature == 'yelo' ? &tag_schema_opensauce_layout : &tag_schema_pc_layout;
		layout_name = opensauce_signature == 'yelo' ? "OpenSauce" : "Halo PC";
		/* (overlapping blocks given copies of their own past the tags, as
		scenario_tags_load gives them room: ce_resources_allocate_tags) */
		tag_validate_room_proc = fuzz_copy_room_limit >= 0 ? fuzz_copy_room : ce_resources_allocate_tags;
		fuzz_copy_room_used = 0;
		valid_tags = tag_validate_tags_layout(layout, tag_cache, tags_size, header->file_length, "fuzz");
		tag_validate_room_proc = NULL;
		if (!valid_tags)
		{
			if (fuzz_verbose)
				fprintf(stderr, "map_fuzz: the map was refused by %s's tag schema\n", layout_name);
			return;
		}
		if (fuzz_verbose)
			fprintf(stderr, "map_fuzz: %s's tag schema made %ld corrections\n", layout_name,
				(long)tag_validate_corrections());
		if (tag_validate_corrections())
		{
			/* (the copies are the tags' now: in what they take) */
			unsigned long copies_end = ce_resources_free_mark();
			long checked_size = copies_end > CE_TAG_CACHE_BASE + (unsigned long)tags_size &&
				copies_end <= CE_TAG_CACHE_BASE + CE_TAG_CACHE_SIZE ?
				(long)(copies_end - CE_TAG_CACHE_BASE) : tags_size;
			boolean valid = tag_validate_tags_layout(layout, tag_cache, checked_size, header->file_length, "fuzz");

			if (!valid || tag_validate_corrections())
			{
				fprintf(stderr, "map_fuzz: the tags as corrected are not clean when checked again (%s, %ld more)\n",
					valid ? "accepted" : "refused", (long)tag_validate_corrections());
				abort();
			}
		}
	}
	model_data = system_malloc((long)model_data_size);
	if (model_data)
	{
		memcpy(model_data, data + model_data_file_offset, model_data_size);
		ce_models_tags_loaded(instances, (long)tag_count, model_data, vertex_data_size, model_data_size);
		if (!ce_map_loaded_unchanged())
		{
			system_free(model_data);
			fuzz_ce_refused("model");
			return;
		}
		ce_shaders_tags_loaded(instances, (long)tag_count);
		system_free(model_data);
	}
	ce_hud_tags_loaded(instances, (long)tag_count);
	if (memcmp(tag_header, tag_cache, sizeof(tag_header)))
	{
		fprintf(stderr, "map_fuzz: a conversion wrote over the tag header\n");
		abort();
	}
	/* (checked again as they loaded: scenario_tags_load refuses them if not) */
	if (!ce_map_check_loaded(instances, (long)tag_count, scenario_tag_index, "fuzz"))
	{
		if (fuzz_verbose)
			fprintf(stderr, "map_fuzz: the map was refused as it loaded\n");
		return;
	}
	if (fuzz_verbose)
		fprintf(stderr, "map_fuzz: the map loaded\n");

	/* each BSP, read where it loads (checked: ce_bsps_check) */
	for (bsp_index = 0; bsp_index < bsp_count; bsp_index++)
	{
		struct bsp_reference reference;
		unsigned long bsp_base;

		memcpy(&reference, (byte *)xbox_pointer(bsps) + bsp_index * BSP_REFERENCE_SIZE, sizeof(reference));
		/* (only as it was checked: scenario_structure_bsp_load) */
		if (!ce_map_bsp_reference_checked(&reference))
		{
			if (fuzz_verbose)
				fprintf(stderr, "map_fuzz: structure BSP %ld is not where it was checked\n", (long)bsp_index);
			continue;
		}
		memcpy(xbox_pointer(reference.base_address), data + reference.file_offset, (size_t)reference.file_size);
		if (!ce_map_loaded_bsp_unchanged(&reference, xbox_pointer(reference.base_address),
			(unsigned long)reference.file_size))
		{
			fuzz_ce_refused("bsp");
			continue;
		}
		memcpy(&bsp_base, xbox_pointer(reference.base_address), 4);
		/* (its predicted resources repaired, then through Halo PC's schema,
		before its vertices are compressed: scenario_structure_bsp_load) */
		ce_repairs_bsp_loaded(xbox_pointer(bsp_base));
		if (fuzz_ce_schema &&
			!tag_validate_structure_bsp(reference.tag_index, xbox_pointer(reference.base_address), reference.file_size))
		{
			if (fuzz_verbose)
				fprintf(stderr, "map_fuzz: structure BSP %ld was refused by Halo PC's tag schema\n", (long)bsp_index);
			continue;
		}
		/* (as it was corrected, it needs no more correction: a correction
		that changed what another block of it was checked to read would
		leave that block unchecked) */
		if (fuzz_ce_schema)
		{
			long corrections = tag_validate_corrections();
			boolean valid = tag_validate_structure_bsp(reference.tag_index, xbox_pointer(reference.base_address),
				reference.file_size);

			if (!valid || tag_validate_corrections() != corrections)
			{
				fprintf(stderr, "map_fuzz: structure BSP %ld as corrected is not clean when checked again (%s, %ld more)\n",
					(long)bsp_index, valid ? "accepted" : "refused", (long)(tag_validate_corrections() - corrections));
				abort();
			}
		}
		ce_bsp_loaded(xbox_pointer(bsp_base));
		ce_bsp_unloaded();
	}
}

static void fuzz_ce(unsigned char const *data, size_t size)
{
	struct map_header header_copy;
	struct map_header const *header = &header_copy;

	if (size < HEADER_SIZE)
		return;
	memcpy(&header_copy, data, sizeof(header_copy));
	/* (a length of 0, which Invader may give, read as the file's, as
	cache_files_windows.c reads it) */
	if (!header_copy.file_length && size <= 0x7fffffff)
		header_copy.file_length = (long)size;
	/* (a HaloMD map's tag data size given as its file's length, read as
	the rest of the file, as cache_file_header_verify reads it) */
	{
		long tag_data_size = ce_map_header_tag_data_size(header_copy.version, header_copy.file_length,
			header_copy.tag_data_offset, header_copy.tag_data_size);

		if (fuzz_verbose && tag_data_size != header_copy.tag_data_size)
			fprintf(stderr, "map_fuzz: the header's tag data size is the file's length: read as %ld\n", tag_data_size);
		header_copy.tag_data_size = tag_data_size;
	}
	if (!fuzz_header_valid(header, size) ||
		(header->version != CACHE_VERSION_CUSTOM_EDITION && header->version != CACHE_VERSION_RETAIL))
	{
		return;
	}
	memset(&fuzz_map_file, 0, sizeof(fuzz_map_file));
	fuzz_map_file.data = data;
	fuzz_map_file.size = (unsigned long)size;
	fuzz_tags_are_ce = FALSE;
	ce_map_cache_version = header->version;
	if (setjmp(fuzz_exit))
	{
		fuzz_exit_armed = FALSE;
		fuzz_tags_are_ce = FALSE;
		ce_resources_tags_unloaded();
		ce_models_tags_unloaded();
		ce_hud_tags_unloaded();
		return;
	}
	fuzz_exit_armed = TRUE;
	if (ce_map_check(&fuzz_map_file, "fuzz", header->file_length, header->tag_data_offset, header->tag_data_size))
	{
		unsigned char *changed = fuzz_ce_change ? fuzz_ce_changed(data, size, header) : NULL;

		if (fuzz_verbose)
			fprintf(stderr, "map_fuzz: the map passed its checks\n");
		fuzz_ce_load(changed ? changed : data, header);
		/* (a frame of the game under way: what the load left to tell the
		player, ce_resources_update) */
		ce_resources_update();
		if (changed && changed != data)
			free(changed);
		/* (the resource maps as they were, for the next input) */
		if (changed == data)
		{
			long index;

			for (index = 0; index < 3; index++)
			{
				if (fuzz_resources[index].data && fuzz_resources[index].size)
					fuzz_resources[index].data[fuzz_resources[index].size - 1] ^= 0x5A;
			}
		}
	}
	else if (fuzz_verbose)
	{
		fprintf(stderr, "map_fuzz: the map was refused by its checks\n");
	}
	fuzz_exit_armed = FALSE;
	fuzz_tags_are_ce = FALSE;
	ce_resources_tags_unloaded();
	ce_models_tags_unloaded();
	ce_hud_tags_unloaded();
}
#endif

#ifdef MAP_FUZZ_FORGE_AI
void forge_ai_tags_loaded(char const *scenario_name);
boolean forge_ai_tags_update(void);
void forge_ai_tags_map_unloaded(void);
boolean forge_ai_tags_active(void);
boolean forge_ai_tags_read(long tag_index, long offset, long size, void *buffer, boolean *read);

/* the multiplayer map's own tag table (cache_files.c's): its scenario
alone, then the characters' tags forge_ai_tags.c adds after it */
static struct tag_instance fuzz_forge_own[1];
static void *fuzz_forge_instances;
static long fuzz_forge_count;
unsigned long cache_files_reserved_after_tags;

void platform_log(char const *format, ...)
{
	if (fuzz_verbose)
	{
		va_list arguments;

		va_start(arguments, format);
		fprintf(stderr, "map_fuzz: ");
		vfprintf(stderr, format, arguments);
		fprintf(stderr, "\n");
		va_end(arguments);
	}
}

boolean forge_ai_tags_wanted(char const *scenario_name)
{
	return TRUE;
}

void *cache_files_tag_instances(long *count)
{
	*count = fuzz_forge_count;
	return fuzz_forge_instances;
}

void cache_files_set_tag_instances(void *instances, long count)
{
	fuzz_forge_instances = instances;
	fuzz_forge_count = count;
}

/* (no room past the map's tags: the characters' tags go in the heap) */
void *cache_files_tag_room(unsigned long *size)
{
	*size = 0;
	return NULL;
}

/* (the multiplayer map has none of the campaign's tags) */
long tag_loaded(long group_tag, const char *name)
{
	return NONE;
}

char *tag_get_name(long tag_index)
{
	return "fuzz";
}

long tag_get_group_tag(long tag_index)
{
	return NONE;
}

void D3DResource_BlockUntilNotBusy(struct D3DResource *resource)
{
}

/* the input is a campaign map's file expanded (its header, then the rest;
with MAP_FUZZ_BASE, a campaign map inflated and changed), written as the
game finds one: the header, then the rest as one zlib stream
of stored blocks, which forge_ai_tags.c inflates */
static void fuzz_forge_ai(unsigned char const *data, size_t size)
{
	enum { STORED_BLOCK = 0xFFFF };
	size_t body = size - HEADER_SIZE;
	size_t blocks = body / STORED_BLOCK + 1;
	size_t at;
	unsigned long adler = 1, s1 = 1, s2 = 0;
	unsigned char *file;
	unsigned char *out;

	/* (forge_ai_tags.c's FORGE_AI_MAXIMUM_FILE) */
	if (size < HEADER_SIZE || size > 0x20000000)
		return;
	file = malloc(HEADER_SIZE + 2 + blocks * 5 + body + 4);
	if (!file)
		return;
	memcpy(file, data, HEADER_SIZE);
	out = file + HEADER_SIZE;
	*out++ = 0x78;
	*out++ = 0x01;
	for (at = 0; at < body || at == 0; )
	{
		size_t length = body - at < STORED_BLOCK ? body - at : STORED_BLOCK;
		boolean last = at + length >= body;

		*out++ = last ? 1 : 0;
		*out++ = (unsigned char)(length & 0xFF);
		*out++ = (unsigned char)(length >> 8);
		*out++ = (unsigned char)(~length & 0xFF);
		*out++ = (unsigned char)((~length >> 8) & 0xFF);
		memcpy(out, data + HEADER_SIZE + at, length);
		out += length;
		at += length;
		if (last)
			break;
	}
	for (at = 0; at < body; at++)
	{
		s1 = (s1 + data[HEADER_SIZE + at]) % 65521;
		s2 = (s2 + s1) % 65521;
	}
	adler = (s2 << 16) | s1;
	*out++ = (unsigned char)(adler >> 24);
	*out++ = (unsigned char)(adler >> 16);
	*out++ = (unsigned char)(adler >> 8);
	*out++ = (unsigned char)adler;
	fuzz_forge_map = file;
	fuzz_forge_map_size = (unsigned long)(out - file);

	memset(fuzz_forge_own, 0, sizeof(fuzz_forge_own));
	fuzz_forge_own[0].group_tag = SCENARIO_GROUP;
	fuzz_forge_own[0].parent_group_tags[0] = NONE;
	fuzz_forge_own[0].parent_group_tags[1] = NONE;
	fuzz_forge_own[0].tag_index = (long)0xE1740000;
	fuzz_forge_instances = fuzz_forge_own;
	fuzz_forge_count = 1;
	forge_ai_tags_loaded("fuzz");
	forge_ai_tags_update();
	/* (each tag brought in: its first pixels or samples read back, as the
	texture and sound caches read them) */
	if (forge_ai_tags_active())
	{
		long index;

		for (index = 1; index < fuzz_forge_count; index++)
		{
			unsigned char buffer[256];
			boolean read = FALSE;

			forge_ai_tags_read(((struct tag_instance *)fuzz_forge_instances)[index].tag_index, 0, sizeof(buffer), buffer,
				&read);
		}
	}
	forge_ai_tags_map_unloaded();
	fuzz_forge_instances = NULL;
	fuzz_forge_count = 0;
	fuzz_forge_map = NULL;
	fuzz_forge_map_size = 0;
	free(file);
}
#endif

int LLVMFuzzerTestOneInput(unsigned char const *data, size_t size)
{
#ifdef MAP_FUZZ_VORBIS
	{
		int channels = 0, rate = 0;
		short *samples = NULL;
		int frames;

		if (size > 0x1000000)
			return 0;
		frames = ce_vorbis_decode(data, (int)size, &channels, &rate, &samples);
		/* (every sample given is read) */
		if (frames > 0 && samples)
		{
			volatile short sink = samples[(size_t)frames * (size_t)channels - 1];

			(void)sink;
		}
		/* (its ends read alone, as ce_resources.c reads a neighbor's) */
		{
			short head[48 * 2], tail[48 * 2];
			int edge_channels, edge_rate, head_frames, tail_frames;

			ce_vorbis_edges(data, (int)size, 48, 1, &edge_channels, &edge_rate, head, &head_frames, tail, &tail_frames);
			ce_vorbis_edges(data, (int)size, 48, 0, &edge_channels, &edge_rate, head, &head_frames, tail, &tail_frames);
		}
		/* (and halved, then doubled, with its own first frames as both
		neighbors: ce_resources.c's joins) */
		if (frames > 0 && samples && frames <= 0x100000)
		{
			int edge = frames < 48 ? frames : 48;
			short *context = malloc((size_t)edge * (size_t)channels * sizeof(*context));

			if (context)
			{
				int halved;

				memcpy(context, samples, (size_t)edge * (size_t)channels * sizeof(*context));
				halved = ce_vorbis_resample_piece(&samples, frames, channels, 44100, 22050, context, edge, context, edge);
				if (halved > 0)
					ce_vorbis_resample_piece(&samples, halved, channels, 22050, 44100, context, edge / 2, context, edge / 2);
				free(context);
			}
		}
		ce_vorbis_free(samples);
#if HALO_MCC
		/* (and through MCC's decoder, its pages checked first) */
		{
			short *pcm = NULL;
			uint32_t mcc_frames = 0, mcc_rate = 0;
			int mcc_channels = 0;

			if (mcc_vorbis_decode(data, (uint32_t)size, 0x1000000u, &pcm, &mcc_frames, &mcc_rate, &mcc_channels) && pcm)
			{
				volatile short sink = pcm[(size_t)mcc_frames * (size_t)mcc_channels - 1];

				(void)sink;
			}
			if (pcm)
				mcc_vorbis_free(pcm);
		}
#endif
	}
#else
#ifdef MAP_FUZZ_CE
	/* (the input a resource map, read with the map named: MAP_FUZZ_CE_RESOURCE;
	kept until the next input's, as a file the game holds open stays) */
	if (fuzz_resource_input >= 0)
	{
		static unsigned char *resource;
		unsigned char *map = malloc(fuzz_resource_map_size);

		free(resource);
		resource = malloc(size ? size : 1);
		if (resource)
			memcpy(resource, data, size);
		fuzz_resources[fuzz_resource_input].data = resource;
		fuzz_resources[fuzz_resource_input].size = resource ? (unsigned long)size : 0;
		if (resource && map)
		{
			memcpy(map, fuzz_resource_map, fuzz_resource_map_size);
			fuzz_ce(map, fuzz_resource_map_size);
		}
		free(map);
		return 0;
	}
#endif
	if (fuzz_base)
		data = fuzz_patch(data, size, &size);
	{
		/* (the input in its own allocation, so that AddressSanitizer guards
		its end) */
		unsigned char *copy = fuzz_base ? NULL : malloc(size ? size : 1);
		unsigned char const *map = data;

		if (copy)
		{
			memcpy(copy, data, size);
			map = copy;
		}
#if defined(MAP_FUZZ_XBOX)
		fuzz_xbox(map, size);
#elif defined(MAP_FUZZ_FORGE_AI)
		fuzz_forge_ai(map, size);
#else
		fuzz_ce(map, size);
#endif
		free(copy);
	}
#endif
	return 0;
}

/* ---------- the C library, by the names the game's headers give it
(port/linux/include/stdio.h; musl-math's, as the game links it) */

#undef snprintf
#undef vsnprintf
#undef sprintf
#undef vsprintf
#undef fprintf
#undef vfprintf
#undef fopen

int halo_linux_vsnprintf(char *buffer, size_t count, char const *format, va_list arguments)
{
	return vsnprintf(buffer, count, format, arguments);
}

int halo_linux_snprintf(char *buffer, size_t count, char const *format, ...)
{
	va_list arguments;
	int result;

	va_start(arguments, format);
	result = vsnprintf(buffer, count, format, arguments);
	va_end(arguments);
	return result;
}

int halo_linux_vsprintf(char *buffer, char const *format, va_list arguments)
{
	return vsprintf(buffer, format, arguments);
}

int halo_linux_sprintf(char *buffer, char const *format, ...)
{
	va_list arguments;
	int result;

	va_start(arguments, format);
	result = vsprintf(buffer, format, arguments);
	va_end(arguments);
	return result;
}

int halo_linux_vfprintf(FILE *stream, char const *format, va_list arguments)
{
	return vfprintf(stream, format, arguments);
}

int halo_linux_fprintf(FILE *stream, char const *format, ...)
{
	va_list arguments;
	int result;

	va_start(arguments, format);
	result = vfprintf(stream, format, arguments);
	va_end(arguments);
	return result;
}

FILE *halo_linux_fopen(char const *path, char const *mode)
{
	return fopen(path, mode);
}

double halo_cos(double x) { return __builtin_cos(x); }
double halo_sin(double x) { return __builtin_sin(x); }
double halo_exp(double x) { return __builtin_exp(x); }
double halo_log(double x) { return __builtin_log(x); }
double halo_pow(double x, double y) { return __builtin_pow(x, y); }

/* (debug_memory.c's, which the game's units' malloc is (cseries.h): in the
game's heap, the Xbox address space) */
#undef malloc
#undef free
#undef realloc

void *debug_malloc(unsigned int size, boolean zero, char const *file, long line)
{
	return fuzz_arena_allocate(size, zero);
}

void debug_free(void *pointer, char const *file, long line)
{
	fuzz_arena_free(pointer);
}

void *debug_realloc(void *pointer, unsigned int size, char const *file, long line)
{
	byte *larger = fuzz_arena_allocate(size, FALSE);

	if (larger && pointer)
	{
		unsigned long old_size;

		old_size = fuzz_arena_size(pointer);
		memcpy(larger, pointer, old_size < size ? old_size : size);
		fuzz_arena_free(pointer);
	}
	return larger;
}

#undef memset
#undef memcpy
#undef memmove
#undef memcmp
#undef strlen
#undef strcat
#undef strcmp

void *csmemset(void *buffer, long c, unsigned long size)
{
	return memset(buffer, (int)c, size);
}

void *csmemcpy(void *destination, void const *source, unsigned long size)
{
	return memcpy(destination, source, size);
}

void *csmemmove(void *destination, void const *source, unsigned long size)
{
	return memmove(destination, source, size);
}

long csmemcmp(void const *a, void const *b, unsigned long size)
{
	return memcmp(a, b, size);
}

unsigned long csstrlen(char const *s)
{
	return strlen(s);
}

char *csstrcat(char *s1, char const *s2)
{
	return strcat(s1, s2);
}

long csstrcmp(char const *s1, char const *s2)
{
	return strcmp(s1, s2);
}

/* (ce_map_checks.c asks whether an address is the menus' own tags: menu_tags.c, not in the harness) */
boolean pc_menu_owns(unsigned long address, long size)
{
	(void)address;
	(void)size;
	return FALSE;
}

#include "opensauce_maps.h"
/* (SPV3 mode, spv3_mode.c, and OpenSauce's larger tag window,
opensauce_maps.c, not in the harness: always off, so the checks see Custom
Edition's own limits) */
boolean spv3_mode_active(void) { return FALSE; }
unsigned long opensauce_maps_tag_cache_size(unsigned long custom_edition_size) { return custom_edition_size; }
boolean opensauce_maps_accepts_header(char const *map_name, unsigned short flags)
{
	(void)map_name;
	(void)flags;
	return FALSE;
}
void opensauce_maps_check_started(void) { }
void opensauce_maps_session_now(struct opensauce_maps_session *session)
{
	session->hosting = session->hosting_internet = session->joined = session->host_on_lan = FALSE;
}
boolean opensauce_maps_online_allows(char const *map_name, unsigned short flags,
	struct opensauce_maps_session const *session)
{
	(void)map_name;
	(void)flags;
	(void)session;
	return TRUE;
}
void opensauce_maps_check_done(void) { }
void spv3_mode_log(char const *format, ...) { (void)format; }

/* (an OpenSauce map's log, opensauce_survey.c, not in the harness: nothing
logged) */
boolean opensauce_survey_wanted(void const *cache_header)
{
	(void)cache_header;
	return FALSE;
}
void opensauce_survey_map(char const *map_name, void const *cache_header, void const *tags,
	unsigned long tag_data_size, unsigned long file_size)
{
	(void)map_name;
	(void)cache_header;
	(void)tags;
	(void)tag_data_size;
	(void)file_size;
}
void opensauce_survey_checked(char const *map_name, char const *refusal)
{
	(void)map_name;
	(void)refusal;
}
void opensauce_survey_note_resource_miss(char const *kind, char const *tag_name)
{
	(void)kind;
	(void)tag_name;
}

/* (tag_validate.c asks whether tag data is Stubbs the Zombie's own, and
whether Stubbs mode is on for its layout: stubbs_mode.c, not in the
harness) */
boolean stubbs_mode_owns_data(void const *address, long size)
{
	(void)address;
	(void)size;
	return FALSE;
}
boolean stubbs_mode_active(void)
{
	return FALSE;
}
