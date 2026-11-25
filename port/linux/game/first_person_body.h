/*
FIRST_PERSON_BODY.H

display.first_person_legs (first_person_body.c): in first person, your own
body below the view, the legs and lower torso of your unit's own model as
everyone else sees it posed (its colors, its animations), the rest of it
(chest, arms, head) folded away so that it never fills the view. Drawing
only: the game, saved games and the network are as before.

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef __FIRST_PERSON_BODY_H
#define __FIRST_PERSON_BODY_H

/* (render_objects.c) whether the unit the view being drawn
(render.local_player_index) looks out of is drawn as its body below the
view; FALSE for any other object */
boolean first_person_body_shown(
	long object_index);

/* (render_objects.c, after first_person_body_shown said TRUE) the unit's
node matrices for its body below the view: its pose as drawn, moved back a
little from the eye, the upper body and any node near the eye folded. Valid
until the next call */
real_matrix4x3 const *first_person_body_node_matrices(
	long object_index);

#endif
