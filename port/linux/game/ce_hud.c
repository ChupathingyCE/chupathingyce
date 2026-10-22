/*
CE_HUD.C

Custom Edition maps' HUD (cache_files.c, Custom Edition maps). Halo PC's HUD
interfaces are the Xbox's but for two things the Xbox's renderer does not
know, which are put right when the map's tags load:

  - Halo PC added a third scaling flag to a HUD element's placement, "use
    high resolution scale", and draws a flagged element at half its bitmap's
    size. Custom Edition's HUD bitmaps are made for that: a flagged element's
    bitmap is twice the size of the one its Xbox counterpart draws (the
    shield meter's, the ammunition meters'), with the same scale. The Xbox
    draws a bitmap at its size times the scale (hud_draw.c), so a flagged
    placement's scale is halved and the flag cleared. Two bitmap flags made
    since do the same from the bitmap an element draws: Chimera's "force HUD
    use high resolution scale" draws it as if its placement had the flag,
    and MCC's "0.5 HUD scale" halves its scale again, after that. (What the
    HUD globals draw with no placement of their own to scale, the digits and
    the waypoints' arrows, is scaled by those flags as it is drawn:
    hud_bitmap_group_scale, hud_draw.c.) A number's digits are drawn at the
    HUD's scale, not its placement's, so a number keeps the flag, and
    hud_draw_numbers draws a flagged number's digits at half size (a custom
    HUD's ammunition and grenade counts: grifball's Halo 3 HUD drew them
    twice Halo PC's size, over its panels).
  - A HUD meter's bitmap: the meter shader (rasterizer_xbox_dynavobgeom.c)
    compares the texel's color with the meter's value (the order the meter
    fills in) and draws only where there is alpha (its shape). Halo PC keeps
    those the other way round, the shape in the color and the fill order in
    alpha. Every bitmap a unit or weapon HUD interface draws as a meter is
    listed (ce_hud_bitmap_is_meter), and the texture cache has the renderer
    sample its channels from where Halo PC keeps them (xbox_texture_cache.c,
    D3DCOMMON_PORT_PC_METER; xbox_textures.c, a texture swizzle).

And for one thing Halo PC's HUD has not: a HUD message's controller
buttons ("Hold %action to swap"). Its HUD globals name the Xbox's button
icons, but its message icons bitmap has none of the Xbox's pictures of the
buttons (13 sequences to the Xbox's 24), so the sequences they name are
other pictures there (the X button drew the rocket launcher). The Xbox's
button icons and their pictures are read once from ui.map (its HUD
globals' and its message icons bitmap's) and added to each Custom Edition
map's: its message icons bitmap gets the Xbox's sequences and bitmaps
after its own (the weapons' icons keep theirs), and its HUD globals the
Xbox's button icons, which name them (ce_hud_button_icons). The Xbox's
bitmaps are textures of their own, made from ui.map's pixels, not read
from the map's files.

Offsets are those of unit_hud_interface_definition.h, hud_definitions.h and
hud_weapon.c's weapon and grenade HUD interfaces (OpenSauce's
hud_definitions.hpp agrees).
*/

#ifdef HALO_CUSTOM_EDITION

#include "cseries.h"
#include "cseries_windows.h"
#include "errors.h"
#include "ce_map_checks.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmaps.h"
#include "rasterizer/rasterizer_swizzle.h"
#include "rasterizer/xbox/rasterizer_xbox_hardware_bitmaps.h"

/* (the port's zlib, as cache_files_decompress_windows.c inflates the maps with) */
#include "../../port/third_party/zlib/zlib_prefixed.h"

#include <stdio.h>
#include <stdlib.h>

/* ---------- constants */

enum
{
	/* a tag block: its count and its elements' address; a tag reference:
	its tag index */
	TAG_BLOCK_COUNT_OFFSET = 0x00,
	TAG_BLOCK_ADDRESS_OFFSET = 0x04,
	TAG_REFERENCE_INDEX_OFFSET = 0x0c,

	/* a placement (hud_placement_definition): its scale and scaling flags */
	PLACEMENT_SCALE_X_OFFSET = 0x04,
	PLACEMENT_SCALE_Y_OFFSET = 0x08,
	PLACEMENT_SCALING_FLAGS_OFFSET = 0x0c,
	PLACEMENT_HIGH_RESOLUTION_SCALE_FLAG = 1 << 2,
	/* a static or meter element (static_hud_element_definition,
	meter_hud_element_definition): its placement, then its bitmap */
	ELEMENT_BITMAP_OFFSET = 0x24,

	/* the unit HUD interface (unit_hud_interface_definition) */
	UNIT_HUD_BACKGROUND_OFFSET = 0x24,
	UNIT_HUD_SHIELD_BACKGROUND_OFFSET = 0x8c,
	UNIT_HUD_SHIELD_METER_OFFSET = 0xf4,
	UNIT_HUD_HEALTH_BACKGROUND_OFFSET = 0x17c,
	UNIT_HUD_HEALTH_METER_OFFSET = 0x1e4,
	UNIT_HUD_MOTION_SENSOR_BACKGROUND_OFFSET = 0x26c,
	UNIT_HUD_MOTION_SENSOR_FOREGROUND_OFFSET = 0x2d4,
	UNIT_HUD_BLIP_PLACEMENT_OFFSET = 0x35c,
	UNIT_HUD_AUXILARY_OVERLAYS_OFFSET = 0x3a4,
	AUXILARY_OVERLAY_SIZE = 0x84,
	UNIT_HUD_AUXILARY_METERS_OFFSET = 0x3cc,
	AUXILARY_METER_SIZE = 0x144,
	AUXILARY_METER_BACKGROUND_OFFSET = 0x14,
	AUXILARY_METER_METER_OFFSET = 0x7c,

	/* the weapon HUD interface: its blocks of statics, meters, numbers
	(each element a 0x24-byte header, then the element), crosshairs and
	overlays (each a bitmap and a block of items, an item's placement
	first) */
	WEAPON_HUD_STATICS_OFFSET = 0x60,
	WEAPON_HUD_METERS_OFFSET = 0x6c,
	WEAPON_HUD_NUMBERS_OFFSET = 0x78,
	WEAPON_HUD_CROSSHAIRS_OFFSET = 0x84,
	WEAPON_HUD_OVERLAYS_OFFSET = 0x90,
	WEAPON_HUD_STATIC_SIZE = 0xb4,
	WEAPON_HUD_METER_SIZE = 0xb4,
	WEAPON_HUD_NUMBER_SIZE = 0xa0,
	WEAPON_HUD_ELEMENT_OFFSET = 0x24,
	WEAPON_HUD_CROSSHAIRS_SIZE = 0x68,
	WEAPON_HUD_OVERLAYS_SIZE = 0x68,
	WEAPON_HUD_BITMAP_OFFSET = 0x24,
	WEAPON_HUD_ITEMS_OFFSET = 0x34,
	WEAPON_HUD_CROSSHAIR_ITEM_SIZE = 0x6c,
	WEAPON_HUD_OVERLAY_ITEM_SIZE = 0x88,

	/* the grenade HUD interface: its background, its count's background
	and numbers, and its overlays' bitmap and items */
	GRENADE_HUD_BACKGROUND_OFFSET = 0x24,
	GRENADE_HUD_COUNT_BACKGROUND_OFFSET = 0x8c,
	GRENADE_HUD_COUNT_NUMBERS_OFFSET = 0xf4,
	GRENADE_HUD_OVERLAY_BITMAP_OFFSET = 0x14c,
	GRENADE_HUD_OVERLAY_ITEMS_OFFSET = 0x15c,

	/* the HUD globals' messages' placement, their icons' bitmap and their
	button icons (hud_messaging_parameters_definition) */
	HUD_GLOBALS_MESSAGING_PLACEMENT_OFFSET = 0x24,
	HUD_GLOBALS_MESSAGING_ICONS_OFFSET = 0xa4,
	HUD_GLOBALS_BUTTON_ICONS_OFFSET = 0xc4,
	HUD_GLOBALS_BUTTON_ICON_SIZE = 0x10,

	/* a bitmap group: its header, sequences (each a block of sprites) and
	bitmaps */
	BITMAP_GROUP_SIZE = 0x6c,
	BITMAP_GROUP_FLAGS_OFFSET = 0x06,
	/* (its flags after the tag set's four: MCC's "0.5 HUD scale" and
	Chimera's "force HUD use high resolution scale") */
	BITMAP_GROUP_HALF_HUD_SCALE_FLAG = FLAG(_bitmap_group_half_hud_scale_bit),
	BITMAP_GROUP_FORCE_HIGH_RESOLUTION_SCALE_FLAG = FLAG(_bitmap_group_force_hud_high_resolution_scale_bit),
	BITMAP_GROUP_SEQUENCES_OFFSET = 0x54,
	BITMAP_GROUP_BITMAPS_OFFSET = 0x60,
	BITMAP_SEQUENCE_SIZE = 0x40,
	BITMAP_SEQUENCE_FIRST_BITMAP_OFFSET = 0x20,
	BITMAP_SEQUENCE_SPRITES_OFFSET = 0x34,
	BITMAP_SPRITE_SIZE = 0x20,
	BITMAP_SIZE = 0x30,

	/* ui.map, an Xbox cache file: its header ('head', version 5, its
	length; its tags' offset and size), its body after the header
	compressed (zlib), its tags at the Xbox's tag cache's address; its
	tags' header (the tag instances' address and count) */
	XBOX_MAP_HEADER_SIZE = 0x800,
	XBOX_MAP_HEADER_SIGNATURE = 'head',
	XBOX_MAP_VERSION = 5,
	XBOX_MAP_MAXIMUM_LENGTH = 0x10000000,
	XBOX_TAG_CACHE_BASE = 0x803a6000,
	XBOX_TAGS_INSTANCES_OFFSET = 0x00,
	XBOX_TAGS_COUNT_OFFSET = 0x0c,
	XBOX_MAXIMUM_TAGS = 0x10000,

	/* the most of the Xbox's button icons and of its message icons'
	sequences, sprites and bitmaps kept (ui.map has 18, 24, 28 and 4) */
	MAXIMUM_XBOX_BUTTON_ICONS = 64,
	MAXIMUM_XBOX_MESSAGE_SEQUENCES = 256,
	MAXIMUM_XBOX_MESSAGE_SPRITES = 1024,
	MAXIMUM_XBOX_MESSAGE_BITMAPS = 16,
	/* (the bitmaps' flags: bitmaps.c's) */
	BITMAP_POWER_OF_TWO_FLAG = 1 << 0,
	BITMAP_COMPRESSED_FLAG = 1 << 1,
	BITMAP_PALETTIZED_FLAG = 1 << 2,
	BITMAP_SWIZZLED_FLAG = 1 << 3,
	BITMAP_LINEAR_FLAG = 1 << 4,
	BITMAP_ALLOCATED_FLAG = 1 << 6,
	BITMAP_TYPE_2D = 0,

	/* the most meter bitmaps listed (Blood Gulch has 2) */
	MAXIMUM_CE_HUD_METER_BITMAPS = 64,

	/* (for the checks: a tag block's and a placement's sizes, and the most
	elements of a HUD block checked, more than any would hold) */
	TAG_BLOCK_SIZE = 0x0c,
	PLACEMENT_SIZE = 0x10,
	CE_HUD_MAXIMUM_ELEMENTS = 0x1000,
};

/* ---------- globals */

static void *ce_hud_tag_instances;
static long ce_hud_tag_count;
static long ce_hud_placements_halved;
/* the bitmaps (bitmap_data) drawn as meters, whose channels are Halo PC's
meter channels: the shape in the color, the fill order in alpha */
static void const *ce_hud_meter_bitmaps[MAXIMUM_CE_HUD_METER_BITMAPS];
static long ce_hud_meter_bitmap_count;

/* the Xbox's button icons (ui.map's), read once: its HUD globals' button
icons, and its message icons bitmap's sequences (each with its first sprite
here), sprites and bitmaps (textures of their own) */
static struct
{
	boolean read;
	boolean ready;
	long icon_count;
	byte icons[MAXIMUM_XBOX_BUTTON_ICONS * HUD_GLOBALS_BUTTON_ICON_SIZE];
	long sequence_count;
	byte sequences[MAXIMUM_XBOX_MESSAGE_SEQUENCES * BITMAP_SEQUENCE_SIZE];
	long first_sprites[MAXIMUM_XBOX_MESSAGE_SEQUENCES];
	long sprite_count;
	byte sprites[MAXIMUM_XBOX_MESSAGE_SPRITES * BITMAP_SPRITE_SIZE];
	long bitmap_count;
	struct bitmap_data *bitmaps[MAXIMUM_XBOX_MESSAGE_BITMAPS];
} ce_xbox_button_icons;

/* the message icons bitmaps given the Xbox's (a map's HUD globals may share
one), and where the Xbox's sequences begin in each */
static struct
{
	void const *bitmap_group;
	long first_sequence;
} ce_hud_button_icon_groups[4];
static long ce_hud_button_icon_group_count;

/* ---------- private code */

boolean ce_hud_bitmap_is_meter(void const *bitmap);
char const *cache_files_map_directory(void);
unsigned long ce_resources_allocate(unsigned long size);
unsigned long ce_resources_free_mark(void);
void ce_resources_free_to(unsigned long mark);

static unsigned long ce_hud_read_long(
	byte const *data)
{
	return *(unsigned long const *)data;
}

/* the bitmap group a tag reference names in this map's tags, or NULL */
static byte *ce_hud_bitmap_group(
	byte const *reference)
{
	long tag_index = (long)ce_hud_read_long(reference + TAG_REFERENCE_INDEX_OFFSET);
	struct ce_tag_instance *instance;

	if (tag_index == -1 || (tag_index & 0xffff) >= ce_hud_tag_count)
		return NULL;
	instance = (struct ce_tag_instance *)((byte *)ce_hud_tag_instances + (tag_index & 0xffff) * CE_TAG_INSTANCE_SIZE);
	if (instance->group_tag != 'bitm' || instance->tag_index != (unsigned long)tag_index || !instance->base_address)
		return NULL;
	return xbox_pointer(instance->base_address);
}

/* a placement, drawing the bitmap the reference `bitmap` names (NULL: none):
with the high resolution scale, its own or forced by its bitmap, drawn at
half its scale, and with its bitmap's 0.5 HUD scale at half that */
static void ce_hud_placement(
	byte *placement,
	byte const *bitmap)
{
	unsigned short *flags = (unsigned short *)(placement + PLACEMENT_SCALING_FLAGS_OFFSET);
	byte const *bitmap_group = bitmap ? ce_hud_bitmap_group(bitmap) : NULL;
	unsigned short bitmap_flags = bitmap_group ?
		*(unsigned short const *)(bitmap_group + BITMAP_GROUP_FLAGS_OFFSET) : 0;
	float scale = 1.0f;

	if ((*flags & PLACEMENT_HIGH_RESOLUTION_SCALE_FLAG) ||
		(bitmap_flags & BITMAP_GROUP_FORCE_HIGH_RESOLUTION_SCALE_FLAG))
	{
		scale *= 0.5f;
	}
	if (bitmap_flags & BITMAP_GROUP_HALF_HUD_SCALE_FLAG)
		scale *= 0.5f;
	if (scale != 1.0f)
	{
		*(float *)(placement + PLACEMENT_SCALE_X_OFFSET) *= scale;
		*(float *)(placement + PLACEMENT_SCALE_Y_OFFSET) *= scale;
		*flags &= (unsigned short)~PLACEMENT_HIGH_RESOLUTION_SCALE_FLAG;
		ce_hud_placements_halved++;
	}
}

/* a static or meter element's placement, drawing its own bitmap */
static void ce_hud_element_placement(
	byte *element)
{
	ce_hud_placement(element, element + ELEMENT_BITMAP_OFFSET);
}

/* the placement at `offset` in each element of a block: a static or meter
element's (each drawing its own bitmap), or another's, drawing `bitmap` */
static void ce_hud_block_placements(
	byte *block,
	unsigned long element_size,
	unsigned long offset,
	boolean elements,
	byte const *bitmap)
{
	long count = (long)ce_hud_read_long(block + TAG_BLOCK_COUNT_OFFSET);
	unsigned long address = ce_hud_read_long(block + TAG_BLOCK_ADDRESS_OFFSET);
	long index;

	if (count <= 0 || !address)
		return;
	for (index = 0; index < count; index++)
	{
		byte *placement = (byte *)xbox_pointer(address) + index * element_size + offset;

		if (elements)
			ce_hud_element_placement(placement);
		else
			ce_hud_placement(placement, bitmap);
	}
}

/* the items of each crosshair or overlay of a weapon HUD's block of them,
each drawing its crosshair's or overlay's bitmap */
static void ce_hud_items_placements(
	byte *block,
	unsigned long element_size,
	unsigned long item_size)
{
	long count = (long)ce_hud_read_long(block + TAG_BLOCK_COUNT_OFFSET);
	unsigned long address = ce_hud_read_long(block + TAG_BLOCK_ADDRESS_OFFSET);
	long index;

	if (count <= 0 || !address)
		return;
	for (index = 0; index < count; index++)
	{
		byte *element = (byte *)xbox_pointer(address) + index * element_size;

		ce_hud_block_placements(element + WEAPON_HUD_ITEMS_OFFSET, item_size, 0, FALSE,
			element + WEAPON_HUD_BITMAP_OFFSET);
	}
}

/* the bitmap a meter element draws: each of its bitmaps listed (once) */
static void ce_hud_meter_bitmap(
	byte const *element)
{
	byte *bitmap_group = ce_hud_bitmap_group(element + ELEMENT_BITMAP_OFFSET);
	long count;
	unsigned long address;
	long index;

	if (!bitmap_group)
		return;
	count = (long)ce_hud_read_long(bitmap_group + BITMAP_GROUP_BITMAPS_OFFSET + TAG_BLOCK_COUNT_OFFSET);
	address = ce_hud_read_long(bitmap_group + BITMAP_GROUP_BITMAPS_OFFSET + TAG_BLOCK_ADDRESS_OFFSET);
	if (count <= 0 || !address)
		return;
	for (index = 0; index < count; index++)
	{
		void const *bitmap = (byte *)xbox_pointer(address) + index * BITMAP_SIZE;

		if (ce_hud_bitmap_is_meter(bitmap))
			continue;
		if (ce_hud_meter_bitmap_count == MAXIMUM_CE_HUD_METER_BITMAPS)
		{
			error(_error_silent, "Custom Edition maps: more than %d meter bitmaps; the others keep their channels",
				MAXIMUM_CE_HUD_METER_BITMAPS);
			return;
		}
		ce_hud_meter_bitmaps[ce_hud_meter_bitmap_count++] = bitmap;
	}
}

/* the meter elements of a block, at `offset` in each element */
static void ce_hud_block_meter_bitmaps(
	byte *block,
	unsigned long element_size,
	unsigned long offset)
{
	long count = (long)ce_hud_read_long(block + TAG_BLOCK_COUNT_OFFSET);
	unsigned long address = ce_hud_read_long(block + TAG_BLOCK_ADDRESS_OFFSET);
	long index;

	if (count <= 0 || !address)
		return;
	for (index = 0; index < count; index++)
		ce_hud_meter_bitmap((byte *)xbox_pointer(address) + index * element_size + offset);
}

static void ce_hud_unit_interface(
	byte *hud)
{
	static unsigned long const placements[] =
	{
		UNIT_HUD_BACKGROUND_OFFSET,
		UNIT_HUD_SHIELD_BACKGROUND_OFFSET,
		UNIT_HUD_SHIELD_METER_OFFSET,
		UNIT_HUD_HEALTH_BACKGROUND_OFFSET,
		UNIT_HUD_HEALTH_METER_OFFSET,
		UNIT_HUD_MOTION_SENSOR_BACKGROUND_OFFSET,
		UNIT_HUD_MOTION_SENSOR_FOREGROUND_OFFSET,
	};
	unsigned long index;

	/* (meters first: a meter's bitmap is read from its element, which the
	placements do not move) */
	ce_hud_meter_bitmap(hud + UNIT_HUD_SHIELD_METER_OFFSET);
	ce_hud_meter_bitmap(hud + UNIT_HUD_HEALTH_METER_OFFSET);
	ce_hud_block_meter_bitmaps(hud + UNIT_HUD_AUXILARY_METERS_OFFSET, AUXILARY_METER_SIZE,
		AUXILARY_METER_METER_OFFSET);
	for (index = 0; index < NUMBEROF(placements); index++)
		ce_hud_element_placement(hud + placements[index]);
	/* (the blips' placement draws no bitmap of its own) */
	ce_hud_placement(hud + UNIT_HUD_BLIP_PLACEMENT_OFFSET, NULL);
	ce_hud_block_placements(hud + UNIT_HUD_AUXILARY_OVERLAYS_OFFSET, AUXILARY_OVERLAY_SIZE, 0, TRUE, NULL);
	ce_hud_block_placements(hud + UNIT_HUD_AUXILARY_METERS_OFFSET, AUXILARY_METER_SIZE,
		AUXILARY_METER_BACKGROUND_OFFSET, TRUE, NULL);
	ce_hud_block_placements(hud + UNIT_HUD_AUXILARY_METERS_OFFSET, AUXILARY_METER_SIZE,
		AUXILARY_METER_METER_OFFSET, TRUE, NULL);
}

static void ce_hud_weapon_interface(
	byte *hud)
{
	ce_hud_block_meter_bitmaps(hud + WEAPON_HUD_METERS_OFFSET, WEAPON_HUD_METER_SIZE, WEAPON_HUD_ELEMENT_OFFSET);
	ce_hud_block_placements(hud + WEAPON_HUD_STATICS_OFFSET, WEAPON_HUD_STATIC_SIZE, WEAPON_HUD_ELEMENT_OFFSET,
		TRUE, NULL);
	ce_hud_block_placements(hud + WEAPON_HUD_METERS_OFFSET, WEAPON_HUD_METER_SIZE, WEAPON_HUD_ELEMENT_OFFSET,
		TRUE, NULL);
	/* (numbers keep their flag: hud_draw_numbers draws a flagged number's
	digits at half size) */
	ce_hud_items_placements(hud + WEAPON_HUD_CROSSHAIRS_OFFSET, WEAPON_HUD_CROSSHAIRS_SIZE,
		WEAPON_HUD_CROSSHAIR_ITEM_SIZE);
	ce_hud_items_placements(hud + WEAPON_HUD_OVERLAYS_OFFSET, WEAPON_HUD_OVERLAYS_SIZE, WEAPON_HUD_OVERLAY_ITEM_SIZE);
}

static void ce_hud_grenade_interface(
	byte *hud)
{
	ce_hud_element_placement(hud + GRENADE_HUD_BACKGROUND_OFFSET);
	ce_hud_element_placement(hud + GRENADE_HUD_COUNT_BACKGROUND_OFFSET);
	ce_hud_block_placements(hud + GRENADE_HUD_OVERLAY_ITEMS_OFFSET, WEAPON_HUD_OVERLAY_ITEM_SIZE, 0, FALSE,
		hud + GRENADE_HUD_OVERLAY_BITMAP_OFFSET);
}

/* ---------- the Xbox's button icons */

/* a tag's instance in an image of an Xbox map's or this map's tags, if its
index is one of them and its group the one asked for */
static struct ce_tag_instance const *ce_hud_instance(
	struct ce_image const *image,
	unsigned long instances,
	long tag_count,
	unsigned long tag_index,
	unsigned long group_tag)
{
	struct ce_tag_instance const *instance;

	if (tag_index == 0xffffffffUL || (long)(tag_index & 0xffff) >= tag_count)
		return NULL;
	instance = ce_image_pointer(image, instances + (tag_index & 0xffff) * CE_TAG_INSTANCE_SIZE,
		CE_TAG_INSTANCE_SIZE);
	if (!instance || instance->group_tag != group_tag || instance->tag_index != tag_index)
		return NULL;
	return instance;
}

/* one of ui.map's message icons bitmaps made a texture of its own, from
its pixels (in the inflated map, swizzled as the Xbox keeps them); NULL if
it is not one this can be (uncompressed 2D ones, as ui.map's are) */
static struct bitmap_data *ce_hud_xbox_bitmap(
	byte const *data,
	byte const *map,
	unsigned long map_length)
{
	struct bitmap_data source;
	struct bitmap_data *bitmap;
	short bytes_per_pixel;
	unsigned long offset = 0;
	short level;

	memcpy(&source, data, sizeof(source));
	bytes_per_pixel = bitmap_format_get_bits_per_pixel(source.format) / 8;
	if (source.type != BITMAP_TYPE_2D || source.depth != 1 ||
		(source.flags & (BITMAP_COMPRESSED_FLAG | BITMAP_PALETTIZED_FLAG | BITMAP_LINEAR_FLAG)) ||
		!(source.flags & BITMAP_POWER_OF_TWO_FLAG) || source.width <= 0 || source.height <= 0 ||
		source.width > 1024 || source.height > 1024 || source.mipmap_count < 0 || source.mipmap_count > 10 ||
		(bytes_per_pixel != 1 && bytes_per_pixel != 2 && bytes_per_pixel != 4) ||
		source.pixels_offset < 0 || source.pixels_size < 0 ||
		!ce_range_within((unsigned long)source.pixels_offset, (unsigned long)source.pixels_size, map_length))
	{
		return NULL;
	}
	bitmap = bitmap_2d_new(source.width, source.height, source.mipmap_count, source.format);
	if (!bitmap)
		return NULL;
	bitmap->registration_point = source.registration_point;
	for (level = 0; level <= bitmap->mipmap_count; level++)
	{
		short width = bitmap_mipmap_get_width(bitmap, level);
		short height = bitmap_mipmap_get_height(bitmap, level);
		unsigned long size = (unsigned long)bitmap_mipmap_get_pixel_data_size(bitmap, level);
		byte const *pixels = map + source.pixels_offset + offset;
		byte *destination = bitmap_mipmap_address(bitmap, level);
		short x, y;

		if (!ce_range_within(offset, size, (unsigned long)source.pixels_size))
		{
			bitmap_delete(bitmap);
			return NULL;
		}
		/* (the texture is made from the bitmap's pixels in rows:
		rasterizer_bitmap_changed swizzles them again) */
		if (source.flags & BITMAP_SWIZZLED_FLAG)
		{
			for (y = 0; y < height; y++)
			{
				for (x = 0; x < width; x++)
				{
					long swizzled[2];

					bitmap_swizzle_vector2d(width, height, x, y, swizzled);
					memcpy(destination + ((long)y * width + x) * bytes_per_pixel,
						pixels + (swizzled[0] | swizzled[1]) * bytes_per_pixel, bytes_per_pixel);
				}
			}
		}
		else
			memcpy(destination, pixels, size);
		offset += size;
	}
	if (!rasterizer_bitmap_new(bitmap))
	{
		bitmap_delete(bitmap);
		return NULL;
	}
	rasterizer_bitmap_changed(bitmap);
	return bitmap;
}

/* ui.map's HUD globals' button icons and its message icons bitmap, from the
inflated map; FALSE if they are not there as the Xbox's are */
static boolean ce_hud_xbox_button_icons_parse(
	byte const *map,
	unsigned long map_length)
{
	struct ce_image image;
	unsigned long instances;
	long tag_count, index;
	struct ce_tag_instance const *instance = NULL;
	byte *hud = NULL, *group, *icons, *sequences, *bitmaps;
	long icon_count, sequence_count, bitmap_count;

	image.data = (byte *)map + ce_read_long(map + 0x10);
	image.base = XBOX_TAG_CACHE_BASE;
	image.size = ce_read_long(map + 0x14);
	if (!ce_image_pointer(&image, XBOX_TAG_CACHE_BASE, XBOX_TAGS_COUNT_OFFSET + 4))
		return FALSE;
	instances = ce_read_long(image.data + XBOX_TAGS_INSTANCES_OFFSET);
	tag_count = (long)ce_read_long(image.data + XBOX_TAGS_COUNT_OFFSET);
	if (tag_count <= 0 || tag_count > XBOX_MAXIMUM_TAGS ||
		!ce_image_pointer(&image, instances, (unsigned long)tag_count * CE_TAG_INSTANCE_SIZE))
	{
		return FALSE;
	}
	for (index = 0; index < tag_count && !hud; index++)
	{
		instance = ce_image_pointer(&image, instances + index * CE_TAG_INSTANCE_SIZE, CE_TAG_INSTANCE_SIZE);
		if (instance->group_tag == 'hudg')
			hud = ce_image_pointer(&image, instance->base_address, HUD_GLOBALS_BUTTON_ICONS_OFFSET + TAG_BLOCK_SIZE);
	}
	if (!hud)
		return FALSE;
	instance = ce_hud_instance(&image, instances, tag_count,
		ce_read_long(hud + HUD_GLOBALS_MESSAGING_ICONS_OFFSET + TAG_REFERENCE_INDEX_OFFSET), 'bitm');
	group = instance ? ce_image_pointer(&image, instance->base_address, BITMAP_GROUP_SIZE) : NULL;
	if (!group ||
		!ce_image_block(&image, hud + HUD_GLOBALS_BUTTON_ICONS_OFFSET, HUD_GLOBALS_BUTTON_ICON_SIZE,
			MAXIMUM_XBOX_BUTTON_ICONS, "ui.map's button icons", &icon_count, &icons) ||
		!ce_image_block(&image, group + BITMAP_GROUP_SEQUENCES_OFFSET, BITMAP_SEQUENCE_SIZE,
			MAXIMUM_XBOX_MESSAGE_SEQUENCES, "ui.map's message icons", &sequence_count, &sequences) ||
		!ce_image_block(&image, group + BITMAP_GROUP_BITMAPS_OFFSET, BITMAP_SIZE,
			MAXIMUM_XBOX_MESSAGE_BITMAPS, "ui.map's message icons", &bitmap_count, &bitmaps) ||
		!icon_count || !sequence_count || !bitmap_count)
	{
		return FALSE;
	}
	ce_xbox_button_icons.sprite_count = 0;
	for (index = 0; index < sequence_count; index++)
	{
		byte *sequence = sequences + index * BITMAP_SEQUENCE_SIZE;
		byte *sprites;
		long sprite_count;

		if (!ce_image_block(&image, sequence + BITMAP_SEQUENCE_SPRITES_OFFSET, BITMAP_SPRITE_SIZE,
			MAXIMUM_XBOX_MESSAGE_SPRITES - ce_xbox_button_icons.sprite_count, "ui.map's message icons' sprites",
			&sprite_count, &sprites))
		{
			return FALSE;
		}
		memcpy(ce_xbox_button_icons.sequences + index * BITMAP_SEQUENCE_SIZE, sequence, BITMAP_SEQUENCE_SIZE);
		ce_xbox_button_icons.first_sprites[index] = ce_xbox_button_icons.sprite_count;
		if (sprite_count)
		{
			memcpy(ce_xbox_button_icons.sprites + ce_xbox_button_icons.sprite_count * BITMAP_SPRITE_SIZE, sprites,
				(size_t)sprite_count * BITMAP_SPRITE_SIZE);
		}
		ce_xbox_button_icons.sprite_count += sprite_count;
	}
	ce_xbox_button_icons.sequence_count = sequence_count;
	memcpy(ce_xbox_button_icons.icons, icons, (size_t)icon_count * HUD_GLOBALS_BUTTON_ICON_SIZE);
	ce_xbox_button_icons.icon_count = icon_count;
	for (index = 0; index < bitmap_count; index++)
	{
		struct bitmap_data *bitmap = ce_hud_xbox_bitmap(bitmaps + index * BITMAP_SIZE, map, map_length);

		if (!bitmap)
		{
			while (--index >= 0)
			{
				rasterizer_bitmap_delete(ce_xbox_button_icons.bitmaps[index]);
				bitmap_delete(ce_xbox_button_icons.bitmaps[index]);
				ce_xbox_button_icons.bitmaps[index] = NULL;
			}
			return FALSE;
		}
		ce_xbox_button_icons.bitmaps[index] = bitmap;
	}
	ce_xbox_button_icons.bitmap_count = bitmap_count;
	return TRUE;
}

/* the Xbox's button icons read from ui.map, the first time a Custom
Edition map loads: its body inflated (about 32 MB, let go once they are
read). FALSE if they could not be, said once */
static boolean ce_hud_xbox_button_icons(
	void)
{
	char path[256];
	HANDLE file;
	unsigned long file_size, file_size_high = 0, bytes_read = 0;
	unsigned long header[6];
	byte *compressed = NULL, *map = NULL;
	z_stream stream;
	boolean inflated = FALSE;

	if (ce_xbox_button_icons.read)
		return ce_xbox_button_icons.ready;
	ce_xbox_button_icons.read = TRUE;
	snprintf(path, sizeof(path), "%sui.map", cache_files_map_directory());
	file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (file == INVALID_HANDLE_VALUE)
	{
		error(_error_silent, "Custom Edition maps: no %s; HUD messages draw no controller buttons", path);
		return FALSE;
	}
	file_size = GetFileSize(file, &file_size_high);
	if (file_size != INVALID_FILE_SIZE && !file_size_high && file_size > XBOX_MAP_HEADER_SIZE &&
		file_size <= XBOX_MAP_MAXIMUM_LENGTH)
	{
		compressed = system_malloc(file_size);
	}
	if (compressed && ReadFile(file, compressed, file_size, &bytes_read, NULL) && bytes_read == file_size)
	{
		memcpy(header, compressed, sizeof(header));
		if (header[0] == XBOX_MAP_HEADER_SIGNATURE && header[1] == XBOX_MAP_VERSION &&
			header[2] > XBOX_MAP_HEADER_SIZE && header[2] <= XBOX_MAP_MAXIMUM_LENGTH &&
			ce_range_within(header[4], header[5], header[2]))
		{
			map = system_malloc(header[2]);
		}
	}
	CloseHandle(file);
	if (map)
	{
		memcpy(map, compressed, XBOX_MAP_HEADER_SIZE);
		memset(&stream, 0, sizeof(stream));
		stream.next_in = compressed + XBOX_MAP_HEADER_SIZE;
		stream.avail_in = file_size - XBOX_MAP_HEADER_SIZE;
		stream.next_out = map + XBOX_MAP_HEADER_SIZE;
		stream.avail_out = header[2] - XBOX_MAP_HEADER_SIZE;
		if (inflateInit(&stream) == Z_OK)
		{
			int result = inflate(&stream, Z_FINISH);

			inflated = (result == Z_STREAM_END || result == Z_BUF_ERROR) &&
				stream.total_out == header[2] - XBOX_MAP_HEADER_SIZE;
			inflateEnd(&stream);
		}
	}
	system_free(compressed);
	ce_xbox_button_icons.ready = inflated && ce_hud_xbox_button_icons_parse(map, header[2]);
	system_free(map);
	if (!ce_xbox_button_icons.ready)
		error(_error_silent, "Custom Edition maps: %s has no button icons this can read; HUD messages draw no "
			"controller buttons", path);
	else
	{
		error(_error_silent, "Custom Edition maps: the Xbox's button icons (%ld, %ld sequences, %ld bitmaps) read "
			"from %s", ce_xbox_button_icons.icon_count, ce_xbox_button_icons.sequence_count,
			ce_xbox_button_icons.bitmap_count, path);
	}
	return ce_xbox_button_icons.ready;
}

/* a map's HUD globals given the Xbox's button icons: its message icons
bitmap the Xbox's sequences and bitmaps after its own, and its button icons
the Xbox's, naming them. Left as it is if it has no message icons bitmap or
the map's tag cache has no room */
static void ce_hud_button_icons(
	byte *hud,
	struct ce_image const *image)
{
	struct ce_tag_instance const *instance;
	byte *group, *sequences, *bitmaps, *icons;
	long sequence_count, bitmap_count, icon_count;
	long first_sequence = NONE;
	unsigned long mark, new_sequences, new_sprites, new_bitmaps, new_icons;
	long index;

	if (!ce_image_pointer(image, (unsigned long)xbox_address(hud), HUD_GLOBALS_BUTTON_ICONS_OFFSET + TAG_BLOCK_SIZE))
		return;
	instance = ce_hud_instance(image, (unsigned long)xbox_address(ce_hud_tag_instances), ce_hud_tag_count,
		ce_read_long(hud + HUD_GLOBALS_MESSAGING_ICONS_OFFSET + TAG_REFERENCE_INDEX_OFFSET), 'bitm');
	group = instance ? ce_image_pointer(image, instance->base_address, BITMAP_GROUP_SIZE) : NULL;
	if (!group || !ce_hud_xbox_button_icons())
		return;
	for (index = 0; index < ce_hud_button_icon_group_count; index++)
	{
		if (ce_hud_button_icon_groups[index].bitmap_group == group)
			first_sequence = ce_hud_button_icon_groups[index].first_sequence;
	}
	mark = ce_resources_free_mark();
	if (first_sequence == NONE)
	{
		if (ce_hud_button_icon_group_count == NUMBEROF(ce_hud_button_icon_groups) ||
			!ce_image_block(image, group + BITMAP_GROUP_SEQUENCES_OFFSET, BITMAP_SEQUENCE_SIZE, 0x7fff,
				"message icons", &sequence_count, &sequences) ||
			!ce_image_block(image, group + BITMAP_GROUP_BITMAPS_OFFSET, BITMAP_SIZE, 0x7fff,
				"message icons", &bitmap_count, &bitmaps) ||
			sequence_count + ce_xbox_button_icons.sequence_count > 0x7fff ||
			bitmap_count + ce_xbox_button_icons.bitmap_count > 0x7fff)
		{
			return;
		}
		new_sequences = ce_resources_allocate(
			(unsigned long)(sequence_count + ce_xbox_button_icons.sequence_count) * BITMAP_SEQUENCE_SIZE);
		new_sprites = ce_resources_allocate((unsigned long)ce_xbox_button_icons.sprite_count * BITMAP_SPRITE_SIZE);
		new_bitmaps = ce_resources_allocate(
			(unsigned long)(bitmap_count + ce_xbox_button_icons.bitmap_count) * BITMAP_SIZE);
		new_icons = ce_resources_allocate((unsigned long)ce_xbox_button_icons.icon_count * HUD_GLOBALS_BUTTON_ICON_SIZE);
		if (!new_sequences || !new_sprites || !new_bitmaps || !new_icons)
		{
			ce_resources_free_to(mark);
			error(_error_silent, "Custom Edition maps: no room for the Xbox's button icons; HUD messages draw no "
				"controller buttons");
			return;
		}
		/* (the map's sequences and bitmaps, then the Xbox's: their sprites'
		bitmaps after the map's) */
		if (sequence_count)
			memcpy(xbox_pointer(new_sequences), sequences, (size_t)sequence_count * BITMAP_SEQUENCE_SIZE);
		for (index = 0; index < ce_xbox_button_icons.sequence_count; index++)
		{
			byte *sequence = (byte *)xbox_pointer(new_sequences) + (sequence_count + index) * BITMAP_SEQUENCE_SIZE;
			long first_sprite = ce_xbox_button_icons.first_sprites[index];
			long sprite_count = (index + 1 < ce_xbox_button_icons.sequence_count ?
				ce_xbox_button_icons.first_sprites[index + 1] : ce_xbox_button_icons.sprite_count) - first_sprite;
			long sprite;

			memcpy(sequence, ce_xbox_button_icons.sequences + index * BITMAP_SEQUENCE_SIZE, BITMAP_SEQUENCE_SIZE);
			ce_write_short(sequence + BITMAP_SEQUENCE_FIRST_BITMAP_OFFSET,
				(short)(ce_read_short(sequence + BITMAP_SEQUENCE_FIRST_BITMAP_OFFSET) + bitmap_count));
			ce_write_long(sequence + BITMAP_SEQUENCE_SPRITES_OFFSET + TAG_BLOCK_COUNT_OFFSET, (unsigned long)sprite_count);
			ce_write_long(sequence + BITMAP_SEQUENCE_SPRITES_OFFSET + TAG_BLOCK_ADDRESS_OFFSET,
				sprite_count ? new_sprites + (unsigned long)first_sprite * BITMAP_SPRITE_SIZE : 0);
			for (sprite = first_sprite; sprite < first_sprite + sprite_count; sprite++)
			{
				byte *at = (byte *)xbox_pointer(new_sprites) + sprite * BITMAP_SPRITE_SIZE;

				memcpy(at, ce_xbox_button_icons.sprites + sprite * BITMAP_SPRITE_SIZE, BITMAP_SPRITE_SIZE);
				ce_write_short(at, (short)(ce_read_short(at) + bitmap_count));
			}
		}
		if (bitmap_count)
			memcpy(xbox_pointer(new_bitmaps), bitmaps, (size_t)bitmap_count * BITMAP_SIZE);
		for (index = 0; index < ce_xbox_button_icons.bitmap_count; index++)
		{
			struct bitmap_data bitmap = *ce_xbox_button_icons.bitmaps[index];

			/* (drawn from its own texture: not the texture cache's, and not
			let go with the map) */
			bitmap.flags &= (unsigned short)~BITMAP_ALLOCATED_FLAG;
			bitmap.tag_index = (long)instance->tag_index;
			memcpy((byte *)xbox_pointer(new_bitmaps) + (bitmap_count + index) * BITMAP_SIZE, &bitmap, BITMAP_SIZE);
		}
		ce_write_long(group + BITMAP_GROUP_SEQUENCES_OFFSET + TAG_BLOCK_COUNT_OFFSET,
			(unsigned long)(sequence_count + ce_xbox_button_icons.sequence_count));
		ce_write_long(group + BITMAP_GROUP_SEQUENCES_OFFSET + TAG_BLOCK_ADDRESS_OFFSET, new_sequences);
		ce_write_long(group + BITMAP_GROUP_BITMAPS_OFFSET + TAG_BLOCK_COUNT_OFFSET,
			(unsigned long)(bitmap_count + ce_xbox_button_icons.bitmap_count));
		ce_write_long(group + BITMAP_GROUP_BITMAPS_OFFSET + TAG_BLOCK_ADDRESS_OFFSET, new_bitmaps);
		first_sequence = sequence_count;
		ce_hud_button_icon_groups[ce_hud_button_icon_group_count].bitmap_group = group;
		ce_hud_button_icon_groups[ce_hud_button_icon_group_count].first_sequence = first_sequence;
		ce_hud_button_icon_group_count++;
	}
	else
	{
		new_icons = ce_resources_allocate((unsigned long)ce_xbox_button_icons.icon_count * HUD_GLOBALS_BUTTON_ICON_SIZE);
		if (!new_icons)
			return;
	}
	/* (the Xbox's button icons, each naming its sequence after the map's) */
	icons = xbox_pointer(new_icons);
	icon_count = ce_xbox_button_icons.icon_count;
	memcpy(icons, ce_xbox_button_icons.icons, (size_t)icon_count * HUD_GLOBALS_BUTTON_ICON_SIZE);
	for (index = 0; index < icon_count; index++)
	{
		byte *icon = icons + index * HUD_GLOBALS_BUTTON_ICON_SIZE;

		ce_write_short(icon, (short)(ce_read_short(icon) + first_sequence));
	}
	ce_write_long(hud + HUD_GLOBALS_BUTTON_ICONS_OFFSET + TAG_BLOCK_COUNT_OFFSET, (unsigned long)icon_count);
	ce_write_long(hud + HUD_GLOBALS_BUTTON_ICONS_OFFSET + TAG_BLOCK_ADDRESS_OFFSET, new_icons);
}

/* ---------- public code */

/* a Custom Edition map's HUD interfaces made the Xbox's (cache_files.c),
after its resources are copied in (ce_resources.c), its bitmaps' flags
among them */
void ce_hud_tags_loaded(
	void *tag_instances,
	long tag_count)
{
	struct ce_image image;
	long index;

	ce_hud_tag_instances = tag_instances;
	ce_hud_tag_count = tag_count;
	ce_hud_placements_halved = 0;
	ce_hud_meter_bitmap_count = 0;
	/* (the button icons first: a message icons bitmap's bitmaps are moved,
	and the meters' are listed by where they are) */
	/* (and the room above it, where the moved blocks may be:
	ce_resources_allocate) */
	image.data = xbox_pointer(CE_IMAGE_TAG_CACHE_BASE);
	image.base = CE_IMAGE_TAG_CACHE_BASE;
	image.size = CE_IMAGE_TAG_CACHE_SIZE + CE_CONVERSION_ROOM;
	ce_hud_button_icon_group_count = 0;
	for (index = 0; index < tag_count; index++)
	{
		struct ce_tag_instance *instance = (struct ce_tag_instance *)((byte *)tag_instances +
			index * CE_TAG_INSTANCE_SIZE);

		if (instance->group_tag == 'hudg' && instance->base_address)
			ce_hud_button_icons(xbox_pointer(instance->base_address), &image);
	}
	for (index = 0; index < tag_count; index++)
	{
		struct ce_tag_instance *instance = (struct ce_tag_instance *)((byte *)tag_instances +
			index * CE_TAG_INSTANCE_SIZE);
		byte *hud;

		if (!instance->base_address)
			continue;
		hud = xbox_pointer(instance->base_address);
		switch (instance->group_tag)
		{
		case 'unhi':
			ce_hud_unit_interface(hud);
			break;
		case 'wphi':
			ce_hud_weapon_interface(hud);
			break;
		case 'grhi':
			ce_hud_grenade_interface(hud);
			break;
		case 'hudg':
			/* (the messages' placement places their text as well as
			their icons: only its own flag scales it) */
			ce_hud_placement(hud + HUD_GLOBALS_MESSAGING_PLACEMENT_OFFSET, NULL);
			break;
		}
	}
	error(_error_silent, "Custom Edition maps: %ld HUD placements at half their scale or less, %ld meter bitmaps sampled in "
		"Halo PC's channels", ce_hud_placements_halved, ce_hud_meter_bitmap_count);
	ce_hud_tag_instances = NULL;
	ce_hud_tag_count = 0;
}

/* the map unloaded (cache_files.c): no bitmaps are meters */
void ce_hud_tags_unloaded(
	void)
{
	ce_hud_meter_bitmap_count = 0;
}

/* the blocks of a weapon HUD interface's crosshairs or overlays, and their
items' blocks: in the tags */
static boolean ce_hud_check_items(
	struct ce_image const *image,
	byte const *block,
	unsigned long element_size,
	unsigned long item_size,
	char const *name)
{
	byte *elements, *items;
	long count, item_count, index;

	if (!ce_image_block(image, block, element_size, CE_HUD_MAXIMUM_ELEMENTS, name, &count, &elements))
		return FALSE;
	for (index = 0; index < count; index++)
	{
		if (!ce_image_block(image, elements + index * element_size + WEAPON_HUD_ITEMS_OFFSET, item_size,
			CE_HUD_MAXIMUM_ELEMENTS, name, &item_count, &items))
		{
			return FALSE;
		}
	}
	return TRUE;
}

/* a Custom Edition map being checked (ce_map_checks.c): every HUD
interface ce_hud_tags_loaded reads in the tags, and each block it walks */
boolean ce_hud_check(
	struct ce_image const *image,
	void const *tag_instances,
	long tag_count)
{
	long index;

	for (index = 0; index < tag_count; index++)
	{
		struct ce_tag_instance const *instance = (struct ce_tag_instance const *)((byte const *)tag_instances +
			index * CE_TAG_INSTANCE_SIZE);
		char const *name = ce_image_tag_name(image, instance);
		unsigned long size;
		byte *hud, *elements;
		long count;

		switch (instance->group_tag)
		{
		case 'unhi': size = UNIT_HUD_AUXILARY_METERS_OFFSET + TAG_BLOCK_SIZE; break;
		case 'wphi': size = WEAPON_HUD_OVERLAYS_OFFSET + TAG_BLOCK_SIZE; break;
		case 'grhi': size = GRENADE_HUD_OVERLAY_ITEMS_OFFSET + TAG_BLOCK_SIZE; break;
		case 'hudg': size = HUD_GLOBALS_MESSAGING_PLACEMENT_OFFSET + PLACEMENT_SIZE; break;
		default: continue;
		}
		hud = ce_image_pointer(image, instance->base_address, size);
		if (!hud)
			return ce_refuse("HUD %s is not in the tags", name);
		switch (instance->group_tag)
		{
		case 'unhi':
			if (!ce_image_block(image, hud + UNIT_HUD_AUXILARY_OVERLAYS_OFFSET, AUXILARY_OVERLAY_SIZE,
				CE_HUD_MAXIMUM_ELEMENTS, name, &count, &elements) ||
				!ce_image_block(image, hud + UNIT_HUD_AUXILARY_METERS_OFFSET, AUXILARY_METER_SIZE,
				CE_HUD_MAXIMUM_ELEMENTS, name, &count, &elements))
			{
				return FALSE;
			}
			break;
		case 'wphi':
			if (!ce_image_block(image, hud + WEAPON_HUD_STATICS_OFFSET, WEAPON_HUD_STATIC_SIZE,
				CE_HUD_MAXIMUM_ELEMENTS, name, &count, &elements) ||
				!ce_image_block(image, hud + WEAPON_HUD_METERS_OFFSET, WEAPON_HUD_METER_SIZE,
				CE_HUD_MAXIMUM_ELEMENTS, name, &count, &elements) ||
				!ce_image_block(image, hud + WEAPON_HUD_NUMBERS_OFFSET, WEAPON_HUD_NUMBER_SIZE,
				CE_HUD_MAXIMUM_ELEMENTS, name, &count, &elements) ||
				!ce_hud_check_items(image, hud + WEAPON_HUD_CROSSHAIRS_OFFSET, WEAPON_HUD_CROSSHAIRS_SIZE,
					WEAPON_HUD_CROSSHAIR_ITEM_SIZE, name) ||
				!ce_hud_check_items(image, hud + WEAPON_HUD_OVERLAYS_OFFSET, WEAPON_HUD_OVERLAYS_SIZE,
					WEAPON_HUD_OVERLAY_ITEM_SIZE, name))
			{
				return FALSE;
			}
			break;
		case 'grhi':
			if (!ce_image_block(image, hud + GRENADE_HUD_OVERLAY_ITEMS_OFFSET, WEAPON_HUD_OVERLAY_ITEM_SIZE,
				CE_HUD_MAXIMUM_ELEMENTS, name, &count, &elements))
			{
				return FALSE;
			}
			break;
		}
	}
	return TRUE;
}

/* whether a bitmap (bitmap_data) of the map loaded is a Custom Edition
meter's, its channels Halo PC's (xbox_texture_cache.c) */
boolean ce_hud_bitmap_is_meter(
	void const *bitmap)
{
	long index;

	for (index = 0; index < ce_hud_meter_bitmap_count; index++)
	{
		if (ce_hud_meter_bitmaps[index] == bitmap)
			return TRUE;
	}
	return FALSE;
}

#endif
