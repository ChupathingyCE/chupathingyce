/*
CE_MODELS.C

Custom Edition maps' models (cache_files.c, Custom Edition maps). Halo PC's
models are gbxmodels (mod2): the Xbox's models (mode) in all but their
geometry, whose parts are larger (local nodes at their end) and whose
vertices and triangle strips are in one block of the map's file, the
vertices uncompressed. When the map's tags load, each gbxmodel becomes a
model as the Xbox's renderer draws it:

  - its geometries' parts are copied into the Xbox's part layout (in the
    map's tag cache, after its resources: ce_resources_allocate);
  - each part's vertices are read from the map, compressed as the Xbox's
    tools compress them (rasterizer.c's rasterizer_model_vertex_compressed:
    normals, binormals and tangents 11:11:10, texture coordinates as 16-bit
    fractions of the model's base map scale, node indices times three, the
    first node's weight a 16-bit fraction; a part with local nodes has its
    vertices' nodes looked up in them), and put in contiguous memory behind
    a Direct3D vertex buffer, as the Xbox's maps' are;
  - its triangle strip is copied into the tag cache behind a Direct3D index
    buffer;
  - its tag becomes a model's (mode).

A part with local nodes names its centroid's nodes by their local index too;
they are looked up in its local nodes, as its vertices' are (the renderer
places a transparent part by its centroid's primary node).

Halo PC's model shaders keep a multipurpose map's masks in other channels
than the Xbox's: the auxiliary (detail) mask, self-illumination, specular and
color change in red, green, blue and alpha, where the Xbox's model shaders
read specular from red, self-illumination from green, color change from blue
and the auxiliary mask from alpha (the cyborg's first-person arms' map,
decoded from both Blood Gulches: the Xbox's red is Halo PC's blue, its blue
Halo PC's alpha). Drawn as they are, the arms' armor took its specular
highlight from the detail mask and its color change from the specular mask,
the plates pale and untinted. Every bitmap a model shader draws as its
multipurpose map is listed (ce_models_bitmap_is_multipurpose), and the
texture cache has the renderer sample its channels from where Halo PC keeps
them (xbox_texture_cache.c, D3DCOMMON_PORT_PC_MULTIPURPOSE; xbox_textures.c,
a texture swizzle). A bitmap a model shader also draws as its base or detail
map keeps its channels, which the renderer has one order of.
*/

#ifdef HALO_64BIT

#include "cseries.h"
#include "cseries_windows.h"
#include "errors.h"
#include "math/real_math.h"
#include "rasterizer/rasterizer_geometry.h"

#include <xtl.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	CE_TAG_INSTANCE_SIZE = 0x20,

	/* a model's geometries, a geometry's parts */
	MODEL_GEOMETRIES_OFFSET = 0xd0,
	GEOMETRY_SIZE = 0x30,
	GEOMETRY_PARTS_OFFSET = 0x24,

	/* a gbxmodel's part, and the Xbox's */
	CE_PART_SIZE = 0x84,
	XBOX_PART_SIZE = 0x68,
	PART_HEADER_SIZE = 0x44, /* flags to the blocks, the same in both */
	PART_CENTROID_PRIMARY_NODE_OFFSET = 0x08,
	PART_CENTROID_SECONDARY_NODE_OFFSET = 0x0a,
	PART_TRIANGLE_BUFFER_OFFSET = 0x44,
	PART_VERTEX_BUFFER_OFFSET = 0x54,
	CE_PART_LOCAL_NODE_COUNT_OFFSET = 0x6b,
	CE_PART_LOCAL_NODES_OFFSET = 0x6c,
	CE_PART_LOCAL_NODES_FLAG = 2,

	/* shaders (shader_definitions.h's shader_base: its type), and a
	transparent chicago shader's extra flags (rasterizer_xbox_transparent_geometry.c),
	and an extended one's (after its two-stage maps) */
	SHADER_TYPE_OFFSET = 0x24,
	CHICAGO_EXTRA_FLAGS_OFFSET = 0x60,
	CHICAGO_EXTENDED_EXTRA_FLAGS_OFFSET = 0x6c,
	CE_SHADER_TYPE_TRANSPARENT_CHICAGO = 6,
	CE_SHADER_TYPE_TRANSPARENT_CHICAGO_EXTENDED = 7,

	/* a model shader's (rasterizer_xbox_models.c) base, multipurpose and
	detail maps: their tag indices */
	MODEL_SHADER_BASE_MAP_INDEX_OFFSET = 0xb0,
	MODEL_SHADER_MULTIPURPOSE_MAP_INDEX_OFFSET = 0xc8,
	MODEL_SHADER_DETAIL_MAP_INDEX_OFFSET = 0xe8,
	/* a bitmap group's bitmaps (bitmap_data) */
	BITMAP_GROUP_BITMAPS_OFFSET = 0x60,
	BITMAP_SIZE = 0x30,

	CE_VERTEX_SIZE = 0x44,
	XBOX_VERTEX_SIZE = 0x20,
	XBOX_MODEL_VERTEX_TYPE = 5, /* (_rasterizer_vertex_type_model_compressed) */
};

/* ---------- structures */

struct ce_vertex
{
	real position[3];
	real normal[3];
	real binormal[3];
	real tangent[3];
	real texture_coordinates[2];
	short node_indices[2];
	real node_weights[2];
};

struct xbox_vertex
{
	real position[3];
	unsigned long normal;
	unsigned long binormal;
	unsigned long tangent;
	short texture_coordinates[2];
	char node_indices[2];
	short node_weight;
};

/* (cache_files.c's) */
struct ce_tag_instance
{
	unsigned long group_tag;
	unsigned long parent_group_tags[2];
	unsigned long tag_index;
	unsigned long name;
	unsigned long base_address;
	unsigned long indexed;
	unsigned long unused;
};

/* ---------- prototypes */

unsigned long ce_resources_allocate(unsigned long size);
unsigned long compress_real_vector3d_to_int32_clamp(union real_vector3d const *v);
short compress_real_to_int16_clamp(real z);

/* ---------- globals */

/* the contiguous memory of the map's vertices, freed with it */
static void **ce_vertex_memory;
static long ce_vertex_memory_count;

/* the bitmaps (bitmap_data) model shaders draw as multipurpose maps, whose
channels are Halo PC's */
static void const **ce_multipurpose_bitmaps;
static long ce_multipurpose_bitmap_count;

/* ---------- private code */

static unsigned long ce_compress_vector(
	real const *vector)
{
	union real_vector3d v;

	v.i = vector[0];
	v.j = vector[1];
	v.k = vector[2];
	return compress_real_vector3d_to_int32_clamp(&v);
}

/* a part's vertices compressed into contiguous memory behind a new vertex
buffer: the buffer's address, or 0 */
static unsigned long ce_part_vertices(
	byte const *model_data,
	unsigned long vertex_offset,
	long count,
	byte const *part)
{
	struct xbox_vertex *vertices;
	unsigned long *buffer = NULL;
	unsigned long buffer_address = ce_resources_allocate(3 * sizeof(unsigned long));
	byte local_node_count = part[CE_PART_LOCAL_NODE_COUNT_OFFSET];
	boolean local_nodes = (*(unsigned long const *)part & CE_PART_LOCAL_NODES_FLAG) && local_node_count;
	long index;

	if (!buffer_address || count <= 0)
		return 0;
	vertices = XPhysicalAlloc(count * XBOX_VERTEX_SIZE, -1, 0, PAGE_READWRITE);
	if (!vertices)
		return 0;
	ce_vertex_memory = realloc(ce_vertex_memory, (ce_vertex_memory_count + 1) * sizeof(void *));
	if (ce_vertex_memory)
		ce_vertex_memory[ce_vertex_memory_count++] = vertices;
	for (index = 0; index < count; index++)
	{
		struct ce_vertex const *in = (struct ce_vertex const *)(model_data + vertex_offset +
			index * CE_VERTEX_SIZE);
		struct xbox_vertex *out = &vertices[index];
		short node;

		out->position[0] = in->position[0];
		out->position[1] = in->position[1];
		out->position[2] = in->position[2];
		out->normal = ce_compress_vector(in->normal);
		out->binormal = ce_compress_vector(in->binormal);
		out->tangent = ce_compress_vector(in->tangent);
		/* (fractions of the base map scale in both: Halo PC's are not
		multiplied by it, the renderer multiplies both by it; the warthog's
		scale is 2 by 3, its coordinates 0.33 to 1) */
		out->texture_coordinates[0] = compress_real_to_int16_clamp(in->texture_coordinates[0]);
		out->texture_coordinates[1] = compress_real_to_int16_clamp(in->texture_coordinates[1]);
		for (node = 0; node < 2; node++)
		{
			short node_index = in->node_indices[node];

			if (node_index < 0)
				node_index = in->node_indices[0] < 0 ? 0 : in->node_indices[0];
			if (local_nodes && node_index < local_node_count)
				node_index = part[CE_PART_LOCAL_NODES_OFFSET + node_index];
			out->node_indices[node] = (char)(node_index * 3);
		}
		out->node_weight = compress_real_to_int16_clamp(in->node_weights[0]);
	}
	buffer = xbox_pointer(buffer_address);
	buffer[0] = D3DCOMMON_TYPE_VERTEXBUFFER | 1;
	buffer[1] = xbox_address(vertices);
	buffer[2] = 0;
	IDirect3DVertexBuffer8_Register((D3DVertexBuffer *)buffer, NULL);
	return buffer_address;
}

/* a part's triangle strip (count + 2 indices) copied into the tag cache
behind a new index buffer: the buffer's address, or 0 */
static unsigned long ce_part_triangles(
	byte const *model_data,
	unsigned long index_offset,
	long count,
	unsigned long *strip_address)
{
	unsigned long size = (count + 2) * sizeof(word);
	unsigned long indices = ce_resources_allocate(size);
	unsigned long buffer_address = ce_resources_allocate(3 * sizeof(unsigned long));
	unsigned long *buffer;

	if (!indices || !buffer_address || count < 0)
		return 0;
	csmemcpy(xbox_pointer(indices), model_data + index_offset, size);
	buffer = xbox_pointer(buffer_address);
	buffer[0] = D3DCOMMON_TYPE_INDEXBUFFER | 1;
	buffer[1] = indices;
	buffer[2] = 0;
	*strip_address = indices;
	return buffer_address;
}

/* a part's centroid node: a local node's index made the model's */
static void ce_part_centroid_node(
	byte *node_index,
	byte const *part)
{
	byte local_node_count = part[CE_PART_LOCAL_NODE_COUNT_OFFSET];
	short index = *(short *)node_index;

	if ((*(unsigned long const *)part & CE_PART_LOCAL_NODES_FLAG) && index >= 0 && index < local_node_count)
		*(short *)node_index = part[CE_PART_LOCAL_NODES_OFFSET + index];
}

/* the bitmap group a tag index names, or NULL */
static byte *ce_bitmap_group(
	void *tag_instances,
	long tag_count,
	long tag_index)
{
	struct ce_tag_instance *instance;

	if (tag_index == -1 || (tag_index & 0xffff) >= tag_count)
		return NULL;
	instance = (struct ce_tag_instance *)((byte *)tag_instances + (tag_index & 0xffff) * CE_TAG_INSTANCE_SIZE);
	if (instance->group_tag != 'bitm' || instance->tag_index != (unsigned long)tag_index || !instance->base_address)
		return NULL;
	return xbox_pointer(instance->base_address);
}

static boolean ce_multipurpose_bitmap_listed(
	void const *bitmap)
{
	long index;

	for (index = 0; index < ce_multipurpose_bitmap_count; index++)
	{
		if (ce_multipurpose_bitmaps[index] == bitmap)
			return TRUE;
	}
	return FALSE;
}

/* a bitmap group's bitmaps listed as multipurpose maps (once), or, with
remove, taken off the list: the number listed or taken off */
static long ce_multipurpose_bitmaps_list(
	byte const *bitmap_group,
	boolean remove)
{
	long count = *(long const *)(bitmap_group + BITMAP_GROUP_BITMAPS_OFFSET);
	unsigned long address = *(unsigned long const *)(bitmap_group + BITMAP_GROUP_BITMAPS_OFFSET + 4);
	long changed = 0;
	long index;

	if (count <= 0 || !address)
		return 0;
	for (index = 0; index < count; index++)
	{
		void const *bitmap = (byte const *)xbox_pointer(address) + index * BITMAP_SIZE;
		long listed;

		if (remove)
		{
			for (listed = 0; listed < ce_multipurpose_bitmap_count; listed++)
			{
				if (ce_multipurpose_bitmaps[listed] == bitmap)
				{
					ce_multipurpose_bitmaps[listed] = ce_multipurpose_bitmaps[--ce_multipurpose_bitmap_count];
					changed++;
					break;
				}
			}
		}
		else if (!ce_multipurpose_bitmap_listed(bitmap))
		{
			void const **bitmaps = realloc((void *)ce_multipurpose_bitmaps,
				(ce_multipurpose_bitmap_count + 1) * sizeof(void const *));

			if (!bitmaps)
				return changed;
			ce_multipurpose_bitmaps = bitmaps;
			ce_multipurpose_bitmaps[ce_multipurpose_bitmap_count++] = bitmap;
			changed++;
		}
	}
	return changed;
}

/* the multipurpose maps of the map's model shaders listed, but for those a
model shader also draws as its base or detail map: the number listed */
static long ce_multipurpose_bitmaps_find(
	void *tag_instances,
	long tag_count)
{
	long pass, index;
	long kept = 0;

	/* (the multipurpose maps, then the base and detail maps taken off) */
	for (pass = 0; pass < 2; pass++)
	{
		for (index = 0; index < tag_count; index++)
		{
			struct ce_tag_instance *instance = (struct ce_tag_instance *)((byte *)tag_instances +
				index * CE_TAG_INSTANCE_SIZE);
			byte const *shader;
			byte *group;

			if (instance->group_tag != 'soso' || !instance->base_address)
				continue;
			shader = xbox_pointer(instance->base_address);
			if (!pass)
			{
				group = ce_bitmap_group(tag_instances, tag_count,
					*(long const *)(shader + MODEL_SHADER_MULTIPURPOSE_MAP_INDEX_OFFSET));
				if (group)
					ce_multipurpose_bitmaps_list(group, FALSE);
				continue;
			}
			group = ce_bitmap_group(tag_instances, tag_count, *(long const *)(shader + MODEL_SHADER_BASE_MAP_INDEX_OFFSET));
			if (group)
				kept += ce_multipurpose_bitmaps_list(group, TRUE);
			group = ce_bitmap_group(tag_instances, tag_count,
				*(long const *)(shader + MODEL_SHADER_DETAIL_MAP_INDEX_OFFSET));
			if (group)
				kept += ce_multipurpose_bitmaps_list(group, TRUE);
		}
	}
	if (kept)
	{
		error(_error_silent, "Custom Edition maps: %ld multipurpose bitmaps also drawn as base or detail maps keep "
			"their channels", kept);
	}
	return ce_multipurpose_bitmap_count;
}

/* ---------- public code */

/* a Custom Edition map's tags and resources loaded (cache_files.c): its
gbxmodels made models; model_data its model data block (the vertices,
vertex_data_size bytes, then the triangles) */
boolean ce_models_tags_loaded(
	void *tag_instances,
	long tag_count,
	byte const *model_data,
	unsigned long vertex_data_size)
{
	long models = 0, parts_converted = 0;
	long index;

	for (index = 0; index < tag_count; index++)
	{
		struct ce_tag_instance *instance = (struct ce_tag_instance *)((byte *)tag_instances +
			index * CE_TAG_INSTANCE_SIZE);
		byte *model;
		unsigned long geometry_count, geometries;
		unsigned long geometry;

		if (instance->group_tag != 'mod2')
			continue;
		model = xbox_pointer(instance->base_address);
		geometry_count = *(unsigned long *)(model + MODEL_GEOMETRIES_OFFSET);
		geometries = *(unsigned long *)(model + MODEL_GEOMETRIES_OFFSET + 4);
		for (geometry = 0; geometry < geometry_count; geometry++)
		{
			byte *geometry_data = (byte *)xbox_pointer(geometries) + geometry * GEOMETRY_SIZE;
			unsigned long part_count = *(unsigned long *)(geometry_data + GEOMETRY_PARTS_OFFSET);
			byte const *ce_parts = xbox_pointer(*(unsigned long *)(geometry_data + GEOMETRY_PARTS_OFFSET + 4));
			unsigned long xbox_parts_address = part_count ? ce_resources_allocate(part_count * XBOX_PART_SIZE) : 0;
			unsigned long part;

			if (part_count && !xbox_parts_address)
			{
				error(_error_silent, "Custom Edition maps: no room for the models' parts");
				return FALSE;
			}
			for (part = 0; part < part_count; part++)
			{
				byte const *ce_part = ce_parts + part * CE_PART_SIZE;
				byte *xbox_part = (byte *)xbox_pointer(xbox_parts_address) + part * XBOX_PART_SIZE;
				short triangle_type = *(short const *)(ce_part + PART_TRIANGLE_BUFFER_OFFSET);
				long triangle_count = *(long const *)(ce_part + PART_TRIANGLE_BUFFER_OFFSET + 4);
				unsigned long triangle_offset = *(unsigned long const *)(ce_part + PART_TRIANGLE_BUFFER_OFFSET + 8);
				long vertex_count = *(long const *)(ce_part + PART_VERTEX_BUFFER_OFFSET + 4);
				unsigned long vertex_offset = *(unsigned long const *)(ce_part + PART_VERTEX_BUFFER_OFFSET + 16);
				unsigned long strip = 0;

				csmemset(xbox_part, 0, XBOX_PART_SIZE);
				csmemcpy(xbox_part, ce_part, PART_HEADER_SIZE);
				ce_part_centroid_node(xbox_part + PART_CENTROID_PRIMARY_NODE_OFFSET, ce_part);
				ce_part_centroid_node(xbox_part + PART_CENTROID_SECONDARY_NODE_OFFSET, ce_part);
				/* the triangle buffer: type, count, the strip, its index buffer */
				*(short *)(xbox_part + PART_TRIANGLE_BUFFER_OFFSET) = triangle_type;
				*(long *)(xbox_part + PART_TRIANGLE_BUFFER_OFFSET + 4) = triangle_count;
				*(unsigned long *)(xbox_part + PART_TRIANGLE_BUFFER_OFFSET + 12) =
					ce_part_triangles(model_data, vertex_data_size + triangle_offset, triangle_count, &strip);
				*(unsigned long *)(xbox_part + PART_TRIANGLE_BUFFER_OFFSET + 8) = strip;
				/* the vertex buffer: type, count, its vertices */
				*(short *)(xbox_part + PART_VERTEX_BUFFER_OFFSET) = XBOX_MODEL_VERTEX_TYPE;
				*(long *)(xbox_part + PART_VERTEX_BUFFER_OFFSET + 4) = vertex_count;
				*(unsigned long *)(xbox_part + PART_VERTEX_BUFFER_OFFSET + 16) =
					ce_part_vertices(model_data, vertex_offset, vertex_count, ce_part);
				parts_converted++;
			}
			*(unsigned long *)(geometry_data + GEOMETRY_PARTS_OFFSET + 4) = xbox_parts_address;
		}
		instance->group_tag = 'mode';
		models++;
	}
	error(_error_silent, "Custom Edition maps: %ld gbxmodels made models (%ld parts)", models, parts_converted);
	return TRUE;
}

/* a Custom Edition map's shaders made the Xbox's (cache_files.c): Halo PC
added a shader type after transparent chicago (7, transparent chicago
extended, scex), so its later types are one more than the Xbox's (water 8,
glass 9, meter 10, plasma 11; the Xbox's 7 to 10). Each type is put back;
each extended chicago shader becomes a chicago one (schi): its four-stage
maps are where a chicago shader's maps are, its two-stage maps (the fallback
for older graphics cards) are dropped, and its extra flags move up to where
a chicago shader's are */
void ce_shaders_tags_loaded(
	void *tag_instances,
	long tag_count)
{
	long extended = 0, shifted = 0;
	long index;

	for (index = 0; index < tag_count; index++)
	{
		struct ce_tag_instance *instance = (struct ce_tag_instance *)((byte *)tag_instances +
			index * CE_TAG_INSTANCE_SIZE);
		byte *shader;
		short *type;

		if (instance->group_tag != 'scex' && instance->parent_group_tags[0] != 'shdr' &&
			instance->parent_group_tags[1] != 'shdr')
		{
			continue;
		}
		shader = xbox_pointer(instance->base_address);
		type = (short *)(shader + SHADER_TYPE_OFFSET);
		if (instance->group_tag == 'scex')
		{
			*(unsigned long *)(shader + CHICAGO_EXTRA_FLAGS_OFFSET) =
				*(unsigned long *)(shader + CHICAGO_EXTENDED_EXTRA_FLAGS_OFFSET);
			*type = CE_SHADER_TYPE_TRANSPARENT_CHICAGO;
			instance->group_tag = 'schi';
			instance->parent_group_tags[0] = 'shdr';
			instance->parent_group_tags[1] = 0xffffffff;
			extended++;
		}
		else if (*type > CE_SHADER_TYPE_TRANSPARENT_CHICAGO_EXTENDED)
		{
			(*type)--;
			shifted++;
		}
	}
	error(_error_silent, "Custom Edition maps: %ld extended chicago shaders made chicago ones, %ld shader types moved",
		extended, shifted);
	error(_error_silent, "Custom Edition maps: %ld multipurpose bitmaps sampled in Halo PC's channels",
		ce_multipurpose_bitmaps_find(tag_instances, tag_count));
}

/* the map unloaded (cache_files.c): its models' vertices freed, no bitmaps
multipurpose maps */
void ce_models_tags_unloaded(
	void)
{
	long index;

	for (index = 0; index < ce_vertex_memory_count; index++)
		XPhysicalFree(ce_vertex_memory[index]);
	free(ce_vertex_memory);
	ce_vertex_memory = NULL;
	ce_vertex_memory_count = 0;
	free((void *)ce_multipurpose_bitmaps);
	ce_multipurpose_bitmaps = NULL;
	ce_multipurpose_bitmap_count = 0;
}

/* whether a bitmap (bitmap_data) of the map loaded is a Custom Edition model
shader's multipurpose map, its channels Halo PC's (xbox_texture_cache.c) */
boolean ce_models_bitmap_is_multipurpose(
	void const *bitmap)
{
	return ce_multipurpose_bitmap_listed(bitmap);
}

#endif
