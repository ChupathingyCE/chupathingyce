/*
STRUCTURE_CULLING_BYTE_ORDER.C

Structure culling on a big-endian machine, as the Xbox 360 and Wii U are. A
map's vertex data stays in the Xbox's byte order (only the tags are swapped
when it loads), so here the vertices are written as little-endian bytes on
every machine, and the culling is built from them.

The case prints the build's "D of T triangles disagree with their vertex
normals" and whether the walk culls. On a little-endian machine D is 0: the
terrain's vertex normals are its faces'. A big-endian build that reads the
bytes in its own order gets D near half of T and cuts holes in the level
(#925); the test (test_structure_culling_byte_order.py) runs this under QEMU
and wants the same D as here whenever the walk culls.

The fake world and the tested functions are structure_culling.c's (the
harness's fixture, included whole): this file adds a terrain and its own
main.
*/

#define main structure_culling_fixture_main
#include "structure_culling.c"
#undef main

#define TERRAIN_SIDE 12
#define TERRAIN_VERTICES ((TERRAIN_SIDE + 1) * (TERRAIN_SIDE + 1))
#define TERRAIN_TRIANGLES (TERRAIN_SIDE * TERRAIN_SIDE * 2)

static unsigned char terrain_bytes[TERRAIN_VERTICES * COMPRESSED_STRUCTURE_VERTEX_SIZE];
static struct structure_surface terrain_surfaces[TERRAIN_TRIANGLES];
static long terrain_indices[TERRAIN_TRIANGLES];
static struct structure_material terrain_material;
static struct structure_lightmap terrain_lightmap;
static struct structure_visibility_cluster terrain_cluster;
static struct structure_visibility_subcluster terrain_subcluster;

/* a float as the Xbox stores it: little-endian, whatever this machine is */
static void write_xbox_real(unsigned char *bytes, real value)
{
	unsigned int word;
	int i;

	memcpy(&word, &value, sizeof(word));
	for (i = 0; i < 4; ++i)
		bytes[i] = (unsigned char)(word >> (8 * i));
}

static real terrain_height(long x, long y)
{
	/* (rolling ground, a cliff along one side: Blood Gulch's kind) */
	return 0.6f * sinf(0.7f * (real)x) * cosf(0.5f * (real)y) + (x > 8 ? 4.0f + 0.3f * (real)y : 0.0f);
}

static void terrain_load(void)
{
	real_point3d points[TERRAIN_VERTICES];
	real_vector3d normals[TERRAIN_VERTICES];
	long x, y, i, j;
	long triangle = 0;

	memset(normals, 0, sizeof(normals));
	for (y = 0; y <= TERRAIN_SIDE; ++y)
	{
		for (x = 0; x <= TERRAIN_SIDE; ++x)
			points[y * (TERRAIN_SIDE + 1) + x] = (real_point3d){(real)x, (real)y, terrain_height(x, y)};
	}
	for (y = 0; y < TERRAIN_SIDE; ++y)
	{
		for (x = 0; x < TERRAIN_SIDE; ++x)
		{
			long corner = y * (TERRAIN_SIDE + 1) + x;
			long quads[2][3] = {{corner, corner + TERRAIN_SIDE + 1, corner + 1},
				{corner + 1, corner + TERRAIN_SIDE + 1, corner + TERRAIN_SIDE + 2}};

			for (i = 0; i < 2; ++i, ++triangle)
			{
				real_vector3d edge0, edge1, face;

				for (j = 0; j < 3; ++j)
					terrain_surfaces[triangle].vertex_indices[j] = (short)quads[i][j];
				vector_from_points3d(&points[quads[i][0]], &points[quads[i][1]], &edge0);
				vector_from_points3d(&points[quads[i][0]], &points[quads[i][2]], &edge1);
				cross_product3d(&edge0, &edge1, &face);
				/* (the game's front: STRUCTURE_CULLING_FRONT_SIGN) */
				for (j = 0; j < 3; ++j)
				{
					normals[quads[i][j]].i += STRUCTURE_CULLING_FRONT_SIGN * face.i;
					normals[quads[i][j]].j += STRUCTURE_CULLING_FRONT_SIGN * face.j;
					normals[quads[i][j]].k += STRUCTURE_CULLING_FRONT_SIGN * face.k;
				}
			}
		}
	}
	/* (the fixture's vertex: point, normal, then texture coordinates) */
	for (i = 0; i < TERRAIN_VERTICES; ++i)
	{
		unsigned char *vertex = terrain_bytes + i * COMPRESSED_STRUCTURE_VERTEX_SIZE;
		real length = sqrtf(magnitude_squared3d(&normals[i]));
		real values[6] = {points[i].x, points[i].y, points[i].z,
			normals[i].i / length, normals[i].j / length, normals[i].k / length};

		for (j = 0; j < 6; ++j)
			write_xbox_real(vertex + 4 * j, values[j]);
	}

	memset(&fixture_bsp, 0, sizeof(fixture_bsp));
	fixture_shader.base.type = 3;
	verify_ok = TRUE;
	global_structure_bsp_index = 0;
	fixture_bsp.surfaces = (struct tag_block){TERRAIN_TRIANGLES, terrain_surfaces, 0};
	fixture_bsp.clusters = (struct tag_block){1, &terrain_cluster, 0};
	fixture_bsp.lightmaps = (struct tag_block){1, &terrain_lightmap, 0};
	fixture_bsp.world_bounds = (real_rectangle3d){0, TERRAIN_SIDE, 0, TERRAIN_SIDE, -2, 16};
	terrain_lightmap.materials = (struct tag_block){1, &terrain_material, 0};
	terrain_material.shader.index = 0;
	terrain_material.first_surface_index = 0;
	terrain_material.surface_count = TERRAIN_TRIANGLES;
	terrain_material.compressed_vertex_data.size = sizeof(terrain_bytes);
	terrain_material.compressed_vertex_data.address = terrain_bytes;
	terrain_cluster.subclusters = (struct tag_block){1, &terrain_subcluster, 0};
	terrain_subcluster.world_bounds = fixture_bsp.world_bounds;
	terrain_subcluster.surface_indices = (struct tag_block){TERRAIN_TRIANGLES, terrain_indices, 0};
	for (i = 0; i < TERRAIN_TRIANGLES; ++i)
		terrain_indices[i] = i;
	structure_culling_structure_bsp_loaded();
}

int main(int argc, char **argv)
{
	long disagree = -1, triangles = -1;
	char const *counts;
	boolean culls;

	CHECK(argc == 2 && !strcmp(argv[1], "terrain"), "one case: terrain");
	terrain_load();
	CHECK(structure_culling.allocation != NULL, "the culling built: %s", build_log);
	counts = strstr(build_log, " ms, ");
	CHECK(counts && sscanf(counts, " ms, %ld of %ld triangles disagree", &disagree, &triangles) == 2,
		"the build's log line: %s", build_log);
	/* (a frame above the terrain, looking down it) */
	fixture_frame();
	render.camera.position = (real_point3d){6, -4, 10};
	culls = structure_culling_traverse(&fixture_bsp);
	printf("disagree %ld of %ld, walk %s\n", disagree, triangles, culls ? "culls" : "falls back");
	structure_culling_dispose();
	return 0;
}
