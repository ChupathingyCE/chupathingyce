/*
LINE_OF_SIGHT_TEST.C

Tests of the host's line of sight on a client's reported hits (port/linux/
game/line_of_sight.c), on walls of their own: boxes a segment hits, solid
(a wall) or passable (glass). A segment that starts inside a box does not
hit it (the engine tests front faces only). tools/test_line_of_sight.py
builds and runs them.
*/

#include "../game/line_of_sight.h"

#include <stdio.h>

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

struct box
{
	float low[3];
	float high[3];
	int passable;
};

struct world
{
	struct box const *boxes;
	int count;
	int tests;
};

static int inside(struct box const *box, float const point[3])
{
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		if (point[axis] < box->low[axis] || point[axis] > box->high[axis])
			return 0;
	}
	return 1;
}

/* where along the segment it enters the box (slabs), -1 for not */
static float enters(struct box const *box, float const from[3], float const to[3])
{
	float near_t = 0.0f;
	float far_t = 1.0f;
	int axis;

	if (inside(box, from))
		return -1.0f;
	for (axis = 0; axis < 3; axis++)
	{
		float delta = to[axis] - from[axis];

		if (delta > -1e-9f && delta < 1e-9f)
		{
			if (from[axis] < box->low[axis] || from[axis] > box->high[axis])
				return -1.0f;
		}
		else
		{
			float t0 = (box->low[axis] - from[axis]) / delta;
			float t1 = (box->high[axis] - from[axis]) / delta;

			if (t0 > t1)
			{
				float swap = t0;

				t0 = t1;
				t1 = swap;
			}
			if (t0 > near_t)
				near_t = t0;
			if (t1 < far_t)
				far_t = t1;
			if (near_t > far_t)
				return -1.0f;
		}
	}
	return near_t;
}

static void world_test(void *context, struct line_of_sight_point const *from, struct line_of_sight_point const *to,
	struct line_of_sight_hit *hit)
{
	struct world *world = (struct world *)context;
	float a[3] = { from->x, from->y, from->z };
	float b[3] = { to->x, to->y, to->z };
	int index;

	world->tests++;
	hit->hit = 0;
	hit->t = 1.0f;
	hit->passable = 0;
	for (index = 0; index < world->count; index++)
	{
		float t = enters(&world->boxes[index], a, b);

		if (t >= 0.0f && t <= 1.0f && (!hit->hit || t < hit->t))
		{
			hit->hit = 1;
			hit->t = t;
			hit->passable = world->boxes[index].passable;
		}
	}
}

static struct line_of_sight_query query_of(struct world *world)
{
	struct line_of_sight_query query;

	query.test = world_test;
	query.context = world;
	query.budget = LINE_OF_SIGHT_DEFAULT_BUDGET;
	return query;
}

static struct line_of_sight_point at(float x, float y, float z)
{
	struct line_of_sight_point point;

	point.x = x;
	point.y = y;
	point.z = z;
	return point;
}

/* a biped's sphere about it (world units: a player stands about 0.7 tall) */
static struct line_of_sight_target target_at(float x, float y, float z)
{
	struct line_of_sight_target target;

	target.center = at(x, y, z);
	target.radius = 0.3f;
	target.impact = at(x - 0.25f, y, z);
	target.has_impact = 1;
	return target;
}

/* a shooter at x 0 and a target at x 10, a wall at x 5 between them */
static void test_wall_and_clear(void)
{
	static struct box const wall[] = { { { 4.8f, -5.0f, -1.0f }, { 5.2f, 5.0f, 5.0f }, 0 } };
	struct world blocked = { wall, 1, 0 };
	struct world open = { wall, 0, 0 };
	struct line_of_sight_point eye = at(0.0f, 0.0f, 0.6f);
	struct line_of_sight_target target = target_at(10.0f, 0.0f, 0.4f);
	struct line_of_sight_query query;

	query = query_of(&blocked);
	CHECK(line_of_sight_shot(&query, &eye, 1, &target, NULL) == 0);
	query = query_of(&open);
	CHECK(line_of_sight_shot(&query, &eye, 1, &target, NULL) == 1);
	CHECK(open.tests == 1);
}

/* the target half behind the end of a wall: its center hidden, its edge
seen */
static void test_target_edge(void)
{
	static struct box const corner[] = { { { 4.8f, -5.0f, -1.0f }, { 5.2f, 0.12f, 5.0f }, 0 } };
	struct world world = { corner, 1, 0 };
	struct line_of_sight_point eye = at(0.0f, 0.0f, 0.4f);
	struct line_of_sight_target target = target_at(10.0f, 0.0f, 0.4f);
	struct line_of_sight_query query = query_of(&world);
	struct line_of_sight_target hidden = target_at(10.0f, -2.0f, 0.4f);

	CHECK(line_of_sight_shot(&query, &eye, 1, &target, NULL) == 1);
	query = query_of(&world);
	CHECK(line_of_sight_shot(&query, &eye, 1, &hidden, NULL) == 0);
}

/* glass between (passable), and a wall behind a pane */
static void test_passable(void)
{
	static struct box const glass[] = { { { 4.9f, -5.0f, -1.0f }, { 5.0f, 5.0f, 5.0f }, 1 } };
	static struct box const glass_and_wall[] = {
		{ { 3.9f, -5.0f, -1.0f }, { 4.0f, 5.0f, 5.0f }, 1 },
		{ { 6.0f, -5.0f, -1.0f }, { 6.5f, 5.0f, 5.0f }, 0 },
	};
	struct world world = { glass, 1, 0 };
	struct world walled = { glass_and_wall, 2, 0 };
	struct line_of_sight_point eye = at(0.0f, 0.0f, 0.6f);
	struct line_of_sight_target target = target_at(10.0f, 0.0f, 0.4f);
	struct line_of_sight_query query = query_of(&world);

	CHECK(line_of_sight_shot(&query, &eye, 1, &target, NULL) == 1);
	query = query_of(&walled);
	CHECK(line_of_sight_shot(&query, &eye, 1, &target, NULL) == 0);
}

/* any of the shooter's points will do: a gunner whose seat's camera sits
low behind the vehicle's own cover, its gun above it */
static void test_vehicle_gunner(void)
{
	static struct box const cover[] = { { { 1.0f, -3.0f, -1.0f }, { 1.4f, 3.0f, 0.9f }, 0 } };
	struct world world = { cover, 1, 0 };
	struct line_of_sight_point camera = at(0.0f, 0.0f, 0.5f);
	struct line_of_sight_point points[2];
	struct line_of_sight_target target = target_at(20.0f, 0.0f, 0.4f);
	struct line_of_sight_query query = query_of(&world);

	points[0] = camera;
	points[1] = at(0.8f, 0.0f, 1.3f);
	CHECK(line_of_sight_shot(&query, &camera, 1, &target, NULL) == 0);
	query = query_of(&world);
	CHECK(line_of_sight_shot(&query, points, 2, &target, NULL) == 1);
}

/* a grenade thrown around a corner: no line from its thrower, a clear one
from where it went off; and one going off behind a wall from the target */
static void test_grenade_around_corner(void)
{
	/* (an L: the thrower in the hall along y, the target around the
	corner along x; and a wall along the hall's far side) */
	static struct box const walls[] = {
		{ { 1.0f, 1.2f, -1.0f }, { 20.0f, 20.0f, 5.0f }, 0 },
		{ { 5.0f, 0.5f, -1.0f }, { 12.0f, 0.7f, 5.0f }, 0 },
	};
	struct world world = { walls, 2, 0 };
	struct line_of_sight_point thrower = at(0.0f, 10.0f, 0.6f);
	struct line_of_sight_target target = target_at(8.0f, 0.0f, 0.3f);
	struct line_of_sight_point epicenter = at(6.5f, 0.2f, 0.05f);
	struct line_of_sight_point behind = at(8.0f, 0.9f, 0.05f);
	struct line_of_sight_query query = query_of(&world);

	target.has_impact = 0;
	CHECK(line_of_sight_shot(&query, &thrower, 1, &target, NULL) == 0);
	query = query_of(&world);
	CHECK(line_of_sight_explosion(&query, &epicenter, 0.0f, &target) == 1);
	query = query_of(&world);
	CHECK(line_of_sight_explosion(&query, &behind, 0.0f, &target) == 0);
}

/* an explosion with a core: its spread reaches past a post the direct line
hits */
static void test_explosion_core(void)
{
	static struct box const post[] = { { { 2.9f, -0.2f, -1.0f }, { 3.1f, 0.2f, 5.0f }, 0 } };
	struct world world = { post, 1, 0 };
	struct line_of_sight_point epicenter = at(2.0f, 0.0f, 0.4f);
	struct line_of_sight_target target = target_at(4.0f, 0.0f, 0.4f);
	struct line_of_sight_query query = query_of(&world);

	target.radius = 0.0f;
	target.has_impact = 0;
	CHECK(line_of_sight_explosion(&query, &epicenter, 0.0f, &target) == 0);
	query = query_of(&world);
	CHECK(line_of_sight_explosion(&query, &epicenter, 1.0f, &target) == 1);
}

/* a homing projectile around a pillar it can bend past, a straight one not */
static void test_bend(void)
{
	static struct box const pillar[] = { { { 4.0f, -0.8f, -1.0f }, { 6.0f, 0.8f, 5.0f }, 0 } };
	struct world world = { pillar, 1, 0 };
	struct line_of_sight_point eye = at(0.0f, 0.0f, 0.6f);
	struct line_of_sight_target target = target_at(10.0f, 0.0f, 0.6f);
	struct line_of_sight_bend homing = { 8.0f, 0.0f, 0.0f };
	struct line_of_sight_bend falling = { 0.0f, 0.01f, 0.3f };
	static struct box const low_wall[] = { { { 4.8f, -5.0f, -1.0f }, { 5.2f, 5.0f, 1.0f }, 0 } };
	struct world walled = { low_wall, 1, 0 };
	struct line_of_sight_query query = query_of(&world);
	float across;
	float up;

	CHECK(line_of_sight_shot(&query, &eye, 1, &target, NULL) == 0);
	query = query_of(&world);
	CHECK(line_of_sight_shot(&query, &eye, 1, &target, &homing) == 1);
	/* (an arc over a low wall) */
	query = query_of(&walled);
	CHECK(line_of_sight_shot(&query, &eye, 1, &target, NULL) == 0);
	query = query_of(&walled);
	CHECK(line_of_sight_shot(&query, &eye, 1, &target, &falling) == 1);
	line_of_sight_bend_offsets(&homing, 10.0f, &across, &up);
	CHECK(across > 1.75f && across < 1.76f && up == 0.0f);
	line_of_sight_bend_offsets(&falling, 10.0f, &across, &up);
	CHECK(across == 0.0f && up > 1.38f && up < 1.40f);
	line_of_sight_bend_offsets(NULL, 10.0f, &across, &up);
	CHECK(across == 0.0f && up == 0.0f);
}

/* the budget: a decision out of tests is taken (-1), never refused */
static void test_budget(void)
{
	static struct box const wall[] = { { { 4.8f, -5.0f, -1.0f }, { 5.2f, 5.0f, 5.0f }, 0 } };
	struct world world = { wall, 1, 0 };
	struct line_of_sight_point eyes[8];
	struct line_of_sight_target target = target_at(10.0f, 0.0f, 0.4f);
	struct line_of_sight_query query = query_of(&world);
	int index;

	for (index = 0; index < 8; index++)
		eyes[index] = at(0.0f, 0.1f * index, 0.6f);
	CHECK(line_of_sight_shot(&query, eyes, 8, &target, NULL) == 0);
	CHECK(query.budget > 0);
	query = query_of(&world);
	query.budget = 3;
	CHECK(line_of_sight_shot(&query, eyes, 8, &target, NULL) == -1);
	CHECK(world.tests > 0);
	/* (no shooter's points: nothing to judge by, taken) */
	query = query_of(&world);
	CHECK(line_of_sight_shot(&query, eyes, 0, &target, NULL) == 1);
}

int main(void)
{
	test_wall_and_clear();
	test_target_edge();
	test_passable();
	test_vehicle_gunner();
	test_grenade_around_corner();
	test_explosion_core();
	test_bend();
	test_budget();
	if (failures)
	{
		printf("line_of_sight_test: %d failed\n", failures);
		return 1;
	}
	printf("line_of_sight_test: ok\n");
	return 0;
}
