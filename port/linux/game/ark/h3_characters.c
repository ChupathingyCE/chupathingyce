/*
H3_CHARACTERS.C

A Halo 3 mission's characters and squads made this engine's AI (a part of
h2_level.c, compiled after h3_vehicles.c and driven by h3_sandbox.c: it
uses the sandbox's build - its models, shaders and bitmaps - and
h2_weapons.c's model writer; built alone it is empty).

What is Halo 3's, read from the mission's own MCC map:
- the squads (scenario 0x384, 0x40 each: name, team 0x24, parent group
  0x26, zone 0x28, its cells 0x30 - 0x60 each: count 4, character 8, weapon
  0xA, vehicle 0x12, starting locations 0x54 - 0x88 each: position 8, facing
  0x18, character 0x28, weapon 0x2A), the character palette (0x3A8) and the
  weapon palette (0x120), each 0x10 a reference; a zone's firing positions
  (0x390, 0x40 each: firing positions 0x28, 0x28 each, a position first);
- the characters (char): parent 4, unit 0x14, variants 0x54 (a model variant
  name first), general 0x6C (actor type at 4), vitality 0x78 (normal body 4,
  shield 8), perception 0x90, movement 0xA8, charge 0xE4, weapons 0x174
  (0xE0 each: weapon 4, ranges from 0x14, first burst delay 0x50, surprise
  0x58) - a block a character leaves empty is its parent's;
- the units (bipd): its model (hlmt at 0x34: mode 0, coll 0x10, jmad 0x20,
  variants 0x64 - 0x30 each: name, regions 0x14 - 0x14 each: name,
  permutations 8 - 0x18 each: name), its pill's heights and radius (0x340),
  the camera heights (0x2E8);
- the render model in the variant's permutations (h3_spartan.c's reader, as
  the sandbox's weapons), its animation graph (h3_animation.c's source
  through h2_animation.c: Halo 2's maker), its collision model's BSPs on
  its nodes (h3_level.c's converter) and pathfinding spheres.

What is this engine's (Halo CE's AI, as a Halo 2 mission's characters are
put on it by h2_ai.c): each character an actor, each character and weapon
an actor variant, each squad an encounter of one squad with the firing
positions of its zone nearest it. The behaviour is this engine's actors'
with Halo 3's values where they map; Halo 3's own behaviours, objectives
and tasks, orders, styles, vehicles, equipment use, grenades and dialogue
are not here. Squads are placed by h3_campaign.c as the mission's scripts
place them.

The biped tag is the kit's multiplayer cyborg's with Halo 3's model, graph,
collision model, sizes and team; the collision model's vitality and
materials the kit cyborg's (a head material where Halo 3 names one).

Desktop only; HALO_H3_AI=0: none of it (the level as before).
*/

#ifdef H2_LEVEL_INTERNAL

#include "h3_campaign.h"
#include "h3_sandbox.h"
#include "ai/actor_types.h"
#include "ai/actor_definitions.h"
#include "ai/ai_scenario_definitions.h"
#include "scenario/scenario_definitions.h"
#include "units/biped_definitions.h"
#include "physics/collision_model_definitions.h"

/* (h3_level.c) this engine's collision BSP (0x60 bytes at field of the
region) from a Halo 3 one in the map's tag data (its eight blocks at
address); FALSE if it cannot be */
boolean h3_level_collision_bsp_write(void *region, unsigned long field, struct h3_cache *cache, unsigned long address,
	long material_count);

/* ---------- constants */

enum
{
	H3C_SCNR_WEAPON_PALETTE = 0x120,
	H3C_SCNR_SQUADS = 0x384,
	H3C_SCNR_ZONES = 0x390,
	H3C_SCNR_CHARACTERS = 0x3A8,
	H3C_SQUAD_SIZE = 0x40,
	H3C_CELL_SIZE = 0x60,
	H3C_LOCATION_SIZE = 0x88,
	H3C_ZONE_SIZE = 0x40,
	H3C_FIRING_POSITION_SIZE = 0x28,

	H3C_CHAR_PARENT = 0x04,
	H3C_CHAR_UNIT = 0x14,
	H3C_CHAR_VARIANTS = 0x54,
	H3C_CHAR_GENERAL = 0x6C,
	H3C_CHAR_VITALITY = 0x78,
	H3C_CHAR_PERCEPTION = 0x90,
	H3C_CHAR_MOVEMENT = 0xA8,
	H3C_CHAR_CHARGE = 0xE4,
	H3C_CHAR_WEAPONS = 0x174,
	H3C_CHAR_WEAPON_SIZE = 0xE0,

	H3C_BIPD_HLMT = 0x34,
	H3C_BIPD_CAMERA = 0x2E8,
	H3C_BIPD_PILL = 0x340,
	H3C_HLMT_MODE = 0x00,
	H3C_HLMT_COLL = 0x10,
	H3C_HLMT_JMAD = 0x20,
	H3C_HLMT_VARIANTS = 0x64,
	H3C_VARIANT_SIZE = 0x30,
	H3C_VARIANT_REGION_SIZE = 0x14,
	H3C_VARIANT_PERMUTATION_SIZE = 0x18,

	H3C_COLL_MATERIALS = 0x14,
	H3C_COLL_REGIONS = 0x20,
	H3C_COLL_SPHERES = 0x2C,
	H3C_COLL_NODES = 0x38,

	H3C_MAXIMUM_SPECIES = 12,
	H3C_MAXIMUM_BIPEDS = 32,
	H3C_MAXIMUM_CHARACTERS = 48,
	H3C_MAXIMUM_VARIANTS = 64,
	H3C_MAXIMUM_SQUADS = 120,
	H3C_MAXIMUM_LOCATIONS = 24,
	H3C_MAXIMUM_POSITIONS = 128,
	H3C_MAXIMUM_CHAIN = 8,
	H3C_MAXIMUM_WEAPON_PATHS = 32,

	H3C_TEAM_PLAYER = 1,
	H3C_TEAM_HUMAN = 2,
	H3C_TEAM_COVENANT = 3,
	H3C_STATE_GUARDING = 8,
};

/* ---------- structures */

/* a Halo 3 model with its graph and collision model: every biped of it
shares them */
struct h3c_species
{
	long hlmt, mode, coll, jmad;
	char name[32];
	struct h2_graph *graph;
	short first_biped;          /* (the biped whose model's nodes the graph and collision model are made on) */
};

/* a unit in a model variant */
struct h3c_biped
{
	short species;
	long bipd;
	char variant[32];
	char name[64];
	struct h2w_model model;
	float standing, crouching, radius, camera_standing, camera_crouching;
};

struct h3c_character
{
	long tag;
	char name[96];
	short h3_type, ce_type, team;
	short biped;                /* h3c.bipeds' (NONE: none made) */
	long chain[H3C_MAXIMUM_CHAIN];
	short chain_length;
	unsigned long general, vitality, perception, movement, charge;
};

struct h3c_variant
{
	short character;
	long weapon;                /* Halo 3's weap tag (NONE) */
};

struct h3c_location
{
	float position[3];
	float facing;
	short variant;
};

struct h3c_squad
{
	long h3_index;
	char name[32];
	short zone;
	short team;
	short location_count;
	struct h3c_location locations[H3C_MAXIMUM_LOCATIONS];
};

static struct
{
	boolean gathered;
	struct h3_cache *cache;
	unsigned long scenario;
	struct h3c_species species[H3C_MAXIMUM_SPECIES];
	short species_count;
	struct h3c_biped *bipeds[H3C_MAXIMUM_BIPEDS];
	short biped_count;
	struct h3c_character characters[H3C_MAXIMUM_CHARACTERS];
	short character_count;
	struct h3c_variant variants[H3C_MAXIMUM_VARIANTS];
	short variant_count;
	struct h3c_squad *squads;
	short squad_count;
	long squads_seen, squads_elsewhere, squads_vehicle, locations_unmade;
	long kit_biped, kit_collision;  /* the kit's tags the bipeds are written on (NONE) */
	/* (models that could not be made: not tried again) */
	long failed[H3C_MAXIMUM_SPECIES];
	short failed_count;
} h3c;

/* ---------- private code: whether */

H2_ALIGN static boolean h3c_on(void)
{
#if !defined(HALO_VITA) && !defined(HALO_GXM_NULL)
	char const *setting = getenv("HALO_H3_AI");

	return !setting || atoi(setting) != 0;
#else
	return FALSE;
#endif
}

/* whether a unit's model is one made (HALO_H3_AI_UNITS: the names wanted,
"grunt,jackal"; else the Covenant's infantry and the Marines) */
H2_ALIGN static boolean h3c_unit_wanted(char const *path)
{
	static char const *const made[] = { "\\grunt\\grunt", "\\jackal\\jackal", "\\brute\\brute", "\\marine\\marine",
		"\\marine_female\\marine_female", "\\dervish\\dervish_ai", "\\elite\\elite", "\\hunter\\hunter" };
	char const *setting = getenv("HALO_H3_AI_UNITS");
	char const *last = path ? strrchr(path, '\\') : NULL;
	unsigned long k;

	if (!path || !last)
		return FALSE;
	if (setting && setting[0])
		return strstr(setting, last + 1) != NULL;
	for (k = 0; k < sizeof(made) / sizeof(made[0]); k++)
	{
		size_t length = strlen(path), suffix = strlen(made[k]);

		if (length >= suffix && !strcmp(path + length - suffix, made[k]))
			return TRUE;
	}
	return FALSE;
}

H2_ALIGN static float h3c_real(struct h3_cache *cache, unsigned long address, float fallback)
{
	float value;

	if (!address)
		return fallback;
	value = h3_cache_readf(cache, address);
	return value == value && fabsf(value) < 1.0e6f ? value : fallback;
}

/* ---------- private code: the gather */

/* a character's property block's first element up its parents (0: none) */
H2_ALIGN static unsigned long h3c_property(struct h3_cache *cache, struct h3c_character const *c, unsigned long field, unsigned long size)
{
	short i;

	for (i = 0; i < c->chain_length; i++)
	{
		struct h3_cache_tag t;
		long count;
		unsigned long elements;

		if (h3_cache_tag_get(cache, c->chain[i], &t) && t.address && h3_cache_block(cache, t.address + field, size, &count, &elements) &&
			count > 0)
		{
			return elements;
		}
	}
	return 0;
}

/* this engine's actor type for Halo 3's (Halo 2's numbers: the first are
this engine's, 16 the Brute, 18 the Drone) */
H2_ALIGN static short h3c_actor_type(short h3_type)
{
	if (h3_type >= 0 && h3_type < NUMBER_OF_ACTOR_TYPES)
		return h3_type == _actor_player ? _actor_marine : h3_type;
	return h3_type == 16 ? _actor_elite : _actor_grunt;
}

/* the variant's permutations as h3_model_read's look ("region=permutation,...") */
H2_ALIGN static void h3c_look(struct h3_cache *cache, unsigned long hlmt_address, char const *variant, char *look, long look_size)
{
	long count, i, length = 0;
	unsigned long variants;

	look[0] = 0;
	if (!h3_cache_block(cache, hlmt_address + H3C_HLMT_VARIANTS, H3C_VARIANT_SIZE, &count, &variants))
		return;
	for (i = 0; i < count; i++)
	{
		unsigned long at = variants + H3C_VARIANT_SIZE * (unsigned long)i, regions;
		long region_count, r;
		char const *name = h3_cache_string(cache, h3_cache_read32(cache, at));

		if (!name || strcmp(name, variant) || !h3_cache_block(cache, at + 0x14, H3C_VARIANT_REGION_SIZE, &region_count, &regions))
			continue;
		for (r = 0; r < region_count; r++)
		{
			unsigned long region = regions + H3C_VARIANT_REGION_SIZE * (unsigned long)r, permutations;
			long permutation_count;
			char region_name[48], permutation_name[48];

			if (!h3_cache_block(cache, region + 8, H3C_VARIANT_PERMUTATION_SIZE, &permutation_count, &permutations) || permutation_count < 1)
				continue;
			snprintf(region_name, sizeof(region_name), "%s", h3_cache_string(cache, h3_cache_read32(cache, region)));
			snprintf(permutation_name, sizeof(permutation_name), "%s", h3_cache_string(cache, h3_cache_read32(cache, permutations)));
			if (!region_name[0] || !permutation_name[0] || length + (long)strlen(region_name) + (long)strlen(permutation_name) + 3 >= look_size)
				continue;
			length += snprintf(look + length, (size_t)(look_size - length), "%s%s=%s", length ? "," : "", region_name, permutation_name);
		}
		break;
	}
}

/* a species' graph made on its first biped's nodes */
H2_ALIGN static boolean h3c_graph_make(struct h3_cache *cache, struct h3c_species *species, struct h2w_model const *model)
{
	struct h2_graph_source source;
	struct h3_animation_source *opened;
	char names[H2W_MAXIMUM_NODES][32];
	struct h2_animation_pose defaults[H2W_MAXIMUM_NODES];
	short parents[H2W_MAXIMUM_NODES];
	char why[160];
	long n;
	int k;

	why[0] = 0;
	opened = h3_animation_source_open(cache, species->jmad, &source, why, sizeof(why));
	if (!opened)
	{
		platform_log("h3 characters: %s: its animation graph: %s", species->name, why[0] ? why : "not read");
		return FALSE;
	}
	for (n = 0; n < model->node_count; n++)
	{
		struct h2w_node const *node = &model->nodes[n];

		snprintf(names[n], sizeof(names[n]), "%s", node->name);
		parents[n] = node->parent;
		for (k = 0; k < 3; k++)
			defaults[n].translation[k] = node->translation[k];
		/* (this engine's convention: conjugated, as h2_biped.c's model_nodes_read) */
		for (k = 0; k < 3; k++)
			defaults[n].rotation[k] = -node->rotation[k];
		defaults[n].rotation[3] = node->rotation[3];
		defaults[n].scale = 1.0f;
	}
	species->graph = h2_graph_make_from(&source, species->jmad, (char const (*)[32])names, defaults, parents, (short)model->node_count, FALSE,
		why, sizeof(why));
	h3_animation_source_close(opened);
	if (!species->graph)
	{
		platform_log("h3 characters: %s: its animation graph: %s", species->name, why[0] ? why : "not made");
		return FALSE;
	}
	{
		struct h2_graph_report report;

		h2_graph_report(species->graph, &report);
		platform_log("h3 characters: %s: its graph Halo 3's: %ld animations of %ld, %d seats, %ld classes, %ld slots missing, %ld KB",
			species->name, report.animations, report.h2_animations, (int)report.seats, report.classes, report.missing, report.bytes / 1024);
	}
	return TRUE;
}

/* the biped of a Halo 3 unit in a model variant (made the first time);
NONE if it cannot be */
H2_ALIGN static short h3c_biped_for(struct build *build, struct h3_cache *cache, long bipd, char const *variant)
{
	struct h3_cache_tag bt, ht;
	struct h3c_species *species = NULL;
	struct h3c_biped *biped;
	long hlmt;
	short i, s;
	char look[512];
	char const *path = h3_cache_tag_name(cache, bipd);

	if (bipd == NONE || !h3_cache_tag_get(cache, bipd, &bt) || bt.group_tag != 'bipd' || !bt.address || !h3c_unit_wanted(path))
		return NONE;
	/* (a variant the model has not: its default one, the object's at 0x30) */
	if (!variant || !variant[0])
		variant = h3_cache_string(cache, h3_cache_read32(cache, bt.address + 0x30));
	if (!variant)
		variant = "";
	for (i = 0; i < h3c.biped_count; i++)
	{
		if (h3c.bipeds[i]->bipd == bipd && !strcmp(h3c.bipeds[i]->variant, variant))
			return i;
	}
	hlmt = h3_cache_reference(cache, bt.address + H3C_BIPD_HLMT);
	if (hlmt == NONE || !h3_cache_tag_get(cache, hlmt, &ht) || !ht.address || h3c.biped_count >= H3C_MAXIMUM_BIPEDS)
		return NONE;
	for (s = 0; s < h3c.failed_count; s++)
	{
		if (h3c.failed[s] == hlmt)
			return NONE;
	}
	for (s = 0; s < h3c.species_count; s++)
	{
		if (h3c.species[s].hlmt == hlmt)
			species = &h3c.species[s];
	}
	if (!species)
	{
		char const *last = path ? strrchr(path, '\\') : NULL;

		if (h3c.species_count >= H3C_MAXIMUM_SPECIES)
			return NONE;
		species = &h3c.species[h3c.species_count];
		memset(species, 0, sizeof(*species));
		species->hlmt = hlmt;
		species->mode = h3_cache_reference(cache, ht.address + H3C_HLMT_MODE);
		species->coll = h3_cache_reference(cache, ht.address + H3C_HLMT_COLL);
		species->jmad = h3_cache_reference(cache, ht.address + H3C_HLMT_JMAD);
		species->first_biped = NONE;
		snprintf(species->name, sizeof(species->name), "%s", last ? last + 1 : "unit");
		if (species->mode == NONE || species->jmad == NONE)
		{
			platform_log("h3 characters: %s: no model or no graph: not made", species->name);
			return NONE;
		}
		h3c.species_count++;
	}
	s = (short)(species - h3c.species);
	biped = calloc(1, sizeof(*biped));
	if (!biped)
		return NONE;
	biped->species = s;
	biped->bipd = bipd;
	snprintf(biped->variant, sizeof(biped->variant), "%s", variant);
	snprintf(biped->name, sizeof(biped->name), "%s %s", species->name, variant[0] ? variant : "default");
	h3c_look(cache, ht.address, variant, look, sizeof(look));
	h2w_model_begin(&biped->model);
	biped->model.regioned = FALSE;
	{
		char unit[96];

		snprintf(unit, sizeof(unit), "the Halo 3 character %s (its model and graph)", biped->name);
		h2_build_heap_unit(unit);
	}
	if (!h3w_model_add_look(build, &biped->model, cache, species->mode, NULL, NULL, look[0] ? look : NULL, H2W_MAXIMUM_NODES) ||
		!h2w_model_finish(build, &biped->model))
	{
		platform_log("h3 characters: %s: its model could not be read: not made", biped->name);
		free(biped);
		goto failed;
	}
	/* (its graph: the species', made on the first biped's nodes - every
	variant of a model has the same) */
	if (!species->graph)
	{
		if (species->first_biped != NONE || !h3c_graph_make(cache, species, &biped->model))
		{
			free(biped);
			goto failed;
		}
		species->first_biped = h3c.biped_count;
	}
	/* its pill and camera (plausible ones alone: the kit biped's else) */
	biped->standing = h3_cache_readf(cache, bt.address + H3C_BIPD_PILL);
	biped->crouching = h3_cache_readf(cache, bt.address + H3C_BIPD_PILL + 4);
	biped->radius = h3_cache_readf(cache, bt.address + H3C_BIPD_PILL + 8);
	biped->camera_standing = h3_cache_readf(cache, bt.address + H3C_BIPD_CAMERA);
	biped->camera_crouching = h3_cache_readf(cache, bt.address + H3C_BIPD_CAMERA + 4);
	platform_log("h3 characters: %s (%s): %ld nodes, %ld markers, look '%s', pill %.2f/%.2f radius %.2f, camera %.2f/%.2f", biped->name,
		path, biped->model.node_count, biped->model.marker_count, look, biped->standing, biped->crouching, biped->radius,
		biped->camera_standing, biped->camera_crouching);
	h3c.bipeds[h3c.biped_count] = biped;
	return h3c.biped_count++;

failed:
	/* (a model none of whose bipeds was made: no tags of its own, not tried again) */
	if (species->first_biped == NONE && species == &h3c.species[h3c.species_count - 1])
	{
		if (h3c.failed_count < H3C_MAXIMUM_SPECIES)
			h3c.failed[h3c.failed_count++] = hlmt;
		h3c.species_count--;
	}
	return NONE;
}

/* a character of the map gathered (its index, NONE) */
H2_ALIGN static short h3c_character_for(struct build *build, struct h3_cache *cache, long tag)
{
	struct h3c_character *c;
	long unit = NONE, at = tag;
	char const *variant = NULL;
	short i;

	if (tag == NONE)
		return NONE;
	for (i = 0; i < h3c.character_count; i++)
	{
		if (h3c.characters[i].tag == tag)
			return i;
	}
	if (h3c.character_count >= H3C_MAXIMUM_CHARACTERS)
		return NONE;
	c = &h3c.characters[h3c.character_count];
	memset(c, 0, sizeof(*c));
	c->tag = tag;
	snprintf(c->name, sizeof(c->name), "%s", h3_cache_tag_name(cache, tag));
	/* its parents; its unit and model variant the first up the chain */
	while (at != NONE && c->chain_length < H3C_MAXIMUM_CHAIN)
	{
		struct h3_cache_tag t;
		long count;
		unsigned long elements;
		short k;

		for (k = 0; k < c->chain_length && c->chain[k] != at; k++)
			;
		if (k < c->chain_length || !h3_cache_tag_get(cache, at, &t) || t.group_tag != 'char' || !t.address)
			break;
		c->chain[c->chain_length++] = at;
		if (unit == NONE)
			unit = h3_cache_reference(cache, t.address + H3C_CHAR_UNIT);
		if (!variant && h3_cache_block(cache, t.address + H3C_CHAR_VARIANTS, 0x14, &count, &elements) && count > 0)
			variant = h3_cache_string(cache, h3_cache_read32(cache, elements));
		at = h3_cache_reference(cache, t.address + H3C_CHAR_PARENT);
	}
	c->general = h3c_property(cache, c, H3C_CHAR_GENERAL, 0x1C);
	c->vitality = h3c_property(cache, c, H3C_CHAR_VITALITY, 0x80);
	c->perception = h3c_property(cache, c, H3C_CHAR_PERCEPTION, 0x2C);
	c->movement = h3c_property(cache, c, H3C_CHAR_MOVEMENT, 0x2C);
	c->charge = h3c_property(cache, c, H3C_CHAR_CHARGE, 0x7C);
	c->h3_type = c->general ? h3_cache_read16(cache, c->general + 4) : 7;
	c->ce_type = h3c_actor_type(c->h3_type);
	c->team = c->ce_type == _actor_marine || c->ce_type == _actor_crew ? H3C_TEAM_HUMAN : H3C_TEAM_COVENANT;
	/* (the Arbiter and his Elites are the player's side in Halo 3) */
	if (strstr(c->name, "\\dervish\\") || strstr(c->name, "\\elite\\"))
		c->team = H3C_TEAM_HUMAN;
	c->biped = h3c_biped_for(build, cache, unit, variant);
	if (getenv("HALO_H3_AI_DEBUG"))
		platform_log("h3 characters: character %s: type %d (this engine's %d), team %d, unit %s variant '%s' -> biped %d", c->name,
			(int)c->h3_type, (int)c->ce_type, (int)c->team, unit != NONE ? h3_cache_tag_name(cache, unit) : "(none)", variant ? variant : "",
			(int)c->biped);
	return h3c.character_count++;
}

/* a character's weapons properties for a weapon (that weapon's entry up
the chain, else the chain's first entry; 0 none) */
H2_ALIGN static unsigned long h3c_weapon_properties(struct h3_cache *cache, struct h3c_character const *c, long weapon, long *default_weapon)
{
	unsigned long first = 0;
	short i;

	if (default_weapon)
		*default_weapon = NONE;
	for (i = 0; i < c->chain_length; i++)
	{
		struct h3_cache_tag t;
		long count, k;
		unsigned long elements;

		if (!h3_cache_tag_get(cache, c->chain[i], &t) || !t.address ||
			!h3_cache_block(cache, t.address + H3C_CHAR_WEAPONS, H3C_CHAR_WEAPON_SIZE, &count, &elements))
		{
			continue;
		}
		for (k = 0; k < count; k++)
		{
			unsigned long at = elements + H3C_CHAR_WEAPON_SIZE * (unsigned long)k;
			long tag = h3_cache_reference(cache, at + 4);

			if (!first)
			{
				first = at;
				if (default_weapon)
					*default_weapon = tag;
			}
			if (weapon != NONE && tag == weapon)
				return at;
		}
	}
	return first;
}

H2_ALIGN static short h3c_variant_for(struct h3_cache *cache, short character, long weapon)
{
	short i;

	if (character == NONE)
		return NONE;
	if (weapon == NONE)
		h3c_weapon_properties(cache, &h3c.characters[character], NONE, &weapon);
	for (i = 0; i < h3c.variant_count; i++)
	{
		if (h3c.variants[i].character == character && h3c.variants[i].weapon == weapon)
			return i;
	}
	if (h3c.variant_count >= H3C_MAXIMUM_VARIANTS)
	{
		for (i = 0; i < h3c.variant_count; i++)
		{
			if (h3c.variants[i].character == character)
				return i;
		}
		return NONE;
	}
	h3c.variants[h3c.variant_count].character = character;
	h3c.variants[h3c.variant_count].weapon = weapon;
	return h3c.variant_count++;
}

/* a palette's entry's tag (NONE) */
H2_ALIGN static long h3c_palette(struct h3_cache *cache, unsigned long scenario, unsigned long field, short index)
{
	long count;
	unsigned long elements;

	if (index < 0 || !h3_cache_block(cache, scenario + field, 0x10, &count, &elements) || index >= count)
		return NONE;
	return h3_cache_reference(cache, elements + 0x10 * (unsigned long)index);
}

H2_ALIGN static void h3c_free(void)
{
	short i;

	for (i = 0; i < h3c.biped_count; i++)
		free(h3c.bipeds[i]);
	for (i = 0; i < h3c.species_count; i++)
		h2_graph_free(h3c.species[i].graph);
	free(h3c.squads);
	memset(&h3c, 0, sizeof(h3c));
}

/* the weapons the mission's squads may hold (the scenario's weapon palette's
paths: the sandbox makes them with the level's own) */
H2_ALIGN static long h3c_weapon_paths(struct h3_cache *cache, struct h3_sandbox_palettes const *palettes, char const **paths, long room)
{
	long count, i, n = 0;
	unsigned long elements;

	if (!h3c_on() || !cache || !palettes->scenario ||
		!h3_cache_block(cache, palettes->scenario + H3C_SCNR_WEAPON_PALETTE, 0x10, &count, &elements))
	{
		return 0;
	}
	for (i = 0; i < count && n < room; i++)
	{
		long tag = h3_cache_reference(cache, elements + 0x10 * (unsigned long)i);
		char const *name = tag != NONE ? h3_cache_tag_name(cache, tag) : NULL;

		if (name && name[0])
			paths[n++] = name;
	}
	return n;
}

/* the mission's squads in the level gathered, their characters' bipeds
made into the build (their models, shaders and bitmaps the sandbox's) */
H2_ALIGN static void h3c_gather(struct build *build, struct h3_cache *cache, struct h3_sandbox_palettes const *palettes, unsigned char *kit)
{
	long squad_count, i;
	unsigned long squads;
	unsigned long long started = vita_host_time_us();

	h3c_free();
	if (!h3c_on() || !cache || !palettes->scenario ||
		!h3_cache_block(cache, palettes->scenario + H3C_SCNR_SQUADS, H3C_SQUAD_SIZE, &squad_count, &squads) || squad_count < 1)
	{
		h3_campaign_clear();
		return;
	}
	h3c.cache = cache;
	h3c.scenario = palettes->scenario;
	h3c.kit_biped = scene_pack_find(kit, 'bipd', "characters\\cyborg_mp\\cyborg_mp");
	h3c.kit_collision = scene_pack_find(kit, 'coll', "characters\\cyborg\\cyborg");
	h3c.squads = calloc(H3C_MAXIMUM_SQUADS, sizeof(*h3c.squads));
	if (!h3c.squads)
		return;
	h3_campaign_gather(cache, palettes->scenario);
	for (i = 0; i < squad_count; i++)
	{
		unsigned long squad = squads + H3C_SQUAD_SIZE * (unsigned long)i, cells;
		long cell_count, k, outside = 0, vehicle_cells = 0;
		struct h3c_squad *s;
		char name[32];

		if (!h3_cache_read(cache, squad, 32, name) || !h3_cache_block(cache, squad + 0x30, H3C_CELL_SIZE, &cell_count, &cells))
			continue;
		name[31] = 0;
		h3c.squads_seen++;
		if (h3c.squad_count >= H3C_MAXIMUM_SQUADS)
			break;
		s = &h3c.squads[h3c.squad_count];
		memset(s, 0, sizeof(*s));
		s->h3_index = i;
		snprintf(s->name, sizeof(s->name), "%s", name);
		s->zone = h3_cache_read16(cache, squad + 0x28);
		s->team = NONE;
		for (k = 0; k < cell_count; k++)
		{
			unsigned long cell = cells + H3C_CELL_SIZE * (unsigned long)k, locations;
			long location_count, n, wanted;
			short cell_character = h3_cache_read16(cache, cell + 8), cell_weapon = h3_cache_read16(cache, cell + 0xA);

			/* (a cell in a vehicle - a Phantom's pilot, a Banshee's -: not here) */
			if (h3_cache_read16(cache, cell + 0x12) != NONE)
			{
				vehicle_cells++;
				continue;
			}
			if (!h3_cache_block(cache, cell + 0x54, H3C_LOCATION_SIZE, &location_count, &locations))
				continue;
			/* (its count of its locations: the first of them - Halo 3 picks
			among them) */
			wanted = h3_cache_read16(cache, cell + 4);
			if (wanted <= 0 || wanted > location_count)
				wanted = location_count;
			for (n = 0; n < wanted && s->location_count < H3C_MAXIMUM_LOCATIONS; n++)
			{
				unsigned char const *l = h3_cache_at(cache, locations + H3C_LOCATION_SIZE * (unsigned long)n, H3C_LOCATION_SIZE);
				struct h3c_location *o = &s->locations[s->location_count];
				short character_index, weapon_index, character, variant;

				if (!l)
					continue;
				memcpy(o->position, l + 8, 12);
				memcpy(&o->facing, l + 0x18, 4);
				if (palettes->in_level && !palettes->in_level(palettes->context, o->position))
				{
					outside++;
					continue;
				}
				character_index = (short)read16(l + 0x28);
				weapon_index = (short)read16(l + 0x2A);
				if (character_index == NONE)
					character_index = cell_character;
				if (weapon_index == NONE)
					weapon_index = cell_weapon;
				character = h3c_character_for(build, cache, h3c_palette(cache, palettes->scenario, H3C_SCNR_CHARACTERS, character_index));
				if (character == NONE || h3c.characters[character].biped == NONE)
				{
					h3c.locations_unmade++;
					continue;
				}
				variant = h3c_variant_for(cache, character, h3c_palette(cache, palettes->scenario, H3C_SCNR_WEAPON_PALETTE, weapon_index));
				if (variant == NONE)
					continue;
				o->variant = variant;
				if (s->team == NONE)
					s->team = h3c.characters[character].team;
				s->location_count++;
			}
		}
		if (!s->location_count)
		{
			if (outside)
				h3c.squads_elsewhere++;
			else if (vehicle_cells)
				h3c.squads_vehicle++;
			continue;
		}
		h3c.squad_count++;
	}
	h3c.gathered = h3c.squad_count > 0;
	platform_log("h3 characters: %s: %d of %ld squads in this level (%ld in other BSPs, %ld vehicles' crews, %ld starting locations of "
		"characters not made), %d characters, %d actor variants, %d bipeds of %d models, %.0f ms", h3_cache_name(cache), (int)h3c.squad_count,
		h3c.squads_seen, h3c.squads_elsewhere, h3c.squads_vehicle, h3c.locations_unmade, (int)h3c.character_count, (int)h3c.variant_count,
		(int)h3c.biped_count, (int)h3c.species_count, (double)(vita_host_time_us() - started) / 1000.0);
}

/* a place beside the first squad of the player's side (else beside the
first squad): its first starting location, a step behind it */
H2_ALIGN static boolean h3c_friendly_start(float *start)
{
	short i, chosen = NONE;

	if (!h3c.gathered)
		return FALSE;
	for (i = 0; i < h3c.squad_count && chosen == NONE; i++)
	{
		if (h3c.squads[i].team == H3C_TEAM_HUMAN)
			chosen = i;
	}
	if (chosen == NONE)
		return FALSE;
	start[0] = h3c.squads[chosen].locations[0].position[0] - 0.6f * cosf(h3c.squads[chosen].locations[0].facing);
	start[1] = h3c.squads[chosen].locations[0].position[1] - 0.6f * sinf(h3c.squads[chosen].locations[0].facing);
	start[2] = h3c.squads[chosen].locations[0].position[2] + 0.1f;
	start[3] = h3c.squads[chosen].locations[0].facing;
	return TRUE;
}

/* ---------- private code: the tags

From the characters' first: each biped's bipd, each model's coll and antr,
each character's actr, each actor variant's actv */

H2_ALIGN static long h3c_tag_count(void)
{
	return h3c.gathered ? h3c.biped_count + 2 * h3c.species_count + h3c.character_count + h3c.variant_count : 0;
}

#define H3C_TAG_BIPED(i) ((long)(i))
#define H3C_TAG_COLLISION(s) ((long)h3c.biped_count + (long)(s))
#define H3C_TAG_GRAPH(s) ((long)h3c.biped_count + (long)h3c.species_count + (long)(s))
#define H3C_TAG_ACTOR(c) ((long)h3c.biped_count + 2 * (long)h3c.species_count + (long)(c))
#define H3C_TAG_VARIANT(v) ((long)h3c.biped_count + 2 * (long)h3c.species_count + (long)h3c.character_count + (long)(v))

H2_ALIGN static void h3c_tag_name(long index, unsigned long *group, char *text, long size)
{
	if (index < H3C_TAG_COLLISION(0))
		*group = 'bipd', snprintf(text, (size_t)size, "h3\\characters\\%s", h3c.bipeds[index]->name);
	else if (index < H3C_TAG_GRAPH(0))
		*group = 'coll', snprintf(text, (size_t)size, "h3\\characters\\%s", h3c.species[index - H3C_TAG_COLLISION(0)].name);
	else if (index < H3C_TAG_ACTOR(0))
		*group = 'antr', snprintf(text, (size_t)size, "h3\\characters\\%s", h3c.species[index - H3C_TAG_GRAPH(0)].name);
	else if (index < H3C_TAG_VARIANT(0))
		*group = 'actr', snprintf(text, (size_t)size, "h3\\ai\\%s", tag_short_name(h3c.characters[index - H3C_TAG_ACTOR(0)].name));
	else
	{
		struct h3c_variant const *v = &h3c.variants[index - H3C_TAG_VARIANT(0)];

		*group = 'actv';
		snprintf(text, (size_t)size, "h3\\ai\\%s %s %ld", tag_short_name(h3c.characters[v->character].name),
			v->weapon != NONE ? tag_short_name(h3_cache_tag_name(h3c.cache, v->weapon)) : "unarmed", index - H3C_TAG_VARIANT(0));
	}
}

/* ---------- private code: writing */

/* (h2_graph_write) the level's tag data as the graph writer's host */
H2_ALIGN static unsigned long h3c_host_alloc(void *context, unsigned long size)
{
	return region_alloc(context, size);
}

H2_ALIGN static unsigned char *h3c_host_at(void *context, unsigned long offset)
{
	return region_at(context, offset);
}

H2_ALIGN static unsigned long h3c_host_block(void *context, unsigned long field, long count, unsigned long element_size)
{
	return region_block(context, field, count, element_size);
}

H2_ALIGN static void h3c_host_data(void *context, unsigned long field, void const *bytes, unsigned long size)
{
	region_data(context, field, bytes, size);
}

H2_ALIGN static boolean h3c_host_failed(void *context)
{
	return ((struct region *)context)->b.failed;
}

H2_ALIGN static void h3c_set_root(struct region *r, unsigned long instances, long tag, unsigned long root)
{
	write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * tag + 20), r->link + root);
}

/* a species' collision model: the kit cyborg's (its vitality, its
materials' kinds) with the model's nodes, Halo 3's collision BSPs on them
and its pathfinding spheres */
H2_ALIGN static boolean h3c_collision_write(struct region *r, unsigned long instances, long tag, struct h3c_species const *species,
	long kit_collision)
{
	struct h3_cache *cache = h3c.cache;
	struct h2w_model const *model = &h3c.bipeds[species->first_biped]->model;
	struct h3_cache_tag t;
	unsigned long kit_root = kit_collision != NONE ? h2w_root(r, instances, kit_collision) : 0;
	unsigned long root, nodes, h3_materials, h3_regions, h3_nodes, h3_spheres, field, kit_materials;
	long material_count = 0, region_count = 0, node_count = 0, sphere_count = 0, kit_material_count, i, kept = 0, left_out = 0;
	short coll_node[64];

	if (!kit_root || species->coll == NONE || !h3_cache_tag_get(cache, species->coll, &t) || !t.address)
		return FALSE;
	root = h2w_tag_copy(r, kit_root, sizeof(struct collision_model), 'coll');
	if (r->b.failed)
		return FALSE;
	h3c_set_root(r, instances, tag, root);
	/* (Halo 3's collision nodes: the model's of the same names, NONE one it lacks) */
	if (h3_cache_block(cache, t.address + H3C_COLL_NODES, 0xC, &node_count, &h3_nodes))
	{
		for (i = 0; i < node_count && i < 64; i++)
		{
			char name[32];

			h3w_name(cache, h3_cache_read32(cache, h3_nodes + 0xC * (unsigned long)i), name, sizeof(name));
			coll_node[i] = h2w_model_node(model, name);
		}
	}
	if (node_count > 64)
		node_count = 64;
	/* its materials: one for each of Halo 3's, in its order (the surfaces keep
	their indices) - the kit's head material where Halo 3's name says head
	(a hit there a headshot), else its body's */
	kit_material_count = (long)read32(region_at(r, root + offsetof(struct collision_model, resistance.materials)));
	kit_materials = read32(region_at(r, root + offsetof(struct collision_model, resistance.materials) + 4));
	if (h3_cache_block(cache, t.address + H3C_COLL_MATERIALS, 4, &material_count, &h3_materials) && material_count > 0 &&
		kit_material_count >= 2 && kit_materials >= r->link)
	{
		struct damage_resistance_material kit[2];
		unsigned long out;

		memcpy(kit, region_at(r, kit_materials - r->link), sizeof(kit));
		out = region_block(r, root + offsetof(struct collision_model, resistance.materials), material_count,
			sizeof(struct damage_resistance_material));
		if (r->b.failed)
			return FALSE;
		for (i = 0; i < material_count; i++)
		{
			struct damage_resistance_material *m = (struct damage_resistance_material *)region_at(r,
				out + sizeof(struct damage_resistance_material) * (unsigned long)i);
			char const *name = h3_cache_string(cache, h3_cache_read32(cache, h3_materials + 4 * (unsigned long)i));

			*m = kit[name && strstr(name, "head") ? 0 : 1];
			snprintf(m->name, sizeof(m->name), "%s", name && name[0] ? name : "body");
			/* (a Jackal's arm shield: what hits it does the body no harm - as
			h2_biped.c's shield material; it is not worn down here) */
			if (name && strstr(name, "shield"))
			{
				m->material_type = 30;
				m->shield_leak_fraction = 0.0f;
				m->shield_damage_multiplier = 1.0f;
				m->body_damage_multiplier = 0.0f;
			}
		}
		write16(region_at(r, root + offsetof(struct collision_model, resistance.indirect_damage_material_index)), 0xFFFF);
	}
	else
		material_count = kit_material_count;
	/* (no shield of the kit cyborg's: a character's shield is its actor variant's) */
	writef(region_at(r, root + offsetof(struct collision_model, resistance.maximum_shield_vitality)), 0.0f);
	/* the nodes: the model's, a BSP on each that Halo 3 gives one (each
	region's first permutation's; a node's second left out) */
	nodes = region_block(r, root + offsetof(struct collision_model, nodes), model->node_count, 64);
	if (r->b.failed)
		return FALSE;
	for (i = 0; i < model->node_count; i++)
	{
		unsigned long node = nodes + 64 * (unsigned long)i;
		long child, sibling;

		snprintf((char *)region_at(r, node), 32, "%s", model->nodes[i].name);
		write16(region_at(r, node + 0x20), (unsigned short)-1);
		write16(region_at(r, node + 0x22), (unsigned short)model->nodes[i].parent);
		for (sibling = i + 1; sibling < model->node_count && model->nodes[sibling].parent != model->nodes[i].parent; sibling++)
			;
		for (child = 0; child < model->node_count && model->nodes[child].parent != i; child++)
			;
		write16(region_at(r, node + 0x24), (unsigned short)(sibling < model->node_count ? sibling : -1));
		write16(region_at(r, node + 0x26), (unsigned short)(child < model->node_count ? child : -1));
	}
	if (h3_cache_block(cache, t.address + H3C_COLL_REGIONS, 0x10, &region_count, &h3_regions))
	{
		for (i = 0; i < region_count; i++)
		{
			long permutation_count, bsp_count, b;
			unsigned long permutations, bsps;

			if (!h3_cache_block(cache, h3_regions + 0x10 * (unsigned long)i + 4, 0x28, &permutation_count, &permutations) ||
				permutation_count < 1 || !h3_cache_block(cache, permutations + 4, 0x64, &bsp_count, &bsps))
			{
				continue;
			}
			for (b = 0; b < bsp_count; b++)
			{
				unsigned long bsp = bsps + 0x64 * (unsigned long)b, node, out;
				short h3_node = h3_cache_read16(cache, bsp), n;

				n = h3_node >= 0 && h3_node < node_count ? coll_node[h3_node] : NONE;
				if (n == NONE)
				{
					left_out++;
					continue;
				}
				node = nodes + 64 * (unsigned long)n;
				if (read32(region_at(r, node + 0x34)))
				{
					left_out++;
					continue;
				}
				out = region_block(r, node + 0x34, 1, 0x60);
				if (r->b.failed)
					return FALSE;
				if (!h3_level_collision_bsp_write(r, out, cache, bsp + 4, material_count))
				{
					memset(region_at(r, node + 0x34), 0, 12);
					left_out++;
					continue;
				}
				write16(region_at(r, node + 0x20), 0);
				kept++;
			}
		}
	}
	/* its pathfinding spheres */
	field = root + offsetof(struct collision_model, pathfinding_spheres);
	memset(region_at(r, field), 0, 12);
	if (h3_cache_block(cache, t.address + H3C_COLL_SPHERES, 0x14, &sphere_count, &h3_spheres) && sphere_count > 0)
	{
		unsigned long out = region_block(r, field, sphere_count, sizeof(struct pathfinding_sphere));

		for (i = 0; i < sphere_count && !r->b.failed; i++)
		{
			unsigned char const *s = h3_cache_at(cache, h3_spheres + 0x14 * (unsigned long)i, 0x14);
			struct pathfinding_sphere *o = (struct pathfinding_sphere *)region_at(r, out + sizeof(struct pathfinding_sphere) * (unsigned long)i);
			short h3_node = s ? (short)read16(s) : NONE;

			o->node_index = h3_node >= 0 && h3_node < node_count && coll_node[h3_node] != NONE ? coll_node[h3_node] : 0;
			if (s)
			{
				memcpy(&o->center, s + 4, 12);
				memcpy(&o->radius, s + 0x10, 4);
			}
		}
	}
	platform_log("h3 characters: %s: its collision model: %ld of Halo 3's BSPs on its nodes (%ld left out), %ld materials, %ld spheres",
		species->name, kept, left_out, material_count, sphere_count);
	return !r->b.failed && kept > 0;
}

/* an actor (actr) of a character: this engine's, with Halo 3's values where they map */
H2_ALIGN static void h3c_actor_write(struct h3c_character const *c, struct actor_definition *a)
{
	struct h3_cache *cache = h3c.cache;
	float camera_standing = c->biped != NONE ? h3c.bipeds[c->biped]->camera_standing : 0.0f;
	float camera_crouching = c->biped != NONE ? h3c.bipeds[c->biped]->camera_crouching : 0.0f;
	unsigned long p = c->perception, m = c->movement, g = c->charge;
	float t;

	memset(a, 0, sizeof(*a));
	a->type = c->ce_type;
	if (c->ce_type == _actor_grunt)
		SET_FLAG(a->flags, _actor_definition_panic_on_surprise_bit, TRUE);
	SET_FLAG(a->flags, _actor_definition_avoid_friend_line_of_fire_bit, TRUE);
	SET_FLAG(a->flags, _actor_definition_crouch_in_line_of_fire_bit, TRUE);
	/* perception (Halo 3's: distance 4, central 8, maximum 0xC and peripheral
	0x10 angles, peripheral distance 0x14, hearing 0x18, the chances 0x1C and
	0x20, the surprise distance 0x28) */
	a->perception.maximum_vision_distance = h3c_real(cache, p ? p + 0x04 : 0, 30.0f);
	a->perception.central_vision_angle = h3c_real(cache, p ? p + 0x08 : 0, 0.35f);
	a->perception.maximum_vision_angle = h3c_real(cache, p ? p + 0x0C : 0, 1.4f);
	a->perception.peripheral_vision_angle = h3c_real(cache, p ? p + 0x10 : 0, 2.1f);
	a->perception.peripheral_distance = h3c_real(cache, p ? p + 0x14 : 0, 3.0f);
	a->perception.hearing_distance = h3c_real(cache, p ? p + 0x18 : 0, 15.0f);
	a->perception.notice_projectile_chance = h3c_real(cache, p ? p + 0x1C : 0, 0.5f);
	a->perception.notice_vehicle_chance = h3c_real(cache, p ? p + 0x20 : 0, 0.5f);
	a->perception.combat_awareness_time = 0.5f;
	a->perception.guard_awareness_time = 1.0f;
	a->perception.non_combat_awareness_time = 1.5f;
	a->panic.surprise_distance = h3c_real(cache, p ? p + 0x28 : 0, 2.0f);
	if (a->perception.maximum_vision_distance <= 0.0f)
		a->perception.maximum_vision_distance = 30.0f;
	t = a->perception.combat_awareness_time;
	a->perception.runtime_awareness_delta_combat = 1.0f / (t * 30.0f);
	t = a->perception.guard_awareness_time;
	a->perception.runtime_awareness_delta_guard = 1.0f / (t * 30.0f);
	t = a->perception.non_combat_awareness_time;
	a->perception.runtime_awareness_delta_non_combat = 1.0f / (t * 30.0f);
	a->perception.gun_offset_stand.k = camera_standing > 0.1f && camera_standing < 2.0f ? camera_standing : 0.62f;
	a->perception.gun_offset_crouch.k = camera_crouching > 0.05f && camera_crouching <= a->perception.gun_offset_stand.k ?
		camera_crouching : 0.6f * a->perception.gun_offset_stand.k;
	/* moving (Halo 3's movement: pathfinding radius 4, dive grenade chance 0xC) */
	a->moving.pathfinding_radius = h3c_real(cache, m ? m + 0x04 : 0, 0.25f);
	if (a->moving.pathfinding_radius <= 0.0f)
		a->moving.pathfinding_radius = 0.25f;
	a->moving.grenade_dive_chance = h3c_real(cache, m ? m + 0x0C : 0, 0.5f);
	a->moving.cover_dive_chance = 0.3f;
	a->moving.cover_emerge_chance = 0.5f;
	a->moving.stationary_moving_distance = 0.5f;
	/* looking, unopposable, panic, defence, pursuit: this engine's own values
	(h2_ai.c's fallbacks - Halo 3's blocks for them are not read) */
	a->looking.maximum_aiming_deviation.yaw = 1.05f;
	a->looking.maximum_aiming_deviation.pitch = 1.05f;
	a->looking.maximum_looking_deviation.yaw = 1.57f;
	a->looking.maximum_looking_deviation.pitch = 1.05f;
	a->looking.noncombat_looking_delta_angles[0] = a->looking.noncombat_looking_delta_angles[1] = 0.52f;
	a->looking.combat_looking_delta_angles[0] = a->looking.combat_looking_delta_angles[1] = 0.79f;
	a->looking.idle_aiming_deviation.yaw = 0.17f;
	a->looking.idle_aiming_deviation.pitch = 0.09f;
	a->looking.idle_looking_deviation.yaw = 0.52f;
	a->looking.idle_looking_deviation.pitch = 0.17f;
	a->looking.event_look_modifier_lower_bound = 1.0f;
	a->looking.event_look_modifier_upper_bound = 1.0f;
	a->looking.idle_look_noncombat.idle_facing_time_lower_bound = 4.0f;
	a->looking.idle_look_noncombat.idle_facing_time_upper_bound = 8.0f;
	a->looking.idle_look_noncombat.idle_aim_time_lower_bound = 2.0f;
	a->looking.idle_look_noncombat.idle_aim_time_upper_bound = 4.0f;
	a->looking.idle_look_noncombat.idle_look_time_lower_bound = 1.5f;
	a->looking.idle_look_noncombat.idle_look_time_upper_bound = 3.0f;
	a->looking.idle_look_guard = a->looking.idle_look_noncombat;
	a->looking.idle_look_search_combat.idle_facing_time_lower_bound = 2.0f;
	a->looking.idle_look_search_combat.idle_facing_time_upper_bound = 4.0f;
	a->looking.idle_look_search_combat.idle_aim_time_lower_bound = 1.0f;
	a->looking.idle_look_search_combat.idle_aim_time_upper_bound = 2.0f;
	a->looking.idle_look_search_combat.idle_look_time_lower_bound = 0.5f;
	a->looking.idle_look_search_combat.idle_look_time_upper_bound = 1.5f;
	a->looking.runtime_maximum_aiming_deviation_cosine.yaw = cosf(a->looking.maximum_aiming_deviation.yaw);
	a->looking.runtime_maximum_aiming_deviation_cosine.pitch = cosf(a->looking.maximum_aiming_deviation.pitch);
	a->looking.runtime_maximum_looking_deviation_cosine.yaw = cosf(a->looking.maximum_looking_deviation.yaw);
	a->looking.runtime_maximum_looking_deviation_cosine.pitch = cosf(a->looking.maximum_looking_deviation.pitch);
	a->unopposable.trigger_time_lower_bound = 2.0f;
	a->unopposable.trigger_time_upper_bound = 4.0f;
	a->unopposable.retreat_time_lower_bound = 3.0f;
	a->unopposable.retreat_time_upper_bound = 6.0f;
	a->panic.cower_time_lower_bound = 2.0f;
	a->panic.cower_time_upper_bound = 4.0f;
	a->panic.panic_chance_friend_killed = c->ce_type == _actor_grunt ? 0.25f : 0.0f;
	a->panic.panic_leader_type = c->ce_type == _actor_grunt ? _actor_elite : _actor_none;
	a->panic.panic_chance_leader_type_killed = c->ce_type == _actor_grunt ? 0.5f : 0.0f;
	a->defensive.hide_time_lower_bound = 3.0f;
	a->defensive.hide_time_upper_bound = 6.0f;
	a->defensive.hide_target_not_visible_time = 1.0f;
	a->defensive.shield_fraction_hide = -1.0f;
	a->defensive.shield_fraction_emerge_attack = 1.0f;
	a->defensive.shield_fraction_emerge_pursue = 1.0f;
	a->defensive.defensive_threshold_attacking = 4.0f;
	a->defensive.defensive_threshold_defending = 8.0f;
	a->defensive.defensive_crouch_min_stand_time = 1.0f;
	a->defensive.defensive_crouch_min_crouch_time = 1.0f;
	a->defensive.defending_hide_time_modifier = 1.0f;
	a->defensive.evasion_danger_threshold_attacking = 4.0f;
	a->defensive.evasion_danger_threshold_defending = 4.0f;
	a->defensive.evasion_delay_timer = 4.0f;
	a->defensive.evasion_seek_cover_chance = 0.5f;
	a->defensive.cover_max_distance = 10.0f;
	a->defensive.cover_damage_threshold = 0.5f;
	a->defensive.stalking_discovery_time = 1.0f;
	a->defensive.stalking_max_distance = 5.0f;
	a->defensive.stationary_facing_angle = 1.05f;
	a->defensive.change_facing_stand_time = 1.0f;
	a->pursuit.uncover_delay_time_lower_bound = 1.0f;
	a->pursuit.uncover_delay_time_upper_bound = 3.0f;
	a->pursuit.target_location_time_lower_bound = 10.0f;
	a->pursuit.target_location_time_upper_bound = 20.0f;
	a->pursuit.pursuit_position_time_lower_bound = 2.0f;
	a->pursuit.pursuit_position_time_upper_bound = 5.0f;
	a->pursuit.coordinated_pursuit_positions = 4;
	a->pursuit.uncoordinated_pursuit_positions = 4;
	/* berserk (Halo 3's charge: melee range 0xC) */
	a->berserk.melee_attack_delay_timer = 1.0f;
	a->berserk.melee_attack_range = h3c_real(cache, g ? g + 0x0C : 0, 1.0f);
	if (a->berserk.melee_attack_range <= 0.0f || a->berserk.melee_attack_range > 5.0f)
		a->berserk.melee_attack_range = 1.0f;
	a->berserk.melee_attack_charge_timer = 2.0f;
	a->firing_position.guard_position_time_lower_bound = 4.0f;
	a->firing_position.guard_position_time_upper_bound = 8.0f;
	a->firing_position.combat_position_time_lower_bound = 3.0f;
	a->firing_position.combat_position_time_upper_bound = 8.0f;
	a->firing_position.old_avoidance_radius = 2.0f;
	a->firing_position.friend_avoidance_radius = 1.0f;
	a->communication.idle_noncombat_time_lower_bound = 10.0f;
	a->communication.idle_noncombat_time_upper_bound = 30.0f;
	a->communication.idle_combat_time_lower_bound = 5.0f;
	a->communication.idle_combat_time_upper_bound = 15.0f;
}

/* whether a Halo 3 weapon fires while its trigger is held */
H2_ALIGN static boolean h3c_weapon_automatic(char const *path)
{
	return path && (strstr(path, "assault_rifle") || strstr(path, "\\smg") || strstr(path, "spike_rifle") || strstr(path, "plasma_rifle") ||
		strstr(path, "needler") || strstr(path, "sentinel") || strstr(path, "flamethrower") || strstr(path, "turret"));
}

/* an actor variant (actv) of a character and weapon (its references the caller's) */
H2_ALIGN static void h3c_variant_write(struct h3c_character const *c, long weapon, struct actor_variant_definition *v)
{
	struct h3_cache *cache = h3c.cache;
	unsigned long w = h3c_weapon_properties(cache, c, weapon, NULL);
	struct actor_ranged_combat_properties *r = &v->ranged_combat;
	char const *path = weapon != NONE ? h3_cache_tag_name(cache, weapon) : NULL;

	v->flags = 0;
	v->movement_switching.movement_type = 0;
	v->movement_switching.initial_crouch_chance = 0.0f;
	v->movement_switching.crouch_time_lower_bound = 1.0f;
	v->movement_switching.crouch_time_upper_bound = 2.0f;
	v->movement_switching.run_time_lower_bound = 2.0f;
	v->movement_switching.run_time_upper_bound = 4.0f;
	/* ranged combat (Halo 3's weapons properties: maximum firing range 0x14,
	normal combat range 0x1C, first burst delay 0x50, surprise delay 0x58 and
	fire wildly time 0x5C, death fire wildly chance 0x60 and time 0x64) */
	r->maximum_firing_range = h3c_real(cache, w ? w + 0x14 : 0, 15.0f);
	if (r->maximum_firing_range <= 0.0f)
		r->maximum_firing_range = 15.0f;
	r->combat_range_lower_bound = h3c_real(cache, w ? w + 0x1C : 0, 3.0f);
	r->combat_range_upper_bound = h3c_real(cache, w ? w + 0x20 : 0, 8.0f);
	if (r->combat_range_upper_bound <= r->combat_range_lower_bound)
		r->combat_range_upper_bound = r->combat_range_lower_bound + 2.0f;
	r->first_burst_delay_lower_bound = h3c_real(cache, w ? w + 0x50 : 0, 0.2f);
	r->first_burst_delay_upper_bound = h3c_real(cache, w ? w + 0x54 : 0, 1.0f);
	r->surprise_delay_time = h3c_real(cache, w ? w + 0x58 : 0, 0.5f);
	r->surprise_wildfire_time = h3c_real(cache, w ? w + 0x5C : 0, 0.0f);
	r->death_wildfire_chance = h3c_real(cache, w ? w + 0x60 : 0, 0.0f);
	r->death_wildfire_time = h3c_real(cache, w ? w + 0x64 : 0, 0.0f);
	/* (the firing pattern: Halo 3 keeps none in these characters - this
	engine's own bursts, the trigger held for a weapon that fires so) */
	r->rate_of_fire = h3c_weapon_automatic(path) ? 0.0f : 2.5f;
	r->target_track_fraction = 1.0f;
	r->target_lead_fraction = 1.0f;
	r->burst_geometry.burst_origin_radius = 0.5f;
	r->burst_geometry.burst_origin_angle = 0.52f;
	r->burst_geometry.burst_return_length_lower_bound = 0.5f;
	r->burst_geometry.burst_return_length_upper_bound = 1.0f;
	r->burst_geometry.burst_return_angle = 0.52f;
	r->burst_geometry.burst_duration_lower_bound = 0.6f;
	r->burst_geometry.burst_duration_upper_bound = 1.4f;
	r->burst_geometry.burst_separation_lower_bound = 0.8f;
	r->burst_geometry.burst_separation_upper_bound = 1.8f;
	r->burst_geometry.burst_maximum_angular_velocity = 6.28f;
	r->weapon_damage_modifier = 1.0f;
	r->projectile_error_angle = 0.035f;
	r->new_target_pattern.burst_duration_modifier = 1.0f;
	r->new_target_pattern.burst_separation_modifier = 1.0f;
	r->new_target_pattern.rate_of_fire_modifier = 1.0f;
	r->new_target_pattern.error_angle_modifier = 1.0f;
	r->moving_pattern = r->new_target_pattern;
	r->berserk_pattern = r->new_target_pattern;
	r->new_target_pattern_time = 1.0f;
	r->melee_range = h3c_real(cache, c->charge ? c->charge + 0x0C : 0, 1.0f);
	if (r->melee_range <= 0.0f || r->melee_range > 5.0f)
		r->melee_range = 1.0f;
	r->melee_abort_range = r->melee_range + 1.0f;
	r->berserk_firing_range_lower_bound = 0.0f;
	r->berserk_firing_range_upper_bound = 4.0f;
	r->berserk_melee_range = r->melee_range;
	r->berserk_melee_abort_range = r->melee_abort_range;
	v->items.weapon_loaded_lower_bound = 0.5f;
	v->items.weapon_loaded_upper_bound = 1.0f;
	/* its vitality: Halo 3's normal body and shield */
	v->unit.maximum_body_vitality = h3c_real(cache, c->vitality ? c->vitality + 0x04 : 0, 0.0f);
	v->unit.maximum_shield_vitality = h3c_real(cache, c->vitality ? c->vitality + 0x08 : 0, 0.0f);
	if (v->unit.maximum_body_vitality < 0.0f)
		v->unit.maximum_body_vitality = 0.0f;
	if (v->unit.maximum_shield_vitality < 0.0f)
		v->unit.maximum_shield_vitality = 0.0f;
	v->unit.forced_shader_permutation_index = NONE;
}

/* a squad's zone's firing positions nearest it, in the level: how many written */
H2_ALIGN static long h3c_positions(struct h3c_squad const *s, struct h3_sandbox_palettes const *palettes,
	struct firing_position_definition *out, long room)
{
	struct h3_cache *cache = h3c.cache;
	long zone_count, position_count, i, k, written = 0;
	unsigned long zones, positions;
	float centre[3] = { 0.0f, 0.0f, 0.0f };
	float *distances;

	long zone, zone_first, zone_last;

	if (!h3_cache_block(cache, h3c.scenario + H3C_SCNR_ZONES, H3C_ZONE_SIZE, &zone_count, &zones) || zone_count < 1)
		return 0;
	/* (a squad with no zone of its own - its objective's tasks name its areas -: every zone's positions) */
	zone_first = s->zone >= 0 && s->zone < zone_count ? s->zone : 0;
	zone_last = s->zone >= 0 && s->zone < zone_count ? s->zone : zone_count - 1;
	for (i = 0; i < s->location_count; i++)
	{
		for (k = 0; k < 3; k++)
			centre[k] += s->locations[i].position[k] / (float)s->location_count;
	}
	distances = malloc(sizeof(float) * (size_t)room);
	if (!distances)
		return 0;
	/* (the room's nearest: kept sorted, the farthest pushed out) */
	for (zone = zone_first; zone <= zone_last; zone++)
	{
	if (!h3_cache_block(cache, zones + H3C_ZONE_SIZE * (unsigned long)zone + 0x28, H3C_FIRING_POSITION_SIZE, &position_count, &positions))
		continue;
	for (i = 0; i < position_count; i++)
	{
		float position[3], d;
		long at;

		if (!h3_cache_read(cache, positions + H3C_FIRING_POSITION_SIZE * (unsigned long)i, 12, position))
			continue;
		d = (position[0] - centre[0]) * (position[0] - centre[0]) + (position[1] - centre[1]) * (position[1] - centre[1]) +
			(position[2] - centre[2]) * (position[2] - centre[2]);
		if (written == room && d >= distances[room - 1])
			continue;
		if (palettes && palettes->in_level && !palettes->in_level(palettes->context, position))
			continue;
		for (at = written < room ? written : room - 1; at > 0 && distances[at - 1] > d; at--)
		{
			distances[at] = distances[at - 1];
			out[at] = out[at - 1];
		}
		distances[at] = d;
		memset(&out[at], 0, sizeof(out[at]));
		memcpy(&out[at].position, position, 12);
		out[at].group_index = 0;
		out[at].cluster_index = NONE;
		out[at].surface_index = NONE;
		if (written < room)
			written++;
	}
	}
	free(distances);
	return written;
}

/* the characters' tags written (first: their first tag; model_first: the
sandbox's models'; weapons_first: its weapons'), the scenario's actor
palette and encounters */
H2_ALIGN static boolean h3c_write(struct build *build, struct region *r, unsigned long instances, long first, long model_first,
	long weapons_first, unsigned long scenario_root, struct h3_sandbox_palettes const *palettes)
{
	long kit_biped = h3c.kit_biped, kit_collision = h3c.kit_collision, i, positions_written = 0, unarmed = 0;
	struct h2_biped_host host;
	boolean collision_made[H3C_MAXIMUM_SPECIES];
	struct firing_position_definition *positions;
	unsigned long encounters;

	(void)build;
	if (!h3c.gathered)
		return TRUE;
	if (kit_biped == NONE || !h2w_root(r, instances, kit_biped))
	{
		platform_log("h3 characters: the kit has no multiplayer cyborg biped: no characters written");
		return TRUE;
	}
	if (kit_collision == NONE)
	{
		/* (the kit biped's own collision model) */
		unsigned long id = read32(region_at(r, h2w_root(r, instances, kit_biped) + offsetof(struct biped_definition, object.collision_model) + 12));

		kit_collision = id != 0xFFFFFFFFUL ? (long)(id & 0xFFFF) : NONE;
	}
	/* the models' nodes and markers */
	for (i = 0; i < h3c.biped_count; i++)
	{
		struct h3c_biped const *biped = h3c.bipeds[i];

		h2w_model_write(r, h2w_root(r, instances, model_first + biped->model.model), &biped->model);
	}
	/* each model's collision model and graph */
	memset(&host, 0, sizeof(host));
	host.context = r;
	host.alloc = h3c_host_alloc;
	host.at = h3c_host_at;
	host.block = h3c_host_block;
	host.data = h3c_host_data;
	host.failed = h3c_host_failed;
	host.link = r->link;
	for (i = 0; i < h3c.species_count && !r->b.failed; i++)
	{
		struct h3c_species *species = &h3c.species[i];
		unsigned long root;

		collision_made[i] = FALSE;
		if (species->first_biped == NONE || !species->graph)
			continue;
		collision_made[i] = h3c_collision_write(r, instances, first + H3C_TAG_COLLISION(i), species, kit_collision);
		if (!collision_made[i])
			platform_log("h3 characters: %s: its collision model could not be made: the kit cyborg's", species->name);
		root = h2_graph_write(species->graph, &host);
		if (!root || r->b.failed)
		{
			platform_log("h3 characters: %s: its graph could not be written", species->name);
			return FALSE;
		}
		h3c_set_root(r, instances, first + H3C_TAG_GRAPH(i), root);
	}
	/* the bipeds: the kit's multiplayer cyborg with Halo 3's model, graph,
	collision model, pill, camera heights and team */
	for (i = 0; i < h3c.biped_count && !r->b.failed; i++)
	{
		struct h3c_biped const *biped = h3c.bipeds[i];
		struct h3c_species const *species = &h3c.species[biped->species];
		unsigned long root = h2w_tag_copy(r, h2w_root(r, instances, kit_biped), sizeof(struct biped_definition), 'bipd');
		struct biped_definition *d;
		short team = H3C_TEAM_COVENANT, c, n;

		if (r->b.failed)
			break;
		h3c_set_root(r, instances, first + H3C_TAG_BIPED(i), root);
		region_reference(r, root + offsetof(struct biped_definition, object.model), 'mode', tag_id(model_first + biped->model.model));
		region_reference(r, root + offsetof(struct biped_definition, object.animation_graph), 'antr', tag_id(first + H3C_TAG_GRAPH(biped->species)));
		if (collision_made[biped->species])
			region_reference(r, root + offsetof(struct biped_definition, object.collision_model), 'coll',
				tag_id(first + H3C_TAG_COLLISION(biped->species)));
		region_reference(r, root + offsetof(struct biped_definition, object.modifier_shader), 'shdr', 0xFFFFFFFFUL);
		for (c = 0; c < h3c.character_count; c++)
		{
			if (h3c.characters[c].biped == i)
				team = h3c.characters[c].team;
		}
		d = (struct biped_definition *)region_at(r, root);
		d->unit.default_team = team;
		memset(&d->unit.initial_weapons, 0, sizeof(d->unit.initial_weapons));
		d->object.bounding_radius = build->models[biped->model.model].radius > 0.1f ? build->models[biped->model.model].radius : 1.0f;
		d->object.bounding_offset.x = d->object.bounding_offset.y = d->object.bounding_offset.z = 0.0f;
		if (biped->standing > 0.2f && biped->standing < 2.5f && biped->crouching > 0.1f && biped->crouching <= biped->standing &&
			biped->radius > 0.05f && biped->radius < 1.5f)
		{
			d->biped.collision_height_standing = biped->standing;
			d->biped.collision_height_crouching = biped->crouching;
			d->biped.collision_radius = biped->radius;
		}
		if (biped->camera_standing > 0.1f && biped->camera_standing < 2.5f && biped->camera_crouching > 0.05f &&
			biped->camera_crouching <= biped->camera_standing)
		{
			d->biped.standing_camera_height = biped->camera_standing;
			d->biped.crouching_camera_height = biped->camera_crouching;
		}
		/* (the nodes the engine keeps by index: the pelvis and the head) */
		n = h2w_model_node(&biped->model, "pelvis");
		d->biped.runtime_pelvis_node_index = n != NONE ? n : 0;
		n = h2w_model_node(&biped->model, "head");
		d->biped.runtime_head_node_index = n != NONE ? n : 0;
		(void)species;
	}
	/* the actors */
	for (i = 0; i < h3c.character_count && !r->b.failed; i++)
	{
		unsigned long root;

		/* (a character whose unit is not made has no variant, but its tag is whole) */
		root = region_alloc(r, sizeof(struct actor_definition));
		if (r->b.failed)
			break;
		h3c_actor_write(&h3c.characters[i], (struct actor_definition *)region_at(r, root));
		h3c_set_root(r, instances, first + H3C_TAG_ACTOR(i), root);
	}
	/* the actor variants */
	for (i = 0; i < h3c.variant_count && !r->b.failed; i++)
	{
		struct h3c_variant const *v = &h3c.variants[i];
		struct h3c_character const *c = &h3c.characters[v->character];
		long made = v->weapon != NONE ? h3_weapons_for(h3_cache_tag_name(h3c.cache, v->weapon)) : NONE;
		unsigned long root = region_alloc(r, sizeof(struct actor_variant_definition));

		if (r->b.failed)
			break;
		h3c_variant_write(c, v->weapon, (struct actor_variant_definition *)region_at(r, root));
		h3c_set_root(r, instances, first + H3C_TAG_VARIANT(i), root);
		region_reference(r, root + offsetof(struct actor_variant_definition, actor_reference), 'actr', tag_id(first + H3C_TAG_ACTOR(v->character)));
		region_reference(r, root + offsetof(struct actor_variant_definition, unit_reference), 'bipd', tag_id(first + H3C_TAG_BIPED(c->biped)));
		region_reference(r, root + offsetof(struct actor_variant_definition, major_upgrade_reference), 'actv', 0xFFFFFFFFUL);
		region_reference(r, root + offsetof(struct actor_variant_definition, ranged_combat.reference), 'weap',
			made != NONE ? tag_id(weapons_first + made) : 0xFFFFFFFFUL);
		region_reference(r, root + offsetof(struct actor_variant_definition, items.equipment_reference), 'eqip', 0xFFFFFFFFUL);
		if (made == NONE)
			unarmed++;
		if (getenv("HALO_H3_AI_DEBUG"))
			platform_log("h3 characters: variant %ld: %s with %s -> weapon %ld, biped %s", i, c->name,
				v->weapon != NONE ? h3_cache_tag_name(h3c.cache, v->weapon) : "(none)", made, h3c.bipeds[c->biped]->name);
	}
	/* the scenario's actor palette */
	{
		unsigned long palette = region_block(r, scenario_root + offsetof(struct scenario, ai_actor_palette), h3c.variant_count,
			sizeof(struct tag_reference));

		for (i = 0; i < h3c.variant_count && !r->b.failed; i++)
			region_reference(r, palette + sizeof(struct tag_reference) * (unsigned long)i, 'actv', tag_id(first + H3C_TAG_VARIANT(i)));
	}
	/* the encounters: a squad each, not created until the scripts place it */
	positions = malloc(sizeof(*positions) * H3C_MAXIMUM_POSITIONS);
	if (!positions)
		return FALSE;
	encounters = region_block(r, scenario_root + offsetof(struct scenario, ai_encounters), h3c.squad_count, sizeof(struct encounter_definition));
	for (i = 0; i < h3c.squad_count && !r->b.failed; i++)
	{
		struct h3c_squad const *s = &h3c.squads[i];
		unsigned long encounter = encounters + sizeof(struct encounter_definition) * (unsigned long)i, squad, locations;
		struct encounter_definition *e;
		struct squad_definition *d;
		long count = h3c_positions(s, palettes, positions, H3C_MAXIMUM_POSITIONS), k;

		if (count)
		{
			unsigned long out = region_block(r, encounter + offsetof(struct encounter_definition, firing_positions), count,
				sizeof(struct firing_position_definition));

			if (r->b.failed)
				break;
			memcpy(region_at(r, out), positions, sizeof(*positions) * (unsigned long)count);
			positions_written += count;
		}
		squad = region_block(r, encounter + offsetof(struct encounter_definition, squads), 1, sizeof(struct squad_definition));
		if (r->b.failed)
			break;
		locations = region_block(r, squad + offsetof(struct squad_definition, starting_locations), s->location_count,
			sizeof(struct actor_starting_location));
		if (r->b.failed)
			break;
		for (k = 0; k < s->location_count; k++)
		{
			struct actor_starting_location *o = (struct actor_starting_location *)region_at(r,
				locations + sizeof(struct actor_starting_location) * (unsigned long)k);

			memcpy(&o->position, s->locations[k].position, 12);
			o->facing = s->locations[k].facing;
			o->cluster_index = NONE;
			o->noncombat_sequence_id = NONE;
			o->actor_variant_index = s->locations[k].variant;
			o->command_list_index = NONE;
		}
		d = (struct squad_definition *)region_at(r, squad);
		snprintf(d->name, sizeof(d->name), "%s", s->name);
		d->actor_palette_index = s->locations[0].variant;
		d->platoon_index = NONE;
		d->initial_state = H3C_STATE_GUARDING;
		d->default_state = H3C_STATE_GUARDING;
		d->unique_leader_type = 1;
		d->maneuver_squad_index = NONE;
		d->min_count = d->max_count = s->location_count;
		d->major_upgrade = _actor_major_upgrade_none;
		for (k = 0; k < MAXIMUM_NUMBER_OF_FIRING_POSITION_GROUPS; k++)
			d->firing_position_groups[k] = 1;
		e = (struct encounter_definition *)region_at(r, encounter);
		snprintf(e->name, sizeof(e->name), "%s", s->name);
		e->team_index = s->team != NONE ? s->team : H3C_TEAM_COVENANT;
		SET_FLAG(e->flags, 0, TRUE);    /* (not initially created) */
		e->manual_structure_bsp_reference_index = NONE;
		e->runtime_structure_bsp_reference_index = NONE;
		h3_campaign_squad_encounter(s->h3_index, (short)i);
	}
	free(positions);
	platform_log("h3 characters: written: %d bipeds, %d collision models and graphs, %d actors, %d actor variants (%ld unarmed), "
		"%d encounters, %ld firing positions", (int)h3c.biped_count, (int)h3c.species_count, (int)h3c.character_count,
		(int)h3c.variant_count, unarmed, (int)h3c.squad_count, positions_written);
	return !r->b.failed;
}

#else

/* (built alone: nothing - h2_level.c compiles it) */
typedef int h3_characters_compiled_in_h2_level;

#endif
