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
*/

#ifdef HALO_64BIT

#include "cseries.h"
#include "cseries_windows.h"
#include "errors.h"
#include "rasterizer/rasterizer_geometry.h"
#include "structures/structure_bsp_definitions.h"

#include <xtl.h>
#include <stdlib.h>

/* ---------- constants */

enum
{
	ENVIRONMENT_VERTEX_UNCOMPRESSED_SIZE = 0x38,
	ENVIRONMENT_VERTEX_COMPRESSED_SIZE = 0x20,
	LIGHTMAP_VERTEX_UNCOMPRESSED_SIZE = 0x14,
	LIGHTMAP_VERTEX_COMPRESSED_SIZE = 0x8,
};

/* ---------- prototypes */

unsigned long ce_resources_allocate(unsigned long size);
void rasterizer_geometry_compress_vertices(short type, long count, void *compressed, long compressed_size,
	void *uncompressed, long uncompressed_size);

/* ---------- globals */

/* the contiguous memory of the loaded BSP's vertices, freed with it */
static void **ce_bsp_vertex_memory;
static long ce_bsp_vertex_memory_count;

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

/* ---------- public code */

/* a Custom Edition map's structure BSP loaded (cache_files.c) */
void ce_bsp_loaded(
	struct structure_bsp *structure)
{
	long materials = 0;
	long lightmap_index;

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
			rasterizer_geometry_compress_vertices(_rasterizer_vertex_type_environment_uncompressed, vertex_count,
				compressed, environment_size, uncompressed, vertex_count * ENVIRONMENT_VERTEX_UNCOMPRESSED_SIZE);
			if (lightmap_vertex_count > 0)
			{
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
}

#endif
