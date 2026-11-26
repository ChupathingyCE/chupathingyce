/* texture_decode_fuzz.c: a texture's texels as the renderers decode them
(port/linux/src/xbox_textures.c, built as the game builds it, with
texture_decode_unit.c after it: tools/test_host_fuzz.py, "game_sources").
The texels come from a map's bitmaps, Custom Edition's and MCC's among them:
DXT and BC7 blocks, and the uncompressed formats, swizzled, linear, volume
and cube, in the Xbox's layout or Halo PC's.

The input is the texture's header, then its texels:
- bytes 0-3: its Format word (little-endian): format, size, levels, cube map,
  dimension;
- bytes 4-7: its Size word: a linear texture's width, height and pitch, or 0;
- bytes 8-11: its Common word's port bits (D3DCOMMON_PORT_PC_LAYOUT and the
  PC meter and multipurpose bits);
- the rest: the texture's bytes, its faces' and levels', short ones zeroes.

Each level of each face is decoded from an allocation of exactly its bytes
into one of exactly its texels (texture_decode_unit.c). The unit is built
for LP64 as the game is (its long 32 bits), so what it calls here takes
unsigned int. */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int texture_decode_fuzz(unsigned int format_word, unsigned int size_word, unsigned int common,
	const unsigned char *texels, unsigned int texel_count);

/* ---------- what xbox_textures.c calls outside its decoders: never reached
here (its cache and uploads), or only for a setting (texture_dump) */

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
NEVER(hud_hires_override_coverage) NEVER(hud_hires_override_find) NEVER(hud_hires_override_point_threshold)
NEVER(hud_hires_override_texture) NEVER(hud_hires_settings) NEVER(memory_watch_generation)
NEVER(memory_watch_protect) NEVER(memory_watch_serial) NEVER(menu_art_texture) NEVER(platform_is_contiguous)
NEVER(SDL_GetTicks) NEVER(text_hires_atlas_fields) NEVER(text_hires_atlas_texture) NEVER(xgpu_gl_state_invalidate)
NEVER(xgpu_mcc_texture_get)
unsigned int D3D__RenderState[256];
/* (the OpenGL entry points: pointers the uploads call through, none here) */
void *halo_glBindTexture, *halo_glCompressedTexImage2D, *halo_glCompressedTexImage3D, *halo_glDeleteTextures,
	*halo_glGenTextures, *halo_glGetError, *halo_glGetIntegerv, *halo_glGetTexImage, *halo_glPixelStorei,
	*halo_glTexImage2D, *halo_glTexImage3D, *halo_glTexParameteri;

int config_boolean(const char *name)
{
	(void)name;
	return 0;
}

const char *config_string(const char *name)
{
	(void)name;
	return "";
}

void platform_log(const char *format, ...)
{
	(void)format;
}

static unsigned int word(const uint8_t *data)
{
	return (unsigned int)data[0] | (unsigned int)data[1] << 8 | (unsigned int)data[2] << 16 |
		(unsigned int)data[3] << 24;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	unsigned char *texels;

	if (size < 12)
		return 0;
	/* (the texels in an allocation of their own, exactly their size) */
	texels = malloc(size - 12 ? size - 12 : 1);
	if (!texels)
		return 0;
	memcpy(texels, data + 12, size - 12);
	texture_decode_fuzz(word(data), word(data + 4), word(data + 8), texels, (unsigned int)(size - 12));
	free(texels);
	return 0;
}
