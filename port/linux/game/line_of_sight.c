/*
LINE_OF_SIGHT.C

Whether a client's reported hit had a clear line to its target
(line_of_sight.h).
*/

#include "line_of_sight.h"

#include <math.h>
#include <stddef.h>

/* ---------- constants */

/* how far past a passable surface a segment goes on from (world units) */
#define PASS_STEP 0.01f
/* jittered shooter points are tried from this many of the first points */
#define JITTERED_STARTS 4

/* ---------- private code */

static struct line_of_sight_point point_along(
	struct line_of_sight_point const *from,
	struct line_of_sight_point const *to,
	float t)
{
	struct line_of_sight_point point;

	point.x = from->x + (to->x - from->x) * t;
	point.y = from->y + (to->y - from->y) * t;
	point.z = from->z + (to->z - from->z) * t;
	return point;
}

static struct line_of_sight_point point_offset(
	struct line_of_sight_point const *point,
	float const direction[3],
	float scale)
{
	struct line_of_sight_point result;

	result.x = point->x + direction[0] * scale;
	result.y = point->y + direction[1] * scale;
	result.z = point->z + direction[2] * scale;
	return result;
}

static float distance_between(
	struct line_of_sight_point const *a,
	struct line_of_sight_point const *b)
{
	float dx = b->x - a->x;
	float dy = b->y - a->y;
	float dz = b->z - a->z;

	return sqrtf(dx * dx + dy * dy + dz * dz);
}

/* across: a unit vector across the line from one point to the other, level
when it can be; up: one across both (up when the line is level) */
static void line_axes(
	struct line_of_sight_point const *from,
	struct line_of_sight_point const *to,
	float across[3],
	float up[3])
{
	float forward[3];
	float length;

	forward[0] = to->x - from->x;
	forward[1] = to->y - from->y;
	forward[2] = to->z - from->z;
	length = sqrtf(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
	if (length < 1e-6f)
	{
		across[0] = 1.0f, across[1] = 0.0f, across[2] = 0.0f;
		up[0] = 0.0f, up[1] = 0.0f, up[2] = 1.0f;
		return;
	}
	forward[0] /= length, forward[1] /= length, forward[2] /= length;
	/* (across = up x forward, level) */
	across[0] = -forward[1];
	across[1] = forward[0];
	across[2] = 0.0f;
	length = sqrtf(across[0] * across[0] + across[1] * across[1]);
	if (length < 1e-6f)
	{
		/* (straight up or down) */
		across[0] = 1.0f, across[1] = 0.0f;
		length = 1.0f;
	}
	across[0] /= length, across[1] /= length;
	/* (up = forward x across) */
	up[0] = forward[1] * across[2] - forward[2] * across[1];
	up[1] = forward[2] * across[0] - forward[0] * across[2];
	up[2] = forward[0] * across[1] - forward[1] * across[0];
	if (up[2] < 0.0f)
		up[0] = -up[0], up[1] = -up[1], up[2] = -up[2];
}

/* ---------- public code */

int line_of_sight_segment_clear(
	struct line_of_sight_query *query,
	struct line_of_sight_point const *from,
	struct line_of_sight_point const *to)
{
	struct line_of_sight_point start = *from;
	float length = distance_between(from, to);
	int pass;

	for (pass = 0; pass <= LINE_OF_SIGHT_MAXIMUM_PASSES; pass++)
	{
		struct line_of_sight_hit hit = { 0, 1.0f, 0 };
		float remaining;
		float step;

		if (query->budget <= 0)
			return -1;
		query->budget--;
		query->test(query->context, &start, to, &hit);
		if (!hit.hit)
			return 1;
		if (!hit.passable || !(hit.t >= 0.0f && hit.t <= 1.0f))
			return 0;
		/* (on past what lets it through, a little beyond the hit) */
		remaining = distance_between(&start, to);
		if (remaining <= PASS_STEP || length <= 0.0f)
			return 1;
		step = hit.t + PASS_STEP / remaining;
		if (step >= 1.0f)
			return 1;
		start = point_along(&start, to, step);
	}
	return 0;
}

int line_of_sight_target_points(
	struct line_of_sight_target const *target,
	struct line_of_sight_point const *from,
	struct line_of_sight_point points[LINE_OF_SIGHT_TARGET_POINTS])
{
	float across[3];
	float up[3];
	float radius = (target->radius > 0.0f ? target->radius : 0.0f) + LINE_OF_SIGHT_TARGET_TOLERANCE;
	static float const vertical[3] = { 0.0f, 0.0f, 1.0f };
	int count = 0;

	line_axes(from, &target->center, across, up);
	points[count++] = target->center;
	if (target->has_impact)
		points[count++] = target->impact;
	points[count++] = point_offset(&target->center, across, radius);
	points[count++] = point_offset(&target->center, across, -radius);
	points[count++] = point_offset(&target->center, up, radius);
	points[count++] = point_offset(&target->center, up, -radius);
	points[count++] = point_offset(&target->center, vertical, radius);
	return count;
}

void line_of_sight_bend_offsets(
	struct line_of_sight_bend const *bend,
	float distance,
	float *across,
	float *up)
{
	*across = 0.0f;
	*up = 0.0f;
	if (!bend || !(distance > 0.0f))
		return;
	/* (homing: the sagitta of an arc of its turning radius over the
	distance, a half of it at most) */
	if (bend->turn_radius > 0.0f)
	{
		float half = 0.5f * distance;

		if (half >= bend->turn_radius)
			*across = half;
		else
			*across = bend->turn_radius - sqrtf(bend->turn_radius * bend->turn_radius - half * half);
	}
	/* (falling: the height of the arc above its chord that lands it,
	g d^2 / 8 v^2, a half of the distance at most) */
	if (bend->gravity > 0.0f)
	{
		float rise = bend->slowest_speed > 0.0f ?
			bend->gravity * distance * distance / (8.0f * bend->slowest_speed * bend->slowest_speed) : distance;

		*up = rise < 0.5f * distance ? rise : 0.5f * distance;
	}
}

int line_of_sight_shot(
	struct line_of_sight_query *query,
	struct line_of_sight_point const *starts,
	int start_count,
	struct line_of_sight_target const *target,
	struct line_of_sight_bend const *bend)
{
	struct line_of_sight_point points[LINE_OF_SIGHT_TARGET_POINTS];
	int index;
	int point_index;
	int clear;

	if (start_count <= 0)
		return 1;
	/* each point to the center, then to the rest */
	for (index = 0; index < start_count; index++)
	{
		clear = line_of_sight_segment_clear(query, &starts[index], &target->center);
		if (clear)
			return clear;
	}
	for (index = 0; index < start_count; index++)
	{
		int count = line_of_sight_target_points(target, &starts[index], points);

		for (point_index = 1; point_index < count; point_index++)
		{
			clear = line_of_sight_segment_clear(query, &starts[index], &points[point_index]);
			if (clear)
				return clear;
		}
	}
	/* the first points a little to either side and above */
	for (index = 0; index < start_count && index < JITTERED_STARTS; index++)
	{
		float across[3];
		float up[3];
		struct line_of_sight_point jittered[3];
		int jitter;

		line_axes(&starts[index], &target->center, across, up);
		jittered[0] = point_offset(&starts[index], across, LINE_OF_SIGHT_SHOOTER_TOLERANCE);
		jittered[1] = point_offset(&starts[index], across, -LINE_OF_SIGHT_SHOOTER_TOLERANCE);
		jittered[2] = point_offset(&starts[index], up, LINE_OF_SIGHT_SHOOTER_TOLERANCE);
		for (jitter = 0; jitter < 3; jitter++)
		{
			int count = line_of_sight_target_points(target, &jittered[jitter], points);

			for (point_index = 0; point_index < count; point_index++)
			{
				clear = line_of_sight_segment_clear(query, &jittered[jitter], &points[point_index]);
				if (clear)
					return clear;
			}
		}
	}
	/* a path that bends: through a point off the middle of the line, as far
	as it can bend there and half that */
	if (bend)
	{
		for (index = 0; index < start_count; index++)
		{
			float across_offset;
			float up_offset;
			float across[3];
			float up[3];
			struct line_of_sight_point middle = point_along(&starts[index], &target->center, 0.5f);
			int direction;

			line_of_sight_bend_offsets(bend, distance_between(&starts[index], &target->center), &across_offset,
				&up_offset);
			if (across_offset < LINE_OF_SIGHT_SHOOTER_TOLERANCE && up_offset < LINE_OF_SIGHT_SHOOTER_TOLERANCE)
				continue;
			line_axes(&starts[index], &target->center, across, up);
			{
				/* (homing bends either way, falling up: each as far as it can
				and half that) */
				float const lift = up_offset > across_offset ? up_offset : across_offset;
				float const *axes[4] = { across, across, up, up };
				float const amounts[4] = { across_offset, -across_offset, lift, -across_offset };

				for (direction = 0; direction < 8; direction++)
				{
					float amount = amounts[direction & 3] * (direction < 4 ? 1.0f : 0.5f);
					struct line_of_sight_point bend_point;

					if (fabsf(amount) < LINE_OF_SIGHT_SHOOTER_TOLERANCE)
						continue;
					bend_point = point_offset(&middle, axes[direction & 3], amount);
					clear = line_of_sight_segment_clear(query, &starts[index], &bend_point);
					if (clear < 0)
						return clear;
					if (!clear)
						continue;
					clear = line_of_sight_segment_clear(query, &bend_point, &target->center);
					if (clear)
						return clear;
					if (target->has_impact)
					{
						clear = line_of_sight_segment_clear(query, &bend_point, &target->impact);
						if (clear)
							return clear;
					}
				}
			}
		}
	}
	return 0;
}

int line_of_sight_explosion(
	struct line_of_sight_query *query,
	struct line_of_sight_point const *epicenter,
	float core_radius,
	struct line_of_sight_target const *target)
{
	struct line_of_sight_point points[LINE_OF_SIGHT_TARGET_POINTS];
	int count = line_of_sight_target_points(target, epicenter, points);
	int index;
	int clear;

	for (index = 0; index < count; index++)
	{
		clear = line_of_sight_segment_clear(query, epicenter, &points[index]);
		if (clear)
			return clear;
	}
	/* (the core's spread, damage.c: a point the core reaches across the line
	to the target, stopped short at what it meets, then on to the center) */
	if (core_radius > 0.0f)
	{
		float across[3];
		float up[3];
		int ray;

		line_axes(epicenter, &target->center, across, up);
		for (ray = 0; ray < 4; ray++)
		{
			float const *axis = ray < 2 ? across : up;
			struct line_of_sight_point reached = point_offset(epicenter, axis, (ray & 1) ? -core_radius : core_radius);
			struct line_of_sight_hit hit = { 0, 1.0f, 0 };

			if (query->budget <= 0)
				return -1;
			query->budget--;
			query->test(query->context, epicenter, &reached, &hit);
			if (hit.hit && !hit.passable && hit.t >= 0.0f && hit.t <= 1.0f)
				reached = point_along(epicenter, &reached, hit.t * 0.99f);
			clear = line_of_sight_segment_clear(query, &reached, &target->center);
			if (clear)
				return clear;
		}
	}
	return 0;
}
