/*
HUD_HIRES_TAGS.C

The bitmaps the high-res HUD's textures stand for (port/linux/src/hud_hires.c),
found in each map's tags as it loads (scenario_tags_load): every map holds
its own copy of the HUD's bitmap groups.

An MCC map's bitmaps are its own converted copies, which the texture cache
never sees (mcc_texture_bridge.c draws them): its stock HUD sheets are found
as its tags load (hud_hires_mcc_tags_loaded), each by its tag's name, its
size and the checksum of its first mip level as the map stores it, so that
a map maker's own art keeps its look whatever its tag is named; and the MCC
texture bridge asks hud_hires_mcc_asset which texture stands for one.

A bitmap's pixels are loaded into the texture cache's memory at its
base_address (xbox_texture_cache.c), which it keeps until its cache block is
reused (when cache_block_index and base_address are cleared). The texture
cache of the platform layer uploads the pixels at an address whenever they
are written there, and asks hud_hires_asset_at which bitmap they are: a block
reused for another bitmap, or a map unloaded, is a write, which asks again.
*/

#include "cseries.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmap_group_lookup.h"
#include "tag_files/tag_groups.h"
#include "scenario/scenario_definitions.h"
#include "../src/hud_hires.h"
#include "../src/mcc_texture_bridge.h"
#include "mcc_cache.h"

#include <string.h>

/* the platform layer's (port/linux/src) */
void platform_log(char const *format, ...);
long hud_hires_asset_count(void);
char const *hud_hires_asset_tag(long asset);
long hud_hires_asset_bitmap(long asset);
long hud_hires_asset_fits(long asset, long width, long height);
long hud_hires_asset_alternate(long asset);
void hud_hires_map_loaded(const long *assets, long count);
void hud_hires_map_unloaded(void);

void hud_hires_tags_loaded(long scenario_index);
void hud_hires_tags_unloaded(void);
long hud_hires_asset_at(unsigned long address, long width, long height);

/* ---------- constants */

enum
{
	MAXIMUM_HIRES_BITMAPS = 128,
};

/* ---------- globals */

static struct
{
	struct bitmap_data *bitmap;
	long asset;
} hires_bitmaps[MAXIMUM_HIRES_BITMAPS];
static long hires_bitmap_count = 0;

/* an MCC map's stock sheets, and the textures standing for them */
static struct
{
	struct bitmap_data const *bitmap;
	long asset;
} mcc_bitmaps[MAXIMUM_HIRES_BITMAPS];
static long mcc_bitmap_count = 0;

/* the stock unit HUD's sheets: its panels' backgrounds and their meters,
drawn over each other (a vehicle's health panel's outlined wrench over its
meter's filled one), so an MCC map has them whole, every one of them the
high-res HUD's or every one the map's own */
static char const *const mcc_unit_sheet_tags[] =
{
	"ui\\hud\\bitmaps\\combined\\hud_unit_backgrounds",
	"ui\\hud\\bitmaps\\combined\\hud_unit_meters",
};

/* ---------- private code */

/* whether a tag is one of the unit HUD's sheets (mcc_unit_sheet_tags) */
static int hud_hires_mcc_unit_sheet(
	char const *tag)
{
	long index;

	for (index = 0; tag && index < (long)NUMBEROF(mcc_unit_sheet_tags); index++)
	{
		if (!strcmp(tag, mcc_unit_sheet_tags[index]))
			return 1;
	}
	return 0;
}

/* whether the embedded texture stands for the MCC map's bitmap of this tag,
index, size and first mip level's checksum (as the map stores it): an MCC
stock sheet's texture, for that sheet alone, as large as it or larger */
static int hud_hires_mcc_match(
	const struct hud_hires_embedded *embedded,
	char const *tag,
	long bitmap,
	long width,
	long height,
	unsigned long crc)
{
	return embedded->mcc && embedded->tag && tag && !strcmp(embedded->tag, tag) && embedded->bitmap == bitmap &&
		width > 0 && height > 0 && embedded->width % width == 0 && embedded->height % height == 0 &&
		embedded->width / width == embedded->height / height &&
		(crc & 0xffffffffUL) == (embedded->crc & 0xffffffffUL);
}

/* an MCC bitmap's texel at x, y as RGBA, as the renderer reads its format
(xbox_textures.c); FALSE for a format or layout not read here: a kept
cell's texels are only ever an uncompressed, swizzled sheet's */
static int mcc_texel(
	struct bitmap_data const *bitmap,
	unsigned char const *pixels,
	unsigned long bytes,
	long x,
	long y,
	unsigned char rgba[4])
{
	unsigned long index = 0, bit = 0, texel;
	long width = bitmap->width, height = bitmap->height, shift_x = 0, shift_y = 0;
	int size = bitmap->format == 10 || bitmap->format == 11 ? 4 : bitmap->format == 3 ? 2 :
		bitmap->format >= 0 && bitmap->format <= 2 ? 1 : 0;

	if (!size || (bitmap->flags & 16) || width <= 0 || height <= 0 || (width & (width - 1)) ||
		(height & (height - 1)) || x < 0 || y < 0 || x >= width || y >= height)
	{
		return 0;
	}
	/* (the Xbox's swizzled order: x and y bits interleaved, x first, the
	longer side's leftover bits on top) */
	while ((1L << shift_x) < width || (1L << shift_y) < height)
	{
		if ((1L << shift_x) < width)
			index |= (unsigned long)((x >> shift_x++) & 1) << bit++;
		if ((1L << shift_y) < height)
			index |= (unsigned long)((y >> shift_y++) & 1) << bit++;
	}
	texel = index * size;
	if (texel + size > bytes)
		return 0;
	switch (bitmap->format)
	{
	case 0: /* a8 */
		rgba[0] = rgba[1] = rgba[2] = 255;
		rgba[3] = pixels[texel];
		break;
	case 1: /* y8 */
		rgba[0] = rgba[1] = rgba[2] = pixels[texel];
		rgba[3] = 255;
		break;
	case 2: /* ay8 */
		rgba[0] = rgba[1] = rgba[2] = rgba[3] = pixels[texel];
		break;
	case 3: /* a8y8 */
		rgba[0] = rgba[1] = rgba[2] = pixels[texel];
		rgba[3] = pixels[texel + 1];
		break;
	default: /* x8r8g8b8, a8r8g8b8 */
		rgba[0] = pixels[texel + 2];
		rgba[1] = pixels[texel + 1];
		rgba[2] = pixels[texel];
		rgba[3] = bitmap->format == 10 ? 255 : pixels[texel + 3];
		break;
	}
	return 1;
}

/* ---------- public code */

void hud_hires_tags_loaded(
	long scenario_index)
{
	long asset_count = hud_hires_asset_count();
	long asset;
	long missing = 0;

	hires_bitmap_count = 0;
	for (asset = 0; asset < asset_count && hires_bitmap_count < MAXIMUM_HIRES_BITMAPS; asset++)
	{
		long group_index;
		struct bitmap_data *bitmap;

		/* (a title of the cyborg set stands in for its classic one, which
		the map's bitmap is found for: hud_hires.c) */
		if (hud_hires_asset_alternate(asset))
			continue;
		/* (and an MCC sheet's is found for MCC maps alone: below) */
		if (hud_hires_asset_mcc(asset))
			continue;
		group_index = tag_loaded(BITMAP_GROUP_TAG, hud_hires_asset_tag(asset));
		bitmap = group_index == NONE ? NULL :
			bitmap_group_try_and_get_bitmap(group_index, (short)hud_hires_asset_bitmap(asset));

		if (!bitmap)
		{
			/* (the main menu's map has only some of the HUD) */
			missing++;
		}
		else if (!hud_hires_asset_fits(asset, bitmap->width, bitmap->height))
		{
			platform_log("high-res hud: %s bitmap %ld is %dx%d here, which its texture does not fit",
				hud_hires_asset_tag(asset), hud_hires_asset_bitmap(asset), bitmap->width, bitmap->height);
		}
		else
		{
			hires_bitmaps[hires_bitmap_count].bitmap = bitmap;
			hires_bitmaps[hires_bitmap_count].asset = asset;
			hires_bitmap_count++;
		}
	}
	platform_log("high-res hud: %ld of %ld bitmaps in this map (%ld not in it)",
		hires_bitmap_count, asset_count, missing);
	/* (their PNGs decoded now, ahead of the HUD's first frame: not the main
	menu's, which draws no HUD) */
	if (scenario_definition_get(scenario_index)->type != _scenario_type_main_menu)
	{
		long assets[MAXIMUM_HIRES_BITMAPS];
		long index;

		for (index = 0; index < hires_bitmap_count; index++)
			assets[index] = hires_bitmaps[index].asset;
		hud_hires_map_loaded(assets, hires_bitmap_count);
	}

	return;
}

void hud_hires_tags_unloaded(
	void)
{
	hires_bitmap_count = 0;
	mcc_bitmap_count = 0;
	hud_hires_map_unloaded();

	return;
}

void hud_hires_mcc_tags_loaded(
	void)
{
	long asset_count = hud_hires_asset_count();
	long asset;
	long assets[MAXIMUM_HIRES_BITMAPS];

	/* (a unit HUD sheet of the map's that is not the stock one) */
	int unit_sheet_own = 0;
	long index, kept;

	mcc_bitmap_count = 0;
	for (asset = 0; asset < asset_count && mcc_bitmap_count < MAXIMUM_HIRES_BITMAPS; asset++)
	{
		const struct hud_hires_embedded *embedded = &hud_hires_embedded[asset];
		struct bitmap_data *bitmap;
		long group_index;
		unsigned long crc = 0;

		if (!embedded->mcc)
			continue;
		group_index = tag_loaded(BITMAP_GROUP_TAG, embedded->tag);
		bitmap = group_index == NONE ? NULL :
			bitmap_group_try_and_get_bitmap(group_index, (short)embedded->bitmap);
		if (!bitmap || !mcc_cache_bitmap_source_crc(bitmap, &crc) ||
			!hud_hires_mcc_match(embedded, embedded->tag, embedded->bitmap, bitmap->width, bitmap->height, crc))
		{
			if (bitmap && hud_hires_mcc_unit_sheet(embedded->tag))
				unit_sheet_own = 1;
			continue;
		}
		/* (its kept cells are copied from the map's texels: a sheet whose
		texels are not read here keeps all of its own look) */
		if (embedded->keep_count)
		{
			unsigned long bytes = 0;
			unsigned char const *pixels = mcc_cache_bitmap_pixels(bitmap, &bytes, NULL);
			unsigned char texel[4];

			if (!pixels || !mcc_texel(bitmap, pixels, bytes, 0, 0, texel))
				continue;
		}
		mcc_bitmaps[mcc_bitmap_count].bitmap = bitmap;
		mcc_bitmaps[mcc_bitmap_count].asset = asset;
		assets[mcc_bitmap_count] = asset;
		mcc_bitmap_count++;
	}
	/* (a map with a unit HUD sheet of its own, as an MCC campaign mod's
	bitmaps.map can have, keeps all of them its own: one of its backgrounds
	with the high-res HUD's meter drew two wrenches, apart) */
	if (unit_sheet_own)
	{
		for (index = kept = 0; index < mcc_bitmap_count; index++)
		{
			if (hud_hires_mcc_unit_sheet(hud_hires_embedded[mcc_bitmaps[index].asset].tag))
				continue;
			mcc_bitmaps[kept] = mcc_bitmaps[index];
			assets[kept] = assets[index];
			kept++;
		}
		platform_log("high-res hud: this MCC map's unit HUD sheets are its own (%ld of its stock bitmaps left so)",
			mcc_bitmap_count - kept);
		mcc_bitmap_count = kept;
	}
	platform_log("high-res hud: %ld of this MCC map's bitmaps are the stock HUD's", mcc_bitmap_count);
	mcc_texture_bridge_hud_refresh();
	hud_hires_map_loaded(assets, mcc_bitmap_count);

	return;
}

long hud_hires_mcc_asset(
	void const *bitmap)
{
	long index;

	for (index = 0; bitmap && index < mcc_bitmap_count; index++)
	{
		if (mcc_bitmaps[index].bitmap == bitmap)
			return mcc_bitmaps[index].asset;
	}

	return NONE;
}

void hud_hires_mcc_keep(
	long asset,
	unsigned char *rgba,
	unsigned long width,
	unsigned long height)
{
	const struct hud_hires_embedded *embedded;
	struct bitmap_data const *bitmap = NULL;
	unsigned char const *pixels;
	unsigned long bytes = 0, cell, scale;
	long index;

	for (index = 0; index < mcc_bitmap_count; index++)
	{
		if (mcc_bitmaps[index].asset == asset)
			bitmap = mcc_bitmaps[index].bitmap;
	}
	pixels = bitmap ? mcc_cache_bitmap_pixels(bitmap, &bytes, NULL) : NULL;
	if (!pixels || bitmap->width <= 0 || width % (unsigned long)bitmap->width ||
		height != width / (unsigned long)bitmap->width * (unsigned long)bitmap->height)
	{
		return;
	}
	embedded = &hud_hires_embedded[asset];
	scale = width / (unsigned long)bitmap->width;
	for (cell = 0; cell < embedded->keep_count; cell++)
	{
		const unsigned short *keep = embedded->keep + cell * 4;
		long x, y;

		for (y = keep[1]; y < keep[3] && y < bitmap->height; y++)
		{
			for (x = keep[0]; x < keep[2] && x < bitmap->width; x++)
			{
				unsigned char texel[4];
				unsigned long dx, dy;

				if (!mcc_texel(bitmap, pixels, bytes, x, y, texel))
					return;
				for (dy = 0; dy < scale; dy++)
				{
					for (dx = 0; dx < scale; dx++)
						memcpy(rgba + (((unsigned long)y * scale + dy) * width + (unsigned long)x * scale + dx) * 4,
							texel, 4);
				}
			}
		}
	}

	return;
}

long hud_hires_asset_at(
	unsigned long address,
	long width,
	long height)
{
	long index;

	for (index = 0; index < hires_bitmap_count; index++)
	{
		struct bitmap_data *bitmap = hires_bitmaps[index].bitmap;

		if (bitmap->cache_block_index != NONE &&
			(unsigned long)bitmap->base_address == address &&
			bitmap->width == width &&
			bitmap->height == height)
		{
			return hires_bitmaps[index].asset;
		}
	}

	return NONE;
}
