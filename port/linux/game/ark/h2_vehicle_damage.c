/*
H2_VEHICLE_DAMAGE.C

port: Halo 2's vehicle model destruction on a Halo 2 level. A Halo 2
vehicle's model (its hlmt) has damage sections - the Warthog's hull, each
wheel, its windshield and bumper; the Ghost's hull, seat and wings - each a
share of the vehicle's vitality; as a section's health falls past its
responses' thresholds the section's region of the render model takes another
permutation (dented, burnt, torn; a bumper gone) and a piece of it is thrown
off (Halo 2's garbage: a hubcap, a fender, the bumper, a wing's shell), and
as the vehicle dies its main section's responses leave the wreck (the
Warthog's burnt hull and bare wheel rims, the Ghost's torn seat).

This engine's vehicle has its Halo 2 regions and their permutations (the
converter's, h2_vehicles.c: the Warthog's sides one region, an object's 8),
each region's middle and radius in its unused fields; a damage to a vehicle's
body (damage.c's object_damage_body) comes here with the point it was dealt
at: the section whose region's middle is nearest takes it all, those within
0.4 units (1.2 m) farther less of it; a section's health crossing a response's
threshold sets that response's region's permutation (only ever a worse one)
and throws its piece - this engine's garbage (h2\vehicles\debris\<name>,
which goes after 10 to 20 seconds; at most H2VD_MAXIMUM_DEBRIS at once, the
oldest removed). The sections and responses are Halo 2's (its hlmts'
damage info, zanzibar's Warthog and Ghost), written out here.
HALO_H2_NO_VEHICLE_DAMAGE: none of it.
*/

#include "cseries.h"
#include "render_epoch.h" /* HALO_ENV_ONCE (perf/grunt-tick-2: the switches read again only when the settings change) */
#include "game/game.h"
#include "models/model_definitions.h"
#include "objects/damage.h"
#include "objects/object_definitions.h"
#include "objects/objects.h"
#include "scenario/scenario.h"
#include "tag_files/tag_groups.h"
#include "text/text_group.h"
#include "units/units.h"
#include "units/unit_definitions.h"
#include "game/game_globals.h"
#include "items/projectiles.h"
#include "models/model_animation_definitions.h"

#include "h2_particle_styles.h"
#include "h2_vehicle_damage.h"
#include "effects/effects.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	H2VD_MAXIMUM_VEHICLES = 64,
	H2VD_MAXIMUM_SECTIONS = 8,
	H2VD_MAXIMUM_RESPONSES = 6,
	H2VD_MAXIMUM_DEBRIS = 16,          /* (the room; at most h2vd_debris_cap() at once) */
	H2VD_DEATH = H2VD_MAXIMUM_SECTIONS,     /* (fired: the main section's responses, the vehicle's death) */
	H2VD_MAXIMUM_DEFINITIONS = 32,
	H2VD_MAXIMUM_PENDING = 32,          /* delayed responses waiting */
	H2VD_LOOP_PERIOD = 60,              /* ticks between a damaged region's looping effects (each emits for about its event's 2 s) */
	H2VD_LOOPS_A_TICK = 6,              /* looping effects begun in a tick, at most */
	H2VD_LOOPING_REGIONS = 4,           /* a vehicle's regions whose looping effects play, at most (the most damaged) */
	H2VD_EFFECTS_A_TICK = 6             /* a vehicle's transition effects in a tick, at most */
};

/* (world units: 3 m) a damage's share of the sections farther than the
nearest: none past this much farther */
#define H2VD_FALLOFF 0.4f

/* ---------- structures */

/* a response: as its section's health falls to threshold, its region's
permutation (by name; NULL none) and a piece thrown from that region (NULL
none) */
struct h2vd_response
{
	float threshold;
	char const *region;
	char const *permutation;
	char const *debris;
};

struct h2vd_section
{
	char const *region;         /* where it is: that region's middle */
	float vitality;             /* its share of the vehicle's */
	struct h2vd_response responses[H2VD_MAXIMUM_RESPONSES];
};

struct h2vd_kind
{
	char const *region;         /* a region of its model's that says it is one */
	struct h2vd_section sections[H2VD_MAXIMUM_SECTIONS];
	struct h2vd_response death[H2VD_MAXIMUM_RESPONSES + 4];
};

/* ---------- globals */

/* (Halo 2's hlmts: objects\vehicles\warthog\warthog's - its tire sections
the wheels', its sides' sections the hull's: the converter's regions - and
objects\vehicles\ghost\ghost's; the permutations their states' names) */
static struct h2vd_kind const h2vd_kinds[] =
{
	{
		"lf wheel",
		{
			{ "hull", 0.5f, {
				{ 0.25f, "hull", "medium", NULL }, { 0.25f, "sides", "medium", NULL },
				{ 0.0f, "hull", "major", "sailpanel" }, { 0.0f, "sides", "major", "winch" } } },
			{ "windshield", 0.05f, { { 0.5f, "windshield", "minor", NULL }, { 0.0f, "windshield", "medium", NULL } } },
			{ "lf wheel", 0.125f, { { 0.75f, "lf wheel", "minor", "hubcap" }, { 0.25f, "lf wheel", "medium", NULL },
				{ 0.0f, "lf wheel", "major", "lf fender" } } },
			{ "rf wheel", 0.125f, { { 0.75f, "rf wheel", "minor", "hubcap" }, { 0.5f, "rf wheel", "medium", NULL },
				{ 0.25f, "rf wheel", "major", "rf fender" } } },
			{ "lb wheel", 0.125f, { { 0.75f, "lb wheel", "minor", "hubcap" }, { 0.5f, "lb wheel", "medium", NULL },
				{ 0.25f, "lb wheel", "major", "lb fender" } } },
			{ "rb wheel", 0.125f, { { 0.75f, "rb wheel", "minor", "hubcap" }, { 0.5f, "rb wheel", "medium", NULL },
				{ 0.25f, "rb wheel", "major", "rb fender" } } },
			{ "bumper", 0.05f, { { 0.75f, "bumper", "medium", NULL }, { 0.0f, "bumper", "gone", "bumper" } } },
		},
		{
			{ 0.0f, "windshield", "medium", NULL }, { 0.0f, "hull", "destroyed", NULL }, { 0.0f, "sides", "major", NULL },
			/* (Halo 2's main section takes the wheels to "destroyed" - bare
			rims - and each wheel's tires_destroyed effect throws its tire,
			Halo 2's garbage tire: the owner's playtest, "the tires don't
			fall off the warthog like they are supposed to") */
			{ 0.0f, "lf wheel", "destroyed", "tire" }, { 0.0f, "rb wheel", "destroyed", "tire" },
			{ 0.0f, "rf wheel", "destroyed", "tire" }, { 0.0f, "lb wheel", "destroyed", "tire" },
		},
	},
	{
		"l wing",
		{
			{ "hull", 0.5f, {
				{ 0.25f, "hull", "medium", NULL }, { 0.25f, "l gun", "medium", NULL }, { 0.25f, "r gun", "medium", NULL },
				{ 0.0f, "hull", "major", "hull shell" }, { 0.0f, "l gun", "major", NULL }, { 0.0f, "r gun", "major", NULL } } },
			{ "seat", 0.5f, { { 0.25f, "seat", "medium", NULL }, { 0.0f, "seat", "major", NULL } } },
			{ "l wing", 0.25f, { { 0.75f, "l wing", "medium", NULL }, { 0.25f, "l wing", "major", "l wing shell" },
				{ 0.0f, "l wing", "destroyed", NULL } } },
			{ "r wing", 0.25f, { { 0.75f, "r wing", "medium", NULL }, { 0.25f, "r wing", "major", "r wing shell" },
				{ 0.0f, "r wing", "destroyed", NULL } } },
		},
		{
			{ 0.0f, "seat", "destroyed", "seat" }, { 0.0f, "l wing", "medium", NULL }, { 0.0f, "r wing", "medium", NULL },
			{ 0.0f, "hull", "medium", NULL },
		},
	},
};

/* a permutation's damage, for "only ever a worse one" */
static char const *const h2vd_order[] = { "base", "minor", "medium", "major", "destroyed", "gone", NULL };

static struct
{
	struct
	{
		long vehicle;
		short kind;
		float health[H2VD_MAXIMUM_SECTIONS];
		unsigned long fired[H2VD_MAXIMUM_SECTIONS + 1];     /* each section's responses fired (bit each), the death's */
		/* (a vehicle with Halo 2's damage sections, read from its tags) */
		short definition;               /* h2vd_definitions' (NONE: one of h2vd_kinds) */
		boolean died, special_death;
		float definition_health[H2VD_DEFINITION_SECTIONS];
		unsigned short definition_fired[H2VD_DEFINITION_SECTIONS];
		unsigned char region_states[MAXIMUM_REGIONS_PER_OBJECT];    /* (our regions') damage state 0..4 */
		long next_loop;                 /* game time its looping effects play again */
		long killer_player;             /* (its last damage's player: its blast's) */
		long effects_tick;              /* game time its transition effects were counted for */
		short effects_this_tick;
	} vehicles[H2VD_MAXIMUM_VEHICLES];
	struct
	{
		long vehicle;                   /* NONE: free */
		short slot, section, response;
		long at;                        /* game time */
	} pending[H2VD_MAXIMUM_PENDING];
	long loop_tick;
	short loops_this_tick;
	long debris[H2VD_MAXIMUM_DEBRIS];   /* (oldest first) */
	struct
	{
		long object;
		long born;                  /* game time */
		short bounces;
		real last_k;                /* its last tick's vertical speed */
		real launch;                /* the speed it was thrown at (a tick's) */
		real_point3d from;
	} motion[H2VD_MAXIMUM_DEBRIS];      /* (each thrown piece's settling: h2_vehicle_damage_debris_update) */
	long scenario;
	unsigned long generation;
	boolean halo2;
	unsigned long seed;
} h2vd = { { { 0 } } };

/* the converted vehicles' damage (the builder's, h2_vehicle_damage_define) */
static struct h2vd_definition *h2vd_definitions[H2VD_MAXIMUM_DEFINITIONS];
static long h2vd_definition_count;
static short h2vd_definition_from_level(long definition_index);

/* Halo 2's effects this engine plays elsewhere (the collision model's
damaged and depleted effects, h2_vehicle_wrecks.c's burning and the Ghost's
explosion and warm-up): not again here */
static char const *const h2vd_effects_elsewhere[] =
{
	"effects\\objects\\vehicles\\warthog\\trans_main_destroyed",
	"effects\\objects\\vehicles\\warthog\\perm_medium_hull",
	"effects\\objects\\vehicles\\warthog\\looping_engine_smoke",
	"effects\\objects\\vehicles\\warthog\\looping_universal_smoke",
	"effects\\objects\\vehicles\\ghost\\hull_destroyed_damage",
	"effects\\objects\\vehicles\\ghost\\hull_destroyed_initial_damage",
	"effects\\objects\\vehicles\\ghost\\perm_hull_destroyed_damage",
	"effects\\objects\\vehicles\\ghost\\perm_hull_med_damage",
	NULL
};

/* ---------- private code */

/* (render_epoch.c) counted up each time the game state is replaced: a
new map, a revert, a network game's start - the objects' datum indices are
used again for new objects */
extern unsigned long halo_map_generation;

/* debris at once: a dead Warthog throws its hubcaps, fenders, bumper, hood
and four tires (the Vita's capped effects tier fewer, the oldest going) */
static long h2vd_debris_cap(void)
{
	return h2_particle_style_tier() == H2_PARTICLE_STYLE_TIER_CAPPED ? 8 : H2VD_MAXIMUM_DEBRIS;
}

static void h2vd_level(void)
{
	char const *name;
	long i;

	if (h2vd.scenario == global_scenario_index && h2vd.generation == halo_map_generation)
		return;
	h2vd.scenario = global_scenario_index;
	h2vd.generation = halo_map_generation;
	name = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : "";
	h2vd.halo2 = name && !strncmp(name, "h2\\", 3) && !HALO_ENV_ONCE("HALO_H2_NO_VEHICLE_DAMAGE");
	for (i = 0; i < H2VD_MAXIMUM_VEHICLES; i++)
		h2vd.vehicles[i].vehicle = NONE;
	for (i = 0; i < H2VD_MAXIMUM_DEBRIS; i++)
		h2vd.debris[i] = h2vd.motion[i].object = NONE;
	for (i = 0; i < H2VD_MAXIMUM_PENDING; i++)
		h2vd.pending[i].vehicle = NONE;
}

static struct model *h2vd_model(struct object_datum *object)
{
	struct object_definition *definition = object_definition_get(object->definition_index);

	return definition->object.model.index != NONE ? model_definition_get(definition->object.model.index) : NULL;
}

/* the model's region of that name; NONE */
static short h2vd_region(struct model *model, char const *name)
{
	short i;

	for (i = 0; i < MIN(model->regions.count, MAXIMUM_REGIONS_PER_OBJECT); i++)
	{
		if (!strcmp(TAG_BLOCK_GET_ELEMENT(&model->regions, i, struct model_region)->name, name))
			return i;
	}
	return NONE;
}

/* a region's middle and radius (the converter's, its unused fields) */
static void h2vd_region_sphere(struct model *model, short region, real_point3d *center, real *radius)
{
	struct model_region *r = TAG_BLOCK_GET_ELEMENT(&model->regions, region, struct model_region);
	real const *fields = (real const *)r->unused;

	center->x = fields[0];
	center->y = fields[1];
	center->z = fields[2];
	*radius = fields[3];
}

static long h2vd_rank(char const *permutation)
{
	long i;

	for (i = 0; h2vd_order[i] && strcmp(h2vd_order[i], permutation); i++)
		;
	return i;
}

/* the object's point in its model's space */
static void h2vd_local(struct object_datum *object, real_point3d const *world, real_point3d *local)
{
	real_vector3d const *forward = &object->object.forward, *up = &object->object.up;
	real_vector3d left, d;

	cross_product3d(up, forward, &left);
	d.i = world->x - object->object.position.x;
	d.j = world->y - object->object.position.y;
	d.k = world->z - object->object.position.z;
	local->x = dot_product3d(&d, forward);
	local->y = dot_product3d(&d, &left);
	local->z = dot_product3d(&d, up);
}

static void h2vd_world(struct object_datum *object, real_point3d const *local, real_point3d *world)
{
	real_vector3d const *forward = &object->object.forward, *up = &object->object.up;
	real_vector3d left;

	cross_product3d(up, forward, &left);
	world->x = object->object.position.x + forward->i*local->x + left.i*local->y + up->i*local->z;
	world->y = object->object.position.y + forward->j*local->x + left.j*local->y + up->j*local->z;
	world->z = object->object.position.z + forward->k*local->x + left.k*local->y + up->k*local->z;
}

/* 0..1, the game's own (the same on every machine that runs the game) */
static real h2vd_random(void)
{
	h2vd.seed = h2vd.seed * 1664525UL + 1013904223UL;
	return (real)((h2vd.seed >> 8) & 0xFFFF) / 65535.0f;
}

/* a unit vector, the game's random (h2vd_random) */
static void h2vd_random_direction(real_vector3d *direction)
{
	real z = 2.0f * h2vd_random() - 1.0f, around = 6.2831853f * h2vd_random(), flat = sqrtf(MAX(0.0f, 1.0f - z * z));

	direction->i = flat * (real)cos(around);
	direction->j = flat * (real)sin(around);
	direction->k = z;
}

/* a piece thrown off the vehicle. Halo 2's (piece, the part of the
response's transition effect that throws it: effect_part_create's 'obje'
case, unknown_175bd0.cpp 0x17a8a0 with 0x177d10 and 0x177f60) at its
location's marker, facing the marker's way, its speed between the part's
velocity bounds along the marker's forward turned about a random axis by
up to its cone angle, the vehicle's velocity added, spinning about a random
axis at between its angular velocity bounds; without its marker (or a
piece of the written-out kinds) from its region's middle, up and out,
tumbling */
static void h2vd_debris(long vehicle_index, struct object_datum *object, struct model *model, short region, char const *name,
	struct h2vd_definition_debris const *piece)
{
	char path[96];
	long definition, made, i;
	struct object_placement_data data;
	real_point3d local, from;
	real radius, out, speed;
	struct object_marker marker;
	boolean at_marker;

	snprintf(path, sizeof(path), "h2\\vehicles\\debris\\%s", name);
	definition = tag_loaded('garb', path);
	if (definition == NONE)
		return;
	object_placement_data_new(&data, definition, NONE);
	at_marker = piece && piece->marker[0] && !HALO_ENV_ONCE("HALO_H2_DEBRIS_OLD") &&
		object_get_marker_by_name(vehicle_index, piece->marker, &marker, 1) > 0;
	if (at_marker)
	{
		real_vector3d direction = marker.matrix.forward, axis;
		real angle = piece->cone * h2vd_random();

		speed = (piece->velocity[0] + (piece->velocity[1] - piece->velocity[0]) * h2vd_random()) / 30.0f;
		if (angle != 0.0f)
		{
			h2vd_random_direction(&axis);
			rotate_vector_about_axis(&direction, &axis, (real)sin(angle), (real)cos(angle));
		}
		data.position = marker.matrix.position;
		data.forward = marker.matrix.forward;
		data.up = marker.matrix.up;
		data.translational_velocity.i = direction.i * speed + object->object.translational_velocity.i;
		data.translational_velocity.j = direction.j * speed + object->object.translational_velocity.j;
		data.translational_velocity.k = direction.k * speed + object->object.translational_velocity.k;
		{
			real spin = (piece->angular_velocity[0] + (piece->angular_velocity[1] - piece->angular_velocity[0]) * h2vd_random()) / 30.0f;

			h2vd_random_direction(&data.angular_velocity);
			data.angular_velocity.i *= spin;
			data.angular_velocity.j *= spin;
			data.angular_velocity.k *= spin;
		}
	}
	else
	{
		h2vd_region_sphere(model, region, &local, &radius);
		h2vd_world(object, &local, &from);
		data.position = from;
		data.position.z += 0.08f;
		out = sqrtf(local.x * local.x + local.y * local.y);
		/* (a tick's: about 2 to 4 m a second out, 6 up) */
		speed = (0.6f + 0.6f*h2vd_random()) / 30.0f;
		{
			real_point3d direction = { out > 0.05f ? local.x / out : h2vd_random() - 0.5f, out > 0.05f ? local.y / out : h2vd_random() - 0.5f, 0.0f };
			real_point3d world_direction, origin = { 0.0f, 0.0f, 0.0f }, world_origin;

			h2vd_world(object, &direction, &world_direction);
			h2vd_world(object, &origin, &world_origin);
			data.translational_velocity.i = (world_direction.x - world_origin.x) * speed + object->object.translational_velocity.i;
			data.translational_velocity.j = (world_direction.y - world_origin.y) * speed + object->object.translational_velocity.j;
			data.translational_velocity.k = (1.6f + 0.8f*h2vd_random()) / 30.0f + object->object.translational_velocity.k;
		}
		data.forward = object->object.forward;
		data.up = object->object.up;
		data.angular_velocity.i = 0.3f*(h2vd_random() - 0.5f);
		data.angular_velocity.j = 0.3f*(h2vd_random() - 0.5f);
		data.angular_velocity.k = 0.3f*(h2vd_random() - 0.5f);
	}
	/* (the oldest gone when there are as many as there may be) */
	for (i = 0; i < h2vd_debris_cap() && h2vd.debris[i] != NONE && object_try_and_get(h2vd.debris[i]); i++)
		;
	if (i == h2vd_debris_cap())
	{
		object_delete(h2vd.debris[0]);
		memmove(&h2vd.debris[0], &h2vd.debris[1], sizeof(h2vd.debris[0]) * (h2vd_debris_cap() - 1));
		i = h2vd_debris_cap() - 1;
	}
	else
	{
		long k, n = 0;

		/* (the ones gone already out of the list) */
		for (k = 0; k < H2VD_MAXIMUM_DEBRIS; k++)
		{
			if (h2vd.debris[k] != NONE && object_try_and_get(h2vd.debris[k]))
				h2vd.debris[n++] = h2vd.debris[k];
		}
		for (k = n; k < H2VD_MAXIMUM_DEBRIS; k++)
			h2vd.debris[k] = NONE;
		i = n;
	}
	made = object_new(&data);
	h2vd.debris[i] = made;
	/* (its settling followed) */
	{
		long m, free_motion = NONE;

		for (m = 0; m < H2VD_MAXIMUM_DEBRIS; m++)
		{
			if (h2vd.motion[m].object == NONE || !object_try_and_get(h2vd.motion[m].object))
			{
				free_motion = m;
				break;
			}
		}
		if (free_motion != NONE && made != NONE)
		{
			h2vd.motion[free_motion].object = made;
			h2vd.motion[free_motion].born = game_time_get();
			h2vd.motion[free_motion].bounces = 0;
			h2vd.motion[free_motion].last_k = data.translational_velocity.k;
			h2vd.motion[free_motion].from = data.position;
			h2vd.motion[free_motion].launch = magnitude3d(&data.translational_velocity);
		}
	}
	if (HALO_ENV_ONCE("HALO_H2_DEBUG") || HALO_ENV_ONCE("HALO_H2_VEHICLE_DAMAGE_LOG"))
		platform_log("h2 vehicle damage: %08lx threw %s (%08lx) from %s at %.2f %.2f %.2f, %.2f wu/s (%.2f %.2f %.2f), spin %.0f deg/s",
			(unsigned long)vehicle_index, name, (unsigned long)made, at_marker ? piece->marker : "its region's middle",
			data.position.x, data.position.y, data.position.z, magnitude3d(&data.translational_velocity) * 30.0f,
			data.translational_velocity.i * 30.0f, data.translational_velocity.j * 30.0f, data.translational_velocity.k * 30.0f,
			magnitude3d(&data.angular_velocity) * 30.0f * 57.29578f);
}

/* a response fired: its region's permutation (if worse) and its piece */
static void h2vd_respond(long vehicle_index, struct object_datum *object, struct model *model, struct h2vd_response const *response)
{
	short region = h2vd_region(model, response->region), p;
	struct model_region *r;

	if (region == NONE)
		return;
	r = TAG_BLOCK_GET_ELEMENT(&model->regions, region, struct model_region);
	if (response->permutation)
	{
		for (p = 0; p < MIN(r->permutations.count, MAXIMUM_PERMUTATIONS_PER_MODEL_REGION); p++)
		{
			char const *name = TAG_BLOCK_GET_ELEMENT(&r->permutations, p, struct model_region_permutation)->name;

			if (!strcmp(name, response->permutation))
			{
				char const *now = TAG_BLOCK_GET_ELEMENT(&r->permutations, object->object.region_permutations[region],
					struct model_region_permutation)->name;

				if (h2vd_rank(response->permutation) > h2vd_rank(now))
					object->object.region_permutations[region] = (byte)p;
				break;
			}
		}
	}
	if (response->debris)
		h2vd_debris(vehicle_index, object, model, region, response->debris, NULL);
	if (HALO_ENV_ONCE("HALO_H2_DEBUG"))
		platform_log("h2 vehicle damage: %08lx %s -> %s", (unsigned long)vehicle_index, response->region,
			response->permutation ? response->permutation : "(as it is)");
}

/* the vehicle's state; NONE if it is not one of the kinds */
static long h2vd_slot(long vehicle_index, struct object_datum *object, struct model *model)
{
	long i, free_slot = NONE;
	short kind, definition;

	for (i = 0; i < H2VD_MAXIMUM_VEHICLES; i++)
	{
		if (h2vd.vehicles[i].vehicle == vehicle_index)
			return i;
		if (free_slot == NONE && (h2vd.vehicles[i].vehicle == NONE || !object_try_and_get(h2vd.vehicles[i].vehicle)))
			free_slot = i;
	}
	{
		long d;
		char const *name = tag_get_name(object->definition_index);

		for (d = 0; d < h2vd_definition_count && strcmp(h2vd_definitions[d]->vehicle, name); d++)
			;
		/* (none made this run - its level read back from its cached file -:
		the level's) */
		if (d == h2vd_definition_count)
			d = h2vd_definition_from_level(object->definition_index);
		definition = d >= 0 && d < h2vd_definition_count && !HALO_ENV_ONCE("HALO_H2_VEHICLE_DAMAGE_OLD") ? (short)d : NONE;
	}
	for (kind = 0; kind < (short)NUMBEROF(h2vd_kinds) && h2vd_region(model, h2vd_kinds[kind].region) == NONE; kind++)
		;
	if (free_slot == NONE || (kind == (short)NUMBEROF(h2vd_kinds) && definition == NONE))
		return NONE;
	csmemset(&h2vd.vehicles[free_slot], 0, sizeof(h2vd.vehicles[free_slot]));
	h2vd.vehicles[free_slot].vehicle = vehicle_index;
	h2vd.vehicles[free_slot].kind = kind;
	h2vd.vehicles[free_slot].definition = definition;
	for (i = 0; i < H2VD_DEFINITION_SECTIONS; i++)
		h2vd.vehicles[free_slot].definition_health[i] = 1.0f;
	h2vd.vehicles[free_slot].killer_player = NONE;
	for (i = 0; i < H2VD_MAXIMUM_SECTIONS; i++)
		h2vd.vehicles[free_slot].health[i] = 1.0f;
	h2vd.seed ^= (unsigned long)vehicle_index * 2654435761UL;
	if (HALO_ENV_ONCE("HALO_H2_DEBUG"))
	{
		short r;

		for (r = 0; r < MIN(model->regions.count, MAXIMUM_REGIONS_PER_OBJECT); r++)
		{
			real_point3d center;
			real radius;

			h2vd_region_sphere(model, r, &center, &radius);
			struct model_region *mr = TAG_BLOCK_GET_ELEMENT(&model->regions, r, struct model_region);
			char names[160] = "";
			short p;

			for (p = 0; p < mr->permutations.count; p++)
			{
				size_t n = strlen(names);

				snprintf(names + n, sizeof(names) - n, " '%s'", TAG_BLOCK_GET_ELEMENT(&mr->permutations, p, struct model_region_permutation)->name);
			}
			platform_log("h2 vehicle damage: %08lx region %s at %.2f %.2f %.2f radius %.2f, %d permutations:%s", (unsigned long)vehicle_index,
				mr->name, center.x, center.y, center.z, radius, (int)mr->permutations.count, names);
		}
	}
	return free_slot;
}

/* ---------- private code: Halo 2's damage sections (read from its tags) */

static boolean h2vd_elsewhere(char const *effect)
{
	long i;

	for (i = 0; h2vd_effects_elsewhere[i]; i++)
	{
		if (!strcmp(h2vd_effects_elsewhere[i], effect))
			return TRUE;
	}
	return FALSE;
}

/* a definition's effect (names[] index) as this level's tag (h2_effects.c
made it: h2\<Halo 2's name>); NONE */
static long h2vd_effect_tag(struct h2vd_definition const *d, short name)
{
	char path[96];

	if (name == NONE || name >= d->name_count || d->names[name][0] != 'e' || h2vd_elsewhere(d->names[name] + 1))
		return NONE;
	snprintf(path, sizeof(path), "h2\\%s", d->names[name] + 1);
	return tag_loaded('effe', path);
}

/* an effect at a point of the vehicle (at marker - Halo 2's response's
effect marker, the effect's "replace" -, facing its way: function_d9d60;
else its region's middle; its own middle) */
static void h2vd_effect_at_marker(long effect, long vehicle_index, struct object_datum *object, struct model *model, short region,
	char const *marker_name)
{
	char const *names[1] = { "" };
	real_point3d point = object->object.bounding_sphere_center;
	real_vector3d up = object->object.up;
	struct object_marker marker;

	if (effect == NONE)
		return;
	if (marker_name && marker_name[0] && object_get_marker_by_name(vehicle_index, marker_name, &marker, 1) > 0)
	{
		point = marker.matrix.position;
		up = marker.matrix.forward;
	}
	else if (region != NONE)
	{
		real_point3d local;
		real radius;

		h2vd_region_sphere(model, region, &local, &radius);
		h2vd_world(object, &local, &point);
	}
	effect_new_unattached_from_markers(effect, vehicle_index, &object->object.translational_velocity, 1, names, &point, &up, 1.0f, 1.0f,
		NULL, NULL, FALSE);
}

static void h2vd_effect_at(long effect, long vehicle_index, struct object_datum *object, struct model *model, short region)
{
	h2vd_effect_at_marker(effect, vehicle_index, object, model, region, NULL);
}

/* the section's region on the model (a region of its name; else its first
response's); NONE */
static short h2vd_section_region(struct model *model, struct h2vd_definition_section const *section)
{
	char name[24];
	char *p;
	short k, region;

	snprintf(name, sizeof(name), "%s", section->name);
	for (p = name; *p; p++)
	{
		if (*p == '_')
			*p = ' ';
	}
	if ((region = h2vd_region(model, name)) != NONE)
		return region;
	for (k = 0; k < section->response_count; k++)
	{
		if (section->responses[k].region[0] && (region = h2vd_region(model, section->responses[k].region)) != NONE)
			return region;
	}
	return NONE;
}

/* the definition's region's damage state permutation (by name: its
permutations', else this engine's order's) on the model; NONE */
static short h2vd_state_permutation(struct h2vd_definition const *d, struct model *model, short region, short state)
{
	struct model_region *r = TAG_BLOCK_GET_ELEMENT(&model->regions, region, struct model_region);
	char const *wanted = state > 0 && state < (short)NUMBEROF(h2vd_order) ? h2vd_order[state] : NULL;
	short k, p;

	for (k = 0; k < d->region_count; k++)
	{
		if (!strcmp(d->regions[k].name, r->name) && d->regions[k].permutations[state][0])
			wanted = d->regions[k].permutations[state];
	}
	for (p = 0; wanted && p < MIN(r->permutations.count, MAXIMUM_PERMUTATIONS_PER_MODEL_REGION); p++)
	{
		if (!strcmp(TAG_BLOCK_GET_ELEMENT(&r->permutations, p, struct model_region_permutation)->name, wanted))
			return p;
	}
	return NONE;
}

/* the region's looping effect for its state (names[]); NONE */
static short h2vd_looping_effect(struct h2vd_definition const *d, struct model *model, short region, short state)
{
	struct model_region *r = TAG_BLOCK_GET_ELEMENT(&model->regions, region, struct model_region);
	short k;

	for (k = 0; k < d->region_count; k++)
	{
		if (!strcmp(d->regions[k].name, r->name))
			return d->regions[k].looping_effects[state];
	}
	return NONE;
}

/* the marker the region's looping effect for its state plays at (Halo 2's
looping effect marker name); NULL none */
static char const *h2vd_looping_marker(struct h2vd_definition const *d, struct model *model, short region, short state)
{
	struct model_region *r = TAG_BLOCK_GET_ELEMENT(&model->regions, region, struct model_region);
	short k;

	for (k = 0; k < d->region_count; k++)
	{
		if (!strcmp(d->regions[k].name, r->name))
			return d->regions[k].looping_markers[state];
	}
	return NULL;
}

/* a response fired now: its region's damage state (only ever a worse
one), its transition effect, its pieces, its seat's rider out; with
kill, the vehicle's body gone (its kills-object flags, in a damage) */
static void h2vd_definition_respond(long slot, struct object_datum *object, struct model *model, struct h2vd_definition const *d,
	struct h2vd_definition_response const *response, boolean kill)
{
	long vehicle_index = h2vd.vehicles[slot].vehicle, now = game_time_get();
	short region = response->region[0] ? h2vd_region(model, response->region) : NONE, k;

	if (region != NONE && response->state > h2vd.vehicles[slot].region_states[region])
	{
		short p = NONE, state;

		/* (its state's permutation; one the model lacks - the Scorpion's
		front has no destroyed one -: the worst it has under it) */
		for (state = response->state; state > 0 && p == NONE; state--)
			p = h2vd_state_permutation(d, model, region, state);

		h2vd.vehicles[slot].region_states[region] = (unsigned char)response->state;
		if (p != NONE)
			object->object.region_permutations[region] = (byte)p;
		/* (an insertion pod's door: its destroyed state has no permutation, Halo 2
		draws nothing there. No permutation drawn is none here too, its collision
		gone with it: models.c) */
		else if (strstr(d->vehicle, "insertion_pod"))
			object->object.region_permutations[region] = (byte)NONE;
		if (HALO_ENV_ONCE("HALO_H2_VEHICLE_DAMAGE_LOG"))
			platform_log("h2 vehicle damage: %08lx region %d (%s) permutation %d", (unsigned long)vehicle_index, (int)region, response->region, (int)p);
	}
	if (h2vd.vehicles[slot].effects_tick != now)
	{
		h2vd.vehicles[slot].effects_tick = now;
		h2vd.vehicles[slot].effects_this_tick = 0;
	}
	if (response->effect != NONE && h2vd.vehicles[slot].effects_this_tick < H2VD_EFFECTS_A_TICK)
	{
		long effect = h2vd_effect_tag(d, response->effect);

		if (effect != NONE)
		{
			h2vd_effect_at_marker(effect, vehicle_index, object, model, region, response->effect_marker);
			h2vd.vehicles[slot].effects_this_tick++;
		}
	}
	for (k = 0; k < H2VD_DEFINITION_DEBRIS && response->debris[k].name != NONE; k++)
		h2vd_debris(vehicle_index, object, model, region != NONE ? region : 0, d->names[response->debris[k].name] + 1, &response->debris[k]);
	/* (its transition damage effect: Halo 2's blast - the Scorpion's
	large_trans_damage as it blows up - an area damage at its region,
	its wreck left alone; owned by whoever killed it) */
	if (response->damage_effect != NONE && !HALO_ENV_ONCE("HALO_H2_NO_VEHICLE_BLASTS"))
	{
		char path[96];
		long blast;

		snprintf(path, sizeof(path), "h2\\vehicles\\damage\\%s", d->names[response->damage_effect] + 1);
		if ((blast = tag_loaded('jpt!', path)) != NONE)
		{
			struct damage_data damage;
			real_point3d point = object->object.bounding_sphere_center;

			if (region != NONE)
			{
				real_point3d local;
				real radius;

				h2vd_region_sphere(model, region, &local, &radius);
				h2vd_world(object, &local, &point);
			}
			damage_data_new(&damage, blast);
			damage.owner_object_index = vehicle_index;
			damage.owner_player_index = h2vd.vehicles[slot].killer_player;
			damage.owner_team_index = object->object.owner_team_index;
			damage.origin = damage.epicenter = point;
			damage.direction = object->object.up;
			scenario_location_from_point(&damage.location, &damage.origin);
			area_of_effect_cause_damage(&damage, vehicle_index);
			if (HALO_ENV_ONCE("HALO_H2_VEHICLE_DAMAGE_LOG"))
				platform_log("h2 vehicle damage: %08lx blast %s at %.2f %.2f %.2f", (unsigned long)vehicle_index, path, point.x, point.y, point.z);
		}
	}
	if (response->eject[0])
	{
		struct unit_definition *definition = unit_definition_get(object->definition_index);
		short seat;

		for (seat = 0; seat < definition->unit.seats.count; seat++)
		{
			if (!strcmp(TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, seat, struct unit_seat)->label, response->eject))
			{
				long rider = unit_seat_occupant(vehicle_index, seat);

				if (rider != NONE)
					unit_try_and_exit_seat(rider);
			}
		}
	}
	if (TEST_FLAG(response->flags, _h2vd_response_triggers_special_death_bit))
		h2vd.vehicles[slot].special_death = TRUE;
	if (kill && (TEST_FLAG(response->flags, _h2vd_response_kills_object_bit) ||
		TEST_FLAG(response->flags, _h2vd_response_kills_object_no_player_solo_bit)) && object->object.body_vitality >= 0.0f)
	{
		object->object.body_vitality = -0.001f;
	}
	if (HALO_ENV_ONCE("HALO_H2_DEBUG") || HALO_ENV_ONCE("HALO_H2_VEHICLE_DAMAGE_LOG"))
		platform_log("h2 vehicle damage: %08lx %s -> state %d%s%s%s", (unsigned long)vehicle_index, response->region[0] ? response->region : "(none)",
			(int)response->state, response->effect != NONE ? ", effect " : "", response->effect != NONE ? d->names[response->effect] + 1 : "",
			kill && TEST_FLAG(response->flags, _h2vd_response_kills_object_bit) ? ", kills it" : "");
}

/* a response's turn: fired now, or (its delay) its delay effect now and
the response later */
static void h2vd_definition_fire(long slot, struct object_datum *object, struct model *model, struct h2vd_definition const *d,
	short section, short response_index, boolean kill)
{
	struct h2vd_definition_response const *response = &d->sections[section].responses[response_index];
	long k;

	/* (a Ghost's delay is h2_vehicle_wrecks.c's warm-up: the hull section
	destroyed - by a grenade's blow at once, or by the body's end - starts
	it, and the warm-up's end fires the death's responses, this one among
	them (hull_destroyed_damage). Before: a Ghost whose hull a grenade
	destroyed fired this at once and died - no whine, no second explosion) */
	if (response->delay > 0.0f && strstr(d->vehicle, "ghost"))
	{
		extern boolean h2_vehicle_warmup_begin(long vehicle_index, real damage);
		boolean held = h2_vehicle_warmup_begin(h2vd.vehicles[slot].vehicle, 0.0f);

		if (HALO_ENV_ONCE("HALO_H2_WRECK_TRACE"))
			platform_log("h2 vehicle damage: %08lx ghost response %d of section %d (delay %.1f s): warm-up %s",
				(unsigned long)h2vd.vehicles[slot].vehicle, (int)response_index, (int)section, (double)response->delay, held ? "started" : "refused");
		if (held)
			return;
	}
	if (response->delay > 0.0f && !strstr(d->vehicle, "ghost"))
	{
		for (k = 0; k < H2VD_MAXIMUM_PENDING && h2vd.pending[k].vehicle != NONE && object_try_and_get(h2vd.pending[k].vehicle); k++)
			;
		if (k < H2VD_MAXIMUM_PENDING)
		{
			h2vd.pending[k].vehicle = h2vd.vehicles[slot].vehicle;
			h2vd.pending[k].slot = (short)slot;
			h2vd.pending[k].section = section;
			h2vd.pending[k].response = response_index;
			h2vd.pending[k].at = game_time_get() + (long)(response->delay * 30.0f);
			h2vd_effect_at(h2vd_effect_tag(d, response->delay_effect), h2vd.vehicles[slot].vehicle, object, model,
				response->region[0] ? h2vd_region(model, response->region) : NONE);
			return;
		}
	}
	h2vd_definition_respond(slot, object, model, d, response, kill);
}

/* (h2_vehicle_damage_body) a vehicle of Halo 2's damage sections hurt:
damage (of its body's maximum) dealt at point, area damage (an explosion's)
or a hit's */
static void h2vd_definition_damage(long slot, struct object_datum *object, struct model *model, real damage, real_point3d const *point,
	boolean area)
{
	struct h2vd_definition const *d = h2vd_definitions[h2vd.vehicles[slot].definition];
	short s, hit = NONE, main = NONE, k;
	real least = 1e30f;
	real_point3d local;

	for (s = 0; s < d->section_count; s++)
	{
		if (!strcmp(d->sections[s].name, "main"))
			main = s;
	}
	/* (the section struck: an area damage's the indirect section; a hit's
	the one whose region's middle is nearest, the main section not one -
	it is the body) */
	if (area && d->indirect_section >= 0 && d->indirect_section < d->section_count)
		hit = d->indirect_section;
	else
	{
		h2vd_local(object, point, &local);
		for (s = 0; s < d->section_count; s++)
		{
			short region = s != main ? h2vd_section_region(model, &d->sections[s]) : NONE;
			real_point3d center;
			real radius, distance;

			if (region == NONE)
				continue;
			h2vd_region_sphere(model, region, &center, &radius);
			distance = (local.x - center.x) * (local.x - center.x) + (local.y - center.y) * (local.y - center.y) +
				(local.z - center.z) * (local.z - center.z);
			if (distance < least)
			{
				least = distance;
				hit = s;
			}
		}
	}
	if (hit != NONE && damage > 0.0f && !h2vd.vehicles[slot].died)
	{
		struct h2vd_definition_section const *section = &d->sections[hit];

		if (section->vitality > 0.0f && hit != main)
			h2vd.vehicles[slot].definition_health[hit] -= damage / section->vitality;
		for (k = 0; k < section->response_count; k++)
		{
			struct h2vd_definition_response const *response = &section->responses[k];
			/* (whatever the damage: Halo 2 fires a section's responses as its
			health falls past them, area damage or a hit - damage.cpp
			function_daaa0 and function_dae60 never test a response's type
			(+0): a Ghost's bolts tear the Warthog's hull, its "receives area
			effect damage" responses, and throw its sail panels and winch, the
			owner's MCC frames. HALO_H2_RESPONSE_TYPES: only the damage its
			type names, as before) */
			boolean kind = !HALO_ENV_ONCE("HALO_H2_RESPONSE_TYPES") || response->type == _h2vd_response_all_damage ||
				(response->type == _h2vd_response_area_damage && area) || (response->type == _h2vd_response_local_damage && !area);
			boolean due = response->total_threshold > 0.0f ? damage >= response->total_threshold :
				hit != main && h2vd.vehicles[slot].definition_health[hit] <= response->threshold;

			if (kind && due && !(h2vd.vehicles[slot].definition_fired[hit] & (1U << k)))
			{
				h2vd.vehicles[slot].definition_fired[hit] |= (unsigned short)(1U << k);
				h2vd_definition_fire(slot, object, model, d, hit, k, TRUE);
			}
		}
	}
	/* (its death - a body below none, this engine's, not a Ghost held at
	none warming up -: the main section's responses, those of its special
	death or not, and every section that takes full damage then gone) */
	if (object->object.body_vitality < 0.0f && !h2vd.vehicles[slot].died)
	{
		h2vd.vehicles[slot].died = TRUE;
		for (s = 0; s < d->section_count; s++)
		{
			struct h2vd_definition_section const *section = &d->sections[s];

			if (s != main && !TEST_FLAG(section->flags, _h2vd_section_takes_full_damage_when_object_dies_bit))
				continue;
			h2vd.vehicles[slot].definition_health[s] = 0.0f;
			for (k = 0; k < section->response_count; k++)
			{
				struct h2vd_definition_response const *response = &section->responses[k];

				if (h2vd.vehicles[slot].definition_fired[s] & (1U << k))
					continue;
				if (s == main && ((TEST_FLAG(response->flags, _h2vd_response_only_on_special_death_bit) && !h2vd.vehicles[slot].special_death) ||
					(TEST_FLAG(response->flags, _h2vd_response_only_not_on_special_death_bit) && h2vd.vehicles[slot].special_death)))
				{
					continue;
				}
				h2vd.vehicles[slot].definition_fired[s] |= (unsigned short)(1U << k);
				h2vd_definition_fire(slot, object, model, d, s, k, FALSE);
			}
		}
	}
}

/* (each tick) the delayed responses due, and the damaged regions' looping
effects */
static void h2vd_definitions_update(void)
{
	long k, now = game_time_get();

	for (k = 0; k < H2VD_MAXIMUM_PENDING; k++)
	{
		struct object_datum *object;
		struct model *model;
		long slot = h2vd.pending[k].slot;

		if (h2vd.pending[k].vehicle == NONE || now < h2vd.pending[k].at)
			continue;
		if ((object = object_try_and_get(h2vd.pending[k].vehicle)) && h2vd.vehicles[slot].vehicle == h2vd.pending[k].vehicle &&
			h2vd.vehicles[slot].definition != NONE && (model = h2vd_model(object)))
		{
			struct h2vd_definition const *d = h2vd_definitions[h2vd.vehicles[slot].definition];

			h2vd_definition_respond(slot, object, model, d, &d->sections[h2vd.pending[k].section].responses[h2vd.pending[k].response], FALSE);
		}
		h2vd.pending[k].vehicle = NONE;
	}
	if (h2vd.loop_tick != now)
	{
		h2vd.loop_tick = now;
		h2vd.loops_this_tick = 0;
	}
	for (k = 0; k < H2VD_MAXIMUM_VEHICLES && h2vd.loops_this_tick < H2VD_LOOPS_A_TICK; k++)
	{
		struct object_datum *object;
		struct model *model;
		struct h2vd_definition const *d;
		short region, played = 0, state;

		if (h2vd.vehicles[k].vehicle == NONE || h2vd.vehicles[k].definition == NONE || now < h2vd.vehicles[k].next_loop ||
			!(object = object_try_and_get(h2vd.vehicles[k].vehicle)) || !(model = h2vd_model(object)))
		{
			continue;
		}
		d = h2vd_definitions[h2vd.vehicles[k].definition];
		h2vd.vehicles[k].next_loop = now + H2VD_LOOP_PERIOD;
		/* (the most damaged first) */
		for (state = H2VD_STATES - 1; state > 0 && played < H2VD_LOOPING_REGIONS; state--)
		{
			for (region = 0; region < MIN(model->regions.count, MAXIMUM_REGIONS_PER_OBJECT) && played < H2VD_LOOPING_REGIONS; region++)
			{
				long effect;

				if (h2vd.vehicles[k].region_states[region] != state)
					continue;
				effect = h2vd_effect_tag(d, h2vd_looping_effect(d, model, region, state));
				if (effect == NONE)
					continue;
				h2vd_effect_at_marker(effect, h2vd.vehicles[k].vehicle, object, model, region, h2vd_looping_marker(d, model, region, state));
				played++;
				h2vd.loops_this_tick++;
			}
		}
	}
}

/* ---------- public code */

/* (damage.c, a vehicle dying) whether Halo 2's damage sections play its
death - their transition effects and blast - in place of its collision
model's depleted effect (the kit's fire explosion, the stand-in's: on the
Scorpion, Banshee, Wraith and Spectre it played besides). The Warthog's and
Ghost's depleted effects are Halo 2's own already (h2_effects.c), their
wrecks' burning and warm-up h2_vehicle_wrecks.c's */
boolean h2_vehicle_damage_owns_death(long vehicle_index)
{
	struct object_datum *object = object_try_and_get(vehicle_index);
	char const *name;
	long d;

	if (!object || object->object.type != _object_type_vehicle || HALO_ENV_ONCE("HALO_H2_KIT_DEATH_EFFECT"))
		return FALSE;
	h2vd_level();
	if (!h2vd.halo2)
		return FALSE;
	name = tag_get_name(object->definition_index);
	if (strstr(name, "warthog") || strstr(name, "ghost"))
		return FALSE;
	for (d = 0; d < h2vd_definition_count; d++)
	{
		if (!strcmp(h2vd_definitions[d]->vehicle, name))
			return TRUE;
	}
	return FALSE;
}

/* the converted vehicles' names in Halo 2's words (its HUD strings'
"<vehicle>_flip": "to flip Shadow"), for the prompts' vehicle names
(hud.c): the builder's, h2_vehicle_prompt_name_define */
static struct
{
	char vehicle[48];
	wchar_t name[32];
} h2vd_prompt_names[H2VD_MAXIMUM_DEFINITIONS];
static long h2vd_prompt_name_count;

void h2_vehicle_prompt_names_clear(void)
{
	h2vd_prompt_name_count = 0;
}

void h2_vehicle_prompt_name_define(char const *vehicle, char const *name)
{
	long i;

	if (h2vd_prompt_name_count >= H2VD_MAXIMUM_DEFINITIONS)
		return;
	snprintf(h2vd_prompt_names[h2vd_prompt_name_count].vehicle, sizeof(h2vd_prompt_names[0].vehicle), "%s", vehicle);
	for (i = 0; name[i] && i < 31; i++)
		h2vd_prompt_names[h2vd_prompt_name_count].name[i] = (wchar_t)(unsigned char)name[i];
	h2vd_prompt_names[h2vd_prompt_name_count].name[i] = 0;
	h2vd_prompt_name_count++;
}

/* (hud.c) a Halo 2 level's vehicle's name in Halo 2's words; NULL none */
wchar_t const *h2_vehicle_prompt_name(long definition_index)
{
	char const *name;
	long i;

	if (definition_index == NONE)
		return NULL;
	h2vd_level();
	if (!h2vd.halo2)
		return NULL;
	name = tag_get_name(definition_index);
	for (i = 0; i < h2vd_prompt_name_count; i++)
	{
		if (!strcmp(h2vd_prompt_names[i].vehicle, name))
			return h2vd_prompt_names[i].name;
	}
	return NULL;
}

/* ---------- public code: a definition kept in the level */

/* A vehicle's damage definition rides in its level's tags (the second
string of its "<name> physics" list, h2_vehicles.c), so a level read back
from its cached file - the builder not run - has Halo 2's damage sections:
without it a second launch had none (the Banshee, the Scorpion, the Spectre
nothing; the Warthog and the Ghost the written-out kinds). Packed: a
header (H2VD_PACKED_MAGIC, H2VD_PACKED_VERSION, the definition's size),
then its bytes, a run of zeros as a 0 and the run's length, two zeros after
(the level's reading ends a string list's string with two) and to whole
words */
enum
{
	H2VD_PACKED_MAGIC = 0x68327664,     /* 'h2vd' */
	H2VD_PACKED_VERSION = 1,
	H2VD_PACKED_HEADER = 12
};

long h2_vehicle_damage_pack(struct h2vd_definition const *definition, unsigned char *out, long size)
{
	unsigned char const *in = (unsigned char const *)definition;
	long length = (long)sizeof(*definition), i = 0, n = H2VD_PACKED_HEADER;
	unsigned long header[3] = { H2VD_PACKED_MAGIC, H2VD_PACKED_VERSION, (unsigned long)sizeof(*definition) };

	if (size < H2VD_PACKED_HEADER)
		return 0;
	memcpy(out, header, H2VD_PACKED_HEADER);
	while (i < length)
	{
		if (n + 2 > size)
			return 0;
		if (in[i])
			out[n++] = in[i++];
		else
		{
			long run = 0;

			while (i < length && !in[i] && run < 255)
			{
				i++;
				run++;
			}
			out[n++] = 0;
			out[n++] = (unsigned char)run;
		}
	}
	/* (two zeros after it - a string list's string has its last two bytes
	made its end as the level is read -, then whole words) */
	while ((n & 3) || out[n - 1] || out[n - 2])
	{
		if (n >= size)
			return 0;
		out[n++] = 0;
	}
	return n;
}

/* a packed definition read back (h2_vehicle_damage_pack's), checked as
anything read from a file is: its sizes, its counts within their room, its
names' indices within its names, its text ended. FALSE: not one */
static boolean h2vd_unpack(unsigned char const *in, long size, struct h2vd_definition *d)
{
#define H2VD_UNPACK_FAIL(n) do { if (HALO_ENV_ONCE("HALO_H2_DEBUG")) platform_log("h2 vehicle damage: a packed definition not one (%d)", n); return FALSE; } while (0)
	unsigned char *out = (unsigned char *)d;
	long length = (long)sizeof(*d), i = H2VD_PACKED_HEADER, n = 0, s, k, q;
	unsigned long header[3];

	if (!in || size < H2VD_PACKED_HEADER)
		H2VD_UNPACK_FAIL(1);
	memcpy(header, in, H2VD_PACKED_HEADER);
	if (header[0] != H2VD_PACKED_MAGIC || header[1] != H2VD_PACKED_VERSION || header[2] != (unsigned long)sizeof(*d))
		H2VD_UNPACK_FAIL(2);
	while (i < size && n < length)
	{
		if (in[i])
			out[n++] = in[i++];
		else
		{
			long run;

			if (i + 1 >= size || (run = in[i + 1]) == 0 || n + run > length)
			{
				if (HALO_ENV_ONCE("HALO_H2_DEBUG"))
					platform_log("h2 vehicle damage: unpack stopped at %ld of %ld (out %ld of %ld): %02x %02x %02x %02x", i, size, n, length,
						in[i], i + 1 < size ? in[i + 1] : 0, i + 2 < size ? in[i + 2] : 0, i + 3 < size ? in[i + 3] : 0);
				H2VD_UNPACK_FAIL(3);
			}
			memset(out + n, 0, (size_t)run);
			n += run;
			i += 2;
		}
	}
	/* (its padding, zeros) */
	while (n == length && i < size && size - i < 8 && !in[i])
		i++;
	if (n != length || i != size)
		H2VD_UNPACK_FAIL(4);
#define H2VD_NAME_OK(x) ((x) == NONE || ((x) >= 0 && (x) < d->name_count))
#define H2VD_TEXT_END(t) ((t)[sizeof(t) - 1] = 0)
	if (d->section_count < 0 || d->section_count > H2VD_DEFINITION_SECTIONS || d->region_count < 0 || d->region_count > H2VD_DEFINITION_REGIONS ||
		d->name_count < 0 || d->name_count > H2VD_DEFINITION_NAMES || d->indirect_section >= d->section_count)
	{
		H2VD_UNPACK_FAIL(5);
	}
	H2VD_TEXT_END(d->vehicle);
	for (k = 0; k < d->name_count; k++)
		H2VD_TEXT_END(d->names[k]);
	for (s = 0; s < d->section_count; s++)
	{
		struct h2vd_definition_section *section = &d->sections[s];

		H2VD_TEXT_END(section->name);
		if (section->response_count < 0 || section->response_count > H2VD_DEFINITION_RESPONSES)
			H2VD_UNPACK_FAIL(6);
		for (k = 0; k < section->response_count; k++)
		{
			struct h2vd_definition_response *response = &section->responses[k];

			H2VD_TEXT_END(response->region);
			H2VD_TEXT_END(response->eject);
			H2VD_TEXT_END(response->effect_marker);
			if (!H2VD_NAME_OK(response->effect) || !H2VD_NAME_OK(response->damage_effect) || !H2VD_NAME_OK(response->delay_effect) ||
				response->state < 0 || response->state >= H2VD_STATES || !(response->delay >= 0.0f && response->delay < 600.0f))
			{
				H2VD_UNPACK_FAIL(7);
			}
			for (q = 0; q < H2VD_DEFINITION_DEBRIS; q++)
			{
				H2VD_TEXT_END(response->debris[q].marker);
				if (!H2VD_NAME_OK(response->debris[q].name))
					H2VD_UNPACK_FAIL(8);
			}
		}
	}
	for (k = 0; k < d->region_count; k++)
	{
		H2VD_TEXT_END(d->regions[k].name);
		for (q = 0; q < H2VD_STATES; q++)
		{
			H2VD_TEXT_END(d->regions[k].permutations[q]);
			H2VD_TEXT_END(d->regions[k].looping_markers[q]);
			if (!H2VD_NAME_OK(d->regions[k].looping_effects[q]))
				H2VD_UNPACK_FAIL(9);
		}
	}
#undef H2VD_NAME_OK
#undef H2VD_TEXT_END
#undef H2VD_UNPACK_FAIL
	return TRUE;
}

/* (h2vd_slot) the vehicle's definition from its level's tags, kept with
the builder's; NONE none */
static short h2vd_definition_from_level(long definition_index)
{
	char name[256];
	long list;
	struct string_list *strings;
	struct string_list_entry *entry;
	struct h2vd_definition *d;
	short made = NONE;

	snprintf(name, sizeof(name), "%s physics", tag_get_name(definition_index));
	if ((list = tag_loaded('str#', name)) == NONE || !(strings = (struct string_list *)tag_get('str#', list)) || strings->strings.count < 2)
		return NONE;
	entry = TAG_BLOCK_GET_ELEMENT(&strings->strings, 1, struct string_list_entry);
	if (!(d = calloc(1, sizeof(*d))))
		return NONE;
	if (h2vd_unpack((unsigned char const *)entry->string.address, entry->string.size, d) && !strcmp(d->vehicle, tag_get_name(definition_index)) &&
		h2vd_definition_count < H2VD_MAXIMUM_DEFINITIONS)
	{
		made = (short)h2vd_definition_count;
		h2vd_definitions[h2vd_definition_count++] = d;
		platform_log("h2 vehicle damage: %s: its damage Halo 2's, from the level (%d sections)", d->vehicle, (int)d->section_count);
		return made;
	}
	platform_log("h2 vehicle damage: %s: its level's damage not read (%ld bytes)", tag_get_name(definition_index), (long)entry->string.size);
	free(d);
	return NONE;
}

void h2_vehicle_damage_define(struct h2vd_definition const *definition)
{
	struct h2vd_definition *copy;

	if (h2vd_definition_count >= H2VD_MAXIMUM_DEFINITIONS || !(copy = malloc(sizeof(*copy))))
		return;
	memcpy(copy, definition, sizeof(*copy));
	h2vd_definitions[h2vd_definition_count++] = copy;
}

void h2_vehicle_damage_definitions_clear(void)
{
	long i;

	for (i = 0; i < h2vd_definition_count; i++)
		free(h2vd_definitions[i]);
	h2vd_definition_count = 0;
}

/* (damage.c's object_damage_body, a vehicle's) damage - of its body's
maximum - dealt at point: its sections' share and their responses; its
death's, when its body is gone */
/* (object_damage_damage_section, h2_ai.c) the named section of a vehicle taken to
none by a script: its responses, all of them - an insertion pod's door off, with
its ejection effect. FALSE: no such section */
boolean h2_vehicle_damage_section(long vehicle_index, char const *name)
{
	struct object_datum *object = object_try_and_get(vehicle_index);
	struct model *model;
	long slot;
	struct h2vd_definition const *d;
	short s, k;

	if (!object || object->object.type != _object_type_vehicle || !name)
		return FALSE;
	h2vd_level();
	if (!h2vd.halo2 || !(model = h2vd_model(object)) || (slot = h2vd_slot(vehicle_index, object, model)) == NONE ||
		h2vd.vehicles[slot].definition == NONE)
	{
		return FALSE;
	}
	d = h2vd_definitions[h2vd.vehicles[slot].definition];
	for (s = 0; s < d->section_count && strcmp(d->sections[s].name, name); s++)
		;
	if (s == d->section_count)
		return FALSE;
	h2vd.vehicles[slot].definition_health[s] = 0.0f;
	for (k = 0; k < d->sections[s].response_count; k++)
	{
		if (!(h2vd.vehicles[slot].definition_fired[s] & (1U << k)))
		{
			h2vd.vehicles[slot].definition_fired[s] |= (unsigned short)(1U << k);
			h2vd_definition_fire(slot, object, model, d, s, k, TRUE);
		}
	}
	return TRUE;
}

void h2_vehicle_damage_body(long vehicle_index, real damage, real_point3d const *point, boolean area, long owner_player_index)
{
	struct object_datum *object = object_try_and_get(vehicle_index);
	struct model *model;
	struct h2vd_kind const *kind;
	long slot, s, nearest = NONE;
	real distance[H2VD_MAXIMUM_SECTIONS], least = 1e30f;
	real_point3d local;

	if (!object || object->object.type != _object_type_vehicle)
		return;
	h2vd_level();
	if (!h2vd.halo2 || !(model = h2vd_model(object)) || (slot = h2vd_slot(vehicle_index, object, model)) == NONE)
		return;
	/* (a client's Ghost warming up, the host's kill replayed here: its damage
	is no blow - the host's Ghost took none at its warm-up's end, and its
	death's explosion is the one) */
	{
		extern boolean h2_vehicle_warming_on_client(long vehicle_index);

		if (h2_vehicle_warming_on_client(vehicle_index))
			return;
	}
	if (h2vd.vehicles[slot].definition != NONE)
	{
		if (owner_player_index != NONE)
			h2vd.vehicles[slot].killer_player = owner_player_index;
		h2vd_definition_damage(slot, object, model, damage, point, area);
		return;
	}
	kind = &h2vd_kinds[h2vd.vehicles[slot].kind];
	h2vd_local(object, point, &local);
	for (s = 0; s < H2VD_MAXIMUM_SECTIONS; s++)
	{
		short region = kind->sections[s].region ? h2vd_region(model, kind->sections[s].region) : NONE;
		real_point3d center;
		real radius, dx, dy, dz;

		distance[s] = 1e30f;
		if (region == NONE)
			continue;
		h2vd_region_sphere(model, region, &center, &radius);
		dx = local.x - center.x;
		dy = local.y - center.y;
		dz = local.z - center.z;
		distance[s] = sqrtf(dx * dx + dy * dy + dz * dz);
		if (distance[s] < least)
		{
			least = distance[s];
			nearest = s;
		}
	}
	for (s = 0; s < H2VD_MAXIMUM_SECTIONS && damage > 0.0f && nearest != NONE; s++)
	{
		struct h2vd_section const *section = &kind->sections[s];
		real share = 1.0f - (distance[s] - least) / H2VD_FALLOFF, r;
		long k;

		if (!section->region || share <= 0.0f || section->vitality <= 0.0f)
			continue;
		h2vd.vehicles[slot].health[s] -= damage * share / section->vitality;
		for (k = 0; k < H2VD_MAXIMUM_RESPONSES && section->responses[k].region; k++)
		{
			r = section->responses[k].threshold;
			if (h2vd.vehicles[slot].health[s] <= r && !(h2vd.vehicles[slot].fired[s] & (1UL << k)))
			{
				h2vd.vehicles[slot].fired[s] |= 1UL << k;
				h2vd_respond(vehicle_index, object, model, &section->responses[k]);
			}
		}
	}
	/* (its death: a body below none, this engine's - not a Ghost held at
	none warming up, h2_vehicle_wrecks.c) */
	if (object->object.body_vitality < 0.0f && !h2vd.vehicles[slot].fired[H2VD_DEATH])
	{
		long k;

		h2vd.vehicles[slot].fired[H2VD_DEATH] = 1;
		for (k = 0; k < (long)NUMBEROF(kind->death) && kind->death[k].region; k++)
			h2vd_respond(vehicle_index, object, model, &kind->death[k]);
	}
}

/* (network_objects.c, a client) the host's body of a vehicle, as a state brings
it: a fall in it is damage here (its sections, their looks and effects; a Ghost
at none starts its warm-up), which a client's replay of the hit has not already
taken. The host alone decides its death */
void h2_vehicle_network_body(long vehicle_index, real body)
{
	struct object_datum *object = object_try_and_get(vehicle_index);
	extern boolean h2_vehicle_warmup_begin(long vehicle_index, real damage);
	real before;

	if (!object || object->object.type != _object_type_vehicle || TEST_FLAG(object->object.damage_flags, _object_dead_bit))
		return;
	before = object->object.body_vitality;
	if (body <= 0.f && before > 0.f)
	{
		h2_vehicle_damage_body(vehicle_index, before, &object->object.bounding_sphere_center, FALSE, NONE);
		object_try_and_get(vehicle_index);
		object->object.body_vitality = 0.f;
		h2_vehicle_warmup_begin(vehicle_index, 0.f);
	}
	else if (before - body > 1.f / 254.f)
	{
		h2_vehicle_damage_body(vehicle_index, before - body, &object->object.bounding_sphere_center, FALSE, NONE);
		object->object.body_vitality = body;
	}
	else if (body - before > 1.f / 254.f)
		object->object.body_vitality = body;
}

/* (network_objects.c, a client) the host says a vehicle is dead: its death's
responses first - its sections' looks, what they throw off -, as the host
plays them before its body is depleted (h2_vehicle_wrecks.c's warm-up end).
Its depleted effect finds the model's markers as the host's did: a Ghost's
cockpit light and seat effect gone with its destroyed seat; made on a client
before them it drew a second light and blob there, a yellow flash with it */
void h2_vehicle_network_death(long vehicle_index)
{
	struct object_datum *object = object_try_and_get(vehicle_index);

	if (!object || object->object.type != _object_type_vehicle || TEST_FLAG(object->object.damage_flags, _object_dead_bit) ||
		object->object.body_vitality < 0.f)
	{
		return;
	}
	object->object.body_vitality = -0.01f;
	h2_vehicle_damage_body(vehicle_index, 0.0f, &object->object.position, FALSE, NONE);
}

/* (main.c's @hit) the damage dealt the vehicle at a point of its model's
(forward, left, up) */
void h2_vehicle_damage_test_hit(long vehicle_index, long damage_definition_index, real const *at)
{
	struct object_datum *object = object_try_and_get(vehicle_index);
	struct damage_data damage;
	real_point3d local, world;

	if (!object)
		return;
	local.x = at[0];
	local.y = at[1];
	local.z = at[2];
	h2vd_world(object, &local, &world);
	damage_data_new(&damage, damage_definition_index);
	damage.origin = damage.epicenter = world;
	/* (from the point toward the vehicle's centre: its push's way) */
	vector_from_points3d(&world, &object->object.position, &damage.direction);
	if (normalize3d(&damage.direction) == 0.0f)
		damage.direction = *global_up3d;
	scenario_location_from_point(&damage.location, &damage.origin);
	/* (a hit's, not an area damage's: its surface's normal, facing back) */
	{
		real_vector3d normal = { -damage.direction.i, -damage.direction.j, -damage.direction.k };

		object_cause_damage(&damage, vehicle_index, NONE, NONE, NONE, HALO_ENV_ONCE("HALO_TEST_HIT_AREA") ? NULL : &normal);
	}
}

/* (h2_vehicle_wrecks.c) how damaged the vehicle's region of that name looks:
0 its base permutation, 1 minor, 2 medium, 3 major, 4 destroyed, 5 gone;
-1 none (not a Halo 2 level's vehicle, no such region) */
long h2_vehicle_damage_state(long vehicle_index, char const *region_name)
{
	struct object_datum *object = object_try_and_get(vehicle_index);
	struct model *model;
	struct model_region *r;
	short region;
	byte permutation;

	if (!object || object->object.type != _object_type_vehicle)
		return -1;
	h2vd_level();
	if (!h2vd.halo2 || !(model = h2vd_model(object)) || (region = h2vd_region(model, region_name)) == NONE)
		return -1;
	r = TAG_BLOCK_GET_ELEMENT(&model->regions, region, struct model_region);
	permutation = object->object.region_permutations[region];
	if (permutation >= MIN(r->permutations.count, MAXIMUM_PERMUTATIONS_PER_MODEL_REGION))
		return 0;
	{
		long rank = h2vd_rank(TAG_BLOCK_GET_ELEMENT(&r->permutations, permutation, struct model_region_permutation)->name);

		/* (a name not in the order: its undamaged look) */
		return rank < (long)NUMBEROF(h2vd_order) - 1 ? rank : 0;
	}
}

/* (game_engine.c, each tick) the pieces thrown off the vehicles settle as
Halo 2's do - a few metres from the wreck, a bounce or two, then still. Halo
2 throws them at its effects' velocity bounds (a tire 1.5 to 3 world units a
second in a 35 degree cone, a hubcap 1.8 to 1.95, the Ghost's seat 2 to 5);
this engine's garbage, light, was then thrown again by the vehicle's own
explosion beside it - as a grenade at its feet throws a weapon -, and slid
and bounced on ("even the debris just shoots around the map", the owner).
Each piece's speed is held to Halo 2's most (H2VD_DEBRIS_SPEED), a bounce
takes half its speed along the ground, and after its third (or two
seconds) it slows to a stop within a second */
#define H2VD_DEBRIS_SPEED (5.0f / 30.0f)      /* world units a tick: the Ghost seat's 5 a second */
#define H2VD_DEBRIS_RISE (3.0f / 30.0f)       /* (its upward speed) */
void h2_vehicle_damage_debris_update(void)
{
	long m, now = game_time_get();
	/* (HALO_H2_DEBRIS_FREE: none of it, a test's - the trace alone) */
	boolean free_flight = HALO_ENV_ONCE("HALO_H2_DEBRIS_FREE") != NULL;

	if (!h2vd.halo2 || h2vd.scenario != global_scenario_index || h2vd.generation != halo_map_generation)
		return;
	h2vd_definitions_update();
	for (m = 0; m < H2VD_MAXIMUM_DEBRIS; m++)
	{
		struct object_datum *object;
		real_vector3d *v;
		real flat, k;

		if (h2vd.motion[m].object == NONE)
			continue;
		if (!(object = object_try_and_get(h2vd.motion[m].object)) || object->object.parent_object_index != NONE)
		{
			h2vd.motion[m].object = NONE;
			continue;
		}
		v = &object->object.translational_velocity;
		/* (bounced: falling last tick, rising or stopped now) */
		if (h2vd.motion[m].last_k < -0.02f && v->k > h2vd.motion[m].last_k + 0.02f)
			h2vd.motion[m].bounces++;
		if (free_flight)
			;
		else if (h2vd.motion[m].last_k < -0.02f && v->k > h2vd.motion[m].last_k + 0.02f)
		{
			v->i *= 0.5f;
			v->j *= 0.5f;
			v->k *= 0.5f;
			object->object.angular_velocity.i *= 0.6f;
			object->object.angular_velocity.j *= 0.6f;
			object->object.angular_velocity.k *= 0.6f;
		}
		/* (its first second: no faster than it was thrown - the vehicle's
		own explosion, beside it as it is thrown, adds nothing; then Halo
		2's most) */
		{
			real most = now - h2vd.motion[m].born < 30 ? MIN(H2VD_DEBRIS_SPEED, h2vd.motion[m].launch) : H2VD_DEBRIS_SPEED;
			real speed = magnitude3d(v);

			if (!free_flight && speed > most && speed > 0.0f)
			{
				v->i *= most / speed;
				v->j *= most / speed;
				v->k *= most / speed;
			}
			if (!free_flight && v->k > H2VD_DEBRIS_RISE)
				v->k = H2VD_DEBRIS_RISE;
		}
		flat = sqrtf(v->i * v->i + v->j * v->j);
		/* (settling: its third bounce, or two seconds on) */
		if (!free_flight && (h2vd.motion[m].bounces >= 3 || now - h2vd.motion[m].born > 2 * 30))
		{
			v->i *= 0.85f;
			v->j *= 0.85f;
			if (v->k > 0.0f)
				v->k *= 0.85f;
			object->object.angular_velocity.i *= 0.8f;
			object->object.angular_velocity.j *= 0.8f;
			object->object.angular_velocity.k *= 0.8f;
		}
		k = v->k;
		if (HALO_ENV_ONCE("HALO_H2_DEBRIS_TRACE") && ((now - h2vd.motion[m].born) % 5) == 0)
		{
			real dx = object->object.position.x - h2vd.motion[m].from.x, dy = object->object.position.y - h2vd.motion[m].from.y;

			platform_log("h2 vehicle damage: debris %08lx %s t+%ld from its place %.2f (%.1f m) up %.2f speed %.3f/%.3f bounces %d",
				(unsigned long)h2vd.motion[m].object, tag_get_name(object->definition_index), now - h2vd.motion[m].born,
				sqrtf(dx * dx + dy * dy), 3.0f * sqrtf(dx * dx + dy * dy), object->object.position.z - h2vd.motion[m].from.z,
				sqrtf(v->i * v->i + v->j * v->j), v->k, (int)h2vd.motion[m].bounces);
		}
		h2vd.motion[m].last_k = k;
		/* (followed five seconds) */
		if (now - h2vd.motion[m].born > 5 * 30)
			h2vd.motion[m].object = NONE;
	}
}

#include "physics/collision_model_definitions.h"

/* ---------- a tank's hull boarders */

/* Halo 2's tanks are not jacked: a boarder on the Wraith's or the
Scorpion's hull (its hull seats - wraith_b_l/_r, scorpion_b_d_l/_r,
scorpion_b_b: UNIT_SEAT_BEATING_MAGIC, h2_vehicles.c) beats on the hatch
with the melee button - its seat's "melee", Halo 2's boarding melee damage
(the Spartan's masterchief_boarding_melee, "h2\vehicles\boarding melee")
on the driver at the blow - or drops a grenade in with the grenade button
- its seat's "throw_grenade", one of its grenades stuck at Halo 2's
boarding grenade marker - and gets off when the driver is gone or its grenade is in. The
animations are the boarder's graph's seat's user1 and user2
(h2_animation.c); the blow lands at the melee's middle, the grenade
leaves the hand at the throw's (Halo 2's own frames are not read). */

enum
{
	H2VD_MAXIMUM_HULL_BOARDERS = 16,
	H2VD_SEAT_ANIMATION_MELEE = 14,             /* (the seat's user1) */
	H2VD_SEAT_ANIMATION_THROW_GRENADE = 15,     /* (its user2) */
};

static struct
{
	long unit;
	long busy_until;            /* game time its blow or throw ends */
	long strike_at;             /* game time it lands (NONE: landed) */
	short action;               /* 0 melee, 1 grenade, 2 its grenade dropped */
	unsigned long held;         /* its control flags last tick */
	long scenario;
	unsigned long generation;
} h2vd_boarders[H2VD_MAXIMUM_HULL_BOARDERS];

/* Halo 2's boarding grenade marker of a hull seat (vehi seats' "boarding
grenade marker") */
static char const *h2vd_hull_grenade_marker(char const *label)
{
	if (!strncmp(label, "wraith_b", 8))
		return "grenade driver";
	if (!strcmp(label, "scorpion_b_b"))
		return "grenade engine";
	return "driver grenade";
}

/* the boarder's seat animation in slot (its graph's seat it sits in),
its frames; NULL none */
static char const *h2vd_seat_animation(struct unit_datum *unit, long graph_index, short slot, short *frames)
{
	struct animation_graph *graph;
	struct animation_graph_unit_seat *seat;
	short animation_index;
	struct animation *animation;

	if (graph_index == NONE)
		return NULL;
	graph = animation_graph_definition_get(graph_index);
	if (!VALID_INDEX(unit->unit.animation.seat_index, graph->unit_seats.count))
		return NULL;
	seat = TAG_BLOCK_GET_ELEMENT(&graph->unit_seats, unit->unit.animation.seat_index, struct animation_graph_unit_seat);
	if (seat->animations.count <= slot)
		return NULL;
	animation_index = animation_graph_animation_index_get(&seat->animations)[slot].animation_index;
	if (!VALID_INDEX(animation_index, graph->animations.count))
		return NULL;
	animation = TAG_BLOCK_GET_ELEMENT(&graph->animations, animation_index, struct animation);
	*frames = animation->frame_count;
	return animation->name;
}

/* the blow: Halo 2's boarding melee damage on the driver */
static void h2vd_hull_blow(long unit_index, long occupant_index, boolean past_shields)
{
	struct unit_datum *unit = unit_get(unit_index);
	long damage_index = tag_loaded('jpt!', "h2\\vehicles\\boarding melee");
	struct damage_data damage;

	if (damage_index == NONE)
		return;
	damage_data_new(&damage, damage_index);
	damage.owner_object_index = unit_index;
	damage.owner_player_index = unit->unit.player_index;
	damage.owner_team_index = unit->object.owner_team_index;
	object_get_origin(unit_index, &damage.origin);
	object_get_origin(occupant_index, &damage.epicenter);
	vector_from_points3d(&damage.origin, &damage.epicenter, &damage.direction);
	if (normalize3d(&damage.direction) == 0.0f)
		damage.direction = *global_up3d;
	damage.origin = damage.epicenter;
	scenario_location_from_point(&damage.location, &damage.origin);
	/* (a Prophet beaten on his throne: past his shield - Regret's recharges
	in a tenth of a second, and 05b's regret_death gives a boarder five
	seconds to pin him, ai_vitality_pinned) */
	if (past_shields)
		SET_FLAG(damage.flags, _damage_bypasses_shields_bit, TRUE);
	object_cause_damage(&damage, occupant_index, NONE, NONE, NONE, NULL);
	if (HALO_ENV_ONCE("HALO_H2_VEHICLE_LOG"))
	{
		struct object_definition const *od = object_definition_get(object_get(occupant_index)->definition_index);
		struct collision_model const *cm = od->object.collision_model.index != NONE ?
			collision_model_definition_get(od->object.collision_model.index) : NULL;

		platform_log("h2 hull: %08lx beats on the hatch: %08lx body %.2f shield %.2f (recharge %.4f a tick, stun %d, collision %s)",
			(unsigned long)unit_index, (unsigned long)occupant_index,
			object_get(occupant_index)->object.body_vitality, object_get(occupant_index)->object.shield_vitality,
			cm ? cm->resistance.runtime_shield_recharge_velocity : -1.0f, (int)object_get(occupant_index)->object.shield_stun_ticks,
			od->object.collision_model.index != NONE ? tag_get_name(od->object.collision_model.index) : "none");
	}
}

/* the grenade: one of the boarder's, stuck at the hatch's grenade marker */
static void h2vd_hull_grenade(long unit_index, long vehicle_index, short seat_index, short target_seat_index)
{
	struct unit_datum *unit = unit_get(unit_index);
	struct unit_definition *vehicle_definition = unit_definition_get(object_get(vehicle_index)->definition_index);
	struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(&vehicle_definition->unit.seats, seat_index, struct unit_seat);
	struct game_globals *globals = scenario_get_game_globals();
	struct game_globals_grenade *grenade;
	struct object_placement_data placement;
	struct object_marker marker;
	short type = unit->unit.current_grenade_index;
	long grenade_index;

	if (type < 0 || type >= globals->grenades.count || type >= NUMBER_OF_UNIT_GRENADE_TYPES || unit->unit.grenade_counts[type] <= 0)
		return;
	if (!object_get_marker_by_name(vehicle_index, h2vd_hull_grenade_marker(seat->label), &marker, 1))
		return;
	grenade = TAG_BLOCK_GET_ELEMENT(&globals->grenades, type, struct game_globals_grenade);
	object_placement_data_new(&placement, grenade->projectile.index, unit_index);
	SET_FLAG(placement.flags, _new_object_never_automatically_delete_bit, TRUE);
	placement.position = marker.matrix.position;
	if ((grenade_index = object_new(&placement)) == NONE)
		return;
	unit->unit.grenade_counts[type]--;
	/* (stuck to the driver inside, at the marker: its blast the driver's
	own - its attached damage, or its area's at the seat's direct scale -
	not the hull's, whose transfer to the driver is a fraction) */
	{
		long occupant_index = unit_seat_occupant(vehicle_index, target_seat_index);

		if (occupant_index != NONE)
			projectile_attach_at(grenade_index, occupant_index, 0, &marker.matrix.position);
		else
			projectile_attach_at(grenade_index, vehicle_index, marker.node_index, &marker.matrix.position);
	}
	if (HALO_ENV_ONCE("HALO_H2_VEHICLE_LOG"))
		platform_log("h2 hull: %08lx drops a grenade in at '%s' of %08lx", (unsigned long)unit_index, h2vd_hull_grenade_marker(seat->label),
			(unsigned long)vehicle_index);
}

/* (units.c's units_boarding_update, the host's: each boarder seated on a
tank's hull, its board done) */
void h2_vehicle_hull_boarder(long unit_index, long vehicle_index, short seat_index, short target_seat_index)
{
	struct unit_datum *unit = unit_get(unit_index);
	long occupant_index = unit_seat_occupant(vehicle_index, target_seat_index);
	long now = game_time_get(), k, free_slot = NONE, graph_index;
	unsigned long pressed;

	for (k = 0; k < H2VD_MAXIMUM_HULL_BOARDERS; k++)
	{
		if (h2vd_boarders[k].scenario != global_scenario_index || h2vd_boarders[k].generation != halo_map_generation)
			h2vd_boarders[k].unit = NONE;
		if (h2vd_boarders[k].unit == unit_index)
			break;
		if (free_slot == NONE && (h2vd_boarders[k].unit == NONE || h2vd_boarders[k].unit == 0 || !object_try_and_get(h2vd_boarders[k].unit) ||
			object_get(h2vd_boarders[k].unit)->object.parent_object_index == NONE))
		{
			free_slot = k;
		}
	}
	if (k == H2VD_MAXIMUM_HULL_BOARDERS)
	{
		if (free_slot == NONE)
			return;
		k = free_slot;
		h2vd_boarders[k].unit = unit_index;
		h2vd_boarders[k].busy_until = now;
		h2vd_boarders[k].strike_at = NONE;
		h2vd_boarders[k].action = 0;
		h2vd_boarders[k].held = unit->unit.control_flags;
		h2vd_boarders[k].scenario = global_scenario_index;
		h2vd_boarders[k].generation = halo_map_generation;
	}
	/* (its blow or grenade lands) */
	if (h2vd_boarders[k].strike_at != NONE && now >= h2vd_boarders[k].strike_at)
	{
		h2vd_boarders[k].strike_at = NONE;
		if (h2vd_boarders[k].action == 0 && occupant_index != NONE)
		{
			struct unit_definition *vd = unit_definition_get(object_get(vehicle_index)->definition_index);

			h2vd_hull_blow(unit_index, occupant_index, VALID_INDEX(seat_index, vd->unit.seats.count) &&
				!strncmp(TAG_BLOCK_GET_ELEMENT(&vd->unit.seats, seat_index, struct unit_seat)->label, "throne_b", 8));
		}
		else if (h2vd_boarders[k].action == 1)
		{
			h2vd_hull_grenade(unit_index, vehicle_index, seat_index, target_seat_index);
			h2vd_boarders[k].action = 2;    /* (then off the hull, its throw done) */
		}
	}
	pressed = unit->unit.control_flags & ~h2vd_boarders[k].held;
	h2vd_boarders[k].held = unit->unit.control_flags;
	if (now < h2vd_boarders[k].busy_until)
		return;
	/* (the driver gone, or a grenade dropped in: off the hull) */
	if (occupant_index == NONE || object_get(occupant_index)->object.body_vitality <= 0.0f || h2vd_boarders[k].action == 2)
	{
		h2vd_boarders[k].unit = NONE;
		unit_try_and_exit_seat(unit_index);
		return;
	}
	graph_index = unit_definition_get(unit->definition_index)->object.animation_graph.index;
	if (TEST_FLAG(pressed, _unit_control_use_equipment_bit) || TEST_FLAG(pressed, _unit_control_throw_grenade_bit))
	{
		short action = TEST_FLAG(pressed, _unit_control_use_equipment_bit) ? 0 : 1;
		short frames = 0;
		char const *name;

		if (action == 1 && (unit->unit.current_grenade_index < 0 || unit->unit.current_grenade_index >= NUMBER_OF_UNIT_GRENADE_TYPES ||
			unit->unit.grenade_counts[unit->unit.current_grenade_index] <= 0))
		{
			return;
		}
		name = h2vd_seat_animation(unit, graph_index, action ? H2VD_SEAT_ANIMATION_THROW_GRENADE : H2VD_SEAT_ANIMATION_MELEE, &frames);
		if (name && !unit_start_user_animation(unit_index, graph_index, name, FALSE))
			name = NULL;
		if (frames < 2)
			frames = 30;
		h2vd_boarders[k].action = action;
		h2vd_boarders[k].busy_until = now + frames;
		h2vd_boarders[k].strike_at = now + frames / 2;
		if (HALO_ENV_ONCE("HALO_H2_VEHICLE_LOG"))
			platform_log("h2 hull: %08lx %s (%s, %d frames)", (unsigned long)unit_index, action ? "drops a grenade" : "beats on the hatch",
				name ? name : "no animation", (int)frames);
	}
}

/* ---------- tests */

#include "effects/effect_definitions.h"
#include "items/projectile_definitions.h"
#include "cache/cache_files.h"

/* (main.c's @vdmg ?) each projectile tag's damage: its impact's and its
detonation effect's damage parts (a vehicle's gun: Halo 2's impact and
detonation, none of the kit's on top) */
void h2_vehicle_damage_test_projectiles(void)
{
	struct tag_iterator tags;
	long tag_index;

	tag_iterator_new(&tags, PROJECTILE_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&tags)) != NONE)
	{
		struct projectile_definition *p = projectile_definition_get(tag_index);
		char parts[256] = "";
		long e, k;

		if (p->projectile.effect.index != NONE)
		{
			struct effect_definition *effect = tag_get('effe', p->projectile.effect.index);

			for (e = 0; e < effect->events.count; e++)
			{
				struct effect_event_definition *event = TAG_BLOCK_GET_ELEMENT(&effect->events, e, struct effect_event_definition);

				for (k = 0; k < event->parts.count; k++)
				{
					struct effect_part_definition *part = TAG_BLOCK_GET_ELEMENT(&event->parts, k, struct effect_part_definition);

					if (part->reference.group_tag == 'jpt!' && part->reference.index != NONE)
					{
						size_t n = strlen(parts);

						snprintf(parts + n, sizeof(parts) - n, " [%s]", tag_get_name(part->reference.index));
					}
				}
			}
		}
		platform_log("test command: projectile %s: impact %s, detonation %s:%s", tag_get_name(tag_index),
			p->projectile.impact_damage.index != NONE ? tag_get_name(p->projectile.impact_damage.index) : "(none)",
			p->projectile.effect.index != NONE ? tag_get_name(p->projectile.effect.index) : "(none)", parts);
	}
}

#include "physics/collisions.h"

/* (main.c's @vray name) rays straight down onto the first vehicle so named
(its model's frame: from 3 world units over it), a grid of 0.2 over
forward -2.4..2.4 and left -1.6..1.6: its collision model's footprint, a
line of the log a row ('#' it, '.' none; a node past the hull's its number) */
void h2_vehicle_damage_test_rays(char const *name)
{
	struct object_iterator iterator;
	struct object_datum *object;

	object_iterator_new(&iterator, _object_mask_vehicle, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		real x, y;

		if (!strstr(tag_get_name(object->definition_index), name))
			continue;
		platform_log("test command: vray %s %08lx (left +, its rows forward from -2.4)", tag_get_name(object->definition_index),
			(unsigned long)iterator.index);
		for (x = 2.4f; x >= -2.41f; x -= 0.2f)
		{
			char row[64];
			int n = 0;

			for (y = 1.6f; y >= -1.61f && n < 62; y -= 0.2f)
			{
				real_point3d local = { x, y, 3.0f }, from;
				real_vector3d down;
				struct collision_result result;

				h2vd_world(object, &local, &from);
				down.i = -3.0f * object->object.up.i * 2.0f;
				down.j = -3.0f * object->object.up.j * 2.0f;
				down.k = -3.0f * object->object.up.k * 2.0f;
				/* ('#' its hull's node, else the node's number: 1-9, then a-z) */
				row[n++] = collision_test_vector(FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_vehicles_bit) |
					FLAG(_collision_test_front_facing_surfaces_bit), &from, &down, NONE, &result) && result.object_index == iterator.index ?
					(result.node_index <= 0 ? '#' : result.node_index < 10 ? (char)('0' + result.node_index) :
					result.node_index < 36 ? (char)('a' + result.node_index - 10) : '+') : '.';
			}
			row[n] = 0;
			platform_log("vray %+5.1f %s", x, row);
		}
		break;
	}
}

/* (main.c's @vdrop name projectile forward left height [k]) a projectile
whose tag's name has `projectile` in it made at that place in the k-th
vehicle's frame (name in its tag's name), flying straight down at its own
speed (1 world unit a tick if it is made at rest): a real hit on the part
of the vehicle under that place - its impact and its detonation - and the
node of its collision model that took it (HALO_H2_DAMAGE_LOG, damage.c) */
void h2_vehicle_damage_test_drop(char const *name, char const *projectile_name, real forward, real left, real height, long nth)
{
	struct object_iterator iterator;
	struct object_datum *object;
	struct tag_iterator tags;
	long tag_index, definition_index = NONE, seen = 0;

	tag_iterator_new(&tags, 'proj');
	while ((tag_index = tag_iterator_next(&tags)) != NONE && definition_index == NONE)
	{
		if (strstr(tag_get_name(tag_index), projectile_name))
			definition_index = tag_index;
	}
	object_iterator_new(&iterator, _object_mask_vehicle, 0);
	while (definition_index != NONE && (object = object_iterator_next(&iterator)) != NULL)
	{
		struct object_placement_data placement;
		real_point3d local, at;
		long projectile_index;

		if (!strstr(tag_get_name(object->definition_index), name) || seen++ != nth)
			continue;
		local.x = forward;
		local.y = left;
		local.z = height;
		h2vd_world(object, &local, &at);
		object_placement_data_new(&placement, definition_index, NONE);
		placement.position = at;
		placement.forward.i = 0.0f;
		placement.forward.j = 0.0f;
		placement.forward.k = -1.0f;
		placement.up.i = 1.0f;
		placement.up.j = 0.0f;
		placement.up.k = 0.0f;
		projectile_index = object_new(&placement);
		if (projectile_index != NONE)
		{
			struct object_datum *projectile = object_get(projectile_index);
			real speed = magnitude3d(&projectile->object.translational_velocity);
			real_vector3d velocity, change;

			if (speed < 0.01f)
				speed = 1.0f;
			velocity.i = 0.0f;
			velocity.j = 0.0f;
			velocity.k = -speed;
			subtract_vectors3d(&velocity, &projectile->object.translational_velocity, &change);
			projectile_accelerate(projectile_index, &change);
		}
		platform_log("test command: vdrop %s at %s %08lx (forward %.2f left %.2f height %.2f; body %.3f of %.0f): %s", tag_get_name(definition_index),
			tag_get_name(object->definition_index), (unsigned long)iterator.index, forward, left, height, object->object.body_vitality,
			object->object.maximum_body_vitality, projectile_index != NONE ? "made" : "not made");
		return;
	}
	platform_log("test command: vdrop: no %s", definition_index == NONE ? "such projectile" : "such vehicle");
}
