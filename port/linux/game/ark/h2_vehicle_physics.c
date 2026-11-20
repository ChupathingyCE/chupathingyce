/*
H2_VEHICLE_PHYSICS.C

port: Halo 2's vehicle force model on a Halo 2 level, in place of the stand-in
vehicle's (vehicles.c): Bungie's game-side vehicle physics (vehicles.cpp
eec90/ef4f0 controls and steering, f1f80 the Warthog, f3010 the Ghost,
f1740/f1950/f15d0 the engine and gears, f5bf0/f5e00/f5ee0 the brakes,
f6bd0 the contact counters; unknown_206090.cpp the friction and
anti-gravity points, unknown_2056e0.cpp the step and its brake), each tick
summing forces and torques on one rigid body - the vehicle's - from its
Halo 2 tags (h2_vehicle_physics.h, read by the level's builder). The body
then moves as this engine moves a vehicle (physics.c's
physics_update_with_forces: its velocities, its mass points as its hull
against the world); none of the model's arithmetic is this engine's, so it
can stand on another solver's body as it is.

- Friction points (the Warthog's wheels): a sphere of the point's radius
  at its marker against the world; compression = radius - distance
  (0.001..radius), normal force N = mass (gravity / ground depth x
  compression - ground damp x normal velocity). The contact's velocity in
  the ground's plane; a powered point's along its rolling direction is the
  wheel's (gear ratio x engine angular velocity); a front turning point
  rolls the steering angle turned, a rear one against it. Within the
  point's moving friction velocity difference it grips: F = -slip x
  sqrt(0.9 ground friction) x fraction of mass x dt x slope x N; past it it
  slides: F = -slip direction x sqrt(0.9 ground moving friction) x fraction
  x slope x N. Slope = 1 - (slope 0 - n.k) / (slope 0 - slope 1), pinned.
  A braking Warthog's wheels lock (their whole velocity slips). The powered
  points' forward pushes are the engine's load.
- Engine: its load slews to the throttle less its speed at 45.45 a second;
  its angular velocity gains (load x ratio + torque) / momentum x dt, the
  torque its gear's cruising and loaded curves blended by the load,
  0.05..1.3 times its maximum; gears up and down at their engine scales
  after their minimum times, a shift taking 0.05 s, reverse and first at
  once, neutral when coasting below a quarter of its maximum.
- Anti-gravity points (the Ghost's): a ray down radius + height (scaled by
  the bank lift) from its marker; lift = (gravity c^2 - damp x normal
  velocity) x strength x mass x power x upright, c = 1 - (distance -
  radius) / height, upright ramping on up.z between normal k0 and k1, a
  small bob (its damage state's error).
- The Ghost (type 4): its ground g (touching points / all x up.z, at least
  0.4, slewed 0.1 a tick); a target velocity from the stick turned into its
  frame, forward and slide speeds blended by the stick's angle, cut on
  slopes (up to 33%), x0.8 braking, x the boost; accelerations capped by
  its acceleration or deceleration, less while touching and steering past
  45 degrees; a yaw torque toward sqrt(2 |steer| x max), countersteering
  harder; off the ground a levelling torque leaning with the stick up to 80
  degrees.
- The step: v += F/m dt, w += I^-1 tau dt; then a brake (the Warthog's
  parking brake) takes velocity off, at most 270 wu/s^2 (level) and 18
  rad/s^2.

HALO_H2_STOCK_VEHICLE_PHYSICS: the stand-in's physics (a comparison);
HALO_H2_VEHICLE_LOG: each driven vehicle's state every 5 ticks.
*/

#include "cseries.h"
#include "ptick.h"
#include "game/game.h"
#include "game/game_globals.h"
#include "math/real_math.h"
#include "math/matrix_math.h"
#include "objects/objects.h"
#include "objects/object_definitions.h"
#include "physics/collision_bsp_definitions.h"
#include "physics/collision_features.h"
#include "physics/collision_model_definitions.h"
#include "physics/collisions.h"
#include "physics/physics.h"
#include "physics/physics_definitions.h"
#include "physics/friction_datum.h"
#include "physics/mass_point_datum.h"
#include "scenario/scenario.h"
#include "structures/structure_bsp_definitions.h"
#include "tag_files/tag_groups.h"
#include "text/text_group.h"
#include "units/units.h"
#include "units/vehicle_datum.h"
#include "h2_vehicle_physics.h"
#include "h2_collision_damage.h"
#include "h2_jolt_hull.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);
real h2_vehicle_boost_scale(long vehicle_index);

/* ---------- constants */

enum
{
	H2VP_MAXIMUM_VEHICLES = 64,
	H2VP_MAXIMUM_DEFINITIONS = 16,
	H2VP_NO_GEAR = -1,
	H2VP_NO_SHIFT = -2,
	H2VP_VEHICLE_TYPE_TANK = 0,
	H2VP_VEHICLE_TYPE_JEEP = 1,
	H2VP_VEHICLE_TYPE_SCOUT = 4,
	H2VP_VEHICLE_TYPE_FIGHTER = 5,
	H2VP_VEHICLE_TYPE_TURRET = 6,
	/* (a fighter's tricks, vehicles.cpp f4360: a roll to the left or right,
	a loop end over end) */
	H2VP_TRICK_NONE = 0,
	H2VP_TRICK_LEFT = 1,
	H2VP_TRICK_RIGHT = 2,
	H2VP_TRICK_LOOP = 3,
};

/* (the fighter's tuning, vehicles.cpp 0x4678b8..0x4678d8: a trick's wait,
its lengths, the lift's speeds, the trick's strafe, the drift's brake, the
fighter's throttle (ef4f0), the dive's climb) */
#define H2VP_TRICK_WAIT 1.5f
#define H2VP_TRICK_LOOP_SECONDS 1.5f
#define H2VP_TRICK_ROLL_SECONDS 1.5f
#define H2VP_LIFT_SPEED_FULL 1.5f
#define H2VP_LIFT_SPEED_NONE 0.5f
#define H2VP_TRICK_STRAFE 7.5f
#define H2VP_DRIFT_BRAKE 0.1f
#define H2VP_FIGHTER_THROTTLE 0.4f
#define H2VP_DIVE_CLIMB 0.4f

#define H2VP_DT (1.0f / 30.0f)
#define H2VP_PI 3.1415927f
/* (the hull's friction on the ground, at most this times its normal force:
Halo 2's hull is a Havok body whose contacts the engine solves; this body's
hull is the stand-in's mass points, their viscous friction capped) */
#define H2VP_HULL_FRICTION 1.0f

/* ---------- structures */

struct h2vp_vehicle
{
	long vehicle;
	struct h2vp_definition const *definition;
	/* (the controls, eec90) */
	real speed;
	real turn;
	real steering_angle;
	/* (the engine, f1950: its angular velocity, load, the drive's
	reaction, gear, the gear being shifted to and the shift's ticks, ticks
	in the gear, over its up-shift scale and under its down-shift scale) */
	real engine_angular_velocity;
	real engine_load;
	real drive_reaction;
	signed char gear;
	signed char pending_gear;
	unsigned char shift_ticks;
	unsigned char gear_ticks;
	unsigned char upshift_ticks;
	unsigned char downshift_ticks;
	/* (the brakes, f5bf0, f5ee0) */
	unsigned char brake_ticks;
	unsigned char idle_ticks;
	/* (f6bd0: ticks off the ground, touching, upside down and touching) */
	unsigned char airborne_ticks;
	unsigned char contact_ticks;
	unsigned char upside_down_ticks;
	/* (a rigid hull's: ticks by the game's at-rest rule) */
	unsigned char rest_ticks;
	/* (the Ghost's ground, f3010) */
	real ground;
	/* (the e-brake held, this tick) */
	boolean e_brake;
	/* (a fighter's trick, f4800: its kind and its ticks since it began -
	or since the last one did) */
	unsigned char trick;
	unsigned char trick_ticks;
	/* (each friction point's: set when not gripping, 2056e0) */
	unsigned long contact_flags;
	real wheel_spin;
	boolean touching;
	/* (the log's) */
	long gripping, sliding, hull_points;
	real_vector3d last_velocity, outside_change, physics_change;
	unsigned long hull_mask;
	real last_wheel_speed;
};

/* the state the step fills (205be0's), a tick's */
struct h2vp_step
{
	real_point3d center;            /* the body's centre of mass */
	real_point3d center_of_mass;    /* (its place in the model: the stand-in's physics') */
	real_vector3d forward, left, up;
	real_vector3d velocity;         /* a second's */
	real_vector3d angular_velocity;
	real_vector3d force;            /* a mass's: world units a second squared */
	real_vector3d torque;           /* (r x the force a mass) */
	real_vector3d angular_acceleration; /* (radians a second squared, put as I alpha) */
	real steering;                  /* (the eased steering angle, eec90) */
	real reaction;                  /* (the powered points' forward pushes, the mass's) */
	long touching_antigrav;
	long gripping, touching_friction, sliding;
	boolean moving_ground;
};

/* ---------- globals */

static struct
{
	long scenario;
	boolean halo2;
	struct
	{
		long definition_index;
		struct h2vp_definition const *physics;
	} definitions[H2VP_MAXIMUM_DEFINITIONS];
	long definition_count;
	struct h2vp_vehicle vehicles[H2VP_MAXIMUM_VEHICLES];
	/* (halo_map_generation when the level's values were taken) */
	unsigned long generation;
} h2vp = { NONE };

/* (HALO_H2_VEHICLE_LOG=2: each wheel's compression, normal, side and
forward force, a mass's) */
static boolean h2vp_wheel_log;
static char h2vp_wheel_log_text[512];
static int h2vp_wheel_log_length;

extern unsigned long halo_map_generation;

/* ---------- private code */

static void h2vp_level(void)
{
	char const *name;
	long i;

	/* (the map's generation too: consecutive maps' scenarios can have the
	same tag index - burial_mounds and coagulation, one after the other, did
	-, and the definitions and vehicles kept then point into the map gone,
	a vehicle of the same object index finding its slot with the last map's
	definition: a segfault in h2vp_antigrav) */
	if (h2vp.scenario == global_scenario_index && h2vp.generation == halo_map_generation)
		return;
	h2vp.scenario = global_scenario_index;
	h2vp.generation = halo_map_generation;
	name = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : "";
	h2vp.halo2 = name && !strncmp(name, "h2\\", 3) && !getenv("HALO_H2_STOCK_VEHICLE_PHYSICS");
	h2vp.definition_count = 0;
	for (i = 0; i < H2VP_MAXIMUM_VEHICLES; i++)
		h2vp.vehicles[i].vehicle = NONE;
}

/* the force model's values of a vehicle's definition (its string list
"<definition's name> physics"), NULL if it has none */
static struct h2vp_definition const *h2vp_definition_of(long definition_index)
{
	struct h2vp_definition const *physics = NULL;
	char name[256];
	long i, list;

	h2vp_level();
	if (!h2vp.halo2)
		return NULL;
	for (i = 0; i < h2vp.definition_count; i++)
	{
		if (h2vp.definitions[i].definition_index == definition_index)
			return h2vp.definitions[i].physics;
	}
	snprintf(name, sizeof(name), "%s physics", tag_get_name(definition_index));
	list = tag_loaded('str#', name);
	if (list != NONE)
	{
		struct string_list *strings = (struct string_list *)tag_get('str#', list);

		/* (its first string; a second is its damage definition, h2_vehicle_damage.c) */
		if (strings->strings.count >= 1)
		{
			struct string_list_entry *entry = TAG_BLOCK_GET_ELEMENT(&strings->strings, 0, struct string_list_entry);
			struct h2vp_definition const *candidate = (struct h2vp_definition const *)entry->string.address;

			if (entry->string.size >= (long)sizeof(struct h2vp_definition) && candidate && candidate->magic == H2VP_MAGIC &&
				candidate->version == H2VP_VERSION && candidate->mass > 0.0f && candidate->gear_count <= H2VP_MAXIMUM_GEARS &&
				candidate->friction_count <= H2VP_MAXIMUM_POINTS && candidate->antigrav_count <= H2VP_MAXIMUM_POINTS &&
				(((candidate->type == H2VP_VEHICLE_TYPE_JEEP || candidate->type == H2VP_VEHICLE_TYPE_TANK) && candidate->friction_count > 0 &&
					candidate->gear_count > 0 &&
					candidate->engine_maximum_angular_velocity > 0.001f && candidate->engine_momentum > 0.0f) ||
				(candidate->type == H2VP_VEHICLE_TYPE_SCOUT && candidate->antigrav_count > 0) ||
				candidate->type == H2VP_VEHICLE_TYPE_FIGHTER || candidate->type == H2VP_VEHICLE_TYPE_TURRET))
			{
				physics = candidate;
			}
		}
	}
	if (h2vp.definition_count < H2VP_MAXIMUM_DEFINITIONS)
	{
		h2vp.definitions[h2vp.definition_count].definition_index = definition_index;
		h2vp.definitions[h2vp.definition_count++].physics = physics;
	}
	platform_log("h2 vehicle physics: %s: %s", tag_get_name(definition_index), physics ? "Halo 2's force model" : "the stand-in's");
	/* (the audit's: its Halo 2 handling values as the level read them) */
	if (physics)
	{
		long g;

		platform_log("h2 vehicle physics: %s: type %d control %d specific %d, speed %.2f/%.2f wu/s accel %.2f/%.2f, slide %.2f/%.2f accel %.2f/%.2f, "
			"turn %.1f/%.1f deg at %.1f deg/s, overdampen %.1f^%.2f, turn scale %.2f, engine %.2f rad/s momentum %.3f, %ld gears, "
			"ground friction %.2f moving %.2f depth %.3f damp %.2f slope %.2f..%.2f, gravity scale %.2f, mass %.1f, damping %.3f/%.3f, flags %lx/%lx",
			tag_get_name(definition_index), (int)physics->type, (int)physics->control, (int)physics->specific_type, physics->speed[0], physics->speed[1],
			physics->speed[2], physics->speed[3], physics->slide[0], physics->slide[1], physics->slide[2], physics->slide[3], physics->turn_angles[0],
			physics->turn_angles[1], physics->turn_rate, physics->overdampen_cusp, physics->overdampen_exponent, physics->turn_scale,
			physics->engine_maximum_angular_velocity, physics->engine_momentum, physics->gear_count, physics->ground_friction,
			physics->ground_moving_friction, physics->ground_depth, physics->ground_damp, physics->slope[0], physics->slope[1], physics->gravity_scale,
			physics->mass, physics->linear_damping, physics->angular_damping, physics->vehicle_flags, physics->physics_flags);
		for (g = 0; g < physics->gear_count; g++)
		{
			struct h2vp_gear const *gear = &physics->gears[g];

			platform_log("h2 vehicle physics: %s: gear %ld ratio %.3f up after %.2f s over %.2f, down after %.2f s under %.2f, loaded %.1f..%.1f peak %.2f^%.2f max %.1f 2x %.1f, cruising %.1f..%.1f peak %.2f^%.2f max %.1f 2x %.1f",
				tag_get_name(definition_index), g, gear->ratio, gear->minimum_upshift_time, gear->upshift_scale, gear->minimum_downshift_time,
				gear->downshift_scale, gear->loaded[0], gear->loaded[1], gear->loaded[2], gear->loaded[3], gear->loaded[4], gear->loaded[5],
				gear->cruising[0], gear->cruising[1], gear->cruising[2], gear->cruising[3], gear->cruising[4], gear->cruising[5]);
		}
		for (g = 0; g < physics->friction_count; g++)
		{
			struct h2vp_friction_point const *f = &physics->friction[g];

			platform_log("h2 vehicle physics: %s: friction point %ld at %.3f %.3f %.3f flags %lx fraction %.3f radius %.3f moving dv %.2f e-brake %.2f/%.2f dv %.2f",
				tag_get_name(definition_index), g, f->position[0], f->position[1], f->position[2], f->flags, f->fraction, f->radius,
				f->moving_velocity_difference, f->e_brake_friction, f->e_brake_moving_friction, f->e_brake_moving_velocity_difference);
		}
		for (g = 0; g < physics->antigrav_count; g++)
		{
			struct h2vp_antigrav_point const *a = &physics->antigrav[g];

			platform_log("h2 vehicle physics: %s: anti-gravity point %ld at %.3f %.3f %.3f flags %lx strength %.2f height %.3f damp %.2f radius %.3f normal %.2f..%.2f error %.2f",
				tag_get_name(definition_index), g, a->position[0], a->position[1], a->position[2], a->flags, a->strength, a->height, a->damp,
				a->radius, a->normal_k0, a->normal_k1, a->error);
		}
	}
	return physics;
}

/* (h2_vehicle_boost.c) a vehicle definition's force model values, NULL if
it has none */
struct h2vp_definition const *h2_vehicle_physics_definition(long definition_index)
{
	return h2vp_definition_of(definition_index);
}

static struct h2vp_vehicle *h2vp_vehicle_get(long vehicle_index, struct h2vp_definition const *definition, boolean create)
{
	long i, free_slot = NONE;

	for (i = 0; i < H2VP_MAXIMUM_VEHICLES; i++)
	{
		if (h2vp.vehicles[i].vehicle == vehicle_index)
		{
			/* (ptick.h) a vehicle's Halo 2 physics state is kept HERE, outside
			the game state a speculative update's snapshot puts back (its
			gears, wheel spin, contact ticks: advanced every update): a
			vehicle with it is not speculated */
			if (HALO_PTICK_GUARDED(_ptick_abort_other, "h2 vehicle physics"))
				return NULL;
			return &h2vp.vehicles[i];
		}
		if (free_slot == NONE && (h2vp.vehicles[i].vehicle == NONE || !object_try_and_get(h2vp.vehicles[i].vehicle)))
			free_slot = i;
	}
	if (!create || free_slot == NONE)
		return NULL;
	if (HALO_PTICK_GUARDED(_ptick_abort_other, "h2 vehicle physics"))
		return NULL;
	csmemset(&h2vp.vehicles[free_slot], 0, sizeof(h2vp.vehicles[free_slot]));
	h2vp.vehicles[free_slot].vehicle = vehicle_index;
	h2vp.vehicles[free_slot].definition = definition;
	h2vp.vehicles[free_slot].gear = H2VP_NO_GEAR;
	h2vp.vehicles[free_slot].pending_gear = H2VP_NO_SHIFT;
	return &h2vp.vehicles[free_slot];
}

static real h2vp_sign(real value)
{
	return value == 0.0f ? 0.0f : (value < 0.0f ? -1.0f : 1.0f);
}

static real h2vp_dot(real_vector3d const *a, real_vector3d const *b)
{
	return a->i * b->i + a->j * b->j + a->k * b->k;
}

static void h2vp_cross(real_vector3d const *a, real_vector3d const *b, real_vector3d *result)
{
	real_vector3d r;

	r.i = a->j * b->k - a->k * b->j;
	r.j = a->k * b->i - a->i * b->k;
	r.k = a->i * b->j - a->j * b->i;
	*result = r;
}

static void h2vp_add_scaled(real_vector3d *a, real_vector3d const *b, real scale)
{
	a->i += b->i * scale;
	a->j += b->j * scale;
	a->k += b->k * scale;
}

static void h2vp_scale(real_vector3d *v, real scale)
{
	v->i *= scale;
	v->j *= scale;
	v->k *= scale;
}

static real h2vp_length(real_vector3d const *v)
{
	return (real)sqrt(v->i * v->i + v->j * v->j + v->k * v->k);
}

/* (206094) made unit length, its length returned (0 and untouched: too short) */
static real h2vp_normalize(real_vector3d *v)
{
	real length = h2vp_length(v);

	if (fabs(length) < 0.0001f)
		return 0.0f;
	v->i /= length;
	v->j /= length;
	v->k /= length;
	return length;
}

static void h2vp_limit(real_vector3d *v, real maximum)
{
	real length_squared = h2vp_dot(v, v);

	if (length_squared > maximum * maximum)
	{
		real scale = maximum / (real)sqrt(length_squared);

		v->i *= scale;
		v->j *= scale;
		v->k *= scale;
	}
}

static real h2vp_normalize2d(real *x, real *y)
{
	real length = (real)sqrt(*x * *x + *y * *y);

	if (fabs(length) < 0.0001f || length == 0.0f)
	{
		*x = 1.0f;
		*y = 0.0f;
		return 0.0f;
	}
	*x /= length;
	*y /= length;
	return length;
}

/* (2053c0) a value moved toward rates' maximum (direction > 0) or minimum
by its acceleration, its deceleration while it is on the other side */
static void h2vp_rate_step(real *value, real const *rates, real direction)
{
	if (direction != 0.0f)
	{
		real acceleration = (real)fabs(direction) * rates[2] * H2VP_DT;
		real deceleration = (real)fabs(direction) * rates[3] * H2VP_DT;

		if (direction > 0.0f)
		{
			if (*value <= -deceleration)
				*value += deceleration;
			else if (*value >= 0.0f)
				*value += acceleration;
			else
				*value = (*value / deceleration + 1.0f) * acceleration;
			*value = *value > rates[0] ? rates[0] : *value;
		}
		else
		{
			if (*value >= deceleration)
				*value -= deceleration;
			else if (*value <= 0.0f)
				*value -= acceleration;
			else
				*value = (*value / deceleration - 1.0f) * acceleration;
			*value = *value > -rates[1] ? *value : -rates[1];
		}
	}
}

/* (eec90's) a value moved toward a bound, not past it */
static void h2vp_approach(real *value, real const *rates, real bound)
{
	if (*value > bound)
	{
		h2vp_rate_step(value, rates, -1.0f);
		if (bound >= *value)
			*value = bound;
	}
	else if (bound > *value)
	{
		h2vp_rate_step(value, rates, 1.0f);
		if (*value >= bound)
			*value = bound;
	}
}

/* (f15d0) a torque curve's torque at an engine angular velocity */
static real h2vp_curve_torque(struct h2vp_definition const *d, float const *curve, real angular_velocity)
{
	real maximum = d->engine_maximum_angular_velocity;
	real peak = maximum * (curve[2] < 0.001f ? 0.001f : curve[2]);

	if (peak > angular_velocity)
		return peak > 0.001f ? (curve[1] - curve[0]) / peak * angular_velocity + curve[0] : 0.0f;
	if (angular_velocity >= maximum)
	{
		real slope = (curve[5] - curve[4]) / maximum;

		return curve[4] - maximum * slope + slope * angular_velocity;
	}
	if (fabs(maximum - peak) > 0.0001f && fabs(curve[1]) > 0.001f)
	{
		real slope = (curve[4] - curve[1]) / (maximum - peak);
		real fraction = (slope * angular_velocity + curve[1] - slope * peak) / curve[1];

		if (fraction >= 0.0f && fraction <= 1.0f)
			fraction = (real)pow(fraction, curve[3]);
		return fraction * curve[1];
	}
	return 0.0f;
}

static real h2vp_gear_ratio(struct h2vp_definition const *d, long gear)
{
	return gear >= 0 && gear < d->gear_count ? d->gears[gear].ratio : 0.0f;
}

/* (f5d70) braking: a moment after the brake (or the throttle against the
motion) begins, or while the brake is held */
static boolean h2vp_braking(struct h2vp_vehicle const *s, boolean brake_control)
{
	return s->brake_ticks && (s->brake_ticks * H2VP_DT < 0.5f || brake_control);
}

/* (f1740) the gear to shift to: neutral coasting slowly; a wheeled vehicle's
(type 1) reverse or first gear as the throttle's direction, up and down at
its engine's shift points; a tank's (type 0) as it is - its control's,
h2vp_tank_controls */
static long h2vp_gear_choose(struct h2vp_vehicle *s, real throttle_input, boolean brake_control, long driver)
{
	struct h2vp_definition const *d = s->definition;
	boolean braking = h2vp_braking(s, brake_control);
	long current = s->gear, gear = current;
	real throttle = braking ? 0.0f : throttle_input;
	real ratio = PIN(s->engine_angular_velocity / d->engine_maximum_angular_velocity, 0.0f, 1.0f);

	if (throttle == 0.0f)
	{
		if (d->engine_maximum_angular_velocity * 0.25f > s->engine_angular_velocity ||
			(driver == NONE && s->gear_ticks * H2VP_DT > 1.0f))
		{
			return H2VP_NO_GEAR;
		}
	}
	else if (d->type != H2VP_VEHICLE_TYPE_JEEP)
	{
	}
	else if (throttle < 0.0f)
	{
		if (!braking)
			return 0;
	}
	else if (current < 1)
	{
		if (!braking)
			return 1;
	}
	else
	{
		struct h2vp_gear const *g = &d->gears[current];

		if (s->upshift_ticks * H2VP_DT > g->minimum_upshift_time && throttle >= ratio && ratio > g->upshift_scale)
			gear = PIN(current + 1, 1, d->gear_count - 1);
		if (s->downshift_ticks * H2VP_DT > g->minimum_downshift_time && g->downshift_scale > ratio)
		{
			if (gear <= 0 || current - 1 < 1)
				return 1;
			gear = current - 1;
		}
	}
	return gear;
}

/* (f1950) the engine run a tick */
static void h2vp_engine_update(struct h2vp_vehicle *s, real throttle_input, long new_gear, boolean brake_control)
{
	struct h2vp_definition const *d = s->definition;
	real maximum = d->engine_maximum_angular_velocity;
	real throttle = !s->shift_ticks && !h2vp_braking(s, brake_control) ? throttle_input : 0.0f;
	long current = s->gear;
	real gear_ratio = h2vp_gear_ratio(d, current);
	real engine = PIN(s->engine_angular_velocity, maximum * -0.5f, maximum * 1.5f) / maximum;
	boolean reversed = new_gear != H2VP_NO_SHIFT && (current >= 1) != (new_gear >= 1);

	s->engine_load = PIN(s->engine_load + (throttle - engine) * (H2VP_DT * 45.454544f), 0.0f, 1.0f);
	if (reversed || (current == H2VP_NO_GEAR && new_gear != current))
	{
		s->gear = (signed char)new_gear;
		s->pending_gear = H2VP_NO_SHIFT;
		s->gear_ticks = s->upshift_ticks = s->shift_ticks = s->downshift_ticks = 0;
	}
	else if (s->pending_gear == H2VP_NO_SHIFT && new_gear == current)
	{
		s->shift_ticks = 0;
		s->gear_ticks = (unsigned char)MIN(s->gear_ticks + 1, 0xFF);
		if (current >= 0)
		{
			struct h2vp_gear const *g = &d->gears[current];

			s->upshift_ticks = engine > g->upshift_scale ? (unsigned char)MIN(s->upshift_ticks + 1, 0xFF) : 0;
			s->downshift_ticks = g->downshift_scale > engine ? (unsigned char)MIN(s->downshift_ticks + 1, 0xFF) : 0;
		}
	}
	else
	{
		long delay = MAX(1, (long)(TICKS_PER_SECOND * 0.05f));

		if (!s->shift_ticks)
		{
			s->pending_gear = (signed char)new_gear;
			s->shift_ticks = 1;
		}
		else if (s->shift_ticks == delay)
		{
			s->gear = s->pending_gear;
			s->pending_gear = H2VP_NO_SHIFT;
			s->gear_ticks = s->upshift_ticks = s->shift_ticks = s->downshift_ticks = 0;
		}
		else
			s->shift_ticks++;
	}
	if (s->gear >= 0)
	{
		struct h2vp_gear const *g = &d->gears[s->gear];
		real torque = h2vp_curve_torque(d, g->cruising, s->engine_angular_velocity) * (1.0f - s->engine_load) +
			h2vp_curve_torque(d, g->loaded, s->engine_angular_velocity) * s->engine_load;

		s->engine_angular_velocity += (s->drive_reaction * gear_ratio + torque) / d->engine_momentum * H2VP_DT;
		s->engine_angular_velocity = PIN(s->engine_angular_velocity, maximum * 0.05f, maximum * 1.3f);
	}
	else
		s->engine_angular_velocity *= 0.95f;
}

/* (eec90) the throttle's and steering's values, and the steering angle
slewed toward the eased angle to the driver's facing; the eased angle */
static real h2vp_controls_update(struct h2vp_vehicle *s, struct vehicle_datum const *vehicle, real angle, boolean braking)
{
	struct h2vp_definition const *d = s->definition;
	real throttle = vehicle->unit.throttle.i, steering = vehicle->unit.throttle.j;
	real cusp, exponent, eased, target, rate, minimum, maximum, difference;

	if (braking)
	{
		h2vp_approach(&s->speed, d->speed, 0.0f);
		if (d->control != 2)
			h2vp_approach(&s->turn, d->slide, 0.0f);
	}
	else
	{
		h2vp_approach(&s->speed, d->speed, (throttle < 0.0f ? d->speed[1] : d->speed[0]) * throttle);
		/* (a tank's turn is its control's, ef070: h2vp_tank_controls) */
		if (d->control != 2)
			h2vp_approach(&s->turn, d->slide, (steering < 0.0f ? d->slide[1] : d->slide[0]) * steering);
	}
	cusp = DEGREES_TO_RADIANS(d->overdampen_cusp == 0.0f ? 1.0f : d->overdampen_cusp);
	exponent = d->overdampen_exponent == 0.0f ? 1.0f : d->overdampen_exponent;
	angle = PIN(angle, -H2VP_PI, H2VP_PI);
	eased = (real)pow(fabs(angle / cusp), exponent) * (angle >= 0.0f ? 1.0f : -1.0f) * cusp;
	eased = PIN(eased, -H2VP_PI, H2VP_PI);
	target = s->gear ? eased : -eased;
	rate = DEGREES_TO_RADIANS(d->turn_rate);
	minimum = DEGREES_TO_RADIANS(d->turn_angles[1]);
	maximum = DEGREES_TO_RADIANS(d->turn_angles[0]);
	difference = target - s->steering_angle;
	if (difference != 0.0f)
	{
		real direction = difference > 0.0f ? 1.0f : -1.0f;
		real remaining;

		s->steering_angle = PIN(direction * rate * H2VP_DT + s->steering_angle, minimum, maximum);
		remaining = h2vp_sign(target - s->steering_angle);
		if (direction != remaining)
			s->steering_angle = target;
	}
	return eased;
}

/* (206090) a point's velocity: the body's at it */
static void h2vp_point_velocity(struct h2vp_step const *step, real_vector3d const *radius, real_vector3d *velocity)
{
	h2vp_cross(&step->angular_velocity, radius, velocity);
	velocity->i += step->velocity.i;
	velocity->j += step->velocity.j;
	velocity->k += step->velocity.k;
}

/* a point of the model's space in the world: the body's position is its
centre of mass, the model's origin the centre's place in it back from it
(objects.c's node matrices) */
static void h2vp_point_world(struct vehicle_datum const *vehicle, struct h2vp_step const *step, float const *place, real_point3d *point)
{
	real x = place[0] - step->center_of_mass.x, y = place[1] - step->center_of_mass.y, z = place[2] - step->center_of_mass.z;

	point->x = vehicle->object.position.x + step->forward.i * x + step->left.i * y + step->up.i * z;
	point->y = vehicle->object.position.y + step->forward.j * x + step->left.j * y + step->up.j * z;
	point->z = vehicle->object.position.z + step->forward.k * x + step->left.k * y + step->up.k * z;
}

/* the force a point's force puts on the body: its force and its torque
about the centre of mass */
static void h2vp_apply(struct h2vp_step *step, real_vector3d const *radius, real_vector3d const *force)
{
	real_vector3d torque;

	step->force.i += force->i;
	step->force.j += force->j;
	step->force.k += force->k;
	h2vp_cross(radius, force, &torque);
	step->torque.i += torque.i;
	step->torque.j += torque.j;
	step->torque.k += torque.k;
}

/* a smooth noise in 0..1 (Halo 2's periodic function 8's table is the
engine's: a stand-in with its period, 28 of its input) */
static real h2vp_noise(real x)
{
	real t = x * (2.0f * H2VP_PI / 28.0f);

	return 0.5f + 0.25f * (real)sin(t * 7.0f) + 0.15f * (real)sin(t * 13.0f + 1.3f) + 0.1f * (real)sin(t * 29.0f + 2.1f);
}

/* (206090) the anti-gravity points' lift */
static void h2vp_antigrav(struct h2vp_vehicle *s, struct vehicle_datum const *vehicle, struct h2vp_step *step, long vehicle_index,
	real power)
{
	struct h2vp_definition const *d = s->definition;
	real gravity = global_gravity * TICKS_PER_SECOND * TICKS_PER_SECOND;
	long i;

	for (i = 0; i < d->antigrav_count; i++)
	{
		struct h2vp_antigrav_point const *a = &d->antigrav[i];
		real_point3d point;
		real_vector3d radius, velocity, probe;
		real bank, by_steering, by_stick, larger, lift_scale, radius_scaled, height_scaled;
		struct collision_result collision;

		h2vp_point_world(vehicle, step, a->position, &point);
		vector_from_points3d(&step->center, &point, &radius);
		h2vp_point_velocity(step, &radius, &velocity);
		/* (a banking vehicle lifts its turn's outside: the steering's bank
		reaction against the throttle's direction, or the stick's slide, x
		the point's side) */
		bank = PIN(d->bank_reaction * step->steering * 0.31830987f, -1.0f, 1.0f) * (vehicle->unit.throttle.i >= 0.0f ? -1.0f : 1.0f);
		by_stick = -(vehicle->unit.throttle.j * a->position[1]);
		by_steering = bank * bank * h2vp_sign(bank) * a->position[1];
		larger = fabs(by_stick) > fabs(by_steering) ? by_stick : by_steering;
		larger = larger > 0.0f ? PIN(larger - 0.1f, 0.0f, 1.0f) : PIN(larger + 0.1f, -1.0f, 0.0f);
		lift_scale = d->bank_lift * larger + 1.0f;
		radius_scaled = lift_scale * a->radius;
		height_scaled = lift_scale * a->height;
		set_real_vector3d(&probe, 0.0f, 0.0f, -(radius_scaled + height_scaled));
		if (collision_test_vector(_collision_test_for_bipeds_dead_flags, &point, &probe, vehicle_index, &collision))
		{
			real distance = (radius_scaled + height_scaled) * collision.t - radius_scaled;
			real upright, c, lift;
			real_vector3d force;

			if (fabs(a->normal_k1 - a->normal_k0) < 0.001f)
				upright = a->normal_k0 < step->up.k ? 1.0f : 0.0f;
			else if (a->normal_k1 > a->normal_k0)
				upright = a->normal_k0 < step->up.k ? (step->up.k < a->normal_k1 ? (step->up.k - a->normal_k0) / (a->normal_k1 - a->normal_k0) : 1.0f) : 0.0f;
			else
				upright = a->normal_k1 < step->up.k ? (step->up.k < a->normal_k0 ? (a->normal_k0 - step->up.k) / (a->normal_k0 - a->normal_k1) : 0.0f) : 1.0f;
			c = distance > 0.0f ? 1.0f - distance / height_scaled : 1.0f;
			lift = (gravity * c * c - a->damp * h2vp_dot(&velocity, &collision.plane.n)) * power * a->strength * upright;
			if (a->error > 0.0f)
			{
				real phase = (((real)((i + 1) / d->antigrav_count) * 0.1f + 0.9f) * (real)game_time_get() * H2VP_DT * 0.3f + (real)i * 42.0f);

				lift *= 1.0f - a->error * (h2vp_noise(phase) * 0.55f + 0.45f);
			}
			force = collision.plane.n;
			force.i *= lift;
			force.j *= lift;
			force.k *= lift;
			h2vp_apply(step, &radius, &force);
			step->touching_antigrav++;
			if (h2vp_wheel_log)
			{
				h2vp_wheel_log_length += snprintf(h2vp_wheel_log_text + h2vp_wheel_log_length, sizeof(h2vp_wheel_log_text) - h2vp_wheel_log_length,
					" [%ld h%.3f c%.2f L%.2f]", i, distance + radius_scaled, c, lift);
			}
		}
	}
}

/* (206090) the friction points' (the wheels') forces; whether they grip */
static boolean h2vp_friction(struct h2vp_vehicle *s, struct vehicle_datum const *vehicle, struct h2vp_step *step, long vehicle_index,
	real wheel_speed, real right_wheel_speed, boolean locked, boolean e_brake)
{
	struct h2vp_definition const *d = s->definition;
	real gravity = global_gravity * TICKS_PER_SECOND * TICKS_PER_SECOND;
	real steering_cosine = (real)cos(s->steering_angle), steering_sine = (real)sin(s->steering_angle);
	long i, processed = 0;

	s->contact_flags = 0;
	for (i = 0; i < d->friction_count; i++)
	{
		struct h2vp_friction_point const *f = &d->friction[i];
		boolean powered = TEST_FLAG(f->flags, _h2vp_friction_powered_bit) != 0;
		real_point3d point;
		real_vector3d radius, velocity, roll, force;
		struct collision_feature_list features;
		struct collision_plane contact;
		boolean touching = FALSE, sliding = FALSE;

		processed++;
		h2vp_point_world(vehicle, step, f->position, &point);
		vector_from_points3d(&step->center, &point, &radius);
		h2vp_point_velocity(step, &radius, &velocity);
		set_real_vector3d(&force, 0.0f, 0.0f, 0.0f);
		/* (its rolling direction: the body's forward, a turning point's
		turned by the steering angle about its up) */
		roll = step->forward;
		if (TEST_FLAG(f->flags, _h2vp_friction_front_turning_bit) || TEST_FLAG(f->flags, _h2vp_friction_rear_turning_bit))
		{
			real sine = TEST_FLAG(f->flags, _h2vp_friction_front_turning_bit) ? steering_sine : -steering_sine;

			roll.i = step->forward.i * steering_cosine + step->left.i * sine;
			roll.j = step->forward.j * steering_cosine + step->left.j * sine;
			roll.k = step->forward.k * steering_cosine + step->left.k * sine;
		}
		if (f->radius > 0.0f &&
			collision_get_features_in_sphere(_collision_test_for_bipeds_dead_flags, &point, f->radius, 0.0f, f->radius, vehicle_index, &features) &&
			collision_features_test_point(&features, &point, &contact) && contact.t > 0.0f)
		{
			real compression = PIN(contact.t, 0.001f, f->radius);
			real_vector3d const *n = &contact.plane.n;

			if (compression > 0.0001f)
			{
				real normal_velocity = h2vp_dot(&velocity, n);
				real normal = gravity / d->ground_depth * compression - d->ground_damp * normal_velocity;
				real_vector3d direction = roll, plane, slip;
				real along;

				force = *n;
				force.i *= normal;
				force.j *= normal;
				force.k *= normal;
				along = h2vp_dot(&direction, n);
				h2vp_add_scaled(&direction, n, -along);
				if (h2vp_normalize(&direction) != 0.0f)
				{
					real slip_speed, grip, moving, threshold, slope;
					real_vector3d friction;
					real_vector3d const *use = &slip;

					plane = velocity;
					h2vp_add_scaled(&plane, n, -normal_velocity);
					slip = plane;
					/* (a tank's: its left points - even - and right - odd - at
					their sides' speeds, f1cd0) */
					if (powered)
						h2vp_add_scaled(&slip, &direction, -((i & 1) ? right_wheel_speed : wheel_speed));
					else
						h2vp_add_scaled(&slip, &direction, -h2vp_dot(&plane, &direction));
					slip_speed = h2vp_length(&slip);
					grip = d->ground_friction;
					moving = d->ground_moving_friction;
					threshold = f->moving_velocity_difference;
					/* (205be0/206090: the e-brake held, its points - the
					Warthog's rear wheels - locked, on their e-brake
					friction, moving friction and its velocity difference) */
					if (e_brake && TEST_FLAG(f->flags, _h2vp_friction_e_brake_bit))
					{
						grip = f->e_brake_friction;
						moving = f->e_brake_moving_friction;
						threshold = f->e_brake_moving_velocity_difference;
						use = &plane;
						slip_speed = h2vp_length(&plane);
					}
					if (locked)
					{
						use = &plane;
						slip_speed = h2vp_length(&plane);
					}
					slope = 1.0f - PIN((d->slope[0] - n->k) / (d->slope[0] - d->slope[1]), 0.0f, 1.0f);
					if (slip_speed > threshold)
					{
						friction = *use;
						h2vp_normalize(&friction);
						h2vp_scale(&friction, -(f->fraction * (real)sqrt(moving * 0.9f) * slope * normal));
						sliding = TRUE;
					}
					else
					{
						friction = *use;
						h2vp_scale(&friction, -((real)sqrt(grip * 0.9f) * f->fraction * H2VP_DT * slope * normal));
						step->gripping++;
					}
					add_vectors3d(&force, &friction, &force);
					/* (a powered point's push along its rolling direction: the
					engine's load) */
					if (powered)
					{
						real forward = h2vp_dot(&direction, &friction);

						step->reaction += forward > 0.0f ? -(real)fabs(forward) : (real)fabs(forward);
					}
				}
				touching = TRUE;
				step->touching_friction++;
				if (h2vp_wheel_log)
				{
					h2vp_wheel_log_length += snprintf(h2vp_wheel_log_text + h2vp_wheel_log_length, sizeof(h2vp_wheel_log_text) - h2vp_wheel_log_length,
						" [%ld c%.3f N%.2f lat%.2f lon%.2f %s n%.2f,%.2f,%.2f o%lx]", i, compression, normal, h2vp_dot(&force, &step->left), h2vp_dot(&force, &step->forward),
						sliding ? "S" : "g", n->i, n->j, n->k, (unsigned long)contact.object_index);
				}
			}
		}
		h2vp_apply(step, &radius, &force);
		if (!touching || sliding)
			s->contact_flags |= 1UL << i;
		if (sliding)
			step->sliding++;
	}
	return d->friction_count && step->gripping > (processed >> 1) && step->up.k > d->slope[1];
}

/* (f3010) the Ghost's controller: its forces and torques */
static void h2vp_scout(struct h2vp_vehicle *s, struct vehicle_datum const *vehicle, struct h2vp_step *step, long vehicle_index,
	real steering, boolean braking, real power)
{
	struct h2vp_definition const *d = s->definition;
	real boost = h2_vehicle_boost_scale(vehicle_index);
	real_vector3d force, alpha;

	set_real_vector3d(&force, 0.0f, 0.0f, 0.0f);
	set_real_vector3d(&alpha, 0.0f, 0.0f, 0.0f);
	if (step->up.k > -0.2f)
	{
		real forward_speed = h2vp_dot(&step->forward, &step->velocity);
		real left_speed = h2vp_dot(&step->left, &step->velocity);
		real facing_x = vehicle->unit.desired_facing_vector.i, facing_y = vehicle->unit.desired_facing_vector.j;
		real forward_x = step->forward.i, forward_y = step->forward.j;
		real cosine, sine, input_x, input_y;

		h2vp_normalize2d(&facing_x, &facing_y);
		h2vp_normalize2d(&forward_x, &forward_y);
		cosine = forward_x * facing_x + forward_y * facing_y;
		sine = facing_y * forward_x - forward_y * facing_x;
		input_y = cosine * vehicle->unit.throttle.j + sine * vehicle->unit.throttle.i;
		input_x = cosine * vehicle->unit.throttle.i - sine * vehicle->unit.throttle.j;
		if (s->ground > 0.0f)
		{
			real angle = (real)atan2(input_y, input_x);
			real forward_fraction = (1.5707964f - (real)fabs(angle)) * 0.63661975f;
			real forward_weight = (real)fabs(forward_fraction);
			real side_weight = (real)fabs((1.0f - forward_weight) * (angle > 0.0f ? 1.0f : -1.0f));
			boolean forwards = forward_fraction > 0.0f, leftwards = angle > 0.0f && forward_weight < 1.0f;
			real forward_maximum = forwards ? d->speed[0] : d->speed[1];
			real side_maximum = leftwards ? d->slide[0] : d->slide[1];
			real target, target_forward, target_left, heading, heading_forward, heading_side, maximum_acceleration;
			real_vector3d acceleration;
			boolean driven;

			/* (a Ghost's slopes slow it, up to a third) */
			if (d->specific_type == 1)
			{
				real pitch = (real)atan2(step->forward.k, sqrt(step->forward.i * step->forward.i + step->forward.j * step->forward.j));
				real roll = (real)atan2(step->left.k, sqrt(step->left.i * step->left.i + step->left.j * step->left.j));
				real pitch_slope = MIN(pitch * 2.3149807f * (pitch * 2.3149807f), 1.0f);
				real roll_slope = MIN(roll * 2.3149807f * (roll * 2.3149807f), 1.0f);

				forward_maximum -= (forwards == (pitch > 0.0f) ? 1.0f : -1.0f) * (real)fabs(pitch_slope) * forward_maximum * 0.33f;
				side_maximum -= (leftwards == (roll > 0.0f) ? 1.0f : -1.0f) * (real)fabs(roll_slope) * side_maximum * 0.33f;
			}
			target = side_maximum * side_weight + forward_maximum * forward_weight;
			if (braking)
				target *= 0.8f;
			target *= boost;
			target_forward = target * input_x;
			target_left = target * input_y;
			acceleration.i = (target_forward - forward_speed) / H2VP_DT;
			acceleration.j = (target_left - left_speed) / H2VP_DT;
			acceleration.k = 0.0f;
			driven = vehicle->unit.throttle.i != 0.0f || vehicle->unit.throttle.j != 0.0f;
			heading = driven ? (real)atan2(target_left, target_forward) : (real)atan2(-acceleration.j, -acceleration.i);
			heading_forward = (real)fabs((1.5707964f - (real)fabs(heading)) * 0.63661975f);
			heading_side = (real)fabs((1.0f - heading_forward) * (heading > 0.0f ? 1.0f : -1.0f));
			maximum_acceleration = driven ? d->slide[2] * heading_side + d->speed[2] * heading_forward :
				d->slide[3] * heading_side + d->speed[3] * heading_forward;
			/* (touching something and steering hard: less push) */
			if (s->contact_ticks > 0 && fabs(steering) > 0.78539819f)
				maximum_acceleration *= 1.0f - MIN(s->contact_ticks * 0.05f, 0.98f);
			h2vp_limit(&acceleration, maximum_acceleration * boost);
			h2vp_add_scaled(&force, &step->forward, acceleration.i * s->ground);
			h2vp_add_scaled(&force, &step->left, acceleration.j * s->ground);
		}
		if (s->ground > 0.0f)
		{
			real current, maximum, target, yaw;

			if (d->specific_type != 1 && d->specific_type != 2 && d->specific_type != 3)
			{
				current = h2vp_dot(&step->angular_velocity, &step->up);
				maximum = H2VP_PI;
			}
			else
			{
				/* (countersteering: harder) */
				current = (h2vp_dot(&step->angular_velocity, &step->up) + step->angular_velocity.k) * 0.5f;
				maximum = 4.712389f;
				if ((steering > 0.0f) != (current > 0.0f))
					maximum += (PIN((real)fabs(current), 0.78539819f, H2VP_PI) - 0.78539819f) * 14.0f;
			}
			target = (real)sqrt(fabs(steering) * maximum * 2.0f) * h2vp_sign(steering);
			if (h2vp_sign(steering) == h2vp_sign(target) && fabs(target) * H2VP_DT * 2.0f > fabs(steering))
				target = steering / H2VP_DT * 0.5f;
			yaw = PIN((target - current) / H2VP_DT, -maximum, maximum) * s->ground;
			h2vp_add_scaled(&alpha, &step->up, yaw);
		}
		if (s->ground < 1.0f)
		{
			/* (off the ground: levelling, leaning with the stick up to 80
			degrees) */
			real forward2d_x = step->forward.i, forward2d_y = step->forward.j;
			real left2d_x = step->left.i, left2d_y = step->left.j;
			real pitch_target, roll_target, airborne = 1.0f - s->ground;

			h2vp_normalize2d(&forward2d_x, &forward2d_y);
			h2vp_normalize2d(&left2d_x, &left2d_y);
			if (step->up.k > 0.0f)
			{
				real error_x = -(step->up.i * forward2d_x + step->up.j * forward2d_y) -
					(step->angular_velocity.i * left2d_x + step->angular_velocity.j * left2d_y) * 0.5f;
				real error_y = -(step->up.i * left2d_x + step->up.j * left2d_y) +
					(step->angular_velocity.i * forward2d_x + step->angular_velocity.j * forward2d_y) * 0.5f;
				real pitch_error = (real)fabs(error_x) * h2vp_sign(error_x * input_x);
				real roll_error = (real)fabs(error_y) * h2vp_sign(error_y * input_y);
				real damping = (1.0f - step->up.k) * 3.4906585f;

				pitch_target = PIN(pitch_error + 1.0f, 0.3f, 2.5f) * input_x * 1.3962634f + error_x * damping;
				roll_target = PIN(roll_error + 1.0f, 0.3f, 2.5f) * input_y * 1.3962634f + error_y * damping;
			}
			else
			{
				pitch_target = input_x * 1.3962634f;
				roll_target = input_y * 1.3962634f;
			}
			h2vp_add_scaled(&alpha, &step->left, pitch_target * airborne);
			h2vp_add_scaled(&alpha, &step->forward, -roll_target * airborne);
		}
		if (braking)
		{
			/* (the brake held: its nose up and a little lift with speed;
			just off the ground, a push on) */
			real speed = PIN(h2vp_dot(&step->forward, &step->velocity) / d->speed[0], 0.0f, 1.0f);
			real_vector3d up = { 0.0f, 0.0f, 1.0f };

			if (speed > 0.0f)
			{
				h2vp_add_scaled(&alpha, &step->left, speed * s->ground * -5.2359877f);
				h2vp_add_scaled(&force, &up, speed * s->ground * 3.6000001f);
			}
			if (s->airborne_ticks > 0)
			{
				real_vector3d direction;

				h2vp_cross(&step->left, &up, &direction);
				if (h2vp_normalize(&direction) > 0.0f)
				{
					real fade = PIN(1.0f - s->airborne_ticks * H2VP_DT, 0.0f, 1.0f);
					real push = (1.0f - s->ground) * fade;

					h2vp_add_scaled(&force, &direction, push * 1.8000001f);
					h2vp_add_scaled(&force, &up, push * 0.90000004f);
				}
			}
		}
		force.i *= power;
		force.j *= power;
		force.k *= power;
		alpha.i *= power;
		alpha.j *= power;
		alpha.k *= power;
	}
	step->force.i += force.i;
	step->force.j += force.j;
	step->force.k += force.k;
	step->angular_acceleration.i += alpha.i;
	step->angular_acceleration.j += alpha.j;
	step->angular_acceleration.k += alpha.k;
}

/* (ef070) a tank's control: the stick a direction in the world (turned by
the driver's facing, the camera's), the vehicle turning toward it - its
turn the angle to it x the turn scale, at most 45 degrees, x the maximum
turn (0x22C), slewed at its rates - and driving forward, or backward when
it lies more than 110 degrees behind an axis between the hull's forward and
the facing (forward x (1 - forward . facing) + facing: with the camera
turned 130 degrees from the hull the axis lies 37 degrees toward it, so
the stick pushed that way still drives forward, turning; backward only
from about 142 degrees), the gear the control's own, 1 forward or 0
reverse, while the stick is pushed (the gear chooser shifts a wheeled
vehicle's gears, f1740's "type 1", never a tank's); the stick's push its
throttle, signed by the gear */
static real h2vp_tank_controls(struct h2vp_vehicle *s, struct vehicle_datum const *vehicle, struct h2vp_step const *step)
{
	struct h2vp_definition const *d = s->definition;
	real facing_x = vehicle->unit.desired_facing_vector.i, facing_y = vehicle->unit.desired_facing_vector.j;
	real forward_x = step->forward.i, forward_y = step->forward.j;
	real stick_x = vehicle->unit.throttle.i, stick_y = vehicle->unit.throttle.j;
	real input_x, input_y, angle, magnitude = (real)sqrt(stick_x * stick_x + stick_y * stick_y), steer, bound;
	real blend_x, blend_y, k;
	boolean reversing;

	h2vp_normalize2d(&facing_x, &facing_y);
	h2vp_normalize2d(&forward_x, &forward_y);
	/* (the stick in the world: forward along the facing, left to its left) */
	input_x = facing_x * stick_x - facing_y * stick_y;
	input_y = facing_y * stick_x + facing_x * stick_y;
	/* (the reversing axis: the forward leaned toward the facing) */
	k = 1.0f - (facing_x * forward_x + facing_y * forward_y);
	blend_x = forward_x * k + facing_x;
	blend_y = forward_y * k + facing_y;
	if (h2vp_normalize2d(&blend_x, &blend_y) == 0.0f)
	{
		blend_x = forward_x;
		blend_y = forward_y;
	}
	reversing = !((real)fabs(atan2(input_y * blend_x - blend_y * input_x, blend_x * input_x + blend_y * input_y)) < 1.9198622f);
	angle = (real)atan2(input_y * forward_x - forward_y * input_x, forward_x * input_x + forward_y * input_y);
	if (reversing)
		angle += angle > 0.0f ? -H2VP_PI : H2VP_PI;
	if (magnitude > 0.0f)
		s->gear = reversing ? 0 : 1;
	steer = PIN(d->turn_scale * angle, -0.78539819f, 0.78539819f) * 1.2732395f;
	bound = (steer < 0.0f ? d->slide[1] : d->slide[0]) * steer;
	h2vp_approach(&s->turn, d->slide, bound);
	return MIN(magnitude, 1.0f) * (s->gear == 0 ? -1.0f : 1.0f);
}

/* (f1380) the acceleration taking velocity to target in a tick, at most
limit, or aligned_limit for the part along target */
static void h2vp_seek(real_vector3d *acceleration, real_vector3d const *target, real_vector3d const *velocity, real aligned_limit, real limit)
{
	real dot, maximum, length_squared;

	acceleration->i = (target->i - velocity->i) / H2VP_DT;
	acceleration->j = (target->j - velocity->j) / H2VP_DT;
	acceleration->k = (target->k - velocity->k) / H2VP_DT;
	dot = h2vp_dot(target, acceleration);
	length_squared = h2vp_dot(acceleration, acceleration);
	if (dot > 0.0001f && length_squared > 0.0f && h2vp_dot(target, target) > 0.0f)
		maximum = dot * dot / length_squared / h2vp_dot(target, target) * (aligned_limit - limit) + limit;
	else
		maximum = limit;
	if (length_squared > maximum * maximum)
		h2vp_scale(acceleration, length_squared > 0.0f ? maximum / (real)sqrt(length_squared) : 0.0f);
}

/* (f4250) how far through its trick the fighter is: eased out for a roll,
in for a loop; 0 none */
static real h2vp_trick_fraction(struct h2vp_vehicle const *s)
{
	real duration = s->trick == H2VP_TRICK_LOOP ? H2VP_TRICK_LOOP_SECONDS : s->trick ? H2VP_TRICK_ROLL_SECONDS : 0.0f;
	real elapsed = s->trick_ticks * H2VP_DT, fraction;

	if (!s->trick || elapsed >= duration)
		return 0.0f;
	fraction = elapsed / duration;
	if (s->trick != H2VP_TRICK_LOOP)
		return 1.0f - (1.0f - fraction) * (1.0f - fraction);
	return fraction * fraction;
}

/* (f4360) the trick the fighter's controls ask for: the brake held and the
stick pushed far - back (144..216 degrees) a loop, else a roll to that
side; none */
static unsigned char h2vp_trick_asked(struct vehicle_datum const *vehicle, boolean brake_control)
{
	real throttle = vehicle->unit.throttle.i, steering = vehicle->unit.throttle.j, direction;

	if (!brake_control || (fabs(throttle) <= 0.75f && fabs(steering) <= 0.7f))
		return H2VP_TRICK_NONE;
	direction = (real)fmod(atan2(steering, throttle) + 4.0 * H2VP_PI, 2.0 * H2VP_PI);
	if (direction >= 2.5132742f && direction <= 3.7699113f)
		return H2VP_TRICK_LOOP;
	if (direction < 0.08726646f || direction > 6.195919f)
		return H2VP_TRICK_NONE;
	return direction <= 2.5132742f ? H2VP_TRICK_LEFT : H2VP_TRICK_RIGHT;
}

/* a frame's (forward, left, up) turned about an axis by an angle (Rodrigues) */
static void h2vp_rotate(real_vector3d *v, real_vector3d const *axis, real angle)
{
	real c = (real)cos(angle), sn = (real)sin(angle), d = h2vp_dot(axis, v);
	real_vector3d cross, out;

	h2vp_cross(axis, v, &cross);
	out.i = v->i * c + cross.i * sn + axis->i * d * (1.0f - c);
	out.j = v->j * c + cross.j * sn + axis->j * d * (1.0f - c);
	out.k = v->k * c + cross.k * sn + axis->k * d * (1.0f - c);
	*v = out;
}

/* (f4800) the fighter - the Banshee: a trick the controls start, a push
toward its speed along its facing (faster diving, with its boost), its
strafe, lift against its gravity with speed, and a turn toward its aim,
level, banked into its turns and strafe, its trick's roll or loop */
static void h2vp_fighter(struct h2vp_vehicle *s, struct vehicle_datum const *vehicle, struct h2vp_step *step, long vehicle_index,
	boolean brake_control, real power, real body_gravity_scale)
{
	struct h2vp_definition const *d = s->definition;
	real gravity = global_gravity * TICKS_PER_SECOND * TICKS_PER_SECOND;
	real boost_power = h2_vehicle_boost_scale(vehicle_index);
	boolean boosting = boost_power > 1.0f, flipping;
	real_vector3d aim, facing_forward, facing_up, facing_left, desired_forward, desired_up, desired_left;
	real_vector3d velocity = step->velocity, target, acceleration, alpha;
	real pitch, climb, maximum_speed, speed, throttle, fraction, lift, fastest, horizontal;
	unsigned char asked;

	/* (a trick, the last one's wait over) */
	if (s->trick_ticks > (unsigned char)(H2VP_TRICK_WAIT * TICKS_PER_SECOND + 0.5f) && power > 0.0f &&
		(asked = h2vp_trick_asked(vehicle, brake_control)) != H2VP_TRICK_NONE)
	{
		s->trick = asked;
		s->trick_ticks = 0;
		if (getenv("HALO_H2_VEHICLE_LOG"))
			platform_log("h2 vehicle %lx trick %d", (unsigned long)vehicle_index, (int)asked);
	}
	fraction = h2vp_trick_fraction(s);
	flipping = fraction > 0.0f;
	if (s->trick_ticks < 0xFE)
		s->trick_ticks++;

	/* (f44a0: its facing - its aim, held level) */
	aim = vehicle->unit.desired_aiming_vector;
	if (h2vp_normalize(&aim) <= 0.0f)
		aim = step->forward;
	if (getenv("HALO_H2_VEHICLE_LOG") && (game_time_get() % 15) == 0)
		platform_log("h2 fighter %lx aim %.2f %.2f %.2f facing %.2f %.2f %.2f speed %.2f turn %.2f trick %d/%d power %.2f", (unsigned long)vehicle_index,
			aim.i, aim.j, aim.k, vehicle->unit.desired_facing_vector.i, vehicle->unit.desired_facing_vector.j, vehicle->unit.desired_facing_vector.k,
			s->speed, s->turn, (int)s->trick, (int)s->trick_ticks, power);
	facing_forward = aim;
	set_real_vector3d(&facing_up, 0.0f, 0.0f, 1.0f);
	h2vp_add_scaled(&facing_up, &facing_forward, -facing_forward.k);
	if (h2vp_normalize(&facing_up) <= 0.0f)
		set_real_vector3d(&facing_up, 1.0f, 0.0f, 0.0f);
	h2vp_cross(&facing_up, &facing_forward, &facing_left);

	/* (the push along its forward: its speed, its boost, faster diving) */
	pitch = (real)atan2(step->forward.k, sqrt(step->forward.i * step->forward.i + step->forward.j * step->forward.j));
	climb = PIN((real)fabs(pitch), 0.0f, 0.58904862f) * (pitch < 0.0f ? 1.0f : -1.0f) * H2VP_DIVE_CLIMB * 1.6976527f + 1.0f;
	maximum_speed = d->speed[0] * boost_power * climb;
	speed = s->speed * boost_power * climb;
	target = step->forward;
	h2vp_scale(&target, speed);
	if (boosting && !flipping && !brake_control)
		throttle = 1.0f;
	else if (s->speed > 0.0f)
		throttle = fabs(maximum_speed) < 0.001f ? 0.0f : s->speed / maximum_speed;
	else
		throttle = fabs(d->speed[1]) < 0.001f ? 0.0f : -s->speed / d->speed[1];
	h2vp_seek(&acceleration, &target, &velocity, d->speed[2] * throttle, d->speed[3] * throttle);
	if (s->speed == 0.0f)
	{
		/* (no throttle: its drift, less its sideways part, braked a little) */
		real_vector3d drift = velocity;

		h2vp_add_scaled(&drift, &facing_left, -h2vp_dot(&facing_left, &velocity));
		drift.k = 0.0f;
		h2vp_add_scaled(&acceleration, &drift, -(real)fabs(d->speed[3]) * H2VP_DRIFT_BRAKE);
	}
	if (!(s->speed > 0.0f))
	{
		/* (none past stopping; none up or down) */
		real *a = &acceleration.i, *v = &velocity.i;
		int c;

		for (c = 0; c < 3; c++)
		{
			if ((a[c] + v[c] >= 0.0f) != (a[c] <= 0.0f))
				a[c] = -v[c];
		}
		acceleration.k = 0.0f;
	}
	/* (its strafe: a roll's at its trick's speed) */
	{
		boolean rolling = flipping && s->trick != H2VP_TRICK_LOOP;

		if (!boosting || rolling)
		{
			real flip_scale = rolling ? (1.0f - fraction) * H2VP_TRICK_STRAFE * boost_power : 1.0f;
			real strafe_maximum = d->speed[0] * flip_scale;
			real strafe = s->turn * flip_scale;
			real strafe_fraction = fabs(strafe_maximum) < 0.001f ? 0.0f : (real)fabs(strafe) / strafe_maximum;
			real side_speed = h2vp_dot(&facing_left, &velocity), along;
			real_vector3d strafe_target = facing_left, strafe_velocity = facing_left, strafe_acceleration;

			if (rolling)
				strafe = (s->trick == H2VP_TRICK_LEFT ? 1.0f : -1.0f) * (real)fabs(d->slide[0]) * flip_scale;
			h2vp_scale(&strafe_target, strafe);
			h2vp_scale(&strafe_velocity, side_speed);
			h2vp_seek(&strafe_acceleration, &strafe_target, &strafe_velocity, d->slide[2] * strafe_fraction,
				strafe_fraction * flip_scale * d->slide[3]);
			along = h2vp_dot(&acceleration, &facing_left) * strafe_fraction;
			h2vp_add_scaled(&acceleration, &facing_left, -along);
			acceleration.i += strafe_acceleration.i;
			acceleration.j += strafe_acceleration.j;
			acceleration.k += strafe_acceleration.k;
			if (s->turn == 0.0f && !rolling)
			{
				real limit = H2VP_DRIFT_BRAKE * d->slide[3], brake = MIN((real)fabs(side_speed), limit);

				h2vp_add_scaled(&acceleration, &facing_left, side_speed < 0.0f ? brake : -brake);
			}
		}
	}
	/* (lift against its gravity, with speed: none below 0.5 wu/s, all of
	it from 1.5) */
	horizontal = (real)sqrt(velocity.i * velocity.i + velocity.j * velocity.j);
	fastest = MAX((real)fabs(s->speed), MAX((real)fabs(s->turn), horizontal));
	lift = PIN(PIN(fastest - H2VP_LIFT_SPEED_NONE, 0.0f, H2VP_LIFT_SPEED_FULL) / (H2VP_LIFT_SPEED_FULL - H2VP_LIFT_SPEED_NONE), 0.0f, 1.0f);
	if (s->speed == 0.0f && !flipping)
		lift *= 1.0f - PIN(velocity.k, 0.0f, 1.0f) * 0.2f;
	lift = PIN(lift, 0.0f, 1.0f);
	acceleration.k += d->gravity_scale * lift * gravity;
	h2vp_scale(&acceleration, power);
	/* (its body falls by its own gravity scale - Halo 2's body's, the
	vehicle's 0x2E0 -, not the stand-in's) */
	if (d->gravity_scale > 0.0f)
		acceleration.k += (body_gravity_scale - d->gravity_scale) * gravity;
	step->force.i += acceleration.i;
	step->force.j += acceleration.j;
	step->force.k += acceleration.k;

	/* (the turn: toward its aim, level, banked; through its trick) */
	if (flipping)
	{
		real_vector3d axis;

		desired_forward = facing_forward;
		desired_up = facing_up;
		if (s->trick == H2VP_TRICK_LOOP)
		{
			/* (end over end about its left) */
			axis = facing_left;
			h2vp_rotate(&desired_forward, &axis, -fraction * 2.0f * H2VP_PI);
			h2vp_rotate(&desired_up, &axis, -fraction * 2.0f * H2VP_PI);
		}
		else
		{
			/* (a roll about its forward) */
			real angle = s->trick == H2VP_TRICK_LEFT ? 0.19634955f - fraction * 6.0868359f : fraction * 6.0868359f - 0.19634955f;

			axis = facing_forward;
			h2vp_rotate(&desired_up, &axis, angle);
		}
	}
	else
	{
		desired_forward = aim;
		desired_up = facing_up;
	}
	if (!flipping || s->trick != H2VP_TRICK_LOOP)
	{
		real maximum_steering = DEGREES_TO_RADIANS(d->turn_angles[0]);
		real speed_fraction = d->speed[0] < 0.001f ? 0.0f : (real)fabs(s->speed) / d->speed[0];
		real steering_fraction = (real)fabs(maximum_steering < 0.001f ? 0.0f : s->steering_angle / maximum_steering);
		real turn_fraction = d->slide[0] < 0.001f ? 0.0f : s->turn / d->slide[0];
		real steering_bias = (real)fabs(steering_fraction) * 2.0f;
		real slip = velocity.j * desired_forward.i - desired_forward.j * velocity.i;
		real slip_bank = 0.0f, turn_bank, strafe_bank, bank;

		if (fabs(slip) >= 0.001f)
		{
			if (slip < 0.0f)
				slip_bank = MIN(slip + steering_bias, 0.0f) - steering_bias;
			else
				slip_bank = MAX(slip - steering_bias, 0.0f) + steering_bias;
		}
		turn_bank = maximum_speed > 0.001f ? MAX(speed_fraction, steering_fraction) / maximum_speed * slip_bank * maximum_steering : 0.0f;
		strafe_bank = turn_fraction * maximum_steering * -0.7f;
		if (strafe_bank < 0.0f)
			bank = turn_bank < 0.0f ? MIN(turn_bank, strafe_bank) : strafe_bank + turn_bank;
		else
			bank = turn_bank < 0.0f ? strafe_bank + turn_bank : MAX(turn_bank, strafe_bank);
		{
			real_vector3d axis;

			h2vp_cross(&desired_forward, &desired_up, &axis);
			{
				real_vector3d up = desired_up;

				desired_up.i = axis.i * (real)sin(bank) + up.i * (real)cos(bank);
				desired_up.j = axis.j * (real)sin(bank) + up.j * (real)cos(bank);
				desired_up.k = axis.k * (real)sin(bank) + up.k * (real)cos(bank);
			}
		}
	}
	h2vp_cross(&desired_up, &desired_forward, &desired_left);
	{
		/* (the turn from its frame to the desired one: its axis and angle;
		a turn rate toward it of angle x the maximum turn rate / pi, reached
		in a tick, by its inertia x the flying torque scale) */
		real_vector3d error, torque_axis, want;
		real cosine, angle, rate, scale;

		h2vp_cross(&step->forward, &desired_forward, &error);
		{
			real_vector3d e;

			h2vp_cross(&step->left, &desired_left, &e);
			add_vectors3d(&error, &e, &error);
			h2vp_cross(&step->up, &desired_up, &e);
			add_vectors3d(&error, &e, &error);
		}
		cosine = PIN((h2vp_dot(&step->forward, &desired_forward) + h2vp_dot(&step->left, &desired_left) +
			h2vp_dot(&step->up, &desired_up) - 1.0f) * 0.5f, -1.0f, 1.0f);
		angle = (real)acos(cosine);
		torque_axis = error;
		if (h2vp_normalize(&torque_axis) <= 0.0f)
			angle = 0.0f;
		rate = angle * DEGREES_TO_RADIANS(d->turn_rate) * 0.31830987f;
		want = torque_axis;
		h2vp_scale(&want, rate);
		alpha.i = (want.i - step->angular_velocity.i) / H2VP_DT;
		alpha.j = (want.j - step->angular_velocity.j) / H2VP_DT;
		alpha.k = (want.k - step->angular_velocity.k) / H2VP_DT;
		/* (the tag's 0: its tags' flying torque scale is 0 - Havok's - , 1
		here) */
		scale = (d->flying_torque_scale > 0.0f ? d->flying_torque_scale : 1.0f) * power;
		h2vp_scale(&alpha, scale);
		step->angular_acceleration.i += alpha.i;
		step->angular_acceleration.j += alpha.j;
		step->angular_acceleration.k += alpha.k;
	}
}

/* a velocity change (a tick's) given the body at a point: all of it to
its centre, and its turn by the body's inertia (this engine's: the
stand-in's inverse inertia, its mass) */
static void h2vp_point_impulse(long vehicle_index, real_point3d const *at, real_vector3d const *change)
{
	struct vehicle_datum *vehicle = vehicle_datum_get(vehicle_index);
	struct object_definition *object_definition = object_definition_get(vehicle->definition_index);
	struct physics_definition *physics;
	real_vector3d radius, moment, turn;
	real_matrix3x3 frame, inverse, frame_transpose;

	add_vectors3d(&vehicle->object.translational_velocity, change, &vehicle->object.translational_velocity);
	vehicle->object.flags &= ~FLAG(_object_at_rest_bit);
	if (!at || object_definition->object.physics.index == NONE)
		return;
	physics = physics_definition_get(object_definition->object.physics.index);
	if (physics->inertial_matrix.count < 2 || physics->mass <= 0.0f)
		return;
	vector_from_points3d(&vehicle->object.position, at, &radius);
	h2vp_cross(&radius, change, &moment);
	h2vp_scale(&moment, physics->mass);
	matrix3x3_from_forward_and_up(&frame, &vehicle->object.forward, &vehicle->object.up);
	matrix3x3_multiply(&frame, TAG_BLOCK_GET_ELEMENT(&physics->inertial_matrix, 1, real_matrix3x3), &inverse);
	frame_transpose = frame;
	matrix3x3_multiply(&inverse, matrix3x3_transpose(&frame_transpose, &frame_transpose), &inverse);
	matrix3x3_transform_vector(&inverse, &moment, &turn);
	add_vectors3d(&vehicle->object.angular_velocity, &turn, &vehicle->object.angular_velocity);
}

/* ---------- public code */

/* (vehicles.c) whether the vehicle moves by Halo 2's force model (and has,
this tick): its update in place of its vehicle type's */
boolean h2_vehicle_physics_update(
	long vehicle_index,
	real steering_angle,
	struct mass_point_datum *mass_points)
{
	struct vehicle_datum *vehicle = vehicle_datum_get(vehicle_index);
	struct h2vp_definition const *d = h2vp_definition_of(vehicle->definition_index);
	struct object_definition *object_definition;
	struct physics_definition *physics;
	struct h2vp_vehicle *s;
	struct h2vp_step step;
	boolean brake_control, braking, grips = FALSE, rigid_hull;
	real throttle, power, brake = 0.0f, wheel_speed = 0.0f;
	unsigned long passive = 0, touching_mask = 0;
	long driver = vehicle->unit.driver_object_index, i;
	real_vector3d force, torque, last_angular_velocity;

	if (!d)
		return FALSE;
	object_definition = object_definition_get(vehicle->definition_index);
	if (object_definition->object.physics.index == NONE)
		return FALSE;
	physics = physics_definition_get(object_definition->object.physics.index);
	if (physics->mass <= 0.0f || physics->inertial_matrix.count < 2 || physics->mass_points.count > 32)
		return FALSE;
	s = h2vp_vehicle_get(vehicle_index, d, TRUE);
	if (!s)
		return FALSE;
	/* (a test's: HALO_H2_VEHICLE_PROBE="x0 y0 x1 y1 step" logs the ground's
	height over that grid once, for finding flat ground to drive on) */
	if (getenv("HALO_H2_VEHICLE_PROBE"))
	{
		static boolean probed;
		float x0, y0, x1, y1, grid;

		char text[128], *c;

		snprintf(text, sizeof(text), "%s", getenv("HALO_H2_VEHICLE_PROBE"));
		for (c = text; *c; c++)
		{
			if (*c == '_' || *c == ',')
				*c = ' ';
		}
		if (!probed && sscanf(text, "%f %f %f %f %f", &x0, &y0, &x1, &y1, &grid) == 5 && grid > 0.0f)
		{
			float x, y;

			probed = TRUE;
			for (y = y0; y <= y1; y += grid)
			{
				char line[1024];
				int length = 0;

				for (x = x0; x <= x1 && length < 1000; x += grid)
				{
					real_point3d top = { x, y, 12.0f };
					real_vector3d down = { 0.0f, 0.0f, -40.0f };
					struct collision_result collision;

					/* (HALO_H2_VEHICLE_PROBE_FLAGS: the test's collision flags,
					hexadecimal - 1000e9 a projectile's -, the vehicles' else;
					an object hit marked '*') */
					unsigned long flags = getenv("HALO_H2_VEHICLE_PROBE_FLAGS") ?
						strtoul(getenv("HALO_H2_VEHICLE_PROBE_FLAGS"), NULL, 16) : (unsigned long)_collision_test_for_bipeds_dead_flags;

					if (collision_test_vector(flags, &top, &down, vehicle_index, &collision))
						length += snprintf(line + length, sizeof(line) - length, " %6.2f%s", collision.point.z, collision.object_index != NONE ? "*" : "");
					else
						length += snprintf(line + length, sizeof(line) - length, "   none");
				}
				platform_log("h2 vehicle probe y %6.1f:%s", y, line);
			}
		}
	}
	if (getenv("HALO_H2_VEHICLE_PHYSICS_DUMP"))
	{
		static long dumped[8];
		static long dumped_count;
		long k;

		for (k = 0; k < dumped_count && dumped[k] != vehicle->definition_index; k++)
			;
		if (k == dumped_count && dumped_count < 8)
		{
			dumped[dumped_count++] = vehicle->definition_index;
			platform_log("h2 vehicle physics: %s stand-in physics mass %.1f com %.3f %.3f %.3f moments %.1f %.1f %.1f, %ld mass points",
				tag_get_name(vehicle->definition_index), physics->mass, physics->center_of_mass.x, physics->center_of_mass.y,
				physics->center_of_mass.z, physics->xx_moment, physics->yy_moment, physics->zz_moment, physics->mass_points.count);
			for (k = 0; k < physics->mass_points.count; k++)
			{
				struct mass_point_definition *m = TAG_BLOCK_GET_ELEMENT(&physics->mass_points, k, struct mass_point_definition);

				platform_log("h2 vehicle physics:   mass point %ld '%s' powered %d at %.3f %.3f %.3f radius %.3f mass %.1f friction %d %.2f %.2f",
					k, m->name, m->powered_mass_point_index, m->position.x, m->position.y, m->position.z, m->radius, m->mass,
					m->friction_type, m->friction_parallel_scale, m->friction_perpendicular_scale);
			}
		}
	}

	/* (the controls: the jump button the brake, as the stand-in's) */
	brake_control = TEST_FLAG(vehicle->unit.control_flags, _unit_control_jump_bit) != 0;
	/* (ef4f0: a fighter's throttle is at least the fighter throttle, 0.4
	(0x4678d4): the stick let go or pulled back still flies the Banshee
	forward at 0.4 of its maximum forward speed - it neither stops nor backs
	up -, a push past it flies it faster, to its 4.5 wu/s, and its boost
	(peak power 2.5) past that. The decompilation reads the clamp as a
	ceiling too (anything outside 0..0.4 made 0.4); the tags say otherwise:
	the Banshee's strafe is 3.5 wu/s and the Ghost flies 5.0, so a 1.8 wu/s
	Banshee that strafes faster than it flies, and boosts to 4.5 while a
	Ghost boosts to 8.25, is not Halo 2's - a floor of 0.4 is) */
	if (d->type == H2VP_VEHICLE_TYPE_FIGHTER && vehicle->unit.throttle.i < H2VP_FIGHTER_THROTTLE)
		vehicle->unit.throttle.i = H2VP_FIGHTER_THROTTLE;
	throttle = vehicle->unit.throttle.i;
	power = vehicle->unit.seat_power[0];

	/* (a rigid hull, h2_jolt_hull.c: the last step's result on the object) */
	last_angular_velocity = vehicle->object.angular_velocity;
	rigid_hull = h2_jolt_hull_begin(vehicle_index, d, physics);

	/* (the log's: what changed its velocity since the last tick's end) */
	vector_from_points3d((real_point3d *)&s->last_velocity, (real_point3d *)&vehicle->object.translational_velocity, &s->outside_change);

	/* (205be0: the body) */
	csmemset(&step, 0, sizeof(step));
	step.forward = vehicle->object.forward;
	step.up = vehicle->object.up;
	h2vp_cross(&step.up, &step.forward, &step.left);
	step.center = vehicle->object.position;
	step.center_of_mass = physics->center_of_mass;
	step.velocity = vehicle->object.translational_velocity;
	step.velocity.i *= TICKS_PER_SECOND;
	step.velocity.j *= TICKS_PER_SECOND;
	step.velocity.k *= TICKS_PER_SECOND;
	step.angular_velocity = vehicle->object.angular_velocity;
	step.angular_velocity.i *= TICKS_PER_SECOND;
	step.angular_velocity.j *= TICKS_PER_SECOND;
	step.angular_velocity.k *= TICKS_PER_SECOND;
	/* (a rigid body turns about its own centre of mass, Halo 2's: the
	state's centre and velocity its, not the object's position's) */
	if (rigid_hull)
	{
		real_vector3d arm, swing;

		h2_jolt_hull_center(vehicle_index, &step.center);
		vector_from_points3d(&vehicle->object.position, &step.center, &arm);
		h2vp_cross(&step.angular_velocity, &arm, &swing);
		step.velocity.i += swing.i;
		step.velocity.j += swing.j;
		step.velocity.k += swing.k;
	}

	if (d->type == H2VP_VEHICLE_TYPE_JEEP)
	{
		/* (f5bf0: the brake's ticks - the brake, or the throttle against
		the motion past 1 wu/s) */
		boolean counting = brake_control;

		if (!counting && throttle != 0.0f && h2vp_length(&step.velocity) > 1.0f)
		{
			real along = h2vp_dot(&step.forward, &step.velocity);

			counting = (throttle < 0.0f && along > 0.0f) || (throttle > 0.0f && along < 0.0f);
		}
		s->brake_ticks = counting ? (unsigned char)MIN(s->brake_ticks + 1, 0xFE) : 0;
	}
	/* (ef4f0/eec90: braking - the brake, or the throttle against the
	speed when the vehicle says so - and the steering) */
	braking = brake_control || (TEST_FLAG(d->vehicle_flags, 4) &&
		((throttle > 0.0f && s->speed < 0.0f) || (throttle < 0.0f && s->speed > 0.0f)));
	/* (a fighter brakes but while it does a trick or boosts, eec90) */
	if (d->type == H2VP_VEHICLE_TYPE_FIGHTER && (h2vp_trick_fraction(s) > 0.0f || h2_vehicle_boost_scale(vehicle_index) > 1.0f))
		braking = FALSE;
	step.steering = h2vp_controls_update(s, vehicle, steering_angle, braking);

	if (d->type == H2VP_VEHICLE_TYPE_JEEP)
	{
		/* (f1f80: the engine, the wheels' speed, the steering) */
		boolean locked = h2vp_braking(s, brake_control);

		h2vp_wheel_log = getenv("HALO_H2_VEHICLE_LOG") && atoi(getenv("HALO_H2_VEHICLE_LOG")) >= 2 && (game_time_get() % 5) == 0 && driver != NONE;
		h2vp_wheel_log_length = 0;
		h2vp_wheel_log_text[0] = 0;
		h2vp_engine_update(s, (real)fabs(throttle), h2vp_gear_choose(s, throttle, brake_control, driver), brake_control);
		wheel_speed = locked || s->gear == H2VP_NO_GEAR ? 0.0f : h2vp_gear_ratio(d, s->gear) * s->engine_angular_velocity;
		/* (the tag's maximum forward and reverse speeds, 0x1F4/0x1F8: Halo
		2's Warthog keeps to its 7.65 wu/s, where on this engine's body - the
		stand-in's hull, no Havok solver between its ticks - its top gear's
		wheels (1.2 x the engine's 7.75) ran it to 9. A cap of this port's,
		not Bungie's code: the powered wheels roll no faster than the
		maximum speed; HALO_H2_NO_SPEED_CAP none; a rigid hull none) */
		if (!rigid_hull && !getenv("HALO_H2_NO_SPEED_CAP"))
		{
			if (d->speed[0] > 0.5f && wheel_speed > d->speed[0])
				wheel_speed = d->speed[0];
			if (d->speed[1] > 0.5f && wheel_speed < -d->speed[1])
				wheel_speed = -d->speed[1];
		}
		/* (ef4f0: the e-brake - a vehicle with one, its driver's crouch:
		the left trigger, Left Shift or Ctrl, the Vita's L as the boost) */
		s->e_brake = TEST_FLAG(d->vehicle_flags, 17) && driver != NONE &&
			TEST_FLAG(vehicle->unit.control_flags, _unit_control_crouch_modifier_bit) && !getenv("HALO_H2_NO_EBRAKE");
		grips = h2vp_friction(s, vehicle, &step, vehicle_index, wheel_speed, wheel_speed, locked, s->e_brake);
		/* (f5e00: the parking brake, after 0.45 s idle and slow) */
		if (s->idle_ticks > 14 && (driver == NONE || h2vp_dot(&step.velocity, &step.velocity) < 3.0625f))
			brake = PIN((s->idle_ticks * H2VP_DT - 0.45f) * 1.25f, 0.0f, 1.0f);
		if (!grips)
			brake = 0.0f;
		s->last_wheel_speed = wheel_speed;
	}
	else if (d->type == H2VP_VEHICLE_TYPE_TANK)
	{
		/* (f1cd0: the engine by the stick's push, each side's points at
		the wheel speed less or more by the turn: a pivot turning in place) */
		real drive = h2vp_tank_controls(s, vehicle, &step), turn, torque, left, right;

		h2vp_wheel_log = getenv("HALO_H2_VEHICLE_LOG") && atoi(getenv("HALO_H2_VEHICLE_LOG")) >= 2 && (game_time_get() % 5) == 0 && driver != NONE;
		h2vp_wheel_log_length = 0;
		h2vp_wheel_log_text[0] = 0;
		h2vp_engine_update(s, (real)fabs(drive), h2vp_gear_choose(s, drive, brake_control, driver), brake_control);
		torque = s->gear == H2VP_NO_GEAR ? 0.0f : h2vp_gear_ratio(d, s->gear) * s->engine_angular_velocity;
		turn = s->turn / MAX(0.001f, d->slide[0]);
		left = (1.0f - (real)fabs(turn)) * torque - (real)fabs(torque) * turn;
		right = (1.0f - (real)fabs(turn)) * torque + (real)fabs(torque) * turn;
		throttle = drive;
		wheel_speed = (left + right) * 0.5f;
		grips = h2vp_friction(s, vehicle, &step, vehicle_index, left, right, FALSE, FALSE);
		if (s->idle_ticks > 14 && (driver == NONE || h2vp_dot(&step.velocity, &step.velocity) < 3.0625f))
			brake = PIN((s->idle_ticks * H2VP_DT - 0.45f) * 1.25f, 0.0f, 1.0f);
		if (!grips)
			brake = 0.0f;
		s->last_wheel_speed = wheel_speed;
	}
	else if (d->type == H2VP_VEHICLE_TYPE_FIGHTER)
	{
		h2vp_wheel_log = FALSE;
		h2vp_fighter(s, vehicle, &step, vehicle_index, brake_control, power, physics->gravity_scale);
	}
	else
	{
		h2vp_wheel_log = getenv("HALO_H2_VEHICLE_LOG") && atoi(getenv("HALO_H2_VEHICLE_LOG")) >= 2 && (game_time_get() % 5) == 0 && driver != NONE;
		h2vp_wheel_log_length = 0;
		h2vp_wheel_log_text[0] = 0;
		h2vp_scout(s, vehicle, &step, vehicle_index, step.steering, braking, power);
		h2vp_antigrav(s, vehicle, &step, vehicle_index, power);
	}

	/* (the step: the force model's force and torque, a mass's, as this
	engine's - a tick's, its mass's, about its centre - with the hull's
	mass points; the model's points' work the wheels' and pads') */
	{
		real scale = physics->mass / (real)(TICKS_PER_SECOND * TICKS_PER_SECOND);
		real_matrix3x3 frame, inertia, frame_transpose;
		real_vector3d alpha_torque;

		force = step.force;
		force.i *= scale;
		force.j *= scale;
		force.k *= scale;
		torque = step.torque;
		torque.i *= scale;
		torque.j *= scale;
		torque.k *= scale;
		/* (angular accelerations: the body's inertia's torque) */
		matrix3x3_from_forward_and_up(&frame, &vehicle->object.forward, &vehicle->object.up);
		matrix3x3_multiply(&frame, TAG_BLOCK_GET_ELEMENT(&physics->inertial_matrix, 0, real_matrix3x3), &inertia);
		frame_transpose = frame;
		matrix3x3_multiply(&inertia, matrix3x3_transpose(&frame_transpose, &frame_transpose), &inertia);
		matrix3x3_transform_vector(&inertia, &step.angular_acceleration, &alpha_torque);
		torque.i += alpha_torque.i / (real)(TICKS_PER_SECOND * TICKS_PER_SECOND);
		torque.j += alpha_torque.j / (real)(TICKS_PER_SECOND * TICKS_PER_SECOND);
		torque.k += alpha_torque.k / (real)(TICKS_PER_SECOND * TICKS_PER_SECOND);
		/* (its weight at Halo 2's centre of mass C, not the stand-in's P -
		the body turns about P: its weight's turn about it. About P a body
		whose mass is at C turns by its forces' moments less (C - P) x m a:
		supported (on its pads or wheels) that is the weight's moment, as
		here; falling free (a = g) it is none - a Ghost off a ledge kept its
		attitude in Halo 2 and pitched 25-37 degrees nose up here before it
		landed. HALO_H2_COM_FREEFALL=1: none while no pad or wheel touches (a
		vehicle with neither - the Scarab, turrets - as before); OFF by default
		for now: it moves 08b's FAST run onto a path whose Tartarus fight never
		completes a cycle (h2audit-runs NOTES), to be understood first) */
		if (d->has_center_of_mass && !d->weight_at_body &&
			(!HALO_ENV_ONCE("HALO_H2_COM_FREEFALL") || step.touching_friction > 0 || step.touching_antigrav > 0 ||
			(d->friction_count == 0 && d->antigrav_count == 0)))
		{
			float offset[3];
			real_point3d at;
			real_vector3d arm, weight, moment;

			offset[0] = d->center_of_mass[0];
			offset[1] = d->center_of_mass[1];
			offset[2] = d->center_of_mass[2];
			h2vp_point_world(vehicle, &step, offset, &at);
			vector_from_points3d(&step.center, &at, &arm);
			set_real_vector3d(&weight, 0.0f, 0.0f, -physics->mass * physics->gravity_scale * global_gravity);
			h2vp_cross(&arm, &weight, &moment);
			add_vectors3d(&torque, &moment, &torque);
		}
	}
	s->touching = step.touching_friction > 0 || step.touching_antigrav > 0;
	if (rigid_hull)
	{
		/* (the force on the mass of the rigid body; its weight, the world's) */
		h2_jolt_hull_push(vehicle_index, &step.force, &step.torque);
	}
	for (i = 0; i < physics->mass_points.count; i++)
	{
		struct mass_point_definition *m = TAG_BLOCK_GET_ELEMENT(&physics->mass_points, i, struct mass_point_definition);

		/* (the hull: every mass point but a jeep's wheels - its friction
		points roll where they are; a hovering vehicle's pads stand high
		over the ground on its anti-gravity points, touching only as a
		hull does) */
		if (m->powered_mass_point_index == NONE || (d->type != H2VP_VEHICLE_TYPE_JEEP && d->type != H2VP_VEHICLE_TYPE_TANK))
			passive |= 1UL << i;
		else if (s->touching)
			touching_mask |= 1UL << i;
	}
	if (rigid_hull)
	{
		real_vector3d extra_force, extra_torque;
		real per_mass = (real)(TICKS_PER_SECOND * TICKS_PER_SECOND) / physics->mass;

		physics_hull_state(vehicle_index, mass_points, passive, touching_mask, &s->last_velocity, &last_angular_velocity, &extra_force, &extra_torque);
		/* (at rest by the game's rule for a whole second, the wheels' spring
		and the solver settled: a hull perched for a tick is not) */
		if (TEST_FLAG(vehicle->object.flags, _object_at_rest_bit))
		{
			s->rest_ticks = (unsigned char)MIN(s->rest_ticks + 1, 0xFF);
			if (s->rest_ticks < TICKS_PER_SECOND)
				vehicle->object.flags &= ~FLAG(_object_at_rest_bit);
		}
		else
			s->rest_ticks = 0;
		h2vp_scale(&extra_force, per_mass);
		h2vp_scale(&extra_torque, per_mass);
		h2_jolt_hull_push(vehicle_index, &extra_force, &extra_torque);
		h2_collision_damage_tick(vehicle_index, d->has_collision ? d->collision : NULL, -1.0f);
	}
	else
	{
		real_vector3d before = vehicle->object.translational_velocity;

		h2_collision_damage_hull(vehicle_index, passive);
		physics_update_with_forces(vehicle_index, mass_points, passive, touching_mask, H2VP_HULL_FRICTION, &force, &torque);
		vector_from_points3d((real_point3d *)&before, (real_point3d *)&vehicle->object.translational_velocity, &s->physics_change);
		/* (Halo 2's collision damage over this tick's contacts, its speed
		the one before its physics: h2_collision_damage.c) */
		h2_collision_damage_tick(vehicle_index, d->has_collision ? d->collision : NULL, magnitude3d(&before) * (real)TICKS_PER_SECOND);
	}

	/* (the body's damping, its heaviest rigid body's: a second's fraction
	off its velocities; a rigid hull's is its solver's) */
	if (!rigid_hull)
	{
		real linear = (real)exp(-d->linear_damping * H2VP_DT), angular = (real)exp(-d->angular_damping * H2VP_DT);

		h2vp_scale(&vehicle->object.translational_velocity, linear);
		h2vp_scale(&vehicle->object.angular_velocity, angular);
	}

	/* (2056e0: the brake takes velocity off, at most 270 wu/s^2 level and
	18 rad/s^2) */
	if (brake > 0.0f)
	{
		real_vector3d linear = vehicle->object.translational_velocity, angular = vehicle->object.angular_velocity;
		real per_tick = 1.0f / (real)(TICKS_PER_SECOND * TICKS_PER_SECOND);

		linear.i *= -brake;
		linear.j *= -brake;
		linear.k = 0.0f;
		h2vp_limit(&linear, 270.0f * per_tick);
		angular.i *= -brake;
		angular.j *= -brake;
		angular.k *= -brake;
		h2vp_limit(&angular, 18.0f * per_tick);
		add_vectors3d(&vehicle->object.translational_velocity, &linear, &vehicle->object.translational_velocity);
		add_vectors3d(&vehicle->object.angular_velocity, &angular, &vehicle->object.angular_velocity);
	}

	/* (efde0: a flying vehicle with power is held inside the level's
	vehicle floor and ceiling, a soft spring of 6 a second, its vertical
	speed damped 0.3 a second) */
	if (d->type == H2VP_VEHICLE_TYPE_FIGHTER && power > 0.0f && global_structure_bsp_get())
	{
		real floor = global_structure_bsp_get()->vehicle_floor, ceiling = global_structure_bsp_get()->vehicle_ceiling;
		real z = vehicle->object.position.z, vz = vehicle->object.translational_velocity.k * TICKS_PER_SECOND;
		real damping = (real)pow(0.3, H2VP_DT);

		if (floor != 0.0f && z < floor)
			vehicle->object.translational_velocity.k = ((floor - z) * 6.0f * H2VP_DT + vz * damping) / TICKS_PER_SECOND;
		if (ceiling != 0.0f && z > ceiling)
			vehicle->object.translational_velocity.k = ((ceiling - z) * 6.0f * H2VP_DT + vz * damping) / TICKS_PER_SECOND;
	}

	if (d->type == H2VP_VEHICLE_TYPE_JEEP || d->type == H2VP_VEHICLE_TYPE_TANK)
	{
		boolean pushed = fabs(throttle) > 0.001f;
		unsigned long all_lifted = (1UL << d->friction_count) - 1;

		/* (f1c80: the engine's load; no upshift timing in the air. A tank's
		twelve powered points' reaction, a tick's, swung its engine between
		its limits each tick at this engine's one step a tick - Havok's
		solver steps finer -: eased, a quarter of the new each tick) */
		real body_mass = rigid_hull ? d->hull_mass : d->mass;

		if (d->type == H2VP_VEHICLE_TYPE_TANK)
			s->drive_reaction += (step.reaction * body_mass - s->drive_reaction) * 0.25f;
		else
			s->drive_reaction = step.reaction * body_mass;
		if (!step.touching_friction)
			s->upshift_ticks = 0;
		/* (f5ee0: the idle ticks) */
		if ((!pushed || h2vp_braking(s, brake_control)) && s->contact_flags != all_lifted &&
			(driver == NONE || h2vp_dot(&step.velocity, &step.velocity) <= 3.0625f))
		{
			s->idle_ticks = (unsigned char)MIN(s->idle_ticks + 1, 0x7E);
		}
		else
			s->idle_ticks = 0;
		/* (the stand-in's animation: its wheels spin at the wheels' speed, a
		tick's, its front ones turned the steering angle) */
		vehicle->vehicle.speed = wheel_speed / TICKS_PER_SECOND;
		vehicle->vehicle.turn = d->type == H2VP_VEHICLE_TYPE_TANK ? s->turn / TICKS_PER_SECOND : s->steering_angle;
	}
	else
	{
		/* (f3010: the ground - the pads touching, upright -, slewed; a
		fighter has none) */
		real grounded = d->antigrav_count > 0 ? PIN((real)step.touching_antigrav / (real)d->antigrav_count * MAX(step.up.k, 0.4f), 0.0f, 1.0f) : 0.0f;

		if (grounded - s->ground > 0.1f)
			s->ground += 0.1f;
		else if (grounded - s->ground < -0.1f)
			s->ground -= 0.1f;
		else
			s->ground = grounded;
		vehicle->vehicle.hover = s->ground;
		vehicle->vehicle.speed = s->speed / TICKS_PER_SECOND;
	}

	/* (f6bd0: ticks off the ground, touching anything - the model's points
	or the hull -, upside down and touching) */
	{
		boolean hull = FALSE;

		s->hull_points = 0;
		s->hull_mask = 0;
		for (i = 0; i < physics->mass_points.count; i++)
		{
			if (TEST_FLAG(passive, i) && TEST_FLAG(mass_points[i].flags, 1))
			{
				hull = TRUE;
				s->hull_points++;
				s->hull_mask |= 1UL << i;
			}
		}
		if (rigid_hull)
		{
			hull = h2_jolt_hull_touching(vehicle_index);
			s->hull_points = hull;
		}
		if (s->airborne_ticks < 0xFF)
			s->airborne_ticks++;
		if (step.touching_antigrav)
			s->airborne_ticks = 0;
		if (hull || step.touching_friction)
		{
			s->contact_ticks = (unsigned char)MIN(s->contact_ticks + 1, 0xFF);
			s->airborne_ticks = 0;
			if (vehicle->object.up.k < 0.0f)
				s->upside_down_ticks = (unsigned char)MIN(s->upside_down_ticks + 1, 0xFF);
			else
				s->upside_down_ticks = 0;
		}
		else
		{
			s->contact_ticks = 0;
			s->upside_down_ticks = 0;
		}
		s->touching = s->touching || hull;
	}
	s->gripping = step.gripping;
	s->sliding = step.sliding;
	s->last_velocity = vehicle->object.translational_velocity;
	if (h2vp_wheel_log)
	{
		real_vector3d const *f = &vehicle->object.forward, *u = &vehicle->object.up;

		platform_log("h2 wheels t%ld pitch %.1f roll %.1f hull %lx:%s", (long)game_time_get(), RADIANS_TO_DEGREES(asin(PIN(f->k, -1.0f, 1.0f))),
			RADIANS_TO_DEGREES(asin(PIN(step.left.k, -1.0f, 1.0f))), s->hull_mask, h2vp_wheel_log_text);
		(void)u;
	}

	if (getenv("HALO_H2_VEHICLE_LOG") && (game_time_get() % 5) == 0 && (driver != NONE || h2vp_length(&step.velocity) > 0.05f))
	{
		real_vector3d const *v = &vehicle->object.translational_velocity;

		platform_log("h2 vehicle %lx %s t%ld at %.2f %.2f %.2f speed %.2f fwd %.2f yaw %.1f up %.3f | thr %.2f str %.2f steer %.1f | gear %d w %.2f load %.2f react %.0f wheel %.2f | grip %ld slide %ld touch %ld hull %ld ag %ld g %.2f brake %.2f side %.2f afwd %.2f aup %.2f phys %.2f out %.2f",
			(unsigned long)vehicle_index, tag_get_name(vehicle->definition_index), (long)game_time_get(), vehicle->object.position.x,
			vehicle->object.position.y, vehicle->object.position.z, h2vp_length(v) * TICKS_PER_SECOND,
			h2vp_dot(v, &vehicle->object.forward) * TICKS_PER_SECOND, RADIANS_TO_DEGREES(vehicle->object.angular_velocity.k * TICKS_PER_SECOND),
			vehicle->object.up.k, throttle, vehicle->unit.throttle.j, RADIANS_TO_DEGREES(s->steering_angle), (int)s->gear,
			s->engine_angular_velocity, s->engine_load, s->drive_reaction, wheel_speed, step.gripping, step.sliding, step.touching_friction,
			s->hull_points, step.touching_antigrav, s->ground, brake, h2vp_dot(v, &step.left) * TICKS_PER_SECOND,
			h2vp_dot(&step.force, &step.forward), step.force.k, h2vp_dot(&s->physics_change, &step.forward) * 900.0f,
			h2vp_dot(&s->outside_change, &step.forward) * 900.0f);
	}
	return TRUE;
}

/* (vehicles.c, after the update) the vehicle's ground ticks, its model's
points' touching (none: the stand-in's) */
void h2_vehicle_physics_ground_ticks(
	long vehicle_index)
{
	struct vehicle_datum *vehicle = vehicle_datum_get(vehicle_index);
	struct h2vp_vehicle *s;

	if (!h2vp.halo2 || h2vp.scenario != global_scenario_index || (s = h2vp_vehicle_get(vehicle_index, NULL, FALSE)) == NULL)
		return;
	if (s->touching)
	{
		vehicle->vehicle.airborne_ticks = 0;
		if (vehicle->vehicle.on_ground_ticks < 0xFF)
			vehicle->vehicle.on_ground_ticks++;
	}
	else
	{
		vehicle->vehicle.on_ground_ticks = 0;
		if (vehicle->vehicle.airborne_ticks < 0xFF)
			vehicle->vehicle.airborne_ticks++;
	}
}

/* (bipeds.c) whether a rider of a Halo 2 force model's vehicle is thrown
out: the vehicle upside down and touching longer than its seat's turnover
time (bipeds.cpp dd360, 0.55 s if the seat has none); FALSE and *handled
FALSE: not such a vehicle (the stand-in's rule) */
boolean h2_vehicle_physics_rider_ejected(
	long vehicle_index,
	boolean *handled)
{
	struct h2vp_vehicle *s;
	long delay;

	*handled = FALSE;
	if (!h2vp.halo2 || h2vp.scenario != global_scenario_index || (s = h2vp_vehicle_get(vehicle_index, NULL, FALSE)) == NULL)
		return FALSE;
	*handled = TRUE;
	delay = (long)floor((s->definition->turnover_time > 0.0f ? s->definition->turnover_time : 0.55f) * TICKS_PER_SECOND + 0.5f);
	return s->upside_down_ticks > 0 && s->upside_down_ticks > delay;
}

/* (units.c, each unit's update) a vehicle whose aim turns only so far aims no
further from its forward, whoever sits in it, a player or an actor: its
desired and actual aiming turned back within its yaw and pitch limits, as
Halo 2's unit clamps its aim to its aim screen (0xcba50, 0xc7840,
halo2-decomp src/unit_object_type.cpp); the looking vector stays free */
void h2_vehicle_seat_aim_limits(
	long unit_index)
{
	struct vehicle_datum *vehicle = (struct vehicle_datum *)object_try_and_get(unit_index);
	struct h2vp_definition const *d;
	real_vector3d left, *vectors[2];
	int k;

	if (!vehicle || vehicle->object.type != _object_type_vehicle || (d = h2vp_definition_of(vehicle->definition_index)) == NULL ||
		!d->has_yaw_limits)
	{
		return;
	}
	h2vp_cross(&vehicle->object.up, &vehicle->object.forward, &left);
	vectors[0] = &vehicle->unit.desired_aiming_vector;
	vectors[1] = &vehicle->unit.aiming_vector;
	for (k = 0; k < 2; k++)
	{
		real_vector3d *a = vectors[k];
		real x = h2vp_dot(a, &vehicle->object.forward), y = h2vp_dot(a, &left), z = h2vp_dot(a, &vehicle->object.up);
		real length = (real)sqrt(x * x + y * y + z * z), yaw, pitch;

		if (length < 0.0001f)
			continue;
		yaw = (real)atan2(y, x);
		pitch = (real)atan2(z, (real)sqrt(x * x + y * y));
		if (yaw >= d->yaw_limits[0] && yaw <= d->yaw_limits[1] && pitch >= d->pitch_limits[0] && pitch <= d->pitch_limits[1])
			continue;
		yaw = PIN(yaw, d->yaw_limits[0], d->yaw_limits[1]);
		pitch = PIN(pitch, d->pitch_limits[0], d->pitch_limits[1]);
		x = length * (real)cos(yaw) * (real)cos(pitch);
		y = length * (real)sin(yaw) * (real)cos(pitch);
		z = length * (real)sin(pitch);
		a->i = vehicle->object.forward.i * x + left.i * y + vehicle->object.up.i * z;
		a->j = vehicle->object.forward.j * x + left.j * y + vehicle->object.up.j * z;
		a->k = vehicle->object.forward.k * x + left.k * y + vehicle->object.up.k * z;
	}
}

/* (player_control.c, as a player's view turns) the rider of a vehicle whose aim
turns only so far looks no further: Halo 2's turret stops his view at its
limits, the tag test on the anti-personnel turret stops it at 90 degrees either
side and at 50 up and down, the mouse moving on. */
void h2_vehicle_seat_view_limits(
	long unit_index,
	real_euler_angles2d *angles)
{
	struct unit_datum *unit = (struct unit_datum *)object_try_and_get(unit_index);
	struct vehicle_datum *vehicle;
	struct h2vp_definition const *d;
	real_vector3d left, view;
	real x, y, z, yaw, pitch, cosine;

	if (!unit || unit->object.parent_object_index == NONE ||
		(vehicle = (struct vehicle_datum *)object_try_and_get(unit->object.parent_object_index)) == NULL ||
		vehicle->object.type != _object_type_vehicle || (d = h2vp_definition_of(vehicle->definition_index)) == NULL || !d->has_yaw_limits)
	{
		return;
	}
	h2vp_cross(&vehicle->object.up, &vehicle->object.forward, &left);
	cosine = (real)cos(angles->pitch);
	view.i = (real)cos(angles->yaw) * cosine;
	view.j = (real)sin(angles->yaw) * cosine;
	view.k = (real)sin(angles->pitch);
	x = h2vp_dot(&view, &vehicle->object.forward);
	y = h2vp_dot(&view, &left);
	z = h2vp_dot(&view, &vehicle->object.up);
	yaw = (real)atan2(y, x);
	pitch = (real)atan2(z, (real)sqrt(x * x + y * y));
	if (yaw >= d->yaw_limits[0] && yaw <= d->yaw_limits[1] && pitch >= d->pitch_limits[0] && pitch <= d->pitch_limits[1])
		return;
	yaw = PIN(yaw, d->yaw_limits[0], d->yaw_limits[1]);
	pitch = PIN(pitch, d->pitch_limits[0], d->pitch_limits[1]);
	x = (real)cos(yaw) * (real)cos(pitch);
	y = (real)sin(yaw) * (real)cos(pitch);
	z = (real)sin(pitch);
	view.i = vehicle->object.forward.i * x + left.i * y + vehicle->object.up.i * z;
	view.j = vehicle->object.forward.j * x + left.j * y + vehicle->object.up.j * z;
	view.k = vehicle->object.forward.k * x + left.k * y + vehicle->object.up.k * z;
	angles->yaw = (real)atan2(view.j, view.i);
	if (angles->yaw < 0.0f)
		angles->yaw += 6.2831855f;
	angles->pitch = (real)asin(PIN(view.k, -1.0f, 1.0f));
}

/* (h2_vehicle_wrecks.c, as a vehicle dies, on the machine that runs the
game) its death's push: Halo 2's destroyed response's transition effect's
acceleration, as Halo 2's effects give it their object (function_179fb0):
the location's direction (the marker's forward) turned by an angle between
the acceleration's inner and outer cone angles, as the tag has them, about
an axis square to it picked at random (random_vector_in_cone: the angle
goes to sin and cos as it is - the Warthog's 0 to 180 and the Ghost's 120
to 180 turn it every way), times the acceleration and the vehicle's
acceleration scale (the Warthog 2 x 0.75, the Ghost 3 x 1, the Scorpion
-30 x 0.1), taken as that velocity change (a second's) at the marker - the
body thrown and turned about its centre of mass by where the marker is.
"it didn't jump around in the air the way it does in Halo 2", the owner's
play tests (Oct 25, Oct 27): the push was along the marker alone, the
Ghost's backward and flat. HALO_H2_DEATH_PUSH_NO_SCOUT leaves the Ghost's
out. FALSE: none (HALO_H2_NO_DEATH_PUSH) */
boolean h2_vehicle_physics_death_push(
	long vehicle_index)
{
	struct vehicle_datum *vehicle = (struct vehicle_datum *)object_try_and_get(vehicle_index);
	struct object_definition *object_definition;
	struct h2vp_definition const *d;
	real_vector3d left, direction, change;
	real_point3d at;
	real_point3d com = { 0.0f, 0.0f, 0.0f };
	float const *place, *facing;
	real x, y, z;

	if (!vehicle || vehicle->object.type != _object_type_vehicle || getenv("HALO_H2_NO_DEATH_PUSH") ||
		(d = h2vp_definition_of(vehicle->definition_index)) == NULL || !d->has_death_push ||
		(d->type == H2VP_VEHICLE_TYPE_SCOUT && getenv("HALO_H2_DEATH_PUSH_NO_SCOUT")))
	{
		return FALSE;
	}
	object_definition = object_definition_get(vehicle->definition_index);
	if (object_definition->object.physics.index != NONE)
		com = physics_definition_get(object_definition->object.physics.index)->center_of_mass;
	h2vp_cross(&vehicle->object.up, &vehicle->object.forward, &left);
	place = d->death_push_position;
	facing = d->death_push_direction;
	x = place[0] - com.x;
	y = place[1] - com.y;
	z = place[2] - com.z;
	at.x = vehicle->object.position.x + vehicle->object.forward.i * x + left.i * y + vehicle->object.up.i * z;
	at.y = vehicle->object.position.y + vehicle->object.forward.j * x + left.j * y + vehicle->object.up.j * z;
	at.z = vehicle->object.position.z + vehicle->object.forward.k * x + left.k * y + vehicle->object.up.k * z;
	direction.i = vehicle->object.forward.i * facing[0] + left.i * facing[1] + vehicle->object.up.i * facing[2];
	direction.j = vehicle->object.forward.j * facing[0] + left.j * facing[1] + vehicle->object.up.j * facing[2];
	direction.k = vehicle->object.forward.k * facing[0] + left.k * facing[1] + vehicle->object.up.k * facing[2];
	if (h2vp_normalize(&direction) <= 0.0f)
		return FALSE;
	/* (random_vector_in_cone: a random unit vector, the axis square to
	the direction and it, the direction turned about it by the angle) */
	{
		real_vector3d random, axis, across;
		real z = real_random_range(-1.0f, 1.0f), around = real_random_range(0.0f, 2.0f * (real)M_PI), flat = (real)sqrt(1.0f - z * z);
		real angle, sine, cosine;

		random.i = flat * (real)cos(around);
		random.j = flat * (real)sin(around);
		random.k = z;
		h2vp_cross(&direction, &random, &axis);
		if (h2vp_normalize(&axis) > 0.0001f)
		{
			angle = real_random_range(d->death_push_inner, d->death_push_cone);
			sine = (real)sin(angle);
			cosine = (real)cos(angle);
			h2vp_cross(&axis, &direction, &across);
			direction.i = direction.i * cosine + across.i * sine;
			direction.j = direction.j * cosine + across.j * sine;
			direction.k = direction.k * cosine + across.k * sine;
			h2vp_normalize(&direction);
		}
	}
	change = direction;
	h2vp_scale(&change, d->death_push_acceleration * object_definition->object.acceleration_scale / (real)TICKS_PER_SECOND);
	h2vp_point_impulse(vehicle_index, &at, &change);
	if (getenv("HALO_H2_WRECK_TRACE"))
		platform_log("h2 wrecks: %08lx death push %.2f x %.2f wu/s along %.2f %.2f %.2f at %.2f %.2f %.2f: velocity %.2f %.2f %.2f, spin %.1f deg/s",
			(unsigned long)vehicle_index, d->death_push_acceleration, object_definition->object.acceleration_scale, direction.i, direction.j, direction.k, at.x, at.y, at.z,
			vehicle->object.translational_velocity.i * TICKS_PER_SECOND, vehicle->object.translational_velocity.j * TICKS_PER_SECOND,
			vehicle->object.translational_velocity.k * TICKS_PER_SECOND, RADIANS_TO_DEGREES(h2vp_length(&vehicle->object.angular_velocity) * TICKS_PER_SECOND));
	return TRUE;
}

/* (damage.c, a hit on a Halo 2 level's vehicle) whether the collision
node it struck is one of its Halo 2 child object's - the Scorpion's cannon,
the Warthog's turret - and its damage info does not take body damage for
its children (flag 1): Halo 2 deals such a hit to the child alone
(damage.cpp function_d7b80: a parent's body only for a child's hit when it
takes body damage for children; the turrets cannot die from damage), and
passes it on to the child's riders alone (function_db210) */
boolean h2_vehicle_physics_child_part(
	long vehicle_index,
	short node_index)
{
	struct object_datum *vehicle = object_try_and_get(vehicle_index);
	struct h2vp_definition const *d;
	struct object_definition *object_definition;
	struct collision_model *collision_model;
	char const *name;
	boolean child = FALSE;
	long i;

	if (!vehicle || vehicle->object.type != _object_type_vehicle || node_index < 0 ||
		(d = h2vp_definition_of(vehicle->definition_index)) == NULL)
	{
		return FALSE;
	}
	object_definition = object_definition_get(vehicle->definition_index);
	if (object_definition->object.collision_model.index == NONE)
		return FALSE;
	collision_model = collision_model_definition_get(object_definition->object.collision_model.index);
	if (node_index >= collision_model->nodes.count)
		return FALSE;
	name = TAG_BLOCK_GET_ELEMENT(&collision_model->nodes, node_index, struct collision_node)->name;
	for (i = 0; i < d->child_node_count && i < H2VP_MAXIMUM_CHILD_NODES && !child; i++)
		child = !strcmp(d->child_nodes[i], name);
	/* (HALO_H2_DAMAGE_LOG: each direct hit on a vehicle and whose it is) */
	if (getenv("HALO_H2_DAMAGE_LOG"))
		platform_log("h2 vehicle hit: %s node %d '%s': %s", tag_get_name(vehicle->definition_index), (int)node_index, name,
			child && !TEST_FLAG(d->damage_info_flags, 1) ? "its child object's (the turret's)" : "its body's");
	return child && !TEST_FLAG(d->damage_info_flags, 1) && !getenv("HALO_H2_TURRET_HITS_HURT");
}

/* (damage.c) an impulse (a velocity, a tick's) on a Halo 2 force model's
vehicle (f7ed0): its gravity scale's, and a jeep with more than half its
wheels not gripping takes 45% of the part along its floor of a push into
the ground; given to its body at the damage's origin (its velocity and its
turn, the body's inertia's: d9640's point impulse). FALSE: not such a
vehicle (the stand-in's vehicle_accelerate) */
boolean h2_vehicle_physics_impulse(
	long vehicle_index,
	real_vector3d *impulse,
	real_point3d const *origin)
{
	struct vehicle_datum *vehicle = (struct vehicle_datum *)object_try_and_get(vehicle_index);
	struct h2vp_definition const *d;
	struct h2vp_vehicle *s;

	if (!vehicle || vehicle->object.type != _object_type_vehicle || (d = h2vp_definition_of(vehicle->definition_index)) == NULL)
		return FALSE;
	/* (as it is while it has no power: no driver) */
	if (vehicle->unit.seat_power[0] != 0.0f)
	{
		h2vp_scale(impulse, d->gravity_scale);
		if (d->type == H2VP_VEHICLE_TYPE_JEEP && d->friction_count > 0 && (s = h2vp_vehicle_get(vehicle_index, NULL, FALSE)) != NULL)
		{
			long empty = 0, i;

			for (i = 0; i < d->friction_count; i++)
			{
				if (TEST_FLAG(s->contact_flags, i))
					empty++;
			}
			if (empty > d->friction_count >> 1)
			{
				real dot = h2vp_dot(&vehicle->object.up, impulse);

				if (dot < 0.0f)
				{
					real_vector3d into = vehicle->object.up;

					h2vp_scale(&into, dot);
					impulse->i = (impulse->i - into.i) * 0.45f + into.i;
					impulse->j = (impulse->j - into.j) * 0.45f + into.j;
					impulse->k = (impulse->k - into.k) * 0.45f + into.k;
				}
			}
		}
	}
	h2vp_point_impulse(vehicle_index, origin, impulse);
	return TRUE;
}

/* (units.c) a rider getting in (entering TRUE: or boarding) or out of a
Halo 2 force model's vehicle pushes it down at the rider (e5300): a
velocity of the rider's mass over the vehicle's x dt x gravity x the
vehicle's seat entrance or exit scale, at the rider's place */
void h2_vehicle_physics_rider_push(
	long vehicle_index,
	real_point3d const *at,
	boolean entering)
{
	struct object_datum *object = object_try_and_get(vehicle_index);
	struct h2vp_definition const *d;
	real factor, scale;
	real_vector3d change;

	if (!object || object->object.type != _object_type_vehicle || (d = h2vp_definition_of(object->definition_index)) == NULL)
		return;
	factor = entering ? d->seat_entrance_scale : d->seat_exit_scale;
	if (factor <= 0.0f || d->rider_mass <= 0.0f)
		return;
	scale = d->rider_mass / d->mass * H2VP_DT * global_gravity * TICKS_PER_SECOND * TICKS_PER_SECOND * factor;
	set_real_vector3d(&change, 0.0f, 0.0f, -scale / TICKS_PER_SECOND);
	h2vp_point_impulse(vehicle_index, at, &change);
}
