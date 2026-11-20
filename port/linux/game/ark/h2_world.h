/*
H2_WORLD.H

(port) A Halo 2 level's world - its structure BSP's render geometry and
lightmap pages - read from the MCC map as the level loads, rather than from
the level made of it (feat/h2-world-direct; HALO_H2_NATIVE_BSP=1). The
geometry is laid out the way the engine's environment shaders read it and
drawn by their passes (structure_render.c), the lightmap pages turned as the
level builder turns them, so that the picture is the made level's.
*/

#ifndef H2_WORLD_H
#define H2_WORLD_H

struct shader;
struct bitmap_data;
struct vertex_buffer;

/* a run of the world's triangles: one shader, one lightmap page */
struct h2_world_group
{
	/* the engine's shader (h2\shaders\...), found among the loaded tags */
	char path[160];
	long shader_tag;
	short atlas;
	long vertex_count;
	long triangle_count;
	/* the triangles (three vertex indices each, clockwise) */
	unsigned short *triangles;
	/* compressed environment vertices (32 bytes) and their lightmap
	vertices (8): CPU copies until the buffers are made */
	unsigned char *vertices;
	unsigned char *lightmap_vertices;
	/* (the renderer's) the two buffers, as a structure material's */
	struct vertex_buffer *buffers;
	/* (the renderer's) its triangles in the frame's dynamic triangles, made
	once a frame and drawn by every pass, as the view's surfaces' are */
	long triangles_index;
	long triangles_frame;
	/* its vertices' centre (the transparent shaders' sort) */
	float centroid[3];
	/* its triangles by cluster (sorted so), in ranges of a cluster's at most
	256 triangles: each range's cluster, first triangle, count and bounding
	sphere; the frame's visible clusters' ranges in the view are drawn */
	long range_count;
	/* (perf/zan-render) half: its box's half sizes (centre +- half holds every
	vertex), the passes' tighter cull and the fog pass's near test */
	struct h2_world_range { short cluster; long first, count; float center[3], radius, half[3]; boolean pad_visible; } *ranges;
	long visible_triangles;
	/* (the renderer's) its triangles in an index buffer of its own, drawn
	through a dynamic triangle buffer standing for it each frame */
	void *index_buffer;
	/* its vertices and triangles in the BSP's room in the tag cache (a
	level with no world of its own), read by the GPU where they are: the
	buffers point at them and own nothing */
	boolean in_room;
	/* the breakable pane its triangles are (NONE none): not drawn once
	broken, as the level's materials are not (breakable_surface_extant) */
	short breakable_surface_index;
	/* its faces take the decals laid on the world (h2_world_triangles_near):
	the structure's and instances' environment shaders' - not water, a
	transparent or overlay run, nor the decorators' grass and debris. 1 yes,
	-1 no, 0 not known yet (its shader looked up at the first ask) */
	signed char takes_decals;
};

/* HALO_H2_NATIVE_BSP=1 */
boolean h2_world_enabled(void);

/* a Halo 2 level resolved (h2_level.c): its world read from the map */
void h2_world_level_resolved(char const *maps_directory, char const *name);

/* (the map's unload, cache_files.c) the level's world let go: its buffers
and the build heap's room it held, for the next level's make */
void h2_world_unloaded(void);

struct scenario_structure_bsp_reference;

/* (cache_files.c) the room a structure BSP of a Halo 2 level built with
HALO_H2_WORLD_FROM_MAP needs after it, for its collision BSP read from the
map as it loads (its reference's unused word; 0: one with its own) */
unsigned long h2_world_structure_bsp_room(struct scenario_structure_bsp_reference const *reference);

/* (cut 3c) a structure BSP's room - what its world read from the map takes
in the tag cache, its whole BSP and its materials' vertices - counted from
the MCC map's section headers and the BSP tag's size, without gathering it
(tens of ms): an upper bound by a fitted margin, for the header's tag cache
room (0x730) of a level made without its BSPs gathered. features (may be
NULL): H2_ROOM_COUNT_FEATURES words, the counts it read (h2_world_load.inc) */
struct h2_cache;
enum
{
	H2_ROOM_COUNT_FEATURES = 10,
	/* (the fit, per mille of a byte: of the BSP tag's bytes, a section
	vertex, a strip index, a decorator vertex at its set's mean model; an
	upper fit - the least total with every BSP of the 40 MCC levels counted
	at least its room, h2world-tools/c3/fit2.py - then a margin and a fixed
	part: on those 90 BSPs the count is 1.10 to 1.6 of the room over 2 MB,
	1.28 on average; the biggest, 08a's 17.4 MB, counted 25.5 MB) */
	H2_ROOM_COUNT_TAG_PERMILLE = 1010,
	H2_ROOM_COUNT_VERTEX_PERMILLE = 12240,
	H2_ROOM_COUNT_INDEX_PERMILLE = 21810,
	H2_ROOM_COUNT_DECORATOR_PERMILLE = 32520,
	H2_ROOM_COUNT_MARGIN_PERCENT = 10,
	H2_ROOM_COUNT_FIXED_BYTES = 64 * 1024
};
unsigned long h2_world_bsp_room_count(struct h2_cache *cache, long bsp, unsigned long *features);

/* (cache_files.c, the BSP read and relocated, before its tag check) its
world read from the map: the render geometry, and the collision BSP into
room (the BSP's collision_bsp element pointed at it), so that the check
checks it as the BSP's own; FALSE if it cannot be */
boolean h2_world_structure_bsp_read(struct scenario_structure_bsp_reference const *reference, void *bsp_base,
	unsigned long file_size, unsigned long room_size);
/* whether the last BSP's world read failed for want of memory (its map's
pieces, the builder's blocks refused), not for its map */
boolean h2_world_memory_short(void);

/* a level with no world of its own (built with HALO_H2_WORLD_FROM_MAP):
its world read from the map whatever HALO_H2_NATIVE_BSP says */
boolean h2_world_required(void);

/* (scenario.c) a structure BSP loaded: its world read with it */
void h2_world_structure_bsp_loaded(void);

/* the world of the structure BSP drawn now, or NULL (the level's own) */
boolean h2_world_active(void);
long h2_world_group_count(void);
struct h2_world_group *h2_world_group_get(long index);
short h2_world_atlas_count(void);
struct bitmap_data *h2_world_atlas_bitmap(short atlas);
/* (decals.c) the world's drawn render triangles that take decals and may reach within
radius of center - the ranges' bounding spheres the test - each given to
each with its corners' positions (read where the buffers hold them, no
copy); the triangles given */
long h2_world_triangles_near(float const center[3], float radius,
	void (*each)(void *context, float const *a, float const *b, float const *c), void *context);

/* (object_lights.c) the world's solid render triangle under point within depth -
Halo 2's object light ray's hit: its height, atlas, corners' vertices (32 bytes)
and lightmap vertices (8 bytes), the place in it (point = v0 + s(v1 - v0) +
t(v2 - v0)) and its shader; FALSE none */
boolean h2_world_surface_below(float const point[3], float depth, float *height, short *atlas, void const *vertex[3],
	void const *lightmap_vertex[3], float *s, float *t, long *shader_tag);
/* an atlas's texel at u, v as 0..1 rgb (the stored value); FALSE none */
boolean h2_world_atlas_texel(short atlas, float u, float v, float rgb[3]);
/* (h2/render-accuracy) whether a triangle's lightmap coordinates (u, v each,
0..1) are a per-vertex lit instance's (h2_level.c section_vertex_lit: three
texels' centres of a 2 x 2 block) - its colour there its corners' texels
interpolated */
boolean h2_world_vertex_lit_triangle(short atlas, void const *coordinates);

#endif
