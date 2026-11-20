/*
H3_LEVEL.H

A Halo 3 level played by this engine (h3_level.c) straight from the
player's own MCC Halo 3 map (guardian.map, riverworld.map, zanzibar.map ...
with shared.map beside it), as a Halo 2 level is (h2_level.h, "a level with
no file"): nothing is written anywhere. As the level loads, its tags are
made in memory, into the tag cache - its structure BSP's render geometry
(the clusters' and the instanced geometry's) and collision, its lighting as
this engine's lightmaps, its bitmaps, its render methods as environment and
transparent shaders, its sky and ocean - with the player, his weapons and
the game's globals from the kit Halo 2's levels use (h2kit.pack, made once
per card from the player's Halo CE map). What the engine reads of the level
as a file (the BSP, the pixels, the kit's data) is served from memory and
the kit's pack by h3_level_direct_read. PC only for now (HALO_H3_LEVELS=1);
on the Vita the calls below are stubs that say no.
*/

#ifndef __H3_LEVEL_H
#define __H3_LEVEL_H

#include "cseries.h"

/* ---------- constants */

enum
{
	H3_LEVEL_WHY_SIZE = 192,
	/* the make's version (the header's 0x704: what made the level's tags) */
	H3_LEVEL_MAKE_VERSION = 12,
};

/* ---------- structures */

struct h3_level_build_result
{
	long tag_count;
	long kit_tag_count;
	long bsp_bytes;
	boolean too_big;            /* the tags and BSP more than the room */
	long tag_bytes;
	long bitmap_bytes;
	long lightmap_bytes;
	long surfaces;
	long vertices;
	long materials;
	long clusters;
	long instances;
	long instances_left_out;
	long instances_lightmapped;
	long instances_vertex_lit;    /* (instances lit by their per-vertex light, a texel block a triangle) */
	long instances_vertex_averaged; /* (a level made again to fit: instances lit by their per-vertex light averaged, a patch) */
	long object_instances;
	long model_vertices;
	long sceneries;
	long models;
	unsigned long heap_peak;
	unsigned long long microseconds;
	char why[H3_LEVEL_WHY_SIZE];
};

/* ---------- prototypes/H3_LEVEL.C: a level with no file */

/* whether Halo 3 maps are played (HALO_H3_LEVELS=1; PC only) */
boolean h3_level_direct_enabled(void);

/* (h3up) Halo 3's own folder, as Halo 2's (h2_level.h's H2_MAPS_FOLDER): the
player's MCC Halo 3 maps with their shared.map and info\ - Halo 2's MCC maps
have a shared.map of their own, so the two games' maps cannot share the maps
folder. The maps folder is looked at when a map is not here (a desktop set up
before). Looked at only with HALO_H3_LEVELS on */
#define H3_MAPS_FOLDER "d:\\halo3\\maps\\"
/* the folder Halo 3's map `name` is in: H3_MAPS_FOLDER if it is there, else
the maps folder */
char const *h3_maps_directory_of(char const *name);

/* (cache_files.c, a direct Halo 3 level made) the tag cache room it came to,
kept in memory (no file): the next prepare of that level writes it at the
header's 0x730 so the level is made at once in the tag cache it needs, not
first in the fixed one and again in the large (Guardian: 25.7 MB of tags) */
void h3_level_direct_room_remember(char const *name, unsigned long room);
/* (map_loaders.c's resolve) the level maps_directory/name.map named, to be
made as it loads: its header made for the loader's slot, path (path_size)
the MCC map's own; FALSE and why if it cannot be */
boolean h3_level_direct_prepare(char const *maps_directory, char const *name, char *path, long path_size, char *why,
	long why_size);
/* (cache_files_windows.c) the header (0x800 bytes) of the direct level
prepared for name, if one was */
boolean h3_level_direct_header(char const *name, void *header);
/* (cache_files.c, where the tag data would be read) the level's tags made
into the tag cache at tags (room bytes, linked where they lie), its header
made whole into header; FALSE and why if not */
boolean h3_level_direct_make(char const *name, unsigned char *tags, unsigned long room, void *header, char *why,
	long why_size);
/* (the cache file thread) size bytes at offset of the level's virtual file
into buffer (zeros where it holds nothing) */
boolean h3_level_direct_read(unsigned long offset, unsigned long size, void *buffer);
/* the direct level let go if it is the one named (NULL or "": whichever) */
void h3_level_direct_unload_named(char const *name);
/* whether the level loaded now is a direct Halo 3 one */
boolean h3_level_direct_loaded(void);
/* whether that level is a Halo 3: ODST map's */
boolean h3_level_direct_odst(void);
/* whether the map being made (or made last) is a Halo 3: ODST one */
boolean h3_level_odst(void);

/* a made level's Halo 3 water (shader_water, the map's own values): its
options (water_shading.fx's categories), parameters and animation periods,
and the maps this engine's water shader has no slot for. The shader's own
slots hold the wave slope sheet (ripple map: the slope array's slices, 4
across), the water colour texture (base map) and the environment cube
(reflection map) */
struct h3_level_water
{
	long shader_tag;                 /* its shader's tag index in the made level */
	long foam_tag, shape_tag;        /* bitmap tag indices, NONE */
	int waveshape, watercolor, reflection, refraction, bankalpha, global_shape, foam;
	float slices;
	float wave_xform[4], slope_xform[4], foam_xform[4], foam_detail_xform[4];   /* scale x y, offset x y */
	float time_warp_period, time_warp_aux_period;                              /* seconds a cycle; 0 still */
	float slope_scroll[2], foam_scroll[2], foam_detail_scroll[2];              /* repeats a second */
	float slope_range[2], detail_slope_scale[3], detail_slope_steepness;
	float wave_height, wave_height_aux, wave_visual_damping_distance, minimal_wave_disturbance;
	float water_color_pure[3], watercolor_coefficient, water_diffuse[3], water_murkiness;
	float fresnel_coefficient, reflection_coefficient, sunspot_cut, shadow_intensity_mark;
	float refraction_texcoord_shift, refraction_extinct_distance, refraction_depth_dominant_ratio;
	float bankalpha_infuence_depth, globalshape_infuence_depth, foam_height, foam_pow;
	float base_map[6];               /* the colour and shape maps' coordinates of a world x y: u = [0] x + [1] y + [2], v = [3] x + [4] y + [5] */
	boolean base_map_fitted;
	float exposure;
	float light[3];                  /* the lightmap's light on it (the lightprobe's first coefficient, its water vertices' mean) */
	float cube_mips;                 /* its environment map's mip levels under the first (the reflection's most blur) */
};

/* the water of a made level's shader tag, NULL if it is not one */
const struct h3_level_water *h3_level_water(long shader_tag_index);
/* (scene_packs.c's clear of the tag cache's free room before the BSP is
read) the range of the tag cache a made level's BSP already lies in - served
from there when the loader reads it -: FALSE if none */
boolean h3_level_direct_in_place_range(unsigned char **first, unsigned char **end);

/* (h3vita) the level's image codecs, for the maps h3_spartan.c makes of a
Halo 3 model's (the sandbox's weapons, vehicles and arms): an RGBA image
with power-of-two sides encoded with its mips - DXT5 if any texel is not
opaque, else DXT1 - the bytes in *out (the caller frees), its format (14 or
16) and mip count; rgba is worked in place */
boolean h3_level_image_encode(unsigned char *rgba, unsigned long w, unsigned long h, unsigned char **out,
	unsigned long *out_size, unsigned short *format, unsigned short *mipmaps);
/* a normal map (RGBA: x, y about 128) as this engine's P8 bump with its mips,
swizzled (format 17; the caller frees); NULL if the sides are not powers of two */
unsigned char *h3_level_bump_p8_make(unsigned char const *rgba, unsigned long w, unsigned long h, unsigned long *size,
	unsigned short *mipmaps);

#endif
