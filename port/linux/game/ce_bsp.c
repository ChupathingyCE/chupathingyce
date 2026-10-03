/*
CE_BSP.C

Custom Edition maps' structure BSPs (cache_files.c, Custom Edition maps).
Halo PC keeps a BSP's rendered vertices uncompressed, in each lightmap
material's uncompressed vertex data (its environment vertices, then its
lightmap vertices), and its BSP header lists no vertex buffers. When such a
BSP loads, each material's vertices are compressed as the Xbox's tools
compress them (rasterizer_geometry_compress_vertices), into contiguous
memory, which becomes its compressed vertex data, and given Direct3D vertex
buffers, as the Xbox's BSPs' are: the material's vertices and lightmap
vertices are then the Xbox's compressed types.

Before the map is opened, each of its BSPs is checked (ce_bsp_check, from
ce_map_checks.c): what is read here lies within the BSP and is of the sizes
its counts give.
*/

#ifdef HALO_CUSTOM_EDITION

#include "cseries.h"
#include "cseries_windows.h"
#include "errors.h"
#include "rasterizer/rasterizer_geometry.h"
#include "structures/structure_bsp_definitions.h"
#include "ce_map_checks.h"

#include <xtl.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	ENVIRONMENT_VERTEX_UNCOMPRESSED_SIZE = 0x38,
	ENVIRONMENT_VERTEX_COMPRESSED_SIZE = 0x20,
	LIGHTMAP_VERTEX_UNCOMPRESSED_SIZE = 0x14,
	LIGHTMAP_VERTEX_COMPRESSED_SIZE = 0x8,
	/* the unit vectors of an uncompressed vertex: an environment vertex's
	normal, binormal and tangent, a lightmap vertex's incident radiosity */
	ENVIRONMENT_VERTEX_VECTORS_OFFSET = 0xc,
	ENVIRONMENT_VERTEX_VECTOR_COUNT = 3,
	LIGHTMAP_VERTEX_VECTORS_OFFSET = 0x0,
	LIGHTMAP_VERTEX_VECTOR_COUNT = 1,

	/* a BSP's header in the map (cache_files.c's
	cache_file_structure_bsp_header) */
	CE_BSP_HEADER_SIZE = 0x18,
	CE_BSP_HEADER_SIGNATURE = 'sbsp',
	/* (the most of each a BSP is checked to have: more would not fit in
	the tag cache in any case) */
	CE_MAXIMUM_BSP_LIGHTMAPS = 0x10000,
	CE_MAXIMUM_LIGHTMAP_MATERIALS = 0x10000,
	CE_MAXIMUM_MATERIAL_VERTICES = 0x100000,
	/* (the room ce_vertex_buffer takes of the tag cache) */
	CE_BUFFER_HEADER_ROOM = 16,
	/* clusters and their portals (structures.c, structures.h's maximums) */
	CE_CLUSTER_SIZE = 0x68,
	CE_MAXIMUM_CLUSTERS = 512,
	CE_CLUSTER_PORTAL_SIZE = 0x40,
	CE_CLUSTER_PORTAL_VERTICES_OFFSET = 0x34,
	CE_PORTAL_VERTEX_SIZE = 0xc,
	CE_MAXIMUM_PORTAL_VERTICES = 128,
};

typedef char verify_ce_structure_material_size[sizeof(struct structure_material) == 0x100 ? 1 : -1];
typedef char verify_ce_structure_lightmap_size[sizeof(struct structure_lightmap) == 0x20 ? 1 : -1];

/* ---------- prototypes */

unsigned long ce_resources_allocate(unsigned long size);
unsigned long ce_resources_free_mark(void);
void ce_resources_free_to(unsigned long mark);
void rasterizer_geometry_compress_vertices(short type, long count, void *compressed, long compressed_size,
	void *uncompressed, long uncompressed_size);

/* ---------- globals */

/* the contiguous memory of the loaded BSP's vertices, freed with it */
static void **ce_bsp_vertex_memory;
static long ce_bsp_vertex_memory_count;
/* the tag cache's room before the loaded BSP's vertex buffers took theirs */
static unsigned long ce_bsp_free_mark;

/* ---------- private code */

/* a new vertex buffer over data (an Xbox address in contiguous memory): its
address, or 0 */
static unsigned long ce_vertex_buffer(
	unsigned long data)
{
	unsigned long address = ce_resources_allocate(3 * sizeof(unsigned long));
	unsigned long *buffer;

	if (!address)
		return 0;
	buffer = xbox_pointer(address);
	buffer[0] = D3DCOMMON_TYPE_VERTEXBUFFER | 1;
	buffer[1] = data;
	buffer[2] = 0;
	IDirect3DVertexBuffer8_Register((D3DVertexBuffer *)buffer, NULL);
	return address;
}

/* count vertices' unit vectors (vector_count of them at vectors_offset in
each vertex, vertex_size bytes apart) put within [-1, 1], as the vertex
compressor asserts they are within a hundredth of; not a number: 0 */
static void ce_unit_vectors(
	byte *vertices,
	long count,
	unsigned long vertex_size,
	unsigned long vectors_offset,
	short vector_count)
{
	long index;

	for (index = 0; index < count; index++)
	{
		real *components = (real *)(vertices + index * vertex_size + vectors_offset);
		short component;

		for (component = 0; component < 3 * vector_count; component++)
		{
			real value = components[component];

			components[component] = value != value ? 0.0f : value < -1.0f ? -1.0f : value > 1.0f ? 1.0f : value;
		}
	}
}

/* ---------- public code */

/* a Custom Edition map's structure BSP, in an image of it, checked before
the map is opened (ce_map_checks.c): its header, its structure in it, and
each lightmap material's vertices, in it, of the size their counts give and
of the kind ce_bsp_loaded converts; the room the conversion takes of the tag
cache in *bytes */
boolean ce_bsp_check(
	struct ce_image const *image,
	unsigned long *bytes)
{
	byte const *header = ce_image_pointer(image, image->base, CE_BSP_HEADER_SIZE);
	unsigned long words[6];
	byte *structure, *lightmaps;
	long lightmap_count, lightmap_index;

	*bytes = 0;
	if (!header)
		return ce_refuse("a structure BSP has no header");
	memcpy(words, header, sizeof(words));
	/* (its structure, and no Xbox vertex buffers to register) */
	if (words[5] != CE_BSP_HEADER_SIGNATURE || words[1] || words[3])
		return ce_refuse("a structure BSP's header is not a Custom Edition one");
	structure = ce_image_pointer(image, words[0], sizeof(struct structure_bsp));
	if (!structure || words[0] % 4)
		return ce_refuse("a structure BSP's structure (%08lx) is not in it", words[0]);
	if (!ce_image_block(image, &((struct structure_bsp *)structure)->lightmaps, sizeof(struct structure_lightmap),
		CE_MAXIMUM_BSP_LIGHTMAPS, "a structure BSP's lightmaps", &lightmap_count, &lightmaps))
	{
		return FALSE;
	}
	/* its clusters and their portals: no more clusters than the game marks
(structures.c), each portal between two of them, and of no more vertices
than sphere_intersects_cluster_portal projects into an array on the stack */
	{
		byte *clusters, *portals, *vertices;
		long cluster_count, portal_count, portal_index, vertex_count;

		if (!ce_image_block(image, &((struct structure_bsp *)structure)->clusters, CE_CLUSTER_SIZE,
			CE_MAXIMUM_CLUSTERS, "a structure BSP's clusters", &cluster_count, &clusters) ||
			!ce_image_block(image, &((struct structure_bsp *)structure)->cluster_portals, CE_CLUSTER_PORTAL_SIZE,
			CE_MAXIMUM_BSP_LIGHTMAPS, "a structure BSP's cluster portals", &portal_count, &portals))
		{
			return FALSE;
		}
		for (portal_index = 0; portal_index < portal_count; portal_index++)
		{
			byte *portal = portals + portal_index * CE_CLUSTER_PORTAL_SIZE;
			short cluster_indices[2];

			memcpy(cluster_indices, portal, sizeof(cluster_indices));
			if (cluster_indices[0] < 0 || cluster_indices[0] >= cluster_count || cluster_indices[1] < 0 ||
				cluster_indices[1] >= cluster_count)
			{
				return ce_refuse("a structure BSP's portal joins clusters %d and %d of %ld", cluster_indices[0],
					cluster_indices[1], cluster_count);
			}
			if (!ce_image_block(image, portal + CE_CLUSTER_PORTAL_VERTICES_OFFSET, CE_PORTAL_VERTEX_SIZE,
				CE_MAXIMUM_PORTAL_VERTICES, "a structure BSP's portal's vertices", &vertex_count, &vertices))
			{
				return FALSE;
			}
		}
	}
	for (lightmap_index = 0; lightmap_index < lightmap_count; lightmap_index++)
	{
		struct structure_lightmap *lightmap = (struct structure_lightmap *)(lightmaps +
			lightmap_index * sizeof(struct structure_lightmap));
		byte *materials;
		long material_count, material_index;

		if (!ce_image_block(image, &lightmap->materials, sizeof(struct structure_material),
			CE_MAXIMUM_LIGHTMAP_MATERIALS, "a structure BSP's lightmap materials", &material_count, &materials))
		{
			return FALSE;
		}
		for (material_index = 0; material_index < material_count; material_index++)
		{
			struct structure_material *material = (struct structure_material *)(materials +
				material_index * sizeof(struct structure_material));
			long vertex_count = material->vertices.count;
			long lightmap_vertex_count = material->lightmap_vertices.count;
			unsigned long data_size, needed;
			byte *data;

			if (vertex_count < 0 || vertex_count > CE_MAXIMUM_MATERIAL_VERTICES ||
				(lightmap_vertex_count && lightmap_vertex_count != vertex_count))
			{
				return ce_refuse("a structure BSP's material has %ld vertices and %ld lightmap vertices",
					vertex_count, lightmap_vertex_count);
			}
			if (!vertex_count)
				continue;
			if (material->vertices.type != _rasterizer_vertex_type_environment_uncompressed)
				return ce_refuse("a structure BSP's material's vertices are of type %d", material->vertices.type);
			if (!ce_image_data(image, &material->uncompressed_vertex_data, "a structure BSP's material's vertices",
				&data_size, &data))
			{
				return FALSE;
			}
			needed = (unsigned long)vertex_count * ENVIRONMENT_VERTEX_UNCOMPRESSED_SIZE +
				(unsigned long)lightmap_vertex_count * LIGHTMAP_VERTEX_UNCOMPRESSED_SIZE;
			if (data_size < needed)
			{
				return ce_refuse("a structure BSP's material's vertices (%lu bytes) are fewer than its counts (%lu)",
					data_size, needed);
			}
			*bytes += (lightmap_vertex_count ? 2 : 1) * CE_BUFFER_HEADER_ROOM;
		}
	}
	return TRUE;
}

/* a Custom Edition map's structure BSP loaded (cache_files.c) */
void ce_bsp_loaded(
	struct structure_bsp *structure)
{
	long materials = 0;
	long lightmap_index;

	ce_bsp_free_mark = ce_resources_free_mark();
	for (lightmap_index = 0; lightmap_index < structure->lightmaps.count; lightmap_index++)
	{
		struct structure_lightmap *lightmap = TAG_BLOCK_GET_ELEMENT(&structure->lightmaps, lightmap_index,
			struct structure_lightmap);
		long material_index;

		for (material_index = 0; material_index < lightmap->materials.count; material_index++)
		{
			struct structure_material *material = TAG_BLOCK_GET_ELEMENT(&lightmap->materials, material_index,
				struct structure_material);
			long vertex_count = material->vertices.count;
			long lightmap_vertex_count = material->lightmap_vertices.count;
			long environment_size = vertex_count * ENVIRONMENT_VERTEX_COMPRESSED_SIZE;
			long size = environment_size + lightmap_vertex_count * LIGHTMAP_VERTEX_COMPRESSED_SIZE;
			byte *uncompressed = xbox_pointer(material->uncompressed_vertex_data.address);
			byte *compressed;

			if (material->vertices.type != _rasterizer_vertex_type_environment_uncompressed || vertex_count <= 0 ||
				!uncompressed)
			{
				continue;
			}
			compressed = XPhysicalAlloc(size, -1, 0, PAGE_READWRITE);
			if (!compressed)
			{
				error(_error_silent, "Custom Edition maps: no memory for a BSP material's vertices");
				return;
			}
			ce_bsp_vertex_memory = realloc(ce_bsp_vertex_memory, (ce_bsp_vertex_memory_count + 1) * sizeof(void *));
			if (ce_bsp_vertex_memory)
				ce_bsp_vertex_memory[ce_bsp_vertex_memory_count++] = compressed;
			ce_unit_vectors(uncompressed, vertex_count, ENVIRONMENT_VERTEX_UNCOMPRESSED_SIZE,
				ENVIRONMENT_VERTEX_VECTORS_OFFSET, ENVIRONMENT_VERTEX_VECTOR_COUNT);
			rasterizer_geometry_compress_vertices(_rasterizer_vertex_type_environment_uncompressed, vertex_count,
				compressed, environment_size, uncompressed, vertex_count * ENVIRONMENT_VERTEX_UNCOMPRESSED_SIZE);
			if (lightmap_vertex_count > 0)
			{
				ce_unit_vectors(uncompressed + vertex_count * ENVIRONMENT_VERTEX_UNCOMPRESSED_SIZE,
					lightmap_vertex_count, LIGHTMAP_VERTEX_UNCOMPRESSED_SIZE, LIGHTMAP_VERTEX_VECTORS_OFFSET,
					LIGHTMAP_VERTEX_VECTOR_COUNT);
				rasterizer_geometry_compress_vertices(_rasterizer_vertex_type_environment_lightmap_uncompressed,
					lightmap_vertex_count, compressed + environment_size,
					lightmap_vertex_count * LIGHTMAP_VERTEX_COMPRESSED_SIZE,
					uncompressed + vertex_count * ENVIRONMENT_VERTEX_UNCOMPRESSED_SIZE,
					lightmap_vertex_count * LIGHTMAP_VERTEX_UNCOMPRESSED_SIZE);
			}
			material->compressed_vertex_data.size = size;
			material->compressed_vertex_data.address = xbox_address(compressed);
			material->vertices.type = _rasterizer_vertex_type_environment_compressed;
			material->vertices.offset = 0;
			material->vertices.base_address = xbox_address(compressed);
			material->vertices.hardware_format = ce_vertex_buffer(xbox_address(compressed));
			if (lightmap_vertex_count > 0)
			{
				material->lightmap_vertices.type = _rasterizer_vertex_type_environment_lightmap_compressed;
				material->lightmap_vertices.offset = 0;
				material->lightmap_vertices.base_address = xbox_address(compressed + environment_size);
				material->lightmap_vertices.hardware_format = ce_vertex_buffer(xbox_address(compressed + environment_size));
			}
			materials++;
		}
	}
	error(_error_silent, "Custom Edition maps: %ld BSP materials' vertices compressed", materials);
}

/* the BSP unloaded (cache_files.c) */
void ce_bsp_unloaded(
	void)
{
	long index;

	for (index = 0; index < ce_bsp_vertex_memory_count; index++)
		XPhysicalFree(ce_bsp_vertex_memory[index]);
	free(ce_bsp_vertex_memory);
	ce_bsp_vertex_memory = NULL;
	ce_bsp_vertex_memory_count = 0;
	/* (its vertex buffers' room, for the next BSP's) */
	ce_resources_free_to(ce_bsp_free_mark);
	ce_bsp_free_mark = 0;
}

#endif
