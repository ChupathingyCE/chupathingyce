/*
HALO_PORT_CAPACITY.H

Memory capacity of the native builds (Windows, Linux, Android), sized for the
session limits in halo_port_limits.h, which includes this file. The Xbox
sizes are given in parentheses below.

Every machine in a session must be built with the same values: the
distributed netcode names objects and players by their datum index, the
same on every machine (port/linux/game/network_objects.c tracks
MAXIMUM_TRACKED_OBJECTS = HALO_PORT_MAXIMUM_OBJECTS_PER_MAP objects, and a
client's own objects take the upper half of the object array).
*/

#ifndef __HALO_PORT_CAPACITY_H
#define __HALO_PORT_CAPACITY_H

/* ---------- game state

The Xbox game state is 0x345000 bytes at 0x80061000 and ends where the tag
cache begins (0x803A6000). Cache files are linked to that tag cache address,
so the game state cannot grow in place. The native builds put a 16 MB game
state above the tag cache (which ends at 0x819A6000), inside the Xbox memory
window (0x80000000 up, port/linux/src/platform.h) and below everything
the window hands out top-down (texture and sound caches, Direct3D resources).

The CPU part holds about 13.6 MB of pools at the sizes below (the Xbox pools
fill 3,165,260 of its 0x305000 bytes); the GPU part holds only the decal
vertices, as on the Xbox. */

#define HALO_PORT_GAME_STATE_BASE_ADDRESS 0x81A00000 /* (0x80061000) */
#define HALO_PORT_GAME_STATE_CPU_SIZE 0xFC0000 /* (0x305000) */
#define HALO_PORT_GAME_STATE_GPU_SIZE 0x40000 /* (0x40000) */
#define HALO_PORT_GAME_STATE_SIZE (HALO_PORT_GAME_STATE_CPU_SIZE+HALO_PORT_GAME_STATE_GPU_SIZE)

/* ---------- texture cache

The texture cache (cache/xbox_texture_cache.c) holds the pixels of the
bitmaps the renderer draws, in 16 KB pages, and is allocated top-down in
the Xbox memory window (cache/physical_memory_map.c). When a frame's
bitmaps do not fit, the cache cannot load the rest ("YOU GOT STABBED" in
debug.txt) and the surfaces drawn with them show whatever is at their
pixels' addresses. The Xbox's maps fit the Xbox's 22 MB. Halo PC's maps
keep their bump maps in 32 bits a pixel, not the Xbox's 8-bit palettized
ones, and community maps draw many large ones at once: Portent's busiest
frames draw 23 MB, Foundation's 66 MB.

The desktop builds (Linux, macOS and Windows, 32-bit and 64-bit) are not
held to the Xbox's memory: their window is 512 MB (port/linux/src/platform.h) and
their cache 128 MB, about twice Foundation's busiest frame; with it the
window still has about 300 MB free. Nothing of the cache's size reaches
the network or the game state. Android's window stays the development
kit's 128 MB (its guest image is linked above it) and it plays no Halo PC
maps yet, so its cache stays the Xbox's, as the console's does
(HALO_XBOX_CONSOLE, the Xbox builds). */

#if !defined(HALO_ANDROID) && !defined(HALO_XBOX_CONSOLE)
#define HALO_PORT_TEXTURE_CACHE_SIZE 0x8000000 /* (0x1600000) */
#else
#define HALO_PORT_TEXTURE_CACHE_SIZE 0x1600000 /* (0x1600000) */
#endif

/* ---------- structure rendering

The structure BSP's surfaces (its triangles) drawn in a frame
(render/render.h: past them, the farther are not drawn) and the dynamic
triangles a frame's draws take, the BSP's among them (rasterizer.h). The
Xbox's maps fit the Xbox's 16384 surfaces; big community maps draw more
(Halo PC's own engine stopped at 16384 too, and the PC community's tools
for those maps raise it to 32767 surfaces with a 65536-triangle buffer).
The desktop builds draw up to 32767 (the count is a short), with twice the
dynamic triangles, so the BSP's do not leave the rest of a frame's draws
none. Only what is drawn changes: nothing reaches the network or the game
state. Android and the Xbox builds keep the Xbox's. */

#if !defined(HALO_ANDROID) && !defined(HALO_XBOX_CONSOLE)
#define HALO_PORT_MAXIMUM_RENDERED_ENVIRONMENT_SURFACES 32767 /* (16384) */
#define HALO_PORT_MAXIMUM_DYNAMIC_TRIANGLES 65536 /* (32768) */
#else
#define HALO_PORT_MAXIMUM_RENDERED_ENVIRONMENT_SURFACES 16384 /* (16384) */
#define HALO_PORT_MAXIMUM_DYNAMIC_TRIANGLES 32768 /* (32768) */
#endif

/* ---------- objects */

#define HALO_PORT_MAXIMUM_OBJECTS_PER_MAP 8192 /* (2048) */
#define HALO_PORT_OBJECT_MEMORY_POOL_SIZE 0x800000 /* (0x100000) */
/* each of the two reference lists of every cluster partition (collideable
objects, noncollideable objects, lights) */
#define HALO_PORT_MAXIMUM_CLUSTER_REFERENCES 8192 /* (2048) */
#define HALO_PORT_MAXIMUM_RENDERED_OBJECTS 1024 /* (256) */
#define HALO_PORT_MAXIMUM_CACHED_OBJECT_RENDER_STATES 1024 /* (256) */
/* objects one explosion can damage */
#define HALO_PORT_MAXIMUM_AREA_OF_EFFECT_OBJECTS 256 /* (64) */
/* object references shared by all script object lists */
#define HALO_PORT_MAXIMUM_LISTED_OBJECTS_PER_MAP 1024 /* (128) */

/* ---------- effects, particles, lights and sounds */

#define HALO_PORT_MAXIMUM_EFFECTS 2048 /* (256) */
#define HALO_PORT_MAXIMUM_EFFECT_LOCATIONS 4096 /* (512) */
#define HALO_PORT_MAXIMUM_PARTICLES 8192 /* (1024) */
#define HALO_PORT_MAXIMUM_PARTICLE_SYSTEMS 256 /* (64) */
#define HALO_PORT_MAXIMUM_SYSTEM_PARTICLES 4096 /* (512) */
#define HALO_PORT_MAXIMUM_CONTRAILS 1024 /* (256) */
#define HALO_PORT_MAXIMUM_CONTRAIL_POINTS 8192 /* (1024) */
#define HALO_PORT_MAXIMUM_LIGHTS_PER_MAP 4096 /* (896) */
#define HALO_PORT_MAXIMUM_GAME_LOOPING_SOUNDS 4096 /* (1024) */

#endif /* __HALO_PORT_CAPACITY_H */
