/*
H2_SHADOWS.C

Objects' shadows on a converted Halo 2 level (h2_shadows.h).
*/

/* ---------- headers */

#include "cseries/cseries.h"
#include "math/real_math.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_geometry_environment.h"
#include "scenario/scenario.h"
#include "structures/structure_bsp_definitions.h"
#include "tag_files/tag_groups.h"
#include "h2_shadows.h"
#include "h2_cache.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* (render_epoch.c: changes with every map and game state replaced) */
extern unsigned long halo_map_generation;

/* ---------- constants */

/* (the sun's height the shadows are cast from at least: below it a
shadow longer than about twice the object's height is drawn as if the sun
stood there) */
#define MINIMUM_SUN_ELEVATION_DEGREES 24.0f

/* ---------- structures */

struct environment_vertex_compressed
{
	real_point3d position;
	unsigned long normal;
	unsigned long binormal;
	unsigned long tangent;
	real_point2d texcoord;
};

struct environment_lightmap_vertex_compressed
{
	unsigned long incident_radiosity;
	short lightmap_u;
	short lightmap_v;
};

/* ---------- prototypes */

void platform_log(const char *format, ...);
const char *config_string(const char *name);

/* ---------- globals */

static struct
{
	char level_name[64];        /* the level the sun below is of (h2_shadows_level_resolved) */
	boolean level_sun_found;
	real_vector3d level_sun;    /* toward its sky's light, unit */
	long scenario_index;        /* the scenario level is of (NONE: none yet) */
	boolean level;
	void const *structure;      /* the structure the sun was found in */
	boolean sun_found;
	real_vector3d sun;          /* toward the sun, unit */
} h2_shadows_globals = { "", FALSE, { 0 }, -2, FALSE, NULL, FALSE, { 0 } };

/* ---------- private code */

/* the level's sun: the lightmap's incident directions (toward the light,
their length its directionality) averaged over the level, each weighted by
its directionality to the fourth - a sunlit vertex's is near 1, the shade's
and the bounce's near 0 - and only those that look up */
static void h2_shadows_find_sun(
	struct structure_bsp *structure)
{
	double sum[3] = { 0.0, 0.0, 0.0 }, weight = 0.0;
	long vertices = 0;
	short l, m;

	h2_shadows_globals.structure = structure;
	h2_shadows_globals.sun_found = FALSE;
	for (l = 0; l < structure->lightmaps.count; l++)
	{
		struct structure_lightmap *lightmap = TAG_BLOCK_GET_ELEMENT(&structure->lightmaps, l, struct structure_lightmap);

		if (lightmap->bitmap_index == NONE)
			continue;
		for (m = 0; m < lightmap->materials.count; m++)
		{
			struct structure_material *material = TAG_BLOCK_GET_ELEMENT(&lightmap->materials, m, struct structure_material);
			struct environment_lightmap_vertex_compressed const *lightmap_vertices;
			long v;

			if (!material->compressed_vertex_data.address || material->vertices.count <= 0 ||
				(material->vertices.type != _rasterizer_vertex_type_environment_uncompressed &&
					material->vertices.type != _rasterizer_vertex_type_environment_compressed))
				continue;
			lightmap_vertices = (struct environment_lightmap_vertex_compressed const *)
				((struct environment_vertex_compressed const *)material->compressed_vertex_data.address + material->vertices.count);
			for (v = 0; v < material->vertices.count; v++)
			{
				real_vector3d incident;
				double length, w;

				environment_lightmap_vertex_compressed_get_incident_radiosity(&lightmap_vertices[v], &incident);
				length = sqrt(incident.i * incident.i + incident.j * incident.j + incident.k * incident.k);
				if (length < 0.5 || incident.k <= 0.0f)
					continue;
				w = length * length * length * length;
				sum[0] += incident.i / length * w;
				sum[1] += incident.j / length * w;
				sum[2] += incident.k / length * w;
				weight += w;
				vertices++;
			}
		}
	}
	if (weight > 0.0)
	{
		double length = sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);

		/* (the directions agree: their mean is long - one sun, not light
		from every side) */
		if (length / weight > 0.5)
		{
			h2_shadows_globals.sun.i = (real)(sum[0] / length);
			h2_shadows_globals.sun.j = (real)(sum[1] / length);
			h2_shadows_globals.sun.k = (real)(sum[2] / length);
			h2_shadows_globals.sun_found = TRUE;
		}
		platform_log("h2 shadows: the sun from %ld lightmap vertices: yaw %.1f pitch %.1f, agreement %.2f%s",
			vertices, atan2(sum[1], sum[0]) * 180.0 / M_PI, asin(sum[2] / length) * 180.0 / M_PI, length / weight,
			h2_shadows_globals.sun_found ? "" : " (too little: each object's own direction)");
	}
	else
	{
		platform_log("h2 shadows: no directional light in the lightmap: each object's own direction");
	}
}

/* whether the level loaded is the one whose sun was resolved */
static boolean h2_shadows_level_is_resolved(
	void)
{
	char const *scenario = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : NULL;
	char const *leaf = scenario ? strrchr(scenario, '\\') : NULL;

	return leaf && !strcmp(leaf + 1, h2_shadows_globals.level_name);
}

/* ---------- public code */

/* (rasterizer_xbox_water.c: the level's sun, toward it, unit - the sea's
specular glitter; FALSE if none was found yet) */
boolean h2_shadows_sun_get(
	real_vector3d *toward)
{
	if (!h2_shadows_globals.sun_found)
		return FALSE;
	*toward = h2_shadows_globals.sun;
	return TRUE;
}

boolean h2_shadows_sun_from_cache(
	struct h2_cache *cache,
	float *yaw,
	float *pitch)
{
	struct h2_cache_tag scenario, sky;
	unsigned long skies, lights;
	long count;
	float const *angles;

	/* (the scenario's skies at 0x08, a reference each; a sky's lights at
	0x78, each one's direction at 0x0C: yaw then pitch, radians - the
	sky layout checked on Zanzibar's: 168 and 36 degrees) */
	if (!cache ||
		!h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario) ||
		!h2_cache_block(cache, scenario.address + 0x08, 8, &count, &skies) || count < 1 ||
		!h2_cache_tag_get(cache, h2_cache_reference(cache, skies), &sky) ||
		!h2_cache_block(cache, sky.address + 0x78, 0x14, &count, &lights) || count < 1 ||
		!(angles = h2_cache_at(cache, lights + 0x0C, 8)) ||
		!(angles[0] == angles[0]) || !(angles[1] == angles[1]) || angles[1] <= 0.0f || angles[1] > 1.6f)
	{
		return FALSE;
	}
	*yaw = angles[0];
	*pitch = angles[1];
	return TRUE;
}

void h2_shadows_level_resolved(
	char const *maps_directory,
	char const *name,
	char const *level_path)
{
	char why[128];
	struct h2_cache *cache;
	float yaw = 0.0f, pitch = 0.0f;

	/* (piece E, DESIGN.md: the sun from the MCC map alone, never from a
	level file's header - a level with no file, HALO_H2_DIRECT, passes no
	path and got no sun at all: its objects' shadows and the sea's glitter
	went, the main menu's scene drawn a little unlike the made level's. One
	way for both: level_path is not read) */
	(void)level_path;
	snprintf(h2_shadows_globals.level_name, sizeof(h2_shadows_globals.level_name), "%s", name ? name : "");
	h2_shadows_globals.level_sun_found = FALSE;
	h2_shadows_globals.structure = NULL;
	if (!name || !maps_directory)
		return;
	cache = h2_cache_open(maps_directory, name, why, sizeof(why));
	if (!cache)
	{
		platform_log("h2 shadows: %s.map cannot be read for its sun: %s", name, why);
		return;
	}
	h2_shadows_globals.level_sun_found = h2_shadows_sun_from_cache(cache, &yaw, &pitch);
	h2_cache_close(cache);
	if (h2_shadows_globals.level_sun_found)
	{
		h2_shadows_globals.level_sun.i = cosf(pitch) * cosf(yaw);
		h2_shadows_globals.level_sun.j = cosf(pitch) * sinf(yaw);
		h2_shadows_globals.level_sun.k = sinf(pitch);
		platform_log("h2 shadows: %s: the sun at yaw %.1f, pitch %.1f (its sky's light, from its map)", name,
			yaw * 180.0f / (float)M_PI, pitch * 180.0f / (float)M_PI);
	}
	else
		platform_log("h2 shadows: %s: no sun in its sky", name);
}

short h2_shadows_tier(
	void)
{
	static unsigned long settings_seen;
	static short tier = -1;
	extern volatile unsigned long halo_settings_generation;

	if (tier < 0 || settings_seen != halo_settings_generation)
	{
		char const *setting = getenv("HALO_H2_SHADOWS");

		settings_seen = halo_settings_generation;
		if (!setting || !*setting)
			setting = config_string("graphics.h2_shadows");
		if (setting && !strcmp(setting, "full"))
			tier = _h2_shadows_full;
		else if (setting && !strcmp(setting, "cheap"))
			tier = _h2_shadows_cheap;
		else if (setting && !strcmp(setting, "off"))
			tier = _h2_shadows_off;
		else if (setting && !strcmp(setting, "auto"))
		{
			/* (the Vita's default: the graphics profile's Object shadows row
			(HALO_VITA_SHADOWS: 0 none, 1 near only, 2 all) decides for a
			Halo 2 level as it does for a Halo CE map - none, else this
			engine's own, the cheap tier; the row's near-only distance
			(render_objects.c) applies to both) */
			char const *row = getenv("HALO_VITA_SHADOWS");

			tier = row && *row && atoi(row) <= 0 ? _h2_shadows_off : _h2_shadows_cheap;
		}
		else
		{
			/* (the desktop's GL renderer alone, until the Vita's GXM, its
			null build's and Android's GLES are seen drawing them) */
#if defined(HALO_VITA) || defined(HALO_GXM_NULL) || defined(HALO_ANDROID)
			tier = _h2_shadows_off;
#else
			tier = _h2_shadows_full;
#endif
		}
	}
	return tier;
}

boolean h2_shadows_level(
	void)
{
	static unsigned long generation;

	/* (the map's generation too: consecutive maps' scenarios can have the
	same tag index, and the structure kept would be the map gone's) */
	if (h2_shadows_globals.scenario_index != global_scenario_index || generation != halo_map_generation)
	{
		char const *scenario = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : NULL;

		h2_shadows_globals.scenario_index = global_scenario_index;
		generation = halo_map_generation;
		/* (a converted level's scenario is h2\<name>...: h2_level.c) */
		h2_shadows_globals.level = scenario && !strncmp(scenario, "h2\\", 3);
		h2_shadows_globals.structure = NULL;
	}
	return h2_shadows_globals.level;
}

short h2_shadows_active(
	void)
{
	return h2_shadows_level() ? h2_shadows_tier() : _h2_shadows_off;
}

boolean h2_shadows_object(
	float shadow_vector[3],
	float shadow_color[3])
{
	struct structure_bsp *structure;
	real_vector3d down;
	real horizontal, minimum_vertical;

	if (h2_shadows_active() != _h2_shadows_full)
		return FALSE;
	/* (from the level's sun: its sky's light (h2_shadows_level_resolved),
	else the lightmap's directions averaged; where neither is found, the
	object's own incident direction) */
	if (h2_shadows_globals.level_sun_found && h2_shadows_level_is_resolved())
	{
		down.i = -h2_shadows_globals.level_sun.i;
		down.j = -h2_shadows_globals.level_sun.j;
		down.k = -h2_shadows_globals.level_sun.k;
	}
	else if ((structure = global_structure_bsp_get()) != NULL &&
		(structure == h2_shadows_globals.structure || (h2_shadows_find_sun(structure), TRUE)) &&
		h2_shadows_globals.sun_found)
	{
		down.i = -h2_shadows_globals.sun.i;
		down.j = -h2_shadows_globals.sun.j;
		down.k = -h2_shadows_globals.sun.k;
	}
	else
	{
		down.i = shadow_vector[0];
		down.j = shadow_vector[1];
		down.k = shadow_vector[2];
	}
	minimum_vertical = sinf(MINIMUM_SUN_ELEVATION_DEGREES * (real)M_PI / 180.0f);
	horizontal = sqrtf(down.i * down.i + down.j * down.j);
	if (-down.k < minimum_vertical && horizontal > 1e-4f)
	{
		real scale = sqrtf(1.0f - minimum_vertical * minimum_vertical) / horizontal;

		down.i *= scale;
		down.j *= scale;
		down.k = -minimum_vertical;
	}
	normalize3d(&down);
	shadow_vector[0] = down.i;
	shadow_vector[1] = down.j;
	shadow_vector[2] = down.k;
	/* (darker than this engine's, as Halo 2's are: a shadow in the sun
	keeps about a quarter of the ground's light - the owner's MCC frames,
	a Ghost's on Zanzibar's sand - where this engine's kept nearly half;
	one in shade, where the light under the object is less directed, a
	little more than before) */
	{
		float darkness = h2_shadows_darkness();
		int k;

		for (k = 0; k < 3; k++)
		{
			float value = 1.0f - (1.0f - shadow_color[k]) * darkness;

			shadow_color[k] = value < 0.12f ? 0.12f : value > 1.0f ? 1.0f : value;
		}
	}
	return TRUE;
}

float h2_shadows_softness(
	void)
{
	static float softness = -1.0f;

	if (softness < 0.0f)
	{
		char const *setting = getenv("HALO_H2_SHADOW_SOFTNESS");

		softness = setting && atof(setting) >= 0.0 ? (float)atof(setting) : 1.0f;
	}
	return softness;
}

float h2_shadows_darkness(
	void)
{
	static float darkness = -1.0f;

	if (darkness < 0.0f)
	{
		char const *setting = getenv("HALO_H2_SHADOW_DARKNESS");

		darkness = setting && atof(setting) > 0.0 ? (float)atof(setting) : 1.35f;
	}
	return darkness;
}
