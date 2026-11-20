/*
H2_MACHINE_RIDERS.C

(port) Bipeds carried by the moving machines of a converted Halo 2 level
(h2_machine_riders.h).

This engine carries a biped only on an elevator: a machine flagged so with
an elevator node, moved by that node's translation (device_machines.c,
machine_elevator_carry_end). Halo 2's machines on Zanzibar are no
elevators: the windmill's blades turn about their hub, the house gate's
flaps fall open about their hinges. A biped on them was only pushed by
their collision as it swept through him - lifted a little, then left
behind, the blade passing through him, or pushed down through the floor
the blades turn into (the owner's Vita: "he clips through and falls
through the map").

Here a biped standing on a machine's surface (its physics' contact, the
surface facing up: bipeds.c) rides the node it stands on: once the machine
has moved this tick (objects.c, after its node matrices are recomputed),
the biped's position in the node's space before the move is put where the
node is now - turned with a blade, lowered with a flap. The move is cut
short where the level's own geometry would stop it, so a ride never goes
through the structure, and one longer than a step (a node teleported, the
machine reset) is not followed.

03b's Scarab is such a machine too: its named tracks walk it down the
street and under the bridges (h2_cinematics.c), its collision node by node
(h2_objanim.c), and a player who drops onto its deck, or the Elites placed
there, ride it. A rider that leaves - a jump, a step off its edge - keeps
the machine's last move as its velocity, as Halo 2's jump keeps its
ground's (halo2-decompiled bipeds.cpp 0xe4770).

A converted Halo 2 level only (h2_shadows_level); HALO_H2_MACHINE_RIDE=0
turns it off.

A machine whose Halo 2 object has an "apply collision damage scale"
(Terminal's trains: 100) kills what its moving collision meets: Halo 2's
Havok contact with it is collision damage times that scale, more than any
unit lives through. This engine's machines pass through what they meet, so
the machine that runs the game looks, each tick such a machine moves, for
the units (bipeds on foot, vehicles) whose middle - or a biped's feet, its
head - is inside its collision, and kills each as a fall does (no one's
kill). HALO_H2_MACHINE_DAMAGE=0 turns it off.
*/

/* ---------- headers */

#include "cseries.h"
#include "math/real_math.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "physics/collision_models.h"
#include "devices/devices.h"
#include "devices/device_definitions.h"
#include "game/game.h"
#include "game/cheats.h"
#include "units/units.h"
#include "h2_machine_riders.h"
#include "h2_shadows.h"
#include "ptick.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	MAXIMUM_RIDERS = 64,
	/* (ticks a rider stays one without touching the machine: a step's
	bounce off a blade's edge, not a jump) */
	RIDER_TICKS = 2,
	MAXIMUM_NODES = 64,
};

/* (the longest move followed, world units a tick) */
#define MAXIMUM_CARRY 1.0f
/* (kept off the structure a move is cut short at) */
#define STRUCTURE_MARGIN 0.02f

/* ---------- globals */

static struct
{
	long biped_index;
	long machine_index;
	short node_index;
	short ticks;
	/* (the machine's move under it this tick: kept by a rider that leaves -
	jumps, steps off -, as Halo 2's biped keeps its ground's velocity,
	halo2-decompiled bipeds.cpp 0xe4770) */
	real_vector3d carried;
} riders[MAXIMUM_RIDERS];
/* (perf/ce-floor-2) a rider was ever taken on: until then every biped's
move (h2_machine_rider_no_contact, ~40 a tick in a Halo CE fight) finds
no slot of its own in the 64 - that answer without the walk */
static boolean riders_ever;

static struct
{
	long machine_index;
	short node_count;
	real_matrix4x3 nodes[MAXIMUM_NODES];
} before;

/* (the killing machines' positions last tick: one moved this tick) */
enum { MAXIMUM_KILLING_MACHINES = 16 };
static struct
{
	long machine_index;
	real position;
} killing[MAXIMUM_KILLING_MACHINES];

/* ---------- private code */

void platform_log(char const *format, ...);
/* damage.c: killed as a fall kills (NONE: nobody's) */
void damage_kill_object_for_player(long object_index, long player_index);

/* (HALO_H2_MACHINE_RIDE_LOG=1: each contact and carry logged) */
static boolean logging(
	void)
{
	static int setting = -1;

	if (setting < 0)
		setting = getenv("HALO_H2_MACHINE_RIDE_LOG") != NULL;
	return setting != 0;
}

static boolean enabled(
	void)
{
	static int setting = -1;

	if (setting < 0)
	{
		char const *value = getenv("HALO_H2_MACHINE_RIDE");

		setting = !value || atoi(value) != 0;
	}
	return setting && h2_shadows_level();
}

/* ---------- public code */

void h2_machine_rider_contact(
	long biped_index,
	long machine_index,
	real_point3d const *point,
	real_vector3d const *normal)
{
	long free_slot = NONE, k;
	short node_index;
	real_point3d start;
	real_vector3d down;
	struct collision_result collision;

	if (!enabled() || machine_index == NONE)
		return;
	/* (the node: the machine's surface under the contact, a short test
	into it - the physics' contact planes do not say) */
	start.x = point->x + normal->i * 0.1f;
	start.y = point->y + normal->j * 0.1f;
	start.z = point->z + normal->k * 0.1f;
	down.i = -normal->i * 0.3f;
	down.j = -normal->j * 0.3f;
	down.k = -normal->k * 0.3f;
	if (!collision_test_vector(FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_machines_bit) |
			FLAG(_collision_test_front_facing_surfaces_bit), &start, &down, biped_index, &collision) ||
		collision.type != _collision_result_object || collision.object_index != machine_index || collision.node_index < 0)
	{
		if (logging())
			platform_log("h2 machine riders: %08lx touches %08lx at %.2f %.2f %.2f, no node under it", (unsigned long)biped_index,
				(unsigned long)machine_index, point->x, point->y, point->z);
		return;
	}
	node_index = collision.node_index;
	/* (ptick.h) the riders are kept HERE, outside the game state a speculative
	update's snapshot puts back: a biped on a machine is not speculated (01b
	parted from the serial tick as a Marine stepped off the armory's lift: a
	dropped speculation had counted his ticks off it once more) */
	if (HALO_PTICK_GUARDED(_ptick_abort_other, "machine rider"))
		return;
	for (k = 0; k < MAXIMUM_RIDERS; k++)
	{
		if (riders[k].ticks > 0 && riders[k].biped_index == biped_index)
		{
			riders[k].machine_index = machine_index;
			riders[k].node_index = node_index;
			riders[k].ticks = RIDER_TICKS;
			riders_ever = TRUE;
			return;
		}
		if (riders[k].ticks <= 0 && free_slot == NONE)
			free_slot = k;
	}
	if (free_slot != NONE)
	{
		set_real_vector3d(&riders[free_slot].carried, 0.0f, 0.0f, 0.0f);
		riders[free_slot].biped_index = biped_index;
		riders[free_slot].machine_index = machine_index;
		riders[free_slot].node_index = node_index;
		riders[free_slot].ticks = RIDER_TICKS;
		riders_ever = TRUE;
	}
}

void h2_machine_rider_no_contact(
	long biped_index,
	real_vector3d *velocity)
{
	long k;

	if (!riders_ever)
		return;
	for (k = 0; k < MAXIMUM_RIDERS; k++)
	{
		/* (ptick.h: a rider's count is written here - not speculated; a biped
		with no entry reads nothing another root could have made) */
		if (riders[k].ticks > 0 && riders[k].biped_index == biped_index && HALO_PTICK_GUARDED(_ptick_abort_other, "machine rider"))
			return;
		if (riders[k].ticks > 0 && riders[k].biped_index == biped_index && --riders[k].ticks == 0)
		{
			/* (off the machine: its last move kept in the biped's velocity -
			Halo 2's jump adds its ground's velocity to the jump's
			(halo2-decompiled bipeds.cpp 0xe4770), so a player jumping on the
			walking Scarab lands where he took off; this engine's velocities
			are a tick's) */
			velocity->i += riders[k].carried.i;
			velocity->j += riders[k].carried.j;
			velocity->k += riders[k].carried.k;
			if (logging())
				platform_log("h2 machine riders: %08lx left %08lx, its move %.3f %.3f %.3f kept", (unsigned long)biped_index,
					(unsigned long)riders[k].machine_index, riders[k].carried.i, riders[k].carried.j, riders[k].carried.k);
			set_real_vector3d(&riders[k].carried, 0.0f, 0.0f, 0.0f);
		}
	}
}

/* (device_machines.c) whether a biped rides the machine here, this tick: its
elevator carry then leaves him to these (an elevator's node moved him once,
these again - 06b's Key, an elevator, slid its riders off its front at
twice its speed - and only these turn him with a node that turns: the Key's
transform) */
boolean h2_machine_rider_carries(
	long biped_index,
	long machine_index)
{
	long k;

	if (!enabled())
		return FALSE;
	for (k = 0; k < MAXIMUM_RIDERS; k++)
	{
		if (riders[k].ticks > 0 && riders[k].biped_index == biped_index && riders[k].machine_index == machine_index)
			return TRUE;
	}
	return FALSE;
}

void h2_machine_riders_begin(
	long machine_index)
{
	struct object_datum *machine;
	long k;
	short node, count;

	/* (ptick.h) a machine of a level with riders is not speculated: the
	record below and the riders are one for the level (a helper's machine
	would clear it under the committer's), and who rides it is decided by
	the bipeds' updates before it in the serial order */
	if (HALO_PTICK_SPECULATING())
	{
		if (enabled())
			halo_ptick_guard(_ptick_abort_other, "machine riders");
		return;
	}
	before.machine_index = NONE;
	if (!enabled())
		return;
	for (k = 0; k < MAXIMUM_RIDERS; k++)
	{
		if (riders[k].ticks > 0 && riders[k].machine_index == machine_index)
			break;
	}
	if (k == MAXIMUM_RIDERS || !(machine = object_try_and_get_and_verify_type(machine_index, _object_mask_machine)))
		return;
	count = (short)(machine->object.node_matrices.size / (short)sizeof(real_matrix4x3));
	if (count > MAXIMUM_NODES)
		count = MAXIMUM_NODES;
	for (node = 0; node < count; node++)
		before.nodes[node] = *object_get_node_matrix(machine_index, node);
	before.node_count = count;
	before.machine_index = machine_index;
}

/* a machine of collision damage moved: what its collision now holds killed */
static void machine_collision_damage(
	long machine_index)
{
	static int setting = -1;
	struct machine_datum *machine;
	struct machine_definition const *definition;
	struct collision_model_instance instance;
	struct object_iterator iterator;
	struct object_datum *unit;
	long slot = NONE, k;
	boolean moved;

	if (setting < 0)
		setting = !getenv("HALO_H2_MACHINE_DAMAGE") || atoi(getenv("HALO_H2_MACHINE_DAMAGE")) != 0;
	if (!setting || game_connection() == _game_connection_network_client || !h2_shadows_level() ||
		!(machine = (struct machine_datum *)object_try_and_get_and_verify_type(machine_index, _object_mask_machine)))
	{
		return;
	}
	definition = machine_definition_get(machine->definition_index);
	if (!(definition->machine.h2_collision_damage_scale > 0.0f))
		return;
	/* (moved since last tick: its position changed) */
	for (k = 0; k < MAXIMUM_KILLING_MACHINES; k++)
	{
		if (killing[k].machine_index == machine_index)
		{
			slot = k;
			break;
		}
		if (slot == NONE && (killing[k].machine_index == 0 || killing[k].machine_index == NONE ||
			!object_try_and_get(killing[k].machine_index)))
		{
			slot = k;
		}
	}
	if (slot == NONE)
		return;
	moved = killing[slot].machine_index == machine_index && killing[slot].position != machine->device.position;
	killing[slot].machine_index = machine_index;
	killing[slot].position = machine->device.position;
	if (!moved || !collision_model_instance_new(&instance, machine_index))
		return;
	object_iterator_new(&iterator, _object_mask_unit, 0);
	while ((unit = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		real_vector3d d;
		real_point3d points[3];
		short n, point_count = 1;

		if (unit->object.parent_object_index != NONE || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
			continue;
		/* (cheat_deathless_player holds against a machine too: object_cause_damage's
		instant kill does not ask, and 06a's closing pistons killed the test player) */
		if (cheat.deathless_player && TEST_FLAG(_object_mask_unit, unit->object.type) &&
			((struct unit_datum *)unit)->unit.player_index != NONE)
		{
			continue;
		}
		vector_from_points3d(&machine->object.bounding_sphere_center, &unit->object.bounding_sphere_center, &d);
		if (magnitude3d(&d) > machine->object.bounding_sphere_radius + unit->object.bounding_sphere_radius)
			continue;
		points[0] = unit->object.bounding_sphere_center;
		if (unit->object.type == _object_type_biped)
		{
			points[1] = unit->object.position;
			points[1].z += 0.1f;
			points[2] = unit->object.bounding_sphere_center;
			points[2].z += 0.5f * unit->object.bounding_sphere_radius;
			point_count = 3;
		}
		for (n = 0; n < point_count; n++)
		{
			if (collision_model_test_point(&instance, &points[n]))
				break;
		}
		if (n == point_count)
			continue;
		if (logging())
		{
			platform_log("h2 machine damage: %08lx (%s) killed by %08lx (%s) at %.2f %.2f %.2f", (unsigned long)iterator.index,
				tag_get_name(unit->definition_index), (unsigned long)machine_index, tag_get_name(machine->definition_index),
				points[n].x, points[n].y, points[n].z);
		}
		damage_kill_object_for_player(iterator.index, NONE);
	}
}

void h2_machine_riders_end(
	long machine_index)
{
	struct object_datum *machine;
	long k;

	/* (ptick.h: h2_machine_riders_begin stopped a speculation on a level with riders) */
	if (HALO_PTICK_SPECULATING())
		return;
	machine_collision_damage(machine_index);

	if (before.machine_index != machine_index || machine_index == NONE ||
		!(machine = object_try_and_get_and_verify_type(machine_index, _object_mask_machine)))
	{
		before.machine_index = NONE;
		return;
	}
	before.machine_index = NONE;
	for (k = 0; k < MAXIMUM_RIDERS; k++)
	{
		struct object_datum *biped;
		real_point3d local, moved, start;
		real_vector3d delta;
		real length2;
		struct collision_result collision;

		if (riders[k].ticks <= 0 || riders[k].machine_index != machine_index || riders[k].node_index >= before.node_count)
			continue;
		biped = object_try_and_get_and_verify_type(riders[k].biped_index, _object_mask_biped);
		if (!biped || biped->object.parent_object_index != NONE)
		{
			riders[k].ticks = 0;
			continue;
		}
		matrix4x3_inverse_transform_point(&before.nodes[riders[k].node_index], &biped->object.position, &local);
		matrix4x3_transform_point(object_get_node_matrix(machine_index, riders[k].node_index), &local, &moved);
		delta.i = moved.x - biped->object.position.x;
		delta.j = moved.y - biped->object.position.y;
		delta.k = moved.z - biped->object.position.z;
		length2 = delta.i * delta.i + delta.j * delta.j + delta.k * delta.k;
		set_real_vector3d(&riders[k].carried, 0.0f, 0.0f, 0.0f);
		if (length2 < 1e-10f)
			continue;
		if (length2 > MAXIMUM_CARRY * MAXIMUM_CARRY)
		{
			riders[k].ticks = 0;
			continue;
		}
		/* (never through the level: the move from a little above the
		biped's feet, cut short where the structure stops it) */
		start = biped->object.position;
		start.z += 0.05f;
		if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
			&start, &delta, NONE, &collision))
		{
			real length = (real)sqrt(length2);
			real t = collision.t - STRUCTURE_MARGIN / length;

			if (t <= 0.0f)
				continue;
			moved.x = biped->object.position.x + delta.i * t;
			moved.y = biped->object.position.y + delta.j * t;
			moved.z = biped->object.position.z + delta.k * t;
		}
		if (logging())
			platform_log("h2 machine riders: %08lx on %08lx node %d carried %.3f %.3f %.3f to %.2f %.2f %.2f (its velocity %.3f %.3f %.3f, tick %ld)",
				(unsigned long)riders[k].biped_index, (unsigned long)machine_index, (int)riders[k].node_index,
				moved.x - biped->object.position.x, moved.y - biped->object.position.y, moved.z - biped->object.position.z,
				moved.x, moved.y, moved.z, biped->object.translational_velocity.i, biped->object.translational_velocity.j,
				biped->object.translational_velocity.k, (long)game_time_get());
		riders[k].carried.i = moved.x - biped->object.position.x;
		riders[k].carried.j = moved.y - biped->object.position.y;
		riders[k].carried.k = moved.z - biped->object.position.z;
		object_translate(riders[k].biped_index, &moved, NULL);
	}
}
