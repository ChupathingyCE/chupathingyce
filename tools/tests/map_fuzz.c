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
                    shaders and HUD converted, each BSP's vertices compressed.
                    The checks are the contract the conversions trust: a map
                    they pass must convert without touching memory it does
                    not own.
  MAP_FUZZ_VORBIS   an Ogg Vorbis stream through ce_vorbis_decode (stb_vorbis),
                    as a Custom Edition sound's permutation is decoded.

An input is the map file itself (uncompressed: an Xbox map's tags inflated as
cache_files.c inflates them), or, with MAP_FUZZ_BASE naming a map file, a list
of changes to that map (each 8 bytes: what to change and a value), which
reaches deeper into a real map than whole-file mutations do. Custom Edition
maps' resource maps (bitmaps.map, sounds.map, loc.map) are read from the
folder MAP_FUZZ_CE_RESOURCES names, if any: without them, a map that keeps
tags there is refused as the game refuses it.

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

#include <sanitizer/asan_interface.h>

/* (the harness's own memory is the host's: cseries.h gives the game's units
the game's heap, debug_malloc below) */
#undef malloc
#undef free
#undef realloc

#if !defined(MAP_FUZZ_XBOX) && !defined(MAP_FUZZ_CE) && !defined(MAP_FUZZ_VORBIS)
#error define MAP_FUZZ_XBOX, MAP_FUZZ_CE or MAP_FUZZ_VORBIS
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
};

/* ---------- prototypes */

int LLVMFuzzerInitialize(int *argc, char ***argv);
int LLVMFuzzerTestOneInput(unsigned char const *data, size_t size);

boolean tag_validate_tags(void *tag_header, long tag_data_size, long file_length, char const *map_name);
boolean tag_validate_structure_bsp(long tag_index, void *base, long size);
long tag_validate_corrections(void);

boolean ce_map_check(HANDLE file, char const *map_name, long file_length, long tag_data_offset, long tag_data_size);
extern long ce_map_cache_version;
boolean ce_resources_tags_loaded(void *tag_instances, long tag_count, unsigned long first_free,
	unsigned long end_free);
void ce_repairs_tags_loaded(void *tag_instances, long tag_count, unsigned long scenario_tag_index);
boolean ce_models_tags_loaded(void *tag_instances, long tag_count, byte const *model_data,
	unsigned long vertex_data_size);
void ce_functions_tags_loaded(void *tag_instances, long tag_count);
void ce_shaders_tags_loaded(void *tag_instances, long tag_count);
void ce_hud_tags_loaded(void *tag_instances, long tag_count);
void ce_bsp_loaded(void *structure);
void ce_repairs_bsp_loaded(void *structure);
void ce_bsp_unloaded(void);
void ce_resources_tags_unloaded(void);
void ce_models_tags_unloaded(void);
void ce_hud_tags_unloaded(void);

int ce_vorbis_decode(const unsigned char *data, int size, int *channels, int *sample_rate, short **samples);
void ce_vorbis_free(short *samples);

/* ---------- globals */

static boolean fuzz_verbose;
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
} fuzz_resources[] = { { "bitmaps" }, { "sounds" }, { "loc" } };
static struct fuzz_file fuzz_map_file;
static struct fuzz_file fuzz_open_files[8];
static boolean fuzz_tags_are_ce;
/* the game exiting on purpose (system_exit), or asserting */
static jmp_buf fuzz_exit;
static boolean fuzz_exit_armed;
static unsigned long fuzz_arena_next;
static unsigned long fuzz_arena_end;

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
	fuzz_commit(CE_TAG_CACHE_BASE, CE_TAG_CACHE_SIZE);
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

unsigned long system_milliseconds(void)
{
	return 0;
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
made, no compressed block decoded) */
void rasterizer_bitmap_changed(void *bitmap) {}
void rasterizer_bitmap_delete(void *bitmap) {}
boolean rasterizer_bitmap_new(void *bitmap) { return TRUE; }
void DecodeBlockAlpha3__single_pixel(void) {}
void DecodeBlockAlpha4__single_pixel(void) {}
void DecodeBlockRGB__single_pixel(void) {}
void bitmap_swizzle_vector2d(void) {}

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
		{
			long resource;

			for (resource = 0; resource < 3; resource++)
			{
				if (!strcmp(path, fuzz_resources[resource].name) && fuzz_resources[resource].data)
				{
					file->data = fuzz_resources[resource].data;
					file->size = fuzz_resources[resource].size;
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

DWORD GetFileSize(HANDLE handle, LPDWORD high)
{
	struct fuzz_file *file = handle;

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

void ce_vorbis_free(short *samples)
{
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

	fuzz_verbose = getenv("MAP_FUZZ_VERBOSE") != NULL;
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
/* the map loaded as scenario_tags_load and scenario_structure_bsp_load load
a Custom Edition map that passed its checks */
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

	memset(tag_cache, 0xCD, CE_TAG_CACHE_SIZE);
	memcpy(tag_cache, data + header->tag_data_offset, (size_t)header->tag_data_size);
	memcpy(&tag_instances, tag_cache, 4);
	memcpy(&scenario_tag_index, tag_cache + 4, 4);
	memcpy(&tag_count, tag_cache + 0xC, 4);
	memcpy(&model_data_file_offset, tag_cache + 0x14, 4);
	memcpy(&vertex_data_size, tag_cache + 0x1C, 4);
	memcpy(&model_data_size, tag_cache + 0x20, 4);
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
	ce_resources_tags_loaded(instances, (long)tag_count, CE_TAG_CACHE_BASE + header->tag_data_size, end_free);
	ce_repairs_tags_loaded(instances, (long)tag_count, scenario_tag_index);
	model_data = system_malloc((long)model_data_size);
	if (model_data)
	{
		memcpy(model_data, data + model_data_file_offset, model_data_size);
		ce_models_tags_loaded(instances, (long)tag_count, model_data, vertex_data_size);
		ce_functions_tags_loaded(instances, (long)tag_count);
		ce_shaders_tags_loaded(instances, (long)tag_count);
		system_free(model_data);
	}
	ce_hud_tags_loaded(instances, (long)tag_count);

	/* each BSP, read where it loads (checked: ce_bsps_check) */
	for (bsp_index = 0; bsp_index < bsp_count; bsp_index++)
	{
		struct bsp_reference reference;
		unsigned long bsp_base;

		memcpy(&reference, (byte *)xbox_pointer(bsps) + bsp_index * BSP_REFERENCE_SIZE, sizeof(reference));
		memcpy(xbox_pointer(reference.base_address), data + reference.file_offset, (size_t)reference.file_size);
		memcpy(&bsp_base, xbox_pointer(reference.base_address), 4);
		ce_bsp_loaded(xbox_pointer(bsp_base));
		ce_repairs_bsp_loaded(xbox_pointer(bsp_base));
		ce_bsp_unloaded();
	}
}

static void fuzz_ce(unsigned char const *data, size_t size)
{
	struct map_header const *header = (struct map_header const *)data;

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
		fuzz_ce_load(data, header);
	fuzz_exit_armed = FALSE;
	fuzz_tags_are_ce = FALSE;
	ce_resources_tags_unloaded();
	ce_models_tags_unloaded();
	ce_hud_tags_unloaded();
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
		ce_vorbis_free(samples);
	}
#else
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
#ifdef MAP_FUZZ_XBOX
		fuzz_xbox(map, size);
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

