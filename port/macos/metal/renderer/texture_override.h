/*
TEXTURE_OVERRIDE.H

debug.texture_override_directory: textures drawn in place of the game's, for
judging upscaled textures in game (step-tools/texture-upscale.py writes them).
A texture is found by a hash of its first mip level's bytes as the map holds
them, 64-bit FNV-1a, written as 16 lowercase hex digits in the file's name:
<hash>.rgba. The file is three little-endian 32-bit words (width, height,
level count), then the levels, largest first, each RGBA8 in rows top first
and half the size of the one before (never below 1). xbox_textures.c reads
it; these are the parts that need no game.
*/

#ifndef TEXTURE_OVERRIDE_H
#define TEXTURE_OVERRIDE_H

#include <stddef.h>
#include <stdint.h>

#define TEXTURE_OVERRIDE_HEADER_SIZE 12
#define TEXTURE_OVERRIDE_MAXIMUM_LEVELS 14

struct texture_override_layout
{
	uint32_t width, height, levels;
	size_t level_offset[TEXTURE_OVERRIDE_MAXIMUM_LEVELS]; /* bytes from the file's start */
	size_t level_size[TEXTURE_OVERRIDE_MAXIMUM_LEVELS];
};

static inline uint64_t texture_override_hash(const unsigned char *bytes, size_t size)
{
	uint64_t hash = 0xcbf29ce484222325ULL;
	size_t index;

	for (index = 0; index < size; index++)
		hash = (hash ^ bytes[index]) * 0x100000001b3ULL;
	return hash;
}

static inline uint32_t texture_override_word(const unsigned char *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

/* NULL if the file can stand for a texture of this size, with its levels in
layout; otherwise why it can't */
static inline const char *texture_override_check(const unsigned char *file, size_t file_size,
	unsigned long original_width, unsigned long original_height, struct texture_override_layout *layout)
{
	unsigned long factor;
	uint32_t level, largest;
	size_t offset = TEXTURE_OVERRIDE_HEADER_SIZE;

	if (file_size < TEXTURE_OVERRIDE_HEADER_SIZE)
		return "shorter than its header";
	layout->width = texture_override_word(file);
	layout->height = texture_override_word(file + 4);
	layout->levels = texture_override_word(file + 8);
	for (factor = 1; factor <= 4; factor *= 2)
	{
		if (original_width && layout->width == original_width * factor && layout->height == original_height * factor)
			break;
	}
	if (factor > 4)
		return "not 1, 2 or 4 times the size of the texture";
	largest = layout->width > layout->height ? layout->width : layout->height;
	if (layout->levels < 1 || layout->levels > TEXTURE_OVERRIDE_MAXIMUM_LEVELS || !(largest >> (layout->levels - 1)))
		return "more levels than halving its size gives";
	for (level = 0; level < layout->levels; level++)
	{
		size_t width = layout->width >> level ? layout->width >> level : 1;
		size_t height = layout->height >> level ? layout->height >> level : 1;

		layout->level_offset[level] = offset;
		layout->level_size[level] = width * height * 4;
		offset += layout->level_size[level];
	}
	if (offset != file_size)
		return "not the size its levels take, each half the one before";
	return NULL;
}

/* RGBA texels as gpu.h's BGRA8, in place */
static inline void texture_override_rgba_to_bgra(unsigned char *texels, size_t count)
{
	size_t index;

	for (index = 0; index < count; index++)
	{
		unsigned char red = texels[index * 4];

		texels[index * 4] = texels[index * 4 + 2];
		texels[index * 4 + 2] = red;
	}
}

#endif
