/*
H2_RULES.C

port: Halo 2's multiplayer rules on a Halo 2 level (a CE game engine
running over a level h2_level.c built from the player's Halo 2 map).

- Spawns: Halo 2 weighs every starting location by influences
  (upstream halo2-decompiled unknown_23b500.cpp, function_23bc40 and
  unknown_14b560.cpp function_14ef00): each living enemy, friend, enemy /
  friendly / empty vehicle, moving vehicle and live grenade (where it will
  be in a lead time), each dead ally where he died, and the scenario's
  static respawn zones (initial zones for a first spawn), the multiplayer
  globals' (mulg runtime 0x150..0x2BC, constants 0x530) weights and radii;
  each influence counts fully inside its inner radius and less out to its
  outer one (on the ground plane, between heights above and below), plus a
  random share of the globals' random bias. The best location not blocked
  wins. This engine's own rating (distance, team) is not used. Only
  locations whose game types (h2_level.c copies Halo 2's: Zanzibar's CTF
  points have none) and spawn types (initial / respawn) fit are weighed.
- Items: Halo 2's netgame equipment (unknown_157450.cpp function_15ae70):
  each placement has its object; its timer starts when it is picked up or
  moved (respawn timer "on pick up") or when it is gone ("on body
  depletion"), and runs its spawn time; then a new one is placed (the old
  one deleted first if "destroy existing on new spawn"). No item comes
  while the last one still lies there. Vehicles left away from home, empty
  and with nobody near, for their "respawn on empty" time come back home.
- Starting grenades: the multiplayer globals' default counts (0 and 0).
- The stock game types (this engine's Slayer, Team Slayer, King, Crazy
  King, Team King, Oddball, Team Oddball, Juggernaut and CTF) play as
  MCC's own default Halo 2 variants of them (halo2/game_variants/*.bin,
  the s_game_variant of unknown_19d220.h: score to win @0x50, round time
  limit @0x54 in seconds, respawn @0x80, suicide penalty @0x84): Slayer to
  25 kills, Team Slayer to 50, Team King to 60 s on the hill, Juggernaut
  to 15, no time limit in any (MCC's are 0 - variant_defaults_initialize's
  480 is not what MCC ships), and their respawn and suicide penalty times.
  The CTF flag goes home 30 s after it was dropped (MCC's flag reset time,
  @0xF4; upstream unknown_2420a0.cpp v24 starts that count when the flag
  leaves its carrier), not the CE engine's 17 s, and touching one's own
  dropped flag does not return it (MCC's "flag may be returned" off).
- Territories (h2_territories.c): its territories read from the map here;
  the starting locations, items, vehicles and spawn zones of Territories
  (Halo 2's game type 8, its engine's zone bit 7) and the "all" ones, not
  King's; the territories a player's side holds weigh on his spawn
  ("controlled territory").

- Teleporters (upstream teleporters.cpp): one per key 0..15, the key's
  first entrance and first exit; each tick the nearest player within half
  a unit of an entrance, riding nothing (a vehicle's rider is not sent),
  goes to the exit turned by the markers' angles, unless he has come
  through one and not left every entrance since; then the teleporter
  rests the multiplayer globals' recharge time (MCC's: 0 s). A filled
  exit (the pill test at the exit, the unit itself left out) holds him,
  with the HUD's blocked message every 4 s; no telefrag, no screen flash
  (Halo 2's "teleporter used" event is its sound alone, to that player).
  This engine's "left the source" latch, telefrag and flash stay the CE
  maps'.

HALO_H2_SPAWN_LOG=1 logs each spawn's weighing; HALO_H2_ITEM_LOG=1 each
item's and vehicle's respawn; HALO_H2_RESPAWN_ON_EMPTY=seconds (tests) the
vehicles' respawn-on-empty time in place of the map's;
HALO_H2_TELEPORTER_LOG=1 each teleporter's sends and blocked exits.
*/

#include "cseries.h"
#include "h2_cache.h"
#include "h2_level.h"
#include "cache/cache_files.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/game_globals.h"
#include "game/players.h"
#include "game/players_runtime.h"
#include "items/equipment_definitions.h"
#include "items/items.h"
#include "math/real_math.h"
#include "networking/network_game_globals.h"
#include "objects/objects.h"
#include "objects/object_types.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_groups.h"
#include "units/units.h"
#include "h2_rules.h"
#include "h2_territories.h"
#include "interface/hud_messaging.h"
#include "physics/collision_features.h"
#include "units/bipeds.h"
#include "game/player_control.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);
char const *cache_files_map_directory(void);
extern unsigned long halo_map_generation;
/* game_engine.c's (the item collection's random item) */
long game_engine_random_item(long item_collection_index);
struct game_variant *build_game_variant_slayer(struct game_variant *variant);
struct game_variant *build_game_variant_team_slayer(struct game_variant *variant);
struct game_variant *build_game_variant_king(struct game_variant *variant);
struct game_variant *build_game_variant_crazy_king(struct game_variant *variant);
struct game_variant *build_game_variant_team_king(struct game_variant *variant);
struct game_variant *build_game_variant_oddball(struct game_variant *variant);
struct game_variant *build_game_variant_team_oddball(struct game_variant *variant);
struct game_variant *build_game_variant_juggernaut(struct game_variant *variant);
struct game_variant *build_game_variant_ctf(struct game_variant *variant);

/* (the CE layouts h2_level.c writes Halo 2's fields into) */
typedef char h2_rules_starting_equipment_offset_assert[offsetof(struct scenario, scenario_starting_equipment) == 0x390 ? 1 : -1];
typedef char h2_rules_netgame_equipment_offset_assert[offsetof(struct scenario, netgame_equipment) == 0x384 ? 1 : -1];
typedef char h2_rules_netgame_flags_offset_assert[offsetof(struct scenario, netgame_flags) == 0x378 && sizeof(struct scenario_netgame_flag) == 148 ? 1 : -1];
typedef char h2_rules_players_offset_assert[offsetof(struct scenario, players) == 0x354 ? 1 : -1];
typedef char h2_rules_powerup_time_offset_assert[offsetof(struct equipment_definition, equipment.powerup_time) == 0x30C ? 1 : -1];

/* ---------- constants */

enum
{
	H2_RULES_ZONES = 64,
	H2_RULES_ITEMS = 256,
	H2_RULES_VEHICLES = 32,
	H2_RULES_PLAYERS = 16,
	H2_RULES_INFLUENCES = 0x80,
	/* (Halo 2's teleporter keys, 0..15: teleporters_initialize) */
	H2_RULES_TELEPORTERS = 16,
	/* (MCC's netgame equipment read for its angles: Halo 2's most is 100) */
	H2_RULES_ITEM_ANGLES = 128,
	/* (game_engine.c's _multiplayer_sound_teleporter_activate, which
	h2_sound.c plays as Halo 2's teleporter_activate) */
	H2_RULES_TELEPORTER_SOUND = 0x1B,

	/* the influence types (mulg runtime 0x180 + 0x1C each) */
	_influence_enemy = 0,
	_influence_friend,
	_influence_enemy_vehicle,
	_influence_friendly_vehicle,
	_influence_empty_vehicle,
	_influence_oddball_inclusion,
	_influence_oddball_exclusion,
	_influence_hill_inclusion,
	_influence_hill_exclusion,
	_influence_last_race_flag,
	_influence_dead_ally,
	_influence_controlled_territory,
	NUMBER_OF_INFLUENCE_TYPES,

	/* a netgame placement's Halo 2 fields in this engine's padding
	(h2_level.c) */
	_h2_flag_destroy_existing = 1 << 1,
	_h2_timer_on_pick_up = 0,
	_h2_timer_on_body_depletion = 1,
};

/* ---------- structures */

struct h2_influence_type
{
	real inner_radius;
	real outer_radius;
	real weight;
};

struct h2_zone
{
	unsigned long teams;
	unsigned long games;
	unsigned long flags;
	real_point3d position;
	real lower_height;
	real upper_height;
	real inner_radius;
	real outer_radius;
	real weight;
};

struct h2_influence
{
	real_point3d point;
	real inner_radius;
	real outer_radius;
	real upper_height;
	real lower_height;
	real weight;
};

struct h2_influence_list
{
	long count;
	struct h2_influence influences[H2_RULES_INFLUENCES];
};

struct h2_item_state
{
	long object_index;
	short timer;           /* seconds left before a new one; 0: none running */
	short empty_seconds;   /* (a vehicle) seconds left empty away from home */
};

struct h2_vehicle_state
{
	long definition_index;
	real_point3d position;
	real_vector3d forward;
	real_vector3d up;
	long object_index;
	short spawn_time;
	short respawn_on_empty;
	short empty_seconds;
	byte flags;
};

static struct
{
	/* the level's (h2_rules_map_loaded) */
	boolean loaded;
	char map_name[64];
	real upper_height;
	real lower_height;
	struct h2_influence_type types[NUMBER_OF_INFLUENCE_TYPES];
	real random_bias;
	real grenade_weight, grenade_inner, grenade_outer, grenade_lead;
	real vehicle_minimum_speed, vehicle_weight, vehicle_radius, vehicle_lead;
	real vehicle_nearby_distance;
	short default_fragmentation, default_plasma;
	/* the teleporters' (mulg runtime constants: "teleporter recharge
	time" @4, "blocked teleporter string" @0xF8, its text in the HUD's
	messages, ui\hud\hud_messages) */
	real teleporter_recharge_seconds;
	wchar_t teleporter_blocked_text[128];
	/* the HUD globals' messaging text colour (hudg 0x70), hud_messaging.c's */
	real_argb_color hud_message_color;
	real hud_message_up_seconds, hud_message_fade_seconds;
	boolean hud_message_color_known;
	long respawn_zone_count, initial_zone_count;
	struct h2_zone respawn_zones[H2_RULES_ZONES];
	struct h2_zone initial_zones[H2_RULES_ZONES];

	/* the game's (reset when a game starts) */
	long scenario_index;
	unsigned long generation;
	long last_time;
	boolean started;
	struct h2_item_state items[H2_RULES_ITEMS];
	long vehicle_count;
	struct h2_vehicle_state vehicles[H2_RULES_VEHICLES];
	boolean spawned[H2_RULES_PLAYERS];
	boolean died[H2_RULES_PLAYERS];
	/* the teleporters' (one per key, Halo 2's 16): resting ticks left
	after one was used, the blocked message's ticks left; each player's
	near an entrance this tick, and gone through and not off every
	entrance since */
	long teleporter_recharge_ticks[H2_RULES_TELEPORTERS];
	long teleporter_blocked_ticks[H2_RULES_TELEPORTERS];
	boolean near_entrance[H2_RULES_PLAYERS];
	boolean teleported[H2_RULES_PLAYERS];
	real_point3d death_position[H2_RULES_PLAYERS];
	long spawn_logs;
	/* MCC's netgame equipment's places and three angles, read as the map
	loads: an item of a level made before the builder put them in its
	padding (builder 272's 'ORI1') takes them from here, by its place -
	a fix made at load, no level made again (h2_level.h's rule) */
	long item_angle_count;
	struct
	{
		real_point3d position;
		real yaw, pitch, roll;
	} item_angles[H2_RULES_ITEM_ANGLES];
} h2_rules;

/* ---------- private code: reading the level's Halo 2 map */

static real h2_rules_real(struct h2_cache *cache, unsigned long address)
{
	unsigned char const *p = h2_cache_at(cache, address, 4);
	unsigned long bits;
	real value;

	if (!p)
		return 0.0f;
	bits = (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
	memcpy(&value, &bits, sizeof(value));
	return value;
}

static long h2_rules_long(struct h2_cache *cache, unsigned long address)
{
	unsigned char const *p = h2_cache_at(cache, address, 4);

	return p ? (long)((unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24)) : 0;
}

static short h2_rules_short(struct h2_cache *cache, unsigned long address)
{
	unsigned char const *p = h2_cache_at(cache, address, 2);

	return p ? (short)((unsigned short)p[0] | ((unsigned short)p[1] << 8)) : 0;
}

/* the text of the HUD's message with the string id (ui\hud\hud_messages,
the HUD globals' strings - where the multiplayer globals' blocked
teleporter string is), English, into the teleporters' blocked text; left
as it is when the cache has no such message */
static void h2_rules_blocked_text(struct h2_cache *cache, unsigned long string_id)
{
	long unic = h2_cache_tag_find(cache, 'unic', "ui\\hud\\hud_messages");
	struct h2_cache_tag list;
	unsigned char const *p;
	long first, count, q;

	if (unic == NONE || !h2_cache_tag_get(cache, unic, &list) || (p = h2_cache_at(cache, list.address + 0x10, 4)) == NULL)
		return;
	first = p[0] | p[1] << 8;
	count = p[2] | p[3] << 8;
	for (q = 0; q < count; q++)
	{
		unsigned long id = 0;
		char words[128];
		unsigned char const *in;
		long length = 0;

		if (!h2_cache_localized_string(cache, 0, first + q, &id, words, sizeof(words)) || id != string_id)
			continue;
		/* (UTF-8 to the game's 16-bit characters) */
		for (in = (unsigned char const *)words; *in && length < (long)NUMBEROF(h2_rules.teleporter_blocked_text) - 1; in++)
		{
			unsigned long c = *in;

			if (c >= 0xE0 && in[1] && in[2])
				c = (c & 0x0F) << 12 | (in[1] & 0x3F) << 6 | (in[2] & 0x3F), in += 2;
			else if (c >= 0xC0 && in[1])
				c = (c & 0x1F) << 6 | (in[1] & 0x3F), in++;
			h2_rules.teleporter_blocked_text[length++] = (wchar_t)c;
		}
		h2_rules.teleporter_blocked_text[length] = 0;
		return;
	}
}

/* the multiplayer globals' spawn influences, constants and default
grenades (mulg: runtime block at 8) from a cache that has them */
static boolean h2_rules_read_globals(struct h2_cache *cache)
{
	long tag = h2_cache_tag_find(cache, 'mulg', "multiplayer\\multiplayer_globals");
	struct h2_cache_tag mulg;
	unsigned long runtime, constants;
	long count = 0, k;

	if (tag == NONE || !h2_cache_tag_get(cache, tag, &mulg) || mulg.group_tag != 'mulg' ||
		!h2_cache_block(cache, mulg.address + 8, 0x540, &count, &runtime) || count < 1 ||
		!h2_cache_at(cache, runtime + 0x2BC, 4))
	{
		return FALSE;
	}
	h2_rules.default_fragmentation = h2_rules_short(cache, runtime + 0x120);
	h2_rules.default_plasma = h2_rules_short(cache, runtime + 0x124);
	h2_rules.upper_height = h2_rules_real(cache, runtime + 0x150);
	h2_rules.lower_height = h2_rules_real(cache, runtime + 0x154);
	for (k = 0; k < NUMBER_OF_INFLUENCE_TYPES; k++)
	{
		h2_rules.types[k].inner_radius = h2_rules_real(cache, runtime + 0x180 + 0x1C * k);
		h2_rules.types[k].outer_radius = h2_rules_real(cache, runtime + 0x184 + 0x1C * k);
		h2_rules.types[k].weight = h2_rules_real(cache, runtime + 0x188 + 0x1C * k);
	}
	count = 0;
	if (h2_cache_block(cache, runtime + 0x530, 0x2C, &count, &constants) && count > 0)
	{
		h2_rules.random_bias = h2_rules_real(cache, constants + 0x00);
		h2_rules.grenade_weight = h2_rules_real(cache, constants + 0x08);
		h2_rules.grenade_inner = h2_rules_real(cache, constants + 0x0C);
		h2_rules.grenade_outer = h2_rules_real(cache, constants + 0x10);
		h2_rules.grenade_lead = h2_rules_real(cache, constants + 0x14);
		h2_rules.vehicle_minimum_speed = h2_rules_real(cache, constants + 0x18);
		h2_rules.vehicle_weight = h2_rules_real(cache, constants + 0x1C);
		h2_rules.vehicle_radius = h2_rules_real(cache, constants + 0x20);
		h2_rules.vehicle_lead = h2_rules_real(cache, constants + 0x24);
		h2_rules.vehicle_nearby_distance = h2_rules_real(cache, constants + 0x28);
		h2_rules.teleporter_recharge_seconds = h2_rules_real(cache, constants + 0x04);
		if (h2_cache_at(cache, constants + 0xF8, 4))
			h2_rules_blocked_text(cache, (unsigned long)h2_rules_long(cache, constants + 0xF8));
	}
	/* (the HUD globals' messaging text colour, hudg ui\hud\default: up
	time 0x58, fade time 0x5C, the icon colour 0x60 and the text colour
	0x70 in MCC's cache - real a r g b, Halo 2's 0.5 0.459 0.729 1.0, a
	half-clear light blue - the colour its HUD messages are written in,
	"Teleporter is blocked" and the pickups: hud_messaging.c) */
	tag = h2_cache_tag_find(cache, 'hudg', "ui\\hud\\default");
	if (tag != NONE && h2_cache_tag_get(cache, tag, &mulg) && mulg.group_tag == 'hudg' && h2_cache_at(cache, mulg.address + 0x70, 16))
	{
		real a = h2_rules_real(cache, mulg.address + 0x70), r = h2_rules_real(cache, mulg.address + 0x74);
		real g = h2_rules_real(cache, mulg.address + 0x78), b = h2_rules_real(cache, mulg.address + 0x7C);

		if (a >= 0.0f && a <= 1.0f && r >= 0.0f && r <= 1.0f && g >= 0.0f && g <= 1.0f && b >= 0.0f && b <= 1.0f && a > 0.0f)
		{
			h2_rules.hud_message_color.alpha = a;
			h2_rules.hud_message_color.red = r;
			h2_rules.hud_message_color.green = g;
			h2_rules.hud_message_color.blue = b;
			h2_rules.hud_message_up_seconds = h2_rules_real(cache, mulg.address + 0x58);
			h2_rules.hud_message_fade_seconds = h2_rules_real(cache, mulg.address + 0x5C);
			h2_rules.hud_message_color_known = h2_rules.hud_message_up_seconds > 0.0f && h2_rules.hud_message_up_seconds < 60.0f &&
				h2_rules.hud_message_fade_seconds > 0.0f && h2_rules.hud_message_fade_seconds < 60.0f;
		}
	}
	return TRUE;
}

static long h2_rules_read_zones(struct h2_cache *cache, unsigned long block, struct h2_zone *zones)
{
	long count = 0, k;
	unsigned long elements;

	if (!h2_cache_block(cache, block, 0x30, &count, &elements))
		return 0;
	if (count > H2_RULES_ZONES)
		count = H2_RULES_ZONES;
	for (k = 0; k < count; k++)
	{
		unsigned long zone = elements + 0x30 * k;

		zones[k].teams = (unsigned long)h2_rules_long(cache, zone + 0x04);
		zones[k].games = (unsigned long)h2_rules_long(cache, zone + 0x08);
		zones[k].flags = (unsigned long)h2_rules_long(cache, zone + 0x0C);
		zones[k].position.x = h2_rules_real(cache, zone + 0x10);
		zones[k].position.y = h2_rules_real(cache, zone + 0x14);
		zones[k].position.z = h2_rules_real(cache, zone + 0x18);
		zones[k].lower_height = h2_rules_real(cache, zone + 0x1C);
		zones[k].upper_height = h2_rules_real(cache, zone + 0x20);
		zones[k].inner_radius = h2_rules_real(cache, zone + 0x24);
		zones[k].outer_radius = h2_rules_real(cache, zone + 0x28);
		zones[k].weight = h2_rules_real(cache, zone + 0x2C);
	}
	return count;
}

/* the scenario's spawn data (0x318): its heights over the globals', its
overloads of the influence types, its static respawn and initial zones */
/* MCC's scenario's netgame equipment (0x120, 0x90 each): each one's place
(0x40) and yaw, pitch and roll (0x4C, 0x50, 0x54), for h2_item_spawn */
static void h2_rules_read_item_angles(struct h2_cache *cache)
{
	struct h2_cache_tag scenario;
	unsigned long items;
	long count = 0, k;

	h2_rules.item_angle_count = 0;
	if (!h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario) ||
		!h2_cache_block(cache, scenario.address + 0x120, 0x90, &count, &items))
	{
		return;
	}
	for (k = 0; k < count && h2_rules.item_angle_count < H2_RULES_ITEM_ANGLES; k++)
	{
		unsigned long h = items + 0x90 * (unsigned long)k;
		long a = h2_rules.item_angle_count++;

		h2_rules.item_angles[a].position.x = h2_rules_real(cache, h + 0x40);
		h2_rules.item_angles[a].position.y = h2_rules_real(cache, h + 0x44);
		h2_rules.item_angles[a].position.z = h2_rules_real(cache, h + 0x48);
		h2_rules.item_angles[a].yaw = h2_rules_real(cache, h + 0x4C);
		h2_rules.item_angles[a].pitch = h2_rules_real(cache, h + 0x50);
		h2_rules.item_angles[a].roll = h2_rules_real(cache, h + 0x54);
	}
}

static void h2_rules_read_scenario(struct h2_cache *cache)
{
	struct h2_cache_tag scenario;
	unsigned long spawn_data, overloads;
	long count = 0, k;
	real lower, upper, zone_lower, zone_upper;

	if (!h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario) ||
		!h2_cache_block(cache, scenario.address + 0x318, 0x60, &count, &spawn_data) || count < 1)
	{
		return;
	}
	lower = h2_rules_real(cache, spawn_data + 0x0);
	upper = h2_rules_real(cache, spawn_data + 0x4);
	/* (the zones' heights left 0: the scenario's, else the globals' -
	function_23b3f0, with the globals' as they were before the scenario's
	overload below) */
	zone_lower = lower != 0.0f ? lower : h2_rules.lower_height;
	zone_upper = upper != 0.0f ? upper : h2_rules.upper_height;
	/* (function_23aea0: the scenario's non-0 heights and overloads replace
	the globals') */
	if (lower != 0.0f)
		h2_rules.lower_height = lower;
	if (upper != 0.0f)
		h2_rules.upper_height = upper;
	count = 0;
	if (h2_cache_block(cache, spawn_data + 0x48, 0x10, &count, &overloads))
	{
		for (k = 0; k < count; k++)
		{
			short type = h2_rules_short(cache, overloads + 0x10 * k);

			if (type >= 0 && type < NUMBER_OF_INFLUENCE_TYPES)
			{
				h2_rules.types[type].inner_radius = h2_rules_real(cache, overloads + 0x10 * k + 0x4);
				h2_rules.types[type].outer_radius = h2_rules_real(cache, overloads + 0x10 * k + 0x8);
				h2_rules.types[type].weight = h2_rules_real(cache, overloads + 0x10 * k + 0xC);
			}
		}
	}
	h2_rules.respawn_zone_count = h2_rules_read_zones(cache, spawn_data + 0x50, h2_rules.respawn_zones);
	h2_rules.initial_zone_count = h2_rules_read_zones(cache, spawn_data + 0x58, h2_rules.initial_zones);
	for (k = 0; k < h2_rules.respawn_zone_count + h2_rules.initial_zone_count; k++)
	{
		struct h2_zone *zone = k < h2_rules.respawn_zone_count ? &h2_rules.respawn_zones[k] :
			&h2_rules.initial_zones[k - h2_rules.respawn_zone_count];

		if (zone->lower_height == 0.0f)
			zone->lower_height = zone_lower;
		if (zone->upper_height == 0.0f)
			zone->upper_height = zone_upper;
		zone->lower_height = (real)fabs(zone->lower_height);
		zone->upper_height = (real)fabs(zone->upper_height);
	}
}

/* ---------- private code: the game */

/* the running game type (game_engine.c's game_engine_get_type: the
variant's engine) */
static long h2_game_type_running(void)
{
	return game_engine_running() ? game_engine_get_variant()->game_engine_index : NONE;
}

static boolean h2_rules_game(void)
{
	return h2_rules.loaded && h2_rules_level() && game_engine_running();
}

static real distance_squared(real_point3d const *a, real_point3d const *b)
{
	real dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;

	return dx * dx + dy * dy + dz * dz;
}

/* (a vehicle) whether a unit rides it */
static boolean h2_vehicle_occupied(long vehicle_index, short team_index, boolean *friendly)
{
	struct object_datum *vehicle = object_try_and_get(vehicle_index);
	long child;
	boolean occupied = FALSE;

	if (friendly)
		*friendly = TRUE;
	for (child = vehicle ? vehicle->object.first_child_object_index : NONE; child != NONE;)
	{
		struct object_datum *object = object_try_and_get(child);

		if (!object)
			break;
		if (object->object.type == _object_type_biped)
		{
			struct unit_datum *unit = (struct unit_datum *)object;

			occupied = TRUE;
			if (friendly && unit->unit.player_index != NONE &&
				(short)player_get(unit->unit.player_index)->team_index != team_index)
			{
				*friendly = FALSE;
			}
		}
		child = object->object.next_object_index;
	}
	return occupied;
}

/* whether a living player's unit is within radius of the point */
static boolean h2_player_near(real_point3d const *point, real radius)
{
	struct data_iterator iterator;
	struct player_datum *player;

	data_iterator_new(&iterator, player_data);
	while ((player = data_iterator_next(&iterator)) != NULL)
	{
		struct object_datum *unit = player->unit_index != NONE ? object_try_and_get(player->unit_index) : NULL;

		real_point3d origin;

		if (unit && !TEST_FLAG(unit->object.damage_flags, _object_dead_bit) &&
			distance_squared(object_get_origin(player->unit_index, &origin), point) < radius * radius)
		{
			return TRUE;
		}
	}
	return FALSE;
}

/* a new game (its time went back, or another map or level): its items,
vehicles and players start over */
static void h2_rules_game_check(void)
{
	long now = game_time_get();

	if (h2_rules.scenario_index == global_scenario_index && h2_rules.generation == halo_map_generation &&
		now >= h2_rules.last_time)
	{
		h2_rules.last_time = now;
		return;
	}
	h2_rules.scenario_index = global_scenario_index;
	h2_rules.generation = halo_map_generation;
	h2_rules.last_time = now;
	h2_rules.started = FALSE;
	h2_rules.vehicle_count = 0;
	h2_rules.spawn_logs = 0;
	memset(h2_rules.items, 0, sizeof(h2_rules.items));
	memset(h2_rules.spawned, 0, sizeof(h2_rules.spawned));
	memset(h2_rules.died, 0, sizeof(h2_rules.died));
	memset(h2_rules.teleporter_recharge_ticks, 0, sizeof(h2_rules.teleporter_recharge_ticks));
	memset(h2_rules.teleporter_blocked_ticks, 0, sizeof(h2_rules.teleporter_blocked_ticks));
	memset(h2_rules.near_entrance, 0, sizeof(h2_rules.near_entrance));
	memset(h2_rules.teleported, 0, sizeof(h2_rules.teleported));
}

/* (each tick) where each player died, while he is dead */
static void h2_rules_players_update(void)
{
	struct data_iterator iterator;
	struct player_datum *player;

	data_iterator_new(&iterator, player_data);
	while ((player = data_iterator_next(&iterator)) != NULL)
	{
		long index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		struct object_datum *unit;

		if (index < 0 || index >= H2_RULES_PLAYERS)
			continue;
		unit = player->unit_index != NONE ? object_try_and_get(player->unit_index) : NULL;
		if (unit && !TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
		{
			/* (its place in the world: a rider's position is its seat's,
			in its vehicle) */
			h2_rules.died[index] = FALSE;
			object_get_origin(player->unit_index, &h2_rules.death_position[index]);
		}
		else if (h2_rules.spawned[index])
			h2_rules.died[index] = TRUE;
	}
}

static void h2_influence_add(struct h2_influence_list *list, real_point3d const *point,
	real inner_radius, real outer_radius, real upper_height, real lower_height, real weight)
{
	if (list->count < H2_RULES_INFLUENCES && weight != 0.0f)
	{
		struct h2_influence *influence = &list->influences[list->count++];

		influence->point = *point;
		influence->inner_radius = inner_radius;
		influence->outer_radius = outer_radius;
		influence->upper_height = upper_height;
		influence->lower_height = lower_height;
		influence->weight = weight;
	}
}

static void h2_influence_add_type(struct h2_influence_list *list, short type, real_point3d const *point)
{
	struct h2_influence_type const *definition = &h2_rules.types[type];

	h2_influence_add(list, point, definition->inner_radius, definition->outer_radius,
		h2_rules.upper_height, h2_rules.lower_height, definition->weight);
}

/* a zone's game bits (Halo 2's relevant games: slayer 0, oddball 1, king
2, CTF 3, race 4) for the running game type, function_23b170 */
static boolean h2_zone_game(unsigned long games)
{
	/* (Territories, Halo 2's engine 8: bit 7) */
	if (territories_active())
		return TEST_FLAG(games, 7);
	switch (h2_game_type_running())
	{
	case game_engine_slayer:
		return TEST_FLAG(games, 0);
	case game_engine_oddball:
		return TEST_FLAG(games, 1);
	case game_engine_king:
		return TEST_FLAG(games, 2);
	case game_engine_ctf:
		return TEST_FLAG(games, 3);
	case game_engine_race:
		return TEST_FLAG(games, 4);
	default:
		return FALSE;
	}
}

/* the influences on a player's spawn (function_23bc40) */
static void h2_influences_collect(long player_index, boolean initial, struct h2_influence_list *list)
{
	struct player_datum *player = player_get(player_index);
	short team = (short)player->team_index;
	boolean teams = game_engine_has_teams();
	struct data_iterator iterator;
	struct player_datum *other;
	struct object_iterator objects;
	struct object_datum *object;
	struct h2_zone const *zones = initial ? h2_rules.initial_zones : h2_rules.respawn_zones;
	long zone_count = initial ? h2_rules.initial_zone_count : h2_rules.respawn_zone_count;
	struct game_globals *globals = scenario_get_game_globals();
	long k;

	list->count = 0;
	/* the static zones of the team and game type (function_23b170: in a
	team game a zone names its teams, red bit 0, blue bit 1) */
	for (k = 0; k < zone_count; k++)
	{
		struct h2_zone const *zone = &zones[k];

		if (teams && (team < 0 || team > 8 || !TEST_FLAG(zone->teams, team)))
			continue;
		if (zone->games && !h2_zone_game(zone->games))
			continue;
		h2_influence_add(list, &zone->position, zone->inner_radius, zone->outer_radius,
			zone->upper_height, zone->lower_height, zone->weight);
	}
	/* (Territories) the territories the player's side holds (the engine's
	influences, unknown_2bd960.cpp 0x2bf740: "controlled territory") */
	{
		real_point3d held[MAXIMUM_TERRITORIES];
		long count = h2_territories_held(player_index, held, MAXIMUM_TERRITORIES);

		for (k = 0; k < count; k++)
			h2_influence_add_type(list, _influence_controlled_territory, &held[k]);
	}
	/* the vehicles (enemy, friendly, empty; a moving one where it will be)
	and the live grenades where they will be (function_23b500) */
	object_iterator_new(&objects, _object_mask_vehicle | _object_mask_projectile, 0);
	while ((object = object_iterator_next(&objects)) != NULL)
	{
		real_vector3d velocity = object->object.translational_velocity;
		real speed;

		/* (this engine's velocities are a tick's; Halo 2's lead times and
		speeds a second's) */
		velocity.i *= TICKS_PER_SECOND;
		velocity.j *= TICKS_PER_SECOND;
		velocity.k *= TICKS_PER_SECOND;
		speed = (real)sqrt(velocity.i * velocity.i + velocity.j * velocity.j + velocity.k * velocity.k);
		if (object->object.type == _object_type_vehicle)
		{
			boolean friendly;
			real_point3d predicted;

			if (TEST_FLAG(object->object.damage_flags, _object_dead_bit))
				continue;
			if (!h2_vehicle_occupied(objects.index, team, &friendly))
				h2_influence_add_type(list, _influence_empty_vehicle, &object->object.position);
			else
				h2_influence_add_type(list, friendly ? _influence_friendly_vehicle : _influence_enemy_vehicle, &object->object.position);
			if (speed > h2_rules.vehicle_minimum_speed)
			{
				real radius = PIN(speed, 0.0f, h2_rules.vehicle_radius);

				predicted.x = object->object.position.x + velocity.i * h2_rules.vehicle_lead;
				predicted.y = object->object.position.y + velocity.j * h2_rules.vehicle_lead;
				predicted.z = object->object.position.z + velocity.k * h2_rules.vehicle_lead;
				h2_influence_add(list, &predicted, radius, radius * 1.2f, h2_rules.upper_height, h2_rules.lower_height,
					h2_rules.vehicle_weight);
			}
		}
		else
		{
			boolean grenade = FALSE;
			short g;

			for (g = 0; globals && g < globals->grenades.count && !grenade; g++)
			{
				grenade = TAG_BLOCK_GET_ELEMENT(&globals->grenades, g, struct game_globals_grenade)->projectile.index ==
					object->definition_index;
			}
			if (grenade)
			{
				real_point3d predicted;

				predicted.x = object->object.position.x + velocity.i * h2_rules.grenade_lead;
				predicted.y = object->object.position.y + velocity.j * h2_rules.grenade_lead;
				predicted.z = object->object.position.z + velocity.k * h2_rules.grenade_lead;
				h2_influence_add(list, &predicted, h2_rules.grenade_inner, h2_rules.grenade_outer,
					h2_rules.upper_height, h2_rules.lower_height, h2_rules.grenade_weight);
			}
		}
	}
	/* the other players: a living one a friend or an enemy, a dead one (an
	ally's, or anyone's in a free for all - and the player's own) where he
	died (function_23b8e0) */
	data_iterator_new(&iterator, player_data);
	while ((other = data_iterator_next(&iterator)) != NULL)
	{
		long index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		struct object_datum *unit = other->unit_index != NONE ? object_try_and_get(other->unit_index) : NULL;
		boolean self = iterator.datum_index == player_index;

		if (unit && !self)
		{
			if (!TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
			{
				real_point3d origin;

				h2_influence_add_type(list, (short)other->team_index == team ? _influence_friend : _influence_enemy,
					object_get_origin(other->unit_index, &origin));
			}
		}
		else if (index >= 0 && index < H2_RULES_PLAYERS && h2_rules.died[index] &&
			(!teams || (short)other->team_index == team))
		{
			h2_influence_add_type(list, _influence_dead_ally, &h2_rules.death_position[index]);
		}
	}
}

/* the influences' weight at a point (function_23b060) */
static real h2_influences_weight(struct h2_influence_list const *list, real_point3d const *point)
{
	real total = 0.0f;
	long k;

	for (k = 0; k < list->count; k++)
	{
		struct h2_influence const *influence = &list->influences[k];
		real dx = point->x - influence->point.x;
		real dy = point->y - influence->point.y;
		real dz = point->z - influence->point.z;
		real distance = dx * dx + dy * dy;

		if (influence->outer_radius * influence->outer_radius > distance &&
			influence->upper_height >= dz && dz >= -influence->lower_height)
		{
			if (influence->inner_radius * influence->inner_radius > distance ||
				influence->inner_radius >= influence->outer_radius)
			{
				total += influence->weight;
			}
			else
			{
				total += (1.0f - ((real)sqrt(distance) - influence->inner_radius) /
					(influence->outer_radius - influence->inner_radius)) * influence->weight;
			}
		}
	}
	return total;
}

/* whether a placement's game types (h2_level.c: Halo 2's, this engine's
values, Territories 8) have the running game: Territories' those of
Territories, all games, all but CTF, all but CTF and race (as Halo 2's
function_23b170 reads its engine 8), not King's */
static boolean h2_match_game_type(long count, short const *game_types)
{
	long k;

	if (!territories_active())
		return match_game_type(h2_game_type_running(), count, game_types);
	for (k = 0; k < count; k++)
	{
		if (game_types[k] == 8 || game_types[k] == 12 || game_types[k] == 13 || game_types[k] == 14)
			return TRUE;
	}
	return FALSE;
}

/* whether a location's game types and spawn types fit this spawn */
static boolean h2_location_fits(struct player_starting_location const *location, boolean initial)
{
	short const *spawn_types = (short const *)location->unused1C;
	long k;

	for (k = 0; k < 4; k++)
	{
		if (h2_match_game_type(1, &location->game_types[k]) &&
			(spawn_types[k] == 0 || (spawn_types[k] == 1 && initial) || (spawn_types[k] == 2 && !initial)))
		{
			return TRUE;
		}
	}
	return FALSE;
}

/* whether a unit or vehicle stands where a player would (function_23bb70
tests the player's own shape there) */
static boolean h2_location_blocked(real_point3d const *position)
{
	struct location location;
	real_point3d center = *position;
	long objects[8];
	short count, k;

	center.z += 0.6f;
	scenario_location_from_point(&location, &center);
	if (location.cluster_index == NONE)
		return FALSE;
	count = objects_in_sphere(0, _object_mask_unit, &location, &center, 0.6f, objects, NUMBEROF(objects));
	for (k = 0; k < count; k++)
	{
		struct object_datum *object = object_try_and_get(objects[k]);

		if (object && (object->object.type == _object_type_vehicle ||
			!TEST_FLAG(object->object.damage_flags, _object_dead_bit)))
		{
			return TRUE;
		}
	}
	return FALSE;
}

/* ---------- private code: items and vehicles */

static void h2_rules_item_log(char const *format, ...)
{
	static int enabled = -1;

	if (enabled < 0)
		enabled = getenv("HALO_H2_ITEM_LOG") && atoi(getenv("HALO_H2_ITEM_LOG")) > 0;
	if (enabled)
	{
		char text[256];
		va_list arguments;

		va_start(arguments, format);
		vsnprintf(text, sizeof(text), format, arguments);
		va_end(arguments);
		platform_log("%s", text);
	}
}

static long h2_item_spawn(struct scenario_netgame_equipment const *equipment)
{
	long definition_index = equipment->item_collection.index != NONE ?
		game_engine_random_item(equipment->item_collection.index) : NONE;
	struct object_placement_data placement;
	long object_index;

	if (definition_index == NONE)
		return NONE;
	/* (a random collection's choice - Coagulation's random powerup) */
	h2_rules_item_log("h2 rules: %s gives %s", tag_get_name(equipment->item_collection.index), tag_get_name(definition_index));
	object_placement_data_new(&placement, definition_index, NONE);
	placement.position = equipment->position;
	/* (the builder's mark and Halo 2's three angles in the padding, h2_level.c:
	yaw, pitch and roll - the energy sword stands tip up) */
	{
		byte const *h2 = (byte const *)equipment;

		boolean marked = *(unsigned long const *)(h2 + 0x28) == 0x4F524931UL;
		float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
		long a;

		if (marked)
			yaw = *(float const *)(h2 + 0x1C), pitch = *(float const *)(h2 + 0x20), roll = *(float const *)(h2 + 0x24);
		/* (a level made before: the angles of MCC's item at its place, read
		as the map loaded - the same three the builder writes now) */
		for (a = 0; !marked && a < h2_rules.item_angle_count; a++)
		{
			if (fabsf(h2_rules.item_angles[a].position.x - equipment->position.x) < 0.001f &&
				fabsf(h2_rules.item_angles[a].position.y - equipment->position.y) < 0.001f &&
				fabsf(h2_rules.item_angles[a].position.z - equipment->position.z) < 0.001f)
			{
				yaw = h2_rules.item_angles[a].yaw, pitch = h2_rules.item_angles[a].pitch, roll = h2_rules.item_angles[a].roll;
				marked = TRUE;
			}
		}
		if (marked)
		{
			float cy = (float)cos(yaw), sy = (float)sin(yaw), cp = (float)cos(pitch), sp = (float)sin(pitch);
			real_vector3d f, u, l;
			float cr = (float)cos(roll), sr = (float)sin(roll);

			/* (Halo 2's angles are a rotation Rz(yaw) Ry(pitch) Rx(roll) of the object's own axes - right
			hand about each, so a positive pitch tips the forward axis DOWN: the energy sword's 90, -80
			and 90 degrees stand its forward axis, the blade's, up) */
			f.i = cy * cp; f.j = sy * cp; f.k = -sp;
			u.i = cy * sp * cr + sy * sr; u.j = sy * sp * cr - cy * sr; u.k = cp * cr;
			(void)l;
			placement.forward = f;
			placement.up = u;
			h2_rules_item_log("h2 rules: item at %.2f %.2f %.2f turned (%s): forward %.3f %.3f %.3f up %.3f %.3f %.3f",
				equipment->position.x, equipment->position.y, equipment->position.z,
				*(unsigned long const *)(h2 + 0x28) == 0x4F524931UL ? "the level's angles" : "MCC's map's angles",
				f.i, f.j, f.k, placement.up.i, placement.up.j, placement.up.k);
		}
	}
	object_index = object_new(&placement);
	if (object_index != NONE)
	{
		struct object_datum *object = object_get(object_index);

		object_set_garbage(object_index, FALSE);
		if (TEST_FLAG(equipment->flags, 0))
			SET_FLAG(object->object.flags, _object_at_rest_bit, TRUE);
	}
	return object_index;
}

/* (forge_canvas.inc) the mode layout's canvas over the level's item spawns */
extern boolean forge_canvas_item_removed(long equipment_index);
extern boolean forge_canvas_item_due(long equipment_index);
extern void forge_canvas_item_made(long equipment_index, long object_index);

/* (once a second for each) Halo 2's netgame equipment, function_15ae70 */
static void h2_items_update(void)
{
	struct scenario *scenario = global_scenario_get();
	long now = game_time_get(), k;

	for (k = 0; k < scenario->netgame_equipment.count && k < H2_RULES_ITEMS; k++)
	{
		struct scenario_netgame_equipment *equipment = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_equipment, k,
			struct scenario_netgame_equipment);
		struct h2_item_state *state = &h2_rules.items[k];
		byte const *h2 = (byte const *)equipment;
		short timer_starts = *(short const *)(h2 + 0x16);
		byte h2_flags = h2[0x18];
		struct object_datum *object;

		if (!h2_match_game_type(NUMBEROF(equipment->game_type), equipment->game_type))
			continue;
		/* (an item spawn the mode layout's canvas takes away places nothing:
		forge_canvas.inc, Ark Editor's BLANK CANVAS) */
		if (forge_canvas_item_removed(k))
		{
			state->object_index = NONE;
			state->timer = 0;
			continue;
		}
		/* (the game's first tick: every item placed) */
		if (!h2_rules.started)
		{
			state->object_index = h2_item_spawn(equipment);
			state->timer = 0;
			forge_canvas_item_made(k, state->object_index);
			continue;
		}
		if ((k % TICKS_PER_SECOND) != (now % TICKS_PER_SECOND))
			continue;
		object = state->object_index != NONE ? object_try_and_get(state->object_index) : NULL;
		if (!object)
		{
			state->object_index = NONE;
			/* (one the layout editor just put back: here within the second) */
			if (forge_canvas_item_due(k))
				state->timer = 1;
		}
		/* (one lying where it was placed, never taken, stays: this engine
		takes away items no one has owned for 30 s, game_engine.c
		game_engine_update_purge; Halo 2's wait to be picked up) */
		else if (object->object.parent_object_index == NONE &&
			distance_squared(&object->object.position, &equipment->position) <= 1.0f)
		{
			((struct item_datum *)object)->item.last_owned_time = now;
		}
		if (state->timer == 0)
		{
			boolean schedule = state->object_index == NONE;

			if (object && timer_starts == _h2_timer_on_pick_up)
			{
				schedule = object->object.parent_object_index != NONE ||
					!TEST_FLAG(object->object.flags, _object_connected_to_map_bit) ||
					distance_squared(&object->object.position, &equipment->position) > 1.0f;
			}
			else if (object && timer_starts == _h2_timer_on_body_depletion)
				schedule = TEST_FLAG(object->object.damage_flags, _object_dead_bit);
			if (schedule)
			{
				state->timer = equipment->spawn_time > 0 ? equipment->spawn_time : 30;
				h2_rules_item_log("h2 rules: item %ld (%s) %s at %ld: a new one in %d s", k,
					equipment->item_collection.index != NONE ? tag_get_name(equipment->item_collection.index) : "none",
					!object ? "gone" : object->object.parent_object_index != NONE ? "picked up" : "moved", now, state->timer);
				if (object && object->object.parent_object_index == NONE)
				{
					h2_rules_item_log("h2 rules:   (it lies at %.2f %.2f %.2f, placed at %.2f %.2f %.2f)",
						object->object.position.x, object->object.position.y, object->object.position.z,
						equipment->position.x, equipment->position.y, equipment->position.z);
				}
			}
		}
		if (state->timer > 0 && --state->timer == 0)
		{
			if (object && TEST_FLAG(h2_flags, 1) && object->object.type != _object_type_weapon &&
				object->object.parent_object_index == NONE)
			{
				object_delete(state->object_index);
			}
			state->object_index = h2_item_spawn(equipment);
			forge_canvas_item_made(k, state->object_index);
			h2_rules_item_log("h2 rules: item %ld (%s) back: %08lx", k,
				equipment->item_collection.index != NONE ? tag_get_name(equipment->item_collection.index) : "none",
				(unsigned long)state->object_index);
		}
	}
}

/* the level's vehicles' homes (the scenario's placements, whose padding
holds Halo 2's spawn time, respawn-on-empty time and flags: h2_level.c) */
static void h2_vehicles_begin(void)
{
	struct scenario *scenario = global_scenario_get();
	long k;

	h2_rules.vehicle_count = 0;
	for (k = 0; k < scenario->vehicles.count && h2_rules.vehicle_count < H2_RULES_VEHICLES; k++)
	{
		byte const *placement = (byte const *)tag_block_get_element_with_size(&scenario->vehicles, k, 0x78);
		struct scenario_object_datum const *datum = (struct scenario_object_datum const *)placement;
		struct h2_vehicle_state *vehicle = &h2_rules.vehicles[h2_rules.vehicle_count];
		struct scenario_object_palette_entry const *entry;

		if (!VALID_INDEX(datum->palette_entry_index, scenario->vehicle_palette.count))
			continue;
		entry = TAG_BLOCK_GET_ELEMENT(&scenario->vehicle_palette, datum->palette_entry_index, struct scenario_object_palette_entry);
		if (entry->reference.index == NONE)
			continue;
		vehicle->definition_index = entry->reference.index;
		vehicle->position = datum->position;
		vectors3d_from_euler_angles3d(&vehicle->forward, &vehicle->up, &datum->rotation);
		vehicle->object_index = NONE;
		vehicle->spawn_time = *(short const *)(placement + 0x5C);
		vehicle->respawn_on_empty = *(short const *)(placement + 0x5E);
		/* (tests: HALO_H2_RESPAWN_ON_EMPTY seconds in place of the map's) */
		if (vehicle->respawn_on_empty > 0 && getenv("HALO_H2_RESPAWN_ON_EMPTY"))
			vehicle->respawn_on_empty = (short)atoi(getenv("HALO_H2_RESPAWN_ON_EMPTY"));
		vehicle->flags = placement[0x62];
		vehicle->empty_seconds = 0;
		h2_rules.vehicle_count++;
	}
}

/* (once a second) a vehicle left empty away from home with nobody near
for its respawn-on-empty time comes back home (function_15ae70) */
static void h2_vehicles_update(void)
{
	long now = game_time_get(), k;

	for (k = 0; k < h2_rules.vehicle_count; k++)
	{
		struct h2_vehicle_state *vehicle = &h2_rules.vehicles[k];
		struct object_datum *object;

		if (((k + 7) % TICKS_PER_SECOND) != (now % TICKS_PER_SECOND))
			continue;
		object = vehicle->object_index != NONE ? object_try_and_get(vehicle->object_index) : NULL;
		if (!object || object->definition_index != vehicle->definition_index ||
			TEST_FLAG(object->object.damage_flags, _object_dead_bit))
		{
			/* (a new one, the level's or the wrecks' (h2_vehicle_wrecks.c),
			standing at its home: this home's) */
			struct object_iterator iterator;
			struct object_datum *candidate;
			long j;

			vehicle->object_index = NONE;
			vehicle->empty_seconds = 0;
			object_iterator_new(&iterator, _object_mask_vehicle, 0);
			while ((candidate = object_iterator_next(&iterator)) != NULL && vehicle->object_index == NONE)
			{
				boolean taken = FALSE;

				if (candidate->definition_index != vehicle->definition_index ||
					TEST_FLAG(candidate->object.damage_flags, _object_dead_bit) ||
					distance_squared(&candidate->object.position, &vehicle->position) > 2.0f * 2.0f)
				{
					continue;
				}
				for (j = 0; j < h2_rules.vehicle_count && !taken; j++)
					taken = h2_rules.vehicles[j].object_index == iterator.index;
				if (!taken)
					vehicle->object_index = iterator.index;
			}
			continue;
		}
		if (vehicle->respawn_on_empty <= 0)
			continue;
		if (h2_vehicle_occupied(vehicle->object_index, NONE, NULL) ||
			distance_squared(&object->object.position, &vehicle->position) <= 10.0f * 10.0f ||
			h2_player_near(&object->object.position, h2_rules.vehicle_nearby_distance))
		{
			vehicle->empty_seconds = 0;
			continue;
		}
		if (++vehicle->empty_seconds > vehicle->respawn_on_empty)
		{
			struct object_placement_data placement;
			long made;

			extern void h2_vehicle_wrecks_replaced(long old_vehicle, long new_vehicle);

			object_placement_data_new(&placement, vehicle->definition_index, NONE);
			placement.position = vehicle->position;
			placement.forward = vehicle->forward;
			placement.up = vehicle->up;
			made = object_new(&placement);
			h2_rules_item_log("h2 rules: %s %08lx left empty %d s at %.1f %.1f %.1f: a new one %08lx at home",
				tag_get_name(vehicle->definition_index), (unsigned long)vehicle->object_index, vehicle->empty_seconds,
				object->object.position.x, object->object.position.y, object->object.position.z, (unsigned long)made);
			h2_vehicle_wrecks_replaced(vehicle->object_index, made);
			object_delete(vehicle->object_index);
			vehicle->object_index = made;
			vehicle->empty_seconds = 0;
		}
	}
}

/* ---------- public code */

/* (perf/ce-floor-2, h2_rules.h h2_rules_level_fast) the answer kept, read
inline by the per-object and per-ray callers */
unsigned long h2_rules_level_seen = ~0UL;
long h2_rules_level_seen_index = NONE;
boolean h2_rules_level_answer;

boolean h2_rules_level(void)
{
	/* (perf/ce-floor) the answer kept until the scenario changes
	(scenario.c's halo_scenario_generation): every object's update asks
	(h2_object_sleep_active), and the name's lookup and compare cost Halo CE's
	tick ~10k instructions a tick */
	if (h2_rules_level_seen != halo_scenario_generation || h2_rules_level_seen_index != global_scenario_index)
	{
		char const *name = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : NULL;

		h2_rules_level_answer = name && !strncmp(name, "h2\\", 3);
		h2_rules_level_seen = halo_scenario_generation;
		h2_rules_level_seen_index = global_scenario_index;
	}
	return h2_rules_level_answer;
}

void h2_rules_map_loaded(
	char const *map_name)
{
	char const *directory = map_name ? h2_maps_directory_of(map_name) : cache_files_map_directory();
	struct h2_cache *cache, *shared = NULL;
	char why[160];
	boolean globals;
	long k;

	h2_rules.loaded = FALSE;
	h2_rules.map_name[0] = 0;
	h2_rules.generation = 0;
	h2_territories_map_cleared();
	if (!map_name || !directory || !h2_level_is_halo2_map(directory, map_name))
		return;
	cache = h2_cache_open(directory, map_name, why, sizeof(why));
	if (!cache)
	{
		platform_log("h2 rules: %s.map cannot be read: %s (this engine's spawns and items)", map_name, why);
		return;
	}
	memset(h2_rules.types, 0, sizeof(h2_rules.types));
	h2_rules.upper_height = h2_rules.lower_height = 3.0f;
	h2_rules.random_bias = 1.0f;
	h2_rules.grenade_weight = h2_rules.vehicle_weight = 0.0f;
	h2_rules.vehicle_nearby_distance = 4.0f;
	h2_rules.default_fragmentation = h2_rules.default_plasma = 0;
	h2_rules.teleporter_recharge_seconds = 0.0f;
	h2_rules.teleporter_blocked_text[0] = 0;
	h2_rules.hud_message_color_known = FALSE;
	globals = h2_rules_read_globals(cache);
	if (!globals && strcmp(map_name, "shared"))
	{
		shared = h2_cache_open(directory, "shared", why, sizeof(why));
		globals = shared && h2_rules_read_globals(shared);
	}
	h2_rules.respawn_zone_count = h2_rules.initial_zone_count = 0;
	h2_rules_read_scenario(cache);
	h2_rules_read_item_angles(cache);
	/* (its territories: h2_territories.c) */
	h2_territories_map_loaded(cache, map_name);
	if (shared)
		h2_cache_close(shared);
	h2_cache_close(cache);
	if (!globals)
	{
		platform_log("h2 rules: %s: no multiplayer globals (this engine's spawns and items)", map_name);
		return;
	}
	h2_rules.loaded = TRUE;
	snprintf(h2_rules.map_name, sizeof(h2_rules.map_name), "%s", map_name);
	platform_log("h2 rules: %s: influences enemy %.2f..%.2f %.2f, friend %.2f..%.2f %.2f, dead ally %.2f..%.2f %.2f; heights %.2f/%.2f; "
		"random %.2f; grenades %.2f %.2f..%.2f, vehicles %.2f; %ld respawn zones, %ld initial; mulg default grenades %d frag %d plasma (not used: Halo 2 starts a unit with 2 frags)",
		map_name, h2_rules.types[_influence_enemy].inner_radius, h2_rules.types[_influence_enemy].outer_radius, h2_rules.types[_influence_enemy].weight,
		h2_rules.types[_influence_friend].inner_radius, h2_rules.types[_influence_friend].outer_radius, h2_rules.types[_influence_friend].weight,
		h2_rules.types[_influence_dead_ally].inner_radius, h2_rules.types[_influence_dead_ally].outer_radius, h2_rules.types[_influence_dead_ally].weight,
		h2_rules.upper_height, h2_rules.lower_height, h2_rules.random_bias, h2_rules.grenade_weight, h2_rules.grenade_inner,
		h2_rules.grenade_outer, h2_rules.vehicle_weight, h2_rules.respawn_zone_count, h2_rules.initial_zone_count,
		h2_rules.default_fragmentation, h2_rules.default_plasma);
	{
		char text[128];
		long c;

		for (c = 0; c < (long)sizeof(text) - 1 && h2_rules.teleporter_blocked_text[c]; c++)
			text[c] = h2_rules.teleporter_blocked_text[c] < 0x80 ? (char)h2_rules.teleporter_blocked_text[c] : '?';
		text[c] = 0;
		platform_log("h2 rules: %s: teleporters rest %.2f s after use; blocked message \"%s\"", map_name,
			h2_rules.teleporter_recharge_seconds, text);
	}
	for (k = 0; k < NUMBER_OF_INFLUENCE_TYPES; k++)
	{
		if (getenv("HALO_H2_SPAWN_LOG"))
		{
			platform_log("h2 rules: influence %ld: %.2f..%.2f weight %.2f", k, h2_rules.types[k].inner_radius,
				h2_rules.types[k].outer_radius, h2_rules.types[k].weight);
		}
	}
}

boolean h2_rules_spawn_location(
	long player_index,
	long *location_index)
{
	struct scenario *scenario;
	struct h2_influence_list *list;
	long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);
	boolean initial, logging;
	long best = NONE, candidates = 0, k;
	real best_score = -1000.0f, second = -1000.0f, worst = 1000.0f;

	/* (a campaign level: the player's own starting location, the first
	player's the first - each of the campaign scenarios has one a player, the
	second beside the first for co-op -, not this engine's pick at random
	among them: 01a's second stands outside the inhibitor the training holds
	the player in, its trigger volume tv_start_spot) */
	if (h2_rules_level() && !game_engine_running() && absolute >= 0)
	{
		scenario = global_scenario_get();
		if (scenario->players.count <= 0)
			return FALSE;
		*location_index = absolute < scenario->players.count ? absolute : 0;
		return TRUE;
	}
	if (!h2_rules_game() || absolute < 0 || absolute >= H2_RULES_PLAYERS)
		return FALSE;
	h2_rules_game_check();
	scenario = global_scenario_get();
	initial = !h2_rules.spawned[absolute];
	list = malloc(sizeof(*list));
	if (!list)
		return FALSE;
	h2_influences_collect(player_index, initial, list);
	for (k = 0; k < scenario->players.count; k++)
	{
		struct player_starting_location *location = player_get_starting_location((short)k);
		real score;

		if (!h2_location_fits(location, initial))
			continue;
		candidates++;
		score = h2_influences_weight(list, &location->position) + h2_rules.random_bias * real_random_range(0.0f, 1.0f);
		if (score < worst)
			worst = score;
		if (score > best_score && !h2_location_blocked(&location->position))
		{
			second = best_score;
			best_score = score;
			best = k;
		}
		else if (score > second)
			second = score;
	}
	/* (none of its game type: any - a level of no locations of this game) */
	if (!candidates)
	{
		for (k = 0; k < scenario->players.count; k++)
		{
			struct player_starting_location *location = player_get_starting_location((short)k);
			real score = h2_influences_weight(list, &location->position) + h2_rules.random_bias * real_random_range(0.0f, 1.0f);

			if (score > best_score && !h2_location_blocked(&location->position))
			{
				best_score = score;
				best = k;
			}
		}
	}
	logging = getenv("HALO_H2_SPAWN_LOG") != NULL;
	if (logging)
	{
		struct player_starting_location *location = best != NONE ? player_get_starting_location((short)best) : NULL;

		platform_log("h2 rules: spawn player %ld (team %ld) %s: location %ld of %ld fitting (%ld in all) at %.1f %.1f %.1f score %.3f "
			"(next %.3f, worst %.3f), %ld influences, time %ld",
			absolute, (long)player_get(player_index)->team_index, initial ? "initial" : "respawn", best, candidates,
			(long)scenario->players.count, location ? location->position.x : 0.0f, location ? location->position.y : 0.0f,
			location ? location->position.z : 0.0f, best_score, second, worst, list->count, (long)game_time_get());
		for (k = 0; k < list->count && h2_rules.spawn_logs < 400; k++, h2_rules.spawn_logs++)
		{
			struct h2_influence const *influence = &list->influences[k];

			platform_log("h2 rules:   influence at %.1f %.1f %.1f radius %.1f..%.1f weight %.2f%s", influence->point.x,
				influence->point.y, influence->point.z, influence->inner_radius, influence->outer_radius, influence->weight,
				location && distance_squared(&influence->point, &location->position) <
					influence->outer_radius * influence->outer_radius ? " (reaches it)" : "");
		}
	}
	free(list);
	if (best != NONE)
		h2_rules.spawned[absolute] = TRUE;
	*location_index = best;
	return TRUE;
}

boolean h2_rules_update_items(
	void)
{
	if (!h2_rules_game())
		return FALSE;
	h2_rules_game_check();
	h2_rules_players_update();
	if (!h2_rules.started)
		h2_vehicles_begin();
	h2_items_update();
	if (h2_rules.started)
		h2_vehicles_update();
	h2_rules.started = TRUE;
	return TRUE;
}

/* a player's unit where Halo 2's teleporters see it: its place in the
world, and FALSE when it rides in or on another object (in a vehicle's
seat), which no teleporter sends */
static boolean h2_teleporter_unit(struct player_datum const *player, real_point3d *position)
{
	struct object_datum *unit = player->unit_index != NONE ? object_try_and_get(player->unit_index) : NULL;

	if (!unit || unit->object.parent_object_index != NONE)
		return FALSE;
	object_get_origin(player->unit_index, position);
	return TRUE;
}

/* (Halo 2's teleporter_exit_blocked) whether something fills the exit
where the player's unit would stand: the features about the unit's pill
there (a sphere half its height and its radius above the exit), the
structure and the objects but the unit itself, holding the point its
radius above the exit */
static boolean h2_teleporter_exit_blocked(struct player_datum const *player, real_point3d const *exit)
{
	struct collision_feature_list features;
	struct collision_plane result;
	real_point3d base, center, point;
	real height, radius, extent;

	biped_get_physics_pill(player->unit_index, &base, &height, &radius);
	extent = height * 0.5f + radius;
	center = point = *exit;
	center.z += extent;
	point.z += radius;
	return collision_get_features_in_sphere(0x200380, &center, extent, height, radius, player->unit_index, &features) &&
		collision_features_test_point(&features, &point, &result);
}

boolean h2_rules_update_teleporters(
	void)
{
	struct scenario *scenario = global_scenario_get();
	struct data_iterator iterator;
	struct player_datum *player;
	long entrances[H2_RULES_TELEPORTERS], exits[H2_RULES_TELEPORTERS];
	long key, k;

	if (!h2_rules_level() || !game_engine_running() || !scenario)
		return FALSE;
	h2_rules_game_check();
	/* (Halo 2's teleporters_initialize: each key's first entrance and
	first exit, which the level builder wrote as the netgame flags'
	teleporter sources and targets, the key their team index) */
	for (key = 0; key < H2_RULES_TELEPORTERS; key++)
		entrances[key] = exits[key] = NONE;
	for (k = 0; k < scenario->netgame_flags.count; k++)
	{
		struct scenario_netgame_flag const *flag = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, k, struct scenario_netgame_flag);

		key = flag->team_index;
		if (key < 0 || key >= H2_RULES_TELEPORTERS)
			continue;
		if (flag->type == _netgame_flag_teleporter_source && entrances[key] == NONE)
			entrances[key] = k;
		else if (flag->type == _netgame_flag_teleporter_target && exits[key] == NONE)
			exits[key] = k;
	}
	/* (teleporters_update: who stands near an entrance that is not
	resting; whoever stands near none may be sent again) */
	memset(h2_rules.near_entrance, 0, sizeof(h2_rules.near_entrance));
	for (key = 0; key < H2_RULES_TELEPORTERS; key++)
	{
		struct scenario_netgame_flag const *entrance;

		if (entrances[key] == NONE || exits[key] == NONE || h2_rules.teleporter_recharge_ticks[key] > 0)
			continue;
		entrance = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, entrances[key], struct scenario_netgame_flag);
		data_iterator_new(&iterator, player_data);
		while ((player = data_iterator_next(&iterator)) != NULL)
		{
			long index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
			real_point3d position;

			if (index >= 0 && index < H2_RULES_PLAYERS && h2_teleporter_unit(player, &position) &&
				distance_squared(&position, &entrance->position) < 0.25f)
			{
				h2_rules.near_entrance[index] = TRUE;
			}
		}
	}
	for (k = 0; k < H2_RULES_PLAYERS; k++)
	{
		if (!h2_rules.near_entrance[k])
			h2_rules.teleported[k] = FALSE;
	}
	/* (teleporter_update, each in turn: the nearest player within half a
	unit of a teleporter that is not resting goes to its exit, unless he
	has just come through one, or something fills the exit - then he is
	told so every 4 seconds. No telefrag: Halo 2 has none, the player
	waits) */
	for (key = 0; key < H2_RULES_TELEPORTERS; key++)
	{
		struct scenario_netgame_flag const *entrance, *exit;
		long closest = NONE;
		real closest_distance = 0.25f;

		if (entrances[key] == NONE || exits[key] == NONE)
			continue;
		if (h2_rules.teleporter_recharge_ticks[key] > 0)
		{
			h2_rules.teleporter_recharge_ticks[key]--;
			continue;
		}
		entrance = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, entrances[key], struct scenario_netgame_flag);
		exit = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, exits[key], struct scenario_netgame_flag);
		data_iterator_new(&iterator, player_data);
		while ((player = data_iterator_next(&iterator)) != NULL)
		{
			real_point3d position;
			real distance;

			if (!h2_teleporter_unit(player, &position))
				continue;
			distance = distance_squared(&position, &entrance->position);
			if (distance < closest_distance)
			{
				closest = iterator.datum_index;
				closest_distance = distance;
			}
		}
		if (closest != NONE)
		{
			long index = DATUM_INDEX_TO_ABSOLUTE_INDEX(closest);

			player = player_get(closest);
			if (index >= 0 && index < H2_RULES_PLAYERS && !h2_rules.teleported[index])
			{
				if (!h2_teleporter_exit_blocked(player, &exit->position))
				{
					struct object_datum *unit = object_get(player->unit_index);
					real_vector3d forward = unit->object.forward;
					real angle = arctangent(forward.j, forward.i) + exit->facing - entrance->facing;

					h2_rules.teleported[index] = TRUE;
					/* (the "teleporter used" event: its sound to the player sent) */
					if (player->local_player_index != NONE)
						game_engine_play_multiplayer_sound(H2_RULES_TELEPORTER_SOUND);
					forward.i = cosine(angle);
					forward.j = sine(angle);
					forward.k = 0.0f;
					normalize3d(&forward);
					object_set_position(player->unit_index, &exit->position, &forward, NULL);
					if (player->local_player_index != NONE)
						player_control_set_facing(player->local_player_index, &forward);
					h2_rules.teleporter_recharge_ticks[key] = (long)floorf(h2_rules.teleporter_recharge_seconds * TICKS_PER_SECOND + 0.5f);
					if (getenv("HALO_H2_TELEPORTER_LOG"))
					{
						platform_log("h2 teleporter %ld: player %ld sent from %.2f %.2f %.2f to %.2f %.2f %.2f (rests %ld ticks)", key,
							index, entrance->position.x, entrance->position.y, entrance->position.z, exit->position.x,
							exit->position.y, exit->position.z, h2_rules.teleporter_recharge_ticks[key]);
					}
				}
				else
				{
					if (h2_rules.teleporter_blocked_ticks[key] > 0)
						h2_rules.teleporter_blocked_ticks[key]--;
					else
					{
						h2_rules.teleporter_blocked_ticks[key] = TICKS_PER_SECOND * 4;
						if (player->local_player_index != NONE && h2_rules.teleporter_blocked_text[0])
							hud_print_message(player->local_player_index, h2_rules.teleporter_blocked_text);
						if (getenv("HALO_H2_TELEPORTER_LOG"))
						{
							platform_log("h2 teleporter %ld: player %ld waits, its exit %.2f %.2f %.2f blocked", key, index,
								exit->position.x, exit->position.y, exit->position.z);
						}
					}
					continue;
				}
			}
		}
		h2_rules.teleporter_blocked_ticks[key] = 0;
	}
	return TRUE;
}

boolean h2_rules_item_next_tick(
	long equipment_index,
	long *tick)
{
	struct scenario *scenario = global_scenario_get();
	struct h2_item_state const *state;
	long now = game_time_get();

	if (!h2_rules_game() || !scenario)
		return FALSE;
	*tick = NONE;
	if (equipment_index < 0 || equipment_index >= scenario->netgame_equipment.count || equipment_index >= H2_RULES_ITEMS ||
		!h2_rules.started)
	{
		return TRUE;
	}
	state = &h2_rules.items[equipment_index];
	/* (its timer counts down once a second, at the second's tick of the
	item's index) */
	if (state->timer > 0)
	{
		long phase = (equipment_index - now) % TICKS_PER_SECOND;

		if (phase < 0)
			phase += TICKS_PER_SECOND;
		*tick = now + phase + (long)(state->timer - 1) * TICKS_PER_SECOND;
	}
	return TRUE;
}

boolean h2_rules_starting_grenades(
	boolean nine_or_more_players,
	long *fragmentation,
	long *plasma)
{
	if (!h2_rules_game())
		return FALSE;
	/* (Halo 2's multiplayer spawn, function_15e970: the unit's starting
	frag count is its limit - 2, 1 in a game of 9 or more players -, the
	plasma count 0; the scenario's starting equipment's flags, the variant's
	and the weapon set then change them. The multiplayer globals' default
	counts (mulg, 0 and 0) are not read there; the biped tag's grenade count
	of 4 is the unit's before it) */
	*fragmentation = nine_or_more_players ? 1 : 2;
	*plasma = 0;
	return TRUE;
}

boolean h2_rules_vehicle_placement_allowed(
	struct scenario_object_datum const *placement)
{
	word flags = *(word const *)((byte const *)placement + 0x5A);
	short bit;

	/* (Territories: h2_level.c's bit 5) */
	if (territories_active())
		return TEST_FLAG(flags, 5) || TEST_FLAG(flags, 13);
	switch (h2_game_type_running())
	{
	case game_engine_slayer: bit = 0; break;
	case game_engine_ctf: bit = 1; break;
	case game_engine_king: bit = 2; break;
	case game_engine_oddball: bit = 3; break;
	case game_engine_race: bit = 4; break;
	default: return TRUE;
	}
	/* (a level built before its game types were written: 0x0F0F there,
	every game) */
	return TEST_FLAG(flags, bit) || TEST_FLAG(flags, bit + 8);
}

void h2_rules_variant_defaults(
	struct game_variant *variant)
{
	/* MCC's default Halo 2 variants (halo2/game_variants): the stock
	variant of this engine each stands for, its score to win (Slayer and
	Juggernaut kills, CTF captures; King and Oddball this engine's minutes,
	MCC's seconds / 60), respawn and suicide penalty in seconds. MCC's round
	time limit is 0 (none) in every one */
	static struct
	{
		struct game_variant *(*build)(struct game_variant *variant);
		char const *mcc;
		long score_to_win;
		long respawn_seconds;
		long suicide_seconds;
	} const stock[] =
	{
		{ build_game_variant_slayer, "01_slayer", 25, 5, 5 },
		{ build_game_variant_team_slayer, "02_team_slayer", 50, 5, 5 },
		{ build_game_variant_ctf, "09_multi_flag_ctf", 3, 10, 10 },
		{ build_game_variant_king, "13_king", 120 / 60, 5, 5 },
		{ build_game_variant_team_king, "14_team_king", 60 / 60, 10, 5 },
		{ build_game_variant_crazy_king, "16_crazy_king", 120 / 60, 5, 5 },
		{ build_game_variant_oddball, "18_oddball", 120 / 60, 5, 5 },
		{ build_game_variant_team_oddball, "21_team_ball", 120 / 60, 10, 5 },
		{ build_game_variant_juggernaut, "24_2_on_1", 15, 5, 5 },
	};
	long k;

	if (!variant || !h2_rules_level())
		return;
	for (k = 0; k < (long)NUMBEROF(stock); k++)
	{
		struct game_variant built;

		stock[k].build(&built);
		/* (whatever vehicles it places: "+vehicles", the big team's) */
		built.universal_variant.vehicle_set = variant->universal_variant.vehicle_set;
		if (memcmp(variant, &built, sizeof(built)))
			continue;
		variant->universal_variant.score_to_win = stock[k].score_to_win;
		variant->universal_variant.respawn_time = stock[k].respawn_seconds * TICKS_PER_SECOND;
		variant->universal_variant.suicide_penalty = stock[k].suicide_seconds * TICKS_PER_SECOND;
		/* (CTF: MCC's multi flag CTF's flags (@0xF0) 2, sudden death
		alone - "flag may be returned" (bit 2) off: touching one's own
		dropped flag does not send it home, its reset time does,
		h2_rules_ctf_flag_return_ticks) */
		if (variant->game_engine_index == game_engine_ctf)
			variant->game_engine_variant.ctf.flag_must_reset = TRUE;
		platform_log("h2 rules: the stock game type as MCC's Halo 2 %s: score to win %ld, %ld s respawn, %ld s suicide penalty, no time limit",
			stock[k].mcc, stock[k].score_to_win, stock[k].respawn_seconds, stock[k].suicide_seconds);
		break;
	}
}

short h2_rules_time_limit(
	short options_time_limit)
{
	/* (MCC's default variants have none: the options') */
	return options_time_limit;
}

long h2_rules_ctf_flag_return_ticks(
	long ticks)
{
	/* (MCC's multi flag CTF: flag reset time 30 s) */
	return h2_rules_level() ? 30 * TICKS_PER_SECOND : ticks;
}

long h2_rules_vehicle_respawn_ticks(
	long definition_index,
	real_point3d const *home,
	long ticks)
{
	long k, best = NONE;
	real best_distance = 4.0f;

	if (!h2_rules_game())
		return ticks;
	for (k = 0; k < h2_rules.vehicle_count; k++)
	{
		real distance = distance_squared(&h2_rules.vehicles[k].position, home);

		if (h2_rules.vehicles[k].definition_index == definition_index && distance < best_distance)
		{
			best_distance = distance;
			best = k;
		}
	}
	return best != NONE && h2_rules.vehicles[best].spawn_time > 0 ? h2_rules.vehicles[best].spawn_time * TICKS_PER_SECOND : ticks;
}

boolean h2_rules_vehicle_respawn_held(
	long vehicle_index)
{
	struct object_datum *vehicle = object_try_and_get(vehicle_index);

	/* (function_15ae70 as a placement's timer runs out with "destroy
	existing on new spawn": a vehicle that is sat in, or that a player
	stands within the globals' vehicle nearby distance of, is kept and
	the timer tried again a second later) */
	if (!h2_rules_game() || !vehicle)
		return FALSE;
	return h2_vehicle_occupied(vehicle_index, NONE, NULL) ||
		h2_player_near(&vehicle->object.position, h2_rules.vehicle_nearby_distance);
}

boolean h2_rules_hud_message_style(
	real_argb_color *color,
	real *up_seconds,
	real *fade_seconds)
{
	if (!h2_rules_level() || !h2_rules.loaded || !h2_rules.hud_message_color_known)
		return FALSE;
	*color = h2_rules.hud_message_color;
	*up_seconds = h2_rules.hud_message_up_seconds;
	*fade_seconds = h2_rules.hud_message_fade_seconds;
	return TRUE;
}
