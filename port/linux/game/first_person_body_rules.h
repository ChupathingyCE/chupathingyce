/*
FIRST_PERSON_BODY_RULES.H

display.first_person_legs (first_person_body.c): the rules that need no
game to decide, so that port/linux/tests/first_person_body_test.c can test
them (tools/test_first_person_legs.py). Nothing here has a dependency but
the C library.

- When your own body is drawn below the first-person view: the setting on,
  the map not drawing its own (SPV3, or a map whose scripts pose objects),
  your unit alive, on foot, in first person, not zoomed, no cinematic and
  not Forge's monitor, and a model with legs.
- Which of the model's nodes are drawn: the legs (thigh, calf, foot, toe),
  the pelvis and the nodes between them and the root, and the first spine
  node above the pelvis (the lower torso). The rest (the chest, arms,
  neck and head) are folded into one point at the lower torso's top,
  where nothing is drawn.
- Which of those are folded too as the camera nears them (crouched,
  looking down): a bone that ends closer to the eye than the near
  distance, or less far below it than the margin, from its start (a knee
  near the eye folds the thigh from the hip), with all of its children.
- How far the body is moved back from the eye, along the way it faces, as
  the view looks down, so that the eye is never inside it.

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef __FIRST_PERSON_BODY_RULES_H
#define __FIRST_PERSON_BODY_RULES_H

/* the most nodes a model is looked at for (MAXIMUM_NODES_PER_MODEL) */
#define FIRST_PERSON_BODY_MAXIMUM_NODES 64

/* (in world units, 10 feet each: 0.1 is a foot. A Spartan's eye stands
about 0.26 above its hips and 0.4 above its knees; crouched and looking
down, the raised knee comes to about 0.15 from the eye) */
/* a bone that ends nearer the eye than this is folded */
#define FIRST_PERSON_BODY_NEAR_DISTANCE 0.20f
/* the same for one that ends less than this far below the eye */
#define FIRST_PERSON_BODY_BELOW_EYE 0.17f
/* the body moved back from the eye looking straight ahead, and more as the
view looks down, to this at straight down */
#define FIRST_PERSON_BODY_BACK_LEVEL 0.04f
#define FIRST_PERSON_BODY_BACK_DOWN 0.14f

/* why the body is or is not drawn, the first that holds */
enum first_person_body_reason
{
	_first_person_body_shown = 0,
	_first_person_body_setting_off,
	_first_person_body_map_draws_its_own,
	_first_person_body_no_unit,
	_first_person_body_dead,
	_first_person_body_not_first_person,
	_first_person_body_cinematic,
	_first_person_body_in_vehicle,
	_first_person_body_zoomed,
	_first_person_body_forge_monitor,
	_first_person_body_no_legs,
	NUMBER_OF_FIRST_PERSON_BODY_REASONS
};

struct first_person_body_state
{
	int setting; /* display.first_person_legs */
	int map_draws_its_own; /* SPV3, or a map's scripts pose objects */
	int has_unit; /* the view's player has a unit */
	int dead;
	int first_person; /* the director's first-person camera, and not the
		scripted camera of a cutscene */
	int cinematic; /* a cinematic or cutscene plays */
	int in_vehicle; /* in a vehicle's seat (a parent object) */
	int zoomed;
	int forge_monitor; /* the view's player is Forge's monitor */
	int has_legs; /* the unit's model has leg nodes */
};

enum first_person_body_reason first_person_body_decide(
	struct first_person_body_state const *state);
char const *first_person_body_reason_name(
	enum first_person_body_reason reason);

/* The nodes drawn, from the model's node names and parents (-1 for the
root): fold_to[node] is -1 for a node drawn, else the node whose origin it
is folded into (every folded vertex there, so nothing of it is drawn): the
lowest node folded whose parent is kept, the chest's. Returns how many leg
nodes the model has (0: none, and nothing is drawn). */
int first_person_body_select_nodes(
	char const *const *names,
	short const *parents,
	int count,
	short *fold_to);

/* the bones that end too near the eye folded as well, each from its
start into the start's origin, its children with it (a node's parent comes
before it); not a bone from the root, which stays.
positions: each node's origin, x y z (z up), as drawn; eye: the camera's
position. Returns how many were folded here. */
int first_person_body_fold_near_eye(
	float const (*positions)[3],
	short const *parents,
	int count,
	float const eye[3],
	short *fold_to);

/* how far to move the body: back along the way the unit faces on the
ground (facing, z up; its length does not matter), more the further the
view looks down (forward: the view's, unit length). A facing with no way on
the ground leaves the body where it is. */
void first_person_body_offset(
	float const forward[3],
	float const facing[3],
	float offset[3]);

#endif
