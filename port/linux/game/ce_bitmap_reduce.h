/*
CE_BITMAP_REDUCE.H

A Custom Edition map's large bitmaps on a 64 MB console (ce_repairs.c).
That console lends a Custom Edition map its own texture cache, about 17 MB
of it (xbox_ce_files.c, port/xbox): a Halo 2 map's bitmaps are made for Halo
PC, with 2048x2048 lightmaps of 8 MB a level and 512x512 bump maps of 1 MB,
so a frame of Coagulation wants more texture memory than that console has,
the cache fills with the frame's own bitmaps, and the game halts
(xbox_texture_cache.c). The Xbox shipped its own maps with smaller bitmaps
than Halo PC's; a 64 MB console draws a Custom Edition map's the same way: a
2D bitmap of CE_REDUCE_PIXELS or more loses its top level, and one whose top
level is still over CE_REDUCE_LEVEL_BYTES loses one more. Its levels are
packed in the file largest first, so the bitmap starts at its next level:
its size, mipmaps, registration point and pixels follow.

What the HUD and the menus draw keeps every level: they size what they draw
by the bitmap's pixels (interface.c), so a halved one would draw at half its
size and out of place. Those are told by the bitmap tag's own type (sprites
and interface bitmaps) and by what refers to it (a HUD, menu or globals tag:
ce_repairs.c), never by its name alone, as a protected map names every tag
"<protected>".

The bitmap is bitmap_group.h's bitmap_data, in the tags' own byte order.
Only the 2D bitmap's own fields are read; the header stands alone so that
tools/test_ce_bitmap_reduce.py can build it.
*/

#ifndef __CE_BITMAP_REDUCE_H
#define __CE_BITMAP_REDUCE_H
#pragma once

/* ---------- headers */

#include <string.h>

/* ---------- constants */

enum
{
	/* bitmap_group.h's bitmap_data: its size and shape, its format and
	flags, its registration point, its mipmaps and its pixels in the file */
	CE_BITMAP_WIDTH_OFFSET = 0x04,
	CE_BITMAP_HEIGHT_OFFSET = 0x06,
	CE_BITMAP_DEPTH_OFFSET = 0x08,
	CE_BITMAP_TYPE_OFFSET = 0x0a,
	CE_BITMAP_FORMAT_OFFSET = 0x0c,
	CE_BITMAP_FLAGS_OFFSET = 0x0e,
	CE_BITMAP_REGISTRATION_POINT_OFFSET = 0x10,
	CE_BITMAP_MIPMAP_COUNT_OFFSET = 0x14,
	CE_BITMAP_PIXELS_OFFSET_OFFSET = 0x18,
	CE_BITMAP_PIXELS_SIZE_OFFSET = 0x1c,
	CE_BITMAP_SIZE = 0x30,

	/* a bitmap's type and flags (bitmaps.c) */
	CE_BITMAP_TYPE_2D = 0,
	CE_BITMAP_COMPRESSED_FLAG = 0x02,
	CE_BITMAP_LINEAR_FLAG = 0x10,

	/* a bitmap tag's types (bitmap_group.h's bitmap_group, its first field;
	bitmap_extract.c) whose bitmaps the HUD and menus draw by the pixel */
	CE_BITMAP_GROUP_TYPE_SPRITES = 3,
	CE_BITMAP_GROUP_TYPE_INTERFACE_BITMAPS = 4,

	/* a 2D bitmap of this many pixels or more loses its top level, and one
	whose top level is still more than this many bytes loses another */
	CE_REDUCE_PIXELS = 512 * 512,
	CE_REDUCE_LEVEL_BYTES = 2 * 1024 * 1024,
	CE_REDUCE_LEVELS_AT_MOST = 2,
	CE_BITMAP_FORMATS = 18,
};

/* ---------- inline code */

/* whether a bitmap tag of this type, or one a HUD, menu or globals tag
refers to, keeps every level */
static inline int ce_bitmap_kept_whole(
	short group_type,
	int interface_drawn)
{
	return interface_drawn || group_type == CE_BITMAP_GROUP_TYPE_SPRITES ||
		group_type == CE_BITMAP_GROUP_TYPE_INTERFACE_BITMAPS;
}

/* the bytes of a 2D bitmap's top level (bitmaps.c:
bitmap_mipmap_get_pixel_data_size, its level 0), or 0 for a format of none */
static inline long ce_bitmap_top_level_bytes(
	short width,
	short height,
	short format,
	unsigned short flags)
{
	/* each format's bits per pixel, 0 for none (bitmaps.c's
	bitmap_format_bits_per_pixel_table: A8 to P8 bump) */
	static signed char const ce_bitmap_format_bits[CE_BITMAP_FORMATS] =
	{
		8, 8, 8, 16, 0, 0, 16, 0, 16, 16, 32, 32, 0, 0, 4, 8, 8, 8,
	};
	long padded_width = width > 1 ? width : 1;
	long padded_height = height > 1 ? height : 1;

	if (format < 0 || format >= CE_BITMAP_FORMATS || ce_bitmap_format_bits[format] <= 0)
		return 0;
	if (flags & CE_BITMAP_COMPRESSED_FLAG)
	{
		padded_width += (-padded_width) & 3;
		padded_height += (-padded_height) & 3;
	}
	return padded_width * padded_height * ce_bitmap_format_bits[format] / 8;
}

/* a bitmap (CE_BITMAP_SIZE bytes) of a bitmap tag of group_type started at
its next level, or two, if it is a large 2D one the HUD and menus don't draw
by the pixel (interface_drawn: a HUD, menu or globals tag refers to it); the
levels it lost (0 if none) */
static inline int ce_bitmap_reduce(
	unsigned char *bitmap,
	short group_type,
	int interface_drawn)
{
	short width, height, depth, type, format, mipmap_count, registration[2];
	unsigned short flags;
	int pixels_offset, pixels_size;
	int drops = 0;

	if (ce_bitmap_kept_whole(group_type, interface_drawn))
		return 0;
	memcpy(&width, bitmap + CE_BITMAP_WIDTH_OFFSET, sizeof(width));
	memcpy(&height, bitmap + CE_BITMAP_HEIGHT_OFFSET, sizeof(height));
	memcpy(&depth, bitmap + CE_BITMAP_DEPTH_OFFSET, sizeof(depth));
	memcpy(&type, bitmap + CE_BITMAP_TYPE_OFFSET, sizeof(type));
	memcpy(&format, bitmap + CE_BITMAP_FORMAT_OFFSET, sizeof(format));
	memcpy(&flags, bitmap + CE_BITMAP_FLAGS_OFFSET, sizeof(flags));
	memcpy(registration, bitmap + CE_BITMAP_REGISTRATION_POINT_OFFSET, sizeof(registration));
	memcpy(&mipmap_count, bitmap + CE_BITMAP_MIPMAP_COUNT_OFFSET, sizeof(mipmap_count));
	memcpy(&pixels_offset, bitmap + CE_BITMAP_PIXELS_OFFSET_OFFSET, sizeof(pixels_offset));
	memcpy(&pixels_size, bitmap + CE_BITMAP_PIXELS_SIZE_OFFSET, sizeof(pixels_size));
	if (type != CE_BITMAP_TYPE_2D || depth != 1 || (flags & CE_BITMAP_LINEAR_FLAG) || width <= 0 || height <= 0)
		return 0;
	while (mipmap_count >= 1 && drops < CE_REDUCE_LEVELS_AT_MOST)
	{
		long level_bytes = ce_bitmap_top_level_bytes(width, height, format, flags);

		if (level_bytes <= 0 || level_bytes >= pixels_size)
			break;
		if (!(level_bytes > CE_REDUCE_LEVEL_BYTES || (drops == 0 && (long)width * height >= CE_REDUCE_PIXELS)))
			break;
		pixels_offset += (int)level_bytes;
		pixels_size -= (int)level_bytes;
		width = (short)(width / 2 > 1 ? width / 2 : 1);
		height = (short)(height / 2 > 1 ? height / 2 : 1);
		registration[0] /= 2;
		registration[1] /= 2;
		mipmap_count--;
		drops++;
	}
	if (!drops)
		return 0;
	memcpy(bitmap + CE_BITMAP_WIDTH_OFFSET, &width, sizeof(width));
	memcpy(bitmap + CE_BITMAP_HEIGHT_OFFSET, &height, sizeof(height));
	memcpy(bitmap + CE_BITMAP_REGISTRATION_POINT_OFFSET, registration, sizeof(registration));
	memcpy(bitmap + CE_BITMAP_MIPMAP_COUNT_OFFSET, &mipmap_count, sizeof(mipmap_count));
	memcpy(bitmap + CE_BITMAP_PIXELS_OFFSET_OFFSET, &pixels_offset, sizeof(pixels_offset));
	memcpy(bitmap + CE_BITMAP_PIXELS_SIZE_OFFSET, &pixels_size, sizeof(pixels_size));
	return drops;
}

#endif // __CE_BITMAP_REDUCE_H
