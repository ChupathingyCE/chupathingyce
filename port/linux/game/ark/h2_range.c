/*
h2_range.c: the Halo 2 test range - a lab laid on top of a loaded MCC map, for checking AI, weapons,
damage, hit registration and vehicles the same way in this engine and in the real one (the H2EK's
halo2_tag_test; see tools/h2range/README.md).

TEST BUILDS ONLY: the whole file is compiled out of release and Vita builds, as the campaign
harness's test commands are (HALO_RELEASE). No map is written or converted: a layout (a plain text
file of names and polar positions, tools/h2range/layouts/*.txt) is applied at run time, spawning
the level's own actors, weapons and vehicles by tag name.

  HALO_H2_RANGE=<layout>           the layout, spawned once the level is up
  HALO_H2_RANGE_SCRIPT=<script>    a test script, run line by line after it (see run_script)
  HALO_H2_RANGE_OUT=<folder>       where range-results.jsonl and range-rounds.tsv go (default: .)
  @range_load <layout>             spawn a layout                  @range_variants [filter]  the level's actors
  @range_fire <weapon> <target> <pulls> [rate|ttk] [aim=center|head|feet] [interval=N]
  @range_ai <groupA> vs <groupB> [seconds] [ai=on|off]
  @range_crash <vehicle> <speed wu/s> <fwd|down>
  @range_log on|off                @range_run <script>             @range_tags <group> [filter]
  @range_heal <name>               @range_summary

The measurements are the same ones the real engine's scripts produce (tools/h2range/compare.py).
*/

#if !defined(HALO_RELEASE) && !defined(HALO_VITA)

#include "cseries.h"
#include "cache/cache_files.h"
#include "ai/actors.h"
#include "game/game_allegiance.h"
#include "game/game.h"
#include "game/players.h"
#include "game/player_control.h"
#include "hs/hs.h"
#include "objects/damage.h"
#include "objects/damage_resistances.h"
#include "objects/object_definitions.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "physics/collision_model_definitions.h"
#include "scenario/scenario.h"
#include "tag_files/tag_groups.h"
#include "units/units.h"
#include "items/projectiles.h"
#include "items/projectile_definitions.h"
#include "math/real_math.h"
#include "devices/devices.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (the host's own files - scripts, layouts, results - not the game's translated paths) */
#undef fopen

#define H2_ALIGN __attribute__((force_align_arg_pointer))

void platform_log(char const *format, ...);
long h2_ai_range_variants(char const *filter);
long h2_ai_range_squads(char const *filter);
long h2_ai_range_spawn(char const *character, char const *weapon, real_point3d const *position, real facing, short team,
	short rank, boolean braindead, long *out_actor_index, char *why, size_t why_size);
long hs_runtime_end_all_threads(void);
long hs_runtime_sleep_all_script_threads(void);
void h2_ai_range_see(long actor_index, long unit_index);
short h2_ai_range_squad(char const *name, int place, long *actors, short maximum);
void h2_ai_range_describe(long actor_index, char *out, size_t size);
void h2_ai_range_animations(long actor_index, char *base, char *action, size_t size);
static void range_anim_log(long const *actors, int count);
void unit_kill(long unit_index);
void h2_range_inject(char const *command, long delay);
void actor_braindead(long actor_index, boolean braindead);
boolean cinematic_in_progress(void);
void main_skip_cinematic(void);

#define RANGE_MAXIMUM_THINGS 96
#define RANGE_MAXIMUM_LINES 256
#define RANGE_TICKS_PER_SECOND 30

enum { THING_ACTOR = 1, THING_WEAPON, THING_VEHICLE };

struct range_thing
{
	char name[32], group[32], what1[64], what2[64];
	int kind;
	real distance, bearing;     /* wu, degrees left of the lane's forward */
	real face;                  /* degrees; 1000 = toward the origin, 1001 = away */
	short team;
	short rank;                 /* 3: the regular character (default), 4: its major (rank=major) - the squad's "major upgrade" */
	long object_index;
	long actor_index;
	real_point3d position;
};

struct range_state
{
	boolean have_origin, origin_fixed;
	real_point3d origin;
	real yaw;                    /* radians */
	struct range_thing things[RANGE_MAXIMUM_THINGS];
	int thing_count;
	int missing;
	boolean log_rounds;
	FILE *results, *rounds;
	char out_folder[256];
	long player_unit;
	/* (the script) */
	char lines[RANGE_MAXIMUM_LINES][200];
	int line_count, line_next;
	boolean busy;
	int step;
	long step_tick;
	char current[200];
	boolean env_started;
	/* (the trigger) */
	real trigger;
};

static struct range_state R = { 0 };

/* ------------------------------------------------------------------ the measurement */

struct range_test
{
	boolean active;
	char kind[8], weapon[48], target[32];
	long target_object, player_unit;
	real distance;
	long first_fire_tick, kill_tick, start_tick;
	long fired, on_target, other_object, structure, nothing;
	long damage_events, direct_events;
	double shield_damage, body_damage;
	double shield_fraction, body_fraction;      /* the same, of the target's maximum shield / body */
	real maximum_shield, maximum_body;
	boolean heal;
	char first_hit_region[40], first_hit_material[40];
	char other_names[160];
	long regions_hit[8];
	char region_names[8][40];
};

static struct range_test T;

static char const *tag_short(long tag_index)
{
	char const *name = tag_index != NONE ? tag_get_name(tag_index) : "-";
	char const *slash = strrchr(name, '\\');

	return slash ? slash + 1 : name;
}

static void rounds_open(void)
{
	if (!R.rounds)
	{
		char path[300];

		snprintf(path, sizeof(path), "%s/range-rounds.tsv", R.out_folder[0] ? R.out_folder : ".");
		R.rounds = fopen(path, "a");
	}
}

static void rounds_line(char const *format, ...) __attribute__((format(printf, 1, 2)));
static void rounds_line(char const *format, ...)
{
	char line[600];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	if (R.log_rounds)
		platform_log("range: %s", line);
	rounds_open();
	if (R.rounds)
		fprintf(R.rounds, "%s\n", line), fflush(R.rounds);
}

static struct range_thing *thing_find(char const *name)
{
	int i;

	for (i = 0; i < R.thing_count; i++)
	{
		if (!strcmp(R.things[i].name, name))
			return &R.things[i];
	}
	return NULL;
}

static struct damage_resistance const *collision_resistance(long object_index)
{
	struct object_datum *object = object_try_and_get(object_index);
	struct object_definition *definition;

	if (!object)
		return NULL;
	definition = object_definition_get(object->definition_index);
	if (definition->object.collision_model.index == NONE)
		return NULL;
	return &collision_model_definition_get(definition->object.collision_model.index)->resistance;
}

static char const *material_name(long object_index, short material)
{
	struct damage_resistance const *resistance = collision_resistance(object_index);

	if (!resistance || material < 0 || material >= resistance->materials.count)
		return "-";
	return TAG_BLOCK_GET_ELEMENT(&resistance->materials, material, struct damage_resistance_material)->name;
}

static char const *region_name(long object_index, short region)
{
	struct damage_resistance const *resistance = collision_resistance(object_index);

	if (!resistance || region < 0 || region >= resistance->regions.count)
		return "-";
	return (char const *)resistance->regions.address + 0x54 * region;   /* (damage.c's struct damage_region: the name first) */
}

static real distance_from_player(real_point3d const *point)
{
	real_point3d origin;

	if (R.player_unit == NONE || !object_try_and_get(R.player_unit))
		return 0.0f;
	object_get_origin(R.player_unit, &origin);
	return (real)sqrt((point->x - origin.x) * (point->x - origin.x) + (point->y - origin.y) * (point->y - origin.y) +
		(point->z - origin.z) * (point->z - origin.z));
}

/* a projectile of the player's unit made (a round fired) */
H2_ALIGN void h2_range_projectile_new(long projectile_index)
{
	struct object_datum *projectile;

	if (!T.active || T.player_unit == NONE || !(projectile = object_try_and_get(projectile_index)))
		return;
	if (getenv("HALO_H2_RANGE_DEBUG"))
		platform_log("range: projectile %s owner %lx (player %lx) trigger %.1f", tag_short(projectile->definition_index),
			(unsigned long)projectile->object.owner_object_index, (unsigned long)T.player_unit, (double)R.trigger);
	if (projectile->object.owner_object_index != T.player_unit)
		return;
	if (!T.fired)
		T.first_fire_tick = game_time_get();
	T.fired++;
}

/* a projectile struck something: what, where, which collision region and material */
H2_ALIGN void h2_range_shot(long projectile_index, struct collision_result const *collision)
{
	struct object_datum *projectile = object_try_and_get(projectile_index);
	char const *what = "nothing";
	long target;
	real err_degrees;

	if (!T.active || !projectile || projectile->object.owner_object_index != T.player_unit)
		return;
	target = T.target_object;
	if (collision->type == _collision_result_object && collision->object_index != NONE && object_try_and_get(collision->object_index))
	{
		long ultimate = object_get_ultimate_parent(collision->object_index);

		if (collision->object_index == target || ultimate == target)
		{
			T.on_target++;
			what = "TARGET";
			if (!T.first_hit_region[0])
			{
				snprintf(T.first_hit_region, sizeof(T.first_hit_region), "%s", region_name(collision->object_index, collision->region_index));
				snprintf(T.first_hit_material, sizeof(T.first_hit_material), "%s", material_name(collision->object_index, collision->material_index));
			}
			{
				int k;
				char const *region = region_name(collision->object_index, collision->region_index);

				for (k = 0; k < 8; k++)
				{
					if (!T.region_names[k][0])
						snprintf(T.region_names[k], sizeof(T.region_names[k]), "%s", region);
					if (!strcmp(T.region_names[k], region))
					{
						T.regions_hit[k]++;
						break;
					}
				}
			}
		}
		else
		{
			T.other_object++;
			what = "object";
			if (strlen(T.other_names) < 120)
				snprintf(T.other_names + strlen(T.other_names), sizeof(T.other_names) - strlen(T.other_names), "%s ",
					tag_short(object_get(collision->object_index)->definition_index));
		}
	}
	else if (collision->type == _collision_result_structure)
	{
		T.structure++;
		what = "structure";
	}
	else
		T.nothing++;
	/* (how far the round's own heading is off the line from the player's eye to the target's centre, degrees: the spread) */
	err_degrees = -1.0;
	if (T.target_object != NONE && object_try_and_get(T.target_object) && R.player_unit != NONE && object_try_and_get(R.player_unit))
	{
		real_point3d eye;
		real_vector3d to_target, heading = projectile->object.translational_velocity;

		object_get_origin(R.player_unit, &eye);
		eye.z += 0.6f;
		vector_from_points3d(&eye, &object_get(T.target_object)->object.bounding_sphere_center, &to_target);
		if (normalize3d(&to_target) > 0.0f && normalize3d(&heading) > 0.0f)
		{
			real_vector3d across;
			real dot = dot_product3d(&to_target, &heading);

			cross_product3d(&to_target, &heading, &across);
			err_degrees = (real)(atan2((double)magnitude3d(&across), (double)dot) * 180.0 / 3.14159265358979);
		}
	}
	rounds_line("shot\t%ld\t%s\t%s\t%s\tregion=%s\tmaterial=%s\tnode=%d\tdist=%.2f\terr=%.2f", (long)game_time_get(), tag_short(projectile->definition_index), what,
		collision->type == _collision_result_object && collision->object_index != NONE && object_try_and_get(collision->object_index) ?
		tag_short(object_get(collision->object_index)->definition_index) : (collision->type == _collision_result_structure ? "bsp" : "-"),
		collision->type == _collision_result_object ? region_name(collision->object_index, collision->region_index) : "-",
		collision->type == _collision_result_object ? material_name(collision->object_index, collision->material_index) : "-",
		(int)collision->node_index, (double)distance_from_player(&collision->point), (double)err_degrees);
}

/* damage an object took */
H2_ALIGN void h2_range_damage(long object_index, struct damage_data const *damage, real shield_before, real body_before, short node_index,
	short region_index, short material_index, boolean direct)
{
	struct object_datum *object = object_try_and_get(object_index);
	double shield_loss, body_loss;

	if (!T.active || !object || (object_index != T.target_object && object_get_ultimate_parent(object_index) != T.target_object))
		return;
	/* (the dead are no more hurt: what a corpse takes is not counted) */
	if (T.kill_tick && game_time_get() > T.kill_tick)
		return;
	/* (in units of the object's maximum vitality, so both engines' numbers are alike) */
	/* (what was there to take: a body past empty is not damage dealt) */
	shield_loss = (double)(shield_before - (object->object.shield_vitality > 0.0f ? object->object.shield_vitality : 0.0f)) * object->object.maximum_shield_vitality;
	body_loss = (double)(body_before - (object->object.body_vitality > 0.0f ? object->object.body_vitality : 0.0f)) * object->object.maximum_body_vitality;
	T.damage_events++;
	if (direct)
		T.direct_events++;
	T.shield_damage += shield_loss;
	T.body_damage += body_loss;
	T.shield_fraction += (double)(shield_before - (object->object.shield_vitality > 0.0f ? object->object.shield_vitality : 0.0f));
	T.body_fraction += (double)(body_before - (object->object.body_vitality > 0.0f ? object->object.body_vitality : 0.0f));
	if (object->object.body_vitality <= 0.0f || TEST_FLAG(object->object.damage_flags, _object_dead_bit))
	{
		if (!T.kill_tick)
			T.kill_tick = game_time_get();
	}
	rounds_line("damage\t%ld\t%s\tshield %.2f->%.2f\tbody %.2f->%.2f\tregion=%s\tmaterial=%s\t%s\tregion_damage=%d\tdestroyed=%04x", (long)game_time_get(),
		damage->definition_index != NONE ? tag_short(damage->definition_index) : "-",
		(double)(shield_before * object->object.maximum_shield_vitality), (double)(object->object.shield_vitality * object->object.maximum_shield_vitality),
		(double)(body_before * object->object.maximum_body_vitality), (double)(object->object.body_vitality * object->object.maximum_body_vitality),
		region_name(object_index, region_index), material_name(object_index, material_index), direct ? "direct" : "parent",
		region_index >= 0 ? (int)object_region_damage_get(object_index, region_index) : -1, (unsigned)object->object.regions_destroyed_flags);
}

H2_ALIGN real h2_range_trigger_override(void)
{
	return R.env_started ? R.trigger : -1.0f;
}

/* ------------------------------------------------------------------ results */

static void results_open(void)
{
	if (!R.results)
	{
		char path[300];

		snprintf(path, sizeof(path), "%s/range-results.jsonl", R.out_folder[0] ? R.out_folder : ".");
		R.results = fopen(path, "a");
	}
}

static void results_write(char const *json)
{
	results_open();
	platform_log("range: RESULT %s", json);
	if (R.results)
		fprintf(R.results, "%s\n", json), fflush(R.results);
}

/* ------------------------------------------------------------------ the world */

static boolean player_unit_get(void)
{
	long player_index = local_player_get_player_index(0);

	R.player_unit = player_index != NONE ? player_get(player_index)->unit_index : NONE;
	return R.player_unit != NONE && object_try_and_get(R.player_unit) != NULL;
}

static boolean ground_at(real_point3d *point, real above)
{
	struct collision_result collision;
	real_point3d from = *point;
	real_vector3d down = { 0.0f, 0.0f, -40.0f };

	from.z += above;
	if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit), &from, &down, NONE, &collision))
	{
		*point = collision.point;
		return TRUE;
	}
	return FALSE;
}

static void polar_position(struct range_thing const *thing, real_point3d *out)
{
	real angle = R.yaw + thing->bearing * (real)(3.14159265358979 / 180.0);

	out->x = R.origin.x + thing->distance * (real)cos(angle);
	out->y = R.origin.y + thing->distance * (real)sin(angle);
	out->z = R.origin.z;
	if (!ground_at(out, 1.5f))
		out->z = R.origin.z;
}

static long tag_find_named(long group, char const *needle)
{
	struct tag_iterator tags;
	long tag_index, found = NONE;

	tag_iterator_new(&tags, group);
	while ((tag_index = tag_iterator_next(&tags)) != NONE)
	{
		if (strstr(tag_get_name(tag_index), needle))
		{
			if (!strncmp(tag_get_name(tag_index), "h2\\", 3))
				return tag_index;
			if (found == NONE)
				found = tag_index;
		}
	}
	return found;
}

static void face_object(long object_index, real_point3d const *at, real degrees)
{
	real yaw;
	real_vector3d forward, up = { 0.0f, 0.0f, 1.0f };
	real_point3d position;

	if (degrees >= 1000.0f)
	{
		real dx = (degrees == 1000.0f ? 1.0f : -1.0f) * (R.origin.x - at->x), dy = (degrees == 1000.0f ? 1.0f : -1.0f) * (R.origin.y - at->y);

		yaw = (real)atan2(dy, dx);
	}
	else
		yaw = degrees * (real)(3.14159265358979 / 180.0);
	forward.i = (real)cos(yaw);
	forward.j = (real)sin(yaw);
	forward.k = 0.0f;
	object_get_origin(object_index, &position);
	object_set_position(object_index, &position, &forward, &up);
}

static boolean thing_spawn(struct range_thing *thing)
{
	real_point3d at;
	char why[160] = "";

	polar_position(thing, &at);
	thing->position = at;
	thing->object_index = NONE;
	thing->actor_index = NONE;
	if (thing->kind == THING_ACTOR)
	{
		long actor_index = NONE;
		real_point3d lifted = at;
		short team = thing->team;

		/* (no team= : the character's own side - the carrier squad's team would make a Jackal a Marine's friend, and a
		friend's hit is not an enemy's in the damage rules) */
		if (team < 0)
		{
			char const *c = thing->what1;

			team = strstr(c, "marine") || strstr(c, "odst") || strstr(c, "johnson") || strstr(c, "miranda") || strstr(c, "sergeant") ||
				strstr(c, "pilot") ? 2 : strstr(c, "flood") || strstr(c, "infection") || strstr(c, "carrier") ? 4 :
				strstr(c, "sentinel") || strstr(c, "enforcer") ? 5 : 3;
		}

		lifted.z += 0.05f;
		thing->object_index = h2_ai_range_spawn(thing->what1, thing->what2, &lifted,
			(thing->face >= 1000.0f ? (real)atan2((thing->face == 1000.0f ? 1 : -1) * (R.origin.y - at.y), (thing->face == 1000.0f ? 1 : -1) * (R.origin.x - at.x)) :
				thing->face * (real)(3.14159265358979 / 180.0)),
			team, thing->rank, TRUE, &actor_index, why, sizeof(why));
		thing->actor_index = actor_index;
		if (thing->object_index == NONE)
		{
			platform_log("range: MISSING %s (%s %s): %s", thing->name, thing->what1, thing->what2, why);
			return FALSE;
		}
		face_object(thing->object_index, &lifted, thing->face);
		/* (placed actors settle: their feet are on the ground) */
	}
	else
	{
		long definition = tag_find_named(thing->kind == THING_WEAPON ? 'weap' : 'vehi', thing->what1);
		struct object_placement_data placement;
		real yaw;

		if (definition == NONE)
		{
			platform_log("range: MISSING %s: no %s tag has \"%s\" in this level", thing->name, thing->kind == THING_WEAPON ? "weapon" : "vehicle", thing->what1);
			return FALSE;
		}
		object_placement_data_new(&placement, definition, NONE);
		placement.position = at;
		placement.position.z += thing->kind == THING_WEAPON ? 0.15f : 0.4f;
		yaw = thing->face >= 1000.0f ? (real)atan2(R.origin.y - at.y, R.origin.x - at.x) : thing->face * (real)(3.14159265358979 / 180.0);
		if (thing->face == 1001.0f)
			yaw += (real)3.14159265358979;
		placement.forward.i = (real)cos(yaw);
		placement.forward.j = (real)sin(yaw);
		placement.forward.k = 0.0f;
		placement.up.i = placement.up.j = 0.0f;
		placement.up.k = 1.0f;
		thing->object_index = object_new(&placement);
		if (thing->object_index == NONE)
		{
			platform_log("range: MISSING %s: object_new failed for %s", thing->name, tag_get_name(definition));
			return FALSE;
		}
	}
	platform_log("range: spawned %s %s at %.2f %.2f %.2f (%.1f wu, %.0f deg)%s%s", thing->kind == THING_ACTOR ? "actor" : thing->kind == THING_WEAPON ? "weapon" : "vehicle",
		thing->name, (double)at.x, (double)at.y, (double)at.z, (double)thing->distance, (double)thing->bearing,
		thing->object_index != NONE ? ": " : "", thing->object_index != NONE ? tag_get_name(object_get(thing->object_index)->definition_index) : "");
	return TRUE;
}

static void things_clear(void)
{
	int i;

	for (i = 0; i < R.thing_count; i++)
	{
		if (R.things[i].object_index != NONE && object_try_and_get(R.things[i].object_index))
			object_delete(R.things[i].object_index);
	}
	R.thing_count = R.missing = 0;
}

static boolean layout_read(char const *name)
{
	char path[512], line[256];
	FILE *file;
	char const *folder = getenv("HALO_H2_RANGE_LAYOUTS");

	if (strchr(name, '/'))
		snprintf(path, sizeof(path), "%s", name);
	else
		snprintf(path, sizeof(path), "%s/%s%s", folder ? folder : "tools/h2range/layouts", name, strstr(name, ".txt") ? "" : ".txt");
	file = fopen(path, "r");
	if (!file)
	{
		platform_log("range: cannot read layout %s", path);
		return FALSE;
	}
	R.thing_count = 0;
	R.origin_fixed = FALSE;
	while (fgets(line, sizeof(line), file))
	{
		char word[16], a[64], b[64], c[64], d[64], e[64];
		int n;
		char *hash = strchr(line, '#');

		if (hash)
			*hash = 0;
		n = sscanf(line, "%15s %63s %63s %63s %63s %63s", word, a, b, c, d, e);
		if (n < 2)
			continue;
		if (!strcmp(word, "origin"))
		{
			if (!strcmp(a, "player"))
				R.origin_fixed = FALSE;
			else if (sscanf(line, "%*s %f %f %f %f", &R.origin.x, &R.origin.y, &R.origin.z, &R.yaw) == 4)
			{
				R.yaw *= (real)(3.14159265358979 / 180.0);
				R.origin_fixed = TRUE;
			}
		}
		else if (!strcmp(word, "actor") || !strcmp(word, "weapon") || !strcmp(word, "vehicle"))
		{
			struct range_thing *t;
			char const *rest;
			int fields;
			char *tok;
			char copy[256];

			if (R.thing_count >= RANGE_MAXIMUM_THINGS)
				break;
			t = &R.things[R.thing_count];
			memset(t, 0, sizeof(*t));
			t->object_index = t->actor_index = NONE;
			t->face = 1000.0f;
			t->team = -1;
			snprintf(t->name, sizeof(t->name), "%s", a);
			if (!strcmp(word, "actor"))
			{
				t->kind = THING_ACTOR;
				fields = sscanf(line, "%*s %*s %63s %63s %f %f", t->what1, t->what2, &t->distance, &t->bearing);
				if (fields < 4)
					continue;
			}
			else
			{
				t->kind = !strcmp(word, "weapon") ? THING_WEAPON : THING_VEHICLE;
				fields = sscanf(line, "%*s %*s %63s %f %f", t->what1, &t->distance, &t->bearing);
				if (fields < 3)
					continue;
			}
			snprintf(copy, sizeof(copy), "%s", line);
			for (tok = strtok(copy, " \t\r\n"), rest = NULL; tok; tok = strtok(NULL, " \t\r\n"))
			{
				if (!strncmp(tok, "team=", 5))
					t->team = !strcmp(tok + 5, "covenant") ? 3 : !strcmp(tok + 5, "human") ? 2 : !strcmp(tok + 5, "player") ? 1 :
						!strcmp(tok + 5, "flood") ? 4 : !strcmp(tok + 5, "sentinel") ? 5 : !strcmp(tok + 5, "heretic") ? 6 :
						!strcmp(tok + 5, "prophet") ? 7 : -1;
				else if (!strncmp(tok, "rank=", 5))
					t->rank = !strcmp(tok + 5, "major") ? 4 : 3;
				else if (!strncmp(tok, "group=", 6))
					snprintf(t->group, sizeof(t->group), "%s", tok + 6);
				else if (!strncmp(tok, "face=", 5))
					t->face = !strcmp(tok + 5, "player") ? 1000.0f : !strcmp(tok + 5, "away") ? 1001.0f : (real)atof(tok + 5);
			}
			(void)rest;
			(void)b; (void)c; (void)d; (void)e;
			R.thing_count++;
		}
	}
	fclose(file);
	platform_log("range: layout %s: %d things", path, R.thing_count);
	return TRUE;
}


/* @range_scan [radius [step]]: where a lab fits around the player - level ground (the first solid below a point 12 wu
above the player's level) with eye-high sight of a fan of 24 degrees (bearings -12, 0, +12 of a yaw) and the
ground along it, at 10, 20 and 40 wu out, within 4 wu of the origin's level, an eye-high line to a target standing on it clear: the best few are logged as "origin x y z
yaw" lines (a layout's origin line takes them) */
/* "spot X Y Z": standing places round a point (an actor's start): at 5/10/15/20/30 wu in 16 directions, level ground whose eye
(0.62 up) sees the point's chest (0.5 up) with nothing of the structure between */
static void range_spot(real x, real y, real z)
{
	static const real distances[] = { 5.0f, 10.0f, 15.0f, 20.0f, 30.0f };
	unsigned long const flags = FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit);
	int d, k;

	for (k = 0; k < (int)NUMBEROF(distances); k++)
	{
		for (d = 0; d < 16; d++)
		{
			real yaw = (real)d * (real)(3.14159265358979 / 8.0);
			real_point3d p, eye, chest;
			real_vector3d down = { 0.0f, 0.0f, -8.0f }, to;
			struct collision_result collision;

			p.x = x + distances[k] * cosf(yaw), p.y = y + distances[k] * sinf(yaw), p.z = z + 4.0f;
			if (!collision_test_vector(flags, &p, &down, NONE, &collision) || collision.plane.n.k < 0.9f)
				continue;
			p = collision.point;
			eye = p, eye.z += 0.62f;
			chest.x = x, chest.y = y, chest.z = z + 0.5f;
			to.i = chest.x - eye.x, to.j = chest.y - eye.y, to.k = chest.z - eye.z;
			if (collision_test_vector(flags, &eye, &to, NONE, &collision))
				continue;
			platform_log("range: spot %.0f wu: %.2f %.2f %.2f yaw %.0f", (double)distances[k], (double)p.x, (double)p.y, (double)p.z,
				(double)(yaw * 57.29578f + 180.0f));
		}
	}
}

static void range_scan(int radius, int step)
{
	real_point3d base;
	int x, y, kept = 0, i;
	struct { real flat, x, y, z, yaw; } best[8];
	unsigned long const flags = FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit);
	long grounded = 0, level = 0;

	memset(best, 0, sizeof(best));
	if (!player_unit_get())
		return;
	object_get_origin(R.player_unit, &base);
	platform_log("range: scan: from %.1f %.1f %.2f", (double)base.x, (double)base.y, (double)base.z);
	for (x = -radius; x <= radius; x += step)
	{
		for (y = -radius; y <= radius; y += step)
		{
			real_point3d p = base;
			struct collision_result collision;
			real_vector3d down = { 0.0f, 0.0f, -24.0f };
			int d;

			p.x += (real)x;
			p.y += (real)y;
			p.z += 12.0f;
			if (!collision_test_vector(flags, &p, &down, NONE, &collision))
				continue;
			p = collision.point;
			if (collision.plane.n.k < 0.95f)
				continue;
			grounded++;
			for (d = 0; d < 16; d++)
			{
				real yaw = (real)d * (real)(3.14159265358979 / 8.0), flat = 0.0f;
				int b;
				boolean ok = TRUE;

				for (b = -1; b <= 1 && ok; b++)
				{
					real angle = yaw + (real)b * (real)(3.14159265358979 / 15.0);
					real_point3d from = p;
					int k;

					from.z += 0.62f;
					for (k = 0; k < 3 && ok; k++)
					{
						static real const along[3] = { 10.0f, 20.0f, 40.0f };
						real_point3d probe = p;
						struct collision_result ground;
						real dz;
						real_vector3d probe_down = { 0.0f, 0.0f, -14.0f };
						real_vector3d sight;

						probe.x += along[k] * (real)cos(angle);
						probe.y += along[k] * (real)sin(angle);
						probe.z += 6.0f;
						if (!collision_test_vector(flags, &probe, &probe_down, NONE, &ground) || ground.plane.n.k < 0.9f)
						{
							ok = FALSE;
							break;
						}
						dz = ground.point.z - p.z;
						sight.i = ground.point.x - from.x;
						sight.j = ground.point.y - from.y;
						sight.k = ground.point.z + 0.3f - from.z;
						if (dz < 0.0f)
							dz = -dz;
						if (dz > 4.0f || collision_test_vector(flags, &from, &sight, NONE, &collision))
							ok = FALSE;
						if (dz > flat)
							flat = dz;
					}
				}
				if (!ok)
					continue;
				level++;
				for (i = 0; i < 8; i++)
				{
					if (i >= kept || flat < best[i].flat)
					{
						int j;

						for (j = 7; j > i; j--)
							best[j] = best[j - 1];
						best[i].flat = flat, best[i].x = p.x, best[i].y = p.y, best[i].z = p.z, best[i].yaw = yaw;
						if (kept < 8)
							kept++;
						break;
					}
				}
			}
		}
	}
	platform_log("range: scan: %ld ground points, %ld level fans", grounded, level);
	for (i = 0; i < kept; i++)
		platform_log("range: scan: origin %.1f %.1f %.2f %.0f   (ground varies %.2f wu over the fan)", (double)best[i].x, (double)best[i].y,
			(double)best[i].z + 0.05, (double)(best[i].yaw * 180.0f / 3.14159265f), (double)best[i].flat);
	if (!kept)
		platform_log("range: scan: no open fan of 24 degrees within %d wu", radius);
}

/* @range_sight [yaw_deg]: how far a level shot (eye height) goes before the structure, for bearings -90..+90 in steps
of 10 around a yaw (default: the player's facing) - where a layout's lanes may go */
static void range_sight(real yaw_degrees, boolean have_yaw)
{
	real_point3d eye;
	int bearing;
	char line[400] = "";
	real yaw;

	if (!player_unit_get())
		return;
	object_get_origin(R.player_unit, &eye);
	eye.z += 0.6f;
	yaw = have_yaw ? yaw_degrees * (real)(3.14159265358979 / 180.0) : player_control_get(0)->desired_angles.yaw;
	for (bearing = -90; bearing <= 90; bearing += 10)
	{
		real angle = yaw + (real)bearing * (real)(3.14159265358979 / 180.0), d = 60.0f;
		real_vector3d ray = { 60.0f * (real)cos(angle), 60.0f * (real)sin(angle), 0.0f };
		struct collision_result collision;

		if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit), &eye, &ray, NONE, &collision))
			d = collision.t * 60.0f;
		snprintf(line + strlen(line), sizeof(line) - strlen(line), " %d:%.0f", bearing, (double)d);
	}
	platform_log("range: sight from %.1f %.1f %.2f yaw %.0f (bearing:wu)%s", (double)eye.x, (double)eye.y, (double)eye.z - 0.6, (double)(yaw * 180.0f / 3.14159265f), line);
}

/* the player's eye and facing */
static void player_stand(real_point3d const *at, real yaw)
{
	real_vector3d forward, up = { 0.0f, 0.0f, 1.0f }, look;
	real_point3d position = *at;

	forward.i = (real)cos(yaw);
	forward.j = (real)sin(yaw);
	forward.k = 0.0f;
	position.z += 0.05f;
	object_set_position(R.player_unit, &position, &forward, &up);
	look = forward;
	player_control_set_facing(0, &look);
}

static void player_aim_at(real_point3d const *point)
{
	real_point3d eye;
	real_vector3d direction;

	object_get_origin(R.player_unit, &eye);
	eye.z += 0.6f;
	vector_from_points3d(&eye, point, &direction);
	if (normalize3d(&direction) > 0.0f && valid_real_normal3d(&direction))
		player_control_set_facing(0, &direction);
}

/* refill a thing's shield and body (a corpse cannot be) */
static boolean object_refill(long object_index)
{
	struct object_datum *object = object_try_and_get(object_index);

	if (!object || TEST_FLAG(object->object.damage_flags, _object_dead_bit))
		return FALSE;
	object->object.shield_vitality = 1.0f;
	object->object.body_vitality = 1.0f;
	return TRUE;
}

/* ------------------------------------------------------------------ the tests */

static void aim_point_of(struct range_thing const *thing, char const *aim, real_point3d *point)
{
	struct object_datum *object = object_get(thing->object_index);

	*point = object->object.bounding_sphere_center;
	/* (aim=m:MARKER: at one of its markers - the Enforcer's r_shield_gen_target) */
	if (!strncmp(aim, "m:", 2))
	{
		struct object_marker marker;

		if (object_get_marker_by_name(thing->object_index, aim + 2, &marker, 1) > 0)
			*point = marker.matrix.position;
		return;
	}
	if (!strcmp(aim, "head"))
		point->z += 0.45f * object->object.bounding_sphere_radius;
	else if (!strcmp(aim, "feet"))
		point->z -= 0.6f * object->object.bounding_sphere_radius;
}

static char const *arg_value(char const *line, char const *key)
{
	char const *p = strstr(line, key);

	return p ? p + strlen(key) : NULL;
}

/* one fire test: the player pulls the trigger `pulls` times (a tap each, as the real engine's runner clicks), aimed at
the target; per pull the damage the target took is sampled - the measure both engines can give (the real one has no
per-round log in AI mode) */
#define FIRE_MAXIMUM_PULLS 64
struct test_fire
{
	char weapon[48], target[32], mode[8], aim[40];
	int pulls, interval, done_pulls, hold;   /* (hold: ticks the trigger is held each pull, 1 a tap) */
	long next_pull, trigger_off, end_tick;
	int kill_pull;           /* the pull in whose window the target died (0: it did not) */
	/* (the window of the pull now: what the counters were when it began) */
	long window_fired, window_on, window_events;
	double window_shield, window_body, window_shield_fraction, window_body_fraction;
	double shield_loss[FIRE_MAXIMUM_PULLS], body_loss[FIRE_MAXIMUM_PULLS], shield_frac[FIRE_MAXIMUM_PULLS], body_frac[FIRE_MAXIMUM_PULLS];
	long rounds[FIRE_MAXIMUM_PULLS], on_target[FIRE_MAXIMUM_PULLS], events[FIRE_MAXIMUM_PULLS];
	int window_open;
};
static struct test_fire F;

/* (a weapon's usual gap between taps, ticks: the real runner's table) */
static int fire_interval_default(char const *weapon)
{
	static struct { char const *name; real seconds; } const table[] = {
		{ "magnum", 0.34f }, { "battle_rifle", 0.62f }, { "smg", 0.09f }, { "shotgun", 1.1f }, { "sniper", 0.7f }, { "rocket", 1.2f },
		{ "plasma_pistol", 0.3f }, { "needler", 0.16f }, { "plasma_rifle", 0.12f }, { "carbine", 0.34f }, { "beam", 0.7f },
		{ "flak", 1.1f }, { "brute_shot", 0.7f }, { "brute_plasma", 0.1f } };
	int i;

	for (i = 0; i < (int)(sizeof(table) / sizeof(table[0])); i++)
	{
		if (strstr(weapon, table[i].name))
			return (int)(table[i].seconds * RANGE_TICKS_PER_SECOND + 0.5f) > 1 ? (int)(table[i].seconds * RANGE_TICKS_PER_SECOND + 0.5f) : 2;
	}
	return 20;
}

static void fire_window_close(void)
{
	int n = F.done_pulls - 1;

	if (!F.window_open || n < 0 || n >= FIRE_MAXIMUM_PULLS)
		return;
	F.window_open = 0;
	F.shield_loss[n] = T.shield_damage - F.window_shield;
	F.body_loss[n] = T.body_damage - F.window_body;
	F.shield_frac[n] = T.shield_fraction - F.window_shield_fraction;
	F.body_frac[n] = T.body_fraction - F.window_body_fraction;
	F.rounds[n] = T.fired - F.window_fired;
	F.on_target[n] = T.on_target - F.window_on;
	F.events[n] = T.damage_events - F.window_events;
	rounds_line("pull\t%d\tshield %.2f\tbody %.2f\trounds %ld\ton_target %ld\tevents %ld", n + 1, F.shield_loss[n], F.body_loss[n], F.rounds[n], F.on_target[n], F.events[n]);
}

static void fire_finish(void)
{
	char json[2200];
	long ticks_to_kill = T.kill_tick && T.first_fire_tick ? T.kill_tick - T.first_fire_tick : 0;
	int k, n, pulls_hit = 0, pulls = F.done_pulls < FIRE_MAXIMUM_PULLS ? F.done_pulls : FIRE_MAXIMUM_PULLS, killed_pull = F.kill_pull;
	double shield_sum = 0.0, body_sum = 0.0, rounds_sum = 0.0, on_sum = 0.0, shield_frac_sum = 0.0, body_frac_sum = 0.0;
	char regions[200] = "";

	for (n = 0; n < pulls; n++)
	{
		shield_sum += F.shield_loss[n];
		body_sum += F.body_loss[n];
		shield_frac_sum += F.shield_frac[n];
		body_frac_sum += F.body_frac[n];
		rounds_sum += (double)F.rounds[n];
		on_sum += (double)F.on_target[n];
		if (F.shield_loss[n] > 0.0 || F.body_loss[n] > 0.0)
			pulls_hit++;
	}
	if (killed_pull > 0 && killed_pull < pulls)
		pulls = killed_pull;       /* (the pulls up to the kill: what a runner that stops at the kill counts) */
	for (k = 0; k < 8 && T.region_names[k][0]; k++)
		snprintf(regions + strlen(regions), sizeof(regions) - strlen(regions), "%s%s:%ld", k ? "," : "", T.region_names[k], T.regions_hit[k]);
	snprintf(json, sizeof(json),
		"{\"engine\":\"ours\",\"kind\":\"fire\",\"weapon\":\"%s\",\"target\":\"%s\",\"dist\":%.1f,\"mode\":\"%s\",\"aim\":\"%s\",\"pulls\":%d,"
		"\"pulls_hit\":%d,\"hit_pull_rate\":%.3f,\"shield_frac_per_pull\":%.4f,\"body_frac_per_pull\":%.4f,\"max_shield\":%.1f,\"max_body\":%.1f,\"shield_per_pull\":%.3f,\"body_per_pull\":%.3f,\"shield_per_hit_pull\":%.3f,\"body_per_hit_pull\":%.3f,"
		"\"killed\":%s,\"ttk_s\":%.2f,\"pulls_to_kill\":%d,"
		"\"rounds\":%ld,\"on_target\":%ld,\"round_hit_rate\":%.3f,\"rounds_per_pull\":%.2f,\"shield_per_round\":%.3f,\"body_per_round\":%.3f,"
		"\"other_object\":%ld,\"structure\":%ld,\"regions\":\"%s\",\"first_region\":\"%s\",\"first_material\":\"%s\",\"blocked_by\":\"%s\"}",
		T.weapon, T.target, (double)T.distance, F.mode, F.aim, pulls, pulls_hit, pulls > 0 ? (double)pulls_hit / pulls : 0.0,
		pulls > 0 ? shield_frac_sum / pulls : 0.0, pulls > 0 ? body_frac_sum / pulls : 0.0, (double)T.maximum_shield, (double)T.maximum_body,
		pulls > 0 ? shield_sum / pulls : 0.0, pulls > 0 ? body_sum / pulls : 0.0, pulls_hit > 0 ? shield_sum / pulls_hit : 0.0, pulls_hit > 0 ? body_sum / pulls_hit : 0.0,
		T.kill_tick ? "true" : "false", (double)ticks_to_kill / RANGE_TICKS_PER_SECOND, T.kill_tick ? killed_pull : 0,
		T.fired, T.on_target, T.fired > 0 ? (double)T.on_target / (double)T.fired : 0.0, pulls > 0 ? rounds_sum / pulls : 0.0,
		T.on_target > 0 ? T.shield_damage / (double)T.on_target : 0.0, T.on_target > 0 ? T.body_damage / (double)T.on_target : 0.0,
		T.other_object, T.structure, regions, T.first_hit_region, T.first_hit_material, T.other_names);
	(void)on_sum;
	results_write(json);
	T.active = FALSE;
	R.trigger = -1.0f;
}

/* returns TRUE while the test runs */
static boolean test_fire_update(void)
{
	long now = game_time_get();
	struct range_thing *thing = thing_find(F.target);
	real_point3d aim;

	if (R.step == 0)
	{
		memset(&T, 0, sizeof(T));
		if (!thing)
		{
			platform_log("range: fire: no target %s in the layout", F.target);
			return FALSE;
		}
		/* (a target a test before killed is put back: its corpse deleted, a fresh actor in its place; a living one is topped up) */
		if (thing->object_index != NONE && object_try_and_get(thing->object_index) &&
			TEST_FLAG(object_get(thing->object_index)->object.damage_flags, _object_dead_bit))
		{
			object_delete(thing->object_index);
			thing->object_index = NONE;
		}
		if (thing->object_index == NONE || !object_try_and_get(thing->object_index))
			thing_spawn(thing);
		else
			object_refill(thing->object_index);
		if (thing->object_index == NONE || !object_try_and_get(thing->object_index))
		{
			platform_log("range: fire: target %s is missing", F.target);
			return FALSE;
		}
		if (!player_unit_get())
		{
			platform_log("range: fire: no player unit");
			return FALSE;
		}
		T.player_unit = R.player_unit;
		T.target_object = thing->object_index;
		T.distance = thing->distance;
		snprintf(T.weapon, sizeof(T.weapon), "%s", F.weapon);
		snprintf(T.target, sizeof(T.target), "%s", F.target);
		T.heal = !strcmp(F.mode, "rate");
		T.maximum_shield = object_get(thing->object_index)->object.maximum_shield_vitality;
		T.maximum_body = object_get(thing->object_index)->object.maximum_body_vitality;
		{
			struct object_datum *o = object_get(thing->object_index);
			real_point3d feet;

			object_get_origin(thing->object_index, &feet);
			platform_log("range: target %s: %s, shield %.1f body %.1f, centre %.2f above its feet, radius %.2f", F.target, tag_get_name(o->definition_index),
				(double)T.maximum_shield, (double)T.maximum_body, (double)(o->object.bounding_sphere_center.z - feet.z), (double)o->object.bounding_sphere_radius);
		}
		/* (the player where the layout puts it, facing the target; the weapon in its hands) */
		{
			char command[120];

			player_stand(&R.origin, (real)atan2(thing->position.y - R.origin.y, thing->position.x - R.origin.x));
			snprintf(command, sizeof(command), "@weapon 0 %s", F.weapon);
			h2_range_inject(command, 1);
		}
		R.step = 1;
		R.step_tick = now;
		return TRUE;
	}
	if (!thing || thing->object_index == NONE || !object_try_and_get(thing->object_index))
		return FALSE;
	aim_point_of(thing, F.aim, &aim);
	if (R.step == 1)
	{
		/* (the weapon raised and the player settled) */
		player_aim_at(&aim);
		if (now - R.step_tick >= 60)
		{
			T.active = TRUE;
			T.start_tick = now;
			F.next_pull = now;
			F.done_pulls = 0;
			F.trigger_off = 0;
			F.window_open = 0;
			R.step = 2;
		}
		return TRUE;
	}
	if (R.step == 2)
	{
		struct object_datum *target = object_get(thing->object_index);

		player_aim_at(&aim);
		if (F.trigger_off && now >= F.trigger_off)
		{
			R.trigger = 0.0f;
			F.trigger_off = 0;
		}
		if (!F.trigger_off && F.done_pulls < F.pulls && now >= F.next_pull)
		{
			/* (the last pull's window ends here; a shield or body nearly gone is made full again, so that the
			numbers stay those of a healthy target, as the real runner does) */
			fire_window_close();
			if (T.heal)
			{
				if (T.maximum_shield > 0.0f && target->object.shield_vitality < 0.7f)
					target->object.shield_vitality = target->object.body_vitality = 1.0f;
			}
			F.window_fired = T.fired, F.window_on = T.on_target, F.window_events = T.damage_events;
			F.window_shield = T.shield_damage, F.window_body = T.body_damage;
			F.window_shield_fraction = T.shield_fraction, F.window_body_fraction = T.body_fraction;
			F.window_open = 1;
			R.trigger = 1.0f;
			F.trigger_off = now + (F.hold > 1 ? F.hold : 1);
			F.next_pull = now + F.interval;
			F.done_pulls++;
		}
		if (T.kill_tick && !F.kill_pull)
			F.kill_pull = F.done_pulls;
		if (T.kill_tick || (F.done_pulls >= F.pulls && !F.trigger_off))
		{
			if (!F.end_tick)
				F.end_tick = now + 45;      /* (rounds still flying land) */
			if (now >= F.end_tick)
			{
				R.trigger = 0.0f;
				fire_window_close();
				fire_finish();
				return FALSE;
			}
		}
		if (now - T.start_tick > 90 * RANGE_TICKS_PER_SECOND)
		{
			platform_log("range: fire: timed out");
			fire_window_close();
			fire_finish();
			return FALSE;
		}
		return TRUE;
	}
	return FALSE;
}

/* ---- AI vs AI */

struct test_ai
{
	char a[32], b[32];
	int seconds;
	boolean ai_on;
	long start;
	int a_total, b_total;
	long first_death, wipe_tick;
	char winner[8];
};
static struct test_ai A;

static void group_count(char const *group, int *living, int *total, double *shield, double *body)
{
	int i;

	*living = *total = 0;
	*shield = *body = 0.0;
	for (i = 0; i < R.thing_count; i++)
	{
		struct object_datum *object;

		if (R.things[i].kind != THING_ACTOR || strcmp(R.things[i].group, group))
			continue;
		(*total)++;
		object = R.things[i].object_index != NONE ? object_try_and_get(R.things[i].object_index) : NULL;
		if (object && !TEST_FLAG(object->object.damage_flags, _object_dead_bit) && object->object.body_vitality > 0.0f)
		{
			(*living)++;
			*shield += object->object.shield_vitality * object->object.maximum_shield_vitality;
			*body += object->object.body_vitality * object->object.maximum_body_vitality;
		}
	}
}

static boolean test_ai_update(void)
{
	long now = game_time_get();
	int a_living, b_living, at, bt, i;
	double as, ab, bs, bb;

	if (R.step == 0)
	{
		int found = 0;

		memset(&T, 0, sizeof(T));
		/* (a fresh start: everyone alive, full, and the AI as asked) */
		for (i = 0; i < R.thing_count; i++)
		{
			struct range_thing *t = &R.things[i];

			if (t->kind != THING_ACTOR || (strcmp(t->group, A.a) && strcmp(t->group, A.b)))
				continue;
			found++;
			if (t->object_index == NONE || !object_try_and_get(t->object_index) ||
				TEST_FLAG(object_get(t->object_index)->object.damage_flags, _object_dead_bit))
				thing_spawn(t);
			else
				object_refill(t->object_index);
			if (t->actor_index != NONE)
				actor_braindead(t->actor_index, !A.ai_on);
		}
		if (!found)
		{
			platform_log("range: ai: no actors in groups %s / %s", A.a, A.b);
			return FALSE;
		}
		R.step = 1;
		A.start = now;
		A.first_death = A.wipe_tick = 0;
		A.winner[0] = 0;
		group_count(A.a, &a_living, &A.a_total, &as, &ab);
		group_count(A.b, &b_living, &A.b_total, &bs, &bb);
		return TRUE;
	}
	/* (the actors that were braindead until now wake: the dead of the first tick are not) */
	group_count(A.a, &a_living, &at, &as, &ab);
	group_count(A.b, &b_living, &bt, &bs, &bb);
	if (!A.first_death && (a_living < at || b_living < bt))
		A.first_death = now;
	if ((now - A.start) % RANGE_TICKS_PER_SECOND == 0)
		rounds_line("ai\t%ld\t%s=%d(%.0f/%.0f)\t%s=%d(%.0f/%.0f)", (long)(now - A.start) / RANGE_TICKS_PER_SECOND, A.a, a_living, as, ab, A.b, b_living, bs, bb);
	if ((a_living == 0 || b_living == 0 || now - A.start >= A.seconds * RANGE_TICKS_PER_SECOND) && A.ai_on)
	{
		char json[600];

		A.wipe_tick = now;
		snprintf(A.winner, sizeof(A.winner), "%s", a_living == 0 && b_living == 0 ? "both" : a_living == 0 ? "B" : b_living == 0 ? "A" : "none");
		snprintf(json, sizeof(json),
			"{\"engine\":\"ours\",\"kind\":\"ai\",\"a\":\"%s\",\"b\":\"%s\",\"a_total\":%d,\"b_total\":%d,\"a_living\":%d,\"b_living\":%d,\"winner\":\"%s\","
			"\"seconds\":%.1f,\"first_death_s\":%.1f,\"a_shield\":%.1f,\"a_body\":%.1f,\"b_shield\":%.1f,\"b_body\":%.1f}",
			A.a, A.b, at, bt, a_living, b_living, A.winner, (double)(now - A.start) / RANGE_TICKS_PER_SECOND,
			A.first_death ? (double)(A.first_death - A.start) / RANGE_TICKS_PER_SECOND : -1.0, as, ab, bs, bb);
		results_write(json);
		return FALSE;
	}
	if (!A.ai_on && now - A.start >= A.seconds * RANGE_TICKS_PER_SECOND)
		return FALSE;
	return TRUE;
}

/* ---- vehicle crash */

struct test_crash
{
	char vehicle[48], dir[8];
	real speed;
	long vehicle_object;
	real body_before, min_body;
	long start, spawn_tick;
	real wall_bearing, wall_distance;
};
static struct test_crash C;

static boolean test_crash_update(void)
{
	long now = game_time_get();
	struct object_datum *object = C.vehicle_object != NONE ? object_try_and_get(C.vehicle_object) : NULL;

	if (R.step == 0)
	{
		long definition = tag_find_named('vehi', C.vehicle);
		struct object_placement_data placement;
		real_point3d at = R.origin;
		real yaw = R.yaw;

		if (definition == NONE)
		{
			platform_log("range: crash: no vehicle tag has \"%s\"", C.vehicle);
			return FALSE;
		}
		if (!player_unit_get())
			return FALSE;
		/* (the player well out of the way: well behind) */
		{
			real_point3d p = R.origin;

			p.x -= 6.0f * (real)cos(R.yaw);
			p.y -= 6.0f * (real)sin(R.yaw);
			ground_at(&p, 2.0f);
			player_stand(&p, R.yaw);
		}
		C.wall_distance = -1.0f;
		if (!strcmp(C.dir, "fwd"))
		{
			/* (the nearest wall within 40 wu among 24 bearings, chest high) */
			int b;
			real best = 1.0e9f;

			for (b = 0; b < 24; b++)
			{
				real angle = (real)b * (real)(3.14159265358979 * 2.0 / 24.0);
				real_vector3d ray = { 40.0f * (real)cos(angle), 40.0f * (real)sin(angle), 0.0f };
				real_point3d from = R.origin;
				struct collision_result collision;

				from.z += 1.0f;
				if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit), &from, &ray, NONE, &collision))
				{
					real d = collision.t * 40.0f;

					if (d < best)
						best = d, C.wall_bearing = angle;
				}
			}
			if (best > 39.0f)
			{
				platform_log("range: crash fwd: no wall within 40 wu of the origin");
				return FALSE;
			}
			C.wall_distance = best;
			yaw = C.wall_bearing;
			/* (the vehicle at the origin, the wall `best` wu away along its bearing: that run is logged) */
		}
		else
		{
			ground_at(&at, 1.0f);
			at.z += 5.0f;
		}
		object_placement_data_new(&placement, definition, NONE);
		placement.position = at;
		if (strcmp(C.dir, "down"))
			placement.position.z += 0.5f;
		placement.forward.i = (real)cos(yaw);
		placement.forward.j = (real)sin(yaw);
		placement.forward.k = 0.0f;
		placement.up.i = placement.up.j = 0.0f;
		placement.up.k = 1.0f;
		C.vehicle_object = object_new(&placement);
		if (C.vehicle_object == NONE)
			return FALSE;
		C.spawn_tick = now;
		R.step = 1;
		return TRUE;
	}
	if (!object)
		return FALSE;
	if (R.step == 1)
	{
		/* (settled for a second: the vehicle at rest, full body) */
		if (now - C.spawn_tick < 2)
			return TRUE;
		if (!strcmp(C.dir, "down"))
		{
			object->object.translational_velocity.i = object->object.translational_velocity.j = 0.0f;
			object->object.translational_velocity.k = -C.speed / (real)RANGE_TICKS_PER_SECOND;
		}
		else
		{
			object->object.translational_velocity.i = C.speed / (real)RANGE_TICKS_PER_SECOND * object->object.forward.i;
			object->object.translational_velocity.j = C.speed / (real)RANGE_TICKS_PER_SECOND * object->object.forward.j;
			object->object.translational_velocity.k = 0.0f;
		}
		object->object.flags &= ~FLAG(_object_at_rest_bit);
		C.body_before = object->object.body_vitality;
		C.min_body = C.body_before;
		C.start = now;
		R.step = 2;
		return TRUE;
	}
	if (object->object.body_vitality < C.min_body)
		C.min_body = object->object.body_vitality;
	if (now - C.start == 5 * RANGE_TICKS_PER_SECOND || TEST_FLAG(object->object.damage_flags, _object_dead_bit))
	{
		char json[500];

		snprintf(json, sizeof(json),
			"{\"engine\":\"ours\",\"kind\":\"crash\",\"vehicle\":\"%s\",\"speed\":%.1f,\"dir\":\"%s\",\"health_before\":%.3f,\"health_after\":%.3f,"
			"\"damage_fraction\":%.3f,\"min_health\":%.3f,\"wall_distance\":%.1f,\"dead\":%s}",
			C.vehicle, (double)C.speed, C.dir, (double)C.body_before, (double)object->object.body_vitality,
			(double)(C.body_before - object->object.body_vitality), (double)C.min_body, (double)C.wall_distance,
			TEST_FLAG(object->object.damage_flags, _object_dead_bit) ? "true" : "false");
		results_write(json);
		object_delete(C.vehicle_object);
		C.vehicle_object = NONE;
		return FALSE;
	}
	return TRUE;
}

/* ------------------------------------------------------------------ the script */

H2_ALIGN void h2_range_command(char const *text);

static void script_add(char const *line)
{
	if (R.line_count < RANGE_MAXIMUM_LINES)
		snprintf(R.lines[R.line_count++], sizeof(R.lines[0]), "%s", line);
}

static void script_file_add(char const *path)
{
	FILE *file = fopen(path, "r");
	char line[200];

	if (!file)
	{
		platform_log("range: cannot read script %s", path);
		return;
	}
	while (fgets(line, sizeof(line), file))
	{
		char *end = line + strlen(line);
		char *hash = strchr(line, '#');

		if (hash)
			*hash = 0, end = hash;
		while (end > line && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t'))
			*--end = 0;
		if (line[0])
			script_add(line);
	}
	fclose(file);
}

/* ---- watch: one actor over time - where it is, how fast it moves, the animation it plays, its action, status and
voice (the AI / animation / sound audit, tools/h2range/tests/audit_*.txt). "watch NAME SECONDS [see=S] [flee=S]
[unflee=S] [kill=S] [every=TICKS]": NAME spawned fresh and awake, the player made known to it at second S (see=), the
ai_debug_flee_always switch on / off (flee=, unflee=), killed (kill=); a row every TICKS (3) to range-track.tsv:
tick, seconds, name, x y z, horizontal speed wu/s over the row's ticks, animation, frame, action, combat status,
target type, fire state, emotions. Its voice's lines go to the same file (h2_range_voice). */

struct test_watch
{
	char name[32];
	int seconds, every;
	int see, flee, unflee, kill, kill2, shoot2, shoot, shootn, shot;
	char other[48];
	long start;
	real_point3d last;
	boolean have_last;
	/* (audit13) orders=NAME: the squad's orders set right after it is placed; moves=T:X,Y,Z;...: the player stood
	at X Y Z at second T of the watch (allies following the player) - a "player" row each second */
	char orders[48];
	int move_count, move_next;
	struct { float t, x, y, z; } moves[16];
	/* (audit14) quiet=GROUP: that squad group erased each second of the watch (a quiet test: the level's scripts go on
	placing its squads) */
	char quiet[48];
};
static struct test_watch W;
static struct { long object_index; int every; char name[48]; } DL = { NONE, 0, "" };
static FILE *track_file;

static void track_line(char const *format, ...) __attribute__((format(printf, 1, 2)));
static void track_line(char const *format, ...)
{
	va_list arguments;

	if (!track_file)
	{
		char path[300];

		snprintf(path, sizeof(path), "%s/range-track.tsv", R.out_folder[0] ? R.out_folder : ".");
		track_file = fopen(path, "a");
		if (!track_file)
			return;
	}
	va_start(arguments, format);
	vfprintf(track_file, format, arguments);
	va_end(arguments);
	fputc('\n', track_file);
	fflush(track_file);
}

/* (unit_dialogue.c) a unit's vocalization chosen, the Halo 2 line it speaks ("-": none) */
H2_ALIGN void h2_range_voice(long unit_index, short vocalization_type, char const *type_name, char const *line)
{
	int i;
	char const *who = "?";
	char unit_name[16];

	if (!R.env_started)
		return;
	for (i = 0; i < R.thing_count; i++)
	{
		if (R.things[i].object_index == unit_index)
			who = R.things[i].name;
	}
	if (!strcmp(who, "?"))
	{
		snprintf(unit_name, sizeof(unit_name), "u%lx", (unsigned long)unit_index);
		who = unit_name;
	}
	track_line("voice\t%ld\t%.2f\t%s\t%d\t%s\t%s", (long)game_time_get(), (double)(game_time_get() - W.start) / RANGE_TICKS_PER_SECOND, who,
		(int)vocalization_type, type_name ? type_name : "?", line ? line : "-");
}

/* (source/ai: RANGE_STAT) a behaviour's decision - a grenade considered / thrown, a dive tried, a surprise, an evade ... -
one row to range-track.tsv: "stat TICK S ACTOR KEY VALUE" */
H2_ALIGN void h2_range_stat(long actor_index, char const *key, long value)
{
	if (!R.env_started)
		return;
	track_line("stat\t%ld\t%.2f\ta%lx\t%s\t%ld", (long)game_time_get(), (double)(game_time_get() - W.start) / RANGE_TICKS_PER_SECOND,
		(unsigned long)actor_index, key, value);
}

/* "watch @SQUAD ...": a level squad placed (its own orders and zones), every actor of it tracked as SQUAD/N */
static boolean test_watch_squad_update(void)
{
	long now = game_time_get();
	long second = (now - W.start) / RANGE_TICKS_PER_SECOND;
	boolean on_second = R.step != 0 && (now - W.start) % RANGE_TICKS_PER_SECOND == 0;
	long actors[32];
	short count, k;
	char line[200];
	static real_point3d last[32];
	static boolean have[32];

	if (R.step == 0)
	{
		hs_compile_and_evaluate("ai_debug_flee_always 0");
		h2_ai_range_squad(W.name + 1, 3, actors, 32);
		count = h2_ai_range_squad(W.name + 1, 1, actors, 32);
		if (W.other[0])
		{
			long others[32];

			h2_ai_range_squad(W.other, 3, others, 32);
			h2_ai_range_squad(W.other, 1, others, 32);
		}
		if (count < 0)
		{
			platform_log("range: watch: no squad %s in this level", W.name + 1);
			return FALSE;
		}
		memset(have, 0, sizeof(have));
		W.start = now;
		R.step = 1;
		track_line("start\t%ld\t0\t%s\t%d actors", (long)now, W.name, (int)count);
		W.move_next = 0;
	}
	/* (the orders two ticks after the placing: the placed squad takes its initial order on its first tick) */
	if (W.orders[0] && now == W.start + 2)
	{
		extern boolean h2_ai_range_set_orders(char const *squad_name, char const *order_name);
		boolean set = h2_ai_range_set_orders(W.name + 1, W.orders);

		track_line("event\t%ld\t%.2f\t%s\torders %s%s", (long)now, (double)(now - W.start) / RANGE_TICKS_PER_SECOND, W.name, W.orders,
			set ? "" : " (unknown)");
	}
	while (W.move_next < W.move_count && (real)(now - W.start) >= W.moves[W.move_next].t * RANGE_TICKS_PER_SECOND && player_unit_get())
	{
		real_point3d at;

		at.x = W.moves[W.move_next].x, at.y = W.moves[W.move_next].y, at.z = W.moves[W.move_next].z;
		player_stand(&at, 0.0f);
		track_line("event\t%ld\t%.2f\t%s\tmove %.2f %.2f %.2f", (long)now, (double)(now - W.start) / RANGE_TICKS_PER_SECOND, W.name,
			(double)at.x, (double)at.y, (double)at.z);
		W.move_next++;
	}
	if (W.quiet[0] && on_second)
		h2_ai_range_squad(W.quiet, 3, NULL, 0);
	if (W.move_count && on_second && player_unit_get())
	{
		real_point3d at;

		object_get_origin(R.player_unit, &at);
		track_line("player\t%ld\t%ld\t%s\t%.3f\t%.3f\t%.3f", (long)now, second, W.name, (double)at.x, (double)at.y, (double)at.z);
	}
	if (R.step == 1 && now == W.start)
		return TRUE;
	count = h2_ai_range_squad(W.name + 1, 0, actors, 32);
	if (count < 0)
		count = 0;
	range_anim_log(actors, count);
	if (on_second && W.see >= 0 && second == W.see && player_unit_get())
	{
		for (k = 0; k < count; k++)
			h2_ai_range_see(actors[k], R.player_unit);
		track_line("event\t%ld\t%ld\t%s\tsee", (long)now, second, W.name);
	}
	if (on_second && W.flee >= 0 && second == W.flee)
	{
		hs_compile_and_evaluate("ai_debug_flee_always 1");
		track_line("event\t%ld\t%ld\t%s\tflee", (long)now, second, W.name);
	}
	if (on_second && W.unflee >= 0 && second == W.unflee)
	{
		hs_compile_and_evaluate("ai_debug_flee_always 0");
		track_line("event\t%ld\t%ld\t%s\tunflee", (long)now, second, W.name);
	}
	/* (shoot2=S: the other squad shot dead - sniper rounds by no one, damage_object, as the real runner does: the real engine's
	Grunts do not react to an ai_kill of their leaders) */
	if (((W.shoot2 >= 0 && second >= W.shoot2 && second < W.shoot2 + 5 && W.other[0]) || (W.shoot >= 0 && second >= W.shoot && second < W.shoot + 5)) &&
		(now - W.start) % 6 == 0)
	{
		long others[32];
		char line[300];
		char const *target = W.shoot >= 0 && second >= W.shoot && second < W.shoot + 5 ? W.name + 1 : W.other;
		short n = h2_ai_range_squad(target, 0, others, 32);

		(void)line;
		if (n > 0)
		{
			/* (as ours' damage_object does - h2_scripts.c: the shield and body depleted, a death by damage) */
			short k;

			for (k = 0; k < n; k++)
			{
				struct actor_datum *leader = (struct actor_datum *)datum_try_and_get(actor_data, others[k]);
				struct object_datum *unit = leader && leader->meta.unit_index != NONE ? object_try_and_get(leader->meta.unit_index) : NULL;

				if (unit && !TEST_FLAG(unit->object.damage_flags, _object_dead_bit) && (W.shootn < 0 || W.shot < W.shootn))
				{
					/* (a sniper round's damage effect, as the real runner's damage_object: pain, death lines and
					the AI's reactions follow a real hit - the shield and body depleted as before when the level
					has no such effect) */
					static long sniper = NONE - 1;

					if (sniper == NONE - 1)
					{
						struct tag_iterator tags;
						long tag;

						sniper = NONE;
						tag_iterator_new(&tags, 'jpt!');
						while ((tag = tag_iterator_next(&tags)) != NONE && sniper == NONE)
						{
							if (tag_get_name(tag) && strstr(tag_get_name(tag), "sniper_rifle_bullet"))
								sniper = tag;
						}
						/* (a Halo 2 level: the sniper round's own impact damage, its converted projectile's) */
						tag_iterator_new(&tags, 'proj');
						while (sniper == NONE && (tag = tag_iterator_next(&tags)) != NONE)
						{
							if (tag_get_name(tag) && strstr(tag_get_name(tag), "sniper") && projectile_definition_get(tag)->projectile.impact_damage.index != NONE)
								sniper = projectile_definition_get(tag)->projectile.impact_damage.index;
						}
						platform_log("range: shots deal %s", sniper != NONE ? tag_get_name(sniper) : "(none: depleted)");
					}
					static long last_target = NONE;
					static int hits;

					hits = last_target == leader->meta.unit_index ? hits + 1 : 1;
					last_target = leader->meta.unit_index;
					if (sniper != NONE && hits <= 5)
					{
						struct damage_data damage;

						damage_data_new(&damage, sniper);
						object_get_origin(leader->meta.unit_index, &damage.epicenter);
						damage.origin = damage.epicenter;
						damage.origin.z += 1.0f;
						damage.direction.i = damage.direction.j = 0.0f, damage.direction.k = -1.0f;
						object_cause_damage(&damage, leader->meta.unit_index, NONE, NONE, NONE, NULL);
					}
					else
					{
						/* (no such effect, or the target took 5 rounds and lives - a Hunter: depleted) */
						track_line("event\t%ld\t%.2f\t%s\tdepleted u%lx", (long)now, (double)(now - W.start) / RANGE_TICKS_PER_SECOND, W.name,
							(unsigned long)leader->meta.unit_index);
						object_deplete_shield(leader->meta.unit_index);
						object_deplete_body(leader->meta.unit_index);
					}
					if (TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
						W.shot++;
					break;
				}
			}
			if (h2_ai_range_squad(target, 0, others, 32) == 0)
				track_line("event\t%ld\t%.2f\t%s\tshot2 %s", (long)now, (double)(now - W.start) / RANGE_TICKS_PER_SECOND, W.name, target);
		}
	}
	if (on_second && W.kill2 >= 0 && second == W.kill2 && W.other[0])
	{
		long others[32];

		h2_ai_range_squad(W.other, 2, others, 32);
		track_line("event\t%ld\t%ld\t%s\tkill2 %s", (long)now, second, W.name, W.other);
	}
	if (on_second && W.kill >= 0 && second == W.kill)
	{
		h2_ai_range_squad(W.name + 1, 2, actors, 32);
		track_line("event\t%ld\t%ld\t%s\tkill", (long)now, second, W.name);
	}
	/* (each second: the living actors of the squad and of the other squad, and every living biped of the level by its tag's
	name - the Flood audit's carriers bursting, combat forms dropping infection forms, bodies getting up: "count TICK S SQ=N
	[OTHER=M] | tag=n ...") */
	/* (each living biped of the last tick that is not now: "gone TICK S TAG dead|deleted KILLER-TAG DAMAGE Z" - the attacker
	that did it the most harm) */
	{
		static long previous[256], previous_tag[256];
		static real previous_z[256];
		static short previous_count;
		long living[256], living_tag[256];
		real living_z[256];
		short living_count = 0, p;
		struct object_iterator iterator;
		struct object_datum *object;

		for (p = 0; p < previous_count; p++)
		{
			struct object_datum *was = object_try_and_get(previous[p]);
			char const *name = tag_get_name(previous_tag[p]), *killer_name = "-";
			long killer = NONE;
			real most = 0.0f;

			if (was && was->definition_index == previous_tag[p] && !TEST_FLAG(was->object.damage_flags, _object_dead_bit))
				continue;
			if (was && was->definition_index == previous_tag[p])
			{
				struct unit_datum *unit = (struct unit_datum *)was;
				struct object_datum *k_object;

				for (k = 0; k < MAXIMUM_ATTACKERS_PER_UNIT; k++)
				{
					if (unit->unit.attackers[k].object_index != NONE && unit->unit.attackers[k].damage_inflicted > most)
						most = unit->unit.attackers[k].damage_inflicted, killer = unit->unit.attackers[k].object_index;
				}
				if (killer != NONE && (k_object = object_try_and_get(killer)) != NULL)
					killer_name = tag_get_name(k_object->definition_index);
			}
			track_line("gone\t%ld\t%.2f\t%s\t%s\t%s\t%s\t%.2f\t%.2f", (long)now, (double)(now - W.start) / RANGE_TICKS_PER_SECOND, W.name,
				name ? (strrchr(name, '\\') ? strrchr(name, '\\') + 1 : name) : "?", was && was->definition_index == previous_tag[p] ? "dead" : "deleted",
				strrchr(killer_name, '\\') ? strrchr(killer_name, '\\') + 1 : killer_name, (double)most, (double)previous_z[p]);
		}
		object_iterator_new(&iterator, FLAG(_object_type_biped), 0);
		while ((object = object_iterator_next(&iterator)) != NULL && living_count < 256)
		{
			if (TEST_FLAG(object->object.damage_flags, _object_dead_bit) || iterator.index == R.player_unit)
				continue;
			living[living_count] = iterator.index;
			living_tag[living_count] = object->definition_index;
			living_z[living_count++] = object->object.position.z;
		}
		memcpy(previous, living, sizeof(long) * living_count);
		memcpy(previous_tag, living_tag, sizeof(long) * living_count);
		memcpy(previous_z, living_z, sizeof(real) * living_count);
		previous_count = living_count;
	}
	if (on_second || R.step == 1)
	{
		struct object_iterator iterator;
		struct object_datum *object;
		char const *names[24];
		int counts[24], kinds = 0, j;
		char census[600];
		size_t used = 0;
		long others[32];
		short n_other = W.other[0] ? h2_ai_range_squad(W.other, 0, others, 32) : -1;

		R.step = 2;
		object_iterator_new(&iterator, FLAG(_object_type_biped), 0);
		while ((object = object_iterator_next(&iterator)) != NULL)
		{
			char const *name = tag_get_name(object->definition_index), *last;

			if (TEST_FLAG(object->object.damage_flags, _object_dead_bit) || !name || iterator.index == R.player_unit)
				continue;
			last = strrchr(name, '\\');
			last = last ? last + 1 : name;
			for (j = 0; j < kinds && strcmp(names[j], last); j++)
				;
			if (j == kinds && kinds < 24)
				names[kinds] = last, counts[kinds++] = 0;
			if (j < kinds)
				counts[j]++;
		}
		census[0] = 0;
		for (j = 0; j < kinds && used < sizeof(census); j++)
			used += snprintf(census + used, sizeof(census) - used, " %s=%d", names[j], counts[j]);
		track_line("count\t%ld\t%.2f\t%s\t%s=%d |%s", (long)now, (double)(now - W.start) / RANGE_TICKS_PER_SECOND, W.name, W.name + 1, (int)count,
			census);
		if (n_other >= 0)
			track_line("count2\t%ld\t%.2f\t%s\t%s=%d", (long)now, (double)(now - W.start) / RANGE_TICKS_PER_SECOND, W.name, W.other, (int)n_other);
	}
	if ((now - W.start) % W.every == 0)
	{
		for (k = 0; k < count && k < 32; k++)
		{
			struct actor_datum *actor = (struct actor_datum *)datum_try_and_get(actor_data, actors[k]);
			long unit_index = actor ? actor->meta.unit_index : NONE;
			real_point3d at;
			real speed = 0.0f;
			char what[300];

			/* (a swarm - the infection forms -: its first units, each a row "SQUAD/K.N" with no actor description) */
			if (actor && actor->meta.swarm)
			{
				long u = actor->meta.swarm_unit_index;
				int n;

				for (n = 0; u != NONE && n < 4 && object_try_and_get(u); n++, u = unit_get(u)->unit.swarm_next_unit_index)
				{
					object_get_origin(u, &at);
					track_line("track\t%ld\t%.2f\t%s/%d.%d\t%.3f\t%.3f\t%.3f\t-\t%s\t-1\tswarm\t%d\t-\t-\tbody=%.2f\tu%lx", (long)now,
						(double)(now - W.start) / RANGE_TICKS_PER_SECOND, W.name + 1, (int)k, n, (double)at.x, (double)at.y, (double)at.z,
						unit_get(u)->unit.melee_attack_state ? "melee" : "-", (int)actor->state.combat_status, (double)unit_get(u)->object.body_vitality,
						(unsigned long)u);
				}
				continue;
			}
			if (unit_index == NONE || !object_try_and_get(unit_index))
				continue;
			object_get_origin(unit_index, &at);
			if (have[k])
				speed = (real)sqrt((at.x - last[k].x) * (at.x - last[k].x) + (at.y - last[k].y) * (at.y - last[k].y)) * RANGE_TICKS_PER_SECOND / (real)W.every;
			last[k] = at;
			have[k] = TRUE;
			h2_ai_range_describe(actors[k], what, sizeof(what));
			track_line("track\t%ld\t%.2f\t%s/%d\t%.3f\t%.3f\t%.3f\t%.3f\t%s\tu%lx", (long)now, (double)(now - W.start) / RANGE_TICKS_PER_SECOND, W.name + 1, (int)k,
				(double)at.x, (double)at.y, (double)at.z, (double)speed, what, (unsigned long)unit_index);
		}
	}
	if (now - W.start >= W.seconds * RANGE_TICKS_PER_SECOND)
	{
		hs_compile_and_evaluate("ai_debug_flee_always 0");
		track_line("end\t%ld\t%.2f\t%s", (long)now, (double)(now - W.start) / RANGE_TICKS_PER_SECOND, W.name);
		h2_ai_range_squad(W.name + 1, 3, actors, 32);
		if (W.other[0])
		{
			long others[32];

			h2_ai_range_squad(W.other, 3, others, 32);
		}
		return FALSE;
	}
	return TRUE;
}

/* (watch, fight) every animation an actor starts that is not plain locomotion - a dive, an evade, a surprise, a throw, a
gesture, a melee, a mode of its own (flee, panic, stunned, cower ...) - one row each to range-track.tsv:
"anim TICK S UNIT CHANNEL NAME ACTION" (the special behaviours' tables, local test runs) */
static struct { long actor; char base[64], action[64]; } range_anim_seen[64];

static boolean range_anim_plain(char const *name)
{
	static char const *const plain[] = { "idle", "move_", "turn_", "aim_", "airborne", "land_", "walk_", "-", "look" };
	char const *state;
	int i;

	if (strncmp(name, "combat:", 7) && strncmp(name, "crouch:", 7) && strncmp(name, "patrol:", 7) && strcmp(name, "-"))
		return FALSE;
	char copy[64];

	/* (a variant's ":varN" is the state's) */
	snprintf(copy, sizeof(copy), "%s", name);
	state = strrchr(copy, ':');
	if (state && !strncmp(state, ":var", 4))
		copy[state - copy] = 0;
	state = strrchr(copy, ':');
	state = state ? state + 1 : copy;
	for (i = 0; i < (int)(sizeof(plain) / sizeof(plain[0])); i++)
	{
		if (!strncmp(state, plain[i], strlen(plain[i])))
			return TRUE;
	}
	return FALSE;
}

static void range_anim_log(long const *actors, int count)
{
	int k, j, free_slot;

	for (k = 0; k < count; k++)
	{
		char base[64], action[64], what[300];
		char *act;

		h2_ai_range_animations(actors[k], base, action, sizeof(base));
		free_slot = -1;
		for (j = 0; j < (int)(sizeof(range_anim_seen) / sizeof(range_anim_seen[0])); j++)
		{
			if (range_anim_seen[j].actor == actors[k])
				break;
			if (free_slot < 0 && range_anim_seen[j].actor == 0)
				free_slot = j;
		}
		if (j == (int)(sizeof(range_anim_seen) / sizeof(range_anim_seen[0])))
		{
			if (free_slot < 0)
				continue;
			j = free_slot;
			range_anim_seen[j].actor = actors[k];
			range_anim_seen[j].base[0] = range_anim_seen[j].action[0] = 0;
		}
		h2_ai_range_describe(actors[k], what, sizeof(what));
		act = strchr(what, '\t');
		act = act ? strchr(act + 1, '\t') : NULL;
		if (act)
		{
			char *end = strchr(++act, '\t');

			if (end)
				*end = 0;
		}
		if (strcmp(base, range_anim_seen[j].base) && !range_anim_plain(base))
			track_line("anim\t%ld\t%.2f\ta%lx\tbase\t%s\t%s", (long)game_time_get(), (double)(game_time_get() - W.start) / RANGE_TICKS_PER_SECOND,
				(unsigned long)actors[k], base, act ? act : "?");
		if (strcmp(action, range_anim_seen[j].action) && strcmp(action, "-"))
			track_line("anim\t%ld\t%.2f\ta%lx\taction\t%s\t%s", (long)game_time_get(), (double)(game_time_get() - W.start) / RANGE_TICKS_PER_SECOND,
				(unsigned long)actors[k], action, act ? act : "?");
		snprintf(range_anim_seen[j].base, sizeof(range_anim_seen[j].base), "%s", base);
		snprintf(range_anim_seen[j].action, sizeof(range_anim_seen[j].action), "%s", action);
	}
}

/* "fight SECONDS SQUAD...": level squads placed together and left to fight SECONDS (their voices to range-track.tsv), then erased */
static struct { int seconds; char squads[6][48]; int count; long start; } FG;

static boolean test_fight_update(void)
{
	long now = game_time_get(), actors[32];
	int i;

	if (R.step == 0)
	{
		for (i = 0; i < FG.count; i++)
		{
			h2_ai_range_squad(FG.squads[i], 3, actors, 32);
			if (h2_ai_range_squad(FG.squads[i], 1, actors, 32) < 0)
				platform_log("range: fight: no squad %s in this level", FG.squads[i]);
		}
		FG.start = now;
		W.start = now;
		R.step = 1;
		track_line("start\t%ld\t0\tfight:%s\t%d squads", (long)now, FG.squads[0], FG.count);
		return TRUE;
	}
	for (i = 0; i < FG.count; i++)
	{
		short n = h2_ai_range_squad(FG.squads[i], 0, actors, 32);

		if (n > 0)
			range_anim_log(actors, n);
	}
	/* (every 2 s each living actor's place and action: "pos", like the real runner's - where each one fights from) */
	if ((now - FG.start) % (getenv("HALO_H2_RANGE_POS_TICKS") ? atoi(getenv("HALO_H2_RANGE_POS_TICKS")) : 2 * RANGE_TICKS_PER_SECOND) == 0)
	{
		for (i = 0; i < FG.count; i++)
		{
			short n = h2_ai_range_squad(FG.squads[i], 0, actors, 32), k;

			for (k = 0; k < n && k < 32; k++)
			{
				struct actor_datum *a = (struct actor_datum *)datum_try_and_get(actor_data, actors[k]);
				char what[400], *action = what;
				int tabs = 0;

				if (!a)
					continue;
				h2_ai_range_describe(actors[k], what, sizeof(what));
				while (*action && tabs < 2)
					tabs += *action++ == '\t';
				/* (its action and combat status) */
				if (strchr(action, '\t') && strchr(strchr(action, '\t') + 1, '\t'))
					*strchr(strchr(action, '\t') + 1, '\t') = 0;
				{
					/* (then its firing position, its path's destination and its squad's order) */
					extern char const *h2_ai_actor_order_name(long actor_index);

					track_line("pos\t%ld\t%.2f\t%s/%d\t%.3f\t%.3f\t%.3f\t%s\tfp %d\tdest %.1f %.1f %.1f\t%s\tdanger %.1f crouch %d", (long)now,
						(double)(now - FG.start) / RANGE_TICKS_PER_SECOND,
						FG.squads[i], k, (double)a->input.position.body_position.x, (double)a->input.position.body_position.y,
						(double)a->input.position.body_position.z, action, (int)a->firing_positions.current_position_index,
						(double)a->control.path.destination.point.x, (double)a->control.path.destination.point.y,
						(double)a->control.path.destination.point.z, h2_ai_actor_order_name(actors[k]),
						(double)a->emotions.perceived_danger, (int)a->control.crouching);
				}
			}
		}
	}
	if ((now - FG.start) % (5 * RANGE_TICKS_PER_SECOND) == 0)
	{
		char counts[200] = "";

		for (i = 0; i < FG.count; i++)
			snprintf(counts + strlen(counts), sizeof(counts) - strlen(counts), " %s=%d", FG.squads[i], (int)h2_ai_range_squad(FG.squads[i], 0, actors, 32));
		track_line("living\t%ld\t%.2f\tfight\t%s", (long)now, (double)(now - FG.start) / RANGE_TICKS_PER_SECOND, counts);
		/* (each squad's median distance from its actors to their nearest living enemy actor of the other squads: where they
		fight from - a Grunt throws inside 2-15 wu) */
		{
			char distances[300] = "";
			int q;

			for (i = 0; i < FG.count; i++)
			{
				long mine[32], theirs[32];
				short n = h2_ai_range_squad(FG.squads[i], 0, mine, 32), k;
				real nearest[32];
				short m = 0;

				for (k = 0; k < n && k < 32; k++)
				{
					struct actor_datum *a = (struct actor_datum *)datum_try_and_get(actor_data, mine[k]);
					real best = -1.0f;

					if (!a)
						continue;
					for (q = 0; q < FG.count; q++)
					{
						short o = q == i ? 0 : h2_ai_range_squad(FG.squads[q], 0, theirs, 32), j;

						for (j = 0; j < o && j < 32; j++)
						{
							struct actor_datum *b = (struct actor_datum *)datum_try_and_get(actor_data, theirs[j]);
							real d;

							if (!b || !game_team_is_enemy(a->meta.team_index, b->meta.team_index))
								continue;
							d = (real)sqrt(distance_squared3d(&a->input.position.body_position, &b->input.position.body_position));
							if (best < 0.0f || d < best)
								best = d;
						}
					}
					if (best >= 0.0f)
						nearest[m++] = best;
				}
				if (m)
				{
					short x, y;

					for (x = 1; x < m; x++)
						for (y = x; y > 0 && nearest[y - 1] > nearest[y]; y--)
						{
							real t = nearest[y];

							nearest[y] = nearest[y - 1];
							nearest[y - 1] = t;
						}
					snprintf(distances + strlen(distances), sizeof(distances) - strlen(distances), " %s=%.1f", FG.squads[i], (double)nearest[m / 2]);
				}
			}
			track_line("distance\t%ld\t%.2f\tfight\t%s", (long)now, (double)(now - FG.start) / RANGE_TICKS_PER_SECOND, distances);
		}
	}
	if (now - FG.start >= FG.seconds * RANGE_TICKS_PER_SECOND)
	{
		track_line("end\t%ld\t%.2f\tfight", (long)now, (double)(now - FG.start) / RANGE_TICKS_PER_SECOND);
		for (i = 0; i < FG.count; i++)
			h2_ai_range_squad(FG.squads[i], 3, actors, 32);
		return FALSE;
	}
	return TRUE;
}

static boolean test_watch_update(void)
{
	long now = game_time_get();
	struct range_thing *t = thing_find(W.name);
	long second = (now - W.start) / RANGE_TICKS_PER_SECOND;
	boolean on_second = R.step != 0 && (now - W.start) % RANGE_TICKS_PER_SECOND == 0;

	if (!t || t->kind != THING_ACTOR)
	{
		platform_log("range: watch: no actor %s in the layout", W.name);
		return FALSE;
	}
	if (R.step == 0)
	{
		int i;

		/* (alone: the layout's other actors, spawned by "load", taken away) */
		for (i = 0; i < R.thing_count; i++)
		{
			if (R.things[i].kind == THING_ACTOR && R.things[i].object_index != NONE && object_try_and_get(R.things[i].object_index))
				object_delete(R.things[i].object_index);
			if (R.things[i].kind == THING_ACTOR)
				R.things[i].object_index = R.things[i].actor_index = NONE;
		}
		if (!thing_spawn(t))
			return FALSE;
		if (t->actor_index != NONE)
			actor_braindead(t->actor_index, FALSE);
		hs_compile_and_evaluate("ai_debug_flee_always 0");
		W.start = now;
		W.have_last = FALSE;
		R.step = 1;
		track_line("start\t%ld\t0\t%s\t%s\t%s", (long)now, W.name, t->what1, t->what2);
		return TRUE;
	}
	if (on_second && W.see >= 0 && second == W.see && t->actor_index != NONE && player_unit_get())
	{
		h2_ai_range_see(t->actor_index, R.player_unit);
		track_line("event\t%ld\t%ld\t%s\tsee", (long)now, second, W.name);
	}
	if (on_second && W.flee >= 0 && second == W.flee)
	{
		hs_compile_and_evaluate("ai_debug_flee_always 1");
		track_line("event\t%ld\t%ld\t%s\tflee", (long)now, second, W.name);
	}
	if (on_second && W.unflee >= 0 && second == W.unflee)
	{
		hs_compile_and_evaluate("ai_debug_flee_always 0");
		track_line("event\t%ld\t%ld\t%s\tunflee", (long)now, second, W.name);
	}
	if (on_second && W.kill >= 0 && second == W.kill && t->object_index != NONE && object_try_and_get(t->object_index))
	{
		unit_kill(t->object_index);
		track_line("event\t%ld\t%ld\t%s\tkill", (long)now, second, W.name);
	}
	if ((now - W.start) % W.every == 0 && t->object_index != NONE && object_try_and_get(t->object_index))
	{
		real_point3d at;
		real speed = 0.0f;
		char what[300];

		object_get_origin(t->object_index, &at);
		if (W.have_last)
		{
			real dx = at.x - W.last.x, dy = at.y - W.last.y;

			speed = (real)sqrt(dx * dx + dy * dy) * RANGE_TICKS_PER_SECOND / (real)W.every;
		}
		W.last = at;
		W.have_last = TRUE;
		what[0] = 0;
		if (t->actor_index != NONE)
			h2_ai_range_describe(t->actor_index, what, sizeof(what));
		track_line("track\t%ld\t%.2f\t%s\t%.3f\t%.3f\t%.3f\t%.3f\t%s", (long)now, (double)(now - W.start) / RANGE_TICKS_PER_SECOND, W.name,
			(double)at.x, (double)at.y, (double)at.z, (double)speed, what);
	}
	if (now - W.start >= W.seconds * RANGE_TICKS_PER_SECOND)
	{
		hs_compile_and_evaluate("ai_debug_flee_always 0");
		track_line("end\t%ld\t%.2f\t%s", (long)now, (double)(now - W.start) / RANGE_TICKS_PER_SECOND, W.name);
		if (t->object_index != NONE && object_try_and_get(t->object_index))
			object_delete(t->object_index);
		t->object_index = t->actor_index = NONE;
		return FALSE;
	}
	return TRUE;
}

static boolean line_start(char const *line)
{
	char word[16] = "";

	sscanf(line, "%15s", word);
	R.step = 0;
	R.step_tick = game_time_get();
	snprintf(R.current, sizeof(R.current), "%s", line);
	platform_log("range: > %s", line);
	if (!strcmp(word, "log"))
	{
		R.log_rounds = strstr(line, "on") != NULL;
		return FALSE;
	}
	if (!strcmp(word, "scan") || !strcmp(word, "sight") || !strcmp(word, "variants") || !strcmp(word, "squads") || !strcmp(word, "spot") || !strcmp(word, "tags"))
	{
		h2_range_command(line);
		return FALSE;
	}
	if (!strcmp(word, "load"))
	{
		char name[200];

		sscanf(line, "%*s %199s", name);
		return layout_read(name);
	}
	if (!strcmp(word, "wait"))
		return TRUE;
	if (!strcmp(word, "stand"))
	{
		/* "stand x y z yaw": the player put there (a place to look at or fire from) */
		float x, y, z, yaw;

		if (sscanf(line, "%*s %f %f %f %f", &x, &y, &z, &yaw) == 4 && player_unit_get())
		{
			real_point3d at;

			at.x = x, at.y = y, at.z = z;
			player_stand(&at, yaw * (real)(3.14159265358979 / 180.0));
		}
		return FALSE;
	}
	if (!strcmp(word, "fire"))
	{
		char *k;

		memset(&F, 0, sizeof(F));
		snprintf(F.mode, sizeof(F.mode), "rate");
		snprintf(F.aim, sizeof(F.aim), "center");
		if (sscanf(line, "%*s %47s %31s %d", F.weapon, F.target, &F.pulls) < 3)
			return FALSE;
		F.interval = fire_interval_default(F.weapon);
		F.end_tick = 0;
		F.kill_pull = 0;
		if (F.pulls > FIRE_MAXIMUM_PULLS)
			F.pulls = FIRE_MAXIMUM_PULLS;
		if (strstr(line, " ttk"))
			snprintf(F.mode, sizeof(F.mode), "ttk");
		if ((k = (char *)arg_value(line, "aim=")) != NULL)
			sscanf(k, "%39s", F.aim);
		if ((k = (char *)arg_value(line, "interval=")) != NULL)
			F.interval = atoi(k);
		if ((k = (char *)arg_value(line, "hold=")) != NULL)
			F.hold = atoi(k);
		if (F.hold > 1 && F.interval <= F.hold)
			F.interval = F.hold + 1;
		return TRUE;
	}
	if (!strcmp(word, "ai"))
	{
		char *k;

		memset(&A, 0, sizeof(A));
		A.seconds = 60;
		A.ai_on = TRUE;
		if (sscanf(line, "%*s %31s vs %31s %d", A.a, A.b, &A.seconds) < 2)
			return FALSE;
		if ((k = (char *)arg_value(line, "ai=")) != NULL)
			A.ai_on = strncmp(k, "off", 3) != 0;
		return TRUE;
	}
	if (!strcmp(word, "crash"))
	{
		memset(&C, 0, sizeof(C));
		C.vehicle_object = NONE;
		if (sscanf(line, "%*s %47s %f %7s", C.vehicle, &C.speed, C.dir) < 3)
			return FALSE;
		return TRUE;
	}
	if (!strcmp(word, "watch"))
	{
		char *k;

		memset(&W, 0, sizeof(W));
		W.seconds = 20;
		W.every = 3;
		W.see = W.flee = W.unflee = W.kill = W.kill2 = W.shoot2 = W.shoot = W.shootn = -1;
		W.shot = 0;
		if (sscanf(line, "%*s %31s %d", W.name, &W.seconds) < 1)
			return FALSE;
		if ((k = (char *)arg_value(line, "see=")) != NULL)
			W.see = atoi(k);
		if ((k = (char *)arg_value(line, "flee=")) != NULL)
			W.flee = atoi(k);
		if ((k = (char *)arg_value(line, "unflee=")) != NULL)
			W.unflee = atoi(k);
		if ((k = (char *)arg_value(line, "kill=")) != NULL)
			W.kill = atoi(k);
		if ((k = (char *)arg_value(line, "kill2=")) != NULL)
			W.kill2 = atoi(k);
		if ((k = (char *)arg_value(line, "shoot2=")) != NULL)
			W.shoot2 = atoi(k);
		if ((k = (char *)arg_value(line, "shoot=")) != NULL)
			W.shoot = atoi(k);
		if ((k = (char *)arg_value(line, "shootn=")) != NULL)
			W.shootn = atoi(k);
		if ((k = (char *)arg_value(line, "other=")) != NULL)
			sscanf(k, "%47s", W.other);
		if ((k = (char *)arg_value(line, "every=")) != NULL && atoi(k) > 0)
			W.every = atoi(k);
		if ((k = (char *)arg_value(line, "orders=")) != NULL)
			sscanf(k, "%47s", W.orders);
		if ((k = (char *)arg_value(line, "quiet=")) != NULL)
			sscanf(k, "%47s", W.quiet);
		if ((k = (char *)arg_value(line, "moves=")) != NULL)
		{
			char const *m = k;
			int n;

			while (W.move_count < (int)NUMBEROF(W.moves) &&
				sscanf(m, "%f:%f,%f,%f%n", &W.moves[W.move_count].t, &W.moves[W.move_count].x, &W.moves[W.move_count].y,
					&W.moves[W.move_count].z, &n) == 4)
			{
				W.move_count++;
				m += n;
				if (*m != ';')
					break;
				m++;
			}
		}
		return TRUE;
	}
	if (!strcmp(word, "fight"))
	{
		char const *p = line + 5;
		int n = 0;

		memset(&FG, 0, sizeof(FG));
		if (sscanf(p, "%d%n", &FG.seconds, &n) < 1)
			return FALSE;
		p += n;
		while (FG.count < 6 && sscanf(p, "%47s%n", FG.squads[FG.count], &n) == 1)
		{
			FG.count++;
			p += n;
		}
		return FG.count > 0;
	}
	if (!strcmp(word, "devlog"))
	{
		/* "devlog NAME [EVERY]" (audit13): a "dev" row to range-track.tsv every EVERY ticks (1) - the named scenario
		object's device position and the player's origin (the door probe: tests/door_*.txt); "devlog off" stops */
		char name[48] = "";

		DL.every = 1;
		sscanf(line, "%*s %47s %d", name, &DL.every);
		DL.object_index = NONE;
		if (strcmp(name, "off"))
		{
			short n = scenario_object_name_index_from_string(global_scenario_get(), name);

			DL.object_index = n != NONE ? object_index_from_name_index(n) : NONE;
			snprintf(DL.name, sizeof(DL.name), "%s", name);
			platform_log("range: devlog %s -> object %ld", name, (long)DL.object_index);
		}
		return FALSE;
	}
	if (!strcmp(word, "bipeds"))
	{
		/* "bipeds" (audit14, ours): a "biped" row to range-track.tsv for each living biped but the player's - its tag,
		where it is, its actor and team (what a quiet test left alive) */
		struct object_iterator iterator;
		struct object_datum *object;

		if (player_unit_get())
		{
			struct object_datum const *p = object_get(R.player_unit);
			real_matrix4x3 const *nodes = (real_matrix4x3 const *)object_get_node_matrices(R.player_unit);

			track_line("biped\t%ld\tplayer\torigin %.3f %.3f %.3f\tnode0 %.3f %.3f %.3f\tsphere %.3f %.3f %.3f r %.3f", (long)game_time_get(),
				(double)p->object.position.x, (double)p->object.position.y, (double)p->object.position.z,
				nodes ? (double)nodes[0].position.x : 0.0, nodes ? (double)nodes[0].position.y : 0.0, nodes ? (double)nodes[0].position.z : 0.0,
				(double)p->object.bounding_sphere_center.x, (double)p->object.bounding_sphere_center.y,
				(double)p->object.bounding_sphere_center.z, (double)p->object.bounding_sphere_radius);
		}
		object_iterator_new(&iterator, FLAG(_object_type_biped), 0);
		while ((object = object_iterator_next(&iterator)) != NULL)
		{
			struct unit_datum const *unit = (struct unit_datum const *)object;

			if (TEST_FLAG(object->object.damage_flags, _object_dead_bit) || iterator.index == R.player_unit)
				continue;
			track_line("biped\t%ld\t%s\t%.2f\t%.2f\t%.2f\tactor %lx\tteam %d", (long)game_time_get(), tag_get_name(object->definition_index),
				(double)object->object.position.x, (double)object->object.position.y, (double)object->object.position.z,
				(unsigned long)unit->unit.actor_index, (int)object->object.owner_team_index);
		}
		return FALSE;
	}
	if (!strcmp(word, "hs"))
	{
		/* "hs LINE": a console line (ours: the script compiler's; the real runner sends it to the tag test's console) */
		hs_compile_and_evaluate(line + 3);
		return FALSE;
	}
	if (!strcmp(word, "regions"))
	{
		/* "regions NAME" (ours, debug): the layout object's 16 regions - permutation, damage (of 255) and destroyed flag */
		char name[32], buf[512];
		struct range_thing *t;
		struct object_datum *object;
		int n = 0;
		short r;

		name[0] = 0;
		sscanf(line, "%*s %31s", name);
		if ((t = thing_find(name)) != NULL && (object = object_try_and_get(t->object_index)) != NULL)
		{
			for (r = 0; r < OBJECT_REGIONS_WITH_STATE; r++)
				n += snprintf(buf + n, sizeof(buf) - n, " %d:%d/%d%s", r, (int)object_region_permutation_get(t->object_index, r),
					(int)object_region_damage_get(t->object_index, r), TEST_FLAG(object->object.regions_destroyed_flags, r) ? "D" : "");
			rounds_line("regions\t%ld\t%s\tdestroyed=%04x\t%s", (long)game_time_get(), name, (unsigned)object->object.regions_destroyed_flags, buf);
			platform_log("range: regions %s destroyed=%04x%s", name, (unsigned)object->object.regions_destroyed_flags, buf);
		}
		else
			platform_log("range: regions %s: no such object", name);
		return FALSE;
	}
	if (!strcmp(word, "markers"))
	{
		/* "markers NAME MARKER..." (ours, debug): each instance of the named markers - node, world position, and
		the position in the object's own frame (forward, left, up from its origin) */
		char name[32], marker_names[4][48];
		struct range_thing *t;
		int count, k;

		name[0] = 0;
		count = sscanf(line, "%*s %31s %47s %47s %47s %47s", name, marker_names[0], marker_names[1], marker_names[2], marker_names[3]) - 1;
		if ((t = thing_find(name)) != NULL && object_try_and_get(t->object_index) != NULL)
		{
			struct object_datum *object = object_get(t->object_index);
			real_matrix4x3 frame, *root = &frame;
			short node_count = object->object.node_matrices.size / (short)sizeof(real_matrix4x3), j;

			frame.scale = 1.0f;
			frame.forward = object->object.forward;
			frame.up = object->object.up;
			cross_product3d(&frame.up, &frame.forward, &frame.left);
			frame.position = object->object.position;
			if (object->object.type <= _object_type_vehicle)
			{
				struct unit_datum *unit = (struct unit_datum *)object;
				real_vector3d const *v[3] = { &unit->unit.desired_facing_vector, &unit->unit.aiming_vector, &unit->unit.looking_vector };
				real_point3d player = { 0 };

				if (R.player_unit != NONE && object_try_and_get(R.player_unit))
				{
					real_vector3d d;

					object_get_origin(R.player_unit, &player);
					vector_from_points3d(&root->position, &player, &d);
					rounds_line("player\t%ld\t%s\tlocal(f,l,u)=%.3f %.3f %.3f", (long)game_time_get(), name,
						dot_product3d(&d, &root->forward), dot_product3d(&d, &root->left), dot_product3d(&d, &root->up));
				}
				for (j = 0; j < 3; j++)
					rounds_line("vector\t%ld\t%s\t%s\tlocal(f,l,u)=%.3f %.3f %.3f", (long)game_time_get(), name, j == 0 ? "facing" : j == 1 ? "aiming" : "looking",
						dot_product3d(v[j], &root->forward), dot_product3d(v[j], &root->left), dot_product3d(v[j], &root->up));
			}
			for (j = 0; j < node_count; j++)
			{
				real_point3d const *w = &object_get_node_matrix(t->object_index, j)->position;
				real_vector3d d;

				vector_from_points3d(&root->position, w, &d);
				rounds_line("node\t%ld\t%s\t%d\tlocal(f,l,u)=%.3f %.3f %.3f", (long)game_time_get(), name, j,
					dot_product3d(&d, &root->forward), dot_product3d(&d, &root->left), dot_product3d(&d, &root->up));
			}

			for (k = 0; k < count; k++)
			{
				struct object_marker markers[8];
				short n = object_get_marker_by_name(t->object_index, marker_names[k], markers, 8), i;

				for (i = 0; i < n; i++)
				{
					real_point3d const *w = &markers[i].matrix.position;
					real_vector3d d;

					vector_from_points3d(&root->position, w, &d);
					rounds_line("marker\t%ld\t%s\t%s\t#%d\tnode=%d\tworld=%.3f %.3f %.3f\tlocal(f,l,u)=%.3f %.3f %.3f", (long)game_time_get(), name,
						marker_names[k], i, markers[i].node_index, w->x, w->y, w->z,
						dot_product3d(&d, &root->forward), dot_product3d(&d, &root->left), dot_product3d(&d, &root->up));
				}
			}
		}
		return FALSE;
	}
	if (!strcmp(word, "heal"))
	{
		char name[32];
		struct range_thing *t;

		sscanf(line, "%*s %31s", name);
		if ((t = thing_find(name)) != NULL && t->object_index != NONE)
		{
			if (!object_refill(t->object_index))
				thing_spawn(t);
		}
		return FALSE;
	}
	if (!strcmp(word, "end") || !strcmp(word, "exit"))
	{
		platform_log("range: script done");
		if (R.results)
			fclose(R.results), R.results = NULL;
		if (!strcmp(word, "exit"))
			exit(EXIT_SUCCESS);
		return FALSE;
	}
	platform_log("range: unknown script line: %s", line);
	return FALSE;
}

static boolean line_update(void)
{
	char word[16] = "";
	long now = game_time_get();

	sscanf(R.current, "%15s", word);
	if (!strcmp(word, "load"))
	{
		/* (spawn once the layout is read: step 0 clears the old world and the level's scripts) */
		if (R.step == 0)
		{
			int i;

			if (!player_unit_get())
				return TRUE;
			hs_runtime_end_all_threads();
			/* (a Halo 2 campaign level's scripts asleep for good - HALO_H2_RANGE_SCRIPTS=1 keeps them running, as before audit16) */
			if (h2_ai_running() && !(getenv("HALO_H2_RANGE_SCRIPTS") && atoi(getenv("HALO_H2_RANGE_SCRIPTS"))))
				platform_log("range: %ld script threads put to sleep", hs_runtime_sleep_all_script_threads());
			hs_compile_and_evaluate("ai_erase_all");
			/* (HALO_H2_RANGE_SEED=n: another sample of a deterministic run) */
			if (getenv("HALO_H2_RANGE_SEED"))
			{
				extern void set_random_seed(unsigned long seed);

				set_random_seed((unsigned long)atol(getenv("HALO_H2_RANGE_SEED")));
			}
			hs_compile_and_evaluate("cheat_deathless_player 1");
			hs_compile_and_evaluate("cheat_infinite_ammo 1");
			if (!R.origin_fixed)
			{
				object_get_origin(R.player_unit, &R.origin);
				R.yaw = player_control_get(0)->desired_angles.yaw;
			}
			else
			{
				real_point3d stand = R.origin;

				player_stand(&stand, R.yaw);
			}
			R.have_origin = TRUE;
			platform_log("range: origin %.2f %.2f %.2f facing %.0f deg", (double)R.origin.x, (double)R.origin.y, (double)R.origin.z,
				(double)(R.yaw * 180.0f / 3.14159265f));
			for (i = 0; i < R.thing_count; i++)
			{
				if (!thing_spawn(&R.things[i]))
					R.missing++;
			}
			platform_log("range: layout up: %d of %d things, %d missing", R.thing_count - R.missing, R.thing_count, R.missing);
			R.step = 1;
			R.step_tick = now;
			return TRUE;
		}
		return now - R.step_tick < 20;
	}
	if (!strcmp(word, "wait"))
	{
		int n = 30;

		sscanf(R.current, "%*s %d", &n);
		return now - R.step_tick < n;
	}
	if (!strcmp(word, "fire"))
		return test_fire_update();
	if (!strcmp(word, "ai"))
		return test_ai_update();
	if (!strcmp(word, "crash"))
		return test_crash_update();
	if (!strcmp(word, "fight"))
		return test_fight_update();
	if (!strcmp(word, "watch"))
		return W.name[0] == '@' ? test_watch_squad_update() : test_watch_update();
	return FALSE;
}

static void devlog_update(void)
{
	real_point3d at = { 0 };
	long now = game_time_get();

	if (DL.object_index == NONE || DL.every <= 0 || now % DL.every)
		return;
	if (!object_try_and_get(DL.object_index))
	{
		short n = scenario_object_name_index_from_string(global_scenario_get(), DL.name);

		/* (made later: a BSP's objects come with the BSP) */
		DL.object_index = n != NONE ? object_index_from_name_index(n) : NONE;
		if (DL.object_index == NONE || !object_try_and_get(DL.object_index))
			return;
	}
	if (player_unit_get())
		object_get_origin(R.player_unit, &at);
	{
		struct machine_datum *m = (struct machine_datum *)object_get(DL.object_index);

		track_line("dev\t%ld\t%s\t%.4f\t%.2f %.2f %.2f\tpower %.2f group %d mflags %lx open_ticks %ld sphere %.2f %.2f %.2f r %.2f fwd %.2f %.2f scale %.3f",
			now, DL.name, (double)device_get_position(DL.object_index), (double)at.x, (double)at.y, (double)at.z,
			(double)m->device.power, (int)m->device.position_group_index, (unsigned long)m->machine.flags, (long)m->machine.door_open_ticks,
			(double)m->object.bounding_sphere_center.x, (double)m->object.bounding_sphere_center.y, (double)m->object.bounding_sphere_center.z,
			(double)m->object.bounding_sphere_radius, (double)m->object.forward.i, (double)m->object.forward.j, (double)m->object.scale);
		if (player_unit_get())
		{
			struct object_datum *p = object_get(R.player_unit);

			track_line("devp\t%ld\tplayer sphere %.2f %.2f %.2f r %.2f", now, (double)p->object.bounding_sphere_center.x,
				(double)p->object.bounding_sphere_center.y, (double)p->object.bounding_sphere_center.z, (double)p->object.bounding_sphere_radius);
		}
	}
}

/* every tick (main.c) */
H2_ALIGN void h2_range_update(void)
{
	if (!game_in_progress())
		return;
	if (!R.env_started && getenv("HALO_H2_RANGE_SCRIPT") == NULL && getenv("HALO_H2_RANGE") == NULL && !R.line_count)
		return;
	/* (a level's opening cinematic skipped: no player control in it) */
	if (cinematic_in_progress())
	{
		if (game_time_get() % 45 == 15)
			main_skip_cinematic();
		return;
	}
	if (!R.out_folder[0])
		snprintf(R.out_folder, sizeof(R.out_folder), "%s", getenv("HALO_H2_RANGE_OUT") ? getenv("HALO_H2_RANGE_OUT") : ".");
	if (!R.env_started)
	{
		/* (the level's first seconds are its own: the lab starts 5 s in) */
		if (!player_unit_get() || game_time_get() < 5 * RANGE_TICKS_PER_SECOND)
		{
			if (game_time_get() % (10 * RANGE_TICKS_PER_SECOND) == 0)
				platform_log("range: waiting to start (tick %ld, player unit %s)", (long)game_time_get(), player_unit_get() ? "yes" : "none");
			return;
		}
		R.env_started = TRUE;
		R.trigger = -1.0f;
		platform_log("range: started at tick %ld", (long)game_time_get());
		if (getenv("HALO_H2_RANGE"))
		{
			char line[260];

			snprintf(line, sizeof(line), "load %s", getenv("HALO_H2_RANGE"));
			script_add(line);
		}
		if (getenv("HALO_H2_RANGE_SCRIPT"))
			script_file_add(getenv("HALO_H2_RANGE_SCRIPT"));
	}
	if (!player_unit_get())
	{
		if (game_time_get() % (10 * RANGE_TICKS_PER_SECOND) == 0)
			platform_log("range: no player unit at tick %ld (%ld lines waiting)", (long)game_time_get(), (long)R.line_count);
		return;
	}
	devlog_update();
	if (R.busy)
	{
		if (!line_update())
		{
			R.busy = FALSE;
			R.trigger = -1.0f;
		}
		return;
	}
	if (R.line_next < R.line_count)
	{
		R.busy = TRUE;
		if (!line_start(R.lines[R.line_next++]))
			R.busy = FALSE;
	}
}

/* "@range_xxx args" */
H2_ALIGN void h2_range_command(char const *text)
{
	char word[24] = "", rest[200] = "";
	char line[240];

	sscanf(text, "%23s %199[^\n]", word, rest);
	if (!R.out_folder[0])
		snprintf(R.out_folder, sizeof(R.out_folder), "%s", getenv("HALO_H2_RANGE_OUT") ? getenv("HALO_H2_RANGE_OUT") : ".");
	R.env_started = TRUE;
	if (R.trigger == 0.0f)
		R.trigger = -1.0f;
	if (!strcmp(word, "variants"))
		h2_ai_range_variants(rest);
	else if (!strcmp(word, "squads"))
		h2_ai_range_squads(rest);
	else if (!strcmp(word, "spot"))
	{
		float x, y, z;

		if (sscanf(rest, "%f %f %f", &x, &y, &z) == 3)
			range_spot(x, y, z);
	}
	else if (!strcmp(word, "tags"))
	{
		char group[8] = "", filter[100] = "";
		struct tag_iterator tags;
		long tag_index, count = 0;

		sscanf(rest, "%7s %99s", group, filter);
		if (strlen(group) == 4)
		{
			long tag = ((long)group[0] << 24) | ((long)group[1] << 16) | ((long)group[2] << 8) | (long)group[3];

			tag_iterator_new(&tags, tag);
			while ((tag_index = tag_iterator_next(&tags)) != NONE)
			{
				if (!filter[0] || strstr(tag_get_name(tag_index), filter))
				{
					platform_log("range: tag %s %s", group, tag_get_name(tag_index));
					count++;
				}
			}
			platform_log("range: %ld %s tags", count, group);
		}
	}
	else if (!strcmp(word, "sight"))
	{
		float yaw = 0.0f;

		range_sight(yaw, sscanf(rest, "%f", &yaw) == 1);
	}
	else if (!strcmp(word, "scan"))
	{
		int radius = 120, step = 8;

		sscanf(rest, "%d %d", &radius, &step);
		range_scan(radius, step);
	}
	else if (!strcmp(word, "log"))
		R.log_rounds = strstr(rest, "on") != NULL;
	else if (!strcmp(word, "run"))
		script_file_add(rest);
	else if (!strcmp(word, "summary"))
		platform_log("range: %d things, %d missing, %d script lines (%d run)", R.thing_count, R.missing, R.line_count, R.line_next);
	else
	{
		snprintf(line, sizeof(line), "%s %s", !strcmp(word, "load") ? "load" : word, rest);
		script_add(line);
	}
}

#endif /* !HALO_RELEASE && !HALO_VITA */
