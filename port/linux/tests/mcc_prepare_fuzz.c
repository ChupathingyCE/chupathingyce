/*
MCC_PREPARE_FUZZ.C

libFuzzer's entry for what loading does with an MCC map's tags after the
version-13 reader passes them (tools/test_host_fuzz.py builds it, target
mcc_prepare), in mcc_cache.c's order: the HUD and tag normalizations
(mcc_hud.c, mcc_tags.c), the scripts (mcc_scripts.c, mcc_syntax.c,
mcc_script_parameters.c), the models' geometry (mcc_geometry.c), and then
the MCC tag validator over the result (mcc_tag_validate.c, with the schemas
of tag_schema*.c). Each stage runs only if the one before it passed, as in
the game. A Workshop map is anyone's.

The input is the map file itself. Its tags go where the game puts them, at
the 32-bit addresses they are linked for, in the Xbox address space this
build keeps at XBOX_ADDRESS_SPACE_BASE (above AddressSanitizer's shadow, as
tools/test_map_fuzz.py places it): only maps linked at 0x50000000 (the
window every MCC map seen uses) are loaded. The game's heap (debug_malloc,
system_malloc, which must give Xbox addresses) is an arena above it,
poisoned between allocations.

The game's units are built as the 64-bit build builds them (long 32 bits,
tools/lp64_rewrite.py), so what they call here takes int where the source
says long. Bitmaps and sounds are not converted here (mcc_bitmaps.c,
mcc_audio.c; the reader target covers their ranges): the validator is told
their bitmaps are valid, so it goes on past them.

mcc_runtime_pointer, mcc_runtime_read, mcc_runtime_allocate and
mcc_cache_contains are mcc_cache.c's, as they are there; as there, the map
is not yet "loaded" (mcc_cache_tags_loaded) while these stages run.
*/

#include "../game/mcc_cache_format.h"

#include <sanitizer/asan_interface.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>

#define XBOX_BASE 0x300000000000ULL
#define TAG_WINDOW 0x50000000u
#define TAG_CAPACITY 0x04000000u
#define ARENA 0x60000000u
#define ARENA_SIZE 0x10000000u
#define REDZONE 64u
#define NONE (-1)

static void *xp(uint32_t address) { return address ? (void *)(uintptr_t)(XBOX_BASE + address) : NULL; }
static uint32_t xa(void const *pointer) { return pointer ? (uint32_t)((uintptr_t)pointer - XBOX_BASE) : 0; }

/* (mcc_runtime.h's, its layout as the game's units have it) */
struct mcc_runtime {
	char level_name[64];
	char failure[320];
	unsigned char *tags;
	unsigned char *tag_index;
	uint32_t used;
	uint32_t capacity;
	struct mcc_cache_source source;
	struct mcc_cache_report report;
	void *geometry, *audio, *bitmaps, *resources;
};

int mcc_hud_bitmaps_prepare(struct mcc_runtime *runtime);
int mcc_tags_prepare(struct mcc_runtime *runtime);
int mcc_scripts_prepare(struct mcc_runtime *runtime);
int mcc_geometry_prepare(struct mcc_runtime *runtime);
void mcc_geometry_dispose(struct mcc_runtime *runtime);
int mcc_tags_validate(struct mcc_runtime *runtime);
void mcc_validation_dispose(struct mcc_runtime *runtime);
void mcc_parameters_dispose(void);

static struct mcc_runtime runtime;

/* ---------- the arena (the game's heap) */

static uint32_t arena_next;

static void arena_reset(void)
{
	arena_next = ARENA;
	ASAN_POISON_MEMORY_REGION(xp(ARENA), ARENA_SIZE);
}

static void *arena_allocate(unsigned int size)
{
	uint32_t start = (arena_next + REDZONE + 15u) & ~15u;
	void *result;

	if (size > ARENA_SIZE - 2 * REDZONE || start - ARENA > ARENA_SIZE - REDZONE - size)
		return NULL;
	result = xp(start);
	ASAN_UNPOISON_MEMORY_REGION(result, size);
	arena_next = start + size;
	return result;
}

void *debug_malloc(unsigned int size, int clear, char const *file, int line)
{
	void *result = arena_allocate(size);

	(void)file; (void)line;
	if (result && clear)
		memset(result, 0, size);
	return result;
}
void debug_free(void *pointer, char const *file, int line) { (void)pointer; (void)file; (void)line; }
void *system_malloc(int size) { return size < 0 ? NULL : debug_malloc((unsigned int)size, 0, NULL, 0); }
void system_free(void *pointer) { (void)pointer; }

/* ---------- mcc_cache.c's */

void *mcc_runtime_pointer(struct mcc_runtime *r, uint32_t address, uint32_t bytes)
{
	uint32_t offset = address - r->report.tag_base;

	if (address < r->report.tag_base || offset > r->used || bytes > r->used - offset)
		return NULL;
	return r->tags + offset;
}

int mcc_runtime_read(struct mcc_runtime *r, uint32_t offset, uint32_t bytes, void *out)
{
	return (uint64_t)offset <= r->source.size && bytes <= r->source.size - offset &&
		r->source.read(r->source.context, offset, bytes, out);
}

void *mcc_runtime_allocate(struct mcc_runtime *r, uint32_t bytes)
{
	uint32_t limit = r->capacity, index;
	uint32_t start = (r->used + 15u) & ~15u;
	void *result;

	for (index = 0; index < r->report.bsp_count; ++index)
	{
		uint32_t bsp_offset = r->report.bsps[index].address - r->report.tag_base;

		if (bsp_offset < limit)
			limit = bsp_offset;
	}
	if (start < r->used || start > limit || bytes > limit - start)
		return NULL;
	result = r->tags + start;
	memset(result, 0, bytes);
	r->used = start + bytes;
	return result;
}

int mcc_cache_tags_loaded(void) { return 0; }
int mcc_cache_contains(void const *address, int bytes) { (void)address; (void)bytes; return 0; }

/* ---------- what the stages call that this target does not run */

void mcc_loading_update(int stage, unsigned int done, unsigned int total) { (void)stage; (void)done; (void)total; }
/* (bitmaps: valid, see above; sounds' and bitmaps' streams: none) */
int mcc_bitmaps_valid(struct mcc_runtime *r, void *bitmap) { (void)r; (void)bitmap; return 1; }
int mcc_bitmaps_group_valid(struct mcc_runtime *r, void *group) { (void)r; (void)group; return 1; }
int mcc_bitmaps_contains(struct mcc_runtime *r, uint32_t offset, uint32_t bytes) { (void)r; (void)offset; (void)bytes; return 0; }
int mcc_audio_contains(struct mcc_runtime *r, int tag, uint32_t offset, uint32_t bytes)
{
	(void)r; (void)tag; (void)offset; (void)bytes;
	return 0;
}

/* (the script functions and globals: every name one there is, so the
scripts' checks go on) */
short hs_find_function_by_name(char const *name) { (void)name; return 1; }
short hs_find_global_by_name(char const *name) { (void)name; return (short)0x8001; }
int hs_can_cast(short actual, short desired) { return actual == desired; }
short const hs_external_global_count = 100;
void *hs_syntax_data;
unsigned short const hs_object_type_masks[16];
short mcc_campaign_find(char const *name) { (void)name; return NONE; }
void *mcc_campaign_function(short index) { (void)index; return NULL; }
void *global_scenario_get(void) { return NULL; }

/* (the vertex and index buffers: as rasterizer_xbox_hardware_geometry.c
fills their descriptors, a hardware format of their own) */
struct vertex_buffer_layout { short type; unsigned short pad; int count, offset; uint32_t base_address, hardware_format; };
struct triangle_buffer_layout { short type; unsigned short pad; int count; uint32_t base_address, hardware_format; };

int rasterizer_vertex_buffer_new(struct vertex_buffer_layout *buffer, int type, int count, void const *vertices, int size)
{
	void *hardware = arena_allocate(16);

	(void)size;
	if (!count || !hardware)
	{
		memset(buffer, 0, sizeof(*buffer));
		return 0;
	}
	buffer->count = count;
	buffer->offset = 0;
	buffer->base_address = xa(vertices);
	buffer->type = (short)type;
	buffer->hardware_format = xa(hardware);
	return 1;
}
void rasterizer_vertex_buffer_delete(struct vertex_buffer_layout *buffer) { buffer->hardware_format = 0; }
int rasterizer_triangle_buffer_new(struct triangle_buffer_layout *buffer, short type, int count, void const *triangles)
{
	void *hardware = arena_allocate(16);

	if (count <= 0 || !hardware)
	{
		memset(buffer, 0, sizeof(*buffer));
		return 0;
	}
	buffer->type = type;
	buffer->count = count;
	buffer->base_address = xa(triangles);
	buffer->hardware_format = xa(hardware);
	return 1;
}
void rasterizer_triangle_buffer_delete(struct triangle_buffer_layout *buffer) { buffer->hardware_format = 0; }

/* (memory/data.c's, the layout the game's units have) */
struct data_array_layout
{
	char name[32];
	short maximum_count, size;
	unsigned char valid, identifier_zero_invalid;
	uint32_t signature;
	short first_free_absolute_index, count, actual_count, next_identifier;
	uint32_t data;
};

void *datum_try_and_get(struct data_array_layout *data, int index)
{
	short identifier, absolute_index;
	short *header;

	if (index == NONE)
		return NULL;
	identifier = (short)(index >> 16);
	absolute_index = (short)index;
	if (absolute_index < 0 || absolute_index >= data->maximum_count)
		return NULL;
	header = (short *)((unsigned char *)xp(data->data) + data->size * absolute_index);
	if (!*header || (identifier && *header != identifier))
		return NULL;
	return header;
}

/* (cseries.c's) */
void *csmemcpy(void *d, void const *s, unsigned int n) { return memcpy(d, s, n); }
void *csmemset(void *d, int c, unsigned int n) { return memset(d, c, n); }
int csmemcmp(void const *a, void const *b, unsigned int n) { return memcmp(a, b, n); }
int csstrcmp(char const *a, char const *b) { return strcmp(a, b); }
int csstrcasecmp(char const *a, char const *b) { return strcasecmp(a, b); }
char *csstrcpy(char *d, char const *s) { return strcpy(d, s); }
unsigned int csstrlen(char const *s) { return (unsigned int)strlen(s); }
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
void error(short priority, char const *format, ...) { (void)priority; (void)format; }
void xbox_address_out_of_range(void const *pointer) { (void)pointer; abort(); }
char *csstrcat(char *d, char const *s) { return strcat(d, s); }
char temporary[256];
char *csprintf(char *buffer, char const *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(buffer, 256, format, arguments);
	va_end(arguments);
	return buffer;
}
/* (an assertion a map can reach halts the game: reported as a crash) */
void display_assert(char const *condition, char const *file, int line, int fatal)
{
	(void)fatal;
	fprintf(stderr, "mcc_prepare_fuzz: assertion failed: %s (%s #%d)\n", condition ? condition : "",
		file ? file : "", line);
	abort();
}
/* (a release build's assertions, as in the game: noted and let go, and the
sanitizers say what comes of it) */
void release_assert_failed(char const *condition, char const *file, int line, int fatal)
{
	(void)condition;
	(void)file;
	(void)line;
	(void)fatal;
}
void system_exit(int code) { (void)code; abort(); }
void tag_validate_report(char const *message) { (void)message; }
int stubbs_mode_owns_data(void const *address, int size) { (void)address; (void)size; return 0; }

/* (only at run time, with objects and scripts running: never here) */
#define NEVER(name) void name(void) { abort(); }
NEVER(ai_index_from_string) NEVER(hs_macro_function_evaluate) NEVER(hs_macro_function_parse) NEVER(hs_return)
NEVER(mcc_executing_thread_evaluate) NEVER(mcc_kill_thread_evaluate) NEVER(mcc_player_add_equipment)
NEVER(object_get_origin) NEVER(object_list_delete) NEVER(object_list_from_ai_reference) NEVER(object_list_get_first)
NEVER(object_list_get_next) NEVER(object_try_and_get_and_verify_type)
void *object_list_header_data;
unsigned int D3D__RenderState[256];
NEVER(D3DDevice_SetRenderState_BackFillMode) NEVER(D3DDevice_SetRenderState_CullMode)
NEVER(D3DDevice_SetRenderState_Deferred) NEVER(D3DDevice_SetRenderState_DoNotCullUncompressed)
NEVER(D3DDevice_SetRenderState_Dxt1NoiseEnable) NEVER(D3DDevice_SetRenderState_EdgeAntiAlias)
NEVER(D3DDevice_SetRenderState_FillMode) NEVER(D3DDevice_SetRenderState_FogColor)
NEVER(D3DDevice_SetRenderState_FrontFace) NEVER(D3DDevice_SetRenderState_LineWidth)
NEVER(D3DDevice_SetRenderState_LogicOp) NEVER(D3DDevice_SetRenderState_MultiSampleAntiAlias)
NEVER(D3DDevice_SetRenderState_MultiSampleMask) NEVER(D3DDevice_SetRenderState_MultiSampleType)
NEVER(D3DDevice_SetRenderState_NormalizeNormals) NEVER(D3DDevice_SetRenderState_OcclusionCullEnable)
NEVER(D3DDevice_SetRenderState_PSTextureModes) NEVER(D3DDevice_SetRenderState_RopZCmpAlwaysRead)
NEVER(D3DDevice_SetRenderState_RopZRead) NEVER(D3DDevice_SetRenderState_ShadowFunc)
NEVER(D3DDevice_SetRenderState_Simple) NEVER(D3DDevice_SetRenderState_StencilCullEnable)
NEVER(D3DDevice_SetRenderState_StencilEnable) NEVER(D3DDevice_SetRenderState_StencilFail)
NEVER(D3DDevice_SetRenderState_TextureFactor) NEVER(D3DDevice_SetRenderState_TwoSidedLighting)
NEVER(D3DDevice_SetRenderState_VertexBlend) NEVER(D3DDevice_SetRenderState_YuvEnable)
NEVER(D3DDevice_SetRenderState_ZBias) NEVER(D3DDevice_SetRenderState_ZEnable)

/* ---------- the input */

struct memory
{
	unsigned char const *bytes;
	uint64_t size;
};

static int memory_read(void *context, uint32_t offset, uint32_t size, void *out)
{
	struct memory const *memory = context;

	if (offset > memory->size || size > memory->size - offset)
		return 0;
	memcpy(out, memory->bytes + offset, size);
	return 1;
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void)argc; (void)argv;
	if (mmap(xp(TAG_WINDOW), TAG_CAPACITY, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) !=
			xp(TAG_WINDOW) ||
		mmap(xp(ARENA), ARENA_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) != xp(ARENA))
	{
		abort();
	}
	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	static uint32_t dirty;
	struct memory memory;
	unsigned char *copy, *raw = NULL;
	struct mcc_cache_identity identity;
	char const *stage;

	if (size < MCC_CACHE_HEADER_BYTES || size > 0x1000000)
		return 0;
	copy = malloc(size);
	if (!copy)
		return 0;
	memcpy(copy, data, size);
	memory.bytes = copy;
	memory.size = size;
	memset(&runtime, 0, sizeof(runtime));
	runtime.source.context = &memory;
	runtime.source.read = memory_read;
	runtime.source.size = size;
	if (mcc_cache_identify(copy, size, size, &identity) != MCC_CACHE_OK ||
		!(raw = malloc(identity.tag_data_size)) ||
		mcc_cache_load(&runtime.source, raw, identity.tag_data_size, &runtime.report) != MCC_CACHE_OK ||
		runtime.report.tag_base != TAG_WINDOW)
	{
		if (getenv("MCC_PREPARE_FUZZ_VERBOSE"))
			fprintf(stderr, "mcc_prepare_fuzz: not read\n");
		free(raw);
		free(copy);
		return 0;
	}
	/* (the window as mcc_memory_reserve gives it: fresh) */
	memset(xp(TAG_WINDOW), 0, dirty);
	arena_reset();
	runtime.capacity = TAG_CAPACITY;
	runtime.tags = xp(TAG_WINDOW);
	memcpy(runtime.tags, raw, identity.tag_data_size);
	runtime.used = identity.tag_data_size;
	runtime.tag_index = runtime.tags + 40;
	free(raw);
	stage = !mcc_hud_bitmaps_prepare(&runtime) ? "hud" : !mcc_tags_prepare(&runtime) ? "tags" :
		!mcc_scripts_prepare(&runtime) ? "scripts" : !mcc_geometry_prepare(&runtime) ? "geometry" : NULL;
	if (!stage)
	{
		uint32_t index_address = xa(runtime.tag_index);

		/* (the tag header made stock-shaped, as mcc_cache_tags_load does) */
		memset(runtime.tags + 16, 0, 20);
		memcpy(runtime.tags, &index_address, 4);
		memcpy(runtime.tags + 12, &runtime.report.tag_count, 4);
		memcpy(runtime.tags + 32, "sgat", 4);
		stage = mcc_tags_validate(&runtime) ? "validated" : "refused by the validator";
	}
	/* (MCC_PREPARE_FUZZ_VERBOSE: how far each input got) */
	if (getenv("MCC_PREPARE_FUZZ_VERBOSE"))
		fprintf(stderr, "mcc_prepare_fuzz: %s\n", stage);
	mcc_validation_dispose(&runtime);
	mcc_geometry_dispose(&runtime);
	mcc_parameters_dispose();
	if (runtime.used > dirty)
		dirty = runtime.used;
	free(copy);
	return 0;
}
