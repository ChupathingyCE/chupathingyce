/*
FIRST_PERSON_BODY_RULES.C

display.first_person_legs: when your own body is drawn below the view,
which of its nodes are, and where (first_person_body_rules.h).

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "first_person_body_rules.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

/* ---------- constants */

/* the words a leg node's name has (Halo's bipeds: "bip01 l thigh",
"bip01 r calf", "bip01 l foot", "bip01 r toe0") */
static char const *const leg_words[] =
{
	"thigh", "calf", "foot", "toe", "leg", "knee", "shin", "ankle",
};

/* ---------- private code */

/* whether a node's name holds a word, any case */
static int name_has_word(
	char const *name,
	char const *word)
{
	size_t length = strlen(word);
	char const *at;

	if (!name)
		return 0;
	for (at = name; *at; at++)
	{
		size_t index;

		for (index = 0; index < length; index++)
		{
			if (!at[index] || tolower((unsigned char)at[index]) != word[index])
				break;
		}
		if (index == length)
			return 1;
	}
	return 0;
}

static int name_is_leg(
	char const *name)
{
	size_t index;

	for (index = 0; index < sizeof(leg_words) / sizeof(leg_words[0]); index++)
	{
		if (name_has_word(name, leg_words[index]))
			return 1;
	}
	return 0;
}

static int parent_of(
	short const *parents,
	int count,
	int node)
{
	int parent = parents[node];

	/* (a parent out of the list, or not before its child, is taken as
	none: a node's parent comes first in Halo's models) */
	return (parent >= 0 && parent < count && parent < node) ? parent : -1;
}

/* ---------- public code */

enum first_person_body_reason first_person_body_decide(
	struct first_person_body_state const *state)
{
	if (!state->setting)
		return _first_person_body_setting_off;
	if (state->map_draws_its_own)
		return _first_person_body_map_draws_its_own;
	if (!state->has_unit)
		return _first_person_body_no_unit;
	if (state->dead)
		return _first_person_body_dead;
	if (!state->first_person)
		return _first_person_body_not_first_person;
	if (state->cinematic)
		return _first_person_body_cinematic;
	if (state->in_vehicle)
		return _first_person_body_in_vehicle;
	if (state->zoomed)
		return _first_person_body_zoomed;
	if (state->forge_monitor)
		return _first_person_body_forge_monitor;
	if (!state->has_legs)
		return _first_person_body_no_legs;
	return _first_person_body_shown;
}

char const *first_person_body_reason_name(
	enum first_person_body_reason reason)
{
	static char const *const names[NUMBER_OF_FIRST_PERSON_BODY_REASONS] =
	{
		"drawn", "setting off", "the map draws its own", "no unit", "dead", "not first person",
		"cinematic", "in a vehicle", "zoomed", "Forge monitor", "no legs",
	};

	return (reason >= 0 && reason < NUMBER_OF_FIRST_PERSON_BODY_REASONS) ? names[reason] : "?";
}

int first_person_body_select_nodes(
	char const *const *names,
	short const *parents,
	int count,
	short *fold_to)
{
	unsigned char keep[FIRST_PERSON_BODY_MAXIMUM_NODES];
	int node;
	int legs = 0;
	int lower_torso = -1;
	int fold_node = -1;

	if (count <= 0)
		return 0;
	if (count > FIRST_PERSON_BODY_MAXIMUM_NODES)
		count = FIRST_PERSON_BODY_MAXIMUM_NODES;
	memset(keep, 0, sizeof(keep));

	/* the legs, and every node between them and the root (the pelvis, the
	model's frame) */
	for (node = 0; node < count; node++)
	{
		if (name_is_leg(names[node]))
		{
			int up;

			legs++;
			for (up = node; up != -1 && !keep[up]; up = parent_of(parents, count, up))
				keep[up] = 1;
		}
	}

	/* the lower torso: the first spine node whose parent is kept (the
	pelvis), so that the body ends above the belt, not at an open hip */
	for (node = 0; legs && node < count; node++)
	{
		int parent = parent_of(parents, count, node);

		if (!keep[node] && parent != -1 && keep[parent] && name_has_word(names[node], "spine"))
		{
			keep[node] = 1;
			lower_torso = node;
			break;
		}
	}

	/* the point the rest folds into: the first node folded whose parent is
	kept, the chest's (above the lower torso) if there is one */
	for (node = 0; legs && node < count; node++)
	{
		int parent = parent_of(parents, count, node);

		if (!keep[node] && parent != -1 && keep[parent] && (lower_torso == -1 || parent == lower_torso))
		{
			fold_node = node;
			break;
		}
	}
	for (node = 0; legs && fold_node == -1 && node < count; node++)
	{
		if (!keep[node])
			fold_node = node;
	}

	for (node = 0; node < count; node++)
		fold_to[node] = (short)(legs && keep[node] ? -1 : (fold_node != -1 ? fold_node : node));
	return legs;
}

int first_person_body_fold_near_eye(
	float const (*positions)[3],
	short const *parents,
	int count,
	float const eye[3],
	short *fold_to)
{
	unsigned char starts[FIRST_PERSON_BODY_MAXIMUM_NODES];
	int node;
	int folded = 0;

	if (count > FIRST_PERSON_BODY_MAXIMUM_NODES)
		count = FIRST_PERSON_BODY_MAXIMUM_NODES;
	memset(starts, 0, sizeof(starts));

	/* the bones that end near the eye, each folded from its start: a knee
	near the eye folds the thigh, whose plate is what fills the view, from
	the hip down. A bone from the root (the pelvis, the model's frame, which
	is never folded: it holds the rest up) is not looked at: its start is
	the root's, as near as the hips always are, and its end is the next
	bone's start */
	for (node = 0; node < count; node++)
	{
		int parent = parent_of(parents, count, node);
		float dx, dy, dz;

		if (fold_to[node] != -1 || parent == -1 || parent_of(parents, count, parent) == -1)
			continue;
		dx = positions[node][0] - eye[0];
		dy = positions[node][1] - eye[1];
		dz = positions[node][2] - eye[2];
		if (dx * dx + dy * dy + dz * dz < FIRST_PERSON_BODY_NEAR_DISTANCE * FIRST_PERSON_BODY_NEAR_DISTANCE ||
			-dz < FIRST_PERSON_BODY_BELOW_EYE)
		{
			starts[parent] = 1;
		}
	}

	/* each folded into its own origin, and its children with it (a node's
	parent comes before it) */
	for (node = 0; node < count; node++)
	{
		int parent = parent_of(parents, count, node);

		if (fold_to[node] != -1)
			continue;
		if (parent != -1 && fold_to[parent] != -1)
		{
			fold_to[node] = fold_to[parent];
			folded++;
		}
		else if (starts[node])
		{
			fold_to[node] = (short)node;
			folded++;
		}
	}
	return folded;
}

void first_person_body_offset(
	float const forward[3],
	float const facing[3],
	float offset[3])
{
	float length = sqrtf(facing[0] * facing[0] + facing[1] * facing[1]);
	float down = -forward[2];
	float back;

	offset[0] = offset[1] = offset[2] = 0.f;
	if (!(length > 1e-4f))
		return;
	if (down < 0.f)
		down = 0.f;
	if (down > 1.f)
		down = 1.f;
	back = FIRST_PERSON_BODY_BACK_LEVEL + (FIRST_PERSON_BODY_BACK_DOWN - FIRST_PERSON_BODY_BACK_LEVEL) * down;
	offset[0] = -facing[0] / length * back;
	offset[1] = -facing[1] / length * back;
}
