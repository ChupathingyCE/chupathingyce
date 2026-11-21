/*
FORGE_PHYSICS.INC

An object's physics (included by forge.c, for its statics): the
properties' PHYSICS row - NORMAL, FIXED, PHASED - for any placed object.

- NORMAL: as the game has it (it falls, it is pushed, it is driven off).
- FIXED: it never moves: the local game puts it back where the editor let
  it go each frame it was moved, its velocity none - a vehicle is not
  driven off, a weapon not knocked about (one picked up is carried as ever,
  and back at its place once let go). The editor still moves it: held, it
  goes where the monitor takes it, and stays where it is let go (it does
  not fall).
- PHASED: no collision, by the engine's own switch (_object_no_collisions_bit,
  the one the scripts' object_set_collideable sets): players, vehicles and
  other objects pass through it, so that pieces can overlap to make shapes.
  Only its own collision goes: a floor of NORMAL or FIXED pieces under it
  holds a player as ever, and the map's structure is never touched. (A
  projectile's line still meets it: the engine's vector test does not read
  the switch, as for a scripted object; so a PHASED object also carries
  _object_forge_phased_bit, which the vector tests of collisions.c read: shots
  pass it, a scripted object's behaviour as it was. The monitor's crosshair
  asks for it, _collision_test_forge_phased_objects_bit: it is still grabbed.)

Saved in the layout as version 7's flags (forge_files.h
FORGE_RECORD_PHYSICS_*). Nothing of a game that is not Forge or a layout
played is touched.
*/

/* ---------- constants */

/* a fixed object's place kept to within this (world units) */
#define FORGE_PHYSICS_FIXED_TOLERANCE 0.005f

/* ---------- private code */

static short forge_physics_mode(
	struct forge_placed const *placed)
{
	if (placed->flags & FORGE_RECORD_PHYSICS_PHASED)
		return _forge_physics_phased;
	if (placed->flags & FORGE_RECORD_PHYSICS_FIXED)
		return _forge_physics_fixed;
	return _forge_physics_normal;
}

static unsigned char forge_physics_flags(
	short mode)
{
	return (unsigned char)(mode == _forge_physics_phased ? FORGE_RECORD_PHYSICS_PHASED :
		mode == _forge_physics_fixed ? FORGE_RECORD_PHYSICS_FIXED : 0);
}

/* the object's collision switch as its physics has it: set for a phased
one, cleared for another only where this set it. (The consoles build this
file but leave Forge's hooks out, HALO_CONSOLE_NO_FORGE: there the object
flag that lets shots pass is not in objects.h, and the switch alone is set.) */
static void forge_physics_apply(
	struct forge_placed *placed)
{
	struct object_datum *object;
	boolean phased = forge_physics_mode(placed) == _forge_physics_phased;

	if (!forge_object_alive(placed->object_index))
	{
		placed->phased_applied = FALSE;
		return;
	}
	object = object_get(placed->object_index);
	if (phased)
	{
		if (!TEST_FLAG(object->object.flags, _object_no_collisions_bit))
			SET_FLAG(object->object.flags, _object_no_collisions_bit, TRUE);
#ifndef HALO_CONSOLE_NO_FORGE
		SET_FLAG(object->object.flags, _object_forge_phased_bit, TRUE);
#endif
		placed->phased_applied = TRUE;
	}
	else if (placed->phased_applied)
	{
		SET_FLAG(object->object.flags, _object_no_collisions_bit, FALSE);
#ifndef HALO_CONSOLE_NO_FORGE
		SET_FLAG(object->object.flags, _object_forge_phased_bit, FALSE);
#endif
		placed->phased_applied = FALSE;
	}
}

/* (each frame, a Forge game or a layout played, before the tick) every
placed object's collision switch; a fixed one put back where it was let go
(the host's, a local game's) */
static void forge_physics_update(
	void)
{
	short index;

	if (!forge_globals.game_active)
		return;
	for (index = 0; index < forge_globals.placed_count && index < FORGE_MAXIMUM_OBJECTS; index++)
	{
		struct forge_placed *placed = &forge_globals.placed[index];
		struct object_datum *object;

		forge_physics_apply(placed);
		if (!(placed->flags & FORGE_RECORD_PHYSICS_FIXED) || !placed->has_home ||
			!forge_object_alive(placed->object_index) || placed->object_index == forge_globals.held_object_index)
		{
			continue;
		}
		object = object_get(placed->object_index);
		if (object->object.parent_object_index != NONE || TEST_FLAG(object->object.damage_flags, _object_dead_bit))
			continue;
		if (distance_squared3d(&object->object.position, &placed->home.position) >
			FORGE_PHYSICS_FIXED_TOLERANCE * FORGE_PHYSICS_FIXED_TOLERANCE ||
			magnitude_squared3d(&object->object.translational_velocity) > 0.f ||
			magnitude_squared3d(&object->object.angular_velocity) > 0.f)
		{
			object_set_position(placed->object_index, &placed->home.position, &placed->home.forward, &placed->home.up);
			object = object_get(placed->object_index);
			object->object.translational_velocity = *global_zero_vector3d;
			object->object.angular_velocity = *global_zero_vector3d;
			SET_FLAG(object->object.flags, _object_at_rest_bit, TRUE);
		}
	}
}

/* (forge_drop, an object turned or set, an undo's move: wherever the editor
lets an object go to fall) a weapon or a piece of equipment let go with its
origin inside a solid object - a crate, a rock - put on that object's top.
An item falls by a line from where it is to where it moves (items.c
item_update), and a line inside an object's collision model crosses none of
its surfaces: one let go even a centimetre into an object fell through it
(one held to look as if it lay on a top has its origin a little in it). The
map's structure needs nothing: an item in the ground stays there (its
location must stay valid), and players and vehicles are pushed out of what
they are in. Only items, only the objects an item rests on (scenery,
machines, controls: items.c's rule). At any depth: the top is looked for
FORGE_ITEM_UNBURY_HEIGHT up, and one under an object, not in it, meets its
underside first and is left alone. A ramp's face is a top too. An object
stacked on it, or one standing through it: its top is inside that one, so
the item is lifted again, out of that one too. */
#define FORGE_ITEM_UNBURY_HEIGHT 4.f
#define FORGE_ITEM_UNBURY_TOP 0.6f
#define FORGE_ITEM_UNBURY_PASSES 8

/* (forge_item_unburied's) the object (scenery, a machine, a control) a point
is inside, and the top it leaves it by going up (its point, the object):
going up, a surface left - a back face - before any entered. One entered at
or before it, of another object: the point is in the first (with the other
standing on it or through it), not under the other in the air, only if going
down that first one's underside is left before any surface is entered */
static boolean forge_item_inside_solid(
	real_point3d const *position,
	long item_index,
	unsigned long solid,
	struct collision_result *left)
{
	struct collision_result entered;
	struct collision_result below_left;
	struct collision_result below_entered;
	real_point3d over = *position;
	real_point3d under = *position;

	over.z += FORGE_ITEM_UNBURY_HEIGHT;
	if (!collision_test_line(solid | FLAG(_collision_test_back_facing_surfaces_bit), position, &over, item_index, left) ||
		left->type != _collision_result_object || left->plane.n.k < FORGE_ITEM_UNBURY_TOP)
	{
		return FALSE;
	}
	if (!collision_test_line(solid | FLAG(_collision_test_front_facing_surfaces_bit), position, &over, item_index,
		&entered) || entered.t > left->t)
	{
		return TRUE;
	}
	if (entered.type != _collision_result_object || entered.object_index == left->object_index)
		return FALSE;
	under.z -= FORGE_ITEM_UNBURY_HEIGHT;
	if (!collision_test_line(solid | FLAG(_collision_test_back_facing_surfaces_bit), position, &under, item_index,
		&below_left) || below_left.type != _collision_result_object || below_left.object_index != left->object_index)
	{
		return FALSE;
	}
	return !collision_test_line(solid | FLAG(_collision_test_front_facing_surfaces_bit), position, &under, item_index,
		&below_entered) || below_left.t <= below_entered.t;
}

static void forge_item_unburied(
	long object_index)
{
	struct object_datum *object = object_try_and_get(object_index);
	unsigned long solid = FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit) |
		FLAG(_collision_test_objects_machines_bit) | FLAG(_collision_test_objects_controls_bit);
	struct collision_result left;
	real_point3d position;
	real_point3d over;
	long top_object_index = NONE;
	short pass;

	if (!object || !(FLAG(object->object.type) & _object_mask_item) || object->object.parent_object_index != NONE)
		return;
	position = object->object.position;
	over = position;
	/* (just over each top it is in: out of a stack or objects through one
	another a pass an object) */
	for (pass = 0; pass < FORGE_ITEM_UNBURY_PASSES && forge_item_inside_solid(&over, object_index, solid, &left); pass++)
	{
		over = left.point;
		over.z += 0.05f;
		top_object_index = left.object_index;
	}
	if (!pass || !object_try_and_get(top_object_index))
		return;
	/* (just over the top: it falls onto it and rests there) */
	object_set_position(object_index, &over, NULL, NULL);
	object->object.translational_velocity = *global_zero_vector3d;
	platform_log("forge: %s let go %.2f into %s: put on its top at %.2f %.2f %.2f%s", forge_tag_leaf(object->definition_index),
		over.z - 0.05f - position.z, forge_tag_leaf(object_get(top_object_index)->definition_index), over.x, over.y, over.z,
		pass > 1 ? " (out of the objects on it too)" : "");
}
