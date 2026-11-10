/*
RENDER_ENUMERATE.C

A map's first frame drawn first through every shader its tags hold, unseen:
every material of the structure, every model's every part, at each level of
detail and region permutation, and every effect's sprites, in each fog the
code tells apart. The draws are the game's own (its structure, model and
effect code) with stand-in geometry, and the frame's own window, drawn after,
clears the target. Two uses:

- debug.draw_every_shader: each draw's key goes into the map's shader key
  list as any draw's (d3d8_shader_prep.inl), so a run with it lists the
  shaders a map can draw, not only those its first minutes did:
  tools/shader_keys.py makes the stock maps' list so.
- display.prepare_shaders on bgfx (halo_gpu_warm_draws): the shaders are
  made at the loading screen, but Metal's and Vulkan's pipelines (a shader
  pair with a draw's blend, depth and target) only at a draw. These draws
  make them before the first frame is shown, not one at a time in play.

Based on work by goatbutt and the OpenH1 team (OpenH1 57834197, startupfoundry):
their render_enumerate.c, which made a map's Metal pipelines at its first
frame; here it feeds the key lists too.

render_window (source/render/render.c) calls in at each part:
render_enumerating says whether the window being drawn is one of these.
*/

#ifdef HALO_SHADER_PREP

#include "cseries.h"
#include "cseries_windows.h"
#include "cache/cache_files.h"
#include "models/model_definitions.h"
#include "models/models.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_console_vars.h"
#include "rasterizer/rasterizer_models.h"
#include "render/render.h"
#include "scenario/scenario.h"
#include "structures/structure_bsp_definitions.h"
#include "bitmaps/bitmap_group.h"
#include "cache/texture_cache.h"
#include "effects/particle_system_definitions.h"
#include "effects/particles.h"
#include "render/render_sprite.h"

#include "render_enumerate.h"

#include <string.h>

/* the platform layer's (port/linux/src/port_config.c, d3d8_shader_prep.inl) */
int config_boolean(char const *name);
int halo_gpu_warm_draws(void);
void platform_log(char const *format, ...);

/* (rasterizer_xbox.c's) */
enum
{
	_planar_fog_off = 0,
	_planar_fog_normal,
	_planar_fog_fully_fogged,
};

/* the fogs the passes are drawn in: the map's own as the camera sees it,
none, the atmosphere's alone, the planar fog's (seen through and from
inside it), both */
enum
{
	_fog_own,
	_fog_none,
	_fog_atmospheric,
	_fog_planar,
	_fog_planar_full,
	_fog_both,
	NUMBER_OF_FOGS,
};

boolean render_enumerating = FALSE;

/* (the renderer's: d3d8_bgfx.inl keeps the shown frame its share of the
frame's buffers) */
int render_enumerate_active(void)
{
	return render_enumerating != FALSE;
}

static struct
{
	/* a map loaded since (its tags' indices are no map's own), and the
	structure enumerated last (NONE: none yet) */
	boolean new_map;
	short structure_bsp_index;
	/* this frame's: whether the models and effects are drawn too (a new
	map), the fog being drawn, the map's own */
	boolean everything;
	short fog;
	struct render_fog own_fog;
	unsigned long started;
} enumerate = { TRUE, NONE };

/* ---------- the fogs */

static void fog_set(short fog)
{
	struct render_fog *to = &render.fog;
	real_point3d origin = { 0.0f, 0.0f, 0.0f };
	real_vector3d up = { 0.0f, 0.0f, 1.0f };

	*to = enumerate.own_fog;
	if (fog == _fog_own)
		return;
	to->screen = NULL;
	to->atmospheric_maximum_distance = 0.0f;
	to->planar_mode = _planar_fog_off;
	if (fog == _fog_atmospheric || fog == _fog_both)
	{
		to->atmospheric_minimum_distance = 1.0f;
		to->atmospheric_maximum_distance = 100.0f;
		to->atmospheric_maximum_density = 0.5f;
	}
	if (fog == _fog_planar || fog == _fog_planar_full || fog == _fog_both)
	{
		to->planar_mode = fog == _fog_planar_full ? _planar_fog_fully_fogged : _planar_fog_normal;
		plane3d_from_point_and_normal(&to->plane, &origin, &up);
		to->planar_maximum_density = 0.5f;
		to->planar_maximum_distance = 50.0f;
		to->planar_maximum_depth = 10.0f;
	}
}

/* ---------- the frame */

void render_enumerate_new_map(void)
{
	enumerate.new_map = TRUE;
}

boolean render_enumerate_begin(void)
{
	static int setting = -1;
	short structure_bsp_index = global_structure_bsp_index_get();

	if (setting < 0)
		setting = config_boolean("debug.draw_every_shader");
	if ((!setting && !halo_gpu_warm_draws()) || structure_bsp_index == NONE ||
		(!enumerate.new_map && structure_bsp_index == enumerate.structure_bsp_index))
	{
		return FALSE;
	}
	enumerate.everything = enumerate.new_map;
	enumerate.new_map = FALSE;
	enumerate.structure_bsp_index = structure_bsp_index;
	enumerate.own_fog = render.fog;
	enumerate.started = system_milliseconds();
	enumerate.fog = 0;
	fog_set(enumerate.fog);
	render_enumerating = TRUE;
	return TRUE;
}

boolean render_enumerate_next(void)
{
	if (++enumerate.fog < NUMBER_OF_FOGS)
	{
		fog_set(enumerate.fog);
		return TRUE;
	}
	render.fog = enumerate.own_fog;
	render_enumerating = FALSE;
	platform_log("render: %s structure %d drawn through every shader, in %d fogs, in %lu ms",
		enumerate.everything ? "the map and its" : "its", enumerate.structure_bsp_index, NUMBER_OF_FOGS,
		(unsigned long)(system_milliseconds() - enumerate.started));
	return FALSE;
}

/* ---------- the structure: a surface of every material of every
lightmap, which every pass then draws as it would the visible ones */

void render_enumerate_structure(void)
{
	struct structure_bsp *structure_bsp = global_structure_bsp_get();
	short lightmap_index, material_index;

	if (!render_enumerating || !structure_bsp)
		return;
	memset(render.environment_surface_flags, 0, sizeof(render.environment_surface_flags));
	render.environment_surface_count = 0;
	for (lightmap_index = 0; lightmap_index < structure_bsp->lightmaps.count; lightmap_index++)
	{
		struct structure_lightmap *lightmap = TAG_BLOCK_GET_ELEMENT(&structure_bsp->lightmaps, lightmap_index,
			struct structure_lightmap);

		for (material_index = 0; material_index < lightmap->materials.count; material_index++)
		{
			struct structure_material *material = TAG_BLOCK_GET_ELEMENT(&lightmap->materials, material_index,
				struct structure_material);
			long surface = material->first_surface_index;

			if (material->surface_count <= 0 || surface < 0 || surface >= structure_bsp->surfaces.count ||
				surface >= MAXIMUM_SURFACES_PER_STRUCTURE * LONG_BITS)
			{
				continue;
			}
			if (!BIT_VECTOR_TEST_FLAG(render.environment_surface_flags, surface))
			{
				BIT_VECTOR_SET_FLAG(render.environment_surface_flags, surface, TRUE);
				render.environment_surface_count++;
			}
		}
	}
}

/* ---------- the models: every part of every model, at each level of
detail, in each of its regions' permutations */

void render_enumerate_models(void)
{
	struct tag_iterator iterator;
	long model_index;
	short saved_lod = rasterizer_debug_options.debug_model_lod;

	if (!render_enumerating || !enumerate.everything)
		return;
	rasterizer_models_begin(FALSE);
	tag_iterator_new(&iterator, MODELS_GROUP_TAG);
	while ((model_index = tag_iterator_next(&iterator)) != NONE)
	{
		struct model *model = model_definition_get(model_index);
		short most_permutations = 1, permutation, region_index, level;

		for (region_index = 0; region_index < model->regions.count && region_index < MAXIMUM_REGIONS_PER_MODEL;
			region_index++)
		{
			struct model_region *region = TAG_BLOCK_GET_ELEMENT(&model->regions, region_index, struct model_region);

			if (region->permutations.count > most_permutations)
				most_permutations = (short)region->permutations.count;
		}
		for (level = 0; level < 5; level++)
		{
			rasterizer_debug_options.debug_model_lod = level;
			for (permutation = 0; permutation < most_permutations; permutation++)
			{
				char permutations[MAXIMUM_REGIONS_PER_MODEL];
				struct render_lighting lighting;
				real_point3d centroid;

				memset(permutations, 0, sizeof(permutations));
				for (region_index = 0; region_index < model->regions.count && region_index < MAXIMUM_REGIONS_PER_MODEL;
					region_index++)
				{
					struct model_region *region = TAG_BLOCK_GET_ELEMENT(&model->regions, region_index,
						struct model_region);

					permutations[region_index] = (char)(permutation < region->permutations.count ? permutation : 0);
				}
				memset(&lighting, 0, sizeof(lighting));
				lighting.ambient_color.red = lighting.ambient_color.green = lighting.ambient_color.blue = 0.5f;
				centroid = render.camera.position;
				render_model(model_index, 100000.0f, NULL, permutations, NULL, NULL, &lighting, &centroid, 1.0f, NULL,
					NONE, 0, 0);
			}
		}
	}
	rasterizer_debug_options.debug_model_lod = saved_lod;
	rasterizer_models_end();
}

/* ---------- the effects: every particle's and particle system state's
sprite (in the world, in first person), a sprite before the
camera, which the transparent geometry's pass then draws as it would theirs */

static void effect_sprite(long bitmap_group_index, struct shader_effect_definition const *shader)
{
	/* (not on the screen: its sprites' vertices are the dynamic screen
	type's, which has no buffer in a map: render_sprite.c; and not in first
	person: the world's transparent pass would sort a first-person group
	among the map's own, which it halts for, d40's Cortana) */
	static unsigned long const sprites_flags[] = { 0 };
	real_argb_color color = { 1.0f, 1.0f, 1.0f, 1.0f };
	real_point3d origin;
	short index;

	if (bitmap_group_index == NONE)
		return;
	origin.x = render.camera.position.x + render.camera.forward.i * 2.0f;
	origin.y = render.camera.position.y + render.camera.forward.j * 2.0f;
	origin.z = render.camera.position.z + render.camera.forward.k * 2.0f;
	for (index = 0; index < (short)NUMBEROF(sprites_flags); index++)
	{
		struct build_sprite_data data;

		build_sprites_begin(&data, 1, bitmap_group_index, shader, sprites_flags[index]);
		build_sprite(&data, _build_sprite_normal, 0, 0, &origin, &render.camera.forward, 0.0f, 0.1f, &color, 1.0f, 0);
		build_sprites_end(&data);
	}
}

void render_enumerate_effects(void)
{
	struct tag_iterator iterator;
	long tag_index;

	if (!render_enumerating || !enumerate.everything)
		return;
	tag_iterator_new(&iterator, PARTICLE_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		struct particle_definition *definition = particle_definition_get(tag_index);

		effect_sprite(definition->bitmap.index, &definition->shader);
	}
	tag_iterator_new(&iterator, PARTICLE_SYSTEM_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		struct particle_system_definition *definition = particle_system_definition_get(tag_index);
		long type_index, state_index;

		for (type_index = 0; type_index < definition->types.count; type_index++)
		{
			struct particle_system_type *type = TAG_BLOCK_GET_ELEMENT(&definition->types, type_index,
				struct particle_system_type);

			for (state_index = 0; state_index < type->particle_states.count; state_index++)
			{
				struct particle_system_type_particle_state *state = TAG_BLOCK_GET_ELEMENT(&type->particle_states,
					state_index, struct particle_system_type_particle_state);

				effect_sprite(state->bitmaps.index, &state->shader);
			}
		}
	}
}

#endif
