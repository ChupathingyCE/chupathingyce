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
    placement's scale is halved and the flag cleared.
  - A HUD meter's bitmap: the meter shader (rasterizer_xbox_dynavobgeom.c)
    compares the texel's color with the meter's value (the order the meter
    fills in) and draws only where there is alpha (its shape). Halo PC keeps
    those the other way round, the shape in the color and the fill order in
    alpha. Every bitmap a unit or weapon HUD interface draws as a meter is
    listed (ce_hud_bitmap_is_meter), and the texture cache has the renderer
    sample its channels from where Halo PC keeps them (xbox_texture_cache.c,
    D3DCOMMON_PORT_PC_METER; xbox_textures.c, a texture swizzle).

Offsets are those of unit_hud_interface_definition.h, hud_definitions.h and
hud_weapon.c's weapon and grenade HUD interfaces (OpenSauce's
hud_definitions.hpp agrees).
*/

#ifdef HALO_64BIT

#include "cseries.h"
#include "cseries_windows.h"
#include "errors.h"
#include "ce_map_checks.h"

/* ---------- constants */

enum
{
	CE_TAG_INSTANCE_SIZE = 0x20,

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
	WEAPON_HUD_ITEMS_OFFSET = 0x34,
	WEAPON_HUD_CROSSHAIR_ITEM_SIZE = 0x6c,
	WEAPON_HUD_OVERLAY_ITEM_SIZE = 0x88,

	/* the grenade HUD interface: its background, its count's background
	and numbers, and its overlays' items */
	GRENADE_HUD_BACKGROUND_OFFSET = 0x24,
	GRENADE_HUD_COUNT_BACKGROUND_OFFSET = 0x8c,
	GRENADE_HUD_COUNT_NUMBERS_OFFSET = 0xf4,
	GRENADE_HUD_OVERLAY_ITEMS_OFFSET = 0x15c,

	/* the HUD globals' messages' placement */
	HUD_GLOBALS_MESSAGING_PLACEMENT_OFFSET = 0x24,

	/* a bitmap group's bitmaps */
	BITMAP_GROUP_BITMAPS_OFFSET = 0x60,
	BITMAP_SIZE = 0x30,

	/* the most meter bitmaps listed (Blood Gulch has 2) */
	MAXIMUM_CE_HUD_METER_BITMAPS = 64,

	/* (for the checks: a tag block's and a placement's sizes, and the most
	elements of a HUD block checked, more than any would hold) */
	TAG_BLOCK_SIZE = 0x0c,
	PLACEMENT_SIZE = 0x10,
	CE_HUD_MAXIMUM_ELEMENTS = 0x1000,
};

/* ---------- structures */

/* (cache_files.c's) */
struct ce_tag_instance
{
	unsigned long group_tag;
	unsigned long parent_group_tags[2];
	unsigned long tag_index;
	unsigned long name;
	unsigned long base_address;
	unsigned long indexed;
	unsigned long unused;
};

/* ---------- globals */

static void *ce_hud_tag_instances;
static long ce_hud_tag_count;
static long ce_hud_placements_halved;
/* the bitmaps (bitmap_data) drawn as meters, whose channels are Halo PC's
meter channels: the shape in the color, the fill order in alpha */
static void const *ce_hud_meter_bitmaps[MAXIMUM_CE_HUD_METER_BITMAPS];
static long ce_hud_meter_bitmap_count;

/* ---------- private code */

boolean ce_hud_bitmap_is_meter(void const *bitmap);

static unsigned long ce_hud_read_long(
	byte const *data)
{
	return *(unsigned long const *)data;
}

/* a placement with the high resolution scale: drawn at half its scale */
static void ce_hud_placement(
	byte *placement)
{
	unsigned short *flags = (unsigned short *)(placement + PLACEMENT_SCALING_FLAGS_OFFSET);

	if (*flags & PLACEMENT_HIGH_RESOLUTION_SCALE_FLAG)
	{
		*(float *)(placement + PLACEMENT_SCALE_X_OFFSET) *= 0.5f;
		*(float *)(placement + PLACEMENT_SCALE_Y_OFFSET) *= 0.5f;
		*flags &= (unsigned short)~PLACEMENT_HIGH_RESOLUTION_SCALE_FLAG;
		ce_hud_placements_halved++;
	}
}

/* the placement at `offset` in each element of a block */
static void ce_hud_block_placements(
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
		ce_hud_placement((byte *)xbox_pointer(address) + index * element_size + offset);
}

/* the items of each crosshair or overlay of a weapon HUD's block of them */
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
		ce_hud_block_placements((byte *)xbox_pointer(address) + index * element_size + WEAPON_HUD_ITEMS_OFFSET,
			item_size, 0);
}

/* the bitmap a meter element draws: each of its bitmaps listed (once) */
static void ce_hud_meter_bitmap(
	byte const *element)
{
	long tag_index = (long)ce_hud_read_long(element + ELEMENT_BITMAP_OFFSET + TAG_REFERENCE_INDEX_OFFSET);
	struct ce_tag_instance *instance;
	byte *bitmap_group;
	long count;
	unsigned long address;
	long index;

	if (tag_index == -1 || (tag_index & 0xffff) >= ce_hud_tag_count)
		return;
	instance = (struct ce_tag_instance *)((byte *)ce_hud_tag_instances + (tag_index & 0xffff) * CE_TAG_INSTANCE_SIZE);
	if (instance->group_tag != 'bitm' || instance->tag_index != (unsigned long)tag_index)
		return;
	bitmap_group = xbox_pointer(instance->base_address);
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
		UNIT_HUD_BLIP_PLACEMENT_OFFSET,
	};
	unsigned long index;

	/* (meters first: a meter's bitmap is read from its element, which the
	placements do not move) */
	ce_hud_meter_bitmap(hud + UNIT_HUD_SHIELD_METER_OFFSET);
	ce_hud_meter_bitmap(hud + UNIT_HUD_HEALTH_METER_OFFSET);
	ce_hud_block_meter_bitmaps(hud + UNIT_HUD_AUXILARY_METERS_OFFSET, AUXILARY_METER_SIZE,
		AUXILARY_METER_METER_OFFSET);
	for (index = 0; index < NUMBEROF(placements); index++)
		ce_hud_placement(hud + placements[index]);
	ce_hud_block_placements(hud + UNIT_HUD_AUXILARY_OVERLAYS_OFFSET, AUXILARY_OVERLAY_SIZE, 0);
	ce_hud_block_placements(hud + UNIT_HUD_AUXILARY_METERS_OFFSET, AUXILARY_METER_SIZE,
		AUXILARY_METER_BACKGROUND_OFFSET);
	ce_hud_block_placements(hud + UNIT_HUD_AUXILARY_METERS_OFFSET, AUXILARY_METER_SIZE,
		AUXILARY_METER_METER_OFFSET);
}

static void ce_hud_weapon_interface(
	byte *hud)
{
	ce_hud_block_meter_bitmaps(hud + WEAPON_HUD_METERS_OFFSET, WEAPON_HUD_METER_SIZE, WEAPON_HUD_ELEMENT_OFFSET);
	ce_hud_block_placements(hud + WEAPON_HUD_STATICS_OFFSET, WEAPON_HUD_STATIC_SIZE, WEAPON_HUD_ELEMENT_OFFSET);
	ce_hud_block_placements(hud + WEAPON_HUD_METERS_OFFSET, WEAPON_HUD_METER_SIZE, WEAPON_HUD_ELEMENT_OFFSET);
	ce_hud_block_placements(hud + WEAPON_HUD_NUMBERS_OFFSET, WEAPON_HUD_NUMBER_SIZE, WEAPON_HUD_ELEMENT_OFFSET);
	ce_hud_items_placements(hud + WEAPON_HUD_CROSSHAIRS_OFFSET, WEAPON_HUD_CROSSHAIRS_SIZE,
		WEAPON_HUD_CROSSHAIR_ITEM_SIZE);
	ce_hud_items_placements(hud + WEAPON_HUD_OVERLAYS_OFFSET, WEAPON_HUD_OVERLAYS_SIZE, WEAPON_HUD_OVERLAY_ITEM_SIZE);
}

static void ce_hud_grenade_interface(
	byte *hud)
{
	ce_hud_placement(hud + GRENADE_HUD_BACKGROUND_OFFSET);
	ce_hud_placement(hud + GRENADE_HUD_COUNT_BACKGROUND_OFFSET);
	ce_hud_placement(hud + GRENADE_HUD_COUNT_NUMBERS_OFFSET);
	ce_hud_block_placements(hud + GRENADE_HUD_OVERLAY_ITEMS_OFFSET, WEAPON_HUD_OVERLAY_ITEM_SIZE, 0);
}

/* ---------- public code */

/* a Custom Edition map's HUD interfaces made the Xbox's (cache_files.c),
after its resources are copied in (ce_resources.c), its bitmaps' flags
among them */
void ce_hud_tags_loaded(
	void *tag_instances,
	long tag_count)
{
	long index;

	ce_hud_tag_instances = tag_instances;
	ce_hud_tag_count = tag_count;
	ce_hud_placements_halved = 0;
	ce_hud_meter_bitmap_count = 0;
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
			ce_hud_placement(hud + HUD_GLOBALS_MESSAGING_PLACEMENT_OFFSET);
			break;
		}
	}
	error(_error_silent, "Custom Edition maps: %ld HUD placements at half their scale, %ld meter bitmaps sampled in "
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
