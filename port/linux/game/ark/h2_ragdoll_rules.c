/*
H2_RAGDOLL_RULES.C

port: Halo 2's ragdoll rules (h2_ragdoll_rules.h), from Bungie's game code
(bipeds.cpp function_e0ef0, function_e0e00; unknown_1cec30.cpp
function_1d35d0). No solver of its own.
*/

#include "cseries.h"
#include "real_math.h"
#include "h2_ragdoll_rules.h"

#include <math.h>

real h2_ragdoll_settle_scale(
	long biped_index,
	real seconds)
{
	static real const looseness[4] = { 2.0f, 1.0f, 1.333f, 1.666f };
	real t = seconds * 0.8f;

	t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
	return 1.0f + (1.0f - t) * looseness[biped_index & 3];
}

static real h2_ragdoll_random_range(
	real (*random)(void *context),
	void *context,
	real lower,
	real upper)
{
	return lower + (upper - lower) * random(context);
}

boolean h2_ragdoll_death_push(
	short count,
	real_point3d const *positions,
	real const *masses,
	short root,
	short hit,
	real_vector3d const *direction_in,
	real push_scale,
	real (*random)(void *context),
	void *context,
	real_vector3d *changes)
{
	short i;

	if (count <= 0)
		return FALSE;
	for (i = 0; i < count; i++)
		changes[i].i = changes[i].j = changes[i].k = 0.0f;
	if (direction_in && hit >= 0 && hit < count)
	{
		/* (Bungie draws the strength, then the lift) */
		real strength = h2_ragdoll_random_range(random, context, 1.5f, 3.25f);
		real lift = h2_ragdoll_random_range(random, context, 0.75f, 1.5f);
		real_point3d center = { 0.0f, 0.0f, 0.0f };
		real_vector3d along, direction = *direction_in;
		real total = 0.0f, length;

		for (i = 0; i < count; i++)
		{
			center.x += positions[i].x * masses[i];
			center.y += positions[i].y * masses[i];
			center.z += positions[i].z * masses[i];
			total += masses[i];
		}
		if (total > 0.0f)
		{
			real farthest = 0.0f;
			real dot;

			center.x /= total;
			center.y /= total;
			center.z /= total;
			along.i = positions[hit].x - center.x;
			along.j = positions[hit].y - center.y;
			along.k = positions[hit].z - center.z;
			length = (real)sqrt(along.i * along.i + along.j * along.j + along.k * along.k);
			if (length > 0.001f)
			{
				along.i /= length;
				along.j /= length;
				along.k /= length;
				dot = along.i * direction.i + along.j * direction.j + along.k * direction.k;
				direction.i -= along.i * dot;
				direction.j -= along.j * dot;
				direction.k -= along.k * dot;
				length = (real)sqrt(direction.i * direction.i + direction.j * direction.j + direction.k * direction.k);
				if (length != 0.0f)
				{
					real push = (push_scale > H2_RAGDOLL_PUSH_SCALE_MAXIMUM ? H2_RAGDOLL_PUSH_SCALE_MAXIMUM : push_scale) *
						strength / length;

					direction.i *= push;
					direction.j *= push;
					direction.k *= push;
					for (i = 0; i < count; i++)
					{
						real distance = (positions[i].x - center.x) * along.i + (positions[i].y - center.y) * along.j +
							(positions[i].z - center.z) * along.k;

						if (!(farthest > distance))
							farthest = distance;
					}
					/* (none ahead of the centre: Halo 2 would divide by
					nothing) */
					if (farthest > 0.0f)
					{
						for (i = 0; i < count; i++)
						{
							real distance = (positions[i].x - center.x) * along.i + (positions[i].y - center.y) * along.j +
								(positions[i].z - center.z) * along.k;
							real scale = (0.0f > distance ? 0.65f : 1.0f) * distance / farthest;

							if (i == hit)
								scale *= 2.0f;
							changes[i].i = direction.i * scale;
							changes[i].j = direction.j * scale;
							changes[i].k = direction.k * scale + lift;
						}
						return TRUE;
					}
				}
			}
		}
	}
	if (root < 0 || root >= count)
		return FALSE;
	changes[root].k = -2.0f;
	return TRUE;
}
