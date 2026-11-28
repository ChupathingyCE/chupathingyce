/*
OPENSAUCE_BSP_SETS.C

OpenSauce's structure BSP lightmap sets and sky sets (opensauce_bsp_sets.h).
An OpenSauce scenario keeps, in the scenario's own padding (0x508, past its
cutscene titles), a block of BSP modifiers: for a BSP, named lightmap sets
(another lightmap bitmap for the whole BSP) and named sky sets (other skies
for some of the scenario's sky indices). Its scripts choose one by the BSP's
index and the set's name. SPV3 uses them on six maps: a30's foggy and sunset
skies, b30_evolved's dawn, d40's dark and light skies, and the rest.

What a choice does is OpenSauce's (Kornner Studios, GPL-3.0-or-later): its
StructureBSP.cpp, read as facts, with the layouts of
scenario_definitions.hpp's s_scenario_bsp_modifier and the rest. The code
is this game's:
  - a lightmap set puts its standard lightmap bitmap in place of the BSP's
    own while that BSP is loaded (the BSP's data is read afresh at each
    load, so the stock one comes back on its own), when the bitmap has as
    many lightmaps as the BSP's own; objects' cached lighting is let go
    so it is taken again;
  - a sky set's skies stand for the scenario's skies at their indices
    while that BSP is current, through scenario_get_sky_definition_index,
    so every renderer draws them;
  - a choice made for a BSP not loaded is kept and put in place when it
    loads, as OpenSauce does;
  - the choices are game state, as OpenSauce keeps them (its own game-state
    memory): a checkpoint's revert or a saved game's load puts back the sets
    chosen when it was saved. A map starts with set 0 chosen for every BSP
    (OpenSauce's state starts zeroed), and a BSP with no set 0 keeps its
    own;
  - OpenSauce's directional lightmaps (its own lighting shader) are not
    drawn here: only the standard lightmap.
The choices ride in the game state's unused tail (game_state_port_cpu_region,
past every allocation, as the MCC checkpoint data does), claimed only while an
OpenSauce map with sets is loaded and given back as it was when it unloads,
so no other map's game state changes by a byte.

The block is not in the scenario's schema (it is padding to Halo PC), so it
is read here with every address checked to lie in the map's tags
(ce_loaded_pointer), every count bounded, and every tag it names looked up
as one of the group it must be. A map without an OpenSauce header never
reaches any of it.
*/

#include "cseries.h"
#include "errors.h"
#include "opensauce_bsp_sets.h"

#ifdef OPENSAUCE_BSP_SETS

#include "ce_map_checks.h"
#include "opensauce.h"
#include "bitmaps/bitmap_group.h"
#include "objects/objects.h"
#include "saved games/game_state.h"
#include "scenario/scenario.h"
#include "structures/structure_bsp_definitions.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "content_bytes.h"

/* ---------- constants */

enum
{
	TAG_INSTANCE_SIZE = 0x20,

	/* scenario_definitions.hpp's (OpenSauce's): the modifiers' block in
	the scenario, and the sizes of what it holds */
	SCENARIO_BSP_MODIFIERS_OFFSET = 0x508,
	SCENARIO_SIZE = 0x5B0,
	MODIFIER_SIZE = 0x40,
	MODIFIER_BSP_INDEX_OFFSET = 0x02,
	MODIFIER_LIGHTMAP_SETS_OFFSET = 0x04,
	MODIFIER_SKY_SETS_OFFSET = 0x10,
	LIGHTMAP_SET_SIZE = 0x7C,
	LIGHTMAP_SET_STANDARD_OFFSET = 0x24,
	SKY_SET_SIZE = 0x2C,
	SKY_SET_SKIES_OFFSET = 0x20,
	SKY_SET_SKY_SIZE = 0x14,
	SKY_SET_SKY_INDEX_OFFSET = 0x02,
	SKY_SET_SKY_REFERENCE_OFFSET = 0x04,
	SET_NAME_SIZE = 32,

	/* the most of each (OpenSauce's tag definitions: 64 sets a BSP, 8
	skies a set; modifiers, one a BSP) */
	MAXIMUM_MODIFIERS = 32,
	MAXIMUM_SETS = 64,
	MAXIMUM_SET_SKIES = 8,
	MAXIMUM_SKIES = 32,
	MAXIMUM_BSPS = 32,

	/* the choices' place in the game state: a mark, then a lightmap set and
	a sky set for each BSP (0 to start, as OpenSauce's) */
	GAME_STATE_MARK = 0x6F736273, /* 'osbs' */
	GAME_STATE_BYTES = 80,
};

struct bsp_sets_game_state
{
	unsigned long mark;
	signed char lightmap_sets[MAXIMUM_BSPS];
	signed char sky_sets[MAXIMUM_BSPS];
};
typedef char bsp_sets_game_state_size[sizeof(struct bsp_sets_game_state) <= GAME_STATE_BYTES ? 1 : -1];

/* ---------- structures */

struct bsp_modifier
{
	short structure_bsp_index;
	byte const *lightmap_sets;
	long lightmap_set_count;
	byte const *sky_sets;
	long sky_set_count;
};

/* ---------- globals */

static struct
{
	void *tag_instances;
	long tag_count;
	struct bsp_modifier modifiers[MAXIMUM_MODIFIERS];
	short modifier_count;
	/* the current BSP's, and what stands in for its skies (NONE: the
	scenario's own) */
	short structure_bsp_index;
	long sky_overrides[MAXIMUM_SKIES];
	/* the current BSP's own lightmap bitmap, while a set's stands in */
	long stock_lightmap;
	boolean lightmap_replaced;
	/* the choices, in the game state's tail, and what was there before */
	struct bsp_sets_game_state *state;
	byte state_original[GAME_STATE_BYTES];
	/* (no room in the tail: kept here instead, not reverted) */
	struct bsp_sets_game_state state_fallback;
} bsp_sets;

/* ---------- private code */

static void bsp_sets_log(
	char const *format,
	...)
{
	char text[512];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	error(_error_silent, "OpenSauce BSP sets: %s", text);
}

static unsigned short bsp_sets_u16(
	void const *at)
{
	unsigned short value;

	memcpy(&value, at, sizeof(value));
	return value;
}

/* a tag handle's root, if it is a tag of group in the map (its instance's
group and handle), else NULL */
static void *bsp_sets_tag_get(
	unsigned long tag_index,
	unsigned long group_tag,
	unsigned long size)
{
	byte const *instance;
	unsigned long instance_group, instance_index, address;

	if (tag_index == 0xFFFFFFFFUL || !bsp_sets.tag_instances || (long)(tag_index & 0xFFFF) >= bsp_sets.tag_count)
		return NULL;
	instance = (byte const *)bsp_sets.tag_instances + (tag_index & 0xFFFF) * TAG_INSTANCE_SIZE;
	memcpy(&instance_group, instance, sizeof(instance_group));
	memcpy(&instance_index, instance + 0x0C, sizeof(instance_index));
	memcpy(&address, instance + 0x14, sizeof(address));
	if (instance_group != group_tag || instance_index != tag_index)
		return NULL;
	return ce_loaded_pointer(address, size);
}

/* a block's elements, all within the map's tags, NULL if none */
static byte const *bsp_sets_block(
	byte const *owner,
	long offset,
	unsigned long element_size,
	long maximum,
	long *count)
{
	long elements = (long)content_u32(owner + offset);

	*count = 0;
	if (elements <= 0 || elements > maximum)
		return NULL;
	*count = elements;
	return ce_loaded_pointer(content_u32(owner + offset + 4), (unsigned long)elements * element_size);
}

static struct bsp_modifier *bsp_sets_modifier(
	short structure_bsp_index)
{
	short index;

	for (index = 0; index < bsp_sets.modifier_count; index++)
	{
		if (bsp_sets.modifiers[index].structure_bsp_index == structure_bsp_index)
			return &bsp_sets.modifiers[index];
	}
	return NULL;
}

/* a set by its name, NONE if there is none */
static short bsp_sets_find(
	byte const *sets,
	long count,
	unsigned long size,
	char const *name)
{
	long index;

	for (index = 0; sets && name && index < count; index++)
	{
		if (!strncmp((char const *)(sets + index * size), name, SET_NAME_SIZE))
			return (short)index;
	}
	return NONE;
}

/* the choices' place in the game state claimed, set 0 chosen for each BSP */
static void bsp_sets_state_claim(
	void)
{
	unsigned long used = 0, capacity = 0, offset;
	byte *image = game_state_port_cpu_region(&used, &capacity);

	offset = (used + 15) & ~15UL;
	if (image && offset + GAME_STATE_BYTES <= capacity)
	{
		bsp_sets.state = (struct bsp_sets_game_state *)(image + offset);
		memcpy(bsp_sets.state_original, bsp_sets.state, GAME_STATE_BYTES);
	}
	else
	{
		bsp_sets_log("no room in the game state: the sets chosen aren't reverted with a checkpoint");
		bsp_sets.state = &bsp_sets.state_fallback;
	}
	memset(bsp_sets.state, 0, sizeof(*bsp_sets.state));
	bsp_sets.state->mark = GAME_STATE_MARK;
}

/* a set's index chosen for a BSP, NONE for none (or one the BSP hasn't) */
static short bsp_sets_chosen(
	struct bsp_modifier const *modifier,
	boolean sky)
{
	long index, count;

	if (!modifier || !bsp_sets.state)
		return NONE;
	index = sky ? bsp_sets.state->sky_sets[modifier->structure_bsp_index] :
		bsp_sets.state->lightmap_sets[modifier->structure_bsp_index];
	count = sky ? modifier->sky_set_count : modifier->lightmap_set_count;
	return index >= 0 && index < count ? (short)index : NONE;
}

/* objects' cached lighting let go, so their lighting is taken again from the
lightmap now in place */
static void bsp_sets_objects_relight(
	void)
{
	struct object_iterator iterator;
	struct object_datum *object;

	object_iterator_new(&iterator, _object_mask_all, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
		object->object.cached_render_state_index = NONE;
}

/* the current BSP's lightmap: its chosen set's, or its own */
static void bsp_sets_apply_lightmap(
	struct bsp_modifier const *modifier)
{
	struct structure_bsp *structure_bsp = global_structure_bsp_get();
	long wanted;

	if (!structure_bsp)
		return;
	if (!bsp_sets.lightmap_replaced)
		bsp_sets.stock_lightmap = structure_bsp->lightmap_group.index;
	wanted = bsp_sets.stock_lightmap;
	if (bsp_sets_chosen(modifier, FALSE) != NONE && modifier->lightmap_sets)
	{
		byte const *set = modifier->lightmap_sets + bsp_sets_chosen(modifier, FALSE) * LIGHTMAP_SET_SIZE;
		unsigned long bitmap_index = content_u32(set + LIGHTMAP_SET_STANDARD_OFFSET + 0x0C);
		struct bitmap_group const *replacement = bsp_sets_tag_get(bitmap_index, 'bitm', sizeof(struct bitmap_group));
		struct bitmap_group const *own = bsp_sets.stock_lightmap != NONE ?
			bsp_sets_tag_get((unsigned long)bsp_sets.stock_lightmap, 'bitm', sizeof(struct bitmap_group)) : NULL;

		/* (as many lightmaps as the BSP's own: its surfaces index them) */
		if (replacement && own && replacement->bitmaps.count >= own->bitmaps.count)
			wanted = (long)bitmap_index;
		else
			bsp_sets_log("lightmap set '%.32s' of bsp %d has no lightmap bitmap fit for it: the bsp's own kept",
				(char const *)set, (int)modifier->structure_bsp_index);
	}
	if (structure_bsp->lightmap_group.index != wanted)
	{
		structure_bsp->lightmap_group.index = wanted;
		bsp_sets_objects_relight();
	}
	bsp_sets.lightmap_replaced = wanted != bsp_sets.stock_lightmap;
}

/* the current BSP's skies: its chosen set's, or the scenario's own */
static void bsp_sets_apply_skies(
	struct bsp_modifier const *modifier)
{
	short index;

	for (index = 0; index < MAXIMUM_SKIES; index++)
		bsp_sets.sky_overrides[index] = NONE;
	if (bsp_sets_chosen(modifier, TRUE) != NONE && modifier->sky_sets)
	{
		byte const *set = modifier->sky_sets + bsp_sets_chosen(modifier, TRUE) * SKY_SET_SIZE;
		long count;
		byte const *skies = bsp_sets_block(set, SKY_SET_SKIES_OFFSET, SKY_SET_SKY_SIZE, MAXIMUM_SET_SKIES, &count);
		long sky;

		for (sky = 0; skies && sky < count; sky++)
		{
			byte const *entry = skies + sky * SKY_SET_SKY_SIZE;
			short sky_index = (short)bsp_sets_u16(entry + SKY_SET_SKY_INDEX_OFFSET);
			unsigned long tag_index = content_u32(entry + SKY_SET_SKY_REFERENCE_OFFSET + 0x0C);

			/* (a sky of the 'sky ' group; anything else passed over) */
			if (sky_index >= 0 && sky_index < MAXIMUM_SKIES && bsp_sets_tag_get(tag_index, 'sky ', 4))
				bsp_sets.sky_overrides[sky_index] = (long)tag_index;
		}
	}
}

/* ---------- public code */

void opensauce_bsp_sets_tags_loaded(
	void *tag_instances,
	long tag_count,
	unsigned long scenario_tag_index)
{
	byte const *scenario, *modifiers;
	long count, index;

	opensauce_bsp_sets_map_unloaded();
	if (!opensauce_map_active() || !tag_instances || tag_count <= 0)
		return;
	bsp_sets.tag_instances = tag_instances;
	bsp_sets.tag_count = tag_count;
	scenario = bsp_sets_tag_get(scenario_tag_index, 'scnr', SCENARIO_SIZE);
	modifiers = scenario ? bsp_sets_block(scenario, SCENARIO_BSP_MODIFIERS_OFFSET, MODIFIER_SIZE, MAXIMUM_MODIFIERS,
		&count) : NULL;
	for (index = 0; modifiers && index < count; index++)
	{
		byte const *modifier = modifiers + index * MODIFIER_SIZE;
		struct bsp_modifier *entry = &bsp_sets.modifiers[bsp_sets.modifier_count];
		short structure_bsp_index = (short)bsp_sets_u16(modifier + MODIFIER_BSP_INDEX_OFFSET);

		if (structure_bsp_index < 0 || structure_bsp_index >= MAXIMUM_BSPS || bsp_sets_modifier(structure_bsp_index))
			continue;
		memset(entry, 0, sizeof(*entry));
		entry->structure_bsp_index = structure_bsp_index;
		entry->lightmap_sets = bsp_sets_block(modifier, MODIFIER_LIGHTMAP_SETS_OFFSET, LIGHTMAP_SET_SIZE, MAXIMUM_SETS,
			&entry->lightmap_set_count);
		entry->sky_sets = bsp_sets_block(modifier, MODIFIER_SKY_SETS_OFFSET, SKY_SET_SIZE, MAXIMUM_SETS,
			&entry->sky_set_count);
		bsp_sets.modifier_count++;
	}
	if (bsp_sets.modifier_count)
	{
		bsp_sets_state_claim();
		bsp_sets_log("%d bsps have lightmap or sky sets", (int)bsp_sets.modifier_count);
	}
}

void opensauce_bsp_sets_map_unloaded(
	void)
{
	short index;

	/* (the game state's tail given back as it was) */
	if (bsp_sets.state && bsp_sets.state != &bsp_sets.state_fallback)
		memcpy(bsp_sets.state, bsp_sets.state_original, GAME_STATE_BYTES);
	memset(&bsp_sets, 0, sizeof(bsp_sets));
	bsp_sets.structure_bsp_index = NONE;
	bsp_sets.stock_lightmap = NONE;
	for (index = 0; index < MAXIMUM_SKIES; index++)
		bsp_sets.sky_overrides[index] = NONE;
}

void opensauce_bsp_sets_structure_bsp_loaded(
	short structure_bsp_index)
{
	struct bsp_modifier const *modifier;

	/* (the BSP's data is read afresh: its own lightmap is what is there) */
	bsp_sets.lightmap_replaced = FALSE;
	bsp_sets.structure_bsp_index = structure_bsp_index;
	if (!bsp_sets.modifier_count)
		return;
	modifier = bsp_sets_modifier(structure_bsp_index);
	if (modifier)
	{
		bsp_sets_log("bsp %d loaded: lightmap set %d of %ld, sky set %d of %ld (-1: its own)", (int)structure_bsp_index,
			(int)bsp_sets_chosen(modifier, FALSE), modifier->lightmap_set_count, (int)bsp_sets_chosen(modifier, TRUE),
			modifier->sky_set_count);
	}
	bsp_sets_apply_lightmap(modifier);
	bsp_sets_apply_skies(modifier);
}

void opensauce_bsp_sets_game_state_loaded(
	void)
{
	struct bsp_modifier const *modifier;

	if (!bsp_sets.modifier_count || !bsp_sets.state)
		return;
	/* (an image saved without them: set 0 for each, as a map starts) */
	if (bsp_sets.state->mark != GAME_STATE_MARK)
	{
		memset(bsp_sets.state, 0, sizeof(*bsp_sets.state));
		bsp_sets.state->mark = GAME_STATE_MARK;
	}
	modifier = bsp_sets_modifier(global_structure_bsp_index_get());
	bsp_sets_log("a game state loaded: bsp %d's sets put back", (int)global_structure_bsp_index_get());
	bsp_sets_apply_lightmap(modifier);
	bsp_sets_apply_skies(modifier);
}

long opensauce_bsp_sets_sky(
	short sky_index,
	long definition_index)
{
	if (!bsp_sets.modifier_count || sky_index < 0 || sky_index >= MAXIMUM_SKIES ||
		bsp_sets.sky_overrides[sky_index] == NONE)
	{
		return definition_index;
	}
	return bsp_sets.sky_overrides[sky_index];
}

boolean opensauce_bsp_sets_set_lightmap_set(
	short structure_bsp_index,
	char const *name)
{
	struct bsp_modifier *modifier = bsp_sets_modifier(structure_bsp_index);
	short set = modifier ? bsp_sets_find(modifier->lightmap_sets, modifier->lightmap_set_count, LIGHTMAP_SET_SIZE,
		name) : NONE;

	if (set == NONE || !bsp_sets.state)
		return FALSE;
	bsp_sets.state->lightmap_sets[structure_bsp_index] = (signed char)set;
	bsp_sets_log("bsp %d: lightmap set '%.32s'", (int)structure_bsp_index, name);
	if (structure_bsp_index == global_structure_bsp_index_get())
		bsp_sets_apply_lightmap(modifier);
	return TRUE;
}

boolean opensauce_bsp_sets_set_sky_set(
	short structure_bsp_index,
	char const *name)
{
	struct bsp_modifier *modifier = bsp_sets_modifier(structure_bsp_index);
	short set = modifier ? bsp_sets_find(modifier->sky_sets, modifier->sky_set_count, SKY_SET_SIZE, name) : NONE;

	if (set == NONE || !bsp_sets.state)
		return FALSE;
	bsp_sets.state->sky_sets[structure_bsp_index] = (signed char)set;
	bsp_sets_log("bsp %d: sky set '%.32s'", (int)structure_bsp_index, name);
	if (structure_bsp_index == global_structure_bsp_index_get())
		bsp_sets_apply_skies(modifier);
	return TRUE;
}

#endif
