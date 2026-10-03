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
#include "ce_map_checks.h"

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
	/* (model_definitions.h's MAXIMUM_NODES_PER_MODEL_GEOMETRY_PART) */
	CE_PART_MAXIMUM_LOCAL_NODES = 22,
	/* a part's blocks of vertices and triangles, which the Xbox's tools
	leave empty, as Halo PC's do */
	PART_BLOCKS_OFFSET = 0x20,
	PART_BLOCKS_SIZE = 0x24,

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

/* a component of a unit vector, within [-1, 1] (not a number: 0), as the
compressor asserts it is within a hundredth of */
static real ce_unit_component(
	real value)
{
	if (value != value)
		return 0.0f;
	return value < -1.0f ? -1.0f : value > 1.0f ? 1.0f : value;
}

static unsigned long ce_compress_vector(
	real const *vector)
{
	union real_vector3d v;

	v.i = ce_unit_component(vector[0]);
	v.j = ce_unit_component(vector[1]);
	v.k = ce_unit_component(vector[2]);
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
			if (local_nodes && node_index < local_node_count && node_index < CE_PART_MAXIMUM_LOCAL_NODES)
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
				csmemset(xbox_part + PART_BLOCKS_OFFSET, 0, PART_BLOCKS_SIZE);
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

/* ---------- checks (ce_map_checks.c) */

enum
{
	/* a gbxmodel (model_definitions.h's model: the Xbox's, but for its parts) */
	MODEL_HEADER_SIZE = 0xe8,
	MODEL_MARKERS_OFFSET = 0xac,
	MODEL_NODES_OFFSET = 0xb8,
	MODEL_REGIONS_OFFSET = 0xc4,
	MODEL_SHADERS_OFFSET = 0xdc,
	MODEL_MARKER_SIZE = 0x40,
	MODEL_MARKER_INSTANCES_OFFSET = 0x34,
	MODEL_MARKER_INSTANCE_SIZE = 0x20,
	MODEL_NODE_SIZE = 0x9c,
	MODEL_NODE_LINKS_OFFSET = 0x20,
	MODEL_REGION_SIZE = 0x4c,
	MODEL_REGION_PERMUTATIONS_OFFSET = 0x40,
	MODEL_PERMUTATION_SIZE = 0x58,
	MODEL_PERMUTATION_GEOMETRIES_OFFSET = 0x40,
	MODEL_SHADER_SIZE = 0x20,
	/* (model_definitions.h's maximums; the renderer skins at most 43 nodes:
	rasterizer.h's RASTERIZER_MAXIMUM_NODES_PER_MODEL) */
	CE_MAXIMUM_MODEL_NODES = 43,
	CE_MAXIMUM_MODEL_MARKERS = 256,
	CE_MAXIMUM_MARKER_INSTANCES = 32,
	CE_MAXIMUM_MODEL_REGIONS = 32,
	CE_MAXIMUM_REGION_PERMUTATIONS = 32,
	CE_MAXIMUM_MODEL_GEOMETRIES = 256,
	CE_MAXIMUM_GEOMETRY_PARTS = 32,
	CE_MAXIMUM_MODEL_SHADERS = 32,
	CE_MAXIMUM_PART_VERTICES = 0xffff,
	CE_MAXIMUM_PART_TRIANGLES = 0xffff - 2,
	CE_PART_SHADER_INDEX_OFFSET = 0x04,
	CE_PART_PREVIOUS_PART_OFFSET = 0x06,
	CE_PART_NEXT_PART_OFFSET = 0x07,
	CE_PART_CENTROID_NODES_OFFSET = 0x08,
	CE_TRIANGLE_BUFFER_PRECOMPILED_STRIP = 1,
	/* (the room ce_models_tags_loaded takes of the tag cache: 16-byte
	aligned) */
	CE_BUFFER_HEADER_ROOM = 16,
	/* shaders' groups and types, Halo PC's (shader_definitions.h, with
	transparent chicago extended at 7) */
	CE_SHADER_GROUP = 'shdr',
	CE_SHADER_SIZE = 0x70,
};

#define CE_ALIGNED(size) (((size) + 15) & ~15UL)

static short ce_read_short(
	byte const *at)
{
	short value;

	memcpy(&value, at, sizeof(value));
	return value;
}

static long ce_read_long32(
	byte const *at)
{
	long value;

	memcpy(&value, at, sizeof(value));
	return value;
}

static boolean ce_is_shader(
	struct ce_tag_instance const *instance)
{
	return instance->group_tag == 'scex' || instance->group_tag == CE_SHADER_GROUP ||
		instance->parent_group_tags[0] == CE_SHADER_GROUP || instance->parent_group_tags[1] == CE_SHADER_GROUP;
}

/* a model's nodes: few enough to skin, and a tree, each node's parent
before it and its child and next sibling after it (so that walking it ends) */
static boolean ce_model_nodes_check(
	struct ce_image const *image,
	byte const *model,
	char const *name,
	long *node_count)
{
	byte *nodes;
	long index;

	if (!ce_image_block(image, model + MODEL_NODES_OFFSET, MODEL_NODE_SIZE, CE_MAXIMUM_MODEL_NODES, name,
		node_count, &nodes))
	{
		return FALSE;
	}
	if (!*node_count)
		return ce_refuse("model %s has no nodes", name);
	for (index = 0; index < *node_count; index++)
	{
		byte const *node = nodes + index * MODEL_NODE_SIZE;
		short link;

		for (link = 0; link < 3; link++)
		{
			short linked = ce_read_short(node + MODEL_NODE_LINKS_OFFSET + link * sizeof(short));

			if (linked != NONE && (linked < 0 || linked >= *node_count))
				return ce_refuse("model %s: node %ld is linked to node %d of %ld", name, index, linked, *node_count);
		}
	}
	/* (from the first node, by children and siblings, no node reached
	twice: walking the nodes ends) */
	{
		boolean reached[CE_MAXIMUM_MODEL_NODES] = { 0 };
		short stack[2 * CE_MAXIMUM_MODEL_NODES + 1];
		long stack_count = 0;

		stack[stack_count++] = 0;
		while (stack_count)
		{
			short node_index = stack[--stack_count];
			byte const *node = nodes + node_index * MODEL_NODE_SIZE;
			short next_sibling = ce_read_short(node + MODEL_NODE_LINKS_OFFSET);
			short first_child = ce_read_short(node + MODEL_NODE_LINKS_OFFSET + 2);

			if (reached[node_index])
				return ce_refuse("model %s: node %d is reached twice from the first", name, node_index);
			reached[node_index] = TRUE;
			if (next_sibling != NONE)
				stack[stack_count++] = next_sibling;
			if (first_child != NONE)
				stack[stack_count++] = first_child;
		}
	}
	return TRUE;
}

/* a part's strip and vertices: in the model data, each index one of its
vertices, each vertex's nodes the model's; the room its conversion takes */
static boolean ce_part_check(
	byte const *part,
	char const *name,
	long node_count,
	byte const *model_data,
	unsigned long model_data_size,
	unsigned long vertex_data_size,
	unsigned long *bytes)
{
	short triangle_type = ce_read_short(part + PART_TRIANGLE_BUFFER_OFFSET);
	long triangle_count = ce_read_long32(part + PART_TRIANGLE_BUFFER_OFFSET + 4);
	unsigned long triangle_offset = (unsigned long)ce_read_long32(part + PART_TRIANGLE_BUFFER_OFFSET + 8);
	long vertex_count = ce_read_long32(part + PART_VERTEX_BUFFER_OFFSET + 4);
	unsigned long vertex_offset = (unsigned long)ce_read_long32(part + PART_VERTEX_BUFFER_OFFSET + 16);
	byte local_node_count = part[CE_PART_LOCAL_NODE_COUNT_OFFSET];
	boolean local_nodes = (ce_read_long32(part) & CE_PART_LOCAL_NODES_FLAG) && local_node_count;
	unsigned long index_count;
	long index;

	if (triangle_type != CE_TRIANGLE_BUFFER_PRECOMPILED_STRIP || triangle_count < 0 ||
		triangle_count > CE_MAXIMUM_PART_TRIANGLES)
	{
		return ce_refuse("model %s: a part's triangles (%ld, of type %d) are not a strip", name, triangle_count,
			triangle_type);
	}
	if (vertex_count < 0 || vertex_count > CE_MAXIMUM_PART_VERTICES)
		return ce_refuse("model %s: a part has %ld vertices", name, vertex_count);
	index_count = (unsigned long)triangle_count + 2;
	if (!ce_range_within(vertex_offset, (unsigned long)vertex_count * CE_VERTEX_SIZE, vertex_data_size) ||
		triangle_offset > model_data_size ||
		!ce_range_within(vertex_data_size, index_count * sizeof(word), model_data_size) ||
		!ce_range_within(triangle_offset, index_count * sizeof(word), model_data_size - vertex_data_size))
	{
		return ce_refuse("model %s: a part's vertices or strip are not in the model data", name);
	}
	if (local_nodes && local_node_count > CE_PART_MAXIMUM_LOCAL_NODES)
		return ce_refuse("model %s: a part has %d local nodes", name, local_node_count);
	for (index = 0; local_nodes && index < local_node_count; index++)
	{
		if (part[CE_PART_LOCAL_NODES_OFFSET + index] >= node_count)
			return ce_refuse("model %s: a part's local node %ld is node %d", name, index,
				part[CE_PART_LOCAL_NODES_OFFSET + index]);
	}
	/* (the strip, used whole by the renderer: every index one of the
	part's vertices) */
	for (index = 0; index < (long)index_count; index++)
	{
		word vertex_index;

		memcpy(&vertex_index, model_data + vertex_data_size + triangle_offset + index * sizeof(word),
			sizeof(vertex_index));
		if (vertex_index >= vertex_count)
			return ce_refuse("model %s: a part's strip names vertex %u of %ld", name, vertex_index, vertex_count);
	}
	/* (each vertex's nodes, as ce_part_vertices finds them) */
	for (index = 0; index < vertex_count; index++)
	{
		struct ce_vertex vertex;
		short node;

		memcpy(&vertex, model_data + vertex_offset + index * CE_VERTEX_SIZE, sizeof(vertex));
		for (node = 0; node < 2; node++)
		{
			short node_index = vertex.node_indices[node];

			if (node_index < 0)
				node_index = vertex.node_indices[0] < 0 ? 0 : vertex.node_indices[0];
			if (local_nodes && node_index < local_node_count)
				node_index = part[CE_PART_LOCAL_NODES_OFFSET + node_index];
			if (node_index >= node_count)
				return ce_refuse("model %s: a vertex is skinned to node %d of %ld", name, node_index, node_count);
		}
	}
	*bytes += CE_ALIGNED(index_count * sizeof(word)) + 2 * CE_BUFFER_HEADER_ROOM;
	return TRUE;
}

/* every gbxmodel of a map being checked: its blocks in the tags, its
indices each naming one of what it indexes, its parts' strips and vertices
in the model data; the room its conversion takes of the tag cache added to
*bytes (ce_models_tags_loaded) */
boolean ce_models_check(
	struct ce_image const *image,
	void const *tag_instances,
	long tag_count,
	byte const *model_data,
	unsigned long model_data_size,
	unsigned long vertex_data_size,
	unsigned long *bytes)
{
	long index;

	*bytes = 0;
	for (index = 0; index < tag_count; index++)
	{
		struct ce_tag_instance const *instance = (struct ce_tag_instance const *)((byte const *)tag_instances +
			index * CE_TAG_INSTANCE_SIZE);
		char const *name;
		byte *model, *markers, *regions, *geometries, *shaders;
		long node_count, marker_count, region_count, geometry_count, shader_count, item;

		if (instance->group_tag != 'mod2')
			continue;
		name = ce_image_tag_name(image, instance);
		model = ce_image_pointer(image, instance->base_address, MODEL_HEADER_SIZE);
		if (!model)
			return ce_refuse("model %s is not in the tags", name);
		if (!ce_model_nodes_check(image, model, name, &node_count) ||
			!ce_image_block(image, model + MODEL_MARKERS_OFFSET, MODEL_MARKER_SIZE, CE_MAXIMUM_MODEL_MARKERS, name,
				&marker_count, &markers) ||
			!ce_image_block(image, model + MODEL_REGIONS_OFFSET, MODEL_REGION_SIZE, CE_MAXIMUM_MODEL_REGIONS, name,
				&region_count, &regions) ||
			!ce_image_block(image, model + MODEL_GEOMETRIES_OFFSET, GEOMETRY_SIZE, CE_MAXIMUM_MODEL_GEOMETRIES, name,
				&geometry_count, &geometries) ||
			!ce_image_block(image, model + MODEL_SHADERS_OFFSET, MODEL_SHADER_SIZE, CE_MAXIMUM_MODEL_SHADERS, name,
				&shader_count, &shaders))
		{
			return FALSE;
		}
		/* (the shaders: each a shader's tag, as tag_get asserts) */
		for (item = 0; item < shader_count; item++)
		{
			unsigned long shader_index = (unsigned long)ce_read_long32(shaders + item * MODEL_SHADER_SIZE + 0xc);
			struct ce_tag_instance const *shader = (struct ce_tag_instance const *)((byte const *)tag_instances +
				(shader_index & 0xffff) * CE_TAG_INSTANCE_SIZE);

			if ((shader_index & 0xffff) >= (unsigned long)tag_count || shader->tag_index != shader_index ||
				!ce_is_shader(shader))
			{
				return ce_refuse("model %s: shader %ld is not a shader (%08lx)", name, item, shader_index);
			}
		}
		/* (the markers' instances: of the model's nodes and regions) */
		for (item = 0; item < marker_count; item++)
		{
			byte *instances;
			long instance_count, instance_index;

			if (!ce_image_block(image, markers + item * MODEL_MARKER_SIZE + MODEL_MARKER_INSTANCES_OFFSET,
				MODEL_MARKER_INSTANCE_SIZE, CE_MAXIMUM_MARKER_INSTANCES, name, &instance_count, &instances))
			{
				return FALSE;
			}
			for (instance_index = 0; instance_index < instance_count; instance_index++)
			{
				byte const *marker = instances + instance_index * MODEL_MARKER_INSTANCE_SIZE;

				if (marker[0] >= region_count || marker[2] >= node_count)
					return ce_refuse("model %s: a marker is on region %d, node %d", name, marker[0], marker[2]);
			}
		}
		/* (the regions' permutations: each level of detail one of the
		model's geometries, or none) */
		for (item = 0; item < region_count; item++)
		{
			byte *permutations;
			long permutation_count, permutation;

			if (!ce_image_block(image, regions + item * MODEL_REGION_SIZE + MODEL_REGION_PERMUTATIONS_OFFSET,
				MODEL_PERMUTATION_SIZE, CE_MAXIMUM_REGION_PERMUTATIONS, name, &permutation_count, &permutations))
			{
				return FALSE;
			}
			for (permutation = 0; permutation < permutation_count; permutation++)
			{
				short level;

				for (level = 0; level < 5; level++)
				{
					short geometry = ce_read_short(permutations + permutation * MODEL_PERMUTATION_SIZE +
						MODEL_PERMUTATION_GEOMETRIES_OFFSET + level * sizeof(short));

					if (geometry != NONE && (geometry < 0 || geometry >= geometry_count))
						return ce_refuse("model %s: a permutation draws geometry %d of %ld", name, geometry,
							geometry_count);
				}
			}
		}
		/* (the geometries' parts) */
		for (item = 0; item < geometry_count; item++)
		{
			byte *parts;
			long part_count, part;

			if (!ce_image_block(image, geometries + item * GEOMETRY_SIZE + GEOMETRY_PARTS_OFFSET, CE_PART_SIZE,
				CE_MAXIMUM_GEOMETRY_PARTS, name, &part_count, &parts))
			{
				return FALSE;
			}
			*bytes += CE_ALIGNED((unsigned long)part_count * XBOX_PART_SIZE);
			for (part = 0; part < part_count; part++)
			{
				byte const *at = parts + part * CE_PART_SIZE;
				short shader_index = ce_read_short(at + CE_PART_SHADER_INDEX_OFFSET);
				signed char previous = (signed char)at[CE_PART_PREVIOUS_PART_OFFSET];
				signed char next = (signed char)at[CE_PART_NEXT_PART_OFFSET];
				short primary = ce_read_short(at + CE_PART_CENTROID_NODES_OFFSET);
				short secondary = ce_read_short(at + CE_PART_CENTROID_NODES_OFFSET + 2);

				if (shader_index < 0 || shader_index >= shader_count)
					return ce_refuse("model %s: a part's shader is %d of %ld", name, shader_index, shader_count);
				if ((previous != NONE && (previous < 0 || previous >= part_count)) ||
					(next != NONE && (next < 0 || next >= part_count)))
				{
					return ce_refuse("model %s: a part's neighbours are parts %d and %d of %ld", name, previous, next,
						part_count);
				}
				if (primary < 0 || primary >= node_count || secondary < 0 || secondary >= node_count)
					return ce_refuse("model %s: a part's centroid is on nodes %d and %d", name, primary, secondary);
				if (!ce_part_check(at, name, node_count, model_data, model_data_size, vertex_data_size, bytes))
					return FALSE;
			}
		}
	}
	return TRUE;
}

/* every shader of a map being checked: in the tags, and of its group's type
(Halo PC's numbering, which ce_shaders_tags_loaded makes the Xbox's) */
boolean ce_shaders_check(
	struct ce_image const *image,
	void const *tag_instances,
	long tag_count)
{
	static struct
	{
		unsigned long group_tag;
		short type;
	} const types[] =
	{
		{ 'senv', 3 }, { 'soso', 4 }, { 'sotr', 5 }, { 'schi', 6 }, { 'scex', 7 },
		{ 'swat', 8 }, { 'sgla', 9 }, { 'smet', 10 }, { 'spla', 11 },
	};
	long index;

	for (index = 0; index < tag_count; index++)
	{
		struct ce_tag_instance const *instance = (struct ce_tag_instance const *)((byte const *)tag_instances +
			index * CE_TAG_INSTANCE_SIZE);
		byte const *shader;
		short type;
		long type_index;

		if (!ce_is_shader(instance))
			continue;
		shader = ce_image_pointer(image, instance->base_address, CE_SHADER_SIZE);
		if (!shader)
			return ce_refuse("shader %s is not in the tags", ce_image_tag_name(image, instance));
		type = ce_read_short(shader + SHADER_TYPE_OFFSET);
		for (type_index = 0; type_index < (long)NUMBEROF(types); type_index++)
		{
			if (types[type_index].group_tag == instance->group_tag)
				break;
		}
		if (type_index < (long)NUMBEROF(types) ? type != types[type_index].type : (type < 0 || type > 11))
		{
			return ce_refuse("shader %s is of type %d, not its group's", ce_image_tag_name(image, instance), type);
		}
	}
	return TRUE;
}

#endif
