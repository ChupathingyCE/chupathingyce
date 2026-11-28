/*
H2_WORLD_LOAD.C

(port) h2_world.h: a Halo 2 level's world read from its MCC map as the level
loads. Included at the end of h2_level.c: the world is laid out by the
level builder's own code (section_read, page_place, geometry_add_whole and
its tone of the lightmap pages), so that what is drawn is what the made
level holds - but read now, from the map, rather than from the made file.

What is read: the structure BSP shown now (a switch reads the next one),
its clusters' and instances' render geometry
(the instances placed in the world as the builder places them, for now) and
its lightmap's pages, into runs of one shader and one atlas
(struct h2_world_group). Not yet: the water definitions' sea, breakable panes, the sky
and fog planes, the decorators (the builder adds them; they are the made
level's as before), the clusters' visibility (every run drawn).
*/

#include "h2_world.h"
#include <stddef.h>

/* (cache_files.c) the loaded tags' table and count, for the weather tags
found by name (tag_groups.h's layout, declared here: its guards are the
release build's) */
struct world_tag_instance
{
	long group_tag;
	long parent_group_tags[2];
	long tag_index;
	char *name;
	void *base_address;
	unsigned long unused[2];
};
extern struct cache_file_tag_instance *global_tag_instances;
extern long halo_loaded_tag_count;
void h2_effects_weather_tags_set(long count, long const *tag_indices);
#include "rasterizer/rasterizer_geometry.h"
#include "bitmaps/bitmap_group.h"
#include "structures/structure_bsp_definitions.h"
extern void *global_d3d_device;
#include "scenario/scenario.h"

void texture_cache_hardware_format_make(struct bitmap_data *bitmap, void *texture);
/* (port/linux/src/xbox_memory.c) a block outside the tag cache the GPU reads
where it is, for a world's geometry: NULL none */
unsigned char *halo_world_geometry_acquire(unsigned long bytes);
void halo_world_geometry_release(void *address);
int halo_world_geometry_holds(const void *address, unsigned long size);
unsigned long texture_cache_hardware_format_size(void);
void *platform_contiguous_side_alloc(unsigned long size, unsigned long alignment);

static struct
{
	boolean loaded;
	long group_count;
	struct h2_world_group *groups;
	short atlas_count;
	struct bitmap_data atlas_bitmaps[MAXIMUM_ATLASES];
	void *atlas_texels[MAXIMUM_ATLASES];
	unsigned char *atlas_hardware;
	struct vertex_buffer *buffers;
	/* the structure BSP's collision BSP read from the map (this engine's
	layout, with its own addresses: an element of the BSP's collision_bsp
	block), and that block's address as the level had it */
	unsigned char *collision_memory;
	void *collision_element;
	void *level_collision_address;
	boolean collision_installed;
	boolean collision_compared;
	/* (a level with no world of its own) the BSP's room in the tag cache
	the world is read into: its size, what was used, whether the GPU reads
	it where it is (the contiguous window's) */
	unsigned long room_size, room_used;
	boolean room_contiguous;
	/* (a level made with its world's geometry apart, world_room_carve) the
	materials' buffers' block outside the tag cache, and its bytes */
	unsigned char *apart;
	unsigned long apart_size;
	/* the read's cost: its passes, microseconds */
	unsigned long long us_collision, us_count, us_fill, us_take, us_total;
	/* (each pass's - [0] the count, [1] the fill - sections read, lightmap
	pages placed, geometry added, decorators: the read's line) */
	unsigned long long us_parts[2][4];
} h2_world;

/* (h2_world_snapshot.inc) a BSP's world kept as a file and read back */
static void world_buffers_make(void);
static boolean world_snapshot_restore(char const *maps_directory, char const *name, long bsp, unsigned char *room, unsigned long room_size);
static void world_snapshot_write_begin(char const *maps_directory, char const *name, long bsp, unsigned char *room, unsigned long room_size);
static void world_snapshot_write_finish(void);

unsigned long long vita_host_time_us(void);
void platform_memory_log(const char *when, const char *map_name);
int halo_d3d_index_buffer_place(void *buffer, void *indices);
void halo_d3d_buffer_in_place(const void *buffer, int in_place);

/* (world_gather_begin) the BSP's lightmap found: its clusters' and
instances' pages read */
static boolean world_lightmaps_begun;

/* (the passes) the map's chunks let go between sections once they hold
this much - the Vita's C heap, when it is nearly full (a switch during
play); a section's tags are read again when touched (its neighbours'
chunk, mostly: Old Mombasa's count went from 0.4 to 1.1 s on Vita3K
trimming at 1 MB throughout) */
enum
{
	WORLD_SEGMENTS_HELD_TIGHT = 1024 * 1024,
	WORLD_SEGMENTS_HELD_SMALL = 2 * 1024 * 1024,
	WORLD_SEGMENTS_HELD = 6 * 1024 * 1024,
	WORLD_HEAP_TIGHT = 14 * 1024 * 1024,
	WORLD_HEAP_SMALL = 64 * 1024 * 1024
};

void platform_heap_usage(unsigned long *in_use, unsigned long *capacity);

/* (a small C heap - the Vita's 48 MB - holds 2 MB of them; one nearly
full, 1 MB; the PC's, 6 MB) */
static void world_segments_trim(struct h2_cache *cache)
{
	unsigned long in_use = 0, capacity = 0, held = WORLD_SEGMENTS_HELD;
	static int no_trim = -1;

	/* (HALO_H2_WORLD_NO_TRIM=1, a test: the chunks kept through the read) */
	if (no_trim < 0)
		no_trim = getenv("HALO_H2_WORLD_NO_TRIM") && atoi(getenv("HALO_H2_WORLD_NO_TRIM"));
	if (no_trim)
		return;
	platform_heap_usage(&in_use, &capacity);
	if (capacity && capacity <= WORLD_HEAP_SMALL)
		held = WORLD_SEGMENTS_HELD_SMALL;
	if (capacity > in_use && capacity - in_use < WORLD_HEAP_TIGHT)
		held = WORLD_SEGMENTS_HELD_TIGHT;
	h2_cache_trim_past(cache, held);
}

/* the shaders a BSP's world may name (the build made for the read has
room for these alone: struct build's shaders are last) */
enum
{
	WORLD_MAXIMUM_SHADERS = 256
};

/* the C heap at a step of the read, and the map's chunks inflated so far
(HALO_H2_DEBUG, or always on the Vita: its C heap is the read's limit) */
static void world_memory_mark(struct build *build, char const *step)
{
	unsigned long inflated = 0, evicted = 0;
	char when[96];

	/* (the load's step, and a frame of the loading screen: h2_level.h) */
	snprintf(when, sizeof(when), "world: %s", step);
	h2_load_step(when);
#ifndef HALO_VITA
	if (!getenv("HALO_H2_DEBUG"))
		return;
#endif
	h2_cache_segment_counts(build->cache, &inflated, &evicted);
	snprintf(when, sizeof(when), "h2 world: %s (%lu chunks inflated)", step, inflated);
	platform_memory_log(when, h2_cache_name(build->cache));
}

/* (each lightmap page's block, kept from world to world: h2_world is
cleared at each read. A page of ATLAS_POOL_SLACK more than its texels: the
texels start a page further every other use of the block, so that the
atlas is a texture at a new address each time the BSP changes - at the same
address Vita3K drew the last BSP's atlas, the picture it had of it, black
or the other BSP's light, on Old Mombasa's walls) */
static void *world_atlas_pool[MAXIMUM_ATLASES];
static unsigned long world_atlas_pool_bytes[MAXIMUM_ATLASES];
static unsigned long world_atlas_pool_turn[MAXIMUM_ATLASES];

enum
{
	ATLAS_POOL_SLACK = 4096
};

/* the structure BSP's collision_bsp block pointed at the collision read from
the map (install) or back at the level's own; the first time, each of the
eight blocks compared with the level's (HALO_H2_DEBUG logs which differ) */
static void world_collision_install(boolean install)
{
	extern short global_structure_bsp_index;
	struct structure_bsp *bsp;
	struct tag_block *block;

	if (install == h2_world.collision_installed || !h2_world.collision_element)
	{
		h2_world.collision_installed = install && h2_world.collision_element;
		return;
	}
	bsp = global_structure_bsp_get();
	if (!bsp)
		return;
	block = &bsp->collision_bsp;
	if (install)
	{
		if (!h2_world.collision_compared && block->count == 1 && block->address)
		{
			static unsigned long const sizes[8] = { 12, 16, 8, 8, 20, 12, 24, 16 };
			struct tag_block const *mine = (struct tag_block const *)h2_world.collision_element;
			struct tag_block const *level = (struct tag_block const *)block->address;
			int k, same = 0;

			for (k = 0; k < 8; k++)
				same += mine[k].count == level[k].count &&
					(!mine[k].count || !memcmp(mine[k].address, level[k].address, (unsigned long)mine[k].count * sizes[k]));
			platform_log("h2 world: the collision BSP read from the map: %d of its 8 blocks the level's to the byte "
				"(%ld surfaces, %ld nodes; the level's %ld, %ld)", same, mine[5].count, mine[0].count, level[5].count, level[0].count);
			h2_world.collision_compared = TRUE;
			{
				long g, triangles = 0;

				for (g = 0; g < h2_world.group_count; g++)
					triangles += h2_world.groups[g].triangle_count;
				platform_log("h2 world: %ld triangles in %ld runs read; the level's BSP %ld surfaces in %ld lightmaps", triangles,
					h2_world.group_count, bsp->surfaces.count, bsp->lightmaps.count);
			}
		}
		h2_world.level_collision_address = block->address;
		block->address = h2_world.collision_element;
	}
	else if (h2_world.collision_element && block->address == h2_world.collision_element)
		block->address = h2_world.level_collision_address;
	h2_world.collision_installed = install;
}

/* (a level built with HALO_H2_WORLD_FROM_MAP: its structure BSP without
lightmaps or collision - its world the map's alone, read whatever the
settings say) */
static boolean world_required;

boolean h2_world_enabled(void)
{
	char const *setting = getenv("HALO_H2_NATIVE_BSP");

	return world_required || (setting && atoi(setting) != 0);
}

boolean h2_world_required(void)
{
	return world_required;
}

static void world_follow_level(void);

/* (read again when the settings change: the tests' @set turns the world
read from the map off and on in one run, its data kept) */
boolean h2_world_active(void)
{
	extern volatile unsigned long halo_settings_generation;
	static unsigned long settings_seen = (unsigned long)-1;
	static boolean drawn, required_seen;

	/* (and when a level with no world of its own loads after one with:
	the menu's backdrop asked first, with nothing required, and a campaign
	level's world went undrawn on the stale answer) */
	if (settings_seen != halo_settings_generation || required_seen != world_required)
	{
		settings_seen = halo_settings_generation;
		required_seen = world_required;
		drawn = h2_world_enabled();
	}
	if (drawn)
		world_follow_level();
	world_collision_install(drawn && h2_world.loaded);
	return drawn && h2_world.loaded && h2_world.group_count > 0;
}

long h2_world_group_count(void)
{
	return h2_world.group_count;
}

struct h2_world_group *h2_world_group_get(long index)
{
	return index >= 0 && index < h2_world.group_count ? &h2_world.groups[index] : NULL;
}

/* (h2_world_triangles_near) each run's triangles' boxes, WORLD_BOX_TRIANGLES
of its (cluster-sorted) triangles a box, made as the world is read and let go
with it (world_boxes_forget): a decal's ask tests the boxes of the ranges
about it rather than every triangle of them - a range is a cluster's 256
triangles, its sphere as wide as the cluster. Each box 16-bit steps of the
world's own box, rounded outward (12 bytes). In user RAM outside the C heap
on the Vita (a block of its own) */
enum
{
	WORLD_BOX_TRIANGLES = 32
};

static struct
{
	boolean made, failed;
	long count;
	long *first;              /* each run's first box */
	unsigned short (*boxes)[6]; /* lo x y z, hi x y z, in steps of the world's box */
	float origin[3], step[3];
	void *memory;
	unsigned long bytes;
	int uid;
} world_boxes;

#ifdef HALO_VITA
void *vita_host_block_alloc(const char *name, unsigned long size, int *uid);
void vita_host_block_free(int uid);
#endif

static void world_boxes_forget(void)
{
	if (world_boxes.memory)
	{
#ifdef HALO_VITA
		vita_host_block_free(world_boxes.uid);
#else
		free(world_boxes.memory);
#endif
	}
	memset(&world_boxes, 0, sizeof(world_boxes));
}

/* the run's buffers' vertex positions and triangles, read where they are */
static boolean world_run_data(struct h2_world_group const *group, BYTE **vertices, BYTE **indices)
{
	*vertices = *indices = NULL;
	if (!group->index_buffer || !group->buffers || !group->buffers[0].hardware_format)
		return FALSE;
	IDirect3DVertexBuffer8_Lock((D3DVertexBuffer *)group->buffers[0].hardware_format, 0, 0, vertices, D3DLOCK_READONLY);
	IDirect3DIndexBuffer8_Lock((D3DIndexBuffer *)group->index_buffer, 0, 0, indices, D3DLOCK_READONLY);
	return *vertices && *indices;
}

static boolean world_boxes_make(void)
{
	long g, total = 0, b = 0, t;
	unsigned long bytes;
	float lo[3] = { 1.0e30f, 1.0e30f, 1.0e30f }, hi[3] = { -1.0e30f, -1.0e30f, -1.0e30f };
	int k, c;

	if (world_boxes.made || world_boxes.failed)
		return world_boxes.made;
	/* (the world's box, for the steps) */
	for (g = 0; g < h2_world.group_count; g++)
	{
		struct h2_world_group const *group = &h2_world.groups[g];
		BYTE *vertices, *indices;

		total += (group->triangle_count + WORLD_BOX_TRIANGLES - 1) / WORLD_BOX_TRIANGLES;
		if (!world_run_data(group, &vertices, &indices))
			continue;
		for (t = 0; t < group->vertex_count; t++)
		{
			float const *p = (float const *)(vertices + 32 * (unsigned long)t);

			for (k = 0; k < 3; k++)
			{
				if (p[k] < lo[k])
					lo[k] = p[k];
				if (p[k] > hi[k])
					hi[k] = p[k];
			}
		}
	}
	if (lo[0] > hi[0])
	{
		world_boxes.failed = TRUE;
		return FALSE;
	}
	for (k = 0; k < 3; k++)
	{
		world_boxes.origin[k] = lo[k];
		world_boxes.step[k] = (hi[k] - lo[k]) / 65000.0f + 1.0e-6f;
	}
	bytes = (unsigned long)(h2_world.group_count + 1) * sizeof(long) + (unsigned long)total * sizeof(unsigned short[6]);
#ifdef HALO_VITA
	/* (a memory block's size: whole 4 KB pages) */
	world_boxes.bytes = (bytes + 4095) & ~4095UL;
	world_boxes.memory = vita_host_block_alloc("h2 world boxes", world_boxes.bytes, &world_boxes.uid);
#else
	world_boxes.bytes = bytes;
	world_boxes.memory = malloc(bytes);
#endif
	if (!world_boxes.memory)
	{
		world_boxes.failed = TRUE;
		return FALSE;
	}
	world_boxes.first = (long *)world_boxes.memory;
	world_boxes.boxes = (unsigned short (*)[6])((long *)world_boxes.memory + h2_world.group_count + 1);
	for (g = 0; g < h2_world.group_count; g++)
	{
		struct h2_world_group const *group = &h2_world.groups[g];
		BYTE *vertices, *indices;
		boolean data = world_run_data(group, &vertices, &indices);
		float box[6];

		world_boxes.first[g] = b;
		for (t = 0; t < group->triangle_count; t++)
		{
			if (t % WORLD_BOX_TRIANGLES == 0)
			{
				box[0] = box[1] = box[2] = 1.0e30f;
				box[3] = box[4] = box[5] = -1.0e30f;
			}
			for (c = 0; data && c < 3; c++)
			{
				unsigned short index = ((unsigned short const *)indices)[3 * t + c];
				float const *p;

				if (index >= group->vertex_count)
					continue;
				p = (float const *)(vertices + 32 * (unsigned long)index);
				for (k = 0; k < 3; k++)
				{
					if (p[k] < box[k])
						box[k] = p[k];
					if (p[k] > box[3 + k])
						box[3 + k] = p[k];
				}
			}
			if (t % WORLD_BOX_TRIANGLES == WORLD_BOX_TRIANGLES - 1 || t == group->triangle_count - 1)
			{
				unsigned short *out = world_boxes.boxes[b + t / WORLD_BOX_TRIANGLES];

				for (k = 0; k < 3; k++)
				{
					/* (an empty box: lo past hi; else rounded outward) */
					float l = box[k] > box[3 + k] ? 65535.0f : floorf((box[k] - world_boxes.origin[k]) / world_boxes.step[k]);
					float h = box[k] > box[3 + k] ? 0.0f : ceilf((box[3 + k] - world_boxes.origin[k]) / world_boxes.step[k]);

					out[k] = (unsigned short)(l < 0.0f ? 0.0f : l > 65535.0f ? 65535.0f : l);
					out[3 + k] = (unsigned short)(h < 0.0f ? 0.0f : h > 65535.0f ? 65535.0f : h);
				}
			}
		}
		b += (group->triangle_count + WORLD_BOX_TRIANGLES - 1) / WORLD_BOX_TRIANGLES;
	}
	world_boxes.first[h2_world.group_count] = b;
	world_boxes.count = total;
	world_boxes.made = TRUE;
	platform_log("h2 world: the decals' boxes of its triangles made: %ld boxes of %d triangles, %lu bytes", total, WORLD_BOX_TRIANGLES,
		world_boxes.bytes);
	return TRUE;
}

/* (decals.c's gap check: every triangle of the ranges, the boxes not asked) */
int h2_world_boxes_bypass;

long h2_world_triangles_near(float const center[3], float radius,
	void (*each)(void *context, float const *a, float const *b, float const *c), void *context)
{
	boolean breakable_surface_extant(short breakable_surface_index);
	long g, r, t, given = 0, query[6];
	boolean boxed;
	static int boxes_off = -1;

	if (!h2_world.loaded)
		return 0;
	/* (HALO_H2_WORLD_BOXES=0: every triangle of the ranges about it given) */
	if (boxes_off < 0)
		boxes_off = getenv("HALO_H2_WORLD_BOXES") && atoi(getenv("HALO_H2_WORLD_BOXES")) == 0;
	boxed = !boxes_off && !h2_world_boxes_bypass && world_boxes_make();
	/* (the asked box in the boxes' steps, rounded outward) */
	if (boxed)
	{
		int k;

		for (k = 0; k < 3; k++)
		{
			float l = floorf((center[k] - radius - world_boxes.origin[k]) / world_boxes.step[k]);
			float h = ceilf((center[k] + radius - world_boxes.origin[k]) / world_boxes.step[k]);

			query[k] = l < 0.0f ? 0L : l > 65535.0f ? 65535L : (long)l;
			query[3 + k] = h < 0.0f ? -1L : h > 65535.0f ? 65535L : (long)h;
		}
	}
	for (g = 0; g < h2_world.group_count; g++)
	{
		struct h2_world_group *group = &h2_world.groups[g];
		BYTE *vertices = NULL, *indices = NULL;

		/* (an environment shader's run - the engine's shader of its name
		an senv - takes decals; water's, a transparent or overlay one's do
		not: looked up once) */
		if (!group->takes_decals)
			group->takes_decals = tag_loaded('senv', group->path) != NONE ? 1 : -1;
		/* (a run not drawn, or a broken pane's: as structure_render.c; or
		one no decal lies on - the water's surface over a shallow bank, a
		decorator's blade: a decal's piece moved onto those floated over the
		ground under them, drawn twice, or streaked a blade) */
		if (group->shader_tag == -2 || group->takes_decals < 0 || !group->ranges || !group->index_buffer || !group->buffers ||
			!group->buffers[0].hardware_format ||
			(group->breakable_surface_index != NONE && !breakable_surface_extant(group->breakable_surface_index)))
		{
			continue;
		}
		for (r = 0; r < group->range_count; r++)
		{
			struct h2_world_range const *range = &group->ranges[r];
			float dx = range->center[0] - center[0], dy = range->center[1] - center[1], dz = range->center[2] - center[2];
			float reach = range->radius + radius;
			long end = range->first + range->count < group->triangle_count ? range->first + range->count : group->triangle_count;

			if (dx * dx + dy * dy + dz * dz > reach * reach)
				continue;
			for (t = range->first; t < end; t++)
			{
				unsigned short const *triangle;

				/* (a box of triangles out of reach: past it) */
				if (boxed && (t == range->first || t % WORLD_BOX_TRIANGLES == 0))
				{
					unsigned short const *box = world_boxes.boxes[world_boxes.first[g] + t / WORLD_BOX_TRIANGLES];

					if (box[0] > query[3] || box[3] < query[0] || box[1] > query[4] || box[4] < query[1] || box[2] > query[5] ||
						box[5] < query[2])
					{
						t = (t / WORLD_BOX_TRIANGLES + 1) * WORLD_BOX_TRIANGLES - 1;
						continue;
					}
				}
				if (!vertices)
				{
					IDirect3DVertexBuffer8_Lock((D3DVertexBuffer *)group->buffers[0].hardware_format, 0, 0, &vertices, D3DLOCK_READONLY);
					IDirect3DIndexBuffer8_Lock((D3DIndexBuffer *)group->index_buffer, 0, 0, &indices, D3DLOCK_READONLY);
					if (!vertices || !indices)
						break;
				}
				triangle = (unsigned short const *)indices + 3 * t;
				if (triangle[0] >= group->vertex_count || triangle[1] >= group->vertex_count || triangle[2] >= group->vertex_count)
					continue;
				/* (a compressed environment vertex: its position first) */
				each(context, (float const *)(vertices + 32 * (unsigned long)triangle[0]),
					(float const *)(vertices + 32 * (unsigned long)triangle[1]),
					(float const *)(vertices + 32 * (unsigned long)triangle[2]));
				given++;
			}
		}
	}
	return given;
}

/* (object_lights.c, h2/render-accuracy) the highest of the world's solid
render triangles under point, within depth below it - Halo 2's object light
ray's hit (its structure's and its instances' surfaces alike: the
decompilation's h2_draw_lighting_ray_query, kinds 1 and 3) -: its height,
lightmap atlas, corners' vertices (compressed environment vertices, 32 bytes)
and lightmap vertices (8), the point's place in it (s, t: point = v0 + s (v1
- v0) + t (v2 - v0)) and its shader. A level whose world is read from the
map has no structure surfaces for the engine's own ray (its BSP a stub):
without this every object took the BSP's default light */
boolean h2_world_surface_below(float const point[3], float depth, float *height, short *atlas, void const *vertex[3],
	void const *lightmap_vertex[3], float *s_out, float *t_out, long *shader_tag)
{
	boolean breakable_surface_extant(short breakable_surface_index);
	boolean found = FALSE;
	float best = point[2] - depth;
	long g, r, t;

	if (!h2_world.loaded)
		return FALSE;
	for (g = 0; g < h2_world.group_count; g++)
	{
		struct h2_world_group *group = &h2_world.groups[g];
		BYTE *vertices = NULL, *indices = NULL, *lightmaps = NULL;

		/* (an environment shader's run - not water, a transparent or overlay
		one, a decorator's: what the ray's collision is) */
		if (!group->takes_decals)
			group->takes_decals = tag_loaded('senv', group->path) != NONE ? 1 : -1;
		if (group->shader_tag == -2 || group->takes_decals < 0 || group->atlas < 0 || !group->ranges || !group->index_buffer ||
			!group->buffers || !group->buffers[0].hardware_format || !group->buffers[1].hardware_format ||
			(group->breakable_surface_index != NONE && !breakable_surface_extant(group->breakable_surface_index)))
		{
			continue;
		}
		for (r = 0; r < group->range_count; r++)
		{
			struct h2_world_range const *range = &group->ranges[r];
			float dx = range->center[0] - point[0], dy = range->center[1] - point[1];
			long end = range->first + range->count < group->triangle_count ? range->first + range->count : group->triangle_count;

			if (dx * dx + dy * dy > range->radius * range->radius || range->center[2] - range->radius > point[2] ||
				range->center[2] + range->radius < best)
			{
				continue;
			}
			for (t = range->first; t < end; t++)
			{
				unsigned short const *triangle;
				float const *p0, *p1, *p2;
				float e1[3], e2[3], nz, z, det, u, v, px, py;

				if (!vertices)
				{
					IDirect3DVertexBuffer8_Lock((D3DVertexBuffer *)group->buffers[0].hardware_format, 0, 0, &vertices, D3DLOCK_READONLY);
					IDirect3DVertexBuffer8_Lock((D3DVertexBuffer *)group->buffers[1].hardware_format, 0, 0, &lightmaps, D3DLOCK_READONLY);
					IDirect3DIndexBuffer8_Lock((D3DIndexBuffer *)group->index_buffer, 0, 0, &indices, D3DLOCK_READONLY);
					if (!vertices || !indices || !lightmaps)
						break;
				}
				triangle = (unsigned short const *)indices + 3 * t;
				if (triangle[0] >= group->vertex_count || triangle[1] >= group->vertex_count || triangle[2] >= group->vertex_count)
					continue;
				p0 = (float const *)(vertices + 32 * (unsigned long)triangle[0]);
				p1 = (float const *)(vertices + 32 * (unsigned long)triangle[1]);
				p2 = (float const *)(vertices + 32 * (unsigned long)triangle[2]);
				e1[0] = p1[0] - p0[0], e1[1] = p1[1] - p0[1], e1[2] = p1[2] - p0[2];
				e2[0] = p2[0] - p0[0], e2[1] = p2[1] - p0[1], e2[2] = p2[2] - p0[2];
				det = e1[0] * e2[1] - e1[1] * e2[0];
				nz = det;
				if (fabsf(nz) < 1e-6f)
					continue;
				px = point[0] - p0[0];
				py = point[1] - p0[1];
				u = (px * e2[1] - py * e2[0]) / det;
				v = (e1[0] * py - e1[1] * px) / det;
				if (u < -1e-4f || v < -1e-4f || u + v > 1.0001f)
					continue;
				z = p0[2] + u * e1[2] + v * e2[2];
				if (z <= best || z > point[2] + 0.01f)
					continue;
				best = z;
				found = TRUE;
				*height = z;
				*atlas = group->atlas;
				vertex[0] = p0, vertex[1] = p1, vertex[2] = p2;
				lightmap_vertex[0] = lightmaps + 8 * (unsigned long)triangle[0];
				lightmap_vertex[1] = lightmaps + 8 * (unsigned long)triangle[1];
				lightmap_vertex[2] = lightmaps + 8 * (unsigned long)triangle[2];
				*s_out = u;
				*t_out = v;
				*shader_tag = group->shader_tag;
			}
		}
	}
	return found;
}

/* (object_lights.c) an atlas's texel at u, v (0..1) as 0..1 rgb - its
stored value (a Halo 2 level's texel holds half the light, h2_lighting.h):
the atlas's swizzled 32-bit or r5g6b5 texels; FALSE none */
boolean h2_world_atlas_texel(short atlas, float u, float v, float rgb[3])
{
	struct bitmap_data const *bitmap;
	unsigned char const *texels;
	long x, y;

	if (atlas < 0 || atlas >= h2_world.atlas_count || !(texels = h2_world.atlas_texels[atlas]))
		return FALSE;
	bitmap = &h2_world.atlas_bitmaps[atlas];
	u -= floorf(u), v -= floorf(v);
	x = (long)(u * bitmap->width), y = (long)(v * bitmap->height);
	x = x < 0 ? 0 : x >= bitmap->width ? bitmap->width - 1 : x;
	y = y < 0 ? 0 : y >= bitmap->height ? bitmap->height - 1 : y;
	if (bitmap->format == 11)
	{
		unsigned long c = read32(texels + 4 * morton((unsigned long)x, (unsigned long)y));

		rgb[0] = (float)((c >> 16) & 255) / 255.0f, rgb[1] = (float)((c >> 8) & 255) / 255.0f, rgb[2] = (float)(c & 255) / 255.0f;
	}
	else
	{
		unsigned long c = (unsigned short)read16(texels + 2 * morton((unsigned long)x, (unsigned long)y));

		rgb[0] = (float)((c >> 11) & 31) / 31.0f, rgb[1] = (float)((c >> 5) & 63) / 63.0f, rgb[2] = (float)(c & 31) / 31.0f;
	}
	return TRUE;
}

boolean h2_world_vertex_lit_triangle(short atlas, void const *coordinates)
{
	float const *uv = coordinates;
	struct bitmap_data const *bitmap;
	float x[3], y[3];
	int k;

	if (atlas < 0 || atlas >= h2_world.atlas_count || !h2_world.atlas_texels[atlas])
		return FALSE;
	bitmap = &h2_world.atlas_bitmaps[atlas];
	for (k = 0; k < 3; k++)
	{
		x[k] = uv[2 * k] * bitmap->width - 0.5f;
		y[k] = uv[2 * k + 1] * bitmap->height - 0.5f;
		/* (each at a texel's centre: the 16-bit coordinates' rounding) */
		if (fabsf(x[k] - floorf(x[k] + 0.5f)) > 0.05f || fabsf(y[k] - floorf(y[k] + 0.5f)) > 0.05f)
			return FALSE;
	}
	/* (in any order - the triangles are wound the other way here -: one at
	the block's first texel, one a texel along, one a texel down) */
	{
		float low_x = fminf(x[0], fminf(x[1], x[2])), low_y = fminf(y[0], fminf(y[1], y[2]));
		int seen = 0;

		for (k = 0; k < 3; k++)
		{
			long dx = lroundf(x[k] - low_x), dy = lroundf(y[k] - low_y);

			if (dx == 0 && dy == 0)
				seen |= 1;
			else if (dx == 1 && dy == 0)
				seen |= 2;
			else if (dx == 0 && dy == 1)
				seen |= 4;
			else
				return FALSE;
		}
		return seen == 7;
	}
}

short h2_world_atlas_count(void)
{
	return h2_world.atlas_count;
}

struct bitmap_data *h2_world_atlas_bitmap(short atlas)
{
	return atlas >= 0 && atlas < h2_world.atlas_count && h2_world.atlas_texels[atlas] ? &h2_world.atlas_bitmaps[atlas] : NULL;
}

/* (the builder's shader_index, as little of it as the world needs: the
engine's shader's path, the base map's scale its vertices take) */
H2_ALIGN static short world_shader_index(struct build *build, long tag)
{
	struct h2_cache_tag t;
	struct shader *s;
	long i;

	if (tag == NONE)
		return NONE;
	for (i = 0; i < build->shader_count; i++)
		if (build->shaders[i].h2_tag == tag)
			return (short)i;
	if (build->shader_count >= WORLD_MAXIMUM_SHADERS || !h2_cache_tag_get(build->cache, tag, &t))
		return NONE;
	s = &build->shaders[build->shader_count];
	memset(s, 0, sizeof(*s));
	s->h2_tag = tag;
	s->rendered = TRUE;
	snprintf(s->path, sizeof(s->path), "h2\\shaders\\%s", tag_short_name(h2_cache_tag_name(build->cache, tag)));
	{
		long template_tag = h2_cache_reference(build->cache, t.address);
		char const *template_name = template_tag != NONE ? h2_cache_tag_name(build->cache, template_tag) : "";

		if (h2_material_read(build->cache, t.address, template_name, &s->material) &&
			(fabsf(s->material.base_scale[0] - 1.0f) > 0.01f || fabsf(s->material.base_scale[1] - 1.0f) > 0.01f) &&
			!getenv("HALO_H2_NO_BASE_SCALE"))
		{
			s->base_scale[0] = s->material.base_scale[0];
			s->base_scale[1] = s->material.base_scale[1];
		}
	}
	return (short)build->shader_count++;
}

/* the BSP's materials' shaders and its lightmap, before the geometry */
H2_ALIGN static boolean world_gather_begin(struct build *build)
{
	struct h2_cache *cache = build->cache;
	unsigned long elements;
	long count, i;

	if (!h2_cache_block(cache, build->sbsp + H2_SBSP_MATERIALS, H2_MATERIAL_SIZE, &count, &elements))
		return FALSE;
	build->material_count = count;
	build->material_shader = malloc((count + 1) * sizeof(short));
	if (!build->material_shader)
		return FALSE;
	for (i = 0; i < count; i++)
	{
		long shader = h2_cache_reference(cache, elements + H2_MATERIAL_SIZE * i + 8);

		if (shader == NONE)
			shader = h2_cache_reference(cache, elements + H2_MATERIAL_SIZE * i);
		build->material_shader[i] = world_shader_index(build, shader);
	}
	world_lightmaps_begun = lightmaps_begin(build);
	if (!h2_cache_block(cache, build->sbsp + H2_SBSP_CLUSTERS, H2_CLUSTER_SIZE, &build->cluster_count, &elements) ||
		build->cluster_count > 512)
	{
		return FALSE;
	}
	return TRUE;
}

/* the BSP's clusters and instances laid out as level_gather lays them out
(a pass: the count of each material's vertices and triangles when
build->world_counting, else the geometry itself) */
/* (the passes' parts timed: h2_world.us_parts) */
#define WORLD_PART_TIMED(part, statement) do { unsigned long long part_from_ = vita_host_time_us(); statement; \
	h2_world.us_parts[build->world_counting ? 0 : 1][part] += vita_host_time_us() - part_from_; } while (0)

H2_ALIGN static boolean world_gather_pass(struct build *build)
{
	struct h2_cache *cache = build->cache;
	unsigned long clusters, definitions, instances, cluster_info = 0, instance_info = 0;
	long i, definition_count, instance_count, cluster_info_count = 0, instance_info_count = 0;
	struct section *definition_sections;
	boolean *definition_read;
	long *definition_last;

	if (world_lightmaps_begun)
	{
		h2_cache_block(cache, build->lightmap_group + 0x28, 4, &cluster_info_count, &cluster_info);
		h2_cache_block(cache, build->lightmap_group + 0x48, 4, &instance_info_count, &instance_info);
	}
	if (!h2_cache_block(cache, build->sbsp + H2_SBSP_CLUSTERS, H2_CLUSTER_SIZE, &build->cluster_count, &clusters))
		return FALSE;
	build->world_trouble = NULL;
	/* (the first pass: the lightmap pages in the order the clusters and
	the instances ask for them - page_tile_take decodes the next few of
	them with each, on the worker threads) */
	free(build->page_order);
	build->page_order = NULL;
	build->page_order_count = build->page_order_next = 0;
	if (build->world_counting && world_lightmaps_begun && cluster_info_count + instance_info_count > 0)
	{
		long n = 0;

		build->page_order = malloc((size_t)(cluster_info_count + instance_info_count) * 2 * sizeof(long));
		for (i = 0; build->page_order && i < cluster_info_count + instance_info_count; i++)
		{
			unsigned char const *info = i < cluster_info_count ? h2_cache_at(cache, cluster_info + 4 * i, 4) :
				h2_cache_at(cache, instance_info + 4 * (i - cluster_info_count), 4);

			if (info && read16(info) >= 0)
			{
				build->page_order[2 * n] = read16(info);
				build->page_order[2 * n + 1] = (signed char)info[2] < 0 ? 0 : (signed char)info[2];
				n++;
			}
		}
		build->page_order_count = build->page_order ? n : 0;
	}
	for (i = 0; i < build->cluster_count && !build->result->why[0]; i++)
	{
		struct section s;
		struct placed_page *page = page_neutral(build);

		world_segments_trim(cache);
		if (i < cluster_info_count)
		{
			unsigned char const *info = h2_cache_at(cache, cluster_info + 4 * i, 4);

			WORLD_PART_TIMED(1, page = page_place(build, read16(info), (signed char)info[2]));
		}
		{
			boolean read;

			WORLD_PART_TIMED(0, read = section_read(build, clusters + H2_CLUSTER_SIZE * i, &s));
			if (read)
			{
				/* (geometry_add: a cluster's triangles on a breakable pane their
				own material - a run not drawn once the pane is broken) */
				WORLD_PART_TIMED(2, geometry_add(build, &s, (short)i, NULL, page));
				section_free(&s);
			}
		}
	}
	if (!h2_cache_block(cache, build->sbsp + H2_SBSP_INSTANCE_DEFINITIONS, H2_INSTANCE_DEFINITION_SIZE,
			&definition_count, &definitions) ||
		!h2_cache_block(cache, build->sbsp + H2_SBSP_INSTANCES, H2_INSTANCE_SIZE, &instance_count, &instances))
	{
		definition_count = instance_count = 0;
	}
	definition_sections = calloc(definition_count + 1, sizeof(*definition_sections));
	definition_read = calloc(definition_count + 1, sizeof(*definition_read));
	/* (each definition's last instance: its section let go after it, not
	held with every other's to the end - the C heap, on the Vita) */
	definition_last = malloc((definition_count + 1) * sizeof(*definition_last));
	for (i = 0; definition_last && i < definition_count; i++)
		definition_last[i] = -1;
	for (i = 0; definition_last && i < instance_count; i++)
	{
		unsigned char const *instance = h2_cache_at(cache, instances + H2_INSTANCE_SIZE * i, H2_INSTANCE_SIZE);
		short definition = instance ? read16(instance + 0x34) : -1;

		if (definition >= 0 && definition < definition_count)
			definition_last[definition] = i;
	}
	for (i = 0; i < instance_count && definition_sections && definition_read && !build->result->why[0]; i++)
	{
		unsigned char const *instance;

		world_segments_trim(cache);
		instance = h2_cache_at(cache, instances + H2_INSTANCE_SIZE * i, H2_INSTANCE_SIZE);
		short definition = read16(instance + 0x34);
		float matrix[13];
		struct section *s;
		struct placed_page *page = page_neutral(build);
		int k;
		boolean vertex_lit = FALSE;
		struct section vertex_section;
		long *vertex_corner = NULL;

		if (definition < 0 || definition >= definition_count)
			continue;
		s = &definition_sections[definition];
		if (!definition_read[definition])
		{
			definition_read[definition] = TRUE;
			WORLD_PART_TIMED(0, section_read(build, definitions + H2_INSTANCE_DEFINITION_SIZE * definition, s));
		}
		if (!s->vertex_count)
			continue;
		for (k = 0; k < 12; k++)
			matrix[k] = readf(instance + 4 + 4 * k);
		matrix[12] = readf(instance);
		if (i < instance_info_count)
		{
			unsigned char const *info = h2_cache_at(cache, instance_info + 4 * i, 4);

			WORLD_PART_TIMED(1, page = page_place(build, read16(info), (signed char)info[2]));
		}
		/* (h2/render-accuracy: no page of its own - Halo 2 lights it per
		vertex, lightmap_pervertex: its bucket's baked colours. Here their
		mean, one patch an instance (quantized to 8 levels a channel step,
		page_of_cluster_light's 256 patches) - a visual approximation of
		Halo 2's per-vertex gradient. The neutral grey patch it had lit
		05a's hanging vines (131 such instances on bsp 1, 712 over the
		level) at half the light's top, pale in the shade.
		HALO_H2_INSTANCE_BAKED_OLD=1 the grey; the desktop's alone - the
		Vita and its null build keep the grey, HALO_H2_INSTANCE_BAKED=1 there) */
#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
		if (page->page < 0 && getenv("HALO_H2_INSTANCE_BAKED") && atoi(getenv("HALO_H2_INSTANCE_BAKED")) == 1)
#else
		if (page->page < 0 && !getenv("HALO_H2_INSTANCE_BAKED_OLD"))
#endif
		{
			float baked[3];

			/* (its colours at its vertices, interpolated, as Halo 2 draws
			them: section_vertex_lit's own patch and its triangles' own
			vertices - the mean's one patch lit a long instance's dark end
			as its lit one: 03b's tunnel strip under the Scorpion, Cairo's
			railings. HALO_H2_INSTANCE_VERTEX_OLD=1 the mean) */
			if (!HALO_ENV_ONCE("HALO_H2_INSTANCE_VERTEX_OLD"))
			{
				unsigned long *colors = h2_lighting_instance_baked_colors(cache, build->lightmap_group, i, s->vertex_count);

				if (colors)
				{
					vertex_lit = section_vertex_lit(build, s, colors, i, &vertex_section, &page, &vertex_corner);
					free(colors);
					if (HALO_ENV_ONCE("HALO_H2_INSTANCE_BAKED_DEBUG") && !build->world_counting)
						platform_log("h2 world: instance %ld at %.1f %.1f %.1f per vertex: %ld vertices, %s (atlas %d at %ld %ld, %ld x %ld)", i,
							matrix[9], matrix[10], matrix[11], s->vertex_count, vertex_lit ? "own patch" : "not made", vertex_lit ? (int)page->atlas : -1,
							vertex_lit ? page->x : 0, vertex_lit ? page->y : 0, vertex_lit ? page->w : 0, vertex_lit ? page->h : 0);
				}
				else if (HALO_ENV_ONCE("HALO_H2_INSTANCE_BAKED_DEBUG") && !build->world_counting)
				{
					platform_log("h2 world: instance %ld at %.1f %.1f %.1f per vertex: no baked colours", i, matrix[9], matrix[10], matrix[11]);
				}
			}
			if (vertex_lit)
				;
			else if (h2_lighting_instance_baked_mean(cache, build->lightmap_group, i, s->vertex_count, baked))
			{
				unsigned char rgb[3];

				lightmap_texel_write(build, baked, rgb);
				for (k = 0; k < 3; k++)
					rgb[k] = (unsigned char)(rgb[k] & 0xF8);
				page = page_of_cluster_light(build, rgb);
				if (HALO_ENV_ONCE("HALO_H2_INSTANCE_BAKED_DEBUG") && !build->world_counting)
					platform_log("h2 world: instance %ld at %.1f %.1f %.1f per vertex: baked mean %.3f %.3f %.3f -> %d %d %d", i, matrix[9],
						matrix[10], matrix[11], baked[0], baked[1], baked[2], rgb[0], rgb[1], rgb[2]);
			}
		}
		build->instance_incident = page->page >= 0 && !build->world_counting ?
			h2_lighting_instance_incident(cache, build->lightmap_group, i, s->vertex_count) : NULL;
		/* (a per-vertex lit instance's vertices made again, one a corner:
		its incident directions with them) */
		if (vertex_lit && build->instance_incident)
		{
			float *spread = malloc(vertex_section.vertex_count * 3 * sizeof(float));
			long v;

			for (v = 0; spread && v < vertex_section.vertex_count; v++)
				memcpy(spread + 3 * v, build->instance_incident + 3 * vertex_corner[v], 3 * sizeof(float));
			free(build->instance_incident);
			build->instance_incident = spread;
		}
		/* (its own breakable panes split from it, as the builder's
		geometry_add splits them: 03a's windows) */
		build->adding_instance_plus1 = i + 1;
		build->adding_definition = definitions + H2_INSTANCE_DEFINITION_SIZE * definition;
		WORLD_PART_TIMED(2, geometry_add(build, vertex_lit ? &vertex_section : s, NONE, matrix, page));
		build->adding_instance_plus1 = 0;
		free(build->instance_incident);
		build->instance_incident = NULL;
		if (vertex_lit)
		{
			section_free(&vertex_section);
			free(vertex_corner);
		}
		if (definition_last && definition_last[definition] == i)
			section_free(s);
	}
	for (i = 0; definition_sections && i < definition_count; i++)
		section_free(&definition_sections[i]);
	free(definition_sections);
	free(definition_read);
	free(definition_last);
	/* the decorators - the grass, the rubble and the debris (Outskirts'
	BSP 0: 80,000 of its 168,000 triangles), as level_gather adds them */
	if (!build->result->why[0] && !getenv("HALO_H2_WORLD_NO_DECORATORS"))
	{
		h2_cache_trim(cache);
		build->adding_decorators = TRUE;
		WORLD_PART_TIMED(3, decorators_add(build));
		build->adding_decorators = FALSE;
	}
	/* (the sea: its plane, as level_gather adds it after the sky) */
	if (!build->result->why[0] && !getenv("HALO_H2_NO_WATER") && build->world_loading)
	{
		unsigned char const *b = h2_cache_at(cache, build->sbsp + H2_SBSP_WORLD_BOUNDS, 24);
		float bounds[6];

		if (b)
		{
			memcpy(bounds, b, sizeof(bounds));
			water_add(build, build->water_cluster, bounds);
		}
	}
	/* (something skipped for want of memory: the passes would not agree) */
	if (h2_cache_memory_failed(cache) && !build->world_trouble)
		build->world_trouble = "out of memory for the map's tags";
	if (build->world_trouble && !build->result->why[0])
		build_fail(build, build->world_trouble);
	return !build->result->why[0];
}

/* an atlas's texels made (bsp_write's: r5g6b5 dithered, or 32-bit,
swizzled as the Xbox's power-of-two textures are) in the pool's block, its
bitmap over them, and its rgb let go: at world_take, and before - as it
fills (world_atlas_closed: the builder would put it in its spill file and
read it back into 3 MB this heap has no run of by a BSP switch) */
H2_ALIGN static boolean world_atlas_close(struct build *build, short a)
{
	unsigned long texel_bytes = h2_lighting_atlas_32bit() ? 4 : 2;
	{
		struct atlas *atlas = &build->atlases[a];
		struct bitmap_data *bitmap = &h2_world.atlas_bitmaps[a];
		unsigned char *texels;
		long x, y;
		unsigned long atlas_sum = 0;
		unsigned long long close_from, dithered_us, swizzled_us;

		if (!atlas->rgb && atlas->spilled)
		{
			atlas->rgb = malloc((unsigned long)LIGHTMAP_ATLAS * LIGHTMAP_ATLAS * 3);
			if (!atlas->rgb || !spill_get(atlas->rgb, (unsigned long)LIGHTMAP_ATLAS * LIGHTMAP_ATLAS * 3, atlas->spill))
				return platform_log("h2 world: atlas %d not back from the spill file", (int)a), FALSE;
			atlas->spilled = FALSE;
		}
		if (!atlas->rgb)
			return TRUE;
		/* (the last world's block for this page again: side blocks are not
		given back, so each one is kept for the next BSP's - Outskirts' fifth
		switch found none left) */
		{
			unsigned long block_bytes = (unsigned long)LIGHTMAP_ATLAS * LIGHTMAP_ATLAS * texel_bytes + ATLAS_POOL_SLACK;
			unsigned char *block = world_atlas_pool_bytes[a] == block_bytes && !getenv("HALO_H2_WORLD_ATLAS_FRESH") ?
				world_atlas_pool[a] : NULL;

			if (block)
				world_atlas_pool_turn[a]++;
			else
			{
				block = platform_contiguous_side_alloc(block_bytes, 4096);
				/* (no side blocks - the desktop's fixed window -: the window's own) */
				if (!block)
					block = window_alloc(block_bytes);
				if (block)
				{
					/* (a block of another size: kept, not reused - a tier change) */
					world_atlas_pool[a] = block;
					world_atlas_pool_bytes[a] = block_bytes;
					world_atlas_pool_turn[a] = 0;
				}
			}
			texels = block ? block + (world_atlas_pool_turn[a] & 1) * ATLAS_POOL_SLACK : NULL;
		}
		if (!texels)
			return platform_log("h2 world: atlas %d: no side block of %lu KB", (int)a,
				(unsigned long)LIGHTMAP_ATLAS * LIGHTMAP_ATLAS * texel_bytes / 1024), FALSE;
		/* (the atlas's bytes summed: a read's texels compared with another's -
		decoded one by one or on the worker threads - by the log alone) */
		{
			unsigned long sum = 0, n = (unsigned long)LIGHTMAP_ATLAS * LIGHTMAP_ATLAS * 3, q;

			for (q = 0; q < n; q++)
				sum += atlas->rgb[q];
			atlas_sum = sum;
		}
		/* (bsp_write's: r5g6b5 dithered, or 32-bit, swizzled as the Xbox's
		power-of-two textures are) */
		close_from = vita_host_time_us();
		if (texel_bytes == 2)
			h2_lighting_atlas_dither_565(atlas->rgb, LIGHTMAP_ATLAS);
		dithered_us = vita_host_time_us() - close_from;
		close_from = vita_host_time_us();
		for (y = 0; y < LIGHTMAP_ATLAS; y++)
		{
			for (x = 0; x < LIGHTMAP_ATLAS; x++)
			{
				unsigned char const *c = atlas->rgb + 3 * (y * LIGHTMAP_ATLAS + x);

				if (texel_bytes == 4)
					write32(texels + 4 * morton(x, y), 0xFF000000UL | (unsigned long)c[0] << 16 | (unsigned long)c[1] << 8 | c[2]);
				else
					write16(texels + 2 * morton(x, y),
						(long)(((unsigned long)(c[0] >> 3) << 11) | ((unsigned long)(c[1] >> 2) << 5) | (c[2] >> 3)));
			}
		}
		{
			/* (and the texels made: their FNV-1a, the swizzle's check) */
			unsigned long made = 2166136261UL, q, n = (unsigned long)LIGHTMAP_ATLAS * LIGHTMAP_ATLAS * texel_bytes;

			swizzled_us = vita_host_time_us() - close_from;
			for (q = 0; q < n; q++)
				made = (made ^ texels[q]) * 16777619UL;
			platform_log("h2 world: atlas %d: its texels sum to %lu, made %08lx (dithered in %lu ms, swizzled in %lu ms)", (int)a,
				atlas_sum, made & 0xFFFFFFFFUL, (unsigned long)(dithered_us / 1000), (unsigned long)(swizzled_us / 1000));
		}
		memset(bitmap, 0, sizeof(*bitmap));
		bitmap->signature = 'bitm';
		bitmap->width = LIGHTMAP_ATLAS;
		bitmap->height = LIGHTMAP_ATLAS;
		bitmap->depth = 1;
		bitmap->type = 0;
		bitmap->format = texel_bytes == 4 ? 11 : 6;
		bitmap->flags = 1;  /* (power of two) */
		bitmap->cache_block_index = NONE;
		bitmap->pixels_size = (long)((unsigned long)LIGHTMAP_ATLAS * LIGHTMAP_ATLAS * texel_bytes);
		bitmap->base_address = texels;
		texture_cache_hardware_format_make(bitmap, h2_world.atlas_hardware + a * texture_cache_hardware_format_size());
		bitmap->hardware_format = h2_world.atlas_hardware + a * texture_cache_hardware_format_size();
		h2_world.atlas_texels[a] = texels;
	}
	{
		struct atlas *atlas = &build->atlases[a];

		window_free(atlas->rgb, (unsigned long)LIGHTMAP_ATLAS * LIGHTMAP_ATLAS * 3);
		atlas->rgb = NULL;
		atlas->spilled = FALSE;
	}
	return TRUE;
}

/* (atlas_new, h2_level.c: the atlas before the new one is full) closed
now; FALSE if it could not be (its rgb stays: the builder's spill) */
H2_ALIGN static boolean world_atlas_closed(struct build *build, short a)
{
	if (!build->world_loading || !h2_world.atlas_hardware || a < 0 || a >= MAXIMUM_ATLASES || h2_world.atlas_texels[a])
		return FALSE;
	return world_atlas_close(build, a);
}

/* the builder's materials and atlases taken as the world's: the vertices
and triangles copied out, the atlases made textures as bsp_write makes them */
H2_ALIGN static boolean world_take(struct build *build)
{
	long i;
	short a;

	h2_world.groups = calloc(build->ce_material_count + 1, sizeof(*h2_world.groups));
	h2_world.buffers = calloc(2 * (build->ce_material_count + 1), sizeof(struct vertex_buffer));
	if (!h2_world.groups || !h2_world.buffers)
		return platform_log("h2 world: out of memory for %ld runs", build->ce_material_count), FALSE;
	for (i = 0; i < build->ce_material_count; i++)
	{
		struct material *m = &build->materials[i];
		struct h2_world_group *g = &h2_world.groups[h2_world.group_count];

		if (m->spilled && !materials_unspill(m, 1))
			return platform_log("h2 world: material %ld not back from the spill file", i), FALSE;
		if (!m->vertex_count || !m->triangle_count || m->shader == NONE)
			continue;
		snprintf(g->path, sizeof(g->path), "%s", build->shaders[m->shader].path);
		g->shader_tag = NONE;
		g->atlas = m->atlas;
		g->breakable_surface_index = (short)(m->breakable_plus1 - 1);
		g->takes_decals = m->decorator ? -1 : 0;
		g->vertex_count = m->vertex_count;
		g->triangle_count = m->triangle_count;
		/* (the material's own vertices taken, not copied - the Vita's C heap
		held Outskirts' first BSP's twice no longer - and let go once its
		buffers are made, world_load; a material in the BSP's room stays
		there, the buffers read it where it is) */
		g->in_room = m->vertices.fixed;
		g->triangles = g->in_room ? (unsigned short *)m->triangles.data : malloc(m->triangles.size);
		if (!g->triangles || !m->vertices.data || !m->lightmap_vertices.data)
			return platform_log("h2 world: material %ld: out of memory", i), FALSE;
		g->vertices = m->vertices.data;
		g->lightmap_vertices = m->lightmap_vertices.data;
		if (!g->in_room)
		{
			memset(&m->vertices, 0, sizeof(m->vertices));
			memset(&m->lightmap_vertices, 0, sizeof(m->lightmap_vertices));
		}
		/* the triangles in cluster order, a range a cluster (the builder's
		cluster of each: an instance's its centre's) */
		{
			short const *clusters = (short const *)m->clusters.data;
			long t, k, n = m->triangle_count;
			long *order = clusters && m->clusters.size >= (unsigned long)n * 2 ? malloc(n * sizeof(long)) : NULL;
			/* (in the room: sorted through a copy, back into place) */
			unsigned short *sorted = order && g->in_room ? malloc(m->triangles.size) : g->triangles;

			if (order && !sorted)
			{
				free(order);
				order = NULL;
			}
			if (order)
			{
				/* (a counting sort by cluster, NONE first: the builder's
				clusters are under 512) */
				static long starts[514];

				memset(starts, 0, sizeof(starts));
				for (t = 0; t < n; t++)
					starts[(clusters[t] < 0 || clusters[t] > 511 ? -1 : clusters[t]) + 2]++;
				for (k = 1; k < 514; k++)
					starts[k] += starts[k - 1];
				for (t = 0; t < n; t++)
					order[starts[(clusters[t] < 0 || clusters[t] > 511 ? -1 : clusters[t]) + 1]++] = t;
				for (t = 0; t < n; t++)
					memcpy(sorted + 3 * t, (unsigned short const *)m->triangles.data + 3 * order[t], 6);
				if (sorted != g->triangles)
				{
					memcpy(g->triangles, sorted, (unsigned long)n * 6);
					free(sorted);
				}
				/* (zeroed: the struct's padding and pad_visible, which nothing
				reads, are kept in the BSP's snapshot - so its bytes are the same
				from one gather to the next, HALO_H2_WORLD_SNAPSHOT_VERIFY) */
				g->ranges = calloc(n, sizeof(*g->ranges));
				for (t = 0; g->ranges && t < n; t++)
				{
					short c = clusters[order[t]];

					if (!g->range_count || g->ranges[g->range_count - 1].cluster != c || g->ranges[g->range_count - 1].count >= 256)
					{
						g->ranges[g->range_count].cluster = c;
						g->ranges[g->range_count].first = t;
						g->ranges[g->range_count].count = 0;
						g->range_count++;
					}
					g->ranges[g->range_count - 1].count++;
				}
				free(order);
				/* each range's bounding sphere (its box's centre, the farthest
				corner) */
				for (t = 0; g->ranges && t < g->range_count; t++)
				{
					struct h2_world_range *range = &g->ranges[t];
					float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
					long u;
					int c3;

					for (u = range->first; u < range->first + range->count; u++)
					{
						for (k = 0; k < 3; k++)
						{
							unsigned short vertex = g->triangles[3 * u + k];
							float const *position = (float const *)(g->vertices + 32 * vertex);

							for (c3 = 0; c3 < 3; c3++)
							{
								if (position[c3] < lo[c3])
									lo[c3] = position[c3];
								if (position[c3] > hi[c3])
									hi[c3] = position[c3];
							}
						}
					}
					range->radius = 0.0f;
					for (c3 = 0; c3 < 3; c3++)
					{
						range->center[c3] = 0.5f * (lo[c3] + hi[c3]);
						range->half[c3] = 0.5f * (hi[c3] - lo[c3]);
						range->radius += 0.25f * (hi[c3] - lo[c3]) * (hi[c3] - lo[c3]);
					}
					range->radius = (float)sqrt(range->radius);
				}
				/* (a range a triangle was made room for: as many as there are kept) */
				if (g->ranges && g->range_count)
				{
					void *kept = realloc(g->ranges, (unsigned long)g->range_count * sizeof(*g->ranges));

					if (kept)
						g->ranges = kept;
				}
			}
			else if (!g->in_room)
				memcpy(g->triangles, m->triangles.data, m->triangles.size);
		}
		{
			long v;
			int k;

			for (k = 0; k < 3; k++)
				g->centroid[k] = 0.0f;
			for (v = 0; v < g->vertex_count; v++)
				for (k = 0; k < 3; k++)
					g->centroid[k] += readf(g->vertices + 32 * v + 4 * k) / (float)g->vertex_count;
		}
		g->buffers = &h2_world.buffers[2 * h2_world.group_count];
		h2_world.group_count++;
	}
	for (a = 0; a < build->atlas_count; a++)
	{
		if (h2_world.atlas_texels[a])
			continue;
		if (!world_atlas_close(build, a))
			return FALSE;
	}
	h2_world.atlas_count = build->atlas_count;
	return TRUE;
}

H2_ALIGN static void world_build_free(struct build *build)
{
	long i;
	short a;

	for (i = 0; i < build->ce_material_count; i++)
	{
		struct material *m = &build->materials[i];

		/* (bytes_free: a buffer in the BSP's room is left where it is) */
		bytes_free(&m->vertices);
		bytes_free(&m->lightmap_vertices);
		bytes_free(&m->triangles);
		bytes_free(&m->clusters);
		bytes_free(&m->bounds);
	}
	free(build->materials);
	free(build->leaf_cluster);
	free(build->world_decorator_pages);
	page_tiles_free(build);
	free(build->page_tiles);
	free(build->page_order);
	for (a = 0; a < build->atlas_count; a++)
		window_free(build->atlases[a].rgb, (unsigned long)LIGHTMAP_ATLAS * LIGHTMAP_ATLAS * 3);
	free(build->placed);
	free(build->vertex_pages);
	free(build->material_shader);
	/* (breakables_read's: each load's - 300 KB of the C heap a load kept) */
	free(build->pane_polygons);
	free(build->instance_pane_polygons);
}

/* the collision BSP converted by the builder's own collision_convert into
memory of its own: once to measure, once into a block that size whose
address the converted blocks take as theirs (region link), so that the
engine reads it as it reads the level's */
H2_ALIGN static boolean world_collision_read(struct build *build)
{
	unsigned long h2 = h2_cache_read32(build->cache, build->sbsp + H2_SBSP_COLLISION_BSP + 4);
	struct region r;
	unsigned long element, size;

	memset(&r, 0, sizeof(r));
	region_alloc(&r, 16);
	element = region_block(&r, 0, 1, 0x60);
	if (r.b.failed || !collision_convert(build, &r, element, h2, NONE))
	{
		window_free(r.b.data, r.b.capacity);
		return FALSE;
	}
	size = r.b.size;
	window_free(r.b.data, r.b.capacity);
	h2_world.collision_memory = malloc(size + 64);
	if (!h2_world.collision_memory)
		return FALSE;
	memset(&r, 0, sizeof(r));
	r.b.data = h2_world.collision_memory;
	r.b.capacity = size + 64;
	r.link = (unsigned long)h2_world.collision_memory;
	region_alloc(&r, 16);
	element = region_block(&r, 0, 1, 0x60);
	if (r.b.failed || r.b.data != h2_world.collision_memory || !collision_convert(build, &r, element, h2, NONE) ||
		r.b.data != h2_world.collision_memory)
	{
		if (r.b.data != h2_world.collision_memory)
			window_free(r.b.data, r.b.capacity);
		free(h2_world.collision_memory);
		h2_world.collision_memory = NULL;
		return FALSE;
	}
	h2_world.collision_element = h2_world.collision_memory + element;
	return TRUE;
}

/* the collision BSP converted into room (the BSP's room after it in the tag
cache, cache_files.c, the builder's measure of it): made there at once - the
room is never grown or moved, past it the conversion fails - its element
copied into the BSP's */
H2_ALIGN static boolean world_collision_into(struct build *build, unsigned char *room, unsigned long room_size,
	unsigned char *bsp_element, unsigned long *used)
{
	unsigned long h2 = h2_cache_read32(build->cache, build->sbsp + H2_SBSP_COLLISION_BSP + 4);
	struct region r;
	unsigned long element;

	memset(&r, 0, sizeof(r));
	r.b.data = room;
	r.b.capacity = room_size;
	r.b.fixed = TRUE;
	r.link = (unsigned long)room;
	region_alloc(&r, 16);
	element = region_block(&r, 0, 1, 0x60);
	if (r.b.failed || !collision_convert(build, &r, element, h2, NONE) || r.b.failed || r.b.data != room)
	{
		platform_log("h2 world: the collision BSP %s (the level's room %lu bytes, %lu used)",
			r.b.failed ? "read from the map does not fit" : h2_cache_memory_failed(build->cache) ?
			"cannot be read from the map: out of memory for its tags" : "cannot be read from the map", room_size, r.b.size);
		return FALSE;
	}
	memcpy(bsp_element, room + element, 0x60);
	*used = r.b.size;
	return TRUE;
}

/* a vertex buffer over memory the GPU reads where it is - the BSP's room
in the tag cache -, as the made BSP's own buffers are registered
(structure_bsp_header_register_vertex_buffers): nothing copied, nothing
owned; unplace frees the buffer's record alone */
H2_ALIGN static boolean world_vertex_buffer_place(struct vertex_buffer *vertex_buffer, long vertex_type, long count, void *vertices)
{
	D3DVertexBuffer *buffer = calloc(1, sizeof(*buffer));

	memset(vertex_buffer, 0, sizeof(*vertex_buffer));
	if (!buffer)
		return FALSE;
	buffer->Common = D3DCOMMON_TYPE_VERTEXBUFFER | 1;
	buffer->Data = 0;
	IDirect3DVertexBuffer8_Register(buffer, vertices);
	vertex_buffer->count = count;
	vertex_buffer->offset = 0;
	vertex_buffer->base_address = vertices;
	vertex_buffer->type = (short)vertex_type;
	vertex_buffer->hardware_format = buffer;
	halo_d3d_buffer_in_place(buffer, TRUE);
	return TRUE;
}

H2_ALIGN static void world_vertex_buffer_unplace(struct vertex_buffer *vertex_buffer)
{
	if (vertex_buffer->hardware_format)
	{
		halo_d3d_buffer_in_place(vertex_buffer->hardware_format, FALSE);
		free(vertex_buffer->hardware_format);
	}
	memset(vertex_buffer, 0, sizeof(*vertex_buffer));
}

/* the materials' buffers carved from the BSP's room after its collision
BSP, each the size the first pass counted and fixed there (the second pass
fills them and no more), laid out as world_material_room measures them
(h2_level.c); the counts cleared for the second pass */
H2_ALIGN static boolean world_room_carve(struct build *build, unsigned char *room, unsigned long room_size,
	unsigned long collision_used, char *why, long why_size)
{
	unsigned char *at = room + world_room_align(collision_used);
	unsigned long needed = world_room_align(collision_used);
	long i;

	for (i = 0; i < build->ce_material_count; i++)
	{
		struct material const *m = &build->materials[i];

		needed += world_material_room(m->vertex_count, m->triangle_count);
	}
	/* (a level made with its world's geometry apart from the tag cache - its
	tags, BSP and whole room more than the tag cache holds, h2_level.c
	world_tables_reserve_measure: the room its tables' and collision's alone):
	the materials' buffers in a block of their own the GPU reads where it is,
	video memory first (halo_world_geometry_acquire) */
	if (needed > room_size && world_room_align(collision_used) <= room_size)
	{
		unsigned long apart = needed - world_room_align(collision_used);

		at = halo_world_geometry_acquire(apart);
		if (!at)
		{
			snprintf(why, (size_t)why_size, "no memory for its world's %lu KB of vertices and triangles apart from the tag cache "
				"(%ld materials; %lu KB of tables in the room)", apart / 1024, build->ce_material_count, collision_used / 1024);
			return FALSE;
		}
		platform_log("h2 world: its %ld materials' vertices and triangles, %lu KB, apart from the tag cache (%lu KB of its room for "
			"the tables and collision)", build->ce_material_count, apart / 1024, room_size / 1024);
		h2_world.apart = at;
		h2_world.apart_size = apart;
	}
	if (needed > room_size && !h2_world.apart)
	{
		snprintf(why, (size_t)why_size, "its world needs %lu KB of room after the BSP (%lu KB of tables, %ld materials' %lu KB), the level left %lu KB",
			needed / 1024, world_room_align(collision_used) / 1024, build->ce_material_count,
			(needed - world_room_align(collision_used)) / 1024, room_size / 1024);
		return FALSE;
	}
	for (i = 0; i < build->ce_material_count; i++)
	{
		struct material *m = &build->materials[i];
		struct bytes *buffers[4] = { &m->vertices, &m->lightmap_vertices, &m->triangles, &m->clusters };
		unsigned long sizes[4];
		int k;

		sizes[0] = (unsigned long)m->vertex_count * COMPRESSED_VERTEX_SIZE;
		sizes[1] = (unsigned long)m->vertex_count * COMPRESSED_LIGHTMAP_VERTEX_SIZE;
		sizes[2] = (unsigned long)m->triangle_count * 6;
		sizes[3] = (unsigned long)m->triangle_count * 2;
		for (k = 0; k < 4; k++)
		{
			bytes_free(buffers[k]);
			buffers[k]->data = at;
			buffers[k]->capacity = sizes[k];
			buffers[k]->fixed = TRUE;
			at += world_room_align(sizes[k]);
		}
		bytes_free(&m->bounds);
		m->vertex_count = 0;
		m->triangle_count = 0;
		m->centroid[0] = m->centroid[1] = m->centroid[2] = 0.0;
		m->lowest = 1e30f;
	}
	h2_world.room_size = room_size;
	h2_world.room_used = h2_world.apart ? world_room_align(collision_used) : (unsigned long)(at - room);
	return TRUE;
}

/* the geometry gathered into the BSP's room: a first pass counting each
material's vertices and triangles, the room carved into buffers of those
sizes, a second pass filling them - the passes read the same sections in
the same order and decide alike, so the second finds the first's materials
(material_for); if they disagree, the read fails */
H2_ALIGN static boolean world_gather_room(struct build *build, unsigned char *room, unsigned long room_size,
	unsigned long collision_used, char *why, long why_size)
{
	unsigned long long started = vita_host_time_us();
	boolean ok;
	long i;

	world_memory_mark(build, "before the count");
	build->world_counting = TRUE;
	ok = world_gather_pass(build);
	build->world_counting = FALSE;
	h2_world.us_count = vita_host_time_us() - started;
	world_memory_mark(build, "counted");
	h2_cache_trim(build->cache);
	if (!ok)
		return FALSE;
	build->world_counted = build->ce_material_count;
	if (!world_room_carve(build, room, room_size, collision_used, why, why_size))
		return FALSE;
	started = vita_host_time_us();
	build->ce_material_count = 0;
	build->world_decorator_page_next = 0;
	build->world_placing = TRUE;
	ok = world_gather_pass(build);
	build->world_placing = FALSE;
	h2_world.us_fill = vita_host_time_us() - started;
	world_memory_mark(build, "filled");
	h2_cache_trim(build->cache);
	if (!ok)
		return FALSE;
	if (build->ce_material_count != build->world_counted)
	{
		snprintf(why, (size_t)why_size, "its world's two passes disagree: %ld materials counted, %ld filled",
			build->world_counted, build->ce_material_count);
		return FALSE;
	}
	for (i = 0; i < build->ce_material_count; i++)
	{
		struct material const *m = &build->materials[i];

		if (m->vertices.size != m->vertices.capacity || m->triangles.size != m->triangles.capacity)
		{
			snprintf(why, (size_t)why_size, "its world's two passes disagree on material %ld: %lu of %lu vertex bytes, %lu of %lu triangle bytes",
				i, m->vertices.size, m->vertices.capacity, m->triangles.size, m->triangles.capacity);
			return FALSE;
		}
	}
	/* (debug) HALO_H2_WORLD_SUM=1: the materials' vertices, lightmap
	vertices, triangles and clusters' FNV-1a, a make's check against
	another's */
	if (getenv("HALO_H2_WORLD_SUM"))
	{
		unsigned long sum = 2166136261UL, q;

		for (i = 0; i < build->ce_material_count; i++)
		{
			struct material const *m = &build->materials[i];
			struct bytes const *parts[4] = { &m->vertices, &m->lightmap_vertices, &m->triangles, &m->clusters };
			int k;

			for (k = 0; k < 4; k++)
				for (q = 0; q < parts[k]->size; q++)
					sum = ((sum ^ parts[k]->data[q]) * 16777619UL) & 0xFFFFFFFFUL;
		}
		platform_log("h2 world: its %ld materials' buffers sum to %08lx", build->ce_material_count, sum);
	}
	return TRUE;
}

/* (the level's build, HALO_H2_WORLD_FROM_MAP) the BSP written as a stub
in place of the whole one made in full: its header and its root with every
block and data empty - the tables are made again from the map as it loads
(world_load's bsp_write into the BSP's memory, then its vertices after) */
H2_ALIGN static boolean world_bsp_stub_write(struct build *build, struct region *r, struct region const *full)
{
	static unsigned long const blocks[] = {
		offsetof(struct structure_bsp, collision_materials), offsetof(struct structure_bsp, collision_bsp),
		offsetof(struct structure_bsp, nodes), offsetof(struct structure_bsp, leaves),
		offsetof(struct structure_bsp, surface_references), offsetof(struct structure_bsp, surfaces),
		offsetof(struct structure_bsp, lightmaps), offsetof(struct structure_bsp, lens_flares),
		offsetof(struct structure_bsp, lens_flare_markers), offsetof(struct structure_bsp, clusters),
		offsetof(struct structure_bsp, cluster_portals), offsetof(struct structure_bsp, breakable_surfaces),
		offsetof(struct structure_bsp, fog_planes), offsetof(struct structure_bsp, fog_regions),
		offsetof(struct structure_bsp, fog_palette), offsetof(struct structure_bsp, weather_palette),
		offsetof(struct structure_bsp, weather_polyhedra), offsetof(struct structure_bsp, pathfinding_surfaces),
		offsetof(struct structure_bsp, pathfinding_edges), offsetof(struct structure_bsp, background_sound_palette),
		offsetof(struct structure_bsp, sound_environment_palette), offsetof(struct structure_bsp, markers),
		offsetof(struct structure_bsp, detail_object_data), offsetof(struct structure_bsp, runtime_decals)
	};
	static unsigned long const datas[] = {
		offsetof(struct structure_bsp, cluster_data), offsetof(struct structure_bsp, sound_cluster_data)
	};
	unsigned long header = region_alloc(r, 0x18), root = region_alloc(r, 0x288);
	unsigned long k;

	(void)build;
	if (r->b.failed || full->b.size < 0x18 + 0x288)
		return FALSE;
	memcpy(region_at(r, header), full->b.data, 0x18 + 0x288);
	for (k = 0; k < NUMBEROF(blocks); k++)
		memset(region_at(r, root + blocks[k]), 0, sizeof(struct tag_block));
	for (k = 0; k < NUMBEROF(datas); k++)
		memset(region_at(r, root + datas[k]), 0, sizeof(struct tag_data));
	return !r->b.failed;
}

/* the level's weather tags, by their names (h2_effects.c's "h2\\weather\\<name> <k>"): the BSP's weather palette written again */
static void world_weather_tags_find(void)
{
	struct world_tag_instance const *instances = (struct world_tag_instance const *)global_tag_instances;
	long indices[16], count = 0, i;

	for (i = 0; i < 16; i++)
		indices[i] = NONE;
	for (i = 0; instances && i < halo_loaded_tag_count; i++)
	{
		char const *name = instances[i].name;
		char const *space;
		long k;

		if (instances[i].group_tag != 'weat' || !name || strncmp(name, "h2\\weather\\", 11))
			continue;
		space = strrchr(name, ' ');
		k = space ? atol(space + 1) : -1;
		if (k >= 0 && k < 16)
		{
			indices[k] = i;
			if (k + 1 > count)
				count = k + 1;
		}
	}
	h2_effects_weather_tags_set(count, indices);
}

/* a shader's loaded tag, by its name (tag_ids.shader_id) */
H2_ALIGN static unsigned long world_shader_id(struct build *build, long shader)
{
	long found;

	if (shader < 0 || shader >= build->shader_count)
		return 0xFFFFFFFFUL;
	found = tag_loaded(build->shaders[shader].group, build->shaders[shader].path);
	return found == NONE ? 0xFFFFFFFFUL : (unsigned long)found;
}

/* the level's skies, fogs and water as the builder gathered them, in its
order over the level's BSPs up to this one (the sky slots and fog skies
bsp_write names, the planar fogs' tags, the sea's cluster): the same code
over the same data, no geometry - sky_add and water_add stop at their
bookkeeping while build->world_loading / world_replaying */
H2_ALIGN static boolean world_level_replay(struct build *build, long bsp, char *why, long why_size)
{
	struct h2_cache *cache = build->cache;
	unsigned long sbsp = build->sbsp, lightmap = build->lightmap;
	long k;

	skies_wanted(build);
	for (k = 0; k <= bsp; k++)
	{
		unsigned char const *b;
		float bounds[6];

		/* (the loading screen drawn between the BSPs and their parts: h2_level.h -
		2.6 s of Zanzibar's load on a Vita with no frame) */
		h2_load_draw_coarse();
		if (k == bsp)
			build->sbsp = sbsp, build->lightmap = lightmap;
		else if (!h2_cache_structure_load(cache, k, &build->sbsp, &build->lightmap, why, why_size))
			return FALSE;
		b = h2_cache_at(cache, build->sbsp + H2_SBSP_WORLD_BOUNDS, 24);
		if (b)
		{
			memcpy(bounds, b, sizeof(bounds));
			/* (level_gather: the first BSP's skies, as models - at load their
			slots alone) */
			if (!getenv("HALO_H2_NO_SKY") && !build->own_sky)
			{
				struct h2_cache_tag scenario_tag;
				long sky_total = 1;
				unsigned long sky_block;

				build->sky_as_model = !getenv("HALO_H2_SKY_IN_BSP");
				if (!h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario_tag) ||
					!h2_cache_block(cache, scenario_tag.address + H2_SCENARIO_SKIES, 8, &sky_total, &sky_block))
				{
					sky_total = 0;
				}
				if (getenv("HALO_H2_ONE_SKY") || !build->sky_as_model || sky_total < 1)
					sky_add(build, 0, bounds, 0);
				else
					skies_add(build, 0, bounds);
				build->own_sky = TRUE;
			}
			h2_load_draw_coarse();
			/* (the level's water: decided, its plane the shown BSP's passes') */
			if (!getenv("HALO_H2_NO_WATER"))
			{
				build->world_replaying = TRUE;
				water_add(build, 0, bounds);
				build->world_replaying = FALSE;
			}
		}
		h2_load_draw_coarse();
		planar_fogs_gather(build);
		h2_load_draw_coarse();
		atmospheric_fogs_gather(build);
		if (k < bsp)
			h2_cache_trim(cache);
	}
	build->sbsp = sbsp;
	build->lightmap = lightmap;
	/* (this BSP's cluster with the most vertices: the sky's and the sea's
	cluster - level_gather's, from the sections it read; here from their
	headers) */
	{
		unsigned long clusters;
		long count, i, most = 0;

		build->water_cluster = NONE;
		if (h2_cache_block(cache, build->sbsp + H2_SBSP_CLUSTERS, H2_CLUSTER_SIZE, &count, &clusters))
		{
			for (i = 0; i < count; i++)
			{
				unsigned char const *g = h2_cache_at(cache, clusters + H2_CLUSTER_SIZE * i, 0x40);
				long vertices = g ? (unsigned short)read16(g) : 0;

				if (g && read32(g + 0x2C) && vertices > most)
				{
					most = vertices;
					build->water_cluster = (short)i;
				}
			}
		}
		if (!build->has_water)
			build->water_cluster = NONE;
	}
	return TRUE;
}

/* the last level's world let go: its groups' copies, collision, buffers
(the side blocks are kept, as the texture streaming's are) */
H2_ALIGN static void world_last_free(void)
{
	long i;

	/* (a snapshot of this world still being written reads it: finished first) */
	world_snapshot_write_finish();
	for (i = 0; i < h2_world.group_count; i++)
	{
		struct h2_world_group *g = &h2_world.groups[i];

		/* (a run in the BSP's room owns nothing there: the room went with
		the BSP) */
		if (!g->in_room)
		{
			window_free(g->vertices, 0);
			window_free(g->lightmap_vertices, 0);
			free(g->triangles);
		}
		free(g->ranges);
		if (h2_world.buffers && g->in_room)
		{
			world_vertex_buffer_unplace(&g->buffers[0]);
			world_vertex_buffer_unplace(&g->buffers[1]);
		}
		else if (h2_world.buffers)
		{
			rasterizer_vertex_buffer_delete(&g->buffers[0]);
			rasterizer_vertex_buffer_delete(&g->buffers[1]);
		}
	}
	world_collision_install(FALSE);
	free(h2_world.collision_memory);
	/* (the index buffers' records before the block: one placed over its
	triangles is told from a copy of its own by the block it lies in,
	d3d8_resources.c index_memory_placed) */
	for (i = 0; i < h2_world.group_count; i++)
		if (h2_world.groups[i].index_buffer)
			IDirect3DIndexBuffer8_Release((D3DIndexBuffer *)h2_world.groups[i].index_buffer);
	/* (the buffers' records gone above, the GPU done with them: the block
	given back - halo_world_geometry_release waits for the GPU) */
	if (h2_world.apart)
		halo_world_geometry_release(h2_world.apart);
	free(h2_world.groups);
	free(h2_world.buffers);
	free(h2_world.atlas_hardware);
	/* (the decals' boxes of its triangles: h2_world_triangles_near) */
	world_boxes_forget();
	memset(&h2_world, 0, sizeof(h2_world));
}

/* the runs' buffers made over their vertices and triangles (in the room or
the block apart: read where they are; else copies of their own, the runs'
bytes let go): the read's end and a snapshot's */
H2_ALIGN static void world_buffers_make(void)
{
	long i;

	/* (the buffers made now, on the loading thread, as a Custom Edition
	map's converted structure's are - custom_edition_geometry.c - and
	read in place) */
	boolean rasterizer_vertex_buffer_new(struct vertex_buffer *vertex_buffer, long vertex_type, long count,
		void const *vertices, long buffer_size);

	for (i = 0; i < h2_world.group_count; i++)
	{
		struct h2_world_group *g = &h2_world.groups[i];
		/* (a run in the room, or in the geometry block apart from it -
		world_room_carve: the block is the GPU's, read where it is) */
		boolean in_place = g->in_room && (h2_world.apart ? halo_world_geometry_holds(g->triangles, (unsigned long)g->triangle_count * 6) :
			h2_world.room_contiguous);

		{
			D3DIndexBuffer *index_buffer = NULL;
			BYTE *locked = NULL;

			/* (in the room: the buffer's record over the room's triangles,
			read where they are - a Custom Edition map's model strips are
			placed so; else a copy of its own. A record of 2 bytes whose
			place was refused drew whatever followed them in the C heap as
			the run's triangles: the Vita's Cairo Station and Old Mombasa
			drawn with their world apart, torn across the screen) */
			if (in_place && IDirect3DDevice8_CreateIndexBuffer(global_d3d_device, 2, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16,
					D3DPOOL_MANAGED, &index_buffer) >= 0 && index_buffer && !halo_d3d_index_buffer_place(index_buffer, g->triangles))
			{
				platform_log("h2 world: run %ld's triangles not placed where they are (%p): copied", i, (void *)g->triangles);
				IDirect3DIndexBuffer8_Release(index_buffer);
				index_buffer = NULL;
				in_place = FALSE;
			}
			if (in_place)
				g->index_buffer = index_buffer;
			else if (IDirect3DDevice8_CreateIndexBuffer(global_d3d_device, (UINT)(g->triangle_count * 6),
					D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &index_buffer) >= 0 && index_buffer)
			{
				IDirect3DIndexBuffer8_Lock(index_buffer, 0, (UINT)(g->triangle_count * 6), &locked, 0);
				if (locked)
					memcpy(locked, g->triangles, (unsigned long)g->triangle_count * 6);
				IDirect3DIndexBuffer8_Unlock(index_buffer);
				g->index_buffer = index_buffer;
			}
		}
		if (g->in_room)
		{
			/* (the room's vertices read where they are: nothing copied) */
			world_vertex_buffer_place(&g->buffers[0], _rasterizer_vertex_type_environment_compressed, g->vertex_count, g->vertices);
			world_vertex_buffer_place(&g->buffers[1], _rasterizer_vertex_type_environment_lightmap_compressed, g->vertex_count,
				g->lightmap_vertices);
		}
		else if (rasterizer_vertex_buffer_new(&g->buffers[0], _rasterizer_vertex_type_environment_compressed, g->vertex_count,
				g->vertices, g->vertex_count * 32) &&
			rasterizer_vertex_buffer_new(&g->buffers[1], _rasterizer_vertex_type_environment_lightmap_compressed,
				g->vertex_count, g->lightmap_vertices, g->vertex_count * 8))
		{
			halo_d3d_buffer_in_place(g->buffers[0].hardware_format, TRUE);
			halo_d3d_buffer_in_place(g->buffers[1].hardware_format, TRUE);
		}
		/* (the buffers hold their own copies: the run's let go; a run in
		the room owns nothing) */
		if (!g->in_room)
		{
			window_free(g->vertices, 0);
			window_free(g->lightmap_vertices, 0);
			free(g->triangles);
		}
		g->vertices = g->lightmap_vertices = NULL;
		g->triangles = NULL;
	}
}

H2_ALIGN static void world_load_body(char const *maps_directory, char const *name, long bsp, unsigned char *room,
	unsigned long room_size)
{
	struct h2_level_build_result result;
	struct build *build;
	char why[H2_CACHE_WHY_SIZE];
	long i;
	boolean ok;
	unsigned long collision_used = 0;
	unsigned long long started, mark;

	world_last_free();
	if (!h2_world_enabled() || !maps_directory)
		return;
	/* (its snapshot from an earlier read of this BSP: the room filled from
	it, nothing gathered - h2_world_snapshot.inc) */
	if (room && world_snapshot_restore(maps_directory, name, bsp, room, room_size))
		return;
#if defined(HALO_VITA)
	platform_memory_log("h2 world: begun", name);
#else
	if (getenv("HALO_H2_DEBUG"))
		platform_memory_log("h2 world: begun", name);
#endif
	/* (a build with room for the world's shaders alone: the builder's 1024
	are 1.9 MB of the 2 MB, and a BSP has under 200) */
	build = calloc(1, offsetof(struct build, shaders) + WORLD_MAXIMUM_SHADERS * sizeof(struct shader));
	if (!build)
		return;
	page_neutral_reset();
	/* (the atlases' bitmaps' hardware formats, for those closed as they fill) */
	h2_world.atlas_hardware = calloc(MAXIMUM_ATLASES, texture_cache_hardware_format_size());
	memset(&result, 0, sizeof(result));
	build->result = &result;
	/* (the level's load keeps one open of its map and serves it to each
	module, and to the game thread's opens through the play while a user
	holds it - the sounds do -, h2_cache.h: opened afresh at a switch during
	play, the map took the C heap the play had little of) */
	build->cache = h2_cache_open(maps_directory, name, why, sizeof(why));
	if (!build->cache)
	{
		platform_log("h2 world: %s.map cannot be read: %s", name, why);
		free(build);
		return;
	}
	why[0] = 0;
	/* (another user's failed reads of the map - the level's load's modules
	reading shared.map through the kept open with the C heap short - are
	not this read's: the flag taken, so only this read's own count) */
	(void)h2_cache_memory_failed(build->cache);
	build->world_loading = TRUE;
	started = vita_host_time_us();
	world_memory_mark(build, "opened");
	h2_world.room_contiguous = room && platform_is_contiguous(room);
	ok = h2_cache_structure_load(build->cache, bsp, &build->sbsp, &build->lightmap, why, sizeof(why));
	if (ok && !(ok = world_gather_begin(build)))
		snprintf(why, sizeof(why), "its materials not read");
	/* (a level with no world of its own: the BSP itself - its tables, its
	collision - written from the map into the BSP's memory as the builder
	would have, the level's skies, fogs and water gathered again first;
	its vertices come after its tables) */
	if (ok && room)
	{
		struct region r;
		struct tag_ids ids;
		long k;
		char map_name[64];

		if (!(ok = world_level_replay(build, bsp, why, sizeof(why))) && !why[0])
			snprintf(why, sizeof(why), "the level's skies and fogs not gathered again");
		h2_load_step("world: the skies and fogs again");
		if (ok && !(ok = leaves_gather(build)))
			snprintf(why, sizeof(why), "its leaves not read");
		h2_load_step("world: the leaves");
		if (ok)
		{
			world_weather_tags_find();
			snprintf(map_name, sizeof(map_name), "%s", h2_cache_name(build->cache));
			memset(&ids, 0, sizeof(ids));
			snprintf(why, sizeof(why), "h2\\%s_lightmaps", map_name);
			k = tag_loaded('bitm', why);
			ids.lightmap_bitmap = k == NONE ? 0xFFFFFFFFUL : (unsigned long)k;
			snprintf(why, sizeof(why), "h2\\%s_sky", map_name);
			ids.sky = tag_loaded('sky ', why) == NONE ? 0xFFFFFFFFUL : 0;
			ids.fog = 0xFFFFFFFFUL;
			for (k = 0; k < H2_MAXIMUM_PLANAR_FOGS; k++)
			{
				long found;

				snprintf(why, sizeof(why), "h2\\%s_planar_fog %ld", map_name, k);
				found = k < build->planar_fog_count ? tag_loaded('fog ', why) : NONE;
				ids.fogs[k] = found == NONE ? 0xFFFFFFFFUL : (unsigned long)found;
				if (k == 0)
					ids.fog = ids.fogs[0];
			}
			ids.shader_id = world_shader_id;
			/* (h2/render-accuracy: its runtime decals - the signs, arrows,
			warning labels Halo 2 makes as their clusters come into view,
			structure_runtime_decals.c - written by bsp_write only with the
			scenario's decal palette counted; the level file's own scenario
			holds the palette (decals_gather made it as the file was made),
			the world's load had none: every level decal was missing on the
			desktop. HALO_H2_WORLD_NO_RUNTIME_DECALS=1: none, as before) */
			if (!build->decal_palette_count && !getenv("HALO_H2_NO_DECALS") && !getenv("HALO_H2_WORLD_NO_RUNTIME_DECALS"))
			{
				struct h2_cache_tag scenario;
				unsigned long palette;
				long count;

				if (h2_cache_tag_get(build->cache, h2_cache_scenario_tag(build->cache), &scenario) &&
					h2_cache_block(build->cache, scenario.address + 0x140, 8, &count, &palette) && count > 0)
					build->decal_palette_count = count > MAXIMUM_DECAL_PALETTE ? MAXIMUM_DECAL_PALETTE : count;
			}
			why[0] = 0;
			memset(&r, 0, sizeof(r));
			r.b.data = room;
			r.b.capacity = room_size;
			r.b.fixed = TRUE;
			r.link = (unsigned long)room;
			if (!(ok = bsp_write(build, &r, &ids)) || r.b.failed || r.b.data != room)
			{
				ok = FALSE;
				snprintf(why, sizeof(why), "its BSP not made from the map (%s)", r.b.failed ? "no room for it" :
					result.why[0] ? result.why : "bsp_write");
			}
			collision_used = r.b.size;
			h2_load_step("world: the BSP's tables and collision written");
		}
		h2_world.us_collision = vita_host_time_us() - started;
		h2_cache_trim(build->cache);
		world_memory_mark(build, "the BSP made");
		if (ok && h2_cache_memory_failed(build->cache))
		{
			ok = FALSE;
			snprintf(why, sizeof(why), "out of memory for the map's tags (the BSP)");
		}
	}
	/* (the shaders' and the lightmap's chunks: the shaders are copied) */
	h2_cache_trim(build->cache);
	if (ok && h2_cache_memory_failed(build->cache))
	{
		ok = FALSE;
		snprintf(why, sizeof(why), "out of memory for the map's tags (the shaders)");
	}
	if (ok && room)
		platform_log("h2 world: %s: its BSP made from the map in %lu KB: %ld clusters, %ld leaves, %ld fog planes, %ld skies, %ld fog skies",
			name, collision_used / 1024, build->cluster_count, build->leaf_count, build->planar_fog_count, build->sky_count,
			build->fog_sky_count);
	/* (a level with no world of its own: the geometry into the BSP's room,
	in two passes; a level with one: into memory of its own, in one) */
	if (ok && !(ok = room ? world_gather_room(build, room, room_size, collision_used, why, sizeof(why)) : world_gather_pass(build)) &&
		!why[0])
	{
		snprintf(why, sizeof(why), "its geometry not gathered (%ld materials, %ld clusters)", (long)build->material_count,
			build->cluster_count);
	}
	mark = vita_host_time_us();
	if (ok && !(ok = world_take(build)))
		snprintf(why, sizeof(why), "its runs not taken (%ld materials, %d atlases)", build->ce_material_count, (int)build->atlas_count);
	if (ok && !room && !(ok = world_collision_read(build)))
		snprintf(why, sizeof(why), "its collision BSP not read");
	if (ok)
	{
		/* (its snapshot written now, while the runs still point at their
		bytes: the buffers made next let those pointers go) */
		if (room)
			world_snapshot_write_begin(maps_directory, name, bsp, room, room_size);
		world_buffers_make();
		h2_world.loaded = TRUE;
		/* (the decals' boxes of its triangles, made now rather than at the first decal) */
		world_boxes_make();
		h2_world.us_take = vita_host_time_us() - mark;
		h2_world.us_total = vita_host_time_us() - started;
		platform_log("h2 world: %s: its world read from the map - %ld runs, %d lightmap atlases (structure BSP %ld) in %lu ms: "
			"collision %lu, count %lu, fill %lu, runs and buffers %lu%s", name, h2_world.group_count, (int)h2_world.atlas_count, bsp,
			(unsigned long)(h2_world.us_total / 1000), (unsigned long)(h2_world.us_collision / 1000),
			(unsigned long)(h2_world.us_count / 1000), (unsigned long)(h2_world.us_fill / 1000), (unsigned long)(h2_world.us_take / 1000),
			room ? "" : " (the level's world drawn from the map beside its own)");
		platform_log("h2 world: the passes' parts (ms; count / fill): sections read %lu / %lu, lightmap pages placed %lu / %lu, "
			"geometry added %lu / %lu, decorators %lu / %lu",
			(unsigned long)(h2_world.us_parts[0][0] / 1000), (unsigned long)(h2_world.us_parts[1][0] / 1000),
			(unsigned long)(h2_world.us_parts[0][1] / 1000), (unsigned long)(h2_world.us_parts[1][1] / 1000),
			(unsigned long)(h2_world.us_parts[0][2] / 1000), (unsigned long)(h2_world.us_parts[1][2] / 1000),
			(unsigned long)(h2_world.us_parts[0][3] / 1000), (unsigned long)(h2_world.us_parts[1][3] / 1000));
		if (room)
			platform_log("h2 world: the BSP's room: %lu of %lu KB used%s", h2_world.room_used / 1024, h2_world.room_size / 1024,
				h2_world.room_contiguous ? ", read by the GPU where it is" : ", NOT in the contiguous window: the triangles copied");
		platform_log("h2 world: %ld lightmap pages placed, %ld not%s%s; %lu ms decoding %lu MB of them, %lu ms into the atlas "
			"(the threads' time together; %ld batches, %lu ms in all, %d workers)",
			build->world_pages_placed, build->world_pages_not_placed, build->world_pages_not_placed ? " - the first: " : "",
			build->world_pages_not_placed ? build->world_page_why : "", (unsigned long)(build->us_pages_read / 1000),
			build->pages_bytes / (1024 * 1024), (unsigned long)(build->us_pages_convert / 1000), build->pages_batches,
			(unsigned long)(build->us_pages_batches / 1000), halo_mt_workers());
	}
	if (!h2_world.loaded)
		platform_log("h2 world: %s: its world not read (%s): the level's own drawn", name, result.why[0] ? result.why : why);
	world_build_free(build);
	h2_cache_trim(build->cache);
	h2_cache_close(build->cache);
	free(build);
#if defined(HALO_VITA)
	platform_memory_log("h2 world: done", name);
#else
	if (getenv("HALO_H2_DEBUG"))
		platform_memory_log("h2 world: done", name);
#endif
}

/* (h2_world_memory_short) the last world read failed for want of memory */
static boolean world_memory_short;

/* a BSP's world read from the map (world_load_body), its memory - the map's
inflated pieces, the BSP's made parts - the builder's heap's (user RAM,
h2_build_heap.h), not the C heap's: during the play (a level's first BSP
after its direct make, a switch) a campaign BSP's pieces came to more than
the Vita's 48 MB C heap had left - 01b_spacestation, 06b, 07a's world "out
of memory for the map's tags (the BSP)", 06a's at a switch after a revert
"its collision BSP is damaged" (fix/h2-direct-sweep) */
H2_ALIGN static void world_load(char const *maps_directory, char const *name, long bsp, unsigned char *room,
	unsigned long room_size)
{
	boolean heap = !h2_build_heap_active();
	unsigned long cache_failures = h2_cache_memory_failures_count();

	if (heap)
		h2_build_heap_begin();
	world_load_body(maps_directory, name, bsp, room, room_size);
	/* (its map's pieces or the builder's blocks refused: said as out of
	memory, not as a damaged map - a switch the play's memory could not
	hold, the dev Vita's Cairo at Quality, Nov 11) */
	world_memory_short = !h2_world.loaded && (h2_cache_memory_failures_count() != cache_failures ||
		(heap && (h2_build_heap_failed(NULL) || h2_build_heap_refused_large())));
	if (heap)
		h2_build_heap_end();
}

boolean h2_world_memory_short(void)
{
	return world_memory_short;
}

#ifdef HALO_VITA
void vita_host_user_ram_census(char *line, unsigned long size);
#endif

void h2_scripts_structure_bsp_switching(void);
boolean h2_menu_level_textures_release(void);
unsigned long platform_contiguous_side_usage(unsigned long *blocks);

/* (scenario.c) a structure BSP refused for memory at a switch: what the play
can do without for a moment let go before it is tried again - the level's
pause menu textures (made again as the level plays on), the scripts' map
pieces (read again as needed); said with what it gave. The shader
compiler's heap cannot be: its compiler is never released (vita_gxm.c: a
release then a compile faulted). The bytes let go (0: nothing) */
unsigned long h2_world_switch_reclaim(void)
{
	unsigned long side_before = platform_contiguous_side_usage(NULL), side_after;
	unsigned long heap_before = 0, heap_after = 0, capacity = 0, side_freed, heap_freed;
	boolean menu;

	platform_heap_usage(&heap_before, &capacity);
	h2_scripts_structure_bsp_switching();
	menu = h2_menu_level_textures_release();
	side_after = platform_contiguous_side_usage(NULL);
	platform_heap_usage(&heap_after, &capacity);
	side_freed = side_before > side_after ? side_before - side_after : 0;
	heap_freed = heap_before > heap_after ? heap_before - heap_after : 0;
	platform_log("bsp switch: memory let go to try again: the pause menu's textures %s (%lu KB of side blocks), the C heap %lu KB",
		menu ? "let go (made again as the level plays on)" : "kept", side_freed / 1024, heap_freed / 1024);
	return side_freed + heap_freed ? side_freed + heap_freed : (unsigned long)menu;
}

/* (scenario.c) a structure BSP switch's memory as it begins and ends: the C
heap, the builder's heap, the level's map's pieces the scripts' open holds
and, on the Vita, the user RAM's census - what a switch the play's memory
could not hold had (the dev Vita's Cairo at Quality, Nov 11) */
void h2_world_switch_memory_log(char const *when)
{
	unsigned long heap_in_use = 0, heap_capacity = 0, build_in_use = 0, build_peak = 0, build_blocks = 0;
	struct h2_cache *scripts = h2_scripts_cache();
	char line[1024];

	platform_heap_usage(&heap_in_use, &heap_capacity);
	h2_build_heap_figures(&build_in_use, &build_peak, &build_blocks);
	line[0] = 0;
#ifdef HALO_VITA
	vita_host_user_ram_census(line, sizeof(line));
#endif
	platform_log("bsp switch memory (%s): C heap %lu KB in use of %lu KB; the builder's heap %lu KB in %lu KB of blocks; the scripts' "
		"map pieces %lu KB%s%s", when, heap_in_use / 1024, heap_capacity / 1024, build_in_use / 1024, build_blocks / 1024,
		scripts ? h2_cache_segment_bytes(scripts) / 1024 : 0UL, line[0] ? "; " : "", line);
}

/* the maps folder the levels resolve in, and the level whose world was read
last: read as the level is first drawn - a level is resolved more than once
(the menus' lists resolve each), not only before it loads */
static char world_maps_directory[512];
static char world_level[64];
static long world_bsp = -1;

/* (the map's unload, cache_files.c) the level's world let go now, not at
the next level's read: what it holds in the build's heap - 8-11 MB on
03a - is the next make's room (03b's make after 03a peaked 29 MB over a
fresh start's on Vita3K) */
void h2_world_unloaded(void)
{
	long a;

	world_last_free();
	world_level[0] = 0;
	world_bsp = -1;
	/* (the lightmap pages' blocks kept from BSP to BSP within a level, given
	back with it: the next level's make has their room, as the first level's
	had - an insider.5 tester's Vita held Turf's ~6 MB of them, and its
	texture stream's, into Zanzibar's make, which ran the system out of user
	RAM; the next level's world makes them again) */
	for (a = 0; a < MAXIMUM_ATLASES; a++)
	{
		if (!world_atlas_pool[a])
			continue;
		if (platform_is_contiguous(world_atlas_pool[a]))
			window_free(world_atlas_pool[a], world_atlas_pool_bytes[a]);
		else
			platform_contiguous_free(world_atlas_pool[a]);
		world_atlas_pool[a] = NULL;
		world_atlas_pool_bytes[a] = 0;
		world_atlas_pool_turn[a] = 0;
	}
}


void h2_world_level_resolved(char const *maps_directory, char const *name)
{
	(void)name;
	if (maps_directory)
		snprintf(world_maps_directory, sizeof(world_maps_directory), "%s", maps_directory);
}

/* (h2_world_active, the render thread) the loaded level's world, read when
the level changes */
static void world_follow_level(void)
{
	extern long global_scenario_index;
	extern short global_structure_bsp_index;
	char const *path, *name;

	if (global_scenario_index == NONE || !world_maps_directory[0])
		return;
	path = tag_get_name(global_scenario_index);
	name = path ? strrchr(path, '\\') : NULL;
	name = name ? name + 1 : path;
	if (!name || (!strcmp(name, world_level) && world_bsp == global_structure_bsp_index))
		return;
	snprintf(world_level, sizeof(world_level), "%s", name);
	/* (the structure BSP the level shows now: a campaign level's switch
	reads the next one's world - the level's BSPs are the map's, in order) */
	world_bsp = global_structure_bsp_index;
	world_load(world_maps_directory, name, world_bsp < 0 ? 0 : world_bsp, NULL, 0);
}

/* (scenario_switch_structure_bsp, the game's thread) the BSP just loaded:
its world read now, with the level's load or the switch's, not at the
first frame drawn (which still reads it if this did not) */
void h2_world_structure_bsp_loaded(void)
{
	/* (a level with no world of its own: read with its BSP already,
	h2_world_structure_bsp_read - its collision the BSP's) */
	if (world_required || !h2_world_enabled())
		return;
	/* (this BSP's collision now the map's - in place before the switch's
	reconnections and the objects' first tick) */
	h2_world.collision_installed = FALSE;
	world_follow_level();
	world_collision_install(TRUE);
}

/* ---------- (cut 3c) a BSP's room counted from the MCC map alone, no
gather: what world_reserve_measure gives once the BSP is made whole, from
its section headers and its tag's size - tens of ms, not the gather's
seconds. Each cluster's and each instance definition's section info: the
vertices at 0, its resources' strip indices (type 0, primary 0x20: two
bytes each); an instance counts its definition's again. The tables and the
collision from the tag's own bytes. The coefficients and the margin are
fitted on every MCC level's BSPs (the build logs both: "room counted") */
enum
{
	ROOM_COUNT_STRIP_RESOURCE = 0x20,
	ROOM_COUNT_DECORATOR_SETS = 64
};

struct room_count_section
{
	unsigned long vertices, indices;
};

H2_ALIGN static void room_count_section(struct h2_cache *cache, unsigned long geometry, struct room_count_section *out)
{
	unsigned char const *g = h2_cache_at(cache, geometry, 0x40);
	long count, i;
	unsigned long resources;

	out->vertices = out->indices = 0;
	if (!g || !read32(g + 0x2C))
		return;
	out->vertices = (unsigned short)read16(g);
	if (!h2_cache_block(cache, geometry + 0x38, 16, &count, &resources))
		return;
	for (i = 0; i < count; i++)
	{
		unsigned char const *r = h2_cache_at(cache, resources + 16 * i, 16);

		if (r && r[0] == 0 && (unsigned short)read16(r + 2) == ROOM_COUNT_STRIP_RESOURCE)
			out->indices += read32(r + 8) / 2;
	}
}

/* the room counted, bytes (0: the BSP not there); features (may be NULL),
H2_ROOM_COUNT_FEATURES words: the tag's bytes, the clusters' vertices and
strip indices, the instances' vertices and strip indices, the clusters, the
instances, the decorators' vertices (their placements at their set's
biggest model), the BSP's materials, the decorators' vertices at their set's
mean model */
unsigned long h2_world_bsp_room_count(struct h2_cache *cache, long bsp, unsigned long *features)
{
	struct h2_cache_tag tag;
	unsigned long clusters, definitions, instances, grids;
	long cluster_count = 0, definition_count = 0, instance_count = 0, grid_count = 0, i;
	unsigned long f[H2_ROOM_COUNT_FEATURES];
	struct room_count_section *defined = NULL;
	unsigned long long room;
	unsigned long set_biggest[ROOM_COUNT_DECORATOR_SETS], set_mean[ROOM_COUNT_DECORATOR_SETS];
	long set_count = 0;

	memset(f, 0, sizeof(f));
	if (features)
		memset(features, 0, sizeof(f));
	if (!cache || bsp < 0 || bsp >= h2_cache_structure_count(cache) ||
		!h2_cache_tag_get(cache, h2_cache_structure_tag(cache, bsp), &tag))
	{
		return 0;
	}
	f[0] = tag.size;
	if (h2_cache_block(cache, tag.address + H2_SBSP_CLUSTERS, H2_CLUSTER_SIZE, &cluster_count, &clusters))
	{
		for (i = 0; i < cluster_count; i++)
		{
			struct room_count_section c;

			room_count_section(cache, clusters + H2_CLUSTER_SIZE * i, &c);
			f[1] += c.vertices;
			f[2] += c.indices;
		}
	}
	if (h2_cache_block(cache, tag.address + H2_SBSP_INSTANCE_DEFINITIONS, H2_INSTANCE_DEFINITION_SIZE, &definition_count,
			&definitions) &&
		h2_cache_block(cache, tag.address + H2_SBSP_INSTANCES, H2_INSTANCE_SIZE, &instance_count, &instances) &&
		definition_count > 0 && (defined = calloc((size_t)definition_count, sizeof(*defined))) != NULL)
	{
		for (i = 0; i < definition_count; i++)
			room_count_section(cache, definitions + H2_INSTANCE_DEFINITION_SIZE * i, &defined[i]);
		for (i = 0; i < instance_count; i++)
		{
			unsigned char const *instance = h2_cache_at(cache, instances + H2_INSTANCE_SIZE * i + 0x34, 2);
			short d = instance ? read16(instance) : NONE;

			if (d >= 0 && d < definition_count)
			{
				f[3] += defined[d].vertices;
				f[4] += defined[d].indices;
			}
		}
		free(defined);
	}
	f[5] = (unsigned long)cluster_count;
	f[6] = (unsigned long)instance_count;
	/* (the decorators: their groups' placements, each its set's model's
indices as vertices, twice as indices - both faces, decorators_add: counted
at the set's biggest model) */
	{
		struct h2_cache_tag scenario;
		long palette_count = 0;
		unsigned long palette = 0;

		if (h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario) &&
			h2_cache_block(cache, scenario.address + 0x378, 8, &palette_count, &palette))
		{
			for (i = 0; i < palette_count && i < ROOM_COUNT_DECORATOR_SETS; i++)
			{
				struct h2_cache_tag set;
				long model_count = 0, m;
				unsigned long models;

				set_biggest[i] = set_mean[i] = 0;
				if (!h2_cache_tag_get(cache, h2_cache_reference(cache, palette + 8 * i), &set) ||
					!h2_cache_block(cache, set.address + 0x18, 8, &model_count, &models))
				{
					continue;
				}
				for (m = 0; m < model_count; m++)
				{
					unsigned char const *model = h2_cache_at(cache, models + 8 * m, 8);
					unsigned long indices = model ? (unsigned short)read16(model + 6) : 0;

					if (indices > set_biggest[i])
						set_biggest[i] = indices;
					set_mean[i] += indices;
				}
				if (model_count > 0)
					set_mean[i] /= (unsigned long)model_count;
				/* (a class of quads - grass, types 3 to 5: two crossed, 8
				vertices a placement; the PC tier's, the Vita's leaves them
				out - counted all the same) */
				{
					long class_count = 0, c;
					unsigned long classes;

					if (h2_cache_block(cache, set.address + 0x10, 0x14, &class_count, &classes))
					{
						for (c = 0; c < class_count; c++)
						{
							unsigned char const *class_data = h2_cache_at(cache, classes + 0x14 * c, 0x14);
							unsigned long type = class_data ? read32(class_data + 4) : 0;

							if (type >= 3 && type <= 5 && set_biggest[i] < 8)
								set_biggest[i] = 8;
							if (type >= 3 && type <= 5 && set_mean[i] < 8)
								set_mean[i] = 8;
						}
					}
				}
			}
		}
		set_count = palette_count < ROOM_COUNT_DECORATOR_SETS ? palette_count : ROOM_COUNT_DECORATOR_SETS;
	}
	if (h2_cache_block(cache, tag.address + 0x234, 0x30, &grid_count, &grids))
	{
		for (i = 0; i < grid_count; i++)
		{
			long group_count = 0, g;
			unsigned long groups;

			if (!h2_cache_block(cache, grids + 0x30 * i + 0x18, 0x18, &group_count, &groups))
				continue;
			for (g = 0; g < group_count; g++)
			{
				unsigned char const *group = h2_cache_at(cache, groups + 0x18 * g, 0x18);
				short count = group ? read16(group + 0xA) : 0;
				long set_index = group ? (signed char)group[0] : -1;

				/* (a group over 65535 vertices is left out by decorators_add:
				none counts for more) */
				if (count > 0 && set_index >= 0 && set_index < set_count)
				{
					unsigned long vertices = (unsigned long)count * set_biggest[set_index];

					f[7] += vertices < 65535 ? vertices : 65535;
					f[9] += (unsigned long)count * set_mean[set_index];
				}
			}
		}
	}
	{
		long materials = 0;
		unsigned long unused;

		if (h2_cache_block(cache, tag.address + H2_SBSP_MATERIALS, H2_MATERIAL_SIZE, &materials, &unused))
			f[8] = (unsigned long)materials;
	}
	if (features)
		memcpy(features, f, sizeof(f));
	/* (fitted: see the build's "room counted" lines) */
	room = ((unsigned long long)f[0] * H2_ROOM_COUNT_TAG_PERMILLE + (unsigned long long)(f[1] + f[3]) * H2_ROOM_COUNT_VERTEX_PERMILLE +
		(unsigned long long)(f[2] + f[4]) * H2_ROOM_COUNT_INDEX_PERMILLE + (unsigned long long)f[9] * H2_ROOM_COUNT_DECORATOR_PERMILLE) / 1000;
	room += room * H2_ROOM_COUNT_MARGIN_PERCENT / 100 + H2_ROOM_COUNT_FIXED_BYTES;
	return room > 0xFFFFFFFFULL ? 0xFFFFFFFFUL : (unsigned long)((room + 0xFFF) & ~0xFFFULL);
}

unsigned long h2_world_structure_bsp_room(struct scenario_structure_bsp_reference const *reference)
{
	boolean cache_file_is_halo2_level(void);
	unsigned long room;

	if (!reference || !cache_file_is_halo2_level())
		return 0;
	memcpy(&room, (unsigned char const *)reference + 0xC, 4);
	return room;
}

boolean h2_world_structure_bsp_read(struct scenario_structure_bsp_reference const *reference, void *bsp_base,
	unsigned long file_size, unsigned long room_size)
{
	extern long global_scenario_index;
	struct scenario *scenario;
	char const *path, *name;
	long index;

	/* (a BSP with its own world: nothing read here) */
	world_required = room_size != 0;
	if (!world_required)
		return TRUE;
	if (global_scenario_index == NONE || !world_maps_directory[0] || !bsp_base)
		return FALSE;
	scenario = tag_get('scnr', global_scenario_index);
	if (!scenario || (unsigned char const *)reference < (unsigned char const *)scenario->structure_bsp_references.address)
		return FALSE;
	index = (long)(((unsigned char const *)reference - (unsigned char const *)scenario->structure_bsp_references.address) /
		sizeof(struct scenario_structure_bsp_reference));
	if (index >= scenario->structure_bsp_references.count)
		return FALSE;
	path = tag_get_name(global_scenario_index);
	name = path ? strrchr(path, '\\') : NULL;
	name = name ? name + 1 : path;
	if (!name)
		return FALSE;
	/* (h2_world_active's following of the level: this one read) */
	snprintf(world_level, sizeof(world_level), "%s", name);
	world_bsp = index;
	/* (the BSP's memory: the stub the level holds - its header and root -
	and the room after it, written whole from the map) */
	world_load(world_maps_directory, name, index, bsp_base, file_size + room_size);
	return h2_world.loaded;
}

#include "h2_world_snapshot.inc"
