/*
HUD_HIRES_TAGS.C

The bitmaps the high-res HUD's textures stand for (port/linux/src/hud_hires.c),
found in each map's tags as it loads (scenario_tags_load): every map holds
its own copy of the HUD's bitmap groups.

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
#ifdef HALO_PS3
#include "bitmaps/bitmaps.h"
#include "rasterizer/xbox/rasterizer_xbox_hardware_bitmaps.h"
#include "interface/hud.h"
#include "interface/hud_definitions.h"
#endif

/* the platform layer's (port/linux/src) */
void platform_log(char const *format, ...);
long hud_hires_asset_count(void);
char const *hud_hires_asset_tag(long asset);
long hud_hires_asset_bitmap(long asset);
long hud_hires_asset_fits(long asset, long width, long height);
void hud_hires_map_loaded(const long *assets, long count);
void hud_hires_map_unloaded(void);
#ifdef HALO_PS3
/* (port/ps3/xbox/hud_hires_rsx.c) */
unsigned long hud_hires_asset_sprites(long asset);
int hud_hires_sprites_drawable(long asset, unsigned long address, unsigned long level0_size);
int hud_hires_asset_sprite_place(long asset, short sequence, short frame, real *cell, real *advance);
void hud_hires_register_placeholder(long asset, unsigned long const *texture);
int hud_hires_rsx_style(void);
#endif

void hud_hires_tags_loaded(long scenario_index);
void hud_hires_tags_unloaded(void);
long hud_hires_asset_at(unsigned long address, long width, long height);
#ifdef HALO_PS3
struct bitmap_data const *hud_hires_sprite_bitmap(struct bitmap_data const *bitmap, short sequence_index);
boolean hud_hires_sprite_stencil(struct bitmap_data const *drawn, struct bitmap_data const *bitmap);
boolean hud_hires_sprite_place(struct bitmap_data const *drawn, struct icon_hud_element_definition const *icon,
	short frame_index, real_rectangle2d *clip, real *advance);
#endif

/* ---------- constants */

enum
{
	MAXIMUM_HIRES_BITMAPS = 128,
#ifdef HALO_PS3
	MAXIMUM_HIRES_SPRITE_BITMAPS = 8,
#endif
};

/* ---------- globals */

static struct
{
	struct bitmap_data *bitmap;
	long asset;
} hires_bitmaps[MAXIMUM_HIRES_BITMAPS];
static long hires_bitmap_count = 0;

#ifdef HALO_PS3
/* the bitmaps some of whose sprites have a PS3 button's texture, with their placeholder (same size and format), and
whether the texture can be drawn for the pixels at checked_address in the style checked_style */
static struct
{
	struct bitmap_data *bitmap;
	long asset;
	struct bitmap_data *placeholder;
	boolean placeholder_failed;
	XPTR(void) checked_address;
	int checked_style;
	boolean drawable;
} hires_sprite_bitmaps[MAXIMUM_HIRES_SPRITE_BITMAPS];
static long hires_sprite_bitmap_count = 0;

/* ---------- private code */

static void hud_hires_sprite_bitmaps_forget(
	void)
{
	long index;

	for (index = 0; index < hires_sprite_bitmap_count; index++)
	{
		if (hires_sprite_bitmaps[index].placeholder)
		{
			hud_hires_register_placeholder(hires_sprite_bitmaps[index].asset, NULL);
			bitmap_delete(hires_sprite_bitmaps[index].placeholder);
		}
	}
	hires_sprite_bitmap_count = 0;

	return;
}
#endif

/* ---------- public code */

void hud_hires_tags_loaded(
	long scenario_index)
{
	long asset_count = hud_hires_asset_count();
	long asset;
	long missing = 0;

	hires_bitmap_count = 0;
#ifdef HALO_PS3
	hud_hires_sprite_bitmaps_forget();
#endif
	for (asset = 0; asset < asset_count && hires_bitmap_count < MAXIMUM_HIRES_BITMAPS; asset++)
	{
		long group_index = tag_loaded(BITMAP_GROUP_TAG, hud_hires_asset_tag(asset));
		struct bitmap_data *bitmap = group_index == NONE ? NULL :
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
#ifdef HALO_PS3
		else if (hud_hires_asset_sprites(asset))
		{
			if (hires_sprite_bitmap_count < MAXIMUM_HIRES_SPRITE_BITMAPS)
			{
				csmemset(&hires_sprite_bitmaps[hires_sprite_bitmap_count], 0, sizeof(hires_sprite_bitmaps[0]));
				hires_sprite_bitmaps[hires_sprite_bitmap_count].bitmap = bitmap;
				hires_sprite_bitmaps[hires_sprite_bitmap_count].asset = asset;
				hires_sprite_bitmap_count++;
			}
		}
#endif
		else
		{
			hires_bitmaps[hires_bitmap_count].bitmap = bitmap;
			hires_bitmaps[hires_bitmap_count].asset = asset;
			hires_bitmap_count++;
		}
	}
#ifdef HALO_PS3
	platform_log("high-res hud: %ld of %ld bitmaps in this map, %ld for some of their sprites (%ld not in it)",
		hires_bitmap_count + hires_sprite_bitmap_count, asset_count, hires_sprite_bitmap_count, missing);
#else
	platform_log("high-res hud: %ld of %ld bitmaps in this map (%ld not in it)",
		hires_bitmap_count, asset_count, missing);
#endif
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
#ifdef HALO_PS3
	hud_hires_sprite_bitmaps_forget();
#endif
	hud_hires_map_unloaded();

	return;
}

#ifdef HALO_PS3
struct bitmap_data const *hud_hires_sprite_bitmap(
	struct bitmap_data const *bitmap,
	short sequence_index)
{
	long index;

	for (index = 0; index < hires_sprite_bitmap_count; index++)
	{
		struct bitmap_data *sprite_bitmap = hires_sprite_bitmaps[index].bitmap;

		if (sprite_bitmap != bitmap ||
			sequence_index < 0 ||
			sequence_index >= 32 ||
			!(hud_hires_asset_sprites(hires_sprite_bitmaps[index].asset) & (1UL << sequence_index)))
		{
			continue;
		}
		/* (checked on its pixels in the texture cache, once they are loaded) */
		if (sprite_bitmap->cache_block_index == NONE || !sprite_bitmap->base_address)
			break;
		/* (and again in another BUTTON ICONS style: XBOX draws no texture) */
		if (hires_sprite_bitmaps[index].checked_address != sprite_bitmap->base_address ||
			hires_sprite_bitmaps[index].checked_style != hud_hires_rsx_style())
		{
			hires_sprite_bitmaps[index].checked_address = sprite_bitmap->base_address;
			hires_sprite_bitmaps[index].drawable = hud_hires_sprites_drawable(
				hires_sprite_bitmaps[index].asset,
				(unsigned long)sprite_bitmap->base_address,
				(unsigned long)bitmap_mipmap_get_pixel_data_size(sprite_bitmap, 0)) != 0;
			hires_sprite_bitmaps[index].checked_style = hud_hires_rsx_style();
		}
		if (hires_sprite_bitmaps[index].drawable &&
			!hires_sprite_bitmaps[index].placeholder &&
			!hires_sprite_bitmaps[index].placeholder_failed)
		{
			struct bitmap_data *placeholder = bitmap_2d_new(
				sprite_bitmap->width,
				sprite_bitmap->height,
				0,
				sprite_bitmap->format);

			if (placeholder && !rasterizer_bitmap_new(placeholder))
			{
				bitmap_delete(placeholder);
				placeholder = NULL;
			}
			if (placeholder)
			{
				hud_hires_register_placeholder(
					hires_sprite_bitmaps[index].asset,
					(unsigned long const *)xbox_pointer(placeholder->hardware_format));
			}
			else
			{
				platform_log("high-res hud: no placeholder for %s's sprites: drawn as they are",
					hud_hires_asset_tag(hires_sprite_bitmaps[index].asset));
				hires_sprite_bitmaps[index].placeholder_failed = TRUE;
			}
			hires_sprite_bitmaps[index].placeholder = placeholder;
		}
		if (hires_sprite_bitmaps[index].drawable && hires_sprite_bitmaps[index].placeholder)
			return hires_sprite_bitmaps[index].placeholder;
		break;
	}

	return bitmap;
}

/* whether a sprite drawn from `drawn` (hud_hires_sprite_bitmap's) is a PS3 button's, not tinted as the Xbox's (green A,
red B, yellow Y): the HUD's stencils take the message's colour, the menus' keep their style's (ui_widget.c) */
boolean hud_hires_sprite_stencil(struct bitmap_data const *drawn, struct bitmap_data const *bitmap)
{
	return drawn != bitmap;
}

/* a PS3 button's own cell on the texture drawn from `drawn` and its width in texels, if not the Xbox sprite's place;
the Xbox's black and white buttons share sequence 13: the white (L1, button icon 5) has sequence 14's */
boolean hud_hires_sprite_place(struct bitmap_data const *drawn, struct icon_hud_element_definition const *icon,
	short frame_index, real_rectangle2d *clip, real *advance)
{
	short sequence = icon->sequence_index;
	long index;

	if (hud_globals && hud_globals->messaging.button_icons.count > 5 && sequence == 13 &&
		icon == TAG_BLOCK_GET_ELEMENT(&hud_globals->messaging.button_icons, 5, struct icon_hud_element_definition))
		sequence = 14;
	for (index = 0; index < hires_sprite_bitmap_count; index++)
	{
		if (drawn && hires_sprite_bitmaps[index].placeholder == drawn)
			return hud_hires_asset_sprite_place(hires_sprite_bitmaps[index].asset, sequence,
				(short)(frame_index & 0x7FFF), clip->n, advance) != 0;
	}

	return FALSE;
}
#endif

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
