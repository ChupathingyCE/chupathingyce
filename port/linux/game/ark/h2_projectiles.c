/*
H2_PROJECTILES.C

port: Halo 2's projectile rules and the rocket launcher's lock-on on a
Halo 2 level (h2_projectiles.h says what and where). HALO_H2_LOCK_LOG logs
the lock-on's states and the shots it fires.
*/

#include "cseries.h"
#include "game/aim_assist.h"
#include "game/game_engine.h"
#include "game/game.h"
#include "game/players.h"
#include "items/projectile_definitions.h"
#include "items/weapon_definitions.h"
#include "items/weapons.h"
#include "objects/object_definitions.h"
#include "objects/objects.h"
#include "tag_files/tag_groups.h"
#include "units/units.h"

#include "h2_projectiles.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);
boolean game_team_is_enemy(short team_index0, short team_index1);

/* ---------- constants */

enum
{
	/* weapons.cpp: locking takes half a second (function_103c40), a lock
	is kept a second without its target (function_103bd0), a target is
	available a second after it was last in the reticle (function_106280) */
	H2_LOCK_LOCKING_TICKS = TICKS_PER_SECOND / 2,
	H2_LOCK_LOCKED_TICKS = TICKS_PER_SECOND,
	H2_LOCK_AVAILABLE_TICKS = TICKS_PER_SECOND,
	H2_LOCK_MAXIMUM_WEAPONS = 32,
};

/* the trigger's states (weapons.cpp's 0, 5, 6, 7) */
enum
{
	_h2_lock_idle = 0,
	_h2_lock_locking,
	_h2_lock_locked,
	_h2_lock_lost,
};

/* ---------- structures */

struct h2_lock
{
	long weapon_index;
	short state;
	short timer;
	boolean released;           /* (flag0: the trigger was up since its last shot) */
	long target_index;          /* the target being locked or locked */
	long seen_index;            /* the last target in the reticle, and when */
	long seen_time;
	long fire_target_index;     /* the shot fired now: its target (NONE: unguided) */
	boolean firing;
};

/* ---------- globals */

static struct
{
	long time;                  /* (the game time last seen: a new game starts over) */
	struct h2_lock locks[H2_LOCK_MAXIMUM_WEAPONS];
} h2_lock_globals;

/* ---------- private code */

static void const *h2_object_port(long definition_index, unsigned long tag)
{
	struct object_definition const *definition;
	unsigned long const *words;

	if (definition_index == NONE)
		return NULL;
	definition = (struct object_definition const *)tag_get('obje', definition_index);
	words = (unsigned long const *)definition->object.unused2 + H2_OBJECT_PORT_WORD;
	return words[0] == tag ? words : NULL;
}

static boolean h2_lock_log(void)
{
	static int log = -1;

	if (log < 0)
		log = getenv("HALO_H2_LOCK_LOG") != NULL;
	return log != 0;
}

/* a weapon's lock (a new one if it has none; NULL if there is no room) */
static struct h2_lock *h2_lock_get(long weapon_index, boolean make)
{
	long now = game_time_get(), i;
	struct h2_lock *free_lock = NULL;

	if (now < h2_lock_globals.time)
		memset(h2_lock_globals.locks, 0, sizeof(h2_lock_globals.locks));
	h2_lock_globals.time = now;
	for (i = 0; i < H2_LOCK_MAXIMUM_WEAPONS; i++)
	{
		struct h2_lock *lock = &h2_lock_globals.locks[i];

		if (lock->weapon_index == weapon_index && weapon_index != 0)
			return lock;
		/* (a lock of a weapon gone, or never used) */
		if (!free_lock && (lock->weapon_index == 0 || !object_try_and_get_and_verify_type(lock->weapon_index, _object_mask_weapon)))
			free_lock = lock;
	}
	if (!make || !free_lock)
		return NULL;
	memset(free_lock, 0, sizeof(*free_lock));
	free_lock->weapon_index = weapon_index;
	free_lock->target_index = free_lock->seen_index = free_lock->fire_target_index = NONE;
	free_lock->released = TRUE;
	return free_lock;
}

/* the lockable target in the weapon's holder's reticle (human tracking: a
vehicle with a target locked by it, within its lock-on distance, within the
weapon's autoaim angle widened by the target's size, in sight; the nearest
to the reticle); NONE */
#define H2_LOCK_REJECT(reason) \
	{ \
		if (h2_lock_log() && game_time_get() % 15 == 0) \
			platform_log("h2 lock: tick %ld: vehicle %08lx not lockable: %s", (long)game_time_get(), (unsigned long)vehicle_index, reason); \
		continue; \
	}

static long h2_lock_find(long weapon_index)
{
	struct weapon_datum *weapon = weapon_get(weapon_index);
	struct weapon_definition const *definition = weapon_definition_get(weapon->definition_index);
	long unit_index = weapon->object.parent_object_index, vehicle_of_unit, best = NONE, team = NONE, holder_team;
	struct unit_datum *unit;
	struct object_iterator iterator;
	struct object_datum *object;
	real_point3d camera;
	real_vector3d aim;
	real best_angle = 0.0f;
	boolean teams = game_engine_has_teams();

	if (unit_index == NONE || !(unit = (struct unit_datum *)object_try_and_get_and_verify_type(unit_index, _object_mask_unit)) ||
		unit->unit.player_index == NONE)
	{
		return NONE;
	}
	/* Halo 2's lock-on works zoomed in (the rocket launcher's tag: one
	magnification level, human tracking): unzoomed the reticle finds nothing */
	if (unit->unit.current_zoom_level == NONE || definition->weapon.zoom_level_count < 1)
	{
		if (h2_lock_log() && game_time_get() % 15 == 0)
			platform_log("h2 lock: tick %ld: not zoomed (level %d of %d): nothing lockable", (long)game_time_get(),
				(int)unit->unit.current_zoom_level, (int)definition->weapon.zoom_level_count);
		return NONE;
	}
	team = player_get(unit->unit.player_index)->team_index;
	holder_team = unit->object.owner_team_index;
	if (holder_team == NONE || holder_team == _game_team_default)
		holder_team = _game_team_player;
	vehicle_of_unit = object_get_ultimate_parent(unit_index);
	unit_get_camera_position(unit_index, &camera);
	aim = unit->unit.aiming_vector;
	if (normalize3d(&aim) == 0.0f)
		return NONE;
	object_iterator_new(&iterator, _object_mask_vehicle, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		long vehicle_index = iterator.index;
		struct h2_lock_port const *port = (struct h2_lock_port const *)h2_object_port(object->definition_index, H2_LOCK_PORT_TAG);
		struct unit_datum *vehicle = (struct unit_datum *)object;
		real_point3d point = object->object.bounding_sphere_center;
		real_vector3d to;
		real distance, angle, cosine;

		if (!port || !(port->flags & FLAG(_h2_lock_human_bit)) || vehicle_index == vehicle_of_unit)
			continue;
		/* only an enemy's: a vehicle with an occupant of an enemy team (in
		a team game or a campaign level the occupant's team against the
		player's; in a free-for-all any other player's) */
		{
			struct unit_datum *driver = vehicle->unit.driver_object_index != NONE ?
				(struct unit_datum *)object_try_and_get_and_verify_type(vehicle->unit.driver_object_index, _object_mask_unit) : NULL;

			if (!driver)
				H2_LOCK_REJECT("no occupant");
			if (driver->unit.player_index != NONE)
			{
				/* (another player: another team in a team game, anyone else
				otherwise) */
				if (driver->unit.player_index == unit->unit.player_index ||
					(teams && player_get(driver->unit.player_index)->team_index == team))
				{
					H2_LOCK_REJECT("not an enemy's");
				}
			}
			else if (!game_team_is_enemy(driver->object.owner_team_index, holder_team))
			{
				H2_LOCK_REJECT("not an enemy's");
			}
		}
		vector_from_points3d(&camera, &point, &to);
		distance = normalize3d(&to);
		if (distance <= 0.0f || distance > port->distance)
			H2_LOCK_REJECT("too far");
		cosine = PIN(dot_product3d(&aim, &to), -1.0f, 1.0f);
		angle = (real)acos(cosine);
		if (angle > definition->weapon.aim_assist_parameters.autoaim_angle + (real)atan2(port->size, distance))
			H2_LOCK_REJECT("outside the reticle");
		if (best != NONE && angle >= best_angle)
			continue;
		if (!aim_assist_clear_line_of_sight(&camera, &point, unit_index, vehicle_index))
			H2_LOCK_REJECT("not in sight");
		best = vehicle_index;
		best_angle = angle;
	}
	return best;
}

static void h2_lock_state(struct h2_lock *lock, short state, short timer, char const *why)
{
	if (h2_lock_log() && lock->state != state)
		platform_log("h2 lock: weapon %08lx tick %ld: %d -> %d (%s), target %08lx", (unsigned long)lock->weapon_index, (long)game_time_get(),
			(int)lock->state, (int)state, why, (unsigned long)lock->target_index);
	lock->state = state;
	lock->timer = timer;
}

/* ---------- public code */

struct h2_projectile_port const *h2_projectile_port(struct projectile_definition const *definition)
{
	unsigned long const *words = (unsigned long const *)definition->object.unused2 + H2_OBJECT_PORT_WORD;

	return words[0] == H2_PROJECTILE_PORT_TAG ? (struct h2_projectile_port const *)words : NULL;
}

real h2_projectile_guided_angular_velocity(struct h2_projectile_port const *port, long projectile_index)
{
	/* (function_f8200: a random one between the bounds for each
	projectile - here from its identifier, the same on every machine) */
	unsigned long hash = (unsigned long)DATUM_INDEX_TO_IDENTIFIER(projectile_index) * 2654435761UL;
	real fraction = (real)((hash >> 16) & 0xFFFF) / 65535.0f;

	return port->guided_angular_velocity[0] + fraction * (port->guided_angular_velocity[1] - port->guided_angular_velocity[0]);
}

short h2_trigger_tracking(struct weapon_trigger_definition const *trigger)
{
	return trigger->port_tracking_tag == H2_TRACKING_PORT_TAG ? trigger->port_tracking_type : _h2_tracking_none;
}

boolean h2_lock_on_trigger_update(long weapon_index, short trigger_index, boolean down)
{
	struct h2_lock *lock = h2_lock_get(weapon_index, TRUE);
	long now = game_time_get(), found;
	boolean available;

	(void)trigger_index;
	if (!lock)
		return down;
	lock->firing = FALSE;
	if (lock->timer > 0)
		lock->timer--;
	found = h2_lock_find(weapon_index);
	if (found != NONE)
	{
		lock->seen_index = found;
		lock->seen_time = now;
	}
	available = lock->seen_index != NONE && now - lock->seen_time < H2_LOCK_AVAILABLE_TICKS &&
		object_try_and_get_and_verify_type(lock->seen_index, _object_mask_vehicle);
	switch (lock->state)
	{
	case _h2_lock_idle:
		if (!down)
		{
			lock->released = TRUE;
			return FALSE;
		}
		if (lock->released && available)
		{
			lock->target_index = lock->seen_index;
			h2_lock_state(lock, _h2_lock_locking, H2_LOCK_LOCKING_TICKS, "pulled on a target");
			return FALSE;
		}
		/* (no target: it fires as the pull is made, held as it is held) */
		lock->released = FALSE;
		lock->fire_target_index = NONE;
		return TRUE;

	case _h2_lock_locking:
		if (down)
		{
			if (!available)
				h2_lock_state(lock, _h2_lock_lost, 0, "target lost while locking");
			else if (lock->seen_index != lock->target_index)
			{
				lock->target_index = lock->seen_index;
				h2_lock_state(lock, _h2_lock_locking, H2_LOCK_LOCKING_TICKS, "another target");
				lock->state = _h2_lock_locking;
			}
			else if (!lock->timer)
				h2_lock_state(lock, _h2_lock_locked, H2_LOCK_LOCKED_TICKS, "locked");
			return FALSE;
		}
		/* (released before the lock: an unguided rocket) */
		h2_lock_state(lock, _h2_lock_idle, 0, "released while locking: unguided");
		lock->released = FALSE;
		lock->fire_target_index = NONE;
		lock->firing = TRUE;
		return TRUE;

	case _h2_lock_locked:
		if (!down)
		{
			h2_lock_state(lock, _h2_lock_idle, 0, "released locked: guided");
			lock->fire_target_index = object_try_and_get_and_verify_type(lock->target_index, _object_mask_vehicle) ? lock->target_index : NONE;
			lock->firing = TRUE;
			return TRUE;
		}
		if (found != NONE && found == lock->target_index)
			lock->timer = H2_LOCK_LOCKED_TICKS;
		else if (!lock->timer)
			h2_lock_state(lock, _h2_lock_lost, 0, "lock lost");
		return FALSE;

	case _h2_lock_lost:
	default:
		if (!down)
		{
			/* (a lost lock's release fires nothing) */
			h2_lock_state(lock, _h2_lock_idle, 0, "released after the lock was lost");
			lock->released = TRUE;
			return FALSE;
		}
		if (available)
		{
			lock->target_index = lock->seen_index;
			h2_lock_state(lock, _h2_lock_locking, H2_LOCK_LOCKING_TICKS, "target again");
		}
		return FALSE;
	}
}

long h2_tracking_target(long weapon_index, struct weapon_trigger_definition const *trigger, long aim_target_index)
{
	switch (h2_trigger_tracking(trigger))
	{
	case _h2_tracking_human:
		{
			struct h2_lock *lock = h2_lock_get(weapon_index, FALSE);
			long target = lock ? lock->fire_target_index : NONE;

			if (lock)
				lock->fire_target_index = NONE;
			if (h2_lock_log())
				platform_log("h2 lock: weapon %08lx tick %ld: fired, %s %08lx", (unsigned long)weapon_index, (long)game_time_get(),
					target != NONE ? "guided at" : "unguided", (unsigned long)target);
			return target;
		}
	case _h2_tracking_plasma:
		/* (its targets the bipeds': the vehicles' are not locked by plasma
		tracking) */
		{
			long target = aim_target_index != NONE && object_try_and_get_and_verify_type(aim_target_index, _object_mask_biped) ? aim_target_index : NONE;

			if (h2_lock_log())
				platform_log("h2 lock: weapon %08lx tick %ld: plasma tracking, aimed at %08lx, tracks %08lx", (unsigned long)weapon_index,
					(long)game_time_get(), (unsigned long)aim_target_index, (unsigned long)target);
			return target;
		}
	default:
		return aim_target_index;
	}
}

boolean h2_lock_on_hud(long weapon_index, boolean *lockable, boolean *locking, boolean *locked, real *fraction, long *target_index)
{
	struct weapon_datum *weapon = weapon_index != NONE ? (struct weapon_datum *)object_try_and_get_and_verify_type(weapon_index, _object_mask_weapon) : NULL;
	struct weapon_definition const *definition;
	struct h2_lock *lock;
	long now = game_time_get();

	*lockable = *locking = *locked = FALSE;
	*fraction = 0.0f;
	*target_index = NONE;
	if (!weapon)
		return FALSE;
	definition = weapon_definition_get(weapon->definition_index);
	if (definition->weapon.triggers.count < 1 ||
		h2_trigger_tracking(TAG_BLOCK_GET_ELEMENT(&definition->weapon.triggers, 0, struct weapon_trigger_definition)) != _h2_tracking_human)
	{
		return FALSE;
	}
	if (!(lock = h2_lock_get(weapon_index, FALSE)))
		return TRUE;
	switch (lock->state)
	{
	case _h2_lock_locking:
		*locking = TRUE;
		*fraction = 1.0f - (real)lock->timer / (real)H2_LOCK_LOCKING_TICKS;
		*target_index = lock->target_index;
		break;
	case _h2_lock_locked:
		*locked = TRUE;
		*fraction = 1.0f;
		*target_index = lock->target_index;
		break;
	case _h2_lock_idle:
		if (lock->seen_index != NONE && now - lock->seen_time < H2_LOCK_AVAILABLE_TICKS)
		{
			*lockable = TRUE;
			*target_index = lock->seen_index;
		}
		break;
	}
	if (*target_index != NONE && !object_try_and_get_and_verify_type(*target_index, _object_mask_vehicle))
	{
		*lockable = *locking = *locked = FALSE;
		*target_index = NONE;
	}
	return TRUE;
}
