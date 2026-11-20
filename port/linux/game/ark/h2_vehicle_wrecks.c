/*
H2_VEHICLE_WRECKS.C

port: Halo 2's damaged and destroyed vehicles on a Halo 2 level (the owner's
MCC frames: a hurt Warthog smokes; a dead one stays a wreck, burning -
yellow-orange flames over the engine, dark smoke over them - and the game
brings a new one back where it stood). This engine's vehicles play one
damaged effect and one destroyed effect (their collision model's,
h2_vehicles.c sets them), and the Xbox game never brings a vehicle back
(game_variant_options' vehicle_respawn_time: 0, never). Here, from the
units' update:
- a vehicle below half health smokes, a grey "smoke h generic animated"
  particle from its engine every H2_WRECK_SMOKE_PERIOD ticks (the damaged
  effect, once, was all);
- a dead one, a wreck, burns: two of the kit's "burning flame" particles over
  its engine every H2_WRECK_FIRE_PERIOD ticks and one dark "smoke h generic
  animated" particle every H2_WRECK_SOOT_PERIOD, for H2_WRECK_BURN_TICKS,
  then smoulders (the smoke alone) until
- H2_WRECK_RESPAWN_TICKS after it died, when (game_engine_update_purge, the
  host's, as the items it removes) the wreck goes and a new vehicle of its
  kind is placed where the first stood as the game began.
Particles only, the kit's (no tags of its own, nothing new drawn): a wreck's
are about 25 flames and 12 smoke puffs alive at once (each lives ~1-1.5 s), at
most H2_WRECKS_BURNING wrecks burn at once. HALO_H2_NO_WRECKS: none of it.
With Halo 2's effects made (h2_effects.c: the level has
h2\effects\objects\vehicles\warthog\looping_engine_smoke and ..._universal_smoke)
those instead of the kit's particles: Halo 2's own burning engine (fire,
fiery smoke, dark smoke) and hurt smoke, an effect every
H2_WRECK_EFFECT_PERIOD ticks (each emits for its event's 2 s) at the engine.
A Warthog whose hull is torn (h2_vehicle_damage.c's "major" hull, Halo 2's
hull state whose effect is looping_engine_smoke) burns before it blows up;
a Ghost never burns: its wreck gets Halo 2's destroyed hull sparks
(perm_hull_destroyed_damage) at its middle, and its antigravity dies with it.
A vehicle's own death explosion leaves its wreck alone (effects.c,
h2_vehicle_wreck_explosion_ignore). The tables belong to a game state: made
again when it is replaced (halo_map_generation), and a vehicle comes back
only where a level's own Halo 2 vehicle of its kind stood, in the map.
HALO_H2_WRECK_TRACE: a hurt or dead vehicle's motion logged.
*/

#include "cseries.h"
#include "h2_sound.h"
#include "effects/effects.h"
#include "effects/particles.h"
#include "game/game.h"
#include "networking/network_game_globals.h"
#include "objects/damage.h"
#include "objects/objects.h"
#include "scenario/scenario.h"
#include "tag_files/tag_groups.h"
#include "units/units.h"

#include "h2_jolt_hull.h"
#include "objects/damage_resistances.h"
#include "physics/collision_model_definitions.h"
#include "objects/object_definitions.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef HALO_LINUX
#include "render_epoch.h" /* HALO_ENV_ONCE */
#endif

/* ---------- constants */

enum
{
	H2_WRECKS_MAXIMUM = 64,          /* the vehicles a level's wrecks and spawns are kept for */
	H2_WRECK_DEBRIS = 8,             /* bits of metal a vehicle throws off as it blows up */
	H2_WRECKS_BURNING = 6,           /* wrecks burning at once, at most (the rest smoulder) */
	H2_WRECK_SMOKE_PERIOD = 6,       /* ticks between a hurt vehicle's smoke puffs */
	H2_WRECK_FIRE_PERIOD = 3,        /* ticks between a wreck's flames */
	H2_WRECK_SOOT_PERIOD = 5,        /* ticks between a wreck's smoke puffs */
	H2_WRECK_BURN_TICKS = 20 * 30,   /* a wreck's flames: 20 seconds */
	H2_WRECK_RESPAWN_TICKS = 30 * 30, /* a new vehicle: 30 seconds after one died */
	H2_WRECK_EFFECT_PERIOD = 60,      /* (Halo 2's effects) ticks between a wreck's burning effects: their event's length */
	H2_GHOST_WARMUP_TICKS = 8 * 30    /* (Halo 2's Ghost hlmt: its hull's destroyed response's delay, 8 s) */
};

/* ---------- globals */

struct h2_wreck
{
	long vehicle;
	long died;                      /* game time; NONE: alive (hurt) */
	long next_smoke, next_fire, next_soot;
	long warming;                   /* (a Ghost's warm-up) game time it began; NONE: none */
	boolean warm_shown;             /* (its delay effect started on this machine) */
};

struct h2_wreck_spawn
{
	long vehicle;
	long definition;                /* (its kind's: a new one of it, and only of it) */
	real_point3d position;
	real_vector3d forward, up;
};

/* (render_epoch.c) counted up each time the game state is replaced (a new
map, a revert, a network game's start: the objects' datum indices are used
again for new objects) */
extern unsigned long halo_map_generation;

static struct
{
	long scenario;                  /* (the level the particles were found in) */
	unsigned long generation;       /* (the game state the tables are of) */
	boolean halo2;
	long smoke, fire, soot, debris;
	long h2_burn, h2_smoke;         /* (Halo 2's effects, h2_effects.c; NONE: the kit's particles) */
	long h2_ghost_wreck;            /* (Halo 2's dead Ghost's sparks and smoke; NONE: smoke) */
	long h2_ghost_warmup;           /* (Halo 2's critical Ghost's warm-up: its delay effect; NONE: smoke) */
	long h2_ghost_explosion;        /* (Halo 2's Ghost's explosion, at its middle; NONE: its depleted effect's alone) */
	unsigned long seed;
	struct h2_wreck wrecks[H2_WRECKS_MAXIMUM];
	struct h2_wreck_spawn spawns[H2_WRECKS_MAXIMUM];
} h2_wrecks = { NONE };

/* ---------- private code */

static long h2_wrecks_tag(unsigned long group, char const *const *names)
{
	long i;

	for (i = 0; names[i]; i++)
	{
		long tag = tag_loaded(group, names[i]);

		if (tag != NONE)
			return tag;
	}
	return NONE;
}

static void h2_wrecks_level(void)
{
	static char const *const smoke[] = { "weapons\\frag grenade\\effects\\smoke", NULL };
	static char const *const fire[] = { "effects\\particles\\flash\\burning flame", NULL };
	static char const *const soot[] = { "effects\\particles\\air\\smoke h generic animated", "effects\\particles\\air\\smoke h generic animated2",
		"weapons\\frag grenade\\effects\\drifting smoke", NULL };
	static char const *const debris[] = { "effects\\particles\\solid\\metal chips", NULL };
	char const *name = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : "";
	long i;

	/* (the tables a game state's: a Halo 3 lobby scene's vehicles, made in
	the menus' game state as the level loads, have the datum indices the
	level's own vehicles get once its game state replaces the menus' - one
	recorded where the scene's Warthog stood, near the origin, brought the
	level's Warthog back there: "a new one e3ed0064 at -0.0 -0.0 0.0", the
	owner's Vita) */
	if (h2_wrecks.scenario == global_scenario_index && h2_wrecks.generation == halo_map_generation)
		return;
	h2_wrecks.scenario = global_scenario_index;
	h2_wrecks.generation = halo_map_generation;
	h2_wrecks.halo2 = name && !strncmp(name, "h2\\", 3) && !HALO_ENV_ONCE("HALO_H2_NO_WRECKS");
	h2_wrecks.smoke = h2_wrecks_tag('effe', smoke);
	h2_wrecks.fire = h2_wrecks_tag('part', fire);
	h2_wrecks.soot = h2_wrecks_tag('part', soot);
	h2_wrecks.debris = h2_wrecks_tag('part', debris);
	h2_wrecks.h2_burn = tag_loaded('effe', "h2\\effects\\objects\\vehicles\\warthog\\looping_engine_smoke");
	h2_wrecks.h2_smoke = tag_loaded('effe', "h2\\effects\\objects\\vehicles\\warthog\\looping_universal_smoke");
	h2_wrecks.h2_ghost_wreck = tag_loaded('effe', "h2\\effects\\objects\\vehicles\\ghost\\perm_hull_destroyed_damage");
	h2_wrecks.h2_ghost_explosion = tag_loaded('effe', "h2\\effects\\objects\\vehicles\\ghost\\hull_destroyed_damage");
	h2_wrecks.h2_ghost_warmup = tag_loaded('effe', "h2\\effects\\objects\\vehicles\\ghost\\hull_destroyed_initial_damage");
	for (i = 0; i < H2_WRECKS_MAXIMUM; i++)
		h2_wrecks.wrecks[i].vehicle = h2_wrecks.spawns[i].vehicle = NONE;
	if (h2_wrecks.halo2)
		platform_log("h2 wrecks: smoke %s, fire %s, soot %s", h2_wrecks.smoke != NONE ? tag_get_name(h2_wrecks.smoke) : "(none)",
			h2_wrecks.fire != NONE ? tag_get_name(h2_wrecks.fire) : "(none)", h2_wrecks.soot != NONE ? tag_get_name(h2_wrecks.soot) : "(none)");
}

/* the vehicle's slot of a table (vehicle the first field of its elements),
a free one given it when create; NONE */
static long h2_wrecks_slot(void *table, long element_size, long vehicle, boolean create)
{
	long i, free_slot = NONE;

	for (i = 0; i < H2_WRECKS_MAXIMUM; i++)
	{
		long *entry = (long *)((char *)table + element_size * i);

		if (*entry == vehicle)
			return i;
		if (free_slot == NONE && (*entry == NONE || !object_try_and_get(*entry)))
			free_slot = i;
	}
	if (create && free_slot != NONE)
		*(long *)((char *)table + element_size * free_slot) = vehicle;
	return create ? free_slot : NONE;
}

/* the hurt vehicle's smoke effect, at its middle, a little over it */
static void h2_wrecks_smoke(long object_index)
{
	struct object_datum *object = object_get(object_index);
	char const *names[1] = { "" };
	real_point3d point = object->object.bounding_sphere_center;
	real_vector3d up = { 0.0f, 0.0f, 1.0f };

	point.z += 0.3f;
	effect_new_unattached_from_markers(h2_wrecks.smoke, object_index, NULL, 1, names, &point, &up, 1.0f, 1.0f, NULL, NULL, FALSE);
}

/* a Ghost (Halo 2's hover vehicle: its own wreck - sparks and smoke, no
burning engine) */
static boolean h2_wrecks_is_ghost(long definition_index)
{
	return strstr(tag_get_name(definition_index), "ghost") != NULL;
}

/* (Halo 2's effects) one at the vehicle's engine (Halo 2's marker
"replace": the hood, facing the vehicle's way) */
static void h2_wrecks_effect(long definition, long object_index)
{
	struct object_datum *object = object_get(object_index);
	char const *names[1] = { "replace" };
	real_point3d point = object->object.position;
	real_vector3d forward = object->object.forward;
	struct object_marker marker;

	/* (a Ghost's dying and its explosion: on it, as Halo 2's response's are,
	their accelerations its and their smoke a trail behind it) */
	if (definition == h2_wrecks.h2_ghost_warmup || definition == h2_wrecks.h2_ghost_explosion)
	{
		effect_new_from_object(definition, object_index, object_index, NONE, 1.0f, 1.0f, NULL, NULL);
		return;
	}
	/* (a Ghost's - its torn hull's sparks -: its middle, however it lies) */
	if (definition == h2_wrecks.h2_ghost_wreck || h2_wrecks_is_ghost(object->definition_index))
		point = object->object.bounding_sphere_center;
	/* (Halo 2's: the hull's damaged states' looping effects - perm_minor_hull,
	perm_medium_hull, looping_engine_smoke - at the model's "engine" marker,
	the variant's permutation states' looping effect marker name) */
	else if (object_get_marker_by_name(object_index, "engine", &marker, 1) > 0 && !HALO_ENV_ONCE("HALO_H2_WRECK_FIRE_OLD"))
	{
		point = marker.matrix.position;
		forward = marker.matrix.forward;
	}
	else
	{
		point.x += 0.55f*forward.i + 0.35f*object->object.up.i;
		point.y += 0.55f*forward.j + 0.35f*object->object.up.j;
		point.z += 0.55f*forward.k + 0.35f*object->object.up.k;
	}
	if (HALO_ENV_ONCE("HALO_H2_WRECK_TRACE"))
		platform_log("h2 wrecks: %08lx %s at %.2f %.2f %.2f facing %.2f %.2f %.2f (its origin %.2f %.2f %.2f)", (unsigned long)object_index,
			tag_get_name(definition), point.x, point.y, point.z, forward.i, forward.j, forward.k,
			object->object.position.x, object->object.position.y, object->object.position.z);
	effect_new_unattached_from_markers(definition, object_index, NULL, 1, names, &point, &forward, 1.0f, 1.0f, NULL, NULL, FALSE);
}

/* 0..1, the wrecks' own (their particles are only drawn: nothing the game
state or the network sees) */
static real h2_wrecks_random(void)
{
	h2_wrecks.seed = h2_wrecks.seed * 1664525UL + 1013904223UL;
	return (real)((h2_wrecks.seed >> 8) & 0xFFFF) / 65535.0f;
}

/* a particle over the wreck's engine (forward_offset ahead of its origin, a
little over it), rising */
static void h2_wrecks_particle(long definition, struct object_datum *object, real forward_offset, real height, real radius, real rise,
	real_argb_color const *color)
{
	struct new_particle_data data;
	real along = forward_offset + 0.5f*(h2_wrecks_random() - 0.5f), across = 0.5f*(h2_wrecks_random() - 0.5f);
	real_vector3d const *forward = &object->object.forward, *up = &object->object.up;
	real_vector3d left;

	cross_product3d(up, forward, &left);
	csmemset(&data, 0, sizeof(data));
	data.definition_index = definition;
	data.object_index = NONE;
	data.node_index = NONE;
	data.local_player_index = NONE;
	data.position = object->object.position;
	data.position.x += forward->i*along + left.i*across + up->i*height;
	data.position.y += forward->j*along + left.j*across + up->j*height;
	data.position.z += forward->k*along + left.k*across + up->k*height;
	data.direction.k = 1.0f;
	data.velocity.i = 0.2f*(h2_wrecks_random() - 0.5f);
	data.velocity.j = 0.2f*(h2_wrecks_random() - 0.5f);
	data.velocity.k = rise*(0.7f + 0.6f*h2_wrecks_random());
	data.rotation = 6.2831853f*h2_wrecks_random();
	data.angular_velocity = h2_wrecks_random() - 0.5f;
	data.radius = radius*(0.75f + 0.5f*h2_wrecks_random());
	data.color = *color;
	particle_new(&data);
}

/* a bit of metal thrown up and out of the wreck */
static void h2_wrecks_debris(struct object_datum *object, real_argb_color const *color)
{
	struct new_particle_data data;
	real angle = 6.2831853f*h2_wrecks_random(), speed = 3.0f + 4.0f*h2_wrecks_random();

	csmemset(&data, 0, sizeof(data));
	data.definition_index = h2_wrecks.debris;
	data.object_index = NONE;
	data.node_index = NONE;
	data.local_player_index = NONE;
	data.position = object->object.bounding_sphere_center;
	data.direction.k = 1.0f;
	data.velocity.i = speed*0.6f*(real)cos(angle);
	data.velocity.j = speed*0.6f*(real)sin(angle);
	data.velocity.k = speed*(0.6f + 0.4f*h2_wrecks_random());
	data.rotation = 6.2831853f*h2_wrecks_random();
	data.angular_velocity = 4.0f*(h2_wrecks_random() - 0.5f);
	data.radius = 0.12f + 0.12f*h2_wrecks_random();
	data.color = *color;
	particle_new(&data);
}

/* a wreck's slot made new */
static void h2_wrecks_wreck_new(long slot, long now)
{
	h2_wrecks.wrecks[slot].died = NONE;
	h2_wrecks.wrecks[slot].next_smoke = h2_wrecks.wrecks[slot].next_fire = h2_wrecks.wrecks[slot].next_soot = now;
	h2_wrecks.wrecks[slot].warming = NONE;
	h2_wrecks.wrecks[slot].warm_shown = FALSE;
}

/* a critical Ghost's warm-up, ticks (HALO_H2_GHOST_WARMUP seconds, a
test's; 0 none: it blows up at once, as before) */
static long h2_wrecks_warmup_ticks(void)
{
	char const *setting = HALO_ENV_ONCE("HALO_H2_GHOST_WARMUP");

	return setting ? (long)(atof(setting) * 30.0 + 0.5) : H2_GHOST_WARMUP_TICKS;
}

/* ---------- public code */

/* (units.c, each unit's update) where a vehicle first stood; a hurt one's
smoke (a Warthog's engine burning once its hull is torn, Halo 2's "major"
hull: the owner's playtest, "the warthog doesn't catch fire before it
explodes"), a dead one's flames and smoke */
void h2_vehicle_wreck_update(long unit_index)
{
	struct object_datum *object = object_try_and_get(unit_index);
	long now = game_time_get(), slot, i, burning = 0;
	struct h2_wreck *wreck;
	boolean ghost, own_effects;
	real body;

	if (!object || object->object.type != _object_type_vehicle)
		return;
	h2_wrecks_level();
	if (!h2_wrecks.halo2)
		return;
	/* (a wreck being removed - h2_vehicle_wrecks_respawn, its slot given up
	as it goes -: no new wreck of it. It was taken for a new death, its push
	and its explosion's effect again where it lay as it vanished) */
	if (TEST_FLAG(object_header_get(unit_index)->flags, _object_header_being_deleted_bit))
		return;
	/* (a level's own vehicle, Halo 2's - not a menu scene's, held still -
	where it stood as it was first seen) */
	if (h2_wrecks_slot(h2_wrecks.spawns, sizeof(struct h2_wreck_spawn), unit_index, FALSE) == NONE &&
		!strncmp(tag_get_name(object->definition_index), "h2\\vehicles\\", 12))
	{
		boolean menu_scene_objects_frozen(long object_index, real *suspension);
		real suspension;

		/* (and one standing somewhere: a vehicle at the very origin is
		one not placed yet - asked again at its next update) */
		if (!menu_scene_objects_frozen(unit_index, &suspension) &&
			(fabs(object->object.position.x) > 0.5f || fabs(object->object.position.y) > 0.5f ||
				fabs(object->object.position.z) > 0.5f) &&
			(slot = h2_wrecks_slot(h2_wrecks.spawns, sizeof(struct h2_wreck_spawn), unit_index, TRUE)) != NONE)
		{
			h2_wrecks.spawns[slot].definition = object->definition_index;
			h2_wrecks.spawns[slot].position = object->object.position;
			h2_wrecks.spawns[slot].forward = object->object.forward;
			h2_wrecks.spawns[slot].up = object->object.up;
		}
	}
	body = object->object.body_vitality;
	ghost = h2_wrecks_is_ghost(object->definition_index);
	/* (Halo 2's effects made: a vehicle neither a Warthog nor a Ghost - the
	Banshee, the Scorpion, the Wraith, the Spectre - smokes and burns with
	its own model's damage states' looping effects alone, h2_vehicle_damage.c
	(the Banshee's perm_hull_destroyed_damage...), never the Warthog's
	burning engine and wheel smoke, which a dead Banshee wore here) */
	own_effects = !ghost && h2_wrecks.h2_burn != NONE && !strstr(tag_get_name(object->definition_index), "warthog") &&
		!HALO_ENV_ONCE("HALO_H2_WRECK_ANY_BURNS");
	/* (HALO_H2_WRECK_TRACE: a hurt or dead vehicle's motion every 3 ticks) */
	if (body < 0.5f && HALO_ENV_ONCE("HALO_H2_WRECK_TRACE") && (now % 3) == 0)
	{
		platform_log("h2 wrecks: trace %08lx t%ld body %.3f at %.2f %.2f %.2f v %.3f %.3f %.3f w %.3f %.3f %.3f up %.2f %.2f %.2f power %.2f flags %04x",
			(unsigned long)unit_index, now, body, object->object.position.x, object->object.position.y, object->object.position.z,
			object->object.translational_velocity.i, object->object.translational_velocity.j, object->object.translational_velocity.k,
			object->object.angular_velocity.i, object->object.angular_velocity.j, object->object.angular_velocity.k,
			object->object.up.i, object->object.up.j, object->object.up.k, ((struct unit_datum *)object)->unit.seat_power[0],
			(unsigned)object->object.damage_flags);
	}
	/* (a dead Ghost falls: Halo 2's antigravity dies with it. This engine's
	powers a dead vehicle's seat down over its powerdown time, a Ghost's
	antigravity with it - a wreck thrown up by its explosion half held up,
	tumbling and swinging on its failing hover: "kinda whips around like the
	gravity device is shorting out", the owner's playtest) */
	if (ghost && body <= 0.0f && TEST_FLAG(object->object.damage_flags, _object_dead_bit))
	{
		struct unit_datum *unit = (struct unit_datum *)object;

		for (i = 0; i < (long)NUMBEROF(unit->unit.seat_power); i++)
			unit->unit.seat_power[i] = 0.0f;
	}
	if (body >= 0.5f)
		return;
	if ((slot = h2_wrecks_slot(h2_wrecks.wrecks, sizeof(struct h2_wreck), unit_index, FALSE)) == NONE)
	{
		if ((slot = h2_wrecks_slot(h2_wrecks.wrecks, sizeof(struct h2_wreck), unit_index, TRUE)) == NONE)
			return;
		h2_wrecks_wreck_new(slot, now);
	}
	wreck = &h2_wrecks.wrecks[slot];
	for (i = 0; i < H2_WRECKS_MAXIMUM; i++)
	{
		if (i != slot && h2_wrecks.wrecks[i].vehicle != NONE && h2_wrecks.wrecks[i].died != NONE &&
			now - h2_wrecks.wrecks[i].died < H2_WRECK_BURN_TICKS && (wreck->died == NONE || h2_wrecks.wrecks[i].died <= wreck->died))
		{
			burning++;
		}
	}
	/* (a Ghost warming up - critically damaged, its body held at none, not
	yet dead: h2_vehicle_warmup_hold -: Halo 2's delay effect, the whine,
	sparks, electric discharge and fiery smoke, once over its 8 s, smoke
	besides; then, the machine that runs the game's, its explosion) */
	if (ghost && body <= 0.0f && !TEST_FLAG(object->object.damage_flags, _object_dead_bit))
	{
		if (wreck->warming == NONE)
			wreck->warming = now;
		if (!wreck->warm_shown)
		{
			wreck->warm_shown = TRUE;
			if (h2_wrecks.h2_ghost_warmup != NONE)
				h2_wrecks_effect(h2_wrecks.h2_ghost_warmup, unit_index);
			/* (the effect carries no sound of its own here: Halo 2's whine
			of the delay, vehicles/ghost/ghost_initial_destroyed) */
			h2_sound_play_at("vehicles\\ghost\\ghost_initial_destroyed", &object->object.position);
		}
		if (now >= wreck->next_smoke)
		{
			if (h2_wrecks.h2_smoke != NONE)
				h2_wrecks_effect(h2_wrecks.h2_smoke, unit_index);
			wreck->next_smoke = now + H2_WRECK_EFFECT_PERIOD;
		}
		if (!network_game_distributed_client() && now - wreck->warming >= h2_wrecks_warmup_ticks())
		{
			/* (its time up: the explosion - the body depleted, this
			engine's death and its depleted effect, Halo 2's
			hull_destroyed_damage; the death's model responses) */
			extern void h2_vehicle_damage_body(long vehicle_index, real damage, real_point3d const *point, boolean area, long owner_player_index);

			if (HALO_ENV_ONCE("HALO_H2_WRECK_TRACE"))
				platform_log("h2 wrecks: %08lx warm-up over after %ld ticks: it blows up", (unsigned long)unit_index, now - wreck->warming);
			object->object.body_vitality = -0.01f;
			h2_vehicle_damage_body(unit_index, 0.0f, &object->object.position, FALSE, NONE);
			object_deplete_body(unit_index);
		}
		return;
	}
	/* (hurt: its smoke again and again; a Warthog whose hull is torn - its
	"major" look, Halo 2's hull state whose effect is the burning engine,
	looping_engine_smoke at its engine - burns, as a wreck does, before it
	blows up) */
	if (body > 0.0f)
	{
		if (own_effects)
			return;
		if (!ghost && h2_wrecks.h2_burn != NONE && burning < H2_WRECKS_BURNING)
		{
			extern long h2_vehicle_damage_state(long vehicle_index, char const *region_name);
			long hull = h2_vehicle_damage_state(unit_index, "hull");

			if (hull >= 3 || (hull < 0 && body < 0.25f))
			{
				if (now >= wreck->next_fire)
				{
					h2_wrecks_effect(h2_wrecks.h2_burn, unit_index);
					wreck->next_fire = now + H2_WRECK_EFFECT_PERIOD;
				}
				return;
			}
		}
		if (now >= wreck->next_smoke)
		{
			static real_argb_color const grey = { 0.8f, 0.3f, 0.29f, 0.28f };

			/* (Halo 2's hurt smoke, its effect a while; else grey smoke
			from the engine, the kit's smoke particle; else the frag grenade's
			smoke effect) */
			if (h2_wrecks.h2_smoke != NONE)
			{
				h2_wrecks_effect(h2_wrecks.h2_smoke, unit_index);
				wreck->next_smoke = now + H2_WRECK_EFFECT_PERIOD;
				return;
			}
			if (h2_wrecks.soot != NONE)
				h2_wrecks_particle(h2_wrecks.soot, object, 0.5f, 0.6f, 0.9f, 1.0f, &grey);
			else if (h2_wrecks.smoke != NONE)
				h2_wrecks_smoke(unit_index);
			wreck->next_smoke = now + H2_WRECK_SMOKE_PERIOD;
		}
		return;
	}
	/* (dead - a wreck -: flames over its engine, a few small flame
	particles every few ticks, while it burns and the wrecks burning are
	few, and dark smoke rising from them) */
	if (wreck->died == NONE)
	{
		wreck->died = now;
		/* (Halo 2's death push: its destroyed response's transition effect's
		acceleration - the Warthog jumps from its explosion, its tail kicked
		up; h2_vehicle_physics.c) */
		if (!network_game_distributed_client())
		{
			extern boolean h2_vehicle_physics_death_push(long vehicle_index);

			h2_vehicle_physics_death_push(unit_index);
		}
		/* (the first burning effect of a Warthog that burned before it blew up
		a while from now, as the one burning goes on; a Ghost's sparks after
		its explosion) */
		if (wreck->next_fire < now + (ghost ? H2_WRECK_EFFECT_PERIOD / 2 : 0))
			wreck->next_fire = now + (ghost ? H2_WRECK_EFFECT_PERIOD / 2 : 0);
		/* (a Ghost's explosion's particles: its middle) */
		if (ghost && h2_wrecks.h2_ghost_explosion != NONE)
			h2_wrecks_effect(h2_wrecks.h2_ghost_explosion, unit_index);
		/* (as it blows up, a few bits of metal thrown off it: Halo 2's
		explosion has its own) */
		if (h2_wrecks.debris != NONE && h2_wrecks.h2_burn == NONE)
		{
			static real_argb_color const metal = { 1.0f, 0.35f, 0.37f, 0.3f };

			for (i = 0; i < H2_WRECK_DEBRIS; i++)
				h2_wrecks_debris(object, &metal);
		}
	}
	if (own_effects)
		return;
	/* (a dead Ghost: Halo 2's destroyed hull - sparks and smoke at its torn
	shell - again and again while it burns, then the smoke; never the
	Warthog's burning engine, which on a Ghost went off over it as a second
	explosion a moment after its own: the owner's playtest) */
	if (ghost && h2_wrecks.h2_burn != NONE)
	{
		if (now >= wreck->next_fire)
		{
			boolean sparks = now - wreck->died < H2_WRECK_BURN_TICKS && burning < H2_WRECKS_BURNING && h2_wrecks.h2_ghost_wreck != NONE;

			if (sparks || h2_wrecks.h2_smoke != NONE)
				h2_wrecks_effect(sparks ? h2_wrecks.h2_ghost_wreck : h2_wrecks.h2_smoke, unit_index);
			wreck->next_fire = now + 2 * H2_WRECK_EFFECT_PERIOD;
		}
		return;
	}
	/* (Halo 2's burning engine, its effect again and again; then its smoke) */
	if (h2_wrecks.h2_burn != NONE)
	{
		if (now >= wreck->next_fire)
		{
			boolean flames = now - wreck->died < H2_WRECK_BURN_TICKS && burning < H2_WRECKS_BURNING;

			if (flames || h2_wrecks.h2_smoke != NONE)
				h2_wrecks_effect(flames ? h2_wrecks.h2_burn : h2_wrecks.h2_smoke, unit_index);
			wreck->next_fire = now + (flames ? H2_WRECK_EFFECT_PERIOD : 2 * H2_WRECK_EFFECT_PERIOD);
		}
		return;
	}
	if (!ghost && h2_wrecks.fire != NONE && now >= wreck->next_fire && now - wreck->died < H2_WRECK_BURN_TICKS && burning < H2_WRECKS_BURNING)
	{
		static real_argb_color const flame = { 1.0f, 1.0f, 0.8f, 0.45f };

		h2_wrecks_particle(h2_wrecks.fire, object, 0.65f, 0.3f, 0.35f, 0.45f, &flame);
		h2_wrecks_particle(h2_wrecks.fire, object, 0.4f, 0.35f, 0.25f, 0.7f, &flame);
		wreck->next_fire = now + H2_WRECK_FIRE_PERIOD;
	}
	if (h2_wrecks.soot != NONE && now >= wreck->next_soot)
	{
		static real_argb_color const soot = { 1.0f, 0.05f, 0.045f, 0.04f };

		h2_wrecks_particle(h2_wrecks.soot, object, 0.5f, 0.9f, 1.5f, 1.3f, &soot);
		if (now - wreck->died < H2_WRECK_BURN_TICKS)
			h2_wrecks_particle(h2_wrecks.soot, object, 0.3f, 1.4f, 1.2f, 1.6f, &soot);
		if (HALO_ENV_ONCE("HALO_H2_DEBUG") && (now % 150) < H2_WRECK_SOOT_PERIOD)
			platform_log("h2 wrecks: %08lx dead %ld ticks, particles alive %d (of %d)", (unsigned long)unit_index, now - wreck->died,
				(int)particle_data->actual_count, (int)particle_data->maximum_count);
		/* (smouldering, once the flames are out: less of it) */
		wreck->next_soot = now + (now - wreck->died < H2_WRECK_BURN_TICKS ? H2_WRECK_SOOT_PERIOD : 3 * H2_WRECK_SOOT_PERIOD);
	}
}

/* (game_engine_update_purge, not on a client of the distributed netcode)
each wreck that has had its time removed, a new vehicle of its kind where the
first stood */
/* (h2_rules.c: a vehicle left empty away from home, replaced by a new one
there) the new one's home the old one's */
void h2_vehicle_wrecks_replaced(long old_vehicle, long new_vehicle)
{
	long spawn;

	if (!h2_wrecks.halo2 || old_vehicle == NONE)
		return;
	spawn = h2_wrecks_slot(h2_wrecks.spawns, sizeof(struct h2_wreck_spawn), old_vehicle, FALSE);
	if (spawn != NONE)
		h2_wrecks.spawns[spawn].vehicle = new_vehicle;
}

void h2_vehicle_wrecks_respawn(void)
{
	long now = game_time_get(), i;

	if (!h2_wrecks.halo2 || h2_wrecks.scenario != global_scenario_index || h2_wrecks.generation != halo_map_generation ||
		network_game_distributed_client())
	{
		return;
	}
	for (i = 0; i < H2_WRECKS_MAXIMUM; i++)
	{
		struct h2_wreck *wreck = &h2_wrecks.wrecks[i];
		struct object_datum *object;
		long spawn, vehicle;

		if (wreck->vehicle == NONE || wreck->died == NONE)
			continue;
		/* (its placement's Halo 2 spawn time, h2_rules.c - Zanzibar's are
		all 30 s, Coagulation's Warthogs 75 s -, else 30 s, Halo 2's default
		(upstream halo2-decompiled unknown_157450.cpp function_15ac50)) */
		{
			extern long h2_rules_vehicle_respawn_ticks(long definition_index, real_point3d const *home, long ticks);
			struct object_datum *dead = object_try_and_get(wreck->vehicle);
			long home = dead ? h2_wrecks_slot(h2_wrecks.spawns, sizeof(struct h2_wreck_spawn), wreck->vehicle, FALSE) : NONE;
			long ticks = home != NONE ? h2_rules_vehicle_respawn_ticks(dead->definition_index,
				&h2_wrecks.spawns[home].position, H2_WRECK_RESPAWN_TICKS) : H2_WRECK_RESPAWN_TICKS;

			if (now - wreck->died < ticks)
				continue;
			/* (Halo 2's rule as its timer runs out, upstream halo2-decompiled
			unknown_157450.cpp function_15ae70: a vehicle to be destroyed for
			the new one is left while anyone sits in it or a player stands
			near it, the timer tried again a second later - h2_rules.c) */
			{
				extern boolean h2_rules_vehicle_respawn_held(long vehicle_index);

				if (dead && h2_rules_vehicle_respawn_held(wreck->vehicle))
					continue;
			}
		}
		vehicle = wreck->vehicle;
		wreck->vehicle = NONE;
		if (!(object = object_try_and_get(vehicle)) || object->object.body_vitality > 0.0f)
			continue;
		spawn = h2_wrecks_slot(h2_wrecks.spawns, sizeof(struct h2_wreck_spawn), vehicle, FALSE);
		/* (its own place, of its own kind, in the level's structure: else
		none comes back - better none than one in a wall or under the map) */
		if (spawn != NONE)
		{
			struct location location;

			scenario_location_from_point(&location, &h2_wrecks.spawns[spawn].position);
			if (h2_wrecks.spawns[spawn].definition != object->definition_index || location.cluster_index == NONE)
			{
				platform_log("h2 wrecks: %s %08lx gone; its place (%.1f %.1f %.1f) is not one, none comes back", tag_get_name(object->definition_index),
					(unsigned long)vehicle, h2_wrecks.spawns[spawn].position.x, h2_wrecks.spawns[spawn].position.y, h2_wrecks.spawns[spawn].position.z);
				h2_wrecks.spawns[spawn].vehicle = NONE;
				spawn = NONE;
			}
		}
		if (spawn != NONE)
		{
			struct object_placement_data data;
			long made;

			object_placement_data_new(&data, object->definition_index, NONE);
			data.position = h2_wrecks.spawns[spawn].position;
			data.forward = h2_wrecks.spawns[spawn].forward;
			data.up = h2_wrecks.spawns[spawn].up;
			made = object_new(&data);
			/* (the new one's place where the first stood, not where it
			first stands: it is placed, then settles) */
			h2_wrecks.spawns[spawn].vehicle = made;
			platform_log("h2 wrecks: %s %08lx gone, a new one %08lx at %.1f %.1f %.1f", tag_get_name(object->definition_index),
				(unsigned long)vehicle, (unsigned long)made, data.position.x, data.position.y, data.position.z);
		}
		object_delete(vehicle);
	}
}

/* (effects.c, an effect's damage part) the object its area damage leaves
alone: a Halo 2 level's dead vehicle's own death explosion (its collision
model's body depleted effect, Halo 2's explosion over the stand-in's damage)
does not throw its own wreck - this engine's area damage pushed it as hard
as a grenade at its middle: a Warthog's wreck thrown metres away, a Ghost's
tossed up and tumbling (Halo 2's wrecks stay where they died, the owner's
MCC frames); NONE: none */
long h2_vehicle_wreck_explosion_ignore(long owner_object_index)
{
	struct object_datum *object;

	if (owner_object_index == NONE || !h2_wrecks.halo2 || h2_wrecks.scenario != global_scenario_index ||
		!(object = object_try_and_get(owner_object_index)) || object->object.type != _object_type_vehicle ||
		!TEST_FLAG(object->object.damage_flags, _object_dead_bit) || HALO_ENV_ONCE("HALO_H2_WRECK_SELF_PUSH"))
	{
		return NONE;
	}
	return owner_object_index;
}

/* a Halo 2 effect's acceleration (effects.c) on the vehicle it is on, each tick
of the event: a direction between the cone's angles from the location's forward,
at the location, by its acceleration scale (unknown_175bd0.cpp's function_179fb0).
The Ghost's hull_destroyed_initial_damage holds them, at accel_left and
accel_right, up its wings: a little for its first 6 s, then 0.4 for 1.75 s - it
lifts and tumbles. The size is the tag test's Ghost's: it rose 1.2 wu in those
1.75 s, 0.8 a tick being about 16 wu/s/s there */
void h2_vehicle_effect_push(long object_index, real_point3d const *point, real_vector3d const *forward,
	real acceleration, real inner_cone, real outer_cone)
{
	struct object_datum *object = object_try_and_get(object_index);
	real const degrees = 0.017453292f;
	real angle, spin, s;
	real_vector3d side, up, direction, velocity, arm, turn;

	if (!object || object->object.type != _object_type_vehicle || network_game_distributed_client())
		return;
	h2_wrecks_level();
	if (!h2_wrecks.halo2)
		return;
	/* (a direction in the cone) */
	angle = (inner_cone + (outer_cone - inner_cone) * (real)rand() / (real)RAND_MAX) * degrees;
	spin = 6.2831855f * (real)rand() / (real)RAND_MAX;
	s = (real)sin(angle);
	side.i = (real)fabs(forward->k) < 0.9f ? -forward->j : 0.0f;
	side.j = (real)fabs(forward->k) < 0.9f ? forward->i : -forward->k;
	side.k = (real)fabs(forward->k) < 0.9f ? 0.0f : forward->j;
	normalize3d(&side);
	cross_product3d(forward, &side, &up);
	direction.i = forward->i * (real)cos(angle) + (side.i * (real)cos(spin) + up.i * (real)sin(spin)) * s;
	direction.j = forward->j * (real)cos(angle) + (side.j * (real)cos(spin) + up.j * (real)sin(spin)) * s;
	direction.k = forward->k * (real)cos(angle) + (side.k * (real)cos(spin) + up.k * (real)sin(spin)) * s;
	scale_vector3d(&direction, acceleration * object_definition_get(object->definition_index)->object.acceleration_scale * 20.0f, &velocity);
	/* (its turning, about its middle, as a ball of 0.4 wu) */
	vector_from_points3d(&object->object.position, point, &arm);
	cross_product3d(&arm, &velocity, &turn);
	scale_vector3d(&turn, 1.0f / (0.4f * 0.4f), &turn);
	object->object.flags &= ~FLAG(_object_at_rest_bit);
	if (h2_jolt_hull_of(object_index))
		h2_jolt_hull_kick(object_index, &velocity, &turn);
	else
	{
		object->object.translational_velocity.i += velocity.i / (30.0f * 30.0f);
		object->object.translational_velocity.j += velocity.j / (30.0f * 30.0f);
		object->object.translational_velocity.k += velocity.k / (30.0f * 30.0f);
		object->object.angular_velocity.i += turn.i / (30.0f * 30.0f);
		object->object.angular_velocity.j += turn.j / (30.0f * 30.0f);
		object->object.angular_velocity.k += turn.k / (30.0f * 30.0f);
	}
}

/* (damage.c's object_damage_body, a vehicle's body just hurt) Halo 2's
critical Ghost: its hull section's destroyed response waits 8 s (the
hlmt's response delay), its delay effect - hull_destroyed_initial_damage:
the whine, sparks, electric discharge, fiery smoke - playing, its driver
thrown out (the response's ejecting seat, ghost_d), and only then fires,
the explosion (hull_destroyed_damage, med_trans_damage) - "we need to pull
more of the ghost logic on when it does its warm-up explosion", the owner.
Here a Ghost whose body would go below none is held at none, alive (this
engine's death is a body below none), its riders put out, and its time
counted (h2_vehicle_wreck_update, which blows it up when it is over, on the
machine that runs the game); a blow meanwhile does not hurry it (Halo 2's
response has no threshold for that: see below). TRUE: held */
boolean h2_vehicle_warmup_begin(long vehicle_index, real damage);

/* (h2_vehicle_damage.c) a client's Ghost warming up by the host's body, and now dead */
boolean h2_vehicle_warming_on_client(long vehicle_index)
{
	struct object_datum *object = object_try_and_get(vehicle_index);
	long slot;

	if (!object || !network_game_distributed_client() || !TEST_FLAG(object->object.damage_flags, _object_dead_bit))
		return FALSE;
	h2_wrecks_level();
	slot = h2_wrecks.halo2 ? h2_wrecks_slot(h2_wrecks.wrecks, sizeof(struct h2_wreck), vehicle_index, FALSE) : NONE;
	return slot != NONE && h2_wrecks.wrecks[slot].warming != NONE;
}

boolean h2_vehicle_warmup_hold(long vehicle_index, real damage)
{
	struct object_datum *object = object_try_and_get(vehicle_index);

	if (object && HALO_ENV_ONCE("HALO_H2_WRECK_TRACE") && h2_wrecks_is_ghost(object->definition_index))
		platform_log("h2 wrecks: %08lx hold asked: body %.3f damage %.3f dead %d", (unsigned long)vehicle_index, object->object.body_vitality,
			damage, (int)TEST_FLAG(object->object.damage_flags, _object_dead_bit));
	if (!object || object->object.body_vitality >= 0.0f)
		return FALSE;
	return h2_vehicle_warmup_begin(vehicle_index, damage);
}

/* (h2_vehicle_damage.c, the Ghost's hull section's destroyed response - its
8 s "response delay" - firing; damage.c through h2_vehicle_warmup_hold) the
Ghost starts warming up, its body held at none whatever it was: Halo 2's
delayed response is the section's, fired when the section is destroyed
(a grenade's blow destroys it at once) - the body's own death is not needed
first. TRUE: warming up (held) */
boolean h2_vehicle_warmup_begin(long vehicle_index, real damage)
{
	struct object_datum *object = object_try_and_get(vehicle_index);
	long slot, now = game_time_get();

	/* (a client's Ghost, its warm-up shown and now dead by the host's word: the
	explosion is that death's, as on the host, not this response's too) */
	if (object && network_game_distributed_client() && TEST_FLAG(object->object.damage_flags, _object_dead_bit))
	{
		h2_wrecks_level();
		slot = h2_wrecks.halo2 ? h2_wrecks_slot(h2_wrecks.wrecks, sizeof(struct h2_wreck), vehicle_index, FALSE) : NONE;
		if (slot != NONE && h2_wrecks.wrecks[slot].warming != NONE)
			return TRUE;
	}
	if (!object || object->object.type != _object_type_vehicle ||
		TEST_FLAG(object->object.damage_flags, _object_dead_bit) || h2_wrecks_warmup_ticks() <= 0)
	{
		if (object && HALO_ENV_ONCE("HALO_H2_WRECK_TRACE"))
			platform_log("h2 wrecks: %08lx no warm-up: %s", (unsigned long)vehicle_index,
				TEST_FLAG(object->object.damage_flags, _object_dead_bit) ? "dead already" : "no vehicle or no warm-up time");
		return FALSE;
	}
	h2_wrecks_level();
	if (!h2_wrecks.halo2 || !h2_wrecks_is_ghost(object->definition_index))
		return FALSE;
	if ((slot = h2_wrecks_slot(h2_wrecks.wrecks, sizeof(struct h2_wreck), vehicle_index, FALSE)) == NONE)
	{
		if ((slot = h2_wrecks_slot(h2_wrecks.wrecks, sizeof(struct h2_wreck), vehicle_index, TRUE)) == NONE)
		{
			if (HALO_ENV_ONCE("HALO_H2_WRECK_TRACE"))
				platform_log("h2 wrecks: %08lx no warm-up: no wreck slot", (unsigned long)vehicle_index);
			return FALSE;
		}
		h2_wrecks_wreck_new(slot, now);
	}
	if (h2_wrecks.wrecks[slot].warming == NONE)
	{
		long rider = object->object.first_child_object_index;

		h2_wrecks.wrecks[slot].warming = now;
		/* (its riders out: Halo 2 ejects the driver as it starts) */
		while (rider != NONE)
		{
			struct object_datum *child = object_get(rider);
			long next = child->object.next_object_index;

			if (child->object.type == _object_type_biped && ((struct unit_datum *)child)->unit.parent_seat_index != NONE &&
				!network_game_distributed_client())
				unit_try_and_exit_seat(rider);
			rider = next;
		}
		if (HALO_ENV_ONCE("HALO_H2_WRECK_TRACE"))
			platform_log("h2 wrecks: %08lx critical: warming up %ld ticks", (unsigned long)vehicle_index, h2_wrecks_warmup_ticks());
	}
	/* (a blow while it warms up - a grenade's, a rocket's, half its body at
	once - does not hurry it: Halo 2's response has no such rule - the
	Ghost's hull destroyed response (hlmt new damage info, its "response
	delay" 8 s, "delay effect" hull_destroyed_initial_damage) carries damage
	threshold 0, skip fraction 0, total damage threshold 0 and the one flag
	"only NOT on special death"; a Ghost stuck with a plasma grenade whines
	its 8 s and then explodes. This port blew it up at once on a blow of half
	its body: the owner's "the Ghost doesn't do its second explosion when
	blown up" - most Ghosts die of a grenade or a heavy gun) */
	(void)damage;
	object->object.body_vitality = 0.0f;
	return TRUE;
}

/* a Halo 2 pane broken (breakable_surfaces.c), at its centre: globals\\glass's
break - its sound, and shards at the pane's middle and about it. Halo 2's
breakable surface emits its glass_death particles over the pane; these are the
level's glass effects, by name (h2_effects.c) */
void h2_breakable_broke(real_point3d const *centre, real radius)
{
	static char const *const names[] = { "h2\\effects\\impact\\impact_bullet_large\\brittle_glass", "h2\\effects\\generic\\solids\\glass_break_death" };
	char const *locations[1] = { "" };
	int n, k;

	h2_sound_play_at("materials\\brittle\\glass\\glass_break_large", centre);
	for (n = 0; n < 2; n++)
	{
		long definition = tag_loaded('effe', names[n]);


		for (k = 0; definition != NONE && k < 5; k++)
		{
			real_point3d point = *centre;
			real_vector3d down = { 0.0f, 0.0f, -1.0f };

			if (k)
			{
				point.x += real_random_range(-0.5f, 0.5f) * radius;
				point.y += real_random_range(-0.5f, 0.5f) * radius;
				point.z += real_random_range(-0.5f, 0.5f) * radius;
			}
			effect_new_unattached_from_markers(definition, NONE, NULL, 1, locations, &point, &down, 1.0f, 1.0f, NULL, NULL, FALSE);
		}
	}
}

/* a material effect (a footstep, a landing, a vehicle's rolling) on water, a
Halo 2 level's: Halo 2's own - the biped's material effects' liquid entries
(masterchief's in the map's tag table: effects\\contact, the chief's footstep
sounds; deep "liquid", shallow liquid_thin_water_shallow, event by event); TRUE
if played. The stand-in's tag has none in the shallow slot */
boolean h2_water_contact(short event, boolean shallow, real_point3d const *point, real_vector3d const *normal, real scale)
{
	static char const *const effects[] = { "walk\\footsteps_water", "run\\footsteps_water", "run\\footsteps_water", "run\\footsteps_water",
		"run\\footsteps_water", "jump_land\\water_land" };
	static char const *const sounds[] = { "run", "run", "run", "shuffle", "jump", "land" };
	char name[128];
	char const *locations[1] = { "" };
	long definition;

	if (event == 9 || event == 10)
		snprintf(name, sizeof(name), "h2\\effects\\contact\\rolling\\vehicle_water");
	else if (event >= 0 && event < (short)NUMBEROF(effects))
		snprintf(name, sizeof(name), "h2\\effects\\contact\\%s", effects[event]);
	else
		return FALSE;
	definition = tag_loaded('effe', name);
	if (definition == NONE)
		return FALSE;
	effect_new_unattached_from_markers(definition, NONE, NULL, 1, locations, point, normal, scale, 0.0f, NULL, NULL, FALSE);
	if (event < (short)NUMBEROF(sounds))
	{
		char sound[128];
		boolean played;

		snprintf(sound, sizeof(sound), "characters\\footsteps\\chief\\%s\\%s", shallow ? "water_knee_deep" : "water_shallow", sounds[event]);
		played = h2_sound_play_at(sound, point);
		if (HALO_ENV_ONCE("HALO_H2_HIT_LOG"))
		{
			extern boolean h2_sound_exists(char const *h2_name);

			platform_log("water contact: event %d %s, sound %s exists %d played %d", (int)event, shallow ? "shallow" : "deep", sound,
				(int)h2_sound_exists(sound), (int)played);
		}
	}
	else if (HALO_ENV_ONCE("HALO_H2_HIT_LOG"))
		platform_log("water contact: event %d, effect %s", (int)event, name);
	return TRUE;
}


/* ---------- a Halo 2 level's explosive objects' stages */

enum
{
	H2_EXPLOSIVES_MAXIMUM = 48,
	H2_EXPLOSIVE_EFFECT_PERIOD = 60    /* (ticks: fusion_coil_major's 2 s event, again and again while it lasts) */
};

static struct
{
	long scenario;
	short count;
	struct
	{
		long object;
		long blast_at;       /* (NONE: no fuse lit) */
		long next_effect;
		long effect;
		unsigned char stages;
	} slots[H2_EXPLOSIVES_MAXIMUM];
} h2_explosives = { NONE };
/* (halo_map_generation when the slots were begun) */
static unsigned long h2_explosives_generation;

static struct h2_explosive_port const *h2_explosive_port_of(struct object_datum const *object)
{
	long collision = object_definition_get(object->definition_index)->object.collision_model.index;
	struct h2_explosive_port const *port;

	if (collision == NONE)
		return NULL;
	port = (struct h2_explosive_port const *)collision_model_definition_get(collision)->resistance.unused;
	return port->mark == H2_EXPLOSIVE_MARK ? port : NULL;
}

/* (damage.c's object_damage_body, a body just hurt, not a unit's): Halo 2's stages:
held alive until its fuse is out, its blast its own - see object_stages_read */
void h2_explosive_damaged(long object_index)
{
	struct object_datum *object = object_get(object_index);
	struct h2_explosive_port const *port = h2_explosive_port_of(object);
	long now = game_time_get(), slot = NONE, i, s;

	if (!port || network_game_distributed_client() || HALO_ENV_ONCE("HALO_H2_NO_EXPLOSIVE_STAGES"))
		return;
	if (HALO_ENV_ONCE("HALO_H2_HIT_LOG"))
		platform_log("h2 explosive: %08lx body %.3f", (unsigned long)object_index, object->object.body_vitality);
	/* (the map's generation too: consecutive maps' scenarios can have the
	same tag index, and the slots kept name the map gone's objects) */
	if (h2_explosives.scenario != global_scenario_index || h2_explosives_generation != halo_map_generation)
	{
		h2_explosives.scenario = global_scenario_index;
		h2_explosives_generation = halo_map_generation;
		h2_explosives.count = 0;
	}
	for (i = 0; i < h2_explosives.count && slot == NONE; i++)
	{
		if (h2_explosives.slots[i].object == object_index)
			slot = i;
	}
	if (slot == NONE)
	{
		if (h2_explosives.count >= H2_EXPLOSIVES_MAXIMUM)
			return;
		slot = h2_explosives.count++;
		h2_explosives.slots[slot].object = object_index;
		h2_explosives.slots[slot].blast_at = NONE;
		h2_explosives.slots[slot].effect = NONE;
		h2_explosives.slots[slot].stages = 0;
	}
	for (s = 0; s < 3; s++)
	{
		if (port->stage_health[s] > 0.0f && object->object.body_vitality <= port->stage_health[s] &&
			!TEST_FLAG(h2_explosives.slots[slot].stages, s))
		{
			SET_FLAG(h2_explosives.slots[slot].stages, s, TRUE);
			if (port->stage_effect[s].index != NONE)
			{
				h2_explosives.slots[slot].effect = port->stage_effect[s].index;
				h2_explosives.slots[slot].next_effect = now;
			}
			if (port->stage_fuse[s] >= 0.0f)
			{
				long at = now + (long)((port->stage_fuse[s] + port->death_delay) * 30.0f);

				if (h2_explosives.slots[slot].blast_at == NONE || at < h2_explosives.slots[slot].blast_at)
					h2_explosives.slots[slot].blast_at = at;
			}
		}
	}
	if (object->object.body_vitality <= 0.0f)
	{
		long at = now + (long)(port->death_delay * 30.0f);

		object->object.body_vitality = 0.001f;
		if (h2_explosives.slots[slot].blast_at == NONE || at < h2_explosives.slots[slot].blast_at)
			h2_explosives.slots[slot].blast_at = at;
	}
}

/* (damage.c's object_destroy) an explosive's blast, unattached at its marker */
boolean h2_explosive_blast(long object_index, long effect_index)
{
	struct object_datum *object = object_get(object_index);
	struct h2_explosive_port const *port = h2_explosive_port_of(object);
	char const *names[1] = { "replace" };
	real_point3d point = object->object.bounding_sphere_center;
	real_vector3d forward = object->object.up;
	struct object_marker marker;

	if (!port || effect_index == NONE)
		return FALSE;
	if (port->marker[0] && object_get_marker_by_name(object_index, port->marker, &marker, 1) > 0)
		point = marker.matrix.position;
	effect_new_unattached_from_markers(effect_index, object_index, NULL, 1, names, &point, &forward, 1.0f, 1.0f, NULL, NULL, FALSE);
	return TRUE;
}

/* (the game's tick) the explosives' sparks and fuses */
void h2_explosives_tick(void)
{
	long now, i;

	if (!h2_explosives.count)
		return;
	if (h2_explosives.scenario != global_scenario_index || h2_explosives_generation != halo_map_generation)
	{
		h2_explosives.count = 0;
		return;
	}
	now = game_time_get();
	for (i = h2_explosives.count - 1; i >= 0; i--)
	{
		long object_index = h2_explosives.slots[i].object;
		struct object_datum *object = object_try_and_get(object_index);
		struct h2_explosive_port const *port = object ? h2_explosive_port_of(object) : NULL;

		if (!port)
		{
			h2_explosives.slots[i] = h2_explosives.slots[--h2_explosives.count];
			continue;
		}
		if (h2_explosives.slots[i].effect != NONE && now >= h2_explosives.slots[i].next_effect)
		{
			char const *names[1];
			real_point3d point = object->object.bounding_sphere_center;
			real_vector3d forward = object->object.up;
			struct object_marker marker;

			names[0] = "replace";
			if (port->marker[0] && object_get_marker_by_name(object_index, port->marker, &marker, 1) > 0)
				point = marker.matrix.position;
			effect_new_unattached_from_markers(h2_explosives.slots[i].effect, object_index, NULL, 1, names, &point, &forward, 1.0f, 1.0f, NULL, NULL, FALSE);
			h2_explosives.slots[i].next_effect = now + H2_EXPLOSIVE_EFFECT_PERIOD;
		}
		if (h2_explosives.slots[i].blast_at != NONE && now >= h2_explosives.slots[i].blast_at)
		{
			if (HALO_ENV_ONCE("HALO_H2_HIT_LOG"))
				platform_log("h2 explosive: %08lx blows up", (unsigned long)object_index);
			h2_explosives.slots[i] = h2_explosives.slots[--h2_explosives.count];
			object->object.body_vitality = -1.0f;
			object_destroy(object_index);
		}
	}
}
