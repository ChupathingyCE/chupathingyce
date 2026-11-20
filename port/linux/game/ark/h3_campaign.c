/*
H3_CAMPAIGN.C

port: a Halo 3 mission's squads placed as its own scripts place them
(h3_campaign.h says what is walked and what is not).

What is read of the scenario (MCC's layout, 12-byte blocks; read against
the owner's 010_jungle.map, the field names Assembly's Halo 3 plugin's - a
reference, no code):
- script strings 0x3D8 (a data field: size, then its address at 0xC);
- scripts 0x3EC (0x34: name, type 0x20 - 0 startup, 1 dormant, 2
  continuous, 3 static, 4 command script -, root expression 0x24);
- globals 0x3F8 (0x28: name, type 0x20, initial expression 0x24);
- expressions 0x49C (0x18: salt, opcode 2, value type 4, expression type 6
  - 8 a group, 9 a value, 10 a script's call, 13 a global -, next 8, its
  source text's offset 0xC, value 0x10): a group's value is its first
  child, the function's name (its text names it: no opcode table is
  needed), the arguments after it;
- trigger volumes 0x258 (0x44: name, forward 0xC, up 0x18, corner 0x24,
  extents 0x30);
- squad groups 0x378 (0x28: name, parent 0x20), squads 0x384 (0x40: name,
  parent group 0x26, cells 0x30 - 0x60 each: a vehicle at 0x12);
- zone sets 0x58 (0x28: name, the structure BSPs it loads a mask at 0xC) and
  the trigger volumes that switch to one 0x270 (8: the volume at 4, the set
  at 6 - NONE where it only readies one).
An AI value: its kind in the top three bits (1 a squad, 2 a squad group, 4
a starting location of the squad in bits 16..28), the index below.
*/

#include "cseries.h"
#include "h3_campaign.h"

#if defined(HALO_LINUX) && !defined(HALO_VITA) && !defined(HALO_GXM_NULL)

#include "h3_cache.h"
#include "game/game.h"
#include "game/players.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "ai/ai_scenario_definitions.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_groups.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef malloc
#undef free
#undef realloc
#undef calloc

void platform_log(char const *format, ...);
/* (ai_script.c's, not in its header) */
short ai_scripting_living_count(long ai_reference);
void ai_scripting_place(long ai_reference);
void ai_scripting_erase(long ai_reference);
void ai_scripting_kill(long ai_reference);
void ai_scripting_allegiance(short team_index0, short team_index1);
boolean h3_level_direct_loaded(void);
long h3_level_point_bsp(float const *point, unsigned long mask);
long h3_level_made_bsp(void);
void h3_level_segment_request(char const *name, long bsp, float const *position, float facing);
void h3_level_segment_arrived(void);
void main_set_map_name(char const *map_name);

/* ---------- constants */

enum
{
	H3S_SCNR_ZONE_SETS = 0x58,
	H3S_ZONE_SET_SIZE = 0x28,
	H3S_SCNR_ZONE_SET_VOLUMES = 0x270,
	H3S_MAXIMUM_ZONE_SETS = 32,
	H3S_MAXIMUM_ZONE_SET_VOLUMES = 64,
	H3S_SCNR_TRIGGER_VOLUMES = 0x258,
	H3S_SCNR_GROUPS = 0x378,
	H3S_SCNR_SQUADS = 0x384,
	H3S_SCNR_STRINGS = 0x3D8,
	H3S_SCNR_SCRIPTS = 0x3EC,
	H3S_SCNR_GLOBALS = 0x3F8,
	H3S_SCNR_EXPRESSIONS = 0x49C,
	H3S_SCRIPT_SIZE = 0x34,
	H3S_GLOBAL_SIZE = 0x28,
	H3S_EXPRESSION_SIZE = 0x18,
	H3S_VOLUME_SIZE = 0x44,
	H3S_GROUP_SIZE = 0x28,
	H3S_SQUAD_SIZE = 0x40,

	H3S_MAXIMUM_SCRIPTS = 1024,
	H3S_MAXIMUM_GLOBALS = 512,
	H3S_MAXIMUM_VOLUMES = 512,
	H3S_MAXIMUM_SQUADS = 512,
	H3S_MAXIMUM_GROUPS = 256,
	H3S_MAXIMUM_EXPRESSIONS = 120000,
	H3S_MAXIMUM_STEPS = 60000,
	H3S_MAXIMUM_OPCODES = 2048,
	H3S_CALL_DEPTH = 8,
	H3S_EVALUATE_DEPTH = 24,
	H3S_STEPS_A_TICK = 400,

	/* an expression's kinds */
	H3S_EXPRESSION_GROUP = 8,
	H3S_EXPRESSION_VALUE = 9,
	H3S_EXPRESSION_SCRIPT = 10,
	H3S_EXPRESSION_GLOBAL = 13,

	/* value types read as numbers */
	H3S_TYPE_BOOLEAN = 5,
	H3S_TYPE_REAL = 6,
	H3S_TYPE_SHORT = 7,
	H3S_TYPE_LONG = 8,

	/* this engine's teams */
	CE_TEAM_PLAYER = 1,
	CE_TEAM_HUMAN = 2,
};

/* the functions known, by name */
enum
{
	_h3s_unknown = 0,
	_h3s_begin,
	_h3s_if,
	_h3s_set,
	_h3s_and,
	_h3s_or,
	_h3s_not,
	_h3s_equal,
	_h3s_not_equal,
	_h3s_less,
	_h3s_greater,
	_h3s_less_equal,
	_h3s_greater_equal,
	_h3s_add,
	_h3s_subtract,
	_h3s_multiply,
	_h3s_divide,
	_h3s_min,
	_h3s_max,
	_h3s_sleep,
	_h3s_sleep_until,
	_h3s_sleep_forever,
	_h3s_wake,
	_h3s_volume_test_players,
	_h3s_ai_place,
	_h3s_ai_erase,
	_h3s_ai_kill,
	_h3s_ai_living_count,
	_h3s_ai_spawn_count,
	_h3s_ai_living_fraction,
	_h3s_game_difficulty,
	_h3s_zero,              /* (a function whose answer here is 0: no co-op) */
	_h3s_insertion_point,
	_h3s_switch_zone_set,
	_h3s_ai_allegiance,
	_h3s_current_zone_set,   /* (the mission's insertion point: the first, HALO_H3_INSERTION=n another) */
	_h3s_one,
	NUMBER_OF_H3S_FUNCTIONS,
};

static struct
{
	char const *name;
	short function;
} const h3s_names[] =
{
	{ "begin", _h3s_begin }, { "begin_random", _h3s_begin }, { "if", _h3s_if }, { "set", _h3s_set },
	{ "and", _h3s_and }, { "or", _h3s_or }, { "not", _h3s_not },
	{ "=", _h3s_equal }, { "!=", _h3s_not_equal }, { "<", _h3s_less }, { ">", _h3s_greater },
	{ "<=", _h3s_less_equal }, { ">=", _h3s_greater_equal },
	{ "+", _h3s_add }, { "-", _h3s_subtract }, { "*", _h3s_multiply }, { "/", _h3s_divide },
	{ "min", _h3s_min }, { "max", _h3s_max },
	{ "sleep", _h3s_sleep }, { "sleep_until", _h3s_sleep_until }, { "sleep_forever", _h3s_sleep_forever }, { "wake", _h3s_wake },
	{ "volume_test_players", _h3s_volume_test_players }, { "volume_test_players_all", _h3s_volume_test_players },
	{ "ai_place", _h3s_ai_place }, { "ai_erase", _h3s_ai_erase }, { "ai_kill", _h3s_ai_kill }, { "ai_kill_silent", _h3s_ai_kill },
	{ "ai_living_count", _h3s_ai_living_count }, { "ai_nonswarm_count", _h3s_ai_living_count },
	{ "ai_spawn_count", _h3s_ai_spawn_count }, { "ai_living_fraction", _h3s_ai_living_fraction }, { "ai_strength", _h3s_ai_living_fraction },
	{ "game_difficulty_get", _h3s_game_difficulty }, { "game_difficulty_get_real", _h3s_game_difficulty },
	{ "game_is_cooperative", _h3s_zero }, { "game_insertion_point_get", _h3s_insertion_point }, { "game_coop_player_count", _h3s_one },
	{ "player_count", _h3s_one },
	{ "ai_allegiance", _h3s_ai_allegiance },
	{ "switch_zone_set", _h3s_switch_zone_set }, { "current_zone_set", _h3s_current_zone_set },
	{ "current_zone_set_fully_active", _h3s_current_zone_set },
};

/* a script's steps */
enum
{
	_step_end = 0,
	_step_wait,             /* a: the expression that must hold */
	_step_sleep,            /* a: the expression of its ticks */
	_step_place,            /* a: the AI value */
	_step_erase,
	_step_kill,
	_step_wake,             /* a: the script */
	_step_call,             /* a: the script (a static one: its steps, then back) */
	_step_set,              /* a: the global, b: the expression */
	_step_jump,             /* a: the step */
	_step_jump_unless,      /* a: the step, b: the expression */
	_step_zone_set,         /* a: the zone set */
	_step_allegiance,       /* a, b: the teams */
};

/* ---------- structures */

struct h3s_step
{
	short kind;
	long a, b;
};

struct h3s_script
{
	char name[32];
	short type;
	unsigned long root;
	long first_step;        /* NONE: not flattened yet */
	/* its thread */
	boolean running;
	long pc;
	long wake_tick;
	long stack[H3S_CALL_DEPTH];
	short depth;
	long waiting_since;     /* (the log) */
};

struct h3s_volume
{
	char name[48];
	float forward[3], left[3], up[3], corner[3], extents[3];
};

struct h3s_squad
{
	char name[32];
	short parent;
	short encounter;        /* this engine's (NONE: not made in this level) */
	boolean vehicles;       /* (every cell of it rides a vehicle - a dropship, its crew: never made here) */
	boolean wanted;         /* the scripts placed it */
	boolean placed;         /* its encounter was created */
	short spawned;          /* how many it was placed with */
};

static struct
{
	boolean gathered;
	boolean started;
	long last_tick;
	/* (its own clock: the game's time begins again with each structure BSP) */
	long clock;
	char map[64], scenario_path[128];
	/* (a structure BSP's change: asked - the map loading again -, and the
	level made for it not yet ticked in) */
	boolean switching, arriving;
	short outside_checks;
	long outside_bsp;
	/* (Halo 3's zone sets: the structure BSPs each loads, the trigger volumes
	that switch to one, the set now - NONE until the scripts or a volume say) */
	unsigned long zone_set_bsps[H3S_MAXIMUM_ZONE_SETS];
	char zone_set_names[H3S_MAXIMUM_ZONE_SETS][32];
	long zone_set_count;
	struct { short volume, zone_set; boolean inside; } zone_set_volumes[H3S_MAXIMUM_ZONE_SET_VOLUMES];
	long zone_set_volume_count;
	long zone_set;
	/* (the scripts as they were when the player came into this structure
	BSP: a death there begins the level made for it again, not the mission) */
	struct
	{
		boolean taken;
		struct h3s_script *scripts;
		void *globals;
		struct h3s_squad *squads;
		long zone_set, clock;
	} arrival;
	float outside_point[3];
	long segments;
	unsigned char *expressions;
	long expression_count;
	char *strings;
	unsigned long strings_size;
	struct h3s_script *scripts;
	long script_count;
	struct { char name[32]; boolean known; float value; float initial; boolean initial_known; boolean unknowable; } *globals;
	long global_count;
	struct h3s_volume *volumes;
	long volume_count;
	struct h3s_squad *squads;
	long squad_count;
	short group_parent[H3S_MAXIMUM_GROUPS];
	long group_count;
	struct h3s_step *steps;
	long step_count;
	short function_of[H3S_MAXIMUM_OPCODES];
	long unknown_waits, placed_total;
	boolean log;
} h3s;

/* ---------- private code: bytes */

static unsigned long get32(unsigned char const *p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static unsigned short get16(unsigned char const *p)
{
	return (unsigned short)(p[0] | (p[1] << 8));
}

static float getf(unsigned char const *p)
{
	float f;

	memcpy(&f, p, 4);
	return f;
}

/* ---------- private code: expressions */

static unsigned char const *expression_at(unsigned long datum)
{
	long index = (long)(datum & 0xFFFF);

	if (datum == 0xFFFFFFFFUL || index >= h3s.expression_count)
		return NULL;
	return h3s.expressions + H3S_EXPRESSION_SIZE * index;
}

static char const *expression_text(unsigned char const *e)
{
	unsigned long offset = get32(e + 0xC);

	return offset < h3s.strings_size ? h3s.strings + offset : "";
}

static unsigned long expression_next(unsigned char const *e)
{
	return get32(e + 8);
}

/* a group's function (its first child's text) and its first argument */
static short group_function(unsigned char const *e, unsigned long *first_argument)
{
	unsigned char const *name = expression_at(get32(e + 0x10));
	unsigned short opcode = get16(e + 2);
	short function = _h3s_unknown;

	*first_argument = name ? expression_next(name) : 0xFFFFFFFFUL;
	if (!name)
		return _h3s_unknown;
	if (opcode < H3S_MAXIMUM_OPCODES && h3s.function_of[opcode] != NONE)
		return h3s.function_of[opcode];
	{
		char const *text = expression_text(name);
		unsigned long k;

		for (k = 0; k < sizeof(h3s_names) / sizeof(h3s_names[0]); k++)
		{
			if (!strcmp(text, h3s_names[k].name))
				function = h3s_names[k].function;
		}
	}
	if (opcode < H3S_MAXIMUM_OPCODES)
		h3s.function_of[opcode] = function;
	return function;
}

static boolean expression_is_group(unsigned char const *e)
{
	return (get16(e + 6) & 0xFF) == H3S_EXPRESSION_GROUP;
}

/* an expression that calls a script (its kind 10, its opcode the script):
the script, NONE if it is not one */
static long expression_script(unsigned char const *e)
{
	long script = (long)get16(e + 2);

	if ((get16(e + 6) & 0xFF) != H3S_EXPRESSION_SCRIPT)
		return NONE;
	return script < h3s.script_count ? script : NONE;
}

/* a value node's number, by its type (FALSE: not a number) */
static boolean literal_number(unsigned char const *e, float *value)
{
	unsigned long v = get32(e + 0x10);

	switch (get16(e + 4))
	{
	case H3S_TYPE_BOOLEAN:
		*value = (float)(v & 0xFF ? 1 : 0);
		return TRUE;
	case H3S_TYPE_REAL:
		*value = getf(e + 0x10);
		return *value == *value;
	case H3S_TYPE_SHORT:
		*value = (float)(short)(v & 0xFFFF);
		return TRUE;
	case H3S_TYPE_LONG:
		*value = (float)(long)v;
		return TRUE;
	default:
		/* (an enumeration - the difficulty, a team -: its index) */
		if (get16(e + 4) >= 40 && get16(e + 4) < 80)
		{
			*value = (float)(short)(v & 0xFFFF);
			return TRUE;
		}
		return FALSE;
	}
}

/* an expression as text (the log's): "(>= g_insertion_index 1)" */
static long expression_print(unsigned long datum, char *out, long size, int depth)
{
	unsigned char const *e = expression_at(datum);
	long length = 0;

	if (size < 8)
		return 0;
	out[0] = 0;
	if (!e || depth > 6)
		return snprintf(out, (size_t)size, "..");
	switch (get16(e + 6) & 0xFF)
	{
	case H3S_EXPRESSION_GROUP:
	case H3S_EXPRESSION_SCRIPT:
	{
		unsigned char const *name = expression_at(get32(e + 0x10));
		unsigned long argument = name ? expression_next(name) : 0xFFFFFFFFUL;
		long script = expression_script(e);
		int count = 0;

		length += snprintf(out + length, (size_t)(size - length), "(%s", script != NONE ? h3s.scripts[script].name : name ? expression_text(name) : "?");
		for (; argument != 0xFFFFFFFFUL && expression_at(argument) && length < size - 8 && count < 6; argument = expression_next(expression_at(argument)), count++)
		{
			out[length++] = ' ';
			length += expression_print(argument, out + length, size - length, depth + 1);
		}
		if (length < size - 2)
			out[length++] = ')', out[length] = 0;
		return length;
	}
	default:
	{
		float value;
		char const *text = expression_text(e);

		if (text[0])
			return snprintf(out, (size_t)size, "%.40s", text);
		if (literal_number(e, &value))
			return snprintf(out, (size_t)size, "%g", (double)value);
		return snprintf(out, (size_t)size, "?");
	}
	}
}

/* ---------- private code: the squads an AI value names */

static boolean squad_in_group(long squad, long group)
{
	short at = h3s.squads[squad].parent;
	int guard = 0;

	while (at >= 0 && at < h3s.group_count && guard++ < 64)
	{
		if (at == group)
			return TRUE;
		at = h3s.group_parent[at];
	}
	return FALSE;
}

/* whether squad is one the AI value names */
static boolean ai_value_names(unsigned long value, long squad)
{
	switch (value >> 29)
	{
	case 1:
		return (long)(value & 0xFFFF) == squad;
	case 2:
		return squad_in_group(squad, (long)(value & 0xFFFF));
	case 4:
	case 5:
		return (long)((value >> 16) & 0x1FFF) == squad;
	default:
		return FALSE;
	}
}

static long squad_living(long squad)
{
	struct h3s_squad const *s = &h3s.squads[squad];

	return s->placed && s->encounter != NONE ? (long)ai_scripting_living_count((long)s->encounter) : 0;
}

/* ---------- private code: an encounter made ready

(as h2_ai.c's encounter_prepare) its starting locations' and firing
positions' clusters and the walkable surfaces under them the loaded BSP's:
the level's make leaves them unresolved, and an actor moves only between
positions that have a surface */

static void point_resolve(real_point3d const *point, short *cluster_index, long *surface_index)
{
	struct location location;
	struct collision_result collision;
	real_point3d start = *point;
	real_vector3d down = { 0.0f, 0.0f, -1.5f };
	int pass;

	start.z += 0.5f;
	scenario_location_from_point(&location, point);
	*cluster_index = location.cluster_index;
	*surface_index = NONE;
	for (pass = 0; pass < 2; pass++)
	{
		if (collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit), &start, &down,
			NONE, &collision))
		{
			*surface_index = collision.surface_index;
			if (*cluster_index == NONE)
				*cluster_index = collision.location.cluster_index;
			return;
		}
		/* (a point on instanced geometry - an object here: the structure's floor under it) */
		down.k = -4.0f;
	}
}

static void encounter_prepare(short encounter_index)
{
	struct scenario *scenario = global_scenario_get();
	struct encounter_definition *encounter;
	long k, unresolved = 0;
	short q;

	if (!scenario || encounter_index < 0 || encounter_index >= scenario->ai_encounters.count)
		return;
	encounter = TAG_BLOCK_GET_ELEMENT(&scenario->ai_encounters, encounter_index, struct encounter_definition);
	encounter->runtime_structure_bsp_reference_index = global_structure_bsp_index;
	encounter->manual_structure_bsp_reference_index = global_structure_bsp_index;
	for (k = 0; k < encounter->firing_positions.count; k++)
	{
		struct firing_position_definition *f = TAG_BLOCK_GET_ELEMENT(&encounter->firing_positions, k, struct firing_position_definition);

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
	if (h3s.log)
		platform_log("h3 campaign: %s: %ld firing positions, %ld of them not on this BSP's ground", encounter->name,
			(long)encounter->firing_positions.count, unresolved);
}

/* ---------- private code: values */

static boolean volume_holds(long volume, real_point3d const *point)
{
	struct h3s_volume const *v;
	float d[3], along;
	int k;

	if (volume < 0 || volume >= h3s.volume_count)
		return FALSE;
	v = &h3s.volumes[volume];
	d[0] = point->x - v->corner[0];
	d[1] = point->y - v->corner[1];
	d[2] = point->z - v->corner[2];
	for (k = 0; k < 3; k++)
	{
		float const *axis = k == 0 ? v->forward : k == 1 ? v->left : v->up;

		along = d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2];
		if (along < 0.0f || along > v->extents[k])
			return FALSE;
	}
	return TRUE;
}

static boolean player_position(real_point3d *point)
{
	long player = local_player_get_player_index(0);
	long unit = player != NONE ? player_get(player)->unit_index : NONE;

	if (unit == NONE || !object_try_and_get(unit))
		return FALSE;
	object_get_origin(unit, point);
	return TRUE;
}

/* an expression's value: FALSE if it is not known here */
static boolean evaluate(unsigned long datum, float *value, int depth)
{
	unsigned char const *e = expression_at(datum);
	unsigned long argument;
	short function;
	float a = 0.0f, b = 0.0f;
	boolean a_known, b_known;

	*value = 0.0f;
	if (!e || depth > H3S_EVALUATE_DEPTH)
		return FALSE;
	switch (get16(e + 6) & 0xFF)
	{
	case H3S_EXPRESSION_VALUE:
		return literal_number(e, value);
	case H3S_EXPRESSION_GLOBAL:
	{
		unsigned long index = get32(e + 0x10);

		/* (one only the AI's command scripts set - "the Pelican has landed" -
		is never set here: not known, so nothing waits on it for ever) */
		if (index >= (unsigned long)h3s.global_count || !h3s.globals[index].known || h3s.globals[index].unknowable)
			return FALSE;
		*value = h3s.globals[index].value;
		return TRUE;
	}
	case H3S_EXPRESSION_SCRIPT:
	{
		/* (a static script that answers: its last expression's value. The
		missions' own "is the cutscene to be played" scripts answer no: no
		cutscene is played here, and their bodies are only its timing) */
		long script = expression_script(e);

		if (script != NONE && (!strcmp(h3s.scripts[script].name, "cinematic_skip_start") ||
			!strcmp(h3s.scripts[script].name, "perspective_skip_start")))
		{
			*value = 0.0f;
			return TRUE;
		}
		return script != NONE && evaluate(h3s.scripts[script].root, value, depth + 1);
	}
	case H3S_EXPRESSION_GROUP:
		break;
	default:
		return FALSE;
	}
	function = group_function(e, &argument);
	switch (function)
	{
	case _h3s_begin:
	{
		/* (its last expression's) */
		unsigned long last = 0xFFFFFFFFUL;

		while (argument != 0xFFFFFFFFUL && expression_at(argument))
		{
			last = argument;
			argument = expression_next(expression_at(argument));
		}
		return last != 0xFFFFFFFFUL && evaluate(last, value, depth + 1);
	}
	case _h3s_if:
	{
		unsigned char const *condition = expression_at(argument);
		unsigned long then_datum = condition ? expression_next(condition) : 0xFFFFFFFFUL;
		unsigned char const *then_expression = expression_at(then_datum);

		if (!condition || !evaluate(argument, &a, depth + 1) || !then_expression)
			return FALSE;
		if (a != 0.0f)
			return evaluate(then_datum, value, depth + 1);
		return evaluate(expression_next(then_expression), value, depth + 1);
	}
	case _h3s_and:
	case _h3s_or:
	{
		boolean all_known = TRUE;

		*value = function == _h3s_and ? 1.0f : 0.0f;
		for (; argument != 0xFFFFFFFFUL && expression_at(argument); argument = expression_next(expression_at(argument)))
		{
			if (!evaluate(argument, &a, depth + 1))
			{
				all_known = FALSE;
				continue;
			}
			/* (one false ends an and, one true an or, whatever the rest are) */
			if ((function == _h3s_and) == (a == 0.0f))
			{
				*value = function == _h3s_and ? 0.0f : 1.0f;
				return TRUE;
			}
		}
		return all_known;
	}
	case _h3s_not:
		if (!evaluate(argument, &a, depth + 1))
			return FALSE;
		*value = a == 0.0f ? 1.0f : 0.0f;
		return TRUE;
	case _h3s_equal:
	case _h3s_not_equal:
	case _h3s_less:
	case _h3s_greater:
	case _h3s_less_equal:
	case _h3s_greater_equal:
	case _h3s_add:
	case _h3s_subtract:
	case _h3s_multiply:
	case _h3s_divide:
	case _h3s_min:
	case _h3s_max:
	{
		unsigned char const *first = expression_at(argument);

		if (!first)
			return FALSE;
		a_known = evaluate(argument, &a, depth + 1);
		b_known = evaluate(expression_next(first), &b, depth + 1);
		if (!a_known || !b_known)
			return FALSE;
		switch (function)
		{
		case _h3s_equal: *value = a == b ? 1.0f : 0.0f; break;
		case _h3s_not_equal: *value = a != b ? 1.0f : 0.0f; break;
		case _h3s_less: *value = a < b ? 1.0f : 0.0f; break;
		case _h3s_greater: *value = a > b ? 1.0f : 0.0f; break;
		case _h3s_less_equal: *value = a <= b ? 1.0f : 0.0f; break;
		case _h3s_greater_equal: *value = a >= b ? 1.0f : 0.0f; break;
		case _h3s_add: *value = a + b; break;
		case _h3s_subtract: *value = a - b; break;
		case _h3s_multiply: *value = a * b; break;
		case _h3s_divide: *value = b != 0.0f ? a / b : 0.0f; break;
		case _h3s_min: *value = a < b ? a : b; break;
		default: *value = a > b ? a : b; break;
		}
		return TRUE;
	}
	case _h3s_volume_test_players:
	{
		unsigned char const *volume = expression_at(argument);
		real_point3d point;

		if (!volume)
			return FALSE;
		*value = player_position(&point) && volume_holds((long)(short)(get32(volume + 0x10) & 0xFFFF), &point) ? 1.0f : 0.0f;
		return TRUE;
	}
	case _h3s_ai_living_count:
	case _h3s_ai_spawn_count:
	case _h3s_ai_living_fraction:
	{
		unsigned char const *ai = expression_at(argument);
		unsigned long ai_value;
		long squad, living = 0, spawned = 0;

		if (!ai || (get16(ai + 6) & 0xFF) != H3S_EXPRESSION_VALUE)
			return FALSE;
		ai_value = get32(ai + 0x10);
		{
			long named = 0, real = 0;

			for (squad = 0; squad < h3s.squad_count; squad++)
			{
				if (ai_value_names(ai_value, squad))
				{
					named++;
					if (h3s.squads[squad].vehicles)
						continue;
					real++;
					living += squad_living(squad);
					spawned += h3s.squads[squad].spawned;
				}
			}
			/* (squads that are all vehicles' - never made here -: not known) */
			if (named > 0 && real == 0)
				return FALSE;
		}
		*value = function == _h3s_ai_living_count ? (float)living : function == _h3s_ai_spawn_count ? (float)spawned :
			spawned > 0 ? (float)living / (float)spawned : 0.0f;
		return TRUE;
	}
	case _h3s_game_difficulty:
	{
		/* (Halo 3's: easy 0, normal 1, heroic 2, legendary 3 - this engine's levels) */
		*value = (float)game_difficulty_level_get();
		return TRUE;
	}
	case _h3s_zero:
		*value = 0.0f;
		return TRUE;
	case _h3s_current_zone_set:
		*value = (float)h3s.zone_set;
		return h3s.zone_set != NONE;
	case _h3s_insertion_point:
		*value = getenv("HALO_H3_INSERTION") ? (float)atoi(getenv("HALO_H3_INSERTION")) : 0.0f;
		return TRUE;
	case _h3s_one:
		*value = 1.0f;
		return TRUE;
	default:
		return FALSE;
	}
}

/* ---------- private code: a script's steps */

static long step_add(short kind, long a, long b)
{
	if (h3s.step_count >= H3S_MAXIMUM_STEPS)
		return H3S_MAXIMUM_STEPS - 1;
	h3s.steps[h3s.step_count].kind = kind;
	h3s.steps[h3s.step_count].a = a;
	h3s.steps[h3s.step_count].b = b;
	return h3s.step_count++;
}

/* a statement's steps appended */
static void flatten(unsigned long datum, int depth)
{
	unsigned char const *e = expression_at(datum);
	unsigned long argument;
	short function;

	if (!e || depth > 64)
		return;
	if ((get16(e + 6) & 0xFF) == H3S_EXPRESSION_SCRIPT)
	{
		long script = expression_script(e);

		if (script != NONE && h3s.scripts[script].type == 3)
			step_add(_step_call, script, 0);
		return;
	}
	if (!expression_is_group(e))
		return;
	function = group_function(e, &argument);
	switch (function)
	{
	case _h3s_begin:
		for (; argument != 0xFFFFFFFFUL && expression_at(argument); argument = expression_next(expression_at(argument)))
			flatten(argument, depth + 1);
		break;
	case _h3s_if:
	{
		unsigned char const *condition = expression_at(argument);
		unsigned long then_datum = condition ? expression_next(condition) : 0xFFFFFFFFUL;
		unsigned char const *then_expression = expression_at(then_datum);
		long skip, over;

		if (!condition || !then_expression)
			break;
		skip = step_add(_step_jump_unless, 0, (long)argument);
		flatten(then_datum, depth + 1);
		if (expression_at(expression_next(then_expression)))
		{
			over = step_add(_step_jump, 0, 0);
			h3s.steps[skip].a = h3s.step_count;
			flatten(expression_next(then_expression), depth + 1);
			h3s.steps[over].a = h3s.step_count;
		}
		else
			h3s.steps[skip].a = h3s.step_count;
		break;
	}
	case _h3s_sleep_until:
	{
		/* (a condition that is a begin: its statements once, its last
		expression the condition - a loop's body runs once here) */
		unsigned char const *condition = expression_at(argument);
		unsigned long inner;

		if (!condition)
			break;
		while (expression_is_group(condition) && group_function(condition, &inner) == _h3s_begin)
		{
			unsigned long last = 0xFFFFFFFFUL;

			for (; inner != 0xFFFFFFFFUL && expression_at(inner); inner = expression_next(expression_at(inner)))
			{
				if (last != 0xFFFFFFFFUL)
					flatten(last, depth + 1);
				last = inner;
			}
			if (last == 0xFFFFFFFFUL)
				break;
			argument = last;
			condition = expression_at(argument);
		}
		step_add(_step_wait, (long)argument, 0);
		break;
	}
	case _h3s_sleep:
		step_add(_step_sleep, (long)argument, 0);
		break;
	case _h3s_sleep_forever:
		/* (with a script named: that script's, not walked; alone: this one ends) */
		if (!expression_at(argument))
			step_add(_step_end, 0, 0);
		break;
	case _h3s_wake:
	{
		unsigned char const *script = expression_at(argument);

		if (script)
			step_add(_step_wake, (long)(get32(script + 0x10) & 0xFFFF), 0);
		break;
	}
	case _h3s_set:
	{
		unsigned char const *global = expression_at(argument);

		if (global && (get16(global + 6) & 0xFF) == H3S_EXPRESSION_GLOBAL)
			step_add(_step_set, (long)get32(global + 0x10), (long)expression_next(global));
		break;
	}
	case _h3s_ai_allegiance:
	{
		/* (Halo 3's teams are this engine's by number: player 1, human 2,
		covenant 3, flood 4, sentinel 5 - those past its ten left out) */
		unsigned char const *first = expression_at(argument);
		unsigned char const *second = first ? expression_at(expression_next(first)) : NULL;
		float a, b;

		if (first && second && literal_number(first, &a) && literal_number(second, &b) && a >= 0.0f && a < 10.0f && b >= 0.0f && b < 10.0f)
			step_add(_step_allegiance, (long)a, (long)b);
		break;
	}
	case _h3s_switch_zone_set:
	{
		unsigned char const *set = expression_at(argument);

		if (set && (get16(set + 6) & 0xFF) == H3S_EXPRESSION_VALUE)
			step_add(_step_zone_set, (long)(short)(get32(set + 0x10) & 0xFFFF), 0);
		break;
	}
	case _h3s_ai_place:
	case _h3s_ai_erase:
	case _h3s_ai_kill:
	{
		unsigned char const *ai = expression_at(argument);

		if (ai && (get16(ai + 6) & 0xFF) == H3S_EXPRESSION_VALUE)
		{
			step_add(function == _h3s_ai_place ? _step_place : function == _h3s_ai_erase ? _step_erase : _step_kill,
				(long)get32(ai + 0x10), 0);
		}
		break;
	}
	default:
		break;
	}
}

/* every global a script's tree sets counted (counts: one a global) */
static void sets_count(unsigned long datum, long *counts, int depth)
{
	unsigned char const *e = expression_at(datum);

	for (; e && depth < 80; datum = expression_next(e), e = expression_at(datum))
	{
		unsigned long argument;
		unsigned char const *name, *global;

		if ((get16(e + 6) & 0xFF) != H3S_EXPRESSION_GROUP && (get16(e + 6) & 0xFF) != H3S_EXPRESSION_SCRIPT)
		{
			if (depth == 0)
				break;
			continue;
		}
		name = expression_at(get32(e + 0x10));
		argument = name ? expression_next(name) : 0xFFFFFFFFUL;
		if (expression_is_group(e) && group_function(e, &argument) == _h3s_set && (global = expression_at(argument)) != NULL &&
			(get16(global + 6) & 0xFF) == H3S_EXPRESSION_GLOBAL && get32(global + 0x10) < (unsigned long)h3s.global_count)
		{
			counts[get32(global + 0x10)]++;
		}
		sets_count(argument, counts, depth + 1);
		if (depth == 0)
			break;
	}
}

static void script_flatten(long script)
{
	struct h3s_script *s = &h3s.scripts[script];

	if (s->first_step != NONE)
		return;
	s->first_step = h3s.step_count;
	flatten(s->root, 0);
	step_add(_step_end, 0, 0);
}

/* ---------- private code: running */

static void script_start(long script)
{
	struct h3s_script *s = &h3s.scripts[script];

	if (s->running)
		return;
	script_flatten(script);
	s->running = TRUE;
	s->pc = s->first_step;
	s->depth = 0;
	s->wake_tick = 0;
	s->waiting_since = NONE;
}

static void ai_act(short kind, unsigned long value, char const *script_name)
{
	long squad;

	for (squad = 0; squad < h3s.squad_count; squad++)
	{
		struct h3s_squad *s = &h3s.squads[squad];

		if (!ai_value_names(value, squad))
			continue;
		if (kind == _step_place)
		{
			s->wanted = TRUE;
			if (s->encounter == NONE || s->placed)
				continue;
			encounter_prepare(s->encounter);
			ai_scripting_place((long)s->encounter);
			s->placed = TRUE;
			s->spawned = ai_scripting_living_count((long)s->encounter);
			h3s.placed_total += s->spawned;
			platform_log("h3 campaign: %s places %s: %d of them (encounter %d) at tick %ld", script_name, s->name, (int)s->spawned,
				(int)s->encounter, h3s.clock);
		}
		else if (s->encounter != NONE && s->placed)
		{
			if (kind == _step_erase)
				ai_scripting_erase((long)s->encounter);
			else
				ai_scripting_kill((long)s->encounter);
			if (h3s.log)
				platform_log("h3 campaign: %s %s %s", script_name, kind == _step_erase ? "erases" : "kills", s->name);
		}
	}
}

static void zone_set_switch(long zone_set, char const *who)
{
	if (zone_set < 0 || zone_set >= h3s.zone_set_count || zone_set == h3s.zone_set)
		return;
	h3s.zone_set = zone_set;
	platform_log("h3 campaign: zone set %ld (%s, structure BSPs %lx) by %s", zone_set, h3s.zone_set_names[zone_set],
		h3s.zone_set_bsps[zone_set], who);
}

/* a script's thread run until it waits or ends */
static void script_run(long script, long tick)
{
	struct h3s_script *s = &h3s.scripts[script];
	long budget = H3S_STEPS_A_TICK;

	if (!s->running || s->wake_tick > tick)
		return;
	while (budget-- > 0)
	{
		struct h3s_step const *step = &h3s.steps[s->pc];
		float value;

		switch (step->kind)
		{
		case _step_end:
			if (s->depth > 0)
			{
				s->pc = s->stack[--s->depth];
				continue;
			}
			s->running = FALSE;
			return;
		case _step_wait:
			if (evaluate((unsigned long)step->a, &value, 0) && value == 0.0f)
			{
				if (s->waiting_since == NONE)
				{
					s->waiting_since = tick;
					if (h3s.log)
					{
						unsigned char const *e = expression_at((unsigned long)step->a);
						unsigned long argument = 0xFFFFFFFFUL;
						unsigned char const *name = e && expression_is_group(e) ? expression_at(get32(e + 0x10)) : NULL;
						unsigned char const *first = NULL;
						long volume = NONE;

						if (e && expression_is_group(e) && group_function(e, &argument) == _h3s_volume_test_players &&
							(first = expression_at(argument)) != NULL)
						{
							volume = (long)(short)(get32(first + 0x10) & 0xFFFF);
						}
						if (volume >= 0 && volume < h3s.volume_count)
						{
							struct h3s_volume const *v = &h3s.volumes[volume];

							platform_log("h3 campaign: %s waits for the player in %s (corner %.1f %.1f %.1f, forward %.2f %.2f, extents %.1f %.1f %.1f) "
								"from tick %ld", s->name, v->name, v->corner[0], v->corner[1], v->corner[2], v->forward[0], v->forward[1],
								v->extents[0], v->extents[1], v->extents[2], tick);
						}
						else
							platform_log("h3 campaign: %s waits (%s) from tick %ld", s->name, name ? expression_text(name) : "a value", tick);
					}
				}
				return;
			}
			s->waiting_since = NONE;
			s->pc++;
			break;
		case _step_sleep:
			s->pc++;
			if (evaluate((unsigned long)step->a, &value, 0) && value > 0.0f)
			{
				s->wake_tick = tick + (long)value;
				return;
			}
			break;
		case _step_place:
		case _step_erase:
		case _step_kill:
			ai_act(step->kind, (unsigned long)step->a, s->name);
			s->pc++;
			break;
		case _step_wake:
			if (step->a >= 0 && step->a < h3s.script_count && h3s.scripts[step->a].type == 1)
				script_start(step->a);
			s->pc++;
			break;
		case _step_call:
			if (s->depth < H3S_CALL_DEPTH && step->a >= 0 && step->a < h3s.script_count)
			{
				script_flatten(step->a);
				/* (the steps table may have moved nothing: it is one block) */
				s->stack[s->depth++] = s->pc + 1;
				s->pc = h3s.scripts[step->a].first_step;
			}
			else
				s->pc++;
			break;
		case _step_set:
			if (step->a >= 0 && step->a < h3s.global_count)
			{
				h3s.globals[step->a].known = evaluate((unsigned long)step->b, &value, 0);
				h3s.globals[step->a].value = value;
			}
			s->pc++;
			break;
		case _step_zone_set:
			zone_set_switch(step->a, s->name);
			s->pc++;
			break;
		case _step_allegiance:
			ai_scripting_allegiance((short)step->a, (short)step->b);
			s->pc++;
			break;
		case _step_jump:
			s->pc = step->a;
			break;
		case _step_jump_unless:
			/* (a condition not known here is taken as true) */
			if (evaluate((unsigned long)step->b, &value, 0) && value == 0.0f)
				s->pc = step->a;
			else
				s->pc++;
			break;
		default:
			s->pc++;
			break;
		}
		if (s->pc < 0 || s->pc >= h3s.step_count)
		{
			s->running = FALSE;
			return;
		}
	}
}

static void arrival_free(void)
{
	free(h3s.arrival.scripts);
	free(h3s.arrival.globals);
	free(h3s.arrival.squads);
	memset(&h3s.arrival, 0, sizeof(h3s.arrival));
}

static void arrival_take(void)
{
	arrival_free();
	h3s.arrival.scripts = malloc(sizeof(*h3s.scripts) * (size_t)(h3s.script_count + 1));
	h3s.arrival.globals = malloc(sizeof(*h3s.globals) * (size_t)(h3s.global_count + 1));
	h3s.arrival.squads = malloc(sizeof(*h3s.squads) * (size_t)(h3s.squad_count + 1));
	if (!h3s.arrival.scripts || !h3s.arrival.globals || !h3s.arrival.squads)
	{
		arrival_free();
		return;
	}
	memcpy(h3s.arrival.scripts, h3s.scripts, sizeof(*h3s.scripts) * (size_t)h3s.script_count);
	if (h3s.global_count)
		memcpy(h3s.arrival.globals, h3s.globals, sizeof(*h3s.globals) * (size_t)h3s.global_count);
	memcpy(h3s.arrival.squads, h3s.squads, sizeof(*h3s.squads) * (size_t)h3s.squad_count);
	h3s.arrival.zone_set = h3s.zone_set;
	h3s.arrival.clock = h3s.clock;
	h3s.arrival.taken = TRUE;
}

/* the squads the scripts placed whose places are in this BSP, not created in it yet */
static void arrival_place(void)
{
	long i;

	ai_scripting_allegiance(CE_TEAM_PLAYER, CE_TEAM_HUMAN);
	for (i = 0; i < h3s.squad_count; i++)
	{
		struct h3s_squad *s = &h3s.squads[i];

		if (s->wanted && s->encounter != NONE && !s->placed)
		{
			encounter_prepare(s->encounter);
			ai_scripting_place((long)s->encounter);
			s->placed = TRUE;
			s->spawned = ai_scripting_living_count((long)s->encounter);
			platform_log("h3 campaign: %s, placed by the scripts before, is in this BSP: %d of them (encounter %d)", s->name,
				(int)s->spawned, (int)s->encounter);
		}
	}
}

/* (a death in a later structure BSP) the scripts as the player came into it */
static void arrival_restore(void)
{
	long i;

	memcpy(h3s.scripts, h3s.arrival.scripts, sizeof(*h3s.scripts) * (size_t)h3s.script_count);
	if (h3s.global_count)
		memcpy(h3s.globals, h3s.arrival.globals, sizeof(*h3s.globals) * (size_t)h3s.global_count);
	for (i = 0; i < h3s.squad_count; i++)
	{
		short encounter = h3s.squads[i].encounter;

		h3s.squads[i] = h3s.arrival.squads[i];
		h3s.squads[i].encounter = encounter;
		h3s.squads[i].placed = FALSE;
	}
	h3s.zone_set = h3s.arrival.zone_set;
	h3s.clock = h3s.arrival.clock;
	h3s.outside_checks = 0;
	platform_log("h3 campaign: begun again in this structure BSP: the scripts as the player came into it");
	arrival_place();
}

static void campaign_start(void)
{
	long i;

	arrival_free();

	for (i = 0; i < h3s.script_count; i++)
	{
		h3s.scripts[i].running = FALSE;
		h3s.scripts[i].depth = 0;
		h3s.scripts[i].wake_tick = 0;
	}
	for (i = 0; i < h3s.global_count; i++)
	{
		h3s.globals[i].known = h3s.globals[i].initial_known;
		h3s.globals[i].value = h3s.globals[i].initial;
	}
	for (i = 0; i < h3s.squad_count; i++)
	{
		h3s.squads[i].wanted = h3s.squads[i].placed = FALSE;
		h3s.squads[i].spawned = 0;
	}
	h3s.placed_total = 0;
	h3s.zone_set = NONE;
	h3s.clock = 0;
	h3s.segments = 0;
	/* (the Marines and the Arbiter are the player's: the mission's own
	ai_allegiance calls say so; said here once for all of them) */
	ai_scripting_allegiance(CE_TEAM_PLAYER, CE_TEAM_HUMAN);
	for (i = 0; i < h3s.script_count; i++)
	{
		if (h3s.scripts[i].type == 0)
			script_start(i);
	}
	h3s.started = TRUE;
}

/* ---------- public code */

void h3_campaign_clear(void)
{
	arrival_free();
	free(h3s.expressions);
	free(h3s.strings);
	free(h3s.scripts);
	free(h3s.globals);
	free(h3s.volumes);
	free(h3s.squads);
	free(h3s.steps);
	memset(&h3s, 0, sizeof(h3s));
}

void h3_campaign_gather(
	struct h3_cache *cache,
	unsigned long scenario)
{
	long count, i;
	unsigned long elements, size, bytes;
	char const *setting = getenv("HALO_H3_SCRIPTS");

	/* (the same mission's next structure BSP: its scripts go on where they
	were - the squads' encounters are the new level's, given again) */
	if (h3s.gathered && h3s.switching && cache && !strcmp(h3s.map, h3_cache_name(cache)))
	{
		for (i = 0; i < h3s.squad_count; i++)
		{
			h3s.squads[i].encounter = NONE;
			h3s.squads[i].placed = FALSE;
		}
		h3s.arriving = TRUE;
		return;
	}
	h3_campaign_clear();
	if (!cache || !scenario || (setting && !atoi(setting)))
		return;
	h3s.log = getenv("HALO_H3_AI_DEBUG") != NULL;
	snprintf(h3s.map, sizeof(h3s.map), "%s", h3_cache_name(cache));
	snprintf(h3s.scenario_path, sizeof(h3s.scenario_path), "%s", h3_cache_scenario_path(cache));
	for (i = 0; i < H3S_MAXIMUM_OPCODES; i++)
		h3s.function_of[i] = NONE;
	/* the expressions and their text */
	if (!h3_cache_block(cache, scenario + H3S_SCNR_EXPRESSIONS, H3S_EXPRESSION_SIZE, &count, &elements) || count < 1 ||
		count > H3S_MAXIMUM_EXPRESSIONS || !(h3s.expressions = malloc((size_t)count * H3S_EXPRESSION_SIZE)) ||
		!h3_cache_read(cache, elements, (unsigned long)count * H3S_EXPRESSION_SIZE, h3s.expressions))
	{
		h3_campaign_clear();
		return;
	}
	h3s.expression_count = count;
	if (!h3_cache_data(cache, scenario + H3S_SCNR_STRINGS, &size, &bytes) || size < 1 || size > 0x400000 ||
		!(h3s.strings = malloc(size + 1)) || !h3_cache_read(cache, bytes, size, h3s.strings))
	{
		h3_campaign_clear();
		return;
	}
	h3s.strings[size] = 0;
	h3s.strings_size = size;
	/* the scripts */
	if (!h3_cache_block(cache, scenario + H3S_SCNR_SCRIPTS, H3S_SCRIPT_SIZE, &count, &elements) || count < 1 ||
		count > H3S_MAXIMUM_SCRIPTS || !(h3s.scripts = calloc((size_t)count, sizeof(*h3s.scripts))))
	{
		h3_campaign_clear();
		return;
	}
	h3s.script_count = count;
	for (i = 0; i < count; i++)
	{
		unsigned char const *s = h3_cache_at(cache, elements + H3S_SCRIPT_SIZE * (unsigned long)i, H3S_SCRIPT_SIZE);

		if (!s)
			continue;
		memcpy(h3s.scripts[i].name, s, 31);
		h3s.scripts[i].type = (short)get16(s + 0x20);
		h3s.scripts[i].root = get32(s + 0x24);
		h3s.scripts[i].first_step = NONE;
	}
	/* the trigger volumes */
	if (h3_cache_block(cache, scenario + H3S_SCNR_TRIGGER_VOLUMES, H3S_VOLUME_SIZE, &count, &elements) && count > 0 &&
		count <= H3S_MAXIMUM_VOLUMES && (h3s.volumes = calloc((size_t)count, sizeof(*h3s.volumes))))
	{
		h3s.volume_count = count;
		for (i = 0; i < count; i++)
		{
			unsigned char const *v = h3_cache_at(cache, elements + H3S_VOLUME_SIZE * (unsigned long)i, H3S_VOLUME_SIZE);
			struct h3s_volume *o = &h3s.volumes[i];
			int k;

			if (!v)
				continue;
			snprintf(o->name, sizeof(o->name), "%s", h3_cache_string(cache, get32(v)));
			for (k = 0; k < 3; k++)
			{
				o->forward[k] = getf(v + 0xC + 4 * k);
				o->up[k] = getf(v + 0x18 + 4 * k);
				o->corner[k] = getf(v + 0x24 + 4 * k);
				o->extents[k] = getf(v + 0x30 + 4 * k);
			}
			/* (left: up x forward) */
			o->left[0] = o->up[1] * o->forward[2] - o->up[2] * o->forward[1];
			o->left[1] = o->up[2] * o->forward[0] - o->up[0] * o->forward[2];
			o->left[2] = o->up[0] * o->forward[1] - o->up[1] * o->forward[0];
		}
	}
	/* the zone sets and the volumes that switch to them (0x270, 8 each: the
	trigger volume at 4, the set it switches to at 6 - NONE: it only readies one) */
	if (h3_cache_block(cache, scenario + H3S_SCNR_ZONE_SETS, H3S_ZONE_SET_SIZE, &count, &elements) && count > 0)
	{
		h3s.zone_set_count = count < H3S_MAXIMUM_ZONE_SETS ? count : H3S_MAXIMUM_ZONE_SETS;
		for (i = 0; i < h3s.zone_set_count; i++)
		{
			h3s.zone_set_bsps[i] = h3_cache_read32(cache, elements + H3S_ZONE_SET_SIZE * (unsigned long)i + 0xC);
			snprintf(h3s.zone_set_names[i], sizeof(h3s.zone_set_names[i]), "%s",
				h3_cache_string(cache, h3_cache_read32(cache, elements + H3S_ZONE_SET_SIZE * (unsigned long)i)));
		}
	}
	if (h3_cache_block(cache, scenario + H3S_SCNR_ZONE_SET_VOLUMES, 8, &count, &elements) && count > 0)
	{
		for (i = 0; i < count && h3s.zone_set_volume_count < H3S_MAXIMUM_ZONE_SET_VOLUMES; i++)
		{
			short volume = h3_cache_read16(cache, elements + 8 * (unsigned long)i + 4);
			short zone_set = h3_cache_read16(cache, elements + 8 * (unsigned long)i + 6);

			if (volume < 0 || zone_set < 0)
				continue;
			h3s.zone_set_volumes[h3s.zone_set_volume_count].volume = volume;
			h3s.zone_set_volumes[h3s.zone_set_volume_count].zone_set = zone_set;
			h3s.zone_set_volumes[h3s.zone_set_volume_count].inside = FALSE;
			h3s.zone_set_volume_count++;
		}
	}
	h3s.zone_set = NONE;
	/* the squads and their groups */
	if (h3_cache_block(cache, scenario + H3S_SCNR_GROUPS, H3S_GROUP_SIZE, &count, &elements) && count > 0)
	{
		h3s.group_count = count < H3S_MAXIMUM_GROUPS ? count : H3S_MAXIMUM_GROUPS;
		for (i = 0; i < h3s.group_count; i++)
			h3s.group_parent[i] = h3_cache_read16(cache, elements + H3S_GROUP_SIZE * (unsigned long)i + 0x20);
	}
	if (h3_cache_block(cache, scenario + H3S_SCNR_SQUADS, H3S_SQUAD_SIZE, &count, &elements) && count > 0 &&
		count <= H3S_MAXIMUM_SQUADS && (h3s.squads = calloc((size_t)count, sizeof(*h3s.squads))))
	{
		h3s.squad_count = count;
		for (i = 0; i < count; i++)
		{
			unsigned char const *s = h3_cache_at(cache, elements + H3S_SQUAD_SIZE * (unsigned long)i, H3S_SQUAD_SIZE);

			h3s.squads[i].encounter = NONE;
			if (!s)
				continue;
			memcpy(h3s.squads[i].name, s, 31);
			h3s.squads[i].parent = (short)get16(s + 0x26);
			/* (its cells, 0x30, 0x60 each: a vehicle at 0x12) */
			{
				long cell_count = 0, k, riding = 0;
				unsigned long cells = 0;

				if (h3_cache_block(cache, elements + H3S_SQUAD_SIZE * (unsigned long)i + 0x30, 0x60, &cell_count, &cells))
				{
					for (k = 0; k < cell_count; k++)
						riding += h3_cache_read16(cache, cells + 0x60 * (unsigned long)k + 0x12) != NONE;
				}
				h3s.squads[i].vehicles = cell_count > 0 && riding == cell_count;
			}
		}
	}
	h3s.steps = calloc(H3S_MAXIMUM_STEPS, sizeof(*h3s.steps));
	if (!h3s.steps)
	{
		h3_campaign_clear();
		return;
	}
	/* the globals: each one's first value */
	if (h3_cache_block(cache, scenario + H3S_SCNR_GLOBALS, H3S_GLOBAL_SIZE, &count, &elements) && count > 0 &&
		count <= H3S_MAXIMUM_GLOBALS && (h3s.globals = calloc((size_t)count, sizeof(*h3s.globals))))
	{
		h3s.global_count = count;
		for (i = 0; i < count; i++)
		{
			unsigned char const *g = h3_cache_at(cache, elements + H3S_GLOBAL_SIZE * (unsigned long)i, H3S_GLOBAL_SIZE);
			unsigned char const *e;

			if (!g)
				continue;
			memcpy(h3s.globals[i].name, g, 31);
			/* (a plain value alone: the rest are worked out by the game) */
			e = expression_at(get32(g + 0x24));
			if (e && (get16(e + 6) & 0xFF) == H3S_EXPRESSION_VALUE)
				h3s.globals[i].initial_known = literal_number(e, &h3s.globals[i].initial);
		}
	}
	/* (a global only command scripts set: not known here) */
	if (h3s.global_count)
	{
		long *by_commands = calloc((size_t)h3s.global_count, sizeof(long)), *by_others = calloc((size_t)h3s.global_count, sizeof(long));
		long unknowable = 0;

		if (by_commands && by_others)
		{
			for (i = 0; i < h3s.script_count; i++)
				sets_count(h3s.scripts[i].root, h3s.scripts[i].type == 4 ? by_commands : by_others, 0);
			for (i = 0; i < h3s.global_count; i++)
			{
				h3s.globals[i].unknowable = by_commands[i] > 0 && by_others[i] == 0;
				unknowable += h3s.globals[i].unknowable;
			}
			if (h3s.log)
				platform_log("h3 campaign: %ld globals are set only by command scripts: not known here", unknowable);
		}
		free(by_commands);
		free(by_others);
	}
	h3s.gathered = TRUE;
	platform_log("h3 campaign: %s: %ld scripts, %ld expressions, %ld globals, %ld trigger volumes, %ld squads in %ld groups read",
		h3_cache_name(cache), h3s.script_count, h3s.expression_count, h3s.global_count, h3s.volume_count, h3s.squad_count, h3s.group_count);
}

void h3_campaign_squad_encounter(
	long h3_squad,
	short encounter)
{
	if (h3s.gathered && h3_squad >= 0 && h3_squad < h3s.squad_count)
		h3s.squads[h3_squad].encounter = encounter;
}

/* the player has walked into another structure BSP of the mission: the
level is made again on it, the player where he stands (h3_level.c) */
static void segment_check(real_point3d const *point)
{
	float p[3];
	long bsp, made = h3_level_made_bsp();
	char const *setting = getenv("HALO_H3_SEGMENTS");

	long k;

	/* (a zone set's trigger volume entered - not stood in -: that set) */
	for (k = 0; k < h3s.zone_set_volume_count; k++)
	{
		boolean inside = volume_holds(h3s.zone_set_volumes[k].volume, point);

		if (inside && !h3s.zone_set_volumes[k].inside)
			zone_set_switch(h3s.zone_set_volumes[k].zone_set, "its trigger volume");
		h3s.zone_set_volumes[k].inside = inside;
	}
	if ((setting && !atoi(setting)) || made == NONE || h3s.switching || h3s.zone_set == NONE)
		return;
	p[0] = point->x;
	p[1] = point->y;
	p[2] = point->z + 0.3f;
	/* (of the BSPs the zone set loads) */
	bsp = h3_level_point_bsp(p, h3s.zone_set_bsps[h3s.zone_set]);
	/* (three ticks in a row in another BSP's cluster - where he was at the
	first of them is where he stands in the level made for it: past the made
	BSP's edge there is no ground under him) */
	if (bsp == NONE || bsp == made || (h3s.outside_checks > 0 && bsp != h3s.outside_bsp))
	{
		h3s.outside_checks = 0;
		return;
	}
	if (!h3s.outside_checks)
	{
		memcpy(h3s.outside_point, p, sizeof(h3s.outside_point));
		h3s.outside_bsp = bsp;
	}
	if (++h3s.outside_checks < 3)
		return;
	memcpy(p, h3s.outside_point, sizeof(p));
	{
		long player = local_player_get_player_index(0);
		long unit = player != NONE ? player_get(player)->unit_index : NONE;
		struct object_datum *object = unit != NONE ? object_try_and_get(unit) : NULL;
		float facing = object ? atan2f(object->object.forward.j, object->object.forward.i) : 0.0f;

		platform_log("h3 campaign: the player at %.2f %.2f %.2f is in structure BSP %ld (this level is %ld): the level made again on it",
			point->x, point->y, point->z, bsp, made);
		h3_level_segment_request(h3s.map, bsp, p, facing);
		h3s.switching = TRUE;
		h3s.outside_checks = 0;
		h3s.segments++;
		main_set_map_name(h3s.scenario_path);
	}
}

/* (HALO_H3_AI_DEBUG=2) every other biped's vitality, and whether a vector
through its middle from 1.5 units in front meets it */
static void bipeds_debug(void)
{
	struct object_iterator iterator;
	struct object_datum *object;
	long player = local_player_get_player_index(0);
	long player_unit = player != NONE ? player_get(player)->unit_index : NONE;

	object_iterator_new(&iterator, _object_mask_biped, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		real_point3d from;
		real_vector3d vector;
		struct collision_result collision;
		boolean hit;

		if (iterator.index == player_unit)
			continue;
		from = object->object.position;
		from.x += 1.5f;
		from.z += 0.4f;
		vector.i = -3.0f;
		vector.j = 0.0f;
		vector.k = 0.0f;
		hit = collision_test_vector(FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_bipeds_bit) |
			FLAG(_collision_test_front_facing_surfaces_bit), &from, &vector, player_unit, &collision);
		platform_log("h3 campaign: biped %08lx %s at %.2f %.2f %.2f: body %.2f of %.0f, shield %.2f of %.0f; a vector through it %s (node %d)",
			(unsigned long)iterator.index, tag_get_name(object->definition_index), object->object.position.x, object->object.position.y,
			object->object.position.z, object->object.body_vitality, object->object.maximum_body_vitality, object->object.shield_vitality,
			object->object.maximum_shield_vitality, hit && collision.type == _collision_result_object ?
			(collision.object_index == iterator.index ? "meets it" : "meets another object") : hit ? "meets the structure" : "meets nothing",
			hit ? (int)collision.node_index : -1);
	}
}

/* the first trigger volume an expression tests the players in that was not walked to yet (NONE) */
static long wait_volume(unsigned long datum, unsigned char const *walked, int depth)
{
	unsigned char const *e = expression_at(datum);
	unsigned long argument;
	short function;

	if (!e || depth > 12 || !expression_is_group(e))
		return NONE;
	function = group_function(e, &argument);
	if (function == _h3s_volume_test_players)
	{
		unsigned char const *volume = expression_at(argument);
		long index = volume ? (long)(short)(get32(volume + 0x10) & 0xFFFF) : NONE;

		return index >= 0 && index < h3s.volume_count && !walked[index] ? index : NONE;
	}
	for (; argument != 0xFFFFFFFFUL && expression_at(argument); argument = expression_next(expression_at(argument)))
	{
		long found = wait_volume(argument, walked, depth + 1);

		if (found != NONE)
			return found;
	}
	return NONE;
}

/* (a test's, HALO_H3_AI_AUTOWALK=ticks) the player put on the ground in the
next trigger volume a script waits for him in, every so many ticks: a
mission's encounters walked through with no one playing */
static void autowalk(void)
{
	static unsigned char walked[H3S_MAXIMUM_VOLUMES];
	static long walked_for;
	char const *setting = getenv("HALO_H3_AI_AUTOWALK");
	long every = setting ? atol(setting) : 0, i, volume = NONE, by = NONE;
	long player = local_player_get_player_index(0);
	long unit = player != NONE ? player_get(player)->unit_index : NONE;
	static boolean met;

	if (h3s.clock <= 1)
		met = FALSE;
	/* (HALO_H3_AI_AUTOWALK_MEET=distance: once a squad that is not the
	player's side is placed, the player stood that far from its first starting
	location, on the ground, and the walk ended - a fight to look at) */
	if (every > 0 && unit != NONE && !h3s.switching && !met && getenv("HALO_H3_AI_AUTOWALK_MEET"))
	{
		struct scenario *scenario = global_scenario_get();
		float distance = (float)atof(getenv("HALO_H3_AI_AUTOWALK_MEET"));

		for (i = 0; i < h3s.squad_count && !met; i++)
		{
			struct h3s_squad const *q = &h3s.squads[i];
			struct encounter_definition *encounter;
			struct squad_definition *squad;
			struct actor_starting_location *location;
			real_point3d from, place, top;
			real_vector3d down, forward, up;
			struct collision_result collision;
			float dx, dy, d;

			if (!q->placed || q->spawned < 2 || q->encounter == NONE || !scenario || q->encounter >= scenario->ai_encounters.count)
				continue;
			encounter = TAG_BLOCK_GET_ELEMENT(&scenario->ai_encounters, q->encounter, struct encounter_definition);
			if (encounter->team_index == CE_TEAM_HUMAN || encounter->team_index == CE_TEAM_PLAYER || encounter->squads.count < 1)
				continue;
			squad = TAG_BLOCK_GET_ELEMENT(&encounter->squads, 0, struct squad_definition);
			if (squad->starting_locations.count < 1)
				continue;
			location = TAG_BLOCK_GET_ELEMENT(&squad->starting_locations, 0, struct actor_starting_location);
			object_get_origin(unit, &from);
			dx = from.x - location->position.x;
			dy = from.y - location->position.y;
			d = sqrtf(dx * dx + dy * dy);
			if (d < 0.01f)
				dx = 1.0f, dy = 0.0f, d = 1.0f;
			place = location->position;
			place.x += distance * dx / d;
			place.y += distance * dy / d;
			top = place;
			top.z += 2.0f;
			down.i = down.j = 0.0f;
			down.k = -8.0f;
			if (collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit), &top, &down, NONE,
				&collision))
			{
				place = collision.point;
			}
			place.z += 0.15f;
			forward.i = -dx / d;
			forward.j = -dy / d;
			forward.k = 0.0f;
			up.i = up.j = 0.0f;
			up.k = 1.0f;
			object_set_position(unit, &place, &forward, &up);
			met = TRUE;
			platform_log("h3 campaign: autowalk: the player stood %.1f from %s at %.2f %.2f %.2f: the walk ends", distance, q->name, place.x,
				place.y, place.z);
		}
	}
	if (every < 1 || unit == NONE || h3s.switching || met || (h3s.clock % every) != 0)
		return;
	if (walked_for != h3s.segments + 1 && h3s.clock <= every)
	{
		memset(walked, 0, sizeof(walked));
		walked_for = h3s.segments + 1;
	}
	for (i = 0; i < h3s.script_count && volume == NONE; i++)
	{
		struct h3s_script const *t = &h3s.scripts[i];

		if (t->running && t->pc >= 0 && t->pc < h3s.step_count && h3s.steps[t->pc].kind == _step_wait &&
			/* (the encounters' own scripts: not the dialogue's, music's and markers') */
			strncmp(t->name, "md_", 3) && strncmp(t->name, "music", 5) && strncmp(t->name, "nav_", 4) && strncmp(t->name, "obj_", 4))
		{
			volume = wait_volume((unsigned long)h3s.steps[t->pc].a, walked, 0);
			by = i;
		}
	}
	if (volume == NONE)
		return;
	walked[volume] = TRUE;
	{
		struct h3s_volume const *v = &h3s.volumes[volume];
		real_point3d top, place;
		real_vector3d down;
		struct collision_result collision;
		int k;
		float centre[3];

		for (k = 0; k < 3; k++)
			centre[k] = v->corner[k] + 0.5f * v->extents[0] * v->forward[k] + 0.5f * v->extents[1] * v->left[k] + (v->extents[2] - 0.2f) * v->up[k];
		top.x = centre[0];
		top.y = centre[1];
		top.z = centre[2];
		down.i = down.j = 0.0f;
		down.k = -(v->extents[2] + 6.0f);
		place = top;
		if (collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_structure_bit), &top, &down, NONE,
			&collision))
		{
			place = collision.point;
			place.z += 0.15f;
		}
		object_set_position(unit, &place, NULL, NULL);
		platform_log("h3 campaign: autowalk: the player to %s (%.2f %.2f %.2f), which %s waits for", v->name, place.x, place.y, place.z,
			h3s.scripts[by].name);
	}
}

void h3_campaign_update(void)
{
	long tick, i;
	real_point3d point;

	if (!h3s.gathered || !h3_level_direct_loaded())
		return;
	tick = (long)game_time_get();
	if (h3s.switching)
	{
		/* (the old level's last ticks: nothing; the new one's first: go on) */
		if (!h3s.arriving || tick > h3s.last_tick)
		{
			h3s.last_tick = tick;
			return;
		}
		h3s.switching = h3s.arriving = FALSE;
		h3_level_segment_arrived();
		arrival_take();
		arrival_place();
	}
	/* (the game begun again - a death's restart -: the scripts from their
	start, or as the player came into this structure BSP) */
	else if (h3s.started && tick < h3s.last_tick && h3s.arrival.taken)
		arrival_restore();
	else if (!h3s.started || tick < h3s.last_tick)
		campaign_start();
	h3s.last_tick = tick;
	/* (not before a player stands in the level) */
	if (!player_position(&point))
		return;
	h3s.clock++;
	for (i = 0; i < h3s.script_count; i++)
	{
		if (h3s.scripts[i].running)
			script_run(i, h3s.clock);
	}
	segment_check(&point);
	autowalk();
	if (h3s.log && getenv("HALO_H3_AI_DEBUG") && atoi(getenv("HALO_H3_AI_DEBUG")) >= 2 && (h3s.clock % 150) == 0)
		bipeds_debug();
	/* (HALO_H3_AI_DEBUG: where each script's thread is, once it has settled) */
	if (h3s.log && h3s.clock == 120)
	{
		static char const *const kinds[] = { "end", "wait", "sleep", "place", "erase", "kill", "wake", "call", "set", "jump", "jump unless", "zone set", "allegiance" };

		for (i = 0; i < h3s.script_count; i++)
		{
			struct h3s_script const *t = &h3s.scripts[i];

			if (t->type == 0 || t->running)
			{
				char text[200];

				text[0] = 0;
				if (t->running && t->pc >= 0 && t->pc < h3s.step_count && h3s.steps[t->pc].kind == _step_wait)
					expression_print((unsigned long)h3s.steps[t->pc].a, text, sizeof(text), 0);
				platform_log("h3 campaign: thread %s (type %d): %s, at step %ld of its %ld.. (%s), depth %d, asleep until %ld %s", t->name, (int)t->type,
					t->running ? "running" : "ended", t->pc - t->first_step, t->first_step,
					t->pc >= 0 && t->pc < h3s.step_count && h3s.steps[t->pc].kind <= _step_allegiance ? kinds[h3s.steps[t->pc].kind] : "?",
					(int)t->depth, t->wake_tick, text);
			}
		}
	}
}

#else

void h3_campaign_gather(struct h3_cache *cache, unsigned long scenario)
{
	(void)cache;
	(void)scenario;
}

void h3_campaign_squad_encounter(long h3_squad, short encounter)
{
	(void)h3_squad;
	(void)encounter;
}

void h3_campaign_clear(void)
{
}

void h3_campaign_update(void)
{
}

#endif
