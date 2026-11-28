/*
H2_OBJECT_LIGHTING.C

The lightmapped surface under a point on a converted Halo 2 level
(h2_lighting.h, h2_lighting_surface_at_point).

This engine lights an object from the lightmap under it: a ray down to the
structure's collision (structure_test_vector), then the render surface on
that collision plane, found through the collision leaf's surface
references (structure_render_surface_from_point_and_leaf). A converted
Halo 2 level's leaves have none (h2_level.c builds the collision BSP and
the render surfaces apart), so every object took the level's default light
(a dim grey, whatever stood in shade or sun). Here the render surface is
found in the structure's own lightmap materials: a grid over x and y of
every triangle, built once a structure, and the triangle that holds the
collision point (within a few centimetres of its plane, its nearest).
*/

/* ---------- headers */

#include "cseries/cseries.h"
#include "math/real_math.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_geometry_environment.h"
#include "render/render.h"
#include "scenario/scenario.h"
#include "structures/structure_bsp_definitions.h"
#include "h2_lighting.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#undef malloc
#undef free
#undef realloc

/* ---------- constants */

enum
{
	GRID_CELL_SIZE = 4,         /* world units */
	GRID_MAXIMUM_CELLS = 1 << 18,
};

#define PLANE_TOLERANCE 0.25f

/* ---------- structures */

struct grid_triangle
{
	short lightmap;
	short material;
	long surface;
};

static struct
{
	void const *structure;      /* the structure the grid is of */
	long surface_count;
	float x0, y0;
	long columns, rows;
	long *first;                /* columns * rows + 1 */
	struct grid_triangle *triangles;
} grid;

/* ---------- private code */

/* (a compressed environment vertex: 32 bytes, its point first) */
#define material_vertex(material, index) \
	((struct environment_vertex_compressed const *)((char const *)(material)->compressed_vertex_data.address + 32 * (index)))

static boolean material_usable(struct structure_lightmap const *lightmap, struct structure_material const *material)
{
	return lightmap->bitmap_index != NONE && material->compressed_vertex_data.address &&
		(material->vertices.type == _rasterizer_vertex_type_environment_uncompressed ||
			material->vertices.type == _rasterizer_vertex_type_environment_compressed);
}

static void triangle_points(struct structure_bsp *structure, struct structure_material const *material, long surface_index,
	real_point3d points[3])
{
	struct structure_surface const *surface = TAG_BLOCK_GET_ELEMENT(&structure->surfaces, surface_index, struct structure_surface);
	int k;

	for (k = 0; k < 3; k++)
		environment_vertex_compressed_get_point(material_vertex(material, surface->vertex_indices[k]), &points[k]);
}

/* each usable triangle passed to visit (with its cell range) */
typedef void (*triangle_visitor)(short lightmap, short material, long surface, long c0, long r0, long c1, long r1);

static void triangles_visit(struct structure_bsp *structure, boolean bounds_only, float bounds[4], triangle_visitor visit)
{
	short l, m;

	for (l = 0; l < structure->lightmaps.count; l++)
	{
		struct structure_lightmap *lightmap = TAG_BLOCK_GET_ELEMENT(&structure->lightmaps, l, struct structure_lightmap);

		for (m = 0; m < lightmap->materials.count; m++)
		{
			struct structure_material *material = TAG_BLOCK_GET_ELEMENT(&lightmap->materials, m, struct structure_material);
			long s;

			if (!material_usable(lightmap, material))
				continue;
			for (s = material->first_surface_index; s < material->first_surface_index + material->surface_count; s++)
			{
				real_point3d p[3];
				float lo[2], hi[2];
				int k;

				if (s < 0 || s >= structure->surfaces.count)
					continue;
				triangle_points(structure, material, s, p);
				lo[0] = hi[0] = p[0].x;
				lo[1] = hi[1] = p[0].y;
				for (k = 1; k < 3; k++)
				{
					lo[0] = fminf(lo[0], p[k].x), hi[0] = fmaxf(hi[0], p[k].x);
					lo[1] = fminf(lo[1], p[k].y), hi[1] = fmaxf(hi[1], p[k].y);
				}
				if (bounds_only)
				{
					bounds[0] = fminf(bounds[0], lo[0]), bounds[1] = fminf(bounds[1], lo[1]);
					bounds[2] = fmaxf(bounds[2], hi[0]), bounds[3] = fmaxf(bounds[3], hi[1]);
				}
				else
				{
					visit(l, m, s, (long)((lo[0] - grid.x0) / GRID_CELL_SIZE), (long)((lo[1] - grid.y0) / GRID_CELL_SIZE),
						(long)((hi[0] - grid.x0) / GRID_CELL_SIZE), (long)((hi[1] - grid.y0) / GRID_CELL_SIZE));
				}
			}
		}
	}
}

static void count_visit(short lightmap, short material, long surface, long c0, long r0, long c1, long r1)
{
	long c, r;

	(void)lightmap, (void)material, (void)surface;
	for (r = r0; r <= r1; r++)
		for (c = c0; c <= c1; c++)
			grid.first[r * grid.columns + c + 1]++;
}

static long *fill;

static void fill_visit(short lightmap, short material, long surface, long c0, long r0, long c1, long r1)
{
	long c, r;

	for (r = r0; r <= r1; r++)
	{
		for (c = c0; c <= c1; c++)
		{
			struct grid_triangle *t = &grid.triangles[grid.first[r * grid.columns + c] + fill[r * grid.columns + c]++];

			t->lightmap = lightmap;
			t->material = material;
			t->surface = surface;
		}
	}
}

static void grid_dispose(void)
{
	free(grid.first);
	free(grid.triangles);
	grid.first = NULL;
	grid.triangles = NULL;
	grid.structure = NULL;
}

static boolean grid_build(struct structure_bsp *structure)
{
	float bounds[4] = { 1e30f, 1e30f, -1e30f, -1e30f };
	long cells, i;

	grid_dispose();
	grid.structure = structure;
	grid.surface_count = structure->surfaces.count;
	triangles_visit(structure, TRUE, bounds, NULL);
	if (getenv("HALO_H2_OBJECT_TRACE"))
	{
		short l, m;
		long materials = 0, usable = 0, no_bitmap = 0, no_vertices = 0, other_type = 0;

		for (l = 0; l < structure->lightmaps.count; l++)
		{
			struct structure_lightmap *lightmap = TAG_BLOCK_GET_ELEMENT(&structure->lightmaps, l, struct structure_lightmap);

			for (m = 0; m < lightmap->materials.count; m++)
			{
				struct structure_material *material = TAG_BLOCK_GET_ELEMENT(&lightmap->materials, m, struct structure_material);

				materials++;
				usable += material_usable(lightmap, material);
				no_bitmap += lightmap->bitmap_index == NONE;
				no_vertices += !material->compressed_vertex_data.address;
				other_type += material->vertices.type != _rasterizer_vertex_type_environment_uncompressed &&
					material->vertices.type != _rasterizer_vertex_type_environment_compressed;
			}
		}
		platform_log("h2 trace: render surface grid: %d lightmaps, %ld materials, %ld usable (no lightmap bitmap %ld, no vertex data %ld, other vertex type %ld), %ld surfaces, bounds %.1f %.1f .. %.1f %.1f",
			(int)structure->lightmaps.count, materials, usable, no_bitmap, no_vertices, other_type, (long)structure->surfaces.count,
			bounds[0], bounds[1], bounds[2], bounds[3]);
	}
	if (bounds[0] > bounds[2])
		return FALSE;
	grid.x0 = bounds[0];
	grid.y0 = bounds[1];
	grid.columns = (long)((bounds[2] - bounds[0]) / GRID_CELL_SIZE) + 1;
	grid.rows = (long)((bounds[3] - bounds[1]) / GRID_CELL_SIZE) + 1;
	cells = grid.columns * grid.rows;
	if (cells <= 0 || cells > GRID_MAXIMUM_CELLS || (grid.first = calloc((unsigned long)cells + 1, sizeof(long))) == NULL)
		return FALSE;
	triangles_visit(structure, FALSE, NULL, count_visit);
	for (i = 0; i < cells; i++)
		grid.first[i + 1] += grid.first[i];
	grid.triangles = malloc((unsigned long)(grid.first[cells] + 1) * sizeof(*grid.triangles));
	fill = calloc((unsigned long)cells, sizeof(long));
	if (!grid.triangles || !fill)
	{
		free(fill);
		fill = NULL;
		grid_dispose();
		grid.structure = structure;
		return FALSE;
	}
	triangles_visit(structure, FALSE, NULL, fill_visit);
	free(fill);
	fill = NULL;
	return TRUE;
}

/* ---------- public code */

boolean h2_lighting_surface_at_point(
	real_point3d const *point,
	short *lightmap_index,
	short *material_index,
	long *surface_index,
	real *s,
	real *t)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	float best = PLANE_TOLERANCE;
	boolean found = FALSE;
	long c, r, i;

	if (!structure || !h2_lighting_overbright())
		return FALSE;
	if (grid.structure != structure || grid.surface_count != structure->surfaces.count)
		grid_build(structure);
	if (!grid.first)
		return FALSE;
	c = (long)((point->x - grid.x0) / GRID_CELL_SIZE);
	r = (long)((point->y - grid.y0) / GRID_CELL_SIZE);
	if (point->x < grid.x0 || point->y < grid.y0 || c >= grid.columns || r >= grid.rows)
		return FALSE;
	for (i = grid.first[r * grid.columns + c]; i < grid.first[r * grid.columns + c + 1]; i++)
	{
		struct grid_triangle const *g = &grid.triangles[i];
		struct structure_lightmap *lightmap = TAG_BLOCK_GET_ELEMENT(&structure->lightmaps, g->lightmap, struct structure_lightmap);
		struct structure_material *material = TAG_BLOCK_GET_ELEMENT(&lightmap->materials, g->material, struct structure_material);
		real_point3d p[3];
		real_vector3d e1, e2, n;
		real length, distance, u, v;

		triangle_points(structure, material, g->surface, p);
		e1.i = p[1].x - p[0].x, e1.j = p[1].y - p[0].y, e1.k = p[1].z - p[0].z;
		e2.i = p[2].x - p[0].x, e2.j = p[2].y - p[0].y, e2.k = p[2].z - p[0].z;
		n.i = e1.j * e2.k - e1.k * e2.j;
		n.j = e1.k * e2.i - e1.i * e2.k;
		n.k = e1.i * e2.j - e1.j * e2.i;
		length = (real)sqrt(n.i * n.i + n.j * n.j + n.k * n.k);
		if (length < 1e-6f)
			continue;
		distance = (real)fabs(((point->x - p[0].x) * n.i + (point->y - p[0].y) * n.j + (point->z - p[0].z) * n.k) / length);
		if (distance >= best || !point_in_triangle3d(point, &p[0], &p[1], &p[2], &u, &v))
			continue;
		best = distance;
		*lightmap_index = g->lightmap;
		*material_index = g->material;
		*surface_index = g->surface;
		*s = u;
		*t = v;
		found = TRUE;
	}
	return found;
}

/* the highest render triangle under a point, up to depth below it, facing up:
what a ray to the collision can pass through. A cinematic BSP's collision is
a floor under its streets - 03a's intro's at z -1, the road six units above
it -, so its objects took their light from under the road. FALSE: none */
boolean h2_lighting_surface_below_point(
	real_point3d const *point,
	real depth,
	real above,
	short *lightmap_index,
	short *material_index,
	long *surface_index,
	real *s,
	real *t,
	real *height)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	boolean found = FALSE;
	real best = above;
	long c, r, i;

	if (!structure || !h2_lighting_overbright())
		return FALSE;
	if (grid.structure != structure || grid.surface_count != structure->surfaces.count)
		grid_build(structure);
	if (!grid.first)
		return FALSE;
	c = (long)((point->x - grid.x0) / GRID_CELL_SIZE);
	r = (long)((point->y - grid.y0) / GRID_CELL_SIZE);
	if (point->x < grid.x0 || point->y < grid.y0 || c >= grid.columns || r >= grid.rows)
		return FALSE;
	for (i = grid.first[r * grid.columns + c]; i < grid.first[r * grid.columns + c + 1]; i++)
	{
		struct grid_triangle const *g = &grid.triangles[i];
		struct structure_lightmap *lightmap = TAG_BLOCK_GET_ELEMENT(&structure->lightmaps, g->lightmap, struct structure_lightmap);
		struct structure_material *material = TAG_BLOCK_GET_ELEMENT(&lightmap->materials, g->material, struct structure_material);
		real_point3d p[3], at;
		real_vector3d e1, e2, n;
		real z, u, v;

		triangle_points(structure, material, g->surface, p);
		e1.i = p[1].x - p[0].x, e1.j = p[1].y - p[0].y, e1.k = p[1].z - p[0].z;
		e2.i = p[2].x - p[0].x, e2.j = p[2].y - p[0].y, e2.k = p[2].z - p[0].z;
		n.i = e1.j * e2.k - e1.k * e2.j;
		n.j = e1.k * e2.i - e1.i * e2.k;
		n.k = e1.i * e2.j - e1.j * e2.i;
		if (fabsf(n.k) < 1e-6f)
			continue;
		z = p[0].z - ((point->x - p[0].x) * n.i + (point->y - p[0].y) * n.j) / n.k;
		if (z <= best || z > point->z || z < point->z - depth)
			continue;
		at.x = point->x, at.y = point->y, at.z = z;
		if (!point_in_triangle3d(&at, &p[0], &p[1], &p[2], &u, &v))
			continue;
		best = z;
		*lightmap_index = g->lightmap;
		*material_index = g->material;
		*surface_index = g->surface;
		*s = u;
		*t = v;
		*height = z;
		found = TRUE;
	}
	return found;
}
