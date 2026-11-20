/*
H3_OBJECT_LIGHT.C

A Halo 3 level's objects lit by Halo 3's own light probes.

Halo 3 lights an object with spherical harmonics (the kit's entry_points.fx
static_sh: the diffuse light ravi_order_3 of the object's coefficients at
each normal - spherical_harmonics.fx, read for its rule). Where the
coefficients come from:

- a placed scenery: its own probe, baked with the lightmap - the scenario
  lightmap's (sLdT) scenery probes, each named by its placement's object id
  (h3_level.c reads them and gives each its placement's position; the
  object here is found by that position);
- anything else: the level's lightprobe texture at the lightmapped surface
  under it (the coefficients the level's own surfaces are lit by, order 2
  with the dominant light - ravi_order_2_with_dominant_light), and, with no
  such surface under it, the scenario lightmap's air probes nearest it
  (the two nearest, by distance). Which of these Halo 3 picks for a moving
  object is its engine's, not in the kit's text: assumed.

This engine lights an object by an ambient color and two distant lights
that multiply its maps as they are on the screen, so the probe's light E(n)
is fitted in that space, as the level's lightmap texels are
(h3_level.c light_texel): D(n) = sqrt(E(n) x exposure), the first light
along the probe's dominant light, the second from behind it, the ambient
the light at the directions square to it. An object's light stops at 1
(the model path has no overbright pass): not worked around.

HALO_H3_OBJECT_LIGHT=0: the lighting as before (the lightmap texel under
the object through Halo 2's object terms). HALO_H3_OBJECT_LIGHT_LOG=1: each
object's probe and lights logged. HALO_H3_OBJECT_LIGHT_GAIN: times the
fitted lights (1). HALO_H3_OBJECT_LIGHT_TEST=v: every object's ambient v,
no lights (a test of the model path's scale).
*/

#include "cseries.h"
#include "h3_object_light.h"

#if defined(HALO_LINUX) && !defined(HALO_VITA) && !defined(HALO_GXM_NULL)

#include "math/real_math.h"
#include "objects/objects.h"
#include "render/render.h"
#include "tag_files/tag_groups.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);

struct probe
{
	int kind;
	float position[3];
	float direction[3], intensity[3];
	float terms[9][3];
};

static struct
{
	struct probe *probes;
	long count, capacity;
	float exposure;
	short atlas;
	boolean ready;
} globals;

void h3_object_light_begin(void)
{
	globals.count = 0;
	globals.ready = FALSE;
}

void h3_object_light_probe_add(int kind, float const position[3], float const direction[3], float const intensity[3],
	float const terms[9][3])
{
	struct probe *p;

	if (globals.count >= globals.capacity)
	{
		long capacity = globals.capacity ? 2 * globals.capacity : 256;
		struct probe *grown = realloc(globals.probes, (size_t)capacity * sizeof(*grown));

		if (!grown)
			return;
		globals.probes = grown;
		globals.capacity = capacity;
	}
	p = &globals.probes[globals.count++];
	p->kind = kind;
	memcpy(p->position, position, sizeof(p->position));
	memcpy(p->direction, direction, sizeof(p->direction));
	memcpy(p->intensity, intensity, sizeof(p->intensity));
	memcpy(p->terms, terms, sizeof(p->terms));
}

void h3_object_light_end(float exposure, short lightprobe_atlas)
{
	globals.exposure = exposure > 0.0f ? exposure : 1.0f;
	globals.atlas = lightprobe_atlas;
	globals.ready = TRUE;
}

/* Halo 3's diffuse light at a normal from nine coefficients a channel
(ravi_order_3 over pack_constants_texture_array's packing) */
static void order_3_light(float const terms[9][3], float const n[3], float light[3])
{
	static const float c1 = 0.429043f, c2 = 0.511664f, c4 = 0.886227f;
	int k;

	for (k = 0; k < 3; k++)
	{
		float x1 = n[0] * terms[3][k] + n[1] * terms[1][k] - n[2] * terms[2][k];
		float x2 = -n[0] * n[1] * terms[4][k] + n[1] * n[2] * terms[5][k] + n[2] * n[0] * terms[7][k];
		float x3 = (n[1] * n[1] - n[0] * n[0]) * terms[8][k] + (1.0f / 3.0f - n[2] * n[2]) * 1.73205080756f * terms[6][k];

		light[k] = (c4 * terms[0][k] - 2.0f * c2 * x1 - 2.0f * c1 * x2 - c1 * x3) / 3.1415926535f;
	}
}

/* the level's surfaces' light at a normal: order 2 less the dominant light,
and the dominant light's lobe (ravi_order_2_with_dominant_light) */
static void order_2_dominant_light(float const sh[4][3], float const direction[3], float const intensity[3], float const n[3],
	float light[3])
{
	float cosine = direction[0] * n[0] + direction[1] * n[1] + direction[2] * n[2];
	int k;

	if (cosine < 0.0f)
		cosine = 0.0f;
	for (k = 0; k < 3; k++)
	{
		float lx = sh[3][k] + 0.4886025f * direction[0] * intensity[k];
		float ly = sh[1][k] + 0.4886025f * direction[1] * intensity[k];
		float lz = -sh[2][k] + 0.4886025f * direction[2] * intensity[k];
		float constant = sh[0][k] - 0.2820948f * intensity[k];

		light[k] = (0.886227f * constant - 2.0f * 0.511664f * (n[0] * lx + n[1] * ly + n[2] * lz)) / 3.1415926535f +
			cosine * intensity[k] * 0.281f;
	}
}

/* a probe's dominant light's direction: the linear coefficients' luminance
(lightmap_sampling.fx) */
static boolean dominant_direction(float const terms[][3], float d[3])
{
	float length;

	d[0] = -(terms[3][0] * 0.212656f + terms[3][1] * 0.715158f + terms[3][2] * 0.0721856f);
	d[1] = -(terms[1][0] * 0.212656f + terms[1][1] * 0.715158f + terms[1][2] * 0.0721856f);
	d[2] = terms[2][0] * 0.212656f + terms[2][1] * 0.715158f + terms[2][2] * 0.0721856f;
	length = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	if (length < 1e-6f)
	{
		d[0] = d[1] = 0.0f;
		d[2] = 1.0f;
		return FALSE;
	}
	d[0] /= length;
	d[1] /= length;
	d[2] /= length;
	return TRUE;
}

static float env_value(char const *name, float fallback)
{
	char const *value = getenv(name);

	return value && *value ? (float)atof(value) : fallback;
}

boolean h3_object_light(long object_index, struct render_lighting *lighting, boolean sampled, short lightmap_bitmap,
	float u, float v)
{
	static int on = -1, log_on, test_on;
	static float gain, test;
	struct object_datum *object;
	struct probe blended;
	float sh[4][3], intensity[3], d[3], side[2][3], drawn[6][3];
	char const *source = NULL;
	boolean order_3 = TRUE;
	long i, nearest[2] = { NONE, NONE };
	float nearest_distance[2] = { 1e30f, 1e30f }, scenery_nearest = 1e30f;
	int k, s;

	if (on < 0)
	{
		char const *setting = getenv("HALO_H3_OBJECT_LIGHT");

		on = !(setting && !atoi(setting));
		log_on = getenv("HALO_H3_OBJECT_LIGHT_LOG") != NULL;
		gain = env_value("HALO_H3_OBJECT_LIGHT_GAIN", 1.0f);
		test_on = getenv("HALO_H3_OBJECT_LIGHT_TEST") != NULL;
		test = env_value("HALO_H3_OBJECT_LIGHT_TEST", 1.0f);
	}
	if (!on || !globals.ready)
		return FALSE;
	object = object_get(object_index);
	if (test_on)
	{
		lighting->ambient_color.red = lighting->ambient_color.green = lighting->ambient_color.blue = test;
		lighting->distant_light_count = 2;
		for (k = 0; k < 3; k++)
			lighting->distant_lights[0].color.n[k] = lighting->distant_lights[1].color.n[k] = 0.0f;
		return TRUE;
	}
	/* a placed scenery's own probe */
	if (object->object.type == _object_type_scenery)
	{
		for (i = 0; i < globals.count; i++)
		{
			struct probe const *p = &globals.probes[i];
			float dx, dy, dz;

			if (p->kind != _h3_object_light_probe_scenery && p->kind != _h3_object_light_probe_instance)
				continue;
			dx = p->position[0] - object->object.position.x;
			dy = p->position[1] - object->object.position.y;
			dz = p->position[2] - object->object.position.z;
			if (dx * dx + dy * dy + dz * dz < scenery_nearest)
				scenery_nearest = dx * dx + dy * dy + dz * dz;
			if (dx * dx + dy * dy + dz * dz < 0.05f * 0.05f)
			{
				blended = *p;
				source = "scenery probe";
				if (p->kind == _h3_object_light_probe_instance)
				{
					memcpy(sh, p->terms, sizeof(sh));
					memcpy(intensity, p->intensity, sizeof(intensity));
					order_3 = FALSE;
					source = "its per-vertex lighting";
				}
				break;
			}
		}
	}
	/* the lightprobe texture under it */
	if (!source && sampled && lightmap_bitmap == globals.atlas && h3_level_lightprobe_at(u, v, sh, intensity) &&
		sh[0][0] + sh[0][1] + sh[0][2] > 1e-5f)
	{
		order_3 = FALSE;
		source = "lightprobe under it";
	}
	/* the air probes nearest it */
	if (!source)
	{
		for (i = 0; i < globals.count; i++)
		{
			struct probe const *p = &globals.probes[i];
			float dx, dy, dz, distance;

			if (p->kind != _h3_object_light_probe_air)
				continue;
			dx = p->position[0] - object->object.bounding_sphere_center.x;
			dy = p->position[1] - object->object.bounding_sphere_center.y;
			dz = p->position[2] - object->object.bounding_sphere_center.z;
			distance = sqrtf(dx * dx + dy * dy + dz * dz);
			if (distance < nearest_distance[0])
			{
				nearest[1] = nearest[0];
				nearest_distance[1] = nearest_distance[0];
				nearest[0] = i;
				nearest_distance[0] = distance;
			}
			else if (distance < nearest_distance[1])
			{
				nearest[1] = i;
				nearest_distance[1] = distance;
			}
		}
		if (nearest[0] != NONE)
		{
			blended = globals.probes[nearest[0]];
			if (nearest[1] != NONE && nearest_distance[0] + nearest_distance[1] > 1e-4f)
			{
				struct probe const *other = &globals.probes[nearest[1]];
				float w = nearest_distance[0] / (nearest_distance[0] + nearest_distance[1]);
				int c;

				for (c = 0; c < 9; c++)
					for (k = 0; k < 3; k++)
						blended.terms[c][k] += (other->terms[c][k] - blended.terms[c][k]) * w;
			}
			source = "air probes";
		}
	}
	if (!source)
		return FALSE;
	/* the directions fitted: the dominant light's, behind it, four square to it */
	if (order_3)
	{
		dominant_direction(blended.terms, d);
	}
	else
		dominant_direction(sh, d);
	{
		float axis[3] = { 0.0f, 0.0f, 1.0f }, length;

		if (fabsf(d[2]) > 0.9f)
		{
			axis[0] = 1.0f;
			axis[2] = 0.0f;
		}
		side[0][0] = d[1] * axis[2] - d[2] * axis[1];
		side[0][1] = d[2] * axis[0] - d[0] * axis[2];
		side[0][2] = d[0] * axis[1] - d[1] * axis[0];
		length = sqrtf(side[0][0] * side[0][0] + side[0][1] * side[0][1] + side[0][2] * side[0][2]);
		for (k = 0; k < 3; k++)
			side[0][k] /= length;
		side[1][0] = d[1] * side[0][2] - d[2] * side[0][1];
		side[1][1] = d[2] * side[0][0] - d[0] * side[0][2];
		side[1][2] = d[0] * side[0][1] - d[1] * side[0][0];
	}
	for (s = 0; s < 6; s++)
	{
		float n[3], light[3];

		for (k = 0; k < 3; k++)
			n[k] = s == 0 ? d[k] : s == 1 ? -d[k] : s == 2 ? side[0][k] : s == 3 ? -side[0][k] : s == 4 ? side[1][k] : -side[1][k];
		if (order_3)
			order_3_light(blended.terms, n, light);
		else
			order_2_dominant_light(sh, d, intensity, n, light);
		for (k = 0; k < 3; k++)
			drawn[s][k] = light[k] > 0.0f ? sqrtf(light[k] * globals.exposure) * gain : 0.0f;
	}
	lighting->distant_light_count = 2;
	for (k = 0; k < 3; k++)
	{
		float square = 0.25f * (drawn[2][k] + drawn[3][k] + drawn[4][k] + drawn[5][k]);
		float ambient = drawn[1][k] < square ? 0.5f * (square + drawn[1][k]) : square;
		float front = drawn[0][k] - ambient, back = drawn[1][k] - ambient;

		lighting->ambient_color.n[k] = PIN(ambient, 0.0f, 1.0f);
		lighting->distant_lights[0].color.n[k] = PIN(front, 0.0f, 1.0f);
		lighting->distant_lights[1].color.n[k] = PIN(back, 0.0f, 1.0f);
		/* (a distant light's direction is the way its light travels) */
		lighting->distant_lights[0].direction.n[k] = -d[k];
		lighting->distant_lights[1].direction.n[k] = d[k];
	}
	if (log_on)
	{
		platform_log("h3 object light: %s at %.2f %.2f %.2f: %s, dominant %.2f %.2f %.2f; drawn toward it %.3f %.3f %.3f, behind %.3f %.3f %.3f, square %.3f %.3f %.3f; ambient %.3f %.3f %.3f, lights %.3f %.3f %.3f | %.3f %.3f %.3f (exposure %.2f; nearest scenery probe %.2f)",
			tag_get_name(object->definition_index), object->object.position.x, object->object.position.y, object->object.position.z,
			source, d[0], d[1], d[2], drawn[0][0], drawn[0][1], drawn[0][2], drawn[1][0], drawn[1][1], drawn[1][2],
			drawn[2][0], drawn[2][1], drawn[2][2], lighting->ambient_color.red, lighting->ambient_color.green,
			lighting->ambient_color.blue, lighting->distant_lights[0].color.red, lighting->distant_lights[0].color.green,
			lighting->distant_lights[0].color.blue, lighting->distant_lights[1].color.red, lighting->distant_lights[1].color.green,
			lighting->distant_lights[1].color.blue, globals.exposure, scenery_nearest < 1e29f ? sqrtf(scenery_nearest) : -1.0f);
	}
	return TRUE;
}

#else

void h3_object_light_begin(void)
{
}

void h3_object_light_probe_add(int kind, float const position[3], float const direction[3], float const intensity[3],
	float const terms[9][3])
{
	(void)kind; (void)position; (void)direction; (void)intensity; (void)terms;
}

void h3_object_light_end(float exposure, short lightprobe_atlas)
{
	(void)exposure; (void)lightprobe_atlas;
}

boolean h3_object_light(long object_index, struct render_lighting *lighting, boolean sampled, short lightmap_bitmap,
	float u, float v)
{
	(void)object_index; (void)lighting; (void)sampled; (void)lightmap_bitmap; (void)u; (void)v;
	return FALSE;
}

#endif
