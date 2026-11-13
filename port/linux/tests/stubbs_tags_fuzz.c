/*
STUBBS_TAGS_FUZZ.C

libFuzzer's entry for Stubbs the Zombie's tags (tools/test_host_fuzz.py
builds it, target stubbs_tags): Stubbs mode rewrites a Stubbs map's tags in
Halo's layouts (port/linux/game/stubbs_mode.c, stubbs_mode_convert_tags)
before the tag validator has looked at them, so it must keep to the tag data
it is given whatever the tags say. The input is the tag data, its tag
header first, linked at Stubbs's tag cache (0x8038B000), in an allocation of
exactly its size.

The game unit is compiled as the 64-bit build compiles it (its long 32 bits:
tools/lp64_rewrite.py), so what it calls here takes int where the source
says long.
*/

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void stubbs_mode_convert_tags(void *tag_cache, unsigned int tag_data_size);

/* ---------- what stubbs_mode.c calls */

const char *map_pack_menus(void) { return "stubbs"; }
/* (the unit definitions the engine and possession note as the tags are
rewritten: nothing to note here) */
void stubbs_engine_note_unit_definition(void) {}
void stubbs_possession_note_unit_definition(void) {}
int _stricmp(const char *a, const char *b) { return strcasecmp(a, b); }
void *csmemcpy(void *d, const void *s, unsigned int n) { return memcpy(d, s, n); }
void *csmemmove(void *d, const void *s, unsigned int n) { return memmove(d, s, n); }
void *csmemset(void *d, int c, unsigned int n) { return memset(d, c, n); }
void *system_calloc(int count, int size) { return calloc((size_t)count, (size_t)size); }
void system_free(void *p) { free(p); }
void error(short priority, const char *format, ...) { (void)priority; (void)format; }
int halo_linux_vsnprintf(char *buffer, size_t count, const char *format, va_list arguments)
{
	return vsnprintf(buffer, count, format, arguments);
}
void xbox_address_out_of_range(const void *pointer) { (void)pointer; abort(); }
/* (sound_classes.c's: a name for every class, some of them empty) */
const char *sound_class_names[64] = {
	"projectile_impact", "", "projectile_detonation", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""
};
unsigned int D3D__RenderState[256];

/* (the Xbox's render-state inlines its headers declare: never called) */
#define NEVER(name) void name(void) { abort(); }
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

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	unsigned char *tags;

	if (size < 0x28 || size > 0x100000)
		return 0;
	tags = malloc(size);
	if (!tags)
		return 0;
	memcpy(tags, data, size);
	stubbs_mode_convert_tags(tags, (unsigned int)size);
	free(tags);
	return 0;
}
