/* texture_decode_unit.c: appended to xbox_textures.c as the game builds it
(tools/test_host_fuzz.py, a target's "game_sources" "append"), so that the
fuzz target reaches its decoders, static there, as the renderers do.

texture_decode_fuzz decodes every level of every face of one texture, given
its header's Format and Size words and the port's Common bits, in the order
and at the offsets the uploads read them (upload here, bg_texture_upload in
d3d8_bgfx.inl): each level from an allocation of exactly the bytes
xgpu_texture_level_bytes gives it, into one of exactly the texels its
dimensions give, so that AddressSanitizer reports a decoder that reads or
writes past either. The texture as a whole must fit in its faces' bytes
(xgpu_texture_face_size), which is what the texture cache maps. Not compiled
with LP64's rewrite: unsigned int here, as the unit's unsigned long is. */

#include <stdlib.h>
#include <string.h>

/* (the decoded texels a texture may have, all levels and faces: past it a
texture is passed over, to keep each input fast; the decoders' arithmetic
is the same at any size the game allows, 4096 square or 512 cubed) */
#define TEXTURE_DECODE_FUZZ_TEXELS (1UL << 20)

static void texture_decode_fuzz_level(const struct xgpu_texture_description *description, unsigned char kind,
	unsigned int level, const unsigned char *bytes, const D3DCOLOR *palette)
{
	unsigned int width = level_dimension(description->width, level);
	unsigned int height = level_dimension(description->height, level);
	unsigned int depth = level_dimension(description->depth, level);
	unsigned int size = xgpu_texture_level_bytes(description, level);
	unsigned char *source = malloc(size ? size : 1);
	D3DCOLOR *destination = malloc((size_t)width * height * depth * sizeof(*destination));

	if (!source || !destination)
		abort();
	memcpy(source, bytes, size);
	switch (kind)
	{
	case _texel_dxt1:
	case _texel_dxt3:
	case _texel_dxt5:
		/* (the OpenGL ES upload's and the Guide's decoder; the others send
		DXT as it is) */
		dxt_decode_level(kind, source, width, height, depth, destination);
		break;
	default:
#if defined(HALO_BGFX) || defined(HALO_CONSOLE_NATIVE_RENDERER)
		if (!xgpu_texture_decode_level(description, level, source, palette, destination))
			abort();
#else
		if (kind == _texel_bc7)
			bc7_decode_level(source, width, height, depth, destination);
		else if (!decode_level(description, level, source, palette, destination))
			abort();
#endif
		break;
	}
	free(destination);
	free(source);
}

int texture_decode_fuzz(unsigned int format_word, unsigned int size_word, unsigned int common,
	const unsigned char *texels, unsigned int texel_count)
{
	struct xgpu_texture_description description;
	unsigned char kind;
	unsigned int faces, face, level, face_size, total, offset, index;
	unsigned long texels_wanted = 0;
	unsigned char *base;
	D3DCOLOR *palette;

	xgpu_texture_describe(format_word, size_word, &description);
	description.pc_layout = (common & D3DCOMMON_PORT_PC_LAYOUT) != 0;
	description.pc_meter = (common & D3DCOMMON_PORT_PC_METER) != 0;
	description.pc_multipurpose = (common & D3DCOMMON_PORT_PC_MULTIPURPOSE) != 0;
	kind = format_information(description.format).kind;
	faces = description.cube_map ? 6 : 1;
	/* (the sizes the texture cache takes, xgpu_texture_get) */
	if (!texture_size_supported(&description))
		return 0;
	for (level = 0; level < description.levels; level++)
	{
		texels_wanted += (unsigned long)level_dimension(description.width, level) *
			level_dimension(description.height, level) * level_dimension(description.depth, level) * faces;
	}
	if (texels_wanted > TEXTURE_DECODE_FUZZ_TEXELS)
		return 0;

	/* the texture's bytes, as the cache maps them: its faces, the texels
	given and then zeroes */
	face_size = xgpu_texture_face_size(&description);
	total = face_size * faces;
	base = calloc(total ? total : 1, 1);
	if (!base)
		abort();
	memcpy(base, texels, texel_count < total ? texel_count : total);
	/* (a P8 texture's palette, its 256 entries) */
	palette = malloc(256 * sizeof(*palette));
	if (!palette)
		abort();
	for (index = 0; index < 256; index++)
		palette[index] = 0xff000000U | (index * 0x010101U);

	for (face = 0; face < faces; face++)
	{
		for (level = 0; level < description.levels; level++)
		{
			unsigned int size = xgpu_texture_level_bytes(&description, level);

			offset = face * face_size + xgpu_texture_level_offset(&description, level);
			/* (Halo PC's cube map: its levels one after another, each the
			six faces', the second and third swapped) */
			if (description.pc_layout && description.cube_map)
				offset = xgpu_texture_level_offset(&description, level) * 6 +
					(face == 1 ? 2 : face == 2 ? 1 : face) * size;
			/* a level the texture's bytes don't hold: the uploads would read
			past what the cache mapped */
			if (offset > total || size > total - offset)
				abort();
			texture_decode_fuzz_level(&description, kind, level, base + offset, palette);
		}
	}
	free(palette);
	free(base);
	return 0;
}
