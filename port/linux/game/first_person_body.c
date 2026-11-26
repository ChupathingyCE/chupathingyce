/*
FIRST_PERSON_BODY.C

display.first_person_legs: your own body below the first-person view
(first_person_body.h).

The game leaves the unit a view looks out of undrawn in first person
(render_objects.c's object_is_first_person_camera), and its shadow too.
With the setting on, render_objects.c asks here, and draws that unit as any
other (its own model, region permutations, change colors and lighting, its
camouflage, the pose the animation gives it between the last two ticks),
but with the node matrices made here, and without what it holds (the
weapon in its hands, which the first-person weapon shows) or its widgets.
Its shadow is cast whole, as everyone else's, with the same rules.

The model's nodes (first_person_body_rules.c): the legs, the pelvis and the
lower torso are drawn; the chest, arms, neck and head are folded into one
point, the chest's origin, with no size (each vertex weighted to them goes
there, so their triangles have no area and nothing of them is drawn; a
vertex shared with the lower torso is pulled toward it, which closes the
waist). As the view comes near the rest (crouched, looking down) the nodes
too near the eye are folded into their own origins as well, the knees
first. The body is moved back a little along the way the unit faces, more
as the view looks down, so that the eye is above its back, not inside it.

Each split-screen view draws only its own player's body: the view's own
unit is the only one the game leaves undrawn, and every other is drawn whole
in it, as before.

Not drawn: on a map that draws its own (SPV3, or a map whose Lua scripts
pose objects, which is how SPV3 shows its own first-person legs:
lua_scripting_map_poses_objects), with no unit, dead, out of first person
(a third-person or following camera, the debug camera, a cutscene's),
during a cinematic, in a vehicle's seat, zoomed, as Forge's monitor, and
for a model without legs.

Nothing here writes to the game's state: it reads the objects, the tags and
the camera, so the determinism replay's hashes are the same with the
setting on or off (tools/test_determinism.py).

Written from the game's own render, object, unit, model and camera code,
not from an earlier first-person legs (CLEAN-ROOM.md).

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "cseries.h"
#include "camera/director.h"
#include "cutscene/cinematics.h"
#include "game/players.h"
#include "math/real_math.h"
#include "models/model_definitions.h"
#include "models/models.h"
#include "objects/object_definitions.h"
#include "objects/objects.h"
#include "render/render.h"
#include "tag_files/tag_groups.h"
#include "units/units.h"

#include "lua_scripting.h"
#include "spv3_mode.h"

#include "first_person_body.h"
#include "first_person_body_rules.h"

#include <string.h>

/* ---------- prototypes */

/* port_config.c */
int config_boolean(char const *name);
unsigned long config_changes(void);
void platform_log(char const *format, ...);

#ifndef HALO_CONSOLE_NO_FORGE
/* forge.c: the view's player is the monitor (its unit stands hidden) */
boolean forge_monitor_hides_prompt(short local_player_index);
#endif

/* ---------- globals */

/* the nodes of the unit first_person_body_shown said TRUE for last */
static struct
{
	long object_index;
	short node_count;
	short fold_to[FIRST_PERSON_BODY_MAXIMUM_NODES];
	short parents[FIRST_PERSON_BODY_MAXIMUM_NODES];
	real_matrix4x3 matrices[MAXIMUM_NODES_PER_MODEL];
	/* what the log said last, so it says each change once */
	short logged_reason;
} first_person_body_globals = { NONE, 0, { 0 }, { 0 }, { { 0 } }, NONE };

/* ---------- private code */

static boolean first_person_body_setting(
	void)
{
	static unsigned long read_at = (unsigned long)-1;
	static boolean enabled = FALSE;

	/* (read again when Settings changes it) */
	if (read_at != config_changes())
	{
		read_at = config_changes();
		enabled = config_boolean("display.first_person_legs") != 0;
	}
	return enabled;
}

/* the unit's model's nodes, which are drawn (fold_to) and their parents;
how many leg nodes it has */
static int first_person_body_model_nodes(
	long object_index,
	short *node_count)
{
	struct object_datum *object = object_get(object_index);
	struct object_definition *definition = object_definition_get(object->definition_index);
	struct model *model;
	char const *names[FIRST_PERSON_BODY_MAXIMUM_NODES];
	short count;
	short node_index;

	*node_count = 0;
	if (definition->object.model.index == NONE)
		return 0;
	model = model_definition_get(definition->object.model.index);
	/* (no more than the object's own pose holds) */
	count = (short)MIN(model->nodes.count, MAXIMUM_NODES_PER_MODEL);
	count = (short)MIN(count, (short)(object->object.node_matrices.size / sizeof(real_matrix4x3)));
	count = (short)MIN(count, FIRST_PERSON_BODY_MAXIMUM_NODES);
	if (count <= 0)
		return 0;
	for (node_index = 0; node_index < count; node_index++)
	{
		struct model_node *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct model_node);

		names[node_index] = node->name;
		first_person_body_globals.parents[node_index] = node->parent_node_index;
	}
	*node_count = count;
	return first_person_body_select_nodes(names, first_person_body_globals.parents, count,
		first_person_body_globals.fold_to);
}

/* ---------- public code */

boolean first_person_body_shown(
	long object_index)
{
	struct first_person_body_state state;
	enum first_person_body_reason reason;
	short local_player_index = render.local_player_index;
	long player_index;
	long unit_index = NONE;

	memset(&state, 0, sizeof(state));
	state.setting = first_person_body_setting();
	if (!state.setting || object_index == NONE)
		return FALSE;

	if (local_player_index != NONE)
	{
		player_index = local_player_get_player_index(local_player_index);
		if (player_index != NONE)
			unit_index = player_get(player_index)->unit_index;
	}
	/* (only the view's own unit: another object the camera is in, a
	scripted camera's, is left as the game leaves it) */
	if (unit_index != object_index)
		return FALSE;

	state.map_draws_its_own = spv3_mode_active() || lua_scripting_map_poses_objects();
	state.has_unit = TRUE;
	{
		struct unit_datum *unit = unit_get(unit_index);
		short node_count;

		state.dead = TEST_FLAG(unit->object.damage_flags, _object_dead_bit);
		state.first_person = director_get_perspective(local_player_index) == _director_perspective_first_person;
		state.cinematic = cinematic_in_progress();
		state.in_vehicle = unit->object.parent_object_index != NONE;
		state.zoomed = unit->unit.current_zoom_level != NONE;
#ifndef HALO_CONSOLE_NO_FORGE
		state.forge_monitor = forge_monitor_hides_prompt(local_player_index);
#endif
		state.has_legs = first_person_body_model_nodes(unit_index, &node_count) > 0;
		first_person_body_globals.node_count = node_count;
	}

	reason = first_person_body_decide(&state);
	/* (the log says when they are first drawn, and when a map or a model
	leaves them out: once each until that changes, not at each zoom) */
	if (reason == _first_person_body_shown || reason == _first_person_body_map_draws_its_own ||
		reason == _first_person_body_no_legs)
	{
		if (first_person_body_globals.logged_reason != (short)reason)
		{
			platform_log("first-person legs: %s%s", reason == _first_person_body_shown ? "" : "not drawn: ",
				first_person_body_reason_name(reason));
			first_person_body_globals.logged_reason = (short)reason;
		}
	}

	if (reason != _first_person_body_shown)
	{
		first_person_body_globals.object_index = NONE;
		return FALSE;
	}
	first_person_body_globals.object_index = object_index;
	return TRUE;
}

real_matrix4x3 const *first_person_body_node_matrices(
	long object_index)
{
	struct object_datum *object = object_get(object_index);
	real_matrix4x3 const *pose = object_get_node_matrices(object_index);
	real_matrix4x3 *matrices = first_person_body_globals.matrices;
	short count = first_person_body_globals.node_count;
	short fold_to[FIRST_PERSON_BODY_MAXIMUM_NODES];
	float positions[FIRST_PERSON_BODY_MAXIMUM_NODES][3];
	float forward[3], facing[3], offset[3], eye[3];
	short node_index;

	if (first_person_body_globals.object_index != object_index || count <= 0)
		return pose;
	memcpy(matrices, pose, sizeof(real_matrix4x3) * (size_t)count);
	memcpy(fold_to, first_person_body_globals.fold_to, sizeof(short) * (size_t)count);

	/* back from the eye, along the way the unit faces */
	forward[0] = render.camera.forward.i;
	forward[1] = render.camera.forward.j;
	forward[2] = render.camera.forward.k;
	facing[0] = object->object.forward.i;
	facing[1] = object->object.forward.j;
	facing[2] = object->object.forward.k;
	first_person_body_offset(forward, facing, offset);
	for (node_index = 0; node_index < count; node_index++)
	{
		matrices[node_index].position.x += offset[0];
		matrices[node_index].position.y += offset[1];
		matrices[node_index].position.z += offset[2];
		positions[node_index][0] = matrices[node_index].position.x;
		positions[node_index][1] = matrices[node_index].position.y;
		positions[node_index][2] = matrices[node_index].position.z;
	}

	/* the nodes the eye is too near, folded too */
	eye[0] = render.camera.position.x;
	eye[1] = render.camera.position.y;
	eye[2] = render.camera.position.z;
	first_person_body_fold_near_eye((float const (*)[3])positions, first_person_body_globals.parents, count, eye,
		fold_to);

	/* each folded node: no size, at the point it folds into */
	for (node_index = 0; node_index < count; node_index++)
	{
		short into = fold_to[node_index];

		if (into != NONE && into >= 0 && into < count)
		{
			matrices[node_index].scale = 0.f;
			matrices[node_index].position.x = positions[into][0];
			matrices[node_index].position.y = positions[into][1];
			matrices[node_index].position.z = positions[into][2];
		}
	}
	return matrices;
}
