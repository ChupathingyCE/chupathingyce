/*
FIRST_PERSON_BODY_TEST.C

Tests of display.first_person_legs' rules (port/linux/game/
first_person_body_rules.h): when your own body is drawn below the view
(the setting, a map that draws its own, dead, out of first person, a
cinematic, a vehicle, zoomed, Forge's monitor, a model with no legs), which
of a Spartan's nodes are drawn and where the rest fold, the nodes folded as
the eye nears them (crouched, looking down), and how far the body moves
back. Built and run by tools/test_first_person_legs.py; exits nonzero on a
failure.

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "../game/first_person_body_rules.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

/* Halo's Spartan (the cyborg's nodes, in the model's order) */
enum
{
	_pelvis, _l_thigh, _r_thigh, _spine, _l_calf, _r_calf, _spine1, _l_clavicle, _l_foot, _neck,
	_r_clavicle, _r_foot, _head, _l_upperarm, _r_upperarm, _l_forearm, _r_forearm, _l_hand, _r_hand,
	SPARTAN_NODES
};

static char const *const spartan_names[SPARTAN_NODES] =
{
	"bip01 pelvis", "bip01 l thigh", "bip01 r thigh", "bip01 spine", "bip01 l calf", "bip01 r calf",
	"bip01 spine1", "bip01 l clavicle", "bip01 l foot", "bip01 neck", "bip01 r clavicle", "bip01 r foot",
	"bip01 head", "bip01 l upperarm", "bip01 r upperarm", "bip01 l forearm", "bip01 r forearm", "bip01 l hand",
	"bip01 r hand",
};

static short const spartan_parents[SPARTAN_NODES] =
{
	-1, _pelvis, _pelvis, _pelvis, _l_thigh, _r_thigh, _spine, _spine1, _l_calf, _spine1,
	_spine1, _r_calf, _neck, _l_clavicle, _r_clavicle, _l_upperarm, _r_upperarm, _l_forearm, _r_forearm,
};

static struct first_person_body_state shown_state(void)
{
	struct first_person_body_state state;

	memset(&state, 0, sizeof(state));
	state.setting = 1;
	state.has_unit = 1;
	state.first_person = 1;
	state.has_legs = 1;
	return state;
}

static void test_when_drawn(void)
{
	struct first_person_body_state state = shown_state();

	CHECK(first_person_body_decide(&state) == _first_person_body_shown);

	/* (each reason alone hides it) */
	state = shown_state(); state.setting = 0;
	CHECK(first_person_body_decide(&state) == _first_person_body_setting_off);
	state = shown_state(); state.map_draws_its_own = 1;
	CHECK(first_person_body_decide(&state) == _first_person_body_map_draws_its_own);
	state = shown_state(); state.has_unit = 0;
	CHECK(first_person_body_decide(&state) == _first_person_body_no_unit);
	state = shown_state(); state.dead = 1;
	CHECK(first_person_body_decide(&state) == _first_person_body_dead);
	state = shown_state(); state.first_person = 0;
	CHECK(first_person_body_decide(&state) == _first_person_body_not_first_person);
	state = shown_state(); state.cinematic = 1;
	CHECK(first_person_body_decide(&state) == _first_person_body_cinematic);
	state = shown_state(); state.in_vehicle = 1;
	CHECK(first_person_body_decide(&state) == _first_person_body_in_vehicle);
	state = shown_state(); state.zoomed = 1;
	CHECK(first_person_body_decide(&state) == _first_person_body_zoomed);
	state = shown_state(); state.forge_monitor = 1;
	CHECK(first_person_body_decide(&state) == _first_person_body_forge_monitor);
	state = shown_state(); state.has_legs = 0;
	CHECK(first_person_body_decide(&state) == _first_person_body_no_legs);

	/* (off by default: a state of nothing is off, whatever else holds) */
	memset(&state, 0, sizeof(state));
	CHECK(first_person_body_decide(&state) == _first_person_body_setting_off);
	state = shown_state(); state.setting = 0; state.zoomed = 1; state.in_vehicle = 1;
	CHECK(first_person_body_decide(&state) == _first_person_body_setting_off);
	/* (the map's own legs win over everything but the setting) */
	state = shown_state(); state.map_draws_its_own = 1; state.dead = 1;
	CHECK(first_person_body_decide(&state) == _first_person_body_map_draws_its_own);

	/* (each reason has a name, for the log) */
	{
		int reason;

		for (reason = 0; reason < NUMBER_OF_FIRST_PERSON_BODY_REASONS; reason++)
			CHECK(strcmp(first_person_body_reason_name((enum first_person_body_reason)reason), "?") != 0);
		CHECK(strcmp(first_person_body_reason_name(NUMBER_OF_FIRST_PERSON_BODY_REASONS), "?") == 0);
	}
}

static void test_spartan_nodes(void)
{
	short fold_to[SPARTAN_NODES];
	int node;

	CHECK(first_person_body_select_nodes(spartan_names, spartan_parents, SPARTAN_NODES, fold_to) == 6);
	/* (drawn: the pelvis, legs and the lower torso) */
	for (node = 0; node < SPARTAN_NODES; node++)
	{
		int drawn = node == _pelvis || node == _l_thigh || node == _r_thigh || node == _l_calf ||
			node == _r_calf || node == _l_foot || node == _r_foot || node == _spine;

		CHECK((fold_to[node] == -1) == drawn);
		/* (the rest folds into the chest's origin) */
		if (!drawn)
			CHECK(fold_to[node] == _spine1);
	}
}

static void test_other_models(void)
{
	short fold_to[8];

	/* (no legs: a model with none draws nothing, as the Forge monitor's
	or a vehicle's) */
	{
		static char const *const names[3] = { "frame", "eye", "antenna" };
		static short const parents[3] = { -1, 0, 0 };

		CHECK(first_person_body_select_nodes(names, parents, 3, fold_to) == 0);
		CHECK(fold_to[0] != -1 && fold_to[1] != -1 && fold_to[2] != -1);
	}
	/* (a model with a frame above the pelvis keeps the frame; names in any
	case; no spine: the rest folds into the first folded node under a kept
	one) */
	{
		static char const *const names[6] = { "Frame", "Pelvis", "L Thigh", "R THIGH", "Chest", "Head" };
		static short const parents[6] = { -1, 0, 1, 1, 1, 4 };

		CHECK(first_person_body_select_nodes(names, parents, 6, fold_to) == 2);
		CHECK(fold_to[0] == -1 && fold_to[1] == -1 && fold_to[2] == -1 && fold_to[3] == -1);
		CHECK(fold_to[4] == 4 && fold_to[5] == 4);
	}
	/* (a parent out of order or out of range counts as none: no reading
	past the list) */
	{
		static char const *const names[3] = { "bip01 l thigh", "bip01 pelvis", "bip01 head" };
		static short const parents[3] = { 1, 99, -5 };

		CHECK(first_person_body_select_nodes(names, parents, 3, fold_to) == 1);
		CHECK(fold_to[0] == -1);
	}
	/* (nothing) */
	CHECK(first_person_body_select_nodes(NULL, NULL, 0, fold_to) == 0);
}

/* the Spartan standing (the eye at 0.62, hips at 0.36, knees at 0.21,
feet on the ground), or crouched with the left knee raised */
static void spartan_pose(float (*positions)[3], int crouched)
{
	int node;

	for (node = 0; node < SPARTAN_NODES; node++)
	{
		positions[node][0] = 0.f;
		positions[node][1] = 0.f;
		positions[node][2] = 0.5f;
	}
	positions[_pelvis][2] = crouched ? 0.24f : 0.36f;
	positions[_l_thigh][1] = 0.04f; positions[_l_thigh][2] = positions[_pelvis][2];
	positions[_r_thigh][1] = -0.04f; positions[_r_thigh][2] = positions[_pelvis][2];
	positions[_spine][2] = positions[_pelvis][2] + 0.04f;
	positions[_l_calf][1] = 0.04f; positions[_l_calf][2] = crouched ? 0.25f : 0.21f;
	positions[_l_calf][0] = crouched ? 0.12f : 0.f;
	positions[_r_calf][1] = -0.04f; positions[_r_calf][2] = crouched ? 0.04f : 0.21f;
	positions[_r_calf][0] = crouched ? -0.1f : 0.f;
	positions[_l_foot][1] = 0.04f; positions[_l_foot][2] = 0.03f; positions[_l_foot][0] = crouched ? 0.12f : 0.f;
	positions[_r_foot][1] = -0.04f; positions[_r_foot][2] = 0.03f; positions[_r_foot][0] = crouched ? -0.2f : 0.f;
}

static void test_near_eye(void)
{
	float positions[SPARTAN_NODES][3];
	short fold_to[SPARTAN_NODES];
	float standing_eye[3] = { 0.f, 0.f, 0.62f };
	float crouched_eye[3] = { 0.05f, 0.f, 0.40f };

	/* (standing: nothing more folds) */
	spartan_pose(positions, 0);
	first_person_body_select_nodes(spartan_names, spartan_parents, SPARTAN_NODES, fold_to);
	CHECK(first_person_body_fold_near_eye((float const (*)[3])positions, spartan_parents, SPARTAN_NODES,
		standing_eye, fold_to) == 0);
	CHECK(fold_to[_l_thigh] == -1 && fold_to[_l_calf] == -1 && fold_to[_l_foot] == -1 && fold_to[_spine] == -1);

	/* (crouched: the raised knee near the eye folds the thigh from the hip,
	with the shin and foot, into the hip; the right leg, down on its knee,
	the lower torso and the pelvis stay) */
	spartan_pose(positions, 1);
	first_person_body_select_nodes(spartan_names, spartan_parents, SPARTAN_NODES, fold_to);
	CHECK(first_person_body_fold_near_eye((float const (*)[3])positions, spartan_parents, SPARTAN_NODES,
		crouched_eye, fold_to) == 3);
	CHECK(fold_to[_l_thigh] == _l_thigh && fold_to[_l_calf] == _l_thigh && fold_to[_l_foot] == _l_thigh);
	CHECK(fold_to[_spine] == -1 && fold_to[_pelvis] == -1);
	CHECK(fold_to[_r_thigh] == -1 && fold_to[_r_calf] == -1 && fold_to[_r_foot] == -1);
	/* (folded already by the selection: unchanged) */
	CHECK(fold_to[_head] == _spine1 && fold_to[_r_hand] == _spine1);

	/* (the eye down at the knees: the root and the bones from it are not
	looked at, so the pelvis and the lower torso stay; the knees near the
	eye fold the legs from the hips) */
	{
		float eye[3];

		spartan_pose(positions, 0);
		eye[0] = 0.f;
		eye[1] = -0.04f;
		eye[2] = 0.1f;
		first_person_body_select_nodes(spartan_names, spartan_parents, SPARTAN_NODES, fold_to);
		first_person_body_fold_near_eye((float const (*)[3])positions, spartan_parents, SPARTAN_NODES, eye, fold_to);
		CHECK(fold_to[_pelvis] == -1 && fold_to[_spine] == -1);
		CHECK(fold_to[_r_thigh] == _r_thigh && fold_to[_r_calf] == _r_thigh && fold_to[_r_foot] == _r_thigh);
		CHECK(fold_to[_l_thigh] == _l_thigh && fold_to[_l_foot] == _l_thigh);
	}
}

static int near(float a, float b)
{
	return fabsf(a - b) < 1e-4f;
}

static void test_offset(void)
{
	float offset[3];
	float level[3] = { 1.f, 0.f, 0.f };
	float down[3] = { 0.f, 0.f, -1.f };
	float up[3] = { 0.f, 0.f, 1.f };
	float half[3] = { 0.70710678f, 0.f, -0.70710678f };
	float facing_x[3] = { 1.f, 0.f, 0.f };
	float facing_y[3] = { 0.f, 2.f, 0.f };
	float facing_none[3] = { 0.f, 0.f, 1.f };

	/* (back along the facing, flat) */
	first_person_body_offset(level, facing_x, offset);
	CHECK(near(offset[0], -FIRST_PERSON_BODY_BACK_LEVEL) && near(offset[1], 0.f) && near(offset[2], 0.f));
	first_person_body_offset(down, facing_x, offset);
	CHECK(near(offset[0], -FIRST_PERSON_BODY_BACK_DOWN) && near(offset[2], 0.f));
	first_person_body_offset(half, facing_x, offset);
	CHECK(offset[0] < -FIRST_PERSON_BODY_BACK_LEVEL && offset[0] > -FIRST_PERSON_BODY_BACK_DOWN);
	/* (looking up: as looking ahead) */
	first_person_body_offset(up, facing_x, offset);
	CHECK(near(offset[0], -FIRST_PERSON_BODY_BACK_LEVEL));
	/* (a facing's length does not matter) */
	first_person_body_offset(down, facing_y, offset);
	CHECK(near(offset[0], 0.f) && near(offset[1], -FIRST_PERSON_BODY_BACK_DOWN));
	/* (no way on the ground: not moved) */
	first_person_body_offset(down, facing_none, offset);
	CHECK(near(offset[0], 0.f) && near(offset[1], 0.f) && near(offset[2], 0.f));
	/* (it never moves the body toward the eye's spot, nor much) */
	CHECK(FIRST_PERSON_BODY_BACK_LEVEL > 0.f && FIRST_PERSON_BODY_BACK_DOWN >= FIRST_PERSON_BODY_BACK_LEVEL &&
		FIRST_PERSON_BODY_BACK_DOWN < 0.25f);
}

int main(void)
{
	test_when_drawn();
	test_spartan_nodes();
	test_other_models();
	test_near_eye();
	test_offset();
	if (failures)
	{
		printf("%d failed\n", failures);
		return 1;
	}
	printf("all passed\n");
	return 0;
}
