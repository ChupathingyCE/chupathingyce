/*
H2_ITEM_PHYSICS.C

port: Halo 2's item motion on a Halo 2 level (h2_item_physics.h). Halo 2
gives its weapons, equipment and garbage no rigid bodies (no physics model:
the battle rifle's, the grenades' models have none); an item is a point that
falls, bounces off what it meets and, slow enough on ground it can stand on,
comes to rest laid on its "ground point" marker, spinning about the ground's
normal until that spin dies (x0.9 a tick) - Halo CE's rule, which Halo 2
kept with these changes (items.cpp function_10b850, function_10cf80, the
upstream decompilation; the behaviour reimplemented here, not its code):

- a slow impact (under 1.5 world units a second into the surface) that does
  not rest - a wall, a slope over 45 degrees, a crate or a vehicle - bounces
  back by 1.8 of its speed into it where Halo CE's always bounce by 1.4
  (fast ones and floors: 1.4 in both); an object's bounce at most 15 world
  units a second (Halo CE's 45);
- four slow impacts in a row and the item rests where it is, whatever it
  met (Halo CE's slides down a slope or skitters along a wall forever, by
  bounces too small to see - a weapon "stuck" sliding);
- the tick it bounces it stays where it was (its new velocity carries it
  off the next);
- a push on an item at rest spins it about the ground's normal by up to 90
  degrees a second (Halo CE's 90 degrees a tick: a whirl);
- a unit's drop at most 4 world units a second (a weapon dropped from a
  speeding vehicle);
- a bouncing projectile (a grenade) left under 0.3 world units a second
  stops (projectiles.cpp function_faa60; Halo CE's only at no speed at all,
  so a grenade crept along the ground until its timer ran out).

The rest - gravity (Halo 2's, h2_player_physics.c), the random tumble a push
gives (up to 90 degrees a second per world unit a second of the push), the
damage impulse (damage.c), the drop's cone and speed - is the same in both
games, or done elsewhere for Halo 2.

HALO_H2_STOCK_ITEM_PHYSICS=1: Halo CE's rules on a Halo 2 level too (a
comparison).
*/

#include "cseries.h"
#include "game/game.h"
#include "game/players.h"
#include "items/items.h"
#include "items/projectiles.h"
#include "memory/data.h"
#include "objects/objects.h"
#include "tag_files/tag_groups.h"
#include "cache/cache_files.h"
#include "scenario/scenario.h"
#include "units/units.h"
#include "physics/collisions.h"

#include "h2_item_physics.h"
#include "h2_player_physics.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (render_epoch.c: changes with every map and game state replaced) */
extern unsigned long halo_map_generation;

void platform_log(char const *format, ...);

/* ---------- constants */

/* world units a tick from world units a second */
#define H2_ITEM_PER_TICK(per_second) ((per_second) / (real)TICKS_PER_SECOND)
/* (items.cpp) an impact slower than this into the surface is slow */
#define H2_ITEM_SLOW_IMPACT H2_ITEM_PER_TICK(1.5f)
/* (items.cpp) a slow impact faster than this bounces as a fast one does */
#define H2_ITEM_FIRM_IMPACT H2_ITEM_PER_TICK(1.0f)
#define H2_ITEM_BOUNCE 1.4f
#define H2_ITEM_SLOW_BOUNCE 1.8f
#define H2_ITEM_OBJECT_BOUNCE_MOST H2_ITEM_PER_TICK(15.0f)
/* (unit_object_type.cpp) a drop's fastest */
#define H2_ITEM_DROP_MOST H2_ITEM_PER_TICK(4.0f)
/* (projectiles.cpp) a bouncing projectile slower than this stops */
#define H2_PROJECTILE_STOP_SPEED H2_ITEM_PER_TICK(0.3f)

enum
{
	H2_ITEM_TRACES = 4,
	H2_ITEM_TRACE_AFTER_REST_TICKS = 2 * TICKS_PER_SECOND,
	H2_ITEM_TRACE_MOST_TICKS = 20 * TICKS_PER_SECOND,
};

/* ---------- globals */

static struct
{
	long scenario_index;
	boolean level;
} h2_item_physics;

static struct
{
	boolean active;
	long object_index;
	long start_time;
	long rest_time;
	long last_time;
	real_point3d start;
	real_point3d last;
	real distance;
	real most_height;
	short bounces;
	boolean was_moving;
} h2_item_traces[H2_ITEM_TRACES];
/* (perf/ce-floor-2) a trace was ever started: until then every item's and
projectile's update (h2_item_physics_trace) has none to follow */
boolean h2_item_traces_ever; /* (perf/ce-floor-3: read inline by h2_item_physics.h's h2_item_physics_trace_fast) */

/* ---------- public code */

boolean h2_item_physics_level(
	void)
{
	static unsigned long generation;

	/* (the map's generation too: consecutive maps' scenarios can have the same tag index) */
	if (h2_item_physics.scenario_index != global_scenario_index || generation != halo_map_generation)
	{
		h2_item_physics.scenario_index = global_scenario_index;
		generation = halo_map_generation;
		h2_item_physics.level = h2_player_physics_level() && !getenv("HALO_H2_STOCK_ITEM_PHYSICS");
	}
	return h2_item_physics.level;
}

boolean h2_item_impact_slow(
	real into)
{
	return into < H2_ITEM_SLOW_IMPACT;
}

real h2_item_bounce_reflection(
	real into,
	boolean surface_floor,
	boolean structure)
{
	real reflection = into * (surface_floor || into > H2_ITEM_FIRM_IMPACT ? H2_ITEM_BOUNCE : H2_ITEM_SLOW_BOUNCE);

	if (!structure)
		reflection = MIN(reflection, H2_ITEM_OBJECT_BOUNCE_MOST);
	return reflection;
}

real h2_item_spin_per_tick(
	real radians_per_second)
{
	return radians_per_second / (real)TICKS_PER_SECOND;
}

void h2_item_drop_velocity_clamp(
	real_vector3d *velocity)
{
	real speed = magnitude3d(velocity);

	if (speed > H2_ITEM_DROP_MOST)
		scale_vector3d(velocity, H2_ITEM_DROP_MOST / speed, velocity);
}

boolean h2_projectile_bounce_stops(
	real speed_squared)
{
	return speed_squared < H2_PROJECTILE_STOP_SPEED * H2_PROJECTILE_STOP_SPEED;
}

/* ---------- the test command */

static struct player_datum *h2_item_test_player(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (player->unit_index != NONE)
			return player;
	}
	return NULL;
}

/* (the same player's index, NONE none) */
static long h2_item_test_player_index(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (player->unit_index != NONE)
			return iterator.datum_index;
	}
	return NONE;
}

static void h2_item_trace_start(
	long object_index)
{
	short k, slot = 0;

	for (k = 0; k < H2_ITEM_TRACES; k++)
	{
		if (!h2_item_traces[k].active || !object_try_and_get(h2_item_traces[k].object_index) ||
			h2_item_traces[k].start_time < h2_item_traces[slot].start_time)
		{
			slot = k;
			if (!h2_item_traces[k].active)
				break;
		}
	}
	memset(&h2_item_traces[slot], 0, sizeof(h2_item_traces[slot]));
	h2_item_traces[slot].active = TRUE;
	h2_item_traces_ever = TRUE;
	h2_item_traces[slot].object_index = object_index;
	h2_item_traces[slot].start_time = game_time_get();
	h2_item_traces[slot].rest_time = NONE;
	h2_item_traces[slot].last_time = NONE;
	h2_item_traces[slot].start = object_get(object_index)->object.position;
	h2_item_traces[slot].last = h2_item_traces[slot].start;
	h2_item_traces[slot].most_height = h2_item_traces[slot].start.z;
}

void h2_item_physics_test_command(
	char const *arguments)
{
	char name[64];
	real v[3] = { 0.f, 0.f, 0.f }, d[3] = { 1.f, 0.f, 0.5f };
	long definition_index = NONE, tag_index;
	unsigned long groups[3] = { 'weap', 'eqip', 'proj' };
	short group;
	struct player_datum *player = h2_item_test_player();
	int count;
	boolean absolute;

	/* ("scan x0 y0 x1 y1 step z": the structure's slopes between 25 and 60
	degrees under that rectangle, from height z down, logged - places to
	toss onto) */
	if (!strncmp(arguments, "scan ", 5))
	{
		real r[6];

		if (sscanf(arguments + 5, "%f %f %f %f %f %f", &r[0], &r[1], &r[2], &r[3], &r[4], &r[5]) == 6 && r[4] > 0.05f)
		{
			real x, y;
			long found = 0;

			for (x = r[0]; x <= r[2] && found < 64; x += r[4])
			{
				for (y = r[1]; y <= r[3] && found < 64; y += r[4])
				{
					struct collision_result collision;
					real_point3d from;
					real_vector3d down = { 0.f, 0.f, -60.f };

					from.x = x;
					from.y = y;
					from.z = r[5];
					if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
							&from, &down, NONE, &collision) &&
						collision.plane.n.k < 0.906f && collision.plane.n.k > 0.5f)
					{
						platform_log("h2 items: slope %.0f degrees at %.2f %.2f %.2f normal %.2f %.2f %.2f",
							(double)(acosf(collision.plane.n.k) * 57.29578f), (double)collision.point.x, (double)collision.point.y,
							(double)collision.point.z, (double)collision.plane.n.i, (double)collision.plane.n.j, (double)collision.plane.n.k);
						found++;
					}
				}
			}
		}
		return;
	}
	/* ("NAME vx vy vz @ x y z": that place and velocity in the world's axes) */
	absolute = strchr(arguments, '@') != NULL;
	count = absolute ?
		sscanf(arguments, "%63s %f %f %f @ %f %f %f", name, &v[0], &v[1], &v[2], &d[0], &d[1], &d[2]) :
		sscanf(arguments, "%63s %f %f %f %f %f %f", name, &v[0], &v[1], &v[2], &d[0], &d[1], &d[2]);
	if (absolute && count < 7)
		count = 0;
	if (count < 4 || !player)
	{
		platform_log("h2 items: toss NAME vx vy vz [dx dy dz] (%s)", player ? "arguments" : "no player");
		return;
	}
	{
		char *c;

		for (c = name; *c; c++)
		{
			if (*c == '/')
				*c = '\\';
		}
	}
	/* ("proj:NAME", "eqip:NAME", "weap:NAME": that group's only) */
	if (strlen(name) > 5 && name[4] == ':')
	{
		unsigned long only = ((unsigned long)name[0] << 24) | ((unsigned long)name[1] << 16) | ((unsigned long)name[2] << 8) | (unsigned long)name[3];

		groups[0] = groups[1] = groups[2] = only;
		memmove(name, name + 5, strlen(name + 5) + 1);
	}
	for (group = 0; group < NUMBEROF(groups) && definition_index == NONE; group++)
	{
		struct tag_iterator tags;

		tag_iterator_new(&tags, groups[group]);
		while ((tag_index = tag_iterator_next(&tags)) != NONE && definition_index == NONE)
		{
			if (strstr(tag_get_name(tag_index), name))
				definition_index = tag_index;
		}
	}
	if (definition_index == NONE)
	{
		platform_log("h2 items: no weapon, equipment or projectile named %s", name);
		return;
	}
	{
		struct object_datum *unit = object_get(player->unit_index);
		real_vector3d forward = unit->object.forward, left, up = { 0.f, 0.f, 1.f };
		real_vector3d velocity;
		struct object_placement_data placement;
		long object_index;

		forward.k = 0.f;
		if (absolute || normalize3d(&forward) == 0.f)
			forward = *global_forward3d;
		cross_product3d(&up, &forward, &left);
		object_placement_data_new(&placement, definition_index, NONE);
		placement.position = unit->object.position;
		if (absolute)
		{
			placement.position.x = d[0];
			placement.position.y = d[1];
			placement.position.z = d[2];
		}
		else
		{
			placement.position.x += forward.i * d[0] + left.i * d[1];
			placement.position.y += forward.j * d[0] + left.j * d[1];
			placement.position.z += d[2];
		}
		placement.forward = forward;
		placement.up = up;
		/* (the player's own, as a throw of his: a projectile's kill is his -
		the HUD's kill lines' tests) */
		placement.owner_player_index = h2_item_test_player_index();
		placement.owner_object_index = player->unit_index;
		placement.owner_team_index = player->team_index;
		object_index = object_new(&placement);
		if (object_index == NONE)
		{
			platform_log("h2 items: %s could not be made", tag_get_name(definition_index));
			return;
		}
		velocity.i = H2_ITEM_PER_TICK(forward.i * v[0] + left.i * v[1]);
		velocity.j = H2_ITEM_PER_TICK(forward.j * v[0] + left.j * v[1]);
		velocity.k = H2_ITEM_PER_TICK(v[2]);
		if (object_get_type(object_index) == _object_type_projectile)
		{
			real_vector3d change;

			subtract_vectors3d(&velocity, &object_get(object_index)->object.translational_velocity, &change);
			projectile_accelerate(object_index, &change);
		}
		else
		{
			item_get(object_index)->item.ignore_object_index = player->unit_index;
			item_accelerate(object_index, &velocity, FALSE);
		}
		platform_log("h2 items: toss %s %08lx at %.2f %.2f %.2f velocity %.2f %.2f %.2f wu/s (%s rules)", tag_get_name(definition_index),
			(unsigned long)object_index, (double)placement.position.x, (double)placement.position.y, (double)placement.position.z,
			(double)(velocity.i * TICKS_PER_SECOND), (double)(velocity.j * TICKS_PER_SECOND), (double)(velocity.k * TICKS_PER_SECOND),
			h2_item_physics_level() ? "Halo 2" : "Halo CE");
		h2_item_trace_start(object_index);
	}
}

void h2_item_physics_trace(
	long object_index)
{
	short k;
	long now;

	if (!h2_item_traces_ever)
		return;
	now = game_time_get();

	for (k = 0; k < H2_ITEM_TRACES; k++)
	{
		struct object_datum *object;
		real speed;
		boolean resting, moving;

		if (!h2_item_traces[k].active || h2_item_traces[k].object_index != object_index || h2_item_traces[k].last_time == now)
			continue;
		h2_item_traces[k].last_time = now;
		object = object_get(object_index);
		speed = magnitude3d(&object->object.translational_velocity) * TICKS_PER_SECOND;
		resting = TEST_FLAG(object->object.flags, _object_at_rest_bit);
		moving = distance3d(&object->object.position, &h2_item_traces[k].last) > 0.0005f;
		/* (a projectile's bounce: its velocity turned up; an item's are noted) */
		if (object_get_type(object_index) == _object_type_projectile && object->object.translational_velocity.k > 0.f &&
			h2_item_traces[k].was_moving)
		{
			h2_item_traces[k].bounces++;
		}
		h2_item_traces[k].distance += distance3d(&object->object.position, &h2_item_traces[k].last);
		h2_item_traces[k].last = object->object.position;
		h2_item_traces[k].most_height = MAX(h2_item_traces[k].most_height, object->object.position.z);
		h2_item_traces[k].was_moving = object->object.translational_velocity.k <= 0.f;
		platform_log("h2 items: trace %08lx t%ld at %.3f %.3f %.3f speed %.2f up %.2f %.2f %.2f spin %.2f%s",
			(unsigned long)object_index, now - h2_item_traces[k].start_time, (double)object->object.position.x,
			(double)object->object.position.y, (double)object->object.position.z, (double)speed, (double)object->object.up.i,
			(double)object->object.up.j, (double)object->object.up.k,
			(double)(magnitude3d(&object->object.angular_velocity) * TICKS_PER_SECOND),
			resting ? " at rest" : moving ? "" : " still");
		if (resting && h2_item_traces[k].rest_time == NONE)
		{
			h2_item_traces[k].rest_time = now;
			platform_log("h2 items: rest %08lx after %.2f s, %.2f wu from its start (%.2f wu along the ground), %.2f wu travelled, %d bounces, up %.2f %.2f %.2f",
				(unsigned long)object_index, (double)(now - h2_item_traces[k].start_time) / TICKS_PER_SECOND,
				(double)distance3d(&object->object.position, &h2_item_traces[k].start),
				(double)sqrtf((object->object.position.x - h2_item_traces[k].start.x) * (object->object.position.x - h2_item_traces[k].start.x) +
					(object->object.position.y - h2_item_traces[k].start.y) * (object->object.position.y - h2_item_traces[k].start.y)),
				(double)h2_item_traces[k].distance, (int)h2_item_traces[k].bounces, (double)object->object.up.i,
				(double)object->object.up.j, (double)object->object.up.k);
		}
		else if (!resting && h2_item_traces[k].rest_time != NONE)
			h2_item_traces[k].rest_time = NONE;
		if ((h2_item_traces[k].rest_time != NONE && now - h2_item_traces[k].rest_time > H2_ITEM_TRACE_AFTER_REST_TICKS) ||
			now - h2_item_traces[k].start_time > H2_ITEM_TRACE_MOST_TICKS)
		{
			if (h2_item_traces[k].rest_time == NONE)
				platform_log("h2 items: never rested %08lx in %.0f s: %.2f wu travelled, %d bounces", (unsigned long)object_index,
					(double)H2_ITEM_TRACE_MOST_TICKS / TICKS_PER_SECOND, (double)h2_item_traces[k].distance, (int)h2_item_traces[k].bounces);
			h2_item_traces[k].active = FALSE;
		}
	}
}

void h2_item_physics_note_bounce(
	long object_index)
{
	short k;

	for (k = 0; k < H2_ITEM_TRACES; k++)
	{
		if (h2_item_traces[k].active && h2_item_traces[k].object_index == object_index)
			h2_item_traces[k].bounces++;
	}
}
