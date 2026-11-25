/*
MODERN_LIGHTING_SCENE.C

What the game knows for the lighting
passes of port/linux/src/modern_lighting.h, from render_window
(source/render/render.c), once a window's opaque surfaces are drawn.

- The view: the world-to-clip matrix the vertex shaders are given
  (rasterizer_set_frustum_z's vertex constants, at the world's clip
  distances), the camera, the sun (the brightest light of the camera
  cluster's sky) and whether the camera's cluster sees a sky.
- For display.ray_tracing, the scene the rays are traced against, the
  render geometry (debug.ray_tracing_geometry / HALO_RT_GEOMETRY / the lab's
  "geometry": the collision geometry instead, round 1's, for comparing):
  - the level: the structure BSP's materials (made once a BSP): an
    environment shader's surfaces, alpha-tested where it is (its bump map's
    alpha, as rasterizer_xbox_environment.c tests it), and a transparent
    chicago or generic shader's that is alpha-tested (its first map's); the
    other transparent ones (water, glass, plasma) cast nothing;
  - each object: the parts render_model draws of it (its region
    permutations, at the level of detail render_model picks for its size on
    the screen, display.full_model_detail's too), solid ones (a model
    shader alpha-tested by its base map unless it says not, as
    rasterizer_xbox_models.c tests it), with the node matrices the vertex
    shaders are given (each node's matrix times its default inverse);
    posix_modern_lighting_rt.c skins them on the GPU.
  Or, the collision geometry: the level's collision BSP and each object's
  collision BSPs (its nodes' regions' permutations), placed by its nodes.

Nothing here writes to the game's state: it reads the tags, the objects and
the camera, which keeps the determinism replay's hashes the same
(tools/test_determinism.py).

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "cseries.h"
#include "cseries_windows.h"
#include "cseries/errors.h"
#include "game/players.h"
#include "math/real_math.h"
#include "objects/objects.h"
#include "objects/object_types.h"
#include "physics/collision_bsp.h"
#include "physics/collision_bsp_definitions.h"
#include "physics/collision_model_definitions.h"
#include "physics/collision_models.h"
#include "physics/collisions.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "render/render_cameras.h"
#include "scenario/scenario.h"
#include "structures/structure_bsp_definitions.h"
#include "structures/structures.h"
#include "tag_files/tag_groups.h"

#include "bitmaps/bitmap_group.h"
#include "cache/texture_cache.h"
#include "cutscene/cinematics.h"
#include "models/model_definitions.h"
#include "objects/object_definitions.h"
#include "rasterizer/rasterizer_geometry.h"
#include "render/render_cameras_internal.h"
#include "shaders/shader_definitions.h"
#include "shaders/shaders.h"
#include "tag_files/tag_files.h"

#include "../src/modern_lighting.h"
#include "ai/ai_scenario_definitions.h"
#include "scenario/scenario_definitions.h"

/* (halo_linux_source_fixups.h makes them nothing where the game's units have
no HALO_PORT_HDR: these are the functions render.c calls) */
#undef modern_lighting_opaque
#undef modern_lighting_shine
#undef modern_lighting_reflections
#undef modern_lighting_replaces_object_shadows

#include <stdlib.h>
#include <string.h>
#include <xtl.h>

/* the sun's direction this frame (toward it), and whether there is one, for
object_level_shaded */
static real_vector3d scene_sun;
static boolean scene_sun_valid;
static unsigned long scene_frame;

/* whether the game's own lighting of an object has the level's shadow in it
already: the object stands where no sky is (a cave, a tunnel: lit with no
sun), or the level hides the sun from its middle (the game lights an object
from the lightmap there, as one). Then the level's traced shadow is not
laid over it a second time (custom bit 17, rt_trace.comp's: its own parts'
and the other objects' shadows still are). Asked once a frame for each
ultimate parent */
static boolean object_level_shaded(long object_index, const struct object_datum *object)
{
	static unsigned long asked[2048];
	static boolean answers[2048];
	struct structure_bsp *structure_bsp = global_structure_bsp_get();
	long slot = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index), cluster_index;
	struct collision_result collision;
	real_vector3d toward;
	boolean shaded;

	if (!structure_bsp || !scene_sun_valid)
		return FALSE;
	if (slot >= 0 && slot < 2048 && asked[slot] == scene_frame)
		return answers[slot];
	cluster_index = cluster_index_from_point(structure_bsp, (real_point3d *)&object->object.bounding_sphere_center);
	if (cluster_index != NONE && cluster_index >= 0 && cluster_index < structure_bsp->clusters.count &&
		TAG_BLOCK_GET_ELEMENT(&structure_bsp->clusters, cluster_index, struct structure_cluster)->sky_index == NONE)
	{
		shaded = TRUE;
	}
	else
	{
		toward.i = scene_sun.i * 400.0f;
		toward.j = scene_sun.j * 400.0f;
		toward.k = scene_sun.k * 400.0f;
		shaded = collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
			&object->object.bounding_sphere_center, &toward, NONE, &collision);
	}
	if (slot >= 0 && slot < 2048)
	{
		asked[slot] = scene_frame;
		answers[slot] = shaded;
	}
	return shaded;
}

/* (a new frame: object_level_shaded asks again) */
static void object_level_shaded_frame(const struct halo_ml_frame *frame)
{
	scene_frame++;
	scene_sun_valid = frame->sun_valid;
	scene_sun.i = frame->sun[0];
	scene_sun.j = frame->sun[1];
	scene_sun.k = frame->sun[2];
}

int config_boolean(const char *name);
unsigned long config_changes(void);
/* bitmaps.c's: the P8 bump maps' colors (and alpha) */
extern pixel32 global_vector_palette[];

/* render_sky.c's view of the sky tag: its lights */
struct ml_sky
{
	struct tag_reference model;
	struct tag_reference animation_graph;
	byte pad20[0x8C];
	struct tag_block render_model_regions;
	struct tag_block animations;
	struct tag_block lights;
};

struct ml_sky_light
{
	struct tag_reference lens_flare;
	char marker_name[TAG_STRING_LENGTH + 1];
	byte pad30[0x1C];
	unsigned long flags;
	real_rgb_color color;
	real power;
	real test_distance;
	byte pad64[4];
	real_euler_angles2d direction;
	real diameter;
};

typedef char ml_sky_light_size_assert[sizeof(struct ml_sky_light) == 0x74 ? 1 : -1];

/* the objects farther than this from the camera are left out of the scene */
#define ML_OBJECT_DISTANCE 120.0f
/* below display.ray_tracing_quality high, objects narrower than this on the
screen are left out of the trace */
#define ML_SMALL_OBJECT_PIXELS 4.0f

static struct
{
	/* the level's mesh: the collision BSP it was made from */
	const void *level_key;
	int level_ready;
	/* a growing scratch for one mesh's triangles */
	float *positions;
	unsigned int *indices;
	int position_capacity, index_capacity;
	int rt_started, rt_failed;
	int skipped;
	/* the render geometry (1) or the collision geometry (0) */
	int meshes;
	unsigned int world;
} scene;

static int scratch_fit(int vertices, int indices)
{
	if (vertices > scene.position_capacity)
	{
		float *grown = realloc(scene.positions, (size_t)vertices * 3 * sizeof(float));

		if (!grown)
			return FALSE;
		scene.positions = grown;
		scene.position_capacity = vertices;
	}
	if (indices > scene.index_capacity)
	{
		unsigned int *grown = realloc(scene.indices, (size_t)indices * sizeof(unsigned int));

		if (!grown)
			return FALSE;
		scene.indices = grown;
		scene.index_capacity = indices;
	}
	return TRUE;
}

/* a collision BSP's visible surfaces as triangles (fans of its polygons),
in the scratch; the triangles' count */
/* the level's collision materials, to leave out the sky's surfaces (none for
an object's collision BSP) */
static const struct tag_block *level_materials;

static boolean surface_is_sky(const struct collision_surface *surface)
{
	const struct structure_collision_material *material;

	if (!level_materials)
		return FALSE;
	if (surface->material_index < 0)
		return TRUE;
	if (surface->material_index >= level_materials->count)
		return FALSE;
	material = TAG_BLOCK_GET_ELEMENT(level_materials, surface->material_index, struct structure_collision_material);
	return material->shader.index == NONE;
}

static int collision_bsp_triangles(const struct collision_bsp *bsp, int *vertex_count)
{
	long surface_index;
	int vertices = 0, triangles = 0;

	if (!scratch_fit(bsp->surfaces.count * MAXIMUM_VERTICES_PER_COLLISION_SURFACE,
		bsp->surfaces.count * (MAXIMUM_VERTICES_PER_COLLISION_SURFACE - 2) * 3))
	{
		return 0;
	}
	for (surface_index = 0; surface_index < bsp->surfaces.count; surface_index++)
	{
		const struct collision_surface *surface =
			TAG_BLOCK_GET_ELEMENT(&bsp->surfaces, surface_index, struct collision_surface);
		real_point3d points[MAXIMUM_VERTICES_PER_COLLISION_SURFACE];
		short count, point;

		/* (the invisible ones: player clips and the like; and those with no
		material: the level's sky, the hull drawn as sky that keeps vehicles
		in, which would put every point in shadow) */
		if ((surface->flags & 2) || surface_is_sky(surface))
		{
			scene.skipped++;
			continue;
		}
		count = collision_surface_polygon(bsp, surface_index, points);
		if (count < 3)
			continue;
		for (point = 0; point < count; point++)
		{
			scene.positions[(vertices + point) * 3 + 0] = points[point].x;
			scene.positions[(vertices + point) * 3 + 1] = points[point].y;
			scene.positions[(vertices + point) * 3 + 2] = points[point].z;
		}
		for (point = 1; point + 1 < count; point++)
		{
			scene.indices[triangles * 3 + 0] = (unsigned int)vertices;
			scene.indices[triangles * 3 + 1] = (unsigned int)(vertices + point);
			scene.indices[triangles * 3 + 2] = (unsigned int)(vertices + point + 1);
			triangles++;
		}
		vertices += count;
	}
	*vertex_count = vertices;
	return triangles;
}

static int collision_bsp_mesh(const struct collision_bsp *bsp)
{
	int mesh = ml_rt_mesh_find(bsp);
	int vertices = 0, triangles;

	if (mesh != -1)
		return mesh;
	triangles = collision_bsp_triangles(bsp, &vertices);
	/* (-2: a mesh with nothing in it, kept so that it is not made again) */
	return ml_rt_mesh_add(bsp, scene.positions, vertices, scene.indices, triangles);
}

static void scene_level(void)
{
	int vertices = 0, triangles;

	if (!global_collision_bsp || (scene.level_ready && scene.level_key == global_collision_bsp))
		return;
	scene.level_key = global_collision_bsp;
	scene.skipped = 0;
	level_materials = global_structure_bsp_get() ? &global_structure_bsp_get()->collision_materials : NULL;
	triangles = collision_bsp_triangles(global_collision_bsp, &vertices);
	level_materials = NULL;
	error(_error_silent, "modern lighting: the level's collision BSP, %ld surfaces, %d left out (invisible or sky)",
		global_collision_bsp->surfaces.count, scene.skipped);
	scene.level_ready = ml_rt_static_mesh(global_collision_bsp, scene.positions, vertices, scene.indices, triangles);
}

/* each object's collision BSPs placed by its nodes; the viewer's instance
number when the camera is inside a unit (first person) */
static int scene_objects(const real_point3d *camera)
{
	struct object_iterator iterator;
	struct object_datum *object;
	int viewer = -1;

	ml_rt_instances_begin();
	object_iterator_new(&iterator, _object_mask_all, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		struct collision_model_instance instance;
		long object_index = iterator.index;
		long parent;
		short node_index;
		unsigned int mask, custom;
		real distance;

		if (TEST_FLAG(object->object.flags, _object_invisible_bit) ||
			TEST_FLAG(object->object.flags, _object_outside_of_map_bit))
		{
			continue;
		}
		distance = distance3d(&object->object.bounding_sphere_center, camera);
		if (distance > ML_OBJECT_DISTANCE + object->object.bounding_sphere_radius)
			continue;
		if (!collision_model_instance_new(&instance, object_index))
			continue;
		parent = object_get_ultimate_parent(object_index);
		/* (the instance's number: its ultimate parent's slot, so that a
		unit and the weapon it holds are one for the shader) */
		custom = (unsigned int)DATUM_INDEX_TO_ABSOLUTE_INDEX(parent) & 0xffff;
		mask = TEST_FLAG(object->object.flags, _object_shadowless_bit) ? HALO_RT_MASK_SHADOWLESS : HALO_RT_MASK_OBJECT;
		{
			struct object_datum *ultimate = object_get(parent);

			if (ultimate->object.type == _object_type_biped || ultimate->object.type == _object_type_vehicle ||
				ultimate->object.type == _object_type_projectile)
			{
				mask |= HALO_RT_MASK_UNIT;
			}
			/* (the camera inside a unit: first person) */
			if ((ultimate->object.type == _object_type_biped || ultimate->object.type == _object_type_vehicle) &&
				distance3d(&ultimate->object.bounding_sphere_center, camera) <
					ultimate->object.bounding_sphere_radius)
			{
				viewer = (int)custom;
			}
			if (object_level_shaded(parent, ultimate))
				custom |= 0x20000u;
		}
		for (node_index = 0; node_index < instance.node_count; node_index++)
		{
			const struct collision_node *node =
				TAG_BLOCK_GET_ELEMENT(&instance.model->nodes, node_index, struct collision_node);
			const struct collision_bsp *bsp;
			const real_matrix4x3 *matrix = &instance.matrices[node_index];
			float transform[12];
			short permutation;
			int mesh;

			if (node->region_index < 0 || node->region_index >= MAXIMUM_REGIONS_PER_OBJECT || node->bsps.count <= 0)
				continue;
			permutation = instance.region_permutation_indices[node->region_index];
			if (permutation == NONE)
				continue;
			permutation = PIN(permutation, 0, node->bsps.count - 1);
			bsp = TAG_BLOCK_GET_ELEMENT(&node->bsps, permutation, struct collision_bsp);
			mesh = collision_bsp_mesh(bsp);
			if (mesh < 0)
				continue;
			transform[0] = matrix->scale * matrix->forward.i;
			transform[1] = matrix->scale * matrix->left.i;
			transform[2] = matrix->scale * matrix->up.i;
			transform[3] = matrix->position.x;
			transform[4] = matrix->scale * matrix->forward.j;
			transform[5] = matrix->scale * matrix->left.j;
			transform[6] = matrix->scale * matrix->up.j;
			transform[7] = matrix->position.y;
			transform[8] = matrix->scale * matrix->forward.k;
			transform[9] = matrix->scale * matrix->left.k;
			transform[10] = matrix->scale * matrix->up.k;
			transform[11] = matrix->position.z;
			ml_rt_instance(mesh, transform, custom, mask);
		}
	}
	return viewer;
}

/* ---------- the render geometry */

/* models.c's own (its file's): a model's geometry and its parts */
struct ml_model_geometry
{
	byte reserved[0x24];
	struct tag_block parts;
};

struct ml_model_geometry_part
{
	unsigned long flags;
	short shader_index;
	char previous_part_index;
	char next_part_index;
	short centroid_primary_node_index;
	short centroid_secondary_node_index;
	real centroid_primary_node_weight;
	real centroid_secondary_node_weight;
	real_point3d centroid;
	struct tag_block uncompressed_vertices;
	struct tag_block compressed_vertices;
	struct tag_block triangles;
	struct triangle_buffer triangle_buffer;
	struct vertex_buffer vertex_buffer;
};

struct ml_model_shader_reference
{
	struct tag_reference shader;
	short permutation_index;
	word pad;
	long unused[3];
};

typedef char ml_model_geometry_part_size_assert[sizeof(struct ml_model_geometry_part) == 0x68 ? 1 : -1];
typedef char ml_model_shader_reference_size_assert[sizeof(struct ml_model_shader_reference) == 0x20 ? 1 : -1];

/* the shader tags' fields the trace reads (rasterizer_xbox_models.c's,
rasterizer_xbox_environment.c's and rasterizer_xbox_transparent_geometry.c's
layouts) */
struct ml_shader_model
{
	struct shader shader;
	word flags;
	short type;
	byte reserved[0x9C - 0x2C];
	real map_u_scale;
	real map_v_scale;
	struct tag_reference base_map;
};

struct ml_shader_environment
{
	struct shader shader;
	word flags;
	short type;
	byte reserved[0x128 - 0x2C];
	struct tag_reference bump_map;
	real_vector2d runtime_bump_map_scale;
};

struct ml_shader_transparent
{
	struct shader_base base;
	byte numeric_counter_limit;
	byte flags;
	byte reserved[0x54 - 0x2A];
	struct tag_block maps;
};

typedef char ml_shader_model_scale_assert[offsetof(struct ml_shader_model, map_u_scale) == 0x9C ? 1 : -1];
typedef char ml_shader_model_base_map_assert[offsetof(struct ml_shader_model, base_map.index) == 0xB0 ? 1 : -1];
typedef char ml_shader_environment_bump_assert[offsetof(struct ml_shader_environment, bump_map) == 0x128 ? 1 : -1];
typedef char ml_shader_environment_scale_assert[
	offsetof(struct ml_shader_environment, runtime_bump_map_scale) == 0x138 ? 1 : -1];
typedef char ml_shader_transparent_maps_assert[offsetof(struct ml_shader_transparent, maps) == 0x54 ? 1 : -1];

/* (models.c's shader types) */
enum
{
	ML_SHADER_TYPE_ENVIRONMENT = 3,
	ML_SHADER_TYPE_MODEL = 4,
	ML_SHADER_TYPE_TRANSPARENT_GENERIC = 5,
	ML_SHADER_TYPE_TRANSPARENT_CHICAGO = 6,
};

enum
{
	ML_SHADER_ENVIRONMENT_ALPHA_TESTED = 1 << 0,
	ML_SHADER_MODEL_NOT_ALPHA_TESTED = 1 << 2,
	ML_SHADER_MODEL_ALPHA_BLENDED_DECAL = 1 << 3,
	ML_SHADER_TRANSPARENT_ALPHA_TESTED = 1 << 0,
	ML_MODEL_PART_STRIPPED = 1 << 0,
	/* (a chicago map's and a generic map's size, and where their scale and
	bitmap are) */
	ML_CHICAGO_MAP_SIZE = 0xDC,
	ML_CHICAGO_MAP_SCALE = 0x54,
	ML_CHICAGO_MAP_BITMAP = 0x6C,
	ML_GENERIC_MAP_SIZE = 0x64,
	ML_GENERIC_MAP_SCALE = 0x04,
	ML_GENERIC_MAP_BITMAP = 0x1C,
	ML_DETAIL_LEVELS = 5,
	/* (an environment's compressed vertex: environment_vertex_compressed) */
	ML_ENVIRONMENT_VERTEX_SIZE = 32,
	ML_MAXIMUM_OBJECT_PARTS = 64,
};

/* (models.c's: a level past this many pixels is one for a camera inside
the model, not drawn whole) */
#define ML_FULL_MODEL_DETAIL_PIXELS 1000.0f

/* the surfaces' alpha: the texture's number (>= 0), -1 opaque, -2 none (a
surface the trace leaves out) */
static int bitmap_alpha(long bitmap_group_index, short permutation)
{
	struct bitmap_group *group;
	struct bitmap_data *bitmap;
	void *hardware_format;
	int found;

	if (bitmap_group_index == NONE)
		return -2;
	group = bitmap_group_get(bitmap_group_index);
	if (!group || group->bitmaps.count <= 0)
		return -2;
	bitmap = TAG_BLOCK_GET_ELEMENT(&group->bitmaps, (permutation < 0 ? 0 : permutation) % group->bitmaps.count,
		struct bitmap_data);
	found = ml_rt_texture_find(bitmap);
	if (found == -1)
	{
		/* (its pixels, read once: loaded now if the cache has them not) */
		hardware_format = _texture_cache_bitmap_get_hardware_format(bitmap, TRUE, TRUE);
		found = halo_ml_rt_texture(bitmap, hardware_format, (const unsigned int *)global_vector_palette);
		error(_error_silent, "modern lighting: alpha of %s: %s", tag_get_name(bitmap_group_index),
			found >= 0 ? "tested" : found == -3 ? "all passes (opaque)" : "not readable (left out)");
	}
	/* (-3: its alpha all passes) */
	return found >= 0 ? found : found == -3 ? -1 : -2;
}

static real scale_or_one(real value)
{
	return value != 0.0f ? value : 1.0f;
}

/* a transparent shader's first map: its bitmap and scale */
static boolean transparent_first_map(struct ml_shader_transparent *shader, boolean chicago, long *bitmap,
	float scale[2])
{
	const byte *map;

	if (shader->maps.count <= 0)
		return FALSE;
	map = (const byte *)TAG_BLOCK_GET_ELEMENT(&shader->maps, 0, byte);
	*bitmap = ((const struct tag_reference *)(map + (chicago ? ML_CHICAGO_MAP_BITMAP : ML_GENERIC_MAP_BITMAP)))->index;
	scale[0] = scale_or_one(*(const real *)(map + (chicago ? ML_CHICAGO_MAP_SCALE : ML_GENERIC_MAP_SCALE)));
	scale[1] = scale_or_one(*(const real *)(map + (chicago ? ML_CHICAGO_MAP_SCALE : ML_GENERIC_MAP_SCALE) + 4));
	return TRUE;
}

/* how a surface of the shader is traced: as surface_alpha's numbers, and
its texture coordinate's scale (base_map_scale: a model's, NULL the level) */
static int shader_surface(struct shader *shader, short permutation, const real_vector2d *base_map_scale, float scale[2])
{
	scale[0] = scale[1] = 1.0f;
	if (!shader)
		return -2;
	switch (shader->base.type)
	{
	case ML_SHADER_TYPE_ENVIRONMENT:
	{
		const struct ml_shader_environment *environment = (const struct ml_shader_environment *)shader;

		if (!(environment->flags & ML_SHADER_ENVIRONMENT_ALPHA_TESTED))
			return -1;
		scale[0] = scale_or_one(environment->runtime_bump_map_scale.i);
		scale[1] = scale_or_one(environment->runtime_bump_map_scale.j);
		return bitmap_alpha(environment->bump_map.index, permutation);
	}
	case ML_SHADER_TYPE_MODEL:
	{
		const struct ml_shader_model *model = (const struct ml_shader_model *)shader;

		if (model->flags & ML_SHADER_MODEL_ALPHA_BLENDED_DECAL)
			return -2;
		if (model->flags & ML_SHADER_MODEL_NOT_ALPHA_TESTED)
			return -1;
		scale[0] = (base_map_scale ? scale_or_one(base_map_scale->i) : 1.0f) * scale_or_one(model->map_u_scale);
		scale[1] = (base_map_scale ? scale_or_one(base_map_scale->j) : 1.0f) * scale_or_one(model->map_v_scale);
		return bitmap_alpha(model->base_map.index, permutation);
	}
	case ML_SHADER_TYPE_TRANSPARENT_CHICAGO:
	case ML_SHADER_TYPE_TRANSPARENT_GENERIC:
	{
		struct ml_shader_transparent *transparent = (struct ml_shader_transparent *)shader;
		long bitmap;

		/* (a see-through one casts nothing; an alpha-tested one, as its
		first map's alpha) */
		if (!(transparent->flags & ML_SHADER_TRANSPARENT_ALPHA_TESTED) ||
			!transparent_first_map(transparent, shader->base.type == ML_SHADER_TYPE_TRANSPARENT_CHICAGO, &bitmap, scale))
		{
			return -2;
		}
		return bitmap_alpha(bitmap, permutation);
	}
	default:
		return -2;
	}
}

/* the level's render geometry, once a BSP */
static void scene_level_render(struct structure_bsp *structure_bsp)
{
	long lightmap_index;
	int materials = 0, alpha_tested = 0, left_out = 0;

	if (!ml_rt_level_begin(structure_bsp))
		return;
	for (lightmap_index = 0; lightmap_index < structure_bsp->lightmaps.count; lightmap_index++)
	{
		struct structure_lightmap *lightmap =
			TAG_BLOCK_GET_ELEMENT(&structure_bsp->lightmaps, lightmap_index, struct structure_lightmap);
		long material_index;

		for (material_index = 0; material_index < lightmap->materials.count; material_index++)
		{
			struct structure_material *material =
				TAG_BLOCK_GET_ELEMENT(&lightmap->materials, material_index, struct structure_material);
			const void *vertices;
			long vertex_count;
			float scale[2];
			int surface;

			if (material->shader.index == NONE || material->surface_count <= 0 || material->first_surface_index < 0 ||
				material->first_surface_index + material->surface_count > structure_bsp->surfaces.count)
			{
				continue;
			}
			surface = shader_surface(shader_definition_get(material->shader.index), material->permutation_index, NULL,
				scale);
			vertices = material->compressed_vertex_data.address ?
				xbox_pointer(material->compressed_vertex_data.address) : NULL;
			vertex_count = material->compressed_vertex_data.size / ML_ENVIRONMENT_VERTEX_SIZE;
			if (surface == -2 || !vertices || vertex_count <= 0)
			{
				left_out++;
				continue;
			}
			ml_rt_level_material(vertices, (int)vertex_count, ML_ENVIRONMENT_VERTEX_SIZE,
				(const unsigned short *)TAG_BLOCK_GET_ELEMENT(&structure_bsp->surfaces, material->first_surface_index,
					struct structure_surface), (int)material->surface_count, surface, scale);
			materials++;
			alpha_tested += surface >= 0;
		}
	}
	error(_error_silent, "modern lighting: the level's render geometry, %d materials (%d alpha-tested), %d left out",
		materials, alpha_tested, left_out);
	scene.level_ready = ml_rt_level_end();
}

/* a part's vertices and triangles as the map keeps them (the hardware
buffers' data, as rasterizer_xbox_transparent_geometry.c reads them) */
static int part_mesh(struct ml_model_geometry_part *part)
{
	BYTE *vertices = NULL, *indices = NULL;
	int kind;

	if (!part->vertex_buffer.hardware_format || !part->triangle_buffer.hardware_format || part->vertex_buffer.offset ||
		part->vertex_buffer.count <= 0 || part->triangle_buffer.count <= 0)
	{
		return -2;
	}
	if (part->vertex_buffer.type == _rasterizer_vertex_type_model_compressed)
		kind = 1;
	else if (part->vertex_buffer.type == _rasterizer_vertex_type_model_uncompressed)
		kind = 2;
	else
		return -2;
	IDirect3DVertexBuffer8_Lock(XBOX_POINTER(IDirect3DVertexBuffer8, part->vertex_buffer.hardware_format), 0, 0,
		&vertices, D3DLOCK_READONLY);
	IDirect3DIndexBuffer8_Lock(XBOX_POINTER(IDirect3DIndexBuffer8, part->triangle_buffer.hardware_format), 0, 0,
		&indices, D3DLOCK_READONLY);
	if (!vertices || !indices)
		return -2;
	return ml_rt_part(part, vertices, (int)part->vertex_buffer.count, kind, (const unsigned short *)indices,
		part->triangle_buffer.type == _triangle_buffer_type_precompiled_strip ? (int)part->triangle_buffer.count + 2 :
		(int)part->triangle_buffer.count * 3, part->triangle_buffer.type == _triangle_buffer_type_precompiled_strip);
}

static boolean full_model_detail(void)
{
	static unsigned long read_at = (unsigned long)-1;
	static boolean full = FALSE;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		full = config_boolean("display.full_model_detail") != 0;
	}
	return full;
}

/* the level of detail render_model draws the model at, for the object's
size on the screen (render_objects.c's object_get_level_of_detail_pixels) */
static short object_detail_level(long object_index, struct object_datum *object, const struct model *model)
{
	real pixels;
	short level = ML_DETAIL_LEVELS - 1;

	if (cinematic_in_progress() && TEST_FLAG(object->object.flags, _object_movie_star_bit))
		pixels = REAL_MAX;
	else
	{
		pixels = render_frustum_sphere_diameter_in_pixels(&global_window_parameters.frustum,
			&object->object.bounding_sphere_center, object->object.bounding_sphere_radius);
	}
	(void)object_index;
	while (level > 0 && pixels < model->detail_cutoff_pixels[level])
		level--;
	if (full_model_detail())
	{
		short full = ML_DETAIL_LEVELS - 1;

		while (full > level && model->detail_cutoff_pixels[full] > ML_FULL_MODEL_DETAIL_PIXELS)
			full--;
		level = full;
	}
	return level;
}

/* an object's parts as render_model draws them; their count */
static int object_parts(long object_index, struct object_datum *object, struct model *model,
	struct ml_rt_part_use *uses)
{
	short region_count = 0, region_index, level = object_detail_level(object_index, object, model);
	byte *permutations = object_get_region_permutations(object_index, &region_count);
	short regions = (short)MIN(model->regions.count, MAXIMUM_REGIONS_PER_OBJECT);
	int count = 0;

	if (!permutations)
		return 0;
	for (region_index = 0; region_index < regions; region_index++)
	{
		struct model_region *region = TAG_BLOCK_GET_ELEMENT(&model->regions, region_index, struct model_region);
		signed char permutation_index = (signed char)permutations[region_index];
		struct model_region_permutation *permutation;
		struct ml_model_geometry *geometry;
		short geometry_index, part_index;

		if (permutation_index == NONE || !VALID_INDEX(permutation_index, region->permutations.count))
			continue;
		permutation = TAG_BLOCK_GET_ELEMENT(&region->permutations, permutation_index, struct model_region_permutation);
		geometry_index = permutation->geometry_indices[level];
		if (!VALID_INDEX(geometry_index, model->geometries.count))
			continue;
		geometry = TAG_BLOCK_GET_ELEMENT(&model->geometries, geometry_index, struct ml_model_geometry);
		for (part_index = 0; part_index < geometry->parts.count && count < ML_MAXIMUM_OBJECT_PARTS; part_index++)
		{
			struct ml_model_geometry_part *part =
				TAG_BLOCK_GET_ELEMENT(&geometry->parts, part_index, struct ml_model_geometry_part);
			struct ml_model_shader_reference *reference;
			struct shader *shader;
			int surface, mesh;

			if (!VALID_INDEX(part->shader_index, model->shaders.count) || (part->flags & ML_MODEL_PART_STRIPPED))
				continue;
			reference = TAG_BLOCK_GET_ELEMENT(&model->shaders, part->shader_index, struct ml_model_shader_reference);
			if (reference->shader.index == NONE)
				continue;
			shader = shader_definition_get(reference->shader.index);
			if (!shader || !shader_type_is_valid_for_model(shader->base.type))
				continue;
			surface = shader_surface(shader, object->object.forced_shader_permutation_index ?
				object->object.forced_shader_permutation_index : reference->permutation_index, &model->base_map_scale,
				uses[count].scale);
			if (surface == -2)
				continue;
			mesh = part_mesh(part);
			if (mesh < 0)
				continue;
			uses[count].part = mesh;
			uses[count].texture = surface;
			count++;
		}
	}
	return count;
}

/* each object's render model as drawn; the viewer's instance number when
the camera is inside a unit (first person) */
static int scene_objects_render(const real_point3d *camera)
{
	static struct ml_rt_part_use uses[ML_MAXIMUM_OBJECT_PARTS];
	static float matrices[MAXIMUM_NODES_PER_MODEL][12];
	struct object_iterator iterator;
	struct object_datum *object;
	int viewer = -1;
	boolean small_left_out = halo_ml_rt_quality() != HALO_RT_QUALITY_HIGH;

	ml_rt_instances_begin();
	object_iterator_new(&iterator, _object_mask_all, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		long object_index = iterator.index;
		struct object_definition *definition;
		struct model *model;
		real_matrix4x3 *nodes;
		long parent;
		unsigned int mask, custom;
		short node_count, node_index;
		int part_count;

		if (TEST_FLAG(object->object.flags, _object_invisible_bit) ||
			TEST_FLAG(object->object.flags, _object_outside_of_map_bit))
		{
			continue;
		}
		if (distance3d(&object->object.bounding_sphere_center, camera) >
			ML_OBJECT_DISTANCE + object->object.bounding_sphere_radius)
		{
			continue;
		}
		/* (below high quality: an object a few pixels wide on the screen, a
		shell casing, a far grenade, left out: its shadow a speck, its
		skinning and its structure's refit each frame not) */
		if (small_left_out && render_frustum_sphere_diameter_in_pixels(&global_window_parameters.frustum,
			&object->object.bounding_sphere_center, object->object.bounding_sphere_radius) < ML_SMALL_OBJECT_PIXELS)
		{
			continue;
		}
		definition = object_definition_get(object->definition_index);
		if (!definition || definition->object.model.index == NONE)
			continue;
		model = model_definition_get(definition->object.model.index);
		nodes = object_get_node_matrices(object_index);
		if (!model || !nodes)
			continue;
		node_count = (short)MIN(model->nodes.count, MAXIMUM_NODES_PER_MODEL);
		node_count = (short)MIN(node_count, (short)(object->object.node_matrices.size / (short)sizeof(real_matrix4x3)));
		if (node_count <= 0)
			continue;
		part_count = object_parts(object_index, object, model, uses);
		if (!part_count)
			continue;
		/* (as render_model gives the rasterizer: each node's matrix times
		its default inverse) */
		for (node_index = 0; node_index < node_count; node_index++)
		{
			struct model_node *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node_index, struct model_node);
			real_matrix4x3 relative;
			float *rows = matrices[node_index];

			matrix4x3_multiply(&nodes[node_index], &node->runtime_default_inverse_matrix, &relative);
			rows[0] = relative.scale * relative.forward.i;
			rows[1] = relative.scale * relative.left.i;
			rows[2] = relative.scale * relative.up.i;
			rows[3] = relative.position.x;
			rows[4] = relative.scale * relative.forward.j;
			rows[5] = relative.scale * relative.left.j;
			rows[6] = relative.scale * relative.up.j;
			rows[7] = relative.position.y;
			rows[8] = relative.scale * relative.forward.k;
			rows[9] = relative.scale * relative.left.k;
			rows[10] = relative.scale * relative.up.k;
			rows[11] = relative.position.z;
		}
		parent = object_get_ultimate_parent(object_index);
		custom = (unsigned int)DATUM_INDEX_TO_ABSOLUTE_INDEX(parent) & 0xffff;
		mask = TEST_FLAG(object->object.flags, _object_shadowless_bit) ? HALO_RT_MASK_SHADOWLESS : HALO_RT_MASK_OBJECT;
		{
			struct object_datum *ultimate = object_get(parent);

			if (ultimate->object.type == _object_type_biped || ultimate->object.type == _object_type_vehicle ||
				ultimate->object.type == _object_type_projectile)
			{
				mask |= HALO_RT_MASK_UNIT;
			}
			if ((ultimate->object.type == _object_type_biped || ultimate->object.type == _object_type_vehicle) &&
				distance3d(&ultimate->object.bounding_sphere_center, camera) <
					ultimate->object.bounding_sphere_radius)
			{
				viewer = (int)custom;
			}
			if (object_level_shaded(parent, ultimate))
				custom |= 0x20000u;
		}
		ml_rt_object((int)DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index), (int)object_index, uses, part_count,
			&matrices[0][0], node_count, custom, mask);
	}
	return viewer;
}

/* the world the scene is of: the scenario's name and tags, and the BSP */
static unsigned int scene_world(void)
{
	const char *name = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : NULL;
	unsigned int hash = 2166136261u;

	for (; name && *name; name++)
		hash = (hash ^ (unsigned char)*name) * 16777619u;
	hash ^= (unsigned int)(size_t)global_scenario_get() * 2654435761u;
	return hash ? hash : 1;
}

/* the sun: the brightest light of the sky */
static void frame_sun(struct halo_ml_frame *frame, short sky_index)
{
	struct ml_sky *sky = (struct ml_sky *)scenario_get_sky(sky_index);
	real best = 0.0f;
	long index;

	if (!sky)
		return;
	for (index = 0; index < sky->lights.count; index++)
	{
		struct ml_sky_light *light = TAG_BLOCK_GET_ELEMENT(&sky->lights, index, struct ml_sky_light);
		real brightness = light->power * (light->color.red + light->color.green + light->color.blue);
		real_vector3d direction;

		if (frame->sun_valid && brightness <= best)
			continue;
		vector3d_from_euler_angles2d(&direction, &light->direction);
		frame->sun[0] = direction.i;
		frame->sun[1] = direction.j;
		frame->sun[2] = direction.k;
		frame->sun_color[0] = light->color.red;
		frame->sun_color[1] = light->color.green;
		frame->sun_color[2] = light->color.blue;
		frame->sun_valid = TRUE;
		best = brightness;
	}
}

static boolean frame_make(const struct render_camera *camera, struct halo_ml_frame *frame)
{
	const struct render_frustum *frustum = &global_window_parameters.frustum;
	struct structure_bsp *structure_bsp = global_structure_bsp_get();
	short column, row;
	short sky_index = 0;

	memset(frame, 0, sizeof(*frame));
	if (!frustum->projection_valid)
		return FALSE;
	/* (as rasterizer_set_frustum_z gives the vertex shaders) */
	for (column = 0; column < 4; column++)
	{
		for (row = 0; row < 4; row++)
		{
			const real *n = frustum->world_to_view.n[row];
			real value = n[0] * frustum->projection_matrix[0][column] + n[1] * frustum->projection_matrix[1][column] +
				n[2] * frustum->projection_matrix[2][column];

			if (row == 3)
				value += frustum->projection_matrix[3][column];
			frame->world_to_clip[column * 4 + row] = value;
		}
	}
	frame->camera[0] = global_window_parameters.camera.position.x;
	frame->camera[1] = global_window_parameters.camera.position.y;
	frame->camera[2] = global_window_parameters.camera.position.z;
	frame->bounds[0] = camera->viewport_bounds.x0;
	frame->bounds[1] = camera->viewport_bounds.y0;
	frame->bounds[2] = camera->viewport_bounds.x1;
	frame->bounds[3] = camera->viewport_bounds.y1;
	frame->network_game = render_tuning_network_game();
	frame->viewer_instance = -1;
	if (structure_bsp)
	{
		long cluster_index = cluster_index_from_point(structure_bsp, (real_point3d *)&global_window_parameters.camera.position);

		if (cluster_index != NONE && cluster_index >= 0 && cluster_index < structure_bsp->clusters.count)
		{
			struct structure_cluster *cluster =
				TAG_BLOCK_GET_ELEMENT(&structure_bsp->clusters, cluster_index, struct structure_cluster);

			frame->outdoor = cluster->sky_index != NONE;
			if (cluster->sky_index != NONE)
				sky_index = cluster->sky_index;
		}
	}
	frame_sun(frame, sky_index);
	return TRUE;
}

/* render_window: once the opaque surfaces are drawn */
void modern_lighting_opaque(const struct render_camera *camera)
{
	struct halo_ml_frame frame;
	int ray_tracing = halo_ml_ray_tracing_mode();

	if (!frame_make(camera, &frame))
		return;
	if (ray_tracing != HALO_RT_OFF && ml_rt_ready())
	{
		double before = halo_ml_now_ms();
		int meshes = halo_ml_ray_tracing_meshes();

		object_level_shaded_frame(&frame);
		ml_rt_world(scene_world());
		/* (a new world, the trace started again, or the other geometry: the
		level made again) */
		if (!scene.rt_started || scene.meshes != meshes)
		{
			scene.level_ready = FALSE;
			scene.level_key = NULL;
		}
		scene.rt_started = TRUE;
		scene.meshes = meshes;
		if (meshes)
		{
			struct structure_bsp *structure_bsp = global_structure_bsp_get();

			if (structure_bsp && (!scene.level_ready || scene.level_key != structure_bsp || scene.world != scene_world()))
			{
				scene.level_key = structure_bsp;
				scene.world = scene_world();
				scene_level_render(structure_bsp);
				scene.level_ready = TRUE;
			}
			frame.viewer_instance = scene_objects_render(&global_window_parameters.camera.position);
		}
		else
		{
			if (scene.world != scene_world())
			{
				scene.world = scene_world();
				scene.level_ready = FALSE;
			}
			scene_level();
			frame.viewer_instance = scene_objects(&global_window_parameters.camera.position);
		}
		halo_ml_scene_ms(halo_ml_now_ms() - before);
	}
	else
		scene.rt_started = FALSE;
	/* (called with both off too: the lab's commands are read there) */
	halo_ml_opaque(&frame);
}

/* the lab's "objects": the vehicles and bipeds, by tag name and place (to
find a view's subject) */
void modern_lighting_scene_list(char *text, int size)
{
	struct object_iterator iterator;
	struct object_datum *object;
	int used = 0;

	if (size <= 0)
		return;
	text[0] = 0;
	object_iterator_new(&iterator, _object_mask_all, 0);
	while ((object = object_iterator_next(&iterator)) != NULL && used < size - 1)
	{
		const char *name;
		int wrote;

		if (object->object.type != _object_type_vehicle && object->object.type != _object_type_biped)
			continue;
		name = tag_get_name(object->definition_index);
		if (name && strrchr(name, '\\'))
			name = strrchr(name, '\\') + 1;
		wrote = snprintf(text + used, (size_t)(size - used), "%s@%.1f,%.1f,%.1f ", name ? name : "?",
			object->object.bounding_sphere_center.x, object->object.bounding_sphere_center.y,
			object->object.bounding_sphere_center.z);
		if (wrote < 0)
			break;
		used += wrote;
	}
}

/* the lab's "encounters": the scenario's AI encounters, by name, and where
each one's first squad starts (to place a fight for the timings) */
void modern_lighting_scene_encounters(char *text, int size)
{
	struct scenario *scenario = global_scenario_get();
	int used = 0, index;

	if (size <= 0)
		return;
	text[0] = 0;
	if (!scenario)
		return;
	for (index = 0; index < scenario->ai_encounters.count && used < size - 1; index++)
	{
		struct encounter_definition *encounter = TAG_BLOCK_GET_ELEMENT(&scenario->ai_encounters, index,
			struct encounter_definition);
		real_point3d where = { 0.0f, 0.0f, 0.0f };
		int wrote;

		if (encounter->squads.count > 0)
		{
			struct squad_definition *squad = TAG_BLOCK_GET_ELEMENT(&encounter->squads, 0, struct squad_definition);

			if (squad->starting_locations.count > 0)
				where = TAG_BLOCK_GET_ELEMENT(&squad->starting_locations, 0, struct actor_starting_location)->position;
		}
		wrote = snprintf(text + used, (size_t)(size - used), "%s@%.1f,%.1f,%.1f/%d ", encounter->name, where.x, where.y,
			where.z, (int)encounter->squads.count);
		if (wrote < 0)
			break;
		used += wrote;
	}
}

/* render_window: once the dynamic lights' shine is drawn, before the
level's own shine and reflections */
void modern_lighting_shine(const struct render_camera *camera)
{
	struct halo_ml_frame frame;
	if (halo_ml_ray_tracing_mode() < HALO_RT_AO || !frame_make(camera, &frame))
		return;
	halo_ml_shine(&frame);
}

/* render_window: once the shine and the reflections are drawn */
void modern_lighting_reflections(const struct render_camera *camera)
{
	struct halo_ml_frame frame;
	if (halo_ml_ray_tracing_mode() < HALO_RT_AO || !frame_make(camera, &frame))
		return;
	halo_ml_reflections(&frame);
}

/* render_window: the objects' projected shadows give way to the traced ones */
boolean modern_lighting_replaces_object_shadows(void)
{
	return halo_ml_ray_tracing_mode() != HALO_RT_OFF && ml_rt_ready();
}
