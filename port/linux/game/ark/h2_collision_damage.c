/*
H2_COLLISION_DAMAGE.C

port: Halo 2's collision damage of a vehicle, by Bungie's rule over the
contacts' impulses (the decompilation's unknown_1d96d0.cpp - the walk of a
body's contacts each tick - and unknown_1da550.cpp function_1da6d0 and
function_1da9e0; nothing of Havok's).

Halo 2, each tick, for a body with contacts (1d96d0): the body's speed; the
largest contact impulse per mass (a velocity change, world units a second)
against a fixed body - the world - and the largest against a moving object,
and that object. Then, with the body's object definition's collision damage
(obje 0x6C "apply collision damage scale", 0x70..0x7C the game acceleration
limits and damage scales, 0x80..0x8C the absolute ones):

- function_1da6d0, the other object's blow: when the moving impulse is over a
  quarter of the fixed one, the other object's speed's share of the faster of
  the two speeds, times the other's transfer scale, times the larger impulse,
  is a force; none when both bodies are under 1.5 wu/s; the damage scale runs
  from the minimum to the maximum game scale as the force runs from the
  minimum to the maximum game acceleration (none at or under the minimum);
  the other object (its driver) owns the damage. (physics.c's splatter is
  this rule with the biped as the body and the vehicle as the other.)
- function_1da9e0, its own speed: when the body moves faster than 4 wu/s and
  the larger impulse is over the minimum absolute acceleration, the damage
  scale runs over the absolute limits; the damage is its own, or the other
  object's when the first rule hit.

The damage effect is the globals' collision damage (the falling damage
block's vehicle collision damage on a Halo 2 level: h2_level.c), its scale
the rule's.

The impulses here: this engine's vehicle physics is a penalty solver, so the
contacts' impulses are taken where the contact begins, as Havok's one-step
contact gives them - a swept mass point meeting a surface loses its velocity
into it (physics_update_new: exact); a landing's is the hull's speed into the
ground on the tick it touches after being off it; two vehicles' is their
closing speed along their mass points' line, each body's share by the other's
mass, on the tick they meet. Only Halo 2 force-model vehicles are noted
(h2_vehicle_physics.c calls the tick).

A contact's impulse in 1d96d0 is the body's velocity change from that one
contact (its impulse over the body's mass): a point off the centre of mass
turns the body as it stops it, so the centre loses only
v_n / (1 + m n.((I^-1 (r x n)) x r)) of the point's speed into the surface
(r the point from the centre of mass, n the surface's normal, I the body's
inertia: its physics' inverse inertia matrix) - the rigid-body law for one
contact, nothing of Havok's code. The H2EK's tag test (h2ekref
behaviour/vehicle_crash_damage.md) shows it: a Ghost pushed into the sand at
10-30 wu/s took the damage of 0.24-0.39 of its speed. The body's speed in
1d96d0 is the rigid body's from before the step resolved the contact (a Ghost
pushed straight down at 10 wu/s stops dead and still takes 0.21 of its body):
h2_collision_damage_tick is given the speed the vehicle had before its
physics ran.

HALO_H2_DAMAGE_LOG logs each impulse that damages.
*/

#include "cseries.h"
#include "ptick.h"
#include "game/game.h"
#include "game/game_globals.h"
#include "math/real_math.h"
#include "objects/damage.h"
#include "objects/object_definitions.h"
#include "objects/objects.h"
#include "physics/physics_definitions.h"
#include "scenario/scenario.h"
#include "tag_files/tag_groups.h"
#include "units/units.h"
#include "units/vehicle_datum.h"
#include "h2_vehicle_physics.h"
#include "h2_collision_damage.h"
#ifdef HALO_LINUX
#include "h2_level_fast.h" /* (perf/ce-floor-2) the Halo 2 level tests inline */
#endif

#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...);
boolean h2_damage_rules(void);

/* (physics.c's copy of the globals' falling damage block) */
struct game_globals_falling_damage
{
	long unused0[2];
	real falling_distance_lower_bound;
	real falling_distance_upper_bound;
	struct tag_reference falling_damage;
	long terminal_velocity_unused[2];
	real maximum_distance;
	struct tag_reference maximum_distance_damage;
	struct tag_reference vehicle_hit_environment_damage_effect;
	struct tag_reference vehicle_killed_unit_damage_effect;
	struct tag_reference vehicle_collision_damage;
	struct tag_reference flaming_death_damage;
	long unused7c[4];
	real runtime_maximum_falling_velocity;
	real runtime_minimum_damage_velocity;
	real runtime_maximum_damage_velocity;
};

enum
{
	H2CD_MAXIMUM_BODIES = 64,
	H2CD_MAXIMUM_PAIRS = 6
};

struct h2cd_body
{
	long object_index;
	long tick;                  /* the tick the notes are of */
	real fixed;                 /* the largest impulse against the world, wu/s */
	real fixed_sum, fixed_raw;  /* this tick's world contacts' impulses together, and the largest point speed into a surface */
	real moving;                /* the largest against a moving object, and it */
	long other;
	unsigned long grounded_last, grounded_now; /* the hull points on the ground last tick, this tick */
	unsigned long hull_mask;    /* its hull's mass points (h2_collision_damage_hull) */
	long crash_tick;            /* the tick of its crash's first damage (NONE: none), and the scales the crash dealt by each rule */
	real crash_scale[2];
	struct
	{
		long other, tick;
	} pairs[H2CD_MAXIMUM_PAIRS]; /* the vehicles met, and when last */
};

static struct h2cd_body h2cd_bodies[H2CD_MAXIMUM_BODIES];

static int h2cd_log(void)
{
	static int level = -1;

	if (level < 0)
		level = getenv("HALO_H2_DAMAGE_LOG") ? atoi(getenv("HALO_H2_DAMAGE_LOG")) : 0;
	return level;
}

static struct h2cd_body *h2cd_body_get(long object_index, boolean create)
{
	long i, free_slot = NONE, tick = game_time_get();

	for (i = 0; i < H2CD_MAXIMUM_BODIES; i++)
	{
		if (h2cd_bodies[i].object_index == object_index)
		{
			/* (ptick.h) the bodies' notes are kept HERE, outside the game state a
			speculative update's snapshot puts back: a body with one is not speculated */
			if (HALO_PTICK_GUARDED(_ptick_abort_other, "h2 collision damage"))
				return NULL;
			if (h2cd_bodies[i].tick != tick)
			{
				h2cd_bodies[i].fixed = 0.0f;
				h2cd_bodies[i].fixed_sum = 0.0f;
				h2cd_bodies[i].fixed_raw = 0.0f;
				h2cd_bodies[i].moving = 0.0f;
				h2cd_bodies[i].other = NONE;
				h2cd_bodies[i].grounded_last = tick - h2cd_bodies[i].tick == 1 ? h2cd_bodies[i].grounded_now : 0;
				h2cd_bodies[i].grounded_now = 0;
				h2cd_bodies[i].tick = tick;
			}
			return &h2cd_bodies[i];
		}
		if (free_slot == NONE && (h2cd_bodies[i].object_index == 0 || h2cd_bodies[i].object_index == NONE ||
			!object_try_and_get(h2cd_bodies[i].object_index) || tick - h2cd_bodies[i].tick > 300))
			free_slot = i;
	}
	if (!create || free_slot == NONE)
		return NULL;
	if (HALO_PTICK_GUARDED(_ptick_abort_other, "h2 collision damage"))
		return NULL;
	memset(&h2cd_bodies[free_slot], 0, sizeof(h2cd_bodies[free_slot]));
	h2cd_bodies[free_slot].object_index = object_index;
	h2cd_bodies[free_slot].tick = tick;
	h2cd_bodies[free_slot].other = NONE;
	h2cd_bodies[free_slot].crash_tick = NONE;
	for (i = 0; i < H2CD_MAXIMUM_PAIRS; i++)
	{
		h2cd_bodies[free_slot].pairs[i].other = NONE;
		h2cd_bodies[free_slot].pairs[i].tick = -2;
	}
	return &h2cd_bodies[free_slot];
}

/* whether the object is a Halo 2 force-model vehicle (the only bodies noted) */
static boolean h2cd_noted(long object_index)
{
	struct object_datum const *object;

	if (!h2_damage_rules_fast())
		return FALSE;
	object = object_try_and_get(object_index);
	if (!object || object_get_type(object_index) != _object_type_vehicle)
		return FALSE;
	return h2_vehicle_physics_definition(object->definition_index) != NULL;
}

/* the share of a contact point's speed into a surface the body's centre of
mass loses: 1 / (1 + m n.((I^-1 (r x n)) x r)), r from its centre of mass
(world), n the surface's normal; 1 with no inertia known */
static real h2cd_contact_share(long object_index, real_vector3d const *from_origin, real_vector3d const *normal)
{
	struct object_datum const *object = object_get(object_index);
	struct object_definition const *definition = (struct object_definition const *)tag_get('obje', object->definition_index);
	struct physics_definition const *physics;
	real_matrix3x3 const *inverse;
	real_vector3d left, center, r, rn, body, turned, world, back;
	real k;

	if (!from_origin || !normal || definition->object.physics.index == NONE)
		return 1.0f;
	physics = (struct physics_definition const *)tag_get('phys', definition->object.physics.index);
	if (!physics || physics->inertial_matrix.count < 2 || physics->mass <= 0.0f)
		return 1.0f;
	inverse = TAG_BLOCK_GET_ELEMENT(&physics->inertial_matrix, 1, real_matrix3x3);
	cross_product3d(&object->object.up, &object->object.forward, &left);
	/* (its centre of mass, in its own axes from its origin) */
	center.i = object->object.forward.i * physics->center_of_mass.x + left.i * physics->center_of_mass.y + object->object.up.i * physics->center_of_mass.z;
	center.j = object->object.forward.j * physics->center_of_mass.x + left.j * physics->center_of_mass.y + object->object.up.j * physics->center_of_mass.z;
	center.k = object->object.forward.k * physics->center_of_mass.x + left.k * physics->center_of_mass.y + object->object.up.k * physics->center_of_mass.z;
	subtract_vectors3d(from_origin, &center, &r);
	cross_product3d(&r, normal, &rn);
	/* (I^-1 in the body's axes: forward, left, up) */
	body.i = dot_product3d(&rn, &object->object.forward);
	body.j = dot_product3d(&rn, &left);
	body.k = dot_product3d(&rn, &object->object.up);
	matrix3x3_transform_vector(inverse, &body, &turned);
	world.i = object->object.forward.i * turned.i + left.i * turned.j + object->object.up.i * turned.k;
	world.j = object->object.forward.j * turned.i + left.j * turned.j + object->object.up.j * turned.k;
	world.k = object->object.forward.k * turned.i + left.k * turned.j + object->object.up.k * turned.k;
	cross_product3d(&world, &r, &back);
	k = physics->mass * dot_product3d(normal, &back);
	if (h2cd_log() >= 3)
		platform_log("h2 damage: %08lx contact at %.2f %.2f %.2f from its centre, normal %.2f %.2f %.2f: share %.3f", (unsigned long)object_index,
			(double)r.i, (double)r.j, (double)r.k, (double)normal->i, (double)normal->j, (double)normal->k, (double)(1.0f / (1.0f + MAX(k, 0.0f))));
	return k > 0.0f && k == k ? 1.0f / (1.0f + k) : 1.0f;
}

/* a contact with the world: impulse its centre of mass's share of raw, the
point's speed into the surface */
static void h2cd_note_fixed(struct h2cd_body *body, real impulse, real raw)
{
	if (impulse > body->fixed)
		body->fixed = impulse;
	body->fixed_sum += impulse;
	if (raw > body->fixed_raw)
		body->fixed_raw = raw;
}

static void h2cd_note_moving(struct h2cd_body *body, long other, real impulse)
{
	if (impulse > body->moving)
	{
		body->moving = impulse;
		body->other = other;
	}
}

void h2_collision_damage_hull(long vehicle_index, unsigned long hull_mask)
{
	struct h2cd_body *body;

	if (!h2cd_noted(vehicle_index))
		return;
	body = h2cd_body_get(vehicle_index, TRUE);
	if (body)
		body->hull_mask = hull_mask;
}

void h2_collision_damage_ground(long object_index, short mass_point_index, real normal_velocity, real_vector3d const *from_origin,
	real_vector3d const *normal)
{
	struct h2cd_body *body;

	if (normal_velocity >= 0.0f || !h2cd_noted(object_index))
		return;
	body = h2cd_body_get(object_index, TRUE);
	if (!body)
		return;
	/* (a wheel's or a pad's ground is no contact of Halo 2's: its friction
	and anti-gravity points are raycasts) */
	if (mass_point_index < 0 || mass_point_index >= 32 || !(body->hull_mask & (1UL << mass_point_index)))
		return;
	/* (a contact made: the point was off every surface last tick - Havok's
	new contact point and its impulse; a point resting on the ground, none) */
	if (!(body->grounded_last & (1UL << mass_point_index)))
	{
		real share = h2cd_contact_share(object_index, from_origin, normal);

		if (h2cd_log() >= 2)
			platform_log("h2 damage: %08lx hull point %d meets a surface at %.2f wu/s, its centre's share %.3f", (unsigned long)object_index,
				mass_point_index, (double)(-normal_velocity * (real)TICKS_PER_SECOND), (double)share);
		h2cd_note_fixed(body, -normal_velocity * share * (real)TICKS_PER_SECOND, -normal_velocity * (real)TICKS_PER_SECOND);
	}
	body->grounded_now |= 1UL << mass_point_index;
}

void h2_collision_damage_sweep(long object_index, long other_object_index, real delta_v, real_point3d const *point,
	real_vector3d const *normal)
{
	struct h2cd_body *body;
	real_vector3d from_origin;
	real share;

	if (delta_v <= 0.0f || !h2cd_noted(object_index))
		return;
	body = h2cd_body_get(object_index, TRUE);
	if (!body)
		return;
	if (point)
		vector_from_points3d(&object_get(object_index)->object.position, point, &from_origin);
	share = h2cd_contact_share(object_index, point ? &from_origin : NULL, normal);
	if (h2cd_log() >= 2)
		platform_log("h2 damage: %08lx swept into %08lx at %.2f wu/s, its centre's share %.3f", (unsigned long)object_index,
			(unsigned long)other_object_index, (double)(delta_v * (real)TICKS_PER_SECOND), (double)share);
	if (other_object_index != NONE && object_try_and_get(other_object_index) &&
		object_get_type(other_object_index) == _object_type_vehicle)
		h2cd_note_moving(body, other_object_index, delta_v * share * (real)TICKS_PER_SECOND);
	else
		h2cd_note_fixed(body, delta_v * share * (real)TICKS_PER_SECOND, delta_v * (real)TICKS_PER_SECOND);
}

void h2_collision_damage_contact(long object_index, long other_object_index, real impulse)
{
	struct h2cd_body *body;

	if (impulse <= 0.0f || !h2cd_noted(object_index))
		return;
	body = h2cd_body_get(object_index, TRUE);
	if (!body)
		return;
	if (h2cd_log() >= 2)
		platform_log("h2 damage: %08lx's hull meets %08lx at %.2f wu/s", (unsigned long)object_index, (unsigned long)other_object_index, (double)impulse);
	if (other_object_index != NONE && object_try_and_get(other_object_index))
		h2cd_note_moving(body, other_object_index, impulse);
	else
		h2cd_note_fixed(body, impulse, impulse);
}

/* whether the pair met before this tick (and remembers it met now) */
static boolean h2cd_pair_continuing(struct h2cd_body *body, long other)
{
	long i, slot = NONE;
	boolean continuing = FALSE;

	for (i = 0; i < H2CD_MAXIMUM_PAIRS; i++)
	{
		if (body->pairs[i].other == other)
		{
			slot = i;
			continuing = body->pairs[i].tick >= body->tick - 1;
			break;
		}
		if (slot == NONE || body->pairs[i].tick < body->pairs[slot].tick)
			slot = i;
	}
	body->pairs[slot].other = other;
	body->pairs[slot].tick = body->tick;
	return continuing;
}

void h2_collision_damage_pair(long object_index, real mass, long other_object_index, real other_mass, real closing_speed)
{
	struct h2cd_body *body;
	real share;

	if (closing_speed <= 0.0f || mass + other_mass <= 0.0f || !h2cd_noted(object_index))
		return;
	body = h2cd_body_get(object_index, TRUE);
	if (!body)
		return;
	if (h2cd_pair_continuing(body, other_object_index))
		return;
	/* (two bodies meeting and staying together: each loses the closing speed
	by the other's share of the mass) */
	share = other_mass / (mass + other_mass);
	if (h2cd_log() >= 2)
		platform_log("h2 damage: %08lx meets %08lx closing at %.2f wu/s, its share %.2f", (unsigned long)object_index,
			(unsigned long)other_object_index, (double)(closing_speed * (real)TICKS_PER_SECOND), (double)share);
	h2cd_note_moving(body, other_object_index, closing_speed * share * (real)TICKS_PER_SECOND);
}

/* the vehicle's responsible object: its driver when it has one */
static long h2cd_responsible(long vehicle_index)
{
	struct unit_datum const *unit = (struct unit_datum const *)object_get(vehicle_index);

	if (unit->unit.driver_object_index != NONE && object_try_and_get(unit->unit.driver_object_index))
		return unit->unit.driver_object_index;
	return vehicle_index;
}

static void h2cd_damage(long vehicle_index, long responsible_index, real scale, char const *why, real impulse)
{
	struct game_globals_falling_damage *falling_damage;
	struct object_datum *vehicle = object_get(vehicle_index);
	struct object_datum *responsible = object_get(responsible_index);
	struct damage_data damage;

	if (scale <= 0.0f || !scenario_get_game_globals()->falling_damage.count)
		return;
	falling_damage = TAG_BLOCK_GET_ELEMENT(&scenario_get_game_globals()->falling_damage, 0, struct game_globals_falling_damage);
	if (falling_damage->vehicle_collision_damage.index == NONE)
		return;
	damage_data_new(&damage, falling_damage->vehicle_collision_damage.index);
	damage.scale = scale;
	SET_FLAG(damage.flags, _damage_area_of_effect_bit, TRUE);
	damage.owner_player_index = responsible->object.owner_player_index;
	damage.owner_object_index = responsible->object.owner_object_index != NONE ?
		responsible->object.owner_object_index : responsible_index;
	damage.owner_team_index = responsible->object.owner_team_index;
	damage.origin = vehicle->object.bounding_sphere_center;
	damage.epicenter = vehicle->object.bounding_sphere_center;
	damage.direction = vehicle->object.translational_velocity;
	if (!(normalize3d(&damage.direction) > 0.0f))
		set_real_vector3d(&damage.direction, 0.0f, 0.0f, -1.0f);
	if (h2cd_log())
		platform_log("h2 damage: %08lx hit by %s at tick %ld: impulse %.2f wu/s, scale %.3f, owner %08lx",
			(unsigned long)vehicle_index, why, (long)game_time_get(), (double)impulse, (double)scale, (unsigned long)responsible_index);
	object_cause_damage(&damage, vehicle_index, NONE, NONE, NONE, NULL);
}

/* one crash, one blow: the scale a rule's damage is dealt with this tick -
the rule's whole scale for a crash's first damage, else only what it is over
the largest the same crash already dealt (0: none). Halo 2's Havok body
resolves a crash's contacts in the step it meets the surface - one
damaging tick a crash (the H2EK tag test, debug_damage: a Ghost driven into
the wall by Zanzibar's gate at 12-20 wu/s, every angle, takes exactly one
'collision' hit) -; this engine's hull is the stand-in's mass points on
penalty springs, which stop the same crash over two to four ticks as its
points meet the surfaces one after another (each tick its own blow by the
decomp's rule: two to three hits, together up to 2.5x Halo 2's). A crash is
the ticks within 0.35 s of its first damage (the window Halo 2 keeps a
body's recent impulse for, unknown_1d96d0.cpp function_1d96d4: 0.35 x the
ticks a second). HALO_H2_CRASH_EACH_TICK=1: every tick its whole blow
(before) */
static real h2cd_crash_scale(struct h2cd_body *body, int rule, real scale)
{
	static int each_tick = -1;
	long tick = game_time_get();

	if (each_tick < 0)
		each_tick = getenv("HALO_H2_CRASH_EACH_TICK") && atoi(getenv("HALO_H2_CRASH_EACH_TICK")) ? 1 : 0;
	if (each_tick || scale <= 0.0f)
		return scale;
	if (body->crash_tick == NONE || tick < body->crash_tick || tick - body->crash_tick > (long)((real)TICKS_PER_SECOND * 0.35f))
	{
		body->crash_tick = tick;
		body->crash_scale[0] = body->crash_scale[1] = 0.0f;
	}
	if (scale <= body->crash_scale[rule])
	{
		if (h2cd_log())
			platform_log("h2 damage: %08lx's crash (tick %ld) goes on at tick %ld: scale %.3f, %.3f already dealt", (unsigned long)body->object_index,
				body->crash_tick, tick, (double)scale, (double)body->crash_scale[rule]);
		return 0.0f;
	}
	{
		real dealt = scale - body->crash_scale[rule];

		body->crash_scale[rule] = scale;
		return dealt;
	}
}

void h2_collision_damage_tick(long vehicle_index, float const collision[9], real speed_before)
{
	struct h2cd_body *body = h2cd_body_get(vehicle_index, FALSE);
	struct object_datum const *vehicle = object_get(vehicle_index);
	real speed = magnitude3d(&vehicle->object.translational_velocity) * (real)TICKS_PER_SECOND;
	real fixed, moving, collision_speed;
	long other = NONE, owner = vehicle_index;
	boolean hit_by_other = FALSE;

	if (!body || body->tick != game_time_get() || !collision)
		return;
	/* (1d96d0's body speed: the rigid body's before the step) */
	if (speed_before >= 0.0f)
		speed = speed_before;
	fixed = body->fixed;
	/* (contacts made together share the stop: a solver's step gives them
	impulses that together stop the body no more than its fastest point
	was moving into the surface) */
	if (body->fixed_sum > body->fixed_raw && body->fixed_sum > 0.0f)
		fixed *= body->fixed_raw / body->fixed_sum;
	moving = body->moving;
	if (body->other != NONE && object_try_and_get(body->other))
		other = body->other;
	collision_speed = MAX(fixed, moving);
	if (collision_speed <= 0.0f)
		return;

	/* (function_1da6d0: the other object's blow) */
	if (other != NONE && moving > fixed * 0.25f)
	{
		struct object_datum const *other_object = object_get(other);
		struct h2vp_definition const *other_definition = h2_vehicle_physics_definition(other_object->definition_index);
		real other_speed = magnitude3d(&other_object->object.translational_velocity) * (real)TICKS_PER_SECOND;
		real maximum_speed = MAX(speed, other_speed);

		if (other_definition && other_definition->has_collision && !(1.5f > speed && 1.5f > other_speed) && maximum_speed >= 0.001f)
		{
			real force = other_speed / maximum_speed * other_definition->collision[0] * collision_speed;

			if (force > collision[1] && collision[2] - collision[1] >= 0.001f)
			{
				real scale = (force - collision[1]) * (collision[4] - collision[3]) / (collision[2] - collision[1]) + collision[3];

				scale = PIN(scale, collision[3], collision[4]);
				owner = h2cd_responsible(other);
				hit_by_other = TRUE;
				h2cd_damage(vehicle_index, owner, h2cd_crash_scale(body, 0, scale), "another body", force);
			}
		}
	}

	/* (function_1da9e0: its own speed against anything) */
	if (speed > 4.0f && collision_speed > collision[5] && collision[6] - collision[5] >= 0.001f)
	{
		real scale = (collision_speed - collision[5]) * (collision[8] - collision[7]) / (collision[6] - collision[5]) + collision[7];

		scale = PIN(scale, collision[7], collision[8]);
		h2cd_damage(vehicle_index, hit_by_other ? owner : h2cd_responsible(vehicle_index), h2cd_crash_scale(body, 1, scale), "its own speed", collision_speed);
	}
}
