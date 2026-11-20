/*
H2_AI.C

port: Halo 2's AI on this engine's AI (h2_ai.h says what is made of what).

What Halo 2 keeps (MCC's cache layout - 8-byte blocks and references,
4-byte string ids - read from the owner's maps against the H2EK's tag
exports' field names; private references, nothing of them kept here):
- the scenario: squad groups 0x158 (0x24: name, parent 0x20, initial
  orders 0x22), squads 0x160 (0x74: name, flags 0x20, team 0x24, parent
  group 0x26, normal and insane counts 0x2C/0x2E, major upgrade 0x30,
  vehicle 0x34, character 0x36, initial zone 0x38, weapon 0x3C, secondary
  weapon 0x3E, grenade type 0x40, initial order 0x42, vehicle variant 0x44,
  starting locations 0x48, placement script 0x50..0x70 and its index 0x70),
  zones 0x168 (0x38: name, flags 0x20 - bit 0 manual BSP -, manual BSP
  0x24, firing positions 0x28, areas 0x30), character palette 0x178 (8),
  weapon palette 0x98 (0x28), orders 0x240 (0x7C: name, style 0x20, flags
  0x24, force combat status 0x28, entry script 0x2C (its index 0x4C), follow
  squad 0x4E, follow radius 0x50, primary area set 0x54 (8: type, zone 4, area 6),
  secondary area set 0x5C, endings 0x74), triggers 0x248 (0x30);
- a starting location (0x64): name (string id), position 4, reference
  frame 0x10, facing (yaw, pitch; radians) 0x14, flags 0x1C, character
  0x20, weapon 0x22, secondary weapon 0x24, vehicle 0x28, seat 0x2A,
  grenade type 0x2C, swarm count 0x2E, actor variant 0x30, vehicle
  variant 0x34, initial movement distance 0x38, emitter vehicle 0x3C,
  initial movement mode 0x3E, placement script 0x40 (its index 0x60);
- a firing position (0x20): position, reference frame 0x0C, flags 0x0E,
  area 0x10, cluster 0x12, the sector 0x14, normal 0x18; an area (0x88):
  name, flags 0x20, its centre 0x24, radius 0x34, its firing positions'
  first 0x38 and count 0x3A;
- a character (char): flags, parent 4, unit 0x0C, style 0x1C, major
  character 0x24, then its properties' blocks from 0x2C, 8 bytes apart:
  variants, general (type at 4), vitality, placement, perception, look,
  movement, swarm, ready, engage, charge, evasion, cover, retreat, search,
  pre-search, idle, vocalization, boarding, boss, weapons (0xCC each),
  firing patterns (0x10: weapon, patterns 8 - 0x40 each), grenades (0x3C),
  vehicle ...; a block a character leaves empty is its parent's.
*/

/* ---------- headers */

#include "cseries.h"
#include "h2_ai.h"
#include "h2_biped.h"
#include "h2_cache.h"
#include "ai/actor_definitions.h"
#include "ai/actor_types.h"
#include "items/weapons.h"
#include "ai/ai_scenario_definitions.h"
#include "scenario/scenario_definitions.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef malloc
#undef free
#undef realloc
#undef calloc

/* (the Vita tier: what a level's build allocates is the builder's own
heap's, not the C heap's - h2_build_heap.h) */
#define H2_BUILD_HEAP_WRAP
#include "h2_build_heap.h"
#include "h2_dialogue.h"

#define H2_ALIGN __attribute__((force_align_arg_pointer))

void platform_log(char const *format, ...);
void h2_run_log(char const *kind, char const *format, ...);

/* ---------- constants */

/* (an actor definition made for a Halo 2 character: its charge's "berserk
whenever charge", in this engine's unused flag) */
#define H2_ACTOR_BERSERK_WHENEVER_CHARGE_BIT _actor_definition_unused_bit

enum
{
	H2S_SQUAD_GROUPS = 0x158,
	H2S_SQUADS = 0x160,
	H2S_ZONES = 0x168,
	H2S_CHARACTERS = 0x178,
	H2S_SCRIPT_STRINGS = 0x1B0,
	H2S_SYNTAX = 0x238,
	H2S_ORDERS = 0x240,
	H2S_TRIGGERS = 0x248,
	H2S_SCENES = 0x170,
	H2_SCENE_SIZE = 0x18,
	H2_SCENE_TRIGGER_SIZE = 0x0C,
	H2_SCENE_CONDITION_SIZE = 8,
	H2_SCENE_ROLE_SIZE = 0x10,
	H2S_WEAPON_PALETTE = 0x98,

	H2_SQUAD_GROUP_SIZE = 0x24,
	H2_SQUAD_SIZE = 0x74,
	H2_STARTING_LOCATION_SIZE = 0x64,
	H2_ZONE_SIZE = 0x38,
	H2_FIRING_POSITION_SIZE = 0x20,
	H2_AREA_SIZE = 0x88,
	H2_ORDER_SIZE = 0x7C,
	H2_ORDER_ENDING_SIZE = 0x14,
	H2_TRIGGER_SIZE = 0x30,
	H2_CONDITION_SIZE = 0x38,
	H2_PALETTE_SIZE = 0x28,
	H2_NODE_SIZE = 20,

	/* a character's property blocks */
	H2C_PARENT = 0x04,
	H2C_UNIT = 0x0C,
	H2C_CREATURE = 0x14,
	H2C_GENERAL = 0x34,
	H2C_VITALITY = 0x3C,
	H2C_PERCEPTION = 0x4C,
	H2C_LOOK = 0x54,
	H2C_MOVEMENT = 0x5C,
	H2C_ENGAGE = 0x74,
	H2C_CHARGE = 0x7C,
	H2C_EVASION = 0x84,
	H2C_COVER = 0x8C,
	H2C_RETREAT = 0x94,
	H2C_SEARCH = 0x9C,
	H2C_PRESEARCH = 0xA4,
	H2C_WEAPONS = 0xCC,
	H2C_FIRING_PATTERNS = 0xD4,
	H2C_GRENADES = 0xDC,

	H2C_GENERAL_SIZE = 0x0C,
	H2C_VITALITY_SIZE = 0x70,
	H2C_PERCEPTION_SIZE = 0x34,
	H2C_LOOK_SIZE = 0x50,
	H2C_MOVEMENT_SIZE = 0x24,
	H2C_ENGAGE_SIZE = 0x10,
	H2C_CHARGE_SIZE = 0x40,
	H2C_EVASION_SIZE = 0x14,
	H2C_COVER_SIZE = 0x40,
	H2C_RETREAT_SIZE = 0x4C,
	H2C_SEARCH_SIZE = 0x14,
	H2C_PRESEARCH_SIZE = 0x24,
	H2C_WEAPON_SIZE = 0xCC,
	H2C_FIRING_PATTERN_PROPERTIES_SIZE = 0x10,
	H2C_FIRING_PATTERN_SIZE = 0x40,
	H2C_GRENADES_SIZE = 0x3C,
	/* (a perception entry kept in an actor definition: vision distance,
	central, maximum and peripheral angles, peripheral distance, hearing,
	notice projectile and vehicle chances, awareness a tick in combat,
	guarding, not in combat) */
	H2_PERCEPTION_FIELDS = 11,

	/* Halo 2's actor types past this engine's: the brute, the drone */
	H2_ACTOR_BRUTE = 16,
	H2_ACTOR_BUGGER = 18,

	MAXIMUM_CHARACTERS = 64,
	MAXIMUM_VARIANTS = 64,
	MAXIMUM_ORDERS_PER_SQUAD = 26,
	MAXIMUM_CE_FIRING_POSITIONS = 512,
	MAXIMUM_CE_STARTING_LOCATIONS = 32,
	MAXIMUM_INHERITANCE = 8,
	MAXIMUM_SQUADS = 400,
	MAXIMUM_CE_ENCOUNTERS = 128,
	MAXIMUM_SQUADS_PER_ENCOUNTER_H2 = 64,
	MAXIMUM_GROUPS = 200,
	MAXIMUM_H2_ORDERS = 512,

	/* (Halo 2's engine globals ai_current_actor and ai_current_squad: their
	values here, an actor index's type with no actor's index, resolved by
	the command script whose thread is evaluating - squads_of, actors_of) */
	RT_AI_CURRENT_ACTOR = (long)0xBFFF0001,
	RT_AI_CURRENT_SQUAD = (long)0xBFFF0002,
	/* (object_get_ai: one actor - its absolute index in the low word) */
	RT_AI_ACTOR = (long)0xBFFD0000,

	/* this engine's teams */
	CE_TEAM_DEFAULT = 0,
	CE_TEAM_PLAYER = 1,
	CE_TEAM_HUMAN = 2,
	CE_TEAM_COVENANT = 3,
	CE_TEAM_FLOOD = 4,
	CE_TEAM_SENTINEL = 5,
	CE_TEAM_HERETIC = 6,
	/* (Halo 2's prophet team - the Brutes, Tartarus, the Prophets -: its own,
	CE's unused 7; 08b's ai_allegiance player covenant left Tartarus the
	Arbiter's ally as a covenant) */
	CE_TEAM_PROPHET = 7,

	/* this engine's actor states */
	CE_STATE_NONE = 0,
	CE_STATE_ALERT = 2,
	CE_STATE_GUARDING = 8,

	/* the firing position's unused word: the Halo 2 order it was given for
	(kept + 1; 0 none) */
	H2_AI_ORDER_MARK = 0x4F72,  /* 'Or' */
	H2_PRESEARCH_MARK = 0x68327073UL,  /* 'h2ps' */
	H2_SCARINESS_MARK = 0x68327363UL,  /* 'h2sc' */
	H2_DANGER_CROUCH_MARK = 0x68326463UL,  /* 'h2dc' */
	/* (a weapon's "AI scariness" in MCC's layout: after its sideways movement penalty 0x234, before its light
	power-on and power-off times and effects - the tag test's 0x328; battle rifle 4, needler 6) */
	H2_WEAPON_AI_SCARINESS = 0x238,
	RT_MAXIMUM_WEAPON_SCARINESS = 128,
};

/* ---------- structures */

struct character
{
	long tag;
	char name[128];
	short h2_type;
	short ce_type;
	short biped;          /* h2_biped.h's (NONE: the stock biped) */
	boolean flies;        /* (its unit flies: a Drone, a Sentinel - h2_biped.c) */
	short team;
	long default_weapon;  /* Halo 2's weapon tag (NONE) */
	/* each property block's element (its first: the normal style's)
	address, after inheritance (0: none) */
	unsigned long general, vitality, perception, look, movement, engage, charge, evasion, cover, retreat, search, presearch, grenades;
	/* the blocks searched by weapon: each character up the chain's */
	unsigned long weapons[MAXIMUM_INHERITANCE];
	long weapon_counts[MAXIMUM_INHERITANCE];
	unsigned long patterns[MAXIMUM_INHERITANCE];
	long pattern_counts[MAXIMUM_INHERITANCE];
	short chain_length;
	long chain[MAXIMUM_INHERITANCE];
	float camera_standing, camera_crouching;
};

struct variant
{
	short character;
	long weapon;          /* Halo 2's weapon tag (NONE) */
	short major;          /* the variant of its character's major character (NONE) */
};

struct squad
{
	char name[32];
	short parent;
	short team;
	short team_ce;        /* this engine's team it fights on */
	short normal_count, insane_count;
	short character;
	long weapon;
	short initial_order;
	short variant;
	short major_upgrade;  /* Halo 2's (normal, few, many, none, all: as this engine's) */
	long location_count;
	unsigned long locations;
	/* the orders it may be given (Halo 2's indices), each a letter */
	short orders[MAXIMUM_ORDERS_PER_SQUAD];
	short order_count;
	boolean orders_full;
};

struct group
{
	char name[32];
	short parent;
	short initial_order;
};

struct h2_ai
{
	struct h2_cache *cache;
	unsigned long scenario;
	struct character characters[MAXIMUM_CHARACTERS];
	short character_count;
	short palette_character[MAXIMUM_CHARACTERS];   /* the palette's entries' characters */
	short palette_count;
	struct variant variants[MAXIMUM_VARIANTS];
	short variant_count;
	struct squad *squads;
	short squad_count;
	struct group *groups;
	short group_count;
	long order_count;
	long weapon_palette_count;
	unsigned long weapon_palette;
	long variants_dropped;
	/* tags: actors (one a character used), then actor variants */
	short actor_tag_of[MAXIMUM_CHARACTERS];
	short actor_tag_count;
	/* each encounter's first squad */
	short squad_first[128];
};

/* ---------- bytes */

static unsigned long get32(void const *p)
{
	unsigned long v;

	memcpy(&v, p, 4);
	return v;
}

static short get16(void const *p)
{
	short v;

	memcpy(&v, p, 2);
	return v;
}

static float getf(void const *p)
{
	float v;

	memcpy(&v, p, 4);
	return v;
}

static unsigned long read32(struct h2_cache *cache, unsigned long address)
{
	unsigned char const *p = h2_cache_at(cache, address, 4);

	return p ? get32(p) : 0;
}

static short read16(struct h2_cache *cache, unsigned long address)
{
	unsigned char const *p = h2_cache_at(cache, address, 2);

	return p ? get16(p) : NONE;
}

static float readf(struct h2_cache *cache, unsigned long address)
{
	unsigned char const *p = h2_cache_at(cache, address, 4);

	return p ? getf(p) : 0.0f;
}

/* a real read, or a fallback when it is not a sane number */
static float readf_or(struct h2_cache *cache, unsigned long address, float fallback)
{
	float value;

	if (!address)
		return fallback;
	value = readf(cache, address);
	return value == value && fabsf(value) < 1.0e6f ? value : fallback;
}

/* ---------- the builder: characters */

/* a character's property block's first element up its parents (0: none);
chain: the characters (their tags) from the character up */
H2_ALIGN static unsigned long property_find(struct h2_cache *cache, long const *chain, short chain_length, unsigned long field,
	unsigned long element_size)
{
	short i;

	for (i = 0; i < chain_length; i++)
	{
		struct h2_cache_tag t;
		long count;
		unsigned long elements;

		if (h2_cache_tag_get(cache, chain[i], &t) && h2_cache_block(cache, t.address + field, element_size, &count, &elements) &&
			count > 0)
		{
			return elements;
		}
	}
	return 0;
}

/* a style's special movement (0x4C, a block of longs: its first; NONE: empty) */
H2_ALIGN static long style_special_movement(struct h2_cache *cache, struct h2_cache_tag const *style)
{
	long count = 0;
	unsigned long elements = 0;

	if (h2_cache_block(cache, style->address + 0x4C, 4, &count, &elements) && count > 0)
		return (long)h2_cache_read32(cache, elements);
	return NONE;
}

/* this engine's actor type for Halo 2's (its first 16 are this engine's) */
static short ce_actor_type(short h2_type)
{
	if (h2_type >= 0 && h2_type < NUMBER_OF_ACTOR_TYPES)
		return h2_type == _actor_player ? _actor_marine : h2_type;
	if (h2_type == H2_ACTOR_BRUTE)
		return _actor_elite;
	/* (the Drone: a Grunt's actor type, flying - character "has flying mode", h2_biped.c) */
	return _actor_grunt;
}

/* the team a character's kind fights on (a squad's own team, when it has
one, wins) */
static short team_for_type(short ce_type)
{
	switch (ce_type)
	{
	case _actor_marine:
	case _actor_crew:
	case _actor_player:
		return CE_TEAM_HUMAN;
	case _actor_combat_form:
	case _actor_infection_form:
	case _actor_carrier_form:
		return CE_TEAM_FLOOD;
	case _actor_sentinel:
	case _actor_monitor:
		return CE_TEAM_SENTINEL;
	default:
		return CE_TEAM_COVENANT;
	}
}

/* the biped h2_biped.c makes for a Halo 2 unit, by its name */
static short biped_for_unit(char const *unit, short ce_type)
{
	/* (h2_biped.c's own by name first: each of the bipeds it makes) */
	if (unit && h2_bipeds_for_unit_name(unit) != NONE)
		return h2_bipeds_for_unit_name(unit);
	if (unit)
	{
		if (strstr(unit, "\\tartarus\\"))
			return H2_BIPED_TARTARUS;
		if (strstr(unit, "\\brute\\"))
			return H2_BIPED_BRUTE;
		if (strstr(unit, "\\prophet\\") && (strstr(unit, "\\mercy\\") || strstr(unit, "\\truth\\") || strstr(unit, "\\regret") ||
			strstr(unit, "prophet_minor")))
		{
			return strstr(unit, "\\mercy\\") ? H2_BIPED_PROPHET_MERCY : strstr(unit, "\\truth\\") ? H2_BIPED_PROPHET_TRUTH :
				strstr(unit, "\\regret_holo\\") ? H2_BIPED_REGRET_HOLOGRAM : strstr(unit, "\\regret\\") ? H2_BIPED_PROPHET_REGRET :
				strstr(unit, "prophet_minor") ? H2_BIPED_PROPHET_MINOR : NONE;
		}
		if (strstr(unit, "\\elite\\"))
			return H2_BIPED_CAMPAIGN_ELITE;
		if (strstr(unit, "\\grunt\\"))
			return H2_BIPED_GRUNT;
		if (strstr(unit, "\\jackal\\"))
			return H2_BIPED_JACKAL;
		if (strstr(unit, "\\hunter\\"))
			return H2_BIPED_HUNTER;
		if (strstr(unit, "\\bugger\\"))
			return H2_BIPED_BUGGER;
		if (strstr(unit, "\\marine\\") || strstr(unit, "\\miranda\\") || strstr(unit, "\\cortana\\"))
			return H2_BIPED_MARINE;
	}
	switch (ce_type)
	{
	case _actor_marine:
	case _actor_crew:
		return H2_BIPED_MARINE;
	case _actor_grunt:
		return H2_BIPED_GRUNT;
	case _actor_jackal:
		return H2_BIPED_JACKAL;
	case _actor_hunter:
		return H2_BIPED_HUNTER;
	default:
		return H2_BIPED_CAMPAIGN_ELITE;
	}
}

H2_ALIGN short h2_ai_biped_for_unit_name(
	char const *name)
{
	if (name && h2_bipeds_for_unit_name(name) != NONE)
		return h2_bipeds_for_unit_name(name);
	if (!name || !(strstr(name, "\\elite\\") || strstr(name, "\\grunt\\") || strstr(name, "\\jackal\\") ||
		strstr(name, "\\hunter\\") || strstr(name, "\\bugger\\") || strstr(name, "\\marine\\") ||
		strstr(name, "\\miranda\\") || strstr(name, "\\cortana\\") || strstr(name, "\\brute\\") || strstr(name, "\\prophet\\mercy\\") ||
		strstr(name, "\\prophet\\truth\\") || strstr(name, "\\prophet\\regret") || strstr(name, "\\prophet_minor")))
	{
		return NONE;
	}
	return biped_for_unit(name, _actor_elite);
}

/* a character of the palette gathered (its index, NONE) */
H2_ALIGN static short character_gather(struct h2_ai *ai, long tag)
{
	struct h2_cache *cache = ai->cache;
	struct character *c;
	long chain[MAXIMUM_INHERITANCE];
	short chain_length = 0, i;
	long unit = NONE;

	if (tag == NONE)
		return NONE;
	for (i = 0; i < ai->character_count; i++)
	{
		if (ai->characters[i].tag == tag)
			return i;
	}
	if (ai->character_count >= MAXIMUM_CHARACTERS)
		return NONE;
	c = &ai->characters[ai->character_count];
	memset(c, 0, sizeof(*c));
	c->tag = tag;
	snprintf(c->name, sizeof(c->name), "%s", h2_cache_tag_name(cache, tag));
	/* the chain of parents */
	{
		long at = tag;

		while (at != NONE && chain_length < MAXIMUM_INHERITANCE)
		{
			struct h2_cache_tag t;
			short k;

			for (k = 0; k < chain_length && chain[k] != at; k++)
				;
			if (k < chain_length || !h2_cache_tag_get(cache, at, &t) || t.group_tag != 'char')
				break;
			chain[chain_length++] = at;
			/* (its unit: a biped, else a creature - Halo 2's infection
			form, the Sentinel constructor: made bipeds here, h2_biped.c) */
			if (unit == NONE)
				unit = h2_cache_reference(cache, t.address + H2C_UNIT);
			if (unit == NONE)
				unit = h2_cache_reference(cache, t.address + H2C_CREATURE);
			at = h2_cache_reference(cache, t.address + H2C_PARENT);
		}
	}
	c->chain_length = chain_length;
	memcpy(c->chain, chain, sizeof(chain));
	c->general = property_find(cache, chain, chain_length, H2C_GENERAL, H2C_GENERAL_SIZE);
	c->vitality = property_find(cache, chain, chain_length, H2C_VITALITY, H2C_VITALITY_SIZE);
	c->perception = property_find(cache, chain, chain_length, H2C_PERCEPTION, H2C_PERCEPTION_SIZE);
	c->look = property_find(cache, chain, chain_length, H2C_LOOK, H2C_LOOK_SIZE);
	c->movement = property_find(cache, chain, chain_length, H2C_MOVEMENT, H2C_MOVEMENT_SIZE);
	c->engage = property_find(cache, chain, chain_length, H2C_ENGAGE, H2C_ENGAGE_SIZE);
	c->charge = property_find(cache, chain, chain_length, H2C_CHARGE, H2C_CHARGE_SIZE);
	c->evasion = property_find(cache, chain, chain_length, H2C_EVASION, H2C_EVASION_SIZE);
	c->cover = property_find(cache, chain, chain_length, H2C_COVER, H2C_COVER_SIZE);
	c->retreat = property_find(cache, chain, chain_length, H2C_RETREAT, H2C_RETREAT_SIZE);
	c->search = property_find(cache, chain, chain_length, H2C_SEARCH, H2C_SEARCH_SIZE);
	c->presearch = property_find(cache, chain, chain_length, H2C_PRESEARCH, H2C_PRESEARCH_SIZE);
	c->grenades = property_find(cache, chain, chain_length, H2C_GRENADES, H2C_GRENADES_SIZE);
	for (i = 0; i < chain_length; i++)
	{
		struct h2_cache_tag t;

		if (!h2_cache_tag_get(cache, chain[i], &t))
			continue;
		if (!h2_cache_block(cache, t.address + H2C_WEAPONS, H2C_WEAPON_SIZE, &c->weapon_counts[i], &c->weapons[i]))
			c->weapon_counts[i] = 0;
		if (!h2_cache_block(cache, t.address + H2C_FIRING_PATTERNS, H2C_FIRING_PATTERN_PROPERTIES_SIZE, &c->pattern_counts[i],
			&c->patterns[i]))
		{
			c->pattern_counts[i] = 0;
		}
	}
	c->h2_type = c->general ? read16(cache, c->general + 4) : 7;
	c->ce_type = ce_actor_type(c->h2_type);
	c->team = team_for_type(c->ce_type);
	/* (its unit's default team, 0xC0 - Halo 2's heretics are Elites and
	Grunts of team heretic, 6, not covenant: 04a's were the Arbiter's allies,
	none to fight, its scripts waiting on their deaths) */
	if (unit != NONE)
	{
		struct h2_cache_tag unit_tag;

		if (h2_cache_tag_get(cache, unit, &unit_tag))
		{
			short team = read16(cache, unit_tag.address + 0xC0);

			if (team >= 2 && team <= 7)
				c->team = team;
		}
	}
	c->biped = biped_for_unit(unit != NONE ? h2_cache_tag_name(cache, unit) : NULL, c->ce_type);
	c->flies = h2_bipeds_tag_flies(cache, unit);
	/* its first weapon (the character's own block, else its parents') */
	c->default_weapon = NONE;
	for (i = 0; i < chain_length && c->default_weapon == NONE; i++)
	{
		if (c->weapon_counts[i] > 0)
			c->default_weapon = h2_cache_reference(cache, c->weapons[i] + 4);
	}
	/* (its unit's camera heights: the gun's height) */
	c->camera_standing = 0.62f;
	c->camera_crouching = 0.35f;
	if (unit != NONE)
	{
		struct h2_cache_tag t;

		if (h2_cache_tag_get(cache, unit, &t) && t.group_tag == 'bipd' && t.size >= 0x220)
		{
			float standing = readf(cache, t.address + 0x218), crouching = readf(cache, t.address + 0x21C);

			if (standing > 0.1f && standing < 4.0f && crouching > 0.05f && crouching <= standing)
				c->camera_standing = standing, c->camera_crouching = crouching;
		}
	}
	return ai->character_count++;
}

/* a character's weapons properties for a weapon (that weapon's entry up the
chain; 0 none: function_1e5280 has no fallback. A sniper's beam rifle has no
entry anywhere: given a plasma pistol's, it backed away from a player 5 wu off
and did not fire, the real one stands and hits in 8.4 s) */
H2_ALIGN static unsigned long character_weapon_properties(struct h2_ai const *ai, struct character const *c, long weapon)
{
	short i;
	long k;

	for (i = 0; i < c->chain_length; i++)
	{
		for (k = 0; k < c->weapon_counts[i]; k++)
		{
			if (h2_cache_reference(ai->cache, c->weapons[i] + H2C_WEAPON_SIZE * k + 4) == weapon)
				return c->weapons[i] + H2C_WEAPON_SIZE * k;
		}
	}
	return 0;
}

static long ai_pattern_count;   /* (character_firing_pattern's last: how many) */

/* a character's firing pattern for a weapon (its first pattern - the
normal one; 0 none) */
H2_ALIGN static unsigned long character_firing_pattern(struct h2_ai const *ai, struct character const *c, long weapon)
{
	short i, pass;
	long k;

	for (pass = 0; pass < 2; pass++)
	{
		for (i = 0; i < c->chain_length; i++)
		{
			for (k = 0; k < c->pattern_counts[i]; k++)
			{
				unsigned long properties = c->patterns[i] + H2C_FIRING_PATTERN_PROPERTIES_SIZE * k;
				long count;
				unsigned long patterns;

				if (pass == 0 && h2_cache_reference(ai->cache, properties) != weapon)
					continue;
				if (h2_cache_block(ai->cache, properties + 8, H2C_FIRING_PATTERN_SIZE, &count, &patterns) && (ai_pattern_count = count) > 0)
					return patterns;
			}
		}
	}
	return 0;
}

/* the actor variant of a character and weapon (made the first time;
NONE: no room) */
static short variant_for(struct h2_ai *ai, short character, long weapon)
{
	short i;

	if (character == NONE)
		return NONE;
	if (weapon == NONE)
		weapon = ai->characters[character].default_weapon;
	/* (an Enforcer's guns are its vehicle's - its tag's beam, rockets and needler -,
	this engine's unit holds one: the needler, of the "needler trigger" marker. The
	chain's first weapon was generic's battle rifle, refused, it stood unarmed) */
	if (strstr(ai->characters[character].name, "sentinel_enforcer"))
	{
		long needler = h2_cache_tag_find(ai->cache, 'weap', "objects\\characters\\sentinel_enforcer\\weapons\\needler\\sentinel_enforcer_needler");

		if (needler != NONE)
			weapon = needler;
	}
	for (i = 0; i < ai->variant_count; i++)
	{
		if (ai->variants[i].character == character && ai->variants[i].weapon == weapon)
			return i;
	}
	if (ai->variant_count >= MAXIMUM_VARIANTS)
	{
		/* (the palette is full: the character's first variant) */
		ai->variants_dropped++;
		for (i = 0; i < ai->variant_count; i++)
		{
			if (ai->variants[i].character == character)
				return i;
		}
		return NONE;
	}
	ai->variants[ai->variant_count].character = character;
	ai->variants[ai->variant_count].weapon = weapon;
	ai->variants[ai->variant_count].major = NONE;
	return ai->variant_count++;
}

/* a palette entry's weapon tag (NONE) */
static long weapon_of_palette(struct h2_ai const *ai, short index)
{
	if (index < 0 || index >= ai->weapon_palette_count)
		return NONE;
	return h2_cache_reference(ai->cache, ai->weapon_palette + H2_PALETTE_SIZE * index);
}

/* ---------- the builder: orders */

static void squad_order_add(struct squad *squad, short order, long order_count)
{
	short i;

	if (order < 0 || order >= order_count)
		return;
	for (i = 0; i < squad->order_count; i++)
	{
		if (squad->orders[i] == order)
			return;
	}
	if (squad->order_count >= MAXIMUM_ORDERS_PER_SQUAD)
	{
		squad->orders_full = TRUE;
		return;
	}
	squad->orders[squad->order_count++] = order;
}

/* whether a squad is under a group (or is the group's squad) */
static boolean squad_in_group(struct h2_ai const *ai, short squad, short group)
{
	short at = ai->squads[squad].parent, depth = 0;

	while (at != NONE && at >= 0 && at < ai->group_count && depth++ < 64)
	{
		if (at == group)
			return TRUE;
		at = ai->groups[at].parent;
	}
	return FALSE;
}

/* an order given to an ai index (Halo 2's: squad, squad group, actor's,
starting location's squad) added to the squads it names */
static void order_give(struct h2_ai *ai, unsigned long ai_index, short order)
{
	unsigned long type = ai_index >> 30;
	short s;

	if (type == 0)
	{
		if ((short)(ai_index & 0xFFFF) < ai->squad_count)
			squad_order_add(&ai->squads[ai_index & 0xFFFF], order, ai->order_count);
	}
	else if (type == 1)
	{
		for (s = 0; s < ai->squad_count; s++)
		{
			if (squad_in_group(ai, s, (short)(ai_index & 0xFFFF)))
				squad_order_add(&ai->squads[s], order, ai->order_count);
		}
	}
	else if (type == 3)
	{
		short squad = (short)((ai_index >> 16) & 0x3FFF);

		if (squad < ai->squad_count)
			squad_order_add(&ai->squads[squad], order, ai->order_count);
	}
}

/* the orders the scripts give (ai_set_orders calls of constant arguments) */
H2_ALIGN static void script_orders_gather(struct h2_ai *ai)
{
	struct h2_cache *cache = ai->cache;
	long count, i, given = 0;
	unsigned long nodes, string_size, strings;

	if (!h2_cache_block(cache, ai->scenario + H2S_SYNTAX, H2_NODE_SIZE, &count, &nodes) || count <= 0)
		return;
	string_size = read32(cache, ai->scenario + H2S_SCRIPT_STRINGS);
	strings = read32(cache, ai->scenario + H2S_SCRIPT_STRINGS + 4);
	for (i = 0; i < count; i++)
	{
		unsigned char node[H2_NODE_SIZE], name_node[H2_NODE_SIZE], a1[H2_NODE_SIZE], a2[H2_NODE_SIZE];
		unsigned char const *p = h2_cache_at(cache, nodes + H2_NODE_SIZE * i, H2_NODE_SIZE);
		unsigned long name_index, offset;
		char name[16];
		unsigned char const *text;

		if (!p)
			continue;
		memcpy(node, p, sizeof(node));
		/* (a call: not a constant, its first node the function's name) */
		if (!get16(node) || (get16(node + 6) & 1))
			continue;
		name_index = get32(node + 0x10) & 0xFFFF;
		if ((long)name_index >= count || !(p = h2_cache_at(cache, nodes + H2_NODE_SIZE * name_index, H2_NODE_SIZE)))
			continue;
		memcpy(name_node, p, sizeof(name_node));
		offset = get32(name_node + 0x0C);
		if (offset >= string_size || !(text = h2_cache_at(cache, strings + offset, 14)))
			continue;
		memcpy(name, text, 14);
		name[14] = 0;
		if (strcmp(name, "ai_set_orders"))
			continue;
		/* its arguments: an ai and an order, constants */
		{
			unsigned long next = get32(name_node + 8) & 0xFFFF;

			if (next >= (unsigned long)count || !(p = h2_cache_at(cache, nodes + H2_NODE_SIZE * next, H2_NODE_SIZE)))
				continue;
			memcpy(a1, p, sizeof(a1));
			next = get32(a1 + 8) & 0xFFFF;
			if (next >= (unsigned long)count || !(p = h2_cache_at(cache, nodes + H2_NODE_SIZE * next, H2_NODE_SIZE)))
				continue;
			memcpy(a2, p, sizeof(a2));
			if ((get16(a1 + 6) & 5) != 1 || (get16(a2 + 6) & 5) != 1)
				continue;
			order_give(ai, get32(a1 + 0x10), get16(a2 + 0x10));
			given++;
		}
	}
	platform_log("h2 ai: %ld ai_set_orders calls of constant squads and orders", given);
}

/* the orders a squad's orders' endings lead to (until there are no more) */
H2_ALIGN static void order_endings_follow(struct h2_ai *ai, struct squad *squad, unsigned long orders)
{
	short i;

	for (i = 0; i < squad->order_count; i++)
	{
		long ending_count, e;
		unsigned long endings;

		if (!h2_cache_block(ai->cache, orders + H2_ORDER_SIZE * squad->orders[i] + 0x74, H2_ORDER_ENDING_SIZE, &ending_count,
			&endings))
		{
			continue;
		}
		for (e = 0; e < ending_count; e++)
			squad_order_add(squad, read16(ai->cache, endings + H2_ORDER_ENDING_SIZE * e), ai->order_count);
	}
}

/* ---------- the builder: public code */

H2_ALIGN struct h2_ai *h2_ai_gather(
	struct h2_cache *cache)
{
	struct h2_cache_tag scenario;
	struct h2_ai *ai;
	long count, i;
	unsigned long elements, orders = 0;

	if (HALO_ENV_ONCE("HALO_H2_NO_AI") || !strstr(h2_cache_scenario_path(cache), "\\solo\\") ||
		!h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario) ||
		!h2_cache_block(cache, scenario.address + H2S_SQUADS, H2_SQUAD_SIZE, &count, &elements) || count <= 0)
	{
		return NULL;
	}
	/* (what an allocation that gets no memory was for: h2_build_heap_unit) */
	h2_build_heap_unit("the campaign AI (its squads, orders, characters and actor variants)");
	ai = calloc(1, sizeof(*ai));
	if (!ai)
		return NULL;
	ai->cache = cache;
	ai->scenario = scenario.address;
	if (!h2_cache_block(cache, scenario.address + H2S_WEAPON_PALETTE, H2_PALETTE_SIZE, &ai->weapon_palette_count,
		&ai->weapon_palette))
	{
		ai->weapon_palette_count = 0;
	}
	if (!h2_cache_block(cache, scenario.address + H2S_ORDERS, H2_ORDER_SIZE, &ai->order_count, &orders))
		ai->order_count = 0;
	if (ai->order_count > MAXIMUM_H2_ORDERS)
		ai->order_count = MAXIMUM_H2_ORDERS;
	/* the character palette */
	{
		long palette_count;
		unsigned long palette;

		if (h2_cache_block(cache, scenario.address + H2S_CHARACTERS, 8, &palette_count, &palette))
		{
			for (i = 0; i < palette_count && i < MAXIMUM_CHARACTERS; i++)
				ai->palette_character[i] = character_gather(ai, h2_cache_reference(cache, palette + 8 * i));
			ai->palette_count = (short)MIN(palette_count, MAXIMUM_CHARACTERS);
		}
	}
	/* the squad groups */
	{
		long group_count;
		unsigned long groups;

		if (h2_cache_block(cache, scenario.address + H2S_SQUAD_GROUPS, H2_SQUAD_GROUP_SIZE, &group_count, &groups) && group_count > 0)
		{
			ai->group_count = (short)MIN(group_count, MAXIMUM_GROUPS);
			ai->groups = calloc((size_t)ai->group_count, sizeof(*ai->groups));
			for (i = 0; ai->groups && i < ai->group_count; i++)
			{
				unsigned char const *p = h2_cache_at(cache, groups + H2_SQUAD_GROUP_SIZE * i, H2_SQUAD_GROUP_SIZE);

				if (!p)
					continue;
				memcpy(ai->groups[i].name, p, 31);
				ai->groups[i].parent = get16(p + 0x20);
				ai->groups[i].initial_order = get16(p + 0x22);
			}
			if (!ai->groups)
				ai->group_count = 0;
		}
	}
	/* the squads */
	ai->squad_count = (short)MIN(count, MAXIMUM_SQUADS);
	ai->squads = calloc((size_t)ai->squad_count, sizeof(*ai->squads));
	if (!ai->squads)
	{
		h2_ai_free(ai);
		return NULL;
	}
	for (i = 0; i < ai->squad_count; i++)
	{
		struct squad *s = &ai->squads[i];
		unsigned char squad[H2_SQUAD_SIZE];
		unsigned char const *p = h2_cache_at(cache, elements + H2_SQUAD_SIZE * i, H2_SQUAD_SIZE);
		short palette_index, k;

		s->character = NONE;
		s->variant = NONE;
		if (!p)
			continue;
		memcpy(squad, p, sizeof(squad));
		memcpy(s->name, squad, 31);
		s->team = get16(squad + 0x24);
		s->parent = get16(squad + 0x26);
		s->major_upgrade = get16(squad + 0x30);
		s->normal_count = get16(squad + 0x2C);
		s->insane_count = get16(squad + 0x2E);
		palette_index = get16(squad + 0x36);
		s->character = palette_index >= 0 && palette_index < ai->palette_count ? ai->palette_character[palette_index] : NONE;
		s->weapon = weapon_of_palette(ai, get16(squad + 0x3C));
		s->initial_order = get16(squad + 0x42);
		s->location_count = (long)get32(squad + 0x48);
		s->locations = get32(squad + 0x4C);
		if (s->location_count < 0 || s->location_count > 256)
			s->location_count = 0;
		/* (a squad of no character of its own: its locations' first) */
		for (k = 0; s->character == NONE && k < s->location_count; k++)
		{
			short location_palette = read16(cache, s->locations + H2_STARTING_LOCATION_SIZE * k + 0x20);

			if (location_palette >= 0 && location_palette < ai->palette_count)
				s->character = ai->palette_character[location_palette];
		}
		s->variant = variant_for(ai, s->character, s->weapon);
		s->team_ce = s->team > 0 && s->team <= 5 ? s->team : s->team == 6 ? CE_TEAM_HERETIC : s->team == 7 ? CE_TEAM_PROPHET : s->team > 6 ? CE_TEAM_COVENANT :
			s->character != NONE ? ai->characters[s->character].team : CE_TEAM_COVENANT;
		/* the orders: its own initial order, its groups' */
		squad_order_add(s, s->initial_order, ai->order_count);
		{
			short at = s->parent, depth = 0;

			while (at >= 0 && at < ai->group_count && depth++ < 64)
			{
				squad_order_add(s, ai->groups[at].initial_order, ai->order_count);
				at = ai->groups[at].parent;
			}
		}
	}
	script_orders_gather(ai);
	for (i = 0; i < ai->squad_count && orders; i++)
		order_endings_follow(ai, &ai->squads[i], orders);
	/* the starting locations' own variants */
	for (i = 0; i < ai->squad_count; i++)
	{
		struct squad *s = &ai->squads[i];
		long k;

		for (k = 0; k < s->location_count && k < MAXIMUM_CE_STARTING_LOCATIONS; k++)
		{
			unsigned long location = s->locations + H2_STARTING_LOCATION_SIZE * k;
			short location_palette = read16(cache, location + 0x20);
			short weapon_palette = read16(cache, location + 0x22);
			short character = location_palette >= 0 && location_palette < ai->palette_count ? ai->palette_character[location_palette] :
				s->character;

			if (character != s->character || weapon_palette != NONE)
				variant_for(ai, character, weapon_palette != NONE ? weapon_of_palette(ai, weapon_palette) : s->weapon);
		}
	}
	/* each variant's rank above it: its character's major character (0x24,
	halo2-upstream-work function_1e0850 walks the chain as a squad places an
	actor, by the chances of its major upgrade), with the same weapon. The
	variants so added are walked in their turn: Grunt, major, ultra */
	for (i = 0; i < ai->variant_count; i++)
	{
		struct h2_cache_tag t;

		if (h2_cache_tag_get(cache, ai->characters[ai->variants[i].character].tag, &t))
		{
			long major = h2_cache_reference(cache, t.address + 0x24);

			if (major != NONE && major != ai->characters[ai->variants[i].character].tag)
			{
				short c = character_gather(ai, major);

				if (c != NONE && c != ai->variants[i].character)
					ai->variants[i].major = variant_for(ai, c, ai->variants[i].weapon);
			}
		}
	}
	/* the actors: one a character a variant uses */
	for (i = 0; i < MAXIMUM_CHARACTERS; i++)
		ai->actor_tag_of[i] = NONE;
	for (i = 0; i < ai->variant_count; i++)
	{
		short c = ai->variants[i].character;

		if (ai->actor_tag_of[c] == NONE)
			ai->actor_tag_of[c] = ai->actor_tag_count++;
	}
	{
		long locations = 0, ordered = 0, full = 0, zero_normal = 0, insane_below = 0;

		for (i = 0; i < ai->squad_count; i++)
		{
			locations += ai->squads[i].location_count;
			ordered += ai->squads[i].order_count > 0;
			full += ai->squads[i].orders_full;
			zero_normal += ai->squads[i].normal_count <= 0 && ai->squads[i].location_count > 0;
			insane_below += ai->squads[i].insane_count < ai->squads[i].normal_count;
		}
		platform_log("h2 ai: %s: %d squads (%ld starting locations, %ld with orders, %ld with more than %d), %d groups, %ld orders, "
			"%d characters, %d actor variants (%ld more left out)", h2_cache_name(cache), ai->squad_count, locations, ordered, full,
			MAXIMUM_ORDERS_PER_SQUAD, ai->group_count, ai->order_count, ai->character_count, ai->variant_count, ai->variants_dropped);
		/* (Halo 2's counts as they are: these place no one on easy and normal
		unless the script gives a count; these have fewer on legendary) */
		platform_log("h2 ai: %s: %ld squads with locations and a normal count of 0, %ld with an insane count below the normal one",
			h2_cache_name(cache), zero_normal, insane_below);
	}
	return ai;
}

H2_ALIGN void h2_ai_free(
	struct h2_ai *ai)
{
	if (!ai)
		return;
	free(ai->squads);
	free(ai->groups);
	free(ai);
}

H2_ALIGN long h2_ai_tag_count(
	struct h2_ai const *ai)
{
	return ai ? ai->actor_tag_count + ai->variant_count : 0;
}

static char const *last_part(char const *path)
{
	char const *slash = strrchr(path, '\\');

	return slash ? slash + 1 : path;
}

H2_ALIGN boolean h2_ai_tag_describe(
	struct h2_ai const *ai,
	long index,
	unsigned long *group,
	char *name,
	long name_size)
{
	short i;

	if (!ai || index < 0 || index >= h2_ai_tag_count(ai))
		return FALSE;
	if (index < ai->actor_tag_count)
	{
		for (i = 0; i < ai->character_count && ai->actor_tag_of[i] != index; i++)
			;
		*group = 'actr';
		snprintf(name, (size_t)name_size, "h2\\ai\\%s", i < ai->character_count ? last_part(ai->characters[i].name) : "actor");
		return TRUE;
	}
	index -= ai->actor_tag_count;
	*group = 'actv';
	{
		struct variant const *v = &ai->variants[index];
		long weapon = v->weapon;

		snprintf(name, (size_t)name_size, "h2\\ai\\%s %s %ld", last_part(ai->characters[v->character].name),
			weapon != NONE ? last_part(h2_cache_tag_name(ai->cache, weapon)) : "unarmed", index);
	}
	return TRUE;
}

/* ---------- the builder: writing */

static float degrees(float d)
{
	return d * 3.14159265f / 180.0f;
}

/* an actor (actr) for a character */
H2_ALIGN static void actor_write(struct h2_ai const *ai, struct character const *c, struct actor_definition *a)
{
	struct h2_cache *cache = ai->cache;
	float t;

	memset(a, 0, sizeof(*a));
	a->type = c->ce_type;
	/* (a Grunt panics when surprised; the Marines and the Covenant's others
	keep from their friends' line of fire) */
	if (c->ce_type == _actor_grunt)
		SET_FLAG(a->flags, _actor_definition_panic_on_surprise_bit, TRUE);
	SET_FLAG(a->flags, _actor_definition_avoid_friend_line_of_fire_bit, TRUE);
	SET_FLAG(a->flags, _actor_definition_crouch_in_line_of_fire_bit, TRUE);
	if (c->ce_type == _actor_hunter)
		SET_FLAG(a->flags, _actor_definition_always_charge_bit, TRUE);
	/* (Halo 2's swarm characters - its general properties' flags, bit 0:
	the infection forms - this engine's swarm actors; a carrier's infection
	forms are made only so. Not forced to charge: Halo 2's swarms hold
	their order's areas (swarm_root / guard) and go for a target the
	swarm behaviour takes on - 04b's lab swarms stayed put, all alive, with
	the player on a ledge 13 wu off; HALO_H2_AI_SWARM_CHARGE=1 = the old
	always-charge, every form chasing it off the ledge) */
	if (c->general && (read32(cache, c->general) & 1))
	{
		SET_FLAG(a->flags, _actor_definition_swarm_actor_bit, TRUE);
		if (HALO_ENV_ONCE("HALO_H2_AI_SWARM_CHARGE"))
			SET_FLAG(a->flags, _actor_definition_always_charge_bit, TRUE);
	}
	/* (a Flood carrier walks up to its target and bursts there, as this
	engine's carrier form's suicidal melee does) */
	if (c->ce_type == _actor_carrier_form)
	{
		SET_FLAG(a->flags, _actor_definition_suicidal_melee_attack_bit, TRUE);
		SET_FLAG(a->flags, _actor_definition_always_charge_bit, TRUE);
	}
	/* (a Drone, a Sentinel, the Monitor fly: their bipeds' flying, h2_biped.c) */
	if (c->flies)
		SET_FLAG(a->flags, _actor_definition_flying_bit, TRUE);
	/* perception (Halo 2's perception properties) */
	{
		unsigned long p = c->perception;

		a->perception.maximum_vision_distance = readf_or(cache, p ? p + 0x04 : 0, 30.0f);
		a->perception.central_vision_angle = readf_or(cache, p ? p + 0x08 : 0, degrees(10.0f));
		a->perception.maximum_vision_angle = readf_or(cache, p ? p + 0x0C : 0, degrees(80.0f));
		a->perception.peripheral_vision_angle = readf_or(cache, p ? p + 0x10 : 0, degrees(120.0f));
		a->perception.peripheral_distance = readf_or(cache, p ? p + 0x14 : 0, 3.0f);
		a->perception.hearing_distance = readf_or(cache, p ? p + 0x18 : 0, 15.0f);
		a->perception.notice_projectile_chance = readf_or(cache, p ? p + 0x1C : 0, 0.5f);
		a->perception.notice_vehicle_chance = readf_or(cache, p ? p + 0x20 : 0, 0.5f);
		a->perception.combat_awareness_time = readf_or(cache, p ? p + 0x24 : 0, 0.5f);
		a->perception.guard_awareness_time = readf_or(cache, p ? p + 0x28 : 0, 1.0f);
		a->perception.non_combat_awareness_time = readf_or(cache, p ? p + 0x2C : 0, 1.5f);
		a->panic.surprise_distance = readf_or(cache, p ? p + 0x30 : 0, 2.0f);
	}
	if (a->perception.maximum_vision_distance <= 0.0f)
		a->perception.maximum_vision_distance = 30.0f;
	/* (every entry of its perception block: Halo 2 picks one by the actor's
	situation each time it looks - h2_ai_perception) */
	{
		long count = 0, k;
		unsigned long elements = 0;
		short i;

		for (i = 0; i < c->chain_length && !elements; i++)
		{
			struct h2_cache_tag t;

			if (h2_cache_tag_get(cache, c->chain[i], &t) &&
				h2_cache_block(cache, t.address + H2C_PERCEPTION, H2C_PERCEPTION_SIZE, &count, &elements) && count > 0)
			{
				break;
			}
			elements = 0;
		}
		memset(a->unused_ranged, 0, sizeof(a->unused_ranged));
		if (elements && count > 0)
		{
			float *f = (float *)&a->unused_ranged[2];

			a->unused_ranged[0] = 0x68327063UL;  /* 'h2pc' */
			a->unused_ranged[1] = (unsigned long)MIN(count, 4);
			for (k = 0; k < MIN(count, 4); k++, f += H2_PERCEPTION_FIELDS)
			{
				unsigned long e = elements + H2C_PERCEPTION_SIZE * k;
				real t;

				f[0] = readf_or(cache, e + 0x04, a->perception.maximum_vision_distance);
				if (f[0] <= 0.0f)
					f[0] = a->perception.maximum_vision_distance;
				f[1] = readf_or(cache, e + 0x08, a->perception.central_vision_angle);
				f[2] = readf_or(cache, e + 0x0C, a->perception.maximum_vision_angle);
				f[3] = readf_or(cache, e + 0x10, a->perception.peripheral_vision_angle);
				f[4] = readf_or(cache, e + 0x14, a->perception.peripheral_distance);
				f[5] = readf_or(cache, e + 0x18, a->perception.hearing_distance);
				f[6] = readf_or(cache, e + 0x1C, a->perception.notice_projectile_chance);
				f[7] = readf_or(cache, e + 0x20, a->perception.notice_vehicle_chance);
				t = readf_or(cache, e + 0x24, 0.0f);
				f[8] = t > 0.0f ? 1.0f / (t * 30.0f) : 1.0f;
				t = readf_or(cache, e + 0x28, 0.0f);
				f[9] = t > 0.0f ? 1.0f / (t * 30.0f) : 1.0f;
				t = readf_or(cache, e + 0x2C, 0.0f);
				f[10] = t > 0.0f ? 1.0f / (t * 30.0f) : 1.0f;
				if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG_WEAPONS"))
					platform_log("h2 ai: character %s perception %ld: vision %.1f, awareness times combat %.2f guard %.2f noncombat %.2f",
						c->name, k, f[0], readf_or(cache, e + 0x24, 0.0f), readf_or(cache, e + 0x28, 0.0f), readf_or(cache, e + 0x2C, 0.0f));
			}
		}
	}
	/* (the times as the engine reads them: a tick's share of awareness) */
	t = a->perception.combat_awareness_time;
	a->perception.runtime_awareness_delta_combat = t > 0.0f ? 1.0f / (t * 30.0f) : 1.0f;
	t = a->perception.guard_awareness_time;
	a->perception.runtime_awareness_delta_guard = t > 0.0f ? 1.0f / (t * 30.0f) : 1.0f;
	t = a->perception.non_combat_awareness_time;
	a->perception.runtime_awareness_delta_non_combat = t > 0.0f ? 1.0f / (t * 30.0f) : 1.0f;
	a->perception.gun_offset_stand.k = c->camera_standing;
	a->perception.gun_offset_crouch.k = c->camera_crouching;
	/* moving (Halo 2's movement and evasion) */
	a->moving.pathfinding_radius = readf_or(cache, c->movement ? c->movement + 0x04 : 0, 0.25f);
	if (a->moving.pathfinding_radius <= 0.0f)
		a->moving.pathfinding_radius = 0.25f;
	a->moving.grenade_dive_chance = readf_or(cache, c->movement ? c->movement + 0x0C : 0, 0.5f);
	a->moving.cover_dive_chance = readf_or(cache, c->evasion ? c->evasion + 0x10 : 0, 0.3f);
	a->moving.cover_emerge_chance = 0.5f;
	a->moving.stationary_moving_distance = 0.5f;
	a->moving.begin_movement_angle = 0.0f;
	a->moving.runtime_begin_movement_angle_cosine = 0.0f;
	/* looking (Halo 2's look properties) */
	{
		unsigned long l = c->look;

		a->looking.maximum_aiming_deviation.yaw = readf_or(cache, l ? l + 0x00 : 0, degrees(60.0f));
		a->looking.maximum_aiming_deviation.pitch = readf_or(cache, l ? l + 0x04 : 0, degrees(60.0f));
		a->looking.maximum_looking_deviation.yaw = readf_or(cache, l ? l + 0x08 : 0, degrees(90.0f));
		a->looking.maximum_looking_deviation.pitch = readf_or(cache, l ? l + 0x0C : 0, degrees(60.0f));
		a->looking.noncombat_looking_delta_angles[0] = readf_or(cache, l ? l + 0x20 : 0, degrees(30.0f));
		a->looking.noncombat_looking_delta_angles[1] = readf_or(cache, l ? l + 0x24 : 0, degrees(30.0f));
		a->looking.combat_looking_delta_angles[0] = readf_or(cache, l ? l + 0x28 : 0, degrees(45.0f));
		a->looking.combat_looking_delta_angles[1] = readf_or(cache, l ? l + 0x2C : 0, degrees(45.0f));
		a->looking.idle_aiming_deviation.yaw = degrees(10.0f);
		a->looking.idle_aiming_deviation.pitch = degrees(5.0f);
		a->looking.idle_looking_deviation.yaw = degrees(30.0f);
		a->looking.idle_looking_deviation.pitch = degrees(10.0f);
		a->looking.event_look_modifier_lower_bound = 1.0f;
		a->looking.event_look_modifier_upper_bound = 1.0f;
		a->looking.idle_look_noncombat.idle_facing_time_lower_bound = 4.0f;
		a->looking.idle_look_noncombat.idle_facing_time_upper_bound = 8.0f;
		a->looking.idle_look_noncombat.idle_aim_time_lower_bound = readf_or(cache, l ? l + 0x38 : 0, 2.0f);
		a->looking.idle_look_noncombat.idle_aim_time_upper_bound = readf_or(cache, l ? l + 0x3C : 0, 4.0f);
		a->looking.idle_look_noncombat.idle_look_time_lower_bound = readf_or(cache, l ? l + 0x30 : 0, 1.5f);
		a->looking.idle_look_noncombat.idle_look_time_upper_bound = readf_or(cache, l ? l + 0x34 : 0, 3.0f);
		a->looking.idle_look_guard = a->looking.idle_look_noncombat;
		a->looking.idle_look_search_combat.idle_facing_time_lower_bound = 2.0f;
		a->looking.idle_look_search_combat.idle_facing_time_upper_bound = 4.0f;
		a->looking.idle_look_search_combat.idle_aim_time_lower_bound = readf_or(cache, l ? l + 0x48 : 0, 1.0f);
		a->looking.idle_look_search_combat.idle_aim_time_upper_bound = readf_or(cache, l ? l + 0x4C : 0, 2.0f);
		a->looking.idle_look_search_combat.idle_look_time_lower_bound = readf_or(cache, l ? l + 0x40 : 0, 0.5f);
		a->looking.idle_look_search_combat.idle_look_time_upper_bound = readf_or(cache, l ? l + 0x44 : 0, 1.5f);
		a->looking.runtime_maximum_aiming_deviation_cosine.yaw = cosf(a->looking.maximum_aiming_deviation.yaw);
		a->looking.runtime_maximum_aiming_deviation_cosine.pitch = cosf(a->looking.maximum_aiming_deviation.pitch);
		a->looking.runtime_maximum_looking_deviation_cosine.yaw = cosf(a->looking.maximum_looking_deviation.yaw);
		a->looking.runtime_maximum_looking_deviation_cosine.pitch = cosf(a->looking.maximum_looking_deviation.pitch);
	}
	/* unopposable: never (Halo 2's AI has no such trigger) */
	a->unopposable.trigger_time_lower_bound = 2.0f;
	a->unopposable.trigger_time_upper_bound = 4.0f;
	a->unopposable.retreat_time_lower_bound = 3.0f;
	a->unopposable.retreat_time_upper_bound = 6.0f;
	/* panic (Halo 2's retreat: the forced cower, the dead leader and peers) */
	{
		unsigned long r = c->retreat;

		a->panic.cower_time_lower_bound = readf_or(cache, r ? r + 0x14 : 0, 2.0f);
		a->panic.cower_time_upper_bound = readf_or(cache, r ? r + 0x18 : 0, 4.0f);
		a->panic.panic_chance_friend_killed = readf_or(cache, r ? r + 0x30 : 0, 0.0f);
		/* (the actor types' leaders, retail's table at 0x471088 - unknown_25d690.cpp -:
		a Grunt's is the Elite, a Brute's the Prophet, no one else's) */
		a->panic.panic_leader_type = c->ce_type == _actor_grunt ? _actor_elite : _actor_none;
		a->panic.panic_chance_leader_type_killed = readf_or(cache, r ? r + 0x2C : 0, 0.0f);
		a->panic.panic_damage_threshold = 0.0f;
		/* (the cower timeout, which this engine's panic has no field for: its
		unused words 0 and 1 - action_flee.c's Halo 2 retreat) */
		{
			real timeout_lower = readf_or(cache, r ? r + 0x1C : 0, 5.0f), timeout_upper = readf_or(cache, r ? r + 0x20 : 0, 7.0f);

			real danger = readf_or(cache, r ? r + 0x0C : 0, 0.0f);

			memcpy(&a->panic.unused[0], &timeout_lower, sizeof(real));
			memcpy(&a->panic.unused[1], &timeout_upper, sizeof(real));
			/* (and its danger threshold: unused word 2 - actions.c's Halo 2 danger retreat) */
			memcpy(&a->panic.unused[2], &danger, sizeof(real));
			/* (and its second peer dead retreat chance: unused word 3 - h2_ai.c's peer_dead_retreat; the first is this
			engine's friend killed chance) */
			{
				real second = readf_or(cache, r ? r + 0x34 : 0, 0.0f);

				memcpy(&a->panic.unused[3], &second, sizeof(real));
			}
		}
	}
	/* defensive (Halo 2's cover and evasion) */
	{
		unsigned long v = c->cover, e = c->evasion;

		a->defensive.hide_time_lower_bound = readf_or(cache, v ? v + 0x04 : 0, 3.0f);
		a->defensive.hide_time_upper_bound = readf_or(cache, v ? v + 0x08 : 0, 6.0f);
		a->defensive.hide_target_not_visible_time = 1.0f;
		a->defensive.shield_fraction_hide = readf_or(cache, v ? v + 0x10 : 0, 0.0f);
		/* (a sniper's style has no cover behaviour, and it has no shield: this engine's
		"shield <= hide fraction", true at 0 and 0, sent it running from a player
		5 wu off where the real one stands and fires) */
		if (strstr(c->name, "jackal_sniper"))
			a->defensive.shield_fraction_hide = -1.0f;
		a->defensive.shield_fraction_emerge_attack = readf_or(cache, v ? v + 0x18 : 0, 1.0f);
		a->defensive.shield_fraction_emerge_pursue = 1.0f;
		a->defensive.defensive_threshold_attacking = readf_or(cache, v ? v + 0x1C : 0, 4.0f);
		a->defensive.defensive_threshold_defending = readf_or(cache, v ? v + 0x20 : 0, 8.0f);
		a->defensive.defensive_crouch_min_stand_time = 1.0f;
		a->defensive.defensive_crouch_min_crouch_time = 1.0f;
		a->defensive.defending_hide_time_modifier = 1.0f;
		a->defensive.evasion_danger_threshold_attacking = readf_or(cache, e ? e + 0x00 : 0, 4.0f);
		a->defensive.evasion_danger_threshold_defending = readf_or(cache, e ? e + 0x00 : 0, 4.0f);
		a->defensive.evasion_delay_timer = readf_or(cache, e ? e + 0x04 : 0, 4.0f);
		a->defensive.evasion_seek_cover_chance = readf_or(cache, e ? e + 0x08 : 0, 0.5f);
		/* (its evasion proximity threshold and its evasion chance, which this
		engine's defensive block has no field for: its unused words 0 and 1 -
		actions.c's Halo 2 evasion) */
		{
			real proximity = readf_or(cache, e ? e + 0x0C : 0, 0.0f), chance = readf_or(cache, e ? e + 0x08 : 0, 0.5f);

			memcpy(&a->defensive.unused[0], &proximity, sizeof(real));
			memcpy(&a->defensive.unused[1], &chance, sizeof(real));
		}
		/* (its engage properties' crouch and stand danger thresholds - tag test
		character_engage_properties, 0x10 a element: flags, crouch +4, stand +8,
		fight danger move +0xC - and its movement flags - 0x24 an element, flags
		+0: 1 "danger crouch allow movement", 0x40 "disallow crouch" -, which
		this engine's definition has no field for: its unused firing position
		words - h2_ai_danger_crouch. A character with no engage properties in
		its chain is never crouched by danger: the getter, 0xa05800, gives none) */
		memset(a->firing_position.unused_firing_position, 0, sizeof(a->firing_position.unused_firing_position));
		if (c->engage)
		{
			real crouch = readf_or(cache, c->engage + 0x04, 10.0f), stand = readf_or(cache, c->engage + 0x08, 10.0f);
			unsigned long movement_flags = c->movement ? h2_cache_read32(cache, c->movement) : 0;

			a->firing_position.unused_firing_position[0] = H2_DANGER_CROUCH_MARK;
			memcpy(&a->firing_position.unused_firing_position[1], &crouch, sizeof(real));
			memcpy(&a->firing_position.unused_firing_position[2], &stand, sizeof(real));
			a->firing_position.unused_firing_position[3] = movement_flags;
		}
		a->defensive.cover_max_distance = 10.0f;
		a->defensive.cover_damage_threshold = readf_or(cache, v ? v + 0x0C : 0, 0.5f);
		a->defensive.stalking_discovery_time = 1.0f;
		a->defensive.stalking_max_distance = 5.0f;
		a->defensive.stationary_facing_angle = degrees(60.0f);
		a->defensive.change_facing_stand_time = 1.0f;
	}
	/* pursuit (Halo 2's search) */
	{
		unsigned long s = c->search;

		a->pursuit.uncover_delay_time_lower_bound = 1.0f;
		a->pursuit.uncover_delay_time_upper_bound = 3.0f;
		a->pursuit.target_location_time_lower_bound = readf_or(cache, s ? s + 0x04 : 0, 10.0f);
		a->pursuit.target_location_time_upper_bound = readf_or(cache, s ? s + 0x08 : 0, 20.0f);
		a->pursuit.pursuit_position_time_lower_bound = 2.0f;
		a->pursuit.pursuit_position_time_upper_bound = 5.0f;
		a->pursuit.coordinated_pursuit_positions = 4;
		a->pursuit.uncoordinated_pursuit_positions = 4;
	}
	/* Halo 2's pre-search (character_presearch_block, the H2EK tag test's
	layout: flags, min presearch time bounds +4, max presearch time bounds
	+0xC, min certainty radius +0x14, a deprecated distance, min suppressing
	time bounds +0x1C), kept in the definition's unused words for
	h2_ai_presearch_hold */
	{
		unsigned long r = c->presearch;
		real values[5];

		values[0] = readf_or(cache, r ? r + 0x04 : 0, 0.0f);
		values[1] = readf_or(cache, r ? r + 0x08 : 0, 0.0f);
		values[2] = readf_or(cache, r ? r + 0x0C : 0, 0.0f);
		values[3] = readf_or(cache, r ? r + 0x10 : 0, 0.0f);
		values[4] = readf_or(cache, r ? r + 0x14 : 0, 0.0f);
		a->unused[0] = r ? H2_PRESEARCH_MARK : 0;
		memcpy(&a->unused[1], values, sizeof(values));
	}
	/* (its scariness - general properties 0x08 - and its retreat's scary target threshold 0x08: the
	communication block's unused words 1 and 2, h2_ai_scary_target_retreat) */
	{
		real values[2];

		values[0] = readf_or(cache, c->general ? c->general + 0x08 : 0, 0.0f);
		values[1] = readf_or(cache, c->retreat ? c->retreat + 0x08 : 0, 0.0f);
		a->communication.unused_communication[0] = H2_SCARINESS_MARK;
		if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG_WEAPONS"))
			platform_log("h2 ai: character %s: scariness %.3f, scary target threshold %.3f", c->name, values[0], values[1]);
		memcpy(&a->communication.unused_communication[1], values, sizeof(values));
	}
	/* berserk (Halo 2's charge: melee) */
	{
		unsigned long g = c->charge;

		a->berserk.melee_attack_delay_timer = readf_or(cache, g ? g + 0x18 : 0, 1.0f);
		a->berserk.melee_attack_range = readf_or(cache, g ? g + 0x0C : 0, 1.0f);
		a->berserk.melee_attack_charge_timer = readf_or(cache, g ? g + 0x14 : 0, 2.0f);
		a->berserk.melee_leap_range_lower_bound = readf_or(cache, g ? g + 0x1C : 0, 0.0f);
		a->berserk.melee_leap_range_upper_bound = readf_or(cache, g ? g + 0x20 : 0, 0.0f);
		a->berserk.melee_leap_velocity = readf_or(cache, g ? g + 0x28 : 0, 0.0f) / 30.0f;
		a->berserk.melee_leap_chance = readf_or(cache, g ? g + 0x24 : 0, 0.0f);
		a->berserk.melee_leap_ballistic = readf_or(cache, g ? g + 0x30 : 0, 0.0f);
		/* (the charge's flags, +0: bit 1 "berserk whenever charge" - kept in
		this engine's unused flag; h2_ai_berserk_update) */
		SET_FLAG(a->flags, H2_ACTOR_BERSERK_WHENEVER_CHARGE_BIT, g && (read32(cache, g) & 2));
	}
	if (a->berserk.melee_attack_range <= 0.0f)
		a->berserk.melee_attack_range = 1.0f;
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

/* an actor variant (actv) for a character and weapon (its references made
by the caller) */
H2_ALIGN static void variant_write(struct h2_ai const *ai, struct character const *c, long weapon, struct actor_variant_definition *v)
{
	struct h2_cache *cache = ai->cache;
	unsigned long w = character_weapon_properties(ai, c, weapon);
	unsigned long f = character_firing_pattern(ai, c, weapon);
	struct { boolean pair; } h2_accuracy = { FALSE };
	unsigned long g = c->grenades;
	struct actor_ranged_combat_properties *r = &v->ranged_combat;
	float rate;

	/* (the references are set apart: everything else here) */
	v->flags = 0;
	if (c->flies)
		SET_FLAG(v->flags, _actor_variant_definition_can_shoot_while_flying_bit, TRUE);
	v->movement_switching.movement_type = 0;
	v->movement_switching.initial_crouch_chance = 0.0f;
	v->movement_switching.crouch_time_lower_bound = 1.0f;
	v->movement_switching.crouch_time_upper_bound = 2.0f;
	v->movement_switching.run_time_lower_bound = 2.0f;
	v->movement_switching.run_time_upper_bound = 4.0f;
	/* ranged combat: Halo 2's weapons properties and firing pattern */
	r->maximum_firing_range = readf_or(cache, w ? w + 0x0C : 0, 15.0f);
	if (r->maximum_firing_range <= 0.0f)
		r->maximum_firing_range = 15.0f;
	r->combat_range_lower_bound = readf_or(cache, w ? w + 0x14 : 0, 3.0f);
	r->combat_range_upper_bound = readf_or(cache, w ? w + 0x18 : 0, 8.0f);
	if (r->combat_range_upper_bound <= r->combat_range_lower_bound)
		r->combat_range_upper_bound = r->combat_range_lower_bound + 2.0f;
	r->first_burst_delay_lower_bound = readf_or(cache, w ? w + 0x48 : 0, 0.0f);
	r->first_burst_delay_upper_bound = readf_or(cache, w ? w + 0x4C : 0, 0.5f);
	r->surprise_delay_time = readf_or(cache, w ? w + 0x50 : 0, 0.5f);
	r->surprise_wildfire_time = readf_or(cache, w ? w + 0x54 : 0, 0.0f);
	r->death_wildfire_chance = readf_or(cache, w ? w + 0x58 : 0, 0.0f);
	r->death_wildfire_time = readf_or(cache, w ? w + 0x5C : 0, 0.0f);
	r->gun_offset_stand.i = readf_or(cache, w ? w + 0x60 : 0, 0.0f);
	r->gun_offset_stand.j = readf_or(cache, w ? w + 0x64 : 0, 0.0f);
	r->gun_offset_stand.k = readf_or(cache, w ? w + 0x68 : 0, 0.0f);
	r->gun_offset_crouch.i = readf_or(cache, w ? w + 0x6C : 0, 0.0f);
	r->gun_offset_crouch.j = readf_or(cache, w ? w + 0x70 : 0, 0.0f);
	r->gun_offset_crouch.k = readf_or(cache, w ? w + 0x74 : 0, 0.0f);
	r->special_fire_mode = w ? read16(cache, w + 0x78) : 0;
	r->special_fire_situation = w ? read16(cache, w + 0x7A) : 0;
	r->special_fire_chance = readf_or(cache, w ? w + 0x7C : 0, 0.0f);
	r->special_fire_delay = readf_or(cache, w ? w + 0x80 : 0, 0.0f);
	r->special_damage_modifier = readf_or(cache, w ? w + 0x84 : 0, 0.0f);
	r->special_projectile_error = readf_or(cache, w ? w + 0x88 : 0, 0.0f);
	r->weapon_super_ballistic_range = readf_or(cache, w ? w + 0x34 : 0, 0.0f);
	r->weapon_bombardment_range = readf_or(cache, w ? w + 0x1C : 0, 0.0f);
	if (r->special_fire_mode < 0 || r->special_fire_mode > 2)
		r->special_fire_mode = 0;
	if (r->special_fire_situation < 0 || r->special_fire_situation > 3)
		r->special_fire_situation = 0;
	/* (Halo 2's rate of fire 0: the trigger held down through the burst -
the SMG's, the Warthog chaingun's, the battle rifle's: this engine's 0 the
same, actor_combat.c; 4 a second only with no firing pattern at all -
4 a second made 03a's Marines' SMGs fire at 3.75 rounds a second) */
	rate = readf_or(cache, f ? f + 0x00 : 0, 4.0f);
	r->rate_of_fire = rate > 0.0f ? rate : f ? 0.0f : 4.0f;
	r->target_track_fraction = readf_or(cache, f ? f + 0x04 : 0, 1.0f);
	r->target_lead_fraction = readf_or(cache, f ? f + 0x08 : 0, 1.0f);
	r->burst_geometry.burst_origin_radius = readf_or(cache, f ? f + 0x0C : 0, 0.5f);
	r->burst_geometry.burst_origin_angle = readf_or(cache, f ? f + 0x10 : 0, degrees(30.0f));
	r->burst_geometry.burst_return_length_lower_bound = readf_or(cache, f ? f + 0x14 : 0, 0.5f);
	r->burst_geometry.burst_return_length_upper_bound = readf_or(cache, f ? f + 0x18 : 0, 1.0f);
	r->burst_geometry.burst_return_angle = readf_or(cache, f ? f + 0x1C : 0, degrees(30.0f));
	r->burst_geometry.burst_duration_lower_bound = readf_or(cache, f ? f + 0x20 : 0, 1.0f);
	r->burst_geometry.burst_duration_upper_bound = readf_or(cache, f ? f + 0x24 : 0, 2.0f);
	r->burst_geometry.burst_separation_lower_bound = readf_or(cache, f ? f + 0x28 : 0, 0.5f);
	r->burst_geometry.burst_separation_upper_bound = readf_or(cache, f ? f + 0x2C : 0, 1.0f);
	r->weapon_damage_modifier = readf_or(cache, f ? f + 0x30 : 0, 1.0f);
	r->projectile_error_angle = readf_or(cache, f ? f + 0x34 : 0, degrees(2.0f));
	h2_accuracy.pair = f && w && ai_pattern_count > 1;
	r->burst_geometry.burst_maximum_angular_velocity = readf_or(cache, f ? f + 0x38 : 0, degrees(360.0f));
	if (r->weapon_damage_modifier <= 0.0f)
		r->weapon_damage_modifier = 1.0f;
	if (r->burst_geometry.burst_duration_upper_bound <= 0.0f)
	{
		r->burst_geometry.burst_duration_lower_bound = 1.0f;
		r->burst_geometry.burst_duration_upper_bound = 2.0f;
	}
	r->new_target_pattern.burst_duration_modifier = 1.0f;
	r->new_target_pattern.burst_separation_modifier = 1.0f;
	r->new_target_pattern.rate_of_fire_modifier = 1.0f;
	r->new_target_pattern.error_angle_modifier = 1.0f;
	r->moving_pattern = r->new_target_pattern;
	r->berserk_pattern = r->new_target_pattern;
	r->new_target_pattern_time = 1.0f;
	/* (a weapon's two patterns are "inaccurate" and "really accurate", its entry's
	accuracy bounds how far along it is, its accuracy time how soon it gets
	there - normal: 0 to 0.6 in 5 seconds. This engine has one pattern and a
	new target's modifiers for a time: the pattern at the upper bound, the
	modifiers the lower's. A step where Halo 2 ramps, and normal's bounds whatever
	the difficulty. With only "inaccurate" every enemy's aim was at its worst) */
	if (h2_accuracy.pair)
	{
		unsigned long f2 = f + H2C_FIRING_PATTERN_SIZE;
		real lower = readf_or(cache, w + 0x98, 0.0f), upper = readf_or(cache, w + 0x9C, 0.6f), time = readf_or(cache, w + 0xA0, 5.0f);
#define H2_ACC(offset, at) (readf_or(cache, f + (offset), 0.0f) + (at) * (readf_or(cache, f2 + (offset), 0.0f) - readf_or(cache, f + (offset), 0.0f)))
		real duration = 0.5f * (H2_ACC(0x20, upper) + H2_ACC(0x24, upper));
		real separation = 0.5f * (H2_ACC(0x28, upper) + H2_ACC(0x2C, upper));
		real error = H2_ACC(0x34, upper);

		r->target_track_fraction = H2_ACC(0x04, upper);
		r->target_lead_fraction = H2_ACC(0x08, upper);
		r->burst_geometry.burst_origin_radius = H2_ACC(0x0C, upper);
		r->burst_geometry.burst_origin_angle = H2_ACC(0x10, upper);
		r->burst_geometry.burst_return_length_lower_bound = H2_ACC(0x14, upper);
		r->burst_geometry.burst_return_length_upper_bound = H2_ACC(0x18, upper);
		r->burst_geometry.burst_return_angle = H2_ACC(0x1C, upper);
		r->burst_geometry.burst_duration_lower_bound = H2_ACC(0x20, upper);
		r->burst_geometry.burst_duration_upper_bound = H2_ACC(0x24, upper);
		r->burst_geometry.burst_separation_lower_bound = H2_ACC(0x28, upper);
		r->burst_geometry.burst_separation_upper_bound = H2_ACC(0x2C, upper);
		r->projectile_error_angle = error;
		if (rate > 0.0f && H2_ACC(0x00, upper) > 0.0f)
		{
			r->rate_of_fire = H2_ACC(0x00, upper);
			r->new_target_pattern.rate_of_fire_modifier = H2_ACC(0x00, lower) / r->rate_of_fire;
		}
		if (duration > 0.0f)
			r->new_target_pattern.burst_duration_modifier = 0.5f * (H2_ACC(0x20, lower) + H2_ACC(0x24, lower)) / duration;
		if (separation > 0.0f)
			r->new_target_pattern.burst_separation_modifier = 0.5f * (H2_ACC(0x28, lower) + H2_ACC(0x2C, lower)) / separation;
		if (error > 0.0f)
			r->new_target_pattern.error_angle_modifier = H2_ACC(0x34, lower) / error;
		if (time > 0.0f)
			r->new_target_pattern_time = time;
		if (r->burst_geometry.burst_duration_upper_bound <= 0.0f)
		{
			r->burst_geometry.burst_duration_lower_bound = 0.1f;
			r->burst_geometry.burst_duration_upper_bound = 0.1f;
		}
		if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
			platform_log("h2 ai: accuracy %s: %.2f..%.2f in %.1f s; separation %.2f, error %.2f deg, modifiers separation %.2f error %.2f", c->name, lower, upper, time,
				separation, error * 57.29578f, r->new_target_pattern.burst_separation_modifier, r->new_target_pattern.error_angle_modifier);
#undef H2_ACC
	}
	/* (Halo 2's charge properties: melee attack range, melee abort range) */
	r->melee_range = readf_or(cache, c->charge ? c->charge + 0x0C : 0, 1.0f);
	r->melee_abort_range = readf_or(cache, c->charge ? c->charge + 0x10 : 0, 2.0f);
	r->berserk_firing_range_lower_bound = 0.0f;
	r->berserk_firing_range_upper_bound = 4.0f;
	r->berserk_melee_range = r->melee_range;
	r->berserk_melee_abort_range = r->melee_abort_range;
	/* grenades (Halo 2's grenades properties; the velocity a tick's, as
	the engine reads it) */
	if (g)
	{
		struct actor_grenade_combat_properties *n = &v->grenade_combat;

		n->grenade_type = read16(cache, g + 0x04);
		n->trajectory_type = read16(cache, g + 0x06);
		/* (Halo 2 throws at a target it sees - its grenade impulse, 0x1ae680 -:
		this engine's "visible target" stimulus; 0 is "never") */
		n->stimulus_type = 1;
		n->minimum_enemy_count = read16(cache, g + 0x0A);
		n->enemy_radius = readf_or(cache, g + 0x0C, 4.0f);
		n->grenade_maximum_velocity = readf_or(cache, g + 0x14, 4.0f) / 30.0f;
		n->grenade_range_lower_bound = readf_or(cache, g + 0x18, 2.0f);
		n->grenade_range_upper_bound = readf_or(cache, g + 0x1C, 15.0f);
		n->collateral_damage_radius = readf_or(cache, g + 0x20, 0.0f);
		n->throw_grenade_chance = readf_or(cache, g + 0x24, 0.3f);
		n->throw_grenade_delay = readf_or(cache, g + 0x28, 6.0f);
		/* (Halo 2 has no squad timer between throws: each actor its own delay) */
		n->encounter_grenade_timeout = 0.0f;
		if (n->grenade_type < 0 || n->grenade_type > 1)
			n->grenade_type = 0;
		if (n->trajectory_type < 0 || n->trajectory_type > 2)
			n->trajectory_type = 0;
		v->items.grenades_lower_bound = read16(cache, g + 0x34);
		v->items.grenades_upper_bound = read16(cache, g + 0x36);
		v->items.dont_drop_grenades_chance = readf_or(cache, g + 0x38, 0.5f);
		if (v->items.grenades_lower_bound < 0 || v->items.grenades_upper_bound < v->items.grenades_lower_bound ||
			v->items.grenades_upper_bound > 8)
		{
			v->items.grenades_lower_bound = v->items.grenades_upper_bound = 0;
		}
	}
	v->items.weapon_loaded_lower_bound = readf_or(cache, w ? w + 0x8C : 0, 0.5f);
	v->items.weapon_loaded_upper_bound = readf_or(cache, w ? w + 0x90 : 0, 1.0f);
	v->items.weapon_ammo_lower_bound = w ? read16(cache, w + 0x94) : 0;
	v->items.weapon_ammo_upper_bound = w ? read16(cache, w + 0x96) : 0;
	if (v->items.weapon_ammo_lower_bound < 0 || v->items.weapon_ammo_upper_bound < v->items.weapon_ammo_lower_bound)
		v->items.weapon_ammo_lower_bound = v->items.weapon_ammo_upper_bound = 0;
	/* the unit's vitality: Halo 2's normal body and shield */
	v->unit.maximum_body_vitality = readf_or(cache, c->vitality ? c->vitality + 0x04 : 0, 0.0f);
	v->unit.maximum_shield_vitality = readf_or(cache, c->vitality ? c->vitality + 0x08 : 0, 0.0f);
	if (v->unit.maximum_body_vitality < 0.0f)
		v->unit.maximum_body_vitality = 0.0f;
	if (v->unit.maximum_shield_vitality < 0.0f)
		v->unit.maximum_shield_vitality = 0.0f;
	/* (its legendary body and shield: h2_ai_actor_vitality) */
	((float *)v->unused2)[0] = readf_or(cache, c->vitality ? c->vitality + 0x0C : 0, 0.0f);
	((float *)v->unused2)[1] = readf_or(cache, c->vitality ? c->vitality + 0x10 : 0, 0.0f);
	v->unit.forced_shader_permutation_index = NONE;
	/* (its Halo 2 character and weapon: the firing patterns at run time,
	h2_ai_fire_update) */
	v->unused[0] = 0x68326670UL;  /* 'h2fp' */
	v->unused[1] = (unsigned long)c->tag;
	v->unused[2] = (unsigned long)weapon;
}

/* the firing positions of an order's areas, appended to an encounter's
(their letter, the order's index kept): how many */
H2_ALIGN static long order_positions(struct h2_ai const *ai, short order, short letter, struct firing_position_definition *out, long room,
	short *bsp)
{
	struct h2_cache *cache = ai->cache;
	unsigned long orders, zones;
	long order_count, zone_count, area_count, a, written = 0;
	unsigned long areas;

	if (!h2_cache_block(cache, ai->scenario + H2S_ORDERS, H2_ORDER_SIZE, &order_count, &orders) || order < 0 || order >= order_count ||
		!h2_cache_block(cache, ai->scenario + H2S_ZONES, H2_ZONE_SIZE, &zone_count, &zones) ||
		!h2_cache_block(cache, orders + H2_ORDER_SIZE * order + 0x54, 8, &area_count, &areas))
	{
		return 0;
	}
	for (a = 0; a < area_count && written < room; a++)
	{
		short zone = read16(cache, areas + 8 * a + 4), area = read16(cache, areas + 8 * a + 6);
		long position_count, area_total, first, n, k;
		unsigned long positions, zone_areas, zone_address;

		if (zone < 0 || zone >= zone_count)
			continue;
		zone_address = zones + H2_ZONE_SIZE * zone;
		if (!h2_cache_block(cache, zone_address + 0x28, H2_FIRING_POSITION_SIZE, &position_count, &positions) ||
			!h2_cache_block(cache, zone_address + 0x30, H2_AREA_SIZE, &area_total, &zone_areas) || area < 0 || area >= area_total)
		{
			continue;
		}
		if (*bsp == NONE && (read32(cache, zone_address + 0x20) & 1))
			*bsp = read16(cache, zone_address + 0x24);
		first = read16(cache, zone_areas + H2_AREA_SIZE * area + 0x38);
		n = read16(cache, zone_areas + H2_AREA_SIZE * area + 0x3A);
		/* (the area's positions: those of its index; its runtime range when
		they are sorted by area, else every one of the zone's) */
		for (k = 0; k < position_count && written < room; k++)
		{
			unsigned char position[H2_FIRING_POSITION_SIZE];
			unsigned char const *p;
			struct firing_position_definition *o;

			if (first >= 0 && n > 0 && first + n <= position_count && (k < first || k >= first + n))
				continue;
			if (!(p = h2_cache_at(cache, positions + H2_FIRING_POSITION_SIZE * k, H2_FIRING_POSITION_SIZE)))
				continue;
			memcpy(position, p, sizeof(position));
			if (get16(position + 0x10) != area)
				continue;
			if (!out)
			{
				written++;
				continue;
			}
			o = &out[written++];
			memset(o, 0, sizeof(*o));
			memcpy(&o->position, position, 12);
			o->group_index = letter;
			o->cluster_index = NONE;
			o->surface_index = NONE;
			o->unresolved[0] = (byte)(H2_AI_ORDER_MARK & 0xFF);
			/* (which of the order's areas: a follow order's actors stay in
			those near what they follow) */
			o->unresolved[1] = (byte)MIN(a, 255);
			o->unresolved[2] = (byte)((order + 1) & 0xFF);
			o->unresolved[3] = (byte)((order + 1) >> 8);
		}
	}
	return written;
}

H2_ALIGN boolean h2_ai_write(
	struct h2_ai *ai,
	struct h2_biped_host const *host,
	struct h2_ai_lookup const *lookup,
	long first_tag,
	unsigned long scenario_root)
{
	void *c = host->context;
	struct firing_position_definition *positions;
	long i, made_positions = 0, squads_positioned = 0, unarmed = 0, stock_bipeds = 0;
	long kit_biped = host->kit_find(c, 'bipd', "characters\\cyborg_mp\\cyborg_mp");
	short encounter_count;

	if (!ai)
		return TRUE;
	positions = malloc(sizeof(*positions) * MAXIMUM_CE_FIRING_POSITIONS);
	if (!positions)
		return FALSE;
	/* the actors */
	for (i = 0; i < ai->character_count; i++)
	{
		unsigned long root;

		if (ai->actor_tag_of[i] == NONE)
			continue;
		root = host->alloc(c, sizeof(struct actor_definition));
		if (host->failed(c))
			break;
		actor_write(ai, &ai->characters[i], (struct actor_definition *)host->at(c, root));
		host->set_root(c, first_tag + ai->actor_tag_of[i], root);
	}
	/* the actor variants: the actor, the unit (the character's biped), the
	weapon (the level's for Halo 2's) */
	for (i = 0; i < ai->variant_count && !host->failed(c); i++)
	{
		struct variant const *v = &ai->variants[i];
		struct character const *character = &ai->characters[v->character];
		long biped = lookup->biped_tag(lookup->context, character->biped);
		long weapon = v->weapon != NONE ? lookup->weapon_tag(lookup->context, v->weapon) : NONE;
		unsigned long root = host->alloc(c, sizeof(struct actor_variant_definition));

		if (host->failed(c))
			break;
		variant_write(ai, character, v->weapon, (struct actor_variant_definition *)host->at(c, root));
		host->set_root(c, first_tag + ai->actor_tag_count + i, root);
		host->reference(c, root + offsetof(struct actor_variant_definition, actor_reference), 'actr',
			host->tag_id(c, first_tag + ai->actor_tag_of[v->character]));
		if (biped == NONE)
		{
			biped = kit_biped;
			stock_bipeds++;
		}
		host->reference(c, root + offsetof(struct actor_variant_definition, unit_reference), 'bipd',
			biped != NONE ? lookup->tag_id(lookup->context, biped) : 0xFFFFFFFFUL);
		host->reference(c, root + offsetof(struct actor_variant_definition, major_upgrade_reference), 'actv',
			v->major != NONE ? host->tag_id(c, first_tag + ai->actor_tag_count + v->major) : 0xFFFFFFFFUL);
		host->reference(c, root + offsetof(struct actor_variant_definition, ranged_combat.reference), 'weap',
			weapon != NONE ? lookup->tag_id(lookup->context, weapon) : 0xFFFFFFFFUL);
		host->reference(c, root + offsetof(struct actor_variant_definition, items.equipment_reference), 'eqip', 0xFFFFFFFFUL);
		if (weapon == NONE)
			unarmed++;
		if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
			platform_log("h2 ai: variant %ld: %s, %s -> weapon tag %ld, biped tag %ld", i, character->name,
				v->weapon != NONE ? h2_cache_tag_name(ai->cache, v->weapon) : "(none)", weapon, biped);
	}
	/* the scenario's actor palette */
	{
		unsigned long palette = host->block(c, scenario_root + offsetof(struct scenario, ai_actor_palette), ai->variant_count,
			sizeof(struct tag_reference));

		for (i = 0; i < ai->variant_count && !host->failed(c); i++)
			host->reference(c, palette + sizeof(struct tag_reference) * i, 'actv', host->tag_id(c, first_tag + ai->actor_tag_count + i));
	}
	/* the encounters: Halo 2's squads packed in this engine's encounters
	(at most 128 a map - its saved games' layout - of 64 squads, 26
	letters and 512 firing positions each): a run of squads of the same
	group and team an encounter, the squads its squads, each order of
	theirs a letter */
	{
		short *encounter_of = calloc((size_t)ai->squad_count, sizeof(short));
		short *order_letter = malloc(sizeof(short) * (size_t)(ai->order_count + 1));
		long *order_size = calloc((size_t)(ai->order_count + 1), sizeof(long));
		short pass, dropped = 0;
		unsigned long encounters;

		encounter_count = 0;
		if (!encounter_of || !order_letter || !order_size)
		{
			free(encounter_of);
			free(order_letter);
			free(order_size);
			free(positions);
			return FALSE;
		}
		for (i = 0; i < ai->order_count; i++)
		{
			short bsp = NONE;

			order_size[i] = order_positions(ai, (short)i, 0, NULL, MAXIMUM_CE_FIRING_POSITIONS, &bsp);
		}
		for (pass = 0; pass < 3; pass++)
		{
			short current = NONE, squads_in = 0, letters = 0;
			long size = 0;

			encounter_count = 0;
			dropped = 0;
			for (i = 0; i < ai->order_count; i++)
				order_letter[i] = NONE;
			for (i = 0; i < ai->squad_count; i++)
			{
				struct squad const *s = &ai->squads[i];
				short new_letters = 0, k;
				long new_size = 0;
				boolean fits;

				for (k = 0; k < s->order_count; k++)
				{
					if (current == NONE || order_letter[s->orders[k]] == NONE)
						new_letters++, new_size += order_size[s->orders[k]];
				}
				fits = current != NONE && squads_in < MAXIMUM_SQUADS_PER_ENCOUNTER_H2 &&
					letters + new_letters <= MAXIMUM_ORDERS_PER_SQUAD && size + new_size <= MAXIMUM_CE_FIRING_POSITIONS &&
					ai->squads[ai->squad_first[current]].team_ce == s->team_ce &&
					(pass > 0 || ai->squads[ai->squad_first[current]].parent == s->parent);
				if (!fits)
				{
					if (encounter_count >= MAXIMUM_CE_ENCOUNTERS)
					{
						encounter_of[i] = NONE;
						dropped++;
						continue;
					}
					current = encounter_count++;
					ai->squad_first[current] = (short)i;
					squads_in = 0;
					letters = 0;
					size = 0;
					for (k = 0; k < ai->order_count; k++)
						order_letter[k] = NONE;
					for (k = 0; k < s->order_count; k++)
					{
						if (order_letter[s->orders[k]] == NONE)
							order_letter[s->orders[k]] = letters++, size += order_size[s->orders[k]];
					}
				}
				else
				{
					for (k = 0; k < s->order_count; k++)
					{
						if (order_letter[s->orders[k]] == NONE)
							order_letter[s->orders[k]] = letters++, size += order_size[s->orders[k]];
					}
				}
				encounter_of[i] = current;
				squads_in++;
			}
			if (!dropped)
				break;
		}
		encounters = host->block(c, scenario_root + offsetof(struct scenario, ai_encounters), encounter_count,
			sizeof(struct encounter_definition));
		for (i = 0; i < encounter_count && !host->failed(c); i++)
		{
			unsigned long encounter = encounters + sizeof(struct encounter_definition) * i;
			unsigned long squad_block;
			struct encounter_definition *e;
			long position_count = 0, n, k;
			short bsp = NONE, squad_total = 0, letters = 0, first = ai->squad_first[i];

			for (k = 0; k < ai->order_count; k++)
				order_letter[k] = NONE;
			/* its squads' orders' letters and firing positions */
			for (n = first; n < ai->squad_count; n++)
			{
				struct squad const *s = &ai->squads[n];
				short o;

				if (encounter_of[n] != i)
					continue;
				squad_total++;
				for (o = 0; o < s->order_count; o++)
				{
					if (order_letter[s->orders[o]] != NONE)
						continue;
					order_letter[s->orders[o]] = letters;
					{
						long made = order_positions(ai, s->orders[o], letters, positions + position_count,
							MAXIMUM_CE_FIRING_POSITIONS - position_count, &bsp);

						if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
							platform_log("h2 ai: encounter %ld squad %s order %d letter %d: %ld positions (%ld in)", i, s->name,
								(int)s->orders[o], (int)letters, made, position_count);
						position_count += made;
					}
					letters++;
				}
			}
			if (position_count)
			{
				unsigned long out = host->block(c, encounter + offsetof(struct encounter_definition, firing_positions), position_count,
					sizeof(struct firing_position_definition));

				if (host->failed(c))
					break;
				memcpy(host->at(c, out), positions, sizeof(*positions) * (unsigned long)position_count);
				made_positions += position_count;
			}
			squad_block = host->block(c, encounter + offsetof(struct encounter_definition, squads), squad_total,
				sizeof(struct squad_definition));
			if (host->failed(c))
				break;
			squad_total = 0;
			for (n = first; n < ai->squad_count && !host->failed(c); n++)
			{
				struct squad const *s = &ai->squads[n];
				unsigned long squad = squad_block + sizeof(struct squad_definition) * squad_total, locations;
				struct squad_definition *d;
				long location_count;
				unsigned long groups = 0;
				short initial_letter;

				if (encounter_of[n] != i)
					continue;
				squad_total++;
				location_count = MIN(s->location_count, MAXIMUM_CE_STARTING_LOCATIONS);
				locations = host->block(c, squad + offsetof(struct squad_definition, starting_locations), location_count,
					sizeof(struct actor_starting_location));
				if (host->failed(c))
					break;
				for (k = 0; k < location_count; k++)
				{
					unsigned char location[H2_STARTING_LOCATION_SIZE];
					unsigned char const *p = h2_cache_at(ai->cache, s->locations + H2_STARTING_LOCATION_SIZE * k,
						H2_STARTING_LOCATION_SIZE);
					struct actor_starting_location *o = (struct actor_starting_location *)host->at(c,
						locations + sizeof(struct actor_starting_location) * k);
					short location_palette, weapon_palette, character_index;

					o->cluster_index = NONE;
					o->actor_variant_index = NONE;
					o->command_list_index = NONE;
					o->noncombat_sequence_id = NONE;
					if (!p)
						continue;
					memcpy(location, p, sizeof(location));
					memcpy(&o->position, location + 4, 12);
					o->facing = getf(location + 0x14);
					location_palette = get16(location + 0x20);
					weapon_palette = get16(location + 0x22);
					character_index = location_palette >= 0 && location_palette < ai->palette_count ?
						ai->palette_character[location_palette] : s->character;
					if (character_index != s->character || weapon_palette != NONE)
						o->actor_variant_index = variant_for(ai, character_index,
							weapon_palette != NONE ? weapon_of_palette(ai, weapon_palette) : s->weapon);
				}
				initial_letter = s->initial_order >= 0 && s->initial_order < ai->order_count ? order_letter[s->initial_order] : NONE;
				if (initial_letter == NONE && s->order_count > 0)
					initial_letter = order_letter[s->orders[0]];
				groups = initial_letter != NONE ? 1UL << initial_letter : 0x3FFFFFFUL;
				d = (struct squad_definition *)host->at(c, squad);
				snprintf(d->name, sizeof(d->name), "%s", s->name);
				d->actor_palette_index = s->variant;
				d->platoon_index = NONE;
				d->initial_state = CE_STATE_GUARDING;
				d->default_state = CE_STATE_GUARDING;
				d->unique_leader_type = 1;    /* (none: Halo 2's characters are their own) */
				d->maneuver_squad_index = NONE;
				/* (Halo 2's counts as they are: easy and normal the normal one,
				heroic their mean, legendary the insane one - a count of 0 places
				no one, whatever the squad's locations; halo2-decompiled
				unknown_2003a0.cpp 0x201520, retail's jump table at 0x201a40.
				This engine's encounter_create picks min, (min + max) / 2 and
				max the same way) */
				d->min_count = (short)MAX(s->normal_count, 0);
				d->max_count = (short)MAX(s->insane_count, 0);
				d->major_upgrade = s->major_upgrade >= 0 && s->major_upgrade < NUMBER_OF_ACTOR_MAJOR_UPGRADES ?
					s->major_upgrade : _actor_major_upgrade_normal;
				for (k = 0; k < MAXIMUM_NUMBER_OF_FIRING_POSITION_GROUPS; k++)
					d->firing_position_groups[k] = groups;
				/* (Halo 2's squad index, for the runtime's table) */
				d->unused1[0] = 'h2sq';
				d->unused1[1] = (unsigned long)n;
				if (s->order_count > 0)
					squads_positioned++;
			}
			e = (struct encounter_definition *)host->at(c, encounter);
			{
				struct squad const *s = &ai->squads[first];
				short parent = s->parent;

				snprintf(e->name, sizeof(e->name), "%s", squad_total > 1 && parent >= 0 && parent < ai->group_count ?
					ai->groups[parent].name : s->name);
				e->team_index = s->team_ce;
			}
			SET_FLAG(e->flags, 0, TRUE);   /* (not initially created: the scripts place its squads) */
			/* (a flier's positions are 3D: this engine's flying actors take no
			others - Sentinels and Drones hung where they were placed, "flying but
			placed in a non-3d-firing-position encounter") */
			if (ai->squad_count > 0 && first >= 0 && ai->squads[first].character != NONE &&
				ai->characters[ai->squads[first].character].flies)
			{
				SET_FLAG(e->flags, _encounter_3d_firing_positions_bit, TRUE);
			}
			e->version = 0;
			e->manual_structure_bsp_reference_index = bsp;
			e->runtime_structure_bsp_reference_index = bsp;
			if (bsp != NONE)
				SET_FLAG(e->flags, _encounter_manual_structure_bsp_index_bit, TRUE);
			e->unused[0] = 'h2en';
		}
		if (dropped)
			platform_log("h2 ai: %d squads left out: no room for more encounters", dropped);
		free(encounter_of);
		free(order_letter);
		free(order_size);
	}
	free(positions);
	platform_log("h2 ai: written: %d actors, %d actor variants (%ld unarmed, %ld on the stock biped), %d encounters of %d squads, "
		"%ld firing positions (%ld squads with orders)", ai->actor_tag_count, ai->variant_count, unarmed, stock_bipeds, encounter_count,
		ai->squad_count, made_positions, squads_positioned);
	return !host->failed(c);
}

/* ---------- the runtime */

/* (the runtime half sees the game's state: these after the builder's) */
#include "h2_scripts.h"
#include "h2_level.h"
#include "ai/ai.h"
#include "ai/actors.h"
#include "ai/encounters.h"
#include "ai/ai_script.h"
#include "ai/actions.h"
#include "ai/props.h"
#include "models/model_animation_definitions.h"
#include "items/weapon_definitions.h"
#include "units/unit_definitions.h"
#include "game/game.h"
#include "hs/hs.h"
#include "hs/hs_scenario_definitions.h"
#include "hs/object_lists.h"
#include "objects/objects.h"
#include "objects/damage.h"
#include "items/projectile_definitions.h"
#include "physics/collision_model_definitions.h"
#include "physics/collisions.h"
#include "scenario/scenario.h"
#include "units/units.h"
#include "cache/cache_files.h"
#include "devices/devices.h"
#include "game/players.h"
#include "game/cheats.h"

H2_ALIGN static void ram_reset(void);
H2_ALIGN static boolean actor_fighting(long actor_index);
H2_ALIGN static void deck_read(struct h2_cache *cache, unsigned long scenario);
H2_ALIGN static void zone_positions_read(struct h2_cache *cache, unsigned long scenario);
H2_ALIGN static boolean deck_link(short encounter_index, short index, struct firing_position_definition const *f);
H2_ALIGN static void deck_unlink(short encounter_index, short index);
H2_ALIGN static void deck_carry(void);
#include "h2_sound.h"
#include "units/vehicle_scripting.h"
#include "units/bipeds.h"
#include "models/model_definitions.h"
#include "objects/object_definitions.h"
#include "math/real_math.h"
#ifdef HALO_LINUX
#include "render_epoch.h"
#endif

char const *cache_files_map_directory(void);
long *hs_runtime_arguments_evaluate_all(long thread_index, short maximum, short *count, boolean initialize);
long hs_runtime_command_script_new(short script_index);
long hs_runtime_executing_thread_index(void);
boolean hs_runtime_thread_alive(long thread_index);
short hs_runtime_thread_run_if_due(long thread_index);
boolean hs_runtime_thread_due(long thread_index);
void hs_runtime_thread_end(long thread_index);
void *hs_runtime_stack_allocate(long thread_index, long size);
void hs_runtime_thread_sleep(long thread_index, long ticks);
void hs_runtime_thread_sleep_command_script(long thread_index);
boolean hs_runtime_thread_waits_on_command_script(long thread_index);
/* (ai_script.c's, not in its header) */
void ai_scripting_kill(long ai_reference);
void ai_scripting_kill_silent(long ai_reference);
short ai_scripting_living_count(long ai_reference);
short ai_scripting_swarm_count(long ai_reference);
short ai_scripting_nonswarm_count(long ai_reference);
real ai_scripting_strength(long ai_reference);
void ai_scripting_migrate(long source_ai_reference, long target_ai_reference);
void ai_scripting_magically_see_unit(long ai_reference, long unit_index);
void ai_scripting_migrate_by_unit(long unit_index, long ai_reference);
void ai_scripting_attach_unit(long unit_index, long ai_reference);
void ai_scripting_set_blind(long ai_reference, boolean blind);
void ai_scripting_set_deaf(long ai_reference, boolean deaf);
void ai_scripting_exit_vehicle(long ai_reference);
void ai_scripting_allegiance_remove(short team1_index, short team2_index);
void main_switch_structure_bsp(short structure_bsp_index);
boolean main_checkpoint_pending(void);
boolean main_checkpoint_pending_immediate(void);

enum
{
	RT_MAXIMUM_POINT_SETS = 256,
	RT_MAXIMUM_FRAME_BSPS = 16,
	RT_MAXIMUM_FRAMES = 256,
	RT_MAXIMUM_COMMAND_SCRIPTS = 48,
	RT_MAXIMUM_SCENE_ROLES = 10,
	RT_APPROACH_PLAYER = -2,
	RT_THREADS_KEPT_FREE = 6,
	RT_MAXIMUM_ACTORS = 256,
	RT_GO_TO_TIMEOUT = 30 * 30,
	CE_REFERENCE_SQUAD = 2,

	/* a command script's override of its actor's orders (all clear when it
	begins: halo2-upstream-work's function_257d80 clears each of them, so an
	actor running one does nothing by itself until the script enables it) */
	CS_MOVE = 1,
	CS_FACE = 2,
	CS_AIM = 4,
	CS_LOOK = 8,
	CS_NO_SHOOT = 16,             /* (cs_shoot false, said outright: a dropship's own gun) */
	CS_MOVING = 32,               /* (cs_enable_moving true) */
	CS_CROUCH = 64,
	CS_ABORT_ON_DAMAGE = 128,
	CS_MOVE_BY = 256,     /* (cs_fly_by, cs_go_by: the point passed, not stopped at) */
	CS_TARGETING = 512,           /* (cs_enable_targeting true: also sets looking) */
	CS_LOOKING = 1024,            /* (cs_enable_looking true) */
	CS_DIALOGUE = 2048,           /* (cs_enable_dialogue true) */
	CS_IGNORE_OBSTACLES = 4096,   /* (cs_ignore_obstacles true: a driver does not back off) */
	CS_BOOST = 8192,              /* (cs_vehicle_boost true) */
	CS_FAILSAFE = 16384,          /* (cs_enable_pathfinding_failsafe true: blocks until its path succeeds) */
	CS_SHOOT = 32768,             /* (cs_shoot true) */
	CS_SHOOT_POINT = 65536,       /* (cs_shoot true <object>, cs_shoot_point true: at the look point) */
	CS_DIALOGUE_SUPPRESSED = 131072, /* (cs_suppress_dialogue_global true) */
	CS_BEHAVIOR = 262144,         /* (cs_set_behavior: its own moves, the script waiting) */

	RT_MAXIMUM_FLYERS = 16,
};

enum
{
	RT_MAXIMUM_ENDINGS = 8,
	RT_MAXIMUM_ENDING_TRIGGERS = 8,
	RT_MAXIMUM_CONDITIONS = 8,
	RT_ORDER_TICKS = 60,          /* (0x201c80: twice the tick rate, at its tenth tick) */
	RT_ORDER_PHASE = 10,
	RT_SQUAD_STATUS_TICKS = 15,   /* (0x201c80: half the tick rate) */
};

struct rt_squad
{
	char name[32];
	short parent;
	short initial_order;
	short encounter;      /* this engine's (NONE: not made) */
	short squad;
	short location_count;
	real_point3d locations[MAXIMUM_CE_STARTING_LOCATIONS];
	unsigned long location_names[MAXIMUM_CE_STARTING_LOCATIONS];
	/* (its vehicles: each location's - this engine's vehicle tag, NONE on
	foot or one not made -, its seat (Halo 2's: 0 default, 1 passenger, 2
	gunner, 3 driver, 6 no driver, 7 no vehicle) and facing) */
	long location_vehicles[MAXIMUM_CE_STARTING_LOCATIONS];
	signed char location_seats[MAXIMUM_CE_STARTING_LOCATIONS];
	/* (a location with no character, its own or its squad's: no one is placed
	there, whatever its seat type) */
	boolean location_nobody[MAXIMUM_CE_STARTING_LOCATIONS];
	real location_yaws[MAXIMUM_CE_STARTING_LOCATIONS];
	long last_vehicle;    /* the vehicle its last placement made (gunners and passengers ride it) */
	/* (a location of a vehicle and no one - seat type 6, "no driver": 06b's
	Spectre and Ghosts the Arbiter takes) the vehicle placed there, empty */
	long location_objects[MAXIMUM_CE_STARTING_LOCATIONS];
	/* the reference frame each location is in (NONE: the world's) */
	short location_frames[MAXIMUM_CE_STARTING_LOCATIONS];
	/* (each location's placement priority: the "always place" flag 0x1C bit 3,
	100, else its seat type 0x2A - halo2-decompiled unknown_2003a0.cpp
	0x2039d0) */
	short location_priorities[MAXIMUM_CE_STARTING_LOCATIONS];
	/* (each location's swarm count 0x2E: the infection forms a swarm character's location places - 04b's lab
	infection squads, 4 locations of 5) */
	short location_swarms[MAXIMUM_CE_STARTING_LOCATIONS];
	/* (its placement scripts: each location's command script, else the
	squad's - the scenario's script index, NONE) */
	short location_scripts[MAXIMUM_CE_STARTING_LOCATIONS];
	short script;
	short placed;         /* actors placed so far (ai_spawn_count) */
	short living_seen;    /* (the log: deaths) */
	short order;          /* its order now (NONE) */
	long order_time;      /* when it was given */
	short engaged_order;  /* (the order "engaged" holds for: "suppress combat until engaged") */
	boolean engaged;
	/* (a follow order: which of its areas are active, when that was worked
	out, for which order) */
	unsigned long active_areas;
	long follow_time;
	short follow_order;
	boolean secondary;    /* (squad +0x60: its order's secondary area set is its areas) */
	boolean active_secondary; /* (the set active_areas was made from) */
	long ending_true_since[RT_MAXIMUM_ENDINGS];
	/* (Halo 2's squad +0x62/+0x64/+0x66: an order an ending chose that waits
	for its delay - the order, its countdown in ticks (less two seconds each
	pass), the ending - NONE: none; cleared whenever the squad is given an
	order. halo2-decompiled unknown_2003a0.cpp 0x202be0, 0x202720) */
	short pending_order, pending_ending;
	long pending_ticks;
	/* (ai_disposable: Halo 2's clump's +0x38 - the last time one of its actors
	saw an enemy within 25 m, NONE never - and +0x3c, a disposal pending;
	halo2-decompiled unknown_26b230.cpp 0x268c90. Halo 2 keeps these per
	clump - the actors that share their perception -; this engine has no
	clumps: the squad stands in) */
	long enemy_close_time;
	boolean dispose_pending;
	/* (Halo 2's squad +0x08: its actors - one more as one is put in it, placed
	or migrated (0x203d70), one less as a living one leaves (0x203ed0), the
	dead and the erased kept (0x1e1a00): ai_strength's and ai_living_fraction's
	share) */
	short actor_count;
};

struct rt_condition
{
	short rule;
	short squad, group, a;
	real x;
	short volume;
	unsigned long flags;
};

struct rt_trigger
{
	char name[32];
	short combination;          /* 0 OR, 1 AND */
	short condition_count;
	/* (its flags +0x20, bit 0 "latch on when true": once it held it holds
	for the rest of the level - Halo 2's bit vector g_5044cc, 0x290e20) */
	unsigned long flags;
	boolean latched;
	struct rt_condition conditions[RT_MAXIMUM_CONDITIONS];
};

struct rt_ending
{
	short next_order;
	short combination;
	short dialogue_type;  /* (0x08: none, advance, charge, fall_back, retreat, move_on, arrival, enter_vehicle, exit_vehicle, follow_player, leave_player, support) */
	real delay;
	short trigger_count;
	short triggers[RT_MAXIMUM_ENDING_TRIGGERS];
	unsigned long trigger_flags[RT_MAXIMUM_ENDING_TRIGGERS];
};

enum
{
	/* an order's flags (the H2EK's names) */
	H2_ORDER_FOLLOW_CLOSEST_PLAYER = 0x10,
	H2_ORDER_FOLLOW_SQUAD = 0x20,
	H2_ORDER_INHIBIT_VEHICLE_USE = 0x100,
	H2_ORDER_ALWAYS_ACTIVE = 0x2,
	H2_ORDER_ACTIVE_CAMO = 0x40,
	H2_ORDER_SUPPRESS_COMBAT = 0x80,
	RT_MAXIMUM_ORDER_AREAS = 32,
	RT_FOLLOW_TICKS = 60,         /* (function_203bf0's 2 seconds) */
	RT_MAXIMUM_ACTIVE_AREAS = 10, /* (function_204450) */
};

struct rt_area
{
	real_point3d centre;
	real radius;
	short type;           /* (the order's area type: 0 default, 1 search, 2 goal) */
	short zone, area;     /* (Halo 2's zone and its area: encounter_order_page_in) */
};

/* a Halo 2 zone's firing position (encounter_order_page_in): where (in its
reference frame), its area */
struct rt_firing_position
{
	real_point3d position;
	short area;
};

struct rt_order
{
	char name[32];
	short style;          /* (its style: the scenario's style palette's, NONE) */
	long special;         /* (its special movement 0x6C: the block's first long, NONE: empty) */
	unsigned long flags;
	short force_status;   /* (Force combat status: 0 none, .. 4 combat: its actors' least) */
	short follow_squad;   /* (NONE) */
	real follow_radius;
	short area_count;     /* (its primary area set's) */
	struct rt_area areas[RT_MAXIMUM_ORDER_AREAS];
	/* (its secondary area set, 0x5C, and the trigger that moves its squad to
	it, 0x64 - combination, dialogue type, triggers: halo2-decompiled
	unknown_2003a0.cpp 0x202be0; a squad's areas then the secondary ones,
	squad +0x60, until it is given an order again, 0x202e90) */
	short area2_count;
	struct rt_area areas2[RT_MAXIMUM_ORDER_AREAS];
	short secondary_combination, secondary_dialogue, secondary_trigger_count;
	short secondary_triggers[RT_MAXIMUM_ENDING_TRIGGERS];
	unsigned long secondary_trigger_flags[RT_MAXIMUM_ENDING_TRIGGERS];
	short ending_count;
	struct rt_ending endings[RT_MAXIMUM_ENDINGS];
};

struct rt_group
{
	char name[32];
	short parent;
	short initial_order;
};

struct rt_point_set
{
	long count;
	real_point3d *points;
	short *frames;        /* each point's reference frame (NONE: the world's) */
};

/* a Halo 2 reference frame: a node of a moving machine (an elevator, the
tram) that starting locations and script points are placed relative to.
The structure BSP's pathfinding data lists the "mobile" objects (its
object refs, flags bit 0), each with its nodes' frame indices; the mobile
objects are the scenario's machines whose tag's pathfinding policy is
"sectors", in placement order (H2EK: 01a's hinge, tram car, elevator) */
struct rt_frame
{
	char tag[96];             /* the machine's tag (Halo 2's name) */
	real_point3d position;    /* where it is placed */
	short node;
};

/* a structure BSP's Halo 2 pathfinding mesh (its pathfinding data, 0xC4:
sectors 0 - 8 bytes: flags, hint, first link -, links 8 - 0x10: vertices,
flags, hint, forward and reverse links, left and right sectors -, vertices
0x28 - a point each): each sector a polygon, its links walked from its
first (forward where it is the left sector, else reverse) */
struct rt_nav_link
{
	unsigned short vertices[2];
	unsigned short flags;
	short hint;
	unsigned short forward, reverse;
	unsigned short left, right;
};

/* (a pathfinding hint a route may take between sectors no link joins, 0x14 each
at pathfinding data 0x38: type, next, 8 shorts. Jump link (1, a wall jump 6):
vertices 0 1 the top edge, 2 3 the bottom one, byte 0x0C its flags, 0x0D the
jump heights it takes (bit 0 down, 1 step, 2 crouch, 3 stand, 4 storey, 5 tower),
0x0E the sector it lands in. Hoist link (5): 0 the sector on top, vertices 1 the
bottom, 2 the top, 3 the facing; byte 0x0C its height, bit 0 step, 1 crouch, 2
stand. Halo 2 (path.cpp 0x272020) takes a hint from the sector that owns it
only: a jump goes down, a hoist up) */
enum { RT_HOP_JUMP = 1, RT_HOP_HOIST = 5, RT_HOP_WALL_JUMP = 6 };

struct rt_nav_hop
{
	long from, to, next;
	real_point3d a, e;
	short type;
	unsigned char hint_type;  /* (the hint's: 1 jump, 5 hoist, 6 wall jump) */
	unsigned char flags;      /* (byte 0x0C: a hoist's height bits) */
	unsigned char heights;    /* (byte 0x0D: a jump's height bits) */
};

/* what an actor's route may take (Halo 2's path settings, unknown_1f9240.cpp
0x1f9240): the hint types (bit k: type k; bit 0 the sectors' own links), the
jump heights (a mask; any: its jump height is infinite) and its movement hints
(0x400 hoist step, 0x800 hoist crouch, 0x1000 hoist stand) */
struct rt_path_settings
{
	unsigned long hint_types;
	unsigned long jump_heights;
	boolean jump_any;
	unsigned long movement_hints;
	real radius;              /* (its pathfinding radius: movement properties 0x04) */
};

struct rt_nav
{
	long sector_count, link_count, vertex_count;
	long hop_count;
	long *first_hop;
	struct rt_nav_hop *hops;
	boolean use_hops;         /* (a route on foot: not a driver's) */
	boolean filter_hops;      /* (and only the hops its actor's settings allow) */
	boolean foot_rules_old;   /* (HALO_H2_FOOT_RULES_OLD=1: a route on foot crosses no link narrower than 1 wu, as before) */
	struct rt_path_settings settings;
	long search_limit;        /* (sectors a search may take, then the nearest reached; 0: all) */
	unsigned char kinds[64];  /* (the last route's: a hop's type at its take-off) */
	long *first_link;
	unsigned short *sector_flags; /* (bit 0: a sector a route may enter - path.cpp 0x271630) */
	struct rt_nav_link *links;
	real_point3d *vertices;
	float *bounds;            /* each sector's x0 y0 x1 y1 z0 z1 */
	/* (perf/grunt-tick-2) the sectors by a grid over x, y: each cell's sectors
	whose box meets it, ascending (nav_locate); NULL: none made */
	long grid_w, grid_h;
	float grid_x0, grid_y0, grid_inverse;
	long *grid_first;         /* (grid_w * grid_h + 1: each cell's first item) */
	unsigned short *grid_items;
	/* (the search's, reused) */
	float *cost;
	long *parent;             /* the link it was entered by */
	long *heap;
	unsigned char *state;
};

enum
{
	RT_ROUTE_POINTS = 48,
};

struct rt_route
{
	real_point3d target;
	real_point3d points[RT_ROUTE_POINTS];
	unsigned char kinds[64];
	short count, index;
	long time;
	boolean failed;
};

static void nav_free(struct rt_nav *n);
H2_ALIGN static void nav_read(struct h2_cache *cache);
static short nav_path(struct rt_nav *n, real_point3d const *from, real_point3d const *to, real width, real_point3d *out, short room);
static long nav_locate(struct rt_nav const *n, real_point3d const *point);
static void nav_grid_make(struct rt_nav *n);
H2_ALIGN static void flood_read(struct h2_cache *cache);
H2_ALIGN static void flood_feign_read(struct h2_cache *cache);
static void flood_ready(void);
static void foot_route_search(long actor_index, struct rt_nav *n, struct rt_route *r, real_point3d const *from);
static boolean flier_line_blocked(real_point3d const *from, real_vector3d const *line, long ignore);

/* a blocking cs_ function's action (command_script_action_evaluate): the
function, its arguments, the tick it began (NONE: not begun) and a point it
works out at the start */
struct rt_cs_action
{
	short function_index;
	short count;
	long arguments[4];
	long started;
	real_point3d point;
};

struct rt_command_script;
/* an action's step, each tick until it is done: Halo 2's per-type arms of
halo2-decompiled unknown_257d00.cpp 0x258cf0 / 0x259430 (CS_STEP_CONTINUE:
it goes on, CS_STEP_DONE: the thread goes on) */
typedef short (*rt_cs_action_step)(struct rt_command_script *c, struct rt_cs_action *action);
enum { CS_STEP_CONTINUE = 0, CS_STEP_DONE = 2 };

struct rt_command_script
{
	long thread_index;    /* NONE: waiting for a thread */
	long actor_index;
	short script;
	unsigned long flags;
	long atom_time;       /* the tick a blocking cs_ function last waited */
	boolean grenade_due, grenade_now; /* (cs_grenade) */
	real_point3d move_point;
	long move_surface;
	real_point3d look_point;
	long look_object;     /* (cs_face_object...: NONE) */
	short abort_status;   /* (cs_abort_on_combat_status: NONE) */
	real body_vitality;
	real speed;           /* (cs_vehicle_speed: its vehicle's top speed's fraction) */
	short queued;         /* (cs_queue_command_script: the script run after it, NONE) */
	short force_status;   /* (cs_force_combat_status: 0 none, 1 asleep, 2 idle, 3 alert, 4 active) */
	short movement_mode;  /* (cs_movement_mode: NONE, or ai_movement_patrol 0 .. berserk 6) */
	long approach_object; /* (cs_start_approach: NONE; RT_APPROACH_PLAYER: the nearest player) */
	real approach_distance, approach_maximum;
	short scene;          /* (ai_scene: rt.scene_table's, its joint command script; NONE) */
	short role_count;
	long roles[RT_MAXIMUM_SCENE_ROLES]; /* (the actor of each of its roles) */
	long start_time, run_time; /* (the tick its thread was made, last run by its actor's update) */
	/* (a blocking cs_ function's action - Halo 2's cs->type and its values,
	halo2-decompiled unknown_276f80.cpp: what the function was given, its
	start; action_step: NULL none, else the step its thread waits on with
	HALO_H2_CS_IN_ACTOR, sleep -3) */
	struct rt_cs_action action;
	rt_cs_action_step action_step;
};

/* Halo 2's AI scenes (scenario 0x170, 0x18 each: name, flags, triggers 0x08,
roles 0x10; a role 0x10: name, group 4 - which of ai_scene's ai it comes
from): halo2-upstream-work unknown_290e20.cpp function_291b40 reads them, and
MCC's maps have the same layout */
struct rt_scene
{
	char name[48];
	unsigned long flags;  /* (bit 0: may run again; else once a level) */
	boolean fired;
	short combine;        /* (its first trigger's: 0 any, else all) */
	short condition_count;
	short triggers[8];
	boolean negated[8];
	short role_count;
	char role_names[RT_MAXIMUM_SCENE_ROLES][32];
	short role_groups[RT_MAXIMUM_SCENE_ROLES];
};

static struct
{
	boolean loaded;
	boolean table_built;
	char map_name[64];
	struct rt_squad *squads;
	short squad_count;
	struct rt_group *groups;
	short group_count;
	struct rt_order *orders;
	long order_count;
	struct rt_trigger *triggers;
	long trigger_count;
	long orders_ended, conditions_unknown;
	struct rt_point_set point_sets[RT_MAXIMUM_POINT_SETS];
	short point_set_count;
	/* each BSP's Halo 2 navigation mesh (rt_nav: its pathfinding data's
	sectors, links and vertices), for the drivers this engine's AI does not
	drive (nav_path) */
	struct rt_nav *nav[RT_MAXIMUM_FRAME_BSPS];
	/* (h2_ai_actor_output) each driver's route: its target, its points */
	struct rt_route *routes[RT_MAXIMUM_ACTORS];
	long nav_paths, nav_failures;
	/* each BSP's reference frames (by frame index) */
	struct rt_frame *frames[RT_MAXIMUM_FRAME_BSPS];
	short frame_count[RT_MAXIMUM_FRAME_BSPS];
	/* this engine's encounter and squad: Halo 2's squad (NONE) */
	short squad_of[128][MAXIMUM_SQUADS_PER_ENCOUNTER_H2];
	/* each encounter's firing positions resolved for (its BSP, NONE) */
	short resolved_bsp[128];
	/* (encounter_order_page_in) the zones' firing positions, each zone's first and count, and whether an
	encounter's firing positions are now fp_pool's */
	struct rt_firing_position *zone_positions;
	long *zone_first, *zone_count;
	short zone_total;
	boolean pool_owned[128];
	long orders_paged, orders_unpaged;
	/* (h2_ai_scary_target_retreat) the AI globals' player and berserking actor scariness (0x160, 0x164 in
	MCC's layout), each weapon's AI scariness by its name, this engine's weapon tag (NONE: not matched yet) */
	real player_scariness, berserk_scariness;
	short weapon_scariness_count;
	struct { char name[96]; real value; long ce_tag; } weapon_scariness[RT_MAXIMUM_WEAPON_SCARINESS];
	long scary_retreats;
	/* an actor's (by absolute index) starting location (NONE) */
	short actor_location[RT_MAXIMUM_ACTORS];
	/* (h2_ai_vocalization_gesture) the dialogue globals' vocalizations with a gesture: their names and animation impulses */
	struct { char name[32]; short impulse; } gestures[192];
	short gesture_count;
	/* (h2_ai_berserk_update: each actor charging at the last tick) */
	boolean actor_charging[RT_MAXIMUM_ACTORS];
	/* (squad_members_update: each actor's datum index and the squad it counts in) */
	long member_actor[RT_MAXIMUM_ACTORS];
	short member_squad[RT_MAXIMUM_ACTORS];
	long charge_berserks;
	/* (ai_disposable: the actor's datum index while it is disposable - Halo 2's
	actor +0x0c - else NONE) */
	long actor_disposable[RT_MAXIMUM_ACTORS];
	long disposed_actors, disposed_vehicles, disposal_kept, squad_vehicles_erased, go_to_long_waits;
	/* (h2_ai_actor_output) a driver's ticks pressing on without moving, and
	its ticks backing off from what stops it */
	short foot_stuck[RT_MAXIMUM_ACTORS];
	long foot_jumps;
	short drive_stuck[RT_MAXIMUM_ACTORS], drive_reverse[RT_MAXIMUM_ACTORS];
	/* (a driver this engine drives: its ticks without a path to its command
	script's point) */
	short drive_pathless[RT_MAXIMUM_ACTORS];
	struct rt_command_script command_scripts[RT_MAXIMUM_COMMAND_SCRIPTS];
	struct rt_scene *scene_table;
	short scene_table_count;
	/* (ai_vehicle_reserve: the vehicles AI may not enter) */
	long reserved_vehicles[64];
	short reserved_vehicle_count;
	/* (ai_suppress_combat: by actor) */
	unsigned long combat_suppressed[RT_MAXIMUM_ACTORS / 32 + 1];
	long placed, killed_seen, command_scripts_run, command_scripts_waiting_max, orders_set, orders_unknown;
	/* (HALO_H2_CS_IN_ACTOR: command scripts run by their actor's update -
	h2_ai_command_scripts_run -: runs, scripts ended there, and ticks a due
	thread was left unrun because its actor was not updated) */
	boolean cs_in_actor;
	/* (HALO_H2_ACTOR_ACTIVATION, default off: Halo 2's
	actor activation - every actor of the loaded BSP updated, not this
	engine's encounters out of the players' sight put to sleep) */
	boolean actor_activation;
	long cs_actor_runs, cs_actor_ends, cs_unrun_ticks, cs_rest_runs, cs_actor_aborts;
	long cs_action_steps, cs_actions_done;
	long encounters_relocated, actors_relocated, actors_out_of_bsp;
	/* (HALO_H2_ACTOR_ACTIVATION: each actor's BSP - Halo 2's +0x3e - and the
	actor it is of) */
	short actor_bsp[RT_MAXIMUM_ACTORS];
	long actor_bsp_datum[RT_MAXIMUM_ACTORS];
	long go_to_arrived, go_to_timed_out, go_to_glides;
	long turrets_deployed, script_grenades, units_attached, follow_updates, follow_positions_cleared, secondary_sets, player_vehicle_boardings;
	long actor_index_counts, actor_index_actions; /* (calls naming one actor: ai_index_names_actor) */
	/* (HALO_H2_AI_STUCK: actors on foot that mean to move and do not, by place) */
	struct { real_point3d at; long time; short stuck; } stuck_watch[RT_MAXIMUM_ACTORS];
	struct { short x, y, z; long seconds; short bsp; short action; char what[24]; char blocked[24]; real_point3d to; } stuck_places[64];
	short stuck_place_count;
	long stuck_seconds, moving_seconds;
	long positions_given_up;
	short squad_peak[MAXIMUM_SQUADS];
	short only_squad;
	long last_men;
	long foot_route_until[RT_MAXIMUM_ACTORS];
	long foot_advance_next[RT_MAXIMUM_ACTORS];
	boolean foot_route_free[RT_MAXIMUM_ACTORS];
	long foot_advances, foot_returns;
	long foot_search_tick, foot_searches_refused;
	short foot_searches_this_tick;
	boolean route_timing;
	long route_searches, route_microseconds, route_worst, route_tick, route_tick_sum, route_worst_tick, route_tick_count, route_worst_tick_count;
	boolean order_camo[RT_MAXIMUM_ACTORS];
	long surprise_actor[RT_MAXIMUM_ACTORS], surprise_start[RT_MAXIMUM_ACTORS], surprises;
	char surprise_state[RT_MAXIMUM_ACTORS];
	boolean surprise_walking[RT_MAXIMUM_ACTORS];
	real_point3d foot_route_from[RT_MAXIMUM_ACTORS];
	long foot_route_started[RT_MAXIMUM_ACTORS];
	long foot_routes, foot_route_failures, foot_hops, foot_hoists;
	long fliers_moved, flier_turns_made;
	real_vector3d flier_turns[RT_MAXIMUM_ACTORS];
	long flier_turn_times[RT_MAXIMUM_ACTORS];
	real_point3d flier_goals[RT_MAXIMUM_ACTORS];
	long flier_goal_times[RT_MAXIMUM_ACTORS];
	boolean trigger_test_logged;
	boolean dialogue_off, dialogue_switched_off;
	long scene_switches;
	long deaths, last_report_time;
	/* (the log: who killed whom) each unit watched, its squad (Halo 2's;
	NONE: a player's) */
	struct { long unit_index; short squad; boolean dead; long encounter; short ce_squad; short type; short team; short clump; } watched[RT_MAXIMUM_ACTORS + 16];
	/* (clumps_update) Halo 2's clumps (0x502420): each actor's (its absolute index) clump + 1 (0: none), and the
	clumps - a team's actors within 6 wu of the centroid of its members */
	short clump_of[RT_MAXIMUM_ACTORS];
	long clump_actor[RT_MAXIMUM_ACTORS];
	struct { boolean used; short team; short count; real_point3d center; } clumps[RT_MAXIMUM_ACTORS];
	/* (h2_ai_behaviour_enabled) the scenario's style palette and the characters' own styles: each style's five
	behaviour bitfields (bit n: the behaviour of retail index n, re/behaviour names 0x46f0c8) */
	struct { unsigned long bits[5]; boolean valid; long special; } styles[16];
	struct { char name[40]; unsigned long bits[5]; boolean valid; } character_styles[64];
	/* (the path settings of each character, by name: its own style's special
	movement (style 0x4C, a block of longs: bit 0 jump, 1 climb, 2 vault, 3
	mount, 4 hoist, 5 wall jump; NONE: no style), its movement properties' jump
	height (0x1A: 0 none, 1 down, 2 step, 3 crouch, 4 stand, 5 storey, 6 tower,
	7 infinite) and movement hints (0x1C)) */
	struct { char name[40]; long special; short jump_height; unsigned long movement_hints; real radius; } path_characters[96];
	short path_character_count;
	short character_style_count;
	/* (h2_ai_danger_values) the AI globals' dangers: broadly facing, shooting near, shooting at, extremely close,
	shield damage, extended shield damage, body damage, extended body damage */
	real dangers[8];
	boolean dangers_read;
	/* (peer_dead_retreat) when each actor (its absolute index) last heard of a peer's death */
	long peer_death_time[RT_MAXIMUM_ACTORS];
	/* (h2_ai_unit_killed_by_script) the units ai_kill killed lately (no leader-dead retreat for them) */
	long script_killed[32];
	short script_killed_next;
	short watched_count;
	long kills_by_player, kills_by_ai, player_deaths;
	long last_update_time;
	long reverts;
	long sections_broken;
	long vehicles_made, vehicles_boarded;
	long placement_scripts;
	/* (ai_cannot_die, object_cannot_die) the objects hurt but not killed */
	long cannot_die[64];
	short cannot_die_count;
	short pending_bsp;
	long pending_bsp_time;
	/* (the scripted fliers: a Phantom's or Pelican's flight, flyers_update) */
	struct rt_flyer
	{
		boolean used;
		long vehicle;
		real_point3d hold;    /* where it hovers when not flying to a point */
		real_vector3d velocity; /* (a tick's: lags its controls, its bank's slip) */
		boolean holding;
		boolean pilotless;    /* (its pilot killed or out: hovering where it is, its cargo let go) */
		real speed;           /* now, a tick's */
		long riders[16];      /* (its riders last tick: those gone dropped) */
		short rider_count;
	} flyers[RT_MAXIMUM_FLYERS];
	/* (a dropship's troops on their way down: no falling damage until they
	have landed) */
	struct { long unit; long time; } dropped[32];
	short dropped_count;
	/* (a dropship's cargo: a vehicle carried at a cargo seat's marker -
	ai_place_in_vehicle of a vehicle's squad -, let go by vehicle_unload) */
	struct { long carrier, cargo; char label[32]; char marker[32]; } cargo[16];
	short cargo_count;
	long cargo_marker_missing;
	long flights, drives, scenes, drive_unsticks, flier_shots;
	long initial_orders, initial_orders_logged;
	long drives_taken_over;
	/* (the Flood: flood_ready) */
	boolean flood_ready;
	long flood_carrier_biped;
	short carrier_spawn_count;
	real carrier_spawn_velocity;
	long carriers_burst, infection_spawned;
	long flood_infection_biped;
	struct { long biped, variant; short kind; } flood_combat[4];
	short flood_combat_count;
	long reanimated;
	/* (the bodies an infection form goes to: Halo 2's infect impulse, flood_update) */
	struct { real_point3d marker; long body, form; } reanimation_bodies[16];
	short reanimation_bodies_count;
	unsigned long unit_spawn_count_offset;
	/* (the combat forms' feign death: Halo 2's elite and human forms') */
	struct { real threshold, time, chance, repeat; } feign[2];
	/* (ai_trigger_test: its ai's squads, condition_squads) */
	short trigger_self[MAXIMUM_SQUADS];
	short trigger_self_count;
} rt;
/* (perf/ce-floor-2) rt.loaded, readable outside (actors.c, actor_moving.c ask
it before their per-actor calls: with none loaded - a Halo CE level - those
return at once) */
boolean h2_ai_loaded;


/* ---------- the runtime: the characters' variants */

/* Halo 2's model variants (hlmt 0x50, 0x38 each: name; regions at 0x14,
0x14 each: name, permutations at 8, 0x20 each: name) of each biped
h2_biped.c made, as this engine's model's regions and permutations: an
object placed with a variant (h2_level.c: the placement's variant number,
the variant's index + 1) shows it, one placed without a variant one of
the model's "default..." variants (a Marine's head Perez, Morgan, Banks,
Dion or Smith, not each of them Johnson's) */
enum
{
	RT_VARIANT_BIPEDS = NUMBER_OF_H2_BIPEDS,
	RT_MAXIMUM_VARIANTS = 32,
	RT_VARIANT_REGIONS = 12,
	RT_VARIANT_CHOICES = 6,
	RT_VARIANT_STATES = 4,
};

H2_ALIGN static float h2_ai_read_real(struct h2_cache *cache, unsigned long address)
{
	unsigned char const *p = h2_cache_at(cache, address, 4);
	float value = 0.0f;

	if (p)
		memcpy(&value, p, 4);
	return value;
}

struct rt_variant
{
	char name[32];
	unsigned char region_count;
	struct
	{
		signed char region;                         /* this engine's model's */
		unsigned char choice_count;
		signed char choices[RT_VARIANT_CHOICES];    /* its permutations' (NONE: Halo 2's empty, the region hidden - none made) */
		float weights[RT_VARIANT_CHOICES];          /* (their probabilities, 0.2 a weight) */
		/* (the states of Halo 2's the permutation may start in - a Marine's
		helmet, destroyed one time in five: bare headed -: each one's chance,
		this engine's permutation of it; the rest of the time none, the
		permutation itself) */
		float state_chances[RT_VARIANT_CHOICES][RT_VARIANT_STATES];
		signed char state_choices[RT_VARIANT_CHOICES][RT_VARIANT_STATES];
		unsigned char state_counts[RT_VARIANT_CHOICES];
	} regions[RT_VARIANT_REGIONS];
};

static struct
{
	long definition;
	short count;
	short defaults[RT_MAXIMUM_VARIANTS];
	short default_count;
	short pinned;       /* (its default the biped's own default variant: its index + 1; 0 none) */
	struct rt_variant *variants;
	/* (this engine's biped made for it, and its Halo 2 model's (hlmt) disappear
	distance, +0x28 - 0 none: ai_disposable's visibility, halo2-decompiled
	unknown_0b8ca0.cpp 0xbba80) */
	long unit_definition;
	real disappear_distance;
} rt_variant_bipeds[RT_VARIANT_BIPEDS];

static long rt_variants_applied;

H2_ALIGN static void variants_dispose(void)
{
	short b;

	for (b = 0; b < RT_VARIANT_BIPEDS; b++)
		free(rt_variant_bipeds[b].variants);
	memset(rt_variant_bipeds, 0, sizeof(rt_variant_bipeds));
	rt_variants_applied = 0;
}

/* (a model's region or permutation by name, NONE) */
H2_ALIGN static short model_region_find(struct model *model, char const *name)
{
	short r;

	for (r = 0; r < MIN(model->regions.count, MAXIMUM_REGIONS_PER_OBJECT); r++)
	{
		if (!strncmp(TAG_BLOCK_GET_ELEMENT(&model->regions, r, struct model_region)->name, name, TAG_STRING_LENGTH))
			return r;
	}
	return NONE;
}

H2_ALIGN static short model_permutation_find(struct model_region *region, char const *name)
{
	short p;

	for (p = 0; p < MIN(region->permutations.count, MAXIMUM_PERMUTATIONS_PER_MODEL_REGION); p++)
	{
		if (!strncmp(TAG_BLOCK_GET_ELEMENT(&region->permutations, p, struct model_region_permutation)->name, name, TAG_STRING_LENGTH))
			return p;
	}
	return NONE;
}

H2_ALIGN static void variants_read(struct h2_cache *cache)
{
	short b;

	for (b = 0; b < RT_VARIANT_BIPEDS; b++)
	{
		char ce_name[128];
		long definition, h2_biped, hlmt, variant_count, i;
		struct h2_cache_tag t, h;
		unsigned long variants;
		struct object_definition *object_definition;
		struct model *model;

		rt_variant_bipeds[b].definition = NONE;
		rt_variant_bipeds[b].unit_definition = NONE;
		rt_variant_bipeds[b].disappear_distance = 0.0f;
		if (!h2_bipeds_source_name(b))
			continue;
		snprintf(ce_name, sizeof(ce_name), "h2\\characters\\%s_mp\\%s_mp", h2_bipeds_source_name(b), h2_bipeds_source_name(b));
		definition = tag_loaded('bipd', ce_name);
		if (definition == NONE)
			continue;
		object_definition = (struct object_definition *)tag_get('obje', definition);
		if (!object_definition || object_definition->object.model.index == NONE)
			continue;
		model = (struct model *)tag_get('mode', object_definition->object.model.index);
		h2_biped = h2_bipeds_made_from(cache, b);
		rt_variant_bipeds[b].unit_definition = definition;
		if (h2_biped != NONE && h2_cache_tag_get(cache, h2_biped, &t) && (hlmt = h2_cache_reference(cache, t.address + 0x34)) != NONE &&
			h2_cache_tag_get(cache, hlmt, &h))
		{
			rt_variant_bipeds[b].disappear_distance = readf(cache, h.address + 0x28);
			if (HALO_ENV_ONCE("HALO_H2_AI_DISPOSAL_LOG"))
				platform_log("h2 ai disposal: biped %s: disappear distance %.2f", h2_bipeds_source_name(b), rt_variant_bipeds[b].disappear_distance);
		}
		if (h2_biped == NONE || !h2_cache_tag_get(cache, h2_biped, &t) || (hlmt = h2_cache_reference(cache, t.address + 0x34)) == NONE ||
			!h2_cache_tag_get(cache, hlmt, &h) || !h2_cache_block(cache, h.address + 0x50, 0x38, &variant_count, &variants) || variant_count < 1)
		{
			continue;
		}
		variant_count = MIN(variant_count, RT_MAXIMUM_VARIANTS);
		rt_variant_bipeds[b].variants = calloc((size_t)variant_count, sizeof(struct rt_variant));
		if (!rt_variant_bipeds[b].variants)
			continue;
		rt_variant_bipeds[b].definition = definition;
		rt_variant_bipeds[b].count = (short)variant_count;
		for (i = 0; i < variant_count; i++)
		{
			struct rt_variant *v = &rt_variant_bipeds[b].variants[i];
			long region_count, r;
			unsigned long regions;

			h2_cache_string(cache, h2_cache_read32(cache, variants + 0x38 * i), v->name, sizeof(v->name));
			if (!strcmp(v->name, "default"))
				rt_variant_bipeds[b].default_count = 0, rt_variant_bipeds[b].defaults[rt_variant_bipeds[b].default_count++] = (short)i;
			else if (!strncmp(v->name, "default", 7) && (rt_variant_bipeds[b].default_count == 0 ||
				strcmp(rt_variant_bipeds[b].variants[rt_variant_bipeds[b].defaults[0]].name, "default")))
			{
				rt_variant_bipeds[b].defaults[rt_variant_bipeds[b].default_count++] = (short)i;
			}
			/* (the cutscenes' people, the Flood, the Sentinels, the
			Heretics...: Halo 2's biped's default model variant, when it
			names one - the Heretic leader's "leader_her", his hologram's
			"holo_her" -, alone) */
			if (b >= H2_BIPED_MIRANDA && h2_bipeds_default_variant(cache, h2_biped) &&
				h2_cache_read32(cache, variants + 0x38 * i) == h2_bipeds_default_variant(cache, h2_biped))
			{
				rt_variant_bipeds[b].pinned = (short)(i + 1);
			}
			if (!h2_cache_block(cache, variants + 0x38 * i + 0x14, 0x14, &region_count, &regions))
				continue;
			for (r = 0; r < region_count && v->region_count < RT_VARIANT_REGIONS; r++)
			{
				char region_name[64];
				short region;
				long permutation_count, k;
				unsigned long permutations;

				h2_cache_string(cache, h2_cache_read32(cache, regions + 0x14 * r), region_name, sizeof(region_name));
				region = model_region_find(model, region_name);
				if (region == NONE || !h2_cache_block(cache, regions + 0x14 * r + 8, 0x20, &permutation_count, &permutations))
					continue;
				v->regions[v->region_count].region = (signed char)region;
				v->regions[v->region_count].choice_count = 0;
				for (k = 0; k < permutation_count && v->regions[v->region_count].choice_count < RT_VARIANT_CHOICES; k++)
				{
					char permutation_name[64];
					short permutation;

					h2_cache_string(cache, h2_cache_read32(cache, permutations + 0x20 * k), permutation_name, sizeof(permutation_name));
					/* (no name of its own: its first state's - states at 0xC,
					0x18 each, the permutation's name first -: Johnson's hat, a
					pilot's helmet and visor, MCC's Outskirts intro) */
					if (!permutation_name[0])
					{
						long state_count;
						unsigned long states;

						if (h2_cache_block(cache, permutations + 0x20 * k + 0xC, 0x18, &state_count, &states) && state_count > 0)
							h2_cache_string(cache, h2_cache_read32(cache, states), permutation_name, sizeof(permutation_name));
					}
					permutation = model_permutation_find(TAG_BLOCK_GET_ELEMENT(&model->regions, region, struct model_region),
						permutation_name[0] ? permutation_name : "none");
					if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
						platform_log("h2 ai: %s variant %s region %s permutation %s: %d", ce_name, v->name, region_name, permutation_name, (int)permutation);
					/* (Halo 2's probability, at 8: none, never - a Marine's
					default_nrl has helmet "on" at 0, bare headed for good) */
					if (permutation != NONE && h2_ai_read_real(cache, permutations + 0x20 * k + 8) > 0.0f)
					{
						short choice = v->regions[v->region_count].choice_count;
						long state_count;
						unsigned long states;

						v->regions[v->region_count].weights[choice] = h2_ai_read_real(cache, permutations + 0x20 * k + 8);
						v->regions[v->region_count].choices[choice] = (signed char)permutation;
						v->regions[v->region_count].state_counts[choice] = 0;
						/* (every state it may start in, each by its initial
						probability at 0x14, not the first alone) */
						if (permutation_name[0] && h2_cache_block(cache, permutations + 0x20 * k + 0xC, 0x18, &state_count, &states))
						{
							long q;

							for (q = 0; q < state_count && v->regions[v->region_count].state_counts[choice] < RT_VARIANT_STATES; q++)
							{
								char state_name[64];
								short state;
								float chance = h2_ai_read_real(cache, states + 0x18 * q + 0x14);
								unsigned char at = v->regions[v->region_count].state_counts[choice];

								h2_cache_string(cache, h2_cache_read32(cache, states + 0x18 * q), state_name, sizeof(state_name));
								state = model_permutation_find(TAG_BLOCK_GET_ELEMENT(&model->regions, region, struct model_region),
									state_name[0] ? state_name : "none");
								if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
									platform_log("h2 ai: %s variant %s region %s: state '%s' at %.2f: permutation %d", ce_name, v->name, region_name,
										state_name, chance, (int)state);
								if (state != NONE && state != permutation && chance > 0.0f)
								{
									v->regions[v->region_count].state_chances[choice][at] = chance;
									v->regions[v->region_count].state_choices[choice][at] = (signed char)state;
									v->regions[v->region_count].state_counts[choice]++;
								}
							}
						}
						v->regions[v->region_count].choice_count++;
					}
				}
				if (v->regions[v->region_count].choice_count)
					v->region_count++;
			}
		}
		if (rt_variant_bipeds[b].pinned)
		{
			rt_variant_bipeds[b].defaults[0] = rt_variant_bipeds[b].pinned - 1;
			rt_variant_bipeds[b].default_count = 1;
		}
		if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
			platform_log("h2 ai: %s: %d variants (%d defaults)", ce_name, rt_variant_bipeds[b].count, rt_variant_bipeds[b].default_count);
	}
}

/* a character's colours for its variant: its Halo 2 biped's change
colours (object 0xAC, 0x10 each: initial permutations at 0, 0x20 each -
weight, lower and upper colour, the variant's name) whose permutations
name the variant (else those naming none), one at random by weight, a
colour between its bounds - an Elite major's red, a Jackal major's
orange. Halo 2's primary and secondary colours are this engine's C and D,
as h2_biped.c writes the bipeds' change colours and objects.c picks them
for a placement's variant (this, for a variant chosen after the object is
made: an AI's character's) */
H2_ALIGN static void variant_change_colours(struct object_datum *object, short biped, char const *variant)
{
	struct h2_cache *cache = h2_scripts_cache();
	long tag, count, i;
	unsigned long colours;
	struct h2_cache_tag t;

	if (!cache || HALO_ENV_ONCE("HALO_H2_NO_VARIANT_COLOURS") || (tag = h2_bipeds_made_from(cache, biped)) == NONE || !h2_cache_tag_get(cache, tag, &t) ||
		!h2_cache_block(cache, t.address + 0xAC, 0x10, &count, &colours) || count < 1)
	{
		return;
	}
	for (i = 0; i < count && i < 2; i++)
	{
		short slot = i == 0 ? _object_change_color_c : _object_change_color_d;

		long permutation_count, k, pass;
		unsigned long permutations;
		float total = 0.0f, pick;

		if (!h2_cache_block(cache, colours + 0x10 * (unsigned long)i, 0x20, &permutation_count, &permutations) || permutation_count < 1)
			continue;
		/* (those naming the variant; else those naming none; else a minor's:
		05a's shieldless Jackal has none of its own, no colour at all gave his
		armour grey where the Xbox's is blue - a fit to that frame) */
		for (pass = 0; pass < 3 && total <= 0.0f; pass++)
		{
			for (k = 0; k < permutation_count; k++)
			{
				char name[64];
				float weight = readf(cache, permutations + 0x20 * (unsigned long)k);

				h2_cache_string(cache, h2_cache_read32(cache, permutations + 0x20 * (unsigned long)k + 0x1C), name, sizeof(name));
				if (pass == 0 ? !strcmp(name, variant) : pass == 1 ? !name[0] : !strcmp(name, "minor"))
					total += weight > 0.0f ? weight : 1.0f;
			}
			if (total > 0.0f)
				break;
		}
		if (total <= 0.0f)
			continue;
		pick = real_random_range(0.0f, total);
		for (k = 0; k < permutation_count; k++)
		{
			char name[64];
			unsigned long at = permutations + 0x20 * (unsigned long)k;
			float weight = readf(cache, at), a;

			h2_cache_string(cache, h2_cache_read32(cache, at + 0x1C), name, sizeof(name));
			if (pass == 0 ? strcmp(name, variant) : pass == 1 ? name[0] != 0 : strcmp(name, "minor"))
				continue;
			weight = weight > 0.0f ? weight : 1.0f;
			if (pick > weight && k + 1 < permutation_count)
			{
				pick -= weight;
				continue;
			}
			a = real_random_range(0.0f, 1.0f);
			object->object.base_change_colors[slot].red = readf(cache, at + 4) + a * (readf(cache, at + 0x10) - readf(cache, at + 4));
			object->object.base_change_colors[slot].green = readf(cache, at + 8) + a * (readf(cache, at + 0x14) - readf(cache, at + 8));
			object->object.base_change_colors[slot].blue = readf(cache, at + 0xC) + a * (readf(cache, at + 0x18) - readf(cache, at + 0xC));
			/* (and what is drawn: objects.c makes it from the base colours
			once, when the object is made - a variant shown since) */
			object->object.outgoing_change_colors[slot].red = PIN(object->object.base_change_colors[slot].red, 0.0f, 1.0f);
			object->object.outgoing_change_colors[slot].green = PIN(object->object.base_change_colors[slot].green, 0.0f, 1.0f);
			object->object.outgoing_change_colors[slot].blue = PIN(object->object.base_change_colors[slot].blue, 0.0f, 1.0f);
			break;
		}
	}
}

/* (objects.c, an object made: its regions' permutations chosen) a
character's variant: the one its placement names (variant_number, the
variant's index + 1), else one of its defaults */
H2_ALIGN void h2_ai_object_variant(
	long object_index,
	short variant_number)
{
	struct object_datum *object = object_get(object_index);
	short b, chosen = NONE;

	/* (a Halo 2 vehicle's placed model variant kept: its shaders' images go
	by it - h2_weapon_counter.c, the heretic Banshees' plates) */
	if (object->object.type == _object_type_vehicle && variant_number > 0 && !HALO_ENV_ONCE("HALO_H2_NO_VARIANT_IMAGES"))
		object->object.variant_number = variant_number;
	if (object->object.type != _object_type_biped)
		return;
	for (b = 0; b < RT_VARIANT_BIPEDS && !(rt_variant_bipeds[b].count && rt_variant_bipeds[b].definition == object->definition_index); b++)
		;
	if (HALO_ENV_ONCE("HALO_H2_CINEMATIC_LOG"))
		platform_log("h2 variant: %s placed %d, table %d of %d", tag_get_name(object->definition_index), (int)variant_number, (int)b,
			b < RT_VARIANT_BIPEDS ? (int)rt_variant_bipeds[b].count : 0);
	if (b == RT_VARIANT_BIPEDS)
		return;
	if (variant_number > 0 && variant_number <= rt_variant_bipeds[b].count)
		chosen = variant_number - 1;
	else if (rt_variant_bipeds[b].default_count)
		chosen = rt_variant_bipeds[b].defaults[rt_variant_bipeds[b].default_count == 1 ? 0 : random_range(0, rt_variant_bipeds[b].default_count)];
	if (chosen != NONE)
	{
		struct rt_variant const *v = &rt_variant_bipeds[b].variants[chosen];
		short r;

		for (r = 0; r < v->region_count; r++)
		{
			short k = 0;

			if (v->regions[r].choice_count > 1)
			{
				float total = 0.0f, pick;

				for (k = 0; k < v->regions[r].choice_count; k++)
					total += v->regions[r].weights[k];
				pick = real_random_range(0.0f, total);
				for (k = 0; k + 1 < v->regions[r].choice_count && pick >= v->regions[r].weights[k]; k++)
					pick -= v->regions[r].weights[k];
			}

			/* (its state: one draw over the states' chances in turn, none
			past them - the permutation as it is) */
			{
				short permutation = v->regions[r].choices[k];

				if (v->regions[r].state_counts[k])
				{
					float draw = real_random_range(0.0f, 1.0f);
					short q;

					for (q = 0; q < v->regions[r].state_counts[k]; q++)
					{
						if (draw < v->regions[r].state_chances[k][q])
						{
							permutation = v->regions[r].state_choices[k][q];
							break;
						}
						draw -= v->regions[r].state_chances[k][q];
					}
				}
				/* (a region past the datum's 8 - a Marine's ninth, a Flood
				Elite's 9-12 - in the side table, objects.h: written here it ran
				into the object's change colours) */
				object_region_permutation_set(object_index, v->regions[r].region, (byte)permutation);
			}
		}
		/* (not the players' Spartan and Elite: their colours the players') */
		if (b >= H2_BIPED_FIRST_CAMPAIGN)
		{
			object->object.variant_number = (short)(chosen + 1);
			variant_change_colours(object, b, v->name);
		}
		rt_variants_applied++;
	}
}

/* (object_set_variant <object> <string_id>) */
H2_ALIGN static short variant_find(long object_index, char const *name);

/* a placed actor's unit shows one of its Halo 2 character's variants
(the character's variants block, 0x2C, 0xC each: the variant's name -
an Elite major's major_scl or major_dog, its rank's armour -; else its
parent's, up the chain), at random; its actor variant's tag names the
character (h2_ai_tag_describe: "h2\\ai\\<character> <weapon> <n>") */
H2_ALIGN static void character_variant_apply(struct actor_datum const *actor)
{
	struct h2_cache *cache = h2_scripts_cache();
	char character[96];
	char const *name;
	long tag = NONE, i, guard;

	if (!cache || actor->meta.unit_index == NONE || actor->meta.variant_definition_index == NONE || HALO_ENV_ONCE("HALO_H2_NO_CHARACTER_VARIANTS"))
		return;
	name = tag_get_name(actor->meta.variant_definition_index);
	if (strncmp(name, "h2\\ai\\", 6))
		return;
	snprintf(character, sizeof(character), "%s", name + 6);
	if (strchr(character, ' '))
		*strchr(character, ' ') = '\0';
	for (i = 0; i < h2_cache_tag_count(cache) && tag == NONE; i++)
	{
		struct h2_cache_tag t;
		char const *n;
		size_t a, b;

		if (!h2_cache_tag_get(cache, i, &t) || t.group_tag != 'char')
			continue;
		n = h2_cache_tag_name(cache, i);
		a = strlen(n), b = strlen(character);
		if (a > b && n[a - b - 1] == '\\' && !strcmp(n + a - b, character))
			tag = i;
	}
	for (guard = 0; tag != NONE && guard < MAXIMUM_INHERITANCE; guard++)
	{
		struct h2_cache_tag t;
		long count;
		unsigned long variants;

		if (!h2_cache_tag_get(cache, tag, &t) || t.group_tag != 'char')
			return;
		if (h2_cache_block(cache, t.address + 0x2C, 0xC, &count, &variants) && count > 0)
		{
			char text[64];
			short number;

			i = count == 1 ? 0 : random_range(0, (short)MIN(count, 32000));
			if (h2_cache_string(cache, h2_cache_read32(cache, variants + 0xC * (unsigned long)i), text, sizeof(text)) &&
				(number = variant_find(actor->meta.unit_index, text)) != NONE)
			{
				h2_ai_object_variant(actor->meta.unit_index, number);
				if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
					platform_log("h2 ai: %s shows variant %s", h2_cache_tag_name(cache, tag), text);
			}
			return;
		}
		tag = h2_cache_reference(cache, t.address + H2C_PARENT);
	}
}

H2_ALIGN static short variant_find(long object_index, char const *name)
{
	struct object_datum *object = object_get(object_index);
	short b, i;

	for (b = 0; b < RT_VARIANT_BIPEDS && !(rt_variant_bipeds[b].count && rt_variant_bipeds[b].definition == object->definition_index); b++)
		;
	if (b == RT_VARIANT_BIPEDS || !name)
		return NONE;
	for (i = 0; i < rt_variant_bipeds[b].count; i++)
	{
		if (!strcmp(rt_variant_bipeds[b].variants[i].name, name))
			return i + 1;
	}
	return NONE;
}

/* ---------- the runtime: the mission's dialogue */

/* the level's mission dialogue (mdlg: lines at 0, 0x10 each - name, variants
at 4 - 0x10 each: designation, sound reference at 4 -, default sound
effect): each line's name and its first variant's sound, as the maps name
it without "sound\\" (h2_sound_play_on_object) */
enum
{
	RT_MAXIMUM_LINES = 512,
};

static struct
{
	long count;
	/* (radio: its default sound effect's - 1 radio_default, heard unspatialized
	wherever its speaker is; 2 radio_3d, the speaker's voice near, the radio's
	far; 0 the speaker's voice) */
	struct rt_line { char name[12]; char sound[100]; char radio; } *lines;
	long played, not_played;
} rt_dialogue;

H2_ALIGN static void dialogue_dispose(void)
{
	free(rt_dialogue.lines);
	memset(&rt_dialogue, 0, sizeof(rt_dialogue));
}

H2_ALIGN static void dialogue_read(struct h2_cache *cache)
{
	long tag, count = h2_cache_tag_count(cache);

	for (tag = 0; tag < count; tag++)
	{
		struct h2_cache_tag t;
		long line_count, i;
		unsigned long lines;

		if (!h2_cache_tag_get(cache, tag, &t) || t.group_tag != 'mdlg' ||
			!h2_cache_block(cache, t.address, 0x10, &line_count, &lines) || line_count <= 0)
		{
			continue;
		}
		if (!rt_dialogue.lines && !(rt_dialogue.lines = calloc(RT_MAXIMUM_LINES, sizeof(struct rt_line))))
			return;
		for (i = 0; i < line_count && rt_dialogue.count < RT_MAXIMUM_LINES; i++)
		{
			struct rt_line *l = &rt_dialogue.lines[rt_dialogue.count];
			long variant_count, sound;
			unsigned long variants;
			char const *name;

			if (!h2_cache_string(cache, h2_cache_read32(cache, lines + 0x10 * i), l->name, sizeof(l->name)) ||
				!h2_cache_block(cache, lines + 0x10 * i + 4, 0x10, &variant_count, &variants) || variant_count < 1 ||
				(sound = h2_cache_reference(cache, variants + 4)) == NONE)
			{
				continue;
			}
			name = h2_cache_tag_name(cache, sound);
			snprintf(l->sound, sizeof(l->sound), "%s", !strncmp(name, "sound\\", 6) ? name + 6 : name);
			{
				char effect[32] = "";

				h2_cache_string(cache, h2_cache_read32(cache, lines + 0x10 * i + 0xC), effect, sizeof(effect));
				l->radio = !strcmp(effect, "radio_default") ? 1 : !strncmp(effect, "radio", 5) ? 2 : 0;
			}
			rt_dialogue.count++;
		}
	}
	if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
		platform_log("h2 ai: mission dialogue: %ld lines", rt_dialogue.count);
}

/* (h2_scripts.c, h2_sound.c's script sounds) the lines' sounds, the first
`maximum` (h2_sound.c gives each the pool sound that plays it) */
H2_ALIGN long h2_ai_dialogue_sound_names(
	char const **names,
	long maximum)
{
	long i;

	for (i = 0; i < rt_dialogue.count && i < maximum; i++)
		names[i] = rt_dialogue.lines[i].sound;
	return i < maximum ? i : maximum;
}

/* a line played on an object (NONE: none - the player's then); its length
in ticks (three seconds when it cannot be heard: the scripts sleep for it) */
H2_ALIGN static short dialogue_play(char const *line, long object_index)
{
	long i;

	/* (no speaker - a radio line, or one whose speaker is gone: heard on the
	player) */
	if (object_index == NONE || !object_try_and_get(object_index))
	{
		long player_index = local_player_get_player_index(0);

		object_index = player_index != NONE ? player_get(player_index)->unit_index : NONE;
	}
	for (i = 0; line && i < rt_dialogue.count; i++)
	{
		if (!strcmp(rt_dialogue.lines[i].name, line))
		{
			real seconds = 0.0f;

			/* (a radio line heard wherever its speaker is - radio_default
			always, radio_3d when the speaker's own voice cannot be heard -, as
			Halo 2 plays its mission dialogue's radio lines) */
			if ((rt_dialogue.lines[i].radio != 1 && object_index != NONE && object_try_and_get(object_index) &&
				h2_sound_play_on_object(rt_dialogue.lines[i].sound, object_index, &seconds) != NONE && seconds > 0.0f) ||
				(rt_dialogue.lines[i].radio && h2_sound_play_unspatialized(rt_dialogue.lines[i].sound, &seconds) != NONE && seconds > 0.0f))
			{
				rt_dialogue.played++;
				/* (a line said by its speaker: its lip-sync on the speaker's
				face, h2_cinematics.c) */
				if (rt_dialogue.lines[i].radio != 1)
					h2_cinematics_face_start(object_index, rt_dialogue.lines[i].sound);
				if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
					platform_log("h2 ai: line '%s' (%s)%s", line, rt_dialogue.lines[i].sound,
						rt_dialogue.lines[i].radio == 1 ? " on the radio" : rt_dialogue.lines[i].radio ? " (radio near or far)" : "");
				return (short)MIN(seconds * 30.0f + 0.5f, 32000.0f);
			}
			break;
		}
	}
	rt_dialogue.not_played++;
	if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
		platform_log("h2 ai: line '%s' not heard (%s, speaker %lx)", line ? line : "(none)",
			i < rt_dialogue.count ? rt_dialogue.lines[i].sound : "no such line", object_index);
	/* (a line not heard still takes its own time, as Halo 2's does: its
	length from the level's gestalt (h2_sound_length_ticks); three seconds
	for one not known) */
	if (i < rt_dialogue.count)
	{
		long known = h2_sound_length_ticks(rt_dialogue.lines[i].sound);

		if (known > 0)
			return (short)known;
	}
	return 90;
}

/* ---------- the runtime: the level read */

H2_ALIGN static void stuck_report(void);

H2_ALIGN void h2_ai_dispose_from_old_map(
	void)
{
	short i;

	if (rt.loaded)
		stuck_report();
	if (rt.loaded)
		platform_log("h2 ai: %s left: %ld squads placed, %ld died (%ld by players, %ld by the AI), players killed %ld, %ld command scripts run, %ld orders given (%ld unknown), %ld orders ended, "
			"cs_go_to %ld arrived %ld failed (no path), %ld waited past 30 s; %ld charge berserks; %ld turrets deployed, %ld script grenades, %ld units attached, %ld follow updates (%ld positions let go), %ld secondary area sets, %ld boarded the player's vehicle; %ld vehicles made, %ld riders seated; %ld variants shown; dialogue lines %ld played, %ld not heard; one-actor ai indices: %ld counts, %ld actions; disposal: %ld actors, %ld vehicles erased, %ld kept a pass; ai_erase: %ld squad vehicles",
			rt.map_name, rt.placed, rt.deaths, rt.kills_by_player, rt.kills_by_ai, rt.player_deaths,
			rt.command_scripts_run, rt.orders_set, rt.orders_unknown,
			rt.orders_ended, rt.go_to_arrived, rt.go_to_timed_out, rt.go_to_long_waits, rt.charge_berserks, rt.turrets_deployed, rt.script_grenades, rt.units_attached, rt.follow_updates, rt.follow_positions_cleared, rt.secondary_sets, rt.player_vehicle_boardings, rt.vehicles_made, rt.vehicles_boarded, rt_variants_applied,
			rt_dialogue.played, rt_dialogue.not_played, rt.actor_index_counts, rt.actor_index_actions, rt.disposed_actors, rt.disposed_vehicles, rt.disposal_kept, rt.squad_vehicles_erased);
	if (rt.loaded && rt.cs_in_actor)
		platform_log("h2 ai: %s left: command scripts in their actor's update: %ld thread runs (%ld of actors this engine did not update), %ld scripts returned there, %ld aborted there, %ld due threads left unrun; "
			"%ld action steps while the thread slept -3, %ld actions done",
			rt.map_name, rt.cs_actor_runs, rt.cs_rest_runs, rt.cs_actor_ends, rt.cs_actor_aborts, rt.cs_unrun_ticks, rt.cs_action_steps, rt.cs_actions_done);
	if (rt.loaded && rt.actor_activation)
		platform_log("h2 ai: %s left: Halo 2's actor activation: %ld encounters moved to the loaded BSP, %ld actors put to sleep out of it, %ld woken in it",
			rt.map_name, rt.encounters_relocated, rt.actors_out_of_bsp, rt.actors_relocated);
	if (rt.loaded && (rt.orders_paged || rt.orders_unpaged))
		platform_log("h2 ai: %s left: %ld orders' firing positions paged into their encounter, %ld with none to page in",
			rt.map_name, rt.orders_paged, rt.orders_unpaged);
	if (rt.loaded || rt.map_name[0])
		h2_scripts_set_bsp_switcher(NULL, NULL);
	variants_dispose();
	dialogue_dispose();
	free(rt.squads);
	free(rt.groups);
	free(rt.orders);
	free(rt.triggers);
	free(rt.scene_table);
	free(rt.zone_positions);
	free(rt.zone_first);
	free(rt.zone_count);
	for (i = 0; i < rt.point_set_count; i++)
	{
		free(rt.point_sets[i].points);
		free(rt.point_sets[i].frames);
	}
	for (i = 0; i < RT_MAXIMUM_FRAME_BSPS; i++)
	{
		free(rt.frames[i]);
		nav_free(rt.nav[i]);
	}
	for (i = 0; i < RT_MAXIMUM_ACTORS; i++)
		free(rt.routes[i]);
	memset(&rt, 0, sizeof(rt));
	h2_ai_loaded = FALSE;
	rt.pending_bsp = NONE;
}

/* the level's reference frames (struct rt_frame) read from each structure
BSP's pathfinding data: scenario machines 0xA8 (0x48: palette 0, position
8), machine palette 0xB0 (0x28: reference 0), a machine tag's pathfinding
policy 0x130 (1 sectors); a structure BSP's pathfinding data 0xC4, its
object refs 0x30 (0x1C: flags 0, nodes 0x14 - 4 each: frame index 0) */
H2_ALIGN static void frames_read(struct h2_cache *cache, unsigned long scenario)
{
	enum { MAXIMUM_MOBILE = 64 };
	struct { long tag; real_point3d position; } mobile[MAXIMUM_MOBILE];
	short mobile_count = 0;
	long machine_count, palette_count, k, b, bsp_count = h2_cache_structure_count(cache);
	unsigned long machines, palette;

	if (!h2_cache_block(cache, scenario + 0xA8, 0x48, &machine_count, &machines) ||
		!h2_cache_block(cache, scenario + 0xB0, 0x28, &palette_count, &palette))
	{
		return;
	}
	for (k = 0; k < machine_count && mobile_count < MAXIMUM_MOBILE; k++)
	{
		unsigned char const *m = h2_cache_at(cache, machines + 0x48 * k, 0x14);
		struct h2_cache_tag tag;
		short type;
		unsigned char const *policy;

		if (!m || (type = get16(m)) < 0 || type >= palette_count ||
			!h2_cache_tag_get(cache, h2_cache_reference(cache, palette + 0x28 * type), &tag) ||
			!(policy = h2_cache_at(cache, tag.address + 0x130, 2)) || get16(policy) != 1)
		{
			continue;
		}
		mobile[mobile_count].tag = h2_cache_reference(cache, palette + 0x28 * type);
		memcpy(&mobile[mobile_count].position, m + 8, 12);
		mobile_count++;
	}
	for (b = 0; b < bsp_count && b < RT_MAXIMUM_FRAME_BSPS; b++)
	{
		struct h2_cache_tag bsp;
		long data_count, ref_count, r;
		unsigned long data, refs;
		short m = 0;

		if (!h2_cache_tag_get(cache, h2_cache_structure_tag(cache, b), &bsp) ||
			!h2_cache_block(cache, bsp.address + 0xC4, 0x38, &data_count, &data) || data_count < 1 ||
			!h2_cache_block(cache, data + 0x30, 0x1C, &ref_count, &refs))
		{
			continue;
		}
		for (r = 0; r < ref_count; r++)
		{
			unsigned char const *ref = h2_cache_at(cache, refs + 0x1C * r, 0x1C);
			long node_count, n;
			unsigned long nodes;

			if (!ref || !(get16(ref) & 1))
				continue;
			if (m < mobile_count && h2_cache_block(cache, refs + 0x1C * r + 0x14, 4, &node_count, &nodes))
			{
				for (n = 0; n < node_count; n++)
				{
					unsigned char const *node = h2_cache_at(cache, nodes + 4 * n, 2);
					short frame = node ? get16(node) : NONE;

					if (frame < 0 || frame >= RT_MAXIMUM_FRAMES)
						continue;
					if (!rt.frames[b] && !(rt.frames[b] = calloc(RT_MAXIMUM_FRAMES, sizeof(struct rt_frame))))
						return;
					snprintf(rt.frames[b][frame].tag, sizeof(rt.frames[b][frame].tag), "%s",
						h2_cache_tag_name(cache, mobile[m].tag) ? h2_cache_tag_name(cache, mobile[m].tag) : "");
					rt.frames[b][frame].position = mobile[m].position;
					rt.frames[b][frame].node = (short)n;
					if (frame >= rt.frame_count[b])
						rt.frame_count[b] = frame + 1;
				}
			}
			m++;
		}
		if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
			platform_log("h2 ai: bsp %ld: %d reference frames, %d mobile object refs, %d sectors machines", b, rt.frame_count[b], m,
				mobile_count);
	}
}

/* a point in reference frame `frame` (the loaded BSP's) put in the world:
the frame's machine (this engine's machine of the same tag nearest where
Halo 2 places it) and its node; FALSE: the frame or machine is not here */
/* (perf/vita-native) deck_carry asks for the same few frames link after link
(every third tick, each link a walk of every object in the game for the
frame's machine: 4% of Cairo Station's tick on the PC): while it runs -
nothing it writes is an object - a frame's machine is the one found for the
link before. frame_machine_memo: the frames found in this deck_carry, NULL
outside it; the point is transformed as ever. HALO_H2_FRAME_MEMO=0: found
again for each link, as before. */
#include "native_switch.h"
#define FRAME_MACHINE_MEMO_ENTRIES 16
struct frame_machine_memo
{
	short count;
	short frames[FRAME_MACHINE_MEMO_ENTRIES];
	long objects[FRAME_MACHINE_MEMO_ENTRIES]; /* (NONE: no machine) */
};
static struct frame_machine_memo *frame_machine_memo;

H2_ALIGN static boolean frame_transform(short frame, real_point3d *point)
{
	short bsp = global_structure_bsp_index;
	struct rt_frame const *f;
	char const *base;
	struct object_iterator iterator;
	struct object_datum *object;
	long best_object = NONE;
	real best = 1.0e9f;
	short memo_index;

	if (frame < 0 || bsp < 0 || bsp >= RT_MAXIMUM_FRAME_BSPS || frame >= rt.frame_count[bsp] || !rt.frames[bsp] ||
		!rt.frames[bsp][frame].tag[0])
	{
		return FALSE;
	}
	f = &rt.frames[bsp][frame];
	for (memo_index = 0; frame_machine_memo && memo_index < frame_machine_memo->count; memo_index++)
	{
		if (frame_machine_memo->frames[memo_index] == frame)
		{
			best_object = frame_machine_memo->objects[memo_index];
			goto found;
		}
	}
	base = strrchr(f->tag, '\\') ? strrchr(f->tag, '\\') + 1 : f->tag;
	object_iterator_new(&iterator, _object_mask_machine, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		char const *name = tag_get_name(object->definition_index);
		char const *own = name && strrchr(name, '\\') ? strrchr(name, '\\') + 1 : name;
		real_point3d origin;
		real d;

		if (!own || strcmp(own, base))
			continue;
		object_get_origin(iterator.index, &origin);
		d = (origin.x - f->position.x) * (origin.x - f->position.x) + (origin.y - f->position.y) * (origin.y - f->position.y) +
			(origin.z - f->position.z) * (origin.z - f->position.z);
		if (d < best)
			best = d, best_object = iterator.index;
	}
	if (frame_machine_memo && frame_machine_memo->count < FRAME_MACHINE_MEMO_ENTRIES)
	{
		frame_machine_memo->frames[frame_machine_memo->count] = frame;
		frame_machine_memo->objects[frame_machine_memo->count] = best_object;
		frame_machine_memo->count++;
	}
found:
	if (best_object == NONE)
		return FALSE;
	{
		struct object_definition *definition = object_definition_get(object_get(best_object)->definition_index);
		struct model *model = definition->object.model.index != NONE ? model_definition_get(definition->object.model.index) : NULL;
		short node = model && f->node < model->nodes.count ? f->node : 0;
		real_point3d local = *point, world;

		matrix4x3_transform_point(object_get_node_matrix(best_object, node), &local, &world);
		/* (a node posed from garbage - 08b e8's Scarab under its twist before
		track_pose, 57f50ccab - gave its gunners' cs_shoot_point points at
		1e38 and actor_looking.c's assert: no point but a finite one in the
		world) */
		if (!(fabsf(world.x) < 1.0e5f && fabsf(world.y) < 1.0e5f && fabsf(world.z) < 1.0e5f))
		{
			static long warnings;

			if (warnings++ < 8)
				platform_log("h2 ai: reference frame %d: %s's node %d gives %g %g %g - not used", (int)frame, tag_get_name(object_get(best_object)->definition_index),
					(int)node, (double)world.x, (double)world.y, (double)world.z);
			return FALSE;
		}
		*point = world;
	}
	return TRUE;
}

/* (h2_scripts.h) Halo 2's scripts' switch_bsp and structure_bsp_index on a
campaign level: its BSPs are this engine's in Halo 2's order (h2_level.c,
h2_campaign.h) */
H2_ALIGN static boolean bsp_switcher(short h2_bsp_index)
{
	struct scenario *scenario = global_scenario_index != NONE ? global_scenario_get() : NULL;

	if (!scenario || h2_bsp_index < 0 || h2_bsp_index >= scenario->structure_bsp_references.count)
		return FALSE;
	/* (as this engine's switch_bsp: asked of the main loop, which switches
	between ticks - a checkpoint asked for before it saves the BSP it was
	asked in, a revert forgets it) */
	/* (a tick later: a checkpoint the same script asked for - Halo 2's
	cinematic_skip_start saves, then its cutscene switches - is taken first,
	in the BSP it was asked in) */
	/* (no checkpoint to be taken at once before it: switched between this
	tick and the next, as Halo 2's (switch_bsp n) (sleep 1) expects - 08b's
	boss_setup teleports the players and places Tartarus a tick after;
	a safe game_save waits, taken in the BSP switched to) */
	if (!main_checkpoint_pending_immediate())
	{
		if (h2_bsp_index != global_structure_bsp_index)
			main_switch_structure_bsp(h2_bsp_index);
		if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
			platform_log("h2 ai: switch_bsp %d sent at tick %ld", h2_bsp_index, game_time_get());
		rt.pending_bsp = NONE;
		return TRUE;
	}
	rt.pending_bsp = h2_bsp_index;
	rt.pending_bsp_time = game_time_get();
	return TRUE;
}

H2_ALIGN static short bsp_current(void)
{
	return global_structure_bsp_index;
}

static void bsp_pending_update(void)
{
	if (rt.pending_bsp == NONE || !rt.map_name[0])
		return;
	if (game_time_get() < rt.pending_bsp_time)
	{
		/* (a revert since: the request forgotten, as this engine's are) */
		rt.pending_bsp = NONE;
		return;
	}
	if (game_time_get() > rt.pending_bsp_time && (!main_checkpoint_pending_immediate() || game_time_get() > rt.pending_bsp_time + 60))
	{
		if (rt.pending_bsp != global_structure_bsp_index)
			main_switch_structure_bsp(rt.pending_bsp);
		if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
			platform_log("h2 ai: switch_bsp %d asked at tick %ld, sent at tick %ld (checkpoint pending %d)", rt.pending_bsp,
				rt.pending_bsp_time, game_time_get(), main_checkpoint_pending());
		rt.pending_bsp = NONE;
	}
}

/* (a squad's or location's vehicle: Halo 2's scenario vehicle palette, 0x78
- 0x28 each) this engine's vehicle h2_vehicles.c made of it
("h2\\vehicles\\<its name>": a Warthog, a Ghost, the Pelican...), NONE if
none was */
H2_ALIGN static long vehicle_tag_of(struct h2_cache *cache, struct h2_cache_tag const *scenario, short palette_index)
{
	long count, tag;
	unsigned long palette;
	char const *name, *last;
	char ce_name[96];

	if (palette_index < 0 || !h2_cache_block(cache, scenario->address + 0x78, 0x28, &count, &palette) || palette_index >= count ||
		(tag = h2_cache_reference(cache, palette + 0x28 * (unsigned long)palette_index)) == NONE)
	{
		return NONE;
	}
	name = h2_cache_tag_name(cache, tag);
	last = strrchr(name, '\\');
	/* (the campaign's plasma cannon: h2_level.c's alias, c_turret_mp) */
	{
		extern char const *h2_level_vehicle_alias(char const *last);

		snprintf(ce_name, sizeof(ce_name), "h2\\vehicles\\%s", h2_level_vehicle_alias(last ? last + 1 : name));
	}
	if (strstr(ce_name, "_no_flare"))
		*strstr(ce_name, "_no_flare") = 0;
	return tag_loaded('vehi', ce_name);
}

/* (a squad's or starting location's placement script: its name, 32
characters, and its index in the scenario's scripts - 0x1B8, 0x28 each, the
type at 0x20 - kept where the name is not empty and the script is a command
script; Halo 2's squad_valid_command_script. This engine's scripts are
Halo 2's in their order, h2_scripts.c) */
H2_ALIGN static short command_script_valid(struct h2_cache *cache, struct h2_cache_tag const *scenario, char const *name, short index)
{
	long count;
	unsigned long scripts;
	unsigned char const *script;

	if (!name[0] || index < 0 || !h2_cache_block(cache, scenario->address + 0x1B8, 0x28, &count, &scripts) || index >= count ||
		!(script = h2_cache_at(cache, scripts + 0x28 * (unsigned long)index, 0x28)) || get16(script + 0x20) != 5)
	{
		return NONE;
	}
	return index;
}

H2_ALIGN void h2_ai_map_loaded(
	char const *map_name)
{
	char const *directory = map_name ? h2_maps_directory_of(map_name) : cache_files_map_directory();
	struct h2_cache *cache;
	struct h2_cache_tag scenario;
	char why[160];
	long count, i;
	unsigned long elements;

	h2_ai_dispose_from_old_map();
	if (!map_name || !directory || !h2_level_is_halo2_map(directory, map_name))
		return;
	cache = h2_cache_open(directory, map_name, why, sizeof(why));
	if (!cache)
		return;
	if (!strstr(h2_cache_scenario_path(cache), "\\solo\\") || !h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario))
	{
		h2_cache_close(cache);
		return;
	}
	if (h2_cache_block(cache, scenario.address + H2S_SQUADS, H2_SQUAD_SIZE, &count, &elements) && count > 0)
	{
		rt.squad_count = (short)MIN(count, MAXIMUM_SQUADS);
		rt.squads = calloc((size_t)rt.squad_count, sizeof(*rt.squads));
		for (i = 0; rt.squads && i < rt.squad_count; i++)
		{
			struct rt_squad *s = &rt.squads[i];
			unsigned char squad[H2_SQUAD_SIZE];
			unsigned char const *p = h2_cache_at(cache, elements + H2_SQUAD_SIZE * i, H2_SQUAD_SIZE);
			long location_count, k;
			unsigned long locations;

			s->encounter = s->squad = NONE;
			s->order = NONE;
			if (!p)
				continue;
			memcpy(squad, p, sizeof(squad));
			memcpy(s->name, squad, 31);
			s->parent = get16(squad + 0x26);
			s->initial_order = get16(squad + 0x42);
			location_count = (long)get32(squad + 0x48);
			locations = get32(squad + 0x4C);
			if (location_count < 0 || location_count > 256)
				location_count = 0;
			s->location_count = (short)MIN(location_count, MAXIMUM_CE_STARTING_LOCATIONS);
			s->last_vehicle = NONE;
			for (k = 0; k < MAXIMUM_CE_STARTING_LOCATIONS; k++)
				s->location_objects[k] = NONE;
			s->script = command_script_valid(cache, &scenario, (char const *)squad + 0x50, get16(squad + 0x70));
			for (k = 0; k < s->location_count; k++)
			{
				unsigned char const *l = h2_cache_at(cache, locations + H2_STARTING_LOCATION_SIZE * k, H2_STARTING_LOCATION_SIZE);

				s->location_vehicles[k] = NONE;
				s->location_seats[k] = 7;
				s->location_priorities[k] = 0;
				s->location_swarms[k] = 0;
				s->location_frames[k] = NONE;
				s->location_scripts[k] = l ? command_script_valid(cache, &scenario, (char const *)l + 0x40, get16(l + 0x60)) : NONE;
				if (l)
				{
					short vehicle = get16(l + 0x28);

					s->location_names[k] = get32(l);
					memcpy(&s->locations[k], l + 4, 12);
					memcpy(&s->location_yaws[k], l + 0x14, 4);
					s->location_seats[k] = (signed char)get16(l + 0x2A);
					s->location_priorities[k] = (get32(l + 0x1C) & 8) ? 100 : get16(l + 0x2A);
					s->location_nobody[k] = get16(l + 0x20) == NONE && get16(squad + 0x36) == NONE;
					if (vehicle == NONE)
						vehicle = get16(squad + 0x34);
					s->location_vehicles[k] = s->location_seats[k] != 7 ? vehicle_tag_of(cache, &scenario, vehicle) : NONE;
					s->location_frames[k] = get16(l + 0x10);
					s->location_swarms[k] = get16(l + 0x2E);
				}
			}
		}
		if (!rt.squads)
			rt.squad_count = 0;
	}
	if (h2_cache_block(cache, scenario.address + H2S_SQUAD_GROUPS, H2_SQUAD_GROUP_SIZE, &count, &elements) && count > 0)
	{
		rt.group_count = (short)MIN(count, MAXIMUM_GROUPS);
		rt.groups = calloc((size_t)rt.group_count, sizeof(*rt.groups));
		for (i = 0; rt.groups && i < rt.group_count; i++)
		{
			unsigned char const *p = h2_cache_at(cache, elements + H2_SQUAD_GROUP_SIZE * i, H2_SQUAD_GROUP_SIZE);

			if (p)
			{
				memcpy(rt.groups[i].name, p, 31);
				rt.groups[i].parent = get16(p + 0x20);
				rt.groups[i].initial_order = get16(p + 0x22);
			}
		}
		if (!rt.groups)
			rt.group_count = 0;
	}
	/* the orders' endings and the triggers */
	if (h2_cache_block(cache, scenario.address + H2S_ORDERS, H2_ORDER_SIZE, &count, &elements) && count > 0)
	{
		rt.order_count = MIN(count, MAXIMUM_H2_ORDERS);
		rt.orders = calloc((size_t)rt.order_count, sizeof(*rt.orders));
		for (i = 0; rt.orders && i < rt.order_count; i++)
		{
			long ending_count, e;
			unsigned long endings;
			unsigned char const *name = h2_cache_at(cache, elements + H2_ORDER_SIZE * i, 32);

			if (name)
			{
				memcpy(rt.orders[i].name, name, 31);
				rt.orders[i].name[31] = 0;
			}
			/* (its flags, the status it holds its actors at, what it follows
			and its areas: h2_ai_firing_position_allowed) */
			{
				unsigned char const *order = h2_cache_at(cache, elements + H2_ORDER_SIZE * i, H2_ORDER_SIZE);
				long area_count = 0, a, zone_count = 0, trigger_count = 0, t;
				unsigned long areas = 0, zones = 0, triggers = 0;
				int set;

				if (order)
				{
					rt.orders[i].style = get16(order + 0x20);
					rt.orders[i].flags = get32(order + 0x24);
					rt.orders[i].force_status = get16(order + 0x28);
					rt.orders[i].follow_squad = get16(order + 0x4E);
					rt.orders[i].follow_radius = getf(order + 0x50);
				}
				{
					long special_count = 0;
					unsigned long specials = 0;

					rt.orders[i].special = NONE;
					if (h2_cache_block(cache, elements + H2_ORDER_SIZE * i + 0x6C, 4, &special_count, &specials) && special_count > 0)
						rt.orders[i].special = (long)read32(cache, specials);
				}
				for (set = 0; set < 2; set++)
				if (h2_cache_block(cache, elements + H2_ORDER_SIZE * i + (set ? 0x5C : 0x54), 8, &area_count, &areas) &&
					h2_cache_block(cache, scenario.address + H2S_ZONES, H2_ZONE_SIZE, &zone_count, &zones))
				{
					struct rt_area *out = set ? rt.orders[i].areas2 : rt.orders[i].areas;

					for (a = 0; a < area_count && a < RT_MAXIMUM_ORDER_AREAS; a++)
					{
						unsigned char const *entry = h2_cache_at(cache, areas + 8 * a, 8);
						short zone = entry ? get16(entry + 4) : (short)NONE, area = entry ? get16(entry + 6) : (short)NONE;
						long zone_area_count = 0;
						unsigned long zone_areas = 0;
						unsigned char const *block;

						if (zone >= 0 && zone < zone_count &&
							h2_cache_block(cache, zones + H2_ZONE_SIZE * zone + 0x30, H2_AREA_SIZE, &zone_area_count, &zone_areas) &&
							area >= 0 && area < zone_area_count &&
							(block = h2_cache_at(cache, zone_areas + H2_AREA_SIZE * area, H2_AREA_SIZE)) != NULL)
						{
							out[a].centre.x = getf(block + 0x24);
							out[a].centre.y = getf(block + 0x28);
							out[a].centre.z = getf(block + 0x2C);
							out[a].radius = getf(block + 0x34);
						}
						out[a].type = entry ? get16(entry) : 0;
						out[a].zone = zone;
						out[a].area = area;
						if (set)
							rt.orders[i].area2_count = (short)(a + 1);
						else
							rt.orders[i].area_count = (short)(a + 1);
					}
					if (area_count > RT_MAXIMUM_ORDER_AREAS)
						platform_log("h2 ai: order %s has %ld areas (%d kept)", rt.orders[i].name, area_count, (int)RT_MAXIMUM_ORDER_AREAS);
				}
				/* (the secondary set's trigger: its first element - 0xC: combination,
				dialogue type, triggers (8: flags, trigger) -, 0x202be0) */
				if (h2_cache_block(cache, elements + H2_ORDER_SIZE * i + 0x64, 0x0C, &trigger_count, &triggers) && trigger_count > 0)
				{
					unsigned char const *head = h2_cache_at(cache, triggers, 0x0C);
					long count;
					unsigned long list;

					if (head)
					{
						rt.orders[i].secondary_combination = get16(head);
						rt.orders[i].secondary_dialogue = get16(head + 2);
						count = (long)get32(head + 4);
						list = get32(head + 8);
						rt.orders[i].secondary_trigger_count = (short)MAX(0, MIN(count, (long)RT_MAXIMUM_ENDING_TRIGGERS));
						for (t = 0; t < rt.orders[i].secondary_trigger_count; t++)
						{
							unsigned char const *r = h2_cache_at(cache, list + 8 * t, 8);

							rt.orders[i].secondary_trigger_flags[t] = r ? get32(r) : 0;
							rt.orders[i].secondary_triggers[t] = r ? get16(r + 4) : NONE;
						}
					}
				}
			}
			if (!h2_cache_block(cache, elements + H2_ORDER_SIZE * i + 0x74, H2_ORDER_ENDING_SIZE, &ending_count, &endings))
				continue;
			rt.orders[i].ending_count = (short)MIN(ending_count, RT_MAXIMUM_ENDINGS);
			for (e = 0; e < rt.orders[i].ending_count; e++)
			{
				struct rt_ending *o = &rt.orders[i].endings[e];
				unsigned char ending[H2_ORDER_ENDING_SIZE];
				unsigned char const *p = h2_cache_at(cache, endings + H2_ORDER_ENDING_SIZE * e, H2_ORDER_ENDING_SIZE);
				long trigger_count, t;
				unsigned long triggers;

				if (!p)
					continue;
				memcpy(ending, p, sizeof(ending));
				o->next_order = get16(ending);
				o->combination = get16(ending + 2);
				o->delay = getf(ending + 4);
				o->dialogue_type = get16(ending + 8);
				trigger_count = (long)get32(ending + 0x0C);
				triggers = get32(ending + 0x10);
				if (trigger_count < 0)
					trigger_count = 0;
				o->trigger_count = (short)MIN(trigger_count, RT_MAXIMUM_ENDING_TRIGGERS);
				for (t = 0; t < o->trigger_count; t++)
				{
					unsigned char const *r = h2_cache_at(cache, triggers + 8 * t, 8);

					o->trigger_flags[t] = r ? get32(r) : 0;
					o->triggers[t] = r ? get16(r + 4) : NONE;
				}
			}
		}
		if (!rt.orders)
			rt.order_count = 0;
	}
	if (h2_cache_block(cache, scenario.address + H2S_TRIGGERS, H2_TRIGGER_SIZE, &count, &elements) && count > 0)
	{
		rt.trigger_count = MIN(count, 1024);
		rt.triggers = calloc((size_t)rt.trigger_count, sizeof(*rt.triggers));
		for (i = 0; rt.triggers && i < rt.trigger_count; i++)
		{
			struct rt_trigger *o = &rt.triggers[i];
			long condition_count, k;
			unsigned long conditions;

			o->combination = read16(cache, elements + H2_TRIGGER_SIZE * i + 0x24);
			o->flags = read32(cache, elements + H2_TRIGGER_SIZE * i + 0x20);
			{
				/* (its name 0, for ai_trigger_test) */
				unsigned char const *n = h2_cache_at(cache, elements + H2_TRIGGER_SIZE * i, 32);

				if (n)
					memcpy(o->name, n, 31);
			}
			if (!h2_cache_block(cache, elements + H2_TRIGGER_SIZE * i + 0x28, H2_CONDITION_SIZE, &condition_count, &conditions))
				continue;
			o->condition_count = (short)MIN(condition_count, RT_MAXIMUM_CONDITIONS);
			for (k = 0; k < o->condition_count; k++)
			{
				unsigned char condition[H2_CONDITION_SIZE];
				unsigned char const *p = h2_cache_at(cache, conditions + H2_CONDITION_SIZE * k, H2_CONDITION_SIZE);
				struct rt_condition *c = &o->conditions[k];

				if (!p)
					continue;
				memcpy(condition, p, sizeof(condition));
				c->rule = get16(condition);
				c->squad = get16(condition + 2);
				c->group = get16(condition + 4);
				c->a = get16(condition + 6);
				c->x = getf(condition + 8);
				c->volume = get16(condition + 0x0C);
				/* (its flags at 0x34, bit 0 "not": 0x10 is the exit
				condition script's name, 32 characters, 0x30 its runtime
				script index - read as the flags before, an odd script index
				turned the condition round: 03a's e1_cov_inf0_grunts_alerted,
				script 391, always true; generic_player_fired lost its "not") */
				c->flags = get32(condition + 0x34);
			}
		}
		if (!rt.triggers)
			rt.trigger_count = 0;
	}
	if (h2_cache_block(cache, scenario.address + H2S_SCENES, H2_SCENE_SIZE, &count, &elements) && count > 0)
	{
		rt.scene_table_count = (short)MIN(count, 256);
		rt.scene_table = calloc((size_t)rt.scene_table_count, sizeof(*rt.scene_table));
		for (i = 0; rt.scene_table && i < rt.scene_table_count; i++)
		{
			struct rt_scene *o = &rt.scene_table[i];
			unsigned long element = elements + H2_SCENE_SIZE * i, list;
			unsigned char const *p = h2_cache_at(cache, element, H2_SCENE_SIZE);
			long block_count, k;

			if (!p)
				continue;
			h2_cache_string(cache, get32(p), o->name, sizeof(o->name));
			o->flags = get32(p + 4);
			if (h2_cache_block(cache, element + 8, H2_SCENE_TRIGGER_SIZE, &block_count, &list) && block_count > 0)
			{
				unsigned long conditions;
				long condition_count;

				o->combine = read16(cache, list);
				if (h2_cache_block(cache, list + 4, H2_SCENE_CONDITION_SIZE, &condition_count, &conditions))
				{
					o->condition_count = (short)MIN(condition_count, 8);
					for (k = 0; k < o->condition_count; k++)
					{
						o->negated[k] = (read16(cache, conditions + H2_SCENE_CONDITION_SIZE * k) & 1) != 0;
						o->triggers[k] = read16(cache, conditions + H2_SCENE_CONDITION_SIZE * k + 4);
					}
				}
			}
			if (h2_cache_block(cache, element + 0x10, H2_SCENE_ROLE_SIZE, &block_count, &list))
			{
				o->role_count = (short)MIN(block_count, RT_MAXIMUM_SCENE_ROLES);
				for (k = 0; k < o->role_count; k++)
				{
					unsigned char const *role = h2_cache_at(cache, list + H2_SCENE_ROLE_SIZE * k, H2_SCENE_ROLE_SIZE);

					if (!role)
						continue;
					h2_cache_string(cache, get32(role), o->role_names[k], sizeof(o->role_names[k]));
					o->role_groups[k] = get16(role + 4);
				}
			}
		}
		if (!rt.scene_table)
			rt.scene_table_count = 0;
	}
	/* the scripts' point sets (scripting data 0x1D8: point sets at 0 - 0x30
	each: name, points 0x20 - 0x3C each: name, position 0x20) */
	{
		long data_count, set_count;
		unsigned long data, sets;

		if (h2_cache_block(cache, scenario.address + 0x1D8, 0x84, &data_count, &data) && data_count > 0 &&
			h2_cache_block(cache, data, 0x30, &set_count, &sets))
		{
			rt.point_set_count = (short)MIN(set_count, RT_MAXIMUM_POINT_SETS);
			for (i = 0; i < rt.point_set_count; i++)
			{
				long point_count, k;
				unsigned long points;

				if (!h2_cache_block(cache, sets + 0x30 * i + 0x20, 0x3C, &point_count, &points) || point_count <= 0 || point_count > 1024)
					continue;
				rt.point_sets[i].points = malloc(sizeof(real_point3d) * (size_t)point_count);
				rt.point_sets[i].frames = malloc(sizeof(short) * (size_t)point_count);
				if (!rt.point_sets[i].points || !rt.point_sets[i].frames)
					continue;
				rt.point_sets[i].count = point_count;
				for (k = 0; k < point_count; k++)
				{
					/* (position 0x20, reference frame 0x2C) */
					unsigned char const *p = h2_cache_at(cache, points + 0x3C * k + 0x20, 14);

					rt.point_sets[i].frames[k] = NONE;
					if (p)
					{
						memcpy(&rt.point_sets[i].points[k], p, 12);
						rt.point_sets[i].frames[k] = get16(p + 12);
					}
				}
			}
		}
	}
	variants_read(cache);
	dialogue_read(cache);
	frames_read(cache, scenario.address);
	deck_read(cache, scenario.address);
	zone_positions_read(cache, scenario.address);
	/* (each weapon's AI scariness: h2_ai_scary_target_retreat) */
	{
		long n = h2_cache_tag_count(cache), k;

		for (k = 0; k < n && rt.weapon_scariness_count < RT_MAXIMUM_WEAPON_SCARINESS; k++)
		{
			struct h2_cache_tag t;
			char const *name;

			if (!h2_cache_tag_get(cache, k, &t) || t.group_tag != 'weap' || !(name = h2_cache_tag_name(cache, k)))
				continue;
			snprintf(rt.weapon_scariness[rt.weapon_scariness_count].name, sizeof(rt.weapon_scariness[0].name), "%s", name);
			rt.weapon_scariness[rt.weapon_scariness_count].value = readf(cache, t.address + H2_WEAPON_AI_SCARINESS);
			if (!(rt.weapon_scariness[rt.weapon_scariness_count].value >= 0.0f && rt.weapon_scariness[rt.weapon_scariness_count].value < 100.0f))
				rt.weapon_scariness[rt.weapon_scariness_count].value = 0.0f;
			rt.weapon_scariness[rt.weapon_scariness_count].ce_tag = NONE;
			if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG_WEAPONS"))
			{
				unsigned char const *raw = h2_cache_at(cache, t.address + 0x220, 0x30);
				char hex[200] = "";
				int b;

				for (b = 0; raw && b < 0x30; b++)
					snprintf(hex + strlen(hex), sizeof(hex) - strlen(hex), "%02x", raw[b]);
				platform_log("h2 ai: weapon raw %s 0x220: %s", name, hex);
			}
			if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
				platform_log("h2 ai: weapon %s: AI scariness %.3f (0x238 %.3f 0x23C %.3f 0x240 %.3f 0x244 %.3f)", name,
					rt.weapon_scariness[rt.weapon_scariness_count].value, readf(cache, t.address + 0x238), readf(cache, t.address + 0x23C),
					readf(cache, t.address + 0x240), readf(cache, t.address + 0x244));
			rt.weapon_scariness_count++;
		}
		platform_log("h2 ai: %s: player scariness %.2f, berserking actor %.2f, %d weapons' AI scariness", map_name,
			rt.player_scariness, rt.berserk_scariness, (int)rt.weapon_scariness_count);
	}
	nav_read(cache);
	flood_read(cache);
	flood_feign_read(cache);
	/* the styles (Halo 2's behaviour switches): the scenario's style palette (0x150, tag references) for the orders'
	style index (order 0x20), and each character's own (character 0x1C, else its parent's); a style's five behaviour
	bitfields at 0x38 */
	{
		long n, k, count = h2_cache_tag_count(cache);
		unsigned long refs;

		memset(rt.styles, 0, sizeof(rt.styles));
		if (h2_cache_block(cache, scenario.address + 0x150, 8, &n, &refs))
		{
			for (k = 0; k < n && k < (long)NUMBEROF(rt.styles); k++)
			{
				long style = h2_cache_tag_from_handle(cache, read32(cache, refs + 8 * k + 4));
				struct h2_cache_tag t;
				short b;

				if (style != NONE && h2_cache_tag_get(cache, style, &t) && t.group_tag == 'styl')
				{
					for (b = 0; b < 5; b++)
						rt.styles[k].bits[b] = read32(cache, t.address + 0x38 + 4 * b);
					rt.styles[k].valid = TRUE;
					rt.styles[k].special = style_special_movement(cache, &t);
				}
			}
		}
		rt.character_style_count = 0;
		rt.path_character_count = 0;
		for (k = 0; k < count && rt.character_style_count < (short)NUMBEROF(rt.character_styles); k++)
		{
			struct h2_cache_tag t, st;
			long c = k, style = NONE;
			short depth, b;

			if (!h2_cache_tag_get(cache, k, &t) || t.group_tag != 'char')
				continue;
			/* (its path settings: its own style - not its parent's, 0x1f9240 reads the
			character's -, the movement properties up its chain) */
			if (rt.path_character_count < (short)NUMBEROF(rt.path_characters))
			{
				long own = h2_cache_tag_from_handle(cache, read32(cache, t.address + 0x1C + 4)), chain[MAXIMUM_INHERITANCE], at = k;
				short chain_length = 0;
				unsigned long movement;
				struct h2_cache_tag ot;

				while (at != NONE && chain_length < MAXIMUM_INHERITANCE)
				{
					struct h2_cache_tag pt;
					short q;

					for (q = 0; q < chain_length && chain[q] != at; q++)
						;
					if (q < chain_length || !h2_cache_tag_get(cache, at, &pt) || pt.group_tag != 'char')
						break;
					chain[chain_length++] = at;
					at = h2_cache_reference(cache, pt.address + H2C_PARENT);
				}
				movement = property_find(cache, chain, chain_length, H2C_MOVEMENT, H2C_MOVEMENT_SIZE);
				snprintf(rt.path_characters[rt.path_character_count].name, sizeof(rt.path_characters[0].name), "%s",
					last_part(h2_cache_tag_name(cache, k)));
				rt.path_characters[rt.path_character_count].special = own != NONE && h2_cache_tag_get(cache, own, &ot) && ot.group_tag == 'styl' ?
					(style_special_movement(cache, &ot) == NONE ? 0 : (long)((unsigned long)style_special_movement(cache, &ot) << 1)) : 0xFF;
				rt.path_characters[rt.path_character_count].jump_height = movement ? (short)(h2_cache_read32(cache, movement + 0x18) >> 16) : 0;
				rt.path_characters[rt.path_character_count].movement_hints = movement ? h2_cache_read32(cache, movement + 0x1C) : 0;
				rt.path_characters[rt.path_character_count].radius = readf_or(cache, movement ? movement + 0x04 : 0, 0.25f);
				if (getenv("HALO_H2_AI_DEBUG"))
					platform_log("h2 ai: path settings %s: special %lx jump height %d hints %lx", rt.path_characters[rt.path_character_count].name,
						(unsigned long)rt.path_characters[rt.path_character_count].special, (int)rt.path_characters[rt.path_character_count].jump_height,
						rt.path_characters[rt.path_character_count].movement_hints);
				rt.path_character_count++;
			}
			for (depth = 0; depth < 4 && c != NONE && style == NONE; depth++)
			{
				struct h2_cache_tag ct;

				if (!h2_cache_tag_get(cache, c, &ct))
					break;
				style = h2_cache_tag_from_handle(cache, read32(cache, ct.address + 0x1C + 4));
				c = h2_cache_tag_from_handle(cache, read32(cache, ct.address + 0x04 + 4));
			}
			if (style == NONE || !h2_cache_tag_get(cache, style, &st) || st.group_tag != 'styl')
				continue;
			snprintf(rt.character_styles[rt.character_style_count].name, sizeof(rt.character_styles[0].name), "%s",
				last_part(h2_cache_tag_name(cache, k)));
			for (b = 0; b < 5; b++)
				rt.character_styles[rt.character_style_count].bits[b] = read32(cache, st.address + 0x38 + 4 * b);
			rt.character_styles[rt.character_style_count++].valid = TRUE;
		}
		platform_log("h2 ai: %s: %ld styles in the palette, %d characters with a style", map_name, (long)n, (int)rt.character_style_count);
	}
	/* the AI globals' dangers (globals\\globals: its ai globals block at 0xC8 in MCC's layout; an actor's danger as it
	sees an enemy face it, shoot near it, at it, come extremely close - at 0, 8, 0x10, 0x18 - and as it takes shield
	damage, more of it, body damage, more of it - at 0x20, 0x24, 0x28, 0x2C: retail 0x267c50's table) */
	{
		static real const defaults[8] = { 2.0f, 4.0f, 6.0f, 6.5f, 7.0f, 8.0f, 9.0f, 10.0f };
		long matg = h2_cache_tag_find(cache, 'matg', "globals\\globals");
		struct h2_cache_tag t;
		long n, d;
		unsigned long ai_globals;
		boolean sane = FALSE;

		static unsigned short const offsets[8] = { 0x00, 0x08, 0x10, 0x18, 0x20, 0x24, 0x28, 0x2C };

		if (matg != NONE && h2_cache_tag_get(cache, matg, &t) && h2_cache_block(cache, t.address + 0xC8, 0x30, &n, &ai_globals) && n > 0)
		{
			sane = TRUE;
			for (d = 0; d < 8; d++)
			{
				rt.dangers[d] = readf(cache, ai_globals + offsets[d]);
				if (!(rt.dangers[d] >= 0.0f && rt.dangers[d] < 100.0f) || (d && rt.dangers[d] < rt.dangers[d - 1]))
					sane = FALSE;
			}
		}
		if (sane)
		{
			if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG_WEAPONS"))
				platform_log("h2 ai: ai globals 0x150..0x168: %.3f %.3f %.3f %.3f %.3f %.3f", readf(cache, ai_globals + 0x150),
					readf(cache, ai_globals + 0x154), readf(cache, ai_globals + 0x158), readf(cache, ai_globals + 0x15C),
					readf(cache, ai_globals + 0x160), readf(cache, ai_globals + 0x164));
			rt.player_scariness = readf(cache, ai_globals + 0x160);
			rt.berserk_scariness = readf(cache, ai_globals + 0x164);
			if (!(rt.player_scariness >= 0.0f && rt.player_scariness < 100.0f))
				rt.player_scariness = 0.0f;
			if (!(rt.berserk_scariness >= 0.0f && rt.berserk_scariness < 100.0f))
				rt.berserk_scariness = 0.0f;
		}
		if (!sane)
			memcpy(rt.dangers, defaults, sizeof(defaults));
		rt.dangers_read = TRUE;
		platform_log("h2 ai: %s: dangers %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f%s", map_name, rt.dangers[0], rt.dangers[1], rt.dangers[2],
			rt.dangers[3], rt.dangers[4], rt.dangers[5], rt.dangers[6], rt.dangers[7], sane ? "" : " (not read: Halo 2's own)");
	}
	/* the vocalizations' gestures (the dialogue globals, ai\\ai_dialogue_globals: its vocalizations block at 0, 0x60
	each - the name's string id at 0, the animation impulse at 0x18) */
	rt.gesture_count = 0;
	{
		long adlg = h2_cache_tag_find(cache, 'adlg', "ai\\ai_dialogue_globals");
		struct h2_cache_tag t;
		long n, v;
		unsigned long vocalizations;

		if (adlg != NONE && h2_cache_tag_get(cache, adlg, &t) && h2_cache_block(cache, t.address, 0x60, &n, &vocalizations))
		{
			for (v = 0; v < n && rt.gesture_count < (short)NUMBEROF(rt.gestures); v++)
			{
				short impulse = read16(cache, vocalizations + 0x60 * v + 0x18);

				if (impulse > 0 && h2_cache_string(cache, read32(cache, vocalizations + 0x60 * v),
					rt.gestures[rt.gesture_count].name, (long)sizeof(rt.gestures[0].name)))
				{
					rt.gestures[rt.gesture_count++].impulse = impulse;
				}
			}
		}
		platform_log("h2 ai: %s: %d vocalizations with a gesture", map_name, (int)rt.gesture_count);
	}
	h2_cache_close(cache);
	for (i = 0; i < RT_MAXIMUM_ACTORS; i++)
		rt.actor_location[i] = NONE, rt.actor_disposable[i] = NONE;
	for (i = 0; i < rt.squad_count; i++)
		rt.squads[i].enemy_close_time = NONE, rt.squads[i].dispose_pending = FALSE, rt.squads[i].actor_count = 0;
	for (i = 0; i < RT_MAXIMUM_ACTORS; i++)
		rt.member_actor[i] = NONE, rt.member_squad[i] = NONE;
	for (i = 0; i < RT_MAXIMUM_COMMAND_SCRIPTS; i++)
		rt.command_scripts[i].actor_index = NONE;
	snprintf(rt.map_name, sizeof(rt.map_name), "%s", map_name);
	memset(rt.actor_charging, 0, sizeof(rt.actor_charging));
	ram_reset();
	platform_log("h2 ai: %s: berserk: Halo 2's charge trigger (0x1aa990) ported; its last-of-type (0x1a9bc0), 0x1a9c50 and "
		"behaviour 0x42 triggers are this engine's berserk rules, a stand-in until they are decompiled", map_name);
	rt.pending_bsp = NONE;
	h2_scripts_set_bsp_switcher(bsp_switcher, bsp_current);
	rt.loaded = rt.squad_count > 0;
	h2_ai_loaded = rt.loaded;
	rt.cs_in_actor = HALO_ENV_ONCE("HALO_H2_CS_IN_ACTOR") && atoi(HALO_ENV_ONCE("HALO_H2_CS_IN_ACTOR")) != 0;
	/* (default off: see h2_ai_actors_activation_by_location's stand-in) */
	rt.actor_activation = HALO_ENV_ONCE("HALO_H2_ACTOR_ACTIVATION") && atoi(HALO_ENV_ONCE("HALO_H2_ACTOR_ACTIVATION")) != 0;
	if (rt.loaded && rt.actor_activation)
		platform_log("h2 ai: %s: Halo 2's actor activation: every actor of the loaded BSP is updated, none put to sleep out of the players' sight (HALO_H2_ACTOR_ACTIVATION)", map_name);
	rt.cs_actor_runs = rt.cs_actor_ends = rt.cs_unrun_ticks = rt.cs_rest_runs = rt.cs_actor_aborts = 0;
	rt.cs_action_steps = rt.cs_actions_done = 0;
	rt.encounters_relocated = rt.actors_relocated = rt.actors_out_of_bsp = 0;
	memset(rt.actor_bsp_datum, 0xff, sizeof(rt.actor_bsp_datum));
	if (rt.loaded && rt.cs_in_actor)
		platform_log("h2 ai: %s: command scripts run in their actor's update (HALO_H2_CS_IN_ACTOR, 0x258660), not the scheduler", map_name);
	if (rt.loaded)
		platform_log("h2 ai: %s: %d squads, %d groups, %ld orders, %ld triggers, %d point sets read", map_name, rt.squad_count,
			rt.group_count, rt.order_count, rt.trigger_count, rt.point_set_count);
}

/* the encounters' squads matched to Halo 2's (the builder's marks) */
H2_ALIGN static boolean table_build(void)
{
	struct scenario *scenario;
	short e, q, matched = 0;

	if (rt.table_built)
		return TRUE;
	if (!rt.loaded || global_scenario_index == NONE || !(scenario = global_scenario_get()))
		return FALSE;
	for (e = 0; e < 128; e++)
	{
		rt.resolved_bsp[e] = NONE;
		for (q = 0; q < MAXIMUM_SQUADS_PER_ENCOUNTER_H2; q++)
			rt.squad_of[e][q] = NONE;
	}
	for (e = 0; e < scenario->ai_encounters.count && e < 128; e++)
	{
		struct encounter_definition *encounter = TAG_BLOCK_GET_ELEMENT(&scenario->ai_encounters, e, struct encounter_definition);

		for (q = 0; q < encounter->squads.count && q < MAXIMUM_SQUADS_PER_ENCOUNTER_H2; q++)
		{
			struct squad_definition *squad = TAG_BLOCK_GET_ELEMENT(&encounter->squads, q, struct squad_definition);
			long h2 = (long)squad->unused1[1];

			if (squad->unused1[0] != 'h2sq' || h2 < 0 || h2 >= rt.squad_count)
				continue;
			rt.squads[h2].encounter = e;
			rt.squads[h2].squad = q;
			rt.squad_of[e][q] = (short)h2;
			matched++;
		}
	}
	rt.table_built = TRUE;
	platform_log("h2 ai: %d of %d squads in %d encounters", matched, rt.squad_count, (int)scenario->ai_encounters.count);
	return TRUE;
}

/* ---------- the runtime: ai indices */

/* this engine's reference to Halo 2's squad (NONE: not made) */
static long squad_reference(short squad)
{
	if (squad < 0 || squad >= rt.squad_count || rt.squads[squad].encounter == NONE)
		return NONE;
	return ((long)CE_REFERENCE_SQUAD << 30) | ((long)rt.squads[squad].squad << 16) | rt.squads[squad].encounter;
}

static boolean group_contains(short group, short squad)
{
	short at = rt.squads[squad].parent, depth = 0;

	while (at >= 0 && at < rt.group_count && depth++ < 64)
	{
		if (at == group)
			return TRUE;
		at = rt.groups[at].parent;
	}
	return FALSE;
}

/* the squads an ai index names (Halo 2's indices): how many */
static long current_actor(void);
static boolean actor_alive(long actor_index);

/* the Halo 2 squad an actor is of (NONE) */
static short actor_h2_squad(long actor_index)
{
	struct actor_datum *actor = actor_alive(actor_index) ? actor_get(actor_index) : NULL;
	short e = actor && actor->meta.encounter_index != NONE ? (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(actor->meta.encounter_index) : NONE;

	return e >= 0 && e < 128 && actor->meta.squad_index >= 0 && actor->meta.squad_index < MAXIMUM_SQUADS_PER_ENCOUNTER_H2 ?
		rt.squad_of[e][actor->meta.squad_index] : NONE;
}

/* (an RT_AI_ACTOR value) its actor's datum index (NONE: gone) */
static long ai_actor_of(long ai_index)
{
	struct data_iterator iterator;

	if (((unsigned long)ai_index & 0xFFFF0000UL) != ((unsigned long)RT_AI_ACTOR & 0xFFFF0000UL))
		return NONE;
	data_iterator_new(&iterator, actor_data);
	while (data_iterator_next(&iterator))
	{
		if (DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index) == (ai_index & 0xFFFF))
			return iterator.datum_index;
	}
	return NONE;
}

static short squads_of(long ai_index, short *out, short room)
{
	unsigned long type = (unsigned long)ai_index >> 30;
	short count = 0, s;

	if (ai_index == NONE || !table_build())
		return 0;
	if (ai_index == RT_AI_CURRENT_ACTOR || ai_index == RT_AI_CURRENT_SQUAD || ai_actor_of(ai_index) != NONE)
	{
		s = actor_h2_squad(ai_index == RT_AI_CURRENT_ACTOR || ai_index == RT_AI_CURRENT_SQUAD ? current_actor() : ai_actor_of(ai_index));
		if (s != NONE && room > 0)
			out[count++] = s;
		return count;
	}
	switch (type)
	{
	case 0:
		s = (short)(ai_index & 0xFFFF);
		if (s < rt.squad_count && room > 0)
			out[count++] = s;
		break;
	case 1:
		for (s = 0; s < rt.squad_count && count < room; s++)
		{
			if (group_contains((short)(ai_index & 0xFFFF), s))
				out[count++] = s;
		}
		break;
	case 3:
		s = (short)(((unsigned long)ai_index >> 16) & 0x3FFF);
		if (s < rt.squad_count && room > 0)
			out[count++] = s;
		break;
	default:
		break;
	}
	return count;
}

/* the actors an ai index names (their datum indices): how many. A
starting location names the actor placed there, an actor index itself */
static short actors_of(long ai_index, long *out, short room)
{
	unsigned long type = (unsigned long)ai_index >> 30;
	short squads[MAXIMUM_SQUADS], squad_count, i, count = 0;

	if (ai_index == NONE || !table_build())
		return 0;
	/* (ai_current_actor: the evaluating command script's actor; object_get_ai's
	actor) */
	if (ai_index == RT_AI_CURRENT_ACTOR || ai_actor_of(ai_index) != NONE)
	{
		long actor_index = ai_index == RT_AI_CURRENT_ACTOR ? current_actor() : ai_actor_of(ai_index);

		if (actor_index == NONE || !actor_alive(actor_index) || room < 1)
			return 0;
		out[0] = actor_index;
		return 1;
	}
	if (ai_index == RT_AI_CURRENT_SQUAD)
	{
		short s = actor_h2_squad(current_actor());

		return s != NONE ? actors_of(s, out, room) : 0;
	}
	/* (an actor's own index: Halo 2's actors are not this engine's - none
	of the scripts' constants is one) */
	if (type == 2)
		return 0;
	squad_count = squads_of(ai_index, squads, MAXIMUM_SQUADS);
	for (i = 0; i < squad_count && count < room; i++)
	{
		long reference = squad_reference(squads[i]);
		struct ai_script_actor_reference_iterator iterator;
		struct actor_datum *actor;

		if (reference == NONE)
			continue;
		ai_index_actor_iterator_new(reference, &iterator);
		while ((actor = ai_index_actor_iterator_next(&iterator)) != NULL && count < room)
		{
			if (type == 3)
			{
				short location = (short)(ai_index & 0xFFFF);
				long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.actor_index);

				if (absolute >= RT_MAXIMUM_ACTORS || rt.actor_location[absolute] != location || !actor_alive(iterator.actor_index))
					continue;
			}
			out[count++] = iterator.actor_index;
			/* (a starting location names one actor: the first of its squad's
			placed there - halo2-decompiled unknown_272b70.cpp 0x272b70; a dead
			one's actor is gone in Halo 2, so it names none) */
			if (type == 3)
				return count;
		}
	}
	return count;
}

/* whether an ai index names one actor, not squads: an actor (Halo 2's type
2 - ai_current_actor, object_get_ai's) or a starting location (type 3,
"squad/name"). halo2-decompiled unknown_272b70.cpp: the counts (0x273f30,
0x274090), ai_erase (0x2736c0), ai_kill (0x273670), ai_vehicle_exit
(0x274da0) and ai_migrate (0x274140) act on that actor alone; ai_spawn_count's
squad walk (0x275d70) finds no squad in it */
static boolean ai_index_names_actor(long ai_index)
{
	unsigned long type = (unsigned long)ai_index >> 30;

	return ai_index != NONE && ai_index != RT_AI_CURRENT_SQUAD && (type == 2 || type == 3);
}

static short player_units(long *out, short room);

static void describe_object(long object_index, char *text, long size);

/* ---------- the runtime: placing */

static boolean squad_order_set(short h2_squad, short order);

/* a point's cluster and the walkable surface under it (the structure's
collision surface a little above to a metre below: Halo 2's points stand
on the ground, as tool's raycast for this engine's own) */
static void point_resolve(real_point3d const *point, short *cluster_index, long *surface_index)
{
	struct location location;
	struct collision_result collision;
	real_point3d start = *point;
	real_vector3d down = { 0.0f, 0.0f, -1.5f };

	start.z += 0.5f;
	scenario_location_from_point(&location, point);
	*cluster_index = location.cluster_index;
	*surface_index = NONE;
	if (collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit), &start, &down,
		NONE, &collision))
	{
		*surface_index = collision.surface_index;
		if (*cluster_index == NONE)
			*cluster_index = collision.location.cluster_index;
		return;
	}
	/* (a point on Halo 2's instanced geometry - an object here: the
	structure's floor under it stands in, as bipeds.c's pathfinding surface
	does for an actor standing there) */
	down.k = -4.0f;
	if (collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit), &start, &down,
		NONE, &collision))
	{
		*surface_index = collision.surface_index;
		if (*cluster_index == NONE)
			*cluster_index = collision.location.cluster_index;
	}
}

/* an encounter made ready to place its squads in the loaded BSP: placed in
it (Halo 2's squads are placed where the scripts are playing), its starting
locations' and firing positions' clusters and surfaces this BSP's */
H2_ALIGN static void encounter_prepare(short encounter_index)
{
	struct scenario *scenario = global_scenario_get();
	struct encounter_definition *encounter;
	short bsp = global_structure_bsp_index, q;
	long k, unresolved = 0, linked = 0;

	if (encounter_index < 0 || encounter_index >= scenario->ai_encounters.count || encounter_index >= 128)
		return;
	encounter = TAG_BLOCK_GET_ELEMENT(&scenario->ai_encounters, encounter_index, struct encounter_definition);
	if (encounter->runtime_structure_bsp_reference_index != bsp)
	{
		encounter->runtime_structure_bsp_reference_index = bsp;
		encounter->manual_structure_bsp_reference_index = bsp;
	}
	if (rt.resolved_bsp[encounter_index] == bsp)
		return;
	rt.resolved_bsp[encounter_index] = bsp;
	for (k = 0; k < encounter->firing_positions.count; k++)
	{
		struct firing_position_definition *f = TAG_BLOCK_GET_ELEMENT(&encounter->firing_positions, k,
			struct firing_position_definition);

		/* (one in a machine's frame: carried by it, deck_carry) */
		if (deck_link(encounter_index, (short)k, f))
		{
			linked++;
			continue;
		}
		point_resolve(&f->position, &f->cluster_index, &f->surface_index);
		unresolved += f->cluster_index == NONE || f->surface_index == NONE;
	}
	for (q = 0; q < encounter->squads.count; q++)
	{
		struct squad_definition *squad = TAG_BLOCK_GET_ELEMENT(&encounter->squads, q, struct squad_definition);

		for (k = 0; k < squad->starting_locations.count; k++)
		{
			struct actor_starting_location *l = TAG_BLOCK_GET_ELEMENT(&squad->starting_locations, k, struct actor_starting_location);
			long surface;

			point_resolve(&l->position, &l->cluster_index, &surface);
		}
	}
	if (unresolved)
		platform_log("h2 ai: %s: %ld of %ld firing positions not on this BSP's (%d) ground", encounter->name, unresolved,
			(long)encounter->firing_positions.count, bsp);
	if (linked)
		platform_log("h2 ai: %s: %ld of %ld firing positions in a machine's frame, carried by it", encounter->name, linked,
			(long)encounter->firing_positions.count);
}

/* the actors of an encounter now (absolute indices set in a bit vector) */
static void encounter_actors_mark(short encounter_index, unsigned long *bits)
{
	struct ai_script_actor_reference_iterator iterator;
	struct actor_datum *actor;

	memset(bits, 0, RT_MAXIMUM_ACTORS / 8);
	ai_index_actor_iterator_new(encounter_index, &iterator);
	while ((actor = ai_index_actor_iterator_next(&iterator)) != NULL)
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.actor_index);

		if (absolute < RT_MAXIMUM_ACTORS)
			bits[absolute / 32] |= 1UL << (absolute % 32);
	}
}

/* a squad placed (count NONE: as many as the difficulty's; location: one
starting location's actor alone, NONE: the squad's) */
/* (a placed actor at a location with a vehicle) a vehicle made there for it -
its driver's, a default's, or one without a driver -, or the squad's last
one ridden as its gunner or passenger; the seat's label Halo 2's
(h2_vehicles.c's seats) or the stand-in's */
H2_ALIGN static boolean seat_take(long unit_index, long vehicle_index, char const *const *labels)
{
	short seats[16], count, k;

	for (; *labels; labels++)
	{
		count = vehicle_scripting_find_available_seats(vehicle_index, *labels, NONE, seats, NUMBEROF(seats));
		for (k = 0; k < count; k++)
		{
			if (seats[k] != NONE && object_get(unit_index)->object.parent_object_index == NONE &&
				unit_enter_seat(unit_index, vehicle_index, seats[k]))
			{
				return TRUE;
			}
		}
	}
	return FALSE;
}

H2_ALIGN static void squad_vehicle_board(struct rt_squad *s, short location, long unit_index)
{
	static char const *const drivers[] = { "_d", "driver", NULL };
	static char const *const gunners[] = { "_g", "gunner", NULL };
	static char const *const passengers[] = { "_p", "passenger", "rider", NULL };
	static char const *const any[] = { "_d", "driver", "_g", "gunner", "_p", "passenger", NULL };
	static char const *const riders[] = { "_g", "gunner", "_p", "passenger", NULL };
	long definition = s->location_vehicles[location];
	signed char seat = s->location_seats[location];
	long vehicle = NONE;
	boolean seated = FALSE;

	if (definition == NONE || seat == 7 || unit_index == NONE || object_get(unit_index)->object.parent_object_index != NONE)
		return;
	/* (a driver, gunner or passenger: the vehicle this placement made last,
	where its seat is free - a Warthog's gunner placed before its driver
	rides with him) */
	if ((seat == 1 || seat == 2 || seat == 3) && s->last_vehicle != NONE && object_try_and_get(s->last_vehicle) &&
		object_get(s->last_vehicle)->definition_index == definition)
	{
		vehicle = s->last_vehicle;
		seated = seat == 3 ? seat_take(unit_index, vehicle, drivers) :
			seat_take(unit_index, vehicle, seat == 2 ? gunners : passengers) || seat_take(unit_index, vehicle, riders);
	}
	if (!seated)
	{
		struct object_placement_data placement;

		object_placement_data_new(&placement, definition, NONE);
		object_get_origin(unit_index, &placement.position);
		placement.position.z += 0.25f;
		placement.forward.i = (real)cos(s->location_yaws[location]);
		placement.forward.j = (real)sin(s->location_yaws[location]);
		placement.forward.k = 0.0f;
		placement.up.i = placement.up.j = 0.0f;
		placement.up.k = 1.0f;
		vehicle = object_new(&placement);
		if (vehicle == NONE)
			return;
		rt.vehicles_made++;
		s->last_vehicle = vehicle;
		/* (the squad's vehicle, its location's: Halo 2's squad vehicle list,
		each vehicle naming the starting location that made it - halo2-
		decompiled unknown_272b70.cpp 0x272c90; ai_erase deletes them) */
		s->location_objects[location] = vehicle;
		seated = seat_take(unit_index, vehicle, seat == 3 || seat == 0 ? drivers : seat == 2 ? gunners : seat == 1 ? passengers : riders) ||
			seat_take(unit_index, vehicle, any);
	}
	if (seated)
		rt.vehicles_boarded++;
	if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
		platform_log("h2 ai: %s location %d: %s %s (seat type %d)", s->name, location, tag_get_name(definition),
			seated ? "boarded" : "made, no seat taken", seat);
}

static boolean command_script_run(long actor_index, short script, long caller_thread);

/* (squad_place) how many units a starting location places: Halo 2's swarm count (0x2E) for a swarm character's location -
the infection forms, a swarm of that many there -, else one (its location's variant, else its squad's) */
static short location_place_count(struct rt_squad const *s, struct squad_definition const *squad, short location)
{
	struct actor_starting_location const *l;
	short palette;
	long variant;

	if (location < 0 || location >= squad->starting_locations.count || location >= s->location_count || s->location_swarms[location] <= 1)
		return 1;
	l = TAG_BLOCK_GET_ELEMENT(&squad->starting_locations, location, struct actor_starting_location);
	palette = l->actor_variant_index >= 0 ? l->actor_variant_index : squad->actor_palette_index;
	if (palette < 0 || palette >= global_scenario_get()->ai_actor_palette.count)
		return 1;
	variant = TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->ai_actor_palette, palette, struct tag_reference)->index;
	if (variant == NONE ||
		!TEST_FLAG(actor_definition_get(actor_variant_definition_get(variant)->actor_reference.index)->flags, _actor_definition_swarm_actor_bit))
	{
		return 1;
	}
	return s->location_swarms[location];
}

H2_ALIGN static void squad_place(short h2_squad, short count, short location)
{
	struct rt_squad *s = &rt.squads[h2_squad];
	long reference = squad_reference(h2_squad);
	struct encounter_definition *encounter;
	struct squad_definition *squad;
	unsigned long before[RT_MAXIMUM_ACTORS / 32];
	boolean location_taken[MAXIMUM_CE_STARTING_LOCATIONS];
	short minimum, maximum, placed = 0;
	struct ai_script_actor_reference_iterator iterator;
	struct actor_datum *actor;

	if (reference == NONE)
		return;
	encounter_prepare(s->encounter);
	encounter = TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->ai_encounters, s->encounter, struct encounter_definition);
	squad = TAG_BLOCK_GET_ELEMENT(&encounter->squads, s->squad, struct squad_definition);
	minimum = squad->min_count;
	maximum = squad->max_count;
	if (count >= 0)
		squad->min_count = squad->max_count = count;
	encounter_actors_mark(s->encounter, before);
	s->last_vehicle = NONE;
	memset(location_taken, 0, sizeof(location_taken));
	if (location != NONE)
	{
		/* (Halo 2's squad/location: that location's actor - the squad's
		locations the one, a moment) */
		struct tag_block locations = squad->starting_locations;

		if (location < 0 || location >= locations.count)
			return;
		squad->min_count = squad->max_count = location_place_count(s, squad, location);
		squad->starting_locations.count = 1;
		squad->starting_locations.address = TAG_BLOCK_GET_ELEMENT(&locations, location, struct actor_starting_location);
		encounter_create(s->encounter, NONE, s->squad);
		squad->starting_locations = locations;
	}
	else
	{
		/* (Halo 2 places its count one actor at a time, each at the location
		of the highest priority not taken yet - the "always place" flag
		(0x1C bit 3) 100, else the seat type (0x2A) -, ties at random, all of
		them free again once each was taken: halo2-decompiled
		unknown_2003a0.cpp 0x201520 (retail) -> 0x2039d0. The count: the
		script's, else the difficulty's (0x201a40's table, as encounter_create's
		min / mean / max). Not 1:1, flagged: a location whose emitter vehicle
		(0x3C) is gone is not blocked here (0x201460 / 0x201400), and the
		random tie draws this engine's random numbers) */
		struct tag_block locations = squad->starting_locations;
		unsigned long available[(MAXIMUM_CE_STARTING_LOCATIONS + 31) / 32];
		short n, total;

		switch (game_difficulty_level_get())
		{
		case _game_difficulty_level_easy:
		case _game_difficulty_level_normal:
			total = squad->min_count;
			break;
		case _game_difficulty_level_hard:
			total = (short)((squad->min_count + squad->max_count) / 2);
			break;
		default:
			total = squad->max_count;
			break;
		}
		memset(available, 0xFF, sizeof(available));
		for (n = 0; n < total && locations.count > 0; n++)
		{
			short k, best = NONE, ties = 0, choice, found = 0;

			for (k = 0; k < locations.count && k < s->location_count; k++)
			{
				if (!(available[k / 32] & (1UL << (k % 32))))
					continue;
				if (best == NONE || s->location_priorities[k] > best)
					best = s->location_priorities[k], ties = 1;
				else if (s->location_priorities[k] == best)
					ties++;
			}
			if (!ties)
			{
				memset(available, 0xFF, sizeof(available));
				n--;
				if (locations.count <= 0 || s->location_count <= 0)
					break;
				continue;
			}
			choice = ties > 1 ? (short)random_range(0, ties) : 0;
			for (k = 0; k < locations.count && k < s->location_count; k++)
			{
				if ((available[k / 32] & (1UL << (k % 32))) && s->location_priorities[k] == best && found++ == choice)
					break;
			}
			if (k >= locations.count || k >= s->location_count)
				break;
			available[k / 32] &= ~(1UL << (k % 32));
			squad->min_count = squad->max_count = location_place_count(s, squad, k);
			squad->starting_locations.count = 1;
			squad->starting_locations.address = TAG_BLOCK_GET_ELEMENT(&locations, k, struct actor_starting_location);
			encounter_create(s->encounter, NONE, s->squad);
			squad->starting_locations = locations;
			squad->min_count = minimum;
			squad->max_count = maximum;
			if (HALO_ENV_ONCE("HALO_H2_AI_INDEX_LOG"))
				platform_log("h2 ai: %s: actor %d of %d at location %d (priority %d, %d tied)", s->name, n + 1, total, k, best, ties);
		}
	}
	squad->min_count = minimum;
	squad->max_count = maximum;
	/* the new actors' starting locations: each at the one it stands at */
	ai_index_actor_iterator_new(reference, &iterator);
	while ((actor = ai_index_actor_iterator_next(&iterator)) != NULL)
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.actor_index);
		real_point3d origin;
		real best = 1.0e9f;
		short k, location = NONE;

		if (absolute >= RT_MAXIMUM_ACTORS || (before[absolute / 32] & (1UL << (absolute % 32))))
			continue;
		placed++;
		/* (a new actor is not disposable: halo2-decompiled ai.cpp 0x1dfb90) */
		rt.actor_disposable[absolute] = NONE;
		if (actor->meta.unit_index == NONE)
			continue;
		character_variant_apply(actor);
		object_get_origin(actor->meta.unit_index, &origin);
		for (k = 0; k < s->location_count; k++)
		{
			real dx = origin.x - s->locations[k].x, dy = origin.y - s->locations[k].y, dz = origin.z - s->locations[k].z;
			real d = dx * dx + dy * dy + dz * dz;

			if (d < best)
				best = d, location = k;
		}
		rt.actor_location[absolute] = location;
		/* (a location in a reference frame - Johnson in 01a's elevator - is
		written as Halo 2 keeps it, relative to the frame: the actor put
		where the frame's machine is) */
		if (location != NONE && s->location_frames[location] != NONE)
		{
			real_point3d at = s->locations[location];

			if (frame_transform(s->location_frames[location], &at))
			{
				object_set_position(actor->meta.unit_index, &at, NULL, NULL);
				if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
					platform_log("h2 ai: %s location %d in frame %d: at %.2f %.2f %.2f", s->name, location,
						s->location_frames[location], at.x, at.y, at.z);
			}
		}
		if (location != NONE)
		{
			squad_vehicle_board(s, location, actor->meta.unit_index);
			location_taken[location] = TRUE;
		}
		/* (its placement script: its location's, else its squad's - Halo 2
		runs it as the actor's command script when it is placed) */
		{
			short script = location != NONE && s->location_scripts[location] != NONE ? s->location_scripts[location] : s->script;

			if (script != NONE)
			{
				command_script_run(iterator.actor_index, script, NONE);
				rt.placement_scripts++;
			}
		}
		/* (debug: the weapon its variant gives and whether its unit's graph
		takes it, once a squad) */
		if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG") && placed == 1)
		{
			struct actor_variant_definition *variant = actor_variant_definition_get(actor->meta.variant_definition_index);
			struct unit_datum *u = unit_get(actor->meta.unit_index);
			struct unit_definition *ud = unit_definition_get(u->definition_index);
			struct animation_graph *graph = ud->object.animation_graph.index != NONE ?
				animation_graph_definition_get(ud->object.animation_graph.index) : NULL;
			char const *label = variant->ranged_combat.reference.index != NONE ?
				weapon_definition_get(variant->ranged_combat.reference.index)->weapon.label : "(none)";
			char types[512];
			short q, c, t;

			types[0] = 0;
			for (q = 0; graph && q < graph->unit_seats.count && q < 2; q++)
			{
				struct animation_graph_unit_seat *seat = TAG_BLOCK_GET_ELEMENT(&graph->unit_seats, q, struct animation_graph_unit_seat);

				snprintf(types + strlen(types), sizeof(types) - strlen(types), "[%s:", seat->label);
				for (c = 0; c < seat->weapon_classes.count; c++)
				{
					struct animation_graph_weapon_class *wc = TAG_BLOCK_GET_ELEMENT(&seat->weapon_classes, c,
						struct animation_graph_weapon_class);

					for (t = 0; t < wc->weapon_types.count; t++)
						snprintf(types + strlen(types), sizeof(types) - strlen(types), " %s",
							TAG_BLOCK_GET_ELEMENT(&wc->weapon_types, t, struct animation_graph_weapon_type)->label);
				}
				snprintf(types + strlen(types), sizeof(types) - strlen(types), "]");
			}
			platform_log("h2 ai: %s: weapon label '%s', the graph's %s", s->name, label, types);
		}
	}
	/* (its locations of a vehicle and no one: the vehicle alone, empty -
	Halo 2's "no driver" seat type, or a location with no character at all
	(04a's and 04b's ledge Banshees, seat type driver; 05a's free Ghosts); at
	most its count's when one is given) */
	{
		short k, made = 0, first = location != NONE ? location : 0, end = location != NONE ? location + 1 : s->location_count;

		for (k = first; k < end && k < s->location_count; k++)
		{
			struct object_placement_data placement;
			real_point3d at = s->locations[k];
			long vehicle;

			/* (an actor placed there: its vehicle squad_vehicle_board's) */
			if ((s->location_seats[k] != 6 && !s->location_nobody[k]) || s->location_vehicles[k] == NONE || location_taken[k] ||
				(count >= 0 && location == NONE && made >= count))
			{
				continue;
			}
			/* (a location placed again makes a new vehicle, its old one -
			taken by a player, flown away - left as it is, and the new one is
			the location's (ai_vehicle_get_from_starting_location): the H2EK
			tag test, 04a's ledge_banshees_01/left placed, its Banshee moved
			out of vol_stuck_10, placed again: a second Banshee in the volume,
			the lookup the new one. Kept to one, 04a's always_get_banshee_02
			found the player's Banshee out of the volume for good and placed
			the other side's again every few seconds: 232 times a run.
			HALO_H2_OLD_VEHICLE_PLACE=1: one a location, as before) */
			if (s->location_objects[k] != NONE && object_try_and_get(s->location_objects[k]) &&
				object_get(s->location_objects[k])->definition_index == s->location_vehicles[k] &&
				getenv("HALO_H2_OLD_VEHICLE_PLACE"))
			{
				continue;
			}
			if (s->location_frames[k] != NONE && !frame_transform(s->location_frames[k], &at))
				continue;
			object_placement_data_new(&placement, s->location_vehicles[k], NONE);
			placement.position = at;
			placement.position.z += 0.25f;
			placement.forward.i = (real)cos(s->location_yaws[k]);
			placement.forward.j = (real)sin(s->location_yaws[k]);
			placement.forward.k = 0.0f;
			placement.up.i = placement.up.j = 0.0f;
			placement.up.k = 1.0f;
			vehicle = object_new(&placement);
			s->location_objects[k] = vehicle;
			if (vehicle == NONE)
				continue;
			made++;
			rt.vehicles_made++;
			if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
				platform_log("h2 ai: %s location %d: %s placed, empty (no driver)", s->name, k, tag_get_name(s->location_vehicles[k]));
		}
	}
	if (placed && s->order == NONE)
	{
		short order = s->initial_order, at = s->parent, depth = 0;

		while (order == NONE && at >= 0 && at < rt.group_count && depth++ < 64)
		{
			order = rt.groups[at].initial_order;
			at = rt.groups[at].parent;
		}
		s->order = NONE;
		if (order != NONE)
		{
			boolean letter = squad_order_set(h2_squad, order);

			/* (the log: each squad's initial order, and whether it came after
			a checkpoint revert - 03a's intro skip reverts before e1's
			Marines are placed) */
			rt.initial_orders++;
			if (rt.initial_orders_logged++ < 64)
				platform_log("h2 ai: %s placed: its initial order %s (%d)%s%s", s->name,
					order < rt.order_count && rt.orders ? rt.orders[order].name : "?", order,
					letter ? "" : ", no firing positions of it", rt.reverts ? ", after a revert" : "");
		}
	}
	s->placed += placed;
	s->living_seen += placed;
	rt.placed++;
	if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
		platform_log("h2 ai: ai_place %s: %d actors (bsp %d)", s->name, placed, global_structure_bsp_index);
#ifndef HALO_RELEASE
	/* (the campaign auto-run harness's log, h2_run.c) */
	h2_run_log("placed", "%s %d", s->name, placed);
#endif
}

/* ---------- the runtime: natives' helpers */

union rt_value
{
	long l;
	short s;
	boolean b;
	real r;
};

static long value_short(short s)
{
	union rt_value v;

	v.l = 0;
	v.s = s;
	return v.l;
}

static long value_real(real r)
{
	union rt_value v;

	v.l = 0;
	v.r = r;
	return v.l;
}

static long value_boolean(boolean b)
{
	union rt_value v;

	v.l = 0;
	v.b = b;
	return v.l;
}

static short argument_short(long a)
{
	union rt_value v;

	v.l = a;
	return v.s;
}

static real argument_real(long a)
{
	union rt_value v;

	v.l = a;
	return v.r;
}

static boolean argument_boolean(long a)
{
	union rt_value v;

	v.l = a;
	return v.b;
}

/* a call's arguments, all evaluated (NULL until they are) */
static long *arguments(long thread_index, short maximum, short *count, boolean initialize)
{
	return hs_runtime_arguments_evaluate_all(thread_index, maximum, count, initialize);
}

/* Halo 2's team as this engine's */
static short team_from_h2(short team)
{
	if (team >= 0 && team <= 5)
		return team;
	return team == 6 ? CE_TEAM_HERETIC : team == 7 ? CE_TEAM_PROPHET : CE_TEAM_COVENANT;
}

static boolean actor_alive(long actor_index)
{
	struct actor_datum *actor = actor_index != NONE ? (struct actor_datum *)datum_try_and_get(actor_data, actor_index) : NULL;
	struct object_datum *unit;

	if (!actor || actor->meta.unit_index == NONE)
		return FALSE;
	unit = object_try_and_get(actor->meta.unit_index);
	return unit && !TEST_FLAG(unit->object.damage_flags, _object_dead_bit);
}

/* ---------- the runtime: the AI functions */

/* (ai_place <ai> [<short>]) */
H2_ALIGN static void ai_place_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count, squads[MAXIMUM_SQUADS], squad_count, i;
	long *a = arguments(thread_index, 2, &count, initialize);

	if (!a)
		return;
	squad_count = squads_of(a[0], squads, MAXIMUM_SQUADS);
	for (i = 0; i < squad_count; i++)
	{
		squad_place(squads[i], count > 1 ? argument_short(a[1]) : NONE,
			((unsigned long)a[0] >> 30) == 3 ? (short)(a[0] & 0xFFFF) : NONE);
	}
	hs_return(thread_index, 0);
	(void)function_index;
}

/* counts and strength: ai_living_count, ai_nonswarm_count, ai_swarm_count,
ai_spawn_count, ai_strength, ai_living_fraction */
H2_ALIGN static void ai_count_evaluate(short function_index, long thread_index, boolean initialize)
{
	struct hs_function_definition const *function = h2_scripts_function_get(function_index);
	char const *name = function ? function->name : "";
	short count, squads[MAXIMUM_SQUADS], squad_count, i, total = 0, spawned = 0, members = 0;
	long *a = arguments(thread_index, 1, &count, initialize);
	real strength = 0.0f, old_strength = 0.0f;
	long result;

	if (!a)
		return;
	/* (one actor: halo2-decompiled unknown_272b70.cpp 0x273f30 - 1 if the
	actor is there (each count), its unit's body vitality the strength, the
	fraction 1/1; 0x274090 - fighting 1 if it fights; 0x275d70 - no squad,
	no spawn count) */
	if (ai_index_names_actor(a[0]))
	{
		long actor_index;
		short n = actors_of(a[0], &actor_index, 1);
		struct actor_datum *actor = n ? actor_get(actor_index) : NULL;

		if (!strcmp(name, "ai_strength"))
			result = value_real(actor && actor->meta.unit_index != NONE ? object_get(actor->meta.unit_index)->object.body_vitality : 0.0f);
		else if (!strcmp(name, "ai_living_fraction"))
			result = value_real(n ? 1.0f : 0.0f);
		else if (!strcmp(name, "ai_spawn_count"))
			result = value_short(0);
		else if (!strcmp(name, "ai_fighting_count"))
			result = value_short(n && actor_fighting(actor_index) ? 1 : 0);
		else
			result = value_short(n);
		rt.actor_index_counts++;
		/* (the log: this and what the squad's whole count is - what this
		returned before) */
		if (HALO_ENV_ONCE("HALO_H2_AI_INDEX_LOG"))
		{
			short whole = 0;

			squad_count = squads_of(a[0], squads, MAXIMUM_SQUADS);
			for (i = 0; i < squad_count; i++)
			{
				if (squad_reference(squads[i]) != NONE)
					whole += ai_scripting_living_count(squad_reference(squads[i]));
			}
			platform_log("h2 ai index: %s %s %lx -> %d actor (the squad's living %d)", name,
				squads_of(a[0], squads, 1) ? rt.squads[squads[0]].name : "?", (unsigned long)a[0], (int)n, (int)whole);
		}
		hs_return(thread_index, result);
		return;
	}
	squad_count = squads_of(a[0], squads, MAXIMUM_SQUADS);
	for (i = 0; i < squad_count; i++)
	{
		long reference = squad_reference(squads[i]);
		short living;

		if (reference == NONE)
			continue;
		spawned += rt.squads[squads[i]].placed;
		members += rt.squads[squads[i]].actor_count;
		if (!strcmp(name, "ai_fighting_count"))
		{
			struct ai_script_actor_reference_iterator iterator;
			struct actor_datum *actor;

			living = 0;
			ai_index_actor_iterator_new(reference, &iterator);
			while ((actor = ai_index_actor_iterator_next(&iterator)) != NULL)
				living += actor_alive(iterator.actor_index) && actor_fighting(iterator.actor_index);
		}
		else if (!strcmp(name, "ai_swarm_count"))
			living = ai_scripting_swarm_count(reference);
		else if (!strcmp(name, "ai_nonswarm_count"))
			living = ai_scripting_nonswarm_count(reference);
		else
			living = ai_scripting_living_count(reference);
		/* (ai_strength: halo2-decompiled unknown_2003a0.cpp 0x203360 - a squad's
		+0x10 is its actors' body vitality summed over its actor count, the dead
		kept in that count (0x1e1a00) - and unknown_272b70.cpp 0x273f30, a
		group's: the squads' vitality weighted by their actor counts, so the
		sum over all of them; no epsilon taken off) */
		if (!strcmp(name, "ai_strength"))
		{
			struct ai_script_actor_reference_iterator iterator;
			struct actor_datum *actor;

			ai_index_actor_iterator_new(reference, &iterator);
			while ((actor = ai_index_actor_iterator_next(&iterator)) != NULL)
			{
				if (actor_alive(iterator.actor_index))
					strength += object_get(actor->meta.unit_index)->object.body_vitality;
			}
			if (living > 0)
				old_strength += ai_scripting_strength(reference) * living;
		}
		total += living;
	}
	/* (the log: what this returned before - this engine's strengths
	weighted by the living) */
	if (!strcmp(name, "ai_strength") && HALO_ENV_ONCE("HALO_H2_AI_INDEX_LOG"))
		platform_log("h2 ai index: ai_strength %lx: %.3f (before: %.3f), %d squads, %d living of %d actors (%d placed)", (unsigned long)a[0],
			members > 0 ? MAX(strength / members, 0.0f) : 0.0f, total > 0 ? old_strength / total : 0.0f, (int)squad_count, (int)total,
			(int)members, (int)spawned);
	if (!strcmp(name, "ai_strength"))
		result = value_real(members > 0 ? MAX(strength / members, 0.0f) : 0.0f);
	else if (!strcmp(name, "ai_living_fraction"))
		result = value_real(members > 0 ? (real)total / members : 0.0f);
	else if (!strcmp(name, "ai_spawn_count"))
		result = value_short(spawned);
	else
		result = value_short(total);
	/* (the log, HALO_H2_AI_INDEX_LOG: every squad count a script asks -
	what the encounter scripts' waits see; a pacing timeline's evidence) */
	if (strcmp(name, "ai_strength") && HALO_ENV_ONCE("HALO_H2_AI_INDEX_LOG"))
	{
		platform_log("h2 ai index: %s %s (%lx, %d squads) -> %d (living %d, placed %d of %d members)", name,
			squad_count > 0 ? rt.squads[squads[0]].name : "?", (unsigned long)a[0], (int)squad_count,
			(int)(!strcmp(name, "ai_spawn_count") ? spawned : total), (int)total, (int)spawned, (int)members);
	}
	hs_return(thread_index, result);
}

#ifndef HALO_RELEASE
/* (the campaign auto-run harness's fast mode, main.c) an ai value's living
actors that are the player's enemies killed (a script's wave waited out at
once; its allies, whom scripts wait for too, kept); how many */
/* (the campaign harness, main.c: 04a's ledge Banshees) the vehicle a squad's starting location placed, by the
squad's and the location's names - what (ai_vehicle_get_from_starting_location squad/location) gives a script
for an empty vehicle; NONE: no such vehicle now */
H2_ALIGN long h2_ai_harness_location_vehicle(char const *squad_name, char const *location_name)
{
	struct h2_cache *cache = h2_scripts_cache();
	short i, q;

	for (i = 0; rt.loaded && cache && i < rt.squad_count; i++)
	{
		struct rt_squad *s = &rt.squads[i];

		if (strcmp(s->name, squad_name))
			continue;
		for (q = 0; q < s->location_count; q++)
		{
			char name[64] = "";

			h2_cache_string(cache, s->location_names[q], name, sizeof(name));
			if (!strcmp(name, location_name) && s->location_objects[q] != NONE && object_try_and_get(s->location_objects[q]) &&
				object_get(s->location_objects[q])->definition_index == s->location_vehicles[q])
				return s->location_objects[q];
		}
	}
	return NONE;
}

H2_ALIGN short h2_ai_harness_kill_enemies(long ai_index, short player_team)
{
	short squads[MAXIMUM_SQUADS], squad_count = squads_of(ai_index, squads, MAXIMUM_SQUADS), i, killed = 0;

	for (i = 0; i < squad_count; i++)
	{
		long reference = squad_reference(squads[i]);
		struct ai_script_actor_reference_iterator iterator;
		struct actor_datum *actor;

		if (reference == NONE)
			continue;
		ai_index_actor_iterator_new(reference, &iterator);
		while ((actor = ai_index_actor_iterator_next(&iterator)) != NULL)
		{
			if (actor_alive(iterator.actor_index) && actor->meta.unit_index != NONE &&
				game_team_is_enemy(player_team, object_get(actor->meta.unit_index)->object.owner_team_index))
			{
				/* (one killed before that did not die - a cannot-die actor,
				06a - is left alone, so the harness moves on) */
				static long tried[64];
				static int tried_next;
				int t;

				for (t = 0; t < 64 && tried[t] != actor->meta.unit_index; t++)
					;
				if (t < 64)
					continue;
				tried[tried_next++ % 64] = actor->meta.unit_index;
				unit_kill(actor->meta.unit_index);
				killed++;
			}
		}
	}
	return killed;
}

/* (the harness's @waitsquad and @clear, main.c) the living actors of every
squad whose name begins with name (-1: no squad so named); kill: they are
killed */
H2_ALIGN short h2_ai_harness_named(char const *name, boolean kill)
{
	short i, living = -1;
	size_t length = strlen(name);

	if (!length || !table_build())
		return -1;
	for (i = 0; i < rt.squad_count; i++)
	{
		long reference;

		if (strncmp(rt.squads[i].name, name, length))
			continue;
		if (living < 0)
			living = 0;
		reference = squad_reference(i);
		if (reference == NONE)
			continue;
		if (kill)
			ai_scripting_kill(reference);
		else
			living += ai_scripting_living_count(reference);
	}
	return living;
}
#endif

/* (the harness's @autoplay) an ai value's squads killed - an encounter a
script waits to be cleared, (<= (ai_living_count squad) 0); how many */
H2_ALIGN short h2_ai_harness_kill(long ai_index)
{
	short squads[MAXIMUM_SQUADS], squad_count = squads_of(ai_index, squads, MAXIMUM_SQUADS), i, killed = 0;

	for (i = 0; i < squad_count; i++)
	{
		long reference = squad_reference(squads[i]);

		if (reference != NONE)
		{
			ai_scripting_kill(reference);
			killed++;
		}
	}
	return killed;
}

/* whether a player is in or on an object (halo2-decompiled unknown_0bbf40.cpp
0xbeb30): it is a player's unit, or one of its children is, or one of its
parents is a player's unit */
static boolean object_held_by_player(long object_index)
{
	struct object_datum *object = object_get(object_index);
	long child, parent;

	if (TEST_FLAG(_object_mask_unit, object_header_get(object_index)->type) && unit_get(object_index)->unit.player_index != NONE)
		return TRUE;
	for (child = object->object.first_child_object_index; child != NONE; child = object_get(child)->object.next_object_index)
	{
		if (object_held_by_player(child))
			return TRUE;
	}
	for (parent = object->object.parent_object_index; parent != NONE; parent = object_get(parent)->object.parent_object_index)
	{
		if (TEST_FLAG(_object_mask_unit, object_header_get(parent)->type) && unit_get(parent)->unit.player_index != NONE)
			return TRUE;
	}
	return FALSE;
}

/* (ai_erase of a squad or group: halo2-decompiled unknown_272b70.cpp 0x2736c0)
the squad's vehicles deleted, those a player is in kept */
static void squad_vehicles_erase(short h2_squad)
{
	struct rt_squad *s = &rt.squads[h2_squad];
	short k;

	for (k = 0; k < s->location_count; k++)
	{
		long vehicle = s->location_objects[k];

		if (vehicle == NONE || !object_try_and_get(vehicle) || object_get(vehicle)->definition_index != s->location_vehicles[k])
			continue;
		if (object_held_by_player(vehicle))
			continue;
		object_delete(vehicle);
		s->location_objects[k] = NONE;
		rt.squad_vehicles_erased++;
		if (HALO_ENV_ONCE("HALO_H2_AI_INDEX_LOG"))
			platform_log("h2 ai index: ai_erase %s: its location %d's vehicle deleted", s->name, k);
	}
}

/* each squad's reference given to one of this engine's functions:
ai_erase, ai_kill, ai_kill_silent, ai_renew, ai_vehicle_exit */
H2_ALIGN static void ai_each_evaluate(short function_index, long thread_index, boolean initialize)
{
	struct hs_function_definition const *function = h2_scripts_function_get(function_index);
	char const *name = function ? function->name : "";
	short count, squads[MAXIMUM_SQUADS], squad_count, i;
	long *a = arguments(thread_index, 2, &count, initialize);

	if (!a)
		return;
	/* (one actor - a starting location, ai_current_actor: that actor alone,
	halo2-decompiled unknown_272b70.cpp: ai_erase 0x2736c0 (its actor and
	unit), ai_kill / ai_kill_silent 0x273670, ai_vehicle_exit 0x274da0 (unit
	request 0x1d: out of its seat), ai_braindead 0x274e70 (per actor). Halo
	2's ai_renew, ai_set_blind / _deaf and ai_force_active are per actor too;
	this engine keeps those per encounter (= squad): none of the campaign's
	calls names a starting location) */
	if (ai_index_names_actor(a[0]) &&
		(!strcmp(name, "ai_erase") || !strcmp(name, "ai_kill") || !strcmp(name, "ai_kill_silent") ||
		!strcmp(name, "ai_vehicle_exit") || (!strcmp(name, "ai_braindead") && count > 1)))
	{
		long actor_index;

		if (actors_of(a[0], &actor_index, 1))
		{
			struct actor_datum *actor = actor_get(actor_index);

			if (!strcmp(name, "ai_erase"))
				actor_erase(actor_index, FALSE);
			else if (!strcmp(name, "ai_kill") || !strcmp(name, "ai_kill_silent"))
				actor_kill(actor_index, !strcmp(name, "ai_kill_silent"), FALSE);
			else if (!strcmp(name, "ai_vehicle_exit"))
			{
				if (actor->input.vehicle_index != NONE && actor->meta.unit_index != NONE)
					unit_try_and_exit_seat(actor->meta.unit_index);
			}
			else
				actor_braindead(actor_index, argument_boolean(a[1]));
			rt.actor_index_actions++;
			if (HALO_ENV_ONCE("HALO_H2_AI_INDEX_LOG"))
				platform_log("h2 ai index: %s %lx -> one actor", name, (unsigned long)a[0]);
		}
		hs_return(thread_index, 0);
		return;
	}
	squad_count = squads_of(a[0], squads, MAXIMUM_SQUADS);
	for (i = 0; i < squad_count; i++)
	{
		long reference = squad_reference(squads[i]);

		if (reference == NONE)
			continue;
		if (!strcmp(name, "ai_erase"))
		{
			ai_scripting_erase(reference);
			squad_vehicles_erase(squads[i]);
		}
		else if (!strcmp(name, "ai_kill"))
			ai_scripting_kill(reference);
		else if (!strcmp(name, "ai_kill_silent"))
			ai_scripting_kill_silent(reference);
		else if (!strcmp(name, "ai_renew"))
			ai_scripting_renew(reference);
		else if (!strcmp(name, "ai_vehicle_exit"))
			ai_scripting_exit_vehicle(reference);
		else if (!strcmp(name, "ai_set_blind") && count > 1)
			ai_scripting_set_blind(reference, argument_boolean(a[1]));
		else if (!strcmp(name, "ai_set_deaf") && count > 1)
			ai_scripting_set_deaf(reference, argument_boolean(a[1]));
		else if (!strcmp(name, "ai_braindead") && count > 1)
			ai_scripting_braindead(reference, argument_boolean(a[1]));
		else if (!strcmp(name, "ai_force_active") && count > 1)
			ai_scripting_force_active(reference, argument_boolean(a[1]));
	}
	hs_return(thread_index, 0);
}

/* (h2_ai_map_loaded) every zone's firing positions - where (in its frame) and their area - for
encounter_order_page_in: Halo 2 keeps them by zone, an order naming its zones' areas, so any order a squad is
given has its positions (05a: 4531 in all) */
H2_ALIGN static void zone_positions_read(struct h2_cache *cache, unsigned long scenario)
{
	long zone_count, z, total = 0, n = 0;
	unsigned long zones;

	if (!h2_cache_block(cache, scenario + H2S_ZONES, H2_ZONE_SIZE, &zone_count, &zones) || zone_count <= 0 || zone_count > 4096)
		return;
	for (z = 0; z < zone_count; z++)
	{
		long count;
		unsigned long positions;

		if (h2_cache_block(cache, zones + H2_ZONE_SIZE * z + 0x28, H2_FIRING_POSITION_SIZE, &count, &positions) && count > 0)
			total += count;
	}
	rt.zone_first = calloc((size_t)zone_count, sizeof(long));
	rt.zone_count = calloc((size_t)zone_count, sizeof(long));
	rt.zone_positions = total ? malloc((size_t)total * sizeof(*rt.zone_positions)) : NULL;
	if (!rt.zone_first || !rt.zone_count || (total && !rt.zone_positions))
	{
		free(rt.zone_first);
		free(rt.zone_count);
		free(rt.zone_positions);
		rt.zone_first = rt.zone_count = NULL;
		rt.zone_positions = NULL;
		return;
	}
	for (z = 0; z < zone_count; z++)
	{
		long count, k;
		unsigned long positions;

		rt.zone_first[z] = n;
		if (!h2_cache_block(cache, zones + H2_ZONE_SIZE * z + 0x28, H2_FIRING_POSITION_SIZE, &count, &positions))
			continue;
		for (k = 0; k < count && n < total; k++)
		{
			unsigned char const *p = h2_cache_at(cache, positions + H2_FIRING_POSITION_SIZE * k, H2_FIRING_POSITION_SIZE);

			if (!p)
				continue;
			memcpy(&rt.zone_positions[n].position, p, 12);
			rt.zone_positions[n].area = get16(p + 0x10);
			n++;
		}
		rt.zone_count[z] = n - rt.zone_first[z];
	}
	rt.zone_total = (short)zone_count;
}

/* the encounters' firing positions when an order's are paged in (not freed with the level: an encounter of
a level still loaded may point at them) */
static struct firing_position_definition *fp_pool[MAXIMUM_CE_ENCOUNTERS];

enum
{
	/* (a paged-out position's letter: no squad's groups have it - letters are 0..25) */
	FIRING_POSITION_PAGED_OUT_LETTER = 30,
};

/* (squad_order_set) an order none of whose firing positions the squad's encounter holds: the builder gave
an encounter the positions of its squads' orders while they fit in this engine's 512 (05a's Marine squads
reach 26 orders and more: 21 of allies_lz_ridge's had none, so an ending or ai_set_orders left them on their
old order's positions - Halo 2's ridge Marines move to allies_lz_entry_ledge's areas 3 s in, ours stayed).
Its positions are written, with a free letter, over those of orders no squad of the encounter holds now (and
past its count): the letter, NONE when none is free or the order has no positions. Each paged-out position
left gets FIRING_POSITION_PAGED_OUT_LETTER and order 0 ("none") */
H2_ALIGN static short encounter_order_page_in(short encounter_index, short order, boolean secondary, short given_letter)
{
	struct scenario *scenario = global_scenario_get();
	struct encounter_definition *encounter;
	unsigned long held[(MAXIMUM_H2_ORDERS + 31) / 32];
	unsigned long letters_used = 0;
	short slots[MAXIMUM_CE_FIRING_POSITIONS];
	short slot_count = 0, letter, x, a, used = 0;
	long count, k;

	if (!rt.zone_positions || order < 0 || order >= rt.order_count || encounter_index < 0 ||
		encounter_index >= scenario->ai_encounters.count || encounter_index >= MAXIMUM_CE_ENCOUNTERS)
	{
		return NONE;
	}
	encounter = TAG_BLOCK_GET_ELEMENT(&scenario->ai_encounters, encounter_index, struct encounter_definition);
	count = MIN(encounter->firing_positions.count, (long)MAXIMUM_CE_FIRING_POSITIONS);
	/* (the orders held: each squad's of the encounter - its initial one while it has none) */
	memset(held, 0, sizeof(held));
	for (x = 0; x < rt.squad_count; x++)
	{
		short o = rt.squads[x].order != NONE ? rt.squads[x].order : rt.squads[x].initial_order;

		/* (its secondary set paged in: the order's own positions are held too) */
		if (rt.squads[x].encounter == encounter_index && o >= 0 && o < rt.order_count && (o != order || secondary))
			held[o / 32] |= 1UL << (o % 32);
	}
	for (k = 0; k < count; k++)
	{
		struct firing_position_definition const *p = TAG_BLOCK_GET_ELEMENT(&encounter->firing_positions, k,
			struct firing_position_definition);
		short o = p->unresolved[0] == (byte)(H2_AI_ORDER_MARK & 0xFF) ? (short)(p->unresolved[2] | (p->unresolved[3] << 8)) - 1 : (short)NONE;

		if (o < 0 || o >= rt.order_count || !(held[o / 32] & (1UL << (o % 32))))
			continue;
		if (p->group_index >= 0 && p->group_index < 32)
			letters_used |= 1UL << p->group_index;
	}
	for (letter = 0; letter < MAXIMUM_ORDERS_PER_SQUAD && (letters_used & (1UL << letter)); letter++)
		;
	if (given_letter != NONE)
		letter = given_letter;
	if (letter >= MAXIMUM_ORDERS_PER_SQUAD)
		return NONE;
	/* (the free places: past the count first, then the paged-out and unheld ones from the last back - the
	order just left keeps its places as long as there are others) */
	for (k = count; k < MAXIMUM_CE_FIRING_POSITIONS; k++)
		slots[slot_count++] = (short)k;
	for (k = count - 1; k >= 0; k--)
	{
		struct firing_position_definition const *p = TAG_BLOCK_GET_ELEMENT(&encounter->firing_positions, k,
			struct firing_position_definition);
		short o = p->unresolved[0] == (byte)(H2_AI_ORDER_MARK & 0xFF) ? (short)(p->unresolved[2] | (p->unresolved[3] << 8)) - 1 : (short)NONE;

		if (p->unresolved[0] == (byte)(H2_AI_ORDER_MARK & 0xFF) && (o < 0 || o >= rt.order_count || !(held[o / 32] & (1UL << (o % 32)))))
			slots[slot_count++] = (short)k;
	}
	/* (the encounter's positions in fp_pool, where there is room for 512) */
	if (!rt.pool_owned[encounter_index])
	{
		if (!fp_pool[encounter_index] &&
			!(fp_pool[encounter_index] = malloc(sizeof(struct firing_position_definition) * MAXIMUM_CE_FIRING_POSITIONS)))
		{
			return NONE;
		}
		if (count)
			memcpy(fp_pool[encounter_index], encounter->firing_positions.address, sizeof(struct firing_position_definition) * (size_t)count);
		encounter->firing_positions.address = fp_pool[encounter_index];
		rt.pool_owned[encounter_index] = TRUE;
	}
	/* (the order's areas' positions, in its areas' order - as the builder's order_positions) */
	for (a = 0; a < (secondary ? rt.orders[order].area2_count : rt.orders[order].area_count) && used < slot_count; a++)
	{
		struct rt_area const *set = secondary ? rt.orders[order].areas2 : rt.orders[order].areas;
		short zone = set[a].zone, area = set[a].area;

		if (zone < 0 || zone >= rt.zone_total)
			continue;
		for (k = 0; k < rt.zone_count[zone] && used < slot_count; k++)
		{
			struct rt_firing_position const *z = &rt.zone_positions[rt.zone_first[zone] + k];
			struct firing_position_definition *o;
			short index;

			if (z->area != area)
				continue;
			index = slots[used++];
			deck_unlink(encounter_index, index);
			o = &fp_pool[encounter_index][index];
			memset(o, 0, sizeof(*o));
			o->position = z->position;
			o->group_index = letter;
			o->cluster_index = NONE;
			o->surface_index = NONE;
			o->unresolved[0] = (byte)(H2_AI_ORDER_MARK & 0xFF);
			/* (which of the set's areas; 0x80 the secondary set's) */
			o->unresolved[1] = (byte)(MIN(a, 0x7F) | (secondary ? 0x80 : 0));
			o->unresolved[2] = (byte)((order + 1) & 0xFF);
			o->unresolved[3] = (byte)((order + 1) >> 8);
			if (index >= encounter->firing_positions.count)
				encounter->firing_positions.count = index + 1;
			/* (placed now in the encounter's BSP - else encounter_prepare does it - or carried by its machine) */
			if (!deck_link(encounter_index, index, o) && rt.resolved_bsp[encounter_index] != NONE)
				point_resolve(&o->position, &o->cluster_index, &o->surface_index);
		}
	}
	if (!used)
		return NONE;
	/* (the free places left: paged out) */
	for (x = used; x < slot_count; x++)
	{
		struct firing_position_definition *o;

		if (slots[x] >= encounter->firing_positions.count)
			continue;
		o = &fp_pool[encounter_index][slots[x]];
		if (o->group_index == FIRING_POSITION_PAGED_OUT_LETTER && !o->unresolved[2] && !o->unresolved[3])
			continue;
		deck_unlink(encounter_index, slots[x]);
		o->group_index = FIRING_POSITION_PAGED_OUT_LETTER;
		o->unresolved[0] = (byte)(H2_AI_ORDER_MARK & 0xFF);
		o->unresolved[1] = 0;
		o->unresolved[2] = o->unresolved[3] = 0;
	}
	rt.orders_paged++;
	if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
		platform_log("h2 ai: encounter %s: order %s paged in, %d positions as letter %d (%d places were free)", encounter->name,
			rt.orders[order].name, (int)used, (int)letter, (int)slot_count);
	return letter;
}

/* a squad given an order: its firing positions those of the order's letter
(the builder lettered each order a squad may be given); FALSE if the
squad has none for it */
static struct rt_command_script *command_script_of_actor(long actor_index);
static struct rt_command_script *command_script_of_thread(long thread_index);
static void actor_guard_refresh(long actor_index);

static boolean squad_order_set(short h2_squad, short order)
{
	struct rt_squad *s = &rt.squads[h2_squad];
	struct encounter_definition *encounter;
	struct squad_definition *squad;
	short letter = NONE, k;
	long f;

	if (s->encounter == NONE || order < 0)
		return FALSE;
	encounter = TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->ai_encounters, s->encounter, struct encounter_definition);
	squad = TAG_BLOCK_GET_ELEMENT(&encounter->squads, s->squad, struct squad_definition);
	for (f = 0; f < encounter->firing_positions.count && letter == NONE; f++)
	{
		struct firing_position_definition const *p = TAG_BLOCK_GET_ELEMENT(&encounter->firing_positions, f,
			struct firing_position_definition);

		if (p->unresolved[0] == (byte)(H2_AI_ORDER_MARK & 0xFF) &&
			(short)(p->unresolved[2] | (p->unresolved[3] << 8)) == order + 1)
		{
			letter = p->group_index;
		}
	}
	/* (one the encounter has no positions for: paged in) */
	if (letter == NONE)
	{
		letter = encounter_order_page_in(s->encounter, order, FALSE, NONE);
		if (letter == NONE)
			rt.orders_unpaged++;
	}
	/* (Halo 2's squad +0x2c, the time its order was last given - written
	every time an order is given, the same one again too (ai_set_orders,
	a placement, an ending), and nowhere else: the "after A ticks" rule's
	clock. halo2-decompiled unknown_2003a0.cpp 0x202e90; 0x200c20 sets it
	NONE at creation) */
	s->order = order;
	s->order_time = game_time_get();
	s->secondary = FALSE;
	memset(s->ending_true_since, 0xFF, sizeof(s->ending_true_since));
	s->pending_order = NONE;
	s->pending_ending = NONE;
	s->pending_ticks = 0;
	if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
		platform_log("h2 ai: %s given order %d (letter %d)", s->name, order, letter);
	if (letter == NONE)
		return FALSE;
	for (k = 0; k < MAXIMUM_NUMBER_OF_FIRING_POSITION_GROUPS; k++)
		squad->firing_position_groups[k] = 1UL << letter;
	{
		long actors[RT_MAXIMUM_ACTORS];
		short count = actors_of(squad_reference(h2_squad), actors, RT_MAXIMUM_ACTORS), n;

		for (n = 0; n < count; n++)
		{
			if (!command_script_of_actor(actors[n]))
				actor_guard_refresh(actors[n]);
		}
	}
	return TRUE;
}

/* (ai_set_orders <ai> <orders>) */
H2_ALIGN static void ai_set_orders_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count, squads[MAXIMUM_SQUADS], squad_count, i, order;
	long *a = arguments(thread_index, 2, &count, initialize);

	if (!a)
		return;
	order = count > 1 ? argument_short(a[1]) : NONE;
	squad_count = squads_of(a[0], squads, MAXIMUM_SQUADS);
	for (i = 0; i < squad_count && order != NONE; i++)
	{
		if (squad_order_set(squads[i], order))
			rt.orders_set++;
		else
			rt.orders_unknown++;
	}
	hs_return(thread_index, 0);
	(void)function_index;
}

/* (ai_migrate <ai> <ai>): to the target's first squad */
H2_ALIGN static void ai_migrate_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count, squads[MAXIMUM_SQUADS], targets[1], squad_count, i;
	long *a = arguments(thread_index, 2, &count, initialize);

	if (!a)
		return;
	if (count > 1 && squads_of(a[1], targets, 1) > 0 && squad_reference(targets[0]) != NONE)
	{
		long target = squad_reference(targets[0]);

		/* (one actor - a starting location: that actor's unit alone,
		halo2-decompiled unknown_272b70.cpp 0x274140 - its actor iterator names
		the one actor) */
		if (ai_index_names_actor(a[0]))
		{
			long actor_index, list;

			if (actors_of(a[0], &actor_index, 1) && actor_get(actor_index)->meta.unit_index != NONE &&
				(list = object_list_new()) != NONE)
			{
				object_list_add(list, actor_get(actor_index)->meta.unit_index);
				ai_scripting_migrate_by_unit(list, target);
				rt.actor_index_actions++;
			}
			hs_return(thread_index, 0);
			return;
		}
		squad_count = squads_of(a[0], squads, MAXIMUM_SQUADS);
		for (i = 0; i < squad_count; i++)
		{
			long reference = squad_reference(squads[i]);

			if (reference != NONE && reference != target)
				ai_scripting_migrate(reference, target);
		}
	}
	hs_return(thread_index, 0);
	(void)function_index;
}

/* (ai_allegiance <team> <team>), (ai_allegiance_remove <team> <team>) */
H2_ALIGN static void ai_allegiance_evaluate(short function_index, long thread_index, boolean initialize)
{
	struct hs_function_definition const *function = h2_scripts_function_get(function_index);
	short count;
	long *a = arguments(thread_index, 2, &count, initialize);

	if (!a)
		return;
	if (count > 1)
	{
		short t0 = team_from_h2(argument_short(a[0])), t1 = team_from_h2(argument_short(a[1]));

		if (function && !strcmp(function->name, "ai_allegiance_remove"))
			ai_scripting_allegiance_remove(t0, t1);
		else if (t0 != t1)
			ai_scripting_allegiance(t0, t1);
	}
	hs_return(thread_index, 0);
}

/* (ai_magically_see <ai> <ai>), (ai_magically_see_object <ai> <object>) */
H2_ALIGN static void ai_magically_see_evaluate(short function_index, long thread_index, boolean initialize)
{
	struct hs_function_definition const *function = h2_scripts_function_get(function_index);
	short count, squads[MAXIMUM_SQUADS], squad_count, i;
	long *a = arguments(thread_index, 2, &count, initialize);
	long targets[64];
	short target_count = 0, k;

	if (!a)
		return;
	if (count > 1)
	{
		if (function && !strcmp(function->name, "ai_magically_see_object"))
		{
			if (a[1] != NONE)
				targets[target_count++] = a[1];
		}
		else
		{
			long actors[64];
			short actor_count = actors_of(a[1], actors, 64);

			for (k = 0; k < actor_count; k++)
			{
				struct actor_datum *actor = actor_get(actors[k]);

				if (actor->meta.unit_index != NONE)
					targets[target_count++] = actor->meta.unit_index;
			}
		}
		squad_count = squads_of(a[0], squads, MAXIMUM_SQUADS);
		for (i = 0; i < squad_count; i++)
		{
			long reference = squad_reference(squads[i]);

			for (k = 0; reference != NONE && k < target_count; k++)
			{
				if (object_try_and_get_and_verify_type(targets[k], _object_mask_unit))
				{
					ai_scripting_magically_see_unit(reference, targets[k]);
				}
			}
		}
	}
	hs_return(thread_index, 0);
}

/* (ai_actors <ai>): the units, a list */
H2_ALIGN static void ai_actors_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count, actor_count, k;
	long *a = arguments(thread_index, 1, &count, initialize);
	long actors[RT_MAXIMUM_ACTORS], list;

	if (!a)
		return;
	list = object_list_new();
	actor_count = actors_of(a[0], actors, RT_MAXIMUM_ACTORS);
	for (k = 0; list != NONE && k < actor_count; k++)
	{
		struct actor_datum *actor = actor_get(actors[k]);

		if (actor->meta.unit_index != NONE)
			object_list_add(list, actor->meta.unit_index);
	}
	hs_return(thread_index, list);
	(void)function_index;
}

/* (ai_get_object <ai>), (ai_get_unit <ai>), (ai_vehicle_get <ai>): the
first living actor's unit, or its vehicle */
H2_ALIGN static void ai_get_object_evaluate(short function_index, long thread_index, boolean initialize)
{
	struct hs_function_definition const *function = h2_scripts_function_get(function_index);
	short count, actor_count, k;
	long *a = arguments(thread_index, 1, &count, initialize);
	long actors[RT_MAXIMUM_ACTORS], result = NONE;

	if (!a)
		return;
	actor_count = actors_of(a[0], actors, RT_MAXIMUM_ACTORS);
	for (k = 0; k < actor_count && result == NONE; k++)
	{
		if (actor_alive(actors[k]))
			result = actor_get(actors[k])->meta.unit_index;
	}
	if (result != NONE && function && !strncmp(function->name, "ai_vehicle_get", 14))
	{
		struct object_datum *object = object_get(result);

		result = object->object.parent_object_index;
	}
	/* (no one there: the empty vehicle its location placed - squad_place) */
	if (result == NONE && function && !strncmp(function->name, "ai_vehicle_get", 14) && a[0] != NONE &&
		((unsigned long)a[0] >> 30) != 2 && a[0] != RT_AI_CURRENT_ACTOR && a[0] != RT_AI_CURRENT_SQUAD && ai_actor_of(a[0]) == NONE)
	{
		short squads[MAXIMUM_SQUADS], squad_count = squads_of(a[0], squads, MAXIMUM_SQUADS), i, q;

		for (i = 0; i < squad_count && result == NONE; i++)
		{
			struct rt_squad *s = &rt.squads[squads[i]];

			for (q = 0; q < s->location_count && result == NONE; q++)
			{
				if ((((unsigned long)a[0] >> 30) != 3 || q == (short)(a[0] & 0xFFFF)) && s->location_objects[q] != NONE &&
					object_try_and_get(s->location_objects[q]) && object_get(s->location_objects[q])->definition_index == s->location_vehicles[q])
				{
					result = s->location_objects[q];
				}
			}
		}
	}
	hs_return(thread_index, result);
}

/* (ai_vehicle_enter <ai> <unit> [<seat>]) */
H2_ALIGN static void ai_vehicle_enter_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count, squads[MAXIMUM_SQUADS], squad_count, i;
	long *a = arguments(thread_index, 3, &count, initialize);

	if (!a)
		return;
	if (count > 1 && a[1] != NONE && object_try_and_get(a[1]))
	{
		struct hs_function_definition const *function = h2_scripts_function_get(function_index);

		/* (ai_vehicle_enter_immediate: hs_doc, "snapped into a vehicle, in the
		substring-specified seats": 03a's intro Warthogs' drivers have no walk
		in them, the scene has them drive off at once) */
		if (function && !strcmp(function->name, "ai_vehicle_enter_immediate"))
		{
			static long actors[RT_MAXIMUM_ACTORS];
			static char const *const any[] = { "", NULL };
			char const *labels[2];
			short actor_count = actors_of(a[0], actors, RT_MAXIMUM_ACTORS), n;

			labels[0] = count > 2 && a[2] != NONE && a[2] ? (char const *)a[2] : "";
			labels[1] = NULL;
			for (n = 0; n < actor_count; n++)
			{
				long unit = actor_get(actors[n])->meta.unit_index;

				if (unit != NONE && object_try_and_get(unit) && object_get(unit)->object.parent_object_index == NONE &&
					!seat_take(unit, a[1], labels))
				{
					seat_take(unit, a[1], any);
				}
			}
			hs_return(thread_index, 0);
			return;
		}
		squad_count = squads_of(a[0], squads, MAXIMUM_SQUADS);
		for (i = 0; i < squad_count; i++)
		{
			long reference = squad_reference(squads[i]);

			if (reference != NONE)
				ai_scripting_go_to_vehicle(reference, a[1], "");
		}
	}
	hs_return(thread_index, 0);
	(void)function_index;
}

/* (ai_combat_status <ai>): Halo 2's levels (0 asleep, 1 idle, 2 alert, 3
active, ... 7 visible) from this engine's actors' state */
/* whether a command script's blocking function has its actor now (Halo 2's
state 0: halo2-upstream-work function_258660 keeps current_cs_index set only
then, so the command script behaviour's priority 3 - actor_behavior_x87.cpp -
replaces the actor's own; while its thread only sleeps, the actor's own
behaviour runs, as far as the script enables it) */
static boolean command_script_in_atom(struct rt_command_script const *c)
{
	return game_time_get() - c->atom_time <= 1;
}

/* a blocking cs_ function goes on waiting */
static void command_script_atom_wait(long thread_index)
{
	struct rt_command_script *c = command_script_of_thread(thread_index);

	if (c)
		c->atom_time = game_time_get();
	hs_runtime_thread_sleep(thread_index, 1);
}

/* a blocking cs_ function (cs_pause, cs_go_to ..., cs_approach,
cs_custom_animation ...): its arguments, then its action - step, given
the function's arguments and the tick it began - each tick until done.
HALO_H2_CS_IN_ACTOR, in a command script's thread: Halo 2's way - the
action set on the command script (halo2-decompiled unknown_276f80.cpp sets
cs->type and its values), the function returns at once and the thread
sleeps -3 (unknown_29f5b0.cpp's evaluators: hs_thread_return_value, then
hs_thread_set_sleep k_hs_sleep_command_script); its actor's update steps
the action and wakes the thread once it is done (h2_ai_command_scripts_run:
0x258660 -> 0x258b60(0x258cf0), the command script behaviour's 0x258a00 ->
0x259430). Otherwise (switch off, or not a command script's thread) this
engine's way: the thread evaluates the function again each tick and the
action is stepped there. The stack keeps what it kept before: whether the
action was set up, and - no command script - the tick it began */
static void command_script_action_evaluate(short function_index, long thread_index, boolean initialize, short argument_count,
	rt_cs_action_step step)
{
	long *state = hs_runtime_stack_allocate(thread_index, 2 * sizeof(long));
	struct rt_command_script *c;
	struct rt_cs_action own, *action;
	short count, k;
	long *a;

	if (!state)
		return;
	if (initialize)
		state[0] = 0, state[1] = NONE;
	a = arguments(thread_index, argument_count, &count, initialize);
	if (!a)
		return;
	c = command_script_of_thread(thread_index);
	action = c ? &c->action : &own;
	/* (set up: the first time, or a script moved to another's slot -
	cs_switch) */
	if (!c || !state[0] || c->action.function_index != function_index)
	{
		action->function_index = function_index;
		action->count = count;
		for (k = 0; k < 4; k++)
			action->arguments[k] = k < count ? a[k] : 0;
		action->started = c ? NONE : state[1];
		if (c && !state[0])
		{
			state[0] = 1;
			if (rt.cs_in_actor)
			{
				c->action_step = step;
				hs_return(thread_index, 0);
				hs_runtime_thread_sleep_command_script(thread_index);
				return;
			}
		}
	}
	if (step(c, action) != CS_STEP_CONTINUE)
	{
		if (c)
			c->action_step = NULL;
		hs_return(thread_index, 0);
		return;
	}
	if (!c)
		state[1] = action->started;
	command_script_atom_wait(thread_index);
}

/* an actor's combat status, Halo 2's scale (ai_combat_status_asleep 0, idle,
alert, active, uninspected, definite, certain, visible, clear_los, dangerous
9), from this engine's target and action. cs_force_combat_status N is N - 1
outright (function_2662f0). An actor whose script keeps it from targeting -
function_258660 clears its target each tick - stays at what it merely
perceives: the H2EK's tag build, 05a, Elites under forever_pause and
stay_shooting read 4 at most, the same Elites fighting 8 */
H2_ALIGN static short actor_combat_status(long actor_index)
{
	struct actor_datum *actor = actor_get(actor_index);
	struct rt_command_script const *c = command_script_of_actor(actor_index);
	short level, s;

	if (c && c->force_status > 0)
		return c->force_status - 1;
	switch (actor->target.target_type)
	{
	case _actor_target_partial_enemy:
	case _actor_target_disregarded_orphan:
	case _actor_target_inspected_orphan:
	case _actor_target_uninspected_orphan:
		level = 4;
		break;
	case _actor_target_definite_orphan:
		level = 5;
		break;
	case _actor_target_acknowledged_enemy:
		level = 6;
		break;
	case _actor_target_clear_line_of_sight_enemy:
		level = 7;
		break;
	case _actor_target_potentially_dangerous_enemy:
	case _actor_target_visible_enemy:
		level = 8;
		break;
	case _actor_target_damaging_enemy:
		level = 9;
		break;
	default:
		level = actor->state.action == _actor_action_sleep ? 0 :
			actor->state.action == _actor_action_fight ? 3 :
			actor->state.action == _actor_action_alert || actor->state.action == _actor_action_search ||
			actor->state.action == _actor_action_uncover ? 2 : 1;
		break;
	}
	if (c && level > 4 && (command_script_in_atom(c) || !(c->flags & CS_TARGETING)))
		level = 4;
	/* (its order's Force combat status: function_2662f0, the least it may be) */
	s = actor_h2_squad(actor_index);
	if (s != NONE && rt.squads[s].order >= 0 && rt.squads[s].order < rt.order_count &&
		level < rt.orders[rt.squads[s].order].force_status - 1)
	{
		level = rt.orders[rt.squads[s].order].force_status - 1;
	}
	return level;
}

/* whether an actor counts as fighting (ai_fighting_count): Halo 2's
(halo2-upstream-work actors_sse.cpp 0x1e1de0, a squad's +0x16 counter of
unknown_2003a0.cpp) - an active actor whose combat status is above alert,
raised within the last 20 s, and not in behaviour 0x38 (not decompiled:
not checked here). This engine's status falls as its target goes stale
(orphans 4-5, none 1-3), which stands in for the 20 s. Before: actors in
this engine's fight action only - 03a e1's rooftop wave read 0-1 fighting
where the H2EK tag test reads 5, and e1_cov_inf2's next wave (living <= 1,
fighting 0) could come while the last one was still shooting */
H2_ALIGN static boolean actor_fighting(long actor_index)
{
	struct actor_datum *actor = actor_get(actor_index);

	return actor_alive(actor_index) && !actor->meta.dormant && actor_combat_status(actor_index) > 2;
}

H2_ALIGN static void ai_combat_status_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count, actor_count, k, status = 0;
	long *a = arguments(thread_index, 1, &count, initialize);
	long actors[RT_MAXIMUM_ACTORS];

	if (!a)
		return;
	actor_count = actors_of(a[0], actors, RT_MAXIMUM_ACTORS);
	for (k = 0; k < actor_count; k++)
	{
		short level;

		if (!actor_alive(actors[k]))
			continue;
		level = actor_combat_status(actors[k]);
		if (level > status)
			status = level;
	}
	hs_return(thread_index, value_short(status));
	(void)function_index;
}

H2_ALIGN boolean h2_ai_external_global_value(
	char const *name,
	long *value)
{
	static char const *const statuses[] =
	{
		"ai_combat_status_asleep", "ai_combat_status_idle", "ai_combat_status_alert", "ai_combat_status_active",
		"ai_combat_status_uninspected", "ai_combat_status_definite", "ai_combat_status_certain", "ai_combat_status_visible",
		"ai_combat_status_clear_los", "ai_combat_status_dangerous",
	};
	short i;

	for (i = 0; i < (short)NUMBEROF(statuses); i++)
	{
		if (!strcmp(name, statuses[i]))
		{
			*value = value_short(i);
			return TRUE;
		}
	}
	{
		static char const *const movements[] =
		{
			"ai_movement_patrol", "ai_movement_asleep", "ai_movement_combat", "ai_movement_flee",
			"ai_movement_flaming", "ai_movement_stunned", "ai_movement_berserk",
		};

		for (i = 0; i < (short)NUMBEROF(movements); i++)
		{
			if (!strcmp(name, movements[i]))
			{
				*value = value_short(i);
				return TRUE;
			}
		}
	}
	if (!strcmp(name, "ai_current_actor") || !strcmp(name, "ai_current_squad"))
	{
		*value = !strcmp(name, "ai_current_actor") ? RT_AI_CURRENT_ACTOR : RT_AI_CURRENT_SQUAD;
		return TRUE;
	}
	return FALSE;
}

/* ---------- the runtime: command scripts */

extern struct data_array *hs_thread_data;

/* the command script an actor runs (NULL: none) */
static struct rt_command_script *command_script_of_actor(long actor_index)
{
	short i;

	for (i = 0; i < RT_MAXIMUM_COMMAND_SCRIPTS; i++)
	{
		if (rt.command_scripts[i].actor_index != NONE && rt.command_scripts[i].actor_index == actor_index)
			return &rt.command_scripts[i];
	}
	return NULL;
}

/* the command script a thread runs (NULL: the thread is not one) */
static struct rt_command_script *command_script_of_thread(long thread_index)
{
	short i;

	for (i = 0; i < RT_MAXIMUM_COMMAND_SCRIPTS; i++)
	{
		struct rt_command_script *c = &rt.command_scripts[i];

		if (c->actor_index != NONE && c->thread_index != NONE &&
			DATUM_INDEX_TO_ABSOLUTE_INDEX(c->thread_index) == DATUM_INDEX_TO_ABSOLUTE_INDEX(thread_index))
		{
			return c;
		}
	}
	return NULL;
}

/* an actor guarding looks for a firing position again (its squad's order's,
squad_order_set): Halo 2's actor goes to its order's area when its command
script ends or its order changes; this engine's guard keeps the position it
chose - or, finding none then, the place it stands - until something else
moves it */
static void actor_guard_refresh(long actor_index)
{
	struct actor_datum *actor;

	if (!actor_alive(actor_index))
		return;
	actor = actor_get(actor_index);
	if (actor->state.action == _actor_action_guard)
		actor->state.action_data.guard.find_new_guard_position = TRUE;
}

/* the actor of the command script whose thread is evaluating (NONE) */
static long current_actor(void)
{
	long thread_index = hs_runtime_executing_thread_index();
	struct rt_command_script *c = thread_index != NONE ? command_script_of_thread(thread_index) : NULL;

	return c ? c->actor_index : NONE;
}

/* whether a command script ends on its actor's combat status
(cs_abort_on_alert, cs_abort_on_combat_status) or its unit hurt
(cs_abort_on_damage): Halo 2 tests these at the top of each pass of
halo2-decompiled unknown_257d00.cpp 0x258660 (+0x79: actor +0x86 at least 2,
+0x7c: at least that status, +0x7a: actor +0x2d8 or +0x2dc above 0) */
static boolean command_script_aborts(struct rt_command_script const *c)
{
	if (c->abort_status != NONE && actor_combat_status(c->actor_index) >= c->abort_status)
		return TRUE;
	return (c->flags & CS_ABORT_ON_DAMAGE) &&
		object_get(actor_get(c->actor_index)->meta.unit_index)->object.body_vitality < c->body_vitality;
}

/* a command script whose thread returned: let go, the one queued after it
begun */
static void command_script_returned(struct rt_command_script *c)
{
	long actor_index = c->actor_index;
	short queued = c->queued;

	actor_guard_refresh(c->actor_index);
	memset(c, 0, sizeof(*c));
	c->actor_index = NONE;
	c->thread_index = NONE;
	/* (the one queued after it begins) */
	if (queued != NONE && queued != 0)
		command_script_run(actor_index, queued, NONE);
}

static void command_script_end(struct rt_command_script *c)
{
	if (c->thread_index != NONE)
		hs_runtime_thread_end(c->thread_index);
	actor_guard_refresh(c->actor_index);
	memset(c, 0, sizeof(*c));
	c->actor_index = NONE;
	c->thread_index = NONE;
}

/* a thread for a waiting command script, if one can be spared (the
level's own scripts and the console keep a few) */
static void command_script_start(struct rt_command_script *c)
{
	if (hs_thread_data->maximum_count - hs_thread_data->actual_count <= RT_THREADS_KEPT_FREE)
		return;
	c->thread_index = hs_runtime_command_script_new(c->script);
	if (c->thread_index != NONE)
		rt.command_scripts_run++;
	c->start_time = game_time_get();
}

/* an actor given a command script (its running one ended - unless that is
the thread asking, caller_thread -); FALSE: no room for another */
static boolean command_script_run(long actor_index, short script, long caller_thread)
{
	struct rt_command_script *c;
	short i, waiting = 0;

	if (!actor_alive(actor_index))
		return TRUE;
	c = command_script_of_actor(actor_index);
	if (c)
	{
		/* (its own running script asks again: left as it is) */
		if (caller_thread != NONE && c->thread_index != NONE &&
			DATUM_INDEX_TO_ABSOLUTE_INDEX(c->thread_index) == DATUM_INDEX_TO_ABSOLUTE_INDEX(caller_thread))
		{
			return TRUE;
		}
		command_script_end(c);
	}
	for (i = 0; i < RT_MAXIMUM_COMMAND_SCRIPTS && rt.command_scripts[i].actor_index != NONE; i++)
		;
	if (i == RT_MAXIMUM_COMMAND_SCRIPTS)
		return FALSE;
	c = &rt.command_scripts[i];
	memset(c, 0, sizeof(*c));
	c->actor_index = actor_index;
	c->thread_index = NONE;
	c->script = script;
	c->move_surface = NONE;
	c->look_object = NONE;
	c->abort_status = NONE;
	c->speed = 1.0f;
	c->atom_time = -100;
	c->movement_mode = NONE;
	c->queued = NONE;
	c->approach_object = NONE;
	c->scene = NONE;
	c->body_vitality = object_get(actor_get(actor_index)->meta.unit_index)->object.body_vitality;
	command_script_start(c);
	if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
		platform_log("h2 ai: command script %d (%s) for actor %lx: thread %lx", c->script,
			script >= 0 && script < global_scenario_get()->hs_scripts.count ?
			TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->hs_scripts, script, struct hs_script)->name : "?", c->actor_index, c->thread_index);
	for (i = 0; i < RT_MAXIMUM_COMMAND_SCRIPTS; i++)
		waiting += rt.command_scripts[i].actor_index != NONE && rt.command_scripts[i].thread_index == NONE;
	if (waiting > rt.command_scripts_waiting_max)
		rt.command_scripts_waiting_max = waiting;
	return TRUE;
}

/* (HALO_H2_CS_IN_ACTOR) whether command scripts run in their actor's update
(hs_runtime.c's scheduler then skips their threads), and whether a thread is
a command script's */
boolean h2_ai_command_scripts_in_actor(void)
{
	return rt.loaded && rt.cs_in_actor;
}

/* (HALO_H2_ACTOR_ACTIVATION; encounters.c's activation test) whether a Halo
2 level's actors are all active: halo2-decompiled unknown_1e3920.cpp
0x1e2f50 walks every actor and updates (0x1e34e0) each whose +0x84 is above
0 - set to 3 when it is made (ai.cpp 0x1dfb90), 0 only by ai_braindead
(unknown_272b70.cpp 0x274e70) - and whose +6 0x1e35f0 set: in the loaded BSP
(+0x3e, or its BSP locations found again). Nothing there puts an actor to
sleep by distance or by what the players see; it is deleted only when ai_erase
(0x2736c0, +0xd for the actor being updated) or the disposal (ai_disposable)
says so */
boolean h2_ai_actors_always_active(void)
{
	return rt.loaded && rt.actor_activation;
}

/* (HALO_H2_ACTOR_ACTIVATION) whether an actor is in the loaded BSP:
halo2-decompiled 0x1e35f0 - an actor whose BSP (+0x3e, the one loaded when
it was made) is the loaded one is; one of another BSP only if
actor_update_bsp_locations finds it in this one, and from then it is of
this BSP. Found here: its unit's (or its vehicle's) location has a cluster
of the loaded BSP (an object that none of its leaves holds has none:
objects_reconnect_to_structure_bsp) */
static boolean actor_in_loaded_bsp(long actor_index, struct actor_datum const *actor)
{
	long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index), object;

	/* (FLAGGED STAND-IN: a unit on its own feet that no leaf of the loaded BSP
	holds sleeps whatever its BSP. The levels' scripts place squads at starting
	locations in another BSP's ground - 03a's e1_cov_inf0 and e1_cov_inf2 while
	BSP 1 is loaded, at -581 -620 0 -: Halo 2 updates them (+0x3e is the loaded
	BSP) and they stand there; this engine's physics drops a unit with no
	cluster through the world (8 m, autoplay with HALO_H2_ACTOR_ACTIVATION=1).
	How Halo 2's objects keep such a unit up is not found yet; one riding a
	vehicle - a Pelican flying beyond the leaves - goes on) */
	if (actor->meta.unit_index != NONE && object_try_and_get(actor->meta.unit_index) &&
		object_get(actor->meta.unit_index)->object.parent_object_index == NONE &&
		object_get(actor->meta.unit_index)->object.location.cluster_index == NONE)
	{
		return FALSE;
	}
	if (absolute >= RT_MAXIMUM_ACTORS || rt.actor_bsp_datum[absolute] != actor_index ||
		rt.actor_bsp[absolute] == global_structure_bsp_index)
	{
		return TRUE;
	}
	if (actor->meta.unit_index == NONE || !object_try_and_get(actor->meta.unit_index))
		return FALSE;
	object = object_get_ultimate_parent(actor->meta.unit_index);
	if (object_get(object)->object.location.cluster_index == NONE)
		return FALSE;
	rt.actor_bsp[absolute] = global_structure_bsp_index;
	return TRUE;
}

/* (HALO_H2_ACTOR_ACTIVATION; encounters.c, an encounter placed in another
BSP than the loaded one) an encounter whose actors stand in the loaded BSP
goes on there - Halo 2 updates each actor that 0x1e35f0 finds in the loaded
BSP, whichever BSP it was placed in: the encounter moved to this BSP (its
starting locations and firing positions found in it, encounter_prepare).
TRUE: moved */
boolean h2_ai_encounter_relocate(long encounter_index)
{
	struct data_iterator iterator;
	struct actor_datum *actor;

	if (!rt.loaded || !rt.actor_activation)
		return FALSE;
	data_iterator_new(&iterator, actor_data);
	while ((actor = (struct actor_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (actor->meta.encounter_index == encounter_index && actor_alive(iterator.datum_index) && actor_in_loaded_bsp(iterator.datum_index, actor))
		{
			encounter_prepare((short)DATUM_INDEX_TO_ABSOLUTE_INDEX(encounter_index));
			rt.encounters_relocated++;
			return TRUE;
		}
	}
	return FALSE;
}

/* (HALO_H2_ACTOR_ACTIVATION; encounters.c, after its activation test) each
actor of an active encounter active only while it is in the loaded BSP -
Halo 2's +6 (0x1e35f0): one that is not there is not updated (here put to
sleep, its unit with it, as this engine sleeps an encounter's), and goes on
once it is found there again */
void h2_ai_actors_activation_by_location(void)
{
	struct data_iterator iterator;
	struct actor_datum *actor;

	if (!rt.loaded || !rt.actor_activation)
		return;
	data_iterator_new(&iterator, actor_data);
	while ((actor = (struct actor_datum *)data_iterator_next(&iterator)) != NULL)
	{
		boolean located;

		if (actor->meta.encounter_index == NONE || actor->meta.swarm || !encounter_get(actor->meta.encounter_index)->active)
			continue;
		located = actor_in_loaded_bsp(iterator.datum_index, actor);
		if (located != actor->meta.active)
		{
			actor_set_active(iterator.datum_index, located);
			if (located)
				rt.actors_relocated++;
			else
				rt.actors_out_of_bsp++;
		}
	}
}

boolean h2_ai_thread_is_command_script(long thread_index)
{
	return rt.loaded && command_script_of_thread(thread_index) != NULL;
}

/* (HALO_H2_CS_IN_ACTOR; actors.c, each actor's update after its perception,
before its decision - as halo2-decompiled unknown_1e3920.cpp 0x1e34e0 runs
0x258660 before its behaviour) the actor's command script run: halo2-decompiled
unknown_257d00.cpp 0x258660's loop over the actor's command scripts, its
state 1 arm through 0x258880 (hs_thread_run_if_due, hs_scheduler.cpp 0x209580):
run until it blocks. A script whose thread returned (0x258880's 0, state 2) is
let go and the one queued after it runs at once, as 0x258660 goes round again
(here one script an actor, its queue the c->queued script). 0x258880's
result 1 - a blocking cs_ function set its action and the thread sleeps -3
(command_script_action_evaluate) - steps the action at once (0x258cf0) and
then each update (0x259430, the command script behaviour's) until done, the
thread run on then. The finishing conditions at 0x258660's top
(+0x79/+0x7a/+0x7c) are tested before each pass. Not ported (open): 0x258880's
type 0x16 (run again at once) and 0x17 (done) results; the actions' steps are
this engine's tests of each function (the cs_ evaluators' own), not yet the
per-type arms of 0x259430. FALSE: the actor is gone (its script erased it) */
boolean h2_ai_command_scripts_run(long actor_index)
{
	short passes;

	if (!rt.loaded || !rt.cs_in_actor)
		return TRUE;
	/* (bounded: a queue of scripts each returning at once, or actions each
	done at once) */
	for (passes = 0; passes < 32; passes++)
	{
		struct rt_command_script *c = command_script_of_actor(actor_index);
		short result;

		if (!c)
			break;
		/* (its finishing conditions, before it runs: 0x258660's state 2 -
		ended, and the next of the actor's scripts, the one queued after it,
		runs as 0x258660 goes round again) */
		if (actor_alive(actor_index) && command_script_aborts(c))
		{
			short queued = c->queued;

			rt.cs_actor_aborts++;
			command_script_end(c);
			if (queued != NONE && queued != 0)
				command_script_run(actor_index, queued, NONE);
			continue;
		}
		/* (one still waiting for a thread - 0x258880's 3 - given one by
		h2_ai_update) */
		if (c->thread_index == NONE)
			break;
		c->run_time = game_time_get();
		/* (its thread waits on its action, sleep -3: 0x258660's state 0 -
		the actor's own update steps the action, the command script
		behaviour's 0x258a00 -> 0x259430; the first step, the tick the
		function set it, is 0x258660's state 1 -> 0x258b60(0x258cf0). The
		action has the actor - current_cs_index, command_script_in_atom -
		until it is done; then the thread runs on at once, as 0x258a00
		goes round to state 1) */
		if (c->action_step && hs_runtime_thread_waits_on_command_script(c->thread_index))
		{
			rt.cs_action_steps++;
			if (c->action_step(c, &c->action) == CS_STEP_CONTINUE)
			{
				c->atom_time = game_time_get();
				break;
			}
			c->action_step = NULL;
			rt.cs_actions_done++;
			if (!datum_try_and_get(actor_data, actor_index))
				return FALSE;
		}
		else
			rt.cs_actor_runs += hs_runtime_thread_due(c->thread_index);
		result = hs_runtime_thread_run_if_due(c->thread_index);
		if (!datum_try_and_get(actor_data, actor_index))
			return FALSE;
		/* (the same slot may now hold another actor's script: ours again?) */
		c = command_script_of_actor(actor_index);
		if (!c)
			break;
		/* (0x258880's 1: it set an action and waits on it - stepped now) */
		if (result == 1 && c->action_step)
			continue;
		if (result != 0)
			break;
		if (hs_runtime_thread_alive(c->thread_index))
			break;
		rt.cs_actor_ends++;
		command_script_returned(c);
	}
	return datum_try_and_get(actor_data, actor_index) != NULL;
}

/* (HALO_H2_THREAD_TRACE=<ticks>: hs_runtime.c's trace of the thread runs of
the first ticks) a command script thread's script and actor; FALSE: not one */
boolean h2_ai_thread_command_script_describe(long thread_index, short *script, long *actor_index)
{
	struct rt_command_script *c = rt.loaded ? command_script_of_thread(thread_index) : NULL;

	if (!c)
		return FALSE;
	*script = c->script;
	*actor_index = c->actor_index;
	return TRUE;
}

/* (HALO_H2_CS_IN_ACTOR; actors.c, after the actor walk, before the
scheduler) the command scripts of living actors whose update did not run
this tick: this engine updates only the actors of active encounters (an
encounter is deactivated out of the players' potentially visible set,
encounters.c), Halo 2 updates each active actor (unknown_1e3920.cpp walks
every actor, 0x1e34e0 runs one whose +6 byte is set) - a flagged stand-in
until that activation is ported: such an actor's script runs here, in actor
order, as its update would have run it */
void h2_ai_command_scripts_run_rest(void)
{
	struct data_iterator iterator;
	struct actor_datum *actor;

	/* (with Halo 2's actor activation the actors not updated are those Halo
	2 does not update either - braindead, +0x84 0, or of another BSP, +6
	clear - and their scripts wait) */
	if (!rt.loaded || !rt.cs_in_actor || rt.actor_activation)
		return;
	data_iterator_new(&iterator, actor_data);
	while ((actor = (struct actor_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long actor_index = iterator.datum_index;
		struct rt_command_script *c = command_script_of_actor(actor_index);

		if (!c || c->thread_index == NONE || c->run_time == game_time_get() || !actor_alive(actor_index))
			continue;
		rt.cs_rest_runs += hs_runtime_thread_due(c->thread_index);
		h2_ai_command_scripts_run(actor_index);
	}
}

/* (cs_run_command_script <ai> <script>), cs_queue_command_script and
cs_stack_command_script (no queue here: the script runs now) */
H2_ALIGN static void cs_run_command_script_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count, actor_count, k;
	long *a = arguments(thread_index, 2, &count, initialize);
	long actors[RT_MAXIMUM_ACTORS];

	if (!a)
		return;
	actor_count = count > 1 ? actors_of(a[0], actors, RT_MAXIMUM_ACTORS) : 0;
	for (k = 0; k < actor_count; k++)
	{
		struct hs_function_definition const *function = h2_scripts_function_get(function_index);
		struct rt_command_script *running = command_script_of_actor(actors[k]);

		/* (cs_queue_command_script: after the one it runs - 03a e13's
		Shadows' decisions after their drives) */
		if (function && !strcmp(function->name, "cs_queue_command_script") && running &&
			!(running->thread_index != NONE && DATUM_INDEX_TO_ABSOLUTE_INDEX(running->thread_index) == DATUM_INDEX_TO_ABSOLUTE_INDEX(thread_index)))
		{
			running->queued = argument_short(a[1]);
			continue;
		}
		if (!command_script_run(actors[k], argument_short(a[1]), thread_index))
			break;
	}
	hs_return(thread_index, 0);
	(void)function_index;
}

/* (cs_command_script_running <ai> <script>) */
H2_ALIGN static void cs_command_script_running_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count, actor_count, k;
	long *a = arguments(thread_index, 2, &count, initialize);
	long actors[RT_MAXIMUM_ACTORS];
	boolean running = FALSE;

	if (!a)
		return;
	actor_count = count > 1 ? actors_of(a[0], actors, RT_MAXIMUM_ACTORS) : 0;
	for (k = 0; k < actor_count && !running; k++)
	{
		struct rt_command_script *c = command_script_of_actor(actors[k]);

		running = c && c->script == argument_short(a[1]);
	}
	hs_return(thread_index, value_boolean(running));
	(void)function_index;
}

/* a point reference's position (Halo 2's: point set << 16 | point) */
static boolean point_get(long reference, real_point3d *point)
{
	short set = (short)((unsigned long)reference >> 16), index = (short)(reference & 0xFFFF);

	if (set < 0 || set >= rt.point_set_count || index < 0 || index >= rt.point_sets[set].count)
		return FALSE;
	*point = rt.point_sets[set].points[index];
	/* (a point on a moving machine: where its node is now) */
	if (rt.point_sets[set].frames && rt.point_sets[set].frames[index] != NONE)
		frame_transform(rt.point_sets[set].frames[index], point);
	return TRUE;
}

static real distance_squared(real_point3d const *a, real_point3d const *b)
{
	real dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;

	return dx * dx + dy * dy + dz * dz;
}

/* the running command script's actor's unit's position (FALSE: none) */
static long actor_flown_vehicle(long actor_index);
static boolean vehicle_scripted_flier(long vehicle_index);
static real vehicle_definition_real(long vehicle_index, unsigned long offset, real fallback);

static boolean command_script_unit_origin(struct rt_command_script const *c, real_point3d *origin)
{
	struct actor_datum *actor;
	long vehicle;

	if (!c || !actor_alive(c->actor_index))
		return FALSE;
	actor = actor_get(c->actor_index);
	/* (a driver's: its vehicle's - Halo 2's driving actor is its vehicle) */
	vehicle = actor->meta.unit_index != NONE ? object_get(actor->meta.unit_index)->object.parent_object_index : NONE;
	if (vehicle != NONE && (!object_try_and_get(vehicle) || object_get(vehicle)->object.type != _object_type_vehicle ||
		unit_get(vehicle)->unit.driver_object_index != actor->meta.unit_index))
	{
		vehicle = NONE;
	}
	object_get_origin(vehicle != NONE ? vehicle : actor->meta.unit_index, origin);
	return TRUE;
}

/* (cs_go_to <point> [<real>]), cs_go_to_and_face, cs_fly_to, cs_fly_by,
cs_go_to_nearest: the actor goes there; the call waits until it is (or a
while has passed: a path it does not find, Halo 2's pathfinding failsafe) */
H2_ALIGN static short cs_go_to_step(struct rt_command_script *c, struct rt_cs_action *action)
{
	struct hs_function_definition const *function = h2_scripts_function_get(action->function_index);
	char const *name = function ? function->name : "";
	real_point3d point, origin;
	real tolerance = 0.6f;

	/* (cs_go_to_nearest <point set>: its point nearest the actor) */
	if (c && action->count > 0 && !strcmp(name, "cs_go_to_nearest") && command_script_unit_origin(c, &origin))
	{
		short set = (short)((unsigned long)action->arguments[0] >> 16), k;
		real best = 1.0e12f;

		for (k = 0; set >= 0 && set < rt.point_set_count && k < rt.point_sets[set].count; k++)
		{
			real_point3d candidate;

			if (point_get(((long)set << 16) | k, &candidate) && distance_squared(&origin, &candidate) < best)
			{
				best = distance_squared(&origin, &candidate);
				action->arguments[0] = ((long)set << 16) | k;
			}
		}
	}
	if (!c || action->count < 1 || !point_get(action->arguments[0], &point) || !command_script_unit_origin(c, &origin))
	{
		if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
			platform_log("h2 ai: %s skipped: command script %d, point %lx (%s), unit %d", name, c != NULL, action->count > 0 ? action->arguments[0] : -1L,
				action->count > 0 && point_get(action->arguments[0], &point) ? "found" : "not found", c && command_script_unit_origin(c, &origin));
		if (c)
			c->flags &= ~CS_MOVE;
		return CS_STEP_DONE;
	}
	if (action->started == NONE)
	{
		short cluster;

		action->started = game_time_get();
		if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
			platform_log("h2 ai: %s %.2f %.2f %.2f from %.2f %.2f %.2f (actor %lx)", name, point.x, point.y, point.z, origin.x, origin.y,
				origin.z, c->actor_index);
		c->flags |= CS_MOVE;
		c->flags &= ~CS_MOVE_BY;
		if (!strcmp(name, "cs_fly_by") || !strcmp(name, "cs_go_by"))
			c->flags |= CS_MOVE_BY;
		c->move_point = point;
		point_resolve(&point, &cluster, &c->move_surface);
		if ((!strcmp(name, "cs_go_to_and_face") || !strcmp(name, "cs_fly_to_and_face")) && action->count > 1 &&
			point_get(action->arguments[1], &c->look_point))
		{
			c->flags |= CS_FACE;
			c->look_object = NONE;
		}
		/* (cs_start_to: the actor sets off, the script goes on) */
		if (!strcmp(name, "cs_start_to"))
			return CS_STEP_DONE;
	}
	if (!strcmp(name, "cs_fly_by") || !strcmp(name, "cs_fly_to") || !strcmp(name, "cs_go_by"))
		tolerance = action->count > 1 && !strcmp(name, "cs_go_by") ? 1.5f : action->count > 1 ? MAX(argument_real(action->arguments[1]), 0.6f) : 1.0f;
	/* (a ground vehicle's driver: arrived nearer than its vehicle turns) */
	if (strncmp(name, "cs_fly", 6) && actor_alive(c->actor_index) && actor_get(c->actor_index)->meta.unit_index != NONE &&
		object_get(actor_get(c->actor_index)->meta.unit_index)->object.parent_object_index != NONE &&
		unit_get(object_get(actor_get(c->actor_index)->meta.unit_index)->object.parent_object_index)->unit.driver_object_index ==
		actor_get(c->actor_index)->meta.unit_index)
	{
		tolerance = MAX(tolerance, 2.0f);
	}
	/* (a Hunter, charging, led over the mesh: it is wide, its point at a wall) */
	if (actor_alive(c->actor_index) && actor_get(c->actor_index)->state.action == _actor_action_charge)
		tolerance = MAX(tolerance, 1.5f);
	/* (a flier - cs_fly_to, cs_fly_by - arrives in all three axes: 03a's
	Pelican, over its landing point 3 wu up, "arrived" by the ground units'
	level test and set its Marines down from the air, never landing) */
	if (distance_squared(&origin, &point) <= tolerance * tolerance ||
		(strncmp(name, "cs_fly", 6) &&
			(origin.x - point.x) * (origin.x - point.x) + (origin.y - point.y) * (origin.y - point.y) <= tolerance * tolerance * 0.5f))
	{
		rt.go_to_arrived++;
		if (game_time_get() - action->started > RT_GO_TO_TIMEOUT && HALO_ENV_ONCE("HALO_H2_AI_INDEX_LOG"))
			platform_log("h2 ai: %s: go-to %.2f %.2f %.2f arrived after %ld ticks", name, point.x, point.y, point.z, game_time_get() - action->started);
		c->flags &= ~CS_MOVE;
		return CS_STEP_DONE;
	}
	/* (no path there - a point on a moving machine, the tram's or an
	elevator's floor, which this engine's pathfinding does not walk; Halo 2
	paths over a mobile object's own sectors: the actor walks straight to
	it, a walking pace a tick, when it has had a moment to find a path and
	stands still without one) */
	if (game_time_get() - action->started > 20 && actor_alive(c->actor_index) && actor_get(c->actor_index)->meta.unit_index != NONE &&
		object_get(actor_get(c->actor_index)->meta.unit_index)->object.parent_object_index == NONE)
	{
		struct actor_datum *actor = actor_get(c->actor_index);

		/* (cs_enable_pathfinding_failsafe: hs_doc, "blocks until pathfinding
		calls succeed" - any distance, after ten seconds with no path: what the
		scripts wait on must arrive) */
		if (!actor->control.path.path.valid && !actor->control.moving && actor->meta.unit_index != NONE &&
			(distance_squared(&origin, &point) < 10.0f * 10.0f ||
			((c->flags & CS_FAILSAFE) && game_time_get() - action->started > 10 * 30)))
		{
			real_vector3d step = { point.x - origin.x, point.y - origin.y, point.z - origin.z };
			real length = (real)sqrt(step.i * step.i + step.j * step.j + step.k * step.k);
			real_vector3d forward = { step.i, step.j, 0.0f };
			real flat = (real)sqrt(forward.i * forward.i + forward.j * forward.j);
			real_point3d next;

			if (length > 0.0f)
			{
				real move = MIN(length, 0.08f) / length;

				next.x = origin.x + step.i * move;
				next.y = origin.y + step.j * move;
				next.z = origin.z + step.k * move;
				if (flat > 0.001f)
				{
					forward.i /= flat, forward.j /= flat;
					object_set_position(actor->meta.unit_index, &next, &forward, NULL);
				}
				else
					object_set_position(actor->meta.unit_index, &next, NULL, NULL);
				if (!rt.go_to_glides++ && HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
					platform_log("h2 ai: %s: no path to %.2f %.2f %.2f, walking straight", name, point.x, point.y, point.z);
			}
		}
	}
	/* (no time limit: Halo 2's go-to ends when the actor arrives, or when its
	movement fails - no path, and not moving - unless
	cs_enable_pathfinding_failsafe holds it (the command script's +0x76),
	which tries again: halo2-decompiled unknown_257d00.cpp 0x259430 (retail;
	its go-to arm at 0x2595e9 reads the movement state 0x1f86f0 arrived,
	0x1f8720 failed), 0x258cf0, the flag's setter unknown_29f5b0.cpp
	0x2a8910. Failed here: this engine's path not valid and the actor not
	moving, once it has had a moment to find one and the straight walk above
	does not take it) */
	if (game_time_get() - action->started > 20 && !(c->flags & CS_FAILSAFE) && actor_alive(c->actor_index))
	{
		struct actor_datum *actor = actor_get(c->actor_index);

		/* (on foot only: a flier's and a driver's moves are this file's
		(flyers_update, the vehicles' drivers), not this engine's paths - their
		go-tos end on arriving) */
		if (actor->meta.unit_index != NONE && object_get(actor->meta.unit_index)->object.parent_object_index == NONE &&
			!actor->state.flying && !actor->control.path.path.valid && !actor->control.moving &&
			distance_squared(&origin, &point) >= 10.0f * 10.0f)
		{
			rt.go_to_timed_out++;
			c->flags &= ~CS_MOVE;
			if (HALO_ENV_ONCE("HALO_H2_AI_INDEX_LOG"))
				platform_log("h2 ai: %s: go-to %.2f %.2f %.2f failed (no path) after %ld ticks", name, point.x, point.y, point.z,
					game_time_get() - action->started);
			return CS_STEP_DONE;
		}
	}
	/* (the log: a go-to still waiting after 30 s - where this port gave up
	before; Halo 2 waits on) */
	/* (the log: a go-to waiting past 30 s, where it is every 5 s) */
	if (game_time_get() - action->started > RT_GO_TO_TIMEOUT && (game_time_get() - action->started) % 150 == 0 && HALO_ENV_ONCE("HALO_H2_AI_INDEX_LOG"))
	{
		struct actor_datum *actor = actor_alive(c->actor_index) ? actor_get(c->actor_index) : NULL;
		long vehicle = actor && actor->meta.unit_index != NONE ? object_get(actor->meta.unit_index)->object.parent_object_index : NONE;
		real_vector3d const *v = vehicle != NONE ? &object_get(vehicle)->object.translational_velocity : NULL;

		platform_log("h2 ai: %s: go-to %.2f %.2f %.2f waiting %ld ticks: at %.2f %.2f %.2f (%.1f m), action %d, moving %d, path %d, "
			"driver type %d, throttle %.2f %.2f, vehicle speed %.3f", name, point.x, point.y, point.z, game_time_get() - action->started,
			origin.x, origin.y, origin.z, (real)sqrt(distance_squared(&origin, &point)), actor ? (int)actor->state.action : -1,
			actor ? (int)actor->control.moving : -1, actor ? (int)actor->control.path.path.valid : -1,
			actor ? (int)actor->input.vehicle_driver_type : -1, actor ? actor->output.throttle.i : 0.0f, actor ? actor->output.throttle.j : 0.0f,
			v ? (real)sqrt(v->i * v->i + v->j * v->j + v->k * v->k) : -1.0f);
	}
	if (game_time_get() - action->started == RT_GO_TO_TIMEOUT)
	{
		rt.go_to_long_waits++;
		if (HALO_ENV_ONCE("HALO_H2_AI_INDEX_LOG"))
			platform_log("h2 ai: %s: go-to %.2f %.2f %.2f still waiting after 30 s (from %.2f %.2f %.2f; actor %lx, %s%s)", name, point.x, point.y, point.z,
				origin.x, origin.y, origin.z, (unsigned long)c->actor_index,
				actor_alive(c->actor_index) && actor_get(c->actor_index)->meta.unit_index != NONE &&
				object_get(actor_get(c->actor_index)->meta.unit_index)->object.parent_object_index != NONE ?
				tag_get_name(object_get(object_get(actor_get(c->actor_index)->meta.unit_index)->object.parent_object_index)->definition_index) : "on foot",
				actor_alive(c->actor_index) && actor_get(c->actor_index)->state.flying ? ", flying" : "");
	}
	return CS_STEP_CONTINUE;
}

H2_ALIGN static void cs_go_to_evaluate(short function_index, long thread_index, boolean initialize)
{
	command_script_action_evaluate(function_index, thread_index, initialize, 2, cs_go_to_step);
}

/* (cs_face <boolean> <point>), cs_aim, cs_look, cs_face_player,
cs_aim_player, cs_look_player, cs_shoot, cs_shoot_point, cs_enable_moving,
cs_crouch, cs_abort_on_damage: the actor's orders overridden for the rest
of its command script */
/* the point an actor looks, aims and fires at on an object (a command
script's cs_face_object, cs_aim_object, cs_look_object, cs_shoot <object>):
Halo 2's AI point of an object (ai.cpp 0x1caa40) - a biped's or vehicle's
"body" marker (unit 0xcaf90, string id 0x40000bd), any other object's centre
(Halo 2 a free physics object's centre of mass). Before: the object's origin
raised 0.5: 08b's Johnson, told to face and aim at Tartarus, aimed 3-4
degrees under his head and the fight's "objects_can_see_object .25" (from his
head along his look to Tartarus's head) never passed while Tartarus stood.
HALO_H2_CS_OBJECT_POINT_OLD=1: the old point.
Looking (a facing or looking order, not aiming or shooting), Halo 2 takes a
biped's or vehicle's HEAD instead: the target resolver 0x2967b0, case 6 (an
object) with aiming false, reads unit 0xcaf60 - the "head" marker, string id
0x4000095 - (a player's eye, 0xcafc0), and only the aiming arm 0x1caa40's body
marker. 08b's Johnson thus looks at Tartarus's head, where the fight's
objects_can_see_object .25 tests (0x29f7a0: the target's head marker, from the
looker's head along its look); on the body point his look sat 1.6-2.2 degrees
under it and FIRE waited for Tartarus to move through the line.
HALO_H2_CS_LOOK_BODY=1: the body point for looking too (before) */
static void cs_object_point(long object_index, real_point3d *point, boolean looking)
{
	struct object_datum *object = object_get(object_index);

	if (HALO_ENV_ONCE("HALO_H2_CS_OBJECT_POINT_OLD"))
	{
		object_get_origin(object_index, point);
		point->z += 0.5f;
	}
	else if (looking && (object->object.type == _object_type_biped || object->object.type == _object_type_vehicle) &&
		!HALO_ENV_ONCE("HALO_H2_CS_LOOK_BODY"))
	{
		long player_index = ((struct unit_datum *)object)->unit.player_index;

		if (player_index != NONE)
			unit_get_head_position(object_index, point);
		else
		{
			struct object_marker marker;

			object_get_marker_by_name(object_index, "head", &marker, 1);
			*point = marker.matrix.position;
		}
	}
	else if (object->object.type == _object_type_biped || object->object.type == _object_type_vehicle)
	{
		struct object_marker marker;

		object_get_marker_by_name(object_index, "body", &marker, 1);
		*point = marker.matrix.position;
	}
	else
		*point = object->object.bounding_sphere_center;
}

H2_ALIGN static void cs_override_evaluate(short function_index, long thread_index, boolean initialize)
{
	struct hs_function_definition const *function = h2_scripts_function_get(function_index);
	char const *name = function ? function->name : "";
	short count;
	long *a = arguments(thread_index, 2, &count, initialize);
	struct rt_command_script *c;
	boolean on;

	if (!a)
		return;
	c = command_script_of_thread(thread_index);
	on = count > 0 && argument_boolean(a[0]);
	if (c)
	{
		unsigned long bit = 0;
		boolean has_point = FALSE;
		boolean shooting = !strcmp(name, "cs_shoot") || !strcmp(name, "cs_shoot_point");

		if (!strncmp(name, "cs_face", 7))
			bit = CS_FACE;
		else if (!strncmp(name, "cs_aim", 6))
			bit = CS_AIM;
		else if (!strncmp(name, "cs_look", 7))
			bit = CS_LOOK;
		else if (shooting)
			bit = CS_SHOOT;
		else if (!strcmp(name, "cs_enable_moving"))
			bit = CS_MOVING;
		else if (!strcmp(name, "cs_crouch"))
			bit = CS_CROUCH;
		else if (!strcmp(name, "cs_abort_on_damage"))
			bit = CS_ABORT_ON_DAMAGE;
		if (strstr(name, "_player"))
		{
			/* (the nearest player: the first local one's unit) */
			long unit = local_player_get_player_index(0) != NONE ?
				player_get(local_player_get_player_index(0))->unit_index : NONE;

			if (unit != NONE && object_try_and_get(unit))
			{
				object_get_origin(unit, &c->look_point);
				c->look_point.z += 0.6f;
				has_point = TRUE;
			}
		}
		else if (count > 1 && strstr(name, "_object"))
		{
			/* (cs_face_object, cs_aim_object, cs_look_object: the object's
			place each tick) */
			c->look_object = object_try_and_get(a[1]) ? a[1] : NONE;
			if (c->look_object != NONE)
				cs_object_point(c->look_object, &c->look_point, bit != CS_AIM);
			has_point = c->look_object != NONE;
		}
		else if (shooting)
		{
			/* (cs_shoot true <object>, cs_shoot_point true <point>: aiming
			there and firing at it, function_25a380's 0x48c fire point; with
			neither, at the target it has) */
			c->flags &= ~CS_SHOOT_POINT;
			if (count > 1 && on)
			{
				if (!strcmp(name, "cs_shoot"))
				{
					c->look_object = object_try_and_get(a[1]) ? a[1] : NONE;
					if (c->look_object != NONE)
					{
						cs_object_point(c->look_object, &c->look_point, FALSE);
						c->flags |= CS_SHOOT_POINT | CS_AIM;
					}
				}
				else if (point_get(a[1], &c->look_point))
				{
					c->look_object = NONE;
					c->flags |= CS_SHOOT_POINT | CS_AIM;
				}
			}
			else if (count > 1)
				c->flags &= ~CS_AIM;
			if (on)
				c->flags &= ~CS_NO_SHOOT;
			else
				c->flags |= CS_NO_SHOOT;
		}
		else if (count > 1)
		{
			has_point = point_get(a[1], &c->look_point);
			c->look_object = NONE;
		}
		if (bit && (on && (has_point || !(bit & (CS_FACE | CS_AIM | CS_LOOK)))))
			c->flags |= bit;
		else if (bit)
			c->flags &= ~bit;
	}
	hs_return(thread_index, 0);
}

/* (cs_vehicle_speed <real>): its vehicle's top speed's fraction, for the
rest of its command script (flyers_update; 1 when the script starts) */
H2_ALIGN static void cs_vehicle_speed_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count;
	long *a = arguments(thread_index, 1, &count, initialize);
	struct rt_command_script *c;

	if (!a)
		return;
	c = command_script_of_thread(thread_index);
	if (c && count > 0)
		c->speed = MAX(0.05f, MIN(1.0f, argument_real(a[0])));
	hs_return(thread_index, 0);
	(void)function_index;
}

/* (cs_abort_on_combat_status <short>), (cs_abort_on_alert <boolean>): the
command script ends when its actor's combat status reaches the level */
H2_ALIGN static void cs_abort_on_status_evaluate(short function_index, long thread_index, boolean initialize)
{
	struct hs_function_definition const *function = h2_scripts_function_get(function_index);
	short count;
	long *a = arguments(thread_index, 1, &count, initialize);
	struct rt_command_script *c;

	if (!a)
		return;
	c = command_script_of_thread(thread_index);
	if (c && count > 0)
	{
		if (function && !strcmp(function->name, "cs_abort_on_alert"))
			c->abort_status = argument_boolean(a[0]) ? 2 : NONE;
		else
			c->abort_status = argument_short(a[0]) > 0 ? argument_short(a[0]) : NONE;
	}
	hs_return(thread_index, 0);
}

/* (cs_moving): whether the command script's actor is still on its way */
H2_ALIGN static void cs_moving_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count;
	long *a = arguments(thread_index, 0, &count, initialize);
	struct rt_command_script *c;

	if (!a)
		return;
	c = command_script_of_thread(thread_index);
	hs_return(thread_index, value_boolean(c && (c->flags & CS_MOVE)));
	(void)function_index;
}

/* (object_damage_damage_section <object> <section> <real>): a section
damaged its whole (1) breaks; this engine's scenery and machines have no
sections, so one so broken is gone - 01b's blast door, which the boarders
come through (its explosion is the script's own damage_new) */
H2_ALIGN static void object_damage_damage_section_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count;
	long *a = arguments(thread_index, 3, &count, initialize);
	struct object_datum *object;

	if (!a)
		return;
	object = count > 2 && a[0] != NONE ? object_try_and_get(a[0]) : NULL;
	/* (a vehicle's: its Halo 2 section takes it - the insertion pods' door, 500,
	off with its ejection effect, as 03a, 06a and 08a's scripts open them) */
	if (object && object->object.type == _object_type_vehicle)
	{
		extern boolean h2_vehicle_damage_section(long vehicle_index, char const *name);

		if (argument_real(a[2]) >= 0.999f && !h2_vehicle_damage_section(a[0], (char const *)a[1]))
			platform_log("h2 ai: %s has no damage section %s", tag_get_name(object->definition_index), (char const *)a[1]);
	}
	else if (object && argument_real(a[2]) >= 0.999f &&
		!TEST_FLAG(_object_mask_unit, object->object.type) && object->object.type != _object_type_vehicle)
	{
		rt.sections_broken++;
		object_delete(a[0]);
	}
	hs_return(thread_index, 0);
	(void)function_index;
}

/* (ai_berserk <ai> <boolean>): this engine's berserking, actor by actor */
H2_ALIGN static void ai_berserk_evaluate(short function_index, long thread_index, boolean initialize)
{
	void actor_berserk(long actor_index, boolean berserk);
	short count;
	long *a = arguments(thread_index, 2, &count, initialize);

	if (!a)
		return;
	if (count > 1)
	{
		long actors[RT_MAXIMUM_ACTORS];
		short n = actors_of(a[0], actors, RT_MAXIMUM_ACTORS), k;

		for (k = 0; k < n; k++)
			actor_berserk(actors[k], argument_boolean(a[1]));
	}
	hs_return(thread_index, 0);
	(void)function_index;
}

/* (ai_teleport_to_starting_location_if_outside_bsp <ai>): hs_doc; an actor out
of the world - a BSP switch left it behind - at its starting location */
H2_ALIGN static void ai_teleport_outside_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count;
	long *a = arguments(thread_index, 1, &count, initialize);

	if (!a)
		return;
	if (count > 0)
	{
		long actors[RT_MAXIMUM_ACTORS];
		short n = actors_of(a[0], actors, RT_MAXIMUM_ACTORS), k;

		for (k = 0; k < n; k++)
		{
			struct actor_datum *actor = actor_get(actors[k]);
			struct object_datum *unit = actor->meta.unit_index != NONE ? object_try_and_get(actor->meta.unit_index) : NULL;
			short squad = actor_alive(actors[k]) ? actor_h2_squad(actors[k]) : NONE;

			if (unit && unit->object.location.cluster_index == NONE && squad != NONE && rt.squads[squad].location_count > 0)
				object_set_position(actor->meta.unit_index, &rt.squads[squad].locations[k % rt.squads[squad].location_count], NULL, NULL);
		}
	}
	hs_return(thread_index, 0);
	(void)function_index;
}

/* (cs_stop_custom_animation): the running actor's custom animation ended */
H2_ALIGN static void cs_stop_custom_animation_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count;
	long *a = arguments(thread_index, 0, &count, initialize);
	struct rt_command_script *c = a ? command_script_of_thread(thread_index) : NULL;

	if (!a)
		return;
	if (c && actor_alive(c->actor_index) && actor_get(c->actor_index)->meta.unit_index != NONE)
		h2_cinematics_object_stop(actor_get(c->actor_index)->meta.unit_index);
	hs_return(thread_index, 0);
	(void)function_index;
}

/* (object_set_permutation <object> <string_id region> <string_id
permutation>): its region shows that permutation (Halo 2's empty one:
"none", the region hidden) */
H2_ALIGN static void object_set_permutation_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count;
	long *a = arguments(thread_index, 3, &count, initialize);
	struct object_datum *object;

	if (!a)
		return;
	object = count > 2 && a[0] != NONE ? object_try_and_get(a[0]) : NULL;
	if (object && a[1] && a[2])
	{
		struct object_definition *definition = object_definition_get(object->definition_index);
		struct model *model = definition->object.model.index != NONE ? model_definition_get(definition->object.model.index) : NULL;
		short region = model ? model_region_find(model, (char const *)a[1]) : NONE;
		short permutation = region != NONE ? model_permutation_find(TAG_BLOCK_GET_ELEMENT(&model->regions, region, struct model_region),
			((char const *)a[2])[0] ? (char const *)a[2] : "none") : NONE;

		if (permutation != NONE)
			object_region_permutation_set(a[0], region, (byte)permutation);
	}
	hs_return(thread_index, 0);
	(void)function_index;
}

/* (object_set_region_state <object> <string_id region> <model_state>): the
region shows its permutation of that state, hs_doc; 01b's two ODSTs' helmets.
Model states: 0 default, 1 minor damage, 2 medium, 3 major, 4 destroyed. This
engine's regions keep permutations by name */
H2_ALIGN static void object_set_region_state_evaluate(short function_index, long thread_index, boolean initialize)
{
	static char const *const states[] = { "default", "minor damage", "medium damage", "major damage", "destroyed" };
	short count;
	long *a = arguments(thread_index, 3, &count, initialize);
	struct object_datum *object;

	if (!a)
		return;
	object = count > 2 && a[0] != NONE ? object_try_and_get(a[0]) : NULL;
	if (object && a[1] && (short)a[2] >= 0 && (short)a[2] < (short)NUMBEROF(states))
	{
		struct object_definition *definition = object_definition_get(object->definition_index);
		struct model *model = definition->object.model.index != NONE ? model_definition_get(definition->object.model.index) : NULL;
		short region = model ? model_region_find(model, (char const *)a[1]) : NONE;

		if (region != NONE)
		{
			struct model_region *r = TAG_BLOCK_GET_ELEMENT(&model->regions, region, struct model_region);
			short permutation = model_permutation_find(r, states[(short)a[2]]);

			/* (destroyed with no such permutation: hidden) */
			if (permutation == NONE && (short)a[2] == 4)
				permutation = model_permutation_find(r, "none");
			if (permutation != NONE)
				object_region_permutation_set(a[0], region, (byte)permutation);
		}
	}
	hs_return(thread_index, 0);
	(void)function_index;
}

/* (unit_set_emotional_state <unit> <string_id emotion> <real weight>
<short ticks>): the emotion on its face, over the ticks (h2_cinematics.c) */
H2_ALIGN static void unit_set_emotional_state_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count;
	long *a = arguments(thread_index, 4, &count, initialize);

	if (!a)
		return;
	if (count > 3 && a[0] != NONE && a[1])
		h2_cinematics_emotion(a[0], (char const *)a[1], argument_real(a[2]), (long)(short)a[3]);
	hs_return(thread_index, 0);
	(void)function_index;
}

/* (tests: @bipedvariant) a character shows the variant so named; FALSE if
its model has none */
H2_ALIGN boolean h2_ai_object_variant_named(
	long object_index,
	char const *name)
{
	short variant = object_index != NONE && object_try_and_get(object_index) ? variant_find(object_index, name) : NONE;

	if (variant != NONE)
		h2_ai_object_variant(object_index, variant);
	return variant != NONE;
}

/* (object_set_variant <object> <string_id>): a character shows that
variant of its model's */
H2_ALIGN static void object_set_variant_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count;
	long *a = arguments(thread_index, 2, &count, initialize);

	if (!a)
		return;
	if (count > 1 && a[0] != NONE && a[1] && object_try_and_get(a[0]))
	{
		short variant = variant_find(a[0], (char const *)a[1]);

		if (variant != NONE)
			h2_ai_object_variant(a[0], variant);
	}
	hs_return(thread_index, 0);
	(void)function_index;
}

/* (ai_cannot_die <ai> <boolean>), (object_cannot_die <object> <boolean>):
hurt, never killed (damage.c asks h2_ai_object_cannot_die) */
H2_ALIGN static void cannot_die_set(long object_index, boolean on)
{
	short i;

	for (i = 0; i < rt.cannot_die_count && rt.cannot_die[i] != object_index; i++)
		;
	if (on && i == rt.cannot_die_count && rt.cannot_die_count < (short)NUMBEROF(rt.cannot_die))
		rt.cannot_die[rt.cannot_die_count++] = object_index;
	else if (!on && i < rt.cannot_die_count)
		rt.cannot_die[i] = rt.cannot_die[--rt.cannot_die_count];
}

H2_ALIGN boolean h2_ai_object_cannot_die(
	long object_index)
{
	short i;

	for (i = 0; i < rt.cannot_die_count; i++)
	{
		if (rt.cannot_die[i] == object_index)
			return TRUE;
	}
	return FALSE;
}

H2_ALIGN static void cannot_die_evaluate(short function_index, long thread_index, boolean initialize)
{
	struct hs_function_definition const *function = h2_scripts_function_get(function_index);
	short count, k, actor_count;
	long *a = arguments(thread_index, 2, &count, initialize);
	long actors[RT_MAXIMUM_ACTORS];

	if (!a)
		return;
	if (count > 1 && function && !strncmp(function->name, "object", 6))
	{
		if (a[0] != NONE && object_try_and_get(a[0]))
			cannot_die_set(a[0], argument_boolean(a[1]));
	}
	else if (count > 1)
	{
		actor_count = actors_of(a[0], actors, RT_MAXIMUM_ACTORS);
		for (k = 0; k < actor_count; k++)
		{
			if (actor_get(actors[k])->meta.unit_index != NONE)
				cannot_die_set(actor_get(actors[k])->meta.unit_index, argument_boolean(a[1]));
		}
	}
	hs_return(thread_index, 0);
}

/* (ai_vitality_pinned <ai>): one of its actors held alive - ai_cannot_die,
object_cannot_die - with its body spent (Halo 2's bosses: 05b's Regret,
08b's Tartarus, a fight's end when it is) */
H2_ALIGN static void ai_vitality_pinned_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count, k, actor_count;
	long *a = arguments(thread_index, 1, &count, initialize);
	long actors[RT_MAXIMUM_ACTORS];
	boolean pinned = FALSE;

	if (!a)
		return;
	actor_count = count > 0 ? actors_of(a[0], actors, RT_MAXIMUM_ACTORS) : 0;
	for (k = 0; k < actor_count && !pinned; k++)
	{
		long unit_index = actor_get(actors[k])->meta.unit_index;

		pinned = unit_index != NONE && object_try_and_get(unit_index) && h2_ai_object_cannot_die(unit_index) &&
			object_get(unit_index)->object.body_vitality <= 0.0f;
	}
	hs_return(thread_index, value_boolean(pinned));
	(void)function_index;
}

/* (ai_set_active_camo <ai> <boolean>): its units camouflaged */
H2_ALIGN static void ai_set_active_camo_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count, k, actor_count;
	long *a = arguments(thread_index, 2, &count, initialize);
	long actors[RT_MAXIMUM_ACTORS];

	if (!a)
		return;
	actor_count = count > 1 ? actors_of(a[0], actors, RT_MAXIMUM_ACTORS) : 0;
	for (k = 0; k < actor_count; k++)
	{
		long unit_index = actor_get(actors[k])->meta.unit_index;

		if (unit_index != NONE && object_try_and_get(unit_index))
			SET_FLAG(unit_get(unit_index)->unit.flags, _unit_active_camouflaged_bit, argument_boolean(a[1]));
	}
	hs_return(thread_index, 0);
	(void)function_index;
}

/* the riders (an ai index: squads placed) placed riding the carrier's (an
ai index) vehicle - its free seats, gunners' then passengers' */
H2_ALIGN static void place_in_vehicle(long riders_index, long carrier_index)
{
	static char const *const riders[] = { "_g", "gunner", "_p", "passenger", "rider", "_d", "driver", NULL };
	short squads[MAXIMUM_SQUADS], squad_count, i, k, actor_count;
	long actors[RT_MAXIMUM_ACTORS], vehicle = NONE;

	actor_count = actors_of(carrier_index, actors, RT_MAXIMUM_ACTORS);
	for (k = 0; k < actor_count && vehicle == NONE; k++)
	{
		long unit_index = actor_get(actors[k])->meta.unit_index;
		long parent = unit_index != NONE && object_try_and_get(unit_index) ? object_get(unit_index)->object.parent_object_index : NONE;

		if (parent != NONE && object_try_and_get(parent) && object_get(parent)->object.type == _object_type_vehicle)
			vehicle = parent;
	}
	squad_count = squads_of(riders_index, squads, MAXIMUM_SQUADS);
	for (i = 0; i < squad_count; i++)
		squad_place(squads[i], NONE, ((unsigned long)riders_index >> 30) == 3 ? (short)(riders_index & 0xFFFF) : NONE);
	if (vehicle != NONE)
	{
		short seated = 0;

		actor_count = actors_of(riders_index, actors, RT_MAXIMUM_ACTORS);
		for (k = 0; k < actor_count; k++)
		{
			long unit_index = actor_get(actors[k])->meta.unit_index;

			if (unit_index != NONE && object_try_and_get(unit_index) && object_get(unit_index)->object.parent_object_index == NONE &&
				seat_take(unit_index, vehicle, riders))
			{
				rt.vehicles_boarded++;
				seated++;
			}
		}
		/* (a rider driving its own vehicle - a Ghost's squad placed in a
		Phantom -: its vehicle the carrier's cargo, at a free cargo seat's
		marker - Halo 2's large or small cargo, "_lc" / "_sc") */
		for (k = 0; k < actor_count; k++)
		{
			long unit_index = actor_get(actors[k])->meta.unit_index;
			long own = unit_index != NONE && object_try_and_get(unit_index) ? object_get(unit_index)->object.parent_object_index : NONE;

			if (own != NONE && own != vehicle && object_try_and_get(own) && object_get(own)->object.type == _object_type_vehicle &&
				unit_get(own)->unit.driver_object_index == unit_index)
			{
				struct unit_definition *carrier = unit_definition_get(object_get(vehicle)->definition_index);
				short q;

				for (q = 0; q < carrier->unit.seats.count && rt.cargo_count < (short)NUMBEROF(rt.cargo); q++)
				{
					struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(&carrier->unit.seats, q, struct unit_seat);
					short n;

					if (!strstr(seat->label, "_sc") && !strstr(seat->label, "_lc"))
						continue;
					for (n = 0; n < rt.cargo_count && !(rt.cargo[n].carrier == vehicle && !strcmp(rt.cargo[n].label, seat->label)); n++)
						;
					if (n < rt.cargo_count)
						continue;
					/* (a small vehicle - a Ghost - in a small cargo seat) */
					if (strstr(seat->label, "_lc") && object_get(own)->object.bounding_sphere_radius < 2.0f)
						continue;
					/* (carried, not attached - this engine takes a unit
					whose parent is a unit for a rider in a seat -: flyers_update
					holds it at the marker) */
					rt.cargo[rt.cargo_count].carrier = vehicle;
					rt.cargo[rt.cargo_count].cargo = own;
					snprintf(rt.cargo[rt.cargo_count].label, sizeof(rt.cargo[0].label), "%s", seat->label);
					snprintf(rt.cargo[rt.cargo_count].marker, sizeof(rt.cargo[0].marker), "%s", seat->marker_name);
					rt.cargo_count++;
					seated++;
					break;
				}
			}
		}
		if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
			platform_log("h2 ai: ai_place_in_vehicle: %d of %d riding %s (%d cargo)", seated, actor_count,
				tag_get_name(object_get(vehicle)->definition_index), rt.cargo_count);
	}
}

/* (ai_place_in_vehicle <ai> <ai>): the first placed, riding the second's
vehicle */
H2_ALIGN static void ai_place_in_vehicle_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count;
	long *a = arguments(thread_index, 2, &count, initialize);

	if (!a)
		return;
	if (count > 1)
		place_in_vehicle(a[0], a[1]);
	hs_return(thread_index, 0);
	(void)function_index;
}

boolean ai_globals_ai_active_get(void);

/* ---------- the runtime: ai_disposable (halo2-decompiled unknown_272b70.cpp
0x2738a0, unknown_26b230.cpp 0x268c90, unknown_0b8ca0.cpp 0xbba80) */

/* (ai_disposable <ai> <boolean>): each actor the index names is disposable
(or not) - Halo 2's actor +0x0c -, and a disposable one's squad has a
disposal pending (the clump's +0x3c) */
H2_ALIGN static void ai_disposable_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count, actor_count, k;
	long *a = arguments(thread_index, 2, &count, initialize);
	long actors[RT_MAXIMUM_ACTORS];

	if (!a)
		return;
	actor_count = count > 1 ? actors_of(a[0], actors, RT_MAXIMUM_ACTORS) : 0;
	for (k = 0; k < actor_count; k++)
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(actors[k]);
		boolean on = argument_boolean(a[1]);
		short squad = actor_h2_squad(actors[k]);

		if (absolute >= RT_MAXIMUM_ACTORS)
			continue;
		rt.actor_disposable[absolute] = on ? actors[k] : NONE;
		if (on && squad != NONE)
			rt.squads[squad].dispose_pending = TRUE;
	}
	if (HALO_ENV_ONCE("HALO_H2_AI_DISPOSAL_LOG"))
		platform_log("h2 ai disposal: ai_disposable %lx %d: %d actors", (unsigned long)a[0], count > 1 ? (int)argument_boolean(a[1]) : -1, (int)actor_count);
	hs_return(thread_index, 0);
	(void)function_index;
}

/* whether a player can see an object (0xbba80): its root object connected
to the map, in a cluster the players' PVS holds, and a player's head within
its bounding radius, or within 1.2 times its model's disappear distance (40
m with none) and the object inside 45 degrees (and its angular radius) of
the player's looking direction - distances over the player's zoom */
H2_ALIGN static boolean disposal_visible_to_player(long object_index)
{
	struct object_datum *object = object_get(object_index), *root_object;
	struct object_header_datum *header;
	unsigned long const *pvs = players_get_combined_pvs();
	long root = object_index;
	real radius_squared = object->object.bounding_sphere_radius * object->object.bounding_sphere_radius, range_squared = 1600.0f;
	struct data_iterator iterator;
	struct player_datum *player;
	short b;

	while (object_get(root)->object.parent_object_index != NONE)
		root = object_get(root)->object.parent_object_index;
	header = object_header_get(root);
	root_object = object_get(root);
	if (!TEST_FLAG(header->flags, _object_header_active_bit) || !TEST_FLAG(root_object->object.flags, _object_connected_to_map_bit) ||
		TEST_FLAG(root_object->object.flags, _object_outside_of_map_bit) || header->cluster_index == NONE || !pvs ||
		!BIT_VECTOR_TEST_FLAG(pvs, header->cluster_index))
	{
		return FALSE;
	}
	for (b = 0; b < RT_VARIANT_BIPEDS; b++)
	{
		if (rt_variant_bipeds[b].unit_definition == object->definition_index && rt_variant_bipeds[b].disappear_distance > 0.0f)
			range_squared = (1.2f * rt_variant_bipeds[b].disappear_distance) * (1.2f * rt_variant_bipeds[b].disappear_distance);
	}
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		real_point3d head;
		real_vector3d offset;
		real distance_squared, magnitude, zoom;
		struct unit_datum *unit;

		if (player->unit_index == NONE || !object_try_and_get(player->unit_index))
			continue;
		unit = unit_get(player->unit_index);
		unit_get_head_position(player->unit_index, &head);
		offset.i = object->object.bounding_sphere_center.x - head.x;
		offset.j = object->object.bounding_sphere_center.y - head.y;
		offset.k = object->object.bounding_sphere_center.z - head.z;
		distance_squared = offset.i * offset.i + offset.j * offset.j + offset.k * offset.k;
		zoom = unit_get_zoom_magnification(player->unit_index, unit_get_zoom_level(player->unit_index));
		if (zoom > 0.0001f)
			distance_squared /= zoom * zoom;
		if (distance_squared < radius_squared)
			return TRUE;
		if (distance_squared < range_squared)
		{
			magnitude = normalize3d(&offset);
			if (offset.i * unit->unit.looking_vector.i + offset.j * unit->unit.looking_vector.j + offset.k * unit->unit.looking_vector.k >
				(real)cos(atan2(object->object.bounding_sphere_radius, magnitude) + 0.7853981852531433))
			{
				return TRUE;
			}
		}
	}
	return FALSE;
}

/* (0x264330 -> 0x268c60) a squad's actors seeing an enemy within 25 m: the
squad's time stamped (Halo 2's perception sight 0 or 1 - seen -: this
engine's visibility partial or full) */
H2_ALIGN static void disposal_enemy_close_update(void)
{
	struct data_iterator iterator;

	data_iterator_new(&iterator, actor_data);
	while (data_iterator_next(&iterator))
	{
		short squad = actor_h2_squad(iterator.datum_index);
		struct prop_iterator props;
		struct prop_datum *prop;

		if (squad == NONE)
			continue;
		prop_iterator_new(&props, iterator.datum_index);
		while ((prop = prop_iterator_next(&props)) != NULL)
		{
			if (prop->enemy && prop->visibility > _actor_perception_none && prop->distance < 25.0f)
			{
				rt.squads[squad].enemy_close_time = game_time_get();
				break;
			}
		}
	}
}

/* (0x268c90, once a second) a squad with a disposal pending whose actors saw
no enemy within 25 m for 10 s: each disposable actor erased with its unit
(and its vehicle, unless a biped rides it), unless it is active and a player
is within 15 m of it or sees its unit - then it waits for the next pass */
H2_ALIGN static void disposal_update(void)
{
	short s;

	for (s = 0; s < rt.squad_count; s++)
	{
		struct rt_squad *squad = &rt.squads[s];
		long actors[RT_MAXIMUM_ACTORS];
		short actor_count, k;

		if (!squad->dispose_pending ||
			(squad->enemy_close_time != NONE && squad->enemy_close_time + 10 * 30 >= game_time_get()))
		{
			continue;
		}
		squad->dispose_pending = FALSE;
		actor_count = actors_of(s, actors, RT_MAXIMUM_ACTORS);
		for (k = 0; k < actor_count; k++)
		{
			long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(actors[k]);
			struct actor_datum *actor = actor_get(actors[k]);
			boolean remove = TRUE;
			long vehicle = NONE, unit = actor->meta.unit_index;

			if (absolute >= RT_MAXIMUM_ACTORS || rt.actor_disposable[absolute] != actors[k])
				continue;
			if (actor->meta.active)
			{
				struct data_iterator iterator;
				struct player_datum *player;
				boolean keep = FALSE;
				real_point3d at;

				if (unit != NONE)
					object_get_origin(unit, &at);
				data_iterator_new(&iterator, player_data);
				while (unit != NONE && !keep && (player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
				{
					real_point3d p;

					if (player->unit_index == NONE || !object_try_and_get(player->unit_index))
						continue;
					object_get_origin(player->unit_index, &p);
					keep = distance_squared(&p, &at) < 225.0f;
				}
				if (!keep && unit != NONE)
					keep = disposal_visible_to_player(unit);
				remove = !keep;
			}
			if (!remove)
			{
				squad->dispose_pending = TRUE;
				rt.disposal_kept++;
				continue;
			}
			if (unit != NONE && object_get(unit)->object.parent_object_index != NONE)
				vehicle = object_get(unit)->object.parent_object_index;
			if (HALO_ENV_ONCE("HALO_H2_AI_DISPOSAL_LOG"))
				platform_log("h2 ai disposal: %s: actor %lx erased%s", squad->name, (unsigned long)actors[k], vehicle != NONE ? " (in a vehicle)" : "");
			rt.actor_disposable[absolute] = NONE;
			actor_erase(actors[k], FALSE);
			rt.disposed_actors++;
			/* (its vehicle, unless a biped is still in it) */
			if (vehicle != NONE && object_try_and_get(vehicle))
			{
				long child = object_get(vehicle)->object.first_child_object_index;
				boolean biped = FALSE;

				for (; child != NONE && !biped; child = object_get(child)->object.next_object_index)
					biped = object_header_get(child)->type == _object_type_biped;
				if (!biped)
				{
					object_delete(vehicle);
					rt.disposed_vehicles++;
				}
			}
		}
	}
}

/* (cs_enable_pathfinding_failsafe <boolean>)...: accepted, nothing to do
here (this engine's paths) */
H2_ALIGN static void accepted_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count;
	long *a = arguments(thread_index, 4, &count, initialize);

	if (!a)
		return;
	hs_return(thread_index, 0);
	(void)function_index;
}

/* (cs_deploy_turret <point>): Halo 2's behaviour 0x82 (halo2-upstream-work
unknown_1c10a0.cpp): a unit carrying a deployable weapon - its tag's "deployed
vehicle": the heavy Grunts' c_turret_mp_item, the plasma cannon - turns to the
point, unpacks it there and takes its gunner's seat (function_1c1160, the
vehicle seat flagged bit 3). The scripts walk it to a go_p half a world unit
behind each point first; the H2EK's tag build, 05a, w_path_turret_0: seated.
Waits for all of that. The deploy animation (unit request 0x35) is not played */
H2_ALIGN static short cs_deploy_turret_step(struct rt_command_script *c, struct rt_cs_action *action)
{
	real_point3d origin, point;

	if (!c || action->count < 1 || !point_get(action->arguments[0], &point) || !command_script_unit_origin(c, &origin))
		return CS_STEP_DONE;
	if (action->started == NONE)
	{
		short cluster;

		action->started = game_time_get();
		c->flags |= CS_MOVE;
		c->flags &= ~CS_MOVE_BY;
		c->move_point = point;
		point_resolve(&c->move_point, &cluster, &c->move_surface);
	}
	if ((origin.x - point.x) * (origin.x - point.x) + (origin.y - point.y) * (origin.y - point.y) < 1.0f ||
		game_time_get() - action->started > 20 * 30)
	{
		long unit = actor_get(c->actor_index)->meta.unit_index;
		long definition = tag_loaded('vehi', "h2\\vehicles\\c_turret_mp");

		c->flags &= ~CS_MOVE;
		if (definition != NONE && object_get(unit)->object.parent_object_index == NONE)
		{
			struct object_placement_data placement;
			static char const *const any[] = { "", NULL };
			long vehicle;

			object_placement_data_new(&placement, definition, NONE);
			placement.position = point;
			placement.position.z += 0.1f;
			placement.forward.i = point.x - origin.x;
			placement.forward.j = point.y - origin.y;
			placement.forward.k = 0.0f;
			{
				real length = (real)sqrt(placement.forward.i * placement.forward.i + placement.forward.j * placement.forward.j);

				if (length > 0.01f)
					placement.forward.i /= length, placement.forward.j /= length;
				else
					placement.forward = object_get(unit)->object.forward;
			}
			placement.up.i = placement.up.j = 0.0f;
			placement.up.k = 1.0f;
			vehicle = object_new(&placement);
			if (vehicle != NONE && seat_take(unit, vehicle, any))
				rt.turrets_deployed++;
		}
		return CS_STEP_DONE;
	}
	return CS_STEP_CONTINUE;
}

H2_ALIGN static void cs_deploy_turret_evaluate(short function_index, long thread_index, boolean initialize)
{
	command_script_action_evaluate(function_index, thread_index, initialize, 1, cs_deploy_turret_step);
}

/* (cs_grenade <point> <short>): function_258cf0's and function_259430's case 5:
the actor is given a grenade of its character's type, turns and throws it at
the point (0 toss, 1 lob, 2 bounce: this engine's throw is the character's
own), waits for it; 2 seconds at most */
H2_ALIGN static short cs_grenade_step(struct rt_command_script *c, struct rt_cs_action *action)
{
	if (!c || action->count < 1 || !point_get(action->arguments[0], &c->look_point))
		return CS_STEP_DONE;
	if (action->started == NONE)
	{
		action->started = game_time_get();
		c->look_object = NONE;
		c->flags |= CS_AIM;
		c->grenade_due = TRUE;
	}
	if (game_time_get() - action->started >= 2 * 30)
	{
		c->flags &= ~CS_AIM;
		c->grenade_due = FALSE;
		return CS_STEP_DONE;
	}
	/* (a third of a second to turn to it, then the throw) */
	if (game_time_get() - action->started == 10 && c->grenade_due)
	{
		c->grenade_due = FALSE;
		c->grenade_now = TRUE;
		rt.script_grenades++;
	}
	return CS_STEP_CONTINUE;
}

H2_ALIGN static void cs_grenade_evaluate(short function_index, long thread_index, boolean initialize)
{
	command_script_action_evaluate(function_index, thread_index, initialize, 2, cs_grenade_step);
}

/* (cs_set_behavior <ai_behavior>): function_259430's case 14 starts the
behaviour under the script's and waits for it to end. The scripts' only one
is guard (28 calls, each followed by sleep_forever): the H2EK's tag build, 05a,
stay_shooting: Elites walk to their post and stop, the LZ's turret gunners
stay and fire, as cs_shoot true tells them */
H2_ALIGN static short cs_set_behavior_step(struct rt_command_script *c, struct rt_cs_action *action)
{
	if (!c)
		return CS_STEP_DONE;
	c->flags |= CS_BEHAVIOR;
	return CS_STEP_CONTINUE;
}

H2_ALIGN static void cs_set_behavior_evaluate(short function_index, long thread_index, boolean initialize)
{
	command_script_action_evaluate(function_index, thread_index, initialize, 1, cs_set_behavior_step);
}

/* (ai_attach_units <object_list> <ai>): the units go to the squad; a unit
with no actor is given one (the two calls move a Marine to the squad of the
vehicle he joins) */
H2_ALIGN static void ai_attach_units_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count, targets[1];
	long *a = arguments(thread_index, 2, &count, initialize);

	if (!a)
		return;
	if (count > 1 && squads_of(a[1], targets, 1) > 0 && squad_reference(targets[0]) != NONE)
	{
		long reference, object, target = squad_reference(targets[0]);

		if (object_try_and_get_and_verify_type(a[0], _object_mask_unit))
			object = a[0], reference = NONE;
		else
			object = object_list_get_first(a[0], &reference);
		for (; object != NONE; object = reference != NONE ? object_list_get_next(a[0], &reference) : NONE)
		{
			long actor = object_try_and_get_and_verify_type(object, _object_mask_unit) ? unit_get(object)->unit.actor_index : NONE;

			if (actor != NONE)
				ai_scripting_migrate_by_unit(object, target);
			else if (object_try_and_get_and_verify_type(object, _object_mask_unit))
				ai_scripting_attach_unit(object, target);
		}
		rt.units_attached++;
	}
	hs_return(thread_index, 0);
	(void)function_index;
}

/* (ai_play_line <ai> <line>), (ai_play_line_at_player <ai> <line>),
(ai_play_line_on_object <object> <line>), (cs_play_line <line>): the
mission dialogue's line spoken by the first living actor's unit (the
object; the command script's actor); its length in ticks */
/* (cs_play_line <line>): the command script's actor says the line and the
script waits until it ends - Halo 2's evaluator (unknown_29f5b0.cpp 0x2a7cf0)
plays it and puts the thread to its command-script sleep, which the actor's
update ends with the line; here a step that is done when the line's ticks
have passed (the line's length from dialogue_play; a line not heard: its
stand-in length) */
H2_ALIGN static short cs_play_line_step(struct rt_command_script *c, struct rt_cs_action *action)
{
	if (action->started == NONE)
	{
		long speaker = c && actor_alive(c->actor_index) ? actor_get(c->actor_index)->meta.unit_index : NONE;
		short ticks = dialogue_play(action->count > 0 ? (char const *)action->arguments[0] : NULL, speaker);

		action->started = game_time_get();
		action->arguments[1] = ticks;
	}
	if (game_time_get() >= action->started + action->arguments[1])
		return CS_STEP_DONE;
	return CS_STEP_CONTINUE;
}

H2_ALIGN static void play_line_evaluate(short function_index, long thread_index, boolean initialize)
{
	struct hs_function_definition const *function = h2_scripts_function_get(function_index);
	short count, ticks;
	long *a;
	long speaker = NONE, actors[RT_MAXIMUM_ACTORS];
	char const *line = NULL;

	if (function && !strcmp(function->name, "cs_play_line"))
	{
		command_script_action_evaluate(function_index, thread_index, initialize, 1, cs_play_line_step);
		return;
	}
	a = arguments(thread_index, 2, &count, initialize);
	if (!a)
		return;
	if (function && !strcmp(function->name, "ai_play_line_on_object"))
	{
		line = count > 1 ? (char const *)a[1] : NULL;
		speaker = a[0];
	}
	else if (count > 1)
	{
		short actor_count = actors_of(a[0], actors, RT_MAXIMUM_ACTORS), k;

		line = (char const *)a[1];
		for (k = 0; k < actor_count && speaker == NONE; k++)
		{
			if (actor_alive(actors[k]))
				speaker = actor_get(actors[k])->meta.unit_index;
		}
	}
	ticks = dialogue_play(line, speaker);
	hs_return(thread_index, value_short(ticks));
}

/* (cs_teleport <point> <point>) */
H2_ALIGN static void cs_teleport_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count;
	long *a = arguments(thread_index, 2, &count, initialize);
	struct rt_command_script *c;
	real_point3d point;

	if (!a)
		return;
	c = command_script_of_thread(thread_index);
	if (c && count > 0 && point_get(a[0], &point) && actor_alive(c->actor_index))
		object_set_position(actor_get(c->actor_index)->meta.unit_index, &point, NULL, NULL);
	hs_return(thread_index, 0);
	(void)function_index;
}

/* (cs_pause <real>): the command script waits that many seconds */
H2_ALIGN static short cs_pause_step(struct rt_command_script *c, struct rt_cs_action *action)
{
	real seconds = action->count > 0 ? argument_real(action->arguments[0]) : 0.0f;

	if (action->started == NONE)
		action->started = game_time_get();
	/* (a negative pause never ends: function_259430 counts its ticks down only
	while they are above 0 - the maps' forever_pause, (cs_pause -1)) */
	if (game_time_get() >= action->started + (long)(30.0f * seconds) + 1 && !(seconds < 0.0f))
		return CS_STEP_DONE;
	return CS_STEP_CONTINUE;
	(void)c;
}

H2_ALIGN static void cs_pause_evaluate(short function_index, long thread_index, boolean initialize)
{
	command_script_action_evaluate(function_index, thread_index, initialize, 1, cs_pause_step);
}

/* (actors.c) a command script's overrides on its actor's orders, after its
action gave them */
H2_ALIGN void h2_ai_actor_orders(
	long actor_index)
{
	struct rt_command_script *c;
	struct actor_datum *actor;
	boolean atom;

	if (!rt.loaded)
		return;
	actor = actor_get(actor_index);
	/* (its order's Force combat status: the least it may be, function_2662f0:
	in that stance whether or not it has seen anyone. Not a fleeing actor's:
	the status is a floor, not a stance over a panic - a Grunt that broke
	under such an order crept away in its crouch, 0.26 wu/s, instead of its
	flee run) */
	{
		short s = actor_h2_squad(actor_index);

		if (s != NONE && rt.squads[s].order >= 0 && rt.squads[s].order < rt.order_count &&
			rt.orders[rt.squads[s].order].force_status >= 3 && actor->orders.move.override_movement_type == NONE &&
			!actor->orders.move.panicked && !actor->orders.move.flaming)
		{
			actor->orders.move.override_movement_type = rt.orders[rt.squads[s].order].force_status >= 4 ?
				_actor_movement_type_combat : _actor_movement_type_alert;
		}
		/* (its order's flags, by their tag names: "always active" - never dormant,
		"active camo" - cloaked while the order holds, 03a's e4_cov_inf2_init, 07a's
		stealth Elites -, "suppress combat until engaged" - holding fire until its
		squad is shot at or hit, a Marine order's) */
		if (s != NONE && rt.squads[s].order >= 0 && rt.squads[s].order < rt.order_count)
		{
			unsigned long flags = rt.orders[rt.squads[s].order].flags;
			long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index);

			if ((flags & H2_ORDER_ALWAYS_ACTIVE) && actor->meta.dormant)
				actor_set_dormant(actor_index, FALSE);
			if (actor->meta.unit_index != NONE && object_try_and_get(actor->meta.unit_index) &&
				object_get(actor->meta.unit_index)->object.type == _object_type_biped && absolute < RT_MAXIMUM_ACTORS)
			{
				boolean camo = (flags & H2_ORDER_ACTIVE_CAMO) != 0;

				if (camo != rt.order_camo[absolute])
				{
					SET_FLAG(unit_get(actor->meta.unit_index)->unit.flags, _unit_active_camouflaged_bit, camo);
					rt.order_camo[absolute] = camo;
				}
			}
			if (flags & H2_ORDER_SUPPRESS_COMBAT)
			{
				struct prop_datum *prop = actor->target.target_prop_index != NONE ? prop_get(actor->target.target_prop_index) : NULL;

				if (rt.squads[s].engaged_order != rt.squads[s].order)
				{
					rt.squads[s].engaged_order = rt.squads[s].order;
					rt.squads[s].engaged = FALSE;
				}
				if (prop && (prop->currently_damaging_me || prop->shooting))
					rt.squads[s].engaged = TRUE;
				if (!rt.squads[s].engaged)
				{
					actor->orders.combat.shoot_at_target = FALSE;
					actor->orders.combat.throw_grenade = FALSE;
				}
			}
		}
	}
	/* Halo 2's "surprise" behaviour, as the real engine shows it (no code of it
	read): 03a, live mission and trials, a Jackal pair and a Hunter, first
	in contact with a still player at 8 wu or under, status 8, walk to it at
	patrol pace (0.43, 0.6 wu/s) and hold their fire, for some 20 seconds, then
	fight at about 1 wu. Marked mine, not seen: only against a player, only
	within 10 wu, and over once the target shoots or hurts them. Grunts: first
	hit 25.6 s, closing to 0.7 wu, its stack not seen.
	OFF since Nov 19 2026 (HALO_H2_AI_SURPRISE_HOLD=1 turns it back on): the tag
	test's range (tools/h2range layout ai_accuracy: a still player 5 to 10 wu in
	front of a lone plasma pistol or needler Grunt or a Jackal that first sees it,
	no magic sight) has them stand where they are - their distance to the player
	does not change in 40 s - and open fire 2.0 to 4.3 s after they are placed.
	The trials above ran with cheat_deathless_player, which in the tag test makes
	the player take no hit at all (an Elite "bursting" at it for 24 s: shield
	1.0), so a first hit could not be timed there; the walking was those level
	squads' own orders */
	if (HALO_ENV_ONCE("HALO_H2_AI_SURPRISE_HOLD") && actor->target.target_prop_index != NONE && actor->meta.unit_index != NONE && !command_script_of_actor(actor_index) &&
		DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index) < RT_MAXIMUM_ACTORS && !HALO_ENV_ONCE("HALO_H2_AI_NO_SURPRISE"))
	{
		char const *name = actor->meta.definition_index != NONE ? tag_get_name(actor->meta.definition_index) : "";
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index);
		struct prop_datum *prop = prop_get(actor->target.target_prop_index);

		if (rt.surprise_actor[absolute] != actor_index)
		{
			rt.surprise_actor[absolute] = actor_index;
			rt.surprise_state[absolute] = 0;
		}
		rt.surprise_walking[absolute] = FALSE;
		if ((strstr(name, "\\jackal") || strstr(name, "\\hunter") || strstr(name, "\\grunt")) && !strstr(name, "sniper") && prop->player && actor->state.combat_status >= 4)
		{
			real distance = (real)sqrt(distance_squared(&object_get(actor->meta.unit_index)->object.position, &prop->body_position));

			if (rt.surprise_state[absolute] == 0 && distance <= 10.0f)
			{
				rt.surprise_state[absolute] = 1;
				rt.surprise_start[absolute] = game_time_get();
				rt.surprises++;
			}
			if (rt.surprise_state[absolute] == 1)
			{
				if (distance < 1.8f || game_time_get() - rt.surprise_start[absolute] > 600 || prop->shooting || prop->currently_damaging_me)
					rt.surprise_state[absolute] = 2;
				else
				{
					actor->orders.combat.shoot_at_target = FALSE;
					actor->orders.combat.throw_grenade = FALSE;
					actor->orders.move.override_movement_type = _actor_movement_type_alert;
					rt.surprise_walking[absolute] = TRUE;
				}
			}
		}
	}
	/* (a sniper crouches where it stands: the real engine's, 03a, firing from a knee) */
	if (actor->meta.definition_index != NONE && strstr(tag_get_name(actor->meta.definition_index), "jackal_sniper"))
		actor->orders.move.stationary_crouch = TRUE;
	/* (below active a Halo 2 actor is in its patrol mode (mode 1 here: asleep, kept), its relaxed stance and
	walk - the H2EK's tag build: "patrol", combat status 1, a Jackal at 0.4 wu/s
	against combat's 2; this engine's alert seat, h2_animation.c) */
	if (actor->orders.move.override_movement_type == NONE && actor->state.mode != 1 &&
		actor_combat_status(actor_index) < 3)
	{
		actor->orders.move.override_movement_type = _actor_movement_type_alert;
	}
	/* (a flier with a target and nowhere to go - a Drone: Halo 2 flocks them round
	it, Sentinels hold their 3D areas; this engine's 3D positions do not serve
	them -: a point near the target, its weapon's combat range out, by the
	character's own range, a new one every 2 seconds. NOT Halo 2's flocking) */
	if (actor->state.flying && actor->target.target_prop_index != NONE && !command_script_of_actor(actor_index) &&
		game_time_get() % 60 == (DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index) & 31) * 2)
	{
		struct prop_datum *prop = (struct prop_datum *)datum_try_and_get(prop_data, actor->target.target_prop_index);

		if (prop && prop->unit_index != NONE && object_try_and_get(prop->unit_index))
		{
			struct actor_variant_definition const *variant = actor_variant_definition_get(actor->meta.variant_definition_index);
			real_point3d at, from;
			real_vector3d to;
			real range = 0.5f * (variant->ranged_combat.combat_range_lower_bound + variant->ranged_combat.combat_range_upper_bound);
			real length, angle = 0.8f * (real_seed_random(get_global_random_seed_address()) - 0.5f);

			object_get_origin(prop->unit_index, &at);
			object_get_origin(actor->meta.unit_index, &from);
			to.i = from.x - at.x, to.j = from.y - at.y, to.k = 0.0f;
			length = (real)sqrt(to.i * to.i + to.j * to.j);
			if (length > 0.01f)
			{
				real x = to.i / length, y = to.j / length;
				real_point3d point, sees = at;
				real best = 1e30f;
				short k;
				boolean found = FALSE;

				/* (of eight points round the target - the nearest it has in its own
				direction first - the nearest to it that sees the target: a Sentinel
				pressed against the pipes of 04b's silo for good, and real ones
				move until they see) */
				sees.z += 0.5f;
				for (k = 0; k < 8; k++)
				{
					real turn = angle + (real)k * 0.7853982f, ring = range * (k & 1 ? 0.7f : 1.0f);
					real c = (real)cos(turn), sn = (real)sin(turn);
					real_point3d test;
					real_vector3d line;
					real d;

					test.x = at.x + ring * (c * x - sn * y);
					test.y = at.y + ring * (sn * x + c * y);
					test.z = at.z + 1.0f + 0.2f * (real)k;
					d = distance_squared(&test, &from);
					if (d >= best)
						continue;
					vector_from_points3d(&test, &sees, &line);
					if (!flier_line_blocked(&test, &line, prop->unit_index))
					{
						best = d, point = test, found = TRUE;
					}
				}
				if (!found)
				{
					point.x = at.x + range * x;
					point.y = at.y + range * y;
					point.z = at.z + 1.5f;
				}
				rt.flier_goals[DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index) % RT_MAXIMUM_ACTORS] = point;
				rt.flier_goal_times[DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index) % RT_MAXIMUM_ACTORS] = game_time_get();
				rt.fliers_moved++;
			}
		}
	}
	/* (ai_suppress_combat) */
	if (DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index) < RT_MAXIMUM_ACTORS &&
		(rt.combat_suppressed[DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index) / 32] >> (DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index) % 32)) & 1)
	{
		actor->orders.combat.shoot_at_target = FALSE;
	}
	c = command_script_of_actor(actor_index);
	if (!c || c->thread_index == NONE)
		return;
	atom = command_script_in_atom(c);
	/* (a script's stance over its order's) */
	/* (through the engine's own moves: they also set the path's
	destination and look for a path, which writing the orders alone, after
	the action's moves this tick, did not - the actor stood still) */
	if (c->flags & CS_MOVE)
		actor_move_to_point(actor_index, &c->move_point, c->move_surface, NONE);
	else if ((atom || !(c->flags & CS_MOVING)) && !(c->flags & CS_BEHAVIOR) && actor->state.action != _actor_action_vehicle &&
		actor->control.path.destination_orders.destination_type != _destination_halt)
	{
		/* (function_25a380: no moving of its own unless enabled, and none at
		all in a blocking function, whose behaviour replaces the actor's. A
		walk to a vehicle is left alone: cs_go_to_vehicle's) */
		actor_move_halt(actor_index);
	}
	/* (cs_movement_mode, else cs_force_combat_status: how it carries itself) */
	if (c->movement_mode != NONE)
	{
		static short const modes[] =
		{
			_actor_movement_type_alert, _actor_movement_type_asleep, _actor_movement_type_combat, _actor_movement_type_panic,
			_actor_movement_type_flaming, _actor_movement_type_panic, _actor_movement_type_combat,
		};

		actor->orders.move.override_movement_type = modes[c->movement_mode];
	}
	else if (c->force_status > 0)
	{
		/* (asleep and idle: this engine's asleep stance stands still -
		actor_move_update stops any move in it -, while Halo 2's status does
		not stop a script's own move: 01a's Guns, forced idle, walks to the
		zapper with cs_go_to, and the training waits on it. Moving, the
		relaxed walk) */
		actor->orders.move.override_movement_type = c->force_status >= 4 ? _actor_movement_type_combat :
			c->force_status == 3 || (c->flags & CS_MOVE) ? _actor_movement_type_alert : _actor_movement_type_asleep;
	}
	/* (looking at its head while it faces or looks at it and does not
	shoot at it: cs_object_point) */
	if (c->look_object != NONE && object_try_and_get(c->look_object))
		cs_object_point(c->look_object, &c->look_point, (c->flags & (CS_FACE | CS_LOOK)) && !((c->flags & CS_SHOOT) && (c->flags & CS_SHOOT_POINT)));
	/* (Halo 2 keeps a command script's facing and its aiming apart
	(function_25a380: the facing direction at actor +0x430, the aiming at
	+0x41c, each its own object or point): an actor told both to face and to
	aim aims, its look held on the point (this engine's locked aiming, which
	faces there too). Before, the facing won and the look stayed free: 08b's
	Johnson, cs_face_object and cs_aim_object at Tartarus, looked off at the
	player as often as at him, and the fight's FIRE (objects_can_see_object
	along his look) waited for chance. HALO_H2_CS_FACE_OVER_AIM=1: before) */
	if (c->flags & (CS_FACE | CS_AIM | CS_LOOK))
	{
		if ((c->flags & CS_AIM) && !HALO_ENV_ONCE("HALO_H2_CS_FACE_OVER_AIM"))
			actor->orders.look.primary_priority = _primary_priority_locked_aiming;
		else
			actor->orders.look.primary_priority = (c->flags & CS_FACE) ? _primary_priority_locked_facing :
				(c->flags & CS_AIM) ? _primary_priority_locked_aiming : _primary_priority_aiming;
		actor->orders.look.primary_direction.type = _direction_specification_point;
		actor->orders.look.primary_direction.point = c->look_point;
	}
	else if (!atom && !(c->flags & CS_LOOKING))
	{
		/* (function_25a380: with the script between functions and looking
		off, along the way it faces) */
		actor->orders.look.primary_priority = _primary_priority_aiming;
		actor->orders.look.primary_direction.type = _direction_specification_vector;
		actor->orders.look.primary_direction.vector = object_get(actor->meta.unit_index)->object.forward;
	}
	else if (atom && !(c->flags & CS_TARGETING))
	{
		/* (no target to look at, the behaviour that turned it to one replaced) */
		actor->orders.look.primary_priority = _primary_priority_none;
	}
	/* firing (function_25a380, function_258660): cs_shoot true fires, at what
	it names or at its target; without it an actor needs its own behaviour
	(the script between functions) and its targeting on, which is what keeps
	its target. A forced facing holds fire unless it is told to shoot */
	if ((c->flags & CS_SHOOT) && (c->flags & CS_SHOOT_POINT))
	{
		actor->orders.combat.shoot_at_target = TRUE;
		actor->orders.combat.use_manual_target_point = TRUE;
		actor->orders.combat.target_point = c->look_point;
		actor->orders.combat.throw_grenade = FALSE;
		/* Halo 2's command script apply (function_25a380, retail 0x25a380): shooting at a valid aim target sets the
		actor's point target and its "engaged" byte (+0x48b), which the combat update reads to start no first-burst
		delay (function_1fee20 0x1fee20: the delay is 0 once engaged) and to skip the burst-disable timer, the
		locked-direction gate and both range gates (function_1fc7e0, 0x1fd1c0, 0x1fd1e7, 0x1fd2be, 0x1fd2dc). This
		engine's own word for it is the command lists' shoot-at order's (action_obey.c). Without it a scripted
		shot waited out the weapon's first burst delay (0.25 to 1.25 s): 08b's Johnson, told to shoot Tartarus
		three times for 5 ticks each, fired 19 beams in 49 tries, so the shield that the player's part of the
		fight waits for almost never dropped. HALO_H2_CS_SHOOT_DELAYED=1: the old wait */
		if (!HALO_ENV_ONCE("HALO_H2_CS_SHOOT_DELAYED"))
			actor->orders.combat.override_firing_restrictions = TRUE;
		/* (aimed at the fire target, as this engine's command lists' shoot-at
		(action_obey.c): actor_combat.c fires only while the look is on the
		target - looking at the point it held fire as "wrong target", and
		08b's Johnson never fired the shots that take Tartarus's shield
		down: the last fight could not be won) */
		actor->orders.look.primary_priority = _primary_priority_locked_aiming;
		actor->orders.look.primary_direction.type = _direction_specification_target;
	}
	else if ((c->flags & CS_SHOOT) && (c->flags & CS_TARGETING))
		actor->orders.combat.shoot_at_target = TRUE;
	else if (atom || !(c->flags & CS_TARGETING) || (c->flags & CS_FACE))
	{
		actor->orders.combat.shoot_at_target = FALSE;
		actor->orders.combat.throw_grenade = FALSE;
		actor->orders.combat.bombard_target = FALSE;
	}
	if (c->grenade_now)
	{
		actor->orders.combat.throw_grenade = TRUE;
		c->grenade_now = FALSE;
	}
	if (c->flags & CS_CROUCH)
		actor->orders.move.stationary_crouch = TRUE;
	if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG") && game_time_get() % 150 == 0)
		platform_log("h2 ai: orders of actor %lx: command script %d flags %x, destination %d, path surface %ld, action %d", actor_index,
			c->script, c->flags, actor->orders.move.destination.destination_type, actor->input.pathfinding_surface_index,
			actor->state.action);
}

/* ---------- Halo 2's firing patterns at run time

A character's weapon has two firing patterns ("inaccurate" and "really
accurate", the character's firing pattern properties for that weapon,
0x40 each) and its weapons properties' accuracy ramp per difficulty (normal
0x98, heroic 0xA4, legendary 0xB0: start, end, seconds). Each tick of an
actor's combat (halo2-upstream-work unknown_1fc7e0.cpp, function_1fc7e0
0x1fc7e0) its blend between the two goes back to the ramp's start when its
fire target changes or it is not firing (no fire, the first burst's delay,
wild fire), else climbs toward the end over the ramp's seconds; the pattern
used is the two blended (function_1fe1e0 0x1fe1e0): the rate of fire, the
target tracking and leading of the burst, the burst's duration, geometry
(origin radius and angle, return length and angle, capped by the maximum
error angle: function_1fe620 0x1fe620), its projectile error and damage
modifier, and the separation after it (function_1fed70 0x1fed70).
The builder's actor variants carry their Halo 2 character and weapon
(variant_write: 'h2fp' in unused[0]); with none, this engine's own pattern. */

enum
{
	H2F_FIELDS = 16,
	H2F_MAXIMUM_SOURCES = 128,
	H2F_MAXIMUM_WEAPONS = 64,
	H2F_MARK = 0x68326670,  /* 'h2fp' */
};

struct h2f_source
{
	long character;
	long weapon;
	boolean valid;
	short pattern_count;
	float patterns[2][H2F_FIELDS];
	float ramps[3][3];
};

static struct
{
	long scenario;
	unsigned long generation;
	struct h2f_source sources[H2F_MAXIMUM_SOURCES];
	short source_count;
	/* (this engine's weapon tag of a Halo 2 weapon, as the variants have them) */
	long ce_weapons[H2F_MAXIMUM_WEAPONS], h2_weapons[H2F_MAXIMUM_WEAPONS];
	short weapon_count;
	struct
	{
		long actor_index;
		short source;
		boolean valid;
		real blend;
		float pattern[H2F_FIELDS];
		long shots, bursts;
	} actors[MAXIMUM_ACTORS];
	short log;
} h2f = { NONE };

extern unsigned long halo_map_generation;

H2_ALIGN static void h2f_reset(void)
{
	long i;

	if (h2f.scenario == global_scenario_index && h2f.generation == halo_map_generation)
		return;
	h2f.scenario = global_scenario_index;
	h2f.generation = halo_map_generation;
	h2f.source_count = 0;
	h2f.weapon_count = 0;
	for (i = 0; i < MAXIMUM_ACTORS; i++)
		h2f.actors[i].actor_index = NONE;
	h2f.log = HALO_ENV_ONCE("HALO_H2_FIRE_LOG") ? 1 : 0;
}

/* a character's patterns and ramps for a weapon (function_1e5280's and
actor_get_character_weapon_extra's walks up the parents), its index (NONE) */
H2_ALIGN static short h2f_source_get(long character, long weapon)
{
	struct h2_cache *cache = h2_scripts_cache();
	struct h2f_source *s;
	unsigned long entry = 0, patterns = 0;
	long pattern_count = 0, at, depth;
	short i, k;

	for (i = 0; i < h2f.source_count; i++)
	{
		if (h2f.sources[i].character == character && h2f.sources[i].weapon == weapon)
			return i;
	}
	if (!cache || h2f.source_count >= H2F_MAXIMUM_SOURCES)
		return NONE;
	s = &h2f.sources[h2f.source_count];
	memset(s, 0, sizeof(*s));
	s->character = character;
	s->weapon = weapon;
	for (at = character, depth = 0; at != NONE && depth < MAXIMUM_INHERITANCE && (!entry || !patterns); depth++)
	{
		struct h2_cache_tag t;
		long count, n;
		unsigned long elements;

		if (!h2_cache_tag_get(cache, at, &t) || t.group_tag != 'char')
			break;
		if (!entry && h2_cache_block(cache, t.address + H2C_WEAPONS, H2C_WEAPON_SIZE, &count, &elements))
		{
			for (n = 0; n < count && !entry; n++)
			{
				if (h2_cache_reference(cache, elements + H2C_WEAPON_SIZE * n + 4) == weapon)
					entry = elements + H2C_WEAPON_SIZE * n;
			}
		}
		if (!patterns && h2_cache_block(cache, t.address + H2C_FIRING_PATTERNS, H2C_FIRING_PATTERN_PROPERTIES_SIZE, &count, &elements))
		{
			for (n = 0; n < count && !patterns; n++)
			{
				unsigned long properties = elements + H2C_FIRING_PATTERN_PROPERTIES_SIZE * n;
				long c;
				unsigned long p;

				if (h2_cache_reference(cache, properties) == weapon &&
					h2_cache_block(cache, properties + 8, H2C_FIRING_PATTERN_SIZE, &c, &p) && c > 0)
				{
					patterns = p;
					pattern_count = c;
				}
			}
		}
		at = h2_cache_reference(cache, t.address + H2C_PARENT);
	}
	if (entry && patterns)
	{
		s->valid = TRUE;
		s->pattern_count = (short)MIN(pattern_count, 2);
		for (i = 0; i < s->pattern_count; i++)
		{
			for (k = 0; k < H2F_FIELDS; k++)
				s->patterns[i][k] = readf(cache, patterns + H2C_FIRING_PATTERN_SIZE * i + 4 * k);
		}
		for (i = 0; i < 3; i++)
		{
			for (k = 0; k < 3; k++)
				s->ramps[i][k] = readf(cache, entry + 0x98 + 12 * i + 4 * k);
		}
	}
	if (h2f.log)
		platform_log("h2 fire: source %d: %s with %s: %s, %d patterns, ramps normal %.2f..%.2f in %.1f s, heroic %.2f..%.2f in %.1f s, "
			"legendary %.2f..%.2f in %.1f s", (int)h2f.source_count, h2_cache_tag_name(cache, character),
			weapon != NONE ? h2_cache_tag_name(cache, weapon) : "-", s->valid ? "found" : "none (this engine's pattern)",
			(int)s->pattern_count, s->ramps[0][0], s->ramps[0][1], s->ramps[0][2], s->ramps[1][0], s->ramps[1][1], s->ramps[1][2],
			s->ramps[2][0], s->ramps[2][1], s->ramps[2][2]);
	return h2f.source_count++;
}

/* (actor_combat.c) each tick of an actor's combat, before its fire state
changes: its blend and its blended pattern (state: its fire state;
changed: its fire target changed this tick) */
H2_ALIGN void h2_ai_fire_update(
	long actor_index,
	short state,
	boolean changed)
{
	struct actor_datum *actor;
	struct actor_variant_definition const *variant;
	long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index), character, weapon, held, i;
	struct h2f_source const *s;
	short source = NONE, difficulty;
	real start, end, seconds, blend;

	if (!rt.loaded || absolute < 0 || absolute >= MAXIMUM_ACTORS)
		return;
	h2f_reset();
	h2f.actors[absolute].valid = FALSE;
	if (HALO_ENV_ONCE("HALO_H2_FIRE_OLD"))
		return;
	actor = actor_get(actor_index);
	if (actor->meta.variant_definition_index == NONE)
		return;
	variant = actor_variant_definition_get(actor->meta.variant_definition_index);
	if (variant->unused[0] != H2F_MARK)
		return;
	character = (long)variant->unused[1];
	weapon = (long)variant->unused[2];
	/* (the weapon it holds: Halo 2 looks its properties up by the held
	weapon's tag; one another variant of the level has) */
	for (i = 0; i < h2f.weapon_count && h2f.ce_weapons[i] != variant->ranged_combat.reference.index; i++)
		;
	if (i == h2f.weapon_count && h2f.weapon_count < H2F_MAXIMUM_WEAPONS && variant->ranged_combat.reference.index != NONE)
	{
		h2f.ce_weapons[h2f.weapon_count] = variant->ranged_combat.reference.index;
		h2f.h2_weapons[h2f.weapon_count++] = weapon;
	}
	held = actor_get_weapon(actor_index);
	if (held != NONE)
	{
		long definition = object_get(held)->definition_index;

		if (definition != variant->ranged_combat.reference.index)
		{
			for (i = 0; i < h2f.weapon_count; i++)
			{
				if (h2f.ce_weapons[i] == definition)
				{
					weapon = h2f.h2_weapons[i];
					break;
				}
			}
		}
	}
	source = h2f_source_get(character, weapon);
	if (source == NONE || !(s = &h2f.sources[source])->valid)
		return;
	difficulty = game_difficulty_level_get();
	i = difficulty == _game_difficulty_level_hard ? 1 : difficulty == _game_difficulty_level_impossible ? 2 : 0;
	start = s->ramps[i][0];
	end = s->ramps[i][1];
	seconds = s->ramps[i][2];
	blend = h2f.actors[absolute].blend;
	if (h2f.actors[absolute].actor_index != actor_index || h2f.actors[absolute].source != source || changed ||
		state == _actor_fire_state_none || state == _actor_fire_state_holding || state == _actor_fire_state_wild)
	{
		blend = start;
	}
	else
		blend += seconds > 0.0f ? (end - start) / (TICKS_PER_SECOND * seconds) : end - start;
	if (blend < 0.0f)
		blend = 0.0f;
	else if (blend > end)
		blend = end;
	if (h2f.actors[absolute].actor_index != actor_index)
		h2f.actors[absolute].shots = h2f.actors[absolute].bursts = 0;
	h2f.actors[absolute].actor_index = actor_index;
	h2f.actors[absolute].source = source;
	h2f.actors[absolute].blend = blend;
	h2f.actors[absolute].valid = TRUE;
	for (i = 0; i < H2F_FIELDS; i++)
	{
		h2f.actors[absolute].pattern[i] = s->pattern_count < 2 ? s->patterns[0][i] :
			s->patterns[0][i] * (1.0f - blend) + s->patterns[1][i] * blend;
	}
}

/* (actor_perception.c) the perception entry a Halo 2 actor sees with now
(halo2-upstream-work character_tag_accessors.cpp function_1e51a0 0x1e51a0:
the second when it has a target, the third at combat status 4 or more,
the fourth at 1 or less, else the first; each only if the character has
that many - generic's: 30 wu and no delay alert, 90 wu with a target, 30
wu searching, 20 wu idle); NULL: the definition's own. Fields as
H2_PERCEPTION_FIELDS says */
H2_ALIGN real const *h2_ai_perception(
	long actor_index)
{
	struct actor_datum *actor;
	struct actor_definition const *definition;
	long count;
	short status, entry = 0;

	if (!rt.loaded || HALO_ENV_ONCE("HALO_H2_PERCEPTION_OLD"))
		return NULL;
	actor = actor_get(actor_index);
	if (actor->meta.definition_index == NONE)
		return NULL;
	definition = actor_definition_get(actor->meta.definition_index);
	if (definition->unused_ranged[0] != 0x68327063UL)
		return NULL;
	count = (long)definition->unused_ranged[1];
	status = actor_combat_status(actor_index);
	if (count > 1 && actor->target.target_prop_index != NONE)
		entry = 1;
	else if (count > 2 && status >= 4)
		entry = 2;
	else if (count > 3 && status <= 1)
		entry = 3;
	return (real const *)&definition->unused_ranged[2] + H2_PERCEPTION_FIELDS * entry;
}

/* (actor_combat.c) an actor's blended Halo 2 firing pattern this tick
(0x40 bytes: rate of fire, tracking, leading, origin radius and angle,
return length bounds and angle, duration and separation bounds, damage
modifier, projectile error, angular velocity, maximum error angle - the
tag's order); NULL: this engine's own */
H2_ALIGN real const *h2_ai_fire_pattern(
	long actor_index)
{
	long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index);

	if (!rt.loaded || absolute < 0 || absolute >= MAXIMUM_ACTORS || !h2f.actors[absolute].valid ||
		h2f.actors[absolute].actor_index != actor_index)
	{
		return NULL;
	}
	return h2f.actors[absolute].pattern;
}

/* (actor_combat.c, HALO_H2_FIRE_LOG) a burst started */
H2_ALIGN void h2_ai_fire_log_burst(
	long actor_index,
	real distance,
	real origin_radius,
	real return_radius,
	real error,
	short ticks)
{
	struct actor_datum *actor;
	long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index);
	short squad;

	h2f_reset();
	if (!rt.loaded || !h2f.log || absolute < 0 || absolute >= MAXIMUM_ACTORS)
		return;
	actor = actor_get(actor_index);
	squad = actor_h2_squad(actor_index);
	if (h2f.actors[absolute].actor_index == actor_index)
		h2f.actors[absolute].bursts++;
	platform_log("h2 fire: burst %ld actor %lx unit %lx %s %s distance %.1f blend %.2f%s radius %.2f return %.2f error %.2f deg ticks %d",
		(long)game_time_get(), (unsigned long)actor_index, (unsigned long)actor->meta.unit_index,
		squad != NONE ? rt.squads[squad].name : "-",
		actor->meta.variant_definition_index != NONE ? tag_get_name(actor->meta.variant_definition_index) : "?", distance,
		h2f.actors[absolute].actor_index == actor_index ? h2f.actors[absolute].blend : -1.0f,
		h2_ai_fire_pattern(actor_index) ? "" : " (own)", origin_radius, return_radius, error * 57.29578f, (int)ticks);
}

/* (weapons.c, HALO_H2_FIRE_LOG) a projectile an actor fired */
H2_ALIGN void h2_ai_fire_log_shot(
	long actor_index)
{
	long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index);

	h2f_reset();
	if (!rt.loaded || !h2f.log || absolute < 0 || absolute >= MAXIMUM_ACTORS)
		return;
	if (h2f.actors[absolute].actor_index == actor_index)
		h2f.actors[absolute].shots++;
	platform_log("h2 fire: shot %ld actor %lx", (long)game_time_get(), (unsigned long)actor_index);
}

/* (damage.c, HALO_H2_FIRE_LOG) a unit damaged: by whom, how much */
H2_ALIGN void h2_ai_fire_log_hit(
	long object_index,
	long owner_object_index,
	long definition_index,
	real shield_before,
	real body_before)
{
	struct object_datum *object, *owner;
	real_point3d a, b;

	h2f_reset();
	if (!rt.loaded || !h2f.log || !(object = object_try_and_get(object_index)) || object->object.type != _object_type_biped)
		return;
	owner = owner_object_index != NONE ? object_try_and_get(owner_object_index) : NULL;
	object_get_origin(object_index, &a);
	b = a;
	if (owner)
		object_get_origin(owner_object_index, &b);
	platform_log("h2 fire: hit %ld victim %lx (actor %lx) %s by %lx (actor %lx) %s with %s distance %.1f shield %.1f -> %.1f body %.1f -> %.1f%s",
		(long)game_time_get(), (unsigned long)object_index,
		(unsigned long)(object->object.type == _object_type_biped ? ((struct unit_datum *)object)->unit.actor_index : NONE),
		tag_get_name(object->definition_index), (unsigned long)owner_object_index,
		(unsigned long)(owner && TEST_FLAG(_object_mask_unit, owner->object.type) ? ((struct unit_datum *)owner)->unit.actor_index : NONE),
		owner ? tag_get_name(owner->definition_index) : "-", definition_index != NONE ? tag_get_name(definition_index) : "-",
		(real)sqrt(distance_squared3d(&a, &b)), shield_before * object->object.maximum_shield_vitality,
		object->object.shield_vitality * object->object.maximum_shield_vitality, body_before * object->object.maximum_body_vitality,
		object->object.body_vitality * object->object.maximum_body_vitality,
		TEST_FLAG(object->object.damage_flags, _object_dead_bit) || object->object.body_vitality <= 0.0f ? " (dead)" : "");
	/* (the victim's facing toward its attacker - cos, 1 face on - and its crouch: whether a shield could be between) */
	if (owner)
	{
		real_vector3d to;
		struct unit_datum *u = (struct unit_datum *)object;
		real facing;

		vector_from_points3d(&a, &b, &to);
		to.k = 0.0f;
		facing = normalize3d(&to) > 0.0f ? (u->object.forward.i * to.i + u->object.forward.j * to.j) /
			MAX(0.001f, (real)sqrt(u->object.forward.i * u->object.forward.i + u->object.forward.j * u->object.forward.j)) : 0.0f;
		platform_log("h2 fire: hit %ld facing %.2f aiming %.2f crouch %.2f", (long)game_time_get(), facing,
			(real)((u->unit.aiming_vector.i * to.i + u->unit.aiming_vector.j * to.j) /
			MAX(0.001f, (real)sqrt(u->unit.aiming_vector.i * u->unit.aiming_vector.i + u->unit.aiming_vector.j * u->unit.aiming_vector.j))),
			u->unit.actor_index != NONE && actor_try_and_get(u->unit.actor_index) ? (real)actor_get(u->unit.actor_index)->control.crouching : -1.0f);
	}
}

/* (actors.c) the vitality an actor of a Halo 2 level is placed with, by
difficulty (halo2-upstream-work function_1e04a0): easy and normal the
character's normal values, legendary its legendary ones, heroic half way -
each its normal values if it has no legendary ones */
H2_ALIGN void h2_ai_actor_vitality(
	struct actor_variant_definition const *variant,
	real *body,
	real *shield)
{
	real legendary_body = ((float const *)variant->unused2)[0], legendary_shield = ((float const *)variant->unused2)[1];
	short difficulty = game_difficulty_level_get();

	*body = variant->unit.maximum_body_vitality;
	*shield = variant->unit.maximum_shield_vitality;
	if (!rt.loaded || (legendary_body <= 0.0f && legendary_shield <= 0.0f))
		return;
	if (difficulty == _game_difficulty_level_impossible)
		*body = legendary_body, *shield = legendary_shield;
	else if (difficulty == _game_difficulty_level_hard)
		*body = (*body + legendary_body) * 0.5f, *shield = (*shield + legendary_shield) * 0.5f;
}

/* a Halo 2 vocalization's gesture played by its speaker (the dialogue globals' vocalization "animation impulse":
1 shakefist, 2 cheer, 5 taunt, 7 point, 8 hold, 9 wave, 10 advance, 11 fallback - its line says it and its body shows
it; h2_dialogue.c's h2_dialogue_speech_play calls this as the line starts). The actor's own animation
impulse (it stands, faces where it faces, plays the class's gesture: units.h's _unit_weapon_class_animation_h2_*); a
unit that is busy (another impulse, a throw, a melee, a ping), in a seat, or without the gesture plays none.
TRUE: it plays */
H2_ALIGN boolean h2_ai_vocalization_animation(
	long unit_index,
	short animation_impulse)
{
	static short const gestures[12] = { NONE, 0, 1, NONE, NONE, 2, NONE, 3, 4, 5, 6, 7 };
	struct unit_datum *unit;
	short impulse;
	real_vector2d facing;

	if (!rt.loaded || animation_impulse < 0 || animation_impulse >= (short)NUMBEROF(gestures) || gestures[animation_impulse] == NONE ||
		(unit = (struct unit_datum *)object_try_and_get(unit_index)) == NULL || unit->object.type != _object_type_biped ||
		unit->unit.actor_index == NONE || unit->object.parent_object_index != NONE ||
		TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
	{
		return FALSE;
	}
	impulse = (short)(_unit_animation_impulse_h2_first_gesture + gestures[animation_impulse]);
	if (!unit_test_animation_impulse(unit_index, impulse))
	{
		if (HALO_ENV_ONCE("HALO_H2_GESTURE_LOG"))
		{
			struct unit_definition *d = unit_definition_get(unit->definition_index);
			struct animation_graph *graph = d->object.animation_graph.index != NONE ? animation_graph_definition_get(d->object.animation_graph.index) : NULL;
			struct animation_graph_unit_seat *seat = graph && unit->unit.animation.seat_index >= 0 && unit->unit.animation.seat_index < graph->unit_seats.count ?
				TAG_BLOCK_GET_ELEMENT(&graph->unit_seats, unit->unit.animation.seat_index, struct animation_graph_unit_seat) : NULL;
			struct animation_graph_weapon_class *wc = seat && unit->unit.animation.weapon_index >= 0 && unit->unit.animation.weapon_index < seat->weapon_classes.count ?
				TAG_BLOCK_GET_ELEMENT(&seat->weapon_classes, unit->unit.animation.weapon_index, struct animation_graph_weapon_class) : NULL;

			platform_log("h2 ai: gesture %d not played by %s: seat %s class %s (%ld slots) state %d", (int)animation_impulse,
				tag_get_name(unit->definition_index), seat ? seat->label : "?", wc ? wc->label : "?", wc ? (long)wc->animations.count : -1L,
				(int)unit->unit.animation.state);
		}
		return FALSE;
	}
	facing.i = unit->object.forward.i;
	facing.j = unit->object.forward.j;
	/* (the unit's own impulse, as this engine's communication gestures are
	started, ai_communication.c: an actor's movement order would be cleared
	before its next decision) */
	if (actor_move_animation_busy(unit->unit.actor_index) || !unit_start_animation_impulse(unit_index, impulse, &facing))
		return FALSE;
#if !defined(HALO_RELEASE) && !defined(HALO_VITA)
	{
		void h2_range_stat(long actor_index, char const *key, long value);

		h2_range_stat(unit->unit.actor_index, "gesture", animation_impulse);
	}
#endif
	return TRUE;
}

/* Halo 2's clumps (0x502420, retail 0x268840 each tick): first each clump's centre is its members' centroid
(0x268900); then each actor (0x2689d0) belongs to the first clump of its team (by index) whose centre is within
6 wu of it (0x268bb0 scores a clump 0 beyond 6 wu and 1.0 inside: (short)(members * 10 / distance), at most 1),
else to a new clump at its own position (0x269040). Squads play no part: a squad spread out is several clumps,
two squads standing together one. The leader-dead and peer-dead retreats are the dead one's clump's */
#define H2_CLUMP_RADIUS 6.0f

H2_ALIGN static short clump_of_actor(long actor_index)
{
	long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index);

	return absolute >= 0 && absolute < RT_MAXIMUM_ACTORS ? (short)(rt.clump_of[absolute] - 1) : NONE;
}

/* Halo 2's ai_magically_see / ai_magically_see_object (halo2-upstream-work
unknown_272b70.cpp function_273ac0 / function_273d30; the H2EK's 0xa4f110 ->
0xa71260 alike): no sight. Each of the squad's prop_refs of the object gets
+0x1c = 3 and +0x1e = 5 s of ticks (function_263670 counts it down, then
+0x1c = NONE). While it is 3, function_25cca0 gives the prop_ref priority 3
(function_264330: +0x26 = the larger of what the actor perceives and 3; +0x27,
what it perceives, stays 0), and the perception loop (function_262ba0) then:
- state 0 (not noticed): awareness (+0x10) grows by function_2683f0 at
  priority 3 - g_44ae58[situation][3]: an actor at combat status 3 or more
  (function_263d30's situation 2) at once, else at its perception entry's
  non-combat awareness time - and at 1 notices it (function_25c230) and,
  perceiving nothing (+0x27 < 1), remembers it (function_25c860: state 3);
- state 1-2 (perceived): left to the perception;
- state 3 (remembered): function_264330 brings the remembered place up to the
  object's (function_25ccd0) each tick the mark lasts.
A remembered object is what ai_combat_status calls an uninspected orphan (4:
the H2EK's status names 0xec5bac), and the real engine's 03a e8 Grunts read 4
within 2.6 s of the see, then search/uncover at once (behaviour stack at 3 s)
and see the player - 8 - by 7 s (audit16 perceive_03a_see). Here: the
remembered place is this engine's uninspected orphan of the unit, not
definitely located (definitely_located would be Halo 2's definite orphan, 5),
made when the awareness is full and moved to the unit each tick of the 5 s.
Before (HALO_H2_MAGIC_SIGHT_OLD=1): acknowledged with partial sight for 5 s -
status 6, a fight in place with no line of sight, the target lost after 5 s,
presearch, then uncover: "visible" ~19 s after the see. As Halo 2's, only a
prop the actor already has is marked (ai_script.c: none is made for it) */
#define H2_MAGIC_SIGHT_TICKS 150   /* (ai_seconds_to_ticks_round(5.0f)) */
static struct
{
	long actor_index, unit_index;
	long start, last;
	real awareness;
	boolean unseen;   /* (made by the mark, the unit never in sight since) */
} magic_sight[64];

static int magic_sight_find(long actor_index, long unit_index, boolean make)
{
	int k, oldest = 0;

	for (k = 0; k < (int)NUMBEROF(magic_sight); k++)
	{
		if (magic_sight[k].actor_index == actor_index && magic_sight[k].unit_index == unit_index &&
			actor_index != NONE && magic_sight[k].start)
		{
			return k;
		}
		if (magic_sight[k].start < magic_sight[oldest].start)
			oldest = k;
	}
	if (!make)
		return -1;
	memset(&magic_sight[oldest], 0, sizeof(magic_sight[oldest]));
	magic_sight[oldest].actor_index = actor_index;
	magic_sight[oldest].unit_index = unit_index;
	return oldest;
}

/* (ai_script.c, ai_scripting_magically_see_unit) whether a Halo 2 level's
magic sight is the mark above: only a prop the actor has is marked */
H2_ALIGN boolean h2_ai_magic_sight_on(void)
{
	static int old = -1;

	if (old < 0)
		old = HALO_ENV_ONCE("HALO_H2_MAGIC_SIGHT_OLD") && atoi(HALO_ENV_ONCE("HALO_H2_MAGIC_SIGHT_OLD")) != 0;
	return rt.loaded && !old;
}

/* (actors.c, actor_handle_unit_effect: the magic sight effect) TRUE: a Halo 2
level's, marked here - the prop gets no sight */
H2_ALIGN boolean h2_ai_magic_sight_begin(
	long actor_index,
	long unit_index)
{
	int k;

	if (!h2_ai_magic_sight_on() || unit_index == NONE || !actor_try_and_get(actor_index))
		return FALSE;
	k = magic_sight_find(actor_index, unit_index, TRUE);
	magic_sight[k].start = game_time_get() + 1;   /* (+1: never 0, the free mark) */
	magic_sight[k].last = game_time_get();
	if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
		platform_log("h2 ai: magic sight: actor %lx unit %lx at %ld (awareness %.2f)", (unsigned long)actor_index,
			(unsigned long)unit_index, (long)game_time_get(), (double)magic_sight[k].awareness);
	return TRUE;
}

/* (actors.c, each actor's update after its perception) the marks of an actor */
H2_ALIGN void h2_ai_magic_sight_update(
	long actor_index)
{
	long now = game_time_get(), prop_index;
	struct prop_datum *prop;
	int k;

	if (!rt.loaded)
		return;
	for (k = 0; k < (int)NUMBEROF(magic_sight); k++)
	{
		if (magic_sight[k].actor_index != actor_index || !magic_sight[k].start)
			continue;
		if (!object_try_and_get_and_verify_type(magic_sight[k].unit_index, _object_mask_unit) ||
			(prop_index = prop_get_base_by_unit_index(actor_index, magic_sight[k].unit_index, FALSE, FALSE)) == NONE)
		{
			magic_sight[k].start = 0;
			continue;
		}
		prop = prop_get(prop_index);
		/* (perceived - Halo 2's state 1-2 - or seen since: the perception's) */
		if (prop->last_visible_time != NONE || (prop->state >= _prop_state_becoming_acknowledged && prop->state <= _prop_state_acknowledged))
			magic_sight[k].unseen = FALSE;
		if (now - (magic_sight[k].start - 1) >= H2_MAGIC_SIGHT_TICKS)
			continue;   /* (the mark is over: the remembered place stays where it was) */
		if (prop->state >= _prop_state_becoming_acknowledged && prop->state <= _prop_state_acknowledged)
		{
			magic_sight[k].last = now;
			continue;
		}
		if (prop->orphan_prop_index == NONE)
		{
			/* (not noticed: the awareness at priority 3) */
			real const *entry = h2_ai_perception(actor_index);
			struct actor_definition const *definition = actor_definition_get(actor_get(actor_index)->meta.definition_index);
			real rate = actor_combat_status(actor_index) >= 3 ? 1.0f :
				entry ? entry[10] : definition->perception.runtime_awareness_delta_non_combat;

			magic_sight[k].awareness += rate * (real)MAX(1, now - magic_sight[k].last);
			magic_sight[k].last = now;
			if (magic_sight[k].awareness < 1.0f)
				continue;
			magic_sight[k].awareness = 1.0f;
			if (prop->last_visible_time == NONE)
				magic_sight[k].unseen = TRUE;
		}
		magic_sight[k].last = now;
		/* (remembered: its place brought up to the unit's) */
		if (actor_perception_create_orphan_from_friend(actor_index, magic_sight[k].unit_index, NONE, NONE) &&
			prop->orphan_prop_index != NONE)
		{
			struct prop_datum *orphan = prop_get(prop->orphan_prop_index);

			orphan->definitely_located = FALSE;
			orphan->ticks_since_definitely_located = NONE;
			orphan->definite_knowledge_source_actor = NONE;
		}
	}
}

/* (h2_ai_presearch_hold) whether an actor's orphan of a unit is one a magic
sight made and the unit has not been in its sight since */
static boolean magic_sight_unseen(long actor_index, long unit_index)
{
	int k = magic_sight_find(actor_index, unit_index, FALSE);

	return k >= 0 && magic_sight[k].unseen;
}

/* (actions.c, the lost-target choice before uncover and search) whether an
actor holds where it is in Halo 2's PRESEARCH (the H2EK tag test's
behavior_presearch.inl, 0xa175c0; engage.presearch.guard in the real 05a
frames): when its target is lost - an orphan - it guards for the pre-search
time before it uncovers or searches. A joint behaviour: the clump's members
on the same target share one start time and one min/max (the first starts
it, the rest join - the joint record's +0x80/+0x84/+0x86), each drawn from
the character's min and max presearch time bounds. It ends once the max
time is up, or once the min time is up while the target is in fact farther
than the min certainty radius from where it was last known (the tag's field
notes). Once over, the target is uncovered and searched as before; a target
seen again starts a new one. Not ported: the suppressing fire (min
suppressing time) and the presearch uncover child. HALO_H2_PRESEARCH=0:
none, as before */
static struct
{
	long actor_index;
	long target_unit_index;
	long start;
	short minimum_ticks, maximum_ticks;
	boolean over;
} presearch[RT_MAXIMUM_ACTORS];

H2_ALIGN boolean h2_ai_presearch_hold(
	long actor_index)
{
	long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index), now = game_time_get(), k;
	struct actor_datum *actor;
	struct actor_definition const *definition;
	struct prop_datum *prop;
	real const *bounds;
	real_point3d at;
	short clump;
	static int on = -1;

	if (on < 0)
		on = !(HALO_ENV_ONCE("HALO_H2_PRESEARCH") && atoi(HALO_ENV_ONCE("HALO_H2_PRESEARCH")) == 0);
	if (!on || !rt.loaded || absolute < 0 || absolute >= RT_MAXIMUM_ACTORS)
		return FALSE;
	actor = actor_get(actor_index);
	if (presearch[absolute].actor_index != actor_index)
	{
		presearch[absolute].actor_index = actor_index;
		presearch[absolute].target_unit_index = NONE;
	}
	if (actor->meta.definition_index == NONE || actor->meta.swarm || actor->target.target_prop_index == NONE)
		return FALSE;
	definition = actor_definition_get(actor->meta.definition_index);
	prop = prop_get(actor->target.target_prop_index);
	if (definition->unused[0] != H2_PRESEARCH_MARK || prop->unit_index == NONE ||
		prop->state < _prop_state_uninspected_orphan || prop->state > _prop_state_inspected_orphan)
	{
		if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG") && presearch[absolute].target_unit_index != NONE)
			platform_log("h2 ai: presearch reset: actor %lx at %ld (prop state %d unit %lx)", (unsigned long)actor_index, now,
				(int)prop->state, (unsigned long)prop->unit_index);
		presearch[absolute].target_unit_index = NONE;
		return FALSE;
	}
	/* (a place it only remembers from ai_magically_see, the unit never in its
	sight: the real engine's 03a e8 Grunts search and uncover at once - stack
	engage/search/uncover 1 s after the see -, no presearch. INFERENCE from
	that run and 0x1b4680's H2EK twin 0xa18f80 - presearch only for a
	remembered prop whose view +0x69 is clear -: which flag keeps it out is
	not read) */
	if (magic_sight_unseen(actor_index, prop->unit_index))
	{
		presearch[absolute].target_unit_index = NONE;
		return FALSE;
	}
	bounds = (real const *)&definition->unused[1];
	/* (an enemy it sees now is its target in Halo 2 - the target choice comes
	before the behaviour: no presearch while one is in sight) */
	{
		struct prop_iterator props;
		struct prop_datum *other;

		prop_iterator_new(&props, actor_index);
		while ((other = prop_iterator_next(&props)) != NULL)
		{
			if (other != prop && other->enemy && !other->dead && other->visibility > _actor_perception_none &&
				other->state < _prop_state_uninspected_orphan)
			{
				if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG") && now % 15 == 0)
					platform_log("h2 ai: presearch not: actor %lx at %ld sees another enemy (unit %lx)", (unsigned long)actor_index, now,
						(unsigned long)other->unit_index);
				return FALSE;
			}
		}
	}
	if (presearch[absolute].target_unit_index != prop->unit_index)
	{
		/* (a new loss: join a clump member's on the same target, or start one) */
		presearch[absolute].target_unit_index = prop->unit_index;
		presearch[absolute].over = FALSE;
		clump = clump_of_actor(actor_index);
		for (k = 0; k < RT_MAXIMUM_ACTORS; k++)
		{
			if (k != absolute && clump != NONE && presearch[k].target_unit_index == prop->unit_index && !presearch[k].over &&
				presearch[k].actor_index != NONE && actor_try_and_get(presearch[k].actor_index) &&
				clump_of_actor(presearch[k].actor_index) == clump)
			{
				break;
			}
		}
		if (k < RT_MAXIMUM_ACTORS)
		{
			presearch[absolute].start = presearch[k].start;
			presearch[absolute].minimum_ticks = presearch[k].minimum_ticks;
			presearch[absolute].maximum_ticks = presearch[k].maximum_ticks;
		}
		else
		{
			presearch[absolute].start = now;
			presearch[absolute].minimum_ticks = (short)(real_random_range(bounds[0], MAX(bounds[0], bounds[1])) * 30.0f);
			presearch[absolute].maximum_ticks = (short)(real_random_range(bounds[2], MAX(bounds[2], bounds[3])) * 30.0f);
		}
		if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
			platform_log("h2 ai: presearch %s: actor %lx unit %lx, %d..%d ticks (min %.1f-%.1f s max %.1f-%.1f s radius %.1f)",
				k < RT_MAXIMUM_ACTORS ? "joined" : "started", (unsigned long)actor_index, (unsigned long)prop->unit_index,
				(int)presearch[absolute].minimum_ticks, (int)presearch[absolute].maximum_ticks, bounds[0], bounds[1], bounds[2],
				bounds[3], bounds[4]);
	}
	if (presearch[absolute].over)
		return FALSE;
	if (now - presearch[absolute].start >= presearch[absolute].maximum_ticks)
		presearch[absolute].over = TRUE;
	else if (now - presearch[absolute].start >= presearch[absolute].minimum_ticks && object_try_and_get(prop->unit_index))
	{
		object_get_origin(prop->unit_index, &at);
		if (distance_squared3d(&at, &prop->body_position) > bounds[4] * bounds[4])
			presearch[absolute].over = TRUE;
	}
	if (presearch[absolute].over && HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
		platform_log("h2 ai: presearch over: actor %lx at %ld (%ld ticks after its start)", (unsigned long)actor_index, now,
			now - presearch[absolute].start);
	return !presearch[absolute].over;
}

H2_ALIGN static boolean clump_actor_counts(struct actor_datum const *actor)
{
	struct object_datum *unit;

	return actor->meta.unit_index != NONE && !actor->meta.swarm && (unit = object_try_and_get(actor->meta.unit_index)) != NULL &&
		!TEST_FLAG(unit->object.damage_flags, _object_dead_bit);
}

H2_ALIGN static void clumps_update(void)
{
	struct data_iterator iterator;
	struct actor_datum *actor;
	real_point3d sums[RT_MAXIMUM_ACTORS];
	short counts[RT_MAXIMUM_ACTORS], c;

	/* (the centres: the members' centroid; the members gone - dead, deleted - let go) */
	memset(sums, 0, sizeof(sums));
	memset(counts, 0, sizeof(counts));
	for (c = 0; c < RT_MAXIMUM_ACTORS; c++)
	{
		struct actor_datum *member = rt.clump_of[c] ? (struct actor_datum *)datum_try_and_get(actor_data, rt.clump_actor[c]) : NULL;

		if (rt.clump_of[c] && (!member || !clump_actor_counts(member)))
			rt.clump_of[c] = 0;
	}
	data_iterator_new(&iterator, actor_data);
	while ((actor = (struct actor_datum *)data_iterator_next(&iterator)) != NULL)
	{
		short own = clump_of_actor(iterator.datum_index);

		if (own != NONE && clump_actor_counts(actor))
		{
			sums[own].x += actor->input.position.body_position.x;
			sums[own].y += actor->input.position.body_position.y;
			sums[own].z += actor->input.position.body_position.z;
			counts[own]++;
		}
	}
	for (c = 0; c < RT_MAXIMUM_ACTORS; c++)
	{
		rt.clumps[c].count = counts[c];
		if (!counts[c])
			rt.clumps[c].used = FALSE;
		else
			set_real_point3d(&rt.clumps[c].center, sums[c].x / counts[c], sums[c].y / counts[c], sums[c].z / counts[c]);
	}
	/* (each actor to the first clump of its team within 6 wu, else a new one where it stands) */
	data_iterator_new(&iterator, actor_data);
	while ((actor = (struct actor_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		short own, found = NONE;

		if (absolute < 0 || absolute >= RT_MAXIMUM_ACTORS || !clump_actor_counts(actor))
			continue;
		own = clump_of_actor(iterator.datum_index);
		for (c = 0; c < RT_MAXIMUM_ACTORS && found == NONE; c++)
		{
			if (rt.clumps[c].used && rt.clumps[c].team == actor->meta.team_index && rt.clumps[c].count > 0 &&
				distance_squared3d(&rt.clumps[c].center, &actor->input.position.body_position) <= H2_CLUMP_RADIUS * H2_CLUMP_RADIUS)
			{
				found = c;
			}
		}
		if (found == own && own != NONE)
			continue;
		if (found == NONE)
		{
			for (c = 0; c < RT_MAXIMUM_ACTORS && rt.clumps[c].used; c++)
				;
			if (c == RT_MAXIMUM_ACTORS)
				continue;
			found = c;
			rt.clumps[c].used = TRUE;
			rt.clumps[c].team = actor->meta.team_index;
			rt.clumps[c].count = 0;
			rt.clumps[c].center = actor->input.position.body_position;
		}
		if (own != NONE && rt.clumps[own].count > 0)
			rt.clumps[own].count--;
		rt.clumps[found].count++;
		rt.clump_of[absolute] = (short)(found + 1);
		rt.clump_actor[absolute] = iterator.datum_index;
	}
}

/* Halo 2's peer-dead retreat (impulse 0x3f, retail 0x1b53a0): an actor of the dead one's type in its clump retreats
from its target at the character's peer dead retreat chance (a Grunt's 0.25), at its second peer dead retreat chance (0.5)
when the last peer died less than 2 s before; it tests no style switch. In place of this engine's friend-killed panic,
which wanted the killer in view (actor_stimulus.c: h2_ai_peer_deaths_own) */
H2_ALIGN static void peer_dead_retreat(long dead_unit_index, short dead_type, short dead_team, short dead_clump)
{
	struct data_iterator iterator;
	struct actor_datum *actor;
	long now = game_time_get();

	if (dead_type == NONE || HALO_ENV_ONCE("HALO_H2_PERCEIVED_LEADER_DEATH"))
		return;
	data_iterator_new(&iterator, actor_data);
	while ((actor = (struct actor_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct actor_definition const *definition;
		struct object_datum *unit;
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		real chance;

		if (actor->meta.type != dead_type || actor->meta.team_index != dead_team || actor->meta.swarm ||
			actor->meta.unit_index == NONE || actor->meta.unit_index == dead_unit_index ||
			(unit = object_try_and_get(actor->meta.unit_index)) == NULL || TEST_FLAG(unit->object.damage_flags, _object_dead_bit) ||
			absolute < 0 || absolute >= RT_MAXIMUM_ACTORS)
		{
			continue;
		}
		/* (the dead one's clump: its last one, clumps_update) */
		if (dead_clump == NONE || clump_of_actor(iterator.datum_index) != dead_clump)
			continue;
		definition = actor_definition_get(actor->meta.definition_index);
		chance = rt.peer_death_time[absolute] != 0 && now - rt.peer_death_time[absolute] < 2 * 30 ?
			*(real const *)&definition->panic.unused[3] : definition->panic.panic_chance_friend_killed;
		rt.peer_death_time[absolute] = now;
		if (actor->target.target_prop_index == NONE || actor->stimuli.panic_type >= 3)
			continue;
		if (real_seed_random(get_global_random_seed_address()) < chance)
		{
			actor->stimuli.panic_type = 3;   /* (_actor_panic_friend_same_type_killed: action_flee.c's Halo 2 retreat) */
			actor->stimuli.panic_prop_index = actor->target.target_prop_index;
			actor_set_dormant(iterator.datum_index, FALSE);
#if !defined(HALO_RELEASE) && !defined(HALO_VITA)
			{
				void h2_range_stat(long actor_index, char const *key, long value);

				h2_range_stat(iterator.datum_index, "peer_dead_retreat", (long)(chance * 100.0f));
			}
#endif
		}
	}
}

H2_ALIGN boolean h2_ai_behaviour_enabled(long actor_index, short behaviour);

/* a weapon object's AI scariness (its Halo 2 tag's, by name) */
H2_ALIGN static real weapon_scariness(long weapon_index)
{
	struct object_datum *weapon = weapon_index != NONE ? object_try_and_get(weapon_index) : NULL;
	char const *name;
	short k;

	if (!weapon)
		return 0.0f;
	for (k = 0; k < rt.weapon_scariness_count; k++)
	{
		if (rt.weapon_scariness[k].ce_tag == weapon->definition_index)
			return rt.weapon_scariness[k].value;
	}
	name = tag_get_name(weapon->definition_index);
	name = strncmp(name, "h2\\", 3) ? name : name + 3;
	for (k = 0; k < rt.weapon_scariness_count; k++)
	{
		if (rt.weapon_scariness[k].ce_tag == NONE && !strcmp(rt.weapon_scariness[k].name, name))
		{
			rt.weapon_scariness[k].ce_tag = weapon->definition_index;
			return rt.weapon_scariness[k].value;
		}
	}
	return 0.0f;
}

/* a unit's scariness (tag test 0xaa3a20): its character's (an actor's) or the AI globals' player scariness (a
player's), its weapon's AI scariness, the berserking actor scariness while berserk. Not ported: the vehicle seat's
scariness and a second (dual wielded) weapon's */
H2_ALIGN static real unit_scariness(long unit_index)
{
	struct unit_datum *unit = unit_index != NONE ? (struct unit_datum *)object_try_and_get(unit_index) : NULL;
	struct actor_datum *actor;
	real scariness = 0.0f;

	if (!unit || (unit->object.type != _object_type_biped && unit->object.type != _object_type_vehicle))
		return 0.0f;
	actor = unit->unit.actor_index != NONE ? actor_try_and_get(unit->unit.actor_index) : NULL;
	if (actor && actor->meta.definition_index != NONE)
	{
		struct actor_definition const *definition = actor_definition_get(actor->meta.definition_index);

		if (definition->communication.unused_communication[0] == H2_SCARINESS_MARK)
			scariness = *(real const *)&definition->communication.unused_communication[1];
	}
	else if (unit->unit.player_index != NONE)
		scariness = rt.player_scariness;
	if (unit->unit.current_weapon_index >= 0 && unit->unit.current_weapon_index < MAXIMUM_WEAPONS_PER_UNIT)
		scariness += weapon_scariness(unit->unit.weapon_object_indices[unit->unit.current_weapon_index]);
	if (actor && actor->emotions.berserk)
		scariness += rt.berserk_scariness;
	return scariness;
}

/* (actions.c) Halo 2's scary target retreat (impulse 0x3d, tag test 0xa22a40): an actor whose target is
scarier than it by the character's retreat scary target threshold or more (target scariness minus its own, the
prop's tracking 0x60: 0xaa6500) and is facing it - the tracking's 0x54 at 0.8: seen, and the target's aim
within 25 degrees of it or passing within 1.5 wu (0xaa7da0's classes 0 and 1) - retreats from it (behaviour
0x38, dialogue retreat_from_scary_target). A threshold of 0 never does. 05a: the ridge Grunts placed without
their Elites face two ODSTs with battle rifles and run (tag test ridge-n1..n5: 5-15 flee lines in 2.5 s) */
H2_ALIGN boolean h2_ai_scary_target_retreat(
	long actor_index)
{
	struct actor_datum *actor = actor_get(actor_index);
	struct actor_definition const *definition;
	struct prop_datum *prop;
	struct unit_datum *target;
	real threshold, difference, c, distance, lateral;
	real_vector3d to_me;

	if (!rt.loaded || actor->meta.definition_index == NONE || actor->target.target_prop_index == NONE ||
		actor->meta.unit_index == NONE || HALO_ENV_ONCE("HALO_H2_NO_SCARY_RETREAT"))
	{
		return FALSE;
	}
	definition = actor_definition_get(actor->meta.definition_index);
	if (definition->communication.unused_communication[0] != H2_SCARINESS_MARK)
		return FALSE;
	threshold = *(real const *)&definition->communication.unused_communication[2];
	if (!(threshold > 0.0f))
		return FALSE;
	prop = prop_get(actor->target.target_prop_index);
	if (prop->unit_index == NONE || prop->visibility <= _actor_perception_none ||
		!(target = (struct unit_datum *)object_try_and_get(prop->unit_index)) || TEST_FLAG(target->object.damage_flags, _object_dead_bit) ||
		(target->object.type != _object_type_biped && target->object.type != _object_type_vehicle))
	{
		return FALSE;
	}
	difference = unit_scariness(prop->unit_index) - unit_scariness(actor->meta.unit_index);
	if (difference < threshold)
		return FALSE;
	/* (the target facing it: 0xaa7da0) */
	vector_from_points3d(&prop->body_position, &actor->input.position.body_position, &to_me);
	distance = normalize3d(&to_me);
	c = dot_product3d(&target->unit.aiming_vector, &to_me);
	lateral = c <= 0.0f ? 1.0e30f : c >= 1.0f ? 0.0f : (real)sqrt(1.0f - c * c) * distance;
	if (!(c > 0.9063f || lateral < 1.5f))
		return FALSE;
	if (!h2_ai_behaviour_enabled(actor_index, 0x38) || !h2_ai_behaviour_enabled(actor_index, 0x3d))
		return FALSE;
	rt.scary_retreats++;
#if !defined(HALO_RELEASE) && !defined(HALO_VITA)
	{
		void h2_range_stat(long actor_index, char const *key, long value);

		h2_range_stat(actor_index, "scary_target_retreat", (long)(difference * 100.0f));
	}
#endif
	return TRUE;
}

/* (actor_stimulus.c) whether a Halo 2 level's peer deaths are h2_ai.c's (peer_dead_retreat) */
H2_ALIGN boolean h2_ai_peer_deaths_own(
	void)
{
	return rt.loaded && !HALO_ENV_ONCE("HALO_H2_PERCEIVED_LEADER_DEATH");
}

/* (actor_stimulus.c) whether a friend's death counts as its leader's for an
actor (function_1b52a0): a Brute is this engine's Elite but leads no Grunt,
and with another of the leader's type near the actor stays */
H2_ALIGN boolean h2_ai_leader_killed(
	long actor_index,
	long dead_unit_index)
{
	struct actor_datum *actor;
	struct data_iterator iterator;
	struct actor_datum *other;

	if (!rt.loaded)
		return TRUE;
	/* (a Halo 2 campaign level: the leader's death retreats its clump as it
	happens - leader_dead_retreat -, not as this engine's actors perceive it) */
	if (!HALO_ENV_ONCE("HALO_H2_PERCEIVED_LEADER_DEATH"))
		return FALSE;
	if (dead_unit_index != NONE && object_try_and_get(dead_unit_index) &&
		strstr(tag_get_name(object_get(dead_unit_index)->definition_index), "brute"))
	{
		return FALSE;
	}
	actor = actor_get(actor_index);
	data_iterator_new(&iterator, actor_data);
	while ((other = (struct actor_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (other->meta.encounter_index == actor->meta.encounter_index && other->meta.type == _actor_elite &&
			other->meta.unit_index != NONE && other->meta.unit_index != dead_unit_index && object_try_and_get(other->meta.unit_index) &&
			!TEST_FLAG(object_get(other->meta.unit_index)->object.damage_flags, _object_dead_bit))
		{
			return FALSE;
		}
	}
	return TRUE;
}

H2_ALIGN boolean h2_ai_firing_position_allowed(long actor_index, struct firing_position_definition const *position);

/* (squad_follow_update) whether an actor's firing position is one of its
follow order's areas active now: else it is let go and the actor looks
again (function_204450's actor loop, 0x2628f0) */
H2_ALIGN static void actor_follow_position_check(long actor_index)
{
	struct actor_datum *actor;
	struct encounter_definition *encounter;
	boolean guard_position;
	short index;

	if (!actor_alive(actor_index))
		return;
	actor = actor_get(actor_index);
	guard_position = actor->state.action == _actor_action_guard && actor->state.action_data.guard.guard_location_type == 3;
	index = guard_position ? actor->state.action_data.guard.guard_firing_position_index : actor->firing_positions.current_position_index;
	if (index != NONE && actor->meta.encounter_index != NONE)
	{
		encounter = TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->ai_encounters,
			DATUM_INDEX_TO_ABSOLUTE_INDEX(actor->meta.encounter_index), struct encounter_definition);
		if (index >= 0 && index < encounter->firing_positions.count)
		{
			struct firing_position_definition const *p = TAG_BLOCK_GET_ELEMENT(&encounter->firing_positions, index,
				struct firing_position_definition);

			if (p->unresolved[0] == (byte)(H2_AI_ORDER_MARK & 0xFF) && h2_ai_firing_position_allowed(actor_index, p))
				return;
		}
	}
	rt.follow_positions_cleared++;
	actor->firing_positions.current_position_index = NONE;
	if (actor->state.action == _actor_action_guard && !command_script_of_actor(actor_index))
		actor->state.action_data.guard.find_new_guard_position = TRUE;
}

/* a squad's follow order brought up to date (function_204450, each two
seconds, function_203bf0): what it follows - the closest player, or the follow
squad's first actor -, its areas whose centre is within radius + follow radius
of that (10 at most), else the nearest of them. What changed sends its guards
to a new position */
H2_ALIGN static void squad_follow_update(short h2_squad)
{
	struct rt_squad *s = &rt.squads[h2_squad];
	struct rt_order const *o;
	struct rt_area const *set;
	short set_count;
	real_point3d at;
	unsigned long active = 0;
	short a, count = 0, nearest = NONE;
	real best = 1.0e12f;
	long unit = NONE;

	if (s->order < 0 || s->order >= rt.order_count)
		return;
	o = &rt.orders[s->order];
	/* (the squad's area set: the secondary one once its trigger held - 0x204450 reads squad +0x60) */
	set = s->secondary ? o->areas2 : o->areas;
	set_count = s->secondary ? o->area2_count : o->area_count;
	if (!(o->flags & (H2_ORDER_FOLLOW_CLOSEST_PLAYER | H2_ORDER_FOLLOW_SQUAD)) || set_count <= 0)
		return;
	/* (0x203360: again when more than 2 s went by) */
	if (s->follow_order == s->order && game_time_get() - s->follow_time <= RT_FOLLOW_TICKS)
		return;
	if (o->flags & H2_ORDER_FOLLOW_CLOSEST_PLAYER)
	{
		long player = local_player_get_player_index(0);

		unit = player != NONE ? player_get(player)->unit_index : NONE;
	}
	else if (o->follow_squad >= 0)
	{
		long actors[8];

		if (actors_of(o->follow_squad, actors, 8) > 0)
			unit = actor_get(actors[0])->meta.unit_index;
	}
	if (unit == NONE || !object_try_and_get(unit))
		return;
	object_get_origin(unit, &at);
	for (a = 0; a < set_count && count < RT_MAXIMUM_ACTIVE_AREAS; a++)
	{
		real distance = (real)sqrt(distance_squared(&set[a].centre, &at));

		if (set[a].radius + o->follow_radius > distance)
		{
			active |= 1UL << a;
			count++;
		}
		else if (distance < best)
		{
			best = distance;
			nearest = a;
		}
	}
	if (!count && nearest != NONE)
		active = 1UL << nearest;
	/* (HALO_H2_FOLLOW_LOG=1: each update - the audit's follow test) */
	if (HALO_ENV_ONCE("HALO_H2_FOLLOW_LOG"))
		platform_log("h2 ai: follow: tick %ld squad %d order %d at %.1f %.1f %.1f active %08lx (%d) nearest %d",
			(long)game_time_get(), (int)h2_squad, (int)s->order, (double)at.x, (double)at.y, (double)at.z, active, (int)count, (int)nearest);
	if (active != s->active_areas || s->follow_order != s->order)
		rt.follow_updates++;
	s->active_areas = active;
	s->active_secondary = s->secondary;
	s->follow_order = s->order;
	s->follow_time = game_time_get();
	/* (0x204450's end, each update: an actor whose firing position is not in
	one of the areas active now - or that has none - loses it (0x2628f0) and
	looks again (actor +0x227 = 0); one whose position is still in them keeps
	it) */
	{
		long actors[RT_MAXIMUM_ACTORS];
		short n = actors_of(h2_squad, actors, RT_MAXIMUM_ACTORS), k;

		for (k = 0; k < n; k++)
			actor_follow_position_check(actors[k]);
	}
}

/* (actor_firing_position.c) whether a firing position is one an actor may
take: on a follow order, in one of the areas active now */
H2_ALIGN boolean h2_ai_firing_position_allowed(
	long actor_index,
	struct firing_position_definition const *position)
{
	short s;
	short order;
	boolean set;

	if (!rt.loaded || position->unresolved[0] != (byte)(H2_AI_ORDER_MARK & 0xFF))
		return TRUE;
	s = actor_h2_squad(actor_index);
	if (s == NONE)
		return TRUE;
	order = rt.squads[s].order;
	if (order < 0 || order >= rt.order_count || (short)(position->unresolved[2] | (position->unresolved[3] << 8)) != order + 1)
		return TRUE;
	set = (position->unresolved[1] & 0x80) != 0;
	/* (a follow order's: those of the areas active now - the list 0x204450
	made from the squad's set at its last update, 0x205010) */
	if (rt.squads[s].follow_order == order && (rt.orders[order].flags & (H2_ORDER_FOLLOW_CLOSEST_PLAYER | H2_ORDER_FOLLOW_SQUAD)))
	{
		if (set != (rt.squads[s].active_secondary != FALSE))
			return FALSE;
		if ((position->unresolved[1] & 0x7F) >= 32)
			return TRUE;
		return (rt.squads[s].active_areas >> (position->unresolved[1] & 0x7F)) & 1;
	}
	/* (any other order's: those of the squad's area set - the primary set's,
	or the secondary's once its trigger held, squad +0x60) */
	return set == (rt.squads[s].secondary != FALSE);
}

/* (actor_firing_position.c) whether a firing position is in one of its
order's SEARCH areas (the order's area set, type 1): Halo 2 picks an actor's
fighting and guarding positions in its order's default and goal areas, its
search areas only while searching (05a: the ledge Grunts placed in search
areas 10, 11 and 36 of lz_enemies_ledge_01 walk to firing positions of its
default areas 4, 5 and 6 and fight from there - real/lctrl-1 rows end within
0.2 wu of positions 47, 52, 68, 69). An order with no other areas keeps them
all (HALO_H2_SEARCH_AREAS_ANY=1: every area, as before) */
H2_ALIGN boolean h2_ai_firing_position_search_area(
	struct firing_position_definition const *position)
{
	short order, area, a, set_count;
	struct rt_area const *set;
	static int any = -1;

	if (any < 0)
		any = HALO_ENV_ONCE("HALO_H2_SEARCH_AREAS_ANY") && atoi(HALO_ENV_ONCE("HALO_H2_SEARCH_AREAS_ANY"));
	if (any || !rt.loaded || position->unresolved[0] != (byte)(H2_AI_ORDER_MARK & 0xFF))
		return FALSE;
	order = (short)(position->unresolved[2] | (position->unresolved[3] << 8)) - 1;
	area = position->unresolved[1] & 0x7F;
	if (order < 0 || order >= rt.order_count)
		return FALSE;
	/* (the set the position is of: 0x80 the secondary) */
	set = (position->unresolved[1] & 0x80) ? rt.orders[order].areas2 : rt.orders[order].areas;
	set_count = (position->unresolved[1] & 0x80) ? rt.orders[order].area2_count : rt.orders[order].area_count;
	if (area >= set_count || set[area].type != 1)
		return FALSE;
	for (a = 0; a < set_count; a++)
	{
		if (set[a].type != 1)
			return TRUE;
	}
	return FALSE;
}

boolean h2_ai_vehicle_reserved(long vehicle_index);

/* the local player's vehicle: an actor goes to it (halo2-upstream-work
unknown_1b8c80.cpp, function_1b9640, slot test 0x4d, "enter_friendly_vehicle"):
an actor on foot with no command script, not an enemy of the player, boards the
player's outermost vehicle if a seat is free for it and the vehicle is alive
enough (function_1ba990: its health 0.5 at least), within 12 wu - twice that
if it is the actor's own squad's - and not running away (speed^2 0.25 at most
a tick). An order that inhibits vehicle use keeps its squad out */
H2_ALIGN static void player_vehicle_boarding_update(void)
{
	long player = local_player_get_player_index(0), unit, vehicle, parent;
	struct data_iterator iterator;
	struct actor_datum *actor;
	struct object_datum *object;
	real_point3d at;
	real_vector3d const *v;

	if (player == NONE || (unit = player_get(player)->unit_index) == NONE || !object_try_and_get(unit))
		return;
	vehicle = object_get(unit)->object.parent_object_index;
	if (vehicle == NONE)
		return;
	if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG") && game_time_get() % 150 == 0)
		platform_log("h2 ai: boarding pass: vehicle %lx vitality %.2f speed2 %.3f", (unsigned long)vehicle, object_get(vehicle)->object.body_vitality,
			object_get(vehicle)->object.translational_velocity.i * object_get(vehicle)->object.translational_velocity.i);
	while ((parent = object_get(vehicle)->object.parent_object_index) != NONE)
		vehicle = parent;
	object = object_get(vehicle);
	if (object->object.type != _object_type_vehicle || object->object.body_vitality < 0.5f)
		return;
	v = &object->object.translational_velocity;
	if (v->i * v->i + v->j * v->j + v->k * v->k > 0.25f)
		return;
	object_get_origin(vehicle, &at);
	data_iterator_new(&iterator, actor_data);
	while ((actor = (struct actor_datum *)data_iterator_next(&iterator)) != NULL)
	{
		short s;
		real_point3d origin;
		real radius = 12.0f;

		if (actor->meta.unit_index == NONE || !object_try_and_get(actor->meta.unit_index) || actor->meta.swarm ||
			object_get(actor->meta.unit_index)->object.parent_object_index != NONE ||
			actor->state.action == _actor_action_vehicle || command_script_of_actor(iterator.datum_index) ||
			game_team_is_enemy(object_get(actor->meta.unit_index)->object.owner_team_index, object_get(unit)->object.owner_team_index))
		{
			continue;
		}
		s = actor_h2_squad(iterator.datum_index);
		if (s != NONE)
		{
			if (rt.squads[s].order >= 0 && rt.squads[s].order < rt.order_count &&
				(rt.orders[rt.squads[s].order].flags & H2_ORDER_INHIBIT_VEHICLE_USE))
			{
				continue;
			}
			if (rt.squads[s].last_vehicle == vehicle)
				radius = 24.0f;
		}
		object_get_origin(actor->meta.unit_index, &origin);
		if (distance_squared(&origin, &at) >= radius * radius)
			continue;
		if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG") && game_time_get() % 150 == 0)
			platform_log("h2 ai: boarding pass: actor %lx in range, action %d", (unsigned long)iterator.datum_index, (int)actor->state.action);
		if (actor_action_try_to_enter_vehicle(iterator.datum_index, vehicle, "", NONE, 0, NULL))
			rt.player_vehicle_boardings++;
		else if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
		{
			short seats[16], n = vehicle_scripting_find_available_seats(vehicle, "", NONE, seats, 16), k;
			extern boolean unit_has_animation_to_enter_seat(long unit_index, long vehicle_index, short seat_index);

			for (k = 0; k < n; k++)
				platform_log("h2 ai: boarding: actor %lx seat %d of %d: animation %d reserved %d", (unsigned long)iterator.datum_index, seats[k], n,
					(int)unit_has_animation_to_enter_seat(actor->meta.unit_index, vehicle, seats[k]), (int)h2_ai_vehicle_reserved(vehicle));
			if (!n)
				platform_log("h2 ai: boarding: actor %lx no seats", (unsigned long)iterator.datum_index);
		}
	}
}

/* a Jackal's arm shield shows while it has shield left: its model's variants
have region shield permutation "active" in state "shielded", else "inactive"
(the model tag's minor and major). The shieldless (a sniper's) has no shield */
H2_ALIGN static void jackal_shields_update(void)
{
	struct data_iterator iterator;
	struct actor_datum *actor;

	data_iterator_new(&iterator, actor_data);
	while ((actor = (struct actor_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct object_datum *object = actor->meta.unit_index != NONE ? object_try_and_get(actor->meta.unit_index) : NULL;

		if (object && object->object.maximum_shield_vitality > 0.0f && strstr(tag_get_name(object->definition_index), "jackal"))
		{
			struct object_definition *definition = object_definition_get(object->definition_index);
			struct model *model = definition->object.model.index != NONE ? model_definition_get(definition->object.model.index) : NULL;
			short region = model ? model_region_find(model, "shield") : NONE;
			short inactive = region != NONE ? model_permutation_find(TAG_BLOCK_GET_ELEMENT(&model->regions, region, struct model_region),
				"inactive") : NONE;
			short permutation = NONE;

			/* (with shield left: its variant's own permutation - a major's
			"major", a minor's "active" (the model's variants' state
			"shielded"); the variant it was placed with: h2_ai_object_variant.
			Its collision follows the permutation: h2_biped.c) */
			if (region != NONE && object->object.shield_vitality > 0.0f)
			{
				short b, chosen = (short)(object->object.variant_number - 1);

				for (b = 0; b < RT_VARIANT_BIPEDS && !(rt_variant_bipeds[b].count && rt_variant_bipeds[b].definition == object->definition_index); b++)
					;
				if (b < RT_VARIANT_BIPEDS && chosen >= 0 && chosen < rt_variant_bipeds[b].count)
				{
					struct rt_variant const *v = &rt_variant_bipeds[b].variants[chosen];
					short r;

					for (r = 0; r < v->region_count && permutation == NONE; r++)
					{
						if (v->regions[r].region == region && v->regions[r].choice_count > 0)
							permutation = v->regions[r].choices[0];
					}
				}
				if (permutation == NONE)
					permutation = model_permutation_find(TAG_BLOCK_GET_ELEMENT(&model->regions, region, struct model_region), "active");
			}
			else if (region != NONE)
				permutation = inactive;
			if (permutation != NONE && permutation != inactive && object->object.region_permutations[region] != inactive)
				permutation = NONE;      /* (its own already, or a scripted one: left) */
			if (permutation != NONE)
				object->object.region_permutations[region] = (byte)permutation;
		}
	}
}

/* (HALO_H2_AI_STUCK=1) each 2 seconds: an actor on foot with a path that is
moving has come less than 0.1 wu since the last look: its place, to the wu,
gathered - the log's "stuck place" lines at the level's end, the most first:
which firing positions, sectors or instances hold actors up */
H2_ALIGN static void stuck_update(void)
{
	struct data_iterator iterator;
	struct actor_datum *actor;

	data_iterator_new(&iterator, actor_data);
	while ((actor = (struct actor_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		struct object_datum *unit = actor->meta.unit_index != NONE ? object_try_and_get(actor->meta.unit_index) : NULL;
		real_point3d at;

		if (absolute >= RT_MAXIMUM_ACTORS || !unit || unit->object.parent_object_index != NONE || actor->state.flying ||
			!actor->meta.active || actor->meta.dormant)
		{
			continue;
		}
		object_get_origin(actor->meta.unit_index, &at);
		/* (only what a player could see: a unit far off is deactivated, its
		actor planning on with the body frozen, which is not a stuck actor) */
		{
			long player = local_player_get_player_index(0);
			struct object_datum *body = player != NONE && player_get(player)->unit_index != NONE ? object_try_and_get(player_get(player)->unit_index) : NULL;
			real_point3d seen;

			if (!body)
				continue;
			object_get_origin(player_get(player)->unit_index, &seen);
			if (distance_squared(&at, &seen) > 35.0f * 35.0f)
			{
				rt.stuck_watch[absolute].at = at;
				continue;
			}
		}
		if (actor->control.moving && actor->control.path.path.valid)
		{
			rt.moving_seconds += 2;
			if (distance_squared(&at, &rt.stuck_watch[absolute].at) < 0.1f * 0.1f)
			{
				short k;
				short x = (short)floor(at.x), y = (short)floor(at.y), z = (short)floor(at.z);

				rt.stuck_seconds += 2;
				for (k = 0; k < rt.stuck_place_count; k++)
				{
					if (rt.stuck_places[k].x == x && rt.stuck_places[k].y == y && rt.stuck_places[k].z == z && rt.stuck_places[k].bsp == global_structure_bsp_index)
						break;
				}
				if (k == rt.stuck_place_count && k < (short)NUMBEROF(rt.stuck_places))
				{
					char const *name = tag_get_name(unit->definition_index), *last = strrchr(name, '\\');

					rt.stuck_place_count++;
					rt.stuck_places[k].x = x, rt.stuck_places[k].y = y, rt.stuck_places[k].z = z;
					rt.stuck_places[k].bsp = (short)global_structure_bsp_index;
					rt.stuck_places[k].action = (short)actor->state.action;
					rt.stuck_places[k].seconds = 0;
					snprintf(rt.stuck_places[k].what, sizeof(rt.stuck_places[k].what), "%s", last ? last + 1 : name);
					/* (what stands before it, by the way it is going) */
					{
						struct collision_result collision;
						real_vector3d to = { actor->control.moving_towards_point.x - at.x, actor->control.moving_towards_point.y - at.y, 0.0f };
						real length = (real)sqrt(to.i * to.i + to.j * to.j);
						real_point3d from = at;

						from.z += 0.3f;
						rt.stuck_places[k].blocked[0] = 0;
						if (length > 0.01f)
						{
							to.i *= 1.0f / length, to.j *= 1.0f / length;
							if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_objects_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
								&from, &to, actor->meta.unit_index, &collision))
							{
								if (collision.type == _collision_result_object && collision.object_index != NONE)
								{
									char const *hit = tag_get_name(object_get(collision.object_index)->definition_index), *hit_last = strrchr(hit, '\\');

									snprintf(rt.stuck_places[k].blocked, sizeof(rt.stuck_places[k].blocked), "%s", hit_last ? hit_last + 1 : hit);
								}
								else
									snprintf(rt.stuck_places[k].blocked, sizeof(rt.stuck_places[k].blocked), "structure");
							}
							else
								snprintf(rt.stuck_places[k].blocked, sizeof(rt.stuck_places[k].blocked), "open");
						}
						rt.stuck_places[k].to = actor->control.moving_towards_point;
					}
				}
				if (k < (short)NUMBEROF(rt.stuck_places))
					rt.stuck_places[k].seconds += 2;
				if (k < (short)NUMBEROF(rt.stuck_places) && rt.stuck_places[k].seconds % 40 == 2)
				{
					real_vector3d const *v = &unit->object.translational_velocity;

					platform_log("h2 ai: stuck %x at %.2f %.2f %.2f vel %.3f %.3f %.3f action %d position %d towards %.2f %.2f %.2f step %d of %d at_dest %d count %d throttle %.2f %.2f facing %.2f %.2f want %.2f type %d",
						(unsigned)iterator.datum_index, at.x, at.y, at.z, v->i, v->j, v->k, (int)actor->state.action, (int)actor->firing_positions.current_position_index,
						actor->control.moving_towards_point.x, actor->control.moving_towards_point.y, actor->control.moving_towards_point.z,
						(int)actor->control.path.path.step_index, (int)actor->control.path.path.step_count, (int)actor->control.path.at_destination,
						(int)rt.foot_stuck[absolute], actor->output.throttle.i, actor->output.throttle.j, actor->input.facing_vector.i, actor->input.facing_vector.j,
						(actor->control.moving_towards_point.x - at.x) * actor->input.facing_vector.i + (actor->control.moving_towards_point.y - at.y) * actor->input.facing_vector.j,
						(int)actor->output.movement_type);
				}
			}
		}
		rt.stuck_watch[absolute].at = at;
	}
}

H2_ALIGN static void stuck_report(void)
{
	short k, n;

	if (!HALO_ENV_ONCE("HALO_H2_AI_STUCK"))
		return;
	platform_log("h2 ai: routes: %ld searches, %ld us, worst %ld us, worst tick %ld us (%ld searches), %ld refused", rt.route_searches, rt.route_microseconds, rt.route_worst, rt.route_worst_tick, rt.route_worst_tick_count, rt.foot_searches_refused);
	platform_log("h2 ai: stuck: %ld actor seconds of %ld moving, %d places, %ld positions given up, %ld sector routes, %ld without, %ld jumps, %ld hoists, %ld kerb lifts, %ld advances", rt.stuck_seconds, rt.moving_seconds, (int)rt.stuck_place_count, rt.positions_given_up, rt.foot_routes, rt.foot_route_failures, rt.foot_hops, rt.foot_hoists, rt.foot_jumps, rt.foot_advances);
	for (n = 0; n < 16; n++)
	{
		short best = NONE;

		for (k = 0; k < rt.stuck_place_count; k++)
		{
			if (rt.stuck_places[k].seconds > 0 && (best == NONE || rt.stuck_places[k].seconds > rt.stuck_places[best].seconds))
				best = k;
		}
		if (best == NONE)
			break;
		platform_log("h2 ai: stuck place bsp %d at %d %d %d: %ld actor seconds, first %s action %d, going to %.1f %.1f %.1f, before it %s", (int)rt.stuck_places[best].bsp, rt.stuck_places[best].x,
			rt.stuck_places[best].y, rt.stuck_places[best].z, rt.stuck_places[best].seconds, rt.stuck_places[best].what, (int)rt.stuck_places[best].action,
			rt.stuck_places[best].to.x, rt.stuck_places[best].to.y, rt.stuck_places[best].to.z, rt.stuck_places[best].blocked);
		rt.stuck_places[best].seconds = 0;
	}
}

/* brute.style's last_man_berserk (retail 0x1a9bc0, halo2-upstream-work unknown_1a9b50.cpp): an actor of its
squad with no other LIVING actor of its type left in that squad goes berserk (0x1e4650). The impulse tests nothing
else; ours asks for an enemy known (Halo 2 combat status above 2), the impulse living under the engage
behaviours. The Brute pair on 08a with one shot dead: the real survivor's "brsrk" line, ours none - the dead one
still counted (a dead unit's actor stays in the squad's list a while) and a current target asked for. Brutes only:
the other styles that list it (elite, marine, sentinels...) are not measured (brute_no_berserk's orders and Tartarus
are not told apart from the plain style but for Tartarus himself) */
H2_ALIGN static void brutes_last_man_update(void)
{
	struct data_iterator iterator;
	struct actor_datum *actor;

	data_iterator_new(&iterator, actor_data);
	while ((actor = (struct actor_datum *)data_iterator_next(&iterator)) != NULL)
	{
		char const *name = actor->meta.definition_index != NONE ? tag_get_name(actor->meta.definition_index) : "";
		short squad;

		if (!strstr(name, "\\brute") || strstr(name, "tartarus") || actor->meta.swarm || !actor_alive(iterator.datum_index))
			continue;
		squad = actor_h2_squad(iterator.datum_index);
		if (squad != NONE && squad < MAXIMUM_SQUADS)
		{
			long others[RT_MAXIMUM_ACTORS];
			short count = actors_of(squad, others, RT_MAXIMUM_ACTORS), k, alike = 0, living = 0;

			for (k = 0; k < count; k++)
			{
				struct actor_datum *other = actor_try_and_get(others[k]);

				if (!other || !actor_alive(others[k]))
					continue;
				living++;
				if (others[k] != iterator.datum_index && other->meta.type == actor->meta.type)
					alike++;
			}
			rt.squad_peak[squad] = MAX(rt.squad_peak[squad], living);
			if (!alike && rt.squad_peak[squad] > 1 && !actor->emotions.berserk && actor_combat_status(iterator.datum_index) > 2)
			{
				actor_berserk(iterator.datum_index, TRUE);
				rt.last_men++;
				if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
					platform_log("h2 ai: last man berserk: squad %d", (int)squad);
			}
		}
	}
}

/* (actor_action_try_to_seek_cover) whether the actor's Halo 2 style lacks the
cover behaviour - a sniper's: jackal_sniper.style has fight, engage and retreats, no
cover, cover peek or perch */
H2_ALIGN boolean h2_ai_actor_style_no_cover(long actor_index)
{
	struct actor_datum *actor;

	if (!rt.loaded)
		return FALSE;
	actor = actor_get(actor_index);
	return actor->meta.definition_index != NONE && strstr(tag_get_name(actor->meta.definition_index), "jackal_sniper") != NULL;
}

/* whether anything solid - the structure, an instance, an object - lies on a line */
static boolean flier_line_blocked(real_point3d const *from, real_vector3d const *line, long ignore)
{
	struct collision_result collision;

	return collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit) |
		FLAG(_collision_test_front_facing_surfaces_bit), from, line, ignore, &collision);
}

/* whether a Halo 2 campaign level's AI runs (source/ai's hooks) */
H2_ALIGN boolean h2_ai_running(
	void)
{
	return rt.loaded;
}

/* (actors.c) whether an actor may speak its own combat lines: a command
script's actor only if cs_enable_dialogue says so (unknown_20fe20.cpp reads the
flag for an actor with a script) */
H2_ALIGN boolean h2_ai_actor_may_speak(
	long actor_index)
{
	struct rt_command_script const *c;

	if (!rt.loaded)
		return TRUE;
	c = command_script_of_actor(actor_index);
	return !c || (c->flags & CS_DIALOGUE) != 0;
}

H2_ALIGN boolean h2_ai_actor_may_speak_unit(
	long unit_index)
{
	struct object_datum *object = unit_index != NONE ? object_try_and_get(unit_index) : NULL;
	long actor_index;

	if (!object || object->object.type != _object_type_biped)
		return TRUE;
	actor_index = unit_get(unit_index)->unit.actor_index;
	return actor_index == NONE || h2_ai_actor_may_speak(actor_index);
}

/* (h2_dialogue.c) a campaign character's unit: its Halo 2 biped's tag path
(h2_biped.c's table) and the model variant it shows (h2_ai_object_variant
kept its number) */
H2_ALIGN boolean h2_ai_object_character(
	long object_index,
	char *biped_path,
	long path_size,
	char *variant,
	long variant_size)
{
	struct object_datum *object = object_index != NONE ? object_try_and_get(object_index) : NULL;
	short b;

	if (!object || object->object.type != _object_type_biped)
		return FALSE;
	for (b = 0; b < RT_VARIANT_BIPEDS && rt_variant_bipeds[b].definition != object->definition_index; b++)
		;
	if (b == RT_VARIANT_BIPEDS || !h2_bipeds_source_path(b))
		return FALSE;
	snprintf(biped_path, (size_t)path_size, "%s", h2_bipeds_source_path(b));
	variant[0] = 0;
	if (object->object.variant_number > 0 && object->object.variant_number <= rt_variant_bipeds[b].count)
		snprintf(variant, (size_t)variant_size, "%s", rt_variant_bipeds[b].variants[object->object.variant_number - 1].name);
	return TRUE;
}

H2_ALIGN boolean h2_ai_actor_follows_player(
	long actor_index)
{
	short h2_squad = rt.loaded ? actor_h2_squad(actor_index) : NONE;
	struct rt_squad const *s;

	if (h2_squad == NONE)
		return FALSE;
	s = &rt.squads[h2_squad];
	if (s->order < 0 || s->order >= rt.order_count)
		return FALSE;
	return (rt.orders[s->order].flags & H2_ORDER_FOLLOW_CLOSEST_PLAYER) != 0;
}

H2_ALIGN short h2_ai_actor_forced_combat_status(
	long actor_index)
{
	short h2_squad = rt.loaded ? actor_h2_squad(actor_index) : NONE;
	struct rt_squad const *s;

	if (h2_squad == NONE)
		return 0;
	s = &rt.squads[h2_squad];
	if (s->order < 0 || s->order >= rt.order_count)
		return 0;
	return rt.orders[s->order].force_status;
}

H2_ALIGN boolean h2_ai_actor_is_leader(
	long actor_index)
{
	struct actor_datum *actor = actor_try_and_get(actor_index);
	struct data_iterator iterator;
	struct actor_datum *other;
	boolean any_leader_type = FALSE, first = TRUE;

	if (!actor || actor->meta.encounter_index == NONE)
		return FALSE;
	if (actor->meta.unique_leader)
		return TRUE;
	data_iterator_new(&iterator, actor_data);
	while ((other = (struct actor_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (other->meta.encounter_index != actor->meta.encounter_index || other->meta.squad_index != actor->meta.squad_index ||
			other->meta.unit_index == NONE || !object_try_and_get(other->meta.unit_index) ||
			TEST_FLAG(object_get(other->meta.unit_index)->object.damage_flags, _object_dead_bit))
		{
			continue;
		}
		if (other->meta.type == _actor_elite && other->meta.type != actor->meta.type)
			any_leader_type = TRUE;
		if (iterator.datum_index < actor_index)
			first = FALSE;
	}
	if (actor->meta.type == _actor_elite)
		return TRUE;
	return !any_leader_type && first;
}

/* ---------- firing positions on a moving machine (the Scarab's deck) */

/* Halo 2's firing positions may stand in a reference frame - a node of a
mobile machine, 03b's Scarab: its zone's firing positions (0x20 each) have
their frame at 0x0C, their position the frame's - and its actors move
between them as the machine walks. This engine's firing positions are the
world's and its paths the structure's, so: at the level's load the frame
positions are listed (rt_deck.points); an encounter made ready finds its own
among them by their position (the builder copies Halo 2's position as it
is) and they are not put on this BSP's ground; every few ticks each is put
where its frame's node now is (frame_transform); and an actor on foot of
such an encounter standing among them (one within 12 wu and 3 wu of its
height: on the machine) walks from one to another itself, straight across
the deck,
holding each for its combat position time (its actor definition's, Halo
2's character's 3-8 s), facing and fighting as its own AI has it. */

enum
{
	RT_DECK_POINTS = 512,
	RT_DECK_LINKS = 512
};

static struct
{
	struct
	{
		real_point3d local;
		short frame;
	} points[RT_DECK_POINTS];
	short point_count;
	struct
	{
		short encounter, position, frame;
		real_point3d local;
	} links[RT_DECK_LINKS];
	short link_count;
	unsigned long encounters[128 / 32];   /* the encounters with links */
	short goal[RT_MAXIMUM_ACTORS];        /* its link + 1 (0 none) */
	long until[RT_MAXIMUM_ACTORS];        /* when it moves on */
	unsigned long arrived[RT_MAXIMUM_ACTORS / 32];
	long moves, carried, arrivals;
} rt_deck;

/* (h2_ai_map_loaded) the level's frame firing positions */
H2_ALIGN static void deck_read(struct h2_cache *cache, unsigned long scenario)
{
	long zone_count, z;
	unsigned long zones;

	memset(&rt_deck, 0, sizeof(rt_deck));
	if (!h2_cache_block(cache, scenario + H2S_ZONES, H2_ZONE_SIZE, &zone_count, &zones))
		return;
	for (z = 0; z < zone_count; z++)
	{
		long count, k;
		unsigned long positions;

		if (!h2_cache_block(cache, zones + H2_ZONE_SIZE * z + 0x28, H2_FIRING_POSITION_SIZE, &count, &positions))
			continue;
		for (k = 0; k < count && rt_deck.point_count < RT_DECK_POINTS; k++)
		{
			unsigned char const *p = h2_cache_at(cache, positions + H2_FIRING_POSITION_SIZE * k, H2_FIRING_POSITION_SIZE);
			short frame = p ? get16(p + 0x0C) : NONE;

			if (frame < 0)
				continue;
			memcpy(&rt_deck.points[rt_deck.point_count].local, p, 12);
			rt_deck.points[rt_deck.point_count].frame = frame;
			rt_deck.point_count++;
		}
	}
	if (rt_deck.point_count)
		platform_log("h2 ai: %d firing positions in reference frames (machines' nodes)", rt_deck.point_count);
}

/* (encounter_prepare) whether firing position `index` of the encounter is a
frame position (then linked: carried by its machine) */
H2_ALIGN static boolean deck_link(short encounter_index, short index, struct firing_position_definition const *f)
{
	short k, l;

	for (l = 0; l < rt_deck.link_count; l++)
	{
		if (rt_deck.links[l].encounter == encounter_index && rt_deck.links[l].position == index)
			return TRUE;
	}
	for (k = 0; k < rt_deck.point_count; k++)
	{
		if (memcmp(&rt_deck.points[k].local, &f->position, sizeof(real_point3d)))
			continue;
		if (rt_deck.link_count >= RT_DECK_LINKS || encounter_index >= 128)
			return TRUE;
		rt_deck.links[rt_deck.link_count].encounter = encounter_index;
		rt_deck.links[rt_deck.link_count].position = index;
		rt_deck.links[rt_deck.link_count].frame = rt_deck.points[k].frame;
		rt_deck.links[rt_deck.link_count].local = rt_deck.points[k].local;
		rt_deck.link_count++;
		rt_deck.encounters[encounter_index / 32] |= 1UL << (encounter_index % 32);
		return TRUE;
	}
	return FALSE;
}

/* (encounter_order_page_in) firing position `index` of the encounter written over: no longer carried */
H2_ALIGN static void deck_unlink(short encounter_index, short index)
{
	short l;

	for (l = 0; l < rt_deck.link_count; l++)
	{
		if (rt_deck.links[l].encounter == encounter_index && rt_deck.links[l].position == index)
		{
			rt_deck.links[l] = rt_deck.links[--rt_deck.link_count];
			return;
		}
	}
}

/* (h2_ai_update, every third tick) the linked firing positions put where
their machines' nodes are */
H2_ALIGN static void deck_carry(void)
{
	struct scenario *scenario;
	short l;

	if (!rt_deck.link_count || game_time_get() % 3 || global_scenario_index == NONE)
		return;
	scenario = global_scenario_get();
	{
		/* (perf/vita-native: frame_machine_memo, above) */
		static int memo_on = -1;
		static unsigned long memo_seen;
		static struct frame_machine_memo memo;

		halo_native_switch("HALO_H2_FRAME_MEMO", &memo_on, &memo_seen); /* (native_switch.h) */
		memo.count = 0;
		frame_machine_memo = memo_on ? &memo : NULL;
	}
	for (l = 0; l < rt_deck.link_count; l++)
	{
		struct encounter_definition *encounter;
		struct firing_position_definition *f;
		real_point3d at = rt_deck.links[l].local;
		struct location location;

		if (rt_deck.links[l].encounter >= scenario->ai_encounters.count)
			continue;
		encounter = TAG_BLOCK_GET_ELEMENT(&scenario->ai_encounters, rt_deck.links[l].encounter, struct encounter_definition);
		if (rt_deck.links[l].position >= encounter->firing_positions.count || !frame_transform(rt_deck.links[l].frame, &at))
			continue;
		f = TAG_BLOCK_GET_ELEMENT(&encounter->firing_positions, rt_deck.links[l].position, struct firing_position_definition);
		f->position = at;
		scenario_location_from_point(&location, &at);
		f->cluster_index = location.cluster_index;
		f->surface_index = NONE;
		rt_deck.carried++;
		if (l == 0 && game_time_get() % 150 == 0 && HALO_ENV_ONCE("HALO_H2_DECK_LOG"))
			platform_log("h2 ai: deck: %s's position %d (frame %d, local %.2f %.2f %.2f) at %.2f %.2f %.2f", encounter->name,
				(int)rt_deck.links[l].position, (int)rt_deck.links[l].frame, rt_deck.links[l].local.x, rt_deck.links[l].local.y,
				rt_deck.links[l].local.z, at.x, at.y, at.z);
	}
	frame_machine_memo = NULL;
}

/* (h2_ai_actor_output) an actor on foot of an encounter with linked
positions, off this structure's surfaces: its walk between them; TRUE: its
throttle set */
H2_ALIGN static boolean deck_walk(long actor_index, struct actor_datum *actor, struct object_datum *unit)
{
	long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index), now = game_time_get();
	short encounter_index, l, goal, choices[64], choice_count = 0;
	struct encounter_definition *encounter;
	struct firing_position_definition *f;
	real_point3d origin;
	real_vector3d to, left;
	real distance;

	if (!rt_deck.link_count || absolute >= RT_MAXIMUM_ACTORS || actor->meta.encounter_index == NONE)
		return FALSE;
	encounter_index = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(actor->meta.encounter_index);
	if (encounter_index < 0 || encounter_index >= 128 || !(rt_deck.encounters[encounter_index / 32] & (1UL << (encounter_index % 32))))
		return FALSE;
	if (encounter_index >= global_scenario_get()->ai_encounters.count)
		return FALSE;
	encounter = TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->ai_encounters, encounter_index, struct encounter_definition);
	object_get_origin(actor->meta.unit_index, &origin);
	goal = rt_deck.goal[absolute] - 1;
	/* (a goal another encounter's - left by the actor that had this slot
	before, or by this one before it was migrated: 01b's elevator Grunts got
	a stairs Grunt's position 413 of their encounter's 108, tag_groups.c's
	index assert -: another) */
	if (goal >= 0 && (goal >= rt_deck.link_count || rt_deck.links[goal].encounter != encounter_index ||
		rt_deck.links[goal].position >= encounter->firing_positions.count))
	{
		goal = NONE;
	}
	/* (its goal left behind - the machine walked off, the actor fell -:
	another) */
	if (goal >= 0)
	{
		f = TAG_BLOCK_GET_ELEMENT(&encounter->firing_positions, rt_deck.links[goal].position, struct firing_position_definition);
		if (distance_squared3d(&origin, &f->position) > 12.0f * 12.0f || fabsf(f->position.z - origin.z) > 3.0f)
			goal = NONE;
	}
	if (goal < 0 || goal >= rt_deck.link_count || now >= rt_deck.until[absolute])
	{
		/* (another of its encounter's, on the same machine - within 12 wu -,
		at random) */
		for (l = 0; l < rt_deck.link_count && choice_count < 64; l++)
		{
			if (rt_deck.links[l].encounter != encounter_index || l == goal || rt_deck.links[l].position >= encounter->firing_positions.count)
				continue;
			f = TAG_BLOCK_GET_ELEMENT(&encounter->firing_positions, rt_deck.links[l].position, struct firing_position_definition);
			if (distance_squared3d(&origin, &f->position) < 12.0f * 12.0f && fabsf(f->position.z - origin.z) < 3.0f)
				choices[choice_count++] = l;
		}
		if (!choice_count)
		{
			if (now % 150 == 0 && HALO_ENV_ONCE("HALO_H2_DECK_LOG"))
				platform_log("h2 ai: deck: %lx at %.2f %.2f %.2f: none of %s's positions near", (unsigned long)actor_index, origin.x, origin.y,
					origin.z, encounter->name);
			rt_deck.goal[absolute] = 0;
			return FALSE;
		}
		goal = choices[random_range(0, choice_count)];
		rt_deck.goal[absolute] = goal + 1;
		rt_deck.arrived[absolute / 32] &= ~(1UL << (absolute % 32));
		/* (the way there: at most 10 s) */
		rt_deck.until[absolute] = now + 10 * TICKS_PER_SECOND;
		rt_deck.moves++;
		if (HALO_ENV_ONCE("HALO_H2_DECK_LOG"))
			platform_log("h2 ai: deck: %lx to %s's position %d", (unsigned long)actor_index, encounter->name, (int)rt_deck.links[goal].position);
	}
	f = TAG_BLOCK_GET_ELEMENT(&encounter->firing_positions, rt_deck.links[goal].position, struct firing_position_definition);
	to.i = f->position.x - origin.x, to.j = f->position.y - origin.y, to.k = 0.0f;
	distance = (real)sqrt(to.i * to.i + to.j * to.j);
	if (distance < 1.0f || fabsf(f->position.z - origin.z) > 3.0f)
	{
		if (distance < 1.0f && rt_deck.goal[absolute] > 0 && !(rt_deck.arrived[absolute / 32] & (1UL << (absolute % 32))))
		{
			struct actor_definition *definition = actor_definition_get(actor->meta.definition_index);
			real lower = MAX(definition->firing_position.combat_position_time_lower_bound, 1.0f);
			real upper = MAX(definition->firing_position.combat_position_time_upper_bound, lower);

			/* (held there for its combat position time) */
			rt_deck.until[absolute] = now + (long)(real_random_range(lower, upper) * TICKS_PER_SECOND);
			rt_deck.arrived[absolute / 32] |= 1UL << (absolute % 32);
			rt_deck.arrivals++;
			if (HALO_ENV_ONCE("HALO_H2_DECK_LOG"))
				platform_log("h2 ai: deck: %lx at %s's position %d", (unsigned long)actor_index, encounter->name, (int)rt_deck.links[goal].position);
		}
		actor->output.throttle.i = actor->output.throttle.j = actor->output.throttle.k = 0.0f;
		return TRUE;
	}
	to.i /= distance, to.j /= distance;
	/* (a walker's throttle is its own: forward, left) */
	left.i = -actor->input.facing_vector.j, left.j = actor->input.facing_vector.i, left.k = 0.0f;
	actor->output.throttle.i = to.i * actor->input.facing_vector.i + to.j * actor->input.facing_vector.j;
	actor->output.throttle.j = to.i * left.i + to.j * left.j;
	actor->output.throttle.k = 0.0f;
	/* (a unit direction's dot with a facing not quite unit can round past 1:
	actors.c's throttle check halted 01b FAST at 11:22 once) */
	actor->output.throttle.i = MAX(-1.0f, MIN(1.0f, actor->output.throttle.i));
	actor->output.throttle.j = MAX(-1.0f, MIN(1.0f, actor->output.throttle.j));
	if (HALO_ENV_ONCE("HALO_H2_DECK_LOG") && atoi(HALO_ENV_ONCE("HALO_H2_DECK_LOG")) >= 2 && now % 15 == 0)
		platform_log("h2 ai: deck: %lx %.2f wu from position %d, throttle %.2f %.2f, speed %.2f wu/s, action %d", (unsigned long)actor_index,
			distance, (int)rt_deck.links[goal].position, actor->output.throttle.i, actor->output.throttle.j,
			magnitude3d(&unit->object.translational_velocity) * (real)TICKS_PER_SECOND, (int)actor->state.action);
	return TRUE;
}

/* ---------- Halo 2's vehicle charge (ram) */

/* A driver whose character's vehicle properties for its vehicle give a
vehicle ram timeout - the generic character's Ghost (3 s), Wraith (0.9 s)
and Creep (3 s) - rams its target as Halo 2's vehicle_charge and
vehicle_ram_behavior do. Halo 2's behaviour code is not in the
decompilation; this follows what the H2EK's tag test shows (h2ekref
behaviour/ghost_ramming.md, an Elite's Ghost against a mortal player on
Zanzibar's beach) and the character's fields (the vehicle properties block,
0xE4, 0xB4 each: ai throttle maximum 0x50, ai ideal turning radius 0x28, ai
charge repeat timeout 0x7C, ai charge look-ahead time 0x80, ai charge
consider distance 0x84, ai charge abort distance 0x88, vehicle ram timeout
0x8C; the element whose unit is the vehicle, the character's own block
first, then its parents'):
- it charges a target it fights, within its consider distance (0: any
distance it fights at - the Ghost charged from 18 wu), no target that cannot
be hurt (with a deathless player the Ghost parked at 1.8 wu: this engine's
fight goes on);
- it steers at where the target will be after its look-ahead time (or
sooner, as far as it is at its speed) at its throttle maximum (the Ghost
closed at ~3 wu/s radial, 0.9 throttle);
- past the target (nearest under its ideal turning radius and drawing
away, or touching) it runs straight on at full throttle for its ram timeout
(the Ghost ran out to 15-20 wu), then turns back and charges again (its
vehicle's own turning: a pass every 8-13 s);
- farther than its abort distance (when it has one), pressing on a second
without moving (against a wall, a rock), or with the level between it and
the target (Halo 2's charge drives its path; this one drives straight), it
gives up, for its charge repeat timeout (at least a second): this engine's
own driving takes it on.
HALO_H2_RAM_LOG=1 logs each phase. */

enum
{
	RT_RAM_PROFILES = 32,
	RT_RAM_NONE = 0,
	RT_RAM_APPROACH,
	RT_RAM_RUNOUT,
	RT_RAM_RESTING
};

struct rt_ram_profile
{
	long variant;               /* the actor variant tag and the vehicle tag it is of */
	long vehicle;
	boolean rams;
	real throttle, ideal_turning_radius, repeat_timeout, look_ahead, consider, abort, ram_timeout;
};

static struct
{
	struct rt_ram_profile profiles[RT_RAM_PROFILES];
	short profile_count;
	unsigned char phase[RT_MAXIMUM_ACTORS];
	long phase_time[RT_MAXIMUM_ACTORS];
	long target[RT_MAXIMUM_ACTORS];
	real nearest[RT_MAXIMUM_ACTORS];
	short stuck[RT_MAXIMUM_ACTORS];   /* ticks pressing on without moving */
	real_vector3d heading[RT_MAXIMUM_ACTORS];
	long passes, charges;
} rt_ram;

static int ram_log(void)
{
	static int level = -1;

	if (level < 0)
		level = HALO_ENV_ONCE("HALO_H2_RAM_LOG") ? atoi(HALO_ENV_ONCE("HALO_H2_RAM_LOG")) : 0;
	return level;
}

H2_ALIGN static void ram_reset(void)
{
	memset(&rt_ram, 0, sizeof(rt_ram));
}

/* the last part of a tag's name ("objects\vehicles\ghost\ghost" -> "ghost") */
static char const *ram_last_part(char const *name)
{
	char const *slash = strrchr(name, '\\');

	return slash ? slash + 1 : name;
}

/* the ram profile of an actor variant (its Halo 2 character, named by
h2_ai_tag_describe) in a vehicle */
H2_ALIGN static struct rt_ram_profile const *ram_profile(long variant, long vehicle_definition)
{
	struct h2_cache *cache = h2_scripts_cache();
	struct rt_ram_profile *p;
	char character[96];
	char const *name, *vehicle_name;
	long tag = NONE, i, guard;
	short k;

	for (k = 0; k < rt_ram.profile_count; k++)
	{
		if (rt_ram.profiles[k].variant == variant && rt_ram.profiles[k].vehicle == vehicle_definition)
			return &rt_ram.profiles[k];
	}
	if (rt_ram.profile_count >= RT_RAM_PROFILES)
		return NULL;
	p = &rt_ram.profiles[rt_ram.profile_count++];
	memset(p, 0, sizeof(*p));
	p->variant = variant;
	p->vehicle = vehicle_definition;
	name = tag_get_name(variant);
	vehicle_name = ram_last_part(tag_get_name(vehicle_definition));
	if (!cache || strncmp(name, "h2\\ai\\", 6))
		return p;
	snprintf(character, sizeof(character), "%s", name + 6);
	if (strchr(character, ' '))
		*strchr(character, ' ') = '\0';
	for (i = 0; i < h2_cache_tag_count(cache) && tag == NONE; i++)
	{
		struct h2_cache_tag t;
		char const *n;
		size_t a, b;

		if (!h2_cache_tag_get(cache, i, &t) || t.group_tag != 'char')
			continue;
		n = h2_cache_tag_name(cache, i);
		a = strlen(n), b = strlen(character);
		if (a > b && n[a - b - 1] == '\\' && !strcmp(n + a - b, character))
			tag = i;
	}
	/* (the character's own vehicle properties first, then its parents') */
	for (guard = 0; tag != NONE && guard < MAXIMUM_INHERITANCE; guard++)
	{
		struct h2_cache_tag t;
		long count;
		unsigned long elements;

		if (!h2_cache_tag_get(cache, tag, &t) || t.group_tag != 'char')
			break;
		if (h2_cache_block(cache, t.address + 0xE4, 0xB4, &count, &elements) && count > 0)
		{
			for (i = 0; i < count; i++)
			{
				unsigned long e = elements + 0xB4 * (unsigned long)i;
				long unit = h2_cache_reference(cache, e);

				if (unit == NONE || strcmp(ram_last_part(h2_cache_tag_name(cache, unit)), vehicle_name))
					continue;
				p->throttle = readf_or(cache, e + 0x50, 1.0f);
				p->ideal_turning_radius = readf_or(cache, e + 0x28, 4.5f);
				p->repeat_timeout = readf_or(cache, e + 0x7C, 0.0f);
				p->look_ahead = readf_or(cache, e + 0x80, 0.0f);
				p->consider = readf_or(cache, e + 0x84, 0.0f);
				p->abort = readf_or(cache, e + 0x88, 0.0f);
				p->ram_timeout = readf_or(cache, e + 0x8C, 0.0f);
				p->rams = p->ram_timeout > 0.0f;
				if (p->throttle <= 0.0f)
					p->throttle = 1.0f;
				platform_log("h2 ai: ram: %s in %s: %s (%s's vehicle properties: throttle %.2f, ideal turning radius %.2f, look-ahead %.2f s, "
					"consider %.1f, abort %.1f, repeat %.2f s, ram timeout %.2f s)", name, tag_get_name(vehicle_definition),
					p->rams ? "rams" : "does not ram", h2_cache_tag_name(cache, tag), p->throttle, p->ideal_turning_radius, p->look_ahead,
					p->consider, p->abort, p->repeat_timeout, p->ram_timeout);
				return p;
			}
		}
		tag = h2_cache_reference(cache, t.address + H2C_PARENT);
	}
	return p;
}

/* whether the target can be hurt: Halo 2's Ghost does not charge a
deathless player (h2ekref ghost_ramming.md) */
static boolean ram_target_mortal(long unit_index)
{
	struct object_datum const *object = object_try_and_get(unit_index);

	if (!object || TEST_FLAG(object->object.damage_flags, _object_dead_bit) ||
		TEST_FLAG(object->object.damage_flags, _object_cannot_take_damage_bit))
	{
		return FALSE;
	}
	if (cheat.deathless_player && object->object.type == _object_type_biped && unit_get(unit_index)->unit.player_index != NONE)
		return FALSE;
	return TRUE;
}

/* whether the level stands between a driver's vehicle and its target (a
ray a little over the ground, the structure and scenery) */
static boolean ram_line_blocked(real_point3d const *from, real_point3d const *to)
{
	struct collision_result collision;
	real_point3d start = *from;
	real_vector3d delta;

	start.z += 0.6f;
	delta.i = to->x - from->x, delta.j = to->y - from->y, delta.k = to->z + 0.6f - start.z;
	return collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
		FLAG(_collision_test_objects_scenery_bit) | FLAG(_collision_test_objects_machines_bit), &start, &delta, NONE, &collision);
}

/* (h2_ai_actor_output) a driver this engine's AI drives: the charge, when
it rams; TRUE: its output set */
H2_ALIGN static boolean ram_drive(long actor_index, struct actor_datum *actor, struct object_datum *vehicle, long vehicle_index,
	struct rt_command_script const *c)
{
	long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index);
	struct rt_ram_profile const *p;
	struct prop_datum *prop;
	long target_unit, now = game_time_get();
	real_point3d origin, at;
	real_vector3d to, target_velocity, steer, aim;
	real distance, speed, ahead, throttle;
	unsigned char phase;

	if (absolute < 0 || absolute >= RT_MAXIMUM_ACTORS || actor->meta.variant_definition_index == NONE)
		return FALSE;
	phase = rt_ram.phase[absolute];
	p = ram_profile(actor->meta.variant_definition_index, vehicle->definition_index);
	if (ram_log() >= 3 && now % 30 == 0)
	{
		struct prop_datum *t = actor->target.target_prop_index != NONE ? prop_try_and_get(actor->target.target_prop_index) : NULL;

		platform_log("h2 ai: ram: %lx rams %d script %d mode %d target prop %lx (unit %lx, enemy %d, mortal %d)", (unsigned long)actor_index,
			p ? (int)p->rams : -1, c != NULL, (int)actor->state.mode, (unsigned long)actor->target.target_prop_index,
			t ? (unsigned long)t->unit_index : 0UL, t ? (int)t->enemy : -1, t ? (int)ram_target_mortal(t->unit_index) : -1);
		if (t && object_try_and_get(t->unit_index))
			platform_log("h2 ai: ram:   target %s type %d damage flags %lx player %ld deathless %d", tag_get_name(object_get(t->unit_index)->definition_index),
				(int)object_get(t->unit_index)->object.type, (unsigned long)object_get(t->unit_index)->object.damage_flags,
				object_get(t->unit_index)->object.type <= _object_type_vehicle ? (long)unit_get(t->unit_index)->unit.player_index : -2L, (int)cheat.deathless_player);
	}
	if (!p || !p->rams || c || actor->state.mode < 3 /* combat */ || actor->target.target_prop_index == NONE ||
		!(prop = prop_try_and_get(actor->target.target_prop_index)) || !prop->enemy || !ram_target_mortal(prop->unit_index))
	{
		if (phase != RT_RAM_NONE && phase != RT_RAM_RESTING)
			rt_ram.phase[absolute] = RT_RAM_NONE;
		if (phase == RT_RAM_RESTING && now - rt_ram.phase_time[absolute] > (long)(p ? p->repeat_timeout * TICKS_PER_SECOND : 0))
			rt_ram.phase[absolute] = RT_RAM_NONE;
		return FALSE;
	}
	target_unit = prop->unit_index;
	/* (a target in a vehicle: the vehicle) */
	if (object_get(target_unit)->object.parent_object_index != NONE && object_try_and_get(object_get(target_unit)->object.parent_object_index))
		target_unit = object_get(target_unit)->object.parent_object_index;
	object_get_origin(vehicle_index, &origin);
	object_get_origin(target_unit, &at);
	to.i = at.x - origin.x, to.j = at.y - origin.y, to.k = 0.0f;
	distance = (real)sqrt(to.i * to.i + to.j * to.j);
	speed = (real)sqrt(vehicle->object.translational_velocity.i * vehicle->object.translational_velocity.i +
		vehicle->object.translational_velocity.j * vehicle->object.translational_velocity.j) * (real)TICKS_PER_SECOND;
	target_velocity = object_get(target_unit)->object.translational_velocity;
	if (rt_ram.target[absolute] != target_unit && phase != RT_RAM_RESTING)
		phase = RT_RAM_NONE;
	switch (phase)
	{
	case RT_RAM_RESTING:
		if (now - rt_ram.phase_time[absolute] <= (long)(MAX(p->repeat_timeout, 1.0f) * TICKS_PER_SECOND))
			return FALSE;
		phase = RT_RAM_NONE;
		/* (fall through) */
	case RT_RAM_NONE:
		if ((p->consider > 0.0f && distance > p->consider) || ram_line_blocked(&origin, &at))
		{
			if (ram_log() >= 3 && now % 30 == 0)
				platform_log("h2 ai: ram: %lx not charging %lx at %.1f wu: %s", (unsigned long)actor_index, (unsigned long)target_unit,
					(double)distance, p->consider > 0.0f && distance > p->consider ? "beyond its consider distance" : "the level in the way");
			return FALSE;
		}
		phase = RT_RAM_APPROACH;
		rt_ram.phase_time[absolute] = now;
		rt_ram.nearest[absolute] = distance;
		rt_ram.stuck[absolute] = 0;
		rt_ram.target[absolute] = target_unit;
		rt_ram.charges++;
		if (ram_log())
			platform_log("h2 ai: ram: %lx charges %lx from %.1f wu", (unsigned long)actor_index, (unsigned long)target_unit, (double)distance);
		break;
	case RT_RAM_APPROACH:
		if (p->abort > 0.0f && distance > p->abort)
		{
			rt_ram.phase[absolute] = RT_RAM_RESTING;
			rt_ram.phase_time[absolute] = now;
			if (ram_log())
				platform_log("h2 ai: ram: %lx gives up at %.1f wu", (unsigned long)actor_index, (double)distance);
			return FALSE;
		}
		if (distance < rt_ram.nearest[absolute])
			rt_ram.nearest[absolute] = distance;
		rt_ram.stuck[absolute] = speed < 0.5f && distance > 1.5f ? rt_ram.stuck[absolute] + 1 : 0;
		if (rt_ram.stuck[absolute] > TICKS_PER_SECOND || ((now + absolute) % 10 == 0 && ram_line_blocked(&origin, &at)))
		{
			rt_ram.stuck[absolute] = 0;
			rt_ram.phase[absolute] = RT_RAM_RESTING;
			rt_ram.phase_time[absolute] = now;
			if (ram_log())
				platform_log("h2 ai: ram: %lx gives up, held at %.1f wu", (unsigned long)actor_index, (double)distance);
			return FALSE;
		}
		/* (past it: nearest within its ideal turning radius and drawing
		away, or touching) */
		if (distance < 1.0f || (rt_ram.nearest[absolute] < p->ideal_turning_radius && distance > rt_ram.nearest[absolute] + 0.5f))
		{
			phase = RT_RAM_RUNOUT;
			rt_ram.phase_time[absolute] = now;
			rt_ram.heading[absolute] = vehicle->object.forward;
			rt_ram.heading[absolute].k = 0.0f;
			rt_ram.passes++;
			if (ram_log())
				platform_log("h2 ai: ram: %lx passes %lx at %.2f wu (nearest %.2f), %.1f wu/s", (unsigned long)actor_index,
					(unsigned long)target_unit, (double)distance, (double)rt_ram.nearest[absolute], (double)speed);
		}
		break;
	case RT_RAM_RUNOUT:
		if (now - rt_ram.phase_time[absolute] >= (long)(p->ram_timeout * TICKS_PER_SECOND + 0.5f))
		{
			phase = RT_RAM_APPROACH;
			rt_ram.phase_time[absolute] = now;
			rt_ram.nearest[absolute] = distance;
			if (ram_log())
				platform_log("h2 ai: ram: %lx turns back at %.1f wu", (unsigned long)actor_index, (double)distance);
		}
		break;
	}
	rt_ram.phase[absolute] = phase;
	if (distance > 0.01f)
		aim.i = to.i / distance, aim.j = to.j / distance, aim.k = 0.0f;
	else
		aim = vehicle->object.forward;
	if (phase == RT_RAM_RUNOUT)
	{
		real length = (real)sqrt(rt_ram.heading[absolute].i * rt_ram.heading[absolute].i + rt_ram.heading[absolute].j * rt_ram.heading[absolute].j);

		steer = rt_ram.heading[absolute];
		if (length > 0.01f)
			steer.i /= length, steer.j /= length;
		throttle = 1.0f;
		aim = steer;
	}
	else
	{
		/* (where it will be: its look-ahead time on, or the time to get
		there at this speed when that is sooner) */
		ahead = speed > 1.0f ? MIN(p->look_ahead, distance / speed) : 0.0f;
		steer.i = at.x + target_velocity.i * (real)TICKS_PER_SECOND * ahead - origin.x;
		steer.j = at.y + target_velocity.j * (real)TICKS_PER_SECOND * ahead - origin.y;
		steer.k = 0.0f;
		{
			real length = (real)sqrt(steer.i * steer.i + steer.j * steer.j);

			if (length > 0.01f)
				steer.i /= length, steer.j /= length;
			else
				steer = aim;
		}
		throttle = p->throttle;
	}
	actor->output.throttle.i = throttle;
	actor->output.throttle.j = actor->output.throttle.k = 0.0f;
	actor->output.facing_vector = steer;
	actor->output.aiming_vector = aim;
	actor->output.looking_vector = aim;
	if (ram_log() >= 2 && now % 3 == 0)
		platform_log("h2 ai: ram: %lx phase %d distance %.2f speed %.2f", (unsigned long)actor_index, (int)phase, (double)distance, (double)speed);
	return TRUE;
}

/* (actors.c, after the actor's own moves) a driver of a ground vehicle this
engine's AI does not drive (its tag no AI driver: the Warthog) driven where
the actor is going - its path's next point (this engine paths it with its
vehicle's radius, it only gives it no throttle), else a command script's
cs_go_to / cs_go_by point straight on: steered at it (the driver's facing,
aiming and looking, which its vehicle turns to) at full throttle, less as
it nears a point it stops at or faces away from it; a driver going nowhere
lets it roll to a stop. The driver of a dropship's cargo stays still */
H2_ALIGN void h2_ai_actor_output(
	long actor_index)
{
	struct rt_command_script *c;
	struct actor_datum *actor;
	struct object_datum *unit, *vehicle;
	real_point3d origin, target;
	real_vector3d to;
	real distance, alignment, throttle;
	boolean stops = TRUE;

	if (!rt.loaded)
		return;
	actor = actor_get(actor_index);
	/* (a flier with a goal - h2_ai_actor_orders -: steered at it, as a dropship is:
	this engine's paths need a surface a flier has not) */
	if (actor->state.flying && actor->meta.unit_index != NONE && (unit = object_try_and_get(actor->meta.unit_index)) != NULL &&
		unit->object.parent_object_index == NONE)
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index) % RT_MAXIMUM_ACTORS;

		if (rt.flier_goal_times[absolute] > 0 && game_time_get() - rt.flier_goal_times[absolute] < 90 && !command_script_of_actor(actor_index))
		{
			real_point3d at;
			real_vector3d way;
			real length;

			object_get_origin(actor->meta.unit_index, &at);
			way.i = rt.flier_goals[absolute].x - at.x, way.j = rt.flier_goals[absolute].y - at.y, way.k = rt.flier_goals[absolute].z - at.z;
			length = (real)sqrt(way.i * way.i + way.j * way.j + way.k * way.k);
			if (length > 0.5f)
			{
				/* (a throttle is the unit's own: forward, left, up) */
				real_vector3d const *f = &actor->input.facing_vector;

				/* (something ahead: round it, up, then to either side, a third of a
				second at a time) */
				if (game_time_get() - rt.flier_turn_times[absolute] < 10)
					way = rt.flier_turns[absolute], length = 1.0f;
				else if (length > 1.0f)
				{
					real_vector3d ahead = way;
					real look = 2.0f / length;

					ahead.i *= look, ahead.j *= look, ahead.k *= look;
					if (length > 2.0f && flier_line_blocked(&at, &ahead, actor->meta.unit_index))
					{
						real h = (real)sqrt(way.i * way.i + way.j * way.j);
						real_vector3d turns[4];
						short t;

						if (h < 0.01f)
							h = 0.01f;
						set_real_vector3d(&turns[0], 0.0f, 0.0f, 1.0f);
						set_real_vector3d(&turns[1], -way.j / h, way.i / h, 0.0f);
						set_real_vector3d(&turns[2], way.j / h, -way.i / h, 0.0f);
						set_real_vector3d(&turns[3], 0.0f, 0.0f, -1.0f);
						for (t = 0; t < 4; t++)
						{
							real_vector3d step = turns[t];

							step.i *= 2.0f, step.j *= 2.0f, step.k *= 2.0f;
							if (!flier_line_blocked(&at, &step, actor->meta.unit_index))
							{
								way = turns[t], length = 1.0f;
								rt.flier_turns[absolute] = way;
								rt.flier_turn_times[absolute] = game_time_get();
								rt.flier_turns_made++;
								break;
							}
						}
					}
				}

				actor->output.throttle.i = 0.99f * (way.i * f->i + way.j * f->j) / length;
				actor->output.throttle.j = 0.99f * (way.j * f->i - way.i * f->j) / length;
				actor->output.throttle.k = 0.99f * way.k / length;
			}
		}
	}
	/* (on foot, pressing along its path and not moving for a second - a kerb this
	engine's paths cross but its biped does not step up, 03a's Elites out of their
	pods stood at the fort's slab for good -: a jump. Halo 2 crosses these by its
	sector links' jump and hoist hints, which this engine's paths do not have) */
	/* (on foot, on a machine's deck: deck_walk) */
	if (actor->meta.unit_index != NONE && (unit = object_try_and_get(actor->meta.unit_index)) != NULL &&
		unit->object.parent_object_index == NONE && unit->object.type == _object_type_biped && !command_script_of_actor(actor_index) &&
		deck_walk(actor_index, actor, unit))
	{
		return;
	}
	if (actor->meta.unit_index != NONE && (unit = object_try_and_get(actor->meta.unit_index)) != NULL &&
		unit->object.parent_object_index == NONE && unit->object.type == _object_type_biped &&
		DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index) < RT_MAXIMUM_ACTORS)
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index);
		real_vector3d const *v = &unit->object.translational_velocity;
		if (HALO_ENV_ONCE("HALO_H2_AI_TRACE") && game_time_get() % 30 == 0)
			platform_log("h2 ai: foot %x moving %d status %d target %x next %ld until %ld enc %x squad %d action %d", (unsigned)actor_index, (int)actor->control.moving, (int)actor->state.combat_status,
				(unsigned)actor->target.target_prop_index, rt.foot_advance_next[absolute], rt.foot_route_until[absolute], (unsigned)actor->meta.encounter_index, (int)actor->meta.squad_index, (int)actor->state.action);
		if (HALO_ENV_ONCE("HALO_H2_AI_TRACE") && game_time_get() % 30 == 0 && rt.routes[absolute])
			platform_log("h2 ai: foot route %x free %d index %d of %d target %.1f %.1f %.1f at %.1f %.1f %.1f vel %.3f throttle %.2f %.2f", (unsigned)actor_index, (int)rt.foot_route_free[absolute], (int)rt.routes[absolute]->index,
				(int)rt.routes[absolute]->count, rt.routes[absolute]->target.x, rt.routes[absolute]->target.y, rt.routes[absolute]->target.z, unit->object.position.x, unit->object.position.y, unit->object.position.z,
				(real)sqrt(v->i * v->i + v->j * v->j), actor->output.throttle.i, actor->output.throttle.j);

		/* (following Halo 2's own sectors round what stopped it - below) */
		/* (a command script's move this engine does not make - an always-charging
		Hunter's, charge holds it, 03a's stood out the 30 seconds of its cs_go_to -:
		over the mesh) */
		{
			struct rt_command_script *move = command_script_of_actor(actor_index);

			if (move && (move->flags & CS_MOVE) && !actor->control.moving && actor->state.action == _actor_action_charge &&
				rt.foot_route_until[absolute] <= game_time_get() && global_structure_bsp_index >= 0 && global_structure_bsp_index < RT_MAXIMUM_FRAME_BSPS &&
				rt.nav[global_structure_bsp_index])
			{
				struct rt_nav *n = rt.nav[global_structure_bsp_index];

				if (!rt.routes[absolute])
					rt.routes[absolute] = calloc(1, sizeof(struct rt_route));
				if (rt.routes[absolute])
				{
					struct rt_route *r = rt.routes[absolute];

					r->target = move->move_point;
					foot_route_search(actor_index, n, r, &unit->object.position);
					if (r->count >= 1)
					{
						rt.foot_route_until[absolute] = game_time_get() + 60;
						rt.foot_route_free[absolute] = TRUE;
					}
				}
			}
		}
		/* (guarding, away from every place its order gives it - an Elite out of its
		insertion pod, 03a's stood by theirs, the real ones walk 6.6 wu to their
		area in 9 seconds -: to the nearest, over the mesh) */
		if (!actor->control.moving && actor->state.action == _actor_action_guard && actor->target.target_prop_index == NONE &&
			rt.foot_route_until[absolute] <= game_time_get() && game_time_get() >= rt.foot_advance_next[absolute] &&
			!command_script_of_actor(actor_index) && actor->meta.encounter_index != NONE && actor->meta.squad_index != NONE &&
			global_structure_bsp_index >= 0 && global_structure_bsp_index < RT_MAXIMUM_FRAME_BSPS && rt.nav[global_structure_bsp_index] &&
			!HALO_ENV_ONCE("HALO_H2_AI_NO_FOOT_ROUTES"))
		{
			struct encounter_definition *encounter = TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->ai_encounters,
				DATUM_INDEX_TO_ABSOLUTE_INDEX(actor->meta.encounter_index), struct encounter_definition);
			struct squad_definition const *squad_of = TAG_BLOCK_GET_ELEMENT(&encounter->squads, actor->meta.squad_index, struct squad_definition);
			real best = 1e30f;
			long f, chosen = NONE;

			rt.foot_advance_next[absolute] = game_time_get() + 90;
			for (f = 0; f < encounter->firing_positions.count; f++)
			{
				struct firing_position_definition const *fp = TAG_BLOCK_GET_ELEMENT(&encounter->firing_positions, f, struct firing_position_definition);
				real d;

				/* (a follow order's: only those of its areas active now, as the
				positions it may take - h2_ai_firing_position_allowed; any other
				led 05a's LZ Marines on allies_lz_all to a place they could not
				leave while the real ones followed the player) */
				if (!TEST_FLAG(squad_of->firing_position_groups[0], fp->group_index) || !h2_ai_firing_position_allowed(actor_index, fp))
					continue;
				d = distance_squared(&fp->position, &unit->object.position);
				if (d < best)
					best = d, chosen = f;
			}
			if (chosen != NONE && best > 3.0f * 3.0f)
			{
				struct rt_nav *n = rt.nav[global_structure_bsp_index];

				if (!rt.routes[absolute])
					rt.routes[absolute] = calloc(1, sizeof(struct rt_route));
				if (rt.routes[absolute])
				{
					struct rt_route *r = rt.routes[absolute];

					r->target = TAG_BLOCK_GET_ELEMENT(&encounter->firing_positions, chosen, struct firing_position_definition)->position;
					foot_route_search(actor_index, n, r, &unit->object.position);
					if (r->count >= 1)
					{
						rt.foot_route_until[absolute] = game_time_get() + 240;
						rt.foot_route_free[absolute] = TRUE;
						rt.foot_returns++;
					}
				}
			}
		}
		/* (it knows where its target is, cannot see it and this engine finds it no
		way - a ledge joined to the ground below only by Halo 2's jump links: 03a's first
		Elites stood on theirs, the real ones walk down to their order's nearest
		position: the sector mesh to its squad's position nearest the target) */
		if (rt.surprise_walking[absolute] && actor->control.moving)
			actor_move_halt(actor_index);
		if ((!actor->control.moving || rt.surprise_walking[absolute]) && actor->state.combat_status >= 4 && actor->target.target_prop_index != NONE &&
			rt.foot_route_until[absolute] <= game_time_get() && game_time_get() >= rt.foot_advance_next[absolute] &&
			(actor->state.action == _actor_action_fight || actor->state.action == _actor_action_uncover || actor->state.action == _actor_action_guard ||
				actor->state.action == _actor_action_search || actor->state.action == _actor_action_charge) &&
			!command_script_of_actor(actor_index) && actor->meta.encounter_index != NONE && actor->meta.squad_index != NONE &&
			!HALO_ENV_ONCE("HALO_H2_AI_NO_FOOT_ROUTES"))
		{
			struct prop_datum *prop = prop_get(actor->target.target_prop_index);
			struct rt_nav *n = global_structure_bsp_index >= 0 && global_structure_bsp_index < RT_MAXIMUM_FRAME_BSPS ?
				rt.nav[global_structure_bsp_index] : NULL;

			rt.foot_advance_next[absolute] = game_time_get() + 90;
			if (n && (actor->state.action == _actor_action_charge || rt.surprise_walking[absolute]))
			{
				/* (a charge goes at its target, and this engine's found it no way -
				a Hunter stood 8 wu off for good) */
				if (!rt.routes[absolute])
					rt.routes[absolute] = calloc(1, sizeof(struct rt_route));
				if (rt.routes[absolute] && distance_squared(&unit->object.position, &prop->body_position) > 2.0f * 2.0f)
				{
					struct rt_route *r = rt.routes[absolute];

					r->target = prop->body_position;
					foot_route_search(actor_index, n, r, &unit->object.position);
					if (r->count >= 1)
					{
						rt.foot_route_until[absolute] = game_time_get() + 90;
						rt.foot_route_free[absolute] = TRUE;
						rt.foot_advances++;
					}
					else
					{
						/* (no way on the mesh - its sectors joined by links narrower than
						a search's width - and a clear line: straight on) */
						real_vector3d line;
						real_point3d eye = unit->object.position;

						eye.z += 0.5f;
						vector_from_points3d(&eye, &prop->body_position, &line);
						if (!flier_line_blocked(&eye, &line, actor->meta.unit_index))
						{
							r->points[0] = r->target;
							r->kinds[0] = 0;
							r->count = 1;
							r->index = 0;
							rt.foot_route_until[absolute] = game_time_get() + 45;
							rt.foot_route_free[absolute] = TRUE;
						}
						else if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
							platform_log("h2 ai: charge without a route: from sector %ld to %ld", nav_locate(n, &unit->object.position), nav_locate(n, &r->target));
					}
				}
			}
			else if (n && prop->line_of_sight != _ai_line_of_sight_clear && prop->line_of_sight != _ai_line_of_sight_occluded)
			{
				struct encounter_definition *encounter = TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->ai_encounters,
					DATUM_INDEX_TO_ABSOLUTE_INDEX(actor->meta.encounter_index), struct encounter_definition);
				struct squad_definition const *squad_of = TAG_BLOCK_GET_ELEMENT(&encounter->squads, actor->meta.squad_index, struct squad_definition);
				struct actor_variant_definition const *variant = actor_variant_definition_get(actor->meta.variant_definition_index);
				real lower = variant->ranged_combat.combat_range_lower_bound;
				real now = distance_squared(&unit->object.position, &prop->body_position);
				real best = now;
				long f, chosen = NONE;

				for (f = 0; f < encounter->firing_positions.count; f++)
				{
					struct firing_position_definition const *fp = TAG_BLOCK_GET_ELEMENT(&encounter->firing_positions, f, struct firing_position_definition);
					real d;

					if (!TEST_FLAG(squad_of->firing_position_groups[0], fp->group_index) || !h2_ai_firing_position_allowed(actor_index, fp))
						continue;
					d = distance_squared(&fp->position, &prop->body_position);
					if (d < best - 1.0f && d >= lower * lower)
						best = d, chosen = f;
				}
				if (chosen != NONE)
				{
					if (!rt.routes[absolute])
						rt.routes[absolute] = calloc(1, sizeof(struct rt_route));
					if (rt.routes[absolute])
					{
						struct rt_route *r = rt.routes[absolute];

						r->target = TAG_BLOCK_GET_ELEMENT(&encounter->firing_positions, chosen, struct firing_position_definition)->position;
						foot_route_search(actor_index, n, r, &unit->object.position);
						if (r->count > 1)
						{
							rt.foot_route_until[absolute] = game_time_get() + 240;
							rt.foot_route_free[absolute] = TRUE;
							rt.foot_advances++;
						}
					}
				}
			}
		}
		if ((actor->control.moving || rt.foot_route_free[absolute]) && rt.foot_route_until[absolute] > game_time_get() && rt.routes[absolute] && rt.routes[absolute]->count > 0)
		{
			struct rt_route *r = rt.routes[absolute];
			real_point3d at = unit->object.position;

			while (r->index < r->count - 1)
			{
				real dx = r->points[r->index].x - at.x, dy = r->points[r->index].y - at.y;

				if (dx * dx + dy * dy > 0.5f * 0.5f)
					break;
				/* (at a jump's top edge: off it. At a hoist's foot: up, Halo 2 plays
				its hoist animation here - this does not yet) */
				if (r->kinds[r->index] == RT_HOP_JUMP)
				{
					actor_unit_control_jump(actor_index);
					rt.foot_hops++;
				}
				else if (r->kinds[r->index] == RT_HOP_HOIST)
				{
					real_point3d on = r->points[r->index + 1];

					on.z += 0.02f;
					object_set_position(actor->meta.unit_index, &on, NULL, NULL);
					at = on;
					rt.foot_hoists++;
				}
				r->index++;
			}
			{
				real dx = r->points[r->index].x - at.x, dy = r->points[r->index].y - at.y;
				real length = (real)sqrt(dx * dx + dy * dy);

				if (length > 0.25f)
				{
					/* (a throttle is the unit's own: forward, left) */
					real_vector3d const *f = &actor->input.facing_vector;

					actor->output.throttle.i = 0.99f * (dx * f->i + dy * f->j) / length;
					actor->output.throttle.j = 0.99f * (dy * f->i - dx * f->j) / length;
					actor->output.throttle.k = 0.0f;
				}
				else
					rt.foot_route_until[absolute] = 0;
			}
			if (rt.foot_route_until[absolute] <= game_time_get() + 1)
				rt.foot_route_free[absolute] = FALSE;
		}
		else if (actor->control.moving && actor->control.path.path.valid && v->i * v->i + v->j * v->j < 0.0004f &&
			(actor->output.throttle.i != 0.0f || actor->output.throttle.j != 0.0f))
		{
			if (++rt.foot_stuck[absolute] >= 30)
			{
				struct rt_nav *n = global_structure_bsp_index >= 0 && global_structure_bsp_index < RT_MAXIMUM_FRAME_BSPS ?
					rt.nav[global_structure_bsp_index] : NULL;

				/* (Halo 2's paths run on its sectors, which go round its instanced
				geometry; this engine's run through it: pressing against one, three
				seconds along Halo 2's route to where it is going) */
				if (HALO_ENV_ONCE("HALO_H2_AI_NO_FOOT_ROUTES"))
					n = NULL;
				/* (a route that left it where the last one did - through something the
				mesh has too -: not again, the position goes) */
				if (distance_squared(&unit->object.position, &rt.foot_route_from[absolute]) < 1.0f && game_time_get() - rt.foot_route_started[absolute] < 300)
					n = NULL;
				if (n && !rt.routes[absolute])
					rt.routes[absolute] = calloc(1, sizeof(struct rt_route));
				if (n && rt.routes[absolute])
				{
					struct rt_route *r = rt.routes[absolute];

					r->target = actor->control.path.destination.point;
					foot_route_search(actor_index, n, r, &unit->object.position);
					if (r->count > 1)
					{
						rt.foot_route_until[absolute] = game_time_get() + 150;
						rt.foot_route_from[absolute] = unit->object.position;
						rt.foot_route_started[absolute] = game_time_get();
						rt.foot_stuck[absolute] = 0;
						rt.foot_routes++;
					}
					else
					{
						rt.foot_route_failures++;
						if (HALO_ENV_ONCE("HALO_H2_AI_STUCK"))
							platform_log("h2 ai: no foot route from %.1f %.1f %.1f (sector %ld) to %.1f %.1f %.1f (sector %ld), %d points", unit->object.position.x,
								unit->object.position.y, unit->object.position.z, nav_locate(n, &unit->object.position), r->target.x, r->target.y, r->target.z,
								nav_locate(n, &r->target), (int)r->count);
					}
				}
			}
			if (rt.foot_stuck[absolute] >= 30)
			{
				real_vector3d to, down = { 0.0f, 0.0f, -1.6f };
				real length;
				real_point3d from;
				struct collision_result collision;

				rt.foot_stuck[absolute] = 0;
				vector_from_points3d(&unit->object.position, &actor->control.moving_towards_point, &to);
				to.k = 0.0f;
				length = (real)sqrt(to.i * to.i + to.j * to.j);
				/* (a firing position close by that it cannot reach - inside or
				hard against an instance, which this engine's paths do not see:
				03a's Grunts walked on the spot before one, 90% of a run's
				time stuck -: given up for another. This engine's actions give a
				position up only once there; Halo 2's sectors never send it) */
				if (length < 3.0f && actor->firing_positions.current_position_index != NONE)
				{
					actor_discard_firing_position(actor_index, actor->firing_positions.current_position_index, FALSE);
					actor->firing_positions.current_position_index = NONE;
					if (actor->state.action == _actor_action_guard)
						actor->state.action_data.guard.find_new_guard_position = TRUE;
					actor_move_halt(actor_index);
					rt.positions_given_up++;
					length = 0.0f;
				}
				/* (a kerb ahead - the ground half a metre or so on stands above its feet:
				up on it, as Halo 2's hoist does; else a jump) */
				if (length > 0.01f)
				{
					from.x = unit->object.position.x + 0.7f * to.i / length;
					from.y = unit->object.position.y + 0.7f * to.j / length;
					from.z = unit->object.position.z + 1.3f;
					if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
						FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit), &from, &down, actor->meta.unit_index, &collision) &&
						collision.point.z > unit->object.position.z + 0.15f && collision.point.z < unit->object.position.z + 1.1f)
					{
						real_point3d on = collision.point;

						on.z += 0.02f;
						object_set_position(actor->meta.unit_index, &on, NULL, NULL);
						rt.foot_jumps++;
					}
					else
						actor_unit_control_jump(actor_index);
				}
			}
		}
		else
			rt.foot_stuck[absolute] = 0;
	}
	if (actor->meta.unit_index == NONE || !(unit = object_try_and_get(actor->meta.unit_index)) || unit->object.parent_object_index == NONE)
		return;
	/* (cs_vehicle_boost true: as a player holds its boost - h2_vehicle_boost.c) */
	c = command_script_of_actor(actor_index);
	if (c && (c->flags & CS_BOOST))
		actor->output.control_flags |= FLAG(_unit_control_crouch_modifier_bit);
	/* (a driver of a dropship's cargo: still until it is let go) */
	if (rt.cargo_count)
	{
		short q;

		for (q = 0; q < rt.cargo_count; q++)
		{
			if (rt.cargo[q].cargo == unit->object.parent_object_index)
			{
				actor->output.throttle.i = actor->output.throttle.j = actor->output.throttle.k = 0.0f;
				return;
			}
		}
	}
	/* (a dropship's pilot: its gun - Halo 2's Phantom's turret, h2_vehicles.c
	- at its target while the target is in sight within 60 wu, as Halo 2's
	Phantoms cover their drops; a command script's cs_shoot false holds it) */
	if (vehicle_scripted_flier(unit->object.parent_object_index) &&
		unit_get(unit->object.parent_object_index)->unit.driver_object_index == actor->meta.unit_index)
	{
		struct prop_datum *prop = actor->target.target_prop_index != NONE ?
			(struct prop_datum *)datum_try_and_get(prop_data, actor->target.target_prop_index) : NULL;

		c = command_script_of_actor(actor_index);
		if (prop && prop->unit_index != NONE && object_try_and_get(prop->unit_index) && !(c && (c->flags & CS_NO_SHOOT)) &&
			!TEST_FLAG(object_get(prop->unit_index)->object.damage_flags, _object_dead_bit))
		{
			real_point3d from, at;
			real_vector3d aim;
			real length;

			object_get_origin(unit->object.parent_object_index, &from);
			object_get_origin(prop->unit_index, &at);
			at.z += 0.5f;
			aim.i = at.x - from.x, aim.j = at.y - from.y, aim.k = at.z - from.z;
			length = (real)sqrt(aim.i * aim.i + aim.j * aim.j + aim.k * aim.k);
			if (length > 1.0f && length < 60.0f)
			{
				struct collision_result collision;
				real_vector3d sight = aim;

				aim.i /= length, aim.j /= length, aim.k /= length;
				actor->output.aiming_vector = aim;
				actor->output.looking_vector = aim;
				/* (in sight: nothing of the structure between) */
				if (!collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
					&from, &sight, NONE, &collision))
				{
					actor_unit_control_primary_trigger(actor_index, TRUE, 1.0f);
					rt.flier_shots++;
				}
			}
		}
		return;
	}
	if (!(vehicle = object_try_and_get(unit->object.parent_object_index)) || vehicle->object.type != _object_type_vehicle ||
		vehicle_scripted_flier(unit->object.parent_object_index) ||
		unit_get(unit->object.parent_object_index)->unit.driver_object_index != actor->meta.unit_index)
	{
		return;
	}
	c = command_script_of_actor(actor_index);
	/* (a driver that rams - Halo 2's vehicle charge -: ram_drive) */
	if (actor->input.vehicle_driver_type != _actor_vehicle_driver_unknown &&
		ram_drive(actor_index, actor, vehicle, unit->object.parent_object_index, c))
	{
		return;
	}
	/* (a driver this engine's AI drives - a Ghost, a Shadow -: its own,
	unless a command script sends it somewhere it finds no path to for a
	second: then Halo 2's mesh, as for a Warthog - 03a e13's second Shadow
	sat at its start, every cs_go_to timing out) */
	if (actor->input.vehicle_driver_type != _actor_vehicle_driver_unknown)
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index);

		if (absolute >= RT_MAXIMUM_ACTORS || !c || !(c->flags & CS_MOVE) || actor->control.path.path.valid)
		{
			if (absolute < RT_MAXIMUM_ACTORS)
				rt.drive_pathless[absolute] = 0;
			return;
		}
		/* (a scene's driver ignoring obstacles waits for nothing: 03a's Warthogs
		sat 45 ticks of a shot) */
		if (++rt.drive_pathless[absolute] < 30 && !(c->flags & CS_IGNORE_OBSTACLES))
			return;
		if (rt.drive_pathless[absolute] == 30)
			rt.drives_taken_over++;
	}
	object_get_origin(unit->object.parent_object_index, &origin);
	if (actor->control.moving && actor->control.path.path.valid)
	{
		struct path_result const *path = &actor->control.path.path;
		short step = path->step_index;

		target = actor->control.moving_towards_point;
		/* (a step closer than its vehicle turns: the one after it) */
		while (step + 1 < path->step_count &&
			(target.x - origin.x) * (target.x - origin.x) + (target.y - origin.y) * (target.y - origin.y) < 2.0f * 2.0f)
		{
			target = path->steps[++step].point;
		}
		stops = path->steps_finish_path && step + 1 >= path->step_count;
	}
	else
	{
		real_point3d goal;
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index);
		struct rt_nav *n = global_structure_bsp_index >= 0 && global_structure_bsp_index < RT_MAXIMUM_FRAME_BSPS ?
			rt.nav[global_structure_bsp_index] : NULL;
		struct rt_route *r;

		/* (where it goes: its command script's point, else where its own
		orders send it - this engine finds no path for it) */
		if (c && (c->flags & CS_MOVE))
			goal = c->move_point;
		else if (actor->orders.move.destination.destination_type != 0 && !actor->control.path.at_destination &&
			actor->control.path.destination_orders.destination_type != 0)
		{
			goal = actor->control.path.destination.point;
		}
		else
			return;
		target = goal;
		/* (Halo 2's own mesh: a route over its sectors, kept until its goal
		moves, it is three seconds old or it is done) */
		if (n && absolute < RT_MAXIMUM_ACTORS)
		{
			if (!rt.routes[absolute])
				rt.routes[absolute] = calloc(1, sizeof(struct rt_route));
			r = rt.routes[absolute];
			if (r && ((r->count == 0 && (!r->failed || game_time_get() - r->time > 90)) || r->index >= r->count || game_time_get() - r->time > 90 ||
				(r->target.x - goal.x) * (r->target.x - goal.x) + (r->target.y - goal.y) * (r->target.y - goal.y) > 4.0f))
			{
				r->target = goal;
				r->time = game_time_get();
				r->index = 0;
				r->count = nav_path(n, &origin, &goal, 1.0f, r->points, RT_ROUTE_POINTS);
				/* (a route ending short of a goal it cannot reach: Halo 2 goes
				as near as it can) */
				r->failed = r->count == 0;
				if (r->failed && HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
					platform_log("h2 ai: no route on bsp %d's mesh from %.1f %.1f %.1f (sector %ld) to %.1f %.1f %.1f (sector %ld)",
						global_structure_bsp_index, origin.x, origin.y, origin.z, nav_locate(n, &origin), goal.x, goal.y, goal.z,
						nav_locate(n, &goal));
				if (r->failed)
					rt.nav_failures++;
				else
					rt.nav_paths++;
			}
			if (r && r->count > 0)
			{
				/* (a point reached - within 2.5 wu -, or passed: nearer the
				next than it is) */
				while (r->index < r->count - 1)
				{
					real_point3d const *a = &r->points[r->index], *e = &r->points[r->index + 1];
					real to_a = (a->x - origin.x) * (a->x - origin.x) + (a->y - origin.y) * (a->y - origin.y);
					real to_e = (e->x - origin.x) * (e->x - origin.x) + (e->y - origin.y) * (e->y - origin.y);
					real a_e = (e->x - a->x) * (e->x - a->x) + (e->y - a->y) * (e->y - a->y);

					if (to_a > 2.5f * 2.5f && to_e > a_e)
						break;
					r->index++;
				}
				target = r->points[r->index];
				if (r->index < r->count - 1)
					stops = FALSE;
			}
		}
	}
	if (c && (c->flags & CS_MOVE_BY))
		stops = FALSE;
	to.i = target.x - origin.x, to.j = target.y - origin.y, to.k = 0.0f;
	distance = (real)sqrt(to.i * to.i + to.j * to.j);
	if (distance < 0.01f)
		return;
	to.i /= distance, to.j /= distance;
	alignment = to.i * vehicle->object.forward.i + to.j * vehicle->object.forward.j;
	throttle = stops ? MIN(1.0f, 0.25f + distance / 4.0f) : 1.0f;
	throttle *= alignment > 0.0f ? 0.5f + 0.5f * alignment : 0.4f;
	/* (pressing on a second without moving - against a wall, a crate -:
	backing off a second and a half, steering the other way) */
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index);
		real_vector3d const *v = &vehicle->object.translational_velocity;
		real speed = (real)sqrt(v->i * v->i + v->j * v->j);

		/* (cs_ignore_obstacles: pressing on) */
		if (absolute < RT_MAXIMUM_ACTORS && !(c && (c->flags & CS_IGNORE_OBSTACLES)))
		{
			if (rt.drive_reverse[absolute] > 0)
			{
				rt.drive_reverse[absolute]--;
				to.i = 2.0f * vehicle->object.forward.i - to.i, to.j = 2.0f * vehicle->object.forward.j - to.j;
				distance = (real)sqrt(to.i * to.i + to.j * to.j);
				if (distance > 0.01f)
					to.i /= distance, to.j /= distance;
				else
					to.i = vehicle->object.forward.i, to.j = vehicle->object.forward.j;
				throttle = -0.7f;
			}
			else if (throttle > 0.3f && distance > 1.0f && speed < 0.01f)
			{
				if (++rt.drive_stuck[absolute] > 30)
				{
					rt.drive_stuck[absolute] = 0;
					rt.drive_reverse[absolute] = 45;
					rt.drive_unsticks++;
				}
			}
			else
				rt.drive_stuck[absolute] = 0;
		}
	}
	/* (cs_vehicle_speed: hs_doc, its vehicle's speed a fraction of its top: held
	there, not a smaller throttle - at 0.9 03a's Warthogs never left first gear,
	whose change up is past 0.9 of its range: 0.134 a tick against 0.23) */
	if (c && c->speed > 0.0f && c->speed < 1.0f)
	{
		real_vector3d const *v = &vehicle->object.translational_velocity;

		if ((real)sqrt(v->i * v->i + v->j * v->j) > c->speed * vehicle_definition_real(unit->object.parent_object_index, 0x2F8, 0.25f))
			throttle = 0.0f;
	}
	actor->output.throttle.i = MAX(-1.0f, MIN(1.0f, throttle));
	actor->output.throttle.j = actor->output.throttle.k = 0.0f;
	actor->output.facing_vector = to;
	actor->output.aiming_vector = to;
	actor->output.looking_vector = to;
	rt.drives++;
	if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG") && game_time_get() % 150 == 0)
		platform_log("h2 ai: driver %lx of %s at %.1f %.1f to %.1f %.1f (%s), throttle %.2f; %ld unstuck so far", actor_index,
			tag_get_name(vehicle->definition_index), origin.x, origin.y, target.x, target.y,
			actor->control.moving && actor->control.path.path.valid ? "its path" :
			rt.routes[DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index) % RT_MAXIMUM_ACTORS] &&
			rt.routes[DATUM_INDEX_TO_ABSOLUTE_INDEX(actor_index) % RT_MAXIMUM_ACTORS]->count ? "Halo 2's mesh" : "straight",
			actor->output.throttle.i, rt.drive_unsticks);
	if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG") && game_time_get() % 150 == 0 && actor->input.vehicle_driver_type != _actor_vehicle_driver_unknown)
		platform_log("h2 ai:   (a driver this engine drives, pathless: %ld taken over)", rt.drives_taken_over);
	if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG") && game_time_get() % 150 == 0)
		platform_log("h2 ai:   its pathfinding surface %ld, destination %d (surface %ld), path valid %d, moving %d", actor->input.pathfinding_surface_index,
			actor->orders.move.destination.destination_type, c ? c->move_surface : -2L, actor->control.path.path.valid, actor->control.moving);
}

/* (the test driver, main.c) the next point of Halo 2's walking mesh from one place
towards another, at least 1.5 wu on; FALSE: none */
H2_ALIGN boolean h2_ai_test_route(real_point3d const *from, real_point3d const *to, real_point3d *next)
{
	struct rt_nav *n = global_structure_bsp_index >= 0 && global_structure_bsp_index < RT_MAXIMUM_FRAME_BSPS ?
		rt.nav[global_structure_bsp_index] : NULL;
	real_point3d points[RT_ROUTE_POINTS];
	short count, k;

	if (!n || (count = nav_path(n, from, to, 1.0f, points, RT_ROUTE_POINTS)) <= 0)
		return FALSE;
	for (k = 0; k < count - 1; k++)
	{
		real dx = points[k].x - from->x, dy = points[k].y - from->y;

		if (dx * dx + dy * dy > 1.5f * 1.5f)
			break;
	}
	*next = points[k];
	return TRUE;
}

/* (test command "@actors"): every living actor - its squad, unit, place, what
carries it, its action and its command script: where a mission's waited-on
character really is */
void h2_ai_test_actors(void)
{
	struct data_iterator iterator;
	struct actor_datum *actor;

	if (!rt.loaded)
		return;
	if (local_player_get_player_index(0) != NONE && player_get(local_player_get_player_index(0))->unit_index != NONE &&
		object_try_and_get(player_get(local_player_get_player_index(0))->unit_index))
	{
		struct object_datum *player = object_get(player_get(local_player_get_player_index(0))->unit_index);
		real_point3d origin;

		object_get_origin(player_get(local_player_get_player_index(0))->unit_index, &origin);
		platform_log("h2 ai: player at %.2f %.2f %.2f shield %.2f body %.2f parent %lx", origin.x, origin.y, origin.z,
			player->object.shield_vitality, player->object.body_vitality, (unsigned long)player->object.parent_object_index);
	}
	data_iterator_new(&iterator, actor_data);
	while ((actor = (struct actor_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct rt_command_script const *c = command_script_of_actor(iterator.datum_index);
		struct object_datum *unit = actor->meta.unit_index != NONE ? object_try_and_get(actor->meta.unit_index) : NULL;
		real_point3d origin = { 0.0f, 0.0f, 0.0f };

		if (unit)
			object_get_origin(actor->meta.unit_index, &origin);
		platform_log("h2 ai: actor %lx %s swarm %d dead %d squad %s %s at %.2f %.2f %.2f parent %lx action %d status %d target %d path %d moving %d script %d flags %lx point %.2f %.2f %.2f variant %s vitality %.0f %.0f",
			(unsigned long)iterator.datum_index, actor_alive(iterator.datum_index) ? "alive" : "gone", (int)actor->meta.swarm,
			unit ? TEST_FLAG(unit->object.damage_flags, _object_dead_bit) != 0 : -1,
			actor_h2_squad(iterator.datum_index) != NONE ? rt.squads[actor_h2_squad(iterator.datum_index)].name : "-",
			unit ? tag_get_name(unit->definition_index) : "(no unit)", origin.x, origin.y, origin.z,
			unit ? (unsigned long)unit->object.parent_object_index : 0xffffffffUL, (int)actor->state.action,
			(int)actor_combat_status(iterator.datum_index), (int)actor->target.target_type,
			(int)actor->control.path.path.valid, (int)actor->control.moving, c ? c->script : -1, c ? c->flags : 0UL,
			c ? c->move_point.x : 0.0f, c ? c->move_point.y : 0.0f, c ? c->move_point.z : 0.0f,
			actor->meta.variant_definition_index != NONE ? tag_get_name(actor->meta.variant_definition_index) : "?",
			unit ? unit->object.maximum_body_vitality : 0.0f, unit ? unit->object.maximum_shield_vitality : 0.0f);
		if (unit && unit->object.type == _object_type_biped)
		{
			struct unit_datum *u = unit_get(actor->meta.unit_index);
			struct animation_graph *graph = u->object.animation.animation_graph_index != NONE ?
				animation_graph_definition_get(u->object.animation.animation_graph_index) : NULL;
			short index = u->object.animation.state.index;

			if (graph && index >= 0 && index < graph->animations.count)
			{
				struct animation const *a = TAG_BLOCK_GET_ELEMENT(&graph->animations, index, struct animation);
				real dx = 0.0f;
				short f;

				for (f = 0; a->frame_info_type > 0 && f < a->frame_count; f++)
				{
					float const *p = (float const *)animation_get_frame_info(a, f, a->frame_info_type == 1 ? 8 : a->frame_info_type == 2 ? 12 : 16);

					dx += p[0];
				}
				platform_log("h2 ai:   its %d frames, info type %d, root dx %.3f a loop, loop frame %d; body %.3f shield %.3f; velocity %.3f wu/s",
					(int)a->frame_count, (int)a->frame_info_type, dx, (int)a->private_loop_frame_index, unit->object.body_vitality, unit->object.shield_vitality,
					30.0f * (real)sqrt(unit->object.translational_velocity.i * unit->object.translational_velocity.i +
						unit->object.translational_velocity.j * unit->object.translational_velocity.j));
			}
			platform_log("h2 ai:   animation %d (%s) frame %d, seat %d class %d type %d base seat %d state %d",
				(int)index, graph && index >= 0 && index < graph->animations.count ?
				TAG_BLOCK_GET_ELEMENT(&graph->animations, index, struct animation)->name : "?",
				(int)u->object.animation.state.frame_index, (int)u->unit.animation.seat_index, (int)u->unit.animation.weapon_index, (int)u->unit.animation.weapon_type_index,
				(int)u->unit.animation.base_seat_index, (int)u->unit.animation.state);
		}
		if (unit && unit->object.type == _object_type_biped)
		{
			struct biped_datum *b = (struct biped_datum *)unit;

			if (actor->emotions.berserk)
				platform_log("h2 ai:   berserk, action %d", (int)actor->state.action);
			if (actor->state.flying && local_player_get_player_index(0) != NONE && player_get(local_player_get_player_index(0))->unit_index != NONE)
			{
				real_point3d to;
				real_vector3d way;
				struct collision_result result;
				long player_unit = player_get(local_player_get_player_index(0))->unit_index;

				object_get_origin(player_unit, &to);
				to.z += 0.5f;
				vector_from_points3d(&actor->input.position.head_position, &to, &way);
				if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_objects_bit) | FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_back_facing_surfaces_bit),
					&actor->input.position.head_position, &way, actor->meta.unit_index, &result))
				{
					platform_log("h2 ai:   line to the player from %.1f %.1f %.1f: hits at t %.2f type %d object %s", actor->input.position.head_position.x, actor->input.position.head_position.y,
						actor->input.position.head_position.z, result.t, (int)result.type, result.type == _collision_result_object && result.object_index != NONE ? tag_get_name(object_get(result.object_index)->definition_index) : "-");
				}
				else
					platform_log("h2 ai:   line to the player clear");
			}
			if (actor->state.action == _actor_action_flee)
				platform_log("h2 ai:   flee panic %d surprised %d stimuli panic %d shield %.2f hide %.2f", (int)actor->state.action_data.flee.panic_type, (int)actor->stimuli.was_surprised,
					(int)actor->stimuli.panic_type, actor->input.shield_vitality, actor_definition_get(actor->meta.definition_index)->defensive.shield_fraction_hide);
			{
				long held = unit_inventory_get_weapon(actor->meta.unit_index, ((struct unit_datum *)unit)->unit.current_weapon_index);
				struct object_datum *w = held != NONE ? object_try_and_get(held) : NULL;

				platform_log("h2 ai:   weapon %s; fire state %d target type %d visible %d; heat %.2f age %.2f flags %lx trigger %.2f last fired %ld ago", w ? tag_get_name(w->definition_index) : "none",
					(int)actor->control.fire_state, (int)actor->control.current_fire_target_type, actor->target.target_prop_index != NONE ? (int)prop_get(actor->target.target_prop_index)->visibility : -1,
					w ? ((struct weapon_datum *)w)->weapon.heat : 0.0f, w ? ((struct weapon_datum *)w)->weapon.age : 0.0f, w ? (unsigned long)((struct weapon_datum *)w)->weapon.flags : 0UL,
					actor->output.analog_primary_trigger, w ? game_time_get() - ((struct weapon_datum *)w)->weapon.game_time_last_fired : -1L);
			}
			platform_log("h2 ai:   towards %.2f %.2f %.2f step %d of %d stuck %d jumps %ld", actor->control.moving_towards_point.x, actor->control.moving_towards_point.y,
				actor->control.moving_towards_point.z, (int)actor->control.path.path.step_index, (int)actor->control.path.path.step_count,
				(int)rt.foot_stuck[DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index) % RT_MAXIMUM_ACTORS], rt.foot_jumps);
			platform_log("h2 ai:   unit dmg %lx uflags %lx stun %d custom %d seat %d unit flags %lx at rest %d cluster %d v %.3f %.3f %.3f throttle %.2f %.2f %.2f output %.2f %.2f biped flags %lx airborne %d anim %d/%d frame %d",
				(unsigned long)unit->object.damage_flags, (unsigned long)b->unit.flags, (int)b->unit.body_stun_ticks, (int)b->unit.animation.state, (int)b->unit.parent_seat_index, (unsigned long)unit->object.flags, TEST_FLAG(unit->object.flags, _object_at_rest_bit) != 0, (int)unit->object.location.cluster_index,
				unit->object.translational_velocity.i, unit->object.translational_velocity.j, unit->object.translational_velocity.k,
				b->unit.throttle.i, b->unit.throttle.j, b->unit.throttle.k, actor->output.throttle.i, actor->output.throttle.j,
				(unsigned long)b->biped.flags, (int)b->biped.airborne_ticks, (int)unit->object.animation.state.index, (int)b->unit.animation.state,
				(int)unit->object.animation.state.frame_index);
		}
	}
}

/* (test command "@aitest ..."): the Halo 2 functions a trial needs, which
the console's Halo CE table does not reach: "place SQUAD [count]", "cs SQUAD
SCRIPT", "order SQUAD ORDER", "see SQUAD" (it knows of the player), "stop SCRIPT" (ends the script's threads: the mission, for a quiet
level) */
void h2_ai_test_command(char const *text)
{
	char verb[16], squad_name[64], script_name[64];
	short squad = NONE, script = NONE, i;
	int count = 0;
	short fields;

	if (!rt.loaded && strncmp(text, "hit obj:", 8))
		return;
	count = NONE;
	verb[0] = squad_name[0] = script_name[0] = 0;
	fields = (short)sscanf(text, "%15s %63s %63s", verb, squad_name, script_name);
	if (fields < 2)
		return;
	if (!strcmp(verb, "stuck"))
	{
		short k;

		stuck_report();
		for (k = 0; k < rt.stuck_place_count; k++)
			rt.stuck_places[k].seconds = 0;
		rt.stuck_place_count = 0;
		return;
	}
	if (!strcmp(verb, "stop"))
	{
		extern void hs_runtime_end_script(char const *name);

		hs_runtime_end_script(squad_name);
		return;
	}
	for (i = 0; i < rt.squad_count; i++)
	{
		if (!strcmp(rt.squads[i].name, squad_name))
			squad = i;
	}
	for (i = 0; fields > 2 && i < global_scenario_get()->hs_scripts.count; i++)
	{
		if (!strcmp(TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->hs_scripts, i, struct hs_script)->name, script_name))
			script = i;
	}
	/* ("whichbsp x y z": the BSPs whose mesh has a sector at the point) */
	if (!strcmp(verb, "whichbsp"))
	{
		real_point3d point;
		short b;

		if (sscanf(text, "%*s %f %f %f", &point.x, &point.y, &point.z) == 3)
		{
			for (b = 0; b < RT_MAXIMUM_FRAME_BSPS; b++)
			{
				if (rt.nav[b] && nav_locate(rt.nav[b], &point) != NONE)
					platform_log("h2 ai: @aitest: point %.1f %.1f %.1f on bsp %d", point.x, point.y, point.z, (int)b);
			}
		}
		return;
	}
	if (squad == NONE && !(!strcmp(verb, "hit") && !strncmp(squad_name, "obj:", 4)))
	{
		platform_log("h2 ai: @aitest: no squad %s", squad_name);
		return;
	}
	/* "hit SQUAD WEAPON MATERIAL COUNT": COUNT hits of the Halo 2 weapon WEAPON's
	(a name in its tag's) first trigger's projectile's impact damage on the
	squad's first actor's collision material named MATERIAL (a Hunter's
	"armor" or "skin"), as a projectile's impact does (the shots to kill
	tests: object_cause_damage, the model's damage table path) */
	/* ("rays": a bullet's line at the squad's first actor from 8 sides, chest high
	and at the head: the collision material each lands on) */
	if (!strcmp(verb, "rays"))
	{
		long actors[RT_MAXIMUM_ACTORS];

		if (actors_of(squad, actors, RT_MAXIMUM_ACTORS) > 0)
		{
			long unit_index = actor_get(actors[0])->meta.unit_index;
			struct object_datum *unit = object_get(unit_index);
			struct collision_model const *collision_model = collision_model_definition_get(object_definition_get(unit->definition_index)->object.collision_model.index);
			real_point3d at;
			real heights[3] = { 0.25f, 0.45f, 0.65f };
			int a, h;

			object_get_origin(unit_index, &at);
			platform_log("h2 ai: rays: facing %.2f %.2f", unit->object.forward.i, unit->object.forward.j);
			for (h = 0; h < 3; h++)
			{
				for (a = 0; a < 8; a++)
				{
					real base = (real)atan2(unit->object.forward.j, unit->object.forward.i) + (real)a * 0.7853982f;
					real_point3d from = at;
					real_vector3d way;
					struct collision_result result;

					from.x += 2.0f * (real)cos(base), from.y += 2.0f * (real)sin(base), from.z += heights[h];
					way.i = at.x - from.x, way.j = at.y - from.y, way.k = 0.0f;
					if (collision_test_vector(FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_bipeds_bit) | FLAG(_collision_test_front_facing_surfaces_bit), &from, &way, NONE, &result) &&
						result.object_index == unit_index)
					{
						struct damage_resistance_material const *dm = result.material_index >= 0 && result.material_index < collision_model->resistance.materials.count ?
							TAG_BLOCK_GET_ELEMENT(&collision_model->resistance.materials, result.material_index, struct damage_resistance_material) : NULL;

						platform_log("h2 ai: rays: height %.2f from %d deg: material %d %s", heights[h], a * 45, (int)result.material_index, dm ? dm->name : "?");
					}
					else
						platform_log("h2 ai: rays: height %.2f from %d deg: misses", heights[h], a * 45);
				}
			}
		}
		return;
	}
	if (!strcmp(verb, "hit"))
	{
		char weapon_name[64], material_name[64];
		int hits = 1, h;
		long actors[RT_MAXIMUM_ACTORS], tag_index, weapon_tag = NONE;
		struct tag_iterator tags;

		weapon_name[0] = material_name[0] = 0;
		sscanf(text, "%*s %*s %63s %63s %d", weapon_name, material_name, &hits);
		tag_iterator_new(&tags, 'weap');
		while ((tag_index = tag_iterator_next(&tags)) != NONE && weapon_tag == NONE)
		{
			if (!strncmp(tag_get_name(tag_index), "h2\\", 3) && strstr(tag_get_name(tag_index), weapon_name))
				weapon_tag = tag_index;
		}
		/* ("obj:NAME": the first living biped whose tag's name has NAME in it,
		one "@biped" made, in place of a squad's actor) */
		long object_unit = NONE;

		if (!strncmp(squad_name, "obj:", 4))
		{
			struct object_iterator iterator;
			struct object_datum *object;

			object_iterator_new(&iterator, _object_mask_biped, 0);
			while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
			{
				if (object_unit == NONE && strstr(tag_get_name(object->definition_index), squad_name + 4) &&
					object->object.body_vitality > 0.0f && !TEST_FLAG(object->object.flags, _object_outside_of_map_bit))
					object_unit = iterator.index;
			}
		}
		if (weapon_tag != NONE && (object_unit != NONE || (squad != NONE && actors_of(squad, actors, RT_MAXIMUM_ACTORS) > 0)))
		{
			struct weapon_definition const *weapon = (struct weapon_definition const *)tag_get('weap', weapon_tag);
			long unit_index = object_unit != NONE ? object_unit : actor_get(actors[0])->meta.unit_index;
			struct object_datum *unit = object_get(unit_index);
			struct collision_model const *collision = collision_model_definition_get(object_definition_get(unit->definition_index)->object.collision_model.index);
			short material = NONE, m;
			long projectile = weapon->weapon.triggers.count ? TAG_BLOCK_GET_ELEMENT(&weapon->weapon.triggers, 0, struct weapon_trigger_definition)->projectile.index : NONE;
			struct projectile_definition const *projectile_definition = projectile != NONE ? (struct projectile_definition const *)tag_get('proj', projectile) : NULL;

			for (m = 0; m < collision->resistance.materials.count; m++)
			{
				if (!strcmp(TAG_BLOCK_GET_ELEMENT(&collision->resistance.materials, m, struct damage_resistance_material)->name, material_name))
					material = m;
			}
			if (projectile_definition && material != NONE)
			{
				real shield = unit->object.shield_vitality, body = unit->object.body_vitality;
				short n = 0;

				for (h = 0; h < hits && unit->object.body_vitality > 0.0f; h++)
				{
					struct damage_data damage;

					damage_data_new(&damage, projectile_definition->projectile.impact_damage.index);
					SET_FLAG(damage.flags, _damage_from_weapon_bit, TRUE);
					damage.scale = 1.0f;
					damage.owner_object_index = local_player_get_player_index(0) != NONE ? player_get(local_player_get_player_index(0))->unit_index : NONE;
					object_get_origin(unit_index, &damage.origin);
					damage.epicenter = damage.origin;
					set_real_vector3d(&damage.direction, 1.0f, 0.0f, 0.0f);
					object_cause_damage(&damage, unit_index, NONE, NONE, material, NULL);
					n++;
				}
				platform_log("h2 ai: @aitest: hit %s on %s: %d hits, %s: shield %.3f -> %.3f body %.3f -> %.3f (max %.1f %.1f)",
					tag_get_name(weapon_tag), squad_name, (int)n, material_name, shield, unit->object.shield_vitality, body,
					unit->object.body_vitality, unit->object.maximum_shield_vitality, unit->object.maximum_body_vitality);
			}
			else
			{
				platform_log("h2 ai: @aitest: hit: no projectile or material %s (%d materials)", material_name, (int)collision->resistance.materials.count);
			for (m = 0; m < collision->resistance.materials.count; m++)
			{
				struct damage_resistance_material const *dm = TAG_BLOCK_GET_ELEMENT(&collision->resistance.materials, m, struct damage_resistance_material);

				platform_log("h2 ai: @aitest:   material %d %s type %d shield x%.2f body x%.2f", (int)m, dm->name, (int)dm->material_type,
					dm->shield_damage_multiplier, dm->body_damage_multiplier);
			}
			}
		}
		else
			platform_log("h2 ai: @aitest: hit: no weapon %s or no actor", weapon_name);
		return;
	}
	/* "board SQUAD": the squad's first actor put in the driver's seat of the
	vehicle "@vspawn" made last (a lock-on test's enemy driver) */
	if (!strcmp(verb, "board"))
	{
		extern long main_test_vehicle;
		long actors[RT_MAXIMUM_ACTORS];

		if (main_test_vehicle != NONE && object_try_and_get(main_test_vehicle) && actors_of(squad, actors, RT_MAXIMUM_ACTORS) > 0)
		{
			long unit_index = actor_get(actors[0])->meta.unit_index;

			platform_log("h2 ai: @aitest: board %s: %s", squad_name, unit_enter_seat(unit_index, main_test_vehicle, 0) ? "in" : "not in");
		}
		return;
	}
	if (!strcmp(verb, "place"))
	{
		if (rt.squads[squad].encounter != NONE)
		{
			struct encounter_definition *e = TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->ai_encounters, rt.squads[squad].encounter, struct encounter_definition);
			struct squad_definition *d = TAG_BLOCK_GET_ELEMENT(&e->squads, rt.squads[squad].squad, struct squad_definition);
			short l;

			platform_log("h2 ai: @aitest: %s: palette %d (%s), %d locations", squad_name, (int)d->actor_palette_index,
				d->actor_palette_index >= 0 ? TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->ai_actor_palette, d->actor_palette_index, struct tag_reference)->name : "none", (int)d->starting_locations.count);
			for (l = 0; l < d->starting_locations.count && l < 4; l++)
			{
				short v = TAG_BLOCK_GET_ELEMENT(&d->starting_locations, l, struct actor_starting_location)->actor_variant_index;

				platform_log("h2 ai: @aitest:   location %d variant %d (%s)", (int)l, (int)v, v >= 0 ? TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->ai_actor_palette, v, struct tag_reference)->name : "none");
			}
		}
		if (fields > 2)
			count = atoi(script_name);
		squad_place(squad, (short)count, NONE);
	}
	else if (!strcmp(verb, "order"))
	{
		for (i = 0; i < rt.order_count; i++)
		{
			if (!strcmp(rt.orders[i].name, script_name))
				squad_order_set(squad, i);
		}
	}
	/* ("only": a trial's: every other squad erased, each half second) */
	else if (!strcmp(verb, "only"))
		rt.only_squad = squad + 1;
	else if (!strcmp(verb, "see"))
	{
		if (squad_reference(squad) != NONE && local_player_get_player_index(0) != NONE &&
			player_get(local_player_get_player_index(0))->unit_index != NONE)
		{
			ai_scripting_magically_see_unit(squad_reference(squad), player_get(local_player_get_player_index(0))->unit_index);
		}
	}
	else if (!strcmp(verb, "cs") && script != NONE)
	{
		long actors[RT_MAXIMUM_ACTORS];
		short actor_count = actors_of(squad, actors, RT_MAXIMUM_ACTORS), k;

		for (k = 0; k < actor_count; k++)
			command_script_run(actors[k], script, NONE);
		platform_log("h2 ai: @aitest: %s runs %s on %d actors", squad_name, script_name, (int)actor_count);
	}
	else
		platform_log("h2 ai: @aitest: no script %s", script_name);
}

/* ---------- the runtime: the orders' endings */

/* a condition's squads: its squad, its group's, else the squad's own */
static short condition_squads(struct rt_condition const *c, short self, short *out, short room)
{
	short count = 0, s;

	if (c->squad >= 0 && c->squad < rt.squad_count)
		out[count++] = c->squad;
	else if (c->group >= 0 && c->group < rt.group_count)
	{
		for (s = 0; s < rt.squad_count && count < room; s++)
		{
			if (group_contains(c->group, s))
				out[count++] = s;
		}
	}
	/* (ai_trigger_test's ai: every squad of it - a group's, 05b's
	gondola_01_enemies -, not its first alone) */
	else if (rt.trigger_self_count > 0)
	{
		for (s = 0; s < rt.trigger_self_count && count < room; s++)
			out[count++] = rt.trigger_self[s];
	}
	else
		out[count++] = self;
	return count;
}

/* the players' units (living) */
static short player_units(long *out, short room)
{
	struct data_iterator iterator;
	struct player_datum *player;
	short count = 0;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL && count < room)
	{
		if (player->unit_index != NONE && object_try_and_get(player->unit_index) &&
			!TEST_FLAG(object_get(player->unit_index)->object.damage_flags, _object_dead_bit))
		{
			out[count++] = player->unit_index;
		}
	}
	return count;
}

/* HALO_H2_ORDER_TIMER_OLD=1: the orders' endings as before (the first
ending whose triggers held for its delay, "after A ticks" from >= A, no
latching triggers) - for A/B runs */
static boolean order_timer_old(void)
{
	static int old = -1;

	if (old < 0)
	{
		char const *v = HALO_ENV_ONCE("HALO_H2_ORDER_TIMER_OLD");

		old = v && *v && *v != '0';
	}
	return old != 0;
}

/* Halo 2's condition rules (the H2EK's names) */
enum
{
	_rule_a_or_greater_alive = 0,
	_rule_a_or_fewer_alive,
	_rule_x_or_greater_strength,
	_rule_x_or_less_strength,
	_rule_if_enemy_sighted,
	_rule_after_a_ticks,
	_rule_if_alerted,
	_rule_script_ref_true,
	_rule_script_ref_false,
	_rule_if_player_in_trigger_volume,
	_rule_if_all_players_in_trigger_volume,
	_rule_combat_status_a_or_more,
	_rule_combat_status_a_or_less,
	_rule_arrived,
	_rule_in_vehicle,
	_rule_sighted_player,
	_rule_a_or_greater_fighting,
	_rule_a_or_fewer_fighting,
	_rule_player_within_x_world_units,
	_rule_player_shot_more_than_x_seconds_ago,
	_rule_game_safe_to_save,
};

H2_ALIGN static boolean condition_test(struct rt_condition const *c, short self)
{
	short squads[MAXIMUM_SQUADS], squad_count = condition_squads(c, self, squads, MAXIMUM_SQUADS), i;
	short living = 0, fighting = 0, status = 0, arrived = 0, in_vehicle = 0, total = 0, members = 0;
	real strength = 0.0f, nearest_player = 1.0e9f;
	long players[16];
	short player_count = player_units(players, 16), k;
	boolean result = FALSE;

	for (i = 0; i < squad_count; i++)
	{
		long reference = squad_reference(squads[i]);
		struct ai_script_actor_reference_iterator iterator;
		struct actor_datum *actor;

		if (reference == NONE)
			continue;
		members += rt.squads[squads[i]].actor_count;
		ai_index_actor_iterator_new(reference, &iterator);
		while ((actor = ai_index_actor_iterator_next(&iterator)) != NULL)
		{
			struct object_datum *unit;
			real_point3d origin;
			short level = 1;

			if (!actor_alive(iterator.actor_index))
				continue;
			unit = object_get(actor->meta.unit_index);
			living++;
			strength += unit->object.body_vitality;
			if (actor->state.action == _actor_action_fight)
				fighting++;
			/* (the squad's combat status, +0x26: the highest of its actors'
			that track a target - halo2-decompiled unknown_2003a0.cpp 0x203360,
			actor +0x86 -, the actors' status as ai_combat_status reads it
			(actor_combat_status); one with no target adds nothing. Before: 7
			for any fighting actor with a target - 03a's e8_cov_inf0 retreated
			to its secondary areas 1.6 s after it saw the player, the real
			squad reads 4 until its target is visible) */
			level = actor->target.target_type != _actor_target_none ? actor_combat_status(iterator.actor_index) : 0;
			if (level > status)
				status = level;
			arrived += actor->control.path.at_destination || !actor->control.moving;
			in_vehicle += unit->object.parent_object_index != NONE;
			object_get_origin(actor->meta.unit_index, &origin);
			for (k = 0; k < player_count; k++)
			{
				real_point3d p;
				real d;

				object_get_origin(players[k], &p);
				d = sqrtf(distance_squared(&origin, &p));
				if (d < nearest_player)
					nearest_player = d;
			}
			total++;
		}
	}
	switch (c->rule)
	{
	case _rule_a_or_greater_alive:
		result = living >= c->a;
		break;
	case _rule_a_or_fewer_alive:
		result = living <= c->a;
		break;
	/* (the squad's strength, +0x10: its living actors' body vitality summed
	over its actor count, the dead kept in that count - halo2-decompiled
	unknown_2003a0.cpp 0x203360, as ai_strength; a group's the same over its
	squads. Before: over the living only, so a squad shot down to one
	unhurt actor stayed at strength 1 - 03a's e1_cov_inf0_2 never fell back) */
	case _rule_x_or_greater_strength:
		result = members > 0 && strength / members >= c->x;
		break;
	case _rule_x_or_less_strength:
		result = members <= 0 || strength / members <= c->x;
		break;
	case _rule_if_enemy_sighted:
	case _rule_sighted_player:
		result = status >= 7;
		break;
	case _rule_after_a_ticks:
		/* (Halo 2: game time > the squad's order time + A, 0x290f60 case 5;
		no squad - a scenario-wide test -: false) */
		if (order_timer_old())
			result = game_time_get() - rt.squads[self].order_time >= c->a;
		else
			result = self >= 0 && self < rt.squad_count && game_time_get() > rt.squads[self].order_time + c->a;
		break;
	case _rule_if_alerted:
		result = status >= 2;
		break;
	case _rule_if_player_in_trigger_volume:
	case _rule_if_all_players_in_trigger_volume:
	{
		short inside = 0;

		for (k = 0; k < player_count; k++)
			inside += c->volume != NONE && scenario_trigger_volume_test_object(c->volume, players[k]);
		result = c->rule == _rule_if_player_in_trigger_volume ? inside > 0 : player_count > 0 && inside == player_count;
		break;
	}
	case _rule_combat_status_a_or_more:
		result = status >= c->a;
		break;
	case _rule_combat_status_a_or_less:
		result = status <= c->a;
		break;
	case _rule_arrived:
		result = total > 0 && arrived == total && game_time_get() - rt.squads[self].order_time > 30;
		break;
	case _rule_in_vehicle:
		result = total > 0 && in_vehicle == total;
		break;
	case _rule_a_or_greater_fighting:
		result = fighting >= c->a;
		break;
	case _rule_a_or_fewer_fighting:
		result = fighting <= c->a;
		break;
	case _rule_player_within_x_world_units:
		result = nearest_player <= c->x;
		break;
	case _rule_game_safe_to_save:
		result = status < 3;
		break;
	default:
		/* (a script's answer, the player's last shot: not kept here) */
		rt.conditions_unknown++;
		result = FALSE;
		break;
	}
	/* (flag 0: not) */
	return (c->flags & 1) ? !result : result;
}

H2_ALIGN static boolean trigger_test(short trigger, short self)
{
	struct rt_trigger *t;
	short k;
	boolean any = FALSE, all = TRUE;

	if (trigger < 0 || trigger >= rt.trigger_count)
		return FALSE;
	t = &rt.triggers[trigger];
	if (!order_timer_old() && (t->flags & 1) && t->latched)
		return TRUE;
	for (k = 0; k < t->condition_count; k++)
	{
		boolean v = condition_test(&t->conditions[k], self);

		any |= v;
		all &= v;
	}
	if (!order_timer_old())
	{
		/* (Halo 2, 0x290e20: OR needs one condition, AND all of them - an
		AND of none holds; a latching trigger stays true once it held) */
		boolean result = t->combination == 1 ? all : any;

		if (result && (t->flags & 1))
			t->latched = TRUE;
		return result;
	}
	return t->condition_count > 0 && (t->combination == 1 ? all : any);
}

/* (ai_trigger_test <trigger> <ai>): the scenario's trigger (by name: a
script's value is its name's string id) tested for the squad - 05a's
old_temple_start, tower1_start, 05b's tower3_cortana_comment wait on it */
H2_ALIGN static void ai_trigger_test_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count, squads[MAXIMUM_SQUADS], squad_count, k, trigger = NONE;
	long *a = arguments(thread_index, 2, &count, initialize);
	char name[64];

	if (!a)
		return;
	{
		struct hs_function_definition const *function = h2_scripts_function_get(function_index);

		/* (its trigger a string - Halo 2's (ai_trigger_test "done_fighting"
		gondola_01_enemies), 05a's and 05b's -: the string's text) */
		if (count > 0 && function && (function->parameter_count <= 0 || function->parameter_types[0] == _hs_type_string) &&
			(unsigned long)a[0] >= 0x10000UL && a[0] != NONE)
		{
			snprintf(name, sizeof(name), "%s", (char const *)a[0]);
			for (k = 0; k < rt.trigger_count && trigger == NONE; k++)
			{
				if (!strcmp(rt.triggers[k].name, name))
					trigger = k;
			}
		}
	}
	if (trigger != NONE)
		;
	else if (count > 0 && a[0] >= 0 && a[0] < rt.trigger_count)
		trigger = (short)a[0];
	else if (count > 0 && h2_scripts_cache() && h2_cache_string(h2_scripts_cache(), (unsigned long)a[0], name, sizeof(name)))
	{
		for (k = 0; k < rt.trigger_count && trigger == NONE; k++)
		{
			if (!strcmp(rt.triggers[k].name, name))
				trigger = k;
		}
	}
	squad_count = count > 1 ? squads_of(a[1], squads, MAXIMUM_SQUADS) : 0;
	rt.trigger_self_count = squad_count;
	memcpy(rt.trigger_self, squads, sizeof(short) * MIN(squad_count, MAXIMUM_SQUADS));
	if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG") || !rt.trigger_test_logged)
	{
		rt.trigger_test_logged = TRUE;
		platform_log("h2 ai: ai_trigger_test %lx (%s) -> trigger %d, squad %d of %d: %d", count > 0 ? a[0] : 0L,
			trigger != NONE ? rt.triggers[trigger].name : "?", trigger, squad_count > 0 ? squads[0] : -1, squad_count,
			trigger != NONE && trigger_test(trigger, squad_count > 0 ? squads[0] : NONE));
	}
	{
		boolean result;

		rt.trigger_self_count = squad_count;
		memcpy(rt.trigger_self, squads, sizeof(short) * MIN(squad_count, MAXIMUM_SQUADS));
		result = trigger != NONE && trigger_test(trigger, squad_count > 0 ? squads[0] : NONE);
		rt.trigger_self_count = 0;
		hs_return(thread_index, value_boolean(result));
	}
	(void)function_index;
}

/* a squad's order's endings tested: the first whose triggers hold (for its
delay) gives the squad its next order */
H2_ALIGN static void squad_secondary_update(short h2_squad);

H2_ALIGN static void squad_endings_update(short h2_squad)
{
	struct rt_squad *s = &rt.squads[h2_squad];
	struct rt_order const *order;
	short e;

	if (s->order < 0 || s->order >= rt.order_count)
		return;
	order = &rt.orders[s->order];
	if (!order_timer_old())
	{
		/* Halo 2 (halo2-decompiled unknown_2003a0.cpp 0x202720 -> 0x202be0,
		unknown_290e20.cpp 0x290cd0), each pass (two seconds): a waiting order's
		countdown goes down by the pass's ticks and the order is given once it
		is down to 0, whatever its triggers do by then; else every ending is
		tested and the LAST one whose triggers hold is chosen - given at once
		with no delay, else made to wait its delay */
		short chosen = NONE;

		if (s->pending_ticks > 0)
			s->pending_ticks -= RT_ORDER_TICKS;
		if (s->pending_order != NONE)
		{
			if (s->pending_ticks > 0)
				return;
			chosen = s->pending_ending;
		}
		else
		{
			for (e = 0; e < order->ending_count; e++)
			{
				struct rt_ending const *ending = &order->endings[e];
				short t, count = 0, needed = ending->combination == 0 ? 1 : ending->trigger_count;

				for (t = 0; t < ending->trigger_count; t++)
				{
					if (ending->triggers[t] != NONE &&
						trigger_test(ending->triggers[t], h2_squad) != ((ending->trigger_flags[t] & 1) != 0))
					{
						if (++count >= needed)
							break;
					}
				}
				if (count >= needed)
					chosen = e;
			}
			if (chosen != NONE)
			{
				long ticks = (long)lrintf(30.0f * order->endings[chosen].delay);

				if (ticks > 0)
				{
					s->pending_order = order->endings[chosen].next_order;
					s->pending_ending = chosen;
					s->pending_ticks = ticks;
					return;
				}
			}
		}
		if (chosen == NONE)
			return;
		e = chosen;
		{
			struct rt_ending const *ending = &order->endings[e];
			short next = s->pending_order != NONE ? s->pending_order : ending->next_order;

			/* (an ending with no next order: nothing given, 0x202be0) */
			if (next < 0 || next >= rt.order_count)
				return;

			if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG") || HALO_ENV_ONCE("HALO_H2_COUNT_LOG"))
				platform_log("h2 ai: %ld %s: order %d %s ends (ending %d): order %d %s", (long)game_time_get(), s->name, s->order,
					rt.orders[s->order].name, e, next, next >= 0 && next < rt.order_count ? rt.orders[next].name : "-");
			rt.orders_ended++;
			s->order = NONE;
			squad_order_set(h2_squad, next);
		}
		return;
	}
	for (e = 0; e < order->ending_count; e++)
	{
		struct rt_ending const *ending = &order->endings[e];
		boolean any = FALSE, all = TRUE;
		short t;

		for (t = 0; t < ending->trigger_count; t++)
		{
			boolean v = trigger_test(ending->triggers[t], h2_squad);

			if (ending->trigger_flags[t] & 1)
				v = !v;
			any |= v;
			all &= v;
		}
		if (!ending->trigger_count || !(ending->combination == 1 ? all : any))
		{
			s->ending_true_since[e] = NONE;
			continue;
		}
		if (s->ending_true_since[e] == NONE)
			s->ending_true_since[e] = game_time_get();
		if (game_time_get() - s->ending_true_since[e] >= (long)(ending->delay * 30.0f))
		{
			if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG") || HALO_ENV_ONCE("HALO_H2_COUNT_LOG"))
				platform_log("h2 ai: %ld %s: order %d %s ends (ending %d): order %d %s", (long)game_time_get(), s->name, s->order,
					rt.orders[s->order].name, e, ending->next_order,
					ending->next_order >= 0 && ending->next_order < rt.order_count ? rt.orders[ending->next_order].name : "-");
			if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG") || HALO_ENV_ONCE("HALO_H2_COUNT_LOG"))
			{
				short k;

				if (ending->next_order >= 0 && ending->next_order < rt.order_count)
				{
					for (k = 0; k < rt.orders[ending->next_order].area_count; k++)
					{
						struct rt_area const *ar = &rt.orders[ending->next_order].areas[k];

						platform_log("h2 ai:   new order's area %d: centre %.2f %.2f %.2f radius %.2f", k, ar->centre.x, ar->centre.y,
							ar->centre.z, ar->radius);
					}
				}

				for (k = 0; k < ending->trigger_count; k++)
				{
					short g = ending->triggers[k], c;

					if (g < 0 || g >= rt.trigger_count)
						continue;
					platform_log("h2 ai:   trigger %s%s (%s of %d)", (ending->trigger_flags[k] & 1) ? "not " : "", rt.triggers[g].name,
						rt.triggers[g].combination ? "and" : "or", (int)rt.triggers[g].condition_count);
					for (c = 0; c < rt.triggers[g].condition_count; c++)
					{
						struct rt_condition const *d = &rt.triggers[g].conditions[c];

						platform_log("h2 ai:     condition rule %d squad %s group %d a %d x %.2f volume %d flags %lx", d->rule,
							d->squad >= 0 && d->squad < rt.squad_count ? rt.squads[d->squad].name : "-", d->group, d->a, d->x, d->volume, d->flags);
					}
				}
			}
			rt.orders_ended++;
			s->order = NONE;
			/* (the ending's dialogue: Halo 2's new_order_* event on the squad) */
			if (ending->dialogue_type > 0)
			{
				struct data_iterator iterator;
				struct actor_datum *actor;
				long first = NONE;

				data_iterator_new(&iterator, actor_data);
				while ((actor = (struct actor_datum *)data_iterator_next(&iterator)) != NULL)
				{
					if (actor_h2_squad(iterator.datum_index) == h2_squad && actor->meta.unit_index != NONE && actor_alive(iterator.datum_index))
					{
						if (first == NONE || h2_ai_actor_is_leader(iterator.datum_index))
							first = actor->meta.unit_index;
					}
				}
				if (first != NONE)
					h2_dialogue_squad_order(first, ending->dialogue_type);
			}
			squad_order_set(h2_squad, ending->next_order);
			return;
		}
	}
}

/* (0x202be0's end) a squad on an order with a secondary area set: once the
set's trigger holds (no order waiting, not already on it) the squad's areas
are the secondary set's until it is given an order again - squad +0x60 -,
each actor takes its order again (0x1e4570) and the squad says the set's
dialogue (type + 0x6e, 0x1fb8a0). The trigger's rule: OR one of its triggers,
AND all of them, each "not" by its flag bit 0 (as an ending's) */
H2_ALIGN static void squad_secondary_update(short h2_squad)
{
	struct rt_squad *s = &rt.squads[h2_squad];
	struct rt_order const *order;
	short t, count = 0, needed, letter = NONE;

	if (order_timer_old() || s->order < 0 || s->order >= rt.order_count || s->pending_order != NONE || s->secondary)
		return;
	order = &rt.orders[s->order];
	if (order->area2_count <= 0 || order->secondary_trigger_count <= 0)
		return;
	needed = order->secondary_combination == 0 ? 1 : order->secondary_trigger_count;
	for (t = 0; t < order->secondary_trigger_count && count < needed; t++)
	{
		if (order->secondary_triggers[t] != NONE &&
			trigger_test(order->secondary_triggers[t], h2_squad) != ((order->secondary_trigger_flags[t] & 1) != 0))
		{
			count++;
		}
	}
	if (count < needed)
		return;
	s->secondary = TRUE;
	rt.secondary_sets++;
	/* (the secondary set's firing positions, the order's letter: paged in once) */
	if (s->encounter != NONE)
	{
		struct encounter_definition *encounter = TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->ai_encounters, s->encounter,
			struct encounter_definition);
		boolean have = FALSE;
		long f;

		for (f = 0; f < encounter->firing_positions.count && !have; f++)
		{
			struct firing_position_definition const *p = TAG_BLOCK_GET_ELEMENT(&encounter->firing_positions, f,
				struct firing_position_definition);

			if (p->unresolved[0] == (byte)(H2_AI_ORDER_MARK & 0xFF) && (short)(p->unresolved[2] | (p->unresolved[3] << 8)) == s->order + 1)
			{
				if (p->unresolved[1] & 0x80)
					have = TRUE;
				else if (letter == NONE)
					letter = p->group_index;
			}
		}
		if (!have && letter != NONE && encounter_order_page_in(s->encounter, s->order, TRUE, letter) == NONE)
			rt.orders_unpaged++;
	}
	if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG") || HALO_ENV_ONCE("HALO_H2_COUNT_LOG"))
		platform_log("h2 ai: %ld %s: order %d %s: its secondary areas (%d)", (long)game_time_get(), s->name, s->order, order->name,
			(int)order->area2_count);
	{
		long actors[RT_MAXIMUM_ACTORS];
		short n = actors_of(h2_squad, actors, RT_MAXIMUM_ACTORS), k;
		long first = NONE;

		for (k = 0; k < n; k++)
		{
			struct actor_datum *actor = actor_get(actors[k]);

			if (!command_script_of_actor(actors[k]))
				actor_guard_refresh(actors[k]);
			if (actor->meta.unit_index != NONE && actor_alive(actors[k]) && (first == NONE || h2_ai_actor_is_leader(actors[k])))
				first = actor->meta.unit_index;
		}
		if (order->secondary_dialogue > 0 && first != NONE)
			h2_dialogue_squad_order(first, order->secondary_dialogue);
	}
}

/* ---------- the runtime: the log's kills */

static void describe_object(long object_index, char *text, long size)
{
	struct object_datum *object = object_index != NONE ? object_try_and_get(object_index) : NULL;
	short i;

	if (!object)
	{
		snprintf(text, (size_t)size, "nothing");
		return;
	}
	for (i = 0; i < rt.watched_count; i++)
	{
		if (rt.watched[i].unit_index == object_index)
		{
			snprintf(text, (size_t)size, "%s", rt.watched[i].squad != NONE ? rt.squads[rt.watched[i].squad].name : "the player");
			return;
		}
	}
	snprintf(text, (size_t)size, "object %lx", object_index);
}

/* the unit's attacker that did it the most harm (NONE) */
static long unit_killer(long unit_index)
{
	struct unit_datum *unit = (struct unit_datum *)object_try_and_get(unit_index);
	long best = NONE;
	real most = 0.0f;
	short k;

	if (!unit)
		return NONE;
	for (k = 0; k < MAXIMUM_ATTACKERS_PER_UNIT; k++)
	{
		if (unit->unit.attackers[k].object_index != NONE && unit->unit.attackers[k].damage_inflicted > most)
		{
			most = unit->unit.attackers[k].damage_inflicted;
			best = unit->unit.attackers[k].object_index;
		}
	}
	return best;
}

/* (source/ai: action_guard.c's cower, ...) a vocalization a Halo 2 behaviour makes that this engine's
dialogue has no type for ("cower"): the test range counts it; the Halo 2 dialogue system (h2_dialogue.c) chooses
and plays its line by its own rules */
#if !defined(HALO_RELEASE) && !defined(HALO_VITA)
void h2_range_stat(long actor_index, char const *key, long value);
#endif
H2_ALIGN void h2_ai_behaviour_voice(
	long unit_index,
	char const *vocalization)
{
#if !defined(HALO_RELEASE) && !defined(HALO_VITA)
	char key[48];

	snprintf(key, sizeof(key), "voice_%s", vocalization);
	h2_range_stat(unit_index, key, 1);
#endif
	h2_dialogue_behaviour_voice(unit_index, vocalization);
}

/* (actors.c actor_kill: ai_kill and the like) a unit a script kills: Halo 2's actors do not react to it as a death (the
H2EK tag test's Grunts fight on when their Elites are ai_kill-ed and retreat when they are shot) */
H2_ALIGN void h2_ai_unit_killed_by_script(
	long unit_index)
{
	if (unit_index == NONE)
		return;
	rt.script_killed[rt.script_killed_next] = unit_index;
	rt.script_killed_next = (short)((rt.script_killed_next + 1) % (short)NUMBEROF(rt.script_killed));
}

/* (source/ai) whether an actor's Halo 2 styles let it use a behaviour (retail index: 0x12 surprise, 0x13
grenade_impulse, 0x2e evasion_impulse, 0x2f dive_impulse, 0x38 retreat, 0x3a flee, 0x3b cower, 0x3e leader_dead_retreat,
0x40 danger_retreat ...): its squad's order's style (the scenario's palette) and its character's own both have its bit -
05a's ledge squads' "assaulting" has no retreat at all (the real ledge Grunts never retreat in a fight; the ridge's,
"guarding", do), the Elite's style no grenade impulse, the Marine's no dive. TRUE on any other level */
H2_ALIGN boolean h2_ai_behaviour_enabled(
	long actor_index,
	short behaviour)
{
	struct actor_datum *actor;
	short e, squad, order;
	unsigned long bit;
	short word;

	if (!rt.loaded || behaviour < 0 || behaviour >= 160 || HALO_ENV_ONCE("HALO_H2_NO_STYLES"))
		return TRUE;
	word = (short)(behaviour >> 5);
	bit = 1UL << (behaviour & 31);
	actor = actor_get(actor_index);
	e = actor->meta.encounter_index != NONE ? (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(actor->meta.encounter_index) : NONE;
	squad = e >= 0 && e < 128 && actor->meta.squad_index >= 0 && actor->meta.squad_index < MAXIMUM_SQUADS_PER_ENCOUNTER_H2 ?
		rt.squad_of[e][actor->meta.squad_index] : NONE;
	order = squad != NONE && squad < rt.squad_count ? rt.squads[squad].order : NONE;
	if (order != NONE && order < rt.order_count && rt.orders[order].style >= 0 && rt.orders[order].style < (short)NUMBEROF(rt.styles) &&
		rt.styles[rt.orders[order].style].valid && !(rt.styles[rt.orders[order].style].bits[word] & bit))
	{
		return FALSE;
	}
	if (actor->meta.definition_index != NONE)
	{
		char const *name = tag_get_name(actor->meta.definition_index);
		short c;

		name = strncmp(name, "h2\\ai\\", 6) ? name : name + 6;
		for (c = 0; c < rt.character_style_count; c++)
		{
			if (!strcmp(rt.character_styles[c].name, name))
				return (rt.character_styles[c].bits[word] & bit) != 0;
		}
	}
	return TRUE;
}

/* (actor_perception.c) the AI globals' dangers on a Halo 2 campaign level (FALSE: not one) */
H2_ALIGN boolean h2_ai_danger_values(
	real *dangers)
{
	if (!rt.loaded || !rt.dangers_read)
		return FALSE;
	memcpy(dangers, rt.dangers, sizeof(rt.dangers));
	return TRUE;
}

/* Halo 2's leader-dead retreat (retail 0x1b52a0, the retreat impulse
0x3e): when an actor of the type that leads an actor's type dies in its
clump and no other of that type is left in it, the actor retreats (the
character's leader dead retreat chance: a Grunt's 1.0) from its target -
to cover, a cower, back to fighting (action_flee.c, action_guard.c). It
does not wait to see or hear the death, as this engine's panic does (its
death scream or the corpse in view: an Elite shot by no one, or out of
sight, left its Grunts fighting), and a scripted death (ai_kill, the act
of god) leaves them be - the H2EK tag test's Grunts do not react to an
ai_kill of their Elites and retreat 11 of 13 when they are shot. The
clump (0x502420, its forming not decompiled) is taken as the actor's
squad and the friends within 10 wu of it */
H2_ALIGN static boolean clump_has(long actor_index, short type, long excluded_unit)
{
	struct data_iterator iterator;
	struct actor_datum *other;
	short clump = clump_of_actor(actor_index);

	data_iterator_new(&iterator, actor_data);
	while ((other = (struct actor_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (iterator.datum_index != actor_index && other->meta.type == type && other->meta.unit_index != excluded_unit &&
			clump != NONE && clump_of_actor(iterator.datum_index) == clump && clump_actor_counts(other))
		{
			return TRUE;
		}
	}
	return FALSE;
}

H2_ALIGN static void leader_dead_retreat(long dead_unit_index, short dead_type, short dead_team, short dead_clump)
{
	struct data_iterator iterator;
	struct actor_datum *actor;

	if (dead_type == NONE || HALO_ENV_ONCE("HALO_H2_PERCEIVED_LEADER_DEATH"))
		return;
	if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
		platform_log("h2 ai: a leader type %d of team %d died in clump %d (%d left in it)", (int)dead_type, (int)dead_team, (int)dead_clump,
			dead_clump != NONE ? (int)rt.clumps[dead_clump].count : -1);
	data_iterator_new(&iterator, actor_data);
	while ((actor = (struct actor_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct actor_definition const *definition;
		struct object_datum *unit;
		long threat;

		if (actor->meta.unit_index == NONE || actor->meta.team_index != dead_team || actor->meta.swarm ||
			(unit = object_try_and_get(actor->meta.unit_index)) == NULL || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
		{
			continue;
		}
		definition = actor_definition_get(actor->meta.definition_index);
		/* (the styles do not hold it back: the impulse, unlike the danger retreat's 0x1b50e0, tests no switch - the tag
		test's ledge Grunts, "assaulting", which has no retreat, run and cower when their Elites are shot) */
		if (definition->panic.panic_leader_type != dead_type)
			continue;
		/* (what it retreats from: its target, else the nearest enemy it knows of - none, no retreat: 0x1b56b0) */
		threat = actor->target.target_prop_index;
		if (threat == NONE)
		{
			struct prop_iterator props;
			struct prop_datum *prop;
			real nearest = 0.0f;

			prop_iterator_new(&props, iterator.datum_index);
			while ((prop = prop_iterator_next(&props)) != NULL)
			{
				if (prop->enemy && !prop->dead && prop->state >= _prop_state_becoming_unacknowledged &&
					prop->state <= _prop_state_acknowledged && (threat == NONE || prop->distance < nearest))
				{
					threat = props.index;
					nearest = prop->distance;
				}
			}
		}
		if (threat == NONE)
			continue;
		/* (the dead one was in its clump - its last one, clumps_update -, and none of its type is left there) */
		if (dead_clump == NONE || clump_of_actor(iterator.datum_index) != dead_clump)
			continue;
		if (clump_has(iterator.datum_index, dead_type, dead_unit_index))
			continue;
		if (actor->stimuli.panic_type < 8 /* _actor_panic_friend_leader_type_killed */ &&
			real_seed_random(get_global_random_seed_address()) < definition->panic.panic_chance_leader_type_killed)
		{
			actor->stimuli.panic_type = 8;
			actor->stimuli.panic_prop_index = threat;
			actor_set_dormant(iterator.datum_index, FALSE);
#if !defined(HALO_RELEASE) && !defined(HALO_VITA)
			{
				void h2_range_stat(long actor_index, char const *key, long value);

				h2_range_stat(iterator.datum_index, "leader_dead_retreat", 1);
			}
#endif
		}
	}
}

/* the AI's units and the players' watched: each death logged with who did
it */
H2_ALIGN static void kills_update(void)
{
	struct data_iterator iterator;
	struct actor_datum *actor;
	long players[16];
	short player_count = player_units(players, 16), i, k;

	/* (new ones watched) */
	data_iterator_new(&iterator, actor_data);
	while ((actor = (struct actor_datum *)data_iterator_next(&iterator)) != NULL)
	{
		short e = actor->meta.encounter_index != NONE ? (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(actor->meta.encounter_index) : NONE;
		short squad = e >= 0 && e < 128 && actor->meta.squad_index >= 0 && actor->meta.squad_index < MAXIMUM_SQUADS_PER_ENCOUNTER_H2 ?
			rt.squad_of[e][actor->meta.squad_index] : NONE;

		if (actor->meta.unit_index == NONE || squad == NONE || !actor_alive(iterator.datum_index))
			continue;
		for (k = 0; k < rt.watched_count && rt.watched[k].unit_index != actor->meta.unit_index; k++)
			;
		/* (its clump while it lives: the retreats at its death are that clump's) */
		if (k < rt.watched_count)
			rt.watched[k].clump = clump_of_actor(iterator.datum_index);
		if (k == rt.watched_count && rt.watched_count < (short)NUMBEROF(rt.watched))
		{
			rt.watched[rt.watched_count].clump = clump_of_actor(iterator.datum_index);
			rt.watched[rt.watched_count].unit_index = actor->meta.unit_index;
			rt.watched[rt.watched_count].encounter = actor->meta.encounter_index;
			rt.watched[rt.watched_count].ce_squad = actor->meta.squad_index;
			rt.watched[rt.watched_count].squad = squad;
			rt.watched[rt.watched_count].dead = FALSE;
			rt.watched[rt.watched_count].type = actor->meta.type;
			rt.watched[rt.watched_count].team = actor->meta.team_index;
			rt.watched_count++;
		}
	}
	for (i = 0; i < player_count; i++)
	{
		for (k = 0; k < rt.watched_count && rt.watched[k].unit_index != players[i]; k++)
			;
		if (k == rt.watched_count && rt.watched_count < (short)NUMBEROF(rt.watched))
		{
			rt.watched[rt.watched_count].unit_index = players[i];
			rt.watched[rt.watched_count].encounter = NONE;
			rt.watched[rt.watched_count].ce_squad = NONE;
			rt.watched[rt.watched_count].squad = NONE;
			rt.watched[rt.watched_count].dead = FALSE;
			rt.watched[rt.watched_count].type = NONE;
			rt.watched[rt.watched_count].team = NONE;
			rt.watched[rt.watched_count].clump = NONE;
			rt.watched_count++;
		}
	}
	/* (the dead logged, and let go) */
	for (k = 0; k < rt.watched_count; k++)
	{
		struct object_datum *unit = object_try_and_get(rt.watched[k].unit_index);
		char victim[64], killer[64];
		long killer_index;

		if (unit && !TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
			continue;
		/* (a Flood carrier bursts as it dies - Halo 2's "spawn death
		children on destroy": its body destroyed, its infection forms out,
		flood_ready) */
		if (unit && unit->object.type == _object_type_biped && rt.flood_carrier_biped != NONE &&
			unit->definition_index == rt.flood_carrier_biped)
		{
			/* (its infection forms of its encounter and squad - this
			engine spawns them as its actor's, gone with its death) */
			unit_get(rt.watched[k].unit_index)->unit.fake_encounter_index = (short)rt.watched[k].encounter;
			unit_get(rt.watched[k].unit_index)->unit.fake_squad_index = rt.watched[k].ce_squad;
			{
				boolean already = TEST_FLAG(unit_get(rt.watched[k].unit_index)->unit.flags, _unit_spawned_actors_bit) != 0;
				short spawned = unit_test_spawning(rt.watched[k].unit_index);

				rt.infection_spawned += spawned;
				if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
					platform_log("h2 ai: a carrier bursts: %d infection forms (encounter %lx squad %d; its definition's variant %lx %d..%d, spawned already %d)", spawned,
						rt.watched[k].encounter, rt.watched[k].ce_squad,
						unit_definition_get(unit->definition_index)->unit.spawned_actor_variant.index,
						unit_definition_get(unit->definition_index)->unit.spawn_actor_lower_bound,
						unit_definition_get(unit->definition_index)->unit.spawn_actor_upper_bound,
						already);
			}
			unit_destroy(rt.watched[k].unit_index);
			rt.carriers_burst++;
			unit = object_try_and_get(rt.watched[k].unit_index);
		}
		if (unit)
		{
			short w;

			killer_index = unit_killer(rt.watched[k].unit_index);
			describe_object(rt.watched[k].unit_index, victim, sizeof(victim));
			describe_object(killer_index, killer, sizeof(killer));
			if (rt.watched[k].squad == NONE)
				rt.player_deaths++;
			else
			{
				for (w = 0; w < rt.watched_count && rt.watched[w].unit_index != killer_index; w++)
					;
				if (w < rt.watched_count && rt.watched[w].squad == NONE)
					rt.kills_by_player++;
				else if (w < rt.watched_count)
					rt.kills_by_ai++;
			}
			platform_log("h2 ai: kill: %s (%s) killed by %s", victim, rt.watched[k].squad == NONE ? "player" : "actor", killer);
			short x;

			for (x = 0; x < (short)NUMBEROF(rt.script_killed) && rt.script_killed[x] != rt.watched[k].unit_index; x++)
				;
			if (rt.watched[k].squad != NONE && x == (short)NUMBEROF(rt.script_killed))
			{
				leader_dead_retreat(rt.watched[k].unit_index, rt.watched[k].type, rt.watched[k].team, rt.watched[k].clump);
				peer_dead_retreat(rt.watched[k].unit_index, rt.watched[k].type, rt.watched[k].team, rt.watched[k].clump);
			}
		}
		rt.watched[k] = rt.watched[--rt.watched_count];
		k--;
	}
}

/* (ai_scene <name> <command script> <ai> [<ai>...]): Halo 2's scene
(halo2-upstream-work unknown_290e20.cpp function_291b40, GPL-3.0-or-later):
the scene named in the scenario, once a level unless its flag says again, when
its trigger conditions hold; each role given a living actor of the ai its group
names, none running a command script; one joint command script, led by the
first role's actor, that cs_switch hands from role to role (function_2768d0).
TRUE when it began. (Role scoring - function_291800's character variants - is
not ported: the first free actor takes a role) */
H2_ALIGN static void ai_scene_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count, k, n;
	long *a = arguments(thread_index, 8, &count, initialize);
	long chosen[RT_MAXIMUM_SCENE_ROLES];
	short roles = 0, scene_index = NONE, wanted = 1;
	struct rt_scene *scene = NULL;
	boolean started = FALSE;

	if (!a)
		return;
	for (k = 0; count > 0 && (unsigned long)a[0] >= 0x10000UL && a[0] != NONE && k < rt.scene_table_count; k++)
	{
		if (!strcmp(rt.scene_table[k].name, (char const *)a[0]))
		{
			scene_index = k;
			scene = &rt.scene_table[k];
			break;
		}
	}
	if (scene)
	{
		short squads[MAXIMUM_SQUADS], self;

		if (!(scene->flags & 1) && scene->fired)
		{
			hs_return(thread_index, value_boolean(FALSE));
			return;
		}
		self = count > 2 && squads_of(a[2], squads, MAXIMUM_SQUADS) > 0 ? squads[0] : NONE;
		if (scene->condition_count > 0)
		{
			boolean any = FALSE, all = TRUE;

			for (k = 0; k < scene->condition_count; k++)
			{
				boolean holds = trigger_test(scene->triggers[k], self);

				if (scene->negated[k])
					holds = !holds;
				any |= holds;
				all &= holds;
			}
			if (!(scene->combine == 0 ? any : all))
			{
				hs_return(thread_index, value_boolean(FALSE));
				return;
			}
		}
		wanted = scene->role_count;
	}
	for (k = 0; k < wanted && count > 2; k++)
	{
		short group = scene ? scene->role_groups[k] : 0;
		long actors[RT_MAXIMUM_ACTORS];
		short actor_count;

		chosen[k] = NONE;
		if (group < 0 || group + 2 >= count)
			break;
		actor_count = actors_of(a[group + 2], actors, RT_MAXIMUM_ACTORS);
		for (n = 0; n < actor_count && chosen[k] == NONE; n++)
		{
			short r;
			boolean taken = FALSE;

			for (r = 0; r < k; r++)
				taken |= chosen[r] == actors[n];
			if (!taken && actor_alive(actors[n]) && !command_script_of_actor(actors[n]))
				chosen[k] = actors[n];
		}
		if (chosen[k] == NONE)
			break;
		roles++;
	}
	if (wanted > 0 && roles == wanted)
	{
		command_script_run(chosen[0], argument_short(a[1]), NONE);
		{
			struct rt_command_script *c = command_script_of_actor(chosen[0]);

			if (c && scene)
			{
				c->scene = scene_index;
				c->role_count = roles;
				memcpy(c->roles, chosen, sizeof(chosen[0]) * roles);
				scene->fired = TRUE;
			}
		}
		started = TRUE;
		rt.scenes++;
	}
	hs_return(thread_index, value_boolean(started));
	(void)function_index;
}

/* (cs_switch <role>): the joint command script's control goes to the role's
actor (function_2768d0); what it overrode was the last actor's */
H2_ALIGN static void cs_switch_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count;
	long *a = arguments(thread_index, 1, &count, initialize);
	struct rt_command_script *c;

	if (!a)
		return;
	c = command_script_of_thread(thread_index);
	if (c && c->scene != NONE && c->scene < rt.scene_table_count && count > 0 && (unsigned long)a[0] >= 0x10000UL && a[0] != NONE)
	{
		struct rt_scene const *scene = &rt.scene_table[c->scene];
		short k;

		for (k = 0; k < scene->role_count && k < c->role_count; k++)
		{
			if (!strcmp(scene->role_names[k], (char const *)a[0]) && c->roles[k] != c->actor_index && actor_alive(c->roles[k]) &&
				!command_script_of_actor(c->roles[k]))
			{
				actor_guard_refresh(c->actor_index);
				c->actor_index = c->roles[k];
				c->flags = 0;
				c->force_status = 0;
				c->look_object = NONE;
				c->body_vitality = object_get(actor_get(c->actor_index)->meta.unit_index)->object.body_vitality;
				rt.scene_switches++;
				break;
			}
		}
	}
	hs_return(thread_index, 0);
	(void)function_index;
}

/* (object_get_ai <object>): the actor driving the unit (NONE) */
H2_ALIGN static void object_get_ai_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count;
	long *a = arguments(thread_index, 1, &count, initialize);
	struct data_iterator iterator;
	struct actor_datum *actor;
	long result = NONE;

	if (!a)
		return;
	data_iterator_new(&iterator, actor_data);
	while (count > 0 && a[0] != NONE && result == NONE && (actor = (struct actor_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (actor->meta.unit_index == a[0] && actor_alive(iterator.datum_index))
			result = RT_AI_ACTOR | (DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index) & 0xFFFF);
	}
	hs_return(thread_index, result);
	(void)function_index;
}

/* where an approach goes: the object's place, or the nearest player's */
static boolean approach_point(long object_index, real_point3d const *origin, real_point3d *point)
{
	if (object_index == RT_APPROACH_PLAYER)
	{
		long players[16];
		short player_count = player_units(players, 16), k;
		real best = 1.0e12f;

		for (k = 0; k < player_count; k++)
		{
			real_point3d p;

			object_get_origin(players[k], &p);
			if (distance_squared(origin, &p) < best)
				best = distance_squared(origin, &p), *point = p;
		}
		return player_count > 0;
	}
	if (object_index == NONE || !object_try_and_get(object_index))
		return FALSE;
	object_get_origin(object_index, point);
	return TRUE;
}

/* (cs_approach <object> <distance> <maximum distance> <follow distance>),
(cs_approach_player <distance> <maximum distance> <follow distance>): the actor
goes to the object or the nearest player until within the distance of it - its
place followed each tick (a driver drives there) -, or given up when it is
beyond the maximum distance or after a while. cs_start_approach and
cs_start_approach_player: the same, not blocking, until cs_approach_stop
(h2_ai_update keeps it going) */
H2_ALIGN static short cs_approach_step(struct rt_command_script *c, struct rt_cs_action *action)
{
	struct hs_function_definition const *function = h2_scripts_function_get(action->function_index);
	char const *name = function ? function->name : "";
	boolean to_player = strstr(name, "_player") != NULL;
	short first = to_player ? 0 : 1;
	real_point3d origin, point;
	long object;
	real distance;

	object = to_player ? RT_APPROACH_PLAYER : action->count > 0 ? action->arguments[0] : NONE;
	if (c && !strncmp(name, "cs_start_", 9))
	{
		c->approach_object = object;
		c->approach_distance = action->count > first ? argument_real(action->arguments[first]) : 1.0f;
		c->approach_maximum = action->count > first + 1 ? argument_real(action->arguments[first + 1]) : 0.0f;
		return CS_STEP_DONE;
	}
	if (!c || !command_script_unit_origin(c, &origin) || !approach_point(object, &origin, &point))
	{
		if (c)
			c->flags &= ~CS_MOVE;
		return CS_STEP_DONE;
	}
	distance = (real)sqrt(distance_squared(&origin, &point));
	if (action->started == NONE)
		action->started = game_time_get();
	/* (Halo 2's approach - halo2-decompiled unknown_272b70.cpp 0x276b40 sets it
	up, type 0x14, the distances squared; retail 0x259430's arm at 0x259a7f
	runs it: it ends once the actor got within <distance>; a target beyond
	<max-distance> is not approached - the script waits on, no time limit) */
	if (action->count > first && distance <= argument_real(action->arguments[first]))
	{
		c->flags &= ~CS_MOVE;
		if (HALO_ENV_ONCE("HALO_H2_AI_INDEX_LOG"))
			platform_log("h2 ai: %s: within %.1f after %ld ticks", name, argument_real(action->arguments[first]), game_time_get() - action->started);
		return CS_STEP_DONE;
	}
	if (action->count > first + 1 && argument_real(action->arguments[first + 1]) > 0.0f && distance > argument_real(action->arguments[first + 1]))
	{
		c->flags &= ~CS_MOVE;
		if ((game_time_get() - action->started) % 300 == 0 && HALO_ENV_ONCE("HALO_H2_AI_INDEX_LOG"))
			platform_log("h2 ai: %s: target %.1f m away, beyond %.1f: waiting (%ld ticks)", name, distance, argument_real(action->arguments[first + 1]),
				game_time_get() - action->started);
		return CS_STEP_CONTINUE;
	}
	c->flags |= CS_MOVE;
	c->flags &= ~CS_MOVE_BY;
	c->move_point = point;
	/* (its floor, again each half second) */
	if (game_time_get() % 15 == 0 || c->move_surface == NONE)
	{
		short cluster;

		point_resolve(&c->move_point, &cluster, &c->move_surface);
	}
	return CS_STEP_CONTINUE;
}

H2_ALIGN static void cs_approach_evaluate(short function_index, long thread_index, boolean initialize)
{
	command_script_action_evaluate(function_index, thread_index, initialize, 4, cs_approach_step);
}

/* (cs_approach_stop) */
H2_ALIGN static void cs_approach_stop_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count;
	long *a = arguments(thread_index, 0, &count, initialize);
	struct rt_command_script *c;

	if (!a)
		return;
	c = command_script_of_thread(thread_index);
	if (c)
	{
		c->approach_object = NONE;
		c->flags &= ~CS_MOVE;
	}
	hs_return(thread_index, 0);
	(void)function_index;
}

/* a command script's boolean (or short) overrides for the rest of it:
cs_enable_targeting, cs_enable_looking, cs_enable_dialogue,
cs_suppress_dialogue_global, cs_ignore_obstacles, cs_vehicle_boost,
cs_force_combat_status, cs_movement_mode (hs_doc: 0 no override, 1 asleep, 2
idle, 3 alert, 4 active; the setters of unknown_29f5b0.cpp) */
H2_ALIGN static void cs_setting_evaluate(short function_index, long thread_index, boolean initialize)
{
	struct hs_function_definition const *function = h2_scripts_function_get(function_index);
	char const *name = function ? function->name : "";
	short count;
	long *a = arguments(thread_index, 1, &count, initialize);
	struct rt_command_script *c;

	if (!a)
		return;
	c = command_script_of_thread(thread_index);
	if (c && count > 0)
	{
		unsigned long bit = 0;
		boolean on = FALSE;

		if (!strcmp(name, "cs_force_combat_status"))
		{
			/* (function_2a88a0: 0 .. 4 taken, any other left as it was) */
			if (argument_short(a[0]) >= 0 && argument_short(a[0]) < 5)
				c->force_status = argument_short(a[0]);
		}
		else if (!strcmp(name, "cs_movement_mode"))
			c->movement_mode = argument_short(a[0]) >= 0 && argument_short(a[0]) < 7 ? argument_short(a[0]) : NONE;
		else if (!strcmp(name, "cs_enable_targeting"))
		{
			bit = CS_TARGETING, on = argument_boolean(a[0]);
			/* (true also sets looking; false leaves it: command_script_set_flag_7f) */
			if (on)
				c->flags |= CS_LOOKING;
		}
		else if (!strcmp(name, "cs_enable_looking"))
			bit = CS_LOOKING, on = argument_boolean(a[0]);
		else if (!strcmp(name, "cs_enable_dialogue"))
			bit = CS_DIALOGUE, on = argument_boolean(a[0]);
		else if (!strcmp(name, "cs_suppress_dialogue_global"))
			bit = CS_DIALOGUE_SUPPRESSED, on = argument_boolean(a[0]);
		else if (!strcmp(name, "cs_ignore_obstacles"))
			bit = CS_IGNORE_OBSTACLES, on = argument_boolean(a[0]);
		else if (!strcmp(name, "cs_vehicle_boost"))
			bit = CS_BOOST, on = argument_boolean(a[0]);
		else if (!strcmp(name, "cs_enable_pathfinding_failsafe"))
			bit = CS_FAILSAFE, on = argument_boolean(a[0]);
		if (bit && on)
			c->flags |= bit;
		else if (bit)
			c->flags &= ~bit;
	}
	hs_return(thread_index, 0);
}

/* (cs_go_to_vehicle <vehicle>): the actor walks to the vehicle and gets in;
the call waits until it is in (or a while has passed: then its seat is taken
where it stands, Halo 2's pathfinding failsafe - a vehicle this engine's paths
do not reach) */
H2_ALIGN static short cs_go_to_vehicle_step(struct rt_command_script *c, struct rt_cs_action *action)
{
	long unit, vehicle;

	vehicle = action->count > 0 ? action->arguments[0] : NONE;
	unit = c && actor_alive(c->actor_index) ? actor_get(c->actor_index)->meta.unit_index : NONE;
	if (unit == NONE || vehicle == NONE || !object_try_and_get(vehicle) || object_get(unit)->object.parent_object_index == vehicle)
		return CS_STEP_DONE;
	if (action->started == NONE)
		action->started = game_time_get();
	if (actor_get(c->actor_index)->state.action != _actor_action_vehicle && game_time_get() % 30 == 0 &&
		object_get(unit)->object.parent_object_index == NONE)
	{
		actor_action_try_to_enter_vehicle(c->actor_index, vehicle, "", NONE, 0, NULL);
	}
	if (game_time_get() - action->started > 10 * 30)
	{
		static char const *const any[] = { "", NULL };

		seat_take(unit, vehicle, any);
		return CS_STEP_DONE;
	}
	return CS_STEP_CONTINUE;
}

H2_ALIGN static void cs_go_to_vehicle_evaluate(short function_index, long thread_index, boolean initialize)
{
	command_script_action_evaluate(function_index, thread_index, initialize, 1, cs_go_to_vehicle_step);
}

/* (cs_move_in_direction <angle> <distance> <facing>): the actor goes that
far at that angle (degrees) from where it faces; waits until it is there */
H2_ALIGN static short cs_move_in_direction_step(struct rt_command_script *c, struct rt_cs_action *action)
{
	real_point3d origin;
	real_point3d *point = &action->point;

	if (!c || action->count < 2 || !command_script_unit_origin(c, &origin))
		return CS_STEP_DONE;
	if (action->started == NONE)
	{
		struct actor_datum *actor = actor_get(c->actor_index);
		long parent = object_get(actor->meta.unit_index)->object.parent_object_index;
		real_vector3d const *forward = &object_get(parent != NONE ? parent : actor->meta.unit_index)->object.forward;
		real yaw = (real)atan2(forward->j, forward->i) + argument_real(action->arguments[0]) * (real)M_PI / 180.0f;
		short cluster;

		action->started = game_time_get();
		point->x = origin.x + argument_real(action->arguments[1]) * (real)cos(yaw);
		point->y = origin.y + argument_real(action->arguments[1]) * (real)sin(yaw);
		point->z = origin.z;
		c->flags |= CS_MOVE;
		c->flags &= ~CS_MOVE_BY;
		c->move_point = *point;
		point_resolve(point, &cluster, &c->move_surface);
	}
	if ((origin.x - point->x) * (origin.x - point->x) + (origin.y - point->y) * (origin.y - point->y) < 0.25f ||
		game_time_get() - action->started > 10 * 30)
	{
		c->flags &= ~CS_MOVE;
		return CS_STEP_DONE;
	}
	return CS_STEP_CONTINUE;
}

H2_ALIGN static void cs_move_in_direction_evaluate(short function_index, long thread_index, boolean initialize)
{
	command_script_action_evaluate(function_index, thread_index, initialize, 3, cs_move_in_direction_step);
}

/* (cs_jump <angle> <velocity>), (cs_jump_to_point <horizontal> <vertical>):
the actor's unit leaves the ground that way (world units a second) */
H2_ALIGN static void cs_jump_evaluate(short function_index, long thread_index, boolean initialize)
{
	struct hs_function_definition const *function = h2_scripts_function_get(function_index);
	short count;
	long *a = arguments(thread_index, 2, &count, initialize);
	struct rt_command_script *c;

	if (!a)
		return;
	c = command_script_of_thread(thread_index);
	if (c && count > 1 && actor_alive(c->actor_index))
	{
		struct object_datum *unit = object_get(actor_get(c->actor_index)->meta.unit_index);
		real along, up;

		if (function && !strcmp(function->name, "cs_jump"))
		{
			real angle = argument_real(a[0]) * (real)M_PI / 180.0f;

			along = argument_real(a[1]) * (real)cos(angle);
			up = argument_real(a[1]) * (real)sin(angle);
		}
		else
			along = argument_real(a[0]), up = argument_real(a[1]);
		unit->object.translational_velocity.i = unit->object.forward.i * along / 30.0f;
		unit->object.translational_velocity.j = unit->object.forward.j * along / 30.0f;
		unit->object.translational_velocity.k = up / 30.0f;
	}
	hs_return(thread_index, 0);
}

/* (cs_custom_animation <graph> <animation> <speed> <interpolate>): the
animation on the actor's unit; waits until it has played */
H2_ALIGN static short cs_custom_animation_step(struct rt_command_script *c, struct rt_cs_action *action)
{
	long unit;

	unit = c && actor_alive(c->actor_index) ? actor_get(c->actor_index)->meta.unit_index : NONE;
	if (unit == NONE || action->count < 2)
		return CS_STEP_DONE;
	if (action->started == NONE)
	{
		action->started = game_time_get();
		if (!h2_cinematics_object_animation(unit, action->arguments[0], action->arguments[1] ? (char const *)action->arguments[1] : "", NONE, NONE, FALSE))
			return CS_STEP_DONE;
	}
	{
		long ticks = h2_cinematics_object_time(unit);

		if (ticks == NONE || ticks <= 0 || game_time_get() - action->started > 60 * 30)
			return CS_STEP_DONE;
	}
	return CS_STEP_CONTINUE;
}

H2_ALIGN static void cs_custom_animation_evaluate(short function_index, long thread_index, boolean initialize)
{
	command_script_action_evaluate(function_index, thread_index, initialize, 4, cs_custom_animation_step);
}

/* (cs_command_script_queued <ai> <script>) */
H2_ALIGN static void cs_command_script_queued_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count, actor_count, k;
	long *a = arguments(thread_index, 2, &count, initialize);
	long actors[RT_MAXIMUM_ACTORS];
	boolean queued = FALSE;

	if (!a)
		return;
	actor_count = count > 1 ? actors_of(a[0], actors, RT_MAXIMUM_ACTORS) : 0;
	for (k = 0; k < actor_count && !queued; k++)
	{
		struct rt_command_script const *c = command_script_of_actor(actors[k]);

		queued = c && c->queued == argument_short(a[1]);
	}
	hs_return(thread_index, value_boolean(queued));
	(void)function_index;
}

/* (ai_dialogue_enable <boolean>): this engine's combat dialogue switch; a
command script may hold it off too (h2_ai_update) */
H2_ALIGN static void ai_dialogue_enable_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count;
	long *a = arguments(thread_index, 1, &count, initialize);

	if (!a)
		return;
	if (count > 0)
		rt.dialogue_off = !argument_boolean(a[0]);
	hs_return(thread_index, 0);
	(void)function_index;
}

/* (ai_suppress_combat <ai> <boolean>): its actors do not fire */
H2_ALIGN static void ai_suppress_combat_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count, actor_count, k;
	long *a = arguments(thread_index, 2, &count, initialize);
	long actors[RT_MAXIMUM_ACTORS];

	if (!a)
		return;
	actor_count = count > 1 ? actors_of(a[0], actors, RT_MAXIMUM_ACTORS) : 0;
	for (k = 0; k < actor_count; k++)
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(actors[k]);

		if (absolute >= RT_MAXIMUM_ACTORS)
			continue;
		if (argument_boolean(a[1]))
			rt.combat_suppressed[absolute / 32] |= 1UL << (absolute % 32);
		else
			rt.combat_suppressed[absolute / 32] &= ~(1UL << (absolute % 32));
	}
	hs_return(thread_index, 0);
	(void)function_index;
}

/* (actions.c) a vehicle ai_vehicle_reserve keeps AI out of */
H2_ALIGN boolean h2_ai_vehicle_reserved(
	long vehicle_index)
{
	short k;

	if (!rt.loaded)
		return FALSE;
	for (k = 0; k < rt.reserved_vehicle_count; k++)
	{
		if (rt.reserved_vehicles[k] == vehicle_index)
			return TRUE;
	}
	return FALSE;
}

/* (ai_vehicle_reserve <vehicle> <boolean>): AI may not enter it.
(ai_vehicle_reserve_seat <vehicle> <seat> <boolean>) is accepted; this keeps
no seat of a vehicle apart */
H2_ALIGN static void ai_vehicle_reserve_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count, k;
	long *a = arguments(thread_index, 2, &count, initialize);

	if (!a)
		return;
	if (count == 2 && a[0] != NONE)
	{
		for (k = 0; k < rt.reserved_vehicle_count && rt.reserved_vehicles[k] != a[0]; k++)
			;
		if (argument_boolean(a[1]) && k == rt.reserved_vehicle_count && k < (short)NUMBEROF(rt.reserved_vehicles))
			rt.reserved_vehicles[rt.reserved_vehicle_count++] = a[0];
		else if (!argument_boolean(a[1]) && k < rt.reserved_vehicle_count)
			rt.reserved_vehicles[k] = rt.reserved_vehicles[--rt.reserved_vehicle_count];
	}
	hs_return(thread_index, value_boolean(TRUE));
	(void)function_index;
}

/* (ai_enter_squad_vehicles <ai>): its squads' actors to their own vehicles
(those their starting locations placed); how many vehicles */
H2_ALIGN static void ai_enter_squad_vehicles_evaluate(short function_index, long thread_index, boolean initialize)
{
	short count, squads[MAXIMUM_SQUADS], squad_count, i, q, entered = 0;
	long *a = arguments(thread_index, 1, &count, initialize);

	if (!a)
		return;
	squad_count = count > 0 ? squads_of(a[0], squads, MAXIMUM_SQUADS) : 0;
	for (i = 0; i < squad_count; i++)
	{
		struct rt_squad *s = &rt.squads[squads[i]];
		long reference = squad_reference(squads[i]);

		for (q = 0; q < s->location_count && reference != NONE; q++)
		{
			if (s->location_objects[q] != NONE && object_try_and_get(s->location_objects[q]))
			{
				ai_scripting_go_to_vehicle(reference, s->location_objects[q], "");
				entered++;
			}
		}
	}
	hs_return(thread_index, value_short(entered));
	(void)function_index;
}

/* ---------- the runtime: the table and the update */

static struct h2_scripts_native const ai_natives[] =
{
	{ "ai_place", _hs_type_void, ai_place_evaluate, NONE, { 0 } },
	{ "ai_living_count", _hs_type_short_integer, ai_count_evaluate, NONE, { 0 } },
	{ "ai_trigger_test", _hs_type_boolean, ai_trigger_test_evaluate, NONE, { 0 } },
	{ "ai_scene", _hs_type_boolean, ai_scene_evaluate, NONE, { 0 } },
	{ "object_get_ai", _hs_type_ai, object_get_ai_evaluate, NONE, { 0 } },
	{ "cs_approach_player", _hs_type_void, cs_approach_evaluate, NONE, { 0 } },
	{ "cs_approach", _hs_type_void, cs_approach_evaluate, NONE, { 0 } },
	{ "cs_start_approach", _hs_type_void, cs_approach_evaluate, NONE, { 0 } },
	{ "cs_start_approach_player", _hs_type_void, cs_approach_evaluate, NONE, { 0 } },
	{ "cs_approach_stop", _hs_type_void, cs_approach_stop_evaluate, NONE, { 0 } },
	{ "cs_switch", _hs_type_void, cs_switch_evaluate, NONE, { 0 } },
	{ "cs_force_combat_status", _hs_type_void, cs_setting_evaluate, NONE, { 0 } },
	{ "cs_movement_mode", _hs_type_void, cs_setting_evaluate, NONE, { 0 } },
	{ "cs_enable_targeting", _hs_type_void, cs_setting_evaluate, NONE, { 0 } },
	{ "cs_enable_looking", _hs_type_void, cs_setting_evaluate, NONE, { 0 } },
	{ "cs_enable_dialogue", _hs_type_void, cs_setting_evaluate, NONE, { 0 } },
	{ "cs_suppress_dialogue_global", _hs_type_void, cs_setting_evaluate, NONE, { 0 } },
	{ "cs_ignore_obstacles", _hs_type_void, cs_setting_evaluate, NONE, { 0 } },
	{ "cs_vehicle_boost", _hs_type_void, cs_setting_evaluate, NONE, { 0 } },
	{ "cs_go_to_nearest", _hs_type_void, cs_go_to_evaluate, NONE, { 0 } },
	{ "cs_go_to_vehicle", _hs_type_void, cs_go_to_vehicle_evaluate, NONE, { 0 } },
	{ "cs_move_in_direction", _hs_type_void, cs_move_in_direction_evaluate, NONE, { 0 } },
	{ "cs_jump", _hs_type_void, cs_jump_evaluate, NONE, { 0 } },
	{ "cs_jump_to_point", _hs_type_void, cs_jump_evaluate, NONE, { 0 } },
	{ "cs_custom_animation", _hs_type_void, cs_custom_animation_evaluate, NONE, { 0 } },
	{ "cs_command_script_queued", _hs_type_boolean, cs_command_script_queued_evaluate, NONE, { 0 } },
	{ "ai_berserk", _hs_type_void, ai_berserk_evaluate, NONE, { 0 } },
	{ "ai_teleport_to_starting_location_if_outside_bsp", _hs_type_void, ai_teleport_outside_evaluate, NONE, { 0 } },
	{ "cs_stop_custom_animation", _hs_type_void, cs_stop_custom_animation_evaluate, NONE, { 0 } },
	/* (bug 3926's: this engine's drivers have no oversteer to cancel; a sharpness of a nondirectional flyer's turn: flyers turn at their own) */
	{ "ai_overcomes_oversteer", _hs_type_void, accepted_evaluate, NONE, { 0 } },
	{ "cs_turn_sharpness", _hs_type_void, accepted_evaluate, NONE, { 0 } },
	{ "cs_deploy_turret", _hs_type_void, cs_deploy_turret_evaluate, NONE, { 0 } },
	{ "cs_grenade", _hs_type_void, cs_grenade_evaluate, NONE, { 0 } },
	{ "cs_set_behavior", _hs_type_void, cs_set_behavior_evaluate, NONE, { 0 } },
	{ "ai_attach_units", _hs_type_void, ai_attach_units_evaluate, NONE, { 0 } },
	{ "ai_dialogue_enable", _hs_type_void, ai_dialogue_enable_evaluate, NONE, { 0 } },
	{ "ai_suppress_combat", _hs_type_void, ai_suppress_combat_evaluate, NONE, { 0 } },
	{ "ai_vehicle_reserve", _hs_type_boolean, ai_vehicle_reserve_evaluate, NONE, { 0 } },
	{ "ai_vehicle_reserve_seat", _hs_type_boolean, ai_vehicle_reserve_evaluate, NONE, { 0 } },
	{ "ai_enter_squad_vehicles", _hs_type_short_integer, ai_enter_squad_vehicles_evaluate, NONE, { 0 } },
	{ "ai_nonswarm_count", _hs_type_short_integer, ai_count_evaluate, NONE, { 0 } },
	{ "ai_swarm_count", _hs_type_short_integer, ai_count_evaluate, NONE, { 0 } },
	{ "ai_spawn_count", _hs_type_short_integer, ai_count_evaluate, NONE, { 0 } },
	{ "ai_fighting_count", _hs_type_short_integer, ai_count_evaluate, NONE, { 0 } },
	{ "ai_strength", _hs_type_real, ai_count_evaluate, NONE, { 0 } },
	{ "ai_living_fraction", _hs_type_real, ai_count_evaluate, NONE, { 0 } },
	{ "ai_erase", _hs_type_void, ai_each_evaluate, NONE, { 0 } },
	{ "ai_kill", _hs_type_void, ai_each_evaluate, NONE, { 0 } },
	{ "ai_kill_silent", _hs_type_void, ai_each_evaluate, NONE, { 0 } },
	{ "ai_renew", _hs_type_void, ai_each_evaluate, NONE, { 0 } },
	{ "ai_vehicle_exit", _hs_type_void, ai_each_evaluate, NONE, { 0 } },
	{ "ai_set_blind", _hs_type_void, ai_each_evaluate, NONE, { 0 } },
	{ "ai_set_deaf", _hs_type_void, ai_each_evaluate, NONE, { 0 } },
	{ "ai_braindead", _hs_type_void, ai_each_evaluate, NONE, { 0 } },
	{ "ai_force_active", _hs_type_void, ai_each_evaluate, NONE, { 0 } },
	{ "ai_set_orders", _hs_type_void, ai_set_orders_evaluate, NONE, { 0 } },
	{ "ai_migrate", _hs_type_void, ai_migrate_evaluate, NONE, { 0 } },
	{ "ai_allegiance", _hs_type_void, ai_allegiance_evaluate, NONE, { 0 } },
	{ "ai_allegiance_remove", _hs_type_void, ai_allegiance_evaluate, NONE, { 0 } },
	{ "ai_magically_see", _hs_type_void, ai_magically_see_evaluate, NONE, { 0 } },
	{ "ai_magically_see_object", _hs_type_void, ai_magically_see_evaluate, NONE, { 0 } },
	{ "ai_actors", _hs_type_object_list, ai_actors_evaluate, NONE, { 0 } },
	{ "ai_get_object", _hs_type_object, ai_get_object_evaluate, NONE, { 0 } },
	{ "ai_get_unit", _hs_type_unit, ai_get_object_evaluate, NONE, { 0 } },
	{ "ai_vehicle_get", _hs_type_vehicle, ai_get_object_evaluate, NONE, { 0 } },
	{ "ai_vehicle_get_from_starting_location", _hs_type_vehicle, ai_get_object_evaluate, NONE, { 0 } },
	{ "ai_vehicle_enter", _hs_type_void, ai_vehicle_enter_evaluate, NONE, { 0 } },
	{ "ai_vehicle_enter_immediate", _hs_type_void, ai_vehicle_enter_evaluate, NONE, { 0 } },
	{ "ai_combat_status", _hs_type_short_integer, ai_combat_status_evaluate, NONE, { 0 } },
	{ "cs_run_command_script", _hs_type_void, cs_run_command_script_evaluate, NONE, { 0 } },
	{ "cs_queue_command_script", _hs_type_void, cs_run_command_script_evaluate, NONE, { 0 } },
	{ "cs_stack_command_script", _hs_type_void, cs_run_command_script_evaluate, NONE, { 0 } },
	{ "cs_command_script_running", _hs_type_boolean, cs_command_script_running_evaluate, NONE, { 0 } },
	{ "cs_go_to", _hs_type_void, cs_go_to_evaluate, NONE, { 0 } },
	{ "cs_go_to_and_face", _hs_type_void, cs_go_to_evaluate, NONE, { 0 } },
	{ "cs_fly_to_and_face", _hs_type_void, cs_go_to_evaluate, NONE, { 0 } },
	{ "cs_go_by", _hs_type_void, cs_go_to_evaluate, NONE, { 0 } },
	{ "cs_start_to", _hs_type_void, cs_go_to_evaluate, NONE, { 0 } },
	{ "cs_face_object", _hs_type_void, cs_override_evaluate, NONE, { 0 } },
	{ "cs_aim_object", _hs_type_void, cs_override_evaluate, NONE, { 0 } },
	{ "cs_look_object", _hs_type_void, cs_override_evaluate, NONE, { 0 } },
	{ "cs_abort_on_combat_status", _hs_type_void, cs_abort_on_status_evaluate, NONE, { 0 } },
	{ "cs_abort_on_alert", _hs_type_void, cs_abort_on_status_evaluate, NONE, { 0 } },
	{ "cs_moving", _hs_type_boolean, cs_moving_evaluate, NONE, { 0 } },
	{ "cs_fly_to", _hs_type_void, cs_go_to_evaluate, NONE, { 0 } },
	{ "cs_fly_by", _hs_type_void, cs_go_to_evaluate, NONE, { 0 } },
	{ "cs_face", _hs_type_void, cs_override_evaluate, NONE, { 0 } },
	{ "cs_aim", _hs_type_void, cs_override_evaluate, NONE, { 0 } },
	{ "cs_look", _hs_type_void, cs_override_evaluate, NONE, { 0 } },
	{ "cs_face_player", _hs_type_void, cs_override_evaluate, NONE, { 0 } },
	{ "cs_aim_player", _hs_type_void, cs_override_evaluate, NONE, { 0 } },
	{ "cs_look_player", _hs_type_void, cs_override_evaluate, NONE, { 0 } },
	{ "cs_shoot", _hs_type_void, cs_override_evaluate, NONE, { 0 } },
	{ "cs_shoot_point", _hs_type_void, cs_override_evaluate, NONE, { 0 } },
	{ "cs_enable_moving", _hs_type_void, cs_override_evaluate, NONE, { 0 } },
	{ "cs_vehicle_speed", _hs_type_void, cs_vehicle_speed_evaluate, NONE, { 0 } },
	{ "cs_crouch", _hs_type_void, cs_override_evaluate, NONE, { 0 } },
	{ "cs_abort_on_damage", _hs_type_void, cs_override_evaluate, NONE, { 0 } },
	{ "cs_teleport", _hs_type_void, cs_teleport_evaluate, NONE, { 0 } },
	{ "object_damage_damage_section", _hs_type_void, object_damage_damage_section_evaluate, NONE, { 0 } },
	{ "cs_pause", _hs_type_void, cs_pause_evaluate, NONE, { 0 } },
	{ "object_set_permutation", _hs_type_void, object_set_permutation_evaluate, NONE, { 0 } },
	{ "object_set_region_state", _hs_type_void, object_set_region_state_evaluate, NONE, { 0 } },
	{ "object_set_variant", _hs_type_void, object_set_variant_evaluate, NONE, { 0 } },
	{ "unit_set_emotional_state", _hs_type_void, unit_set_emotional_state_evaluate, NONE, { 0 } },
	{ "ai_play_line", _hs_type_short_integer, play_line_evaluate, NONE, { 0 } },
	{ "ai_play_line_at_player", _hs_type_short_integer, play_line_evaluate, NONE, { 0 } },
	{ "ai_play_line_on_object", _hs_type_short_integer, play_line_evaluate, NONE, { 0 } },
	{ "cs_play_line", _hs_type_short_integer, play_line_evaluate, NONE, { 0 } },
	{ "ai_cannot_die", _hs_type_void, cannot_die_evaluate, NONE, { 0 } },
	{ "ai_vitality_pinned", _hs_type_boolean, ai_vitality_pinned_evaluate, NONE, { 0 } },
	{ "object_cannot_die", _hs_type_void, cannot_die_evaluate, NONE, { 0 } },
	{ "ai_set_active_camo", _hs_type_void, ai_set_active_camo_evaluate, NONE, { 0 } },
	{ "ai_place_in_vehicle", _hs_type_void, ai_place_in_vehicle_evaluate, NONE, { 0 } },
	{ "ai_disposable", _hs_type_void, ai_disposable_evaluate, NONE, { 0 } },
	{ "cs_enable_pathfinding_failsafe", _hs_type_void, cs_setting_evaluate, NONE, { 0 } },
};

H2_ALIGN struct h2_scripts_native const *h2_ai_natives(
	long *count)
{
	*count = (long)NUMBEROF(ai_natives);
	return ai_natives;
}

/* (h2_scripts.c's vehicle_unload) the carrier's cargo at seats whose label
holds the name let go (falling to the ground with its crew): how many */
H2_ALIGN short h2_ai_vehicle_unload_cargo(
	long vehicle_index,
	char const *seat_name)
{
	short n, count = 0;

	for (n = 0; n < rt.cargo_count;)
	{
		if (rt.cargo[n].carrier == vehicle_index && (!seat_name || !seat_name[0] || strstr(rt.cargo[n].label, seat_name)))
		{
			if (object_try_and_get(rt.cargo[n].cargo))
				count++;
			rt.cargo[n] = rt.cargo[--rt.cargo_count];
			continue;
		}
		n++;
	}
	if (count && HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
		platform_log("h2 ai: vehicle_unload %s: %d cargo let go", seat_name ? seat_name : "", count);
	return count;
}

/* ---------- the runtime: Halo 2's navigation mesh */

static void nav_free(struct rt_nav *n)
{
	if (!n)
		return;
	free(n->first_link);
	free(n->links);
	free(n->first_hop);
	free(n->hops);
	free(n->vertices);
	free(n->bounds);
	free(n->grid_first);
	free(n->grid_items);
	free(n->cost);
	free(n->parent);
	free(n->heap);
	free(n->state);
	free(n->sector_flags);
	free(n);
}

/* a sector's polygon's links, in order (how many; 0: a broken ring) */
static short nav_ring(struct rt_nav const *n, long sector, long *links, short room)
{
	long link = n->first_link[sector];
	short count = 0;

	while (link >= 0 && link < n->link_count && count < room)
	{
		struct rt_nav_link const *l = &n->links[link];

		links[count++] = link;
		link = l->left == sector ? l->forward : l->reverse;
		if (link == n->first_link[sector])
			return count;
	}
	return 0;
}

/* a link's ends, in the sector's winding */
static void nav_link_ends(struct rt_nav const *n, long sector, long link, real_point3d *a, real_point3d *b)
{
	struct rt_nav_link const *l = &n->links[link];
	long v0 = l->left == sector ? l->vertices[0] : l->vertices[1], v1 = l->left == sector ? l->vertices[1] : l->vertices[0];

	*a = n->vertices[v0 < n->vertex_count ? v0 : 0];
	*b = n->vertices[v1 < n->vertex_count ? v1 : 0];
}

H2_ALIGN static void nav_read(struct h2_cache *cache)
{
	long b, bsp_count = h2_cache_structure_count(cache);

	for (b = 0; b < bsp_count && b < RT_MAXIMUM_FRAME_BSPS; b++)
	{
		struct h2_cache_tag bsp;
		long data_count, sector_count, link_count, vertex_count, k, broken = 0;
		unsigned long data, sectors, links, vertices;
		struct rt_nav *n;

		if (!h2_cache_tag_get(cache, h2_cache_structure_tag(cache, b), &bsp) ||
			!h2_cache_block(cache, bsp.address + 0xC4, 0x38, &data_count, &data) || data_count < 1 ||
			!h2_cache_block(cache, data, 8, &sector_count, &sectors) ||
			!h2_cache_block(cache, data + 8, 0x10, &link_count, &links) ||
			!h2_cache_block(cache, data + 0x28, 12, &vertex_count, &vertices) ||
			sector_count <= 0 || link_count <= 0 || vertex_count <= 0 ||
			sector_count > 65534 || link_count > 65534 || vertex_count > 65534)
		{
			continue;
		}
		n = calloc(1, sizeof(*n));
		if (!n)
			continue;
		n->sector_count = sector_count, n->link_count = link_count, n->vertex_count = vertex_count;
		n->first_link = malloc(sizeof(long) * (size_t)sector_count);
		n->links = malloc(sizeof(*n->links) * (size_t)link_count);
		n->vertices = malloc(sizeof(*n->vertices) * (size_t)vertex_count);
		n->bounds = malloc(sizeof(float) * 6 * (size_t)sector_count);
		n->cost = malloc(sizeof(float) * (size_t)sector_count);
		n->parent = malloc(sizeof(long) * (size_t)sector_count);
		n->heap = malloc(sizeof(long) * (size_t)sector_count);
		n->state = malloc((size_t)sector_count);
		n->sector_flags = malloc(sizeof(unsigned short) * (size_t)sector_count);
		if (!n->sector_flags ||
			!n->first_link || !n->links || !n->vertices || !n->bounds || !n->cost || !n->parent || !n->heap || !n->state)
		{
			nav_free(n);
			continue;
		}
		for (k = 0; k < sector_count; k++)
		{
			unsigned char const *p = h2_cache_at(cache, sectors + 8 * (unsigned long)k, 8);

			n->first_link[k] = p ? (long)get32(p + 4) : -1;
			n->sector_flags[k] = p ? (unsigned short)get16(p) : 0;
		}
		for (k = 0; k < link_count; k++)
		{
			unsigned char const *p = h2_cache_at(cache, links + 0x10 * (unsigned long)k, 0x10);
			struct rt_nav_link *l = &n->links[k];

			memset(l, 0xFF, sizeof(*l));
			if (!p)
				continue;
			l->vertices[0] = (unsigned short)get16(p), l->vertices[1] = (unsigned short)get16(p + 2);
			l->flags = (unsigned short)get16(p + 4), l->hint = get16(p + 6);
			l->forward = (unsigned short)get16(p + 8), l->reverse = (unsigned short)get16(p + 0xA);
			l->left = (unsigned short)get16(p + 0xC), l->right = (unsigned short)get16(p + 0xE);
		}
		for (k = 0; k < vertex_count; k++)
		{
			unsigned char const *p = h2_cache_at(cache, vertices + 12 * (unsigned long)k, 12);

			if (p)
				memcpy(&n->vertices[k], p, 12);
			else
				memset(&n->vertices[k], 0, 12);
		}
		for (k = 0; k < sector_count; k++)
		{
			long ring[64];
			short count = nav_ring(n, k, ring, 64), q;
			float *box = &n->bounds[6 * k];

			box[0] = box[1] = box[4] = 1e30f, box[2] = box[3] = box[5] = -1e30f;
			if (!count)
				broken++;
			for (q = 0; q < count; q++)
			{
				real_point3d a, e;

				nav_link_ends(n, k, ring[q], &a, &e);
				box[0] = MIN(box[0], a.x), box[1] = MIN(box[1], a.y), box[2] = MAX(box[2], a.x), box[3] = MAX(box[3], a.y);
				box[4] = MIN(box[4], a.z), box[5] = MAX(box[5], a.z);
			}
		}
		nav_grid_make(n);
		rt.nav[b] = n;
		{
			long hint_count = 0, hop_room = 0, h;
			unsigned long hints = 0;
			boolean hops_old = getenv("HALO_H2_HOPS_OLD") != NULL;

			n->first_hop = malloc(sizeof(long) * (size_t)sector_count);
			if (n->first_hop)
				memset(n->first_hop, 0xFF, sizeof(long) * (size_t)sector_count);
			if (n->first_hop && h2_cache_block(cache, data + 0x38, 0x14, &hint_count, &hints))
			{
				for (h = 0; h < hint_count; h++)
				{
					unsigned char const *p = h2_cache_at(cache, hints + 0x14 * (unsigned long)h, 0x14);

					if (p && (get16(p) == RT_HOP_JUMP || get16(p) == RT_HOP_WALL_JUMP || get16(p) == RT_HOP_HOIST))
						hop_room += 2;
				}
				n->hops = hop_room ? malloc(sizeof(*n->hops) * (size_t)hop_room) : NULL;
				for (h = 0; n->hops && h < hint_count; h++)
				{
					unsigned char const *p = h2_cache_at(cache, hints + 0x14 * (unsigned long)h, 0x14);
					short type = p ? get16(p) : 0;
					real_point3d bottom, top;
					long sectors_of[2];

					if (type == RT_HOP_JUMP || type == RT_HOP_WALL_JUMP)
					{
						long v[4];
						short q;

						for (q = 0; q < 4; q++)
						{
							v[q] = (unsigned short)get16(p + 4 + 2 * q);
							if (v[q] >= vertex_count)
								break;
						}
						if (q < 4)
							continue;
						top.x = 0.5f * (n->vertices[v[0]].x + n->vertices[v[1]].x), top.y = 0.5f * (n->vertices[v[0]].y + n->vertices[v[1]].y),
							top.z = 0.5f * (n->vertices[v[0]].z + n->vertices[v[1]].z);
						bottom.x = 0.5f * (n->vertices[v[2]].x + n->vertices[v[3]].x), bottom.y = 0.5f * (n->vertices[v[2]].y + n->vertices[v[3]].y),
							bottom.z = 0.5f * (n->vertices[v[2]].z + n->vertices[v[3]].z);
					}
					else if (type == RT_HOP_HOIST)
					{
						long v0 = (unsigned short)get16(p + 6), v1 = (unsigned short)get16(p + 8);

						if (v0 >= vertex_count || v1 >= vertex_count)
							continue;
						bottom = n->vertices[v0], top = n->vertices[v1];
					}
					else
						continue;
					sectors_of[0] = nav_locate(n, &top), sectors_of[1] = nav_locate(n, &bottom);
					if (sectors_of[0] == NONE || sectors_of[1] == NONE || sectors_of[0] == sectors_of[1])
						continue;
					/* (a jump goes down; a hoist goes up - Halo 2's only way: before, a hoist
					was also taken down as a jump, HALO_H2_HOPS_OLD=1) */
					if (type != RT_HOP_HOIST || hops_old)
					{
						struct rt_nav_hop *hop = &n->hops[n->hop_count];

						hop->from = sectors_of[0], hop->to = sectors_of[1], hop->a = top, hop->e = bottom, hop->type = RT_HOP_JUMP;
						hop->hint_type = (unsigned char)(type == RT_HOP_HOIST ? RT_HOP_JUMP : type), hop->flags = p[0x0C], hop->heights = type == RT_HOP_HOIST ? 0xFF : p[0x0D];
						hop->next = n->first_hop[hop->from], n->first_hop[hop->from] = n->hop_count++;
					}
					if (type == RT_HOP_HOIST)
					{
						struct rt_nav_hop *hop = &n->hops[n->hop_count];

						hop->from = sectors_of[1], hop->to = sectors_of[0], hop->a = bottom, hop->e = top, hop->type = RT_HOP_HOIST;
						hop->hint_type = RT_HOP_HOIST, hop->flags = p[0x0C], hop->heights = 0;
						hop->next = n->first_hop[hop->from], n->first_hop[hop->from] = n->hop_count++;
					}
					if (getenv("HALO_H2_AI_DEBUG"))
						platform_log("h2 ai: bsp %ld hint %ld type %d flags %02x heights %02x sector %d from %.1f %.1f %.1f to %.1f %.1f %.1f", b, h, (int)type,
							(unsigned)p[0x0C], (unsigned)p[0x0D], (int)(type == RT_HOP_HOIST ? get16(p + 4) : get16(p + 0x0E)), top.x, top.y, top.z, bottom.x, bottom.y, bottom.z);
				}
			}
			if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
				platform_log("h2 ai: bsp %ld: %ld pathfinding hints, %ld hops", b, hint_count, n->hop_count);
		}
		if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
		{
			long hinted = 0, by_flag[16] = { 0 };

			for (k = 0; k < link_count; k++)
			{
				if (n->links[k].hint != NONE)
					hinted++;
				by_flag[n->links[k].flags & 15]++;
			}
			long narrow = 0, closed = 0;

			for (k = 0; k < link_count; k++)
				narrow += (n->links[k].flags & 0x80) != 0;
			long steps = 0;

			for (k = 0; k < sector_count; k++)
				closed += !(n->sector_flags[k] & 1), steps += (n->sector_flags[k] & 0x1000) != 0;
			platform_log("h2 ai: bsp %ld: %ld links hinted; flags 0-3 %ld %ld %ld %ld; %ld links of flag 0x80, %ld sectors without bit 0, %ld steps", b, hinted,
				by_flag[0], by_flag[1], by_flag[2], by_flag[3], narrow, closed, steps);
		}
		platform_log("h2 ai: bsp %ld: navigation mesh of %ld sectors (%ld broken rings), %ld links, %ld vertices", b, sector_count, broken,
			link_count, vertex_count);
	}
}

/* (perf/grunt-tick-2) the grid's column / row of a coordinate: the same
monotonic arithmetic for the boxes as for the points (a point inside a box
falls in a cell between the box's ends' cells), clamped to the grid */
static long nav_grid_cell(float value, float origin, float inverse, long size)
{
	float f = (value - origin) * inverse;

	if (!(f > 0.0f))
		return 0;
	if (f >= (float)size)
		return size - 1;
	return (long)f;
}

/* (perf/grunt-tick-2) nav_locate walked every sector's box (the mesh's bounds,
24 bytes a sector: ~1.6k DRAM misses a search on the Vita's caches, two a
route search); the grid's cell lists the sectors whose box can hold a point
there, in ascending order, so the walk meets the same boxes that pass the
test in the same order: the same answer. A mesh too spread out for it
(items past 16 a sector) keeps no grid. HALO_NAV_GRID=0 as before; 2 both,
compared */
static void nav_grid_make(struct rt_nav *n)
{
	long k, cells, total = 0;
	float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f, extent;

	n->grid_first = NULL;
	n->grid_items = NULL;
	n->grid_w = n->grid_h = 0;
	if (n->sector_count <= 0 || n->sector_count > 65535)
		return;
	for (k = 0; k < n->sector_count; k++)
	{
		float const *box = &n->bounds[6 * k];

		/* (a box with no number in x or y: every point passes its walk's
		x, y test - no grid) */
		if (box[0] != box[0] || box[1] != box[1] || box[2] != box[2] || box[3] != box[3])
			return;
		if (!(box[0] <= box[2] && box[1] <= box[3]))
			continue;
		x0 = MIN(x0, box[0]), y0 = MIN(y0, box[1]), x1 = MAX(x1, box[2]), y1 = MAX(y1, box[3]);
	}
	if (!(x0 <= x1 && y0 <= y1))
		return;
	extent = MAX(x1 - x0, y1 - y0);
	{
		/* (about as many cells as sectors, no more than 64 a side, none under a metre) */
		long side = MIN(64, (long)sqrtf((float)n->sector_count) + 1);

		n->grid_x0 = x0;
		n->grid_y0 = y0;
		n->grid_inverse = extent > (float)side ? (float)side / extent : 1.0f;
		n->grid_w = MIN(side, (long)((x1 - x0) * n->grid_inverse) + 1);
		n->grid_h = MIN(side, (long)((y1 - y0) * n->grid_inverse) + 1);
	}
	cells = n->grid_w * n->grid_h;
	n->grid_first = (long *)calloc((size_t)cells + 1, sizeof(long));
	if (!n->grid_first)
		return;
	/* (counts, then each cell's start, then the items in sector order) */
	for (k = 0; k < n->sector_count; k++)
	{
		float const *box = &n->bounds[6 * k];
		long cx, cy;

		if (!(box[0] <= box[2] && box[1] <= box[3]))
			continue;
		for (cy = nav_grid_cell(box[1], n->grid_y0, n->grid_inverse, n->grid_h); cy <= nav_grid_cell(box[3], n->grid_y0, n->grid_inverse, n->grid_h); cy++)
			for (cx = nav_grid_cell(box[0], n->grid_x0, n->grid_inverse, n->grid_w); cx <= nav_grid_cell(box[2], n->grid_x0, n->grid_inverse, n->grid_w); cx++)
				n->grid_first[cy * n->grid_w + cx + 1]++, total++;
	}
	if (total > 16 * n->sector_count + 4096 || !(n->grid_items = (unsigned short *)malloc(sizeof(unsigned short) * (size_t)(total ? total : 1))))
	{
		free(n->grid_first);
		n->grid_first = NULL;
		n->grid_w = n->grid_h = 0;
		return;
	}
	for (k = 1; k <= cells; k++)
		n->grid_first[k] += n->grid_first[k - 1];
	{
		long *at = (long *)malloc(sizeof(long) * (size_t)cells);

		if (!at)
		{
			free(n->grid_first);
			free(n->grid_items);
			n->grid_first = NULL;
			n->grid_items = NULL;
			n->grid_w = n->grid_h = 0;
			return;
		}
		memcpy(at, n->grid_first, sizeof(long) * (size_t)cells);
		for (k = 0; k < n->sector_count; k++)
		{
			float const *box = &n->bounds[6 * k];
			long cx, cy;

			if (!(box[0] <= box[2] && box[1] <= box[3]))
				continue;
			for (cy = nav_grid_cell(box[1], n->grid_y0, n->grid_inverse, n->grid_h); cy <= nav_grid_cell(box[3], n->grid_y0, n->grid_inverse, n->grid_h); cy++)
				for (cx = nav_grid_cell(box[0], n->grid_x0, n->grid_inverse, n->grid_w); cx <= nav_grid_cell(box[2], n->grid_x0, n->grid_inverse, n->grid_w); cx++)
					n->grid_items[at[cy * n->grid_w + cx]++] = (unsigned short)k;
		}
		free(at);
	}
	platform_log("h2 ai: navigation grid %ldx%ld over %ld sectors: %ld items (%ld KB)", n->grid_w, n->grid_h, n->sector_count, total,
		(long)((sizeof(long) * (size_t)(cells + 1) + sizeof(unsigned short) * (size_t)total) / 1024));
}

/* sector k tried for the point: its dz, as nav_locate's walk tests it (FALSE: not this one) */
static __inline__ boolean nav_locate_try(struct rt_nav const *n, long k, real_point3d const *point, real *dz)
{
	float const *box = &n->bounds[6 * k];
	long ring[64];
	short count, q;
	boolean inside = FALSE;
	real z = 0.0f;

	if (point->x < box[0] || point->x > box[2] || point->y < box[1] || point->y > box[3] ||
		point->z < box[4] - 1.0f || point->z > box[5] + 3.0f)
	{
		return FALSE;
	}
	count = nav_ring(n, k, ring, 64);
	for (q = 0; q < count; q++)
	{
		real_point3d a, e;

		nav_link_ends(n, k, ring[q], &a, &e);
		if (((a.y > point->y) != (e.y > point->y)) && point->x < (e.x - a.x) * (point->y - a.y) / (e.y - a.y) + a.x)
			inside = !inside;
		z += a.z;
	}
	if (!inside || !count)
		return FALSE;
	z /= count;
	if (!(point->z - z > -1.0f))
		return FALSE;
	*dz = fabsf(point->z - z);
	return TRUE;
}

/* the sector under a point (NONE): its polygon holds the point's x, y and
its floor is nearest below (or a little above) it */
static long nav_locate_walk(struct rt_nav const *n, real_point3d const *point)
{
	long k, best = NONE;
	real best_dz = 3.0f, dz;

	for (k = 0; k < n->sector_count; k++)
	{
		if (nav_locate_try(n, k, point, &dz) && dz < best_dz)
			best_dz = dz, best = k;
	}
	return best;
}

static long nav_locate(struct rt_nav const *n, real_point3d const *point)
{
	extern volatile unsigned long halo_settings_generation;
	static unsigned long seen = ~0UL;
	static int mode = 1;
	long best = NONE, item, last;
	real best_dz = 3.0f, dz;

	if (seen != halo_settings_generation)
	{
		char const *setting = getenv("HALO_NAV_GRID");

		seen = halo_settings_generation;
		mode = setting && *setting ? atoi(setting) : 1;
	}
	if (!mode || !n->grid_first || point->x != point->x || point->y != point->y)
		return nav_locate_walk(n, point);
	{
		long cell = nav_grid_cell(point->y, n->grid_y0, n->grid_inverse, n->grid_h) * n->grid_w +
			nav_grid_cell(point->x, n->grid_x0, n->grid_inverse, n->grid_w);

		last = n->grid_first[cell + 1];
		for (item = n->grid_first[cell]; item < last; item++)
		{
			long k = n->grid_items[item];

			if (nav_locate_try(n, k, point, &dz) && dz < best_dz)
				best_dz = dz, best = k;
		}
	}
	if (mode == 2)
	{
		static unsigned long checks, differ;
		long walked = nav_locate_walk(n, point);

		checks++;
		if (walked != best && differ++ < 8)
			platform_log("h2 ai: nav grid check: sector %ld, the walk %ld (%lu of %lu differ)", best, walked, differ, checks);
		if (checks % 200 == 0)
			platform_log("h2 ai: nav grid check: %lu points, %lu differ", checks, differ);
		return walked;
	}
	return best;
}

static void nav_center(struct rt_nav const *n, long sector, real_point3d *center)
{
	float const *box = &n->bounds[6 * sector];

	center->x = 0.5f * (box[0] + box[2]), center->y = 0.5f * (box[1] + box[3]), center->z = box[4];
}

/* a route over the mesh from one point to another (A* over its sectors,
crossing links at their middles; links narrower than a vehicle's width
not crossed): its points, the last the goal (how many; 0: none) */
static short nav_path_search(struct rt_nav *n, real_point3d const *from, real_point3d const *to, real width, real_point3d *out, short room)
{
	long start = nav_locate(n, from), goal = nav_locate(n, to), heap_count = 0, k, current = NONE;
	long crossed[256], closest = NONE;
	real closest_distance = 1e30f;
	real_point3d end = *to;
	long taken = 0;
	short count = 0, crossed_count = 0, q;
	boolean cut_short = FALSE;

	if (start == NONE || goal == NONE)
		return 0;
	memset(n->state, 0, (size_t)n->sector_count);
	n->cost[start] = 0.0f;
	n->parent[start] = NONE;
	n->state[start] = 1;
	n->heap[heap_count++] = start;
	while (heap_count > 0)
	{
		long best = 0;
		real best_f = 1e30f;
		real_point3d here, goal_center;

		if (n->search_limit && ++taken > n->search_limit)
		{
			cut_short = TRUE;
			break;
		}

		/* (a linear pick: the meshes' open sets stay small) */
		nav_center(n, goal, &goal_center);
		for (k = 0; k < heap_count; k++)
		{
			real_point3d c;
			real f;

			nav_center(n, n->heap[k], &c);
			f = n->cost[n->heap[k]] + (real)sqrt((c.x - goal_center.x) * (c.x - goal_center.x) + (c.y - goal_center.y) * (c.y - goal_center.y));
			if (f < best_f)
				best_f = f, best = k;
		}
		current = n->heap[best];
		n->heap[best] = n->heap[--heap_count];
		n->state[current] = 2;
		if (current == goal)
			break;
		{
			real_point3d c;
			real d;

			nav_center(n, current, &c);
			d = (c.x - goal_center.x) * (c.x - goal_center.x) + (c.y - goal_center.y) * (c.y - goal_center.y);
			if (d < closest_distance)
				closest_distance = d, closest = current;
		}
		nav_center(n, current, &here);
		{
			long ring[64];
			short ring_count = nav_ring(n, current, ring, 64);

			for (q = 0; q < ring_count; q++)
			{
				struct rt_nav_link const *l = &n->links[ring[q]];
				long other = l->left == current ? l->right : l->left;
				real_point3d a, e, there;
				real length, cost;

				if (other >= n->sector_count || n->state[other] == 2)
					continue;
				nav_link_ends(n, current, ring[q], &a, &e);
				length = (real)sqrt((a.x - e.x) * (a.x - e.x) + (a.y - e.y) * (a.y - e.y));
				/* (on foot, Halo 2's rules (path.cpp 0x271630): a sector whose flags
				lack bit 0 (walkable) is not entered, nor a step sector (0x1000) from
				another; an edge of flag 0x80 narrower than the
				actor's diameter (its pathfinding radius, at least 0.2) is not
				crossed; any other is. A driver's: no link narrower than its
				vehicle, nor one 2 wu out of level) */
				if (n->use_hops && !n->foot_rules_old)
				{
					real diameter = 2.0f * MAX(0.2f, n->settings.radius);

					if (!(n->sector_flags[other] & 1) || ((n->sector_flags[current] & 0x1000) && (n->sector_flags[other] & 0x1000)) || ((l->flags & 0x80) &&
						(a.x - e.x) * (a.x - e.x) + (a.y - e.y) * (a.y - e.y) + (a.z - e.z) * (a.z - e.z) < diameter * diameter))
					{
						continue;
					}
				}
				else if (length < width || fabsf(a.z - e.z) > 2.0f)
					continue;
				nav_center(n, other, &there);
				cost = n->cost[current] + (real)sqrt((there.x - here.x) * (there.x - here.x) + (there.y - here.y) * (there.y - here.y));
				if (n->state[other] == 1 && cost >= n->cost[other])
					continue;
				n->cost[other] = cost;
				n->parent[other] = ring[q];
				if (n->state[other] != 1 && heap_count < n->sector_count)
				{
					n->state[other] = 1;
					n->heap[heap_count++] = other;
				}
			}
		}
		/* (a jump, a hoist: the parent, -2 - its index) */
		if (n->use_hops && n->first_hop)
		{
			long h;

			for (h = n->first_hop[current]; h != NONE; h = n->hops[h].next)
			{
				struct rt_nav_hop const *hop = &n->hops[h];
				real cost;

				if (n->state[hop->to] == 2)
					continue;
				/* (path.cpp 0x272020: the hint's type in the actor's settings; a jump
				of a height it takes - or any, an infinite jumper's; a hoist of a
				height its movement hints have) */
				if (n->filter_hops)
				{
					struct rt_path_settings const *ps = &n->settings;

					if (!n->foot_rules_old && (n->sector_flags[current] & 0x1000) && (n->sector_flags[hop->to] & 0x1000))
						continue;

					if (!(ps->hint_types & (1UL << hop->hint_type)))
						continue;
					if (hop->hint_type != RT_HOP_HOIST && !ps->jump_any && !(ps->jump_heights & hop->heights))
						continue;
					if (hop->hint_type == RT_HOP_HOIST && !(((ps->movement_hints & 0x400) && (hop->flags & 1)) ||
						((ps->movement_hints & 0x800) && (hop->flags & 2)) || ((ps->movement_hints & 0x1000) && (hop->flags & 4))))
					{
						continue;
					}
				}
				cost = n->cost[current] + 2.0f + (real)sqrt((hop->a.x - here.x) * (hop->a.x - here.x) + (hop->a.y - here.y) * (hop->a.y - here.y)) +
					(real)sqrt((hop->a.x - hop->e.x) * (hop->a.x - hop->e.x) + (hop->a.y - hop->e.y) * (hop->a.y - hop->e.y));
				if (n->state[hop->to] == 1 && cost >= n->cost[hop->to])
					continue;
				n->cost[hop->to] = cost;
				n->parent[hop->to] = -2 - h;
				if (n->state[hop->to] != 1 && heap_count < n->sector_count)
				{
					n->state[hop->to] = 1;
					n->heap[heap_count++] = hop->to;
				}
			}
		}
	}
	/* (the goal out of reach: the sector nearest it reached, its centre the
	route's end) */
	if (current != goal)
	{
		/* (on foot, a goal no search reaches has no route - Halo 2's path,
		0x270930, ends at the nearest node only within the destination's
		radius: a Marine whose position no route reaches stays. A search
		cut short by its limit - the Vita's - still walks the nearest) */
		if (n->use_hops && !n->foot_rules_old && !cut_short)
			return 0;
		if (closest == NONE || closest == start)
			return 0;
		goal = closest;
		nav_center(n, goal, &end);
	}
	/* (the links crossed, from the goal back) */
	for (k = goal; k != start && crossed_count < (short)NUMBEROF(crossed);)
	{
		long link = n->parent[k];
		struct rt_nav_link const *l;

		if (link == NONE)
			break;
		crossed[crossed_count++] = link;
		if (link <= -2)
		{
			k = n->hops[-2 - link].from;
			continue;
		}
		l = &n->links[link];
		k = l->left == k ? l->right : l->left;
	}
	memset(n->kinds, 0, sizeof(n->kinds));
	for (q = crossed_count - 1; q >= 0 && count < room - 2; q--)
	{
		struct rt_nav_link const *l;

		if (crossed[q] <= -2)
		{
			struct rt_nav_hop const *hop = &n->hops[-2 - crossed[q]];

			n->kinds[count] = (unsigned char)hop->type;
			out[count++] = hop->a;
			out[count++] = hop->e;
			continue;
		}
		l = &n->links[crossed[q]];
		real_point3d const *a = &n->vertices[l->vertices[0] < n->vertex_count ? l->vertices[0] : 0];
		real_point3d const *e = &n->vertices[l->vertices[1] < n->vertex_count ? l->vertices[1] : 0];

		out[count].x = 0.5f * (a->x + e->x), out[count].y = 0.5f * (a->y + e->y), out[count].z = 0.5f * (a->z + e->z);
		count++;
	}
	out[count++] = end;
	return count;
}

/* a sector route, timed for the "stuck" report: searches, time, the worst
search and the worst tick's sum (HALO_H2_AI_STUCK only) */
static short nav_path(struct rt_nav *n, real_point3d const *from, real_point3d const *to, real width, real_point3d *out, short room)
{
	extern unsigned long long halo_fine_tick_now(void);
	unsigned long long begin;
	short count;
	boolean timing = rt.route_timing;
	long microseconds;

	if (!timing)
		return nav_path_search(n, from, to, width, out, room);
	begin = halo_fine_tick_now();
	count = nav_path_search(n, from, to, width, out, room);
	microseconds = (long)(halo_fine_tick_now() - begin);
	rt.route_searches++;
	rt.route_microseconds += microseconds;
	rt.route_worst = MAX(rt.route_worst, microseconds);
	if (rt.route_tick != game_time_get())
	{
		rt.route_tick = game_time_get();
		rt.route_tick_sum = 0;
		rt.route_tick_count = 0;
	}
	rt.route_tick_sum += microseconds;
	rt.route_tick_count++;
	rt.route_worst_tick = MAX(rt.route_worst_tick, rt.route_tick_sum);
	rt.route_worst_tick_count = MAX(rt.route_worst_tick_count, rt.route_tick_count);
	return count;
}

/* a route on foot over the mesh, its hops allowed, into the actor's route. At
most so many a tick, and fewer on the Vita tier (the harness: 18-23 us a search,
the worst 250 us, 11 in a tick; x60 on the Vita, whose Outskirts tick is 31 ms):
one past its share gets no route and asks again when its actor's turn comes */
/* an actor's path settings (Halo 2's 0x1f9240): its character's own style's
special movement (no style: every hint type), cut by its squad's order's special
movement, else that order's style's; the sectors' own links always; the jump
heights below its character's jump height; its movement hints. (A command
script's style, 0x1f9240's first cut, is not read: this engine's command
scripts carry none.) FALSE: no Halo 2 character - every hop allowed */
static boolean actor_path_settings(long actor_index, struct rt_path_settings *settings)
{
	struct actor_datum *actor = actor_get(actor_index);
	char const *name;
	short c, e, squad, order;
	long special = NONE;

	settings->hint_types = 0xFF, settings->jump_heights = 0xFF, settings->jump_any = TRUE, settings->movement_hints = 0xFFFFFFFF;
	settings->radius = 0.25f;
	if (actor->meta.definition_index == NONE || getenv("HALO_H2_HOPS_OLD"))
		return FALSE;
	name = tag_get_name(actor->meta.definition_index);
	name = strncmp(name, "h2\\ai\\", 6) ? name : name + 6;
	for (c = 0; c < rt.path_character_count && strcmp(rt.path_characters[c].name, name); c++)
		;
	if (c >= rt.path_character_count)
		return FALSE;
	settings->hint_types = (unsigned long)rt.path_characters[c].special;
	e = actor->meta.encounter_index != NONE ? (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(actor->meta.encounter_index) : NONE;
	squad = e >= 0 && e < 128 && actor->meta.squad_index >= 0 && actor->meta.squad_index < MAXIMUM_SQUADS_PER_ENCOUNTER_H2 ?
		rt.squad_of[e][actor->meta.squad_index] : NONE;
	order = squad != NONE && squad < rt.squad_count ? rt.squads[squad].order : NONE;
	if (order != NONE && order < rt.order_count)
	{
		special = rt.orders[order].special;
		if (special == NONE && rt.orders[order].style >= 0 && rt.orders[order].style < (short)NUMBEROF(rt.styles) && rt.styles[rt.orders[order].style].valid)
			special = rt.styles[rt.orders[order].style].special;
		if (special != NONE)
			settings->hint_types &= (unsigned long)special << 1;
	}
	settings->hint_types |= 1;
	settings->jump_heights = 0;
	settings->jump_any = FALSE;
	if (rt.path_characters[c].jump_height > 0)
	{
		settings->jump_heights = (1UL << MIN(rt.path_characters[c].jump_height, 6)) - 1;
		settings->jump_any = rt.path_characters[c].jump_height >= 7;
	}
	settings->movement_hints = rt.path_characters[c].movement_hints;
	settings->radius = rt.path_characters[c].radius;
	return TRUE;
}

static void foot_route_search(long actor_index, struct rt_nav *n, struct rt_route *r, real_point3d const *from)
{
#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
	boolean tight = TRUE;
#else
	boolean tight = HALO_ENV_ONCE("HALO_H2_ROUTE_TIGHT") != NULL;
#endif
	short searches_a_tick = tight ? 1 : 4;

	if (rt.foot_search_tick != game_time_get())
	{
		rt.foot_search_tick = game_time_get();
		rt.foot_searches_this_tick = 0;
	}
	if (rt.foot_searches_this_tick >= searches_a_tick)
	{
		r->count = 0;
		r->failed = TRUE;
		rt.foot_searches_refused++;
		return;
	}
	rt.foot_searches_this_tick++;
	r->time = game_time_get();
	r->index = 0;
	r->failed = FALSE;
	n->use_hops = TRUE;
	n->filter_hops = actor_path_settings(actor_index, &n->settings);
	n->foot_rules_old = getenv("HALO_H2_FOOT_RULES_OLD") != NULL;
	n->search_limit = tight ? 96 : 0;
	r->count = nav_path(n, from, &r->target, 1.0f, r->points, RT_ROUTE_POINTS);
	n->search_limit = 0;
	n->use_hops = FALSE;
	n->filter_hops = FALSE;
	memcpy(r->kinds, n->kinds, sizeof(r->kinds));
	/* (HALO_H2_ROUTE_LOG=1: each foot route, its points and hops) */
	if (HALO_ENV_ONCE("HALO_H2_ROUTE_LOG"))
	{
		char line[1024];
		int at = 0;
		short q;

		for (q = 0; q < r->count && at < (int)sizeof(line) - 48; q++)
			at += snprintf(line + at, sizeof(line) - (size_t)at, " %.1f,%.1f,%.1f%s", r->points[q].x, r->points[q].y, r->points[q].z,
				q < 64 && r->kinds[q] ? (r->kinds[q] == RT_HOP_HOIST ? "H" : "J") : "");
		platform_log("h2 ai: route tick %ld actor %x types %lx jumps %lx hints %lx from %.1f %.1f %.1f to %.1f %.1f %.1f:%s", (long)game_time_get(),
			(unsigned)actor_index, n->settings.hint_types, n->settings.jump_heights, n->settings.movement_hints, from->x, from->y, from->z,
			r->target.x, r->target.y, r->target.z, r->count ? line : " none");
	}
}

/* (h2_scripts.c's vehicle_load_magic) the vehicles of the list carried by
the carrier at a seat whose label holds the name (Halo 2 loads a Ghost into
a Shadow's creep_sc01 or a Phantom's phantom_sc): how many */
H2_ALIGN short h2_ai_vehicle_load_cargo(
	long vehicle_index,
	char const *seat_name,
	long object_list)
{
	long reference, object;
	short count = 0;
	struct unit_definition *carrier;

	if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
		platform_log("h2 ai: vehicle_load_magic %lx %s list %lx", vehicle_index, seat_name ? seat_name : "", object_list);
	if (!rt.loaded || vehicle_index == NONE || !object_try_and_get(vehicle_index) || object_list == NONE ||
		object_get(vehicle_index)->object.type != _object_type_vehicle)
	{
		return 0;
	}
	carrier = unit_definition_get(object_get(vehicle_index)->definition_index);
	/* (one vehicle passed as itself - h2_scripts.c - or a list of them) */
	{
		struct object_datum *one = object_try_and_get(object_list);

		reference = NONE;
		object = one && one->object.type == _object_type_vehicle ? object_list : NONE;
	}
	if (object == NONE)
		object = object_list_get_first(object_list, &reference);
	for (; object != NONE; object = reference != NONE ? object_list_get_next(object_list, &reference) : NONE)
	{
		short q;

		if (!object_try_and_get(object) || object_get(object)->object.type != _object_type_vehicle || object == vehicle_index)
			continue;
		for (q = 0; q < carrier->unit.seats.count && rt.cargo_count < (short)NUMBEROF(rt.cargo); q++)
		{
			struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(&carrier->unit.seats, q, struct unit_seat);
			short n;

			if (seat_name && seat_name[0] && !strstr(seat->label, seat_name))
				continue;
			for (n = 0; n < rt.cargo_count && !(rt.cargo[n].carrier == vehicle_index && !strcmp(rt.cargo[n].label, seat->label)) &&
				rt.cargo[n].cargo != object; n++)
				;
			if (n < rt.cargo_count)
				continue;
			rt.cargo[rt.cargo_count].carrier = vehicle_index;
			rt.cargo[rt.cargo_count].cargo = object;
			snprintf(rt.cargo[rt.cargo_count].label, sizeof(rt.cargo[0].label), "%s", seat->label);
			snprintf(rt.cargo[rt.cargo_count].marker, sizeof(rt.cargo[0].marker), "%s", seat->marker_name);
			rt.cargo_count++;
			count++;
			break;
		}
	}
	if (count && HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
		platform_log("h2 ai: vehicle_load_magic %s: %d vehicles carried by %s", seat_name ? seat_name : "", count,
			tag_get_name(object_get(vehicle_index)->definition_index));
	return count;
}

/* the cargo of carriers that are not scripted fliers (a Shadow's Ghost):
held at its seat's marker as it was last drawn, moving with it */
static void ground_cargo_update(void)
{
	short q;

	for (q = 0; q < rt.cargo_count; q++)
	{
		struct object_datum *carrier = object_try_and_get(rt.cargo[q].carrier), *cargo = object_try_and_get(rt.cargo[q].cargo);
		struct object_marker marker;
		real_point3d at;

		if (!carrier || !cargo || vehicle_scripted_flier(rt.cargo[q].carrier))
			continue;
		if (object_get_marker_by_name(rt.cargo[q].carrier, rt.cargo[q].marker, &marker, 1) >= 1)
			at = marker.matrix.position;
		else
		{
			object_get_origin(rt.cargo[q].carrier, &at);
			at.z += 1.0f;
		}
		object_set_position(rt.cargo[q].cargo, &at, &carrier->object.forward, &carrier->object.up);
		cargo->object.translational_velocity = carrier->object.translational_velocity;
		cargo->object.angular_velocity.i = cargo->object.angular_velocity.j = cargo->object.angular_velocity.k = 0.0f;
	}
}

/* ---------- the runtime: the Flood */

/* (once a level) the carrier's biped given Halo 2's death spawn - its
infection forms' actor variant (the one this level made of Halo 2's
flood_infection character), how many (its biped's death spawn count, read
at load) and how hard they are thrown (its spawned velocity) - which this
engine's unit_destroy spawns */
static void flood_ready(void)
{
	struct tag_iterator iterator;
	long tag, carrier_variant = NONE, infection_variant = NONE;
	short q;

	rt.flood_ready = TRUE;
	rt.flood_carrier_biped = NONE;
	rt.flood_infection_biped = NONE;
	tag_iterator_new(&iterator, 'actv');
	while ((tag = tag_iterator_next(&iterator)) != NONE)
	{
		char const *name = tag_get_name(tag);

		if (!name)
			continue;
		if (!strncmp(name, "h2\\ai\\flood_carrier ", 20) && carrier_variant == NONE)
			carrier_variant = tag;
		if (!strncmp(name, "h2\\ai\\flood_infection ", 22) && infection_variant == NONE)
			infection_variant = tag;
	}
	/* (the combat forms' bipeds and a variant of each: an infection form
	reanimates a dead combat form as its own character - Halo 2's combat
	form bipeds' reanimation character, flood_update) */
	rt.flood_infection_biped = infection_variant != NONE ? actor_variant_definition_get(infection_variant)->unit_reference.index : NONE;
	rt.flood_combat_count = 0;
	tag_iterator_new(&iterator, 'actv');
	while ((tag = tag_iterator_next(&iterator)) != NONE && rt.flood_combat_count < 4)
	{
		char const *name = tag_get_name(tag);
		long biped;
		short q;

		if (!name || (strncmp(name, "h2\\ai\\floodcombat_elite ", 24) && strncmp(name, "h2\\ai\\flood_combat_human ", 25)))
			continue;
		biped = actor_variant_definition_get(tag)->unit_reference.index;
		for (q = 0; q < rt.flood_combat_count && rt.flood_combat[q].biped != biped; q++)
			;
		if (q == rt.flood_combat_count && biped != NONE)
		{
			rt.flood_combat[q].biped = biped;
			rt.flood_combat[q].variant = tag;
			rt.flood_combat[q].kind = strstr(name, "floodcombat_elite") ? 0 : 1;
			rt.flood_combat_count++;
		}
	}
	/* (the combat forms' feign death, Halo 2's biped fields - threshold, time, chance, repeat chance - are this engine's
	unit's own: units.c arms it as the unit is made (chance), and a hit that takes more than the threshold of its body
	without killing it drops it as dead for the time + 0..1 s, the same unit and actor getting up after (the repeat chance
	arms it again). The elite form's threshold and time are 0: it never feigns - 04b's shot elite forms stayed down in
	the real engine) */
	for (q = 0; q < rt.flood_combat_count; q++)
	{
		struct unit_definition *u = unit_definition_get(rt.flood_combat[q].biped);
		short kind = rt.flood_combat[q].kind;

		u->unit.feign_death_threshold = rt.feign[kind].threshold;
		u->unit.feign_death_time = rt.feign[kind].time;
		u->unit.feign_death_chance = rt.feign[kind].chance;
		u->unit.feign_death_repeat_chance = rt.feign[kind].repeat;
	}
	if (carrier_variant == NONE)
		return;
	rt.flood_carrier_biped = actor_variant_definition_get(carrier_variant)->unit_reference.index;
	if (rt.flood_carrier_biped == NONE || infection_variant == NONE || rt.carrier_spawn_count <= 0)
		return;
	{
		struct unit_definition *u = unit_definition_get(rt.flood_carrier_biped);

		u->unit.spawned_actor_variant.group_tag = 'actv';
		u->unit.spawned_actor_variant.index = infection_variant;
		u->unit.spawn_actor_lower_bound = u->unit.spawn_actor_upper_bound = rt.carrier_spawn_count;
		u->unit.spawn_throw_velocity = rt.carrier_spawn_velocity;
		platform_log("h2 ai: the Flood carrier bursts into %d infection forms (%s), thrown at %.1f wu/s", rt.carrier_spawn_count,
			tag_get_name(infection_variant), rt.carrier_spawn_velocity);
	}
}

/* (at load) Halo 2's carrier biped's death spawn count: the short after its
death spawn character's reference (the infection form's character), and
its spawned velocity - searched for, the unit's layout not otherwise read
here */
/* (at load, after the carrier's) the combat forms' feign death: Halo 2's
unit fields before its spawned turret character - feign death threshold,
time, ..., chance and repeat chance (the kit's order: the spawned actor
count found in the carrier's tag 8 bytes after that reference, the same
unit layout in every biped) */
H2_ALIGN static void flood_feign_read(struct h2_cache *cache)
{
	static char const *const bipeds[2] =
	{
		"objects\\characters\\floodcombat_elite\\floodcombat_elite", "objects\\characters\\floodcombat_human\\floodcombat_human",
	};
	short b;

	for (b = 0; b < 2; b++)
	{
		long tag = h2_cache_tag_find(cache, 'bipd', bipeds[b]);
		struct h2_cache_tag t;
		unsigned long reference = rt.unit_spawn_count_offset - 8;
		unsigned char const *p;

		rt.feign[b].chance = 0.0f;
		if (!rt.unit_spawn_count_offset || tag == NONE || !h2_cache_tag_get(cache, tag, &t) ||
			!(p = h2_cache_at(cache, t.address + reference - 28, 28)))
		{
			continue;
		}
		rt.feign[b].threshold = getf(p);
		rt.feign[b].time = getf(p + 4);
		rt.feign[b].chance = getf(p + 20);
		rt.feign[b].repeat = getf(p + 24);
		if (!(rt.feign[b].chance >= 0.0f && rt.feign[b].chance <= 1.0f && rt.feign[b].repeat >= 0.0f && rt.feign[b].repeat <= 1.0f &&
			rt.feign[b].time >= 0.0f && rt.feign[b].time < 30.0f && rt.feign[b].threshold >= 0.0f && rt.feign[b].threshold <= 1.0f))
		{
			rt.feign[b].chance = 0.0f;
		}
		platform_log("h2 ai: %s: feign death threshold %.2f, chance %.2f (again %.2f), down %.1f s", bipeds[b], rt.feign[b].threshold,
			rt.feign[b].chance, rt.feign[b].repeat, rt.feign[b].time);
	}
}

H2_ALIGN static void flood_read(struct h2_cache *cache)
{
	long carrier = h2_cache_tag_find(cache, 'bipd', "objects\\characters\\floodcarrier\\floodcarrier");
	long infection = h2_cache_tag_find(cache, 'char', "objects\\characters\\flood_infection\\ai\\flood_infection");
	struct h2_cache_tag t;
	unsigned long offset;

	rt.carrier_spawn_count = 0;
	rt.carrier_spawn_velocity = 0.0f;
	if (carrier == NONE || infection == NONE || !h2_cache_tag_get(cache, carrier, &t))
		return;
	for (offset = 0; offset < 0x800; offset += 4)
	{
		unsigned char const *p;

		if (h2_cache_reference(cache, t.address + offset) != infection || !(p = h2_cache_at(cache, t.address + offset + 8, 2)))
			continue;
		if (get16(p) > 0 && get16(p) < 32)
		{
			rt.carrier_spawn_count = get16(p);
			break;
		}
	}
	/* (its spawned velocity: the real after its spawned actor count's two
	shorts - Halo 2's 3 wu/s -, found by the count range before it) */
	for (offset = 0; offset + 8 < 0x800; offset += 2)
	{
		unsigned char const *p = h2_cache_at(cache, t.address + offset, 8);
		short lower = p ? get16(p) : 0, upper = p ? get16(p + 2) : 0;
		float v = p ? getf(p + 4) : 0.0f;

		if (p && lower > 0 && upper >= lower && upper < 32 && v > 0.5f && v < 20.0f)
		{
			rt.carrier_spawn_velocity = v;
			rt.unit_spawn_count_offset = offset;
			break;
		}
	}
}

/* Halo 2's infection forms reanimate the Flood's dead: an infection form
that reaches a dead combat form (within 1.5 wu) is spent and the body gets
up as its reanimation character - a combat form of its own kind, of the
infection form's squad (every half second) */
static void flood_update(void)
{
	struct object_iterator iterator;
	struct object_datum *object;
	long infection[16];
	short infection_count = 0, k;

	if (rt.flood_infection_biped == NONE || !rt.flood_combat_count)
		return;
	/* (each tick: an infection form given a body goes to it, its infection marker, a step a tick along a clear line - stopped
	by the level's geometry, never through it) */
	for (k = 0; k < rt.reanimation_bodies_count; k++)
	{
		struct object_datum *form = object_try_and_get(rt.reanimation_bodies[k].form);
		real_point3d p, next;
		real_vector3d step, facing;
		real dx, dy, length;
		struct collision_result collision;

		if (!form || form->definition_index != rt.flood_infection_biped || TEST_FLAG(form->object.damage_flags, _object_dead_bit))
			continue;
		object_get_origin(rt.reanimation_bodies[k].form, &p);
		dx = rt.reanimation_bodies[k].marker.x - p.x, dy = rt.reanimation_bodies[k].marker.y - p.y;
		length = (real)sqrt(dx * dx + dy * dy);
		if (length < 0.5f)
			continue;
		facing.i = dx / length, facing.j = dy / length, facing.k = 0.0f;
		next = p;
		next.x += 0.12f * facing.i, next.y += 0.12f * facing.j;
		next.z += (rt.reanimation_bodies[k].marker.z - p.z) * 0.1f;
		step.i = next.x - p.x, step.j = next.y - p.y, step.k = next.z - p.z;
		p.z += 0.1f;
		if (!collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit), &p, &step,
			rt.reanimation_bodies[k].form, &collision))
		{
			object_set_position(rt.reanimation_bodies[k].form, &next, &facing, NULL);
		}
	}
	if (game_time_get() % 15)
		return;
	rt.reanimation_bodies_count = 0;
	object_iterator_new(&iterator, FLAG(_object_type_biped), 0);
	while ((object = object_iterator_next(&iterator)) != NULL && infection_count < (short)NUMBEROF(infection))
	{
		if (object->definition_index == rt.flood_infection_biped && !TEST_FLAG(object->object.damage_flags, _object_dead_bit) &&
			(unit_get(iterator.index)->unit.actor_index != NONE || unit_get(iterator.index)->unit.swarm_actor_index != NONE))
		{
			infection[infection_count++] = iterator.index;
		}
	}
	if (!infection_count)
		return;
	object_iterator_new(&iterator, FLAG(_object_type_biped), 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		long body = iterator.index;
		short q;
		real_point3d at;

		if (!TEST_FLAG(object->object.damage_flags, _object_dead_bit))
			continue;
		for (q = 0; q < rt.flood_combat_count && rt.flood_combat[q].biped != object->definition_index; q++)
			;
		if (q == rt.flood_combat_count)
			continue;
		object_get_origin(body, &at);
		/* (Halo 2's infect impulse, retail 0x256810: a body the swarm perceives with an infection marker - front, else back -
		facing up (only the combat forms' and the Juggernaut's models carry them: a Marine or an Elite is never taken), not
		taken already, given to the nearest free form; the form must see it - within its vision, a clear line. Not ported:
		the AI globals' cap of 13 at once, the props' own perception states) */
		if (rt.reanimation_bodies_count < (short)NUMBEROF(rt.reanimation_bodies))
		{
			struct object_marker marker;
			boolean up = (object_get_marker_by_name(body, "infection front", &marker, 1) >= 1 && marker.matrix.forward.k > 0.0f) ||
				(object_get_marker_by_name(body, "infection back", &marker, 1) >= 1 && marker.matrix.forward.k > 0.0f);
			real best = 1.0e9f;
			long form = NONE;
			short f, b;

			for (f = 0; up && f < infection_count; f++)
			{
				real_point3d p;
				real_vector3d line;
				struct collision_result collision;
				struct unit_datum *u = infection[f] != NONE ? unit_get(infection[f]) : NULL;
				long actor_index = u ? (u->unit.swarm_actor_index != NONE ? u->unit.swarm_actor_index : u->unit.actor_index) : NONE;
				real vision = actor_index != NONE ? actor_definition_get(actor_get(actor_index)->meta.definition_index)->perception.maximum_vision_distance : 0.0f;
				real d;

				if (actor_index == NONE)
					continue;
				for (b = 0; b < rt.reanimation_bodies_count && rt.reanimation_bodies[b].form != infection[f]; b++)
					;
				if (b < rt.reanimation_bodies_count)
					continue;
				object_get_origin(infection[f], &p);
				d = distance_squared(&p, &marker.matrix.position);
				if (d >= best || d > vision * vision)
					continue;
				p.z += 0.1f;
				line.i = marker.matrix.position.x - p.x, line.j = marker.matrix.position.y - p.y, line.k = marker.matrix.position.z + 0.1f - p.z;
				if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit), &p, &line, infection[f],
					&collision))
				{
					continue;
				}
				best = d, form = infection[f];
			}
			if (form != NONE)
			{
				rt.reanimation_bodies[rt.reanimation_bodies_count].marker = marker.matrix.position;
				rt.reanimation_bodies[rt.reanimation_bodies_count].body = body;
				rt.reanimation_bodies[rt.reanimation_bodies_count++].form = form;
			}
		}
		if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG") && game_time_get() % 150 == 0)
		{
			real nearest = 1e9f;

			for (k = 0; k < infection_count; k++)
			{
				real_point3d p;

				object_get_origin(infection[k], &p);
				nearest = MIN(nearest, distance_squared(&p, &at));
			}
			platform_log("h2 ai: a dead %s, the nearest of %d infection forms %.1f wu off", tag_get_name(object->definition_index),
				infection_count, (real)sqrt(nearest));
		}
		for (k = 0; k < infection_count; k++)
		{
			real_point3d p;

			if (infection[k] == NONE)
				continue;
			object_get_origin(infection[k], &p);
			if (distance_squared(&p, &at) < 1.5f * 1.5f)
			{
				long before = actor_data->actual_count;

				/* (spawned from the body as of the infection form's encounter
				and squad - the spawn reads a swarm unit's actor_index, NONE:
				actors.c) */
				struct unit_datum *infecting = unit_get(infection[k]);
				long swarm = infecting->unit.swarm_actor_index != NONE ? infecting->unit.swarm_actor_index : infecting->unit.actor_index;
				struct unit_datum *dead = unit_get(body);

				if (swarm == NONE || dead->unit.actor_index != NONE || dead->unit.swarm_actor_index != NONE)
					continue;
				dead->unit.fake_encounter_index = (short)actor_get(swarm)->meta.encounter_index;
				dead->unit.fake_squad_index = actor_get(swarm)->meta.squad_index;
				p = at;
				if (actors_spawn_from_unit(body, rt.flood_combat[q].variant, 1, 0.0f) > 0)
				{
					/* (the new combat form where the body lay, the body and the
					infection form gone) */
					struct object_iterator newest;
					struct object_datum *n;
					long made = NONE;

					object_iterator_new(&newest, FLAG(_object_type_biped), 0);
					while ((n = object_iterator_next(&newest)) != NULL)
					{
						if (n->definition_index == rt.flood_combat[q].biped && !TEST_FLAG(n->object.damage_flags, _object_dead_bit) &&
							distance_squared(&n->object.position, &p) < 2.0f && newest.index != body)
						{
							made = newest.index;
						}
					}
					if (made != NONE)
						object_set_position(made, &at, &object->object.forward, NULL);
					object_delete(body);
					unit_kill_silent(infection[k]);
					infection[k] = NONE;
					rt.reanimated++;
					if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
						platform_log("h2 ai: an infection form reanimates a dead %s (%ld actors before)", tag_get_name(rt.flood_combat[q].biped),
							before);
				}
				break;
			}
		}
		/* (the iterator's object deleted: start again next time) */
		if (k < infection_count)
			break;
	}
}

/* ---------- the runtime: scripted fliers */

/* (a campaign dropship: Halo 2's Phantom and Pelican, h2_vehicles.c's
"h2\vehicles\phantom" and "...\pelican") */
static boolean vehicle_scripted_flier(long vehicle_index)
{
	struct object_datum *object = object_try_and_get(vehicle_index);
	char const *name, *last;

	if (!object || object->object.type != _object_type_vehicle)
		return FALSE;
	name = tag_get_name(object->definition_index);
	last = name ? strrchr(name, '\\') : NULL;
	return last && (!strcmp(last + 1, "phantom") || !strcmp(last + 1, "pelican"));
}

/* the dropship an actor pilots (NONE: none) */
static long actor_flown_vehicle(long actor_index)
{
	struct actor_datum *actor;
	struct object_datum *unit;
	long vehicle;

	if (!actor_alive(actor_index))
		return NONE;
	actor = actor_get(actor_index);
	if (actor->meta.unit_index == NONE || !(unit = object_try_and_get(actor->meta.unit_index)))
		return NONE;
	vehicle = unit->object.parent_object_index;
	if (vehicle == NONE || !vehicle_scripted_flier(vehicle) || unit_get(vehicle)->unit.driver_object_index != actor->meta.unit_index)
		return NONE;
	return vehicle;
}

/* a vehicle tag's real at an offset of this engine's vehicle (h2_vehicles.c
writes Halo 2's driving there: its maximum forward speed 0x2F8, a tick's;
its acceleration and deceleration 0x300 / 0x304, a tick's a tick) */
static real vehicle_definition_real(long vehicle_index, unsigned long offset, real fallback)
{
	unsigned char const *definition = tag_get('vehi', object_get(vehicle_index)->definition_index);
	real r;

	if (!definition)
		return fallback;
	memcpy(&r, definition + offset, sizeof(r));
	return r > 0.0f && r < 100.0f ? r : fallback;
}

/* Halo 2's dropships fly their command scripts' points (cs_fly_to, cs_fly_by:
the pilot's craft moved there at its tag's top speed - cs_vehicle_speed's
fraction -, accelerating and slowing at its tag's rates, slowing to stop
at a cs_fly_to's point; facing a cs_face point, else the way it goes) and
hover where they stopped. This engine has no pathing in the air for them:
the craft is placed each tick along the straight line (the scripts' points
are Halo 2's own flight paths, its fliers' flight among them is the same
line) */
static void flyers_update(void)
{
	short i, k;

	/* (the dropped troops landed - on the ground a second, or 10 s gone -:
	falling damage theirs again) */
	for (i = 0; i < rt.dropped_count;)
	{
		struct biped_datum *biped = (struct biped_datum *)object_try_and_get(rt.dropped[i].unit);
		long elapsed = game_time_get() - rt.dropped[i].time;

		if (biped && biped->object.type == _object_type_biped && elapsed < 300 &&
			(elapsed < 30 || biped->biped.airborne_ticks > 0))
		{
			/* (down the craft's gravity lift: Halo 2's troops sink out of a
			Phantom's belly, not fall - 3 wu a second at most) */
			if (biped->object.translational_velocity.k < -0.1f)
				biped->object.translational_velocity.k = -0.1f;
			i++;
			continue;
		}
		if (biped && biped->object.type == _object_type_biped)
			SET_FLAG(biped->unit.flags, _unit_no_falling_damage_bit, FALSE);
		rt.dropped[i] = rt.dropped[--rt.dropped_count];
	}

	/* (each piloted dropship given a flier) */
	for (i = 0; i < RT_MAXIMUM_COMMAND_SCRIPTS; i++)
	{
		struct rt_command_script *c = &rt.command_scripts[i];
		long vehicle = c->actor_index != NONE ? actor_flown_vehicle(c->actor_index) : NONE;
		short free_slot = NONE;

		if (vehicle == NONE)
			continue;
		for (k = 0; k < RT_MAXIMUM_FLYERS && !(rt.flyers[k].used && rt.flyers[k].vehicle == vehicle); k++)
		{
			if (!rt.flyers[k].used && free_slot == NONE)
				free_slot = k;
		}
		if (k == RT_MAXIMUM_FLYERS && free_slot != NONE)
		{
			struct rt_flyer *f = &rt.flyers[free_slot];

			memset(f, 0, sizeof(*f));
			f->used = TRUE;
			f->vehicle = vehicle;
			object_get_origin(vehicle, &f->hold);
			f->holding = TRUE;
			rt.flights++;
		}
	}
	for (k = 0; k < RT_MAXIMUM_FLYERS; k++)
	{
		struct rt_flyer *f = &rt.flyers[k];
		struct object_datum *object;
		struct rt_command_script *c = NULL;
		long driver;
		real_point3d origin, target, next;
		real_vector3d forward, up = { 0.0f, 0.0f, 1.0f }, step, desired;
		real distance, top, acceleration, deceleration, wanted;
		boolean moving = FALSE;

		if (!f->used)
			continue;
		object = object_try_and_get(f->vehicle);
		/* (no craft, or a dead one: its own physics - Halo 2's shot-down
		dropship falls as its hull's destroyed state has it) */
		driver = object && object->object.type == _object_type_vehicle ? unit_get(f->vehicle)->unit.driver_object_index : NONE;
		if (!object || TEST_FLAG(object->object.damage_flags, _object_dead_bit))
		{
			/* (its cargo let go with it) */
			h2_ai_vehicle_unload_cargo(f->vehicle, NULL);
			f->used = FALSE;
			continue;
		}
		/* (a living craft whose pilot was killed or got out: it stays in the
		air where it is, hovering on its flight model as Halo 2's dropship
		does with no one at the controls, its cargo let go once. It was let
		go to the stand-in's physics and fell - 5ackwood's Phantom that fell
		on him, Metropolis, Nov 2026) */
		if (driver == NONE)
		{
			if (!f->pilotless)
			{
				h2_ai_vehicle_unload_cargo(f->vehicle, NULL);
				f->pilotless = TRUE;
			}
		}
		else
			f->pilotless = FALSE;
		for (i = 0; i < RT_MAXIMUM_COMMAND_SCRIPTS && !c; i++)
		{
			if (rt.command_scripts[i].actor_index != NONE && actor_alive(rt.command_scripts[i].actor_index) &&
				actor_get(rt.command_scripts[i].actor_index)->meta.unit_index == driver)
			{
				c = &rt.command_scripts[i];
			}
		}
		object_get_origin(f->vehicle, &origin);
		forward = object->object.forward;
		top = vehicle_definition_real(f->vehicle, 0x2F8, 0.25f) * (c && c->speed > 0.0f ? c->speed : 1.0f);
		acceleration = vehicle_definition_real(f->vehicle, 0x300, 0.01f);
		deceleration = vehicle_definition_real(f->vehicle, 0x304, 0.02f);
		if (c && (c->flags & CS_MOVE))
		{
			target = c->move_point;
			f->holding = FALSE;
			moving = TRUE;
		}
		else
		{
			if (!f->holding)
			{
				f->hold = origin;
				f->holding = TRUE;
			}
			target = f->hold;
		}
		step.i = target.x - origin.x, step.j = target.y - origin.y, step.k = target.z - origin.z;
		distance = (real)sqrt(step.i * step.i + step.j * step.j + step.k * step.k);
		/* (its speed: up to the top, down to stop at a point it stops at) */
		wanted = top;
		if (!moving || !(c->flags & CS_MOVE_BY))
			wanted = MIN(wanted, (real)sqrt(2.0f * deceleration * distance));
		if (f->speed < wanted)
			f->speed = MIN(wanted, f->speed + acceleration);
		else
			f->speed = MAX(wanted, f->speed - deceleration);
		/* (Halo 2's human plane, vehicles.cpp function_f2360, in a tick's units:
		its velocity brought to its controls' by (controls' speed / most)^2 x its
		thrust scale 0x280 a second - the craft slides round a corner, which is
		what banks it below) */
		desired = step;
		if (distance > 0.001f)
		{
			real move = MIN(f->speed, distance) / distance;

			desired.i *= move, desired.j *= move, desired.k *= move;
		}
		else
			desired.i = desired.j = desired.k = 0.0f;
		{
			real most = vehicle_definition_real(f->vehicle, 0x2F8, 0.25f);
			real fraction = most > 0.0f ? MIN(1.0f, f->speed / most) : 1.0f;
			real rate = MAX(0.03f, fraction * fraction * vehicle_definition_real(f->vehicle, 0x280, 1.5f) / 30.0f);

			/* (stopping - f2360's friction with no control -: held firmly in
			its braking distance, or it swung about its point) */
			if ((!moving || !(c->flags & CS_MOVE_BY)) && deceleration > 0.0f && distance < 0.75f + f->speed * f->speed / (2.0f * deceleration))
				rate = MAX(rate, 0.2f);

			if (rate > 1.0f || HALO_ENV_ONCE("HALO_H2_FLIER_LEVEL"))
				rate = 1.0f;
			f->velocity.i += (desired.i - f->velocity.i) * rate;
			f->velocity.j += (desired.j - f->velocity.j) * rate;
			f->velocity.k += (desired.k - f->velocity.k) * rate;
		}
		step = f->velocity;
		next.x = origin.x + step.i, next.y = origin.y + step.j, next.z = origin.z + step.k;
		/* (its facing: a cs_face point's, else the way it flies; turning at
		most 90 degrees a second) */
		{
			real_vector3d want = forward;
			real length, current_yaw, wanted_yaw, turn;

			if (c && (c->flags & CS_FACE))
			{
				if (c->look_object != NONE && object_try_and_get(c->look_object))
					object_get_origin(c->look_object, &c->look_point);
				want.i = c->look_point.x - next.x, want.j = c->look_point.y - next.y;
			}
			else if (moving && f->speed > 0.02f)
				want.i = step.i, want.j = step.j;
			length = (real)sqrt(want.i * want.i + want.j * want.j);
			if (length > 0.001f)
			{
				current_yaw = (real)atan2(forward.j, forward.i);
				wanted_yaw = (real)atan2(want.j, want.i);
				turn = wanted_yaw - current_yaw;
				while (turn > (real)M_PI)
					turn -= 2.0f * (real)M_PI;
				while (turn < -(real)M_PI)
					turn += 2.0f * (real)M_PI;
				/* (120 degrees a second at most, 2 x the angle left: f2360's
				rate) */
				turn = MAX(-2.0943952f / 30.0f, MIN(2.0943952f / 30.0f, turn * 2.0f / 30.0f));
				forward.i = (real)cos(current_yaw + turn), forward.j = (real)sin(current_yaw + turn), forward.k = 0.0f;
			}
			else
			{
				length = (real)sqrt(forward.i * forward.i + forward.j * forward.j);
				forward.i = length > 0.001f ? forward.i / length : 1.0f, forward.j = length > 0.001f ? forward.j / length : 0.0f;
				forward.k = 0.0f;
			}
		}
		/* (its attitude, f2360: nose along its climb or dive; banked by the
		slip of its velocity across its facing and by its sideways control, up
		to 90 degrees; turned to at 2 x the angle a second, 120 degrees a second
		at most. Not its Havok body: placed here) */
		if (!HALO_ENV_ONCE("HALO_H2_FLIER_LEVEL"))
		{
			real most = vehicle_definition_real(f->vehicle, 0x2F8, 0.25f);
			real horizontal = (real)sqrt(step.i * step.i + step.j * step.j), pitch = 0.0f;
			real_vector3d flat = forward, side;
			real left_speed, forward_speed, turn_fraction, speed_fraction, slip_scale, slip, bank, c0, s0;
			real_vector3d want_up, want_forward, old_forward = object->object.forward, old_up = object->object.up;
			real slide = 6.0f / 30.0f;
			real blend, angle, dot;

			if (horizontal > 0.25f * (real)fabs(step.k) && horizontal > 0.001f && !(c && (c->flags & CS_FACE)))
				pitch = (real)atan2(step.k, horizontal);
			pitch = MAX(-0.35f, MIN(0.35f, pitch));
			side.i = -flat.j, side.j = flat.i, side.k = 0.0f;
			forward_speed = step.i * flat.i + step.j * flat.j;
			left_speed = step.i * side.i + step.j * side.j;
			speed_fraction = most > 0.0f ? (real)fabs(forward_speed) / most : 0.0f;
			turn_fraction = slide > 0.0f ? left_speed / slide : 0.0f;
			turn_fraction = MAX(-1.0f, MIN(1.0f, turn_fraction));
			slip_scale = MAX(0.0f, speed_fraction - (real)fabs(turn_fraction));
			slip = most > 0.0f ? (step.j * flat.i - flat.j * step.i) / most * slip_scale * 1.5707964f : 0.0f;
			bank = turn_fraction * 1.5707964f * -0.25f;
			if ((real)fabs(bank) > 1.5707964f * 0.075f)
			{
				if (bank < 0.0f ? bank > slip : !(bank > slip))
					bank = slip;
			}
			else if ((real)fabs(slip) > (real)fabs(bank))
				bank = slip;
			bank = MAX(-1.5707964f, MIN(1.5707964f, bank));
			c0 = (real)cos(pitch), s0 = (real)sin(pitch);
			want_forward.i = flat.i * c0, want_forward.j = flat.j * c0, want_forward.k = s0;
			{
				real_vector3d pitched_up = { -flat.i * s0, -flat.j * s0, c0 }, axis;

				axis.i = want_forward.j * pitched_up.k - want_forward.k * pitched_up.j;
				axis.j = want_forward.k * pitched_up.i - want_forward.i * pitched_up.k;
				axis.k = want_forward.i * pitched_up.j - want_forward.j * pitched_up.i;
				want_up.i = (real)cos(bank) * pitched_up.i + (real)sin(bank) * axis.i;
				want_up.j = (real)cos(bank) * pitched_up.j + (real)sin(bank) * axis.j;
				want_up.k = (real)cos(bank) * pitched_up.k + (real)sin(bank) * axis.k;
			}
			dot = old_up.i * want_up.i + old_up.j * want_up.j + old_up.k * want_up.k;
			dot = MAX(-1.0f, MIN(1.0f, dot));
			angle = (real)acos(dot);
			blend = angle > 0.0001f ? MIN(1.0f, MIN(angle * 2.0f / 30.0f, 2.0943952f / 30.0f) / angle) : 1.0f;
			forward.i = want_forward.i, forward.j = want_forward.j, forward.k = old_forward.k + (want_forward.k - old_forward.k) * blend;
			normalize3d(&forward);
			up.i = old_up.i + (want_up.i - old_up.i) * blend;
			up.j = old_up.j + (want_up.j - old_up.j) * blend;
			up.k = old_up.k + (want_up.k - old_up.k) * blend;
			{
				real along = up.i * forward.i + up.j * forward.j + up.k * forward.k;

				up.i -= along * forward.i, up.j -= along * forward.j, up.k -= along * forward.k;
			}
			normalize3d(&up);
		}
		/* (its troops who left it since the last tick - vehicle_unload - come
		down to the ground unhurt, as Halo 2's out of a Phantom's lift: falling
		damage off until they land, their fall slowed) */
		{
			long now[16];
			short now_count = 0, q;
			long child = object->object.first_child_object_index;

			while (child != NONE && now_count < 16)
			{
				struct object_datum *o = object_get(child);

				if (TEST_FLAG(_object_mask_unit, o->object.type) && child != driver)
					now[now_count++] = child;
				child = o->object.next_object_index;
			}
			for (q = 0; q < f->rider_count; q++)
			{
				short n;

				for (n = 0; n < now_count && now[n] != f->riders[q]; n++)
					;
				if (n == now_count && object_try_and_get(f->riders[q]) && rt.dropped_count < (short)NUMBEROF(rt.dropped) &&
					object_get(f->riders[q])->object.type == _object_type_biped)
				{
					SET_FLAG(unit_get(f->riders[q])->unit.flags, _unit_no_falling_damage_bit, TRUE);
					rt.dropped[rt.dropped_count].unit = f->riders[q];
					rt.dropped[rt.dropped_count++].time = game_time_get();
				}
			}
			memcpy(f->riders, now, sizeof(now[0]) * now_count);
			f->rider_count = now_count;
		}
		{
			/* (its cargo held at its cargo seats' markers - their place in
			the craft's frame before the move, put in its frame after it) */
			real_vector3d left = { up.j * forward.k - up.k * forward.j, up.k * forward.i - up.i * forward.k, up.i * forward.j - up.j * forward.i };
			real_vector3d old_forward = object->object.forward, old_up = object->object.up, old_left;
			short q;

			old_left.i = old_up.j * old_forward.k - old_up.k * old_forward.j;
			old_left.j = old_up.k * old_forward.i - old_up.i * old_forward.k;
			old_left.k = old_up.i * old_forward.j - old_up.j * old_forward.i;
			for (q = 0; q < rt.cargo_count; q++)
			{
				struct object_marker marker;
				real_vector3d d;
				real lx, ly, lz;
				real_point3d at;
				struct object_datum *cargo;

				if (rt.cargo[q].carrier != f->vehicle || !(cargo = object_try_and_get(rt.cargo[q].cargo)))
					continue;
				/* (a marker the model lacks: the craft's origin, under it) */
				if (object_get_marker_by_name(f->vehicle, rt.cargo[q].marker, &marker, 1) < 1)
				{
					marker.matrix.position = origin;
					marker.matrix.position.z -= 1.0f;
					if (!rt.cargo_marker_missing++ && HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
						platform_log("h2 ai: cargo seat %s: no marker '%s' on its carrier", rt.cargo[q].label, rt.cargo[q].marker);
				}
				d.i = marker.matrix.position.x - origin.x, d.j = marker.matrix.position.y - origin.y, d.k = marker.matrix.position.z - origin.z;
				lx = d.i * old_forward.i + d.j * old_forward.j + d.k * old_forward.k;
				ly = d.i * old_left.i + d.j * old_left.j + d.k * old_left.k;
				lz = d.i * old_up.i + d.j * old_up.j + d.k * old_up.k;
				at.x = next.x + lx * forward.i + ly * left.i + lz * up.i;
				at.y = next.y + lx * forward.j + ly * left.j + lz * up.j;
				at.z = next.z + lx * forward.k + ly * left.k + lz * up.k;
				object_set_position(rt.cargo[q].cargo, &at, &forward, &up);
				cargo->object.translational_velocity = step;
				cargo->object.angular_velocity.i = cargo->object.angular_velocity.j = cargo->object.angular_velocity.k = 0.0f;
			}
		}
		object_set_position(f->vehicle, &next, &forward, &up);
		if (HALO_ENV_ONCE("HALO_H2_FLIER_LOG"))
			platform_log("h2 flier: tick %ld %s at %.2f %.2f %.2f speed %.3f forward %.3f %.3f %.3f up %.3f %.3f %.3f", (long)game_time_get(),
				tag_get_name(object->definition_index), next.x, next.y, next.z, f->speed, forward.i, forward.j, forward.k, up.i, up.j, up.k);
		if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG") && game_time_get() % 60 == 0)
		{
			real_point3d seat = { 0.0f, 0.0f, 0.0f };
			/* (a dropship whose pilot is gone - killed, it hovers - has none) */
			boolean piloted = driver != NONE && object_try_and_get(driver) != NULL;

			if (piloted)
				object_get_origin(driver, &seat);
			platform_log("h2 ai: flier %s at %.2f %.2f %.2f facing %.2f %.2f, speed %.3f (top %.3f), %s; %s %.2f %.2f %.2f; %d riders, %d cargo",
				tag_get_name(object->definition_index), next.x, next.y, next.z, forward.i, forward.j, f->speed, top,
				moving ? "flying" : "hovering", piloted ? "its pilot at" : "no pilot", seat.x, seat.y, seat.z, f->rider_count, rt.cargo_count);
			{
				struct object_iterator it;
				struct object_datum *o;
				long bolts = 0, other = 0;

				object_iterator_new(&it, FLAG(_object_type_projectile), 0);
				while ((o = object_iterator_next(&it)) != NULL)
				{
					if (strstr(tag_get_name(o->definition_index), "phantom"))
						bolts++;
					else
						other++;
				}
				platform_log("h2 ai:   fliers' trigger ticks so far %ld; projectiles now: %ld of a dropship's, %ld others", rt.flier_shots,
					bolts, other);
			}
		}
		object->object.translational_velocity = step;
		object->object.angular_velocity.i = object->object.angular_velocity.j = object->object.angular_velocity.k = 0.0f;
	}
}

/* a revert to a checkpoint (game_state.c, through h2_scripts.c, as soon as
the checkpoint is read - before the level's scripts run again that tick):
the game's state is the checkpoint's - its actors and threads - so what was
kept here of the later one goes */
H2_ALIGN void h2_ai_game_state_reverted(
	void)
{
	short i;

	if (!rt.loaded)
		return;
	rt.reverts++;
	for (i = 0; i < rt.scene_table_count; i++)
		rt.scene_table[i].fired = FALSE;
	for (i = 0; i < rt.squad_count; i++)
	{
		rt.squads[i].living_seen = 0;
		rt.squads[i].order = NONE;
		/* (counted again from the checkpoint's living actors: its dead are not
		known here - Halo 2 keeps the count in its game state) */
		rt.squads[i].actor_count = 0;
	}
	for (i = 0; i < RT_MAXIMUM_ACTORS; i++)
		rt.member_actor[i] = NONE, rt.member_squad[i] = NONE;
	for (i = 0; i < RT_MAXIMUM_COMMAND_SCRIPTS; i++)
	{
		memset(&rt.command_scripts[i], 0, sizeof(rt.command_scripts[i]));
		rt.command_scripts[i].actor_index = NONE;
		rt.command_scripts[i].thread_index = NONE;
	}
	for (i = 0; i < RT_MAXIMUM_ACTORS; i++)
		rt.actor_location[i] = NONE;
	for (i = 0; i < RT_MAXIMUM_FLYERS; i++)
		rt.flyers[i].used = FALSE;
	rt.dropped_count = 0;
	rt.cargo_count = 0;
	rt.watched_count = 0;
	rt.last_report_time = 0;
	platform_log("h2 ai: the game reverted to tick %ld: the AI's own state let go", game_time_get());
	rt.last_update_time = game_time_get();
}

/* berserk (halo2-decompiled unknown_1a9b50.cpp): Halo 2 has four triggers.
Ported: the charge's (0x1aa990) - an actor whose character's charge has
"berserk whenever charge" (flags bit 1) goes berserk as its charge starts
(this engine's charge action begun). STAND-IN until the decomp has them:
the last of its type in its clump (0x1a9bc0), the +0x358/+0x35e condition
with its 50% flag (0x1a9c50) and behaviour 0x42 (0x1a9b50) are
behaviour-tree entries whose activation is not decompiled; this engine's
own berserk rules (damage, proximity and leader stimuli, stalking) stay on
for Halo 2's actors in their place, and brutes_last_man_update stands in
for 0x1a9bc0's Brutes */
/* the squads' actor counts kept as Halo 2 keeps them (rt_squad.actor_count) */
static void squad_members_update(void)
{
	struct data_iterator iterator;

	data_iterator_new(&iterator, actor_data);
	while (data_iterator_next(&iterator))
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		short squad = actor_h2_squad(iterator.datum_index);

		/* (Halo 2's actor +0x3e: the BSP loaded when it was made, ai.cpp
		0x1dfb90 - HALO_H2_ACTOR_ACTIVATION, h2_ai_actors_activation_by_location) */
		if (absolute < RT_MAXIMUM_ACTORS && rt.actor_bsp_datum[absolute] != iterator.datum_index)
		{
			rt.actor_bsp_datum[absolute] = iterator.datum_index;
			rt.actor_bsp[absolute] = global_structure_bsp_index;
		}
		/* (a dead one: actor_h2_squad has none - it stays counted where it was) */
		if (absolute >= RT_MAXIMUM_ACTORS || squad == NONE)
			continue;
		if (rt.member_actor[absolute] == iterator.datum_index && rt.member_squad[absolute] == squad)
			continue;
		if (rt.member_actor[absolute] == iterator.datum_index && rt.member_squad[absolute] != NONE &&
			rt.squads[rt.member_squad[absolute]].actor_count > 0)
		{
			rt.squads[rt.member_squad[absolute]].actor_count--;
		}
		rt.member_actor[absolute] = iterator.datum_index;
		rt.member_squad[absolute] = squad;
		rt.squads[squad].actor_count++;
	}
}

H2_ALIGN static void h2_ai_berserk_update(void)
{
	struct data_iterator iterator;
	struct actor_datum *actor;

	data_iterator_new(&iterator, actor_data);
	while ((actor = (struct actor_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		boolean charging = actor->state.action == _actor_action_charge;

		if (absolute >= RT_MAXIMUM_ACTORS)
			continue;
		if (charging && !rt.actor_charging[absolute] && !actor->emotions.berserk && actor->meta.definition_index != NONE &&
			TEST_FLAG(actor_definition_get(actor->meta.definition_index)->flags, H2_ACTOR_BERSERK_WHENEVER_CHARGE_BIT))
		{
			actor_berserk(iterator.datum_index, TRUE);
			rt.charge_berserks++;
			if (HALO_ENV_ONCE("HALO_H2_AI_INDEX_LOG"))
				platform_log("h2 ai: berserk: actor %lx charges, its character berserks whenever it charges (0x1aa990)",
					(unsigned long)iterator.datum_index);
		}
		rt.actor_charging[absolute] = charging;
	}
}

/* (HALO_H2_ACTIVATION_LOG=1, debugging Halo 2's actor activation) an active
actor's unit falling fast, said once an actor: its encounter's BSP, the
loaded one, its cluster, its last pathfinding surface */
static void activation_watch(void)
{
	static boolean said[RT_MAXIMUM_ACTORS];
	static long said_level = -1;
	struct data_iterator iterator;
	struct actor_datum *actor;
	long said_count = 0;

	if (!HALO_ENV_ONCE("HALO_H2_ACTIVATION_LOG"))
		return;
	if (game_time_get() < said_level || said_level < 0)
		memset(said, 0, sizeof(said));
	said_level = game_time_get();
	data_iterator_new(&iterator, actor_data);
	while ((actor = (struct actor_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		struct object_datum *unit;
		short encounter_bsp = NONE;

		if (actor->meta.unit_index == NONE || absolute >= RT_MAXIMUM_ACTORS || said[absolute] || said_count > 4)
			continue;
		unit = object_get(actor->meta.unit_index);
		if (unit->object.parent_object_index != NONE || unit->object.translational_velocity.k > -0.2f)
			continue;
		said[absolute] = TRUE;
		said_count++;
		if (actor->meta.encounter_index != NONE)
			encounter_bsp = TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->ai_encounters,
				DATUM_INDEX_TO_ABSOLUTE_INDEX(actor->meta.encounter_index), struct encounter_definition)->runtime_structure_bsp_reference_index;
		platform_log("h2 ai activation: actor %lx (%s, encounter %s, its BSP %d, loaded %d) falling %.2f/tick at %.2f %.2f %.2f: cluster %d, "
			"pathfinding surface %ld, dormant %d, active %d, mode %d", (unsigned long)iterator.datum_index, tag_get_name(unit->definition_index),
			actor->meta.encounter_index != NONE ? TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->ai_encounters,
				DATUM_INDEX_TO_ABSOLUTE_INDEX(actor->meta.encounter_index), struct encounter_definition)->name : "none",
			(int)encounter_bsp, (int)global_structure_bsp_index, unit->object.translational_velocity.k, unit->object.position.x,
			unit->object.position.y, unit->object.position.z, (int)unit->object.location.cluster_index, actor->input.pathfinding_surface_index,
			(int)actor->meta.dormant, (int)actor->meta.active, (int)actor->state.mode);
	}
}

H2_ALIGN void h2_ai_update(
	void)
{
	short i;
	boolean dialogue_held = FALSE;

	bsp_pending_update();
	if (!rt.loaded || !table_build())
		return;
	if (!rt.flood_ready)
		flood_ready();
	deck_carry();
	/* (a revert this side did not hear of: let go now) */
	if (game_time_get() < rt.last_update_time)
		h2_ai_game_state_reverted();
	rt.last_update_time = game_time_get();
	activation_watch();
	/* (tests) HALO_H2_AI_TEST_PLACE="tick:squad,squad;tick:...": those squads
	placed as ai_place places them, at that tick of the level */
	if (HALO_ENV_ONCE("HALO_H2_AI_TEST_PLACE"))
	{
		char const *at = HALO_ENV_ONCE("HALO_H2_AI_TEST_PLACE");

		while (*at)
		{
			long tick = atol(at);
			char const *colon = strchr(at, ':'), *end = strchr(at, ';');

			if (!end)
				end = at + strlen(at);
			if (colon && colon < end && tick == game_time_get())
			{
				char const *name = colon + 1;

				while (name < end)
				{
					char const *comma = memchr(name, ',', (size_t)(end - name));
					size_t length = (size_t)((comma ? comma : end) - name);

					char const *arrow = memchr(name, '>', length);

					/* ("riders>carrier": ai_place_in_vehicle's) */
					if (arrow)
					{
						short rider = NONE, carrier = NONE;

						for (i = 0; i < rt.squad_count; i++)
						{
							if (strlen(rt.squads[i].name) == (size_t)(arrow - name) && !strncmp(rt.squads[i].name, name, (size_t)(arrow - name)))
								rider = i;
							if (strlen(rt.squads[i].name) == length - (size_t)(arrow - name) - 1 &&
								!strncmp(rt.squads[i].name, arrow + 1, length - (size_t)(arrow - name) - 1))
							{
								carrier = i;
							}
						}
						if (rider != NONE && carrier != NONE)
							place_in_vehicle(rider, carrier);
					}
					for (i = 0; !arrow && i < rt.squad_count; i++)
					{
						if (strlen(rt.squads[i].name) == length && !strncmp(rt.squads[i].name, name, length))
							squad_place(i, NONE, NONE);
					}
					name += length + (comma ? 1 : 0);
				}
			}
			at = *end ? end + 1 : end;
		}
	}
	/* (tests) HALO_H2_AI_TEST_GOTO="tick:squad": at that tick the first
	player's unit put 4 wu from the squad's first living actor, facing it */
	if (HALO_ENV_ONCE("HALO_H2_AI_TEST_GOTO") && atol(HALO_ENV_ONCE("HALO_H2_AI_TEST_GOTO")) == game_time_get())
	{
		char const *name = strchr(HALO_ENV_ONCE("HALO_H2_AI_TEST_GOTO"), ':');
		long players[4];
		short q;

		for (q = 0; name && q < rt.squad_count; q++)
		{
			long actors[RT_MAXIMUM_ACTORS];
			short n;

			if (strcmp(rt.squads[q].name, name + 1) || player_units(players, 4) < 1)
				continue;
			n = actors_of(q, actors, RT_MAXIMUM_ACTORS);
			if (n > 0 && actor_alive(actors[0]))
			{
				real_point3d at;
				real_vector3d facing = { -1.0f, 0.0f, 0.0f };

				object_get_origin(actor_get(actors[0])->meta.unit_index, &at);
				at.x += 4.0f;
				at.z += 0.3f;
				object_set_position(players[0], &at, &facing, NULL);
				platform_log("h2 ai: test goto: the player put by %s at %.1f %.1f %.1f", name + 1, at.x, at.y, at.z);
			}
			break;
		}
	}
	/* (tests) HALO_H2_AI_TEST_KILL="tick:squad;tick:squad": those squads
	killed (ai_kill) */
	if (HALO_ENV_ONCE("HALO_H2_AI_TEST_KILL"))
	{
		char const *at = HALO_ENV_ONCE("HALO_H2_AI_TEST_KILL");

		while (*at)
		{
			char const *colon = strchr(at, ':'), *end = strchr(at, ';');
			size_t length;
			short q;

			if (!end)
				end = at + strlen(at);
			length = colon && colon < end ? (size_t)(end - colon - 1) : 0;
			for (q = 0; length && atol(at) == game_time_get() && q < rt.squad_count; q++)
			{
				if (strlen(rt.squads[q].name) == length && !strncmp(rt.squads[q].name, colon + 1, length) && squad_reference(q) != NONE)
				{
					ai_scripting_kill(squad_reference(q));
					platform_log("h2 ai: test kill: %s (%d watched)", rt.squads[q].name, rt.watched_count);
				}
			}
			at = *end ? end + 1 : end;
		}
	}
	clumps_update();
	if (game_time_get() % 5 == 0)
		kills_update();
	/* (tests) HALO_H2_AI_WATCH="name,name": each named object's index and
	(a device's) position, every second */
	if (HALO_ENV_ONCE("HALO_H2_AI_WATCH") && game_time_get() % 30 == 0)
	{
		char names[256], *name, *rest;

		snprintf(names, sizeof(names), "%s", HALO_ENV_ONCE("HALO_H2_AI_WATCH"));
		for (name = strtok_r(names, ",", &rest); name; name = strtok_r(NULL, ",", &rest))
		{
			short name_index = scenario_object_name_index_from_string(global_scenario_get(), name);
			long object_index = name_index != NONE ? object_index_from_name_index(name_index) : NONE;
			struct object_datum *object = object_index != NONE ? object_try_and_get(object_index) : NULL;

			struct scenario_object_name *entry = name_index != NONE ? TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->object_names,
				name_index, struct scenario_object_name) : NULL;

			platform_log("h2 ai: watch %s: name %d (type %d placement %d) object %lx%s position %.3f", name, name_index,
				entry ? entry->runtime_object_type : -2, entry ? entry->runtime_scenario_datum_index : -2, object_index,
				object ? "" : " (none)", object && TEST_FLAG(_object_mask_device, object->object.type) ? device_get_position(object_index) : -1.0f);
			if (object)
			{
				real_point3d at;

				object_get_origin(object_index, &at);
				platform_log("h2 ai:   tick %ld at %.2f %.2f %.2f, %s, cluster %d, parent %lx", game_time_get(), at.x, at.y, at.z,
					h2_scripts_object_hidden(object_index) ? "hidden" : "shown", object->object.location.cluster_index, object->object.parent_object_index);
			}
			if (!object && entry && entry->runtime_object_type == 7)
			{
				long element_size;
				struct tag_block *datums = scenario_get_object_type_scenario_datums(global_scenario_get(), 7, &element_size);
				struct scenario_object_datum const *placement = (struct scenario_object_datum const *)((char const *)
					datums->address + element_size * entry->runtime_scenario_datum_index);
				struct tag_reference const *reference = (struct tag_reference const *)((char const *)
					global_scenario_get()->machine_palette.address + 48 * placement->palette_entry_index);
				struct location location;

				scenario_location_from_point(&location, &placement->position);
				platform_log("h2 ai:   placement palette %d (tag %lx %s) at %.1f %.1f %.1f cluster %d flags %x", placement->palette_entry_index,
					reference->index, reference->index != NONE ? tag_get_name(reference->index) : "", placement->position.x,
					placement->position.y, placement->position.z, location.cluster_index, placement->placement_flags);
			}
		}
	}
	/* (tests) HALO_H2_AI_TEST_CAMERA=ticks: every so many ticks the debug
	camera put on a fighting actor (behind and above it, looking at it) and
	the next frame saved (shot_ai_<tick>), the AI's screenshots */
	if (HALO_ENV_ONCE("HALO_H2_AI_TEST_CAMERA") && atol(HALO_ENV_ONCE("HALO_H2_AI_TEST_CAMERA")) > 0 &&
		game_time_get() % atol(HALO_ENV_ONCE("HALO_H2_AI_TEST_CAMERA")) == 0)
	{
		extern char halo_screenshot_name[64];
		void director_load_camera(void);
		struct data_iterator iterator;
		struct actor_datum *actor, *best = NULL;
		long best_index = NONE;

		/* (HALO_H2_AI_TEST_CAMERA_SQUAD=name: that squad's first living
		actor, fighting or not) */
		if (HALO_ENV_ONCE("HALO_H2_AI_TEST_CAMERA_SQUAD"))
		{
			short q;

			for (q = 0; q < rt.squad_count && !best; q++)
			{
				long actors[RT_MAXIMUM_ACTORS];
				short n, k;

				if (strcmp(rt.squads[q].name, HALO_ENV_ONCE("HALO_H2_AI_TEST_CAMERA_SQUAD")))
					continue;
				n = actors_of(q, actors, RT_MAXIMUM_ACTORS);
				for (k = 0; k < n && !best; k++)
				{
					if (actor_alive(actors[k]))
						best = actor_get(actors[k]), best_index = actors[k];
				}
			}
		}
		data_iterator_new(&iterator, actor_data);
		while (!HALO_ENV_ONCE("HALO_H2_AI_TEST_CAMERA_SQUAD") && (actor = (struct actor_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (!actor_alive(iterator.datum_index) || actor->state.action != _actor_action_fight)
				continue;
			if (!best || (actor->control.current_fire_target_visible && !best->control.current_fire_target_visible))
				best = actor, best_index = iterator.datum_index;
		}
		if (best)
		{
			real_point3d at, target;
			real_vector3d forward;
			real yaw, length;
			FILE *file;

			/* (one in a vehicle: from further off and higher, the vehicle seen
			whole) */
			long parent = object_get(best->meta.unit_index)->object.parent_object_index;
			real far = parent != NONE ? MAX(2.5f, object_get(parent)->object.bounding_sphere_radius * 0.6f) : 1.0f;

			/* (HALO_H2_AI_TEST_CAMERA_FAR=scale: nearer or further) */
			if (HALO_ENV_ONCE("HALO_H2_AI_TEST_CAMERA_FAR"))
				far *= (real)atof(HALO_ENV_ONCE("HALO_H2_AI_TEST_CAMERA_FAR"));

			/* (a vehicle's: the vehicle framed, not its rider's seat) */
			object_get_origin(parent != NONE ? parent : best->meta.unit_index, &target);
			at = target;
			forward = parent != NONE ? object_get(parent)->object.forward : best->input.looking_vector;
			length = sqrtf(forward.i * forward.i + forward.j * forward.j);
			if (length < 0.1f)
				forward.i = 1.0f, forward.j = 0.0f, length = 1.0f;
			forward.i /= length, forward.j /= length;
			{
				/* (its target's unit, when it has one: the camera to the side of
				the two, looking between them) */
				struct prop_datum *prop = parent == NONE && best->control.current_fire_target_type != 0 &&
					best->control.current_fire_target_prop_index != NONE ?
					(struct prop_datum *)datum_try_and_get(prop_data, best->control.current_fire_target_prop_index) : NULL;
				real_point3d other;

				if (prop && prop->unit_index != NONE && object_try_and_get(prop->unit_index))
				{
					real dx, dy, separation;

					object_get_origin(prop->unit_index, &other);
					dx = other.x - target.x, dy = other.y - target.y;
					separation = sqrtf(dx * dx + dy * dy);
					if (separation > 0.5f && separation < 40.0f)
					{
						target.x = 0.5f * (target.x + other.x);
						target.y = 0.5f * (target.y + other.y);
						target.z = 0.5f * (target.z + other.z);
						forward.i = dx / separation, forward.j = dy / separation;
						at.x = target.x - forward.j * (0.6f * separation + 3.0f * far) - forward.i * 1.5f * far;
						at.y = target.y + forward.i * (0.6f * separation + 3.0f * far) - forward.j * 1.5f * far;
						at.z = target.z + 2.0f * far;
						target.z += 0.4f;
						goto aimed;
					}
				}
			}
			/* (else behind its shoulder, looking at it) */
			at.x -= far * (3.0f * forward.i - 1.0f * forward.j);
			at.y -= far * (3.0f * forward.j + 1.0f * forward.i);
			at.z += 1.5f * far;
			target.z += 0.4f;
		aimed:
			yaw = atan2f(target.y - at.y, target.x - at.x);
			{
				real dx = target.x - at.x, dy = target.y - at.y, dz = target.z - at.z;
				real pitch = atan2f(dz, sqrtf(dx * dx + dy * dy));

				/* (a place with a clear view of the target point: the first of
				a few round it) */
				{
					static real const turns[] = { 0.0f, 3.14159265f, 1.5707963f, -1.5707963f, 0.785f, -0.785f, 2.356f, -2.356f };
					real radius = sqrtf((at.x - target.x) * (at.x - target.x) + (at.y - target.y) * (at.y - target.y));
					real base = atan2f(at.y - target.y, at.x - target.x), height = at.z - target.z;
					short t;

					for (t = 0; t < (short)NUMBEROF(turns); t++)
					{
						real_point3d candidate;
						real_vector3d to;
						struct collision_result collision;
						struct location location;

						candidate.x = target.x + radius * cosf(base + turns[t]);
						candidate.y = target.y + radius * sinf(base + turns[t]);
						candidate.z = target.z + height;
						scenario_location_from_point(&location, &candidate);
						to.i = target.x - candidate.x, to.j = target.y - candidate.y, to.k = target.z - candidate.z;
						if (location.cluster_index != NONE && !collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit) |
							FLAG(_collision_test_back_facing_surfaces_bit) | FLAG(_collision_test_structure_bit), &candidate, &to, NONE,
							&collision))
						{
							at = candidate;
							break;
						}
					}
					yaw = atan2f(target.y - at.y, target.x - at.x);
					dx = target.x - at.x, dy = target.y - at.y, dz = target.z - at.z;
					pitch = atan2f(dz, sqrtf(dx * dx + dy * dy));
				}
				if ((file = fopen("d:\\camera.txt", "w")) != NULL)
				{
					fprintf(file, "%f %f %f\n%f %f %f\n%f %f %f\n%f\n", at.x, at.y, at.z, cosf(yaw) * cosf(pitch), sinf(yaw) * cosf(pitch),
						sinf(pitch), -cosf(yaw) * sinf(pitch), -sinf(yaw) * sinf(pitch), cosf(pitch), 70.0f * 3.14159265f / 180.0f);
					fclose(file);
					director_load_camera();
					snprintf(halo_screenshot_name, sizeof(halo_screenshot_name), "shot_ai_%ld", game_time_get());
					platform_log("h2 ai: test camera on actor %lx at %.1f %.1f %.1f", best_index, at.x, at.y, at.z);
				}
			}
		}
	}
	/* (HALO_H2_COUNT_LOG=prefix,prefix...: each second, the squads so named
	together - living, fighting as ai_fighting_count counts, the highest
	combat status - the H2EK reference's counts.tsv side by side) */
	if (game_time_get() % 30 == 0 && HALO_ENV_ONCE("HALO_H2_COUNT_LOG"))
	{
		char list[256], *prefix, *save = NULL;

		snprintf(list, sizeof(list), "%s", HALO_ENV_ONCE("HALO_H2_COUNT_LOG"));
		for (prefix = strtok_r(list, ",", &save); prefix; prefix = strtok_r(NULL, ",", &save))
		{
			short living = 0, fighting = 0, status = -1;

			for (i = 0; i < rt.squad_count; i++)
			{
				long reference = squad_reference(i);
				struct ai_script_actor_reference_iterator iterator;
				struct actor_datum *actor;

				if (strncmp(rt.squads[i].name, prefix, strlen(prefix)) || reference == NONE)
					continue;
				living += ai_scripting_living_count(reference);
				ai_index_actor_iterator_new(reference, &iterator);
				while ((actor = ai_index_actor_iterator_next(&iterator)) != NULL)
				{
					struct object_datum *unit;

					if (!actor_alive(iterator.actor_index))
						continue;
					fighting += actor_fighting(iterator.actor_index);
					status = MAX(status, actor_combat_status(iterator.actor_index));
					/* (each one's body and shield, as the reference samples them) */
					if (game_time_get() % 60 == 0 && (unit = object_try_and_get(actor->meta.unit_index)) != NULL)
					{
						/* (and the nearest living enemy actor's distance, the reference's
						objects_distance_to_object of the other side's squads) */
						struct data_iterator others;
						struct actor_datum *other;
						real_point3d here, there;
						real nearest = -1.0f;

						object_get_origin(actor->meta.unit_index, &here);
						data_iterator_new(&others, actor_data);
						while ((other = (struct actor_datum *)data_iterator_next(&others)) != NULL)
						{
							if (!actor_alive(others.datum_index) || other->meta.unit_index == NONE ||
								!game_team_is_enemy(actor->meta.team_index, other->meta.team_index) || !object_try_and_get(other->meta.unit_index))
							{
								continue;
							}
							object_get_origin(other->meta.unit_index, &there);
							if (nearest < 0.0f || distance3d(&here, &there) < nearest)
								nearest = distance3d(&here, &there);
						}
						platform_log("h2 ai vitals: %ld %s actor %lx body %.1f/%.0f shield %.1f/%.0f nearest enemy %.1f at %.2f %.2f %.2f", game_time_get(),
							rt.squads[i].name, (unsigned long)iterator.actor_index, unit->object.body_vitality * unit->object.maximum_body_vitality,
							unit->object.maximum_body_vitality, unit->object.shield_vitality * unit->object.maximum_shield_vitality,
							unit->object.maximum_shield_vitality, nearest, here.x, here.y, here.z);
					}
				}
			}
			platform_log("h2 ai counts: %ld %s living %d fighting %d status %d", game_time_get(), prefix, (int)living, (int)fighting, (int)status);
		}
	}
	/* (the log: each squad's deaths as they happen, and now and then the
	actors living and fighting) */
	if (game_time_get() % 30 == 0)
	{
		long living_total = 0, fighting = 0;

		for (i = 0; i < rt.squad_count; i++)
		{
			struct rt_squad *s = &rt.squads[i];
			long reference = squad_reference(i);
			short living;

			if (!s->placed || reference == NONE)
				continue;
			living = ai_scripting_living_count(reference);
			if (living < s->living_seen)
			{
				rt.deaths += s->living_seen - living;
				platform_log("h2 ai: %s: %d died (%d of %d left)", s->name, s->living_seen - living, living, s->placed);
			}
			s->living_seen = living;
			living_total += living;
		}
		{
			struct data_iterator iterator;
			struct actor_datum *actor;

			data_iterator_new(&iterator, actor_data);
			while ((actor = (struct actor_datum *)data_iterator_next(&iterator)) != NULL)
				fighting += actor->state.action == _actor_action_fight;
		}
		if (game_time_get() - rt.last_report_time >= 30 * 10 && (living_total || rt.deaths))
		{
			rt.last_report_time = game_time_get();
			platform_log("h2 ai: %ld carriers burst", rt.carriers_burst);
		platform_log("h2 ai: %ld actors living, %ld fighting, %ld died so far (%ld killed by players, %ld by the AI; players killed %ld); "
				"%ld command scripts run (%ld placement scripts; cs_go_to %ld there, %ld given up), %ld initial orders, %ld orders given (ai_set_orders), %ld ended; vehicles %ld (%ld riders); "
				"lines %ld spoken, %ld not heard", living_total, fighting, rt.deaths,
				rt.kills_by_player, rt.kills_by_ai, rt.player_deaths, rt.command_scripts_run, rt.placement_scripts, rt.go_to_arrived, rt.go_to_timed_out,
				rt.initial_orders, rt.orders_set, rt.orders_ended, rt.vehicles_made, rt.vehicles_boarded, rt_dialogue.played, rt_dialogue.not_played);
			if (HALO_ENV_ONCE("HALO_H2_AI_DEBUG"))
			{
				struct data_iterator iterator;
				struct actor_datum *actor;

				data_iterator_new(&iterator, actor_data);
				while ((actor = (struct actor_datum *)data_iterator_next(&iterator)) != NULL)
				{
					real_point3d origin = { 0.0f, 0.0f, 0.0f };
					struct encounter_datum *encounter = actor->meta.encounter_index != NONE ?
						encounter_get(actor->meta.encounter_index) : NULL;

					if (actor->meta.unit_index != NONE && object_try_and_get(actor->meta.unit_index))
						object_get_origin(actor->meta.unit_index, &origin);
					platform_log("h2 ai:   actor cluster %d pathfinding surface %ld destination type %d at destination %d moving %d; "
						"guard location %d (new %d, wait %d), firing position %d",
						actor->meta.unit_index != NONE && object_try_and_get(actor->meta.unit_index) ?
						object_get(actor->meta.unit_index)->object.location.cluster_index : -2, actor->input.pathfinding_surface_index,
						actor->control.path.destination_orders.destination_type, actor->control.path.at_destination, actor->control.moving,
						actor->state.action == _actor_action_guard ? actor->state.action_data.guard.guard_location_type : -1,
						actor->state.action == _actor_action_guard ? actor->state.action_data.guard.find_new_guard_position : -1,
						actor->state.action == _actor_action_guard ? actor->state.action_data.guard.wait_ticks : -1,
						actor->firing_positions.current_position_index);
					if (actor->meta.unit_index != NONE && object_try_and_get(actor->meta.unit_index))
					{
						/* what is underfoot: the structure, and anything */
						real_point3d from = origin;
						real_vector3d down = { 0.0f, 0.0f, -20.0f };
						struct collision_result structure, any;
						boolean hit_structure, hit_any;

						from.z += 0.4f;
						hit_structure = collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit),
							&from, &down, actor->meta.unit_index, &structure);
						hit_any = collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit) |
							FLAG(_collision_test_objects_bit) | _collision_test_objects_sight_blocking_flags,
							&from, &down, actor->meta.unit_index, &any);
						platform_log("h2 ai:     ground: structure %s %.2f m (surface %ld), anything %.2f m (type %d %s)",
							hit_structure ? "at" : "none within", hit_structure ? structure.t * 20.0f : 20.0f,
							hit_structure ? structure.surface_index : NONE, hit_any ? any.t * 20.0f : 20.0f, hit_any ? any.type : -1,
							hit_any && any.type == _collision_result_object && object_try_and_get(any.object_index) ?
								tag_get_name(object_get(any.object_index)->definition_index) : "");
					}
					platform_log("h2 ai:   actor %lx type %d team %d action %d active %d dormant %d frozen %d encounter %lx (active %d) "
						"at %.1f %.1f %.1f target %d props %lx enemies %d vis %d", iterator.datum_index, actor->meta.type, actor->meta.team_index, actor->state.action,
						actor->meta.active, actor->meta.dormant, actor->meta.frozen, actor->meta.encounter_index, encounter ? encounter->active : -1,
						origin.x, origin.y, origin.z, actor->target.target_type, actor->meta.first_prop_index, (int)actor->situation.known_enemies, (int)actor->situation.visible_reachable_enemies);
					if (actor->meta.unit_index != NONE && object_try_and_get(actor->meta.unit_index))
					{
						struct unit_datum *u = unit_get(actor->meta.unit_index);
						short w = u->unit.current_weapon_index;
						long weapon = w >= 0 && w < MAXIMUM_WEAPONS_PER_UNIT ? u->unit.weapon_object_indices[w] : NONE;

						{
							struct prop_datum *prop = actor->control.current_fire_target_type != 0 &&
								actor->control.current_fire_target_prop_index != NONE ?
								(struct prop_datum *)datum_try_and_get(prop_data, actor->control.current_fire_target_prop_index) : NULL;
							char text[64];

							describe_object(prop ? prop->unit_index : NONE, text, sizeof(text));
							platform_log("h2 ai:     shooting at %s", text);
						}
						platform_log("h2 ai:     fire state %d range %.1f target type %d visible %d, controls %lx, trigger %.2f, weapon %s, body %.2f shield %.2f",
							actor->control.fire_state, actor->control.weapon_maximum_range, actor->control.current_fire_target_type,
							actor->control.current_fire_target_visible, actor->output.control_flags, actor->output.analog_primary_trigger,
							weapon != NONE && object_try_and_get(weapon) ? tag_get_name(object_get(weapon)->definition_index) : "none",
							u->object.body_vitality, u->object.shield_vitality);
						if (actor->input.vehicle_index != NONE && object_try_and_get(actor->input.vehicle_index))
						{
							struct unit_datum *v = unit_get(actor->input.vehicle_index);

							platform_log("h2 ai:     in %s: driver type %d, gunner %d (the vehicle's driver %lx gunner %lx, this %lx), its weapon %s",
								tag_get_name(v->definition_index), actor->input.vehicle_driver_type, actor->input.vehicle_gunner,
								v->unit.driver_object_index, v->unit.gunner_object_index, actor->meta.unit_index,
								unit_inventory_get_weapon(actor->input.vehicle_index, (word)v->unit.current_weapon_index) != NONE ?
								tag_get_name(object_get(unit_inventory_get_weapon(actor->input.vehicle_index,
								(word)v->unit.current_weapon_index))->definition_index) : "none");
						}
					}
					{
						long players[4];
						short player_count = player_units(players, 4);

						if (player_count)
						{
							struct location here;
							real_point3d p;

							object_get_origin(players[0], &p);
							scenario_location_from_point(&here, &p);
							platform_log("h2 ai:     player body %.2f shield %.2f cluster %d (at its origin %d, bsp %d, parent %lx, flags %lx)",
								object_get(players[0])->object.body_vitality, object_get(players[0])->object.shield_vitality,
								object_get(players[0])->object.location.cluster_index, here.cluster_index, global_structure_bsp_index,
								object_get(players[0])->object.parent_object_index, object_get(players[0])->object.flags);
						}

						if (player_count && actor->meta.unit_index != NONE)
						{
							real_point3d from = origin, to;
							real_vector3d vector;
							struct collision_result collision;
							boolean hit;

							object_get_origin(players[0], &to);
							from.z += 0.6f;
							to.z += 0.6f;
							vector.i = to.x - from.x, vector.j = to.y - from.y, vector.k = to.z - from.z;
							hit = collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit) |
								FLAG(_collision_test_back_facing_surfaces_bit) | FLAG(_collision_test_structure_bit) |
								FLAG(_collision_test_objects_bit) | _collision_test_objects_sight_blocking_flags |
								FLAG(_collision_test_ignore_two_sided_surfaces_bit), &from, &vector, actor->meta.unit_index, &collision);
							platform_log("h2 ai:     player at %.1f %.1f %.1f: line of sight %s (t %.2f, type %d, object %lx %s, surface %ld, at %.1f %.1f %.1f)",
								to.x, to.y, to.z, hit ? "blocked" : "clear", hit ? collision.t : 1.0f, hit ? collision.type : -1,
								hit ? collision.object_index : NONE,
								hit && collision.type == _collision_result_object && object_try_and_get(collision.object_index) ?
									tag_get_name(object_get(collision.object_index)->definition_index) : "",
								hit ? collision.surface_index : NONE, hit ? collision.point.x : 0.0f,
								hit ? collision.point.y : 0.0f, hit ? collision.point.z : 0.0f);
							/* (a machine in the way: whether it opens by itself) */
							if (hit && collision.type == _collision_result_object && object_try_and_get(collision.object_index) &&
								object_get(collision.object_index)->object.type == _object_type_machine)
							{
								struct machine_datum *machine = (struct machine_datum *)object_get(collision.object_index);

								platform_log("h2 ai:     in the way: machine %lx flags %lx device flags %lx power %.2f position %.2f",
									collision.object_index, machine->machine.flags, machine->device.flags, machine->device.power,
									machine->device.position);
							}
						}
					}
					{
						struct prop_iterator props;
						struct prop_datum *prop;
						short shown = 0;

						prop_iterator_new(&props, iterator.datum_index);
						while ((prop = prop_iterator_next(&props)) != NULL && shown++ < 6)
						{
							platform_log("h2 ai:     prop unit %lx team %d state %d enemy %d vis %d los %d awareness %.2f perception %d",
								prop->unit_index, prop->team_index, prop->state, prop->enemy, prop->visibility, prop->line_of_sight,
								prop->awareness, prop->perception);
						}
					}
				}
			}
		}
	}
	/* ai_disposable's collector (halo2-decompiled unknown_26b230.cpp: the clumps'
	update 0x268510 runs it when the tick is a whole second) */
	squad_members_update();
	/* (HALO_H2_ACTOR_ACTIVATION: each tick, as Halo 2's walk tests +6 each
	tick - an actor whose ground went with a BSP switch sleeps before it
	falls) */
	h2_ai_actors_activation_by_location();
	h2_ai_berserk_update();
	if (ai_globals_ai_active_get())
	{
		disposal_enemy_close_update();
		if (game_time_get() % 30 == 0)
			disposal_update();
	}
	/* the orders' endings: every two seconds, at the tenth tick of the two
	(halo2-decompiled unknown_2003a0.cpp 0x201c80 -> 0x202720); and each
	squad's status - its follow order's areas (0x203360 -> 0x204450) - every
	half second, squad by squad (the squad's index plus the tick) */
	if (game_time_get() % RT_ORDER_TICKS == RT_ORDER_PHASE)
	{
		for (i = 0; i < rt.squad_count; i++)
		{
			if (rt.squads[i].placed && rt.squads[i].living_seen > 0 && rt.squads[i].order != NONE)
			{
				squad_endings_update(i);
				squad_secondary_update(i);
			}
		}
	}
	for (i = 0; i < rt.squad_count; i++)
	{
		if ((i + game_time_get()) % RT_SQUAD_STATUS_TICKS == 0 && rt.squads[i].placed && rt.squads[i].living_seen > 0)
			squad_follow_update(i);
	}
	/* the command scripts: ended with their thread or their actor; one that
	aborts on damage ended when its unit is hurt; those waiting given
	threads */
	for (i = 0; i < RT_MAXIMUM_COMMAND_SCRIPTS; i++)
	{
		struct rt_command_script *c = &rt.command_scripts[i];

		if (c->actor_index == NONE)
			continue;
		if (c->flags & CS_DIALOGUE_SUPPRESSED)
			dialogue_held = TRUE;
		if (!actor_alive(c->actor_index))
		{
			/* (a joint command script goes on with a role left alive) */
			short role;

			for (role = 0; role < c->role_count && (!actor_alive(c->roles[role]) || command_script_of_actor(c->roles[role])); role++)
				;
			if (role < c->role_count)
			{
				c->actor_index = c->roles[role];
				c->flags = 0;
				c->look_object = NONE;
				c->body_vitality = object_get(actor_get(c->actor_index)->meta.unit_index)->object.body_vitality;
				continue;
			}
			command_script_end(c);
			continue;
		}
		/* (cs_start_approach: followed until cs_approach_stop) */
		if (c->approach_object != NONE)
		{
			real_point3d origin, point;

			if (command_script_unit_origin(c, &origin) && approach_point(c->approach_object, &origin, &point))
			{
				real distance = (real)sqrt(distance_squared(&origin, &point));

				if (distance <= c->approach_distance || (c->approach_maximum > 0.0f && distance > c->approach_maximum))
					c->flags &= ~CS_MOVE;
				else
				{
					c->flags |= CS_MOVE;
					c->flags &= ~CS_MOVE_BY;
					c->move_point = point;
					if (game_time_get() % 15 == 0 || c->move_surface == NONE)
					{
						short cluster;

						point_resolve(&c->move_point, &cluster, &c->move_surface);
					}
				}
			}
		}
		if (c->thread_index != NONE && !hs_runtime_thread_alive(c->thread_index))
		{
			command_script_returned(c);
			continue;
		}
		/* (HALO_H2_CS_IN_ACTOR: a thread due before this tick that its actor's
		update did not run - an actor this engine does not update, as a
		braindead one: Halo 2 would not run it either, 0x1e34e0 runs only
		active actors - counted, and said the first times) */
		if (rt.cs_in_actor && c->thread_index != NONE && c->run_time != game_time_get() && c->start_time != game_time_get() &&
			hs_runtime_thread_due(c->thread_index))
		{
			if (rt.cs_unrun_ticks++ < 8)
				platform_log("h2 ai: command script %d of actor %lx due but not run by its actor's update (mode %d)", c->script,
					c->actor_index, (int)actor_get(c->actor_index)->state.mode);
		}
		if (command_script_aborts(c))
		{
			command_script_end(c);
			continue;
		}
		if (c->thread_index == NONE)
			command_script_start(c);
	}
	/* (ai_dialogue_enable, and cs_suppress_dialogue_global's scripts) */
	if ((rt.dialogue_off || dialogue_held) != rt.dialogue_switched_off)
	{
		rt.dialogue_switched_off = rt.dialogue_off || dialogue_held;
		ai_globals_dialogue_triggers_enabled(!rt.dialogue_switched_off);
	}
	if (game_time_get() % 15 == 0)
		player_vehicle_boarding_update();
	if (game_time_get() % 6 == 0)
		jackal_shields_update();
	if (game_time_get() % 60 == 0 && HALO_ENV_ONCE("HALO_H2_AI_STUCK"))
	{
		rt.route_timing = TRUE;
		stuck_update();
	}
	if (game_time_get() % 30 == 7)
		brutes_last_man_update();
	if (rt.only_squad && game_time_get() % 15 == 3)
	{
		short s;

		for (s = 0; s < rt.squad_count; s++)
		{
			if (s != rt.only_squad - 1 && squad_reference(s) != NONE)
				ai_scripting_erase(squad_reference(s));
		}
	}
	flyers_update();
	ground_cargo_update();
	flood_update();
}

/* ---------- the actors of the level's palette made at a place (the test range,
h2_range.c, and the mode layouts' Firefight squads on a Halo 2 mission,
forge_modes_h2.inc: every build) */

/* the level's actor variant whose actor tag name has `character` and (not "*") whose weapon tag has
`weapon`; the first so named, NONE if there is none */
static short range_variant_find(char const *character, char const *weapon)
{
	short n, count = global_scenario_get()->ai_actor_palette.count;

	for (n = 0; n < count; n++)
	{
		struct tag_reference const *reference = TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->ai_actor_palette, n, struct tag_reference);
		struct actor_variant_definition const *v;
		char const *actor_name, *weapon_name;
		size_t character_length = strlen(character);

		if (reference->index == NONE)
			continue;
		v = actor_variant_definition_get(reference->index);
		actor_name = v->actor_reference.index != NONE ? tag_get_name(v->actor_reference.index) : "";
		weapon_name = v->ranged_combat.reference.index != NONE ? tag_get_name(v->ranged_combat.reference.index) : "";
		/* (the character: its tag's last name exactly, "jackal" is not "jackal_sniper"; else has it) */
		{
			char const *last = strrchr(actor_name, '\\');

			last = last ? last + 1 : actor_name;
			if (strcmp(last, character) && !(strchr(character, '\\') && strstr(actor_name, character)))
				continue;
		}
		if (strcmp(weapon, "*") && !strstr(weapon_name, weapon))
			continue;
		return n;
	}
	return NONE;
}

/* (the mode layouts, forge_modes_h2.inc) whether this level's palette has a
character (its tag's last name) with a weapon (a part of its tag's name;
"*" any) - what h2_ai_range_spawn could make */
boolean h2_ai_range_variant_exists(char const *character, char const *weapon)
{
	if (!rt.loaded || !table_build() || !character || !weapon)
		return FALSE;
	return range_variant_find(character, weapon) != NONE;
}

/* (test range, h2_range.c "watch") the player made known to one actor, as ai_magically_see_object does */
void h2_ai_range_see(long actor_index, long unit_index)
{
	long prop_index;
	struct actor_datum *actor = actor_try_and_get(actor_index);

	if (!actor || unit_index == NONE)
		return;
	if (actor->meta.encounter_index != NONE)
		encounter_force_activate(actor->meta.encounter_index);
	prop_index = prop_get_base_by_unit_index(actor_index, unit_index, TRUE, FALSE);
	if (prop_index != NONE)
		actor_handle_unit_effect(actor_index, prop_index, 3);   /* (_ai_unit_effect_magic_sight, ai.c) */
}

long h2_ai_range_spawn(char const *character, char const *weapon, real_point3d const *position, real facing, short team,
	short rank, boolean braindead, long *out_actor_index, char *why, size_t why_size)
{
	static short carrier = NONE;
	short variant, s, k;
	long before[RT_MAXIMUM_ACTORS], after[RT_MAXIMUM_ACTORS];
	short before_count, after_count;

	why[0] = 0;
	if (!rt.loaded || !table_build())
	{
		snprintf(why, why_size, "this level has no Halo 2 AI (no squads)");
		return NONE;
	}
	variant = range_variant_find(character, weapon);
	if (variant == NONE)
	{
		snprintf(why, why_size, "no actor variant %s with weapon %s in this level (see @range_variants)", character, weapon);
		return NONE;
	}
	/* (a carrier: the first squad with a starting location and a made encounter) */
	if (carrier == NONE || carrier >= rt.squad_count || rt.squads[carrier].encounter == NONE)
	{
		for (s = 0; s < rt.squad_count; s++)
		{
			if (rt.squads[s].encounter != NONE && rt.squads[s].location_count > 0 && squad_reference(s) != NONE)
			{
				carrier = s;
				break;
			}
		}
		if (s >= rt.squad_count)
		{
			snprintf(why, why_size, "no squad to carry the actor");
			return NONE;
		}
	}
	{
		struct rt_squad *q = &rt.squads[carrier];

		/* (a range actor is no squad's last man: the carrier squad's peak count, kept for brute.style's last_man_berserk,
		made every lone range Brute berserk 1.3 s into its fight - the tag test's lone Brute stands and fires) */
		if (carrier < MAXIMUM_SQUADS)
			rt.squad_peak[carrier] = 0;
		struct encounter_definition *e;
		struct squad_definition *d;
		struct actor_starting_location *l;

		encounter_prepare(q->encounter);
		e = TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->ai_encounters, q->encounter, struct encounter_definition);
		d = TAG_BLOCK_GET_ELEMENT(&e->squads, q->squad, struct squad_definition);
		l = TAG_BLOCK_GET_ELEMENT(&d->starting_locations, 0, struct actor_starting_location);
		l->position = *position;
		l->facing = facing;
		l->actor_variant_index = variant;
		l->cluster_index = NONE;
		q->locations[0] = *position;
		q->location_yaws[0] = facing;
		q->location_vehicles[0] = NONE;
		q->location_seats[0] = 7;
		q->location_nobody[0] = FALSE;
		q->location_frames[0] = NONE;
		q->location_scripts[0] = NONE;
		q->script = NONE;
		d->actor_palette_index = variant;
		/* (the rank pinned: Halo 2's squad "major upgrade" none (3) = always the character asked for, all (4) = always its
		major; the carrier squad's own setting would give the major by chance - a range Elite or Jackal of 1.5x the
		vitality now and then) */
		d->major_upgrade = rank == _actor_major_upgrade_all ? _actor_major_upgrade_all : _actor_major_upgrade_none;
		before_count = actors_of(carrier, before, RT_MAXIMUM_ACTORS);
		squad_place(carrier, 1, 0);
		after_count = actors_of(carrier, after, RT_MAXIMUM_ACTORS);
	}
	for (s = 0; s < after_count; s++)
	{
		boolean old = FALSE;

		for (k = 0; k < before_count; k++)
			old |= before[k] == after[s];
		if (!old)
		{
			struct actor_datum *actor = actor_get(after[s]);

			if (actor->meta.unit_index == NONE)
				break;
			/* (the actor's own team too: actor_set_team sets its unit's, and the props' enemy test reads the actor's - a
			Covenant layout actor on a human carrier squad took the player for an ally and never fired) */
			if (team >= 0)
			{
				actor_set_team(after[s], team);
				actor->meta.team_index = team;
			}
			if (braindead)
				actor_braindead(after[s], TRUE);
			if (out_actor_index)
				*out_actor_index = after[s];
			object_set_position(actor->meta.unit_index, position, NULL, NULL);
			return actor->meta.unit_index;
		}
	}
	snprintf(why, why_size, "the carrier squad %s placed no one", rt.squads[carrier].name);
	return NONE;
}

#if !defined(HALO_RELEASE) && !defined(HALO_VITA)
/* ---------- the test range (port/linux/game/h2_range.c; test builds only) */

/* the tags of the level's actor palette: "i: actor tag | unit tag | weapon tag" logged; the
count returned */
long h2_ai_range_variants(char const *filter)
{
	short n, count = global_scenario_get()->ai_actor_palette.count;

	for (n = 0; n < count; n++)
	{
		struct tag_reference const *reference = TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->ai_actor_palette, n, struct tag_reference);
		struct actor_variant_definition const *v;
		char line[400];

		if (reference->index == NONE)
			continue;
		v = actor_variant_definition_get(reference->index);
		snprintf(line, sizeof(line), "%s | %s | %s", v->actor_reference.index != NONE ? tag_get_name(v->actor_reference.index) : "-",
			v->unit_reference.index != NONE ? tag_get_name(v->unit_reference.index) : "-",
			v->ranged_combat.reference.index != NONE ? tag_get_name(v->ranged_combat.reference.index) : "-");
		if (!filter || !*filter || strstr(line, filter))
			platform_log("range: variant %d: %s | %s", (int)n, tag_get_name(reference->index), line);
	}
	return count;
}



/* (test range, "squads [filter]") the level's squads: each one's team is the squad's, each starting location's place,
facing (degrees) and actor variant (character and weapon by name) */
long h2_ai_range_squads(char const *filter)
{
	short i;
	long listed = 0;

	if (!rt.loaded)
		return 0;
	for (i = 0; i < rt.squad_count; i++)
	{
		struct rt_squad *q = &rt.squads[i];
		struct encounter_definition *e;
		struct squad_definition *d;
		short l;

		if (q->encounter == NONE || (filter && *filter && !strstr(q->name, filter)))
			continue;
		e = TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->ai_encounters, q->encounter, struct encounter_definition);
		d = TAG_BLOCK_GET_ELEMENT(&e->squads, q->squad, struct squad_definition);
		for (l = 0; l < d->starting_locations.count && l < q->location_count; l++)
		{
			short v = TAG_BLOCK_GET_ELEMENT(&d->starting_locations, l, struct actor_starting_location)->actor_variant_index;

			platform_log("range: squad %s %d: %.2f %.2f %.2f yaw %.0f frame %d seat %d | %s", q->name, (int)l,
				(double)q->locations[l].x, (double)q->locations[l].y, (double)q->locations[l].z,
				(double)(q->location_yaws[l] * 57.29578f), (int)q->location_frames[l], (int)q->location_seats[l],
				v >= 0 ? TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->ai_actor_palette, v, struct tag_reference)->name :
				d->actor_palette_index >= 0 ? TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->ai_actor_palette, d->actor_palette_index, struct tag_reference)->name : "none");
		}
		listed++;
	}
	return listed;
}

/* an actor of the level's palette made at a place: a carrier squad's (one with a starting location, no
orders matter - the actor is braindead or given orders after) location 0 rewritten and that location
placed. Returns its unit (NONE and why in `why` if not) */
/* (test range, "watch @SQUAD") a level squad by name: placed (place 1), killed (2, as ai_kill), erased (3, as ai_erase); its
living actors */
short h2_ai_range_squad(char const *name, int place, long *actors, short maximum)
{
	short i, squad = NONE;

	if (!rt.loaded)
		return -1;
	for (i = 0; i < rt.squad_count; i++)
	{
		if (!strcmp(rt.squads[i].name, name))
			squad = i;
	}
	/* (audit14, "watch quiet=GROUP": a squad group erased - each squad in it) */
	if (squad == NONE && place == 3)
	{
		short g, n = 0;

		for (g = 0; g < rt.group_count; g++)
		{
			if (strcmp(rt.groups[g].name, name))
				continue;
			for (i = 0; i < rt.squad_count; i++)
			{
				if (group_contains(g, i) && squad_reference(i) != NONE)
				{
					ai_scripting_erase(squad_reference(i));
					squad_vehicles_erase(i);
					n++;
				}
			}
			return n;
		}
	}
	if (squad == NONE)
		return -1;
	if (place == 1)
		squad_place(squad, NONE, NONE);
	else if (place >= 2 && squad_reference(squad) != NONE)
	{
		if (place == 2)
			ai_scripting_kill(squad_reference(squad));
		else
		{
			ai_scripting_erase(squad_reference(squad));
			squad_vehicles_erase(squad);
		}
	}
	return actors_of(squad, actors, maximum);
}

/* (test range, h2_range.c "watch orders=") a squad given an order by names, as ai_set_orders does (the console
cannot name an order: the scripts' calls are resolved when the level is read); FALSE when either is unknown */
boolean h2_ai_range_set_orders(char const *squad_name, char const *order_name)
{
	short i, squad = NONE, order = NONE;

	if (!rt.loaded)
		return FALSE;
	for (i = 0; i < rt.squad_count; i++)
	{
		if (!strcmp(rt.squads[i].name, squad_name))
			squad = i;
	}
	for (i = 0; i < rt.order_count; i++)
	{
		if (!strcmp(rt.orders[i].name, order_name))
			order = i;
	}
	if (squad == NONE || order == NONE)
		return FALSE;
	if (squad_order_set(squad, order))
		rt.orders_set++;
	return TRUE;
}


/* (test range, "watch") one actor now: its animation (name, frame), action, combat status, target, fire state and
emotions, tab separated */
void h2_ai_range_describe(long actor_index, char *out, size_t size)
{
	static char const *const actions[] = { "none", "sleep", "alert", "fight", "flee", "uncover", "guard", "search", "wait", "vehicle",
		"charge", "obey", "converse", "avoid" };
	struct actor_datum *actor = actor_try_and_get(actor_index);
	struct object_datum *unit = actor && actor->meta.unit_index != NONE ? object_try_and_get(actor->meta.unit_index) : NULL;
	char const *animation = "-";
	short frame = -1;

	out[0] = 0;
	if (!actor || !unit)
		return;
	if (unit->object.type == _object_type_biped || unit->object.type == _object_type_vehicle)
	{
		struct animation_graph *graph = unit->object.animation.animation_graph_index != NONE ?
			animation_graph_definition_get(unit->object.animation.animation_graph_index) : NULL;
		short index = unit->object.animation.state.index;

		if (graph && index >= 0 && index < graph->animations.count)
		{
			animation = TAG_BLOCK_GET_ELEMENT(&graph->animations, index, struct animation)->name;
			frame = unit->object.animation.state.frame_index;
		}
	}
	snprintf(out, size, "%s\t%d\t%s\t%d\t%d\t%d\tberserk=%d crouch=%d flee_panic=%d dead=%d body=%.2f shield=%.2f",
		animation, (int)frame, (int)actor->state.action < (int)NUMBEROF(actions) && actor->state.action >= 0 ? actions[actor->state.action] : "?",
		(int)actor_combat_status(actor_index), (int)actor->target.target_type, (int)actor->control.fire_state,
		(int)actor->emotions.berserk, (int)(actor->emotions.defensive_crouch || actor->orders.move.stationary_crouch || actor->orders.move.moving_crouch),
		actor->state.action == _actor_action_flee ? (int)actor->state.action_data.flee.panic_type : -1,
		TEST_FLAG(unit->object.damage_flags, _object_dead_bit) != 0, unit->object.body_vitality, unit->object.shield_vitality);
	/* (the player as this actor knows it: its prop's state, visibility, line of sight, distance - the placement perception) */
	{
		struct prop_iterator props;
		struct prop_datum *prop;
		size_t used = strlen(out);

		prop_iterator_new(&props, actor_index);
		while ((prop = prop_iterator_next(&props)) != NULL)
		{
			if (prop->player && used < size)
				used += snprintf(out + used, size - used, " player=st%d,vis%d,los%d,%.1fwu,enemy%d,ally%d,team%d/%d", (int)prop->state, (int)prop->visibility,
					(int)prop->line_of_sight, (double)prop->distance, (int)prop->enemy, (int)prop->ally, (int)prop->team_index, (int)actor->meta.team_index);
		}
		if (used < size)
			snprintf(out + used, size - used, " dormant=%d clump=%d fp=%d", (int)actor->meta.dormant, (int)clump_of_actor(actor_index),
				(int)actor->firing_positions.current_position_index);
	}
}

/* (the range's animation log) the names of the animations an actor's unit plays in its base and action channels ("-": none) */
void h2_ai_range_animations(long actor_index, char *base, char *action, size_t size)
{
	struct actor_datum *actor = actor_try_and_get(actor_index);
	struct unit_datum *unit = actor && actor->meta.unit_index != NONE ? (struct unit_datum *)object_try_and_get(actor->meta.unit_index) : NULL;
	struct animation_graph *graph;

	snprintf(base, size, "-");
	snprintf(action, size, "-");
	if (!unit || unit->object.type != _object_type_biped || unit->object.animation.animation_graph_index == NONE)
		return;
	graph = animation_graph_definition_get(unit->object.animation.animation_graph_index);
	if (unit->object.animation.state.index >= 0 && unit->object.animation.state.index < graph->animations.count)
		snprintf(base, size, "%s", TAG_BLOCK_GET_ELEMENT(&graph->animations, unit->object.animation.state.index, struct animation)->name);
	if (unit->unit.animation.action_animation.index >= 0 && unit->unit.animation.action_animation.index < graph->animations.count)
		snprintf(action, size, "%s", TAG_BLOCK_GET_ELEMENT(&graph->animations, unit->unit.animation.action_animation.index, struct animation)->name);
}

#endif

/* (debug, HALO_H2_AI_DEBUG_LOS: what a blocked line of sight to an enemy
met - actor_perception.c) */
void h2_ai_debug_los(long actor_index, long unit_index, real_point3d const *from, real_point3d const *to,
	long ignore_object_index, short result, short visibility, real awareness)
{
	struct actor_datum *actor = actor_get(actor_index);
	real_vector3d vector;
	struct collision_result collision;
	boolean hit;
	char text[64];
	struct unit_datum *u = actor->meta.unit_index != NONE ? unit_get(actor->meta.unit_index) : NULL;

	vector_from_points3d(from, to, &vector);
	hit = result != _ai_line_of_sight_clear && collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit) |
		FLAG(_collision_test_back_facing_surfaces_bit) | FLAG(_collision_test_structure_bit) |
		FLAG(_collision_test_objects_bit) | _collision_test_objects_sight_blocking_flags |
		FLAG(_collision_test_ignore_two_sided_surfaces_bit), from, &vector, ignore_object_index, &collision);
	describe_object(hit && collision.type == _collision_result_object ? collision.object_index : NONE, text, sizeof(text));
	platform_log("h2 ai los: t %ld actor %lx unit %lx (base seat %d, seat power %.2f %.2f) eye %.2f %.2f %.2f -> %lx at %.2f %.2f %.2f: los %d vis %d awareness %.2f, "
		"met %s t %.3f (%.2f wu) type %d object %s surface %ld at %.2f %.2f %.2f normal %.2f %.2f %.2f",
		game_time_get(), actor_index, actor->meta.unit_index, u ? u->unit.animation.base_seat_index : -1, u ? u->unit.seat_power[0] : -1.0f, u ? u->unit.seat_power[1] : -1.0f,
		from->x, from->y, from->z, unit_index, to->x, to->y, to->z, result, visibility, awareness, hit ? "yes" : "no", hit ? collision.t : 1.0f,
		hit ? collision.t * magnitude3d(&vector) : 0.0f, hit ? collision.type : -1, hit ? text : "",
		hit ? collision.surface_index : NONE, hit ? collision.point.x : 0.0f, hit ? collision.point.y : 0.0f,
		hit ? collision.point.z : 0.0f, hit ? collision.plane.n.i : 0.0f, hit ? collision.plane.n.j : 0.0f,
		hit ? collision.plane.n.k : 0.0f);
}

/* (tests: the h2 squad's current order's name, for the range's pos rows) */
char const *h2_ai_actor_order_name(long actor_index)
{
	short h2_squad = rt.loaded ? actor_h2_squad(actor_index) : NONE;

	if (h2_squad == NONE || rt.squads[h2_squad].order < 0 || rt.squads[h2_squad].order >= rt.order_count)
		return "-";
	return rt.orders[rt.squads[h2_squad].order].name;
}

/* (actor_moving.c) Halo 2's danger crouch impulse (behaviour 0x31, the H2EK
tag test's 0xa20070): never chosen, it sets the actor's crouch orders each
time it is tested - an actor standing crouches when its danger (actor
+0x3E0, this engine's perceived danger on Halo 2's scale) is over its engage
properties' crouch danger threshold; one crouching (+0x5DC, the crouch last
put on its unit) stands once the danger is under the stand threshold. Both
its stationary and its moving crouch (+0x451, +0x452) are set to that; the
behaviours after it only ever set them (the fight's partial cover, a friend's
line of fire), never clear them. A character without "danger crouch allow
movement" crouching so is also held (+0x488; not ported: what holds it is
not traced, and no campaign character but the Monitor - 0 and 0, flying - can
pass a threshold of its generic 10). TRUE: crouch */
H2_ALIGN boolean h2_ai_danger_crouch(
	long actor_index)
{
	struct actor_datum *actor = actor_get(actor_index);
	struct actor_definition const *definition;
	real crouch_threshold, stand_threshold, danger;

	static int off = -1;

	/* (HALO_H2_DANGER_CROUCH_OFF=1: none, as before - for A/B runs) */
	if (off < 0)
		off = HALO_ENV_ONCE("HALO_H2_DANGER_CROUCH_OFF") && atoi(HALO_ENV_ONCE("HALO_H2_DANGER_CROUCH_OFF")) != 0;
	if (off || !rt.loaded || actor->meta.definition_index == NONE)
		return FALSE;
	definition = actor_definition_get(actor->meta.definition_index);
	if (definition->firing_position.unused_firing_position[0] != H2_DANGER_CROUCH_MARK)
		return FALSE;
	memcpy(&crouch_threshold, &definition->firing_position.unused_firing_position[1], sizeof(real));
	memcpy(&stand_threshold, &definition->firing_position.unused_firing_position[2], sizeof(real));
	danger = actor->emotions.perceived_danger;
	if (!actor->control.crouching)
		return danger > crouch_threshold;
	return !(stand_threshold > danger);
}

/* (actor_moving.c) Halo 2's "disallow crouch" movement flag (0x40): such an
actor's unit is never crouched (0xa6d5c2, as its orders are put on it) */
H2_ALIGN boolean h2_ai_crouch_disallowed(
	long actor_index)
{
	struct actor_datum *actor = actor_get(actor_index);
	struct actor_definition const *definition;

	if (!rt.loaded || actor->meta.definition_index == NONE)
		return FALSE;
	definition = actor_definition_get(actor->meta.definition_index);
	return definition->firing_position.unused_firing_position[0] == H2_DANGER_CROUCH_MARK && (definition->firing_position.unused_firing_position[3] & 0x40) != 0;
}
