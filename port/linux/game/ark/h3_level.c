/*
H3_LEVEL.C

A Halo 3 level made in memory as it loads (h3_level.h: no level file), from
the player's MCC Halo 3 map read by h3_cache.c.

What is read, and how (the layouts read against the owner's zanzibar.map;
field names from Assembly's Halo3MCC plugins, read as a reference):
- the structure BSP (sbsp): collision materials at 0x28 (0x18 each: a
  render method first), each collision leaf's cluster at 0x34 (a byte
  each), world bounds at 0x40, cluster portals at 0x7C (0x28 each: back and
  front cluster, plane, centroid, radius, flags at 0x18, vertices at 0x1C),
  clusters at 0xC4 (0x118 each: bounds first, portals at 0x3C, mesh at
  0xD8), materials at 0xD0 (0x24 each: a render method first), the
  instanced geometry's instances at 0x1C0 (0x78 each: scale, forward,
  left, up, position, definition at 0x34, name at 0x54), and the resource
  of its collision (0x38C);
- that resource's control: its root's first block the collision BSPs (the
  first one the level's), its third the instanced geometry definitions
  (0xC4 each: checksum, bounding sphere, then their collision BSP at 0x14);
  a collision BSP is eight blocks (3D nodes of 8 bytes, planes 16, leaves 8,
  2D references 4, 2D nodes 16, surfaces 12, edges 12, vertices 16), its
  elements in the resource's page: Halo 2's layout (h2_level.c) but for its
  8-byte leaves and 12-byte surfaces;
- the lightmap BSP (Lbsp): its lightprobe texture (0xE0, a 1024 x 1024
  array of DXT5 slices: the first the light's colour) and its intensity
  (0xF0: the first slice its brightness), the per-vertex lighting (0x100,
  0x10 each: the vertex buffer at 0xC, five words a vertex, the first the
  light's colour), each instance's (0x118, 8 each: the per-vertex block at
  2), the meshes (0x134, 0x4C each: parts at 0 - 0x10 each: material,
  index start and count, type, flags -, vertex buffers at 0x18, index
  buffer at 0x28), their compression (0x140, 0x2C each: the bounds of the
  16-bit positions and texture coordinates) and the geometry's resource
  (0x1AC): vertex buffers (count, declaration, stride, size, data at 0x14)
  and index buffers (type: 3 lists, 5 strips; size; data at 0x10). The
  clusters' meshes are of declaration 1 (floats: position, texture
  coordinates), the instanced geometry's of declaration 2 (16-bit fractions
  of the mesh's bounds, texture coordinates at 8), their lightmap
  coordinates (the clusters') two 16-bit fractions of the lightprobe
  texture;
- render methods: their postprocess definition (0x28, 0x8C each: the
  template at 0, textures at 0x10 - 0x1C each: the bitmap at 0, the
  transform's float constant at 0x16 -, float constants at 0x1C, the blend
  mode at 0x68); the template's (rmt2) texture names at 0x6C (string ids)
  say which is which (base_map, detail_map, alpha_test_map ...);
- bitmaps: their images at 0x60 (0x30 each: width, height, depth, type,
  format, flags, mipmap count at 0x14), their resources at 0x8C: the
  control's sizes of the required (0) and optional (0x14) data, the top mip
  in the optional page when there is one, the others in the required one;
- models (mode): nodes at 0x30, materials at 0x48, meshes at 0x68,
  compression at 0x74, the geometry's resource at 0xE0.

The lighting is baked as this engine's lightmaps, no more (the owner's
rule: Halo CE's lightmap pass on the Vita): the lightprobe texture's colour
slice times its intensity for the clusters, each instance's per-vertex
lighting averaged into a small patch of its own, the rest neutral.
*/


/* ---------- headers */

#include "cseries.h"
#include "cseries_windows.h"
#include "h3_level.h"
#include "h3_cache.h"
#include "h2_level.h"
#include "h3_sandbox.h"
#include "h3_frame.h"
#include "h3_object_light.h"
#include "h3_animation.h"
#include "h2_animation.h"
#include "h2_biped.h"
#include "h2_lighting.h"
#include "h2_materials.h"
#include "scene_pack.h"
#include "tag_schema.h"
#include "cache/physical_memory_map.h"

#include "tag_relocate.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#undef malloc
#undef free
#undef realloc
#undef calloc

/* (h3up: the Vita tier - what a Halo 3 level's make allocates is the
builder's own heap's, user RAM outside newlib's 48 MB C heap, as Halo 2's
make's: h2_build_heap.h; the desktop's PC tier as before) */
#define H2_BUILD_HEAP_WRAP
#include "h2_build_heap.h"

#define H3_ALIGN __attribute__((force_align_arg_pointer))

void platform_log(char const *format, ...);
unsigned long long vita_host_time_us(void);
void tag_validate_pack_allow_globals(boolean allowed);

/* ---------- constants */

/* Halo 3: ODST (desktop). An MCC ODST map is read by this same reader: its
header, tag index, zone and play tags are Halo 3's. What differs, read
against the ODST kit's own tag layouts (the layout strings its tag files
carry - a private reference, as the Halo 3 kit's are) and checked against
the maps:
- the structure BSP has one surface block where MCC Halo 3 has two ("large
  structure surfaces" alone): every field from the cluster portals on lies
  12 bytes lower (portals 0x70, clusters 0xB8, materials 0xC4, instances
  0x1B4, decorator sets 0x1C0, the resource 0x380);
- its resource's level collision is the LARGE collision BSP, the root's
  second block (the first is empty): h3_collision_large_sizes.
An ODST map is known by its tag groups (achievements, 'achi'; AI globals,
'aigl': Halo 3 has neither). HALO_H3_ODST=0: read as Halo 3 (the level
refused, as before) */
static boolean h3_odst;

#define H3_SBSP_FIELD(h3) ((unsigned long)(h3) - (h3_odst ? 0xC : 0))
#define H3_SBSP_CLUSTER_PORTALS H3_SBSP_FIELD(0x7C)
#define H3_SBSP_CLUSTERS H3_SBSP_FIELD(0xC4)
#define H3_SBSP_MATERIALS H3_SBSP_FIELD(0xD0)
#define H3_SBSP_INSTANCES H3_SBSP_FIELD(0x1C0)
#define H3_SBSP_DECORATOR_SETS H3_SBSP_FIELD(0x1CC)
#define H3_SBSP_DECORATOR_MESHES H3_SBSP_FIELD(0x1DC)
#define H3_SBSP_DECORATOR_COMPRESSION H3_SBSP_FIELD(0x1E8)
#define H3_SBSP_DECORATOR_RESOURCE H3_SBSP_FIELD(0x254)
#define H3_SBSP_COLLISION_RESOURCE H3_SBSP_FIELD(0x38C)
/* (an instanced geometry definition: ODST's 0xB8 bytes - the kit's
structure_bsp_instanced_geometry_definition_block -, MCC Halo 3's 0xC4;
the same through the collision BSP at 0x14) */
#define H3_DEFINITION_SIZE (h3_odst ? 0xB8UL : 0xC4UL)

/* (a scenario root field: ODST's scenario has blocks Halo 3's has not - its
PDA definitions and campaign players before the skies, ARG devices before
the controls, squad patrols before the character palette -: an MCC Halo 3
offset from the skies on lies 0x1C higher, from the controls on 0x34, from
the character palette on 0x40. Read against both games' maps block by block
(the palettes' groups, the element sizes); a player starting location is
0x1C bytes, the insertion point it belongs to at 0x14) */
#define H3_SCNR(h3) ((unsigned long)(h3) + (!h3_odst || (h3) < 0x34 ? 0 : (h3) < 0x168 ? 0x1C : (h3) < 0x39C ? 0x34 : 0x40))
#define H3_PLAYER_START_SIZE (h3_odst ? 0x1CUL : 0x18UL)
/* (a render method's postprocess texture: ODST's 0x18 bytes - the bitmap,
its index, the address and filter modes, the extern mode, then the
transform's float constant at 0x15 -, MCC Halo 3's 0x1C with it at 0x16.
Read as Halo 3's, an ODST shader's first texture alone was right: its detail
and bump maps and every terrain material after the blend map were another
texture's bytes) */
#define H3_TEXTURE_SIZE (h3_odst ? 0x18UL : 0x1CUL)
#define H3_TEXTURE_TRANSFORM (h3_odst ? 0x15 : 0x16)

enum
{
	/* the structure BSP (sbsp) */
	H3_SBSP_COLLISION_MATERIALS = 0x28,
	H3_SBSP_LEAF_CLUSTERS = 0x34,
	H3_SBSP_WORLD_BOUNDS = 0x40,
	/* (H3_SBSP_CLUSTER_PORTALS and the fields after it: macros below, ODST's lie 12 bytes lower) */
	H3_COLLISION_MATERIAL_SIZE = 0x18,
	H3_PORTAL_SIZE = 0x28,
	H3_CLUSTER_SIZE = 0x118,
	H3_MATERIAL_SIZE = 0x24,
	H3_INSTANCE_SIZE = 0x78,

	/* the lightmap BSP (Lbsp) */
	H3_LBSP_LIGHTPROBE_TEXTURE = 0xE0,
	H3_LBSP_INTENSITY_TEXTURE = 0xF0,
	H3_LBSP_PER_VERTEX = 0x100,
	H3_LBSP_CLUSTERS = 0x10C,
	H3_LBSP_INSTANCES = 0x118,
	H3_LBSP_MESHES = 0x134,
	H3_LBSP_COMPRESSION = 0x140,
	H3_LBSP_INSTANCE_TEXCOORDS = 0x1A0,
	H3_LBSP_GEOMETRY_RESOURCE = 0x1AC,
	H3_MESH_SIZE = 0x4C,
	H3_PART_SIZE = 0x10,
	H3_COMPRESSION_SIZE = 0x2C,

	/* models (mode) */
	H3_MODE_NODES = 0x30,
	H3_MODE_MATERIALS = 0x48,
	H3_MODE_MESHES = 0x68,
	H3_MODE_COMPRESSION = 0x74,
	H3_MODE_RESOURCE = 0xE0,

	/* the scenario (scnr) */
	H3_SCNR_SKIES = 0x34,
	H3_SCNR_SCENERY = 0xB4,
	H3_SCNR_SCENERY_PALETTE = 0xC0,
	H3_SCNR_VEHICLES = 0xE4,
	H3_SCNR_VEHICLE_PALETTE = 0xF0,
	H3_SCNR_EQUIPMENT = 0xFC,
	H3_SCNR_EQUIPMENT_PALETTE = 0x108,
	H3_SCNR_WEAPONS = 0x114,
	H3_SCNR_WEAPON_PALETTE = 0x120,
	H3_SCNR_MACHINES = 0x138,
	H3_SCNR_MACHINE_PALETTE = 0x144,
	H3_SCNR_PLAYER_STARTS = 0x24C,

	/* this engine's (the Xbox cache's) */
	XBOX_TAG_HEADER_SIZE = 0x24,
	XBOX_TAG_INSTANCE_SIZE = 0x20,
	CACHE_HEADER_SIZE = 0x800,
	MAXIMUM_MATERIAL_VERTICES = 32000,
	MAXIMUM_MATERIAL_SURFACES = 20000,
	MAXIMUM_SUBCLUSTER_SURFACES = 128,
	LIGHTMAP_ATLAS = 1024,
	COMPRESSED_VERTEX_SIZE = 32,
	COMPRESSED_LIGHTMAP_VERTEX_SIZE = 8,
	MAXIMUM_SHADERS = 1024,
	MAXIMUM_ATLASES = 8,
	MAXIMUM_OBJECT_PALETTE = 100,
	MAXIMUM_PARTS_PER_GEOMETRY = 32,      /* a model geometry's parts, a model's shaders and regions */
	/* the H3 lightprobe atlas, kept at its size: 8 slices (two for each of
	the four spherical harmonics' coefficients), its intensity's 2 */
	H3_LIGHTPROBE_SIZE = 1024,
	LIGHTPROBE_KEPT = 1024,
	H3_LIGHTPROBE_SLICES = 8,
	H3_INTENSITY_SLICES = 2,
	/* the vertices the BSP may hold (the tag cache: about 40 bytes each)
	and its surfaces (this engine's most: the PC tier's 524288 a structure
	BSP, halo_port_capacity.h - the Xbox's 131072 left out 16 to 340
	instances of nine levels, their geometry missing: Longshore's BSP is
	361236 surfaces, 502917 vertices with all of them) */
	DEFAULT_VERTEX_BUDGET = 900000,
	MAXIMUM_BSP_SURFACES = HALO_PORT_MAXIMUM_SURFACES_PER_STRUCTURE,
	DEFAULT_SURFACE_BUDGET = HALO_PORT_MAXIMUM_SURFACES_PER_STRUCTURE - 3072,
};

/* ---------- structures */

struct bytes
{
	unsigned char *data;
	unsigned long size;
	unsigned long capacity;
	boolean failed;
	/* (memory not its own - the tag cache the tags are made into: never
	grown past its capacity, never freed) */
	boolean fixed;
};

/* a resource's vertex and index buffers: where they are in its required
page and, once read, their bytes */
struct vertex_buffer
{
	short declaration;
	short stride;
	long count;
	unsigned long offset;
	unsigned long size;
	unsigned char *data;
	float light[3];          /* (per-vertex lighting's) its average */
	boolean light_done;
	float *sh;               /* (per-vertex lighting) each vertex's light: the dominant light's intensity, then the four coefficients, RGB each (15 floats) */
};

struct index_buffer
{
	long type;
	unsigned long offset;
	unsigned long size;
	unsigned char *data;
};

struct geometry
{
	struct h3_resource resource;
	long vertex_buffer_count;
	struct vertex_buffer *vertex_buffers;
	long index_buffer_count;
	struct index_buffer *index_buffers;
	unsigned long meshes;
	long mesh_count;
	unsigned long compression;
	long compression_count;
};

/* a mesh decoded: positions, texture coordinates, packed normal, binormal
and tangent, lightmap coordinates; its triangles as a list; its parts */
struct section
{
	long vertex_count;
	float *positions;
	float *coordinates;
	unsigned long *packed;
	float *lightmap_coordinates;
	float *colours;              /* (a mesh drawn by its per-vertex colour - a sky's: its vertex buffer of declaration 14, three reals a vertex) NULL none */
	long index_count;
	unsigned short *indices;
	long part_count;
	struct section_part
	{
		short type;
		short material;
		long first;
		long count;
	} *parts;
};

struct shader
{
	long h3_tag;
	short h3_water;              /* its Halo 3 water (h3_waters') + 1; 0 none */
	long bitmap;                /* base map */
	long detail;                /* primary detail map, or NONE */
	long detail2;               /* secondary detail map, or NONE */
	float base_scale[2];        /* the base map's (baked into the texture coordinates) */
	float detail_scale;
	float detail2_scale;
	unsigned long group;        /* 'senv' or 'schi' */
	boolean alpha_tested;
	boolean water;
	boolean scrolled;
	boolean dome;
	boolean blended;            /* this engine's blended type: base map alpha picks the detail */
	long ripple;                /* (swat) its ripple map, reflection cube */
	long reflection;
	short blend;
	boolean edge_fade;          /* (a halogram) faded edge-on: this engine's framebuffer fade */
	boolean rendered;
	char path[256];
	/* (Halo 3's material on this engine's environment shader: the Halo 2
	extension's passes - h2_materials.h) */
	long bump;                  /* its bump map (mode 7: this engine's P8 bump), NONE none */
	float bump_scale[2];        /* over the base map's */
	float detail_aspect;        /* the primary detail's v scale over its u scale (1 the same) */
	unsigned long specular_color;   /* X8R8G8B8, 0 none: its cook-torrance's analytical highlight */
	unsigned char lightmap_type;    /* _h2_lightmap_type_*: its highlight's curve */
	long terrain_bumps[4];      /* (a terrain) its materials' bump maps: the most used one's its bump */
	float terrain_bump_scales[4][2];
};

struct bitmap
{
	long h3_tag;
	char path[256];
	unsigned short width, height, mipmaps, format;
	long page;
	unsigned long offset, size;  /* the pixels wanted in the page */
	unsigned long file_offset;   /* in the cache file */
	unsigned long file_size;
	unsigned short tint;         /* (water's) its colours this one, R5G6B5; 0 none */
	unsigned char alpha;         /* (water's) its alpha this one; 0 its own */
	boolean written;
	boolean convert;             /* made again: decoded, resized, encoded */
	boolean generated;           /* made here (the sky's gradient) */
	short mode;                  /* 0 as it is; 1 a detail (grey, its mean a half); 2 read and kept, not written */
	unsigned long tint_key;      /* (h3/light-1) 0 none; its material's tint (bitmap_tint_wanted): a map a tint */
	float material_tint[3];      /* its albedo color times its diffuse coefficient (linear light) */
	unsigned char *kept;         /* (mode 2) its RGBA at its made size */
	float mean[4];               /* its texels' mean colour and lightness (made ones) */
	boolean composite;           /* a terrain's base map made of its materials (bitmap_composite_write) */
	long sources[6];             /* (composite) its overlay, blend map, materials' textures */
	long scale_source;           /* (mode 3) the detail whose mean lightness scales it */
	short type;                  /* 0 2D, 2 a cube (the water's reflection) */
	boolean swizzled;            /* its pixels swizzled already */
	short generated_kind;        /* (generated) 0 the sky's gradient, 1 the water's reflection cube */
	float source_scales[4];
	long detail_sources[4];
	boolean needs_alpha;         /* a shader of it draws its alpha (alpha tested, transparent, water, blended) */
	short force_alpha;           /* its alpha made this (0: its own): water made a colour */
	boolean keep;                /* (mode 1) its pixels kept too, for a terrain's composite */
	boolean additive;            /* a sky's additive cloud: its colour times its alpha, dimmed */
	boolean white;               /* a cloud's mask: its colour made a cloud's white, its alpha kept */
	boolean sprite;              /* a detail object collection's: its sequence a sprite of all of it */      /* (composite, no overlay) its materials' own detail maps: their mean colours only */
	short shader;
	unsigned short made_width, made_height;
	unsigned char curve;         /* its image's curve (0x15): 0 unknown, 1 xRGB, 2 gamma 2, 3 linear, 5 sRGB */
	boolean not_colour;          /* (a terrain's blend map) weights, not colours: no curve */
	boolean product;             /* a render method's albedo made one map (bitmap_product_write): sources base, detail, detail 2; source_scales their scales over the base's; product_option its albedo option */
	short product_option;
	float product_tint[3];       /* (product options 100, 101: a halogram's self-illumination) its colour times its intensity */
	boolean hazed;               /* a sky material's map: held (held_rgba) until its haze is known, then written (sky_haze_write) */
	unsigned char *held_rgba;    /* (hazed) its RGBA at its made size, drawn colours */
	double haze_sum[7];          /* (hazed) its sky vertices' extinction, inscatter (linear), and their count */
	boolean cloud;               /* (hazed, mode 5) a sky's cloud held as it is: drawn by its vertices' colours through the atmosphere if they have them (haze_coloured), else brightened as before (sky_haze_write) */
	boolean haze_coloured;       /* (hazed) its sky vertices have colours: haze_sum's extinction is times them */
};

struct material
{
	short shader;
	short atlas;
	struct bytes vertices;
	struct bytes lightmap_vertices;
	struct bytes triangles;
	struct bytes clusters;
	struct bytes bounds;
	long vertex_count;
	long triangle_count;
	double centroid[3];
};

struct atlas
{
	unsigned char *rgb;
	long x, y, row;
	/* (per-vertex lighting's texels, linear light as Halo 3's, encoded once
	the exposure is known: lightprobe_bake) each texel written, in order - a
	texel written again takes the later value, as when they were a 12 MB
	array of the whole atlas's texels (h3vita: an eighth of them are
	written, 2 MB) */
	struct atlas_pending_light *pending;
	long pending_count, pending_capacity;
};

struct atlas_pending_light
{
	long index;
	float light[3];
};

struct placed_page
{
	short atlas;
	long x, y, w, h;
	boolean whole;      /* the H3 lightprobe atlas: lightmap coordinates used */
};

struct model_part_made
{
	short shader;
	long vertex_count;
	long triangle_count;
	unsigned char *vertices;
	unsigned long vertex_bytes;
	unsigned char *strip;
	unsigned long strip_bytes;
	float centroid[3];
};

/* an instanced geometry definition made a model */
struct model_made
{
	long part_count;
	struct model_part_made *parts;
	float scale;
	float radius;
};

/* an object: a definition at a scale (hundredths), its slot in the
palettes (NONE: none), its model (NONE: drawn in the BSP) */
struct object_key
{
	short definition;
	long scale;
	long uses;
	long slot;
	long model;
	float radius;
	/* its bounding sphere's centre (this engine's object's bounding
	offset) and whether a placement of it would not be placed as scenery
	(object_types_place_objects: its origin and that centre both outside
	the level's leaves) */
	float centre[3];
	boolean outside;
	long prop;               /* (a scenario object's: its prop, else NONE) */
};

/* where the tags written by objects_write are */
struct tag_layout
{
	long shader_first;
	long soso_first;
	long model_first;
	long object_first;
	long *slot_collision;
	short *shader_soso;
	unsigned long kit_offset;
	unsigned long kit_size;
	long gear_object;          /* the gear's machine tag, NONE */
	long gear_graph;           /* its animation graph's */
};

struct collision_bsp
{
	long counts[8];
	unsigned char *arrays[8];
	boolean large;   /* ODST's large collision BSP: 32-bit indices (h3_collision_large_sizes) */
};

struct stream;

/* a Halo 3 atmosphere setting (sky_atm_parameters' atmosphere settings,
0xA4 each - field names from the kit's own tag definition: flags, name,
the sun's pitch and heading, its colour and intensity, sea level, the
Rayleigh and Mie height scales and multipliers, the sun phase function,
de-saturation, distance bias, maximum fog thickness, beta_m, beta_p and
their theta prefixes) as atmosphere.fx's compute_scattering takes it */
struct atmosphere
{
	boolean on;
	char name[32];
	float sun[3];               /* toward the sun */
	float sun_intensity[3];     /* colour times intensity */
	float rayleigh[3], mie[3];  /* beta_m, beta_p: the totals */
	float rayleigh_theta[3], mie_theta[3];
	float g;                    /* the Henyey-Greenstein constant (sun phase function) */
	float sea_level, rayleigh_height, mie_height, distance_bias, maximum_thickness;
};

enum { SKY_DOME_SIDE = 256 };

struct build
{
	struct h3_cache *cache;
	struct h3_level_build_result *result;
	struct stream *stream;
	unsigned long sbsp;
	unsigned long lbsp;
	unsigned long scenario;
	/* (h3_sandbox.c) the sandbox's models' parts: buffer entries after the
	level's own - their count, and (objects_write) the tables written and the
	first entry free past the level's */
	long sandbox_buffer_count;
	unsigned long vertex_table, index_table;
	long buffer_next;

	long material_count;
	short *material_shader;
	struct shader shaders[MAXIMUM_SHADERS];
	long shader_count;
	struct bitmap *bitmaps;
	long bitmap_count;
	unsigned long bitmap_planned;   /* (their bytes as planned: bitmap_for) */

	struct material *materials;
	long ce_material_count;
	long ce_material_capacity;

	struct atlas atlases[MAXIMUM_ATLASES];
	short atlas_count;
	struct placed_page lightprobe;
	struct placed_page neutral;
	boolean neutral_made;
	struct placed_page bright;
	boolean bright_made;
	unsigned char *lightprobe_slices;   /* the 8 slices, as read */
	unsigned char *intensity_slices;    /* the 2 slices, as read */
	float *lightprobe_normals;   /* LIGHTPROBE_KEPT^2 * 3: each texel's surface's normal, 0 none */
	float lightprobe_ranges[5];  /* the Lbsp's compression: each coefficient's, then the intensity's */
	float lightmap_scale;
	float vertex_light_scale;
	boolean exposure_set;        /* HALO_H3_LIGHTMAP_SCALE given: no auto exposure */
	float exposure;              /* the level's (lightprobe_bake) */
	struct atmosphere atmosphere_level, atmosphere_sky;
	double sky_extinction_sum;   /* (sky_add, h3/pc-acc) the sky setting's extinction to the sky model's vertices, rgb's mean, summed */
	long sky_extinction_count;
	float view_point[3];         /* (the atmosphere's) the level's middle */
	/* (the sky dome as Halo 3 draws it, static_per_vertex_color: its vertices'
	colour times its albedo through the sky's atmosphere - times the
	extinction, plus the inscatter -, its triangles drawn into a map
	SKY_DOME_SIDE a side, the zenith its middle and the horizon a circle
	(sky_dome_coordinates): rgb and whether drawn, four reals a texel) */
	float *sky_dome_image;
	float sky_dome_albedo[3];    /* (the sky dome's render method's albedo colour: what the atmosphere's extinction shows of space) */
	float *instance_probes;      /* (the objects' light) each instance placed as an object: its position, then its per-vertex lighting's mean (15 floats) */
	long instance_probe_count;
	float exposure_range[3];     /* the level's camera fx (cfxs): exposure stops, least and most, and its target (log2) */
	float sky_exposure;          /* (h3/light-1) the exposure the sky's maps are made at: the frame pass's own for the sky's pixels; 0 the level's */
	boolean exposure_known;

	struct geometry bsp_geometry;
	struct geometry sky_geometry;

	/* collision */
	struct h3_resource collision_resource;
	struct collision_bsp collision;
	long definition_count;
	struct collision_bsp *definition_collision;
	float *definition_radius;
	float *definition_centre;
	struct object_key *keys;
	long key_count;
	struct model_made *models;
	long model_count;
	struct model_made sky_model;    /* the sky's, being made */
	/* the map's gear machine (Zanzibar's windmill): where it is, its model,
	its turn (a key's quaternions from its device position animation), its
	turn's speed */
	struct gear
	{
		boolean found;
		long mode_tag;
		float position[3];
		float rotation[3];
		float velocity;              /* turns a second */
		long frame_count;
		unsigned char *animation;    /* its animation resource's first bytes */
		unsigned long animation_size;
		struct geometry geometry;
		struct model_made model;
		long model_index;
		float axis[3];
		float radius;
		char name[64];
	} gear;
	long sky_model_index;           /* its model's index among the models, NONE */
	long slot_key[3 * MAXIMUM_OBJECT_PALETTE];
	long slot_count;

	long cluster_count;
	unsigned long clusters;
	short *leaf_cluster;
	long leaf_count;

	long instance_count;
	struct instance { short definition; long key; boolean placed; float matrix[13]; } *instances;
	/* each cluster's surfaces so far (this engine's most a cluster:
	MAXIMUM_SURFACES_PER_CLUSTER, 32768 - past it the cluster is not drawn) */
	long cluster_surfaces[512];

	/* the scenario's static objects (its crates, scenery, and machines and
	controls but the gear) made this engine's scenery as the instanced
	geometry's are: each object's model, and of its collision the largest BSP
	whose node is not turned (moved to the model's space) */
	struct prop
	{
		long object_tag;
		char name[64];
		struct geometry geometry;
		boolean geometry_open;
		struct collision_bsp collision;
		long collision_bsps_left_out;   /* (the object's others) */
		short *shaders;
		long material_count;
		long placement_count;
		struct prop_placement { float position[3]; float angles[3]; float scale; } *placements;
	} *props;
	long prop_count;

	/* the structure BSP's decorators kept as geometry (decorators_find):
	each wanted set's model, its shader (its texture alpha tested), and the
	placements' buffer (the sbsp's decorator resource) */
	struct decorator_set
	{
		long set_index;
		char name[64];
		struct geometry geometry;
		boolean geometry_open;
		short shader;
		long budget;                 /* the triangles it may take */
		long placed, total;
	} *decorators;
	long decorator_count;
	struct geometry decorator_buffers;
	boolean decorator_buffers_open;
	unsigned char *decorator_wanted_buffers;
	long decorator_triangles;
	/* the decorators' grass as detail objects (grass_gather) */
	struct grass_set { long set_index; char name[64]; long bitmap; } grass_sets[4];
	long grass_set_count;
	struct grass_cell { short x, y, z; unsigned long layers; long start, count_index; } *grass_cells;
	long grass_cell_count;
	unsigned char *grass_objects;
	long grass_object_count;
	unsigned short *grass_counts;
	long grass_count_count;
};

/* ---------- private code: buffers */

H3_ALIGN static void *bytes_grow(struct bytes *b, unsigned long size)
{
	if (b->failed)
		return NULL;
	if (b->size + size > b->capacity && b->fixed)
	{
		b->failed = TRUE;
		return NULL;
	}
	if (b->size + size > b->capacity)
	{
		unsigned long capacity = b->capacity ? b->capacity : 4096;
		unsigned char *data;

		while (capacity < b->size + size)
			capacity = capacity < 4 * 1024 * 1024 ? capacity * 2 : capacity + capacity / 4;
		data = realloc(b->data, capacity);
		if (!data)
		{
			b->failed = TRUE;
			return NULL;
		}
		b->data = data;
		b->capacity = capacity;
	}
	b->size += size;

	return b->data + b->size - size;
}

H3_ALIGN static boolean bytes_put(struct bytes *b, void const *data, unsigned long size)
{
	void *p = bytes_grow(b, size);

	if (p)
		memcpy(p, data, size);
	return p != NULL;
}

H3_ALIGN static void bytes_free(struct bytes *b)
{
	if (!b->fixed)
		free(b->data);
	memset(b, 0, sizeof(*b));
}

H3_ALIGN static unsigned long read32(void const *p)
{
	unsigned char const *b = p;

	return (unsigned long)b[0] | ((unsigned long)b[1] << 8) | ((unsigned long)b[2] << 16) | ((unsigned long)b[3] << 24);
}

H3_ALIGN static short read16(void const *p)
{
	unsigned char const *b = p;

	return (short)(b[0] | (b[1] << 8));
}

H3_ALIGN static float readf(void const *p)
{
	float f;

	memcpy(&f, p, 4);
	return f;
}

H3_ALIGN static void write32(void *p, unsigned long v)
{
	unsigned char *b = p;

	b[0] = (unsigned char)v;
	b[1] = (unsigned char)(v >> 8);
	b[2] = (unsigned char)(v >> 16);
	b[3] = (unsigned char)(v >> 24);
}

H3_ALIGN static void write16(void *p, long v)
{
	unsigned char *b = p;

	b[0] = (unsigned char)v;
	b[1] = (unsigned char)(v >> 8);
}

H3_ALIGN static void writef(void *p, float f)
{
	memcpy(p, &f, 4);
}

H3_ALIGN static void build_fail(struct build *build, char const *why)
{
	if (!build->result->why[0])
		snprintf(build->result->why, sizeof(build->result->why), "%s", why);
}

H3_ALIGN static char const *tag_short_name(char const *path)
{
	char const *slash = strrchr(path, '\\');

	return slash ? slash + 1 : path;
}

H3_ALIGN static float env_float(char const *name, float fallback)
{
	char const *text = getenv(name);

	return text ? (float)atof(text) : fallback;
}

/* ---------- private code: vectors */

/* this engine's 11:11:10 packing (the Xbox's NORMPACKED3) */
H3_ALIGN static unsigned long pack_component(float f, int bits)
{
	unsigned long sign = 1UL << (bits - 1);
	unsigned long mask = sign - 1;

	if (f > 1.0f)
		f = 1.0f;
	if (f < -1.0f)
		f = -1.0f;
	if (f >= 0.0f)
		return (unsigned long)(f * mask + 0.5f);
	return ((unsigned long)((1.0f + f) * mask + 0.5f)) | sign;
}

H3_ALIGN static unsigned long pack_vector(float const v[3])
{
	return pack_component(v[0], 11) | (pack_component(v[1], 11) << 11) | (pack_component(v[2], 10) << 22);
}

H3_ALIGN static float unpack_component(unsigned long v, int bits)
{
	unsigned long sign = 1UL << (bits - 1);
	unsigned long mask = sign - 1;
	long value = (long)(v & (sign | mask));

	if (value & sign)
		value -= (long)(sign << 1);
	return (float)value / (float)mask;
}

H3_ALIGN static void unpack_vector(unsigned long v, float out[3])
{
	out[0] = unpack_component(v, 11);
	out[1] = unpack_component(v >> 11, 11);
	out[2] = unpack_component(v >> 22, 10);
}

H3_ALIGN static void normalize3(float v[3])
{
	float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

	if (length > 1e-12f)
	{
		v[0] /= length;
		v[1] /= length;
		v[2] /= length;
	}
	else
	{
		v[0] = 0.0f;
		v[1] = 0.0f;
		v[2] = 1.0f;
	}
}

/* ---------- private code: geometry resources */

/* a render geometry's resource read (its control: the vertex and index
buffers' descriptions; their bytes are asked for later), its meshes and
compression at the tag's addresses */
H3_ALIGN static boolean geometry_open(struct build *build, struct geometry *g, unsigned long resource_datum,
	unsigned long meshes_field, unsigned long compression_field)
{
	struct h3_cache *cache = build->cache;
	struct h3_resource *r = &g->resource;
	unsigned long kind, at;
	long count, i;

	memset(g, 0, sizeof(*g));
	if (!h3_cache_block(cache, meshes_field, H3_MESH_SIZE, &g->mesh_count, &g->meshes) ||
		!h3_cache_block(cache, compression_field, H3_COMPRESSION_SIZE, &g->compression_count, &g->compression) ||
		resource_datum == 0xFFFFFFFFUL || !h3_cache_resource_get(cache, (long)(resource_datum & 0xFFFF), r) ||
		r->page[0] == NONE)
	{
		return FALSE;
	}
	/* (the root: vertex buffers at 0x18, index buffers at 0x24) */
	count = (long)h3_resource_read32(r, r->root + 0x18);
	if (count < 0 || count > 0x10000 || (count && !h3_resource_pointer(r, r->root + 0x1C, &kind, &at)) ||
		(count && (kind != 2 || at + 12 * (unsigned long)count > r->control_size)))
	{
		return FALSE;
	}
	g->vertex_buffers = calloc(count + 1, sizeof(*g->vertex_buffers));
	if (!g->vertex_buffers)
		return FALSE;
	g->vertex_buffer_count = count;
	for (i = 0; i < count; i++)
	{
		unsigned long definition, data_kind, data;
		struct vertex_buffer *v = &g->vertex_buffers[i];

		if (!h3_resource_pointer(r, at + 12 * i, &kind, &definition) || kind != 2 ||
			definition + 0x18 > r->control_size ||
			!h3_resource_pointer(r, definition + 0x14, &data_kind, &data) || data_kind != 4)
		{
			v->declaration = -1;
			continue;
		}
		v->count = (long)h3_resource_read32(r, definition);
		v->declaration = (short)(h3_resource_read32(r, definition + 4) & 0xFFFF);
		v->stride = (short)(h3_resource_read32(r, definition + 4) >> 16);
		v->size = h3_resource_read32(r, definition + 8);
		v->offset = data;
		if (v->stride <= 0 || v->count < 0 || (unsigned long)v->count * (unsigned long)v->stride > v->size ||
			v->offset > r->size[0] || v->size > r->size[0] - v->offset)
		{
			v->declaration = -1;
		}
	}
	count = (long)h3_resource_read32(r, r->root + 0x24);
	if (count < 0 || count > 0x10000 || (count && !h3_resource_pointer(r, r->root + 0x28, &kind, &at)) ||
		(count && (kind != 2 || at + 12 * (unsigned long)count > r->control_size)))
	{
		return FALSE;
	}
	g->index_buffers = calloc(count + 1, sizeof(*g->index_buffers));
	if (!g->index_buffers)
		return FALSE;
	g->index_buffer_count = count;
	for (i = 0; i < count; i++)
	{
		unsigned long definition, data_kind, data;
		struct index_buffer *b = &g->index_buffers[i];

		if (!h3_resource_pointer(r, at + 12 * i, &kind, &definition) || kind != 2 ||
			definition + 0x14 > r->control_size ||
			!h3_resource_pointer(r, definition + 0x10, &data_kind, &data) || data_kind != 4)
		{
			b->type = -1;
			continue;
		}
		b->type = (long)h3_resource_read32(r, definition);
		b->size = h3_resource_read32(r, definition + 4);
		b->offset = data;
		if (b->offset > r->size[0] || b->size > r->size[0] - b->offset)
			b->type = -1;
	}

	return TRUE;
}

H3_ALIGN static void geometry_close(struct geometry *g)
{
	long i;

	for (i = 0; i < g->vertex_buffer_count; i++)
	{
		free(g->vertex_buffers[i].data);
		free(g->vertex_buffers[i].sh);
	}
	for (i = 0; i < g->index_buffer_count; i++)
		free(g->index_buffers[i].data);
	free(g->vertex_buffers);
	free(g->index_buffers);
	h3_resource_free(&g->resource);
	memset(g, 0, sizeof(*g));
}

/* the requests a geometry's buffers want: those its meshes use (a mesh's
vertex buffers 0 and 1 - positions, lightmap coordinates - and its index
buffer), each its own buffer */
H3_ALIGN static void geometry_request(struct build *build, struct geometry *g, struct h3_page_request **requests,
	long *count, long *capacity)
{
	long m, k;
	unsigned char *want_vertices = calloc(g->vertex_buffer_count + 1, 1);
	unsigned char *want_indices = calloc(g->index_buffer_count + 1, 1);

	if (!want_vertices || !want_indices)
	{
		free(want_vertices);
		free(want_indices);
		return;
	}
	for (m = 0; m < g->mesh_count; m++)
	{
		unsigned char const *mesh = h3_cache_at(build->cache, g->meshes + H3_MESH_SIZE * m, H3_MESH_SIZE);
		int slot;
		short ib;

		if (!mesh)
			continue;
		for (slot = 0; slot < 8; slot++)
		{
			short vb = read16(mesh + 0x18 + 2 * slot);

			/* (0 its vertices, 1 its lightmap coordinates; 6 and 7 its
			water's: each triangle's vertices and the water vertices'
			parameters) */
			if (slot >= 2 && slot < 6 && !(vb >= 0 && vb < g->vertex_buffer_count && g->vertex_buffers[vb].declaration == 14))
				continue;
			if (vb >= 0 && vb < g->vertex_buffer_count && g->vertex_buffers[vb].declaration >= 0)
				want_vertices[vb] = 1;
		}
		ib = read16(mesh + 0x28);
		if (ib >= 0 && ib < g->index_buffer_count && g->index_buffers[ib].type >= 0)
			want_indices[ib] = 1;
	}
	for (k = 0; k < g->vertex_buffer_count + g->index_buffer_count; k++)
	{
		boolean vertex = k < g->vertex_buffer_count;
		long n = vertex ? k : k - g->vertex_buffer_count;
		struct h3_page_request *r;

		if (!(vertex ? want_vertices[n] : want_indices[n]))
			continue;
		if (*count >= *capacity)
		{
			long grown_capacity = *capacity ? *capacity * 2 : 256;
			struct h3_page_request *grown = realloc(*requests, grown_capacity * sizeof(**requests));

			if (!grown)
				break;
			*requests = grown;
			*capacity = grown_capacity;
		}
		r = &(*requests)[(*count)++];
		memset(r, 0, sizeof(*r));
		r->page = g->resource.page[0];
		r->offset = g->resource.offset[0] + (vertex ? g->vertex_buffers[n].offset : g->index_buffers[n].offset);
		r->size = vertex ? g->vertex_buffers[n].size : g->index_buffers[n].size;
		r->tag = vertex ? n : -1 - n;
		r->context = g;
	}
	free(want_vertices);
	free(want_indices);
}

/* a request of a geometry's, done: its bytes the buffer's */
H3_ALIGN static void geometry_got(struct geometry *g, struct h3_page_request *r)
{
	if (r->tag >= 0 && r->tag < g->vertex_buffer_count)
		g->vertex_buffers[r->tag].data = r->out;
	else if (r->tag < 0 && -1 - r->tag < g->index_buffer_count)
		g->index_buffers[-1 - r->tag].data = r->out;
	else
		free(r->out);
	r->out = NULL;
}

H3_ALIGN static void section_free(struct section *s)
{
	free(s->positions);
	free(s->coordinates);
	free(s->packed);
	free(s->lightmap_coordinates);
	free(s->colours);
	free(s->indices);
	free(s->parts);
	memset(s, 0, sizeof(*s));
}

/* a section's normals from its faces (Halo 3's counter-clockwise) and a
tangent frame from its texture coordinates, packed */
H3_ALIGN static void section_frame(struct section *s)
{
	long i, n;

	/* normals from the faces (Halo 3's counter-clockwise), a tangent frame
	from the texture coordinates */
	{
		float *normals = calloc(s->vertex_count * 3, sizeof(float));
		float *tangents = calloc(s->vertex_count * 3, sizeof(float));

		if (normals && tangents)
		{
			for (i = 0; i + 2 < s->index_count; i += 3)
			{
				float const *a = s->positions + 3 * s->indices[i], *b = s->positions + 3 * s->indices[i + 1], *c = s->positions + 3 * s->indices[i + 2];
				float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
				float f[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
				float const *ta = s->coordinates + 2 * s->indices[i], *tb = s->coordinates + 2 * s->indices[i + 1], *tc = s->coordinates + 2 * s->indices[i + 2];
				float du1 = tb[0] - ta[0], dv1 = tb[1] - ta[1], du2 = tc[0] - ta[0], dv2 = tc[1] - ta[1];
				float det = du1 * dv2 - du2 * dv1, t[3];
				int j, k;

				for (k = 0; k < 3; k++)
					t[k] = fabsf(det) > 1e-12f ? (e1[k] * dv2 - e2[k] * dv1) / det : 0.0f;
				for (j = 0; j < 3; j++)
				{
					for (k = 0; k < 3; k++)
					{
						normals[3 * s->indices[i + j] + k] += f[k];
						tangents[3 * s->indices[i + j] + k] += t[k];
					}
				}
			}
			for (n = 0; n < s->vertex_count; n++)
			{
				float *nn = normals + 3 * n, *tt = tangents + 3 * n, b[3], d;

				normalize3(nn);
				d = tt[0] * nn[0] + tt[1] * nn[1] + tt[2] * nn[2];
				tt[0] -= d * nn[0];
				tt[1] -= d * nn[1];
				tt[2] -= d * nn[2];
				if (tt[0] * tt[0] + tt[1] * tt[1] + tt[2] * tt[2] < 1e-12f)
				{
					tt[0] = fabsf(nn[2]) < 0.9f ? 0.0f : 1.0f;
					tt[1] = 0.0f;
					tt[2] = fabsf(nn[2]) < 0.9f ? 1.0f : 0.0f;
				}
				normalize3(tt);
				b[0] = nn[1] * tt[2] - nn[2] * tt[1];
				b[1] = nn[2] * tt[0] - nn[0] * tt[2];
				b[2] = nn[0] * tt[1] - nn[1] * tt[0];
				s->packed[3 * n] = pack_vector(nn);
				s->packed[3 * n + 1] = pack_vector(b);
				s->packed[3 * n + 2] = pack_vector(tt);
			}
		}
		free(normals);
		free(tangents);
	}

}

/* (the water's maps' coordinates' fit, below) */
static double h3_water_fit[14];
/* (and its vertices' lightmap coordinates, some of them: the lightmap's light on the water) */
enum { H3_WATER_LIGHT_SAMPLES = 4096 };
static float h3_water_light_at[H3_WATER_LIGHT_SAMPLES][2];
static long h3_water_light_count;

/* a geometry's mesh decoded (its positions and texture coordinates out of
their compression, its strips as lists, its normals from its faces);
FALSE if it cannot be */
H3_ALIGN static boolean section_read(struct build *build, struct geometry *g, long mesh_index, struct section *s)
{
	struct h3_cache *cache = build->cache;
	unsigned char mesh[H3_MESH_SIZE];
	struct vertex_buffer *vb, *lb = NULL;
	struct index_buffer *ib;
	float bounds[10] = { 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f };
	long part_count, i, n;
	unsigned long parts;
	short vb_index, lb_index, ib_index;
	unsigned short *list;
	long used = 0;

	memset(s, 0, sizeof(*s));
	if (mesh_index < 0 || mesh_index >= g->mesh_count ||
		!h3_cache_read(cache, g->meshes + H3_MESH_SIZE * mesh_index, H3_MESH_SIZE, mesh) ||
		!h3_cache_block(cache, g->meshes + H3_MESH_SIZE * mesh_index, H3_PART_SIZE, &part_count, &parts))
	{
		return FALSE;
	}
	vb_index = read16(mesh + 0x18);
	lb_index = read16(mesh + 0x1A);
	ib_index = read16(mesh + 0x28);
	if (vb_index < 0 || vb_index >= g->vertex_buffer_count || ib_index < 0 || ib_index >= g->index_buffer_count)
		return FALSE;
	vb = &g->vertex_buffers[vb_index];
	ib = &g->index_buffers[ib_index];
	if (lb_index >= 0 && lb_index < g->vertex_buffer_count && g->vertex_buffers[lb_index].declaration == 4 &&
		g->vertex_buffers[lb_index].data && g->vertex_buffers[lb_index].count == vb->count)
	{
		lb = &g->vertex_buffers[lb_index];
	}
	if (!vb->data || !ib->data || vb->count <= 0 || vb->count > 0xFFFF ||
		!((vb->declaration == 1 && vb->stride >= 20) || ((vb->declaration == 2 || vb->declaration == 3) && vb->stride >= 12)) ||
		!(ib->type == 3 || ib->type == 5))
	{
		return FALSE;
	}
	if (vb->declaration != 1)
	{
		/* (the mesh's compression, else the first; a skinned mesh's - 3 -
		as a rigid one's, its vertices in the model's space at its default
		pose, its nodes and weights after) */
		long c = mesh_index < g->compression_count ? mesh_index : 0;
		unsigned char const *comp = g->compression_count ? h3_cache_at(cache, g->compression + H3_COMPRESSION_SIZE * c,
			H3_COMPRESSION_SIZE) : NULL;

		if (!comp)
			return FALSE;
		for (i = 0; i < 10; i++)
			bounds[i] = readf(comp + 4 + 4 * i);
	}
	s->vertex_count = vb->count;
	s->positions = malloc(s->vertex_count * 12);
	s->coordinates = malloc(s->vertex_count * 8);
	s->packed = calloc(s->vertex_count, 12);
	s->lightmap_coordinates = lb ? malloc(s->vertex_count * 8) : NULL;
	/* (its per-vertex colours: Halo 3's static_per_vertex_color meshes - the
	vertex buffer of its slots 1..5 of declaration 14, three reals a vertex) */
	{
		int slot;

		for (slot = 1; slot < 6 && !s->colours; slot++)
		{
			short cb = read16(mesh + 0x18 + 2 * slot);

			if (cb >= 0 && cb < g->vertex_buffer_count && g->vertex_buffers[cb].declaration == 14 && g->vertex_buffers[cb].stride == 12 &&
				g->vertex_buffers[cb].data && g->vertex_buffers[cb].count == vb->count && (s->colours = malloc(s->vertex_count * 12)) != NULL)
			{
				for (n = 0; n < s->vertex_count * 3; n++)
					s->colours[n] = readf(g->vertex_buffers[cb].data + 4 * n);
			}
		}
	}
	s->parts = calloc(part_count + 1, sizeof(*s->parts));
	list = malloc((ib->size / 2) * 3 * sizeof(unsigned short) + 6);
	if (!s->positions || !s->coordinates || !s->packed || (lb && !s->lightmap_coordinates) || !s->parts || !list)
	{
		free(list);
		section_free(s);
		return FALSE;
	}
	for (n = 0; n < s->vertex_count; n++)
	{
		unsigned char const *v = vb->data + (unsigned long)vb->stride * n;
		int k;

		if (vb->declaration == 1)
		{
			for (k = 0; k < 3; k++)
				s->positions[3 * n + k] = readf(v + 4 * k);
			s->coordinates[2 * n] = readf(v + 12);
			s->coordinates[2 * n + 1] = readf(v + 16);
		}
		else
		{
			for (k = 0; k < 3; k++)
			{
				s->positions[3 * n + k] = bounds[2 * k] +
					(float)(unsigned short)read16(v + 2 * k) / 65535.0f * (bounds[2 * k + 1] - bounds[2 * k]);
			}
			for (k = 0; k < 2; k++)
			{
				s->coordinates[2 * n + k] = bounds[6 + 2 * k] +
					(float)(unsigned short)read16(v + 8 + 2 * k) / 65535.0f * (bounds[6 + 2 * k + 1] - bounds[6 + 2 * k]);
			}
		}
		if (lb)
		{
			s->lightmap_coordinates[2 * n] = (float)(unsigned short)read16(lb->data + 4 * n) / 65535.0f;
			s->lightmap_coordinates[2 * n + 1] = (float)(unsigned short)read16(lb->data + 4 * n + 2) / 65535.0f;
		}
	}
	/* the parts' triangles, as lists */
	for (i = 0; i < part_count; i++)
	{
		unsigned char const *p = h3_cache_at(cache, parts + H3_PART_SIZE * i, H3_PART_SIZE);
		long first = p ? (unsigned short)read16(p + 4) : 0, count = p ? (unsigned short)read16(p + 6) : 0;
		long total = (long)(ib->size / 2), start = used, k;
		unsigned short const *indices = (unsigned short const *)ib->data;

		s->parts[i].material = p ? read16(p) : NONE;
		s->parts[i].type = p ? (short)p[0xC] : 0;
		s->parts[i].first = start;
		s->parts[i].count = 0;
		if (!p || first > total || count > total - first)
			continue;
		if (ib->type == 3)
		{
			for (k = 0; k + 2 < count; k += 3)
			{
				unsigned short a = indices[first + k], b = indices[first + k + 1], c = indices[first + k + 2];

				if (a >= s->vertex_count || b >= s->vertex_count || c >= s->vertex_count || a == b || b == c || a == c)
					continue;
				list[used++] = a;
				list[used++] = b;
				list[used++] = c;
			}
		}
		else
		{
			for (k = 0; k + 2 < count; k++)
			{
				unsigned short a = indices[first + k], b = indices[first + k + 1], c = indices[first + k + 2];

				if (a >= s->vertex_count || b >= s->vertex_count || c >= s->vertex_count || a == b || b == c || a == c)
					continue;
				list[used++] = a;
				list[used++] = (k & 1) ? c : b;
				list[used++] = (k & 1) ? b : c;
			}
		}
		s->parts[i].count = used - start;
	}
	/* the level's water: each triangle corner's water values (the mesh's
	eighth buffer: 24 bytes a corner of its index list - the scale of its
	waves and the water's depth there, then its colour and shape maps'
	coordinates -, zeros for a corner that is not water's; the buffer's own
	stride says 36) - the coordinates' fit to the world's x y gathered
	(h3_water_fit: water_shading.fx reads the maps at them) */
	if (g == &build->bsp_geometry && ib->type == 3)
	{
		short wi = read16(mesh + 0x18 + 2 * 7);
		struct vertex_buffer *w = wi >= 0 && wi < g->vertex_buffer_count ? &g->vertex_buffers[wi] : NULL;

		if (w && w->data && w->declaration >= 0)
		{
			long records = (long)(w->size / 24), total = (long)(ib->size / 2), j;
			unsigned short const *indices = (unsigned short const *)ib->data;

			for (j = 0; j < records && j < total; j++)
			{
				unsigned char const *e = w->data + 24 * j;
				double x, y, u, v;

				if (!read32(e) || indices[j] >= s->vertex_count)
					continue;
				x = s->positions[3 * indices[j]];
				y = s->positions[3 * indices[j] + 1];
				u = readf(e + 12);
				v = readf(e + 16);
				h3_water_fit[0] += x * x; h3_water_fit[1] += x * y; h3_water_fit[2] += x;
				h3_water_fit[3] += y * y; h3_water_fit[4] += y; h3_water_fit[5] += 1.0;
				h3_water_fit[6] += u * x; h3_water_fit[7] += u * y; h3_water_fit[8] += u;
				h3_water_fit[9] += v * x; h3_water_fit[10] += v * y; h3_water_fit[11] += v;
				h3_water_fit[12] += u * u; h3_water_fit[13] += v * v;
				if (s->lightmap_coordinates && h3_water_light_count < H3_WATER_LIGHT_SAMPLES)
				{
					h3_water_light_at[h3_water_light_count][0] = s->lightmap_coordinates[2 * indices[j]];
					h3_water_light_at[h3_water_light_count][1] = s->lightmap_coordinates[2 * indices[j] + 1];
					h3_water_light_count++;
				}
			}
		}
	}
	s->part_count = part_count;
	s->indices = list;
	s->index_count = used;
	section_frame(s);

	return used > 0;
}

/* ---------- private code: bitmaps */

/* the bytes of a mip of a format (0: not a format read here) */
H3_ALIGN static unsigned long mip_bytes(unsigned short format, unsigned long w, unsigned long h)
{
	if (w < 1)
		w = 1;
	if (h < 1)
		h = 1;
	switch (format)
	{
		case 14: case 31: case 35: case 36: case 37: case 38:
			return ((w + 3) / 4) * ((h + 3) / 4) * 8;
		case 15: case 16: case 33:
			return ((w + 3) / 4) * ((h + 3) / 4) * 16;
		case 10: case 11: return w * h * 4;
		case 3: case 6: case 8: case 9: return w * h * 2;
		case 0: case 1: case 2: return w * h;
	}
	return 0;
}

H3_ALIGN static boolean is_power_of_two(unsigned long v)
{
	return v && !(v & (v - 1));
}

/* the side a texture is made (a power of two, at most limit): the largest
power of two not more than side, or the next one when side is half as big
again (384 -> 512) */
H3_ALIGN static unsigned long side_made(unsigned long side, unsigned long limit)
{
	unsigned long p = 1;

	while (p * 2 <= side)
		p *= 2;
	if (p != side && side * 10 >= p * 14)
		p *= 2;
	while (p > limit)
		p /= 2;
	return p < 4 ? 4 : p;
}

/* the bitmap of an H3 bitmap tag (its first image), NONE if it cannot be
used. A DXT image whose sides are powers of two goes as it is (the first
mip not more than HALO_H3_MAXIMUM_TEXTURE, 512 by default, a side, and the
ones below it); any other (Halo 3's sides of 384, 640, 768; its 32-bit,
8-bit and DXT5a ones) is made again when it is read: decoded, resized to
powers of two, encoded DXT1 or DXT5 with its mips (bitmap_got) */
H3_ALIGN static long bitmap_for_mode(struct build *build, long tag, short mode);

/* (h3/light-1) the tint the next bitmap asked for is wanted in: a
material's albedo color times its diffuse coefficient (Halo 3's
calc_albedo_default_ps: base x detail x 4.59 x albedo_color; its material
models: the diffuse light times diffuse_coefficient). This engine's opaque
shader has no tint of its own, so the base map is made in the tint - a map
a tint, each its own bitmap. Key 0: none */
static unsigned long bitmap_tint_key;
static float bitmap_tint_wanted[3];

H3_ALIGN static void bitmap_tint_set(float const *tint)
{
	bitmap_tint_key = 0;
	if (tint)
	{
		int k;
		unsigned long key = 0;

		for (k = 0; k < 3; k++)
		{
			float v = tint[k] < 0.0f ? 0.0f : tint[k] > 4.0f ? 4.0f : tint[k];

			bitmap_tint_wanted[k] = v;
			key = key << 10 | (unsigned long)(v * 255.0f + 0.5f);
		}
		/* (white: no tint) */
		if (key != ((255UL << 20) | (255UL << 10) | 255UL))
			bitmap_tint_key = key | 0x80000000UL;
	}
}

H3_ALIGN static long bitmap_for(struct build *build, long tag)
{
	return bitmap_for_mode(build, tag, 0);
}

H3_ALIGN static long bitmap_for_mode(struct build *build, long tag, short mode)
{
	struct h3_cache *cache = build->cache;
	struct h3_cache_tag t;
	long count, i, resources_count;
	unsigned long images, resources;
	unsigned char image[0x30];
	struct h3_resource r;
	struct bitmap *b;
	unsigned long top_w, top_h, limit, skip = 0, chain = 0, req_size, opt_size;
	long level, levels, page, optional_levels = 0;
	unsigned long offset;
	unsigned short format;
	boolean optional, convert;

	if (tag == NONE)
		return NONE;
	for (i = 0; i < build->bitmap_count; i++)
	{
		if (build->bitmaps[i].h3_tag == tag && build->bitmaps[i].mode == mode &&
			build->bitmaps[i].tint_key == ((mode == 0 || mode == 3) ? bitmap_tint_key : 0))
		{
			return i;
		}
	}
	if (!h3_cache_tag_get(cache, tag, &t) || t.group_tag != 'bitm' ||
		!h3_cache_block(cache, t.address + 0x60, 0x30, &count, &images) || count < 1 ||
		!h3_cache_read(cache, images, 0x30, image) ||
		!h3_cache_block(cache, t.address + 0x8C, 8, &resources_count, &resources) || resources_count < 1)
	{
		return NONE;
	}
	format = (unsigned short)read16(image + 0xC);
	top_w = (unsigned short)read16(image + 4);
	top_h = (unsigned short)read16(image + 6);
	levels = 1 + image[0x14];
	if (read16(image + 0xA) != 0 || image[8] > 1 || !mip_bytes(format, 4, 4) || top_w < 1 || top_h < 1 ||
		top_w > 4096 || top_h > 4096)
	{
		return NONE;
	}
	if (!h3_cache_resource_get(cache, (long)(h3_cache_read32(cache, resources) & 0xFFFF), &r))
		return NONE;
	req_size = h3_resource_read32(&r, 0);
	opt_size = h3_resource_read32(&r, 0x14);
	optional = r.page[1] != NONE && opt_size > 0;
	/* (512 a side; the Vita's texture cache: 256) */
#ifdef HALO_VITA
	limit = getenv("HALO_H3_MAXIMUM_TEXTURE") ? (unsigned long)atol(getenv("HALO_H3_MAXIMUM_TEXTURE")) : 256;
#else
	limit = getenv("HALO_H3_MAXIMUM_TEXTURE") ? (unsigned long)atol(getenv("HALO_H3_MAXIMUM_TEXTURE")) : 1024;
#endif
	if (limit < 4)
		limit = 4;
	/* (once the bitmaps planned pass the budget - HALO_H3_TEXTURE_BUDGET,
	20 MB: this engine's texture cache is 22 MB and a wide view of the level
	wants most of them at once, the rest drawn as the cache's stand-in when
	it is full - the later ones at half the side, not less than 256: the
	scenario's objects', planned last; the Vita's 256 are not touched) */
	if (limit > 256 && build->bitmap_planned > (getenv("HALO_H3_TEXTURE_BUDGET") ?
		(unsigned long)atol(getenv("HALO_H3_TEXTURE_BUDGET")) : 64UL << 20))
	{
		limit /= 2;
	}
	if (mode == 4)
		limit = 128;
	if (mode == 6)
		limit = 64;
	/* (a bump map: this engine's P8 bump, a byte a texel, 512 a side at most) */
	if (mode == 7 && limit > 512)
		limit = 512;
	convert = !(format >= 14 && format <= 16) || !is_power_of_two(top_w) || !is_power_of_two(top_h) || mode != 0;
	/* the mip read: the first not more than the limit a side (made: the
	smallest not less than the side it is made at) */
	if (convert)
	{
		unsigned long made_w = side_made(top_w, limit), made_h = side_made(top_h, limit);

		for (level = 0; level + 1 < levels && (top_w >> (level + 1)) >= made_w && (top_h >> (level + 1)) >= made_h; level++)
			;
	}
	else
	{
		for (level = 0; level + 1 < levels && ((top_w >> level) > limit || (top_h >> level) > limit); level++)
			;
	}
	/* (the top levels are in the optional page, as many as its bytes make -
	one, or two on a 2048 -, the others in the required one) */
	if (optional)
	{
		unsigned long bytes = 0;

		for (optional_levels = 0; optional_levels < levels && bytes < opt_size; optional_levels++)
		{
			if (level == optional_levels)
				skip = bytes;
			bytes += mip_bytes(format, top_w >> optional_levels, top_h >> optional_levels);
		}
		if (bytes != opt_size)
			optional_levels = 1, skip = 0;
	}
	if (level < optional_levels)
	{
		page = r.page[1];
		offset = r.offset[1] + skip;
		chain = mip_bytes(format, top_w >> level, top_h >> level);
		if (skip + chain > opt_size || skip + chain > r.size[1])
		{
			h3_resource_free(&r);
			return NONE;
		}
	}
	else
	{
		skip = 0;
		for (i = optional_levels; i < level; i++)
			skip += mip_bytes(format, top_w >> i, top_h >> i);
		page = r.page[0];
		offset = r.offset[0] + skip;
		chain = mip_bytes(format, top_w >> level, top_h >> level);
		if (page == NONE || skip + chain > req_size || skip + chain > r.size[0])
		{
			h3_resource_free(&r);
			return NONE;
		}
	}
	h3_resource_free(&r);
	if (getenv("HALO_H3_DEBUG"))
		platform_log("h3 level: bitmap %s mode %d: %lux%lu fmt %d level %ld of %ld, pages %ld %ld, sizes %lu %lu chain %lu skip %lu", h3_cache_tag_name(cache, tag),
			(int)mode, top_w, top_h, (int)format, level, levels, r.page[0], r.page[1], req_size, opt_size, chain, skip);
	b = realloc(build->bitmaps, (build->bitmap_count + 1) * sizeof(*b));
	if (!b)
		return NONE;
	build->bitmaps = b;
	b = &build->bitmaps[build->bitmap_count];
	memset(b, 0, sizeof(*b));
	b->h3_tag = tag;
	b->format = format;
	b->width = (unsigned short)(top_w >> level ? top_w >> level : 1);
	b->height = (unsigned short)(top_h >> level ? top_h >> level : 1);
	b->page = page;
	b->offset = offset;
	b->size = chain;
	b->mipmaps = 0;
	b->convert = convert;
	if (convert)
	{
		b->made_width = (unsigned short)side_made(top_w, limit);
		b->made_height = (unsigned short)side_made(top_h, limit);
	}
	/* (DXT as it is: the smaller mips too, when they follow in the same page) */
	if (!convert && page == r.page[0] && level + 1 < levels)
	{
		long m;
		unsigned long more = 0;

		for (m = level + 1; m < levels && (top_w >> m) >= 4 && (top_h >> m) >= 4; m++)
		{
			unsigned long bytes = mip_bytes(format, top_w >> m, top_h >> m);

			if (skip + chain + more + bytes > req_size || skip + chain + more + bytes > r.size[0])
				break;
			more += bytes;
			b->mipmaps++;
		}
		b->size += more;
	}
	b->mode = mode;
	if ((mode == 0 || mode == 3) && bitmap_tint_key)
	{
		b->tint_key = bitmap_tint_key;
		memcpy(b->material_tint, bitmap_tint_wanted, sizeof(b->material_tint));
	}
	b->curve = image[0x15];
	b->scale_source = NONE;
	/* (what it will be: a DXT5's bytes at its made sides, its mips a third) */
	build->bitmap_planned += convert ? mip_bytes(16, b->made_width, b->made_height) * 4 / 3 : b->size;
	snprintf(b->path, sizeof(b->path), "h3\\bitmaps\\%s%s", tag_short_name(h3_cache_tag_name(cache, tag)),
		mode == 1 ? " detail" : mode == 2 ? " source" : mode == 6 ? " mean" : mode == 3 ? " lit" : mode == 4 ? " ripples" : mode == 5 ? " bright" :
		mode == 7 ? " bump" : "");
	if (b->tint_key)
	{
		size_t length = strlen(b->path);

		snprintf(b->path + length, sizeof(b->path) - length, " tint %08lx", b->tint_key & 0x3FFFFFFFUL);
	}

	return build->bitmap_count++;
}

/* ---------- private code: textures made again */

/* a BC4 (DXT5's alpha) block's 16 values */
H3_ALIGN static void bc4_decode(unsigned char const *block, unsigned char out[16])
{
	unsigned char a0 = block[0], a1 = block[1], values[8];
	unsigned long long bits = 0;
	int i;

	for (i = 0; i < 6; i++)
		bits |= (unsigned long long)block[2 + i] << (8 * i);
	values[0] = a0;
	values[1] = a1;
	for (i = 1; i < 7; i++)
	{
		if (a0 > a1)
			values[1 + i] = (unsigned char)(((7 - i) * a0 + i * a1 + 3) / 7);
		else if (i < 5)
			values[1 + i] = (unsigned char)(((5 - i) * a0 + i * a1 + 2) / 5);
	}
	if (a0 <= a1)
	{
		values[6] = 0;
		values[7] = 255;
	}
	for (i = 0; i < 16; i++)
		out[i] = values[(bits >> (3 * i)) & 7];
}

/* a DXT colour block's 16 RGBA texels (four-colour always: DXT3 and DXT5) */
H3_ALIGN static void bc1_decode(unsigned char const *block, unsigned char out[16][4], boolean dxt1)
{
	unsigned long c0 = (unsigned long)block[0] | ((unsigned long)block[1] << 8);
	unsigned long c1 = (unsigned long)block[2] | ((unsigned long)block[3] << 8);
	unsigned long bits = read32(block + 4);
	unsigned char colors[4][4];
	int i, k;

	colors[0][0] = (unsigned char)(((c0 >> 11) & 31) * 255 / 31);
	colors[0][1] = (unsigned char)(((c0 >> 5) & 63) * 255 / 63);
	colors[0][2] = (unsigned char)((c0 & 31) * 255 / 31);
	colors[1][0] = (unsigned char)(((c1 >> 11) & 31) * 255 / 31);
	colors[1][1] = (unsigned char)(((c1 >> 5) & 63) * 255 / 63);
	colors[1][2] = (unsigned char)((c1 & 31) * 255 / 31);
	colors[0][3] = colors[1][3] = colors[2][3] = colors[3][3] = 255;
	for (k = 0; k < 3; k++)
	{
		if (!dxt1 || c0 > c1)
		{
			colors[2][k] = (unsigned char)((2 * colors[0][k] + colors[1][k] + 1) / 3);
			colors[3][k] = (unsigned char)((colors[0][k] + 2 * colors[1][k] + 1) / 3);
		}
		else
		{
			colors[2][k] = (unsigned char)((colors[0][k] + colors[1][k]) / 2);
			colors[3][k] = 0;
		}
	}
	if (dxt1 && c0 <= c1)
		colors[3][3] = 0;
	for (i = 0; i < 16; i++)
		memcpy(out[i], colors[(bits >> (2 * i)) & 3], 4);
}

/* an image of a format decoded to RGBA (w x h) */
H3_ALIGN static boolean image_decode(unsigned short format, unsigned long w, unsigned long h, unsigned char const *p,
	unsigned long size, unsigned char *rgba)
{
	unsigned long x, y;

	if (size < mip_bytes(format, w, h))
		return FALSE;
	if (format == 14 || format == 15 || format == 16 || format == 31 || format == 33 || (format >= 35 && format <= 38))
	{
		unsigned long bw = (w + 3) / 4, bh = (h + 3) / 4, bx, by;
		unsigned long block_size = format == 15 || format == 16 || format == 33 ? 16 : 8;

		for (by = 0; by < bh; by++)
		{
			for (bx = 0; bx < bw; bx++)
			{
				unsigned char const *block = p + block_size * (by * bw + bx);
				unsigned char texels[16][4], alpha[16];
				int i;

				if (format == 33)
				{
					/* (DXN: x then y, each a BC4 block) */
					unsigned char nx[16], ny[16];

					bc4_decode(block, nx);
					bc4_decode(block + 8, ny);
					for (i = 0; i < 16; i++)
					{
						texels[i][0] = nx[i];
						texels[i][1] = ny[i];
						texels[i][2] = 255;
						texels[i][3] = 255;
					}
				}
				else if (format == 14)
					bc1_decode(block, texels, TRUE);
				else if (format == 15 || format == 16)
				{
					bc1_decode(block + 8, texels, FALSE);
					if (format == 16)
						bc4_decode(block, alpha);
					else
					{
						for (i = 0; i < 16; i++)
							alpha[i] = (unsigned char)(((block[i / 2] >> (4 * (i & 1))) & 15) * 17);
					}
					for (i = 0; i < 16; i++)
						texels[i][3] = alpha[i];
				}
				else
				{
					/* (one channel: DXT5a's and DXT3a's, grey or alpha) */
					boolean alpha_only = format == 35 || format == 37;

					if (format == 35 || format == 36)
					{
						for (i = 0; i < 16; i++)
							alpha[i] = (unsigned char)(((block[i / 2] >> (4 * (i & 1))) & 15) * 17);
					}
					else
						bc4_decode(block, alpha);
					for (i = 0; i < 16; i++)
					{
						texels[i][0] = texels[i][1] = texels[i][2] = alpha_only ? 255 : alpha[i];
						texels[i][3] = alpha_only ? alpha[i] : 255;
					}
				}
				for (i = 0; i < 16; i++)
				{
					x = bx * 4 + (i & 3);
					y = by * 4 + (i >> 2);
					if (x < w && y < h)
						memcpy(rgba + 4 * (y * w + x), texels[i], 4);
				}
			}
		}
		return TRUE;
	}
	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			unsigned char *o = rgba + 4 * (y * w + x);
			unsigned long i = y * w + x;
			unsigned long v;

			switch (format)
			{
				case 10: case 11:
					o[0] = p[4 * i + 2]; o[1] = p[4 * i + 1]; o[2] = p[4 * i]; o[3] = format == 11 ? p[4 * i + 3] : 255;
					break;
				case 0: o[0] = o[1] = o[2] = 255; o[3] = p[i]; break;
				case 1: o[0] = o[1] = o[2] = p[i]; o[3] = 255; break;
				case 2: o[0] = o[1] = o[2] = o[3] = p[i]; break;
				case 3: o[0] = o[1] = o[2] = p[2 * i]; o[3] = p[2 * i + 1]; break;
				case 6:
					v = (unsigned long)p[2 * i] | ((unsigned long)p[2 * i + 1] << 8);
					o[0] = (unsigned char)(((v >> 11) & 31) * 255 / 31); o[1] = (unsigned char)(((v >> 5) & 63) * 255 / 63);
					o[2] = (unsigned char)((v & 31) * 255 / 31); o[3] = 255;
					break;
				case 8:
					v = (unsigned long)p[2 * i] | ((unsigned long)p[2 * i + 1] << 8);
					o[0] = (unsigned char)(((v >> 10) & 31) * 255 / 31); o[1] = (unsigned char)(((v >> 5) & 31) * 255 / 31);
					o[2] = (unsigned char)((v & 31) * 255 / 31); o[3] = (v & 0x8000) ? 255 : 0;
					break;
				case 9:
					v = (unsigned long)p[2 * i] | ((unsigned long)p[2 * i + 1] << 8);
					o[0] = (unsigned char)(((v >> 8) & 15) * 17); o[1] = (unsigned char)(((v >> 4) & 15) * 17);
					o[2] = (unsigned char)((v & 15) * 17); o[3] = (unsigned char)(((v >> 12) & 15) * 17);
					break;
				default:
					return FALSE;
			}
		}
	}
	return TRUE;
}

/* an RGBA image resized (each made texel the average of the source's
texels it covers, bilinear when it is made larger) */
H3_ALIGN static void image_resize(unsigned char const *in, unsigned long w, unsigned long h, unsigned char *out,
	unsigned long nw, unsigned long nh)
{
	unsigned long x, y;

	for (y = 0; y < nh; y++)
	{
		for (x = 0; x < nw; x++)
		{
			float fx0 = (float)x * w / nw, fx1 = (float)(x + 1) * w / nw;
			float fy0 = (float)y * h / nh, fy1 = (float)(y + 1) * h / nh;
			float sum[4] = { 0, 0, 0, 0 }, total = 0.0f;
			long sx, sy;
			int k;

			if (fx1 - fx0 < 1.0f || fy1 - fy0 < 1.0f)
			{
				/* (larger: bilinear at the texel's middle) */
				float cx = (x + 0.5f) * w / nw - 0.5f, cy = (y + 0.5f) * h / nh - 0.5f;
				long x0 = (long)floorf(cx), y0 = (long)floorf(cy);
				float ax = cx - x0, ay = cy - y0;
				long xs[2], ys[2];
				int i, j;

				xs[0] = x0 < 0 ? 0 : x0; xs[1] = x0 + 1 >= (long)w ? (long)w - 1 : x0 + 1;
				ys[0] = y0 < 0 ? 0 : y0; ys[1] = y0 + 1 >= (long)h ? (long)h - 1 : y0 + 1;
				if (xs[0] >= (long)w) xs[0] = (long)w - 1;
				if (ys[0] >= (long)h) ys[0] = (long)h - 1;
				for (j = 0; j < 2; j++)
				{
					for (i = 0; i < 2; i++)
					{
						float weight = (i ? ax : 1.0f - ax) * (j ? ay : 1.0f - ay);

						for (k = 0; k < 4; k++)
							sum[k] += weight * in[4 * (ys[j] * w + xs[i]) + k];
						total += weight;
					}
				}
			}
			else
			{
				for (sy = (long)fy0; sy < (long)ceilf(fy1) && sy < (long)h; sy++)
				{
					float wy = fminf(fy1, (float)sy + 1.0f) - fmaxf(fy0, (float)sy);

					for (sx = (long)fx0; sx < (long)ceilf(fx1) && sx < (long)w; sx++)
					{
						float weight = wy * (fminf(fx1, (float)sx + 1.0f) - fmaxf(fx0, (float)sx));

						if (weight <= 0.0f)
							continue;
						for (k = 0; k < 4; k++)
							sum[k] += weight * in[4 * (sy * w + sx) + k];
						total += weight;
					}
				}
			}
			for (k = 0; k < 4; k++)
				out[4 * (y * nw + x) + k] = (unsigned char)(total > 0.0f ? sum[k] / total + 0.5f : 0);
		}
	}
}

H3_ALIGN static unsigned long rgb565(unsigned char const *c)
{
	return ((unsigned long)(c[0] >> 3) << 11) | ((unsigned long)(c[1] >> 2) << 5) | (c[2] >> 3);
}

/* a 4x4 block encoded: its colours (DXT1's four-colour mode: the ends the
texels' bounding box's along its longest diagonal, each texel the nearest
of the four) and, for DXT5, its alpha (the ends its least and most) */
H3_ALIGN static void block_encode(unsigned char const texels[16][4], unsigned char *out, boolean dxt5)
{
	unsigned char lo[3] = { 255, 255, 255 }, hi[3] = { 0, 0, 0 }, ends[2][3];
	unsigned long c0, c1, bits = 0;
	float palette[4][3];
	int i, k;

	if (dxt5)
	{
		unsigned char a_lo = 255, a_hi = 0;
		unsigned long long abits = 0;

		for (i = 0; i < 16; i++)
		{
			if (texels[i][3] < a_lo) a_lo = texels[i][3];
			if (texels[i][3] > a_hi) a_hi = texels[i][3];
		}
		out[0] = a_hi;
		out[1] = a_lo;
		for (i = 0; i < 16; i++)
		{
			unsigned long index;

			if (a_hi == a_lo)
				index = 0;
			else
			{
				/* (a0 > a1: 0 is a0, 1 is a1, 2..7 between, from a0 toward a1) */
				long step = (long)((float)(a_hi - texels[i][3]) * 7.0f / (float)(a_hi - a_lo) + 0.5f);

				index = step == 0 ? 0 : step == 7 ? 1 : (unsigned long)(step + 1);
			}
			abits |= (unsigned long long)index << (3 * i);
		}
		for (i = 0; i < 6; i++)
			out[2 + i] = (unsigned char)(abits >> (8 * i));
		out += 8;
	}
	for (i = 0; i < 16; i++)
	{
		for (k = 0; k < 3; k++)
		{
			if (texels[i][k] < lo[k]) lo[k] = texels[i][k];
			if (texels[i][k] > hi[k]) hi[k] = texels[i][k];
		}
	}
	/* (the diagonal: the box's, its sign per channel by the texels' co-variance with green) */
	{
		float mean[3] = { 0, 0, 0 }, cov[3] = { 0, 0, 0 };

		for (i = 0; i < 16; i++)
			for (k = 0; k < 3; k++)
				mean[k] += texels[i][k] / 16.0f;
		for (i = 0; i < 16; i++)
		{
			float g = texels[i][1] - mean[1];

			for (k = 0; k < 3; k++)
				cov[k] += (texels[i][k] - mean[k]) * g;
		}
		for (k = 0; k < 3; k++)
		{
			boolean flip = k != 1 && cov[k] < 0.0f;

			ends[0][k] = flip ? lo[k] : hi[k];
			ends[1][k] = flip ? hi[k] : lo[k];
		}
	}
	c0 = rgb565(ends[0]);
	c1 = rgb565(ends[1]);
	if (c0 < c1)
	{
		unsigned long swap = c0;

		c0 = c1;
		c1 = swap;
	}
	for (k = 0; k < 3; k++)
	{
		float e0 = k == 0 ? ((c0 >> 11) & 31) * 255.0f / 31.0f : k == 1 ? ((c0 >> 5) & 63) * 255.0f / 63.0f : (c0 & 31) * 255.0f / 31.0f;
		float e1 = k == 0 ? ((c1 >> 11) & 31) * 255.0f / 31.0f : k == 1 ? ((c1 >> 5) & 63) * 255.0f / 63.0f : (c1 & 31) * 255.0f / 31.0f;

		palette[0][k] = e0;
		palette[1][k] = e1;
		palette[2][k] = (2.0f * e0 + e1) / 3.0f;
		palette[3][k] = (e0 + 2.0f * e1) / 3.0f;
	}
	for (i = 0; i < 16; i++)
	{
		float best = 1e30f;
		int choice = 0, j;

		for (j = 0; j < 4; j++)
		{
			float d = 0.0f;

			for (k = 0; k < 3; k++)
				d += (texels[i][k] - palette[j][k]) * (texels[i][k] - palette[j][k]);
			if (d < best)
			{
				best = d;
				choice = j;
			}
		}
		/* (c0 == c1: all the first) */
		if (c0 == c1)
			choice = 0;
		bits |= (unsigned long)choice << (2 * i);
	}
	write16(out, (long)c0);
	write16(out + 2, (long)c1);
	write32(out + 4, bits);
}

/* an RGBA image (power-of-two sides) encoded with its mips down to 4 a
side: DXT5 if any texel is not opaque, else DXT1; the bytes in *out (the
caller frees), its format and mip count */
H3_ALIGN static boolean image_encode(unsigned char *rgba, unsigned long w, unsigned long h, unsigned char **out,
	unsigned long *out_size, unsigned short *format, unsigned short *mipmaps)
{
	boolean dxt5 = FALSE;
	unsigned long i, total = 0, at = 0, mw = w, mh = h;
	unsigned short levels = 0;
	unsigned char *level = rgba;

	for (i = 0; i < w * h && !dxt5; i++)
		dxt5 = rgba[4 * i + 3] < 250;
	for (mw = w, mh = h; ; mw /= 2, mh /= 2)
	{
		total += ((mw + 3) / 4) * ((mh + 3) / 4) * (dxt5 ? 16 : 8);
		if (mw <= 4 || mh <= 4)
			break;
		levels++;
	}
	*out = malloc(total);
	if (!*out)
		return FALSE;
	for (mw = w, mh = h; ; )
	{
		unsigned long bx, by;
		unsigned char *smaller;

		for (by = 0; by < (mh + 3) / 4; by++)
		{
			for (bx = 0; bx < (mw + 3) / 4; bx++)
			{
				unsigned char texels[16][4];
				int t;

				for (t = 0; t < 16; t++)
				{
					unsigned long x = bx * 4 + (t & 3), y = by * 4 + (t >> 2);

					if (x >= mw) x = mw - 1;
					if (y >= mh) y = mh - 1;
					memcpy(texels[t], level + 4 * (y * mw + x), 4);
				}
				block_encode(texels, *out + at, dxt5);
				at += dxt5 ? 16 : 8;
			}
		}
		if (mw <= 4 || mh <= 4)
			break;
		/* (the next mip: 2x2 averaged, in place) */
		smaller = level;
		for (by = 0; by < mh / 2; by++)
		{
			for (bx = 0; bx < mw / 2; bx++)
			{
				int k;

				for (k = 0; k < 4; k++)
				{
					unsigned long sum = level[4 * ((2 * by) * mw + 2 * bx) + k] + level[4 * ((2 * by) * mw + 2 * bx + 1) + k] +
						level[4 * ((2 * by + 1) * mw + 2 * bx) + k] + level[4 * ((2 * by + 1) * mw + 2 * bx + 1) + k];

					smaller[4 * (by * (mw / 2) + bx) + k] = (unsigned char)((sum + 2) / 4);
				}
			}
		}
		mw /= 2;
		mh /= 2;
	}
	*out_size = total;
	*format = dxt5 ? 16 : 14;
	*mipmaps = levels;

	return TRUE;
}

/* a bitmap made here: the sky dome's gradient (4 x 64: zenith at the top,
the horizon at the bottom, then the haze below it), DXT1, written once the
pages are read */
H3_ALIGN static long bitmap_generated(struct build *build, char const *path, short kind)
{
	struct bitmap *b;
	long i;

	for (i = 0; i < build->bitmap_count; i++)
	{
		if (build->bitmaps[i].generated && !strcmp(build->bitmaps[i].path, path))
			return i;
	}
	b = realloc(build->bitmaps, (build->bitmap_count + 1) * sizeof(*b));
	if (!b)
		return NONE;
	build->bitmaps = b;
	b = &build->bitmaps[build->bitmap_count];
	memset(b, 0, sizeof(*b));
	b->h3_tag = NONE;
	b->generated = TRUE;
	b->page = NONE;
	b->generated_kind = kind;
	/* (kinds 3 and 4: Halo 3's water's wave slope sheet and environment cube,
	their h3_tag the caller's) */
	b->width = kind == 1 || kind == 4 ? 64 : kind == 3 ? 512 : 4;
	b->height = kind == 3 ? 512 : 64;
	b->type = kind == 1 || kind == 4 ? 2 : 0;
	snprintf(b->path, sizeof(b->path), "%s", path);

	return build->bitmap_count++;
}

H3_ALIGN static long shader_texture(struct build *build, unsigned long postprocess, long template_tag, char const *name,
	float scale[2]);
H3_ALIGN static boolean bitmap_is_real(struct build *build, long tag);

/* ---------- private code: Halo 3's water (h3_level_water) */

enum { H3_MAXIMUM_WATERS = 8 };

static struct h3_level_water h3_waters[H3_MAXIMUM_WATERS];
static long h3_water_count;
/* (the fit of the water vertices' colour map coordinates to their world x y:
the sums of x x, x y, x, y y, y, 1, then u's and v's with x, y, 1, then u u
and v v) */
static double h3_water_fit[14];

H3_ALIGN static boolean water_cube_readable(unsigned char const *image);
H3_ALIGN static boolean bitmap_image_header(struct build *build, long tag, unsigned char image[0x30]);

const struct h3_level_water *h3_level_water(long shader_tag_index)
{
	long i;

	for (i = 0; i < h3_water_count; i++)
	{
		if (h3_waters[i].shader_tag == shader_tag_index)
			return &h3_waters[i];
	}
	return NULL;
}

/* (the desktop's device sets it: a custom fragment program's textures past
the device's four stages, d3d8_gl.c) */
void (*halo_custom_fragment_extra)(int slot, int stage, int wrap) = NULL;

const struct h3_level_water *h3_level_water_by_index(long index)
{
	return index >= 0 && index < h3_water_count && h3_waters[index].shader_tag != NONE ? &h3_waters[index] : NULL;
}

/* a bitmap's first image and its required page's bytes, read now (the
caller's to free); NULL if they cannot be */
H3_ALIGN static unsigned char *bitmap_bytes_now(struct build *build, long tag, unsigned char image[0x30], unsigned long *size)
{
	struct h3_cache *cache = build->cache;
	struct h3_cache_tag t;
	long count, resources_count;
	unsigned long images, resources;
	struct h3_resource r;
	struct h3_page_request request;
	char why[160];

	if (tag == NONE || !h3_cache_tag_get(cache, tag, &t) || t.group_tag != 'bitm' ||
		!h3_cache_block(cache, t.address + 0x60, 0x30, &count, &images) || count < 1 ||
		!h3_cache_read(cache, images, 0x30, image) ||
		!h3_cache_block(cache, t.address + 0x8C, 8, &resources_count, &resources) || resources_count < 1 ||
		!h3_cache_resource_get(cache, (long)(h3_cache_read32(cache, resources) & 0xFFFF), &r))
	{
		return NULL;
	}
	memset(&request, 0, sizeof(request));
	request.page = r.page[0];
	request.offset = r.offset[0];
	request.size = h3_resource_read32(&r, 0);
	if (request.size > r.size[0])
		request.size = r.size[0];
	h3_resource_free(&r);
	if (request.page == NONE || !request.size || request.size > (64UL << 20) ||
		!h3_cache_pages_read(cache, &request, 1, why, sizeof(why), NULL) || !request.got)
	{
		free(request.out);
		return NULL;
	}
	*size = request.size;
	return request.out;
}

/* a terrain's composite base map: its blend map and overlay read and kept,
its materials' textures (the overlay's detail maps, or their own base maps)
as grey details; NONE if it has no blend map */
H3_ALIGN static long bitmap_composite(struct build *build, unsigned long postprocess, long template_tag, char const *name)
{
	long base[4], detail[4], blend, i, count = 0, first_base = NONE;
	float base_scale[4][2], detail_scale[4][2], blend_scale[2];
	boolean overlay = TRUE;
	struct bitmap *b;
	long index;

	blend = shader_texture(build, postprocess, template_tag, "blend_map", blend_scale);
	for (i = 0; i < 4; i++)
	{
		static char const *const base_names[4] = { "base_map_m_0", "base_map_m_1", "base_map_m_2", "base_map_m_3" };
		static char const *const detail_names[4] = { "detail_map_m_0", "detail_map_m_1", "detail_map_m_2", "detail_map_m_3" };

		base[i] = shader_texture(build, postprocess, template_tag, base_names[i], base_scale[i]);
		detail[i] = shader_texture(build, postprocess, template_tag, detail_names[i], detail_scale[i]);
		/* (a material may be off, a later one on: Valhalla's _m_0 and _m_2;
		each blend channel is its material's) */
		if (base[i] == NONE)
			continue;
		if (count && base[i] != first_base)
			overlay = FALSE;
		if (!bitmap_is_real(build, detail[i]))
			overlay = FALSE;
		if (!count)
			first_base = base[i];
		count++;
	}
	if (count < 2)
		overlay = FALSE;
	if (getenv("HALO_H3_DEBUG"))
		platform_log("h3 level: terrain %s: blend %ld, %ld materials: %ld %ld %ld %ld overlay %d", name, blend, count, base[0],
			base[1], base[2], base[3], (int)overlay);
	if (blend == NONE || count < 1)
		return NONE;
	/* (one real material - the others Halo 3's default grey -: no composite,
	the material drawn as a plain environment shader. Valhalla's distant
	hills, its distant cliff map alone at 10 and two grey defaults, came out
	of the composite one flat grey slab) */
	{
		long real = 0;

		for (i = 0; i < 4; i++)
			real += base[i] != NONE && bitmap_is_real(build, base[i]);
		if (real <= 1 && base[0] != NONE && bitmap_is_real(build, base[0]))
			return NONE;
	}
	b = realloc(build->bitmaps, (build->bitmap_count + 1) * sizeof(*b));
	if (!b)
		return NONE;
	build->bitmaps = b;
	index = build->bitmap_count++;
	b = &build->bitmaps[index];
	memset(b, 0, sizeof(*b));
	b->h3_tag = NONE;
	b->composite = TRUE;
	b->page = NONE;
	snprintf(b->path, sizeof(b->path), "h3\\bitmaps\\%s terrain", name);
	for (i = 0; i < 6; i++)
		b->sources[i] = NONE;
	for (i = 0; i < 4; i++)
		b->detail_sources[i] = NONE;
	/* (the sources: bitmap_for_mode may move the bitmaps: indices only) */
	{
		long sources[6], details[4];
		float scales[4];

		sources[0] = overlay ? bitmap_for_mode(build, first_base, 2) : NONE;
		sources[1] = bitmap_for_mode(build, blend, 2);
		for (i = 0; i < 4; i++)
		{
			long texture = base[i] != NONE ? (overlay ? detail[i] : base[i]) : NONE;

			scales[i] = base[i] != NONE ? (overlay ? detail_scale[i][0] : base_scale[i][0]) : 1.0f;
			sources[2 + i] = texture != NONE && bitmap_is_real(build, texture) ? bitmap_for_mode(build, texture, 1) : NONE;
			if (sources[2 + i] != NONE && !overlay)
				build->bitmaps[sources[2 + i]].keep = TRUE;
		}
		/* (no overlay: each material's base map is drawn as its detail;
		Halo 3's detail map tints it, by its mean colour - mode 6, read for
		its mean alone) */
		for (i = 0; i < 4; i++)
			details[i] = !overlay && base[i] != NONE && detail[i] != NONE && bitmap_is_real(build, detail[i]) ?
				bitmap_for_mode(build, detail[i], 6) : NONE;
		if (sources[1] != NONE)
			build->bitmaps[sources[1]].not_colour = TRUE;
		b = &build->bitmaps[index];
		memcpy(b->detail_sources, details, sizeof(details));
		memcpy(b->sources, sources, sizeof(sources));
		memcpy(b->source_scales, scales, sizeof(scales));
	}
	if (b->sources[1] == NONE)
		return NONE;
	b->width = build->bitmaps[b->sources[1]].made_width;
	b->height = build->bitmaps[b->sources[1]].made_height;
	b->made_width = b->width;
	b->made_height = b->height;

	return index;
}

/* ---------- private code: shaders */

/* a render method's postprocess texture named name: its bitmap tag (NONE)
and its transform's scale */
H3_ALIGN static long shader_texture(struct build *build, unsigned long postprocess, long template_tag, char const *name,
	float scale[2])
{
	struct h3_cache *cache = build->cache;
	struct h3_cache_tag template_info;
	long name_count, texture_count, constant_count, i;
	unsigned long names, textures, constants;

	scale[0] = scale[1] = 1.0f;
	if (!h3_cache_tag_get(cache, template_tag, &template_info) ||
		!h3_cache_block(cache, template_info.address + 0x6C, 4, &name_count, &names) ||
		!h3_cache_block(cache, postprocess + 0x10, H3_TEXTURE_SIZE, &texture_count, &textures) ||
		!h3_cache_block(cache, postprocess + 0x1C, 0x10, &constant_count, &constants))
	{
		return NONE;
	}
	for (i = 0; i < name_count && i < texture_count; i++)
	{
		if (!strcmp(h3_cache_string(cache, h3_cache_read32(cache, names + 4 * i)), name))
		{
			unsigned char const *texture = h3_cache_at(cache, textures + H3_TEXTURE_SIZE * i, H3_TEXTURE_SIZE);
			signed char transform = texture ? (signed char)texture[H3_TEXTURE_TRANSFORM] : -1;

			if (!texture)
				return NONE;
			if (transform >= 0 && transform < constant_count)
			{
				scale[0] = h3_cache_readf(cache, constants + 0x10 * transform);
				scale[1] = h3_cache_readf(cache, constants + 0x10 * transform + 4);
				if (!(scale[0] > 0.001f && scale[0] < 1000.0f))
					scale[0] = 1.0f;
				if (!(scale[1] > 0.001f && scale[1] < 1000.0f))
					scale[1] = 1.0f;
			}
			return h3_cache_tag_from_datum(cache, read32(texture + 0xC));
		}
	}
	return NONE;
}

/* a render method's float parameter named name (its template's parameters
at 0x48, its float constants one each in that order - a texture's its
transform - when there are as many; FALSE when not) */
H3_ALIGN static boolean shader_constant(struct build *build, unsigned long postprocess, long template_tag, char const *name,
	float value[4])
{
	struct h3_cache *cache = build->cache;
	struct h3_cache_tag template_info;
	long name_count, constant_count, i;
	unsigned long names, constants;

	if (!h3_cache_tag_get(cache, template_tag, &template_info) ||
		!h3_cache_block(cache, template_info.address + 0x48, 4, &name_count, &names) ||
		!h3_cache_block(cache, postprocess + 0x1C, 0x10, &constant_count, &constants) || constant_count != name_count)
	{
		return FALSE;
	}
	for (i = 0; i < name_count; i++)
	{
		if (!strcmp(h3_cache_string(cache, h3_cache_read32(cache, names + 4 * i)), name))
		{
			int k;

			for (k = 0; k < 4; k++)
				value[k] = h3_cache_readf(cache, constants + 0x10 * i + 4 * k);
			return TRUE;
		}
	}
	return FALSE;
}

/* a render method template's option in a category (its name's numbers:
shader_templates\_A_B_C..., A the albedo's, B the bump mapping's ...); -1 */
H3_ALIGN static int template_option(struct build *build, long template_tag, int category)
{
	char const *name = tag_short_name(h3_cache_tag_name(build->cache, template_tag));
	int i;

	for (i = 0; *name && i <= category; i++)
	{
		if (*name != '_')
			return -1;
		name++;
		if (i == category)
			return atoi(name);
		while (*name && *name != '_')
			name++;
	}
	return -1;
}

H3_ALIGN static boolean bitmap_is_real(struct build *build, long tag);

/* a render method's albedo of maps made one map (a blended material - this
engine's transparent shader takes one): Halo 3's albedo option (albedo.fx,
read for its rules) of the base, detail and second detail at their scales
over the base's; NONE when its option is not one of these: 0 default (base
x detail x 4.59, alpha their product), 1 detail blend (the details blended
by the base's alpha), 7 two detail and 9 two detail black point (base x
detail x detail 2 x 4.59 squared; 9's alpha the base's black point) */
H3_ALIGN static long bitmap_product(struct build *build, int option, long base_tag, float const *base_scale, long detail_tag,
	float const *detail_scale, long detail2_tag, float const *detail2_scale)
{
	struct bitmap *b;
	long index, sources[3];
	int i;

	if (option == 101)
	{
		/* (a self-illumination map alone, tinted: no detail) */
		if (getenv("HALO_H3_NO_PRODUCT") || base_tag == NONE)
			return NONE;
	}
	else if (getenv("HALO_H3_NO_PRODUCT") || base_tag == NONE || !bitmap_is_real(build, detail_tag) ||
		!(option == 0 || option == 1 || option == 7 || option == 9 || option == 100) ||
		((option == 1 || (option >= 7 && option < 100)) && detail2_tag == NONE))
	{
		return NONE;
	}
	for (i = 0; i < build->bitmap_count; i++)
	{
		struct bitmap const *o = &build->bitmaps[i];

		if (o->product && o->product_option == option && option < 100 && o->sources[3] == base_tag && o->sources[4] == detail_tag &&
			o->sources[5] == (option ? detail2_tag : NONE))
		{
			return i;
		}
	}
	sources[0] = bitmap_for_mode(build, base_tag, 2);
	sources[1] = option == 101 ? NONE : bitmap_for_mode(build, detail_tag, 2);
	sources[2] = option && option < 100 ? bitmap_for_mode(build, detail2_tag, 2) : NONE;
	if (sources[0] == NONE || (option != 101 && sources[1] == NONE) || (option && option < 100 && sources[2] == NONE))
		return NONE;
	b = realloc(build->bitmaps, (build->bitmap_count + 1) * sizeof(*b));
	if (!b)
		return NONE;
	build->bitmaps = b;
	index = build->bitmap_count++;
	b = &build->bitmaps[index];
	memset(b, 0, sizeof(*b));
	b->h3_tag = NONE;
	b->composite = TRUE;
	b->product = TRUE;
	b->product_option = (short)option;
	b->page = NONE;
	b->scale_source = NONE;
	b->shader = NONE;
	for (i = 0; i < 4; i++)
		b->detail_sources[i] = NONE;
	b->sources[0] = sources[0];
	b->sources[1] = sources[1];
	b->sources[2] = sources[2];
	b->sources[3] = base_tag;
	b->sources[4] = detail_tag;
	b->sources[5] = option && option < 100 ? detail2_tag : NONE;
	b->source_scales[0] = detail_scale[0] / (base_scale[0] > 0.0f ? base_scale[0] : 1.0f);
	b->source_scales[1] = detail_scale[1] / (base_scale[1] > 0.0f ? base_scale[1] : 1.0f);
	b->source_scales[2] = option && option < 100 && detail2_scale ? detail2_scale[0] / (base_scale[0] > 0.0f ? base_scale[0] : 1.0f) : 1.0f;
	b->source_scales[3] = option && option < 100 && detail2_scale ? detail2_scale[1] / (base_scale[1] > 0.0f ? base_scale[1] : 1.0f) : 1.0f;
	b->product_tint[0] = b->product_tint[1] = b->product_tint[2] = 1.0f;
	b->needs_alpha = TRUE;
	snprintf(b->path, sizeof(b->path), "h3\\bitmaps\\%s %s", tag_short_name(h3_cache_tag_name(build->cache, base_tag)),
		option >= 100 ? "illumination" : "albedo");

	return index;
}

/* a Halo 3 water shader's (shader_water) own values gathered for this
engine's Halo 3 water pass (rasterizer_xbox_water.c, the desktop's): its
options (the template's name: wave shape, water colour, reflection,
refraction, bank alpha, appearance, global shape, foam), its parameters by
name, its animated ones' periods (the postprocess's functions at 0x5C, 0x24
each: the kind at 0 - 0 a value, 5 and 6 a transform's x and y translation -,
the period in seconds at 0xC, the function's bytes at 0x10 - an identity
from the value at 4 to the one at 8; the parameter each drives the last
byte of its entry in the table at 0x50), its wave slope array as a sheet
and its environment cube. The water's index + 1; 0 if it cannot be drawn so
(no slope array or cube read: the level's water as before) */
H3_ALIGN static short water_gather(struct build *build, struct shader *s, unsigned long postprocess, long template_tag)
{
	struct h3_cache *cache = build->cache;
	struct h3_level_water *w;
	struct h3_cache_tag template_info;
	float v[4], unused[2];
	long slope_tag, cube_tag, function_count = 0, route_count = 0, name_count = 0, i;
	unsigned long functions = 0, routes = 0, names = 0;
	char path[256];

	if (h3_water_count >= H3_MAXIMUM_WATERS || env_float("HALO_H3_WATER", 1.0f) == 0.0f)
		return 0;
	w = &h3_waters[h3_water_count];
	memset(w, 0, sizeof(*w));
	w->shader_tag = w->foam_tag = w->shape_tag = NONE;
	w->waveshape = template_option(build, template_tag, 0);
	w->watercolor = template_option(build, template_tag, 1);
	w->reflection = template_option(build, template_tag, 2);
	w->refraction = template_option(build, template_tag, 3);
	w->bankalpha = template_option(build, template_tag, 4);
	w->global_shape = template_option(build, template_tag, 6);
	w->foam = template_option(build, template_tag, 7);
	slope_tag = shader_texture(build, postprocess, template_tag, "wave_slope_array", unused);
	cube_tag = shader_texture(build, postprocess, template_tag, "environment_map", unused);
	{
		unsigned char image[0x30];

		/* (the maps as this reads them: 16 slices of 128, two bytes a texel;
		a cube of A8R8G8B8) */
		if (slope_tag != NONE && (!bitmap_image_header(build, slope_tag, image) || read16(image + 4) != 128 || read16(image + 6) != 128 ||
				image[8] != 16 || read16(image + 0xC) != 22))
		{
			slope_tag = NONE;
		}
		if (cube_tag != NONE && (!bitmap_image_header(build, cube_tag, image) || !water_cube_readable(image)))
		{
			platform_log("h3 level: water: environment map %s: %dx%d type %d format %d - not read here", h3_cache_tag_name(cache, cube_tag),
				(int)read16(image + 4), (int)read16(image + 6), (int)read16(image + 0xA), (int)read16(image + 0xC));
			cube_tag = NONE;
		}
		/* (its environment map's own levels under the first: Halo 3 blurs the
		reflection no further than the map's smallest level) */
		w->cube_mips = cube_tag != NONE ? (float)image[0x14] : 16.0f;
	}
	if (w->waveshape != 0 || slope_tag == NONE || w->reflection != 1 || cube_tag == NONE)
	{
		platform_log("h3 level: water: %s: not drawn as Halo 3's (wave shape %d, reflection %d, slope array %s, environment map %s)",
			tag_short_name(h3_cache_tag_name(cache, template_tag)), w->waveshape, w->reflection, slope_tag == NONE ? "none" : "there",
			cube_tag == NONE ? "none" : "there");
		return 0;
	}
#define WATER_VALUE(name, field, fallback) field = shader_constant(build, postprocess, template_tag, name, v) ? v[0] : (fallback)
#define WATER_XFORM(name, field) \
	if (shader_constant(build, postprocess, template_tag, name, v)) memcpy(field, v, sizeof(v)); \
	else field[0] = field[1] = 1.0f, field[2] = field[3] = 0.0f
	WATER_XFORM("wave_displacement_array", w->wave_xform);
	WATER_XFORM("wave_slope_array", w->slope_xform);
	WATER_XFORM("foam_texture", w->foam_xform);
	WATER_XFORM("foam_texture_detail", w->foam_detail_xform);
	WATER_VALUE("slope_range_x", w->slope_range[0], 1.0f);
	WATER_VALUE("slope_range_y", w->slope_range[1], 1.0f);
	WATER_VALUE("detail_slope_scale_x", w->detail_slope_scale[0], 1.0f);
	WATER_VALUE("detail_slope_scale_y", w->detail_slope_scale[1], 1.0f);
	WATER_VALUE("detail_slope_scale_z", w->detail_slope_scale[2], 1.0f);
	WATER_VALUE("detail_slope_steepness", w->detail_slope_steepness, 0.0f);
	WATER_VALUE("wave_height", w->wave_height, 1.0f);
	WATER_VALUE("wave_height_aux", w->wave_height_aux, 1.0f);
	WATER_VALUE("wave_visual_damping_distance", w->wave_visual_damping_distance, 8.0f);
	WATER_VALUE("minimal_wave_disturbance", w->minimal_wave_disturbance, 0.0f);
	WATER_VALUE("watercolor_coefficient", w->watercolor_coefficient, 1.0f);
	WATER_VALUE("water_murkiness", w->water_murkiness, 1.0f);
	WATER_VALUE("fresnel_coefficient", w->fresnel_coefficient, 0.1f);
	WATER_VALUE("reflection_coefficient", w->reflection_coefficient, 1.0f);
	WATER_VALUE("sunspot_cut", w->sunspot_cut, 1.0f);
	WATER_VALUE("shadow_intensity_mark", w->shadow_intensity_mark, 0.0f);
	WATER_VALUE("refraction_texcoord_shift", w->refraction_texcoord_shift, 0.0f);
	WATER_VALUE("refraction_extinct_distance", w->refraction_extinct_distance, 1000.0f);
	WATER_VALUE("refraction_depth_dominant_ratio", w->refraction_depth_dominant_ratio, 1.0f);
	WATER_VALUE("bankalpha_infuence_depth", w->bankalpha_infuence_depth, 0.0f);
	WATER_VALUE("globalshape_infuence_depth", w->globalshape_infuence_depth, 0.0f);
	WATER_VALUE("foam_height", w->foam_height, 0.0f);
	WATER_VALUE("foam_pow", w->foam_pow, 1.0f);
	if (shader_constant(build, postprocess, template_tag, "water_color_pure", v))
		memcpy(w->water_color_pure, v, 12);
	if (shader_constant(build, postprocess, template_tag, "water_diffuse", v))
		memcpy(w->water_diffuse, v, 12);
#undef WATER_VALUE
#undef WATER_XFORM
	/* the animated parameters */
	if (h3_cache_tag_get(cache, template_tag, &template_info) &&
		h3_cache_block(cache, template_info.address + 0x48, 4, &name_count, &names) &&
		h3_cache_block(cache, postprocess + 0x5C, 0x24, &function_count, &functions) &&
		h3_cache_block(cache, postprocess + 0x50, 4, &route_count, &routes))
	{
		for (i = 0; i < function_count; i++)
		{
			unsigned char e[0x24], d[0x20];
			unsigned long size = 0, at = 0;
			long kind, r, parameter = NONE;
			float period, span, rate;
			char const *name;

			if (!h3_cache_read(cache, functions + 0x24 * i, sizeof(e), e) ||
				!h3_cache_data(cache, functions + 0x24 * i + 0x10, &size, &at) || size < 0xC || !h3_cache_read(cache, at, 0xC, d))
			{
				continue;
			}
			for (r = 0; r < route_count && parameter == NONE; r++)
			{
				unsigned char route[4];

				if (h3_cache_read(cache, routes + 4 * r, 4, route) && route[2] == i)
					parameter = route[3];
			}
			kind = (long)read32(e);
			period = readf(e + 0xC);
			span = readf(d + 8) - readf(d + 4);
			if (parameter == NONE || parameter >= name_count || d[0] != 0 || !(period > 0.0f) || span == 0.0f)
				continue;
			name = h3_cache_string(cache, h3_cache_read32(cache, names + 4 * parameter));
			rate = span / period;
			if (kind == 0 && !strcmp(name, "time_warp"))
				w->time_warp_period = 1.0f / rate;
			else if (kind == 0 && !strcmp(name, "time_warp_aux"))
				w->time_warp_aux_period = 1.0f / rate;
			else if ((kind == 5 || kind == 6) && !strcmp(name, "wave_slope_array"))
				w->slope_scroll[kind - 5] = rate;
			else if ((kind == 5 || kind == 6) && !strcmp(name, "foam_texture"))
				w->foam_scroll[kind - 5] = rate;
			else if ((kind == 5 || kind == 6) && !strcmp(name, "foam_texture_detail"))
				w->foam_detail_scroll[kind - 5] = rate;
			else
				platform_log("h3 level: water: animated %s (kind %ld, %.2f s) not followed", name, kind, period);
		}
	}
	/* its maps: the slope sheet and the environment cube made here, the foam
	and shape maps as they are */
	snprintf(path, sizeof(path), "h3\\bitmaps\\%s slope sheet", tag_short_name(h3_cache_tag_name(cache, slope_tag)));
	i = bitmap_generated(build, path, 3);
	if (i == NONE)
		return 0;
	build->bitmaps[i].h3_tag = slope_tag;
	s->ripple = i;
	w->slices = 16.0f;
	snprintf(path, sizeof(path), "h3\\bitmaps\\%s water cube", tag_short_name(h3_cache_tag_name(cache, cube_tag)));
	i = bitmap_generated(build, path, 4);
	if (i == NONE)
		return 0;
	build->bitmaps[i].h3_tag = cube_tag;
	s->reflection = i;
	w->foam_tag = bitmap_for(build, shader_texture(build, postprocess, template_tag, "foam_texture", unused));
	w->shape_tag = bitmap_for(build, shader_texture(build, postprocess, template_tag, "global_shape_texture", unused));
	if (getenv("HALO_H3_WATER_DEBUG"))
	{
		unsigned char image[0x30];
		unsigned long size = 0;
		long shape = shader_texture(build, postprocess, template_tag, "global_shape_texture", unused);
		unsigned char *bytes = bitmap_bytes_now(build, shape, image, &size);

		if (bytes)
		{
			unsigned long sw = (unsigned short)read16(image + 4) / 2, sh = (unsigned short)read16(image + 6) / 2;
			unsigned char *rgba = malloc(sw * sh * 4);

			if (rgba && image_decode((unsigned short)read16(image + 0xC), sw, sh, bytes, size, rgba))
			{
				unsigned long gx, gy;

				platform_log("h3 level: water: shape map %lux%lu (its second level), red x10 on a grid of 8 across, 16 down:", sw, sh);
				for (gy = 0; gy < 16; gy++)
				{
					char line[128];
					int at = 0;

					for (gx = 0; gx < 8; gx++)
					{
						unsigned char const *c = rgba + 4 * ((gy * sh / 16 + sh / 32) * sw + gx * sw / 8 + sw / 16);

						at += snprintf(line + at, sizeof(line) - at, "%d%d%d ", c[0] * 10 / 256, c[1] * 10 / 256, c[2] * 10 / 256);
					}
					platform_log("h3 level: water:   %s", line);
				}
				platform_log("h3 level: water: corners (0,0) %d %d %d, (1,0) %d %d %d, (0,1) %d %d %d, (1,1) %d %d %d", rgba[0], rgba[1], rgba[2],
					rgba[4 * (sw - 1)], rgba[4 * (sw - 1) + 1], rgba[4 * (sw - 1) + 2], rgba[4 * (sh - 1) * sw], rgba[4 * (sh - 1) * sw + 1],
					rgba[4 * (sh - 1) * sw + 2], rgba[4 * (sh * sw - 1)], rgba[4 * (sh * sw - 1) + 1], rgba[4 * (sh * sw - 1) + 2]);
			}
			free(rgba);
			free(bytes);
		}
	}
	/* (the foam's alpha is how much of it there is; the shape map's channels are its values) */
	if (w->foam_tag != NONE)
		build->bitmaps[w->foam_tag].needs_alpha = TRUE;
	if (w->shape_tag != NONE)
		build->bitmaps[w->shape_tag].needs_alpha = TRUE;
	platform_log("h3 level: water: %s: options wave %d colour %d reflection %d refraction %d bank %d shape %d foam %d; time warp %.2f s (aux %.2f s), "
		"slopes scroll %.3f %.3f, foam %.3f %.3f; murkiness %.2f, fresnel %.2f, reflection x%.0f (sun spot above %.3f), bank depth %.2f",
		tag_short_name(h3_cache_tag_name(cache, template_tag)), w->waveshape, w->watercolor, w->reflection, w->refraction, w->bankalpha,
		w->global_shape, w->foam, w->time_warp_period, w->time_warp_aux_period, w->slope_scroll[0], w->slope_scroll[1], w->foam_scroll[0],
		w->foam_scroll[1], w->water_murkiness, w->fresnel_coefficient, w->reflection_coefficient, w->sunspot_cut, w->bankalpha_infuence_depth);
	return (short)++h3_water_count;
}

/* a bitmap not one of Halo 3's stand-ins (default_*, color_* ...) */
H3_ALIGN static boolean bitmap_is_real(struct build *build, long tag)
{
	char const *name = tag != NONE ? tag_short_name(h3_cache_tag_name(build->cache, tag)) : "";

	return tag != NONE && strncmp(name, "default", 7) && strncmp(name, "color_", 6) && strcmp(name, "gray_50_percent") &&
		strcmp(name, "white") && strcmp(name, "black");
}

static short shader_index(struct build *build, long tag);

H3_ALIGN static short shader_for(struct build *build, long tag)
{
	short index = shader_index(build, tag);

	return index != NONE && build->shaders[index].rendered ? index : NONE;
}

H3_ALIGN static short shader_index(struct build *build, long tag)
{
	struct h3_cache *cache = build->cache;
	struct h3_cache_tag t;
	struct shader *s;
	long i, count, template_tag = NONE, base_tag = NONE, detail_tag = NONE, detail2_tag = NONE;
	boolean plain_water = FALSE, flat_colour = FALSE;
	unsigned long postprocess;
	float base_scale[2] = { 1.0f, 1.0f }, detail_scale[2] = { 1.0f, 1.0f }, detail2_scale[2] = { 1.0f, 1.0f };
	long blend_mode = 0;
	char const *name;

	if (tag == NONE)
		return NONE;
	for (i = 0; i < build->shader_count; i++)
	{
		if (build->shaders[i].h3_tag == tag)
			return (short)i;
	}
	if (build->shader_count >= MAXIMUM_SHADERS || !h3_cache_tag_get(cache, tag, &t))
		return NONE;
	s = &build->shaders[build->shader_count];
	memset(s, 0, sizeof(*s));
	s->h3_tag = tag;
	s->bitmap = s->detail = s->detail2 = NONE;
	s->ripple = s->reflection = NONE;
	s->base_scale[0] = s->base_scale[1] = 1.0f;
	s->bump = NONE;
	s->bump_scale[0] = s->bump_scale[1] = 1.0f;
	s->detail_aspect = 1.0f;
	for (i = 0; i < 4; i++)
		s->terrain_bumps[i] = NONE;
	s->group = 'senv';
	name = tag_short_name(h3_cache_tag_name(cache, tag));
	if (getenv("HALO_H3_SHADER_DUMP") && strstr(name, getenv("HALO_H3_SHADER_DUMP")) &&
		h3_cache_block(cache, t.address + 0x28, 0x8C, &count, &postprocess) && count > 0)
	{
		struct h3_cache_tag tt;
		long n, j, f;
		unsigned long block;

		fprintf(stderr, "h3 dump: %s template %s\n", name, h3_cache_tag_name(cache, h3_cache_reference(cache, postprocess)));
		if (h3_cache_tag_get(cache, h3_cache_reference(cache, postprocess), &tt))
		{
			for (f = 0x3C; f <= 0x9C; f += 12)
			{
				if (!h3_cache_block(cache, tt.address + f, 4, &n, &block) || n <= 0 || n > 64)
					continue;
				fprintf(stderr, "h3 dump:  template +%lx:", f);
				for (j = 0; j < n; j++)
					fprintf(stderr, " %s", h3_cache_string(cache, h3_cache_read32(cache, block + 4 * j)));
				fprintf(stderr, "\n");
			}
		}
		if (h3_cache_block(cache, postprocess + 0x1C, 0x10, &n, &block))
		{
			for (j = 0; j < n; j++)
				fprintf(stderr, "h3 dump:  constant %ld: %.3f %.3f %.3f %.3f\n", j, h3_cache_readf(cache, block + 16 * j),
					h3_cache_readf(cache, block + 16 * j + 4), h3_cache_readf(cache, block + 16 * j + 8), h3_cache_readf(cache, block + 16 * j + 12));
		}
	}
	if (h3_cache_block(cache, t.address + 0x28, 0x8C, &count, &postprocess) && count > 0)
	{
		template_tag = h3_cache_reference(cache, postprocess);
		if (getenv("HALO_H3_SHADER_DEBUG") && strstr(h3_cache_tag_name(cache, tag), getenv("HALO_H3_SHADER_DEBUG")))
		{
			struct h3_cache_tag ti;
			long nc = 0, cc = 0, tn = 0, tc = 0, q;
			unsigned long names = 0, constants = 0, tnames = 0, textures = 0;

			platform_log("h3 shader: %s template %s", h3_cache_tag_name(cache, tag), h3_cache_tag_name(cache, template_tag));
			if (h3_cache_tag_get(cache, template_tag, &ti) && h3_cache_block(cache, ti.address + 0x48, 4, &nc, &names) &&
				h3_cache_block(cache, postprocess + 0x1C, 0x10, &cc, &constants) &&
				h3_cache_block(cache, ti.address + 0x6C, 4, &tn, &tnames) &&
				h3_cache_block(cache, postprocess + 0x10, H3_TEXTURE_SIZE, &tc, &textures))
			{
				for (q = 0; q < nc && q < cc; q++)
					platform_log("h3 shader:   %s = %g %g %g %g", h3_cache_string(cache, h3_cache_read32(cache, names + 4 * q)),
						h3_cache_readf(cache, constants + 0x10 * q), h3_cache_readf(cache, constants + 0x10 * q + 4),
						h3_cache_readf(cache, constants + 0x10 * q + 8), h3_cache_readf(cache, constants + 0x10 * q + 12));
				for (q = 0; q < tn && q < tc; q++)
				{
					unsigned char const *texture = h3_cache_at(cache, textures + H3_TEXTURE_SIZE * q, H3_TEXTURE_SIZE);

					platform_log("h3 shader:   texture %s = %s", h3_cache_string(cache, h3_cache_read32(cache, tnames + 4 * q)),
						texture ? h3_cache_tag_name(cache, h3_cache_tag_from_datum(cache, read32(texture + 0xC))) : "?");
				}
			}
		}
		blend_mode = (long)h3_cache_read32(cache, postprocess + 0x68);
		if (t.group_tag == 'rmtr' && !getenv("HALO_H3_NO_TERRAIN"))
		{
			/* terrain: its materials (up to four, a blend map's channels each)
			made one base map (bitmap_composite_write), this engine's blended
			environment shader: the two most used materials' textures its
			details, the base map's alpha choosing between them */
			long composite = bitmap_composite(build, postprocess, template_tag, name);

			if (composite != NONE)
			{
				static char const *const bump_names[4] = { "bump_map_m_0", "bump_map_m_1", "bump_map_m_2", "bump_map_m_3" };

				for (i = 0; i < 4 && !getenv("HALO_H3_NO_BUMP"); i++)
				{
					long bump_tag = shader_texture(build, postprocess, template_tag, bump_names[i], s->terrain_bump_scales[i]);

					s = &build->shaders[build->shader_count];
					if (bump_tag != NONE && bitmap_is_real(build, bump_tag))
						s->terrain_bumps[i] = bitmap_for_mode(build, bump_tag, 7);
					s = &build->shaders[build->shader_count];
				}
				build->bitmaps[composite].shader = (short)build->shader_count;
				s->bitmap = composite;
				s->blended = TRUE;
				s->rendered = TRUE;
				snprintf(s->path, sizeof(s->path), "h3\\shaders\\%s", name);
				build->shader_count++;
				return (short)(build->shader_count - 1);
			}
			base_tag = shader_texture(build, postprocess, template_tag, "base_map_m_0", base_scale);
			detail_tag = shader_texture(build, postprocess, template_tag, "detail_map_m_0", detail_scale);
		}
		else if (t.group_tag == 'rmtr')
		{
			base_tag = shader_texture(build, postprocess, template_tag, "base_map_m_0", base_scale);
			detail_tag = shader_texture(build, postprocess, template_tag, "detail_map_m_0", detail_scale);
		}
		else if (t.group_tag == 'rmw ')
		{
			/* water (its waves, colour and reflection Halo 3's shaders' own):
			the level's sky's ocean's base map (an rmsh named water, beside the
			sky), else its foam - scrolled, blended (below) */
			long ocean = NONE;

			for (i = 0; i < h3_cache_tag_count(cache) && ocean == NONE; i++)
			{
				char const *other = h3_cache_tag_name(cache, i);
				size_t length = strlen(other);
				struct h3_cache_tag o;

				if (length > 6 && !strcmp(other + length - 6, "\\water") && h3_cache_tag_get(cache, i, &o) &&
					o.group_tag == 'rmsh')
				{
					unsigned long ocean_postprocess;
					long ocean_count;

					if (h3_cache_block(cache, o.address + 0x28, 0x8C, &ocean_count, &ocean_postprocess) && ocean_count > 0)
					{
						float unused[2];
						long bump = shader_texture(build, ocean_postprocess, h3_cache_reference(cache, ocean_postprocess), "bump_map", unused);

						ocean = shader_texture(build, ocean_postprocess, h3_cache_reference(cache, ocean_postprocess), "base_map", base_scale);
						if (bump != NONE && !getenv("HALO_H3_WATER_SCHI"))
						{
							s->ripple = bitmap_for_mode(build, bump, 4);
							s->reflection = bitmap_generated(build, "h3\\bitmaps\\water reflection", 1);
						}
					}
				}
			}
			base_tag = ocean;
			/* (no ocean: a river's or lake's own colour, see through to its bed) */
			if (base_tag == NONE)
			{
				base_tag = shader_texture(build, postprocess, template_tag, "watercolor_texture", base_scale);
				plain_water = base_tag != NONE && bitmap_is_real(build, base_tag);
			}
			if (base_tag == NONE)
				base_tag = shader_texture(build, postprocess, template_tag, "foam_texture", base_scale);
			s->water = TRUE;
#if !defined(HALO_VITA) && !defined(HALO_GXM_NULL)
			/* (the desktop: Halo 3's own water pass - its colour texture the
			base map, where it has one) */
			{
				long before_ripple = s->ripple, before_reflection = s->reflection;

				s->h3_water = water_gather(build, s, postprocess, template_tag);
				if (s->h3_water)
				{
					long colour = shader_texture(build, postprocess, template_tag, "watercolor_texture", base_scale);

					if (colour != NONE)
						base_tag = colour;
					plain_water = FALSE;
				}
				else
					s->ripple = before_ripple, s->reflection = before_reflection;
			}
#endif
		}
		else
		{
			base_tag = shader_texture(build, postprocess, template_tag, "base_map", base_scale);
			detail_tag = shader_texture(build, postprocess, template_tag, "detail_map", detail_scale);
			detail2_tag = shader_texture(build, postprocess, template_tag, "detail_map2", detail2_scale);
			{
				float unused[2];

				if (shader_texture(build, postprocess, template_tag, "alpha_test_map", unused) != NONE)
					s->alpha_tested = TRUE;
			}
			/* (its bump map, at its scale over the base's; its cook-torrance
			highlight - material model 2 - its normal specular tint times its
			specular coefficient times its analytical (dominant light) share,
			its curve by its power) */
			if (t.group_tag == 'rmsh' && !getenv("HALO_H3_NO_BUMP"))
			{
				float bump_scale[2];
				long bump_tag = shader_texture(build, postprocess, template_tag, "bump_map", bump_scale);

				if (bump_tag != NONE && bitmap_is_real(build, bump_tag))
				{
					s->bump = bitmap_for_mode(build, bump_tag, 7);
					s = &build->shaders[build->shader_count];
					s->bump_scale[0] = bump_scale[0];
					s->bump_scale[1] = bump_scale[1];
				}
			}
			if (t.group_tag == 'rmsh' && template_option(build, template_tag, 4) == 2 && !getenv("HALO_H3_NO_SPECULAR"))
			{
				float tint[4], coefficient[4], analytical[4], power[4];

				if (shader_constant(build, postprocess, template_tag, "normal_specular_tint", tint) &&
					shader_constant(build, postprocess, template_tag, "specular_coefficient", coefficient) &&
					shader_constant(build, postprocess, template_tag, "analytical_specular_contribution", analytical))
				{
					float k = coefficient[0] * analytical[0];
					int c;

					s->specular_color = 0;
					for (c = 0; c < 3; c++)
					{
						float v = tint[c] * k;

						s->specular_color |= (unsigned long)(v > 1.0f ? 255.0f : v < 0.0f ? 0.0f : v * 255.0f + 0.5f) << (16 - 8 * c);
					}
					s->lightmap_type = _h2_lightmap_type_default;
					if (shader_constant(build, postprocess, template_tag, "normal_specular_power", power))
						s->lightmap_type = power[0] >= 30.0f ? _h2_lightmap_type_shiny : power[0] < 6.0f ? _h2_lightmap_type_dull :
							_h2_lightmap_type_default;
				}
			}
		}
	}
	/* (a halogram - Sierra 117's god rays - whose look is its self-illumination
	alone: its base map a stand-in grey, its albedo colour black. Halo 3 draws
	the illumination map (times its detail and the detail multiplier, option 5
	illum_detail; alone, option 1 simple) times its colour and intensity, added,
	and fades it edge-on (edge fade: the edge's tint to the centre's by the
	view's angle to the surface). Drawn before as its grey base added: large
	white see-through sheets. HALO_H3_HALOGRAM_ILLUM=0: as before) */
	if (t.group_tag == 'rmhg' && blend_mode == 1 && (base_tag == NONE || !bitmap_is_real(build, base_tag)) &&
		h3_cache_block(cache, t.address + 0x28, 0x8C, &count, &postprocess) && count > 0 &&
		(template_option(build, template_tag, 1) == 5 || template_option(build, template_tag, 1) == 1) &&
		env_float("HALO_H3_HALOGRAM_ILLUM", 1.0f) != 0.0f)
	{
		float illum_scale[2] = { 1.0f, 1.0f }, illum_detail_scale[2] = { 1.0f, 1.0f }, colour[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		float intensity[4] = { 1.0f, 1.0f, 1.0f, 1.0f }, albedo[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		long illum_tag = shader_texture(build, postprocess, template_tag, "self_illum_map", illum_scale);
		long illum_detail_tag = shader_texture(build, postprocess, template_tag, "self_illum_detail_map", illum_detail_scale);
		int option = template_option(build, template_tag, 1) == 5 && bitmap_is_real(build, illum_detail_tag) ? 100 : 101;

		shader_constant(build, postprocess, template_tag, "albedo_color", albedo);
		if (illum_tag != NONE && bitmap_is_real(build, illum_tag) && albedo[0] + albedo[1] + albedo[2] < 0.05f)
		{
			long product = bitmap_product(build, option, illum_tag, illum_scale, illum_detail_tag, illum_detail_scale, NONE, NULL);

			s = &build->shaders[build->shader_count];
			if (product != NONE)
			{
				float edge[4] = { 1.0f, 1.0f, 1.0f, 1.0f }, centre[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
				int c;

				shader_constant(build, postprocess, template_tag, "self_illum_color", colour);
				shader_constant(build, postprocess, template_tag, "self_illum_intensity", intensity);
				for (c = 0; c < 3; c++)
					build->bitmaps[product].product_tint[c] = colour[c] * intensity[0] * env_float("HALO_H3_HALOGRAM_SCALE", 1.0f);
				s->bitmap = product;
				base_tag = illum_tag;
				base_scale[0] = illum_scale[0];
				base_scale[1] = illum_scale[1];
				detail_tag = NONE;
				/* (its edge fade: option 6, simple - dark edge-on, whole face-on) */
				if (template_option(build, template_tag, 6) == 1 &&
					shader_constant(build, postprocess, template_tag, "edge_fade_edge_tint", edge) &&
					shader_constant(build, postprocess, template_tag, "edge_fade_center_tint", centre) &&
					edge[0] + edge[1] + edge[2] < 0.3f && centre[0] + centre[1] + centre[2] > 1.5f)
				{
					s->edge_fade = TRUE;
				}
				platform_log("h3 level: %s: a halogram drawn by its self-illumination (%s%s, colour %.3f %.3f %.3f x %.2f%s)", name,
					tag_short_name(h3_cache_tag_name(cache, illum_tag)), option == 100 ? " x its detail" : "", colour[0], colour[1], colour[2],
					intensity[0], s->edge_fade ? ", faded edge-on" : "");
			}
		}
	}
	/* (a surface of a colour alone, its bump map its only texture: a lake's
	bed - not the invisible z_ ones) */
	if (base_tag == NONE && t.group_tag == 'rmsh' && strncmp(name, "z_", 2) && strstr(name, "lake"))
		flat_colour = TRUE;
	if (base_tag != NONE && !bitmap_is_real(build, base_tag) && bitmap_is_real(build, detail_tag))
	{
		/* (a stand-in base: its detail is the look) */
		base_tag = detail_tag;
		base_scale[0] = detail_scale[0];
		base_scale[1] = detail_scale[1];
		detail_tag = NONE;
	}
	if (strstr(name, "invis") || strstr(name, "nodraw") || strstr(name, "portal") || strstr(name, "seamsealer") ||
		strstr(name, "sky_override") || strstr(name, "collision_only") ||
		/* (tests: HALO_H3_HIDE="a b ..." - render methods left undrawn, by name) */
		(getenv("HALO_H3_HIDE") && strstr(getenv("HALO_H3_HIDE"), name)))
	{
		base_tag = NONE;
	}
	/* (Halo 3's albedo with a detail map: the base times the detail times
	its detail multiplier, 4.59 - this engine's: the base times twice the
	detail. So the detail made a half on average (bitmap mode 1) and the
	base times 4.59 times the detail's mean lightness (mode 3, read once the
	details are: level_read's second pass)) */
	/* (a blended material of more than one map: its albedo made one map,
	this engine's transparent shader's - Guardian's canopy, Valhalla's
	horizon clouds: their masks drawn alone were flat grey and white cards) */
	if (base_tag != NONE && t.group_tag == 'rmsh' && !s->water && (blend_mode == 1 || blend_mode == 2 || blend_mode == 3 || blend_mode == 5))
	{
		long product = bitmap_product(build, template_option(build, template_tag, 0), base_tag, base_scale, detail_tag, detail_scale,
			detail2_tag, detail2_scale);

		if (product != NONE)
		{
			s->bitmap = product;
			if (getenv("HALO_H3_DEBUG"))
				platform_log("h3 level: %s: its albedo made one map (option %d)", name, template_option(build, template_tag, 0));
		}
	}
	/* (h3/light-1) the material's tint, the base map made in it: Halo 3's
	albedo color (its default albedo's: base x detail x 4.59 x albedo_color)
	times its material model's diffuse coefficient (the diffuse light times
	it: cook_torrance.fx and the others). Guardian's walls are 0.23 0.27
	0.27 x 0.8 of their maps - drawn white-grey before.
	HALO_H3_MATERIAL_TINT=0: none, as before */
	if (base_tag != NONE && s->bitmap == NONE && t.group_tag == 'rmsh' && !s->water && count > 0 &&
		env_float("HALO_H3_MATERIAL_TINT", 1.0f) != 0.0f)
	{
		float tint[3] = { 1.0f, 1.0f, 1.0f }, albedo[4], diffuse[4];
		int k;

		if (template_option(build, template_tag, 0) == 0 && shader_constant(build, postprocess, template_tag, "albedo_color", albedo))
			for (k = 0; k < 3; k++)
				tint[k] = albedo[k];
		if (shader_constant(build, postprocess, template_tag, "diffuse_coefficient", diffuse) && diffuse[0] >= 0.0f)
			for (k = 0; k < 3; k++)
				tint[k] *= diffuse[0];
		bitmap_tint_set(tint);
	}
	if (base_tag != NONE && s->bitmap == NONE && !getenv("HALO_H3_NO_DETAIL") && bitmap_is_real(build, detail_tag) && !s->water)
	{
		long detail = bitmap_for_mode(build, detail_tag, 1);
		long base = detail != NONE ? bitmap_for_mode(build, base_tag, 3) : NONE;

		if (base != NONE)
		{
			s->bitmap = base;
			s->detail = detail;
			s->detail_scale = detail_scale[0] / (base_scale[0] > 0.0f ? base_scale[0] : 1.0f);
			/* (its v scale apart from its u: Sandtrap's copper's base 1 x 0.25
			under a detail of 0.25 x 0.25 - the detail 0.25 along u and 1 along
			v over the base) */
			if (detail_scale[0] > 0.0f && base_scale[1] > 0.0f)
				s->detail_aspect = (detail_scale[1] / base_scale[1]) / (detail_scale[0] / (base_scale[0] > 0.0f ? base_scale[0] : 1.0f));
			if (build->bitmaps[base].scale_source == NONE || build->bitmaps[base].scale_source == detail)
				build->bitmaps[base].scale_source = detail;
		}
	}
	if (base_tag != NONE && s->bitmap == NONE)
		s->bitmap = bitmap_for(build, base_tag);
	bitmap_tint_set(NULL);
	s->base_scale[0] = base_scale[0];
	s->base_scale[1] = base_scale[1];
	if (s->bump != NONE)
	{
		s->bump_scale[0] /= base_scale[0] > 0.0f ? base_scale[0] : 1.0f;
		s->bump_scale[1] /= base_scale[1] > 0.0f ? base_scale[1] : 1.0f;
	}
	(void)detail2_tag;
	(void)detail2_scale;
	if (s->water && s->ripple != NONE && s->reflection != NONE)
	{
		/* water: this engine's own (shader_transparent_water: ripples,
		reflection, the view's tints - water_shader_write) */
		s->group = 'swat';
		s->rendered = TRUE;
	}
	else if (s->water)
	{
		/* water: blended over the sea bed, scrolled (shader_transparent_chicago) */
		s->group = 'schi';
		s->blend = 0 | 0x100;
		if (plain_water && s->bitmap != NONE)
			build->bitmaps[s->bitmap].force_alpha = (short)env_float("HALO_H3_WATER_ALPHA", 170.0f);
	}
	else if (!strcmp(name, "water") && s->bitmap != NONE)
	{
		/* (the sky's ocean: opaque, scrolled as the level's water is) */
		s->scrolled = TRUE;
	}
	else if (flat_colour)
	{
		s->bitmap = bitmap_generated(build, "h3\\bitmaps\\flat colour", 2);
		s->rendered = s->bitmap != NONE;
	}
	else if (!strncmp(name, "skydome", 7) && t.group_tag == 'rmsh' && base_tag == NONE)
	{
		/* (Halo 3's sky dome: a constant albedo its atmosphere lights; here a
		gradient made for it, horizon to zenith) */
		s->dome = TRUE;
		s->bitmap = bitmap_generated(build, "h3\\bitmaps\\sky_gradient", 0);
		{
			float albedo[4];

			if (h3_cache_block(cache, t.address + 0x28, 0x8C, &count, &postprocess) && count > 0 &&
				shader_constant(build, postprocess, h3_cache_reference(cache, postprocess), "albedo_color", albedo))
			{
				memcpy(build->sky_dome_albedo, albedo, sizeof(build->sky_dome_albedo));
			}
		}
	}
	else if (blend_mode == 1 || blend_mode == 2 || blend_mode == 3 || blend_mode == 5)
	{
		/* (Halo 3's blend modes: 1 additive, 2 multiply, 3 alpha blend, 5
		pre-multiplied alpha) - this engine's: 3 add, 1 multiply, 0 alpha */
		s->group = 'schi';
		s->blend = blend_mode == 1 ? 3 : blend_mode == 2 ? 1 : 0;
		if (s->edge_fade)
			s->blend |= 0x200;
	}
	s->rendered = s->bitmap != NONE || s->group == 'swat';
	snprintf(s->path, sizeof(s->path), "h3\\shaders\\%s", name);
	if (getenv("HALO_H3_DEBUG"))
	{
		fprintf(stderr, "h3 level: shader %ld %s (%c%c%c%c) base %s (%.2f) detail %s (%.2f) blend %ld%s%s\n",
			build->shader_count, name, (char)(t.group_tag >> 24), (char)(t.group_tag >> 16), (char)(t.group_tag >> 8),
			(char)t.group_tag, s->bitmap != NONE ? build->bitmaps[s->bitmap].path : "-", s->base_scale[0],
			s->detail != NONE ? build->bitmaps[s->detail].path : "-", s->detail_scale, blend_mode,
			s->alpha_tested ? " alpha-tested" : "", s->water ? " water" : "");
	}
	/* (its base map's alpha drawn: kept; else the base made DXT1 when it is
	written, bitmap_got) */
	if (s->bitmap != NONE && (s->alpha_tested || s->group == 'schi' || s->water || s->blended || s->blend || s->dome))
		build->bitmaps[s->bitmap].needs_alpha = TRUE;
	build->shader_count++;

	return (short)(build->shader_count - 1);
}

/* ---------- private code: the atmosphere */

/* one atmosphere setting (0xA4 bytes of the sky_atm_parameters' block) */
H3_ALIGN static void atmosphere_setting_read(struct h3_cache *cache, unsigned long at, struct atmosphere *a)
{
	float pitch = h3_cache_readf(cache, at + 0x08) * 3.1415926535f / 180.0f;
	float heading = h3_cache_readf(cache, at + 0x0C) * 3.1415926535f / 180.0f;
	float intensity = h3_cache_readf(cache, at + 0x1C);
	int k;

	a->on = (h3_cache_read16(cache, at) & 1) != 0;
	snprintf(a->name, sizeof(a->name), "%s", h3_cache_string(cache, h3_cache_read32(cache, at + 4)));
	/* (heading from x toward y, pitch up: Last Resort's 175 and 40 are its
	lightprobe's dominant light, measured) */
	a->sun[0] = cosf(pitch) * cosf(heading);
	a->sun[1] = cosf(pitch) * sinf(heading);
	a->sun[2] = sinf(pitch);
	for (k = 0; k < 3; k++)
	{
		a->sun_intensity[k] = h3_cache_readf(cache, at + 0x10 + 4 * k) * intensity;
		a->rayleigh[k] = h3_cache_readf(cache, at + 0x44 + 4 * k);
		a->mie[k] = h3_cache_readf(cache, at + 0x50 + 4 * k);
		a->rayleigh_theta[k] = h3_cache_readf(cache, at + 0x5C + 4 * k);
		a->mie_theta[k] = h3_cache_readf(cache, at + 0x68 + 4 * k);
	}
	a->sea_level = h3_cache_readf(cache, at + 0x20);
	a->rayleigh_height = h3_cache_readf(cache, at + 0x24);
	a->mie_height = h3_cache_readf(cache, at + 0x28);
	a->g = h3_cache_readf(cache, at + 0x34);
	a->distance_bias = h3_cache_readf(cache, at + 0x3C);
	a->maximum_thickness = h3_cache_readf(cache, at + 0x40);
	if (a->rayleigh_height <= 0.0f || a->mie_height <= 0.0f || getenv("HALO_H3_NO_ATMOSPHERE"))
		a->on = FALSE;
}

/* the level's atmosphere settings (its sky_atm_parameters' block at 0x34):
the level's the first, the sky's the first named for the sky dome
("skydome"), else the level's; off when the tag or its flag (enable
atmosphere, bit 0) is not there */
H3_ALIGN static void atmosphere_read(struct build *build)
{
	struct h3_cache *cache = build->cache;
	long tag = h3_cache_find_tag(cache, 'skya', NULL), count, i, sky = NONE;
	unsigned long settings;
	struct h3_cache_tag t;

	memset(&build->atmosphere_level, 0, sizeof(build->atmosphere_level));
	memset(&build->atmosphere_sky, 0, sizeof(build->atmosphere_sky));
	if (tag == NONE || !h3_cache_tag_get(cache, tag, &t) || !h3_cache_block(cache, t.address + 0x34, 0xA4, &count, &settings) || count < 1)
		return;
	for (i = 0; i < count && sky == NONE; i++)
	{
		if (strstr(h3_cache_string(cache, h3_cache_read32(cache, settings + 0xA4 * i + 4)), "skydome"))
			sky = i;
	}
	for (i = 0; i < 2; i++)
	{
		struct atmosphere *a = i ? &build->atmosphere_sky : &build->atmosphere_level;

		atmosphere_setting_read(cache, settings + 0xA4 * (i && sky != NONE ? sky : 0), a);
		platform_log("h3 level: atmosphere (%s) %s: %s, sun %.2f %.2f %.2f x %.2f %.2f %.2f, rayleigh %.5f %.5f %.5f height %.0f, mie %.5f %.5f %.5f height %.1f, g %.2f",
			i ? "sky" : "level", a->name, a->on ? "on" : "off", a->sun[0], a->sun[1], a->sun[2], a->sun_intensity[0], a->sun_intensity[1],
			a->sun_intensity[2], a->rayleigh[0], a->rayleigh[1], a->rayleigh[2], a->rayleigh_height, a->mie[0], a->mie[1], a->mie[2],
			a->mie_height, a->g);
	}
}

H3_ALIGN static void frame_atmosphere_copy(struct atmosphere const *a, struct halo_h3_frame_atmosphere *o)
{
	int k;

	o->on = a->on;
	for (k = 0; k < 3; k++)
	{
		o->sun[k] = a->sun[k];
		o->sun_intensity[k] = a->sun_intensity[k];
		o->rayleigh[k] = a->rayleigh[k];
		o->mie[k] = a->mie[k];
		o->rayleigh_theta[k] = a->rayleigh_theta[k];
		o->mie_theta[k] = a->mie_theta[k];
	}
	o->sea_level = a->sea_level;
	o->rayleigh_height = a->rayleigh_height;
	o->mie_height = a->mie_height;
	o->g = a->g;
	o->distance_bias = a->distance_bias;
	o->maximum_thickness = a->maximum_thickness;
}

/* (h3/light-1) the frame pass's atmospheres: every setting of the level's
sky_atm_parameters, and which each cluster is drawn through - the structure
BSP's atmosphere palette (0x94: a name and the setting's index, 8 bytes
each) by the cluster's atmosphere index (the cluster's byte 0x19; its sky
index 0x18 and camera fx index 0x1A beside it, the kit's own tag's field
names). A cluster with none, and a level with no palette (the multiplayer
maps), take the first setting */
H3_ALIGN static void frame_atmospheres_write(struct build *build, struct h3_frame_level *f)
{
	struct h3_cache *cache = build->cache;
	long tag = h3_cache_find_tag(cache, 'skya', NULL), count = 0, palette_count = 0, cluster_count = 0, i, used[H3_FRAME_SETTINGS];
	unsigned long settings = 0, palette = 0, clusters = 0;
	struct h3_cache_tag t;

	frame_atmosphere_copy(&build->atmosphere_level, &f->level);
	frame_atmosphere_copy(&build->atmosphere_sky, &f->sky);
	f->setting_count = 0;
	memset(f->cluster_setting, 0xFF, sizeof(f->cluster_setting));
	memset(used, 0, sizeof(used));
	if (tag == NONE || !h3_cache_tag_get(cache, tag, &t) || !h3_cache_block(cache, t.address + 0x34, 0xA4, &count, &settings) || count < 1)
		return;
	for (i = 0; i < count && i < H3_FRAME_SETTINGS; i++)
	{
		struct atmosphere a;

		memset(&a, 0, sizeof(a));
		atmosphere_setting_read(cache, settings + 0xA4 * i, &a);
		frame_atmosphere_copy(&a, &f->settings[i]);
		snprintf(f->setting_names[i], sizeof(f->setting_names[i]), "%s", a.name);
	}
	f->setting_count = (int)(count < H3_FRAME_SETTINGS ? count : H3_FRAME_SETTINGS);
	if (!h3_cache_block(cache, build->sbsp + 0x94, 8, &palette_count, &palette))
		palette_count = 0;
	if (!h3_cache_block(cache, build->sbsp + H3_SBSP_CLUSTERS, H3_CLUSTER_SIZE, &cluster_count, &clusters))
		cluster_count = 0;
	for (i = 0; i < cluster_count && i < H3_FRAME_CLUSTERS; i++)
	{
		unsigned char const *c = h3_cache_at(cache, clusters + H3_CLUSTER_SIZE * i + 0x18, 4);
		int index = c ? (signed char)c[1] : -1, setting = 0;

		if (index >= 0 && index < palette_count)
			setting = h3_cache_read16(cache, palette + 8 * index + 4);
		if (setting < 0 || setting >= f->setting_count)
			setting = 0;
		f->cluster_setting[i] = (signed char)setting;
		used[setting]++;
	}
	for (i = 0; i < f->setting_count; i++)
	{
		if (used[i])
			platform_log("h3 level: atmosphere setting %ld %s: %ld clusters (sun x %.2f %.2f %.2f, mie %.5f height %.1f)", i, f->setting_names[i], used[i],
				f->settings[i].sun_intensity[0], f->settings[i].sun_intensity[1], f->settings[i].sun_intensity[2], f->settings[i].mie[1],
				f->settings[i].mie_height);
	}
}

/* Halo 3's scattering between a view point and a point of the scene
(atmosphere.fx compute_scattering, read for its rule): the extinction
(times the scene's light) and the inscatter (added), linear light */
H3_ALIGN static void atmosphere_scatter(struct atmosphere const *a, float const *view, float const *scene, float extinction[3],
	float inscatter[3])
{
	float v[3], dist, c, view_height, scene_height, diff, dp, dm, hg;
	int k;

	if (!a->on)
	{
		extinction[0] = extinction[1] = extinction[2] = 1.0f;
		inscatter[0] = inscatter[1] = inscatter[2] = 0.0f;
		return;
	}
	for (k = 0; k < 3; k++)
		v[k] = view[k] - scene[k];
	dist = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (dist > 1e-4f)
		for (k = 0; k < 3; k++)
			v[k] /= dist;
	c = -(v[0] * a->sun[0] + v[1] * a->sun[1] + v[2] * a->sun[2]);
	dist = dist + a->distance_bias > 0.0f ? dist + a->distance_bias : 0.0f;
	if (dist > a->maximum_thickness)
		dist = a->maximum_thickness;
	view_height = view[2] - a->sea_level > 0.0f ? view[2] - a->sea_level : 0.0f;
	scene_height = scene[2] - a->sea_level > 0.0f ? scene[2] - a->sea_level : 0.0f;
	diff = view_height - scene_height;
	if (diff * diff > 0.001f)
	{
		dp = -(expf(-view_height / a->mie_height) - expf(-scene_height / a->mie_height)) * dist * a->mie_height / diff;
		dm = -(expf(-view_height / a->rayleigh_height) - expf(-scene_height / a->rayleigh_height)) * dist * a->rayleigh_height / diff;
	}
	else
	{
		dp = expf(-view_height / a->mie_height) * dist;
		dm = expf(-view_height / a->rayleigh_height) * dist;
	}
	/* (h3/light-1: Halo 3's constant here is its "Heyey-Greenstein constant
	plus one", 1 + g, not the textbook's 1 + g squared - with it the far
	haze of Guardian comes out the tag test's, 0.034 0.158 0.118 against
	0.036 0.167 0.123 measured at 1.77 stops; with g squared it is 1.24 x
	that. The old term with the frame pass off, the look before) */
	hg = powf(1.0f + (h3_frame_enabled() ? a->g : a->g * a->g) - 2.0f * a->g * c, -1.5f);
	for (k = 0; k < 3; k++)
	{
		float total = a->rayleigh[k] + a->mie[k];

		extinction[k] = expf(-(a->rayleigh[k] * dm + a->mie[k] * dp));
		inscatter[k] = total > 0.0f ? a->sun_intensity[k] / total *
			(a->rayleigh_theta[k] * (1.0f + c * c) + a->mie_theta[k] * (1.0f - a->g * a->g) * hg) * (1.0f - extinction[k]) : 0.0f;
	}
}

/* ---------- private code: lightmaps */

H3_ALIGN static struct atlas *atlas_new(struct build *build)
{
	struct atlas *a;

	if (build->atlas_count >= MAXIMUM_ATLASES)
		return NULL;
	a = &build->atlases[build->atlas_count];
	a->rgb = calloc((unsigned long)LIGHTMAP_ATLAS * LIGHTMAP_ATLAS, 3);
	if (!a->rgb)
		return NULL;
	a->x = a->y = a->row = 0;
	build->atlas_count++;

	return a;
}

H3_ALIGN static short atlas_place(struct build *build, long w, long h, long *x, long *y)
{
	struct atlas *a = build->atlas_count ? &build->atlases[build->atlas_count - 1] : atlas_new(build);

	if (!a || w > LIGHTMAP_ATLAS || h > LIGHTMAP_ATLAS)
		return NONE;
	if (a->x + w > LIGHTMAP_ATLAS)
	{
		a->x = 0;
		a->y += a->row;
		a->row = 0;
	}
	if (a->y + h > LIGHTMAP_ATLAS)
	{
		a = atlas_new(build);
		if (!a)
			return NONE;
	}
	*x = a->x;
	*y = a->y;
	a->x += w;
	if (h > a->row)
		a->row = h;

	return (short)(build->atlas_count - 1);
}

/* a light (0..: Halo 3's linear light, times the exposure) as this
engine's lightmap texel. Halo 3 lights in linear space - its albedo maps
decoded from their curve (xRGB, about gamma 2), the light times the
albedo, the frame encoded again for the screen (final_composite) - while
this engine multiplies the colours as they are on the screen: the texel
is the light's square root, which makes the product the same (the square
root of a product the product of the square roots). With the overbright
pass (h2_lighting_h3_overbright: the texture pass doubles it) the texel
holds half, so the light reaches twice its albedo, as Halo 3's sun does;
without it, it stops at the albedo */
/* the lightmap's response (HALO_H3_LIGHTMAP_GAMMA): the light to this
power first (a test) */
static float light_gamma = 1.0f;
static float light_texel_scale = 1.0f;   /* 0.5 overbright (light_texel_begin) */

H3_ALIGN static void light_texel_begin(void)
{
	light_texel_scale = h2_lighting_h3_overbright() ? 0.5f : 1.0f;
}

H3_ALIGN static unsigned char light_texel(float value)
{
	if (value > 0.0f && light_gamma != 1.0f)
		value = powf(value, light_gamma);
	if (value < 0.0f)
		value = 0.0f;
	value = sqrtf(value) * light_texel_scale;
	if (value > 1.0f)
		value = 1.0f;
	return (unsigned char)(value * 255.0f + 0.5f);
}

/* a patch of one light (4 x 4 texels, its middle used) */
H3_ALIGN static struct placed_page patch_place(struct build *build, float const light[3])
{
	struct placed_page p;
	long i, j;

	memset(&p, 0, sizeof(p));
	p.atlas = atlas_place(build, 4, 4, &p.x, &p.y);
	p.w = p.h = 4;
	if (p.atlas != NONE)
	{
		for (j = 0; j < 4; j++)
		{
			for (i = 0; i < 4; i++)
			{
				unsigned char *o = build->atlases[p.atlas].rgb + 3 * ((p.y + j) * LIGHTMAP_ATLAS + p.x + i);

				o[0] = light_texel(light[0]);
				o[1] = light_texel(light[1]);
				o[2] = light_texel(light[2]);
			}
		}
	}
	return p;
}

H3_ALIGN static struct placed_page const *page_neutral(struct build *build)
{
	if (!build->neutral_made)
	{
		float light[3];

		light[0] = light[1] = light[2] = env_float("HALO_H3_NEUTRAL_LIGHT", 0.7f);
		build->neutral = patch_place(build, light);
		build->neutral_made = TRUE;
	}
	return &build->neutral;
}

/* the full light patch (the sky's: drawn as bright as its maps) */
H3_ALIGN static struct placed_page const *page_bright(struct build *build)
{
	if (!build->bright_made)
	{
		float light[3];

		light[0] = light[1] = light[2] = env_float("HALO_H3_SKY_LIGHT", 1.0f);
		build->bright = patch_place(build, light);
		build->bright_made = TRUE;
	}
	return &build->bright;
}

/* DXT5's colour block decoded (16 RGB texels, 0..1) */
H3_ALIGN static void dxt_colors(unsigned char const *block, float out[16][3])
{
	unsigned long c0 = (unsigned long)block[0] | ((unsigned long)block[1] << 8);
	unsigned long c1 = (unsigned long)block[2] | ((unsigned long)block[3] << 8);
	unsigned long bits = read32(block + 4);
	float colors[4][3];
	int i, k;

	colors[0][0] = ((c0 >> 11) & 31) / 31.0f; colors[0][1] = ((c0 >> 5) & 63) / 63.0f; colors[0][2] = (c0 & 31) / 31.0f;
	colors[1][0] = ((c1 >> 11) & 31) / 31.0f; colors[1][1] = ((c1 >> 5) & 63) / 63.0f; colors[1][2] = (c1 & 31) / 31.0f;
	for (k = 0; k < 3; k++)
	{
		{
			colors[2][k] = (2.0f * colors[0][k] + colors[1][k]) / 3.0f;
			colors[3][k] = (colors[0][k] + 2.0f * colors[1][k]) / 3.0f;
		}
	}
	for (i = 0; i < 16; i++)
	{
		int index = (int)((bits >> (2 * i)) & 3);

		for (k = 0; k < 3; k++)
			out[i][k] = colors[index][k];
	}
}

/* the lightprobe atlas: its place in the first atlas (baked once the
geometry has said each texel's normal, lightprobe_bake) */
H3_ALIGN static void lightprobe_place(struct build *build)
{
	struct placed_page *p = &build->lightprobe;

	if (!build->lightprobe_slices || !build->intensity_slices)
		return;
	p->atlas = atlas_place(build, LIGHTPROBE_KEPT, LIGHTPROBE_KEPT, &p->x, &p->y);
	p->w = p->h = LIGHTPROBE_KEPT;
	p->whole = TRUE;
	if (p->atlas != NONE)
		build->lightprobe_normals = calloc((unsigned long)LIGHTPROBE_KEPT * LIGHTPROBE_KEPT, 3 * sizeof(float));
}

/* a triangle's normals into the texels its lightmap coordinates cover (a
texel of the triangle's rim too: the sampler reads a little past it) */
H3_ALIGN static void lightprobe_normals_add(struct build *build, float const *uv, float const *normals)
{
	float x[3], y[3], area, margin;
	long x0, x1, y0, y1, px, py;
	int k;

	if (!build->lightprobe_normals)
		return;
	for (k = 0; k < 3; k++)
	{
		x[k] = uv[2 * k] * LIGHTPROBE_KEPT - 0.5f;
		y[k] = uv[2 * k + 1] * LIGHTPROBE_KEPT - 0.5f;
	}
	area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
	if (fabsf(area) < 1e-4f)
		return;
	x0 = (long)floorf(fminf(x[0], fminf(x[1], x[2]))) - 1;
	x1 = (long)ceilf(fmaxf(x[0], fmaxf(x[1], x[2]))) + 1;
	y0 = (long)floorf(fminf(y[0], fminf(y[1], y[2]))) - 1;
	y1 = (long)ceilf(fmaxf(y[0], fmaxf(y[1], y[2]))) + 1;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 >= LIGHTPROBE_KEPT) x1 = LIGHTPROBE_KEPT - 1;
	if (y1 >= LIGHTPROBE_KEPT) y1 = LIGHTPROBE_KEPT - 1;
	margin = 1.5f * (fabsf(x[1] - x[0]) + fabsf(y[1] - y[0]) + fabsf(x[2] - x[1]) + fabsf(y[2] - y[1]) +
		fabsf(x[0] - x[2]) + fabsf(y[0] - y[2])) / fabsf(area);
	if (margin > 0.5f)
		margin = 0.5f;
	for (py = y0; py <= y1; py++)
	{
		for (px = x0; px <= x1; px++)
		{
			float w1 = ((px - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (py - y[0])) / area;
			float w2 = ((x[1] - x[0]) * (py - y[0]) - (px - x[0]) * (y[1] - y[0])) / area;
			float w0 = 1.0f - w1 - w2;

			if (w0 >= -margin && w1 >= -margin && w2 >= -margin)
			{
				float *o = build->lightprobe_normals + 3 * (py * LIGHTPROBE_KEPT + px);

				/* (the texel's own triangle wins over a neighbour's rim) */
				if (o[0] == 0.0f && o[1] == 0.0f && o[2] == 0.0f || (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f))
				{
					for (k = 0; k < 3; k++)
						o[k] = w0 * normals[k] + w1 * normals[3 + k] + w2 * normals[6 + k];
				}
			}
		}
	}
}

/* two DXT5 slices' block as Halo 3 reads them (lightmap_sampling's
decode_bpp16_luvw): (uvw * 2 - 2) * L, L their alphas times the range */
H3_ALIGN static void luvw_decode(unsigned char const *block0, unsigned char const *block1, float range, float out[16][3])
{
	float c0[16][3], c1[16][3];
	unsigned char a0[16], a1[16];
	int i, k;

	dxt_colors(block0 + 8, c0);
	dxt_colors(block1 + 8, c1);
	bc4_decode(block0, a0);
	bc4_decode(block1, a1);
	for (i = 0; i < 16; i++)
	{
		float l = a0[i] / 255.0f * (a1[i] / 255.0f) * range;

		for (k = 0; k < 3; k++)
			out[i][k] = (c0[i][k] + c1[i][k]) * 2.0f - 2.0f;
		for (k = 0; k < 3; k++)
			out[i][k] *= l;
	}
}

static int float_compare(void const *a, void const *b)
{
	float x = *(float const *)a, y = *(float const *)b;

	return x < y ? -1 : x > y;
}

/* Halo 3's static light at a surface of normal n (unit): its
ravi_order_2_with_dominant_light (the kit's spherical_harmonics.fx, read
for its rule) - the order-2 spherical harmonics less the dominant light,
the dominant light's cosine lobe. sh[c][k]: coefficient c (0 the
constant, 1 2 3 the linear y z x as Halo 3 orders them), channel k;
intensity the dominant light's; its direction the linear terms' luminance */
H3_ALIGN static void ravi_light(float const sh[4][3], float const intensity[3], float const *normal, float light[3])
{
	float d[3], n[3], length, cosine;
	int k;

	d[0] = -(sh[3][0] * 0.212656f + sh[3][1] * 0.715158f + sh[3][2] * 0.0721856f);
	d[1] = -(sh[1][0] * 0.212656f + sh[1][1] * 0.715158f + sh[1][2] * 0.0721856f);
	d[2] = sh[2][0] * 0.212656f + sh[2][1] * 0.715158f + sh[2][2] * 0.0721856f;
	length = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	if (length > 1e-5f)
	{
		d[0] /= length;
		d[1] /= length;
		d[2] /= length;
	}
	else
	{
		d[0] = d[1] = 0.0f;
		d[2] = 1.0f;
	}
	n[0] = d[0];
	n[1] = d[1];
	n[2] = d[2];
	if (normal)
	{
		length = sqrtf(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
		if (length > 1e-3f)
		{
			n[0] = normal[0] / length;
			n[1] = normal[1] / length;
			n[2] = normal[2] / length;
		}
	}
	cosine = d[0] * n[0] + d[1] * n[1] + d[2] * n[2];
	if (cosine < 0.0f)
		cosine = 0.0f;
	for (k = 0; k < 3; k++)
	{
		/* (each channel's linear terms, the dominant light out of them) */
		float lx = sh[3][k] + 0.4886025f * d[0] * intensity[k];
		float ly = sh[1][k] + 0.4886025f * d[1] * intensity[k];
		float lz = -sh[2][k] + 0.4886025f * d[2] * intensity[k];
		float linear = n[0] * lx + n[1] * ly + n[2] * lz;
		float constant = sh[0][k] - 0.2820948f * intensity[k];

		light[k] = (0.886227f * constant - 2.0f * 0.511664f * linear) / 3.1415926535f + cosine * intensity[k] * 0.281f;
	}
}

/* a vertex's light from its per-vertex lighting (15 floats: intensity,
then the coefficients) at its normal */
H3_ALIGN static void vertex_light(float const *vertex_sh, float const *normal, float light[3])
{
	float sh[4][3];
	int c, k;

	for (c = 0; c < 4; c++)
		for (k = 0; k < 3; k++)
			sh[c][k] = vertex_sh[3 + 3 * c + k];
	ravi_light(sh, vertex_sh, normal, light);
}

H3_ALIGN static float detail_multiplier_drawn(void);
H3_ALIGN static float level_albedo(struct build *build);

/* the exposure the sky's maps and colours are made at: the level's, or -
with the frame pass, which shows the sky's pixels from their own exposure -
half the camera fx's least (a sky texel holds no more than 1: the half
leaves it a stop over white at the least exposure) */
H3_ALIGN static float sky_exposure_of(struct build *build)
{
	if (build->sky_exposure > 0.0f)
		return build->sky_exposure;
	return build->exposure > 0.0f ? build->exposure : 1.0f;
}
H3_ALIGN static void bitmap_generated_write(struct build *build, struct bitmap *b);

/* the light's incident direction at a surface as this engine's lightmap
vertex carries it (h2_materials.h: the relief pass, c(1 + a(N.L - Lz))):
Halo 3's dominant light's direction, its length the square root of a -
the dominant light's share of the surface's light (its cosine lobe's
slope over the light: Halo 3 evaluates the whole at the bump's normal,
ravi_order_2_with_dominant_light, whose change with the normal is the
dominant light's alone). A surface of no light keeps its normal */
H3_ALIGN static unsigned long incident_pack(float const sh[4][3], float const intensity[3], float const *normal)
{
	float d[3], light[3], length, total, share;
	int k;

	d[0] = -(sh[3][0] * 0.212656f + sh[3][1] * 0.715158f + sh[3][2] * 0.0721856f);
	d[1] = -(sh[1][0] * 0.212656f + sh[1][1] * 0.715158f + sh[1][2] * 0.0721856f);
	d[2] = sh[2][0] * 0.212656f + sh[2][1] * 0.715158f + sh[2][2] * 0.0721856f;
	length = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	ravi_light(sh, intensity, normal, light);
	total = 0.2126f * light[0] + 0.7152f * light[1] + 0.0722f * light[2];
	if (length < 1e-5f || total <= 1e-4f || getenv("HALO_H3_NO_INCIDENT"))
		return pack_vector(normal);
	share = (0.2126f * intensity[0] + 0.7152f * intensity[1] + 0.0722f * intensity[2]) * 0.281f / total;
	share = share > 1.0f ? 1.0f : share < 0.0f ? 0.0f : share;
	for (k = 0; k < 3; k++)
		d[k] = d[k] / length * sqrtf(share);
	return pack_vector(d);
}

/* the lightprobe's coefficients at a lightmap coordinate (its texel's
4 x 4 block decoded); FALSE when not read */
H3_ALIGN static boolean lightprobe_sample(struct build *build, float u, float v, float sh[4][3], float intensity[3])
{
	long blocks = H3_LIGHTPROBE_SIZE / 4, x, y, t;
	unsigned long slice = (unsigned long)blocks * blocks * 16, at;
	float block_sh[16][3], block_intensity[16][3];
	int c, k;

	if (!build->lightprobe_slices || !build->intensity_slices)
		return FALSE;
	x = (long)(u * H3_LIGHTPROBE_SIZE);
	y = (long)(v * H3_LIGHTPROBE_SIZE);
	x = x < 0 ? 0 : x >= H3_LIGHTPROBE_SIZE ? H3_LIGHTPROBE_SIZE - 1 : x;
	y = y < 0 ? 0 : y >= H3_LIGHTPROBE_SIZE ? H3_LIGHTPROBE_SIZE - 1 : y;
	at = 16UL * ((y / 4) * blocks + x / 4);
	t = (y & 3) * 4 + (x & 3);
	for (c = 0; c < 4; c++)
	{
		luvw_decode(build->lightprobe_slices + slice * (2 * c) + at, build->lightprobe_slices + slice * (2 * c + 1) + at,
			build->lightprobe_ranges[c], block_sh);
		for (k = 0; k < 3; k++)
			sh[c][k] = block_sh[t][k];
	}
	luvw_decode(build->intensity_slices + at, build->intensity_slices + slice + at, build->lightprobe_ranges[4], block_intensity);
	for (k = 0; k < 3; k++)
		intensity[k] = block_intensity[t][k];
	return TRUE;
}

/* the exposure the light is drawn at (the texels' light times it).
Halo 3 exposes each frame automatically (exposure_downsample.hlsl: the
frame's mean log intensity) toward its camera fx's target, within the
level's least and most stops (cfxs: 0x10, 0x14 stops, 0x18 the target's
log2 - the owner's maps: Last Resort 0..3 and -4.47, Valhalla 0..3 and
-4.84, Guardian 1.25..2.5, Narrows -0.5..3, Foundry 0.5..1.5). Here once
for the level, the exposure that brings its middle - its lit texels'
median light times its surfaces' albedo (level_albedo) - to the target:
Last Resort comes out at its least stop, 1, where the owner's MCC frames
of its sand and seawall are met (measured); Valhalla 1, Guardian 2.8,
Narrows 1.2, Foundry 2.2, Sandtrap its least, 1.19.
HALO_H3_LIGHTMAP_SCALE a fixed one (a test), HALO_H3_SCENE_ALBEDO a fixed
albedo */
H3_ALIGN static float exposure_auto(struct build *build, float median)
{
	float albedo = getenv("HALO_H3_SCENE_ALBEDO") ? env_float("HALO_H3_SCENE_ALBEDO", 0.15f) : level_albedo(build), stops;

	if (albedo <= 0.0f)
		albedo = 0.15f;
	if (build->exposure_set || !build->exposure_known || median <= 0.0f)
		return build->lightmap_scale;
	stops = build->exposure_range[2] - log2f(albedo * median);
	if (stops < build->exposure_range[0])
		stops = build->exposure_range[0];
	if (stops > build->exposure_range[1])
		stops = build->exposure_range[1];
	return exp2f(stops);
}

/* the level's albedo (linear, as Halo 3 lights it): its surfaces' base
maps' mean colours as drawn (squared back to light), weighted by area -
the drawn mean is the base's (its detail averages out), times its lit
factor (a detail's mean, mode 3); terrains' composites, water and the
sky's left out. 0 none known */
H3_ALIGN static float level_albedo(struct build *build)
{
	double area_sum = 0.0, albedo_sum = 0.0;
	long i, t;

	for (i = 0; i < build->ce_material_count; i++)
	{
		struct material const *m = &build->materials[i];
		struct shader const *sh = m->shader >= 0 && m->shader < build->shader_count ? &build->shaders[m->shader] : NULL;
		struct bitmap const *b = sh && sh->bitmap != NONE ? &build->bitmaps[sh->bitmap] : NULL;
		double area = 0.0;
		float drawn;

		if (!b || b->composite || b->generated || sh->water || sh->dome || sh->group != 'senv' || b->mean[3] <= 0.0f)
			continue;
		drawn = b->mean[3];
		if (b->mode == 3 && b->scale_source != NONE)
			drawn *= detail_multiplier_drawn() * build->bitmaps[b->scale_source].mean[3];
		for (t = 0; t < m->triangle_count; t++)
		{
			unsigned char const *tri = m->triangles.data + 6 * t;
			float p[3][3], u[3], v[3], c[3];
			int k, j;

			for (k = 0; k < 3; k++)
			{
				unsigned short index = (unsigned short)read16(tri + 2 * k);

				for (j = 0; j < 3; j++)
					p[k][j] = readf(m->vertices.data + (unsigned long)index * COMPRESSED_VERTEX_SIZE + 4 * j);
			}
			for (j = 0; j < 3; j++)
			{
				u[j] = p[1][j] - p[0][j];
				v[j] = p[2][j] - p[0][j];
			}
			c[0] = u[1] * v[2] - u[2] * v[1];
			c[1] = u[2] * v[0] - u[0] * v[2];
			c[2] = u[0] * v[1] - u[1] * v[0];
			area += 0.5 * sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
		}
		area_sum += area;
		albedo_sum += area * drawn * drawn;
	}
	return area_sum > 0.0 ? (float)(albedo_sum / area_sum) : 0.0f;
}

/* a texel's light kept for later (per-vertex lighting's: the exposure is
the lightprobe's, lightprobe_bake) */
H3_ALIGN static void light_pending(struct build *build, short atlas, long x, long y, float const light[3])
{
	struct atlas *a = &build->atlases[atlas];
	struct atlas_pending_light *e;

	if (a->pending_count >= a->pending_capacity)
	{
		long capacity = a->pending_capacity ? 2 * a->pending_capacity : 4096;
		struct atlas_pending_light *grown = realloc(a->pending, (size_t)capacity * sizeof(*grown));

		if (!grown)
			return;
		a->pending = grown;
		a->pending_capacity = capacity;
	}
	e = &a->pending[a->pending_count++];
	e->index = y * LIGHTMAP_ATLAS + x;
	memcpy(e->light, light, 3 * sizeof(float));
}

/* the lightprobe atlas baked: each texel's light as Halo 3's static lighting
makes it at that texel's surface's normal (ravi_light). The bump maps'
normals are not here: this engine's lightmap is one colour a texel. A texel
of no surface takes its dominant light's direction. Then the exposure
(exposure_auto), then every texel encoded (light_texel), the per-vertex
lighting's too */
H3_ALIGN static void lightprobe_bake(struct build *build)
{
	struct placed_page *p = &build->lightprobe;
	long blocks = H3_LIGHTPROBE_SIZE / 4, bx, by, i;
	unsigned long slice = (unsigned long)blocks * blocks * 16;
	float ambient = env_float("HALO_H3_LIGHTMAP_AMBIENT", 0.0f);
	float *grown, *light_stats = NULL, *lights = NULL, exposure;
	long stats_count = 0, a;
	boolean debug = getenv("HALO_H3_VERTEX_DEBUG") != NULL;

	boolean baking = p->atlas != NONE && p->whole && build->lightprobe_slices;
	/* (h3vita) the texels' light in two passes - the first for the exposure's
	statistics, the second computing each texel again and encoding it at once
	- rather than every texel's light held in 12 MB of floats between the
	two: the same floats come out of the same inputs, the lightmap is the
	same byte for byte, the probe sampling runs twice (the Vita tier's, where
	the make's memory is the blocker; HALO_H3_LIGHT_TWO_PASS 0/1 either tier) */
	boolean two_pass = getenv("HALO_H3_LIGHT_TWO_PASS") ? atoi(getenv("HALO_H3_LIGHT_TWO_PASS")) != 0 : H2_BUILD_HEAP != 0;
	int round;

	if (baking)
	{
		lights = two_pass ? NULL : malloc((unsigned long)LIGHTPROBE_KEPT * LIGHTPROBE_KEPT * 3 * sizeof(float));
		light_stats = malloc((unsigned long)LIGHTPROBE_KEPT * LIGHTPROBE_KEPT * sizeof(float));
		if (!two_pass && !lights)
			baking = FALSE;
	}
	/* (two rounds of a chart's normals past its rim: each texel with no normal
	takes the sum of its eight neighbours' as they were before the round. The
	rows before and at the one worked are kept as they were - 24 KB -, the
	row after is not yet touched: the same sums as from a copy of the whole
	12 MB, h3vita) */
	grown = baking && build->lightprobe_normals ? malloc(2 * (unsigned long)LIGHTPROBE_KEPT * 3 * sizeof(float)) : NULL;
	for (i = 0; grown && i < 2; i++)
	{
		long x, y;
		float *row_before = grown, *row_at = grown + 3 * LIGHTPROBE_KEPT;

		memcpy(row_before, build->lightprobe_normals, (unsigned long)LIGHTPROBE_KEPT * 3 * sizeof(float));
		for (y = 1; y + 1 < LIGHTPROBE_KEPT; y++)
		{
			float *swap;

			memcpy(row_at, build->lightprobe_normals + 3 * (y * LIGHTPROBE_KEPT), (unsigned long)LIGHTPROBE_KEPT * 3 * sizeof(float));
			for (x = 1; x + 1 < LIGHTPROBE_KEPT; x++)
			{
				float const *q = row_at + 3 * x;
				float *o = build->lightprobe_normals + 3 * (y * LIGHTPROBE_KEPT + x);
				long dx, dy;

				if (q[0] != 0.0f || q[1] != 0.0f || q[2] != 0.0f)
					continue;
				for (dy = -1; dy <= 1; dy++)
				{
					for (dx = -1; dx <= 1; dx++)
					{
						float const *n = dy < 0 ? row_before + 3 * (x + dx) : dy == 0 ? row_at + 3 * (x + dx) :
							build->lightprobe_normals + 3 * ((y + 1) * LIGHTPROBE_KEPT + x + dx);

						o[0] += n[0];
						o[1] += n[1];
						o[2] += n[2];
					}
				}
			}
			swap = row_before;
			row_before = row_at;
			row_at = swap;
		}
	}
	free(grown);
	exposure = build->lightmap_scale;
	build->exposure = exposure;
	for (round = 0; baking && round < (two_pass ? 2 : 1); round++)
	{
		for (by = 0; by < blocks; by++)
		{
			for (bx = 0; bx < blocks; bx++)
			{
				unsigned long at = 16UL * (by * blocks + bx);
				float sh[4][16][3], intensity[16][3];
				int c, t;

				for (c = 0; c < 4; c++)
				{
					luvw_decode(build->lightprobe_slices + slice * (2 * c) + at, build->lightprobe_slices + slice * (2 * c + 1) + at,
						build->lightprobe_ranges[c], sh[c]);
				}
				luvw_decode(build->intensity_slices + at, build->intensity_slices + slice + at, build->lightprobe_ranges[4], intensity);
				for (t = 0; t < 16; t++)
				{
					long x = bx * 4 + (t & 3), y = by * 4 + (t >> 2);
					float const *normal = build->lightprobe_normals ? build->lightprobe_normals + 3 * (y * LIGHTPROBE_KEPT + x) : NULL;
					float texel_sh[4][3], own[3], *light = lights ? lights + 3 * (y * LIGHTPROBE_KEPT + x) : own;
					int k;

					for (c = 0; c < 4; c++)
						for (k = 0; k < 3; k++)
							texel_sh[c][k] = sh[c][t][k];
					ravi_light(texel_sh, intensity[t], normal, light);
					if (round == 1)
					{
						unsigned char *o = build->atlases[p->atlas].rgb + 3 * ((p->y + y) * LIGHTMAP_ATLAS + p->x + x);

						for (k = 0; k < 3; k++)
							o[k] = light_texel(light[k] * exposure + ambient);
						continue;
					}
					if (debug && (x % 128) == 64 && (y % 128) == 64 && normal && (normal[0] != 0.0f || normal[1] != 0.0f))
						platform_log("h3 texel light: %ld %ld normal %.2f %.2f %.2f light %.3f %.3f %.3f I %.3f %.3f %.3f sh0 %.3f %.3f %.3f dir %.2f %.2f %.2f", x, y,
							normal[0], normal[1], normal[2], light[0], light[1], light[2], intensity[t][0], intensity[t][1], intensity[t][2],
							texel_sh[0][0], texel_sh[0][1], texel_sh[0][2], -texel_sh[3][1], -texel_sh[1][1], texel_sh[2][1]);
					/* (the median of the texels of a surface: the exposure's) */
					if (light_stats && normal && (normal[0] != 0.0f || normal[1] != 0.0f || normal[2] != 0.0f))
						light_stats[stats_count++] = 0.2126f * light[0] + 0.7152f * light[1] + 0.0722f * light[2];
				}
			}
		}
		if (round == 0 && light_stats && stats_count)
		{
			qsort(light_stats, (size_t)stats_count, sizeof(float), float_compare);
			exposure = exposure_auto(build, light_stats[stats_count / 2]);
			/* (h3/light-1) with the frame pass (h3_frame.c) the exposure is the
			frame's own, set there every frame; the bake's only places the
			light in the texel's range - a texel holds up to 4 of light times
			exposure (the doubling pass), in 8 bits of its square root: the
			exposure that puts the level's brightest texels (their 99.5th
			percentile) just under the top, so the sun-lit ones keep their
			light when the frame is shown darker than the bake and the shade's
			steps are the smallest they can be. HALO_H3_LIGHTMAP_SCALE still
			fixes it */
			if (h3_frame_enabled() && !build->exposure_set && build->exposure_known)
			{
				float bright = light_stats[(long)((double)stats_count * 0.995)];

				exposure = env_float("HALO_H3_BAKE_HEADROOM", 3.6f) / (bright > 0.001f ? bright : 0.001f);
				if (exposure < 0.25f)
					exposure = 0.25f;
				if (exposure > 64.0f)
					exposure = 64.0f;
				platform_log("h3 level: lighting: the bake's exposure %.2f (the brightest texels' light %.3f); the frame pass sets the frame's", exposure, bright);
			}
			build->exposure = exposure;
			platform_log("h3 level: lighting: %ld lit texels, median light %.3f (p90 %.3f, p99 %.3f), albedo %.3f; exposure %.2f (stops %.2f..%.2f, target %.2f)%s%s",
				stats_count, light_stats[stats_count / 2], light_stats[stats_count * 9 / 10], light_stats[stats_count * 99 / 100], level_albedo(build), exposure,
				build->exposure_range[0], build->exposure_range[1], build->exposure_range[2], build->exposure_set ? " (set)" : "",
				two_pass ? " (two passes)" : "");
		}
		if (round == 0)
		{
			free(light_stats);
			light_stats = NULL;
		}
	}
	free(light_stats);
	for (i = 0; lights && i < (long)LIGHTPROBE_KEPT * LIGHTPROBE_KEPT; i++)
	{
		long x = i % LIGHTPROBE_KEPT, y = i / LIGHTPROBE_KEPT;
		unsigned char *o = build->atlases[p->atlas].rgb + 3 * ((p->y + y) * LIGHTMAP_ATLAS + p->x + x);
		int k;

		for (k = 0; k < 3; k++)
			o[k] = light_texel(lights[3 * i + k] * exposure + ambient);
	}
	free(lights);
	/* (the per-vertex lighting's texels) */
	for (a = 0; a < build->atlas_count; a++)
	{
		struct atlas *atlas = &build->atlases[a];

		for (i = 0; i < atlas->pending_count; i++)
		{
			struct atlas_pending_light const *e = &atlas->pending[i];
			int k;

			for (k = 0; k < 3; k++)
				atlas->rgb[3 * e->index + k] = light_texel(e->light[k] * exposure + ambient);
		}
		if (atlas->pending && getenv("HALO_H3_HEAP_LOG"))
			platform_log("h3 level: lighting: atlas %ld: %ld texels of per-vertex light held (%lu KB)", a, atlas->pending_count,
				(unsigned long)atlas->pending_capacity * sizeof(*atlas->pending) / 1024);
		free(atlas->pending);
		atlas->pending = NULL;
		atlas->pending_count = atlas->pending_capacity = 0;
	}
}

/* ---------- private code: the objects' light probes (h3_object_light.c) */

#if !defined(HALO_VITA) && !defined(HALO_GXM_NULL)
/* the level's lightprobe texture kept after its make (a copy: 10 MB), for
the light under an object */
static struct
{
	unsigned char *lightprobe_slices, *intensity_slices;
	float ranges[5];
} object_lightprobe;

boolean h3_level_lightprobe_at(float u, float v, float sh[4][3], float intensity[3])
{
	long blocks = H3_LIGHTPROBE_SIZE / 4, x, y, t;
	unsigned long slice = (unsigned long)blocks * blocks * 16, at;
	float block_sh[16][3];
	int c, k;

	if (!object_lightprobe.lightprobe_slices || !object_lightprobe.intensity_slices)
		return FALSE;
	x = (long)(u * H3_LIGHTPROBE_SIZE);
	y = (long)(v * H3_LIGHTPROBE_SIZE);
	x = x < 0 ? 0 : x >= H3_LIGHTPROBE_SIZE ? H3_LIGHTPROBE_SIZE - 1 : x;
	y = y < 0 ? 0 : y >= H3_LIGHTPROBE_SIZE ? H3_LIGHTPROBE_SIZE - 1 : y;
	at = 16UL * ((y / 4) * blocks + x / 4);
	t = (y & 3) * 4 + (x & 3);
	for (c = 0; c < 4; c++)
	{
		luvw_decode(object_lightprobe.lightprobe_slices + slice * (2 * c) + at,
			object_lightprobe.lightprobe_slices + slice * (2 * c + 1) + at, object_lightprobe.ranges[c], block_sh);
		for (k = 0; k < 3; k++)
			sh[c][k] = block_sh[t][k];
	}
	luvw_decode(object_lightprobe.intensity_slices + at, object_lightprobe.intensity_slices + slice + at,
		object_lightprobe.ranges[4], block_sh);
	for (k = 0; k < 3; k++)
		intensity[k] = block_sh[t][k];
	return TRUE;
}

/* a 16-bit float */
H3_ALIGN static float half_value(unsigned char const *p)
{
	unsigned long h = (unsigned long)p[0] | ((unsigned long)p[1] << 8);
	long exponent = (long)((h >> 10) & 31);
	float mantissa = (float)(h & 1023) / 1024.0f, value;

	if (exponent == 0)
		value = ldexpf(mantissa, -14);
	else if (exponent == 31)
		value = 0.0f;
	else
		value = ldexpf(1.0f + mantissa, (int)exponent - 15);
	return (h & 0x8000) ? -value : value;
}

/* the scenario lightmap's (sLdT) probes given to h3_object_light.c: the air
probes at 0x1C (0x5C each: position, name, flags, then the probe) and the
scenery probes at 0x28 (0x50 each: the placement's object id - unique id,
origin BSP, type, source -, then the probe). A probe is 36 16-bit floats:
the dominant light's direction and a pad, its intensity and a pad, nine
red, nine green and nine blue coefficients, a pad (the kit's tag names
them; the directions read are unit vectors and the linear coefficients'
luminance points along them). A scenery probe's place is its placement's
(the scenario's scenery, its object id at 0x38). The device machines'
probes (0x34) are not read. Then the lightprobe texture kept */
H3_ALIGN static void object_light_gather(struct build *build)
{
	struct h3_cache *cache = build->cache;
	long sldt = h3_cache_find_tag(cache, 'sLdT', NULL), count, i, air = 0, scenery = 0, unplaced = 0;
	long placement_count = 0;
	unsigned long elements, placements = 0;
	struct h3_cache_tag t;
	int pass;

	h3_object_light_begin();
	free(object_lightprobe.lightprobe_slices);
	free(object_lightprobe.intensity_slices);
	object_lightprobe.lightprobe_slices = object_lightprobe.intensity_slices = NULL;
	if (getenv("HALO_H3_OBJECT_LIGHT") && !atoi(getenv("HALO_H3_OBJECT_LIGHT")))
		return;
	h3_cache_block(cache, build->scenario + H3_SCNR_SCENERY, 0xB4, &placement_count, &placements);
	for (pass = 0; pass < 2 && sldt != NONE && h3_cache_tag_get(cache, sldt, &t); pass++)
	{
		unsigned long size = pass ? 0x50 : 0x5C, probe_at = pass ? 8 : 0x14;

		if (!h3_cache_block(cache, t.address + (pass ? 0x28 : 0x1C), size, &count, &elements))
			continue;
		for (i = 0; i < count; i++)
		{
			unsigned char e[0x5C];
			float position[3], direction[3], intensity[3], terms[9][3];
			int c, k;

			if (!h3_cache_read(cache, elements + size * i, size, e))
				continue;
			for (k = 0; k < 3; k++)
			{
				direction[k] = half_value(e + probe_at + 2 * k);
				intensity[k] = half_value(e + probe_at + 8 + 2 * k);
				for (c = 0; c < 9; c++)
					terms[c][k] = half_value(e + probe_at + 16 + 2 * (9 * k + c));
			}
			if (!pass)
			{
				memcpy(position, e, 12);
				air++;
			}
			else
			{
				long q;

				for (q = 0; q < placement_count; q++)
				{
					unsigned char p[0x40];

					if (h3_cache_read(cache, placements + 0xB4 * q, 0x40, p) && !memcmp(p + 0x38, e, 4))
					{
						memcpy(position, p + 8, 12);
						break;
					}
				}
				if (q == placement_count)
				{
					unplaced++;
					continue;
				}
				scenery++;
			}
			h3_object_light_probe_add(pass ? _h3_object_light_probe_scenery : _h3_object_light_probe_air, position, direction,
				intensity, terms);
		}
	}
	/* (the instances placed as objects: their per-vertex lighting's mean,
	order 2 with the dominant light as the level's surfaces') */
	for (i = 0; i < build->instance_probe_count; i++)
	{
		float const *o = build->instance_probes + 18 * i;
		float terms[9][3], direction[3] = { 0.0f, 0.0f, 1.0f };
		int c, k;

		memset(terms, 0, sizeof(terms));
		for (c = 0; c < 4; c++)
			for (k = 0; k < 3; k++)
				terms[c][k] = o[6 + 3 * c + k];
		h3_object_light_probe_add(_h3_object_light_probe_instance, o, direction, o + 3, terms);
	}
	if (build->lightprobe_slices && build->intensity_slices && build->lightprobe.atlas != NONE && build->lightprobe.whole &&
		build->lightprobe.x == 0 && build->lightprobe.y == 0 && build->lightprobe.w == LIGHTMAP_ATLAS)
	{
		unsigned long texels = (unsigned long)H3_LIGHTPROBE_SIZE * H3_LIGHTPROBE_SIZE;

		object_lightprobe.lightprobe_slices = malloc(texels * H3_LIGHTPROBE_SLICES);
		object_lightprobe.intensity_slices = malloc(texels * H3_INTENSITY_SLICES);
		if (object_lightprobe.lightprobe_slices && object_lightprobe.intensity_slices)
		{
			memcpy(object_lightprobe.lightprobe_slices, build->lightprobe_slices, texels * H3_LIGHTPROBE_SLICES);
			memcpy(object_lightprobe.intensity_slices, build->intensity_slices, texels * H3_INTENSITY_SLICES);
			memcpy(object_lightprobe.ranges, build->lightprobe_ranges, sizeof(object_lightprobe.ranges));
		}
		else
		{
			free(object_lightprobe.lightprobe_slices);
			free(object_lightprobe.intensity_slices);
			object_lightprobe.lightprobe_slices = object_lightprobe.intensity_slices = NULL;
		}
	}
	h3_object_light_end(build->exposure, build->lightprobe.atlas);
	platform_log("h3 level: objects' light: %ld air probes, %ld scenery probes at their placements (%ld with no placement), %ld instances' per-vertex lighting, the lightprobe texture %s, exposure %.2f",
		air, scenery, unplaced, build->instance_probe_count, object_lightprobe.lightprobe_slices ? "kept" : "not kept", build->exposure);
}
#endif

/* ---------- private code: render geometry */

H3_ALIGN static struct material *material_for(struct build *build, short shader, short atlas, long vertices, long triangles)
{
	long i;
	struct material *m;

	for (i = build->ce_material_count - 1; i >= 0; i--)
	{
		m = &build->materials[i];
		if (m->shader == shader && m->atlas == atlas)
		{
			if (m->vertex_count + vertices <= MAXIMUM_MATERIAL_VERTICES &&
				m->triangle_count + triangles <= MAXIMUM_MATERIAL_SURFACES)
			{
				return m;
			}
			break;
		}
	}
	if (build->ce_material_count >= build->ce_material_capacity)
	{
		long capacity = build->ce_material_capacity ? build->ce_material_capacity * 2 : 64;
		struct material *grown = realloc(build->materials, capacity * sizeof(*grown));

		if (!grown)
			return NULL;
		build->materials = grown;
		build->ce_material_capacity = capacity;
	}
	m = &build->materials[build->ce_material_count++];
	memset(m, 0, sizeof(*m));
	m->shader = shader;
	m->atlas = atlas;

	return m;
}

/* a section's parts added to the materials, in cluster, transformed by
matrix (forward, left, up, position, scale) when it is placed: its texture
coordinates scaled by each shader's base map scale; its lightmap
coordinates the lightprobe atlas's (page whole) or the page's middle */
H3_ALIGN static void geometry_add(struct build *build, struct section const *s, short cluster, float const *matrix,
	struct placed_page const *page)
{
	long p;

	for (p = 0; p < s->part_count; p++)
	{
		struct section_part const *part = &s->parts[p];
		short shader;
		long first = part->first, count = part->count, t, n;
		long *remap;
		struct material *m;
		long used = 0;

		if (part->material < 0 || part->material >= build->material_count || count < 3)
		{
			if (getenv("HALO_H3_DEBUG"))
				fprintf(stderr, "h3 level: dropped part: material %d of %ld, %ld triangles\n", part->material, build->material_count, count / 3);
			continue;
		}
		shader = build->material_shader[part->material];
		if (shader == NONE)
		{
			if (getenv("HALO_H3_DEBUG"))
				fprintf(stderr, "h3 level: dropped part: material %d has no shader, %ld triangles\n", part->material, count / 3);
			continue;
		}
		if (first < 0 || first > s->index_count || count > s->index_count - first)
			continue;
		/* (tests: the shaders named left out) */
		if (getenv("HALO_H3_SKIP") && strstr(getenv("HALO_H3_SKIP"), tag_short_name(build->shaders[shader].path)))
			continue;
		count -= count % 3;
		if (cluster >= 0 && cluster < 512)
			build->cluster_surfaces[cluster] += count / 3;
		if (getenv("HALO_H3_DEBUG"))
			fprintf(stderr, "h3 level: kept part: shader %d %s, cluster %d, %s, %ld triangles\n", shader,
				tag_short_name(build->shaders[shader].path), cluster, matrix ? "instance" : "cluster", count / 3);
		remap = malloc(s->vertex_count * sizeof(long));
		if (!remap)
			continue;
		for (n = 0; n < s->vertex_count; n++)
			remap[n] = -1;
		for (t = 0; t < count; t++)
		{
			long v = s->indices[first + t];

			if (remap[v] < 0)
				remap[v] = used++;
		}
		m = material_for(build, shader, page->atlas, used, count / 3);
		if (!m)
		{
			free(remap);
			build_fail(build, "out of memory");
			return;
		}
		{
			long base = m->vertex_count;
			unsigned char *out = bytes_grow(&m->vertices, (unsigned long)used * COMPRESSED_VERTEX_SIZE);
			unsigned char *lightmap_out = bytes_grow(&m->lightmap_vertices,
				(unsigned long)used * COMPRESSED_LIGHTMAP_VERTEX_SIZE);
			float const *scale = build->shaders[shader].base_scale;
			float *bake_normals = page->whole && s->lightmap_coordinates && build->lightprobe_normals ?
				malloc((unsigned long)used * 3 * sizeof(float)) : NULL;

			if (!out || !lightmap_out)
			{
				free(bake_normals);
				free(remap);
				build_fail(build, "out of memory");
				return;
			}
			for (n = 0; n < s->vertex_count; n++)
			{
				unsigned char *o, *lo;
				float position[3], lu, lv;
				unsigned long packed[3];
				int k;

				if (remap[n] < 0)
					continue;
				o = out + remap[n] * COMPRESSED_VERTEX_SIZE;
				lo = lightmap_out + remap[n] * COMPRESSED_LIGHTMAP_VERTEX_SIZE;
				for (k = 0; k < 3; k++)
				{
					position[k] = s->positions[3 * n + k];
					packed[k] = s->packed[3 * n + k];
				}
				/* (water's: straight up - its faces' would show each facet) */
				if (build->shaders[shader].group == 'swat')
				{
					static float const up[3] = { 0.0f, 0.0f, 1.0f }, x_axis[3] = { 1.0f, 0.0f, 0.0f }, y_axis[3] = { 0.0f, 1.0f, 0.0f };

					packed[0] = pack_vector(up);
					packed[1] = pack_vector(y_axis);
					packed[2] = pack_vector(x_axis);
				}
				if (matrix)
				{
					float world[3], vector[3], turned[3];
					int axis;

					for (k = 0; k < 3; k++)
					{
						world[k] = matrix[9 + k] + matrix[12] *
							(position[0] * matrix[k] + position[1] * matrix[3 + k] + position[2] * matrix[6 + k]);
					}
					memcpy(position, world, sizeof(world));
					for (axis = 0; axis < 3; axis++)
					{
						unpack_vector(packed[axis], vector);
						for (k = 0; k < 3; k++)
							turned[k] = vector[0] * matrix[k] + vector[1] * matrix[3 + k] + vector[2] * matrix[6 + k];
						packed[axis] = pack_vector(turned);
					}
				}
				for (k = 0; k < 3; k++)
				{
					writef(o + 4 * k, position[k]);
					write32(o + 12 + 4 * k, packed[k]);
					m->centroid[k] += position[k];
				}
				writef(o + 24, s->coordinates[2 * n] * scale[0]);
				writef(o + 28, s->coordinates[2 * n + 1] * scale[1]);
				lu = page->whole && s->lightmap_coordinates ? s->lightmap_coordinates[2 * n] : 0.5f;
				lv = page->whole && s->lightmap_coordinates ? s->lightmap_coordinates[2 * n + 1] : 0.5f;
				if (lu < 0.0f) lu = 0.0f;
				if (lu > 1.0f) lu = 1.0f;
				if (lv < 0.0f) lv = 0.0f;
				if (lv > 1.0f) lv = 1.0f;
				lu = (page->x + lu * page->w) / (float)LIGHTMAP_ATLAS;
				lv = (page->y + lv * page->h) / (float)LIGHTMAP_ATLAS;
				if (bake_normals)
				{
					float normal[3];

					unpack_vector(packed[0], normal);
					memcpy(bake_normals + 3 * remap[n], normal, sizeof(normal));
				}
				/* (its incident direction: the lightprobe's dominant light there) */
				{
					float sh[4][3], intensity[3], normal[3];
					unsigned long incident = packed[0];

					if (page->whole && s->lightmap_coordinates &&
						lightprobe_sample(build, s->lightmap_coordinates[2 * n], s->lightmap_coordinates[2 * n + 1], sh, intensity))
					{
						unpack_vector(packed[0], normal);
						incident = incident_pack(sh, intensity, normal);
					}
					write32(lo, incident);
				}
				write16(lo + 4, (long)(lu * 32767.0f + 0.5f));
				write16(lo + 6, (long)(lv * 32767.0f + 0.5f));
			}
			/* the triangles: Halo 3's counter-clockwise, this engine's clockwise */
			for (t = 0; t < count; t += 3)
			{
				unsigned char triangle[6];
				long a = remap[s->indices[first + t]];
				long b = remap[s->indices[first + t + 1]];
				long c = remap[s->indices[first + t + 2]];
				float bounds[6];
				int k;

				if (a == b || b == c || a == c)
					continue;
				if (bake_normals)
				{
					float uv[6], normals[9];
					long const corners[3] = { s->indices[first + t], s->indices[first + t + 1], s->indices[first + t + 2] };
					long const used_corners[3] = { a, b, c };

					for (k = 0; k < 3; k++)
					{
						uv[2 * k] = s->lightmap_coordinates[2 * corners[k]];
						uv[2 * k + 1] = s->lightmap_coordinates[2 * corners[k] + 1];
						memcpy(normals + 3 * k, bake_normals + 3 * used_corners[k], 3 * sizeof(float));
					}
					lightprobe_normals_add(build, uv, normals);
				}
				write16(triangle, base + a);
				write16(triangle + 2, base + c);
				write16(triangle + 4, base + b);
				bytes_put(&m->triangles, triangle, 6);
				bytes_put(&m->clusters, &cluster, sizeof(cluster));
				for (k = 0; k < 3; k++)
				{
					float pa = readf(out + a * COMPRESSED_VERTEX_SIZE + 4 * k);
					float pb = readf(out + b * COMPRESSED_VERTEX_SIZE + 4 * k);
					float pc = readf(out + c * COMPRESSED_VERTEX_SIZE + 4 * k);

					bounds[k] = fminf(pa, fminf(pb, pc));
					bounds[3 + k] = fmaxf(pa, fmaxf(pb, pc));
				}
				bytes_put(&m->bounds, bounds, sizeof(bounds));
				m->triangle_count++;
				build->result->surfaces++;
			}
			m->vertex_count += used;
			build->result->vertices += used;
			free(bake_normals);
		}
		free(remap);
	}
}

/* a section's parts added with its per-vertex lighting (Halo 3's
static_per_vertex: each vertex's own order-2 light, vertex_sh 15 floats a
vertex): each triangle its own 2 x 2 texels of a block of the atlas, its
corners' light at three of them (the light at the vertex's normal, placed)
and the fourth their plane's (b + c - a), its lightmap coordinates the
three texels' centres - the bilinear filter then draws the light across
the triangle as Halo 3's interpolation does. Its vertices are its
triangles' corners, unshared. FALSE when the atlas has no room (the
caller's patch then) */
H3_ALIGN static boolean geometry_add_per_vertex(struct build *build, struct section const *s, short cluster, float const *matrix,
	float const *vertex_sh)
{
	long p;
	boolean added = FALSE;

	for (p = 0; p < s->part_count; p++)
	{
		struct section_part const *part = &s->parts[p];
		long first = part->first, count = part->count, t, triangles, all, columns, rows, bx, by, done;
		short shader, atlas;
		struct material *m;
		unsigned char *out, *lightmap_out;
		long base;
		float const *scale;

		if (part->material < 0 || part->material >= build->material_count || count < 3 ||
			first < 0 || first > s->index_count || count > s->index_count - first)
		{
			continue;
		}
		shader = build->material_shader[part->material];
		if (shader == NONE || (getenv("HALO_H3_SKIP") && strstr(getenv("HALO_H3_SKIP"), tag_short_name(build->shaders[shader].path))))
			continue;
		all = (count - count % 3) / 3;
		/* (in pieces a material holds: 3 vertices a triangle) */
		for (; all > 0; all -= triangles, first += 3 * triangles)
		{
		done = 0;
		triangles = all < MAXIMUM_MATERIAL_VERTICES / 3 - 1 ? all : MAXIMUM_MATERIAL_VERTICES / 3 - 1;
		columns = (long)ceilf(sqrtf((float)triangles));
		rows = (triangles + columns - 1) / columns;
		atlas = atlas_place(build, 2 * columns, 2 * rows, &bx, &by);
		if (atlas == NONE)
		{
			/* (no room: nothing added yet, the caller's patch; else this
			piece left out, logged) */
			if (!added)
				return FALSE;
			platform_log("h3 level: no lightmap room for %ld triangles of per-vertex light: left out", triangles);
			continue;
		}
		added = TRUE;
		if (cluster >= 0 && cluster < 512)
			build->cluster_surfaces[cluster] += triangles;
		m = material_for(build, shader, atlas, 3 * triangles, triangles);
		if (!m)
		{
			build_fail(build, "out of memory");
			return FALSE;
		}
		base = m->vertex_count;
		out = bytes_grow(&m->vertices, (unsigned long)3 * triangles * COMPRESSED_VERTEX_SIZE);
		lightmap_out = bytes_grow(&m->lightmap_vertices, (unsigned long)3 * triangles * COMPRESSED_LIGHTMAP_VERTEX_SIZE);
		if (!out || !lightmap_out)
		{
			build_fail(build, "out of memory");
			return FALSE;
		}
		scale = build->shaders[shader].base_scale;
		for (t = 0; t < triangles; t++)
		{
			long cx = bx + 2 * (t % columns), cy = by + 2 * (t / columns);
			float corner_light[3][3], fourth[3], bounds[6];
			unsigned char triangle[6];
			int c, k;

			for (c = 0; c < 3; c++)
			{
				long n = s->indices[first + 3 * t + c];
				unsigned char *o = out + (3 * done + c) * COMPRESSED_VERTEX_SIZE;
				unsigned char *lo = lightmap_out + (3 * done + c) * COMPRESSED_LIGHTMAP_VERTEX_SIZE;
				float position[3], normal[3], lu, lv;
				unsigned long packed[3];

				for (k = 0; k < 3; k++)
				{
					position[k] = s->positions[3 * n + k];
					packed[k] = s->packed[3 * n + k];
				}
				if (matrix)
				{
					float world[3], vector[3], turned[3];
					int axis;

					for (k = 0; k < 3; k++)
					{
						world[k] = matrix[9 + k] + matrix[12] *
							(position[0] * matrix[k] + position[1] * matrix[3 + k] + position[2] * matrix[6 + k]);
					}
					memcpy(position, world, sizeof(world));
					for (axis = 0; axis < 3; axis++)
					{
						unpack_vector(packed[axis], vector);
						for (k = 0; k < 3; k++)
							turned[k] = vector[0] * matrix[k] + vector[1] * matrix[3 + k] + vector[2] * matrix[6 + k];
						packed[axis] = pack_vector(turned);
					}
				}
				for (k = 0; k < 3; k++)
				{
					writef(o + 4 * k, position[k]);
					write32(o + 12 + 4 * k, packed[k]);
					m->centroid[k] += position[k];
				}
				writef(o + 24, s->coordinates[2 * n] * scale[0]);
				writef(o + 28, s->coordinates[2 * n + 1] * scale[1]);
				unpack_vector(packed[0], normal);
				vertex_light(vertex_sh + 15 * n, normal, corner_light[c]);
				if (getenv("HALO_H3_VERTEX_DEBUG") && t == 0 && c == 0)
				{
					float const *v = vertex_sh + 15 * n;
					float d[3] = { -(v[12] * 0.2127f + v[13] * 0.7152f + v[14] * 0.0722f), -(v[6] * 0.2127f + v[7] * 0.7152f + v[8] * 0.0722f),
						v[9] * 0.2127f + v[10] * 0.7152f + v[11] * 0.0722f };

					platform_log("h3 vertex light: at %.1f %.1f %.1f normal %.2f %.2f %.2f light %.3f %.3f %.3f I %.3f %.3f %.3f sh0 %.3f %.3f %.3f dir %.2f %.2f %.2f",
						position[0], position[1], position[2], normal[0], normal[1], normal[2], corner_light[c][0], corner_light[c][1],
						corner_light[c][2], v[0], v[1], v[2], v[3], v[4], v[5], d[0], d[1], d[2]);
				}
				for (k = 0; k < 3; k++)
					if (corner_light[c][k] < 0.0f)
						corner_light[c][k] = 0.0f;
				light_pending(build, atlas, cx + (c == 1), cy + (c == 2), corner_light[c]);
				lu = (cx + (c == 1) + 0.5f) / (float)LIGHTMAP_ATLAS;
				lv = (cy + (c == 2) + 0.5f) / (float)LIGHTMAP_ATLAS;
				{
					float const *v = vertex_sh + 15 * n;
					float sh[4][3];
					int q;

					for (q = 0; q < 4; q++)
						for (k = 0; k < 3; k++)
							sh[q][k] = v[3 + 3 * q + k];
					write32(lo, incident_pack(sh, v, normal));
				}
				write16(lo + 4, (long)(lu * 32767.0f + 0.5f));
				write16(lo + 6, (long)(lv * 32767.0f + 0.5f));
			}
			for (k = 0; k < 3; k++)
			{
				fourth[k] = corner_light[1][k] + corner_light[2][k] - corner_light[0][k];
				if (fourth[k] < 0.0f)
					fourth[k] = 0.0f;
			}
			light_pending(build, atlas, cx + 1, cy + 1, fourth);
			/* the triangle: Halo 3's counter-clockwise, this engine's clockwise */
			write16(triangle, base + 3 * done);
			write16(triangle + 2, base + 3 * done + 2);
			write16(triangle + 4, base + 3 * done + 1);
			bytes_put(&m->triangles, triangle, 6);
			bytes_put(&m->clusters, &cluster, sizeof(cluster));
			for (k = 0; k < 3; k++)
			{
				float pa = readf(out + (3 * done) * COMPRESSED_VERTEX_SIZE + 4 * k);
				float pb = readf(out + (3 * done + 1) * COMPRESSED_VERTEX_SIZE + 4 * k);
				float pc = readf(out + (3 * done + 2) * COMPRESSED_VERTEX_SIZE + 4 * k);

				bounds[k] = fminf(pa, fminf(pb, pc));
				bounds[3 + k] = fmaxf(pa, fmaxf(pb, pc));
			}
			bytes_put(&m->bounds, bounds, sizeof(bounds));
			m->triangle_count++;
			build->result->surfaces++;
			done++;
		}
		m->vertex_count += 3 * done;
		build->result->vertices += 3 * done;
		}
	}
	return TRUE;
}

/* ---------- private code: laying tags out (h2_level.c's) */

struct region
{
	struct bytes b;
	unsigned long link;
};

H3_ALIGN static unsigned long region_alloc(struct region *r, unsigned long size)
{
	unsigned long offset;

	while (r->b.size & 3)
		bytes_grow(&r->b, 1), r->b.data[r->b.size - 1] = 0;
	offset = r->b.size;
	if (bytes_grow(&r->b, size))
		memset(r->b.data + offset, 0, size);

	return offset;
}

H3_ALIGN static unsigned char *region_at(struct region *r, unsigned long offset)
{
	return r->b.data + offset;
}

H3_ALIGN static unsigned long region_block(struct region *r, unsigned long field, long count, unsigned long size)
{
	unsigned long offset = count ? region_alloc(r, (unsigned long)count * size) : 0;

	if (r->b.failed)
		return 0;
	write32(region_at(r, field), (unsigned long)count);
	write32(region_at(r, field + 4), count ? r->link + offset : 0);
	write32(region_at(r, field + 8), 0);

	return offset;
}

H3_ALIGN static void region_data(struct region *r, unsigned long field, void const *data, unsigned long size)
{
	unsigned long offset = size ? region_alloc(r, size) : 0;

	if (r->b.failed)
		return;
	if (size)
		memcpy(region_at(r, offset), data, size);
	write32(region_at(r, field), size);
	write32(region_at(r, field + 4), 0);
	write32(region_at(r, field + 8), 0);
	write32(region_at(r, field + 12), size ? r->link + offset : 0);
	write32(region_at(r, field + 16), 0);
}

static unsigned long *reference_names;
static long reference_name_count;

H3_ALIGN static void region_reference(struct region *r, unsigned long field, unsigned long group, unsigned long id)
{
	unsigned char *p = region_at(r, field);
	long index = (long)(id & 0xFFFF);

	write32(p, id == 0xFFFFFFFFUL ? 0xFFFFFFFFUL : group);
	write32(p + 4, id != 0xFFFFFFFFUL && index < reference_name_count ? reference_names[index] : 0);
	write32(p + 8, 0);
	write32(p + 12, id);
}

/* ---------- private code: collision */

static unsigned long const h3_collision_sizes[8] = { 8, 16, 8, 4, 16, 12, 12, 16 };

/* ODST's large collision BSP (its levels' own: the structure BSP resource's
second block, the first then empty), the layout of the ODST kit's
global_large_collision_bsp_block: 3D nodes of 12 bytes (plane, back child,
front child: longs), planes 16, leaves 8 (flags, 2D reference count, first
2D reference), 2D references 8 (plane, 2D node), 2D nodes 20 (a 2D plane,
left and right child), surfaces 16 (plane, first edge, then the material,
breakable surface set and breakable surface - shorts - and the flags at
14), edges 24 (start and end vertex, forward and reverse edge, left and
right surface), vertices 20 (point, first edge, sink). Its indices are
longs, a child or plane's top bit its flag: this engine's own layout but
for the surfaces' tail and the vertices' sink */
static unsigned long const h3_collision_large_sizes[8] = { 12, 16, 8, 8, 20, 16, 24, 20 };

#define COLLISION_SIZE(bsp, k) ((bsp)->large ? h3_collision_large_sizes[k] : h3_collision_sizes[k])

/* a collision BSP's eight blocks (at control offset field of the collision
resource): the requests for their elements */
H3_ALIGN static boolean collision_request(struct build *build, unsigned long field, struct collision_bsp *bsp,
	struct h3_page_request **requests, long *count, long *capacity, boolean large)
{
	struct h3_resource *r = &build->collision_resource;
	int k;

	memset(bsp, 0, sizeof(*bsp));
	bsp->large = large;
	for (k = 0; k < 8; k++)
	{
		long n = (long)h3_resource_read32(r, field + 12 * k);
		unsigned long kind, at;
		struct h3_page_request *q;

		if (n < 0 || n > 0x800000)
			return FALSE;
		bsp->counts[k] = n;
		if (!n)
			continue;
		if (!h3_resource_pointer(r, field + 12 * k + 4, &kind, &at) || kind != 4 ||
			at > r->size[0] || (unsigned long)n * COLLISION_SIZE(bsp, k) > r->size[0] - at)
		{
			return FALSE;
		}
		if (*count >= *capacity)
		{
			long grown_capacity = *capacity ? *capacity * 2 : 256;
			struct h3_page_request *grown = realloc(*requests, grown_capacity * sizeof(**requests));

			if (!grown)
				return FALSE;
			*requests = grown;
			*capacity = grown_capacity;
		}
		q = &(*requests)[(*count)++];
		memset(q, 0, sizeof(*q));
		q->page = r->page[0];
		q->offset = r->offset[0] + at;
		q->size = (unsigned long)n * COLLISION_SIZE(bsp, k);
		q->context = bsp;
		q->tag = 1000 + k;
	}
	return TRUE;
}

H3_ALIGN static void collision_free(struct collision_bsp *bsp)
{
	int k;

	for (k = 0; k < 8; k++)
		free(bsp->arrays[k]);
	memset(bsp, 0, sizeof(*bsp));
}

/* Halo 3's collision surfaces may have more than eight edges (Edge's and
Narrows' long flat faces: 9 to 23 of them); this engine walks at most
MAXIMUM_EDGES_PER_COLLISION_SURFACE (8: its polygons, prisms and the map
checker's ring test, tag_schema_collision.c). Such a surface - a convex
polygon - is cut here into a fan of pieces of at most eight edges from its
first corner: each cut a new edge (the two pieces either side of it), each
piece after the first a new surface of the same plane, material and flags,
and in every 2D BSP that ends in the surface a chain of new nodes along the
cuts (the line through the cut, in that reference's projection) choosing the
piece. The collision geometry is the same; only its bookkeeping is split */
enum
{
	SPLIT_EDGES = 8,                 /* MAXIMUM_EDGES_PER_COLLISION_SURFACE */
	SPLIT_RING_MOST = 4096
};

struct collision_split
{
	long *pieces;        /* a surface's piece count (1 if not cut), NULL if none is */
	long extra_nodes;    /* the most new 2D nodes the cuts need */
};

extern short const global_projection3d_mappings[3][2][2];

H3_ALIGN static long split_piece_count(long edges)
{
	return edges <= SPLIT_EDGES ? 1 : 1 + (edges - SPLIT_EDGES + (SPLIT_EDGES - 3)) / (SPLIT_EDGES - 2);
}

/* (the same count over ODST's large arrays) */
H3_ALIGN static long collision_split_count_large(struct collision_bsp const *bsp, struct collision_split *split)
{
	long i, extra = 0;

	for (i = 0; i < bsp->counts[5]; i++)
	{
		unsigned char const *surface = bsp->arrays[5] + 16 * i;
		unsigned long first = read32(surface + 4), e = first;
		long n = 0;

		if (first == 0xFFFFFFFFUL)
			continue;
		while (e < (unsigned long)bsp->counts[6] && n < SPLIT_RING_MOST)
		{
			unsigned char const *edge = bsp->arrays[6] + 24 * e;

			n++;
			e = read32(edge + 8 + 4 * (read32(edge + 20) == (unsigned long)i));
			if (e == first)
				break;
		}
		if (e != first || n <= SPLIT_EDGES)
			continue;
		if (!split->pieces)
		{
			split->pieces = calloc(bsp->counts[5], sizeof(long));
			if (!split->pieces)
				return 0;
		}
		split->pieces[i] = split_piece_count(n);
		extra += split->pieces[i] - 1;
	}
	if (!split->pieces)
		return 0;
	for (i = 0; i < bsp->counts[4]; i++)
	{
		unsigned char const *node = bsp->arrays[4] + 20 * i;
		int c;

		for (c = 0; c < 2; c++)
		{
			unsigned long child = read32(node + 12 + 4 * c);

			if ((child & 0x80000000UL) && (long)(child & 0x7FFFFFFFUL) < bsp->counts[5] && split->pieces[child & 0x7FFFFFFFUL] > 1)
				split->extra_nodes += split->pieces[child & 0x7FFFFFFFUL] - 1;
		}
	}
	for (i = 0; i < bsp->counts[3]; i++)
	{
		unsigned long root = read32(bsp->arrays[3] + 8 * i + 4);

		if ((root & 0x80000000UL) && (long)(root & 0x7FFFFFFFUL) < bsp->counts[5] && split->pieces[root & 0x7FFFFFFFUL] > 1)
			split->extra_nodes += split->pieces[root & 0x7FFFFFFFUL] - 1;
	}
	return extra;
}

/* the surfaces to cut (Halo 3's arrays): the new surfaces (and edges) they
need; split->extra_nodes the 2D nodes, counting each place a surface ends a
2D BSP (a node's child or a reference's root) */
H3_ALIGN static long collision_split_count(struct collision_bsp const *bsp, struct collision_split *split)
{
	long i, extra = 0;

	memset(split, 0, sizeof(*split));
	if (bsp->large)
		return collision_split_count_large(bsp, split);
	for (i = 0; i < bsp->counts[5]; i++)
	{
		unsigned char const *surface = bsp->arrays[5] + 12 * i;
		long first = (long)(unsigned short)read16(surface + 2), e = first, n = 0;

		if (first == 0xFFFF)
			continue;
		while (e >= 0 && e < bsp->counts[6] && n < SPLIT_RING_MOST)
		{
			unsigned char const *edge = bsp->arrays[6] + 12 * e;

			n++;
			e = (long)(unsigned short)read16(edge + 4 + 2 * ((unsigned short)read16(edge + 10) == i));
			if (e == first)
				break;
		}
		if (e != first || n <= SPLIT_EDGES)
			continue;
		if (!split->pieces)
		{
			split->pieces = calloc(bsp->counts[5], sizeof(long));
			if (!split->pieces)
				return 0;
		}
		split->pieces[i] = split_piece_count(n);
		extra += split->pieces[i] - 1;
	}
	if (!split->pieces)
		return 0;
	for (i = 0; i < bsp->counts[4]; i++)
	{
		unsigned char const *node = bsp->arrays[4] + 16 * i;
		int c;

		for (c = 0; c < 2; c++)
		{
			unsigned long child = (unsigned short)read16(node + 12 + 2 * c);

			if ((child & 0x8000) && (long)(child & 0x7FFF) < bsp->counts[5] && split->pieces[child & 0x7FFF] > 1)
				split->extra_nodes += split->pieces[child & 0x7FFF] - 1;
		}
	}
	for (i = 0; i < bsp->counts[3]; i++)
	{
		unsigned long root = (unsigned short)read16(bsp->arrays[3] + 4 * i + 2);

		if ((root & 0x8000) && (long)(root & 0x7FFF) < bsp->counts[5] && split->pieces[root & 0x7FFF] > 1)
			split->extra_nodes += split->pieces[root & 0x7FFF] - 1;
	}
	return extra;
}

H3_ALIGN static void collision_split_free(struct collision_split *split)
{
	free(split->pieces);
	split->pieces = NULL;
}

/* (a cut surface's pieces: its first surface index, and its corners) */
struct split_surface
{
	long first_piece;    /* the second piece's surface; the rest follow */
	long count;
	long corners;        /* its ring's edge count */
	long *vertices;      /* each edge's first corner, in ring order */
};

/* the cuts made in this engine's arrays (out[k] each block, counts Halo
3's, the blocks counts plus the new elements) */
H3_ALIGN static void collision_split_surfaces(struct region *r, unsigned long field, unsigned long const *out,
	long const *counts, struct collision_split const *split)
{
	static unsigned long const ce_sizes[8] = { 12, 16, 8, 8, 20, 12, 24, 16 };
	long surface_next = counts[5], edge_next = counts[6], node_next = counts[4], node_most;
	long i, t, k;
	struct split_surface *cut = calloc(counts[5], sizeof(*cut));
	long *ring = malloc(SPLIT_RING_MOST * sizeof(long));
	long *stack = NULL, stack_size = 0, stack_capacity = 0;
	long cut_surfaces = 0;

	node_most = counts[4] + split->extra_nodes;
#define AT(k, i) (region_at(r, out[k] + ce_sizes[k] * (unsigned long)(i)))
	if (!cut || !ring)
		goto done;
	/* the surfaces and edges */
	for (i = 0; i < counts[5]; i++)
	{
		long n = 0, e, first, pieces = split->pieces[i], piece;
		unsigned char *surface;

		if (pieces <= 1)
			continue;
		surface = AT(5, i);
		first = (long)read32(surface + 4);
		e = first;
		do
		{
			unsigned char *edge = AT(6, e);

			ring[n++] = e;
			e = (long)read32(edge + 8 + 4 * (read32(edge + 20) == (unsigned long)i));
		} while (e != first && n < SPLIT_RING_MOST);
		cut[i].vertices = malloc(n * sizeof(long));
		if (!cut[i].vertices)
			continue;
		cut[i].first_piece = surface_next;
		cut[i].count = pieces;
		cut[i].corners = n;
		for (t = 0; t < n; t++)
		{
			unsigned char *edge = AT(6, ring[t]);

			cut[i].vertices[t] = (long)read32(edge + 4 * (read32(edge + 20) == (unsigned long)i));
		}
		/* piece 0: edges 0..6 then cut 0 (corner 7 to corner 0); piece p
		(0 < p < last): cut p-1 the other way, edges 7+6(p-1).. six of them,
		cut p; the last: cut p-1 the other way and the edges left */
		for (piece = 0; piece < pieces; piece++)
		{
			long surface_index = piece ? surface_next + piece - 1 : i;
			long from = piece ? SPLIT_EDGES - 1 + (SPLIT_EDGES - 2) * (piece - 1) : 0;
			long to = piece == pieces - 1 ? n : SPLIT_EDGES - 1 + (SPLIT_EDGES - 2) * piece;  /* edges [from, to) */
			long cut_in = piece ? edge_next + piece - 1 : NONE, cut_out = piece < pieces - 1 ? edge_next + piece : NONE;
			long first_edge = piece ? cut_in : ring[0];

			if (piece)
			{
				memcpy(AT(5, surface_index), surface, 12);
				write32(AT(5, surface_index) + 4, (unsigned long)cut_in);
			}
			for (t = from; t < to; t++)
			{
				unsigned char *edge = AT(6, ring[t]);
				int side = read32(edge + 20) == (unsigned long)i;

				write32(edge + 16 + 4 * side, (unsigned long)surface_index);
				if (t == to - 1)
					write32(edge + 8 + 4 * side, (unsigned long)(cut_out != NONE ? cut_out : first_edge));
			}
			if (cut_in != NONE)
			{
				/* (the cut into this piece: its side 1, corner 0 to corner from) */
				unsigned char *edge = AT(6, cut_in);

				write32(edge + 12, (unsigned long)ring[from]);
				write32(edge + 20, (unsigned long)surface_index);
			}
			if (cut_out != NONE)
			{
				/* (the cut out of it: side 0, corner to back to corner 0) */
				unsigned char *edge = AT(6, cut_out);

				write32(edge, (unsigned long)cut[i].vertices[to]);
				write32(edge + 4, (unsigned long)cut[i].vertices[0]);
				write32(edge + 8, (unsigned long)first_edge);
				write32(edge + 16, (unsigned long)surface_index);
			}
		}
		surface_next += pieces - 1;
		edge_next += pieces - 1;
		cut_surfaces++;
	}
	/* the 2D BSPs: each reference's tree walked; a child (or the root) that
	is a cut surface becomes a chain of nodes along its cuts */
	for (i = 0; i < counts[3]; i++)
	{
		unsigned char *reference = AT(3, i);
		unsigned long designator = read32(reference);
		long plane = (long)(designator & 0x7FFFFFFFUL);
		float n3[3];
		short projection, sign;
		short const *mapping;

		if (plane >= (long)read32(region_at(r, field + 12 * 1)))
			continue;
		for (k = 0; k < 3; k++)
			n3[k] = readf(AT(1, plane) + 4 * k);
		projection = fabsf(n3[2]) >= fabsf(n3[1]) && fabsf(n3[2]) >= fabsf(n3[0]) ? 2 : fabsf(n3[1]) >= fabsf(n3[0]) ? 1 : 0;
		sign = (n3[projection] > 0.0f) != ((designator & 0x80000000UL) != 0);
		mapping = global_projection3d_mappings[projection][sign];
		stack_size = 0;
		/* (slots to look at: the root's (-1 - i) or a node's child (node * 2 + c)) */
		for (t = -1; ; )
		{
			unsigned char *slot = t < 0 ? reference + 4 : AT(4, t / 2) + 12 + 4 * (t & 1);
			unsigned long child = read32(slot);

			if (child & 0x80000000UL)
			{
				long surface_index = (long)(child & 0x7FFFFFFFUL);

				if (child != 0xFFFFFFFFUL && surface_index < counts[5] && cut[surface_index].count > 1 &&
					node_next + cut[surface_index].count - 1 <= node_most)
				{
					struct split_surface const *c = &cut[surface_index];
					long piece;

					write32(slot, (unsigned long)node_next);
					for (piece = 0; piece < c->count - 1; piece++)
					{
						unsigned char *node = AT(4, node_next);
						long corner = SPLIT_EDGES - 1 + (SPLIT_EDGES - 2) * piece;
						unsigned char *a = AT(7, c->vertices[0]), *b = AT(7, c->vertices[corner]);
						unsigned char *inside = AT(7, c->vertices[corner - 1]);
						float ax = readf(a + 4 * mapping[0]), ay = readf(a + 4 * mapping[1]);
						float bx = readf(b + 4 * mapping[0]), by = readf(b + 4 * mapping[1]);
						float nx = by - ay, ny = ax - bx, length = sqrtf(nx * nx + ny * ny), d, in;
						long other = piece == c->count - 2 ? (long)(0x80000000UL | (unsigned long)(c->first_piece + piece)) :
							node_next + 1;

						if (length > 0.0f)
							nx /= length, ny /= length;
						d = nx * ax + ny * ay;
						in = nx * readf(inside + 4 * mapping[0]) + ny * readf(inside + 4 * mapping[1]) - d;
						writef(node, nx);
						writef(node + 4, ny);
						writef(node + 8, d);
						/* (this piece where its inside corner is; the rest the other side) */
						write32(node + 12 + 4 * (in >= 0.0f), piece ? 0x80000000UL | (unsigned long)(c->first_piece + piece - 1) :
							0x80000000UL | (unsigned long)surface_index);
						write32(node + 12 + 4 * (in < 0.0f), (unsigned long)other);
						node_next++;
					}
				}
			}
			else if ((long)child < counts[4])
			{
				/* (a node of the map's: its two children looked at) */
				if (stack_size + 2 > stack_capacity)
				{
					long grown_capacity = stack_capacity ? stack_capacity * 2 : 256;
					long *grown = realloc(stack, grown_capacity * sizeof(long));

					if (!grown)
						break;
					stack = grown;
					stack_capacity = grown_capacity;
				}
				stack[stack_size++] = (long)child * 2;
				stack[stack_size++] = (long)child * 2 + 1;
			}
			if (!stack_size)
				break;
			t = stack[--stack_size];
		}
	}
	/* (the 2D nodes block as many as made: a node shared by references is
	cut once) */
	write32(region_at(r, field + 12 * 4), (unsigned long)node_next);
	if (getenv("HALO_H3_DEBUG"))
		fprintf(stderr, "h3 level: collision: %ld surfaces of more than %d edges cut into %ld more, %ld 2D nodes added (of %ld)\n",
			cut_surfaces, SPLIT_EDGES, surface_next - counts[5], node_next - counts[4], split->extra_nodes);
done:
#undef AT
	if (cut)
	{
		for (i = 0; i < counts[5]; i++)
			free(cut[i].vertices);
	}
	free(cut);
	free(ring);
	free(stack);
}

/* this engine's collision BSP (0x60 bytes) at field from a Halo 3 one (its
arrays read), its surfaces' material material unless NONE */
H3_ALIGN static boolean collision_convert(struct region *r, unsigned long field, struct collision_bsp const *bsp,
	short material, long material_count, float scale)
{
	static unsigned long const ce_sizes[8] = { 12, 16, 8, 8, 20, 12, 24, 16 };
	unsigned long out[8];
	long i, k;

	struct collision_split split;
	long extra_surfaces;

	for (k = 0; k < 8; k++)
	{
		if (bsp->counts[k] && !bsp->arrays[k])
			return FALSE;
	}
	extra_surfaces = collision_split_count(bsp, &split);
	for (k = 0; k < 8; k++)
	{
		long extra = k == 5 || k == 6 ? extra_surfaces : k == 4 ? split.extra_nodes : 0;

		out[k] = region_block(r, field + 12 * k, bsp->counts[k] + extra, ce_sizes[k]);
	}
	if (r->b.failed)
	{
		collision_split_free(&split);
		return FALSE;
	}
#define H3_AT(k, i) (bsp->arrays[k] + COLLISION_SIZE(bsp, k) * (i))
#define CE_AT(k, i) (region_at(r, out[k] + ce_sizes[k] * (i)))
	if (bsp->large)
	{
		/* (ODST's large collision BSP: this engine's own arrays but for the
		surfaces - the flags at 14, the material at 8 - and the vertices'
		sink) */
		for (i = 0; i < bsp->counts[0]; i++)
			memcpy(CE_AT(0, i), H3_AT(0, i), 12);
		for (i = 0; i < bsp->counts[1]; i++)
		{
			memcpy(CE_AT(1, i), H3_AT(1, i), 16);
			writef(CE_AT(1, i) + 12, readf(H3_AT(1, i) + 12) * scale);
		}
		for (i = 0; i < bsp->counts[2]; i++)
		{
			unsigned char const *l = H3_AT(2, i);
			unsigned char *o = CE_AT(2, i);

			write16(o, read16(l) & 1);
			write16(o + 2, read16(l + 2));
			write32(o + 4, read32(l + 4));
		}
		for (i = 0; i < bsp->counts[3]; i++)
			memcpy(CE_AT(3, i), H3_AT(3, i), 8);
		for (i = 0; i < bsp->counts[4]; i++)
		{
			memcpy(CE_AT(4, i), H3_AT(4, i), 20);
			writef(CE_AT(4, i) + 8, readf(H3_AT(4, i) + 8) * scale);
		}
		for (i = 0; i < bsp->counts[5]; i++)
		{
			unsigned char const *s = H3_AT(5, i);
			unsigned char *o = CE_AT(5, i);
			short m = read16(s + 8);

			write32(o, read32(s));
			write32(o + 4, read32(s + 4));
			o[8] = s[14] & 0x0F;
			o[9] = 0xFF;
			if (material != NONE)
				m = material;
			else if (m < 0 || m >= material_count)
				m = 0;
			write16(o + 10, m);
		}
		for (i = 0; i < bsp->counts[6]; i++)
			memcpy(CE_AT(6, i), H3_AT(6, i), 24);
		for (i = 0; i < bsp->counts[7]; i++)
		{
			unsigned char const *v = H3_AT(7, i);
			unsigned char *o = CE_AT(7, i);

			writef(o, readf(v) * scale);
			writef(o + 4, readf(v + 4) * scale);
			writef(o + 8, readf(v + 8) * scale);
			write32(o + 12, read32(v + 12));
		}
		goto converted;
	}
#define FLAG16(v) (((unsigned long)(v) & 0x8000) ? (0x80000000UL | ((unsigned long)(v) & 0x7FFF)) : ((unsigned long)(v) & 0x7FFF))
#define INDEX16(v) ((unsigned short)(v) == 0xFFFF ? 0xFFFFFFFFUL : (unsigned long)(unsigned short)(v))
	for (i = 0; i < bsp->counts[0]; i++)
	{
		unsigned char const *n = H3_AT(0, i);
		unsigned long back = n[2] | (n[3] << 8) | ((unsigned long)n[4] << 16);
		unsigned long front = n[5] | (n[6] << 8) | ((unsigned long)n[7] << 16);
		unsigned char *o = CE_AT(0, i);

		write32(o, INDEX16(read16(n)));
		write32(o + 4, back == 0xFFFFFF ? 0xFFFFFFFFUL : (back & 0x800000) ? (0x80000000UL | (back & 0x7FFFFF)) : back);
		write32(o + 8, front == 0xFFFFFF ? 0xFFFFFFFFUL : (front & 0x800000) ? (0x80000000UL | (front & 0x7FFFFF)) : front);
	}
	for (i = 0; i < bsp->counts[1]; i++)
	{
		memcpy(CE_AT(1, i), H3_AT(1, i), 16);
		writef(CE_AT(1, i) + 12, readf(H3_AT(1, i) + 12) * scale);
	}
	for (i = 0; i < bsp->counts[2]; i++)
	{
		unsigned char const *l = H3_AT(2, i);
		unsigned char *o = CE_AT(2, i);

		/* (flags, 2D reference count, first 2D reference) */
		write16(o, read16(l) & 1);
		write16(o + 2, read16(l + 2));
		write32(o + 4, read32(l + 4));
	}
	for (i = 0; i < bsp->counts[3]; i++)
	{
		unsigned char const *f = H3_AT(3, i);
		unsigned char *o = CE_AT(3, i);

		write32(o, FLAG16(read16(f)));
		write32(o + 4, FLAG16(read16(f + 2)));
	}
	for (i = 0; i < bsp->counts[4]; i++)
	{
		unsigned char const *n = H3_AT(4, i);
		unsigned char *o = CE_AT(4, i);

		memcpy(o, n, 12);
		writef(o + 8, readf(n + 8) * scale);
		write32(o + 12, FLAG16(read16(n + 12)));
		write32(o + 16, FLAG16(read16(n + 14)));
	}
	for (i = 0; i < bsp->counts[5]; i++)
	{
		/* (plane with its flag, first edge, material, breakable surface,
		then the flags at 10) */
		unsigned char const *s = H3_AT(5, i);
		unsigned char *o = CE_AT(5, i);
		short m = read16(s + 4);

		write32(o, FLAG16(read16(s)));
		write32(o + 4, INDEX16(read16(s + 2)));
		o[8] = s[10] & 0x0F;
		o[9] = 0xFF;
		if (material != NONE)
			m = material;
		else if (m < 0 || m >= material_count)
			m = 0;
		write16(o + 10, m);
	}
	for (i = 0; i < bsp->counts[6]; i++)
	{
		unsigned char const *e = H3_AT(6, i);
		unsigned char *o = CE_AT(6, i);

		for (k = 0; k < 6; k++)
			write32(o + 4 * k, INDEX16(read16(e + 2 * k)));
	}
	for (i = 0; i < bsp->counts[7]; i++)
	{
		unsigned char const *v = H3_AT(7, i);
		unsigned char *o = CE_AT(7, i);

		writef(o, readf(v) * scale);
		writef(o + 4, readf(v + 4) * scale);
		writef(o + 8, readf(v + 8) * scale);
		write32(o + 12, INDEX16(read16(v + 12)));
	}
converted:
	if (extra_surfaces)
		collision_split_surfaces(r, field, out, bsp->counts, &split);
	collision_split_free(&split);
#undef H3_AT
#undef CE_AT
#undef FLAG16
#undef INDEX16

	return TRUE;
}

/* ---------- private code: the structure BSP */

struct surface_ref
{
	long surface;
	float center;
	float bounds[6];
};

H3_ALIGN static int surface_ref_compare(void const *a, void const *b)
{
	float x = ((struct surface_ref const *)a)->center, y = ((struct surface_ref const *)b)->center;

	return x < y ? -1 : x > y ? 1 : 0;
}

H3_ALIGN static int material_compare(void const *a, void const *b)
{
	struct material const *x = a, *y = b;

	return x->atlas != y->atlas ? x->atlas - y->atlas : x->shader - y->shader;
}

struct tag_ids
{
	unsigned long lightmap_bitmap;
	unsigned long first_shader;
};

/* the structure BSP laid out in r (h2_level.c's bsp_write, from Halo 3's) */
/* the pathfinding surfaces (a byte each collision surface: bit 6 walkable -
the bots' searches and lanes, bots_nav.c / bots_routes.c), h2_level.c's
rule: a surface facing up (within about 46 degrees) that is not invisible,
and a step's riser (steep, at most 0.35 units from bottom to top) */
H3_ALIGN static void pathfinding_write(struct region *r, unsigned long root, unsigned long field)
{
	long surface_count = (long)read32(region_at(r, field + 12 * 5));
	long plane_count = (long)read32(region_at(r, field + 12 * 1));
	long edge_count = (long)read32(region_at(r, field + 12 * 6)), vertex_count = (long)read32(region_at(r, field + 12 * 7));
	unsigned long surfaces = surface_count ? read32(region_at(r, field + 12 * 5 + 4)) - r->link : 0;
	unsigned long planes = plane_count ? read32(region_at(r, field + 12 * 1 + 4)) - r->link : 0;
	unsigned long edges = edge_count ? read32(region_at(r, field + 12 * 6 + 4)) - r->link : 0;
	unsigned long vertices = vertex_count ? read32(region_at(r, field + 12 * 7 + 4)) - r->link : 0;
	unsigned long flags = region_block(r, root + 0x1E4, surface_count, 1);
	long i, walkable = 0, risers = 0;

	if (r->b.failed)
		return;
	for (i = 0; i < surface_count; i++)
	{
		unsigned char const *surface = region_at(r, surfaces + 12 * i);
		unsigned long designator = read32(surface);
		long plane = (long)(designator & 0x7FFFFFFFUL);
		float up;

		if (plane >= plane_count || (surface[8] & 2))
			continue;
		up = readf(region_at(r, planes + 16 * plane + 8));
		if (designator & 0x80000000UL)
			up = -up;
		if (up <= 0.7f && up > -0.2f && edge_count > 0)
		{
			unsigned long first = read32(surface + 4), edge = first;
			float low = 1e30f, high = -1e30f;
			int corners = 0;

			do
			{
				unsigned char const *e;
				int right;
				unsigned long vertex;

				if (edge >= (unsigned long)edge_count)
				{
					corners = 0;
					break;
				}
				e = region_at(r, edges + 24 * edge);
				right = read32(e + 20) == (unsigned long)i;
				vertex = read32(e + 4 * right);
				if (vertex >= (unsigned long)vertex_count)
				{
					corners = 0;
					break;
				}
				{
					float z = readf(region_at(r, vertices + 16 * vertex + 8));

					low = z < low ? z : low;
					high = z > high ? z : high;
				}
				edge = read32(e + 8 + 4 * right);
			} while (edge != first && ++corners < 16);
			if (corners >= 2 && high - low <= 0.35f)
				up = 1.0f, risers++;
		}
		if (up > 0.7f)
		{
			region_at(r, flags)[i] = 1 << 6;
			walkable++;
		}
	}
	platform_log("h3 level: pathfinding: %ld of %ld surfaces walkable (%ld steps' risers)", walkable, surface_count, risers);
}

H3_ALIGN static boolean bsp_write(struct build *build, struct region *r, struct tag_ids const *ids)
{
	struct h3_cache *cache = build->cache;
	unsigned long header = region_alloc(r, 0x18);
	unsigned long root = region_alloc(r, 0x288);
	unsigned long field;
	long i, k, count;
	unsigned long elements;
	long total_surfaces = 0, total_vertices = 0;
	short lightmap_count = 0;
	long *material_first;
	struct surface_ref **cluster_surfaces;
	long *cluster_surface_counts;
	long collision_material_count;

	region_reference(r, root + 0x00, 'bitm', ids->lightmap_bitmap);
	/* the grass's detail objects (grass_gather; structure_detail_objects.c):
	one data block - cells (0x20: x, y, z, z offset, valid layers, first
	object, first count), objects (6), counts (a word a cell's layer), no z
	reference vectors (the default: z up), valid */
	if (build->grass_object_count)
	{
		unsigned long data = region_block(r, root + 0x24C, 1, 0x40);
		unsigned long cells = r->b.failed ? 0 : region_block(r, data + 0x00, build->grass_cell_count, 0x20);
		unsigned long objects = r->b.failed ? 0 : region_block(r, data + 0x0C, build->grass_object_count, 6);
		unsigned long counts = r->b.failed ? 0 : region_block(r, data + 0x18, build->grass_count_count, 2);

		if (!r->b.failed)
		{
			for (i = 0; i < build->grass_cell_count; i++)
			{
				unsigned char *o = region_at(r, cells + 0x20 * i);
				struct grass_cell const *c = &build->grass_cells[i];

				write16(o, c->x);
				write16(o + 2, c->y);
				write16(o + 4, c->z);
				write16(o + 6, 0);
				write32(o + 8, c->layers);
				write32(o + 12, (unsigned long)c->start);
				write32(o + 16, (unsigned long)c->count_index);
			}
			memcpy(region_at(r, objects), build->grass_objects, 6 * (unsigned long)build->grass_object_count);
			for (i = 0; i < build->grass_count_count; i++)
				write16(region_at(r, counts + 2 * i), build->grass_counts[i]);
			region_at(r, data + 0x30)[0] = 1;
		}
	}
	writef(region_at(r, root + 0x10), -1000.0f);
	writef(region_at(r, root + 0x14), 1000.0f);
	{
		static float const lighting[] = {
			0.2f, 0.2f, 0.2f, 0.0f,
			1.0f, 1.0f, 1.0f, -0.577f, -0.577f, -0.577f,
			0.4f, 0.4f, 0.5f, 1.0f, 0.0f, 0.0f,
		};
		for (k = 0; k < 3; k++)
			writef(region_at(r, root + 0x2C + 4 * k), lighting[k]);
		for (k = 0; k < 12; k++)
			writef(region_at(r, root + 0x3C + 4 * k), lighting[4 + k]);
		writef(region_at(r, root + 0x78), 0.5f);
		writef(region_at(r, root + 0x7C), 1.0f);
		writef(region_at(r, root + 0x80), 1.0f);
		writef(region_at(r, root + 0x84), 1.0f);
		writef(region_at(r, root + 0x90), -1.0f);
	}
	{
		unsigned char const *bounds = h3_cache_at(cache, build->sbsp + H3_SBSP_WORLD_BOUNDS, 24);

		if (bounds)
			memcpy(region_at(r, root + 0xC8), bounds, 24);
	}

	/* collision materials: each one's render method's shader */
	if (!h3_cache_block(cache, build->sbsp + H3_SBSP_COLLISION_MATERIALS, H3_COLLISION_MATERIAL_SIZE, &count, &elements))
		return build_fail(build, "its collision materials are damaged"), FALSE;
	collision_material_count = count ? count : 1;
	field = region_block(r, root + 0xA4, collision_material_count, 20);
	for (i = 0; i < collision_material_count; i++)
	{
		short shader = i < count ? shader_index(build, h3_cache_reference(cache, elements + H3_COLLISION_MATERIAL_SIZE * i)) : NONE;

		if (shader == NONE)
			shader = 0;
		region_reference(r, field + 20 * i, build->shaders[shader].group, ids->first_shader + ((unsigned long)shader << 16) + shader);
	}

	/* the collision BSP */
	field = region_block(r, root + 0xB0, 1, 0x60);
	if (r->b.failed || !collision_convert(r, field, &build->collision, NONE, collision_material_count, 1.0f))
		return build_fail(build, "its collision BSP is damaged"), FALSE;
	pathfinding_write(r, root, field);
	{
		long node_count = build->collision.counts[0];

		field = region_block(r, root + 0xBC, node_count, 6);
		for (i = 0; i < node_count; i++)
		{
			unsigned char *n = region_at(r, field + 6 * i);

			n[1] = n[3] = n[5] = 255;
		}
	}

	/* leaves: each one's cluster */
	field = region_block(r, root + 0xE0, build->collision.counts[2], 16);
	for (i = 0; i < build->collision.counts[2]; i++)
	{
		unsigned char *l = region_at(r, field + 16 * i);

		l[1] = l[3] = l[5] = 255;
		write16(l + 8, i < build->leaf_count ? build->leaf_cluster[i] : 0);
	}

	/* surfaces and lightmaps: the materials in atlas order */
	qsort(build->materials, build->ce_material_count, sizeof(*build->materials), material_compare);
	material_first = calloc(build->ce_material_count + 1, sizeof(long));
	if (!material_first)
		return build_fail(build, "out of memory"), FALSE;
	for (i = 0; i < build->ce_material_count; i++)
	{
		material_first[i] = total_surfaces;
		total_surfaces += build->materials[i].triangle_count;
		total_vertices += build->materials[i].vertex_count;
	}
	field = region_block(r, root + 0xF8, total_surfaces, 6);
	for (i = 0; i < build->ce_material_count; i++)
	{
		if (build->materials[i].triangle_count)
			memcpy(region_at(r, field + 6 * material_first[i]), build->materials[i].triangles.data,
				(unsigned long)build->materials[i].triangle_count * 6);
		bytes_free(&build->materials[i].triangles);
	}
	for (i = 0; i < build->ce_material_count; i++)
	{
		if (i == 0 || build->materials[i].atlas != build->materials[i - 1].atlas)
			lightmap_count++;
	}
	{
		unsigned long lightmaps = region_block(r, root + 0x104, lightmap_count, 32);
		short lightmap = -1;
		long first = 0;

		for (i = 0; i < build->ce_material_count; i = first)
		{
			long n;
			unsigned long materials;

			lightmap++;
			for (first = i; first < build->ce_material_count && build->materials[first].atlas == build->materials[i].atlas; first++)
				;
			write16(region_at(r, lightmaps + 32 * lightmap), build->materials[i].atlas);
			materials = region_block(r, lightmaps + 32 * lightmap + 0x14, first - i, 256);
			for (n = i; n < first; n++)
			{
				struct material *m = &build->materials[n];
				unsigned char *o;
				unsigned long data;

				region_reference(r, materials + 256 * (n - i), build->shaders[m->shader].group,
					ids->first_shader + ((unsigned long)m->shader << 16) + m->shader);
				o = region_at(r, materials + 256 * (n - i));
				write32(o + 0x14, material_first[n]);
				write32(o + 0x18, m->triangle_count);
				for (k = 0; k < 3; k++)
					writef(o + 0x1C + 4 * k, m->vertex_count ? (float)(m->centroid[k] / m->vertex_count) : 0.0f);
				writef(o + 0x28, 0.2f); writef(o + 0x2C, 0.2f); writef(o + 0x30, 0.2f);
				write16(o + 0x34, 2);
				writef(o + 0x38, 1.0f); writef(o + 0x3C, 1.0f); writef(o + 0x40, 1.0f);
				writef(o + 0x44, -0.577f); writef(o + 0x48, -0.577f); writef(o + 0x4C, -0.577f);
				writef(o + 0x50, 0.4f); writef(o + 0x54, 0.4f); writef(o + 0x58, 0.5f);
				writef(o + 0x5C, 1.0f);
				writef(o + 0x74, 0.5f); writef(o + 0x78, 1.0f); writef(o + 0x7C, 1.0f); writef(o + 0x80, 1.0f);
				writef(o + 0x8C, -1.0f);
				write16(o + 0xAC, -1);
				write16(o + 0xB0, 1);
				write32(o + 0xB4, m->vertex_count);
				write16(o + 0xC4, 3);
				write32(o + 0xC8, m->vertex_count);
				data = region_alloc(r, m->vertices.size + m->lightmap_vertices.size);
				if (r->b.failed)
					break;
				memcpy(region_at(r, data), m->vertices.data, m->vertices.size);
				memcpy(region_at(r, data + m->vertices.size), m->lightmap_vertices.data, m->lightmap_vertices.size);
				free(m->vertices.data);
				free(m->lightmap_vertices.data);
				m->vertices.data = m->lightmap_vertices.data = NULL;
				m->vertices.capacity = m->lightmap_vertices.capacity = 0;
				o = region_at(r, materials + 256 * (n - i));
				write32(o + 0xB8, m->vertices.size);
				write32(o + 0xEC, m->vertices.size + m->lightmap_vertices.size);
				write32(o + 0xEC + 12, r->link + data);
			}
		}
	}

	/* clusters: their surfaces in subclusters of 128, their portals */
	cluster_surfaces = calloc(build->cluster_count + 1, sizeof(*cluster_surfaces));
	cluster_surface_counts = calloc(build->cluster_count + 1, sizeof(long));
	if (!cluster_surfaces || !cluster_surface_counts)
		return build_fail(build, "out of memory"), FALSE;
	for (i = 0; i < build->ce_material_count; i++)
	{
		struct material *m = &build->materials[i];
		short const *clusters = (short const *)m->clusters.data;

		for (k = 0; k < m->triangle_count; k++)
		{
			short c = clusters[k];

			if (c >= 0 && c < build->cluster_count)
				cluster_surface_counts[c]++;
		}
	}
	for (i = 0; i < build->cluster_count; i++)
	{
		cluster_surfaces[i] = malloc((cluster_surface_counts[i] + 1) * sizeof(struct surface_ref));
		cluster_surface_counts[i] = 0;
	}
	for (i = 0; i < build->ce_material_count; i++)
	{
		struct material *m = &build->materials[i];
		short const *clusters = (short const *)m->clusters.data;
		float const *bounds = (float const *)m->bounds.data;

		for (k = 0; k < m->triangle_count; k++)
		{
			short c = clusters[k];
			struct surface_ref *s;

			if (c < 0 || c >= build->cluster_count || !cluster_surfaces[c])
				continue;
			s = &cluster_surfaces[c][cluster_surface_counts[c]++];
			s->surface = material_first[i] + k;
			memcpy(s->bounds, bounds + 6 * k, sizeof(s->bounds));
		}
		bytes_free(&m->clusters);
		bytes_free(&m->bounds);
	}
	field = region_block(r, root + 0x134, build->cluster_count, 104);
	for (i = 0; i < build->cluster_count; i++)
	{
		unsigned long cluster = build->clusters + H3_CLUSTER_SIZE * i;
		unsigned char *o = region_at(r, field + 104 * i);
		struct surface_ref *s = cluster_surfaces[i];
		long n = cluster_surface_counts[i];
		float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
		int axis = 0;
		long portal_count;
		unsigned long portals;
		unsigned long subclusters;

		write16(o + 0, build->sky_model_index != NONE ? 0 : -1);
		write16(o + 2, -1);
		write16(o + 4, -1);
		write16(o + 6, -1);
		write16(o + 8, -1);
		for (k = 0; k < n && s; k++)
		{
			int j;

			for (j = 0; j < 3; j++)
			{
				float c = 0.5f * (s[k].bounds[j] + s[k].bounds[3 + j]);

				lo[j] = fminf(lo[j], c);
				hi[j] = fmaxf(hi[j], c);
			}
		}
		if (hi[1] - lo[1] > hi[axis] - lo[axis])
			axis = 1;
		if (hi[2] - lo[2] > hi[axis] - lo[axis])
			axis = 2;
		for (k = 0; k < n && s; k++)
			s[k].center = 0.5f * (s[k].bounds[axis] + s[k].bounds[3 + axis]);
		if (n && s)
			qsort(s, n, sizeof(*s), surface_ref_compare);
		if (getenv("HALO_H3_DEBUG"))
			fprintf(stderr, "h3 level: cluster %ld: %ld surfaces, centres %.1f %.1f %.1f .. %.1f %.1f %.1f\n", i, n, lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
		/* (an empty cluster keeps one empty subcluster: this engine takes the
		first cluster's having none as a level without them, and then draws no
		surface - Valhalla's clusters 0 and 1 are empty) */
		subclusters = region_block(r, field + 104 * i + 0x34, n ? (n + MAXIMUM_SUBCLUSTER_SURFACES - 1) / MAXIMUM_SUBCLUSTER_SURFACES : 1, 36);
		for (k = 0; k < n && !r->b.failed; k += MAXIMUM_SUBCLUSTER_SURFACES)
		{
			long sub = k / MAXIMUM_SUBCLUSTER_SURFACES;
			long m = n - k < MAXIMUM_SUBCLUSTER_SURFACES ? n - k : MAXIMUM_SUBCLUSTER_SURFACES;
			float blo[3] = { 1e30f, 1e30f, 1e30f }, bhi[3] = { -1e30f, -1e30f, -1e30f };
			unsigned long indices;
			long j;
			int c;

			for (j = 0; j < m; j++)
			{
				for (c = 0; c < 3; c++)
				{
					blo[c] = fminf(blo[c], s[k + j].bounds[c]);
					bhi[c] = fmaxf(bhi[c], s[k + j].bounds[3 + c]);
				}
			}
			for (c = 0; c < 3; c++)
			{
				writef(region_at(r, subclusters + 36 * sub + 8 * c), blo[c]);
				writef(region_at(r, subclusters + 36 * sub + 8 * c + 4), bhi[c]);
			}
			indices = region_block(r, subclusters + 36 * sub + 0x18, m, 4);
			for (j = 0; j < m && !r->b.failed; j++)
				write32(region_at(r, indices + 4 * j), s[k + j].surface);
		}
		if (h3_cache_block(cache, cluster + 0x3C, 2, &portal_count, &portals))
		{
			unsigned long out = region_block(r, field + 104 * i + 0x5C, portal_count, 2);

			for (k = 0; k < portal_count && !r->b.failed; k++)
				write16(region_at(r, out + 2 * k), h3_cache_read16(cache, portals + 2 * k));
		}
	}
	for (i = 0; i < build->cluster_count; i++)
		free(cluster_surfaces[i]);
	free(cluster_surfaces);
	free(cluster_surface_counts);
	free(material_first);

	/* every cluster sees every other (the portals cull) */
	{
		long words = (build->cluster_count + 31) / 32;
		unsigned long size = (unsigned long)build->cluster_count * words * 4;
		unsigned char *all = malloc(size + 1);

		if (all)
		{
			memset(all, 0xFF, size);
			region_data(r, root + 0x140, all, size);
			free(all);
		}
	}
	/* cluster portals */
	if (h3_cache_block(cache, build->sbsp + H3_SBSP_CLUSTER_PORTALS, H3_PORTAL_SIZE, &count, &elements))
	{
		field = region_block(r, root + 0x154, count, 64);
		for (i = 0; i < count && !r->b.failed; i++)
		{
			unsigned char p[H3_PORTAL_SIZE];
			unsigned char *o;
			long vertex_count;
			unsigned long vertices, out;

			if (!h3_cache_read(cache, elements + H3_PORTAL_SIZE * i, H3_PORTAL_SIZE, p))
				continue;
			o = region_at(r, field + 64 * i);
			write16(o, read16(p));
			write16(o + 2, read16(p + 2));
			memcpy(o + 4, p + 4, 20);
			write32(o + 0x18, read32(p + 0x18) & 3);
			if (h3_cache_block(cache, elements + H3_PORTAL_SIZE * i + 0x1C, 12, &vertex_count, &vertices))
			{
				/* (a vertex repeated - Halo 3's polygons close on their first, some
				repeat one - makes this engine's hull intersection fail: the
				cluster behind it is not drawn) */
				{
					float *kept = malloc(((unsigned long)vertex_count + 1) * 12);
					long used = 0;

					for (k = 0; kept && k < vertex_count; k++)
					{
						float q[3];
						long j;
						boolean again = FALSE;

						if (!h3_cache_read(cache, vertices + 12 * k, 12, q))
							continue;
						for (j = 0; j < used && !again; j++)
						{
							again = fabsf(q[0] - kept[3 * j]) < 0.01f && fabsf(q[1] - kept[3 * j + 1]) < 0.01f &&
								fabsf(q[2] - kept[3 * j + 2]) < 0.01f;
						}
						if (!again)
						{
							memcpy(kept + 3 * used, q, 12);
							used++;
						}
					}
					vertex_count = used;
					out = region_block(r, field + 64 * i + 0x34, vertex_count, 12);
					if (kept && !r->b.failed)
						memcpy(region_at(r, out), kept, (unsigned long)vertex_count * 12);
					free(kept);
				}
				if (getenv("HALO_H3_DEBUG"))
				{
					long plane = (long)read32(p + 4) & 0x7FFFFFFF;
					float worst = 0.0f;

					if (plane < build->collision.counts[1])
					{
						unsigned char const *q = build->collision.arrays[1] + 16 * plane;

						for (k = 0; k < vertex_count; k++)
						{
							float const *v = (float const *)region_at(r, out + 12 * k);
							float d = v[0] * readf(q) + v[1] * readf(q + 4) + v[2] * readf(q + 8) - readf(q + 12);

							if (fabsf(d) > fabsf(worst))
								worst = d;
						}
						fprintf(stderr, "h3 level: portal %ld clusters %d %d plane %ld of %ld (%.2f %.2f %.2f %.2f) vertices %ld worst %.2f\n", i,
							(int)read16(p), (int)read16(p + 2), plane, build->collision.counts[1], readf(q), readf(q + 4), readf(q + 8), readf(q + 12),
							vertex_count, worst);
					}
					else
						fprintf(stderr, "h3 level: portal %ld clusters %d %d plane %ld of %ld: none\n", i, (int)read16(p), (int)read16(p + 2), plane,
							build->collision.counts[1]);
				}
			}
		}
	}
	{
		unsigned long size = (unsigned long)build->cluster_count * (build->cluster_count - 1) / 2 + 4;
		unsigned char *zeros = calloc(size, 1);

		if (zeros)
		{
			region_data(r, root + 0x214, zeros, size);
			free(zeros);
		}
	}

	/* the header: the BSP, then each material's vertex and lightmap vertex buffers */
	{
		unsigned long vertex_buffers = region_alloc(r, 12UL * build->ce_material_count);
		unsigned long lightmap_buffers = region_alloc(r, 12UL * build->ce_material_count);
		unsigned long lightmaps = read32(region_at(r, root + 0x104 + 4)) - r->link;
		long material = 0;

		if (r->b.failed)
			return build_fail(build, "out of memory"), FALSE;
		for (i = 0; i < lightmap_count; i++)
		{
			long n = (long)read32(region_at(r, lightmaps + 32 * i + 0x14));
			unsigned long materials = read32(region_at(r, lightmaps + 32 * i + 0x18)) - r->link;

			for (k = 0; k < n; k++, material++)
			{
				unsigned char *o = region_at(r, materials + 256 * k);
				unsigned long data = read32(o + 0xEC + 12);
				unsigned long vertices_size = read32(o + 0xB8);

				write32(region_at(r, vertex_buffers + 12 * material + 4), data);
				write32(region_at(r, lightmap_buffers + 12 * material + 4), data + vertices_size);
				write32(o + 0xB8, 0);
				write32(o + 0xC0, r->link + vertex_buffers + 12 * material);
				write32(o + 0xD4, r->link + lightmap_buffers + 12 * material);
			}
		}
		write32(region_at(r, header), r->link + root);
		write32(region_at(r, header + 4), build->ce_material_count);
		write32(region_at(r, header + 8), r->link + vertex_buffers);
		write32(region_at(r, header + 12), build->ce_material_count);
		write32(region_at(r, header + 16), r->link + lightmap_buffers);
		write32(region_at(r, header + 20), 'sbsp');
	}
	build->result->surfaces = total_surfaces;
	build->result->vertices = total_vertices;
	build->result->materials = build->ce_material_count;
	build->result->clusters = build->cluster_count;

	return !r->b.failed;
}

/* ---------- private code: the other tags (h2_level.c's) */

/* an environment shader of the base map bitmap_id, its details */
H3_ALIGN static void shader_write(struct region *r, unsigned long root, unsigned long bitmap_id,
	unsigned long detail_id, float detail_scale, unsigned long detail2_id, float detail2_scale)
{
	unsigned char *o = region_at(r, root);
	static unsigned long const one_offsets[] = {
		0xB4, 0xC8, 0xF8, 0x154, 0x158, 0x160, 0x164, 0x1B8, 0x1F4, 0x230, 0x250,
	};
	int k;

	write16(o + 0x02, 2);
	write16(o + 0x22, 7);
	write16(o + 0x24, 3);
	write16(o + 0x28, 0);
	write16(o + 0x2A, 0);
	region_reference(r, root + 0x30, 'lens', 0xFFFFFFFFUL);
	region_reference(r, root + 0x88, 'bitm', bitmap_id);
	region_reference(r, root + 0xB8, 'bitm', detail_id);
	region_reference(r, root + 0xCC, 'bitm', detail2_id);
	region_reference(r, root + 0xFC, 'bitm', 0xFFFFFFFFUL);
	region_reference(r, root + 0x128, 'bitm', 0xFFFFFFFFUL);
	region_reference(r, root + 0x254, 'bitm', 0xFFFFFFFFUL);
	region_reference(r, root + 0x324, 'bitm', 0xFFFFFFFFUL);
	o = region_at(r, root);
	for (k = 0; k < (int)(sizeof(one_offsets) / sizeof(one_offsets[0])); k++)
		writef(o + one_offsets[k], 1.0f);
	/* (the details: biased multiply, at their scale over the base map's) */
	write16(o + 0xB0, 0);
	if (detail_id != 0xFFFFFFFFUL)
		writef(o + 0xB4, detail_scale > 0.0f ? detail_scale : 1.0f);
	if (detail2_id != 0xFFFFFFFFUL)
		writef(o + 0xC8, detail2_scale > 0.0f ? detail2_scale : 1.0f);
	writef(o + 0x10C, 1.0f); writef(o + 0x110, 1.0f); writef(o + 0x114, 1.0f);
	writef(o + 0x2A8, 0.447f); writef(o + 0x2AC, 0.455f); writef(o + 0x2B0, 0.553f);
	writef(o + 0x2B4, 0.243f); writef(o + 0x2B8, 0.294f); writef(o + 0x2BC, 0.302f);
}

/* a transparent shader (shader_transparent_chicago) of one map; water's
scrolled (its map sliding along u and v) */
H3_ALIGN static void transparent_shader_write(struct region *r, unsigned long root, unsigned long bitmap_id, short blend,
	boolean water, float map_scale)
{
	unsigned long maps;
	unsigned char *o;

	write16(region_at(r, root + 0x02), 2);
	write16(region_at(r, root + 0x24), 6);
	write16(region_at(r, root + 0x2C), blend & 0xFF);
	if (blend & 0x100)
		write16(region_at(r, root + 0x28), 1 << 2);
	/* (framebuffer fade mode: 0x200 faded edge-on, a halogram's edge fade - this
	engine's "fade when parallel", 2: whole face-on, dark edge-on) */
	if (blend & 0x200)
		write16(region_at(r, root + 0x2E), (short)env_float("HALO_H3_HALOGRAM_FADE_MODE", 2.0f));
	region_reference(r, root + 0x38, 'lens', 0xFFFFFFFFUL);
	maps = region_block(r, root + 0x54, 1, 220);
	if (r->b.failed)
		return;
	o = region_at(r, maps);
	writef(o + 0x54, map_scale);
	writef(o + 0x58, map_scale);
	writef(o + 0xA8, 1.0f);
	writef(o + 0xB0, 1.0f);
	writef(o + 0xB8, 1.0f);
	writef(o + 0xC0, 1.0f);
	writef(o + 0xC8, 1.0f);
	writef(o + 0xD0, 1.0f);
	if (water)
	{
		/* (u slides over 40 seconds, v over 55: the swell's drift) */
		write16(o + 0xA6, 6);
		writef(o + 0xA8, env_float("HALO_H3_WATER_U_PERIOD", 40.0f));
		write16(o + 0xB6, 6);
		writef(o + 0xB8, env_float("HALO_H3_WATER_V_PERIOD", 55.0f));
	}
	region_reference(r, maps + 0x6C, 'bitm', bitmap_id);
}

/* this engine's water (shader_transparent_water, 0x140 bytes; h2_level.c's
water_shader_write, its values Zanzibar's own where Halo 3 has them): four
ripple layers of the ripple map, the reflection cube, looking down the
water's colour (water_color_pure, its fresnel coefficient 0.1: dim), along
it the sky's */
H3_ALIGN static void water_shader_write(struct region *r, unsigned long root, unsigned long ripple_id,
	unsigned long reflection_id, unsigned long base_id)
{
	unsigned long ripples;
	unsigned char *o;
	static float const angles[4] = { 0.0f, 2.0944f, 1.5708f, -1.5708f };
	static float const speeds[4] = { 0.03f, 0.02f, 0.05f, 0.025f };
	static float const offsets[4][2] = { { 0.0f, 0.0f }, { 0.0f, 0.5f }, { 0.5f, 0.0f }, { 0.25f, 0.25f } };
	int k;

	write16(region_at(r, root + 0x02), 2);
	write16(region_at(r, root + 0x22), 0x13);
	write16(region_at(r, root + 0x24), 7);
	write16(region_at(r, root + 0x28), 1 << 2);
	region_reference(r, root + 0x4C, 'bitm', base_id);
	region_reference(r, root + 0x9C, 'bitm', reflection_id);
	region_reference(r, root + 0xC8, 'bitm', ripple_id);
	o = region_at(r, root);
	writef(o + 0x6C, env_float("HALO_H3_WATER_PERPENDICULAR", 0.35f));
	writef(o + 0x70, 0.035f * 3.0f); writef(o + 0x74, 0.133f * 3.0f); writef(o + 0x78, 0.129f * 3.0f);
	writef(o + 0x7C, env_float("HALO_H3_WATER_PARALLEL", 0.7f));
	writef(o + 0x80, 0.72f); writef(o + 0x84, 0.86f); writef(o + 0x88, 0.90f);
	writef(o + 0xBC, 4.712389f);
	writef(o + 0xC0, 0.03f);
	writef(o + 0xC4, env_float("HALO_H3_WATER_RIPPLE_SCALE", 16.0f));
	write16(o + 0xD8, 4);
	writef(o + 0xDC, 0.9f);
	writef(o + 0xE0, 0.6f);
	ripples = region_block(r, root + 0x124, 4, 0x4C);
	if (r->b.failed)
		return;
	for (k = 0; k < 4; k++)
	{
		o = region_at(r, ripples + 0x4C * k);
		writef(o + 0x04, 0.3f);
		writef(o + 0x28, angles[k]);
		writef(o + 0x2C, speeds[k]);
		writef(o + 0x30, offsets[k][0]);
		writef(o + 0x34, offsets[k][1]);
		write16(o + 0x38, 1);
		write16(o + 0x3A, 0);
	}
}

H3_ALIGN static void bitmap_write(struct region *r, unsigned long root, unsigned long id, unsigned short width,
	unsigned short height, unsigned short format, unsigned short flags, unsigned short mipmaps,
	unsigned long file_offset, unsigned long size, unsigned short usage, short type)
{
	unsigned long sequence, data;
	unsigned char *o;

	write16(region_at(r, root + 0x04), usage);
	write16(region_at(r, root + 0x4C), mipmaps);
	sequence = region_block(r, root + 0x54, 1, 64);
	data = region_block(r, root + 0x60, 1, 48);
	if (r->b.failed)
		return;
	write16(region_at(r, sequence + 0x22), 1);
	o = region_at(r, data);
	write32(o, 'bitm');
	write16(o + 4, width);
	write16(o + 6, height);
	write16(o + 8, 1);
	write16(o + 0xA, type);
	write16(o + 0xC, format);
	write16(o + 0xE, flags);
	write16(o + 0x10, width / 2);
	write16(o + 0x12, height / 2);
	write16(o + 0x14, mipmaps);
	write32(o + 0x18, file_offset);
	write32(o + 0x1C, size);
	write32(o + 0x20, id);
	write32(o + 0x24, 0xFFFFFFFFUL);
}

/* ---------- private code: the level's virtual file (h2_level.c's direct level)

No file is written: the offsets a level file would have had (the kit's
data, the bitmaps' pixels, the lightmaps, the structure BSP) are counted by
the stream as the make goes, and each range remembers where its bytes really
are - the kit's in h2kit.pack's data, the rest in memory kept for the game -
for the cache file thread's reads (h3_level_direct_read). Padding is zeros,
held nowhere */

#include <pthread.h>

enum direct_range_kind
{
	_direct_range_memory,           /* memory: the C heap's, freed with free */
	_direct_range_pack,             /* h2kit.pack's data at pack_offset */
	_direct_range_kept,             /* memory: the kept pieces' (kept_alloc), let go all at once */
	_direct_range_in_place          /* memory: the tag cache, where the loader reads it to (the BSP): nothing to copy, nothing freed */
};

struct direct_range
{
	unsigned long offset, size;
	short kind;
	unsigned char *memory;
	unsigned long pack_offset;
};

static struct
{
	pthread_mutex_t lock;
	boolean prepared;               /* its header made (h3_level_direct_prepare) */
	boolean made;                   /* its tags made into the tag cache */
	char name[64];
	char maps_directory[512];
	unsigned char header[CACHE_HEADER_SIZE];
	struct direct_range *ranges;
	long range_count, range_capacity;
	unsigned long end;              /* the virtual file's length */
	FILE *pack;                     /* h2kit.pack, open for the kit's data */
	unsigned long kept_bytes;       /* (the log) memory kept */
} direct = { PTHREAD_MUTEX_INITIALIZER };

/* (h3vita) what the virtual file keeps in memory for the game - the pixels,
the lightmaps, the BSP: 30-70 MB - lives in memory blocks of its own
(h2_build_heap.h: on the Vita sceKernel blocks of the physically contiguous
main memory first - its 26 MB are otherwise the movie decoder's alone -,
then user RAM, outside the C heap and the builder's heap; on the harness
mmap), in
pieces of KEPT_PIECE bytes filled one after another, a range larger than
half a piece a block of its own; all let go with the level. Before, each
kept range was a malloc of the builder's heap: the heap's blocks could not
be given back after the make (72 MB held kept 98 MB of blocks: a quarter
lost to what lay between), and the Vita's C heap was the fallback. Where no
block can be had (the desktop's tier, or no user RAM) the heap's malloc as
before (_direct_range_memory). HALO_H3_KEPT_BLOCKS=0: the heap's always */
enum
{
	KEPT_PIECE = 4 << 20,
	KEPT_PIECES_MAXIMUM = 64,
	KEPT_ALIGNMENT = 128
};

static struct
{
	struct { unsigned char *base; unsigned long size, used; } pieces[KEPT_PIECES_MAXIMUM];
	long count;
	unsigned long bytes;            /* (the log) the pieces' bytes */
} kept;

H3_ALIGN static unsigned char *kept_alloc(unsigned long size)
{
	unsigned char *at;
	long i;

#if !H2_BUILD_HEAP
	if (!getenv("HALO_H3_KEPT_BLOCKS") || !atoi(getenv("HALO_H3_KEPT_BLOCKS")))
		return NULL;
#else
	if (getenv("HALO_H3_KEPT_BLOCKS") && !atoi(getenv("HALO_H3_KEPT_BLOCKS")))
		return NULL;
#endif
	if (!size || kept.count >= KEPT_PIECES_MAXIMUM)
		return NULL;
	size = (size + KEPT_ALIGNMENT - 1) & ~(unsigned long)(KEPT_ALIGNMENT - 1);
	if (size > KEPT_PIECE / 2)
	{
		at = h2_phycont_block_alloc_named("h3 level kept", size);
		if (!at)
			at = h2_user_block_alloc_named("h3 level kept", size);
		/* (then video memory - the Vita's CDRAM, which the CPU reads
		uncached: the kept pixels and lightmaps are read once each, into the
		texture cache - rather than the make stopping for the room) */
		if (!at)
			at = h2_video_block_alloc_named("h3 level kept", size);
		if (!at)
			return NULL;
		kept.pieces[kept.count].base = at;
		kept.pieces[kept.count].size = kept.pieces[kept.count].used = size;
		kept.count++;
		kept.bytes += size;
		return at;
	}
	/* (the last piece with room: the pieces fill in order) */
	for (i = kept.count - 1; i >= 0; i--)
	{
		if (kept.pieces[i].size - kept.pieces[i].used >= size && kept.pieces[i].size == KEPT_PIECE)
		{
			at = kept.pieces[i].base + kept.pieces[i].used;
			kept.pieces[i].used += size;
			return at;
		}
		if (kept.pieces[i].size == KEPT_PIECE)
			break;
	}
	/* (the Vita's contiguous main memory first - 26 MB nothing else of a
	multiplayer level's uses -, user RAM when it is gone) */
	at = h2_phycont_block_alloc_named("h3 level kept", KEPT_PIECE);
	if (!at)
		at = h2_user_block_alloc_named("h3 level kept", KEPT_PIECE);
	/* (then video memory: see above) */
	if (!at)
		at = h2_video_block_alloc_named("h3 level kept", KEPT_PIECE);
	if (!at)
		return NULL;
	kept.pieces[kept.count].base = at;
	kept.pieces[kept.count].size = KEPT_PIECE;
	kept.pieces[kept.count].used = size;
	kept.count++;
	kept.bytes += KEPT_PIECE;
	return at;
}

H3_ALIGN static void kept_free_all(void)
{
	long i;

	for (i = 0; i < kept.count; i++)
		h2_user_block_free(kept.pieces[i].base);
	memset(&kept, 0, sizeof(kept));
}

/* (under the lock, or before any read) every range let go, the pack closed */
H3_ALIGN static void direct_ranges_free(void)
{
	long i;

	for (i = 0; i < direct.range_count; i++)
	{
		if (direct.ranges[i].kind == _direct_range_memory)
			free(direct.ranges[i].memory);
	}
	kept_free_all();
	free(direct.ranges);
	direct.ranges = NULL;
	direct.range_count = direct.range_capacity = 0;
	direct.end = 0;
	direct.kept_bytes = 0;
	if (direct.pack)
		fclose(direct.pack);
	direct.pack = NULL;
}

/* a range of the virtual file at offset: memory's (taken: freed at unload)
or the pack's */
H3_ALIGN static boolean direct_range_add(unsigned long offset, unsigned long size, short kind, unsigned char *memory,
	unsigned long pack_offset)
{
	struct direct_range *r;

	if (!size)
	{
		if (kind == _direct_range_memory)
			free(memory);
		return TRUE;
	}
	if (direct.range_count >= direct.range_capacity)
	{
		long capacity = direct.range_capacity ? 2 * direct.range_capacity : 1024;
		struct direct_range *grown = realloc(direct.ranges, (size_t)capacity * sizeof(*grown));

		if (!grown)
			return FALSE;
		direct.ranges = grown;
		direct.range_capacity = capacity;
	}
	r = &direct.ranges[direct.range_count++];
	r->offset = offset;
	r->size = size;
	r->kind = kind;
	r->memory = memory;
	r->pack_offset = pack_offset;
	if (kind == _direct_range_memory || kind == _direct_range_kept)
		direct.kept_bytes += size;
	if (offset + size > direct.end)
		direct.end = offset + size;
	return TRUE;
}

/* size bytes at data kept for the game at the virtual file's offset: in the
kept pieces (data copied there and freed), else data itself adopted (freed
at unload); FALSE if neither could be */
H3_ALIGN static boolean direct_range_keep(unsigned long offset, unsigned char *data, unsigned long size)
{
	unsigned char *at = kept_alloc(size);

	if (at)
	{
		memcpy(at, data, size);
		free(data);
		return direct_range_add(offset, size, _direct_range_kept, at, 0);
	}
	return direct_range_add(offset, size, _direct_range_memory, data, 0);
}

struct stream
{
	unsigned long total;
	boolean failed;
};

/* size bytes of data at the stream's end: a copy kept (data NULL: zeros,
kept nowhere) */
H3_ALIGN static void stream_put(struct stream *s, void const *data, unsigned long size)
{
	if (s->failed)
		return;
	if (data && size)
	{
		unsigned char *copy = kept_alloc(size);

		if (copy)
		{
			memcpy(copy, data, size);
			if (!direct_range_add(CACHE_HEADER_SIZE + s->total, size, _direct_range_kept, copy, 0))
			{
				s->failed = TRUE;
				return;
			}
		}
		else
		{
			copy = malloc(size);
			if (!copy)
			{
				s->failed = TRUE;
				return;
			}
			memcpy(copy, data, size);
			if (!direct_range_add(CACHE_HEADER_SIZE + s->total, size, _direct_range_memory, copy, 0))
			{
				free(copy);
				s->failed = TRUE;
				return;
			}
		}
	}
	s->total += size;
}

/* size bytes of the kit pack's data (at pack_offset in h2kit.pack) at the
stream's end, read from the pack when asked */
H3_ALIGN static void stream_put_pack(struct stream *s, unsigned long pack_offset, unsigned long size)
{
	if (!s->failed && !direct_range_add(CACHE_HEADER_SIZE + s->total, size, _direct_range_pack, NULL, pack_offset))
		s->failed = TRUE;
	s->total += size;
}

H3_ALIGN static void stream_pad(struct stream *s, unsigned long alignment)
{
	while ((CACHE_HEADER_SIZE + s->total) & (alignment - 1))
		s->total++;
}

H3_ALIGN static unsigned long stream_offset(struct stream const *s)
{
	return CACHE_HEADER_SIZE + s->total;
}

#if defined(__GLIBC__) && !defined(HALO_VITA)
struct h3_mallinfo2 { size_t arena, ordblks, smblks, hblks, hblkhd, usmblks, fsmblks, uordblks, fordblks, keepcost; };
extern struct h3_mallinfo2 h3_mallinfo2(void) __asm__("mallinfo2");
static unsigned long heap_in_use(void)
{
	struct h3_mallinfo2 info = h3_mallinfo2();

	return (unsigned long)(info.uordblks + info.hblkhd);
}
#else
static unsigned long heap_in_use(void)
{
	return 0;
}
#endif
static unsigned long heap_base, heap_peak;
/* (h3vita, HALO_H3_HEAP_LOG=1) each sample logged with the builder's heap's
figures (h2_build_heap.h: the Vita tier's; 0 on the desktop) and its line:
where the make's peak lies */
H3_ALIGN static void heap_sample_log(long line)
{
	unsigned long in_use = 0, peak = 0, blocks = 0;

	if (!getenv("HALO_H3_HEAP_LOG"))
		return;
	h2_build_heap_figures(&in_use, &peak, &blocks);
	platform_log("h3 level: heap at line %ld: the build's heap %lu KB in use (%lu KB at most, %lu KB of blocks), the C heap %lu KB", line,
		in_use / 1024, peak / 1024, blocks / 1024, heap_in_use() / 1024);
	/* (HALO_H3_TRACE_LINE=<line> with HALO_H2_BUILD_HEAP_TRACE=1: what the
	heap holds at that sample, by caller) */
	if (getenv("HALO_H3_TRACE_LINE") && atol(getenv("HALO_H3_TRACE_LINE")) == line)
		h2_build_heap_trace_log("an h3 heap sample");
}
#define HEAP_SAMPLE() do { unsigned long in_use_ = heap_in_use(); if (in_use_ > heap_peak) heap_peak = in_use_; heap_sample_log(__LINE__); } while (0)

/* ---------- private code: the bitmaps' pixels, as they are read */

/* the Xbox's swizzle of a power-of-two texture (h2_level.c's) */
H3_ALIGN static unsigned long morton(unsigned long x, unsigned long y)
{
	unsigned long r = 0;
	int i;

	for (i = 0; i < 16; i++)
		r |= ((x >> i) & 1) << (2 * i) | ((y >> i) & 1) << (2 * i + 1);
	return r;
}

/* a colour as this engine draws it from Halo 3's texel of a curve. Halo 3
reads its maps as linear light (each decoded from its image's curve: xRGB
and gamma 2 squared, sRGB its curve, linear as they are), lights them,
and encodes the frame about gamma 2 for the screen (final_composite); this
engine multiplies the colours as they are on the screen. So a texel of the
linear curve - most of Halo 3's level maps - is drawn as its square root,
an sRGB one as its light's square root, xRGB and gamma 2 as they are
(HALO_H3_CURVES=0: every map as it is, as before) */
static unsigned char curve_tables[6][256];
static boolean curve_tables_made;

H3_ALIGN static unsigned char const *curve_table(unsigned char curve)
{
	if (getenv("HALO_H3_CURVES") && !atoi(getenv("HALO_H3_CURVES")))
		return NULL;
	if (curve != 3 && curve != 5)
		return NULL;
	if (!curve_tables_made)
	{
		int i;

		for (i = 0; i < 256; i++)
		{
			float v = i / 255.0f;
			float srgb = v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);

			curve_tables[3][i] = (unsigned char)(sqrtf(v) * 255.0f + 0.5f);
			curve_tables[5][i] = (unsigned char)(sqrtf(srgb) * 255.0f + 0.5f);
		}
		curve_tables_made = TRUE;
	}
	return curve_tables[curve];
}

/* Halo 3's detail multiplier (DETAIL_MULTIPLIER, 4.59479: a detail map's
grey, linear, made 1) as this engine's colours take it: its square root
(the drawn colours the light's square roots, curve_table) */
H3_ALIGN static float detail_multiplier_drawn(void)
{
	float multiplier = env_float("HALO_H3_DETAIL_MULTIPLIER", 4.59479f);

	return getenv("HALO_H3_CURVES") && !atoi(getenv("HALO_H3_CURVES")) ? multiplier : sqrtf(multiplier);
}

/* (h3/light-1) a bitmap's three channels' tables: its curve's (curve_table)
and its material's tint - drawn, the tint's square root, as the light's is.
FALSE: neither, the texels as they are */
H3_ALIGN static boolean bitmap_tables(struct bitmap const *b, unsigned char tables[3][256])
{
	unsigned char const *curve = b->not_colour ? NULL : curve_table(b->curve);
	int k, i;

	if (!curve && !b->tint_key)
		return FALSE;
	for (k = 0; k < 3; k++)
	{
		float tint = b->tint_key ? sqrtf(b->material_tint[k]) : 1.0f;

		for (i = 0; i < 256; i++)
		{
			float v = (float)(curve ? curve[i] : i) * tint;

			tables[k][i] = (unsigned char)(v > 255.0f ? 255.0f : v + 0.5f);
		}
	}
	return TRUE;
}

/* a 5- or 6-bit channel through the table */
H3_ALIGN static unsigned long curve_channel(unsigned char const *table, unsigned long value, unsigned long top)
{
	unsigned long v = table[(value * 255 + top / 2) / top];

	return (v * top + 127) / 255;
}

H3_ALIGN static unsigned long curve_565(unsigned char tables[3][256], unsigned long c)
{
	return curve_channel(tables[0], c >> 11 & 31, 31) << 11 | curve_channel(tables[1], c >> 5 & 63, 63) << 5 | curve_channel(tables[2], c & 31, 31);
}

/* DXT1/3/5 blocks' colours through the table (their two end colours: the
texels between them follow; a DXT1 block keeps its kind - four colours
when the first is the greater, three and transparent black when not) */
H3_ALIGN static void curve_blocks(unsigned char table[3][256], unsigned char *p, unsigned long size, unsigned short format)
{
	unsigned long step = format == 14 ? 8 : 16, colour = format == 14 ? 0 : 8, k;

	for (k = 0; k + step <= size; k += step)
	{
		unsigned char *b = p + k + colour;
		unsigned long c0 = read16(b), c1 = read16(b + 2), n0 = curve_565(table, c0), n1 = curve_565(table, c1);
		unsigned long bits = read32(b + 4), swapped = 0;
		int i;

		if (format == 14 && (c0 > c1) != (n0 > n1))
		{
			if (n0 == n1)
			{
				/* (one colour now: every texel the first) */
				if (c0 > c1)
					bits = 0;
			}
			else
			{
				unsigned long t = n0;

				n0 = n1;
				n1 = t;
				for (i = 0; i < 16; i++)
				{
					unsigned long index = bits >> (2 * i) & 3;

					/* (four colours: 0 1 2 3 -> 1 0 3 2; three: 0 1 -> 1 0, 2 and 3 kept) */
					index = index < 2 ? index ^ 1 : c0 > c1 ? index ^ 1 : index;
					swapped |= index << (2 * i);
				}
				bits = swapped;
			}
		}
		write16(b, (unsigned short)n0);
		write16(b + 2, (unsigned short)n1);
		write32(b + 4, bits);
	}
}

/* a signed BC4 block (BC5's halves: Halo 3's DXN normal maps are signed,
-1..1 - read unsigned, Last Resort's cliff bump's endpoints fall about 0
and 255) as unsigned bytes about 128 */
H3_ALIGN static void bc4_signed_decode(unsigned char const *block, unsigned char out[16])
{
	float a0 = (signed char)block[0] < -127 ? -1.0f : (signed char)block[0] / 127.0f;
	float a1 = (signed char)block[1] < -127 ? -1.0f : (signed char)block[1] / 127.0f, values[8];
	unsigned long long bits = 0;
	int i;

	for (i = 0; i < 6; i++)
		bits |= (unsigned long long)block[2 + i] << (8 * i);
	values[0] = a0;
	values[1] = a1;
	if ((signed char)block[0] > (signed char)block[1])
	{
		for (i = 1; i < 7; i++)
			values[i + 1] = ((7 - i) * a0 + i * a1) / 7.0f;
	}
	else
	{
		for (i = 1; i < 5; i++)
			values[i + 1] = ((5 - i) * a0 + i * a1) / 5.0f;
		values[6] = -1.0f;
		values[7] = 1.0f;
	}
	for (i = 0; i < 16; i++)
	{
		float v = values[(bits >> (3 * i)) & 7];

		out[i] = (unsigned char)(v * 127.5f + 127.5f + 0.5f);
	}
}

H3_ALIGN static boolean bc5_signed_decode(unsigned long w, unsigned long h, unsigned char const *p, unsigned long size,
	unsigned char *rgba)
{
	unsigned long bw = (w + 3) / 4, bh = (h + 3) / 4, bx, by;

	if (size < bw * bh * 16)
		return FALSE;
	for (by = 0; by < bh; by++)
	{
		for (bx = 0; bx < bw; bx++)
		{
			unsigned char nx[16], ny[16];
			int i;

			bc4_signed_decode(p + 16 * (by * bw + bx), nx);
			bc4_signed_decode(p + 16 * (by * bw + bx) + 8, ny);
			for (i = 0; i < 16; i++)
			{
				unsigned long x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);

				if (x < w && y < h)
				{
					unsigned char *o = rgba + 4 * (y * w + x);

					o[0] = nx[i];
					o[1] = ny[i];
					o[2] = 255;
					o[3] = 255;
				}
			}
		}
	}
	return TRUE;
}

/* a normal map (RGBA: x, y about 128; z made) as this engine's P8 bump:
each texel the nearest of its vector palette (bitmaps.c's; h2_level.c's
lookup), its mips the normals averaged, swizzled - the caller frees it */
H3_ALIGN static unsigned char *bump_p8_make(unsigned char const *rgba, unsigned long w, unsigned long h, unsigned long *size,
	unsigned short *mipmaps)
{
	extern unsigned long global_vector_palette[];
	static unsigned char lookup[32][32][32];
	static boolean lookup_made;
	unsigned long total = 0, lw, lh, at = 0, x, y;
	float *normals, *next;
	unsigned char *out;
	short levels = 0;

	if (!lookup_made)
	{
		int r, g, bl;

		for (r = 0; r < 32; r++)
			for (g = 0; g < 32; g++)
				for (bl = 0; bl < 32; bl++)
				{
					long best = 0, k;
					unsigned long best_distance = 0xFFFFFFFFUL;

					for (k = 0; k < 256; k++)
					{
						long dr = (long)((global_vector_palette[k] >> 16) & 255) - (r * 8 + 4);
						long dg = (long)((global_vector_palette[k] >> 8) & 255) - (g * 8 + 4);
						long db = (long)(global_vector_palette[k] & 255) - (bl * 8 + 4);
						unsigned long distance = (unsigned long)(dr * dr + dg * dg + db * db);

						if (distance < best_distance)
							best_distance = distance, best = k;
					}
					lookup[r][g][bl] = (unsigned char)best;
				}
		lookup_made = TRUE;
	}
	if ((w & (w - 1)) || (h & (h - 1)) || !w || !h)
		return NULL;
	for (lw = w, lh = h; ; lw = lw > 1 ? lw / 2 : 1, lh = lh > 1 ? lh / 2 : 1)
	{
		total += lw * lh;
		levels++;
		if (lw == 1 && lh == 1)
			break;
	}
	out = malloc(total);
	normals = malloc(w * h * 3 * sizeof(float));
	next = malloc(w * h * 3 * sizeof(float));
	if (!out || !normals || !next)
	{
		free(out);
		free(normals);
		free(next);
		return NULL;
	}
	for (y = 0; y < h * w; y++)
	{
		float nx = rgba[4 * y] / 127.5f - 1.0f, ny = rgba[4 * y + 1] / 127.5f - 1.0f, nz = 1.0f - nx * nx - ny * ny;

		normals[3 * y] = nx;
		normals[3 * y + 1] = ny;
		normals[3 * y + 2] = nz > 0.0f ? sqrtf(nz) : 0.0f;
	}
	for (lw = w, lh = h; ; )
	{
		unsigned long m = lw < lh ? lw : lh;

		for (y = 0; y < lh; y++)
		{
			for (x = 0; x < lw; x++)
			{
				float const *n = normals + 3 * (y * lw + x);
				float length = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
				int c[3], k;

				for (k = 0; k < 3; k++)
				{
					float v = length > 1e-5f ? n[k] / length : (k == 2 ? 1.0f : 0.0f);

					c[k] = (int)((v * 0.5f + 0.5f) * 255.0f + 0.5f);
					c[k] = c[k] < 0 ? 0 : c[k] > 255 ? 255 : c[k];
				}
				out[at + (x / m + (y / m) * (lw / m)) * m * m + morton(x % m, y % m)] = lookup[c[0] >> 3][c[1] >> 3][c[2] >> 3];
			}
		}
		at += lw * lh;
		if (lw == 1 && lh == 1)
			break;
		{
			unsigned long nw = lw > 1 ? lw / 2 : 1, nh = lh > 1 ? lh / 2 : 1;

			for (y = 0; y < nh; y++)
			{
				for (x = 0; x < nw; x++)
				{
					int k;

					unsigned long x0 = lw > 1 ? 2 * x : x, x1 = lw > 1 ? 2 * x + 1 : x;
					unsigned long y0 = lh > 1 ? 2 * y : y, y1 = lh > 1 ? 2 * y + 1 : y;

					for (k = 0; k < 3; k++)
					{
						next[3 * (y * nw + x) + k] = 0.25f * (normals[3 * (y0 * lw + x0) + k] + normals[3 * (y0 * lw + x1) + k] +
							normals[3 * (y1 * lw + x0) + k] + normals[3 * (y1 * lw + x1) + k]);
					}
				}
			}
			memcpy(normals, next, nw * nh * 3 * sizeof(float));
			lw = nw;
			lh = nh;
		}
	}
	free(normals);
	free(next);
	*size = total;
	*mipmaps = (unsigned short)(levels - 1);
	return out;
}

/* a bitmap's pixels read: into the file (uncompressed ones swizzled as
this engine's Xbox textures are; water's tinted) */
H3_ALIGN static void bitmap_got(struct build *build, struct bitmap *b, unsigned char *p)
{
	struct stream *stream = build->stream;
	unsigned long got = b->size;
	unsigned char *made = NULL;
	/* (a DXT3/5 whose alpha no shader draws: made DXT1, half its bytes -
	Halo 3 keeps specular masks there; HALO_H3_KEEP_ALPHA keeps them) */
	boolean opaque = !b->needs_alpha && !b->tint && b->mode != 4 && b->mode != 5 && !getenv("HALO_H3_KEEP_ALPHA");

	if (b->force_alpha && !b->convert)
	{
		b->convert = TRUE;
		b->made_width = b->width;
		b->made_height = b->height;
	}
	if (opaque && !b->convert && (b->format == 15 || b->format == 16) && b->mode == 0)
	{
		b->convert = TRUE;
		b->made_width = b->width;
		b->made_height = b->height;
	}
	if (b->convert)
	{
		unsigned char *rgba = malloc((unsigned long)b->width * b->height * 4);
		unsigned char *resized = malloc((unsigned long)b->made_width * b->made_height * 4);
		unsigned long made_size;
		unsigned short format, mipmaps;

		if (rgba && resized && (b->mode == 7 && b->format == 33 ? bc5_signed_decode(b->width, b->height, p, got, rgba) :
			image_decode(b->format, b->width, b->height, p, got, rgba)))
		{
			unsigned long n = (unsigned long)b->made_width * b->made_height, i;
			double sum[4] = { 0, 0, 0, 0 };

			image_resize(rgba, b->width, b->height, resized, b->made_width, b->made_height);
			{
				unsigned char tables[3][256];

				if (b->mode != 4 && bitmap_tables(b, tables))
				{
					for (i = 0; i < n; i++)
					{
						resized[4 * i] = tables[0][resized[4 * i]];
						resized[4 * i + 1] = tables[1][resized[4 * i + 1]];
						resized[4 * i + 2] = tables[2][resized[4 * i + 2]];
					}
				}
			}
			for (i = 0; i < n; i++)
			{
				sum[0] += resized[4 * i];
				sum[1] += resized[4 * i + 1];
				sum[2] += resized[4 * i + 2];
				sum[3] += 0.3 * resized[4 * i] + 0.59 * resized[4 * i + 1] + 0.11 * resized[4 * i + 2];
			}
			for (i = 0; i < 4; i++)
				b->mean[i] = (float)(sum[i] / n / 255.0);
			if (getenv("HALO_H3_DEBUG") && strstr(b->path, "cloud"))
			{
				unsigned long lo[4] = { 255, 255, 255, 255 }, hi[4] = { 0, 0, 0, 0 };
				int k;

				for (i = 0; i < n; i++)
				{
					for (k = 0; k < 4; k++)
					{
						if (resized[4 * i + k] < lo[k]) lo[k] = resized[4 * i + k];
						if (resized[4 * i + k] > hi[k]) hi[k] = resized[4 * i + k];
					}
				}
				fprintf(stderr, "h3 level: cloud bitmap %s format %d: r %lu-%lu g %lu-%lu b %lu-%lu a %lu-%lu\n", b->path, (int)b->format,
					lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], lo[3], hi[3]);
			}
			if (opaque)
			{
				for (i = 0; i < n; i++)
					resized[4 * i + 3] = 255;
			}
			if (b->force_alpha)
			{
				for (i = 0; i < n; i++)
					resized[4 * i + 3] = (unsigned char)b->force_alpha;
			}
			if (b->mode == 1 && b->keep && !b->kept)
			{
				/* (a composite samples it too: as it is, a texel at a time) */
				b->kept = malloc(n * 4);
				if (b->kept)
					memcpy(b->kept, resized, n * 4);
			}
			if (b->mode == 2)
			{
				/* (kept: a composite's source; its own texture a stand-in) */
				b->kept = resized;
				resized = NULL;
			}
			else if (b->mode == 6)
			{
				/* (its mean colour alone: its texture a stand-in) */
				free(resized);
				resized = NULL;
			}
			else if (b->mode == 5 && b->cloud && b->hazed)
			{
				/* (held as it is: sky_haze_write) */
			}
			else if (b->mode == 5)
			{
				float factor = env_float("HALO_H3_CLOUD_BRIGHTNESS", 2.2f);

				for (i = 0; i < n; i++)
				{
					int k;

					for (k = 0; k < 3; k++)
					{
						float v = b->white ? 235.0f : resized[4 * i + k] * (b->additive ? resized[4 * i + 3] / 255.0f * env_float("HALO_H3_CLOUD_ADDITIVE", 0.3f) : factor);

						resized[4 * i + k] = (unsigned char)(v > 255.0f ? 255.0f : v);
					}
				}
			}
			else if (b->mode == 1 || b->mode == 3)
			{
				/* (a detail: its colours over its mean lightness, a half on
				average; a lit base: times 4.59 times its detail's mean) */
				float factor = b->mode == 1 ? (b->mean[3] > 0.01f ? 0.5f / b->mean[3] : 1.0f) :
					b->scale_source != NONE ? detail_multiplier_drawn() * build->bitmaps[b->scale_source].mean[3] : 1.0f;

				for (i = 0; i < n; i++)
				{
					int k;

					for (k = 0; k < 3; k++)
					{
						float v = resized[4 * i + k] * factor;

						resized[4 * i + k] = (unsigned char)(v > 255.0f ? 255.0f : v);
					}
					if (b->mode == 1)
						resized[4 * i + 3] = 255;
				}
			}
			if (resized && b->hazed)
			{
				/* (held for its haze: sky_haze_write) */
				b->held_rgba = resized;
				free(rgba);
				return;
			}
			if (resized && b->mode == 7)
			{
				made = bump_p8_make(resized, b->made_width, b->made_height, &got, &b->mipmaps);
				if (made)
				{
					b->width = b->made_width;
					b->height = b->made_height;
					b->format = 17;
					b->swizzled = TRUE;
					p = made;
				}
			}
			else if (resized && b->mode == 4)
			{
				/* (the water's ripples: its normals (x, y; z made) as this
				engine's water wants them, A8R8G8B8 B G R = z y x, 4 mips,
				swizzled - h2_level.c's water_maps) */
				unsigned long lw, at = 0, total = 0, x, y;
				short levels = 0;

				for (lw = b->made_width; lw >= 16 && levels < 4; lw /= 2, levels++)
					total += lw * lw * 4;
				made = malloc(total);
				if (made && b->made_width == b->made_height)
				{
					for (i = 0; i < n; i++)
					{
						float nx = resized[4 * i] / 127.5f - 1.0f, ny = resized[4 * i + 1] / 127.5f - 1.0f;
						float nz = 1.0f - nx * nx - ny * ny;

						nz = nz > 0.0f ? sqrtf(nz) : 0.0f;
						resized[4 * i] = (unsigned char)((nz * 0.5f + 0.5f) * 255.0f);
						resized[4 * i + 1] = (unsigned char)((ny * 0.5f + 0.5f) * 255.0f);
						resized[4 * i + 2] = (unsigned char)((nx * 0.5f + 0.5f) * 255.0f);
						resized[4 * i + 3] = 255;
					}
					for (lw = b->made_width; at < total; lw /= 2)
					{
						unsigned long m = lw;

						for (y = 0; y < lw; y++)
						{
							for (x = 0; x < lw; x++)
								memcpy(made + at + 4 * ((x / m + (y / m) * (lw / m)) * m * m + morton(x % m, y % m)),
									resized + 4 * (y * lw + x), 4);
						}
						at += lw * lw * 4;
						for (y = 0; y < lw / 2; y++)
						{
							for (x = 0; x < lw / 2; x++)
							{
								int c;

								for (c = 0; c < 4; c++)
									resized[4 * (y * (lw / 2) + x) + c] = (unsigned char)((resized[4 * (2 * y * lw + 2 * x) + c] +
										resized[4 * (2 * y * lw + 2 * x + 1) + c] + resized[4 * ((2 * y + 1) * lw + 2 * x) + c] +
										resized[4 * ((2 * y + 1) * lw + 2 * x + 1) + c] + 2) / 4);
							}
						}
					}
					b->width = b->made_width;
					b->height = b->made_height;
					b->format = 11;
					b->mipmaps = (unsigned short)(levels - 1);
					b->swizzled = TRUE;
					p = made;
					got = total;
				}
				else
				{
					free(made);
					made = NULL;
				}
			}
			else if (resized && image_encode(resized, b->made_width, b->made_height, &made, &made_size, &format, &mipmaps))
			{
				b->width = b->made_width;
				b->height = b->made_height;
				b->format = format;
				b->mipmaps = mipmaps;
				p = made;
				got = made_size;
			}
		}
		free(rgba);
		free(resized);
		if (!made)
		{
			/* (not made: a grey stand-in, DXT1 4 x 4) */
			static unsigned char grey[8] = { 0x10, 0x84, 0x10, 0x84, 0, 0, 0, 0 };

			b->width = b->height = 4;
			b->format = 14;
			b->mipmaps = 0;
			p = grey;
			got = 8;
		}
	}
	else if (b->format >= 14 && b->format <= 16 && (b->tint_key || (!b->not_colour && curve_table(b->curve))))
	{
		unsigned char tables[3][256];

		if (bitmap_tables(b, tables))
			curve_blocks(tables, p, got, b->format);
	}
	else if (!(b->format >= 14 && b->format <= 16))
	{
		unsigned long bpp = b->format == 10 || b->format == 11 ? 4 : b->format == 3 || b->format == 6 ||
			b->format == 8 || b->format == 9 ? 2 : 1;
		unsigned long w = b->width, h = b->height, x, y, m = w < h ? w : h;
		unsigned char *swizzled = got < w * h * bpp ? NULL : malloc(w * h * bpp);

		if (swizzled)
		{
			for (y = 0; y < h; y++)
			{
				for (x = 0; x < w; x++)
				{
					unsigned long index = (x / m + (y / m) * (w / m)) * m * m + morton(x % m, y % m);

					memcpy(swizzled + bpp * index, p + bpp * (y * w + x), bpp);
				}
			}
			memcpy(p, swizzled, w * h * bpp);
			free(swizzled);
		}
	}
	if (b->tint && b->format == 16)
	{
		unsigned long k;

		for (k = 0; k + 16 <= got; k += 16)
		{
			write16(p + k + 8, b->tint);
			write16(p + k + 10, b->tint);
			write32(p + k + 12, 0);
			if (b->alpha)
			{
				memset(p + k, 0, 8);
				p[k] = p[k + 1] = b->alpha;
			}
		}
	}
	/* (a detail object's sprite: its top level alone - its mips' averaged
	alpha would fail the alpha test, the thin blades gone a few units off) */
	if (b->sprite && b->mipmaps && b->format >= 14 && b->format <= 16 && !b->swizzled)
	{
		unsigned long top = mip_bytes(b->format, b->width, b->height);

		if (top && top < got)
		{
			got = top;
			b->mipmaps = 0;
		}
	}
	stream_pad(stream, 0x80);
	b->file_offset = stream_offset(stream);
	b->file_size = got;
	stream_put(stream, p, got);
	free(made);
	b->written = TRUE;
	build->result->bitmap_bytes += got;
	if (getenv("HALO_H3_DEBUG"))
		fprintf(stderr, "h3 level: bitmap written %lu %ux%u %s\n", got, b->width, b->height, b->path);
}

/* the sky as Halo 3's atmosphere draws it at an elevation (radians): the
inscatter of its whole thickness, averaged around the compass, at the
level's exposure, as drawn (0..1) */
H3_ALIGN static void sky_atmosphere_color(struct build *build, float elevation, float c[3])
{
	double sum[3] = { 0.0, 0.0, 0.0 };
	int a, k;

	for (a = 0; a < 16; a++)
	{
		float azimuth = a * 3.1415926535f / 8.0f, scene[3], extinction[3], inscatter[3];
		float distance = build->atmosphere_sky.maximum_thickness > 0.0f ? build->atmosphere_sky.maximum_thickness : 65536.0f;

		scene[0] = build->view_point[0] + distance * cosf(elevation) * cosf(azimuth);
		scene[1] = build->view_point[1] + distance * cosf(elevation) * sinf(azimuth);
		scene[2] = build->view_point[2] + distance * sinf(elevation);
		atmosphere_scatter(&build->atmosphere_sky, build->view_point, scene, extinction, inscatter);
		for (k = 0; k < 3; k++)
			sum[k] += inscatter[k] + extinction[k] * build->sky_dome_albedo[k];
	}
	for (k = 0; k < 3; k++)
	{
		float v = sqrtf((float)(sum[k] / 16.0) * sky_exposure_of(build));

		c[k] = v > 1.0f ? 1.0f : v;
	}
}

#if !defined(HALO_VITA) && !defined(HALO_GXM_NULL)
/* (h3/pc-acc, the desktop) the level's atmosphere (its first setting, the
one Halo 3 draws the level's surfaces through - atmosphere.fx's
compute_scattering, atmosphere_scatter) as this engine's sky's outdoor fog,
the one a CE sky gives every cluster that shows it: a linear ramp of
density from start to opaque distance up to a most, toward one colour
(render_sky.c / the rasterizer's per-vertex fog). Halo 3's
scene * extinction + inscatter seen level from the level's middle, around
the compass: the fog's share 1 - extinction (its rgb's mean), the colour
the inscatter over that share - Halo 3's sun-lit haze - at the level's
exposure, as drawn (sky_atmosphere_color's rule); the most the share at the
setting's maximum thickness (at most 1), the ramp through the share's half
way point (twice its distance: the ramp's half there too). Under Halo 3's
height falloff the fog of a level view is its view height's: the view
point's. fog: colour, most, start, opaque distance. FALSE: no atmosphere,
or HALO_H3_FOG=0 (the level unfogged, as before h3/pc-acc) */
H3_ALIGN static boolean atmosphere_fog(struct build *build, float fog[6])
{
	struct atmosphere const *a = &build->atmosphere_level;
	float far_distance = a->maximum_thickness > 1.0f ? a->maximum_thickness : 2048.0f;
	float most = 0.0f, half = 0.0f, colour[3] = { 0.0f, 0.0f, 0.0f };
	double inscatter_sum[3] = { 0.0, 0.0, 0.0 }, share_sum = 0.0;
	char const *env = getenv("HALO_H3_FOG");
	int step, k;

	if (!a->on || (env && atoi(env) == 0))
		return FALSE;
	/* (the share at a distance, around the compass; the colour from the
	far distance's light) */
	for (step = 0; step <= 64; step++)
	{
		float distance = far_distance * (float)step / 64.0f;
		double share = 0.0;
		int azimuth;

		for (azimuth = 0; azimuth < 16; azimuth++)
		{
			float angle = azimuth * 3.1415926535f / 8.0f, scene[3], extinction[3], inscatter[3];

			scene[0] = build->view_point[0] + distance * cosf(angle);
			scene[1] = build->view_point[1] + distance * sinf(angle);
			scene[2] = build->view_point[2];
			atmosphere_scatter(a, build->view_point, scene, extinction, inscatter);
			share += (3.0 - extinction[0] - extinction[1] - extinction[2]) / 3.0 / 16.0;
			if (step == 64)
			{
				for (k = 0; k < 3; k++)
					inscatter_sum[k] += inscatter[k] / 16.0;
				share_sum += (3.0 - extinction[0] - extinction[1] - extinction[2]) / 3.0 / 16.0;
			}
		}
		if (step == 64)
			most = (float)share;
	}
	if (most <= 0.001f || share_sum <= 0.0)
		return FALSE;
	if (most > 1.0f)
		most = 1.0f;
	/* (the half way point: the distance whose share is half the most,
	halved down to a hundredth of a unit - the share grows with distance) */
	{
		float low = 0.0f, high = far_distance;
		int round;

		for (round = 0; round < 40 && high - low > 0.01f; round++)
		{
			float distance = 0.5f * (low + high), scene[3], extinction[3], inscatter[3];

			scene[0] = build->view_point[0] + distance;
			scene[1] = build->view_point[1];
			scene[2] = build->view_point[2];
			atmosphere_scatter(a, build->view_point, scene, extinction, inscatter);
			if ((3.0f - extinction[0] - extinction[1] - extinction[2]) / 3.0f >= 0.5f * most)
				high = distance;
			else
				low = distance;
		}
		half = high;
	}
	if (half <= 0.0f)
		half = far_distance * 0.5f;
	for (k = 0; k < 3; k++)
	{
		float v = sqrtf((float)(inscatter_sum[k] / share_sum) * (build->exposure > 0.0f ? build->exposure : 1.0f));

		colour[k] = v > 1.0f ? 1.0f : v;
	}
	fog[0] = colour[0];
	fog[1] = colour[1];
	fog[2] = colour[2];
	fog[3] = most * env_float("HALO_H3_FOG_DENSITY", 1.0f);
	if (fog[3] > 1.0f)
		fog[3] = 1.0f;
	/* (Halo 3's distance bias: a negative one leaves the first units clear -
	the ramp starts there) */
	fog[4] = a->distance_bias < 0.0f ? -a->distance_bias : 0.0f;
	if (fog[4] > half)
		fog[4] = 0.0f;
	fog[5] = fog[4] + 2.0f * (half - fog[4]) * env_float("HALO_H3_FOG_DISTANCE", 1.0f);
	platform_log("h3 level: fog from the atmosphere %s: colour %.3f %.3f %.3f, most %.2f, %.1f to %.1f (share half at %.1f; sea level %.1f, bias %.1f, thickness %.1f, the view at %.1f %.1f %.1f)",
		a->name, fog[0], fog[1], fog[2], fog[3], fog[4], fog[5], half, a->sea_level, a->distance_bias, a->maximum_thickness, build->view_point[0], build->view_point[1], build->view_point[2]);
	return TRUE;
}

/* (h3/pc-acc, the desktop) whether Halo 3 draws the level's sky as its
atmosphere's inscatter alone: the sky setting's extinction to the sky
model's vertices (sky_add: Halo 3 scatters to each sky vertex, as the
haze of the sky's maps does here), their mean, under HALO_H3_FOG_SKY_LEFT
(0.3) - Guardian's forest haze - its sky model's colours hardly show; this
engine's sky then draws no model, and the frame's clear colour, the fog's
(rasterizer_xbox.c), stands in for the inscatter. HALO_H3_FOG_SKY=0: the
model drawn as before */
H3_ALIGN static boolean atmosphere_sky_hidden(struct build *build)
{
	char const *env = getenv("HALO_H3_FOG_SKY");
	float mean;

	if (!build->atmosphere_sky.on || build->sky_extinction_count <= 0 || (env && atoi(env) == 0))
		return FALSE;
	mean = (float)(build->sky_extinction_sum / build->sky_extinction_count);
	platform_log("h3 level: the sky model's vertices keep %.3f of their colour through the sky's atmosphere (%ld vertices)", mean,
		build->sky_extinction_count);
	return mean < env_float("HALO_H3_FOG_SKY_LEFT", 0.3f);
}
#endif

/* the sky's maps held for their haze (bitmap_got) written: each texel's
light (its drawn colour squared) times its sky vertices' mean extinction
plus their mean inscatter at the level's exposure - Halo 3's
(colour * extinction + inscatter) * exposure, the sky drawn as bright as its
maps (its light times the exposure 1) - then the made sky's maps (their
colours the atmosphere's: sky_atmosphere_color) */
H3_ALIGN static void sky_haze_write(struct build *build)
{
	long i;

	for (i = 0; i < build->bitmap_count; i++)
	{
		struct bitmap *b = &build->bitmaps[i];
		unsigned char *made = NULL;
		unsigned long made_size, n, j;
		unsigned short format, mipmaps;
		float extinction[3] = { 1.0f, 1.0f, 1.0f }, inscatter[3] = { 0.0f, 0.0f, 0.0f };
		int k;

		if (b->generated)
		{
			bitmap_generated_write(build, b);
			continue;
		}
		if (!b->hazed || !b->held_rgba)
			continue;
		if (b->haze_sum[6] > 0.0)
		{
			for (k = 0; k < 3; k++)
			{
				extinction[k] = (float)(b->haze_sum[k] / b->haze_sum[6]);
				inscatter[k] = (float)(b->haze_sum[3 + k] / b->haze_sum[6]) * sky_exposure_of(build);
			}
		}
		platform_log("h3 level: sky haze %s: extinction %.2f %.2f %.2f, inscatter %.2f %.2f %.2f (%.0f vertices%s)", b->path, extinction[0],
			extinction[1], extinction[2], inscatter[0], inscatter[1], inscatter[2], b->haze_sum[6],
			b->cloud ? b->haze_coloured ? ", a cloud by its vertices' colours" : ", a cloud brightened" : "");
		n = (unsigned long)b->made_width * b->made_height;
		if (b->cloud && !(b->haze_coloured && b->haze_sum[6] > 0.0))
		{
			/* (a cloud whose vertices have no colours: brightened, as before) */
			float factor = env_float("HALO_H3_CLOUD_BRIGHTNESS", 2.2f);

			for (j = 0; j < n; j++)
			{
				for (k = 0; k < 3; k++)
				{
					float v = b->white ? 235.0f : b->held_rgba[4 * j + k] * (b->additive ? b->held_rgba[4 * j + 3] / 255.0f *
						env_float("HALO_H3_CLOUD_ADDITIVE", 0.3f) : factor);

					b->held_rgba[4 * j + k] = (unsigned char)(v > 255.0f ? 255.0f : v);
				}
			}
		}
		else if (b->cloud)
		{
			/* (Halo 3's: (its albedo times its vertices' colour times the
			extinction, plus the inscatter) times the exposure; an added cloud
			takes no inscatter, its colour by its alpha; a mask's albedo white) */
			float exposure = sky_exposure_of(build);

			for (j = 0; j < n; j++)
			{
				for (k = 0; k < 3; k++)
				{
					float light = b->white ? 1.0f : b->held_rgba[4 * j + k] / 255.0f;
					float v = light * light * extinction[k] * exposure;

					if (b->additive)
						v *= b->held_rgba[4 * j + 3] / 255.0f;
					else
						v += inscatter[k];
					v = sqrtf(v);
					b->held_rgba[4 * j + k] = (unsigned char)(v > 1.0f ? 255.0f : v * 255.0f + 0.5f);
				}
			}
		}
		else for (j = 0; j < n; j++)
		{
			for (k = 0; k < 3; k++)
			{
				float light = b->held_rgba[4 * j + k] / 255.0f;
				float v = sqrtf(light * light * extinction[k] + inscatter[k]);

				b->held_rgba[4 * j + k] = (unsigned char)(v > 1.0f ? 255.0f : v * 255.0f + 0.5f);
			}
		}
		if (image_encode(b->held_rgba, b->made_width, b->made_height, &made, &made_size, &format, &mipmaps))
		{
			b->width = b->made_width;
			b->height = b->made_height;
			b->format = format;
			b->mipmaps = mipmaps;
			stream_pad(build->stream, 0x80);
			b->file_offset = stream_offset(build->stream);
			b->file_size = made_size;
			stream_put(build->stream, made, made_size);
			b->written = TRUE;
			build->result->bitmap_bytes += made_size;
		}
		free(made);
		free(b->held_rgba);
		b->held_rgba = NULL;
	}
}

/* a made bitmap (the sky's gradient: Halo 3's atmosphere's bright haze at
the horizon to a clear blue at the zenith - HALO_H3_SKY_ZENITH and
HALO_H3_SKY_HORIZON, "r g b" 0..1) encoded and written */
/* the made sky's colour at an elevation (radians; under the horizon its
haze), 0..1 */
H3_ALIGN static void sky_color(float elevation, float c[3])
{
	float zenith[3] = { 0.36f, 0.56f, 0.86f }, horizon[3] = { 0.84f, 0.88f, 0.92f }, below[3] = { 0.62f, 0.70f, 0.76f };
	float t = 0.8f - 0.8f * elevation / 1.5707963f;
	int k;

	if (getenv("HALO_H3_SKY_ZENITH"))
		sscanf(getenv("HALO_H3_SKY_ZENITH"), "%f %f %f", &zenith[0], &zenith[1], &zenith[2]);
	if (getenv("HALO_H3_SKY_HORIZON"))
		sscanf(getenv("HALO_H3_SKY_HORIZON"), "%f %f %f", &horizon[0], &horizon[1], &horizon[2]);
	if (t < 0.0f)
		t = 0.0f;
	if (t > 1.0f)
		t = 1.0f;
	for (k = 0; k < 3; k++)
	{
		if (t < 0.8f)
		{
			float a = t / 0.8f;

			c[k] = zenith[k] + (horizon[k] - zenith[k]) * a * a;
		}
		else
			c[k] = horizon[k] + (below[k] - horizon[k]) * (t - 0.8f) / 0.2f;
	}
}

/* the water's reflection: a cube of the made sky (6 faces of 64, D3D's
order +x -x +y -y +z -z, A8R8G8B8 swizzled - as h2_level.c's), the sky
seen in Zanzibar's water (its water_color_pure, 0.035 0.133 0.129, the
tint's direction) */
H3_ALIGN static void bitmap_cube_write(struct build *build, struct bitmap *b)
{
	enum { FACE = 64 };
	unsigned char *faces = malloc(6UL * FACE * FACE * 4), *linear = malloc((unsigned long)FACE * FACE * 4);
	int face;

	if (!faces || !linear)
	{
		free(faces);
		free(linear);
		return;
	}
	for (face = 0; face < 6; face++)
	{
		unsigned long x, y, m = FACE;

		for (y = 0; y < FACE; y++)
		{
			for (x = 0; x < FACE; x++)
			{
				float s = 2.0f * (x + 0.5f) / FACE - 1.0f, t = 2.0f * (y + 0.5f) / FACE - 1.0f;
				float d[3], length, c[3];
				static float const tint[3] = { 0.62f, 0.86f, 0.84f };
				int k;

				switch (face)
				{
				case 0: d[0] = 1.0f; d[1] = -t; d[2] = -s; break;
				case 1: d[0] = -1.0f; d[1] = -t; d[2] = s; break;
				case 2: d[0] = s; d[1] = 1.0f; d[2] = t; break;
				case 3: d[0] = s; d[1] = -1.0f; d[2] = -t; break;
				case 4: d[0] = s; d[1] = -t; d[2] = 1.0f; break;
				default: d[0] = -s; d[1] = -t; d[2] = -1.0f; break;
				}
				length = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
				sky_color(asinf(d[2] / length), c);
				/* (B G R A: the sky's colour a third, the rest the sea's own -
				water_color_pure brought up to the sea's look in daylight) */
				{
					static float const sea[3] = { 0.06f, 0.24f, 0.30f };
					float share = env_float("HALO_H3_WATER_SKY_SHARE", 0.35f);

					for (k = 0; k < 3; k++)
						linear[4 * (y * FACE + x) + 2 - k] = (unsigned char)((sea[k] + (c[k] * tint[k] - sea[k]) * share) * 255.0f);
				}
				linear[4 * (y * FACE + x) + 3] = 255;
			}
		}
		for (y = 0; y < FACE; y++)
		{
			for (x = 0; x < FACE; x++)
				memcpy(faces + 4UL * FACE * FACE * face + 4 * ((x / m + (y / m) * (FACE / m)) * m * m + morton(x % m, y % m)),
					linear + 4 * (y * FACE + x), 4);
		}
	}
	b->format = 11;
	b->mipmaps = 0;
	b->width = b->height = FACE;
	stream_pad(build->stream, 0x80);
	b->file_offset = stream_offset(build->stream);
	b->file_size = 6UL * FACE * FACE * 4;
	stream_put(build->stream, faces, b->file_size);
	b->written = TRUE;
	build->result->bitmap_bytes += b->file_size;
	free(faces);
	free(linear);
}

/* Halo 3's water's wave slopes (wave_slope_array: 16 slices of 128, two
signed bytes a texel holding 0 to 1 - restore_slope scales them to the
shader's slope range) as a sheet of its slices, 4 across: A8R8G8B8, R the
x slope and G the y, 0 to 254; its mips each cell's own (the cells are
powers of two) */
H3_ALIGN static void bitmap_slope_sheet_write(struct build *build, struct bitmap *b)
{
	enum { CELL = 128, ACROSS = 4, SIDE = CELL * ACROSS, LEVELS = 8 };
	unsigned char image[0x30];
	unsigned long size = 0, chain = 0, lw, at = 0, total = 0, x, y;
	unsigned char *bytes = bitmap_bytes_now(build, b->h3_tag, image, &size);
	unsigned char *sheet = malloc((unsigned long)SIDE * SIDE * 4), *made = NULL;
	long slices = bytes ? image[8] : 0, levels = bytes ? 1 + image[0x14] : 0, k;
	double sum = 0.0;
	long least = 127, most = -128;

	for (k = 0; k < levels; k++)
		chain += (unsigned long)((CELL >> k) ? (CELL >> k) : 1) * ((CELL >> k) ? (CELL >> k) : 1) * 2;
	for (lw = SIDE, k = 0; k < LEVELS; lw /= 2, k++)
		total += lw * lw * 4;
	made = malloc(total);
	if (!bytes || !sheet || !made || read16(image + 4) != CELL || read16(image + 6) != CELL || slices != ACROSS * ACROSS ||
		read16(image + 0xC) != 22 || size < (unsigned long)slices * chain)
	{
		platform_log("h3 level: water: the wave slope array %s is not 16 slices of 128 (V8U8): none made", b->path);
		free(bytes);
		free(sheet);
		free(made);
		return;
	}
	for (k = 0; k < slices; k++)
	{
		unsigned char const *slice = bytes + (unsigned long)k * chain;

		for (y = 0; y < CELL; y++)
		{
			for (x = 0; x < CELL; x++)
			{
				signed char u = (signed char)slice[2 * (y * CELL + x)], v = (signed char)slice[2 * (y * CELL + x) + 1];
				unsigned char *o = sheet + 4 * (((k / ACROSS) * CELL + y) * SIDE + (k % ACROSS) * CELL + x);

				sum += u;
				least = u < least ? u : least;
				most = u > most ? u : most;
				o[0] = 255;
				o[1] = (unsigned char)((v < 0 ? 0 : v) * 2);
				o[2] = (unsigned char)((u < 0 ? 0 : u) * 2);
				o[3] = 255;
			}
		}
	}
	platform_log("h3 level: water: wave slopes %s: %ld slices, x slope bytes %ld..%ld mean %.1f", b->path, slices, least, most,
		sum / ((double)slices * CELL * CELL));
	for (lw = SIDE; at < total; lw /= 2)
	{
		for (y = 0; y < lw; y++)
		{
			for (x = 0; x < lw; x++)
				memcpy(made + at + 4 * morton(x, y), sheet + 4 * (y * lw + x), 4);
		}
		at += lw * lw * 4;
		for (y = 0; y < lw / 2; y++)
		{
			for (x = 0; x < lw / 2; x++)
			{
				int c;

				for (c = 0; c < 4; c++)
					sheet[4 * (y * (lw / 2) + x) + c] = (unsigned char)((sheet[4 * (2 * y * lw + 2 * x) + c] +
						sheet[4 * (2 * y * lw + 2 * x + 1) + c] + sheet[4 * ((2 * y + 1) * lw + 2 * x) + c] +
						sheet[4 * ((2 * y + 1) * lw + 2 * x + 1) + c] + 2) / 4);
			}
		}
	}
	b->format = 11;
	b->mipmaps = LEVELS - 1;
	b->width = b->height = SIDE;
	b->swizzled = TRUE;
	stream_pad(build->stream, 0x80);
	b->file_offset = stream_offset(build->stream);
	b->file_size = total;
	stream_put(build->stream, made, total);
	b->written = TRUE;
	build->result->bitmap_bytes += total;
	free(bytes);
	free(sheet);
	free(made);
}

/* whether a bitmap's first image is a cube this reads: a format decoded
here (image_decode), a power of two a side, 16 to 256 */
H3_ALIGN static boolean water_cube_readable(unsigned char const *image)
{
	unsigned long side = (unsigned short)read16(image + 4);

	return read16(image + 0xA) == 2 && mip_bytes((unsigned short)read16(image + 0xC), 4, 4) != 0 &&
		side == (unsigned short)read16(image + 6) && side >= 16 && side <= 256 && is_power_of_two(side);
}

/* a bitmap tag's first image (0x30 bytes); FALSE if there is none */
H3_ALIGN static boolean bitmap_image_header(struct build *build, long tag, unsigned char image[0x30])
{
	struct h3_cache_tag t;
	long count;
	unsigned long images;

	return tag != NONE && h3_cache_tag_get(build->cache, tag, &t) && t.group_tag == 'bitm' &&
		h3_cache_block(build->cache, t.address + 0x60, 0x30, &count, &images) && count > 0 &&
		h3_cache_read(build->cache, images, 0x30, image);
}

/* Halo 3's water's environment map (a cube, A8R8G8B8: its alpha the
sun's spot, water_shading.fx's sunspot_cut) as this engine's cube: the six
faces in D3D's order, as Halo 3 looks them up, each swizzled */
H3_ALIGN static void bitmap_water_cube_write(struct build *build, struct bitmap *b)
{
	unsigned char image[0x30];
	unsigned long size = 0, chain = 0, x, y;
	unsigned char *bytes = bitmap_bytes_now(build, b->h3_tag, image, &size);
	unsigned long FACE = bytes ? (unsigned short)read16(image + 4) : 0;
	unsigned char *faces = FACE ? malloc(6UL * FACE * FACE * 4) : NULL;
	long levels = bytes ? 1 + image[0x14] : 0, k;
	int face;

	unsigned short format = bytes ? (unsigned short)read16(image + 0xC) : 0;
	unsigned char *rgba = FACE ? malloc(FACE * FACE * 4) : NULL;

	for (k = 0; k < levels; k++)
		chain += mip_bytes(format, FACE >> k, FACE >> k);
	if (!bytes || !faces || !rgba || !water_cube_readable(image) || size < 6 * chain)
	{
		/* (checked when the water was gathered, water_cube_readable: a page
		that cannot be read. A grey cube stands in) */
		platform_log("h3 level: water: the environment map %s could not be read (%lu bytes of %lu)", b->path, size, 6 * chain);
		free(bytes);
		free(faces);
		FACE = 4;
		faces = malloc(6UL * FACE * FACE * 4);
		if (!faces)
			return;
		memset(faces, 0x40, 6UL * FACE * FACE * 4);
		bytes = NULL;
	}
	for (face = 0; bytes && face < 6; face++)
	{
		if (!image_decode(format, FACE, FACE, bytes + (unsigned long)face * chain, chain, rgba))
			memset(rgba, 0x40, FACE * FACE * 4);
		if (getenv("HALO_H3_WATER_DEBUG"))
		{
			unsigned long n, bins[6] = { 0, 0, 0, 0, 0, 0 }, sum[4] = { 0, 0, 0, 0 }, top[4] = { 0, 0, 0, 0 }, top_n = 0;

			for (n = 0; n < FACE * FACE; n++)
			{
				unsigned char a = rgba[4 * n + 3];

				bins[a < 2 ? 0 : a < 4 ? 1 : a < 8 ? 2 : a < 32 ? 3 : a < 128 ? 4 : 5]++;
				sum[0] += rgba[4 * n], sum[1] += rgba[4 * n + 1], sum[2] += rgba[4 * n + 2], sum[3] += a;
				if (n < FACE * 8)
					top[0] += rgba[4 * n], top[1] += rgba[4 * n + 1], top[2] += rgba[4 * n + 2], top[3] += a, top_n++;
			}
			platform_log("h3 level: water: cube face %d: mean rgb %lu %lu %lu alpha %lu; first rows %lu %lu %lu alpha %lu; alpha <2 %lu, <4 %lu, <8 %lu, <32 %lu, <128 %lu, more %lu",
				face, sum[0] / n, sum[1] / n, sum[2] / n, sum[3] / n, top[0] / top_n, top[1] / top_n, top[2] / top_n, top[3] / top_n,
				bins[0], bins[1], bins[2], bins[3], bins[4], bins[5]);
		}
		for (y = 0; y < FACE; y++)
		{
			for (x = 0; x < FACE; x++)
			{
				unsigned char const *c = rgba + 4 * (y * FACE + x);
				unsigned char *o = faces + 4UL * FACE * FACE * face + 4 * morton(x, y);

				/* (B G R A) */
				o[0] = c[2], o[1] = c[1], o[2] = c[0], o[3] = c[3];
			}
		}
	}
	free(rgba);
	b->format = 11;
	b->mipmaps = 0;
	b->width = b->height = (unsigned short)FACE;
	stream_pad(build->stream, 0x80);
	b->file_offset = stream_offset(build->stream);
	b->file_size = 6UL * FACE * FACE * 4;
	stream_put(build->stream, faces, b->file_size);
	b->written = TRUE;
	build->result->bitmap_bytes += b->file_size;
	free(bytes);
	free(faces);
}

/* the sky dome's map (sky_dome_draw's) written: the texels no triangle drew
taken from their drawn neighbours, each at the level's exposure, as drawn */
H3_ALIGN static boolean sky_dome_write(struct build *build, struct bitmap *b)
{
	unsigned long n = (unsigned long)SKY_DOME_SIDE * SKY_DOME_SIDE, i, made_size;
	unsigned char *rgba = malloc(n * 4), *made = NULL;
	float *image = build->sky_dome_image, *next = malloc(n * 4 * sizeof(float));
	unsigned short format, mipmaps;
	float scale = sky_exposure_of(build) * env_float("HALO_H3_SKY_DOME_SCALE", 1.0f);
	long pass, x, y, drawn = 0;
	boolean ok = FALSE;

	for (i = 0; i < n && image; i++)
		drawn += image[4 * i + 3] > 0.0f;
	if (!rgba || !next || !image || !drawn)
	{
		free(rgba);
		free(next);
		return FALSE;
	}
	for (pass = 0; pass < 2 * SKY_DOME_SIDE; pass++)
	{
		long filled = 0;

		memcpy(next, image, n * 4 * sizeof(float));
		for (y = 0; y < SKY_DOME_SIDE; y++)
		{
			for (x = 0; x < SKY_DOME_SIDE; x++)
			{
				float const *o = image + 4 * (y * SKY_DOME_SIDE + x);
				float sum[3] = { 0.0f, 0.0f, 0.0f };
				int count = 0, dx, dy, k;

				if (o[3] > 0.0f)
					continue;
				for (dy = -1; dy <= 1; dy++)
				{
					for (dx = -1; dx <= 1; dx++)
					{
						long nx = x + dx, ny = y + dy;
						float const *q;

						if (nx < 0 || ny < 0 || nx >= SKY_DOME_SIDE || ny >= SKY_DOME_SIDE)
							continue;
						q = image + 4 * (ny * SKY_DOME_SIDE + nx);
						if (q[3] <= 0.0f)
							continue;
						for (k = 0; k < 3; k++)
							sum[k] += q[k];
						count++;
					}
				}
				if (count)
				{
					float *w = next + 4 * (y * SKY_DOME_SIDE + x);

					for (k = 0; k < 3; k++)
						w[k] = sum[k] / count;
					w[3] = 0.25f;
					filled++;
				}
			}
		}
		memcpy(image, next, n * 4 * sizeof(float));
		if (!filled)
			break;
	}
	for (i = 0; i < n; i++)
	{
		int k;

		for (k = 0; k < 3; k++)
		{
			float v = sqrtf(image[4 * i + k] * scale > 0.0f ? image[4 * i + k] * scale : 0.0f);

			rgba[4 * i + k] = (unsigned char)(v > 1.0f ? 255.0f : v * 255.0f + 0.5f);
		}
		rgba[4 * i + 3] = 255;
	}
	if (image_encode(rgba, SKY_DOME_SIDE, SKY_DOME_SIDE, &made, &made_size, &format, &mipmaps))
	{
		b->width = b->made_width = SKY_DOME_SIDE;
		b->height = b->made_height = SKY_DOME_SIDE;
		b->format = format;
		b->mipmaps = mipmaps;
		stream_pad(build->stream, 0x80);
		b->file_offset = stream_offset(build->stream);
		b->file_size = made_size;
		stream_put(build->stream, made, made_size);
		b->written = TRUE;
		build->result->bitmap_bytes += made_size;
		ok = TRUE;
		platform_log("h3 level: the sky dome drawn by its vertices' colours through the sky's atmosphere (%ld of %lu texels drawn, exposure %.2f)",
			drawn, n, scale);
	}
	free(made);
	free(rgba);
	free(next);
	return ok;
}

H3_ALIGN static void bitmap_generated_write(struct build *build, struct bitmap *b)
{
	if (b->generated_kind == 1)
	{
		bitmap_cube_write(build, b);
		return;
	}
	if (b->generated_kind == 3)
	{
		bitmap_slope_sheet_write(build, b);
		return;
	}
	if (b->generated_kind == 4)
	{
		bitmap_water_cube_write(build, b);
		return;
	}

	unsigned char *rgba = malloc((unsigned long)b->width * b->height * 4);
	unsigned char *made = NULL;
	unsigned long made_size, x, y;
	unsigned short format, mipmaps;
	float zenith[3] = { 0.36f, 0.56f, 0.86f }, horizon[3] = { 0.84f, 0.88f, 0.92f }, below[3] = { 0.62f, 0.70f, 0.76f };

	if (getenv("HALO_H3_SKY_ZENITH"))
		sscanf(getenv("HALO_H3_SKY_ZENITH"), "%f %f %f", &zenith[0], &zenith[1], &zenith[2]);
	if (getenv("HALO_H3_SKY_HORIZON"))
		sscanf(getenv("HALO_H3_SKY_HORIZON"), "%f %f %f", &horizon[0], &horizon[1], &horizon[2]);
	if (!rgba)
		return;
	/* (Halo 3's dome, where its vertices have colours: sky_dome_draw) */
	if (b->generated_kind == 0 && build->sky_dome_image && sky_dome_write(build, b))
	{
		free(rgba);
		return;
	}
	for (y = 0; y < b->height; y++)
	{
		/* (the top 0.8 the sky, zenith to horizon - faster near the horizon -
		then the haze under it) */
		float t = (float)y / (float)(b->height - 1), c[3];
		int k;

		if (b->generated_kind == 0 && build->atmosphere_sky.on && getenv("HALO_H3_SKY_ATMOSPHERE"))
		{
			/* (a test, HALO_H3_SKY_ATMOSPHERE: Halo 3's atmosphere seen to its
			thickness - the sky's inscatter, at the elevation of the row,
			around the compass - over the dome's albedo colour: Valhalla's
			zenith came out a dark violet where MCC's is blue; its dome's
			self-illumination map and the scattering's sky constants are not
			read right yet, so the made gradient stays the default) */
			sky_atmosphere_color(build, t < 0.8f ? (1.0f - t / 0.8f) * 1.5707963f : 0.0f, c);
			if (t > 0.8f)
				for (k = 0; k < 3; k++)
					c[k] *= 1.0f - 0.25f * (t - 0.8f) / 0.2f;
		}
		else for (k = 0; k < 3; k++)
		{
			if (b->generated_kind == 2)
				c[k] = 0.14f + 0.03f * k;
			else if (t < 0.8f)
			{
				float a = t / 0.8f;

				a = a * a;
				c[k] = zenith[k] + (horizon[k] - zenith[k]) * a;
			}
			else
				c[k] = horizon[k] + (below[k] - horizon[k]) * (t - 0.8f) / 0.2f;
		}
		for (x = 0; x < b->width; x++)
		{
			for (k = 0; k < 3; k++)
				rgba[4 * (y * b->width + x) + k] = (unsigned char)(c[k] * 255.0f + 0.5f);
			rgba[4 * (y * b->width + x) + 3] = 255;
		}
	}
	if (image_encode(rgba, b->width, b->height, &made, &made_size, &format, &mipmaps))
	{
		b->format = format;
		b->mipmaps = mipmaps;
		stream_pad(build->stream, 0x80);
		b->file_offset = stream_offset(build->stream);
		b->file_size = made_size;
		stream_put(build->stream, made, made_size);
		b->written = TRUE;
		build->result->bitmap_bytes += made_size;
	}
	free(made);
	free(rgba);
}

/* a kept map's texel (wrapped) at u, v (0..1 a repeat), RGBA 0..1, its
colours as light (the drawn colour squared) */
H3_ALIGN static void kept_sample(struct bitmap const *b, float u, float v, float out[4])
{
	long w = b->made_width, h = b->made_height, x, y;
	unsigned char const *t;

	u -= floorf(u);
	v -= floorf(v);
	x = (long)(u * w) % w;
	y = (long)(v * h) % h;
	t = b->kept + 4 * (y * w + x);
	out[0] = t[0] / 255.0f * (t[0] / 255.0f);
	out[1] = t[1] / 255.0f * (t[1] / 255.0f);
	out[2] = t[2] / 255.0f * (t[2] / 255.0f);
	out[3] = t[3] / 255.0f;
}

/* a render method's albedo made one map (bitmap_product): at the base's
side, or more for its details' repeats (1024 at most), each texel the
albedo's rule of its maps' texels there, drawn (its square root) */
H3_ALIGN static void bitmap_product_write(struct build *build, struct bitmap *b)
{
	struct bitmap const *base = &build->bitmaps[b->sources[0]], *detail = b->sources[1] != NONE ? &build->bitmaps[b->sources[1]] : NULL;
	struct bitmap const *detail2 = b->sources[2] != NONE ? &build->bitmaps[b->sources[2]] : NULL;
	unsigned long w = base->made_width, h = base->made_height, x, y;
	unsigned char *rgba, *made = NULL;
	unsigned long made_size;
	unsigned short format, mipmaps;
	float m = detail_multiplier_drawn() * detail_multiplier_drawn();

	if (!base->kept || (detail && !detail->kept) || (detail2 && !detail2->kept) || !w || !h)
	{
		platform_log("h3 level: %s: its maps were not kept: not made", b->path);
		return;
	}
	while (detail && w < 1024 && w < detail->made_width * b->source_scales[0])
		w *= 2;
	while (detail && h < 1024 && h < detail->made_height * b->source_scales[1])
		h *= 2;
	rgba = malloc(w * h * 4);
	if (!rgba)
		return;
	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			float u = (x + 0.5f) / w, v = (y + 0.5f) / h, c[4], d[4] = { 1.0f, 1.0f, 1.0f, 1.0f }, d2[4] = { 1.0f, 1.0f, 1.0f, 1.0f }, o[4];
			int k;

			kept_sample(base, u, v, c);
			if (detail)
				kept_sample(detail, u * b->source_scales[0], v * b->source_scales[1], d);
			if (detail2)
				kept_sample(detail2, u * b->source_scales[2], v * b->source_scales[3], d2);
			switch (b->product_option)
			{
			case 100:
			case 101:
				/* (self_illumination.fx: the illumination map times its detail
				times the detail multiplier, times its colour and intensity; added) */
				for (k = 0; k < 3; k++)
					o[k] = c[k] * d[k] * (b->product_option == 100 ? m : 1.0f) * b->product_tint[k];
				o[3] = 1.0f;
				break;
			case 1:
				for (k = 0; k < 4; k++)
					o[k] = (1.0f - c[3]) * d[k] + c[3] * d2[k];
				for (k = 0; k < 3; k++)
					o[k] *= c[k] * detail_multiplier_drawn() * detail_multiplier_drawn();
				break;
			case 7:
			case 9:
				for (k = 0; k < 3; k++)
					o[k] = c[k] * d[k] * d2[k] * m * m;
				if (b->product_option == 7)
					o[3] = c[3] * d[3] * d2[3];
				else
				{
					/* (apply_black_point: the base's alpha the black point) */
					float black = c[3], alpha = d[3] * d2[3], middle = (black + 1.0f) / 2.0f, t;

					t = middle - black > 1e-4f ? (alpha - black) / (middle - black) : 1.0f;
					t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
					o[3] = middle * t + (alpha - middle > 0.0f ? (alpha - middle < 1.0f ? alpha - middle : 1.0f) : 0.0f);
				}
				break;
			default:
				for (k = 0; k < 3; k++)
					o[k] = c[k] * d[k] * m;
				o[3] = c[3] * d[3];
				break;
			}
			for (k = 0; k < 3; k++)
			{
				float drawn = sqrtf(o[k] > 0.0f ? o[k] : 0.0f);

				rgba[4 * (y * w + x) + k] = (unsigned char)(drawn > 1.0f ? 255.0f : drawn * 255.0f + 0.5f);
			}
			rgba[4 * (y * w + x) + 3] = (unsigned char)(o[3] < 0.0f ? 0.0f : o[3] > 1.0f ? 255.0f : o[3] * 255.0f + 0.5f);
		}
	}
	if (b->hazed)
	{
		/* (held for its haze: sky_haze_write) */
		b->made_width = (unsigned short)w;
		b->made_height = (unsigned short)h;
		b->held_rgba = rgba;
		return;
	}
	if (image_encode(rgba, w, h, &made, &made_size, &format, &mipmaps))
	{
		b->width = b->made_width = (unsigned short)w;
		b->height = b->made_height = (unsigned short)h;
		b->format = format;
		b->mipmaps = mipmaps;
		stream_pad(build->stream, 0x80);
		b->file_offset = stream_offset(build->stream);
		b->file_size = made_size;
		stream_put(build->stream, made, made_size);
		b->written = TRUE;
		build->result->bitmap_bytes += made_size;
	}
	free(made);
	free(rgba);
}

/* a terrain's base map made: each texel its materials' mean colours
weighed by its blend map (times its overlay over the overlay's mean, when
the materials share one), its alpha the second most used material's share
of the two most used (this engine's blended environment shader: alpha 0 the
primary detail, 1 the secondary - HALO_H3_TERRAIN_SWAP the other way) */
H3_ALIGN static void bitmap_composite_write(struct build *build, long index)
{
	struct bitmap *b = &build->bitmaps[index];
	struct bitmap *blend = &build->bitmaps[b->sources[1]];
	struct bitmap *overlay = b->sources[0] != NONE ? &build->bitmaps[b->sources[0]] : NULL;
	unsigned long w = blend->made_width, h = blend->made_height, x, y;
	unsigned char *rgba;
	unsigned char *made = NULL;
	unsigned long made_size;
	unsigned short format, mipmaps;
	double use[4] = { 0, 0, 0, 0 };
	long first = NONE, second = NONE, i;
	float means[4][3];
	struct shader *shader = &build->shaders[b->shader];
	boolean swap = getenv("HALO_H3_TERRAIN_SWAP") != NULL;
	float detail_multiplier = detail_multiplier_drawn();
	float sampled_tiling = env_float("HALO_H3_TERRAIN_SAMPLED", 8.0f);

	unsigned char *stand_in = NULL;
	unsigned char const *kept = blend->kept;

	if (!kept || !w || !h)
	{
		/* (a blend map not read - Sandbox's rubble's, R5G6B5, is never
		asked for: its first material alone, 4 x 4, a stand-in) */
		platform_log("h3 level: %s: its blend map (format %d) was not read: its first material alone (STAND-IN)", b->path,
			(int)blend->format);
		w = h = 4;
		stand_in = calloc(w * h, 4);
		for (i = 0; stand_in && i < (long)(w * h); i++)
			stand_in[4 * i] = 255;
		kept = stand_in;
	}
	if (!kept || !(rgba = malloc(w * h * 4)))
	{
		free(stand_in);
		platform_log("h3 level: %s: no memory for its blend map", b->path);
		return;
	}
	/* (its size the blend map's as it was made, not as it was when the
	composite was named - Sandbox's rubble's blend map was made later: the
	composite's 0 x 31925 was cut by the map checker) */
	b->width = b->made_width = (unsigned short)w;
	b->height = b->made_height = (unsigned short)h;
	for (i = 0; i < 4; i++)
	{
		long source = b->sources[2 + i];
		int k;

		for (k = 0; k < 3; k++)
		{
			long detail = b->detail_sources[i];

			means[i][k] = source != NONE ? build->bitmaps[source].mean[3] : 0.5f;
			/* (Halo 3's albedo the base times the detail times 4.59: the base's
			lightness - this engine's detail its colours over it - times its
			detail's mean colour times 4.59) */
			if (detail != NONE && build->bitmaps[detail].written)
				means[i][k] *= build->bitmaps[detail].mean[k] * detail_multiplier;
		}
		if (getenv("HALO_H3_DEBUG"))
			platform_log("h3 level: terrain %s: material %ld source %ld scale %.2f kept %d mean %.2f %.2f %.2f", b->path, i, source,
				b->source_scales[i], source != NONE && build->bitmaps[source].kept ? 1 : 0, means[i][0], means[i][1], means[i][2]);
	}
	for (i = 0; i < (long)(w * h); i++)
	{
		int k;

		for (k = 0; k < 4; k++)
			use[k] += b->sources[2 + k] != NONE ? kept[4 * i + k] : 0;
	}
	for (i = 0; i < 4; i++)
	{
		if (b->sources[2 + i] == NONE)
			continue;
		if (first == NONE || use[i] > use[first])
			second = first, first = i;
		else if (second == NONE || use[i] > use[second])
			second = i;
	}
	if (first == NONE)
	{
		platform_log("h3 level: %s: no material of its blend%s", b->path, h3_odst ? " (a flat grey STAND-IN)" : "");
		if (!h3_odst)
		{
			free(rgba);
			free(stand_in);
			return;
		}
		/* (ODST: a terrain shader none of whose materials' maps is read does
		not stop the level - a flat mid grey, no detail) */
		for (i = 0; i < (long)(w * h); i++)
		{
			rgba[4 * i] = rgba[4 * i + 1] = rgba[4 * i + 2] = 128;
			rgba[4 * i + 3] = 0;
		}
		goto encode;
	}
	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			unsigned char const *weights = kept + 4 * (y * w + x);
			float total = 0.0f, c[3] = { 0, 0, 0 }, o[3] = { 1.0f, 1.0f, 1.0f }, a;
			int k;

			for (i = 0; i < 4; i++)
			{
				struct bitmap const *material;
				float scale;

				if (b->sources[2 + i] == NONE)
					continue;
				total += weights[i];
				material = &build->bitmaps[b->sources[2 + i]];
				scale = b->source_scales[i];
				/* (a material's map that tiles little, its own texels here - the
				distant mountains' whole look; one that tiles a lot, its mean, its
				look left to the detail pass) */
				if (material->kept && scale <= sampled_tiling)
				{
					unsigned long mx = (unsigned long)((float)x / (float)w * scale * material->made_width) % material->made_width;
					unsigned long my = (unsigned long)((float)y / (float)h * scale * material->made_height) % material->made_height;
					unsigned char const *t = material->kept + 4 * (my * material->made_width + mx);

					for (k = 0; k < 3; k++)
						c[k] += weights[i] * means[i][k] / (material->mean[3] > 0.01f ? material->mean[3] : 1.0f) * (t[k] / 255.0f);
				}
				else
				{
					for (k = 0; k < 3; k++)
						c[k] += weights[i] * means[i][k];
				}
			}
			if (total < 1.0f)
			{
				total = 1.0f;
				for (k = 0; k < 3; k++)
					c[k] = means[first][k];
			}
			if (overlay && overlay->kept && overlay->mean[3] > 0.01f)
			{
				unsigned long ox = x * overlay->made_width / w, oy = y * overlay->made_height / h;
				unsigned char const *t = overlay->kept + 4 * (oy * overlay->made_width + ox);

				/* (its overlay is the materials' base map - Halo 3's albedo the
				base times the detail times 4.59, the detail drawn as this
				engine's detail, its mean lightness here: the overlay's own
				colour, times 4.59) */
				for (k = 0; k < 3; k++)
					o[k] = t[k] / 255.0f * detail_multiplier;
			}
			for (k = 0; k < 3; k++)
			{
				float v = c[k] / total * o[k] * 255.0f;

				rgba[4 * (y * w + x) + k] = (unsigned char)(v > 255.0f ? 255.0f : v);
			}
			a = second == NONE ? 0.0f : (float)weights[second] / (float)(weights[first] + weights[second] + 1);
			if (swap)
				a = 1.0f - a;
			rgba[4 * (y * w + x) + 3] = (unsigned char)(a * 255.0f + 0.5f);
		}
	}
encode:
	/* (DXT5 always: its alpha the blend) */
	rgba[3] = rgba[3] == 255 ? 254 : rgba[3];
	if (image_encode(rgba, w, h, &made, &made_size, &format, &mipmaps))
	{
		b->width = (unsigned short)w;
		b->height = (unsigned short)h;
		b->format = format;
		b->mipmaps = mipmaps;
		stream_pad(build->stream, 0x80);
		b->file_offset = stream_offset(build->stream);
		b->file_size = made_size;
		stream_put(build->stream, made, made_size);
		b->written = TRUE;
		build->result->bitmap_bytes += made_size;
		if (first == NONE)
			goto encoded;
		shader->detail = b->sources[2 + first];
		if (shader->terrain_bumps[first] != NONE)
		{
			shader->bump = shader->terrain_bumps[first];
			shader->bump_scale[0] = shader->terrain_bump_scales[first][0];
			shader->bump_scale[1] = shader->terrain_bump_scales[first][1];
		}
		shader->detail_scale = b->source_scales[first];
		shader->detail2 = second != NONE ? b->sources[2 + second] : NONE;
		shader->detail2_scale = second != NONE ? b->source_scales[second] : 1.0f;
		if (getenv("HALO_H3_DEBUG"))
			fprintf(stderr, "h3 level: terrain %s: materials %ld (%.0f%%) and %ld, details %.1f and %.1f\n", b->path, first,
				100.0 * use[first] / (use[0] + use[1] + use[2] + use[3] + 1), second, shader->detail_scale, shader->detail2_scale);
	}
encoded:
	free(made);
	free(rgba);
	free(stand_in);
}

/* the context of a page request: what it is for */
enum
{
	REQUEST_GEOMETRY = 1,
	REQUEST_SKY_GEOMETRY,
	REQUEST_COLLISION,
	REQUEST_DEFINITION_COLLISION,
	REQUEST_BITMAP,
	REQUEST_LIGHTPROBE,
	REQUEST_INTENSITY,
	REQUEST_VERTEX_LIGHT,
	REQUEST_GEAR_GEOMETRY,
	REQUEST_GEAR_ANIMATION,
	REQUEST_PROP_GEOMETRY,
	REQUEST_DECORATOR_GEOMETRY,
	REQUEST_DECORATOR_BUFFER,
};

struct request_context
{
	struct build *build;
	long kind;
	long index;
};

H3_ALIGN static void request_done(void *context, struct h3_page_request *r)
{
	struct request_context const *c = context;
	struct build *build = c->build;

	HEAP_SAMPLE();
	switch (c->kind)
	{
		case REQUEST_GEOMETRY:
			geometry_got(&build->bsp_geometry, r);
			break;
		case REQUEST_SKY_GEOMETRY:
			geometry_got(&build->sky_geometry, r);
			break;
		case REQUEST_GEAR_GEOMETRY:
			geometry_got(&build->gear.geometry, r);
			break;
		case REQUEST_PROP_GEOMETRY:
			geometry_got(&build->props[c->index].geometry, r);
			break;
		case REQUEST_DECORATOR_GEOMETRY:
			geometry_got(&build->decorators[c->index].geometry, r);
			break;
		case REQUEST_DECORATOR_BUFFER:
			geometry_got(&build->decorator_buffers, r);
			break;
		case REQUEST_GEAR_ANIMATION:
			free(build->gear.animation);
			build->gear.animation = r->out;
			build->gear.animation_size = r->size;
			r->out = NULL;
			break;
		case REQUEST_COLLISION:
		case REQUEST_DEFINITION_COLLISION:
		{
			struct collision_bsp *bsp = c->kind == REQUEST_COLLISION ? &build->collision : &build->definition_collision[c->index];

			if (r->tag >= 1000 && r->tag < 1008)
				bsp->arrays[r->tag - 1000] = r->out;
			else
				free(r->out);
			r->out = NULL;
			break;
		}
		case REQUEST_BITMAP:
			bitmap_got(build, &build->bitmaps[c->index], r->out);
			free(r->out);
			r->out = NULL;
			break;
		case REQUEST_LIGHTPROBE:
		case REQUEST_INTENSITY:
		{
			unsigned char **target = c->kind == REQUEST_LIGHTPROBE ? &build->lightprobe_slices : &build->intensity_slices;

			*target = r->out;
			r->out = NULL;
			break;
		}
		case REQUEST_VERTEX_LIGHT:
		{
			/* (five words a vertex, each R G B E - the sun's warm colour
			came out blue read as B G R - : the dominant light's
			intensity, then the four coefficients - Halo 3's static_per_vertex
			vertex shader: each byte made signed, x 2 - 1, the colour times
			2 to the 31.75 E) */
			struct vertex_buffer *v = &build->bsp_geometry.vertex_buffers[c->index];
			double sum[3] = { 0.0, 0.0, 0.0 };
			long n, count = (long)(r->size / 20);

			free(v->sh);
			v->sh = malloc((unsigned long)count * 15 * sizeof(float));
			for (n = 0; n < count; n++)
			{
				double magnitude = r->out[20 * n + 3] / 255.0;
				int w;

				sum[0] += r->out[20 * n + 2] * magnitude;
				sum[1] += r->out[20 * n + 1] * magnitude;
				sum[2] += r->out[20 * n] * magnitude;
				for (w = 0; v->sh && w < 5; w++)
				{
					unsigned char const *word = r->out + 20 * n + 4 * w;
					float scale = exp2f((word[3] / 255.0f * 2.0f - 1.0f) * 31.75f);

					v->sh[15 * n + 3 * w] = (word[0] / 255.0f * 2.0f - 1.0f) * scale;
					v->sh[15 * n + 3 * w + 1] = (word[1] / 255.0f * 2.0f - 1.0f) * scale;
					v->sh[15 * n + 3 * w + 2] = (word[2] / 255.0f * 2.0f - 1.0f) * scale;
				}
			}
			if (count)
			{
				int k;

				for (k = 0; k < 3; k++)
					v->light[k] = (float)(sum[k] / count / 255.0);
				v->light_done = TRUE;
			}
			free(r->out);
			r->out = NULL;
			break;
		}
		default:
			free(r->out);
			r->out = NULL;
			break;
	}
}

/* ---------- private code: gathering */

/* a request added (grown as need be); NULL if out of memory */
H3_ALIGN static struct h3_page_request *request_add(struct h3_page_request **requests, long *count, long *capacity)
{
	struct h3_page_request *r;

	if (*count >= *capacity)
	{
		long grown_capacity = *capacity ? *capacity * 2 : 256;
		struct h3_page_request *grown = realloc(*requests, grown_capacity * sizeof(**requests));

		if (!grown)
			return NULL;
		*requests = grown;
		*capacity = grown_capacity;
	}
	r = &(*requests)[(*count)++];
	memset(r, 0, sizeof(*r));
	return r;
}

H3_ALIGN static boolean model_make(struct build *build, struct section const *s, float scale, struct model_made *model,
	float uv_scale);
H3_ALIGN static void model_free(struct model_made *model);

/* a key's collision BSP: its prop's, or its definition's; NULL if it has
none (no nodes or no surfaces) */
H3_ALIGN static struct collision_bsp const *key_collision(struct build const *build, struct object_key const *key)
{
	struct collision_bsp const *bsp = NULL;

	if (key->prop != NONE)
		bsp = &build->props[key->prop].collision;
	else if (key->definition >= 0 && key->definition < build->definition_count)
		bsp = &build->definition_collision[key->definition];
	return bsp && bsp->counts[0] > 0 && bsp->counts[5] > 0 ? bsp : NULL;
}

/* a scenario object kept as a prop: not a game type's marker (a spawn
point, a goal, a hill, a zone: Halo 3 draws them in Forge only), nor a
Forge stand-in of a triangle or two */
H3_ALIGN static boolean prop_wanted(struct build *build, long object_tag, long model)
{
	static char const *const markers[] = { "spawn", "_zone", "_area", "_static", "destination" };
	char const *name = tag_short_name(h3_cache_tag_name(build->cache, object_tag));
	struct h3_cache_tag t;
	long mesh_count, m, indices = 0;
	unsigned long meshes;
	unsigned k;

	for (k = 0; k < sizeof(markers) / sizeof(markers[0]); k++)
	{
		if (strstr(name, markers[k]))
			return FALSE;
	}
	if (!h3_cache_tag_get(build->cache, model, &t) ||
		!h3_cache_block(build->cache, t.address + H3_MODE_MESHES, H3_MESH_SIZE, &mesh_count, &meshes))
	{
		return FALSE;
	}
	for (m = 0; m < mesh_count; m++)
	{
		long part_count, n;
		unsigned long parts;

		if (!h3_cache_block(build->cache, meshes + H3_MESH_SIZE * m, H3_PART_SIZE, &part_count, &parts))
			continue;
		for (n = 0; n < part_count; n++)
			indices += (unsigned short)h3_cache_read16(build->cache, parts + H3_PART_SIZE * n + 6);
	}
	return indices >= 12;
}

/* a prop's collision: of each region's first permutation's BSPs, the one
with the most surfaces whose node (the render model's, at its default pose)
is not turned, moved by that node's place into the model's space - its
planes' and its 2D nodes' distances (each 2D tree projected as its
reference's plane projects it: collision_bsp.c, global_projection3d_mappings)
and its vertices; the others left out */
H3_ALIGN static void prop_collision(struct build *build, struct prop *prop, long collision_tag, long model)
{
	static short const mappings[3][2][2] = { { { 2, 1 }, { 1, 2 } }, { { 0, 2 }, { 2, 0 } }, { { 1, 0 }, { 0, 1 } } };
	struct h3_cache *cache = build->cache;
	struct h3_cache_tag c, m;
	long region_count, r, node_count = 0, best_surfaces = 0, considered = 0;
	unsigned long regions, nodes = 0, best = 0;
	float best_offset[3] = { 0.0f, 0.0f, 0.0f };
	int k;

	if (!h3_cache_tag_get(cache, collision_tag, &c) || !h3_cache_block(cache, c.address + 0x20, 0x10, &region_count, &regions))
		return;
	if (h3_cache_tag_get(cache, model, &m))
		h3_cache_block(cache, m.address + H3_MODE_NODES, 0x60, &node_count, &nodes);
	for (r = 0; r < region_count; r++)
	{
		long permutation_count, bsp_count, b;
		unsigned long permutations, bsps;

		if (!h3_cache_block(cache, regions + 0x10 * r + 4, 0x28, &permutation_count, &permutations) || permutation_count < 1 ||
			!h3_cache_block(cache, permutations + 4, 0x64, &bsp_count, &bsps))
		{
			continue;
		}
		for (b = 0; b < bsp_count; b++)
		{
			unsigned long base = bsps + 0x64 * b;
			short node = h3_cache_read16(cache, base);
			long surfaces = (long)h3_cache_read32(cache, base + 4 + 12 * 5);
			float offset[3] = { 0.0f, 0.0f, 0.0f };
			boolean turned = FALSE;

			considered++;
			if (node >= 0 && node < node_count)
			{
				unsigned char const *n = h3_cache_at(cache, nodes + 0x60 * node, 0x60);
				float f[3], l[3], u[3], q[3];

				if (!n)
					continue;
				for (k = 0; k < 3; k++)
				{
					f[k] = readf(n + 0x2C + 4 * k);
					l[k] = readf(n + 0x38 + 4 * k);
					u[k] = readf(n + 0x44 + 4 * k);
					q[k] = -readf(n + 0x50 + 4 * k);
				}
				turned = fabsf(f[0] - 1.0f) > 0.001f || fabsf(l[1] - 1.0f) > 0.001f || fabsf(u[2] - 1.0f) > 0.001f;
				offset[0] = f[0] * q[0] + f[1] * q[1] + f[2] * q[2];
				offset[1] = l[0] * q[0] + l[1] * q[1] + l[2] * q[2];
				offset[2] = u[0] * q[0] + u[1] * q[1] + u[2] * q[2];
			}
			if (turned || surfaces <= best_surfaces)
				continue;
			best_surfaces = surfaces;
			best = base;
			memcpy(best_offset, offset, sizeof(best_offset));
		}
	}
	prop->collision_bsps_left_out = considered - (best ? 1 : 0);
	if (!best)
		return;
	for (k = 0; k < 8; k++)
	{
		long count;
		unsigned long elements;
		void const *data;

		if (!h3_cache_block(cache, best + 4 + 12 * k, h3_collision_sizes[k], &count, &elements))
			count = 0;
		data = count ? h3_cache_at(cache, elements, (unsigned long)count * h3_collision_sizes[k]) : NULL;
		if (count && !data)
		{
			collision_free(&prop->collision);
			return;
		}
		prop->collision.counts[k] = count;
		prop->collision.arrays[k] = count ? malloc((unsigned long)count * h3_collision_sizes[k]) : NULL;
		if (count && !prop->collision.arrays[k])
		{
			collision_free(&prop->collision);
			return;
		}
		if (count)
			memcpy(prop->collision.arrays[k], data, (unsigned long)count * h3_collision_sizes[k]);
	}
	if (best_offset[0] != 0.0f || best_offset[1] != 0.0f || best_offset[2] != 0.0f)
	{
		struct collision_bsp *bsp = &prop->collision;
		unsigned char *done = calloc(bsp->counts[4] + 1, 1);
		long i;

		for (i = 0; i < bsp->counts[1]; i++)
		{
			unsigned char *plane = bsp->arrays[1] + 16 * i;

			writef(plane + 12, readf(plane + 12) + readf(plane) * best_offset[0] + readf(plane + 4) * best_offset[1] +
				readf(plane + 8) * best_offset[2]);
		}
		for (i = 0; i < bsp->counts[7]; i++)
		{
			unsigned char *vertex = bsp->arrays[7] + 16 * i;

			for (k = 0; k < 3; k++)
				writef(vertex + 4 * k, readf(vertex + 4 * k) + best_offset[k]);
		}
		/* (each 2D reference's tree, its nodes once: a stack of them) */
		for (i = 0; i < bsp->counts[3] && done; i++)
		{
			unsigned char const *reference = bsp->arrays[3] + 4 * i;
			unsigned short plane_designator = (unsigned short)read16(reference);
			unsigned short root = (unsigned short)read16(reference + 2);
			unsigned char const *plane;
			float n[3], shift[2];
			int axis, sign;
			unsigned short stack[256];
			int depth = 0;

			if ((plane_designator & 0x7FFF) >= bsp->counts[1] || (root & 0x8000))
				continue;
			plane = bsp->arrays[1] + 16 * (plane_designator & 0x7FFF);
			for (k = 0; k < 3; k++)
				n[k] = readf(plane + 4 * k);
			axis = fabsf(n[2]) >= fabsf(n[1]) && fabsf(n[2]) >= fabsf(n[0]) ? 2 : fabsf(n[1]) >= fabsf(n[0]) ? 1 : 0;
			sign = (n[axis] > 0.0f) != ((plane_designator & 0x8000) != 0);
			shift[0] = best_offset[mappings[axis][sign][0]];
			shift[1] = best_offset[mappings[axis][sign][1]];
			stack[depth++] = root;
			while (depth > 0)
			{
				unsigned short node = stack[--depth];
				unsigned char *n2;
				unsigned short children[2];
				int c2;

				if ((node & 0x8000) || node >= bsp->counts[4] || done[node])
					continue;
				done[node] = 1;
				n2 = bsp->arrays[4] + 16 * node;
				writef(n2 + 8, readf(n2 + 8) + readf(n2) * shift[0] + readf(n2 + 4) * shift[1]);
				children[0] = (unsigned short)read16(n2 + 12);
				children[1] = (unsigned short)read16(n2 + 14);
				for (c2 = 0; c2 < 2; c2++)
				{
					if (!(children[c2] & 0x8000) && depth < 256)
						stack[depth++] = children[c2];
				}
			}
		}
		free(done);
	}
}

/* the scenario's static objects found (scenery at 0xB4, palette 0xC0, 0xB4
each; crates at 0x5BC, palette 0x5C8, 0xB0 each; machines and controls as
gear_find reads them, 0x88 and 0x78 each, but the gear): a placement's
palette at 0, flags at 4 (never placed: 3; not automatically: 0), position
at 8, yaw, pitch and roll at 0x14, scale at 0x20; the objects' render
models' shaders asked for and their collision read */
H3_ALIGN static void props_find(struct build *build)
{
	static struct { unsigned long placements, palette, size; } const blocks[] = {
		{ H3_SCNR_SCENERY, H3_SCNR_SCENERY_PALETTE, 0xB4 },
		{ 0x5BC, 0x5C8, 0xB0 },
		{ H3_SCNR_MACHINES, H3_SCNR_MACHINE_PALETTE, 0x88 },
		{ 0x168, 0x174, 0x78 },
		/* (Halo 3's equipment - the bubble shield, regenerator, power drainer,
		trip mine, grav lift, radar jammer, flare, deployable cover: objects of
		objects\equipment\ -: drawn where the map places it, Halo 3's own model,
		nothing to bump into. This engine has no equipment to carry and deploy:
		it is a thing to see, not yet to use. Before, the placement was left
		out. HALO_H3_EQUIPMENT=0: as before) */
		{ H3_SCNR_EQUIPMENT, H3_SCNR_EQUIPMENT_PALETTE, 0x8C },
	};
	struct h3_cache *cache = build->cache;
	unsigned b;
	long i, total = 0, equipment = 0;

	build->props = NULL;
	build->prop_count = 0;
	if (getenv("HALO_H3_NO_PROPS"))
		return;
	for (b = 0; b < sizeof(blocks) / sizeof(blocks[0]); b++)
	{
		long count, palette_count;
		unsigned long placements, palette;

		if (!h3_cache_block(cache, build->scenario + H3_SCNR(blocks[b].placements), blocks[b].size, &count, &placements) ||
			!h3_cache_block(cache, build->scenario + H3_SCNR(blocks[b].palette), 0x10, &palette_count, &palette))
		{
			continue;
		}
		for (i = 0; i < count; i++)
		{
			unsigned char e[0x24];
			short index;
			long object, model, p;
			struct h3_cache_tag t, h;
			struct prop *prop = NULL;
			struct prop_placement *placement;

			if (!h3_cache_read(cache, placements + blocks[b].size * i, 0x24, e))
				continue;
			index = read16(e);
			if (index < 0 || index >= palette_count || (read32(e + 4) & 0x9))
				continue;
			object = h3_cache_reference(cache, palette + 0x10 * index);
			if (!h3_cache_tag_get(cache, object, &t) || !h3_cache_tag_get(cache, h3_cache_reference(cache, t.address + 0x34), &h))
				continue;
			if (blocks[b].placements == H3_SCNR_EQUIPMENT)
			{
				char const *path = h3_cache_tag_name(cache, object);

				/* (not the grenades and powerups: the netgame items') */
				if (!path || !strstr(path, "objects\\equipment\\") || (getenv("HALO_H3_EQUIPMENT") && !atoi(getenv("HALO_H3_EQUIPMENT"))))
					continue;
				equipment++;
			}
			/* (the gear: gear_find's) */
			if (blocks[b].placements == H3_SCNR_MACHINES && build->gear.found && h3_cache_read16(cache, t.address + 0x190) == 2)
				continue;
			model = h3_cache_reference(cache, h.address);
			for (p = 0; p < build->prop_count; p++)
			{
				if (build->props[p].object_tag == object)
					prop = &build->props[p];
			}
			if (!prop)
			{
				struct prop *grown;

				if (!prop_wanted(build, object, model))
					continue;
				grown = realloc(build->props, (build->prop_count + 1) * sizeof(*grown));
				if (!grown)
					continue;
				build->props = grown;
				prop = &build->props[build->prop_count++];
				memset(prop, 0, sizeof(*prop));
				prop->object_tag = object;
				snprintf(prop->name, sizeof(prop->name), "%s", tag_short_name(h3_cache_tag_name(cache, object)));
				if (blocks[b].placements != H3_SCNR_EQUIPMENT)
					prop_collision(build, prop, h3_cache_reference(cache, h.address + 0x10), model);
				{
					long material_count, k;
					unsigned long materials;
					struct h3_cache_tag r;

					if (h3_cache_tag_get(cache, model, &r) &&
						h3_cache_block(cache, r.address + H3_MODE_MATERIALS, 0x24, &material_count, &materials))
					{
						for (k = 0; k < material_count; k++)
							shader_index(build, h3_cache_reference(cache, materials + 0x24 * k));
					}
				}
			}
			placement = realloc(prop->placements, (prop->placement_count + 1) * sizeof(*placement));
			if (!placement)
				continue;
			prop->placements = placement;
			placement = &prop->placements[prop->placement_count++];
			memcpy(placement->position, e + 8, 12);
			memcpy(placement->angles, e + 0x14, 12);
			placement->scale = readf(e + 0x20);
			if (!(placement->scale > 0.01f && placement->scale < 100.0f))
				placement->scale = 1.0f;
			total++;
		}
	}
	platform_log("h3 level: props: %ld placements of %ld scenario objects (%ld of them Halo 3 equipment, drawn where placed)", total,
		build->prop_count, equipment);
	for (i = 0; i < build->prop_count && getenv("HALO_H3_DEBUG"); i++)
	{
		fprintf(stderr, "h3 level: prop %s: %ld placed, collision %ld surfaces (%ld BSPs left out)\n", build->props[i].name,
			build->props[i].placement_count, build->props[i].collision.counts[5], build->props[i].collision_bsps_left_out);
	}
}

/* a prop's model at a scale (its meshes', its materials' shaders), its
key's bounding centre and radius about it */
H3_ALIGN static boolean prop_model(struct build *build, struct prop *prop, float scale, struct model_made *model,
	struct object_key *key)
{
	struct h3_cache *cache = build->cache;
	struct h3_cache_tag t;
	long material_count = 0, i, mo = NONE;
	unsigned long materials = 0;
	short *shaders, *saved_shaders;
	long saved_count;
	float low[3] = { 1e30f, 1e30f, 1e30f }, high[3] = { -1e30f, -1e30f, -1e30f };
	float uv_scale = 0.0f;
	int k;

	memset(model, 0, sizeof(*model));
	if (!prop->geometry_open)
		return FALSE;
	if (h3_cache_tag_get(cache, prop->object_tag, &t) && h3_cache_tag_get(cache, h3_cache_reference(cache, t.address + 0x34), &t))
		mo = h3_cache_reference(cache, t.address);
	if (!h3_cache_tag_get(cache, mo, &t) ||
		!h3_cache_block(cache, t.address + H3_MODE_MATERIALS, 0x24, &material_count, &materials))
	{
		return FALSE;
	}
	shaders = malloc((material_count + 1) * sizeof(short));
	if (!shaders)
		return FALSE;
	for (i = 0; i < material_count; i++)
		shaders[i] = shader_for(build, h3_cache_reference(cache, materials + 0x24 * i));
	saved_shaders = build->material_shader;
	saved_count = build->material_count;
	build->material_shader = shaders;
	build->material_count = material_count;
	/* (the meshes' texture coordinate scale the model's one: the largest of
theirs, found by a first pass) */
	for (i = 0; i < prop->geometry.mesh_count; i++)
	{
		struct section s;
		struct model_made mesh;
		long before = build->result->model_vertices;

		if (!section_read(build, &prop->geometry, i, &s))
			continue;
		if (model_make(build, &s, scale, &mesh, 0.0f))
		{
			uv_scale = mesh.scale > uv_scale ? mesh.scale : uv_scale;
			model_free(&mesh);
		}
		build->result->model_vertices = before;
		section_free(&s);
	}
	for (i = 0; i < prop->geometry.mesh_count && uv_scale > 0.0f; i++)
	{
		struct section s;
		struct model_made mesh;
		long v;

		if (!section_read(build, &prop->geometry, i, &s))
			continue;
		for (v = 0; v < s.vertex_count; v++)
		{
			for (k = 0; k < 3; k++)
			{
				float x = s.positions[3 * v + k] * scale;

				low[k] = x < low[k] ? x : low[k];
				high[k] = x > high[k] ? x : high[k];
			}
		}
		if (model_make(build, &s, scale, &mesh, uv_scale))
		{
			struct model_part_made *grown = realloc(model->parts, (model->part_count + mesh.part_count) * sizeof(*grown));

			if (grown)
			{
				memcpy(grown + model->part_count, mesh.parts, mesh.part_count * sizeof(*grown));
				model->parts = grown;
				model->part_count += mesh.part_count;
				model->scale = mesh.scale > model->scale ? mesh.scale : model->scale;
				model->radius = mesh.radius > model->radius ? mesh.radius : model->radius;
				free(mesh.parts);
			}
			else
				model_free(&mesh);
		}
		section_free(&s);
	}
	build->material_shader = saved_shaders;
	build->material_count = saved_count;
	free(shaders);
	if (!model->part_count)
		return FALSE;
	for (k = 0; k < 3; k++)
		key->centre[k] = 0.5f * (low[k] + high[k]);
	key->radius = 0.5f * sqrtf((high[0] - low[0]) * (high[0] - low[0]) + (high[1] - low[1]) * (high[1] - low[1]) +
		(high[2] - low[2]) * (high[2] - low[2]));
	return TRUE;
}

/* ---------- private code: decorators */

/* The structure BSP's decorators (its clusters' decorator groups at 0xE8,
0x3C each: placement count at 0, set at 2, placement buffer at 3, its first
placement's byte at 4, position minimum at 8, a placement's step at 0x18; the sets
at 0x1CC, a decorator set each: its model at 0, texture at 0x20; the
placements in the decorator resource at 0x254: a vertex buffer a set, 16
bytes a placement - its position three 16-bit steps from the group's
minimum, its variant at 7, its orientation a quaternion of four bytes at 8
(x, y, z, w: (b - 128) / 127.5, normalized), its colour at 12, its scale at
15 (128 one)). The ferns and vines (the owner's choice: Halo 3's own models,
thinned) are kept as the level's geometry: each set's model (one mesh of
vertex type 15, unindexed strips, a variant a subpart) at each kept
placement, lit flat (a patch). Their triangles are HALO_H3_DECORATOR_TRIANGLES
(6000 by default: the tag cache - 22 MB for the tags and the BSP - had about 580 KB left) at most, shared by the sets; the placements thinned
evenly (one a cell of a grid whose side grows until they fit the share). */

static struct { char const *name; float share; } const decorator_wanted[] = {
	{ "broad_leaf_low", 0.40f },
	{ "salal", 0.25f },
	{ "creeper_vine", 0.25f },
	{ "vine_small_leaves", 0.10f },
};

H3_ALIGN static void decorators_find(struct build *build)
{
	struct h3_cache *cache = build->cache;
	long set_count, i;
	unsigned long sets;
	long total = getenv("HALO_H3_DECORATOR_TRIANGLES") ? atol(getenv("HALO_H3_DECORATOR_TRIANGLES")) : 6000;

	build->decorators = NULL;
	build->decorator_count = 0;
	if (total <= 0 || !h3_cache_block(cache, build->sbsp + H3_SBSP_DECORATOR_SETS, 0x10, &set_count, &sets))
		return;
	for (i = 0; i < set_count; i++)
	{
		long set = h3_cache_reference(cache, sets + 0x10 * i), model, texture;
		char const *name;
		struct h3_cache_tag t, mt;
		struct decorator_set *d, *grown;
		unsigned w;
		float share = 0.0f;

		if (!h3_cache_tag_get(cache, set, &t))
			continue;
		name = tag_short_name(h3_cache_tag_name(cache, set));
		for (w = 0; w < sizeof(decorator_wanted) / sizeof(decorator_wanted[0]); w++)
		{
			if (!strcmp(name, decorator_wanted[w].name))
				share = decorator_wanted[w].share;
		}
		/* (the grass: detail objects, grass_gather) */
		if ((!strcmp(name, "thick_grass_green") || !strcmp(name, "tall_grass")) && build->grass_set_count < 4 &&
			getenv("HALO_H3_GRASS") && atol(getenv("HALO_H3_GRASS")) > 0)
		{
			long texture_tag = h3_cache_reference(cache, t.address + 0x20);
			long bitmap = texture_tag != NONE ? bitmap_for(build, texture_tag) : NONE;

			if (bitmap != NONE)
			{
				struct grass_set *grass = &build->grass_sets[build->grass_set_count++];

				grass->set_index = i;
				grass->bitmap = bitmap;
				snprintf(grass->name, sizeof(grass->name), "%s", name);
				build->bitmaps[bitmap].needs_alpha = TRUE;
				build->bitmaps[bitmap].sprite = TRUE;
			}
			continue;
		}
		if (share <= 0.0f)
			continue;
		model = h3_cache_reference(cache, t.address);
		texture = h3_cache_reference(cache, t.address + 0x20);
		if (!h3_cache_tag_get(cache, model, &mt) || texture == NONE || build->shader_count >= MAXIMUM_SHADERS)
			continue;
		grown = realloc(build->decorators, (build->decorator_count + 1) * sizeof(*grown));
		if (!grown)
			continue;
		build->decorators = grown;
		d = &build->decorators[build->decorator_count];
		memset(d, 0, sizeof(*d));
		d->set_index = i;
		d->budget = (long)(total * share);
		snprintf(d->name, sizeof(d->name), "%s", name);
		if (!geometry_open(build, &d->geometry, h3_cache_read32(cache, mt.address + H3_MODE_RESOURCE),
			mt.address + H3_MODE_MESHES, mt.address + H3_MODE_COMPRESSION))
		{
			geometry_close(&d->geometry);
			continue;
		}
		d->geometry_open = TRUE;
		/* its shader: its texture, alpha tested */
		{
			struct shader *sh = &build->shaders[build->shader_count];

			memset(sh, 0, sizeof(*sh));
			sh->h3_tag = NONE;
			sh->bitmap = bitmap_for(build, texture);
			sh->detail = sh->detail2 = sh->ripple = sh->reflection = NONE;
			sh->base_scale[0] = sh->base_scale[1] = 1.0f;
			sh->detail_scale = sh->detail2_scale = 1.0f;
			sh->group = 'senv';
			sh->alpha_tested = TRUE;
			sh->rendered = sh->bitmap != NONE;
			snprintf(sh->path, sizeof(sh->path), "h3\\shaders\\decorator %s", name);
			if (!sh->rendered)
			{
				geometry_close(&d->geometry);
				continue;
			}
			build->bitmaps[sh->bitmap].needs_alpha = TRUE;
			d->shader = (short)build->shader_count++;
		}
		build->decorator_count++;
	}
	if (!build->decorator_count && !build->grass_set_count)
		return;
	/* the placements' buffers */
	build->decorator_buffers_open = geometry_open(build, &build->decorator_buffers, h3_cache_read32(cache, build->sbsp + H3_SBSP_DECORATOR_RESOURCE),
		build->sbsp + H3_SBSP_DECORATOR_MESHES, build->sbsp + H3_SBSP_DECORATOR_COMPRESSION);
}

/* the requests: each set's model (its meshes' buffers) and the placement
buffers its groups name */
H3_ALIGN static void decorators_request(struct build *build, struct h3_page_request **requests, long *count, long *capacity)
{
	struct h3_cache *cache = build->cache;
	struct geometry *g = &build->decorator_buffers;
	long i, j, cluster_count;
	unsigned long clusters;

	for (i = 0; i < build->decorator_count; i++)
		geometry_request(build, &build->decorators[i].geometry, requests, count, capacity);
	if (!build->decorator_buffers_open || !g->vertex_buffer_count)
		return;
	build->decorator_wanted_buffers = calloc(g->vertex_buffer_count + 1, 1);
	if (!build->decorator_wanted_buffers ||
		!h3_cache_block(cache, build->sbsp + H3_SBSP_CLUSTERS, H3_CLUSTER_SIZE, &cluster_count, &clusters))
	{
		return;
	}
	for (i = 0; i < cluster_count; i++)
	{
		long group_count;
		unsigned long groups;

		if (!h3_cache_block(cache, clusters + H3_CLUSTER_SIZE * i + 0xE8, 0x3C, &group_count, &groups))
			continue;
		for (j = 0; j < group_count; j++)
		{
			unsigned char const *e = h3_cache_at(cache, groups + 0x3C * j, 8);
			long k;

			if (!e)
				continue;
			for (k = 0; k < build->decorator_count; k++)
			{
				if ((signed char)e[2] == build->decorators[k].set_index && e[3] < g->vertex_buffer_count)
					build->decorator_wanted_buffers[e[3]] = 1;
			}
			for (k = 0; k < build->grass_set_count; k++)
			{
				if ((signed char)e[2] == build->grass_sets[k].set_index && e[3] < g->vertex_buffer_count)
					build->decorator_wanted_buffers[e[3]] = 1;
			}
		}
	}
	for (i = 0; i < g->vertex_buffer_count; i++)
	{
		struct h3_page_request *r;

		if (!build->decorator_wanted_buffers[i] || g->vertex_buffers[i].declaration < 0)
			continue;
		r = request_add(requests, count, capacity);
		if (!r)
			return;
		r->page = g->resource.page[0];
		r->offset = g->resource.offset[0] + g->vertex_buffers[i].offset;
		r->size = g->vertex_buffers[i].size;
		r->tag = i;
		r->context = g;
	}
}

/* the scenario's first gear machine (H3's machine type 2: the windmill):
its placement (machines at 0x138, 0x88 each: palette at 0, position at 8,
rotation at 0x14), its definition (mach: its model at 0x34, type at
0x190; its position's speed a second at 0x104, 0.04 Zanzibar's: a turn in
25 seconds), its model's render model (hlmt: at 0) and animation graph (at
0x20: animations at 0x50, 0x88 each, frame count at 0x10; its resource
group at 0xF8, the resource's datum at 4) - its materials' shaders made
now */
H3_ALIGN static void gear_find(struct build *build)
{
	struct h3_cache *cache = build->cache;
	long count, palette_count, i;
	unsigned long machines, palette;

	memset(&build->gear, 0, sizeof(build->gear));
	build->gear.model_index = NONE;
	if (getenv("HALO_H3_NO_GEARS") ||
		!h3_cache_block(cache, build->scenario + H3_SCNR(H3_SCNR_MACHINES), 0x88, &count, &machines) ||
		!h3_cache_block(cache, build->scenario + H3_SCNR(H3_SCNR_MACHINE_PALETTE), 0x10, &palette_count, &palette))
	{
		return;
	}
	for (i = 0; i < count && !build->gear.found; i++)
	{
		unsigned char m[0x20];
		short index;
		long definition, model, render_model, graph;
		struct h3_cache_tag t, h;

		if (!h3_cache_read(cache, machines + 0x88 * i, 0x20, m))
			continue;
		index = read16(m);
		if (index < 0 || index >= palette_count)
			continue;
		definition = h3_cache_reference(cache, palette + 0x10 * index);
		if (!h3_cache_tag_get(cache, definition, &t) || h3_cache_read16(cache, t.address + 0x190) != 2)
			continue;
		model = h3_cache_reference(cache, t.address + 0x34);
		if (!h3_cache_tag_get(cache, model, &h))
			continue;
		render_model = h3_cache_reference(cache, h.address);
		graph = h3_cache_reference(cache, h.address + 0x20);
		if (render_model == NONE)
			continue;
		build->gear.found = TRUE;
		build->gear.mode_tag = render_model;
		memcpy(build->gear.position, m + 8, 12);
		memcpy(build->gear.rotation, m + 0x14, 12);
		build->gear.velocity = h3_cache_readf(cache, t.address + 0x104);
		if (!(build->gear.velocity > 0.0001f && build->gear.velocity < 10.0f))
			build->gear.velocity = 0.04f;
		snprintf(build->gear.name, sizeof(build->gear.name), "%s", tag_short_name(h3_cache_tag_name(cache, definition)));
		/* (its animation's frame count and resource: the frame count in the
		low 16 bits, the resource in the high, until it is asked for) */
		{
			struct h3_cache_tag g;
			long animation_count, group_count;
			unsigned long animations, groups;

			if (h3_cache_tag_get(cache, graph, &g) &&
				h3_cache_block(cache, g.address + 0x50, 0x88, &animation_count, &animations) && animation_count > 0 &&
				h3_cache_block(cache, g.address + 0xF8, 0x40, &group_count, &groups) && group_count > 0)
			{
				unsigned long datum = h3_cache_read32(cache, groups + 4);

				build->gear.frame_count = (h3_cache_read16(cache, animations + 0x10) & 0xFFFF) | (long)((datum & 0xFFFF) << 16);
				build->gear.animation_size = 0x400;
			}
		}
		/* its materials' shaders */
		{
			long material_count, k;
			unsigned long materials;
			struct h3_cache_tag r;

			if (h3_cache_tag_get(cache, render_model, &r) &&
				h3_cache_block(cache, r.address + H3_MODE_MATERIALS, 0x24, &material_count, &materials))
			{
				for (k = 0; k < material_count; k++)
					shader_index(build, h3_cache_reference(cache, materials + 0x24 * k));
			}
		}
	}
}

/* the gear's turn: its axis from its animation's keys (the first run of
its frame count of 16-bit unit quaternions, 8 bytes apart: the second
key's axis, turned to the way it turns), else its model's thinnest side;
its model made (the render model's meshes, its materials' shaders) */
H3_ALIGN static void gear_make(struct build *build)
{
	struct h3_cache *cache = build->cache;
	struct gear *g = &build->gear;
	struct h3_cache_tag t;
	long material_count, i;
	unsigned long materials;
	short *shaders, *saved_shaders;
	long saved_count;

	if (!g->found || !h3_cache_tag_get(cache, g->mode_tag, &t) ||
		!h3_cache_block(cache, t.address + H3_MODE_MATERIALS, 0x24, &material_count, &materials))
	{
		g->found = FALSE;
		return;
	}
	g->axis[0] = 0.0f, g->axis[1] = 1.0f, g->axis[2] = 0.0f;
	if (g->animation && g->frame_count >= 2)
	{
		unsigned long at;

		for (at = 0x10; at + 8 * (unsigned long)g->frame_count <= g->animation_size && at < 0x200; at += 2)
		{
			long k;
			boolean unit = TRUE;

			for (k = 0; k < g->frame_count && unit; k++)
			{
				float q[4], length = 0.0f;
				int c;

				for (c = 0; c < 4; c++)
				{
					q[c] = read16(g->animation + at + 8 * k + 2 * c) / 32767.0f;
					length += q[c] * q[c];
				}
				unit = length > 0.97f * 0.97f && length < 1.03f * 1.03f;
			}
			if (unit)
			{
				float q[4], v;
				int c;

				for (c = 0; c < 4; c++)
					q[c] = read16(g->animation + at + 8 + 2 * c) / 32767.0f;
				/* (the vector part, signed as w is: the axis it turns about by
				the key's step) */
				v = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
				if (v > 0.1f)
				{
					float sign = q[3] < 0.0f ? -1.0f : 1.0f;

					for (c = 0; c < 3; c++)
						g->axis[c] = sign * q[c] / v;
				}
				break;
			}
		}
	}
	if (getenv("HALO_H3_DEBUG"))
		fprintf(stderr, "h3 level: gear %s at %.2f %.2f %.2f: axis %.2f %.2f %.2f, %.3f turns a second, %ld frames\n", g->name,
			g->position[0], g->position[1], g->position[2], g->axis[0], g->axis[1], g->axis[2], g->velocity, g->frame_count);
	shaders = malloc((material_count + 1) * sizeof(short));
	if (!shaders)
		return;
	for (i = 0; i < material_count; i++)
		shaders[i] = shader_for(build, h3_cache_reference(cache, materials + 0x24 * i));
	saved_shaders = build->material_shader;
	saved_count = build->material_count;
	build->material_shader = shaders;
	build->material_count = material_count;
	for (i = 0; i < g->geometry.mesh_count; i++)
	{
		struct section s;
		struct model_made mesh;

		if (!section_read(build, &g->geometry, i, &s))
			continue;
		if (model_make(build, &s, 1.0f, &mesh, 0.0f))
		{
			struct model_part_made *grown = realloc(g->model.parts, (g->model.part_count + mesh.part_count) * sizeof(*grown));

			if (grown)
			{
				memcpy(grown + g->model.part_count, mesh.parts, mesh.part_count * sizeof(*grown));
				g->model.parts = grown;
				g->model.part_count += mesh.part_count;
				g->model.scale = mesh.scale > g->model.scale ? mesh.scale : g->model.scale;
				if (mesh.radius > g->model.radius)
					g->model.radius = mesh.radius;
				free(mesh.parts);
			}
			else
				model_free(&mesh);
		}
		section_free(&s);
	}
	build->material_shader = saved_shaders;
	build->material_count = saved_count;
	free(shaders);
	if (!g->model.part_count)
		g->found = FALSE;
}

/* the scenario's sky (its first: a scenery), its model's geometry */
H3_ALIGN static long sky_model(struct build *build)
{
	struct h3_cache *cache = build->cache;
	long count, scenery, model, render_model;
	unsigned long skies;
	struct h3_cache_tag t;

	if (!h3_cache_block(cache, build->scenario + H3_SCNR(H3_SCNR_SKIES), 0x14, &count, &skies) || count < 1)
		return NONE;
	scenery = h3_cache_reference(cache, skies);
	if (!h3_cache_tag_get(cache, scenery, &t))
		return NONE;
	model = h3_cache_reference(cache, t.address + 0x34);
	if (!h3_cache_tag_get(cache, model, &t))
		return NONE;
	render_model = h3_cache_reference(cache, t.address);
	return render_model;
}

/* everything read: shaders and bitmaps planned, then every page each
wants read in one pass (the bitmaps into the file as they come) */
H3_ALIGN static boolean level_read(struct build *build)
{
	struct h3_cache *cache = build->cache;
	long count, i;
	unsigned long elements;
	struct h3_page_request *requests = NULL;
	struct request_context *contexts = NULL;
	long request_count = 0, request_capacity = 0;
	long sky_tag = sky_model(build);
	char why[H3_CACHE_WHY_SIZE];
	boolean ok = FALSE;
	long first_bsp_request, first_sky_request = 0, sky_request_end = 0, first_gear_request = 0, gear_request_end = 0;
	long first_prop_request = 0, prop_request_end = 0, first_decorator_request = 0, decorator_request_end = 0;

	/* the atmosphere (the sky's maps are hazed by it) */
	atmosphere_read(build);
	{
		unsigned char const *bounds = h3_cache_at(cache, build->sbsp + H3_SBSP_WORLD_BOUNDS, 24);

		if (bounds)
		{
			/* (x0 x1 y0 y1 z0 z1: the middle) */
			build->view_point[0] = 0.5f * (readf(bounds) + readf(bounds + 4));
			build->view_point[1] = 0.5f * (readf(bounds + 8) + readf(bounds + 12));
			build->view_point[2] = 0.5f * (readf(bounds + 16) + readf(bounds + 20));
		}
	}
	/* the BSP's materials' shaders and the collision materials' */
	if (!h3_cache_block(cache, build->sbsp + H3_SBSP_MATERIALS, H3_MATERIAL_SIZE, &count, &elements))
		return build_fail(build, "its materials are damaged"), FALSE;
	build->material_count = count;
	build->material_shader = malloc((count + 1) * sizeof(short));
	if (!build->material_shader)
		return build_fail(build, "out of memory"), FALSE;
	for (i = 0; i < count; i++)
		build->material_shader[i] = shader_for(build, h3_cache_reference(cache, elements + H3_MATERIAL_SIZE * i));
	if (h3_cache_block(cache, build->sbsp + H3_SBSP_COLLISION_MATERIALS, H3_COLLISION_MATERIAL_SIZE, &count, &elements))
	{
		for (i = 0; i < count; i++)
			shader_index(build, h3_cache_reference(cache, elements + H3_COLLISION_MATERIAL_SIZE * i));
	}
	/* the sky's materials' */
	if (sky_tag != NONE)
	{
		struct h3_cache_tag t;

		if (h3_cache_tag_get(cache, sky_tag, &t) && h3_cache_block(cache, t.address + H3_MODE_MATERIALS, 0x24, &count, &elements))
		{
			for (i = 0; i < count; i++)
			{
				short index = shader_index(build, h3_cache_reference(cache, elements + 0x24 * i));
				struct shader *sh = index != NONE ? &build->shaders[index] : NULL;

				/* (an opaque sky material's map, the sky's own: hazed by the
				atmosphere once its geometry and the exposure are known -
				Valhalla's far mountains, Last Resort's far islands, Guardian's
				far trees. Blended ones are not (HALO_H3_HAZE_BLENDED a test):
				Guardian's canopy came out a flat bright mint - its maps are a
				flat grey, its colour Halo 3's lighting of the sky object, which
				is not read yet) */
				if (sh && sh->bitmap != NONE && (sh->group == 'senv' || (build->bitmaps[sh->bitmap].product && getenv("HALO_H3_HAZE_BLENDED"))) &&
					!sh->dome && !sh->water &&
					build->atmosphere_sky.on && (!build->bitmaps[sh->bitmap].composite || build->bitmaps[sh->bitmap].product) &&
					strstr(h3_cache_tag_name(cache, sh->h3_tag), "\\sky\\") &&
					!strstr(tag_short_name(h3_cache_tag_name(cache, sh->h3_tag)), "cloud") &&
					!strstr(tag_short_name(h3_cache_tag_name(cache, sh->h3_tag)), "sun") && !getenv("HALO_H3_NO_SKY_HAZE"))
				{
					struct bitmap *hb = &build->bitmaps[sh->bitmap];
					long other;

					/* (a map a level surface draws too - Last Resort's ocean, its
					water's - is not the sky's alone: left as it is) */
					for (other = 0; other < build->shader_count; other++)
					{
						if (other != index && build->shaders[other].bitmap == sh->bitmap &&
							(build->shaders[other].h3_tag == NONE || !strstr(h3_cache_tag_name(cache, build->shaders[other].h3_tag), "\\sky\\")))
						{
							break;
						}
					}
					if (other >= build->shader_count)
					{
						hb->hazed = TRUE;
						if (!hb->convert && !hb->product)
						{
							hb->convert = TRUE;
							hb->made_width = hb->width;
							hb->made_height = hb->height;
						}
					}
				}

				/* (the sky's clouds and sun are blended, sky_add: their alpha kept;
				and every other blended sky material's - Guardian's canopy and fog
				plane, Valhalla's matte: drawn without it they were flat grey
				shapes) */
				if (sh && sh->bitmap != NONE && (strstr(tag_short_name(h3_cache_tag_name(cache, sh->h3_tag)), "cloud") ||
					strstr(tag_short_name(h3_cache_tag_name(cache, sh->h3_tag)), "sundisk") || sh->group == 'schi'))
				{
					build->bitmaps[sh->bitmap].needs_alpha = TRUE;
				}
				/* (clouds: Halo 3 lights them as its sky's sun does - their maps
				brightened, HALO_H3_CLOUD_BRIGHTNESS times) */
				if (sh && sh->bitmap != NONE && strstr(tag_short_name(h3_cache_tag_name(cache, sh->h3_tag)), "cloud") &&
					build->bitmaps[sh->bitmap].h3_tag != NONE && !build->bitmaps[sh->bitmap].product)
				{
					/* (a base that is only a mask: its alpha the cloud's shape, its
					colour a cloud's white - the detail's look left out; an additive
					cloud's colour goes by its alpha, dimmed: its dark detail isn't
					multiplied in) */
					long source = build->bitmaps[sh->bitmap].h3_tag;
					boolean masked = strstr(tag_short_name(h3_cache_tag_name(cache, source)), "_mask") != NULL;
					long bright = bitmap_for_mode(build, source, 5);

					if (bright != NONE)
					{
						sh = &build->shaders[index];
						build->bitmaps[bright].white = masked;
						if (sh->group == 'schi' && (sh->blend & 0xFF) == 3)
							build->bitmaps[bright].additive = TRUE;
						sh->bitmap = bright;
						/* (held until the sky's geometry is read: a cloud whose
						vertices have colours is drawn by them through the sky's
						atmosphere, Halo 3's static_per_vertex_color - sky_haze_write.
						HALO_H3_SKY_CLOUDS=0: brightened as before) */
						if (build->atmosphere_sky.on && env_float("HALO_H3_SKY_CLOUDS", 1.0f) != 0.0f && !getenv("HALO_H3_NO_SKY_HAZE"))
						{
							build->bitmaps[bright].cloud = TRUE;
							build->bitmaps[bright].hazed = TRUE;
						}
					}
				}
			}
		}
	}
	if (!build->shader_count)
		return build_fail(build, "it has no shaders"), FALSE;

	/* the geometry */
	if (!geometry_open(build, &build->bsp_geometry, h3_cache_read32(cache, build->lbsp + H3_LBSP_GEOMETRY_RESOURCE),
		build->lbsp + H3_LBSP_MESHES, build->lbsp + H3_LBSP_COMPRESSION))
	{
		return build_fail(build, "its render geometry is damaged"), FALSE;
	}
	geometry_request(build, &build->bsp_geometry, &requests, &request_count, &request_capacity);
	/* (the instances that share the lightprobe atlas: their coordinates, a
	vertex buffer an instance, in the Lbsp's list) */
	if (h3_cache_block(cache, build->lbsp + H3_LBSP_INSTANCE_TEXCOORDS, 0x10, &count, &elements))
	{
		for (i = 0; i < count; i++)
		{
			long vb = h3_cache_read16(cache, elements + 0x10 * i + 0xC);
			struct vertex_buffer *v;
			struct h3_page_request *r;

			if (vb < 0 || vb >= build->bsp_geometry.vertex_buffer_count)
				continue;
			v = &build->bsp_geometry.vertex_buffers[vb];
			if (v->declaration < 0 || !v->size || !(r = request_add(&requests, &request_count, &request_capacity)))
				continue;
			r->page = build->bsp_geometry.resource.page[0];
			r->offset = build->bsp_geometry.resource.offset[0] + v->offset;
			r->size = v->size;
			r->tag = vb;
			r->context = &build->bsp_geometry;
		}
	}
	first_bsp_request = 0;
	first_sky_request = request_count;
	if (sky_tag != NONE)
	{
		struct h3_cache_tag t;

		if (h3_cache_tag_get(cache, sky_tag, &t) &&
			geometry_open(build, &build->sky_geometry, h3_cache_read32(cache, t.address + H3_MODE_RESOURCE),
				t.address + H3_MODE_MESHES, t.address + H3_MODE_COMPRESSION))
		{
			geometry_request(build, &build->sky_geometry, &requests, &request_count, &request_capacity);
		}
	}
	sky_request_end = request_count;
	gear_find(build);
	first_gear_request = request_count;
	if (build->gear.found)
	{
		struct h3_cache_tag t;

		if (h3_cache_tag_get(cache, build->gear.mode_tag, &t) &&
			geometry_open(build, &build->gear.geometry, h3_cache_read32(cache, t.address + H3_MODE_RESOURCE),
				t.address + H3_MODE_MESHES, t.address + H3_MODE_COMPRESSION))
		{
			geometry_request(build, &build->gear.geometry, &requests, &request_count, &request_capacity);
		}
		else
			build->gear.found = FALSE;
	}
	gear_request_end = request_count;
	props_find(build);
	first_prop_request = request_count;
	for (i = 0; i < build->prop_count; i++)
	{
		struct prop *prop = &build->props[i];
		struct h3_cache_tag t;
		long model = NONE;

		if (h3_cache_tag_get(cache, prop->object_tag, &t) && h3_cache_tag_get(cache, h3_cache_reference(cache, t.address + 0x34), &t))
			model = h3_cache_reference(cache, t.address);
		if (h3_cache_tag_get(cache, model, &t) &&
			geometry_open(build, &prop->geometry, h3_cache_read32(cache, t.address + H3_MODE_RESOURCE),
				t.address + H3_MODE_MESHES, t.address + H3_MODE_COMPRESSION))
		{
			prop->geometry_open = TRUE;
			geometry_request(build, &prop->geometry, &requests, &request_count, &request_capacity);
		}
	}
	prop_request_end = request_count;
	decorators_find(build);
	first_decorator_request = request_count;
	decorators_request(build, &requests, &request_count, &request_capacity);
	decorator_request_end = request_count;
	if (build->gear.found && build->gear.animation_size)
	{
		struct h3_page_request *q = request_add(&requests, &request_count, &request_capacity);
		struct h3_resource r;

		if (q && h3_cache_resource_get(cache, (long)build->gear.frame_count >> 16, &r) && r.page[0] != NONE)
		{
			q->page = r.page[0];
			q->offset = r.offset[0];
			q->size = r.size[0] < build->gear.animation_size ? r.size[0] : build->gear.animation_size;
			q->tag = REQUEST_GEAR_ANIMATION;
			h3_resource_free(&r);
		}
		else if (q)
			request_count--;
		build->gear.frame_count &= 0xFFFF;
		build->gear.animation_size = 0;
	}

	/* the per-vertex lighting: each instance's buffer averaged */
	if (h3_cache_block(cache, build->lbsp + H3_LBSP_PER_VERTEX, 0x10, &count, &elements))
	{
		for (i = 0; i < count; i++)
		{
			long vb = (long)h3_cache_read32(cache, elements + 0x10 * i + 0xC);
			struct vertex_buffer *v;
			struct h3_page_request *r;

			if (vb < 0 || vb >= build->bsp_geometry.vertex_buffer_count)
				continue;
			v = &build->bsp_geometry.vertex_buffers[vb];
			if (v->declaration != 6 || v->stride != 20 || v->light_done || !(r = request_add(&requests, &request_count, &request_capacity)))
				continue;
			r->page = build->bsp_geometry.resource.page[0];
			r->offset = build->bsp_geometry.resource.offset[0] + v->offset;
			r->size = (unsigned long)v->count * 20;
			r->tag = REQUEST_VERTEX_LIGHT;
			r->context = (void *)(long)vb;
			v->light_done = TRUE;   /* (asked for once) */
		}
		for (i = 0; i < build->bsp_geometry.vertex_buffer_count; i++)
			build->bsp_geometry.vertex_buffers[i].light_done = FALSE;
	}

	/* the lightprobe textures, every slice */
	for (i = 0; i < 2; i++)
	{
		long tag = h3_cache_reference(cache, build->lbsp + (i ? H3_LBSP_INTENSITY_TEXTURE : H3_LBSP_LIGHTPROBE_TEXTURE));
		struct h3_cache_tag t;
		long resource_count;
		unsigned long resources;
		struct h3_resource r;
		struct h3_page_request *q;

		if (!h3_cache_tag_get(cache, tag, &t) || !h3_cache_block(cache, t.address + 0x8C, 8, &resource_count, &resources) ||
			resource_count < 1 || !h3_cache_resource_get(cache, (long)(h3_cache_read32(cache, resources) & 0xFFFF), &r))
		{
			continue;
		}
		if (r.page[0] != NONE && r.size[0] >= (unsigned long)H3_LIGHTPROBE_SIZE * H3_LIGHTPROBE_SIZE * (i ? H3_INTENSITY_SLICES : H3_LIGHTPROBE_SLICES) &&
			(q = request_add(&requests, &request_count, &request_capacity)) != NULL)
		{
			q->page = r.page[0];
			q->offset = r.offset[0];
			q->size = (unsigned long)H3_LIGHTPROBE_SIZE * H3_LIGHTPROBE_SIZE * (i ? H3_INTENSITY_SLICES : H3_LIGHTPROBE_SLICES);
			q->tag = i ? REQUEST_INTENSITY : REQUEST_LIGHTPROBE;
		}
		h3_resource_free(&r);
	}

	/* the collision: the level's BSP and the instanced geometry's */
	{
		unsigned long datum = h3_cache_read32(cache, build->sbsp + H3_SBSP_COLLISION_RESOURCE);
		struct h3_resource *r = &build->collision_resource;
		long first;
		unsigned long kind, at, definitions;

		boolean large = FALSE;

		if (datum == 0xFFFFFFFFUL || !h3_cache_resource_get(cache, (long)(datum & 0xFFFF), r) || r->page[0] == NONE)
		{
			build_fail(build, "its collision is damaged");
			goto done;
		}
		/* (ODST: the level's collision the large collision BSP, the root's
		second block, when the first is empty) */
		if (h3_odst && h3_resource_read32(r, r->root) < 1 && h3_resource_read32(r, r->root + 0xC) >= 1)
			large = TRUE;
		if (h3_resource_read32(r, r->root + (large ? 0xC : 0)) < 1 ||
			!h3_resource_pointer(r, r->root + (large ? 0x10 : 4), &kind, &at) || kind != 2)
		{
			build_fail(build, "its collision is damaged");
			goto done;
		}
		first = request_count;
		if (!collision_request(build, at, &build->collision, &requests, &request_count, &request_capacity, large))
		{
			build_fail(build, "its collision BSP is damaged");
			goto done;
		}
		for (i = first; i < request_count; i++)
			requests[i].tag = 1000 + (requests[i].tag - 1000), requests[i].context = (void *)-1L;
		build->definition_count = (long)h3_resource_read32(r, r->root + 0x18);
		if (build->definition_count < 0 || build->definition_count > 0x4000 ||
			(build->definition_count && (!h3_resource_pointer(r, r->root + 0x1C, &kind, &definitions) || kind != 2 ||
				definitions > r->control_size || (unsigned long)build->definition_count * H3_DEFINITION_SIZE > r->control_size - definitions)))
		{
			build->definition_count = 0;
		}
		if (getenv("HALO_H3_COLLISION_DEBUG"))
		{
			unsigned long o;

			fprintf(stderr, "h3 collision: control %lu bytes, root %lu, large %d, definitions %ld at %lu\n", r->control_size, r->root,
				(int)large, build->definition_count, build->definition_count ? definitions : 0);
			for (o = 0; o < 0x30; o += 4)
				fprintf(stderr, " root+%02lX %08lX\n", o, h3_resource_read32(r, r->root + o));
			for (o = 0; build->definition_count && o < 0x1A0 && definitions + o + 4 <= r->control_size; o += 4)
				fprintf(stderr, " def+%03lX %08lX %g\n", o, read32(r->control + definitions + o), readf(r->control + definitions + o));
		}
		build->definition_collision = calloc(build->definition_count + 1, sizeof(*build->definition_collision));
		build->definition_radius = calloc(build->definition_count + 1, sizeof(float));
		build->definition_centre = calloc(3 * (build->definition_count + 1), sizeof(float));
		if (!build->definition_collision || !build->definition_radius || !build->definition_centre)
		{
			build_fail(build, "out of memory");
			goto done;
		}
		for (i = 0; i < build->definition_count; i++)
		{
			long k;

			first = request_count;
			build->definition_radius[i] = readf(r->control + definitions + H3_DEFINITION_SIZE * i + 0x10);
			for (k = 0; k < 3; k++)
				build->definition_centre[3 * i + k] = readf(r->control + definitions + H3_DEFINITION_SIZE * i + 4 + 4 * k);
			if (!collision_request(build, definitions + H3_DEFINITION_SIZE * i + 0x14, &build->definition_collision[i],
				&requests, &request_count, &request_capacity, FALSE))
			{
				request_count = first;
				memset(&build->definition_collision[i], 0, sizeof(build->definition_collision[i]));
				continue;
			}
			for (k = first; k < request_count; k++)
				requests[k].context = (void *)(-2L - i);
		}
	}

	/* the bitmaps */
	for (i = 0; i < build->bitmap_count; i++)
	{
		struct h3_page_request *q;

		if (build->bitmaps[i].generated || build->bitmaps[i].composite || build->bitmaps[i].mode == 3)
			continue;
		q = request_add(&requests, &request_count, &request_capacity);
		if (!q)
			break;
		q->page = build->bitmaps[i].page;
		q->offset = build->bitmaps[i].offset;
		q->size = build->bitmaps[i].size;
		q->tag = REQUEST_BITMAP;
		q->context = (void *)i;
	}

	/* every request's context, then the pages read */
	contexts = calloc(request_count + 1, sizeof(*contexts));
	if (!contexts)
	{
		build_fail(build, "out of memory");
		goto done;
	}
	for (i = 0; i < request_count; i++)
	{
		struct h3_page_request *q = &requests[i];
		struct request_context *c = &contexts[i];

		c->build = build;
		if (i < first_sky_request)
			c->kind = REQUEST_GEOMETRY, (void)first_bsp_request;
		else if (i < sky_request_end)
			c->kind = REQUEST_SKY_GEOMETRY;
		else if (i >= first_gear_request && i < gear_request_end)
			c->kind = REQUEST_GEAR_GEOMETRY;
		else if (i >= first_decorator_request && i < decorator_request_end)
		{
			long p;

			c->kind = REQUEST_DECORATOR_BUFFER;
			c->index = 0;
			for (p = 0; p < build->decorator_count; p++)
			{
				if (q->context == &build->decorators[p].geometry)
					c->kind = REQUEST_DECORATOR_GEOMETRY, c->index = p;
			}
		}
		else if (i >= first_prop_request && i < prop_request_end)
		{
			long p;

			c->kind = REQUEST_PROP_GEOMETRY;
			c->index = 0;
			for (p = 0; p < build->prop_count; p++)
			{
				if (q->context == &build->props[p].geometry)
					c->index = p;
			}
		}
		else if (q->tag >= 1000 && q->tag < 1008)
		{
			long which = (long)q->context;

			c->kind = which == -1 ? REQUEST_COLLISION : REQUEST_DEFINITION_COLLISION;
			c->index = which == -1 ? 0 : -2 - which;
			q->context = c;
			q->done = request_done;
			continue;
		}
		else
		{
			c->kind = q->tag;
			c->index = (long)q->context;
		}
		if (c->kind == REQUEST_GEOMETRY || c->kind == REQUEST_SKY_GEOMETRY)
		{
			/* (geometry_got reads the buffer's number from the request's tag) */
		}
		q->context = c;
		q->done = request_done;
	}
	HEAP_SAMPLE();
	if (!h3_cache_pages_read(cache, requests, request_count, why, sizeof(why), NULL))
	{
		build_fail(build, why);
		goto done;
	}
	/* (the second pass: the lit bases, their details' means known) */
	{
		struct h3_page_request *second = calloc(build->bitmap_count + 1, sizeof(*second));
		struct request_context *second_contexts = calloc(build->bitmap_count + 1, sizeof(*second_contexts));
		long n = 0;

		if (!second || !second_contexts)
		{
			free(second);
			free(second_contexts);
			build_fail(build, "out of memory");
			goto done;
		}
		for (i = 0; i < build->bitmap_count; i++)
		{
			if (build->bitmaps[i].mode != 3)
				continue;
			second[n].page = build->bitmaps[i].page;
			second[n].offset = build->bitmaps[i].offset;
			second[n].size = build->bitmaps[i].size;
			second_contexts[n].build = build;
			second_contexts[n].kind = REQUEST_BITMAP;
			second_contexts[n].index = i;
			second[n].context = &second_contexts[n];
			second[n].done = request_done;
			n++;
		}
		if (n && !h3_cache_pages_read(cache, second, n, why, sizeof(why), NULL))
		{
			for (i = 0; i < n; i++)
			{
				if (!second[i].got)
					free(second[i].out);
			}
			free(second);
			free(second_contexts);
			build_fail(build, why);
			goto done;
		}
		free(second);
		free(second_contexts);
	}
	/* (the made sky's maps once the exposure is known: sky_haze_write) */
	for (i = 0; i < build->bitmap_count; i++)
	{
		if (build->bitmaps[i].product)
			bitmap_product_write(build, &build->bitmaps[i]);
		else if (build->bitmaps[i].composite)
			bitmap_composite_write(build, i);
	}
	HEAP_SAMPLE();
	ok = TRUE;

done:
	for (i = 0; i < request_count; i++)
	{
		if (!requests[i].got)
			free(requests[i].out);
	}
	free(requests);
	free(contexts);

	return ok;
}

/* ---------- private code: the instanced geometry's objects */

/* a strip's next triangle (a, b, c counter-clockwise, Halo 3's): joined to
what is there by repeated indices, turned as its place turns it, given
clockwise (this engine's) - scene_pack_blocks.c's strip_triangle */
H3_ALIGN static void strip_triangle(unsigned short *strip, long *count, unsigned short a, unsigned short b, unsigned short c)
{
	unsigned short swap = b;

	b = c;
	c = swap;
	if (*count)
	{
		unsigned short last = strip[*count - 1];

		strip[(*count)++] = last;
		strip[(*count)++] = a;
		if ((*count) & 1)
			strip[(*count)++] = a;
	}
	strip[(*count)++] = a;
	strip[(*count)++] = b;
	strip[(*count)++] = c;
}

/* (rasterizer_geometry.c's compress_real_vector3d_to_int32_clamp) */
H3_ALIGN static unsigned long compress_model_vector(float const *vector)
{
	float i = vector[0] < -1.f ? -1.f : vector[0] > 1.f ? 1.f : vector[0];
	float j = vector[1] < -1.f ? -1.f : vector[1] > 1.f ? 1.f : vector[1];
	float k = vector[2] < -1.f ? -1.f : vector[2] > 1.f ? 1.f : vector[2];
	long ci = (long)floor((double)i * 1023.5f) & 0x7FF;
	long cj = (long)floor((double)j * 1023.5f) & 0x7FF;
	long ck = (long)floor((double)k * 511.5f) & 0x3FF;

	return (unsigned long)((((ck << 11) | cj) << 11) | ci);
}

H3_ALIGN static short compress_model_real(float value)
{
	float clamped = value < -1.f ? -1.f : value > 1.f ? 1.f : value;

	return (short)(long)floor((double)clamped * 32767.5f);
}

/* a definition's mesh made a model (at scale): its parts, one a shader,
compressed vertices and a strip each; FALSE if it has none drawn */
/* (a made model part's most triangles: its strip at most six indices a
triangle (strip_triangle), under 65535) */
enum { MODEL_PART_TRIANGLES = 10000 };

H3_ALIGN static boolean model_make(struct build *build, struct section const *s, float scale, struct model_made *model,
	float uv_scale)
{
	long p, n;
	float largest = 0.0f;
	long *remap = malloc((s->vertex_count + 1) * sizeof(long));

	long chunk_parts = 0;

	memset(model, 0, sizeof(*model));
	/* (a part of more triangles than a strip of this engine's holds - 65535
	indices, MAXIMUM_TRIANGLES_PER_MODEL_GEOMETRY_PART - made several:
	Guardian's and Longshore's biggest, 74509 and 67099 triangles, were
	refused whole by the map checker) */
	for (p = 0; p < s->part_count; p++)
		chunk_parts += s->parts[p].count / 3 / MODEL_PART_TRIANGLES + 1;
	model->parts = calloc(chunk_parts + 1, sizeof(*model->parts));
	if (!remap || !model->parts)
	{
		free(remap);
		free(model->parts);
		model->parts = NULL;
		return FALSE;
	}
	for (n = 0; n < s->vertex_count; n++)
	{
		float const *v = s->positions + 3 * n;
		float d = scale * sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

		if (d > model->radius)
			model->radius = d;
	}
	/* (the texture coordinates' largest, each shader's base scale on them:
	the model's base map scale a power of two over it) */
	for (p = 0; p < s->part_count; p++)
	{
		short shader = s->parts[p].material >= 0 && s->parts[p].material < build->material_count ?
			build->material_shader[s->parts[p].material] : NONE;
		long t;

		if (shader == NONE)
			continue;
		for (t = 0; t < s->parts[p].count; t++)
		{
			long v = s->indices[s->parts[p].first + t];
			float u = fabsf(s->coordinates[2 * v] * build->shaders[shader].base_scale[0]);
			float w = fabsf(s->coordinates[2 * v + 1] * build->shaders[shader].base_scale[1]);

			largest = u > largest ? u : largest;
			largest = w > largest ? w : largest;
		}
	}
	for (model->scale = 1.0f; model->scale < largest && model->scale < 65536.0f; model->scale *= 2.0f)
		;
	if (uv_scale > 0.0f)
		model->scale = uv_scale;
	for (p = 0; p < s->part_count; p++)
	{
	long part_first = s->parts[p].first, part_count = s->parts[p].count - s->parts[p].count % 3, chunk_first;

	for (chunk_first = 0; chunk_first < part_count || (chunk_first == 0 && part_count == 0); chunk_first += 3 * MODEL_PART_TRIANGLES)
	{
		short shader = s->parts[p].material >= 0 && s->parts[p].material < build->material_count ?
			build->material_shader[s->parts[p].material] : NONE;
		struct model_part_made *part;
		long first = part_first + chunk_first, t, used = 0;
		long count = part_count - chunk_first < 3 * MODEL_PART_TRIANGLES ? part_count - chunk_first : 3 * MODEL_PART_TRIANGLES;
		unsigned short *strip;
		unsigned char *out;
		double centroid[3] = { 0.0, 0.0, 0.0 };

		if (shader == NONE || count < 3)
		{
			if (getenv("HALO_H3_DEBUG") && count >= 3)
				fprintf(stderr, "h3 level: dropped model part: material %d has no shader, %ld triangles\n", s->parts[p].material, count / 3);
			continue;
		}
		for (n = 0; n < s->vertex_count; n++)
			remap[n] = -1;
		for (t = 0; t < count; t++)
		{
			long v = s->indices[first + t];

			if (remap[v] < 0)
				remap[v] = used++;
		}
		part = &model->parts[model->part_count];
		part->shader = shader;
		part->vertices = malloc((unsigned long)used * 32);
		strip = malloc((unsigned long)count * 2 * sizeof(unsigned short) + 8);
		if (!part->vertices || !strip)
		{
			free(part->vertices);
			free(strip);
			part->vertices = NULL;
			continue;
		}
		for (n = 0; n < s->vertex_count; n++)
		{
			float normal[3], binormal[3], tangent[3];
			int k;

			if (remap[n] < 0)
				continue;
			out = part->vertices + 32 * remap[n];
			for (k = 0; k < 3; k++)
			{
				writef(out + 4 * k, s->positions[3 * n + k] * scale);
				centroid[k] += s->positions[3 * n + k] * scale;
			}
			unpack_vector(s->packed[3 * n], normal);
			unpack_vector(s->packed[3 * n + 1], binormal);
			unpack_vector(s->packed[3 * n + 2], tangent);
			write32(out + 12, compress_model_vector(normal));
			write32(out + 16, compress_model_vector(binormal));
			write32(out + 20, compress_model_vector(tangent));
			write16(out + 24, compress_model_real(s->coordinates[2 * n] * build->shaders[shader].base_scale[0] / model->scale));
			write16(out + 26, compress_model_real(s->coordinates[2 * n + 1] * build->shaders[shader].base_scale[1] / model->scale));
			out[28] = 0;
			out[29] = 0;
			write16(out + 30, compress_model_real(1.0f));
		}
		{
			long indices = 0;

			for (t = 0; t + 2 < count; t += 3)
			{
				strip_triangle(strip, &indices, (unsigned short)remap[s->indices[first + t]],
					(unsigned short)remap[s->indices[first + t + 1]], (unsigned short)remap[s->indices[first + t + 2]]);
			}
			part->strip = (unsigned char *)strip;
			part->strip_bytes = (unsigned long)indices * 2;
			part->triangle_count = indices - 2;
			for (t = 0; t < indices; t++)
			{
				unsigned short value = strip[t];

				write16(part->strip + 2 * t, value);
			}
		}
		part->vertex_count = used;
		part->vertex_bytes = (unsigned long)used * 32;
		for (n = 0; n < 3; n++)
			part->centroid[n] = used ? (float)(centroid[n] / used) : 0.0f;
		model->part_count++;
		build->result->model_vertices += used;
	}
	}
	free(remap);

	return model->part_count > 0;
}

H3_ALIGN static void model_free(struct model_made *model)
{
	long p;

	for (p = 0; p < model->part_count; p++)
	{
		free(model->parts[p].vertices);
		free(model->parts[p].strip);
	}
	free(model->parts);
	memset(model, 0, sizeof(*model));
}

/* an instance's scale as a key's (hundredths) */
H3_ALIGN static long scale_key(float scale)
{
	long key = (long)floorf(scale * 100.0f + 0.5f);

	return key < 1 ? 100 : key;
}

/* the object (key) of a definition at a scale: found or added (NONE: none) */
H3_ALIGN static long object_key(struct build *build, short definition, long scale)
{
	long i;
	struct object_key *grown;

	for (i = 0; i < build->key_count; i++)
	{
		if (build->keys[i].definition == definition && build->keys[i].scale == scale)
			return i;
	}
	grown = realloc(build->keys, (build->key_count + 1) * sizeof(*grown));
	if (!grown)
		return NONE;
	build->keys = grown;
	memset(&build->keys[build->key_count], 0, sizeof(*grown));
	build->keys[build->key_count].definition = definition;
	build->keys[build->key_count].scale = scale;
	build->keys[build->key_count].slot = NONE;
	build->keys[build->key_count].model = NONE;
	build->keys[build->key_count].prop = NONE;
	return build->key_count++;
}

/* the cluster that holds a point (the smallest whose bounds hold it, else
the nearest) */
enum { CLUSTER_SURFACE_ROOM = 32000 };

/* the cluster that holds a point (the smallest whose bounds hold it, else
the nearest) with room for surfaces more (CLUSTER_SURFACE_ROOM; else the
nearest that has) */
H3_ALIGN static short cluster_of_room(struct build *build, float const centre[3], long surfaces)
{
	float best = 1e30f;
	short cluster = 0;
	long j;
	int k;

	for (j = 0; j < build->cluster_count; j++)
	{
		unsigned char const *b = h3_cache_at(build->cache, build->clusters + H3_CLUSTER_SIZE * j, 24);
		float bounds[6], d = 0.0f;

		if (!b || (surfaces > 0 && j < 512 && build->cluster_surfaces[j] + surfaces > CLUSTER_SURFACE_ROOM))
			continue;
		memcpy(bounds, b, sizeof(bounds));
		for (k = 0; k < 3; k++)
		{
			float lo = bounds[2 * k], hi = bounds[2 * k + 1];

			if (centre[k] < lo)
				d += (lo - centre[k]) * (lo - centre[k]);
			else if (centre[k] > hi)
				d += (centre[k] - hi) * (centre[k] - hi);
		}
		if (d == 0.0f)
			d = -1.0f / (1.0f + (bounds[1] - bounds[0]) * (bounds[3] - bounds[2]) * (bounds[5] - bounds[4]));
		if (d < best)
		{
			best = d;
			cluster = (short)j;
		}
	}
	return cluster;
}

struct instance_order { long index; float size; };

H3_ALIGN static int instance_order_compare(void const *a, void const *b)
{
	float x = ((struct instance_order const *)a)->size, y = ((struct instance_order const *)b)->size;

	return x > y ? -1 : x < y ? 1 : 0;
}

/* where a direction of the sky dome is on its map: the zenith the middle, the
horizon a circle, 15 degrees under it the rim */
H3_ALIGN static void sky_dome_coordinates(float const *q, float *uv)
{
	float flat = sqrtf(q[0] * q[0] + q[1] * q[1]), elevation = atan2f(q[2], flat + 1.0f);
	float r = (1.5707963f - elevation) / (1.5707963f + 0.2617994f);

	r = r < 0.0f ? 0.0f : r > 1.0f ? 1.0f : r;
	uv[0] = 0.5f + 0.48f * r * (flat > 1e-3f ? q[0] / flat : 0.0f);
	uv[1] = 0.5f + 0.48f * r * (flat > 1e-3f ? q[1] / flat : 0.0f);
}

/* the sky dome's triangles (count indices from first of the section's) drawn
into the dome's map as Halo 3 draws them (static_per_vertex_color: the
vertex's colour times the albedo, times the atmosphere's extinction, plus its
inscatter, between a triangle's corners), and the vertices given the map's
coordinates. FALSE: it has no vertex colours or the sky no atmosphere (the
made gradient, as before), or HALO_H3_SKY_DOME=0 */
H3_ALIGN static boolean sky_dome_draw(struct build *build, struct section *s, long first, long count)
{
	float *lit;
	long t, v;

	if (!s->colours || !build->atmosphere_sky.on || env_float("HALO_H3_SKY_DOME", 1.0f) == 0.0f || count < 3)
		return FALSE;
	if (!build->sky_dome_image)
		build->sky_dome_image = calloc((size_t)SKY_DOME_SIDE * SKY_DOME_SIDE, 4 * sizeof(float));
	lit = malloc((size_t)s->vertex_count * 3 * sizeof(float));
	if (!build->sky_dome_image || !lit)
	{
		free(lit);
		return FALSE;
	}
	for (v = 0; v < s->vertex_count; v++)
	{
		float extinction[3], inscatter[3];
		int k;

		atmosphere_scatter(&build->atmosphere_sky, build->view_point, s->positions + 3 * v, extinction, inscatter);
		for (k = 0; k < 3; k++)
			lit[3 * v + k] = s->colours[3 * v + k] * build->sky_dome_albedo[k] * extinction[k] + inscatter[k];
		if (getenv("HALO_H3_SKY_DEBUG") && v % 61 == 0)
		{
			float const *q = s->positions + 3 * v;

			platform_log("h3 sky dome: azimuth %.0f elevation %.1f colour %.3f %.3f %.3f extinction %.3f %.3f %.3f inscatter %.4f %.4f %.4f",
				atan2f(q[1], q[0]) * 57.29578f, atan2f(q[2], sqrtf(q[0] * q[0] + q[1] * q[1]) + 1.0f) * 57.29578f, s->colours[3 * v],
				s->colours[3 * v + 1], s->colours[3 * v + 2], extinction[0], extinction[1], extinction[2], inscatter[0], inscatter[1], inscatter[2]);
		}
	}
	for (t = 0; t + 2 < count; t += 3)
	{
		long corner[3] = { s->indices[first + t], s->indices[first + t + 1], s->indices[first + t + 2] };
		float uv[3][2], area;
		long x0, x1, y0, y1, x, y;
		int k;

		for (k = 0; k < 3; k++)
		{
			sky_dome_coordinates(s->positions + 3 * corner[k], uv[k]);
			uv[k][0] *= SKY_DOME_SIDE;
			uv[k][1] *= SKY_DOME_SIDE;
		}
		area = (uv[1][0] - uv[0][0]) * (uv[2][1] - uv[0][1]) - (uv[2][0] - uv[0][0]) * (uv[1][1] - uv[0][1]);
		if (fabsf(area) < 1e-6f)
			continue;
		x0 = (long)floorf(fminf(uv[0][0], fminf(uv[1][0], uv[2][0])));
		x1 = (long)ceilf(fmaxf(uv[0][0], fmaxf(uv[1][0], uv[2][0])));
		y0 = (long)floorf(fminf(uv[0][1], fminf(uv[1][1], uv[2][1])));
		y1 = (long)ceilf(fmaxf(uv[0][1], fmaxf(uv[1][1], uv[2][1])));
		x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0;
		x1 = x1 > SKY_DOME_SIDE - 1 ? SKY_DOME_SIDE - 1 : x1, y1 = y1 > SKY_DOME_SIDE - 1 ? SKY_DOME_SIDE - 1 : y1;
		for (y = y0; y <= y1; y++)
		{
			for (x = x0; x <= x1; x++)
			{
				float px = x + 0.5f, py = y + 0.5f, w[3];
				float *o = build->sky_dome_image + 4 * (y * SKY_DOME_SIDE + x);

				w[0] = ((uv[1][0] - px) * (uv[2][1] - py) - (uv[2][0] - px) * (uv[1][1] - py)) / area;
				w[1] = ((uv[2][0] - px) * (uv[0][1] - py) - (uv[0][0] - px) * (uv[2][1] - py)) / area;
				w[2] = 1.0f - w[0] - w[1];
				/* (a texel whose middle the triangle misses by under a texel
				still takes it, if nothing has drawn it: no seams of nothing) */
				if (w[0] < -0.02f || w[1] < -0.02f || w[2] < -0.02f)
				{
					if (o[3] > 0.0f || w[0] < -0.75f || w[1] < -0.75f || w[2] < -0.75f)
						continue;
					for (k = 0; k < 3; k++)
						w[k] = w[k] < 0.0f ? 0.0f : w[k];
					{
						float sum = w[0] + w[1] + w[2];

						for (k = 0; k < 3; k++)
							w[k] /= sum > 0.0f ? sum : 1.0f;
					}
					for (k = 0; k < 3; k++)
						o[k] = w[0] * lit[3 * corner[0] + k] + w[1] * lit[3 * corner[1] + k] + w[2] * lit[3 * corner[2] + k];
					o[3] = 0.5f;
					continue;
				}
				for (k = 0; k < 3; k++)
					o[k] = w[0] * lit[3 * corner[0] + k] + w[1] * lit[3 * corner[1] + k] + w[2] * lit[3 * corner[2] + k];
				o[3] = 1.0f;
			}
		}
	}
	for (t = 0; t < count; t++)
		sky_dome_coordinates(s->positions + 3 * s->indices[first + t], s->coordinates + 2 * s->indices[first + t]);
	free(lit);
	return TRUE;
}

/* the sky: its model's meshes as level geometry, in cluster, scaled about
the level's middle to inside the far clip (HALO_H3_SKY_SCALE) */
H3_ALIGN static void sky_add(struct build *build, short cluster, float const *bounds)
{
	struct h3_cache *cache = build->cache;
	long sky_tag = sky_model(build);
	struct h3_cache_tag t;
	long material_count, i;
	unsigned long materials;
	short *shaders, *saved_shaders;
	long saved_count;
	float matrix[13];

	if (sky_tag == NONE || !build->sky_geometry.mesh_count || !h3_cache_tag_get(cache, sky_tag, &t) ||
		!h3_cache_block(cache, t.address + H3_MODE_MATERIALS, 0x24, &material_count, &materials))
	{
		return;
	}
	shaders = malloc((material_count + 1) * sizeof(short));
	if (!shaders)
		return;
	for (i = 0; i < material_count; i++)
	{
		shaders[i] = shader_for(build, h3_cache_reference(cache, materials + 0x24 * i));
		if (shaders[i] != NONE && getenv("HALO_H3_SKIP") &&
			strstr(getenv("HALO_H3_SKIP"), tag_short_name(h3_cache_tag_name(cache, build->shaders[shaders[i]].h3_tag))))
		{
			shaders[i] = NONE;
		}
		/* (the sky's: two-sided, blended, drawn far off) */
		if (shaders[i] != NONE && build->shaders[shaders[i]].group == 'senv' && !getenv("HALO_H3_SKY_OPAQUE"))
		{
			char const *name = tag_short_name(h3_cache_tag_name(cache, build->shaders[shaders[i]].h3_tag));
			struct shader *s = &build->shaders[shaders[i]];

			if (strstr(name, "cloud") || strstr(name, "sundisk"))
				s->group = 'schi', s->blend = 3 | 0x100;
		}
	}
	memset(matrix, 0, sizeof(matrix));
	matrix[0] = matrix[4] = matrix[8] = 1.0f;
	matrix[9] = 0.5f * (bounds[0] + bounds[1]);
	matrix[10] = 0.5f * (bounds[2] + bounds[3]);
	matrix[11] = env_float("HALO_H3_SKY_Z", 0.0f);
	matrix[12] = env_float("HALO_H3_SKY_SCALE", 0.06f);
	saved_shaders = build->material_shader;
	saved_count = build->material_count;
	build->material_shader = shaders;
	build->material_count = material_count;
	for (i = 0; i < build->sky_geometry.mesh_count; i++)
	{
		struct section s;

		if (!section_read(build, &build->sky_geometry, i, &s))
			continue;
		/* (h3/pc-acc: how much of the sky model Halo 3's sky atmosphere
		lets through, its vertices' mean - atmosphere_sky_hidden) */
		{
			long v;

			for (v = 0; v < s.vertex_count && build->atmosphere_sky.on; v++)
			{
				float extinction[3], inscatter[3];

				atmosphere_scatter(&build->atmosphere_sky, build->view_point, s.positions + 3 * v, extinction, inscatter);
				build->sky_extinction_sum += (extinction[0] + extinction[1] + extinction[2]) / 3.0;
				build->sky_extinction_count++;
			}
		}
		if (getenv("HALO_H3_DEBUG"))
			fprintf(stderr, "h3 level: sky mesh %ld: %ld vertices, %ld indices, %ld parts, material %d\n", i, s.vertex_count,
				s.index_count, s.part_count, s.part_count ? s.parts[0].material : -1);
		/* (the dome's texture coordinates its elevation's: the gradient's) */
		{
			long p, t;

			for (p = 0; p < s.part_count; p++)
			{
				short shader = s.parts[p].material >= 0 && s.parts[p].material < material_count ? shaders[s.parts[p].material] : NONE;

				if (shader == NONE || !build->shaders[shader].dome)
					continue;
				if (sky_dome_draw(build, &s, s.parts[p].first, s.parts[p].count))
					continue;
				for (t = 0; t < s.parts[p].count; t++)
				{
					long v = s.indices[s.parts[p].first + t];
					float const *q = s.positions + 3 * v;
					float elevation = atan2f(q[2], sqrtf(q[0] * q[0] + q[1] * q[1]) + 1.0f);

					s.coordinates[2 * v] = 0.5f;
					s.coordinates[2 * v + 1] = 0.8f - 0.8f * elevation / 1.5707963f;
					if (s.coordinates[2 * v + 1] < 0.01f)
						s.coordinates[2 * v + 1] = 0.01f;
					if (s.coordinates[2 * v + 1] > 0.99f)
						s.coordinates[2 * v + 1] = 0.99f;
				}
			}
		}
		if (getenv("HALO_H3_SKY_UV_DEBUG"))
		{
			long p, t;

			for (p = 0; p < s.part_count; p++)
			{
				short shader = s.parts[p].material >= 0 && s.parts[p].material < material_count ? shaders[s.parts[p].material] : NONE;
				float lo[2] = { 1e9f, 1e9f }, hi[2] = { -1e9f, -1e9f };

				for (t = 0; t < s.parts[p].count; t++)
				{
					long v = s.indices[s.parts[p].first + t];
					int k;

					for (k = 0; k < 2; k++)
					{
						lo[k] = fminf(lo[k], s.coordinates[2 * v + k]);
						hi[k] = fmaxf(hi[k], s.coordinates[2 * v + k]);
					}
				}
				platform_log("h3 sky uv: mesh %ld part %ld %s: u %.3f..%.3f v %.3f..%.3f", i, p,
					shader != NONE ? build->shaders[shader].path : "-", lo[0], hi[0], lo[1], hi[1]);
			}
		}
		/* (each hazed map's sky vertices: their scattering from the level's
		middle, summed - the sky model's own coordinates, Halo 3's sky object
		at the origin) */
		{
			long p, t;

			for (p = 0; p < s.part_count; p++)
			{
				short shader = s.parts[p].material >= 0 && s.parts[p].material < material_count ? shaders[s.parts[p].material] : NONE;
				struct bitmap *hb = shader != NONE && build->shaders[shader].bitmap != NONE ? &build->bitmaps[build->shaders[shader].bitmap] : NULL;

				if (!hb || !hb->hazed)
					continue;
				for (t = 0; t < s.parts[p].count; t++)
				{
					long v = s.indices[s.parts[p].first + t];
					float extinction[3], inscatter[3];
					int k;

					atmosphere_scatter(&build->atmosphere_sky, build->view_point, s.positions + 3 * v, extinction, inscatter);
					/* (a cloud's vertices' colours: what lights it) */
					if (hb->cloud && s.colours)
						hb->haze_coloured = TRUE;
					for (k = 0; k < 3; k++)
					{
						hb->haze_sum[k] += extinction[k] * (hb->cloud && s.colours ? s.colours[3 * v + k] : 1.0f);
						hb->haze_sum[3 + k] += inscatter[k];
					}
					hb->haze_sum[6] += 1.0;
				}
			}
		}
		if (getenv("HALO_H3_SKY_GEOMETRY"))
			geometry_add(build, &s, cluster, matrix, page_bright(build));
		else
		{
			/* (this engine's sky: a sky tag's model, drawn about the camera at
			a 1024th - render_sky.c - behind every cluster that has it) */
			struct model_made mesh;

			if (model_make(build, &s, env_float("HALO_H3_SKY_MODEL_SCALE", 1.0f), &mesh, 128.0f))
			{
				struct model_part_made *grown = realloc(build->sky_model.parts,
					(build->sky_model.part_count + mesh.part_count) * sizeof(*grown));

				if (grown)
				{
					memcpy(grown + build->sky_model.part_count, mesh.parts, mesh.part_count * sizeof(*grown));
					build->sky_model.parts = grown;
					build->sky_model.part_count += mesh.part_count;
					if (mesh.scale > build->sky_model.scale)
						build->sky_model.scale = mesh.scale;
					free(mesh.parts);
				}
				else
					model_free(&mesh);
			}
		}
		section_free(&s);
	}
	build->material_shader = saved_shaders;
	build->material_count = saved_count;
	free(shaders);
}

/* whether a point is in one of the level's leaves (not solid nor outside):
the walk of this engine's bsp3d_test_point over the Halo 3 collision BSP's
3D nodes (h3_collision_sizes: plane at 0, back and front children of 24
bits at 2 and 5, the top bit a leaf, all ones none) */
H3_ALIGN static unsigned long slot_group(long slot);

H3_ALIGN static boolean point_in_level(struct build const *build, float const p[3])
{
	struct collision_bsp const *bsp = &build->collision;
	unsigned long node = 0;
	int depth = 0;

	if (!bsp->counts[0] || !bsp->arrays[0] || !bsp->arrays[1])
		return TRUE;
	for (;;)
	{
		unsigned char const *n;
		unsigned char const *plane;
		unsigned short plane_index;
		float d;

		if (node >= (unsigned long)bsp->counts[0] || depth++ > 256)
			return FALSE;
		if (bsp->large)
		{
			unsigned long large_plane;

			/* (ODST's large collision BSP: longs, the top bit a leaf) */
			n = bsp->arrays[0] + 12 * node;
			large_plane = read32(n) & 0x7FFFFFFFUL;
			if (large_plane >= (unsigned long)bsp->counts[1])
				return FALSE;
			plane = bsp->arrays[1] + 16 * large_plane;
			d = readf(plane) * p[0] + readf(plane + 4) * p[1] + readf(plane + 8) * p[2] - readf(plane + 12);
			node = read32(n + (d >= 0.0f ? 8 : 4));
			if (node == 0xFFFFFFFFUL)
				return FALSE;
			if (node & 0x80000000UL)
				return TRUE;
			continue;
		}
		n = bsp->arrays[0] + 8 * node;
		plane_index = (unsigned short)read16(n);
		if (plane_index >= bsp->counts[1])
			return FALSE;
		plane = bsp->arrays[1] + 16 * plane_index;
		d = readf(plane) * p[0] + readf(plane + 4) * p[1] + readf(plane + 8) * p[2] - readf(plane + 12);
		node = d >= 0.0f ? (n[5] | (n[6] << 8) | ((unsigned long)n[7] << 16)) : (n[2] | (n[3] << 8) | ((unsigned long)n[4] << 16));
		if (node == 0xFFFFFF)
			return FALSE;
		if (node & 0x800000)
			return TRUE;
	}
}

/* an instance's placement's yaw, pitch and roll: those of which this
engine's matrix4x3_rotation_from_angles (vectors3d_from_euler_angles3d,
which places an object) makes the instance's forward, left and up again -
its forward (cy cp, sy cr - cy sp sr, cy sp cr + sy sr), left (-sy cp,
sy sp sr + cy cr, cy sr - sy sp cr), up (-sp, -cp sr, cp cr); at a pitch
of a quarter turn (up along x) no roll, the yaw from forward's y and left's */
H3_ALIGN static void instance_angles(struct instance const *instance, float angles[3])
{
	float const *f = instance->matrix, *l = instance->matrix + 3, *u = instance->matrix + 6;

	angles[1] = asinf(-u[0] > 1.0f ? 1.0f : -u[0] < -1.0f ? -1.0f : -u[0]);
	if (sqrtf(u[1] * u[1] + u[2] * u[2]) < 0.01f)
	{
		angles[0] = atan2f(f[1], l[1]);
		angles[2] = 0.0f;
	}
	else
	{
		angles[0] = atan2f(-l[0], f[0]);
		angles[2] = atan2f(-u[1], u[2]);
	}
}

/* whether an instance placed as an object would be placed by this engine:
its origin, or its key's centre where object_types_place_objects puts it
(matrix4x3_rotation_from_angles of the placement's angles, as it is), in a
leaf */
H3_ALIGN static boolean instance_placeable(struct build const *build, struct instance const *instance,
	struct object_key const *key)
{
	float a[3], n[3][3], centre[3];
	float cr, sr, cp, sp, cy, sy;
	int k;

	if (point_in_level(build, instance->matrix + 9))
		return TRUE;
	instance_angles(instance, a);
	cy = cosf(a[0]); sy = sinf(a[0]); cp = cosf(a[1]); sp = sinf(a[1]); cr = cosf(a[2]); sr = sinf(a[2]);
	n[0][0] = cy * cp; n[0][1] = sy * cr - cy * sp * sr; n[0][2] = cy * sp * cr + sy * sr;
	n[1][0] = -(sy * cp); n[1][1] = sy * sp * sr + cy * cr; n[1][2] = cy * sr - sy * sp * cr;
	n[2][0] = -sp; n[2][1] = -(cp * sr); n[2][2] = cp * cr;
	for (k = 0; k < 3; k++)
		centre[k] = instance->matrix[9 + k] + n[0][k] * key->centre[0] + n[1][k] * key->centre[1] + n[2][k] * key->centre[2];
	return point_in_level(build, centre);
}

/* the decorators kept: each set's variants made sections (in the model's
space), its placements in a grid of cells (the side grown until one a cell
fits the set's triangles), the first of each cell placed */
H3_ALIGN static void decorators_add(struct build *build)
{
	struct h3_cache *cache = build->cache;
	long cluster_count, d;
	unsigned long clusters;
	float light[3];
	struct placed_page page;

	if (!build->decorator_count || !build->decorator_buffers_open ||
		!h3_cache_block(cache, build->sbsp + H3_SBSP_CLUSTERS, H3_CLUSTER_SIZE, &cluster_count, &clusters))
	{
		return;
	}
	light[0] = light[1] = light[2] = env_float("HALO_H3_DECORATOR_LIGHT", 0.6f);
	page = patch_place(build, light);
	if (page.atlas == NONE)
		return;
	for (d = 0; d < build->decorator_count; d++)
	{
		struct decorator_set *set = &build->decorators[d];
		struct geometry *g = &set->geometry;
		unsigned char mesh[H3_MESH_SIZE];
		struct vertex_buffer *vb;
		float bounds[10];
		long subpart_count, pass, i, j;
		unsigned long subparts;
		struct section variants[16];
		long variant_count = 0, variant_triangles = 0;
		float cell = 0.1f;
		short *saved_shaders = build->material_shader;
		long saved_count = build->material_count;
		short shader = set->shader;

		memset(variants, 0, sizeof(variants));
		if (!g->mesh_count || !h3_cache_read(cache, g->meshes, H3_MESH_SIZE, mesh) ||
			read16(mesh + 0x18) < 0 || read16(mesh + 0x18) >= g->vertex_buffer_count || !g->compression_count)
		{
			continue;
		}
		vb = &g->vertex_buffers[read16(mesh + 0x18)];
		if (!vb->data || vb->stride < 12)
			continue;
		for (i = 0; i < 10; i++)
			bounds[i] = h3_cache_readf(cache, g->compression + 4 + 4 * i);
		if (!h3_cache_block(cache, g->meshes + 0xC, 8, &subpart_count, &subparts))
			subpart_count = 0;
		/* the variants: a subpart's strip each */
		for (i = 0; i < subpart_count && variant_count < 16; i++)
		{
			unsigned char const *sp = h3_cache_at(cache, subparts + 8 * i, 8);
			long first = sp ? (unsigned short)read16(sp) : 0, count = sp ? (unsigned short)read16(sp + 2) : 0, k, used = 0;
			struct section *s = &variants[variant_count];

			if (count < 3 || first + count > vb->count)
				continue;
			s->vertex_count = count;
			s->positions = malloc(count * 12);
			s->coordinates = malloc(count * 8);
			s->packed = calloc(count, 12);
			s->indices = malloc(count * 3 * sizeof(unsigned short));
			s->parts = calloc(1, sizeof(*s->parts));
			if (!s->positions || !s->coordinates || !s->packed || !s->indices || !s->parts)
			{
				section_free(s);
				continue;
			}
			for (k = 0; k < count; k++)
			{
				unsigned char const *x = vb->data + (unsigned long)vb->stride * (first + k);
				int c;

				for (c = 0; c < 3; c++)
					s->positions[3 * k + c] = bounds[2 * c] + (unsigned short)read16(x + 2 * c) / 65535.0f * (bounds[2 * c + 1] - bounds[2 * c]);
				for (c = 0; c < 2; c++)
					s->coordinates[2 * k + c] = bounds[6 + 2 * c] + (unsigned short)read16(x + 8 + 2 * c) / 65535.0f * (bounds[7 + 2 * c] - bounds[6 + 2 * c]);
			}
			for (k = 0; k + 2 < count; k++)
			{
				float const *a = s->positions + 3 * k, *b = a + 3, *c = a + 6;

				if (!memcmp(a, b, 12) || !memcmp(b, c, 12) || !memcmp(a, c, 12))
					continue;
				s->indices[used++] = (unsigned short)k;
				s->indices[used++] = (unsigned short)((k & 1) ? k + 2 : k + 1);
				s->indices[used++] = (unsigned short)((k & 1) ? k + 1 : k + 2);
			}
			s->index_count = used;
			s->part_count = 1;
			s->parts[0].material = 0;
			s->parts[0].first = 0;
			s->parts[0].count = used;
			section_frame(s);
			variant_triangles += used / 3;
			variant_count++;
		}
		if (!variant_count)
			continue;
		variant_triangles /= variant_count;
		if (variant_triangles < 1)
			variant_triangles = 1;
		/* two passes: the cells' side, then the placing */
		build->material_shader = &shader;
		build->material_count = 1;
		for (pass = 0; pass < 2; pass++)
		{
			for (;;)
			{
				long kept = 0, taken_capacity = 4096, taken_count = 0;
				long *taken = malloc(taken_capacity * sizeof(long));

				set->total = 0;
				for (i = 0; i < cluster_count && taken; i++)
				{
					long group_count;
					unsigned long groups;

					if (!h3_cache_block(cache, clusters + H3_CLUSTER_SIZE * i + 0xE8, 0x3C, &group_count, &groups))
						continue;
					for (j = 0; j < group_count; j++)
					{
						unsigned char e[0x24];
						struct vertex_buffer *pb;
						long n, count, offset;

						if (!h3_cache_read(cache, groups + 0x3C * j, 0x24, e) || (signed char)e[2] != set->set_index ||
							e[3] >= build->decorator_buffers.vertex_buffer_count)
						{
							continue;
						}
						pb = &build->decorator_buffers.vertex_buffers[e[3]];
						count = (unsigned short)read16(e);
						offset = (long)read32(e + 4) / 16;   /* (in bytes) */
						for (n = 0; n < count && pb->data && (unsigned long)(offset + n + 1) * 16 <= pb->size; n++)
						{
							unsigned char const *x = pb->data + 16 * (offset + n);
							float position[3], q[4], length = 0.0f, scale;
							long key, t;
							boolean seen = FALSE;
							int c;

							set->total++;
							for (c = 0; c < 3; c++)
								position[c] = readf(e + 8 + 4 * c) + (unsigned short)read16(x + 2 * c) * readf(e + 0x18 + 4 * c);
							key = ((long)floorf(position[0] / cell) * 73856093L) ^ ((long)floorf(position[1] / cell) * 19349663L) ^
								((long)floorf(position[2] / cell) * 83492791L);
							for (t = 0; t < taken_count && !seen; t++)
								seen = taken[t] == key;
							if (seen)
								continue;
							if (taken_count >= taken_capacity)
							{
								long *more = realloc(taken, 2 * taken_capacity * sizeof(long));

								if (!more)
									continue;
								taken = more;
								taken_capacity *= 2;
							}
							taken[taken_count++] = key;
							kept++;
							if (pass == 0)
								continue;
							/* placed: its quaternion's frame, its scale */
							for (c = 0; c < 4; c++)
							{
								q[c] = (x[8 + c] - 128.0f) / 127.5f;
								length += q[c] * q[c];
							}
							length = sqrtf(length);
							if (length < 0.01f)
								q[0] = q[1] = q[2] = 0.0f, q[3] = 1.0f, length = 1.0f;
							for (c = 0; c < 4; c++)
								q[c] /= length;
							scale = getenv("HALO_H3_DECORATOR_NO_SCALE") ? 1.0f : x[15] / 128.0f;
							if (!(scale > 0.2f && scale < 4.0f))
								scale = 1.0f;
							{
								float matrix[13];
								struct section const *s = &variants[x[7] % variant_count];
								float qx = q[0], qy = q[1], qz = q[2], qw = q[3];

								/* forward, left, up: the rotation's columns */
								matrix[0] = 1 - 2 * (qy * qy + qz * qz); matrix[1] = 2 * (qx * qy + qz * qw); matrix[2] = 2 * (qx * qz - qy * qw);
								matrix[3] = 2 * (qx * qy - qz * qw); matrix[4] = 1 - 2 * (qx * qx + qz * qz); matrix[5] = 2 * (qy * qz + qx * qw);
								matrix[6] = 2 * (qx * qz + qy * qw); matrix[7] = 2 * (qy * qz - qx * qw); matrix[8] = 1 - 2 * (qx * qx + qy * qy);
								memcpy(matrix + 9, position, 12);
								matrix[12] = scale;
								geometry_add(build, s, cluster_of_room(build, position, s->index_count / 3), matrix, &page);
								set->placed++;
								build->decorator_triangles += s->index_count / 3;
							}
						}
					}
				}
				free(taken);
				if (pass == 1 || kept * variant_triangles <= set->budget || cell > 64.0f)
					break;
				cell *= 1.25f;
			}
		}
		build->material_shader = saved_shaders;
		build->material_count = saved_count;
		for (i = 0; i < variant_count; i++)
			section_free(&variants[i]);
		platform_log("h3 level: decorators: %s: %ld of %ld placed (one a %.2f-unit cell), %ld triangles a placement",
			set->name, set->placed, set->total, cell, variant_triangles);
	}
}

/* the decorators' grass (thick_grass_green, tall_grass) as this engine's
detail objects (the owner's choice: its camera-facing sprites stand in for
Halo 3's grass models): each set a detail object collection of one type,
its decorator's texture the sprite; each kept placement a detail object in
its 8-unit cell (position a byte of the cell each way: this engine's
default z reference vector, z its byte over 255 plus the cell's), its colour
the placement's (bytes 12-14, R5G6B5); thinned to HALO_H3_GRASS placements
- set it: off by default, not ready: here a sprite under HALO_H3_GRASS_SIZE 2
is not drawn and one of 2 or more stands over a unit tall, a wall of grass
where Halo 3 has ankle-high tufts, and a quad of sky shows above the
windmill's cliff - the detail objects' drawing, nv2a_vsh.c's emulation of
the detail object vertex shader, is to be looked into) as the decorators
are, one a cell of a grid. Sorted by cell (x, y, z) as structure_detail_objects.c finds them,
each cell's layers' objects in its layers' order, a count a layer */
struct grass_item
{
	short cell[3];
	unsigned char position[3];
	unsigned char layer;
	unsigned short color;
};

H3_ALIGN static int grass_item_compare(void const *a, void const *b)
{
	struct grass_item const *x = a, *y = b;
	int k;

	for (k = 0; k < 3; k++)
	{
		if (x->cell[k] != y->cell[k])
			return x->cell[k] < y->cell[k] ? -1 : 1;
	}
	return (int)x->layer - (int)y->layer;
}

H3_ALIGN static void grass_gather(struct build *build)
{
	struct h3_cache *cache = build->cache;
	long cluster_count, i, j, g, kept_total = 0, total = 0;
	unsigned long clusters;
	long budget = getenv("HALO_H3_GRASS") ? atol(getenv("HALO_H3_GRASS")) : 0;
	struct grass_item *items = NULL;
	long item_count = 0, item_capacity = 0;
	float cell_side = 0.15f;

	if (!build->grass_set_count || budget <= 0 || !build->decorator_buffers_open ||
		!h3_cache_block(cache, build->sbsp + H3_SBSP_CLUSTERS, H3_CLUSTER_SIZE, &cluster_count, &clusters))
	{
		return;
	}
	/* the grid's side: grown until the kept fit the budget */
	for (;;)
	{
		/* (the cells taken: an open-addressed set of their keys) */
		enum { TAKEN_SIZE = 1 << 16 };
		long *taken = calloc(TAKEN_SIZE, sizeof(long));
		unsigned char *used = calloc(TAKEN_SIZE, 1);

		item_count = 0;
		total = 0;
		for (g = 0; g < build->grass_set_count && taken && used; g++)
		{
			memset(used, 0, TAKEN_SIZE);
			for (i = 0; i < cluster_count; i++)
			{
				long group_count;
				unsigned long groups;

				if (!h3_cache_block(cache, clusters + H3_CLUSTER_SIZE * i + 0xE8, 0x3C, &group_count, &groups))
					continue;
				for (j = 0; j < group_count; j++)
				{
					unsigned char e[0x24];
					struct vertex_buffer *pb;
					long n, count, offset;

					if (!h3_cache_read(cache, groups + 0x3C * j, 0x24, e) ||
						(signed char)e[2] != build->grass_sets[g].set_index ||
						e[3] >= build->decorator_buffers.vertex_buffer_count)
					{
						continue;
					}
					pb = &build->decorator_buffers.vertex_buffers[e[3]];
					count = (unsigned short)read16(e);
					offset = (long)read32(e + 4) / 16;
					for (n = 0; n < count && pb->data && (unsigned long)(offset + n + 1) * 16 <= pb->size; n++)
					{
						unsigned char const *x = pb->data + 16 * (offset + n);
						float position[3];
						long key;
						unsigned long slot;
						boolean seen = FALSE;
						int c;

						total++;
						for (c = 0; c < 3; c++)
							position[c] = readf(e + 8 + 4 * c) + (unsigned short)read16(x + 2 * c) * readf(e + 0x18 + 4 * c);
						key = ((long)floorf(position[0] / cell_side) * 73856093L) ^ ((long)floorf(position[1] / cell_side) * 19349663L) ^
							((long)floorf(position[2] / cell_side) * 83492791L);
						for (slot = (unsigned long)key * 2654435761UL % TAKEN_SIZE; used[slot]; slot = (slot + 1) % TAKEN_SIZE)
						{
							if (taken[slot] == key)
							{
								seen = TRUE;
								break;
							}
						}
						if (seen)
							continue;
						used[slot] = 1;
						taken[slot] = key;
						if (item_count >= item_capacity)
						{
							long capacity = item_capacity ? 2 * item_capacity : 4096;
							struct grass_item *more = realloc(items, capacity * sizeof(*more));

							if (!more)
								continue;
							items = more;
							item_capacity = capacity;
						}
						{
							struct grass_item *item = &items[item_count++];

							for (c = 0; c < 3; c++)
							{
								float in = position[c] / 8.0f;
								float whole = floorf(in);
								long byte = (long)((in - whole) * 255.0f + 0.5f);

								if (byte > 255)
									byte = 255;
								item->cell[c] = (short)whole;
								item->position[c] = (unsigned char)byte;
							}
							item->layer = (unsigned char)g;
							item->color = (unsigned short)(((x[12] >> 3) << 11) | ((x[13] >> 2) << 5) | (x[14] >> 3));
						}
					}
				}
			}
		}
		free(taken);
		free(used);
		if (item_count <= budget || cell_side > 64.0f)
			break;
		cell_side *= 1.2f;
	}
	if (!item_count)
	{
		free(items);
		return;
	}
	qsort(items, (size_t)item_count, sizeof(*items), grass_item_compare);
	build->grass_cells = calloc(item_count + 1, sizeof(*build->grass_cells));
	build->grass_objects = malloc(6 * (unsigned long)item_count);
	build->grass_counts = calloc(item_count + 1, sizeof(unsigned short));
	if (!build->grass_cells || !build->grass_objects || !build->grass_counts)
	{
		free(items);
		return;
	}
	for (i = 0; i < item_count; i++)
	{
		struct grass_item const *item = &items[i];
		struct grass_cell *cell = build->grass_cell_count ? &build->grass_cells[build->grass_cell_count - 1] : NULL;
		unsigned char *o = build->grass_objects + 6 * i;

		if (!cell || cell->x != item->cell[0] || cell->y != item->cell[1] || cell->z != item->cell[2])
		{
			cell = &build->grass_cells[build->grass_cell_count++];
			cell->x = item->cell[0];
			cell->y = item->cell[1];
			cell->z = item->cell[2];
			cell->layers = 0;
			cell->start = i;
			cell->count_index = build->grass_count_count;
		}
		if (!(cell->layers & (1UL << item->layer)))
		{
			cell->layers |= 1UL << item->layer;
			build->grass_counts[build->grass_count_count++] = 0;
		}
		build->grass_counts[build->grass_count_count - 1]++;
		o[0] = item->position[0];
		o[1] = item->position[1];
		o[2] = item->position[2];
		o[3] = 0;   /* (type 0, sprite 0) */
		write16(o + 4, item->color);
		kept_total++;
	}
	build->grass_object_count = item_count;
	free(items);
	platform_log("h3 level: grass: %ld of %ld placements as detail objects (one a %.2f-unit cell) in %ld cells, %ld sets",
		kept_total, total, cell_side, build->grass_cell_count, build->grass_set_count);
}

/* (level_make_fitted: the budgets of a make again, its BSP too big for the
tag cache's room after its tags; 0 the defaults - and first the per-vertex
light averaged an instance, its vertices shared) */
static long fit_vertex_budget, fit_surface_budget;
static boolean fit_shared_vertex_light;

H3_ALIGN static boolean level_gather(struct build *build)
{
	struct h3_cache *cache = build->cache;
	long count, i, instance_count;
	unsigned long elements, instances, lbsp_instances = 0, per_vertex = 0;
	long lbsp_instance_count = 0, per_vertex_count = 0;
	struct section *definition_sections;
	boolean *definition_read;
	struct instance_order *order;
	long budget = fit_vertex_budget ? fit_vertex_budget :
		getenv("HALO_H3_VERTEX_BUDGET") ? atol(getenv("HALO_H3_VERTEX_BUDGET")) : DEFAULT_VERTEX_BUDGET;
	long surface_budget = fit_surface_budget ? fit_surface_budget :
		getenv("HALO_H3_SURFACE_BUDGET") ? atol(getenv("HALO_H3_SURFACE_BUDGET")) : DEFAULT_SURFACE_BUDGET;
	long sky_cluster = 0, sky_cluster_size = -1;
	boolean *modelled = NULL;

	/* leaves' clusters */
	if (!h3_cache_block(cache, build->sbsp + H3_SBSP_LEAF_CLUSTERS, 1, &build->leaf_count, &elements))
		return build_fail(build, "its leaves are damaged"), FALSE;
	build->leaf_cluster = malloc((build->leaf_count + 1) * sizeof(short));
	if (!build->leaf_cluster)
		return build_fail(build, "out of memory"), FALSE;
	for (i = 0; i < build->leaf_count; i++)
	{
		unsigned char const *b = h3_cache_at(cache, elements + i, 1);

		build->leaf_cluster[i] = b ? (short)(signed char)b[0] : 0;
	}

	/* the lighting */
	build->lightmap_scale = env_float("HALO_H3_LIGHTMAP_SCALE", 1.0f);
	build->exposure_set = getenv("HALO_H3_LIGHTMAP_SCALE") != NULL;
	/* (the level's camera fx: its exposure's stops and target - the one
	that is not globals\default, else that) */
	{
		long n, chosen = NONE;
		struct h3_cache_tag cfxs;

		for (n = 0; n < h3_cache_tag_count(cache); n++)
		{
			if (h3_cache_tag_get(cache, n, &cfxs) && cfxs.group_tag == 'cfxs' &&
				(chosen == NONE || strcmp(h3_cache_tag_name(cache, n), "globals\\default")))
			{
				chosen = n;
			}
		}
		if (chosen != NONE && h3_cache_tag_get(cache, chosen, &cfxs))
		{
			build->exposure_range[0] = h3_cache_readf(cache, cfxs.address + 0x10);
			build->exposure_range[1] = h3_cache_readf(cache, cfxs.address + 0x14);
			build->exposure_range[2] = h3_cache_readf(cache, cfxs.address + 0x18);
			build->exposure_known = build->exposure_range[0] <= build->exposure_range[1] &&
				build->exposure_range[1] - build->exposure_range[0] < 32.0f && fabsf(build->exposure_range[2]) < 32.0f;
			platform_log("h3 level: camera fx %s: exposure stops %.2f..%.2f, target %.3f", h3_cache_tag_name(cache, chosen),
				build->exposure_range[0], build->exposure_range[1], build->exposure_range[2]);
		}
		/* (h3/light-1) the whole camera fx for the frame pass (h3_frame.c). The
		tag's layout from the kit's own tag (its field names): exposure 0x00
		(flags, stops, maximum change, blend speed, minimum, maximum, the
		auto-exposure screen brightness - log2 in the map -, delay),
		sensitivity 0x20, anti-bloom 0x28, bloom point 0x30, inherent bloom
		0x40, bloom intensity 0x50 (flags, value, maximum change, blend
		speed each), the large / medium / small bloom colors 0x60 0x70 0x80
		(flags, r g b). A value whose flags' bit 0 is set is globals\default's */
		{
			struct h3_frame_level *f = h3_frame_level();
			struct h3_cache_tag own, standard;
			boolean have_own = chosen != NONE && h3_cache_tag_get(cache, chosen, &own);
			boolean have_standard = FALSE;
			int c, k;

			memset(f, 0, sizeof(*f));
			for (n = 0; n < h3_cache_tag_count(cache); n++)
			{
				if (h3_cache_tag_get(cache, n, &standard) && standard.group_tag == 'cfxs' &&
					!strcmp(h3_cache_tag_name(cache, n), "globals\\default"))
				{
					have_standard = TRUE;
					break;
				}
			}
			if (have_own && build->exposure_known)
			{
#define CFXS_AT(offset) ((h3_cache_read32(cache, own.address + (offset)) & 1) && have_standard ? standard.address + (offset) : own.address + (offset))
				f->exposure_flags = (int)h3_cache_read32(cache, own.address);
				f->exposure_stops = h3_cache_readf(cache, CFXS_AT(0x00) + 0x04);
				f->exposure_maximum_change = h3_cache_readf(cache, CFXS_AT(0x00) + 0x08);
				f->exposure_blend_speed = h3_cache_readf(cache, CFXS_AT(0x00) + 0x0C);
				f->exposure_minimum = h3_cache_readf(cache, CFXS_AT(0x00) + 0x10);
				f->exposure_maximum = h3_cache_readf(cache, CFXS_AT(0x00) + 0x14);
				f->exposure_target = h3_cache_readf(cache, CFXS_AT(0x00) + 0x18);
				f->exposure_delay = h3_cache_readf(cache, CFXS_AT(0x00) + 0x1C);
				f->sensitivity = h3_cache_readf(cache, CFXS_AT(0x20) + 4);
				f->anti_bloom = h3_cache_readf(cache, CFXS_AT(0x28) + 4);
				f->bloom_point = h3_cache_readf(cache, CFXS_AT(0x30) + 4);
				f->bloom_inherent = h3_cache_readf(cache, CFXS_AT(0x40) + 4);
				f->bloom_intensity = h3_cache_readf(cache, CFXS_AT(0x50) + 4);
				for (c = 0; c < 3; c++)
					for (k = 0; k < 3; k++)
						f->bloom_colors[c][k] = h3_cache_readf(cache, CFXS_AT(0x80 - 0x10 * c) + 4 + 4 * k);
#undef CFXS_AT
				f->valid = TRUE;
				if (h3_frame_enabled())
					build->sky_exposure = exp2f(f->exposure_minimum) * env_float("HALO_H3_SKY_BAKE", 0.5f);
				platform_log("h3 level: camera fx: exposure flags %x, %.2f stops (%.2f..%.2f, change %.3f, blend %.3f, delay %.3f), sensitivity %.2f, anti-bloom %.2f; bloom point %.2f, inherent %.2f, intensity %.2f, small %.2f %.2f %.2f, medium %.2f %.2f %.2f, large %.2f %.2f %.2f",
					f->exposure_flags, f->exposure_stops, f->exposure_minimum, f->exposure_maximum, f->exposure_maximum_change, f->exposure_blend_speed,
					f->exposure_delay, f->sensitivity, f->anti_bloom, f->bloom_point, f->bloom_inherent, f->bloom_intensity,
					f->bloom_colors[0][0], f->bloom_colors[0][1], f->bloom_colors[0][2], f->bloom_colors[1][0], f->bloom_colors[1][1], f->bloom_colors[1][2],
					f->bloom_colors[2][0], f->bloom_colors[2][1], f->bloom_colors[2][2]);
			}
		}
	}
	for (i = 0; i < 5; i++)
		build->lightprobe_ranges[i] = h3_cache_readf(cache, build->lbsp + 8 + 24 * i);
	light_gamma = env_float("HALO_H3_LIGHTMAP_GAMMA", 1.0f);
	light_texel_begin();
	build->vertex_light_scale = env_float("HALO_H3_VERTEX_LIGHT_SCALE", 6.0f);
	lightprobe_place(build);
	page_neutral(build);
	if (getenv("HALO_H3_DEBUG"))
		fprintf(stderr, "h3 level: lightprobe %s, atlas %d at %ld %ld size %ld, atlases %d\n", build->lightprobe_slices ? "read" : "NOT READ",
			(int)build->lightprobe.atlas, build->lightprobe.x, build->lightprobe.y, build->lightprobe.w, (int)build->atlas_count);
	h3_cache_block(cache, build->lbsp + H3_LBSP_INSTANCES, 8, &lbsp_instance_count, &lbsp_instances);
	h3_cache_block(cache, build->lbsp + H3_LBSP_PER_VERTEX, 0x10, &per_vertex_count, &per_vertex);

	/* clusters */
	if (!h3_cache_block(cache, build->sbsp + H3_SBSP_CLUSTERS, H3_CLUSTER_SIZE, &build->cluster_count, &build->clusters) ||
		build->cluster_count > 512 || build->cluster_count < 1)
	{
		return build_fail(build, "its clusters are damaged or too many"), FALSE;
	}
	for (i = 0; i < build->cluster_count && !build->result->why[0]; i++)
	{
		struct section s;
		short mesh = h3_cache_read16(cache, build->clusters + H3_CLUSTER_SIZE * i + 0xD8);
		struct placed_page const *page = build->lightprobe.atlas != NONE && build->lightprobe.w ? &build->lightprobe :
			page_neutral(build);

		if (section_read(build, &build->bsp_geometry, mesh, &s))
		{
			if (s.vertex_count > sky_cluster_size)
			{
				sky_cluster_size = s.vertex_count;
				sky_cluster = i;
			}
			if (getenv("HALO_H3_DEBUG"))
				fprintf(stderr, "h3 level: cluster %ld: mesh %d, %ld vertices, %ld indices, %ld parts%s\n", i, mesh,
					s.vertex_count, s.index_count, s.part_count, s.lightmap_coordinates ? ", lightmapped" : "");
			geometry_add(build, &s, (short)i, NULL, s.lightmap_coordinates ? page : page_neutral(build));
			section_free(&s);
		}
	}

	/* the sky, in the largest cluster */
	if (!getenv("HALO_H3_NO_SKY"))
	{
		unsigned char const *b = h3_cache_at(cache, build->sbsp + H3_SBSP_WORLD_BOUNDS, 24);
		float bounds[6];

		if (b)
		{
			memcpy(bounds, b, sizeof(bounds));
			sky_add(build, (short)sky_cluster, bounds);
		}
	}

	/* instanced geometry: the largest first, while the budget lasts */
	if (!h3_cache_block(cache, build->sbsp + H3_SBSP_INSTANCES, H3_INSTANCE_SIZE, &instance_count, &instances))
		instance_count = 0;
	build->instances = calloc(instance_count + 1, sizeof(*build->instances));
	order = calloc(instance_count + 1, sizeof(*order));
	definition_sections = calloc(build->bsp_geometry.mesh_count + 1, sizeof(*definition_sections));
	definition_read = calloc(build->bsp_geometry.mesh_count + 1, sizeof(*definition_read));
	if (!build->instances || !order || !definition_sections || !definition_read)
	{
		free(order);
		free(definition_sections);
		free(definition_read);
		return build_fail(build, "out of memory"), FALSE;
	}
	for (i = 0; i < instance_count; i++)
	{
		unsigned char instance[H3_INSTANCE_SIZE];

		order[i].index = i;
		order[i].size = 0.0f;
		if (h3_cache_read(cache, instances + H3_INSTANCE_SIZE * i, H3_INSTANCE_SIZE, instance))
		{
			int k;

			order[i].size = readf(instance + 0x4C);
			build->instances[i].definition = read16(instance + 0x34);
			for (k = 0; k < 12; k++)
				build->instances[i].matrix[k] = readf(instance + 4 + 4 * k);
			build->instances[i].matrix[12] = readf(instance);
		}
		else
			build->instances[i].definition = NONE;
	}
	build->instance_count = instance_count;
	/* each definition's uses: one placed twice or more is made a model (its
	geometry once, an object at each place: HALO_H3_MODEL_MINIMUM, 2 by
	default; 0 none), once in the BSP */
	{
		long *uses = calloc(build->bsp_geometry.mesh_count + 1, sizeof(long));
		long minimum = getenv("HALO_H3_MODEL_MINIMUM") ? atol(getenv("HALO_H3_MODEL_MINIMUM")) : 2;

		modelled = calloc(build->bsp_geometry.mesh_count + 1, sizeof(boolean));
		if (!uses || !modelled)
		{
			free(uses);
			free(order);
			free(definition_sections);
			free(definition_read);
			return build_fail(build, "out of memory"), FALSE;
		}
		for (i = 0; i < instance_count; i++)
		{
			short d = build->instances[i].definition;

			if (d >= 0 && d < build->bsp_geometry.mesh_count)
				uses[d]++;
		}
		for (i = 0; i < build->bsp_geometry.mesh_count; i++)
			modelled[i] = minimum > 0 && uses[i] >= minimum;
		free(uses);
	}
	for (i = 0; i < instance_count; i++)
	{
		struct instance *instance = &build->instances[i];

		instance->key = instance->definition >= 0 && instance->definition < build->bsp_geometry.mesh_count ?
			object_key(build, instance->definition, scale_key(instance->matrix[12])) : NONE;
		if (instance->key != NONE)
			build->keys[instance->key].uses++;
	}
	qsort(order, instance_count, sizeof(*order), instance_order_compare);
	for (i = 0; i < instance_count && !build->result->why[0]; i++)
	{
		long index = order[i].index;
		struct instance *instance = &build->instances[index];
		short definition = instance->definition;
		struct section *s;
		struct placed_page patch;
		struct placed_page const *page = page_neutral(build);
		float centre[3] = { 0.0f, 0.0f, 0.0f };
		float const *per_vertex_sh = NULL;
		long v;
		int k;

		if (definition < 0 || definition >= build->bsp_geometry.mesh_count)
			continue;
		s = &definition_sections[definition];
		if (!definition_read[definition])
		{
			definition_read[definition] = TRUE;
			section_read(build, &build->bsp_geometry, definition, s);
		}
		/* (a model's: placed as an object) */
		instance->placed = TRUE;
		if (modelled[definition])
		{
#if !defined(HALO_VITA) && !defined(HALO_GXM_NULL)
			/* (the object's light, h3_object_light.c: Halo 3 lights the
			instance by its own per-vertex lighting - the mean of its
			vertices' intensity and coefficients kept, the light field they
			average to) */
			if (index < lbsp_instance_count && s->vertex_count)
			{
				unsigned char const *l = h3_cache_at(cache, lbsp_instances + 8 * index, 8);
				short block = l ? read16(l + 2) : NONE;
				long vb = block >= 0 && block < per_vertex_count ? (long)h3_cache_read32(cache, per_vertex + 0x10 * block + 0xC) : NONE;

				if (vb >= 0 && vb < build->bsp_geometry.vertex_buffer_count && build->bsp_geometry.vertex_buffers[vb].light_done &&
					build->bsp_geometry.vertex_buffers[vb].sh && build->bsp_geometry.vertex_buffers[vb].count >= s->vertex_count)
				{
					float *grown = realloc(build->instance_probes, (size_t)(build->instance_probe_count + 1) * 18 * sizeof(float));

					if (grown)
					{
						float *o = grown + 18 * build->instance_probe_count++;
						float const *sh = build->bsp_geometry.vertex_buffers[vb].sh;
						long n;

						build->instance_probes = grown;
						memcpy(o, instance->matrix + 9, 12);
						for (k = 0; k < 15; k++)
							o[3 + k] = 0.0f;
						for (n = 0; n < s->vertex_count; n++)
							for (k = 0; k < 15; k++)
								o[3 + k] += sh[15 * n + k] / (float)s->vertex_count;
						/* (one light an object where Halo 3 has one a vertex: a
						tree's lit crown would brighten its shaded trunk. The mean
						is scaled so that it draws as bright as the vertices do on
						average - the screen shows the light's square root: the
						square of the vertices' mean root over their mean.
						HALO_H3_OBJECT_LIGHT_MEAN=1: the plain mean) */
						if (!getenv("HALO_H3_OBJECT_LIGHT_MEAN"))
						{
							double mean = 0.0, mean_root = 0.0;

							for (n = 0; n < s->vertex_count; n++)
							{
								float normal[3], turned[3], one[3], luminance;

								unpack_vector(s->packed[3 * n], normal);
								for (k = 0; k < 3; k++)
									turned[k] = normal[0] * instance->matrix[k] + normal[1] * instance->matrix[3 + k] + normal[2] * instance->matrix[6 + k];
								vertex_light(sh + 15 * n, turned, one);
								luminance = 0.2126f * one[0] + 0.7152f * one[1] + 0.0722f * one[2];
								if (luminance < 0.0f)
									luminance = 0.0f;
								mean += luminance;
								mean_root += sqrtf(luminance);
							}
							if (mean > 0.0)
							{
								float scale = (float)(mean_root * mean_root / (mean * (double)s->vertex_count));

								for (k = 0; k < 15; k++)
									o[3 + k] *= scale;
							}
						}
					}
				}
			}
#endif
			continue;
		}
		if (!s->vertex_count)
			continue;
		if (build->result->vertices + s->vertex_count > budget ||
			build->result->surfaces + s->index_count / 3 > surface_budget)
		{
			build->result->instances_left_out++;
			instance->placed = FALSE;
			continue;
		}
		for (v = 0; v < s->vertex_count; v++)
		{
			for (k = 0; k < 3; k++)
			{
				centre[k] += instance->matrix[9 + k] + instance->matrix[12] * (s->positions[3 * v] * instance->matrix[k] +
					s->positions[3 * v + 1] * instance->matrix[3 + k] + s->positions[3 * v + 2] * instance->matrix[6 + k]);
			}
		}
		for (k = 0; k < 3; k++)
			centre[k] /= (float)s->vertex_count;
		/* its light: its per-vertex lighting's average (a patch), else neutral */
		if (index < lbsp_instance_count)
		{
			unsigned char const *l = h3_cache_at(cache, lbsp_instances + 8 * index, 8);
			short block = l ? read16(l + 2) : NONE;

			if (block >= 0 && block < per_vertex_count)
			{
				long vb = (long)h3_cache_read32(cache, per_vertex + 0x10 * block + 0xC);

				if (vb >= 0 && vb < build->bsp_geometry.vertex_buffer_count && build->bsp_geometry.vertex_buffers[vb].light_done &&
					build->bsp_geometry.vertex_buffers[vb].sh && build->bsp_geometry.vertex_buffers[vb].count >= s->vertex_count &&
					!(getenv("HALO_H3_VERTEX_LIGHTING") && !atoi(getenv("HALO_H3_VERTEX_LIGHTING"))))
				{
					per_vertex_sh = build->bsp_geometry.vertex_buffers[vb].sh;
					/* (a level made again to fit the tag cache - Edge, Waterfall:
					a texel block a triangle triples their vertices - takes the
					instance's per-vertex light averaged, a patch, so its instances
					all fit; flagged) */
					if (fit_shared_vertex_light)
					{
						float light[3] = { 0.0f, 0.0f, 0.0f }, one[3], normal[3];
						long px, py, count = 0;
						short atlas;

						for (v = 0; v < s->vertex_count; v++, count++)
						{
							float turned[3];

							unpack_vector(s->packed[3 * v], normal);
							for (k = 0; k < 3; k++)
								turned[k] = normal[0] * instance->matrix[k] + normal[1] * instance->matrix[3 + k] + normal[2] * instance->matrix[6 + k];
							vertex_light(per_vertex_sh + 15 * v, turned, one);
							for (k = 0; k < 3; k++)
								light[k] += one[k] > 0.0f ? one[k] : 0.0f;
						}
						per_vertex_sh = NULL;
						atlas = count ? atlas_place(build, 4, 4, &px, &py) : NONE;
						if (atlas != NONE)
						{
							for (k = 0; k < 3; k++)
								light[k] /= (float)count;
							for (k = 0; k < 16; k++)
								light_pending(build, atlas, px + (k & 3), py + (k >> 2), light);
							patch.atlas = atlas;
							patch.x = px;
							patch.y = py;
							patch.w = patch.h = 4;
							patch.whole = FALSE;
							page = &patch;
							build->result->instances_vertex_averaged++;
						}
					}
				}
				if (vb >= 0 && vb < build->bsp_geometry.vertex_buffer_count && build->bsp_geometry.vertex_buffers[vb].light_done &&
					!per_vertex_sh && page != &patch)
				{
					float light[3];

					for (k = 0; k < 3; k++)
						light[k] = build->bsp_geometry.vertex_buffers[vb].light[k] * build->vertex_light_scale;
					patch = patch_place(build, light);
					if (patch.atlas != NONE)
						page = &patch;
				}
			}
		}
		/* (per-pixel, sharing the atlas: its own coordinates, the Lbsp's list's) */
		{
			float *own = NULL, *shared = s->lightmap_coordinates;
			long list_count = 0;
			unsigned long list = 0;
			short block = index < instance_count ? h3_cache_read16(cache, instances + H3_INSTANCE_SIZE * index + 0x38) : NONE;

			if (block >= 0 && build->lightprobe.whole && build->lightprobe.atlas != NONE &&
				h3_cache_block(cache, build->lbsp + H3_LBSP_INSTANCE_TEXCOORDS, 0x10, &list_count, &list) && block < list_count)
			{
				long vb = h3_cache_read16(cache, list + 0x10 * block + 0xC);

				if (vb >= 0 && vb < build->bsp_geometry.vertex_buffer_count)
				{
					struct vertex_buffer const *b = &build->bsp_geometry.vertex_buffers[vb];

					if (b->data && b->stride == 4 && b->count >= s->vertex_count && (own = malloc(s->vertex_count * 8)) != NULL)
					{
						for (v = 0; v < s->vertex_count; v++)
						{
							own[2 * v] = (float)(unsigned short)read16(b->data + 4 * v) / 65535.0f;
							own[2 * v + 1] = (float)(unsigned short)read16(b->data + 4 * v + 2) / 65535.0f;
						}
						s->lightmap_coordinates = own;
						page = &build->lightprobe;
						build->result->instances_lightmapped++;
					}
				}
			}
			if (!(page != &build->lightprobe && per_vertex_sh &&
				geometry_add_per_vertex(build, s, cluster_of_room(build, centre, s->index_count / 3), instance->matrix, per_vertex_sh)))
			{
				geometry_add(build, s, cluster_of_room(build, centre, s->index_count / 3), instance->matrix, page);
			}
			else
				build->result->instances_vertex_lit++;
			s->lightmap_coordinates = shared;
			free(own);
		}
		build->result->instances++;
	}

	platform_log("h3 level: %ld instances of %ld baked on the lightprobe atlas's own coordinates, %ld lit by their per-vertex light, %ld by it averaged",
		build->result->instances_lightmapped, build->result->instances, build->result->instances_vertex_lit,
		build->result->instances_vertex_averaged);

	/* the decorators kept (geometry: decorators_add) */
	decorators_add(build);
	/* (h3vita: the decorators' buffers as read are theirs alone: let go
	here, not after the whole gather - 10 MB of Guardian's gather peak) */
	for (i = 0; i < build->decorator_count; i++)
		geometry_close(&build->decorators[i].geometry);
	if (build->decorator_buffers_open)
		geometry_close(&build->decorator_buffers);
	build->decorator_buffers_open = FALSE;
	grass_gather(build);

	/* the props' placements, instances of their own keys (a prop at a
scale each): turned as the engine turns them (matrix4x3_rotation_from_angles
of Halo 3's angles, which instance_angles gives back) */
	{
		long total = 0, p, n = build->instance_count;
		struct instance *grown;

		for (p = 0; p < build->prop_count; p++)
			total += build->props[p].placement_count;
		grown = total ? realloc(build->instances, (build->instance_count + total + 1) * sizeof(*grown)) : NULL;
		if (grown)
		{
			build->instances = grown;
			for (p = 0; p < build->prop_count; p++)
			{
				struct prop const *prop = &build->props[p];
				long j;

				for (j = 0; j < prop->placement_count; j++)
				{
					struct prop_placement const *placement = &prop->placements[j];
					struct instance *instance = &build->instances[n++];
					float cy = cosf(placement->angles[0]), sy = sinf(placement->angles[0]);
					float cp = cosf(placement->angles[1]), sp = sinf(placement->angles[1]);
					float cr = cosf(placement->angles[2]), sr = sinf(placement->angles[2]);
					long scale = scale_key(placement->scale), key = NONE, q;

					memset(instance, 0, sizeof(*instance));
					instance->definition = NONE;
					instance->matrix[0] = cy * cp; instance->matrix[1] = sy * cr - cy * sp * sr; instance->matrix[2] = cy * sp * cr + sy * sr;
					instance->matrix[3] = -(sy * cp); instance->matrix[4] = sy * sp * sr + cy * cr; instance->matrix[5] = cy * sr - sy * sp * cr;
					instance->matrix[6] = -sp; instance->matrix[7] = -(cp * sr); instance->matrix[8] = cp * cr;
					memcpy(instance->matrix + 9, placement->position, 12);
					instance->matrix[12] = placement->scale;
					for (q = 0; q < build->key_count; q++)
					{
						if (build->keys[q].prop == p && build->keys[q].scale == scale)
							key = q;
					}
					if (key == NONE)
					{
						struct object_key *keys = realloc(build->keys, (build->key_count + 1) * sizeof(*keys));

						if (keys)
						{
							build->keys = keys;
							memset(&keys[build->key_count], 0, sizeof(*keys));
							keys[build->key_count].definition = NONE;
							keys[build->key_count].scale = scale;
							keys[build->key_count].slot = NONE;
							keys[build->key_count].model = NONE;
							keys[build->key_count].prop = p;
							key = build->key_count++;
						}
					}
					instance->key = key;
					instance->placed = key != NONE;
					if (key != NONE)
						build->keys[key].uses++;
				}
			}
			build->instance_count = n;
		}
	}

	/* the models (a key each of a modelled definition), then the objects'
	slots: the models' first (the most placed first), then the collision's
	of the BSP's own (the largest first), as many as the palettes hold */
	build->models = calloc(build->key_count + 1, sizeof(*build->models));
	if (!build->models)
		return build_fail(build, "out of memory"), FALSE;
	for (i = 0; i < build->key_count; i++)
	{
		struct object_key *key = &build->keys[i];
		struct section const *s;
		float scale = key->scale / 100.0f;
		int k;

		if (key->prop != NONE)
		{
			if (prop_model(build, &build->props[key->prop], scale, &build->models[build->model_count], key))
				key->model = build->model_count++;
			/* (h3vita: the keys come in prop order - the placements' loop
			above -: a prop's buffers as read are let go after its last key's
			model, not after every prop's: the gather's peak, Guardian 11 MB) */
			if (build->props[key->prop].geometry_open)
			{
				long q;

				for (q = i + 1; q < build->key_count && build->keys[q].prop != key->prop; q++)
					;
				if (q == build->key_count)
				{
					geometry_close(&build->props[key->prop].geometry);
					build->props[key->prop].geometry_open = FALSE;
				}
			}
			continue;
		}
		s = &definition_sections[key->definition];
		key->radius = (key->definition < build->definition_count ? build->definition_radius[key->definition] : 0.0f) * scale;
		/* (its bounding sphere about Halo 3's centre: this engine places a
		scenery whose origin is in solid when that centre is in the level) */
		for (k = 0; k < 3; k++)
			key->centre[k] = key->definition < build->definition_count ? build->definition_centre[3 * key->definition + k] * scale : 0.0f;
		if (!modelled[key->definition] || !s->vertex_count)
			continue;
		if (model_make(build, s, scale, &build->models[build->model_count], 0.0f))
		{
			long v;
			float most = 0.0f;

			for (v = 0; v < s->vertex_count; v++)
			{
				float d[3];

				for (k = 0; k < 3; k++)
					d[k] = s->positions[3 * v + k] * scale - key->centre[k];
				if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > most)
					most = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
			}
			if (sqrtf(most) > key->radius)
				key->radius = sqrtf(most);
			key->model = build->model_count++;
		}
	}
	for (i = 0; i < build->bsp_geometry.mesh_count; i++)
		section_free(&definition_sections[i]);
	free(definition_sections);
	free(definition_read);
	free(order);
	while (build->slot_count < 3 * MAXIMUM_OBJECT_PALETTE - (build->gear.found ? 1 : 0))
	{
		long best = NONE;

		for (i = 0; i < build->key_count; i++)
		{
			struct object_key const *key = &build->keys[i];
			boolean collides = key_collision(build, key) != NULL;

			if (key->slot != NONE || (key->model == NONE && !collides))
				continue;
			if (best == NONE || (key->model != NONE) > (build->keys[best].model != NONE) ||
				((key->model != NONE) == (build->keys[best].model != NONE) &&
					(key->model != NONE ? key->uses > build->keys[best].uses : key->radius > build->keys[best].radius)))
			{
				best = i;
			}
		}
		if (best == NONE)
			break;
		build->keys[best].slot = build->slot_count;
		build->slot_key[build->slot_count++] = best;
	}
	/* the keys a placement of which this engine would not place as scenery
	or a light fixture (object_types_place_objects: its origin and centre
	outside the level's leaves - a palm tree's foot in the ground, a brace's
	in a wall) made machines, which are placed wherever they are: the slots'
	order kept but for those, last (the machines' palette) */
	{
		long *order_keys = malloc((build->slot_count + 1) * sizeof(long));
		long outside_keys = 0, outside_placements = 0, lost = 0, n = 0, pass;

		for (i = 0; i < build->instance_count; i++)
		{
			struct instance const *instance = &build->instances[i];

			if (instance->key != NONE && instance->placed && !build->keys[instance->key].outside &&
				!instance_placeable(build, instance, &build->keys[instance->key]))
			{
				build->keys[instance->key].outside = TRUE;
			}
		}
		for (i = 0; i < build->instance_count; i++)
		{
			struct instance const *instance = &build->instances[i];

			if (instance->key != NONE && instance->placed && build->keys[instance->key].slot != NONE &&
				build->keys[instance->key].outside && !instance_placeable(build, instance, &build->keys[instance->key]))
			{
				outside_placements++;
			}
		}
		/* (now only with HALO_H3_OUTSIDE_MACHINES=1: the engine places Halo
		3's instances as scenery wherever they are, object_types.c - Orbital's
		outside kinds took 450 of the machines' 400 placements) */
		if (order_keys && getenv("HALO_H3_OUTSIDE_MACHINES"))
		{
			/* (the inside keys in order, then the outside ones; those of
			the latter that land before the machines' slots stay lost) */
			for (pass = 0; pass < 2; pass++)
			{
				for (i = 0; i < build->slot_count; i++)
				{
					if ((build->keys[build->slot_key[i]].outside != FALSE) == (pass == 1))
						order_keys[n++] = build->slot_key[i];
				}
			}
			for (i = 0; i < build->slot_count; i++)
			{
				build->slot_key[i] = order_keys[i];
				build->keys[order_keys[i]].slot = i;
				if (build->keys[order_keys[i]].outside)
				{
					outside_keys++;
					if (slot_group(i) != 'mach')
						lost++;
				}
			}
		}
		free(order_keys);
		platform_log("h3 level: objects: %ld placements outside the level's leaves; %ld kinds of them made machines (%ld kinds left scenery)",
			outside_placements, outside_keys, lost);
	}
	/* the palettes' placements within the scenario's most (scenery 2000,
	light fixtures 500, machines 400: Orbital's 580 machines were cut to
	400 by the map checker, 180 of its pieces missing): a light fixture's or
	machine's kind of the most placements (not one outside the leaves)
	traded for a scenery kind of fewer while its palette is over */
	{
		static long const most[3] = { 2000, 500, 400 };
		long *placed = calloc(build->key_count + 1, sizeof(long));
		long total[3] = { 0, 0, 0 }, swaps = 0;
		int g;

		if (placed)
		{
			for (i = 0; i < build->instance_count; i++)
			{
				if (build->instances[i].key != NONE && build->instances[i].placed)
					placed[build->instances[i].key]++;
			}
			for (i = 0; i < build->slot_count; i++)
				total[i / MAXIMUM_OBJECT_PALETTE < 3 ? i / MAXIMUM_OBJECT_PALETTE : 2] += placed[build->slot_key[i]];
			for (g = 1; g < 3; g++)
			{
				while (total[g] > most[g] && swaps < 1000)
				{
					long heavy = NONE, light = NONE, j;

					for (j = g * MAXIMUM_OBJECT_PALETTE; j < (g + 1) * MAXIMUM_OBJECT_PALETTE && j < build->slot_count; j++)
					{
						if ((!build->keys[build->slot_key[j]].outside || !getenv("HALO_H3_OUTSIDE_MACHINES")) &&
							(heavy == NONE || placed[build->slot_key[j]] > placed[build->slot_key[heavy]]))
							heavy = j;
					}
					for (j = 0; j < MAXIMUM_OBJECT_PALETTE && j < build->slot_count; j++)
					{
						if (light == NONE || placed[build->slot_key[j]] < placed[build->slot_key[light]])
							light = j;
					}
					if (getenv("HALO_H3_DEBUG"))
						fprintf(stderr, "h3 trade: group %d total %ld heavy %ld (%ld) light %ld (%ld) scenery %ld\n", g, total[g], heavy,
							heavy != NONE ? placed[build->slot_key[heavy]] : -1, light, light != NONE ? placed[build->slot_key[light]] : -1, total[0]);
					if (heavy == NONE || light == NONE || placed[build->slot_key[light]] >= placed[build->slot_key[heavy]] ||
						total[0] - placed[build->slot_key[light]] + placed[build->slot_key[heavy]] > most[0])
					{
						break;
					}
					total[0] += placed[build->slot_key[heavy]] - placed[build->slot_key[light]];
					total[g] += placed[build->slot_key[light]] - placed[build->slot_key[heavy]];
					j = build->slot_key[heavy];
					build->slot_key[heavy] = build->slot_key[light];
					build->slot_key[light] = j;
					build->keys[build->slot_key[heavy]].slot = heavy;
					build->keys[build->slot_key[light]].slot = light;
					swaps++;
				}
			}
			if (swaps || total[1] > most[1] || total[2] > most[2])
				platform_log("h3 level: objects: %ld kinds traded into the scenery palette: placements scenery %ld, light fixtures %ld, machines %ld",
					swaps, total[0], total[1], total[2]);
			free(placed);
		}
	}
	{
		long collision_only = 0, collision_placements = 0;

		for (i = 0; i < build->key_count; i++)
		{
			if (build->keys[i].model == NONE && build->keys[i].slot == NONE && key_collision(build, &build->keys[i]))
				collision_only++, collision_placements += build->keys[i].uses;
		}
		platform_log("h3 level: objects: %ld slots of %d; %ld collision-only kinds (%ld placements) without one",
			build->slot_count, 3 * MAXIMUM_OBJECT_PALETTE, collision_only, collision_placements);
	}
	for (i = 0; i < build->key_count; i++)
	{
		if (build->keys[i].model != NONE && build->keys[i].slot == NONE)
			build->result->instances_left_out += build->keys[i].uses;
		else if (build->keys[i].model != NONE)
			build->result->object_instances += build->keys[i].uses;
	}
	/* (the sky's model last) */
	build->sky_model_index = NONE;
	if (build->sky_model.part_count)
	{
		struct model_made *grown = realloc(build->models, (build->model_count + 1) * sizeof(*grown));

		if (grown)
		{
			build->models = grown;
			build->models[build->model_count] = build->sky_model;
			memset(&build->sky_model, 0, sizeof(build->sky_model));
			build->sky_model_index = build->model_count++;
		}
	}
	/* (the gear's model, after) */
	gear_make(build);
	geometry_close(&build->gear.geometry);
	if (build->gear.found)
	{
		struct model_made *grown = realloc(build->models, (build->model_count + 1) * sizeof(*grown));

		if (grown)
		{
			build->models = grown;
			build->models[build->model_count] = build->gear.model;
			memset(&build->gear.model, 0, sizeof(build->gear.model));
			build->gear.model_index = build->model_count++;
		}
		else
			build->gear.found = FALSE;
	}
	build->result->models = build->model_count;
	build->result->sceneries = build->slot_count;
	free(modelled);

	return !build->result->why[0];
}

/* ---------- private code: the player's things (h2_level.c's kit) */

struct relink
{
	unsigned char *base;
	unsigned long size;
	unsigned long link;
	unsigned char *done;
};

H3_ALIGN static void relink_word(struct relink *r, unsigned char *word)
{
	unsigned long offset = (unsigned long)(word - r->base);
	unsigned long value;

	if (offset + 4 > r->size || (offset & 3))
		return;
	if (r->done[offset / 32] & (1 << ((offset / 4) & 7)))
		return;
	value = read32(word);
	if (value >= (unsigned long)r->base && value - (unsigned long)r->base < r->size)
	{
		write32(word, value - (unsigned long)r->base + r->link);
		r->done[offset / 32] |= (unsigned char)(1 << ((offset / 4) & 7));
	}
}

H3_ALIGN static unsigned char *relink_target(struct relink *r, unsigned char const *word, unsigned long size)
{
	unsigned long value = read32(word);

	if (value < (unsigned long)r->base || value - (unsigned long)r->base >= r->size ||
		size > r->size - (value - (unsigned long)r->base))
	{
		return NULL;
	}

	return (unsigned char *)value;
}

H3_ALIGN static void relink_walk(struct relink *r, unsigned short layout, unsigned char *at, int depth)
{
	long count = halo_tag_layout_field_count(layout);
	long i;

	if (layout == HALO_TAG_LAYOUT_NONE || layout == HALO_TAG_LAYOUT_UNKNOWN || depth > 32)
		return;
	for (i = 0; i < count; i++)
	{
		struct halo_tag_layout_field field;
		unsigned char *f;

		if (!halo_tag_layout_field(layout, i, &field))
			continue;
		f = at + field.offset;
		if ((unsigned long)(f - r->base) + 4 > r->size)
			continue;
		switch (field.kind)
		{
			case HALO_TAG_LAYOUT_FIELD_BLOCK:
			{
				long n = (long)read32(f);
				unsigned char *elements = n > 0 && field.child_size ? relink_target(r, f + 4, (unsigned long)n * field.child_size) : NULL;
				long e;

				if (elements && field.child != HALO_TAG_LAYOUT_NONE)
				{
					for (e = 0; e < n; e++)
						relink_walk(r, field.child, elements + field.child_size * e, depth + 1);
				}
				relink_word(r, f + 4);
				break;
			}
			case HALO_TAG_LAYOUT_FIELD_DATA:
				relink_word(r, f + 12);
				break;
			case HALO_TAG_LAYOUT_FIELD_REFERENCE:
				relink_word(r, f + 4);
				break;
			case HALO_TAG_LAYOUT_FIELD_POINTER:
				relink_word(r, f);
				break;
			case HALO_TAG_LAYOUT_FIELD_STRUCT:
				relink_walk(r, field.child, f, depth + 1);
				break;
		}
	}
}

H3_ALIGN static boolean kit_relink(unsigned char *tags, unsigned long size, unsigned long link)
{
	struct relink r;
	long count, i;
	unsigned char *instances;

	r.base = tags;
	r.size = size;
	r.link = link;
	r.done = calloc(size / 32 + 1, 1);
	if (!r.done)
		return FALSE;
	instances = relink_target(&r, tags, 0);
	count = (long)read32(tags + 12);
	if (!instances || count < 0 || (unsigned long)count * XBOX_TAG_INSTANCE_SIZE > size)
	{
		free(r.done);
		return FALSE;
	}
	for (i = 0; i < count; i++)
	{
		unsigned char *instance = instances + XBOX_TAG_INSTANCE_SIZE * i;
		unsigned char *root = relink_target(&r, instance + 20, 4);

		if (root)
			relink_walk(&r, halo_tag_layout_root(read32(instance)), root, 0);
		relink_word(&r, instance + 16);
		relink_word(&r, instance + 20);
	}
	{
		long vertex_count = (long)read32(tags + 16), index_count = (long)read32(tags + 24);
		unsigned char *vertex_buffers = vertex_count > 0 ? relink_target(&r, tags + 20, (unsigned long)vertex_count * 12) : NULL;
		unsigned char *index_buffers = index_count > 0 ? relink_target(&r, tags + 28, (unsigned long)index_count * 12) : NULL;

		for (i = 0; vertex_buffers && i < vertex_count; i++)
			relink_word(&r, vertex_buffers + 12 * i + 4);
		for (i = 0; index_buffers && i < index_count; i++)
			relink_word(&r, index_buffers + 12 * i + 4);
		relink_word(&r, tags + 20);
		relink_word(&r, tags + 28);
	}
	relink_word(&r, tags);
	free(r.done);

	return TRUE;
}

/* the kit - the game's globals, the player's weapons -: Halo 2's levels'
(h2kit.pack, made once per card from the player's Halo CE map, h2_level.c),
its tags read whole (all the pack's: the globals' multiplayer block, every
weapon, grenade, powerup and vehicle it holds), the pack left open for its
data's reads */
H3_ALIGN static unsigned char *kit_load(struct build *build, struct scene_pack_info *info, FILE **file)
{
	char why[SCENE_PACK_WHY_SIZE];
	unsigned char *tags = NULL;

	*file = NULL;
	if (!h2_level_kit_pack_open(info, file))
	{
		build_fail(build, "the kit (h2kit.pack: the game's globals and the player's weapons, from the player's Halo map) "
			"could not be made");
		return NULL;
	}
	tags = malloc(info->tags_size);
	tag_validate_pack_allow_globals(TRUE);
	if (!tags || !scene_pack_load_file(*file, info, tags, why, sizeof(why)))
	{
		tag_validate_pack_allow_globals(FALSE);
		if (!tags)
			snprintf(why, sizeof(why), "out of memory");
		free(tags);
		build_fail(build, why);
		return NULL;
	}
	tag_validate_pack_allow_globals(FALSE);
	platform_log("h3 level: the kit from h2kit.pack: %ld tags, %lu KB of tags, %lu KB of data", info->tag_count,
		info->tags_size / 1024, info->data_size / 1024);

	return tags;
}

/* (h3vita) the kit's tags read again from the open pack (kit_load's file),
as they were when first read */
H3_ALIGN static unsigned char *kit_reload(struct build *build, struct scene_pack_info *info, FILE *file)
{
	char why[SCENE_PACK_WHY_SIZE];
	unsigned char *tags = malloc(info->tags_size);

	tag_validate_pack_allow_globals(TRUE);
	if (!tags || !file || !scene_pack_load_file(file, info, tags, why, sizeof(why)))
	{
		tag_validate_pack_allow_globals(FALSE);
		if (!tags)
			snprintf(why, sizeof(why), "out of memory");
		free(tags);
		build_fail(build, why);
		return NULL;
	}
	tag_validate_pack_allow_globals(FALSE);
	return tags;
}

H3_ALIGN static unsigned long tag_id(long index)
{
	return ((unsigned long)(0xE174 + index) << 16) | (unsigned long)index;
}

/* the made static devices' scenario device groups (objects_write) */
enum
{
	H3_STATIC_POWER_GROUP = 0,
	H3_STATIC_POSITION_GROUP = 1
};

/* the kind of object a slot is: scenery (100), machines (100), light
fixtures (100): each palette's most */
H3_ALIGN static unsigned long slot_group(long slot)
{
	return slot < MAXIMUM_OBJECT_PALETTE ? 'scen' : slot < 2 * MAXIMUM_OBJECT_PALETTE ? 'lifi' : 'mach';
}

/* the instanced geometry's objects written: each slot's collision model
(its definition's BSP at its scale), each model, each slot's object, the
scenario's palettes and placements; the models' vertices and strips in the
tag data, their buffers added to the vertex and index buffer tables */
H3_ALIGN static void objects_write(struct build *build, struct region *tags, unsigned long instances, unsigned long header,
	unsigned long scenario_root, struct tag_layout const *layout)
{
	long i, k;
	long kind_count[3] = { 0, 0, 0 };
	long kind_placed[3] = { 0, 0, 0 }, kind_done[3] = { 0, 0, 0 };
	unsigned long placements[3], palettes[3];
	static unsigned long const palette_fields[3] = { 0x21C, 0x2D0, 0x2A0 };
	static unsigned long const placement_fields[3] = { 0x210, 0x2C4, 0x294 };
	static unsigned long const placement_sizes[3] = { 72, 0x58, 64 };
	static unsigned long const palette_groups[3] = { 'scen', 'lifi', 'mach' };

	/* collision models */
	for (i = 0; i < build->slot_count && !tags->b.failed; i++)
	{
		struct object_key const *key = &build->keys[build->slot_key[i]];
		unsigned long root, field, nodes;
		static unsigned long const collision_nulls[] = { 0x70, 0x84, 0x98, 0xA8, 0xBC, 0x188, 0x198, 0x1A8 };

		if (layout->slot_collision[i] == NONE)
			continue;
		root = region_alloc(tags, 664);
		write32(region_at(tags, instances + XBOX_TAG_INSTANCE_SIZE * layout->slot_collision[i] + 20), tags->link + root);
		for (k = 0; k < (long)(sizeof(collision_nulls) / sizeof(collision_nulls[0])); k++)
			region_reference(tags, root + collision_nulls[k], 0, 0xFFFFFFFFUL);
		writef(region_at(tags, root + 0x08), 1.0f);
		field = region_block(tags, root + 0x234, 1, 72);
		if (tags->b.failed)
			return;
		memcpy(region_at(tags, field), "default", 8);
		write16(region_at(tags, field + 0x24), 7);
		writef(region_at(tags, field + 0x2C), 1.0f);
		writef(region_at(tags, field + 0x3C), 1.0f);
		field = region_block(tags, root + 0x240, 1, 84);
		if (tags->b.failed)
			return;
		memcpy(region_at(tags, field), "default", 8);
		region_reference(tags, field + 0x38, 0, 0xFFFFFFFFUL);
		{
			unsigned long permutation = region_block(tags, field + 0x48, 1, 32);

			if (!tags->b.failed)
				memcpy(region_at(tags, permutation), "__base", 7);
		}
		nodes = region_block(tags, root + 0x28C, 1, 64);
		if (tags->b.failed)
			return;
		memcpy(region_at(tags, nodes), "frame", 6);
		write16(region_at(tags, nodes + 0x20), 0);
		write16(region_at(tags, nodes + 0x22), -1);
		write16(region_at(tags, nodes + 0x24), -1);
		write16(region_at(tags, nodes + 0x26), -1);
		field = region_block(tags, nodes + 0x34, 1, 0x60);
		if (tags->b.failed || !collision_convert(tags, field, key_collision(build, key), 0, 1,
			key->scale / 100.0f))
		{
			build_fail(build, "an instance's collision BSP is damaged");
			return;
		}
	}

	/* models: their vertices and strips in the tag data, a buffer each */
	{
		long total = 0, kit_vertex_count = (long)read32(region_at(tags, header + 16));
		long kit_index_count = (long)read32(region_at(tags, header + 24));
		unsigned long kit_vertex_table = read32(region_at(tags, header + 20)), kit_index_table = read32(region_at(tags, header + 28));
		unsigned long vertex_table, index_table;
		long next = 0;

		for (i = 0; i < build->model_count; i++)
			total += build->models[i].part_count;
		/* (the sandbox's models' entries after the level's: h3_sandbox_write) */
		total += build->sandbox_buffer_count;
		vertex_table = region_alloc(tags, 12UL * (kit_vertex_count + total));
		index_table = region_alloc(tags, 12UL * (kit_index_count + total));
		if (tags->b.failed)
			return;
		build->vertex_table = vertex_table;
		build->index_table = index_table;
		build->buffer_next = kit_vertex_count;
		/* (the kit's entries first, its parts' pointers to them moved) */
		if (kit_vertex_count && kit_vertex_table)
			memcpy(region_at(tags, vertex_table), region_at(tags, kit_vertex_table - tags->link), 12UL * kit_vertex_count);
		if (kit_index_count && kit_index_table)
			memcpy(region_at(tags, index_table), region_at(tags, kit_index_table - tags->link), 12UL * kit_index_count);
		{
			unsigned long at, end = layout->kit_offset + layout->kit_size;

			for (at = layout->kit_offset; at + 4 <= end; at += 4)
			{
				unsigned long v = read32(region_at(tags, at));

				if (kit_vertex_count && v >= kit_vertex_table && v < kit_vertex_table + 12UL * kit_vertex_count &&
					(v - kit_vertex_table) % 12 == 0)
				{
					write32(region_at(tags, at), tags->link + vertex_table + (v - kit_vertex_table));
				}
				else if (kit_index_count && v >= kit_index_table && v < kit_index_table + 12UL * kit_index_count &&
					(v - kit_index_table) % 12 == 0)
				{
					write32(region_at(tags, at), tags->link + index_table + (v - kit_index_table));
				}
			}
		}
		write32(region_at(tags, header + 16), kit_vertex_count + total);
		write32(region_at(tags, header + 20), tags->link + vertex_table);
		write32(region_at(tags, header + 24), kit_index_count + total);
		write32(region_at(tags, header + 28), tags->link + index_table);
		for (i = 0; i < build->model_count && !tags->b.failed; i++)
		{
			struct model_made *model = &build->models[i];
			unsigned long root = region_alloc(tags, 0xE8), node, region, permutation, geometry, parts, shaders;

			write32(region_at(tags, instances + XBOX_TAG_INSTANCE_SIZE * (layout->model_first + i) + 20), tags->link + root);
			writef(region_at(tags, root + 0x30), model->scale);
			writef(region_at(tags, root + 0x34), model->scale);
			node = region_block(tags, root + 0xB8, 1, 0x9C);
			if (tags->b.failed)
				return;
			memcpy(region_at(tags, node), "frame", 6);
			write16(region_at(tags, node + 0x20), -1);
			write16(region_at(tags, node + 0x22), -1);
			write16(region_at(tags, node + 0x24), -1);
			writef(region_at(tags, node + 0x34 + 12), 1.0f);
			writef(region_at(tags, node + 0x68), 1.0f);
			writef(region_at(tags, node + 0x68 + 4), 1.0f);
			writef(region_at(tags, node + 0x68 + 20), 1.0f);
			writef(region_at(tags, node + 0x68 + 36), 1.0f);
			/* (a model of more parts than a geometry's 32 - Assembly's,
			Icebox's, Waterfall's biggest, 48 to 85 - a geometry of 32 a
			region, each region's one permutation drawing its own; its
			shaders each once, at most the model's 32) */
			{
				long geometry_count = (model->part_count + MAXIMUM_PARTS_PER_GEOMETRY - 1) / MAXIMUM_PARTS_PER_GEOMETRY;
				long *unique = malloc((model->part_count + 1) * sizeof(long)), unique_count = 0, g;

				if (!unique)
				{
					tags->b.failed = TRUE;
					return;
				}
				if (geometry_count < 1)
					geometry_count = 1;
				if (geometry_count > MAXIMUM_PARTS_PER_GEOMETRY)
					geometry_count = MAXIMUM_PARTS_PER_GEOMETRY;
				for (k = 0; k < model->part_count; k++)
				{
					long u;

					for (u = 0; u < unique_count && unique[u] != model->parts[k].shader; u++)
						;
					if (u == unique_count)
						unique[unique_count++] = model->parts[k].shader;
				}
				if (unique_count > MAXIMUM_PARTS_PER_GEOMETRY)
					platform_log("h3 level: model %ld: %ld shaders, the model's most 32: the rest drawn with its 32nd", i, unique_count);
				region = region_block(tags, root + 0xC4, geometry_count, 0x4C);
				geometry = region_block(tags, root + 0xD0, geometry_count, 0x30);
				shaders = region_block(tags, root + 0xDC, unique_count < MAXIMUM_PARTS_PER_GEOMETRY ? unique_count : MAXIMUM_PARTS_PER_GEOMETRY, 0x20);
				if (tags->b.failed)
				{
					free(unique);
					return;
				}
				for (k = 0; k < unique_count && k < MAXIMUM_PARTS_PER_GEOMETRY; k++)
				{
					struct shader const *s = &build->shaders[unique[k]];

					if (s->group == 'senv' && layout->shader_soso[unique[k]] != NONE)
						region_reference(tags, shaders + 0x20 * k, 'soso', tag_id(layout->soso_first + layout->shader_soso[unique[k]]));
					else
						region_reference(tags, shaders + 0x20 * k, s->group, tag_id(layout->shader_first + unique[k]));
				}
				for (g = 0; g < geometry_count && !tags->b.failed; g++)
				{
					long first_part = g * MAXIMUM_PARTS_PER_GEOMETRY;
					long part_count = model->part_count - first_part < MAXIMUM_PARTS_PER_GEOMETRY ? model->part_count - first_part :
						MAXIMUM_PARTS_PER_GEOMETRY;
					unsigned long region_at_g = region + 0x4C * g;

					if (g)
						snprintf((char *)region_at(tags, region_at_g), 32, "base%ld", g);
					else
						memcpy(region_at(tags, region_at_g), "base", 5);
					permutation = region_block(tags, region_at_g + 0x40, 1, 0x58);
					if (tags->b.failed)
						break;
					memcpy(region_at(tags, permutation), "base", 5);
					for (k = 0; k < 5; k++)
						write16(region_at(tags, permutation + 0x40 + 2 * k), (unsigned short)g);
					parts = region_block(tags, geometry + 0x30 * g + 0x24, part_count, 0x68);
					for (k = 0; k < part_count && !tags->b.failed; k++)
					{
						struct model_part_made *part = &model->parts[first_part + k];
						unsigned long vertices = region_alloc(tags, part->vertex_bytes);
						unsigned long strip = region_alloc(tags, part->strip_bytes);
						unsigned char *o;
						long vb = kit_vertex_count + next, ib = kit_index_count + next, u;

						if (tags->b.failed)
							break;
						for (u = 0; u < unique_count && unique[u] != part->shader; u++)
							;
						if (u >= MAXIMUM_PARTS_PER_GEOMETRY)
							u = MAXIMUM_PARTS_PER_GEOMETRY - 1;
						next++;
						build->buffer_next = kit_vertex_count + next;
						memcpy(region_at(tags, vertices), part->vertices, part->vertex_bytes);
						memcpy(region_at(tags, strip), part->strip, part->strip_bytes);
						write32(region_at(tags, vertex_table + 12 * vb + 4), tags->link + vertices);
						write32(region_at(tags, index_table + 12 * ib + 4), tags->link + strip);
						o = region_at(tags, parts + 0x68 * k);
						write16(o + 0x04, (unsigned short)u);
						o[6] = 0xFF;
						o[7] = 0xFF;
						writef(o + 0x0C, 1.0f);
						for (vb = 0; vb < 3; vb++)
							writef(o + 0x14 + 4 * vb, part->centroid[vb]);
						write16(o + 0x44, 1);   /* a strip */
						write32(o + 0x48, (unsigned long)part->triangle_count);
						write32(o + 0x4C, tags->link + strip);
						write32(o + 0x50, tags->link + index_table + 12 * ib);
						write16(o + 0x54, 5);   /* compressed model vertices */
						write32(o + 0x58, (unsigned long)part->vertex_count);
						write32(o + 0x60, tags->link + vertices);
						write32(o + 0x64, tags->link + vertex_table + 12 * (kit_vertex_count + next - 1));
					}
				}
				free(unique);
			}
		}
	}

	/* the objects */
	for (i = 0; i < build->slot_count && !tags->b.failed; i++)
	{
		struct object_key const *key = &build->keys[build->slot_key[i]];
		unsigned long group = slot_group(i);
		unsigned long size = group == 'scen' ? 0x1FC : group == 'mach' ? 0x324 : 0x290;
		unsigned long root = region_alloc(tags, size);
		float radius = key->radius > 0.0f ? key->radius : 1.0f;
		static unsigned long const object_nulls[] = { 0x28, 0x38, 0x70, 0x80, 0x90, 0xA0 };

		write32(region_at(tags, instances + XBOX_TAG_INSTANCE_SIZE * (layout->object_first + i) + 20), tags->link + root);
		for (k = 0; k < (long)(sizeof(object_nulls) / sizeof(object_nulls[0])); k++)
			region_reference(tags, root + object_nulls[k], 0, 0xFFFFFFFFUL);
		if (key->model != NONE)
			region_reference(tags, root + 0x28, 'mode', tag_id(layout->model_first + key->model));
		if (layout->slot_collision[i] != NONE)
			region_reference(tags, root + 0x70, 'coll', tag_id(layout->slot_collision[i]));
		write16(region_at(tags, root), group == 'scen' ? 6 : group == 'mach' ? 7 : 9);
		/* (no shadow: it is the level's) */
		write16(region_at(tags, root + 2), 1);
		if (group != 'scen')
		{
			static unsigned long const device_nulls[] = { 0x1A0, 0x1B0, 0x1C0, 0x1D0, 0x1E0, 0x1F0, 0x20C };
			static unsigned long const ones[] = { 0x180, 0x184, 0x188, 0x18C, 0x190, 0x194, 0x274, 0x278, 0x27C, 0x280, 0x284, 0x288 };

			for (k = 0; k < (long)(sizeof(device_nulls) / sizeof(device_nulls[0])); k++)
				region_reference(tags, root + device_nulls[k], 0, 0xFFFFFFFFUL);
			for (k = 0; k < (long)(sizeof(ones) / sizeof(ones[0])); k++)
				writef(region_at(tags, root + ones[k]), 1.0f);
			if (group == 'mach')
				write16(region_at(tags, root + 0x2EA), -1);
		}
		writef(region_at(tags, root + 4), radius + 0.1f);
		/* (its bounding offset: Halo 3's centre) */
		for (k = 0; k < 3; k++)
			writef(region_at(tags, root + 8 + 4 * k), key->centre[k]);
		writef(region_at(tags, root + 0x20), 1.0f);
		writef(region_at(tags, root + 0x104), radius + 0.1f);
		write16(region_at(tags, root + 0x13C), -1);
		write16(region_at(tags, root + 0x13E), -1);
		kind_count[group == 'scen' ? 0 : group == 'lifi' ? 1 : 2]++;
	}

	/* palettes and placements */
	for (k = 0; k < 3; k++)
	{
		long extra = k == 2 && layout->gear_object != NONE ? 1 : 0;

		palettes[k] = region_block(tags, scenario_root + palette_fields[k], kind_count[k] + extra, 48);
		for (i = 0; i < kind_count[k] && !tags->b.failed; i++)
			region_reference(tags, palettes[k] + 48 * i, palette_groups[k], tag_id(layout->object_first + MAXIMUM_OBJECT_PALETTE * k + i));
		if (extra && !tags->b.failed)
			region_reference(tags, palettes[k] + 48 * kind_count[k], 'mach', tag_id(layout->gear_object));
	}
	for (i = 0; i < build->instance_count; i++)
	{
		long key = build->instances[i].key;

		if (key != NONE && build->keys[key].slot != NONE && build->instances[i].placed)
			kind_placed[build->keys[key].slot / MAXIMUM_OBJECT_PALETTE]++;
	}
	if (getenv("HALO_H3_DEBUG"))
		fprintf(stderr, "h3 level: objects: scenery %ld types %ld placed, light fixtures %ld types %ld placed, machines %ld types %ld placed\n",
			kind_count[0], kind_placed[0], kind_count[1], kind_placed[1], kind_count[2], kind_placed[2]);
	/* the scenario's device groups: Halo 3's instanced geometry placed as
	light fixtures and machines is static, so every such placement shares
	one power group (on) and one position group (shut). A device with no
	group gets two of its own (device_add_scenario_information), and the
	game state holds 1024: Turf and Orbital place 500+ of them
	("no more free device groups", then a crash on the NONE group) */
	if (kind_placed[1] || kind_placed[2])
	{
		unsigned long groups = region_block(tags, scenario_root + 0x288, 2, 0x34);

		if (!tags->b.failed)
		{
			memcpy(region_at(tags, groups + 0x34 * H3_STATIC_POWER_GROUP), "h3 static power", 16);
			writef(region_at(tags, groups + 0x34 * H3_STATIC_POWER_GROUP + 0x20), 1.0f);
			memcpy(region_at(tags, groups + 0x34 * H3_STATIC_POSITION_GROUP), "h3 static position", 19);
			writef(region_at(tags, groups + 0x34 * H3_STATIC_POSITION_GROUP + 0x20), 0.0f);
		}
	}
	for (k = 0; k < 3; k++)
		placements[k] = region_block(tags, scenario_root + placement_fields[k],
			kind_placed[k] + (k == 2 && layout->gear_object != NONE ? 1 : 0), placement_sizes[k]);
	/* (the gear: its machine, where Halo 3 has it - last) */
	if (layout->gear_object != NONE && !tags->b.failed)
	{
		unsigned char *o = region_at(tags, placements[2] + 64 * kind_placed[2]);

		write16(o, kind_count[2]);
		write16(o + 2, -1);
		write16(o + 6, -1);
		memcpy(o + 8, build->gear.position, 12);
		memcpy(o + 0x14, build->gear.rotation, 12);
		o[0x24] = 0xFF;
		write16(o + 0x28, -1);
		write16(o + 0x2A, -1);
	}
	for (i = 0; i < build->instance_count && !tags->b.failed; i++)
	{
		struct instance const *instance = &build->instances[i];
		unsigned char *o;
		float angles[3];
		long slot, kind;

		if (instance->key == NONE || build->keys[instance->key].slot == NONE || !instance->placed)
			continue;
		slot = build->keys[instance->key].slot;
		kind = slot / MAXIMUM_OBJECT_PALETTE;
		o = region_at(tags, placements[kind] + placement_sizes[kind] * kind_done[kind]++);
		write16(o, slot - MAXIMUM_OBJECT_PALETTE * kind);
		write16(o + 2, -1);
		write16(o + 6, -1);
		writef(o + 8, instance->matrix[9]);
		writef(o + 12, instance->matrix[10]);
		writef(o + 16, instance->matrix[11]);
		instance_angles(instance, angles);
		writef(o + 0x14, angles[0]);
		writef(o + 0x18, angles[1]);
		writef(o + 0x1C, angles[2]);
		if (kind == 0)
		{
			write16(o + 0x20, 1);
			o[0x24] = 0xFF;
		}
		else
		{
			/* (a device: the made static geometry's shared power and
			position groups - one each, not two a placement) */
			o[0x24] = 0xFF;
			write16(o + 0x28, H3_STATIC_POWER_GROUP);
			write16(o + 0x2A, H3_STATIC_POSITION_GROUP);
		}
	}
}

/* ---------- public code */

/* ---------- private code: which structure BSP holds a point

(h3_characters.c: a mission's squads and firing positions in the level
made) A point is this level's when, of every structure BSP of the map, the
smallest cluster whose bounds hold it is one of the made BSP's - the rule
the player's start chooses the BSP by. The clusters' bounds of every BSP
are read once a build. */

struct bsp_clusters
{
	unsigned long sbsp;
	long order;         /* the map's nth structure BSP (HALO_H3_BSP's count) */
	long count;
	float *bounds;      /* 6 each */
};

/* (kept after the make: the game asks which BSP holds the player,
h3_level_point_bsp) */
static struct
{
	long count;
	long made;          /* bsps' (NONE: none) */
	/* (the BSPs a point of this level may be in: the made one and those a
	zone set of the mission loads with it - not its cutscenes' BSPs, which
	lie over the others) */
	unsigned long mask;
	struct bsp_clusters bsps[32];
} level_clusters = { 0, NONE, 0xFFFFFFFFUL, { { 0, 0, 0, NULL } } };

/* (a mission played a structure BSP at a time: the next BSP asked for and
where the player stands in it - h3_level_segment_request; pending until the
game ticks in the level made for it) */
static struct
{
	boolean pending;
	long bsp;
	float position[3];
	float facing;
	char name[64];
} level_segment;

H3_ALIGN static void level_clusters_free(void)
{
	long i;

	for (i = 0; i < level_clusters.count; i++)
		free(level_clusters.bsps[i].bounds);
	memset(&level_clusters, 0, sizeof(level_clusters));
	level_clusters.made = NONE;
	level_clusters.mask = 0xFFFFFFFFUL;
}

/* every structure BSP's clusters' bounds read; the made one build->sbsp */
H3_ALIGN static void level_clusters_read(struct build *build)
{
	long i, order = 0;

	level_clusters_free();
	for (i = 0; i < h3_cache_tag_count(build->cache) && level_clusters.count < 32; i++)
	{
		struct h3_cache_tag s;
		struct bsp_clusters *b = &level_clusters.bsps[level_clusters.count];
		long cluster_count = 0, c;
		unsigned long clusters = 0;

		if (!h3_cache_tag_get(build->cache, i, &s) || s.group_tag != 'sbsp')
			continue;
		order++;
		if (!s.address || !h3_cache_block(build->cache, s.address + H3_SBSP_CLUSTERS, H3_CLUSTER_SIZE, &cluster_count, &clusters) ||
			cluster_count < 1)
		{
			continue;
		}
		b->bounds = malloc(sizeof(float) * 6 * (size_t)cluster_count);
		if (!b->bounds)
			continue;
		b->sbsp = s.address;
		b->order = order - 1;
		b->count = 0;
		for (c = 0; c < cluster_count; c++)
		{
			if (h3_cache_read(build->cache, clusters + H3_CLUSTER_SIZE * c, 24, b->bounds + 6 * b->count))
				b->count++;
		}
		if (s.address == build->sbsp)
			level_clusters.made = level_clusters.count;
		level_clusters.count++;
	}
	/* (the mission's zone sets, scenario 0x58, 0x28 each: the BSPs each loads
	a mask at 0xC - those of every set that loads the made one) */
	if (level_clusters.made != NONE && level_clusters.bsps[level_clusters.made].order < 32)
	{
		unsigned long made_bit = 1UL << level_clusters.bsps[level_clusters.made].order, mask = 0;
		long set_count = 0;
		unsigned long sets = 0;

		if (h3_cache_block(build->cache, build->scenario + 0x58, 0x28, &set_count, &sets))
		{
			for (i = 0; i < set_count; i++)
			{
				unsigned long loaded = h3_cache_read32(build->cache, sets + 0x28 * (unsigned long)i + 0xC);

				if (loaded & made_bit)
					mask |= loaded;
			}
		}
		level_clusters.mask = mask ? mask : 0xFFFFFFFFUL;
	}
}

/* of every BSP's clusters, the BSP of the smallest whose bounds hold the point (NONE) */
H3_ALIGN static long level_clusters_of(float const *p, unsigned long mask)
{
	long i, c, best = NONE;
	float best_volume = 3.0e38f;

	for (i = 0; i < level_clusters.count; i++)
	{
		struct bsp_clusters const *b = &level_clusters.bsps[i];

		/* (mask: the BSPs looked at, a bit each by its order) */
		if (b->order < 32 && !(mask & (1UL << b->order)))
			continue;

		for (c = 0; c < b->count; c++)
		{
			float const *q = b->bounds + 6 * c;

			if (p[0] >= q[0] && p[0] <= q[1] && p[1] >= q[2] && p[1] <= q[3] && p[2] >= q[4] && p[2] <= q[5])
			{
				float v = (q[1] - q[0]) * (q[3] - q[2]) * (q[5] - q[4]);

				if (v < best_volume)
				{
					best_volume = v;
					best = i;
				}
			}
		}
	}
	return best;
}

H3_ALIGN static boolean level_holds_point(void *context, float const *p)
{
	long best;

	(void)context;
	best = level_clusters_of(p, level_clusters.mask);
	return best != NONE && best == level_clusters.made;
}

/* (h3_campaign.c) the structure BSP (the map's nth) a point is in, and the
one made; NONE: none */
long h3_level_point_bsp(float const *point, unsigned long mask)
{
	long best = level_clusters_of(point, mask);

	return best != NONE ? level_clusters.bsps[best].order : NONE;
}

long h3_level_made_bsp(void)
{
	return level_clusters.made != NONE ? level_clusters.bsps[level_clusters.made].order : NONE;
}

/* (h3_campaign.c) the level `name` made next on its nth structure BSP, the
player standing at position, facing (a yaw): asked before the map is loaded
again; arrived: the game ticks in it */
void h3_level_segment_request(char const *name, long bsp, float const *position, float facing)
{
	level_segment.pending = TRUE;
	level_segment.bsp = bsp;
	memcpy(level_segment.position, position, sizeof(level_segment.position));
	level_segment.facing = facing;
	snprintf(level_segment.name, sizeof(level_segment.name), "%s", name ? name : "");
}

void h3_level_segment_arrived(void)
{
	level_segment.pending = FALSE;
}

H3_ALIGN static boolean level_segment_for(char const *name)
{
	return level_segment.pending && name && !strcmp(level_segment.name, name);
}

/* (h3_characters.c) this engine's collision BSP (0x60 bytes at field of
region - the level's tag data being laid out) from a Halo 3 one in the
map's tag data (a collision model's: its eight blocks at address), its
surfaces keeping their materials (below material_count); FALSE if it
cannot be read or made */
boolean h3_level_collision_bsp_write(void *region, unsigned long field, struct h3_cache *cache, unsigned long address,
	long material_count)
{
	struct collision_bsp bsp;
	boolean made = TRUE;
	int k;

	memset(&bsp, 0, sizeof(bsp));
	for (k = 0; k < 8 && made; k++)
	{
		long count;
		unsigned long elements;

		if (!h3_cache_block(cache, address + 12 * (unsigned long)k, h3_collision_sizes[k], &count, &elements))
			count = 0;
		bsp.counts[k] = count;
		if (!count)
			continue;
		bsp.arrays[k] = malloc((unsigned long)count * h3_collision_sizes[k]);
		made = bsp.arrays[k] && h3_cache_read(cache, elements, (unsigned long)count * h3_collision_sizes[k], bsp.arrays[k]);
	}
	made = made && bsp.counts[0] > 0 && bsp.counts[5] > 0 &&
		collision_convert(region, field, &bsp, NONE, material_count > 0 ? material_count : 1, 1.0f);
	collision_free(&bsp);
	return made;
}

/* ---------- private code: the multiplayer level's netgame items

Halo 3's multiplayer scenario places its players' spawns as scenery (the
objects\multi\spawning\respawn_point markers - every game type - and each
game type's initial spawn points), its weapons (0x114, 0xA8 each), its
equipment, grenades and powerups (0xFC, 0x8C each) and its vehicles (0xE4,
0xA8 each) as placements: palette at 0, flags at 4 (never placed: 1, 8),
position at 8, yaw pitch roll at 0x14. Here they become this engine's: the
spawns its player starting locations (the game types this engine has: a
game type of Halo 3's it lacks - VIP, Territories, Assault, Infection - left
out), the items netgame equipment of an item collection each, the vehicles
vehicle placements - every item and vehicle the kit's STAND-IN for Halo 3's
(h3_stand_ins: each logged; M-B2/B3/B4/B5 replace them with Halo 3's own) */

static struct
{
	char const *h3;         /* in the palette object's path */
	unsigned long group;    /* the kit's tag's */
	char const *ce;
} const h3_stand_ins[] =
{
	{ "\\assault_rifle\\", 'weap', "weapons\\assault rifle\\assault rifle" },
	{ "\\smg\\", 'weap', "weapons\\assault rifle\\assault rifle" },
	{ "\\spike_rifle\\", 'weap', "weapons\\assault rifle\\assault rifle" },
	{ "\\battle_rifle\\", 'weap', "weapons\\pistol\\pistol" },
	{ "\\magnum\\", 'weap', "weapons\\pistol\\pistol" },
	{ "\\covenant_carbine\\", 'weap', "weapons\\pistol\\pistol" },
	{ "\\plasma_pistol\\", 'weap', "weapons\\plasma pistol\\plasma pistol" },
	{ "\\plasma_rifle\\", 'weap', "weapons\\plasma rifle\\plasma rifle" },
	{ "\\needler\\", 'weap', "weapons\\needler\\needler" },
	{ "\\shotgun\\", 'weap', "weapons\\shotgun\\shotgun" },
	{ "\\excavator\\", 'weap', "weapons\\shotgun\\shotgun" },
	{ "\\sniper_rifle\\", 'weap', "weapons\\sniper rifle\\sniper rifle" },
	{ "\\beam_rifle\\", 'weap', "weapons\\sniper rifle\\sniper rifle" },
	{ "\\rocket_launcher\\", 'weap', "weapons\\rocket launcher\\rocket launcher" },
	{ "\\spartan_laser\\", 'weap', "weapons\\rocket launcher\\rocket launcher" },
	{ "\\brute_shot\\", 'weap', "weapons\\rocket launcher\\rocket launcher" },
	{ "\\flak_cannon\\", 'weap', "weapons\\rocket launcher\\rocket launcher" },
	{ "\\frag_grenade\\", 'eqip', "weapons\\frag grenade\\frag grenade" },
	{ "\\plasma_grenade\\", 'eqip', "weapons\\plasma grenade\\plasma grenade" },
	{ "\\powerup_blue\\", 'eqip', "powerups\\active camouflage" },
	{ "\\powerup_red\\", 'eqip', "powerups\\over shield" },
	{ "\\warthog\\", 'vehi', "vehicles\\warthog\\warthog" },
	{ "\\ghost\\", 'vehi', "vehicles\\ghost\\ghost_mp" },
	{ "\\mongoose\\", 'vehi', "vehicles\\ghost\\ghost_mp" },
};

enum
{
	H3_STAND_IN_COUNT = sizeof(h3_stand_ins) / sizeof(h3_stand_ins[0]),
	NETGAME_MAXIMUM_STARTS = 128,
	NETGAME_MAXIMUM_ITEMS = 256,
};

struct netgame_placement
{
	float position[3];
	float yaw;
	short stand_in;         /* h3_stand_ins' (a start: its game type); NONE none */
	short path;             /* its palette's path (ng->paths) */
	short key;              /* (resolved) the collection it places, ng->keys'; NONE left out */
};

enum
{
	NETGAME_MAXIMUM_PATHS = 192,
	NETGAME_MAXIMUM_KEYS = 96,
	NETGAME_PATH_SIZE = 128,
};

/* a collection the level places: the sandbox's Halo 3 weapon or vehicle
(sandbox: its tag from the sandbox's first) or the kit's stand-in (kit: the
kit's tag index) */
struct netgame_key
{
	long sandbox;
	long kit;
	unsigned long group;
	boolean vehicle;
	short path;
};

struct netgame
{
	boolean multiplayer;
	long start_count, item_count, vehicle_count, left_out;
	struct netgame_placement starts[NETGAME_MAXIMUM_STARTS];
	struct netgame_placement items[NETGAME_MAXIMUM_ITEMS];
	struct netgame_placement vehicles[NETGAME_MAXIMUM_ITEMS];
	long kit[H3_STAND_IN_COUNT];     /* the kit's tag index of each stand-in (NONE: not in the kit) */
	/* the palettes' tag paths: the weapons' and equipment's first, the
	vehicles' after (what the sandbox makes: h3_sandbox_palettes) */
	char paths[NETGAME_MAXIMUM_PATHS][NETGAME_PATH_SIZE];
	char const *path_pointers[NETGAME_MAXIMUM_PATHS];
	long path_count, weapon_path_count;
	/* the collections placed (netgame_resolve) */
	struct netgame_key keys[NETGAME_MAXIMUM_KEYS];
	long key_count;
	short start_key[2];                 /* the players' start: Halo 3's assault rifle and magnum (their keys; NONE none) */
	long itmc[NETGAME_MAXIMUM_KEYS];    /* each key's item collection (NONE: a vehicle's), from 0 */
	long vehicle_slot[NETGAME_MAXIMUM_KEYS];
	long itmc_count, vehicle_kinds;
};

H3_ALIGN static long h3_stand_in(char const *path)
{
	long i;

	for (i = 0; path && i < H3_STAND_IN_COUNT; i++)
	{
		if (strstr(path, h3_stand_ins[i].h3))
			return i;
	}
	return NONE;
}

/* this engine's game type of a spawn marker (NONE: not one, or a game type
it lacks): every game (12), CTF 1, Slayer 2, Oddball 3, King 4 */
H3_ALIGN static short h3_spawn_game_type(char const *path)
{
	if (!path || !strstr(path, "\\multi\\"))
		return NONE;
	if (strstr(path, "\\spawning\\respawn_point"))
		return 12;
	if (strstr(path, "slayer_initial_spawn_point"))
		return 2;
	if (strstr(path, "ctf_initial_spawn_point"))
		return 1;
	if (strstr(path, "oddball_initial_spawn_point"))
		return 3;
	if (strstr(path, "koth_initial_spawn_point"))
		return 4;
	return NONE;
}

/* a palette's path kept (once), its index; NONE if there is no room */
H3_ALIGN static short netgame_path(struct netgame *ng, char const *path)
{
	long i;

	if (!path || !path[0])
		return NONE;
	for (i = 0; i < ng->path_count; i++)
	{
		if (!strcmp(ng->paths[i], path))
			return (short)i;
	}
	if (ng->path_count >= NETGAME_MAXIMUM_PATHS)
		return NONE;
	snprintf(ng->paths[ng->path_count], NETGAME_PATH_SIZE, "%s", path);
	ng->path_pointers[ng->path_count] = ng->paths[ng->path_count];
	return (short)ng->path_count++;
}

H3_ALIGN static void netgame_gather(struct build *build, unsigned char *kit, struct netgame *ng)
{
	static struct { unsigned long placements, palette, size; int kind; } const blocks[] = {
		{ H3_SCNR_SCENERY, H3_SCNR_SCENERY_PALETTE, 0xB4, 0 },
		{ H3_SCNR_WEAPONS, H3_SCNR_WEAPON_PALETTE, 0xA8, 1 },
		{ H3_SCNR_EQUIPMENT, H3_SCNR_EQUIPMENT_PALETTE, 0x8C, 1 },
		{ H3_SCNR_VEHICLES, H3_SCNR_VEHICLE_PALETTE, 0xA8, 2 },
	};
	struct h3_cache *cache = build->cache;
	unsigned b;
	long i;

	memset(ng, 0, sizeof(*ng));
	for (i = 0; i < H3_STAND_IN_COUNT; i++)
		ng->kit[i] = scene_pack_find(kit, h3_stand_ins[i].group, h3_stand_ins[i].ce);
	if (getenv("HALO_H3_NO_NETGAME"))
		return;
	/* the palettes' paths (the weapons' and equipment's, then the vehicles') */
	for (b = 1; b < sizeof(blocks) / sizeof(blocks[0]); b++)
	{
		long palette_count;
		unsigned long palette;

		if (b == 3)
			ng->weapon_path_count = ng->path_count;
		if (!h3_cache_block(cache, build->scenario + H3_SCNR(blocks[b].palette), 0x10, &palette_count, &palette))
			continue;
		for (i = 0; i < palette_count; i++)
			netgame_path(ng, h3_cache_tag_name(cache, h3_cache_reference(cache, palette + 0x10 * i)));
	}
	for (b = 0; b < sizeof(blocks) / sizeof(blocks[0]); b++)
	{
		long count, palette_count;
		unsigned long placements, palette;

		if (!h3_cache_block(cache, build->scenario + H3_SCNR(blocks[b].placements), blocks[b].size, &count, &placements) ||
			!h3_cache_block(cache, build->scenario + H3_SCNR(blocks[b].palette), 0x10, &palette_count, &palette))
		{
			continue;
		}
		for (i = 0; i < count; i++)
		{
			unsigned char e[0x20];
			short index;
			char const *path;
			struct netgame_placement p;

			if (!h3_cache_read(cache, placements + blocks[b].size * i, sizeof(e), e))
				continue;
			index = read16(e);
			if (index < 0 || index >= palette_count || (read32(e + 4) & 0x9))
				continue;
			path = h3_cache_tag_name(cache, h3_cache_reference(cache, palette + 0x10 * index));
			memcpy(p.position, e + 8, 12);
			p.yaw = readf(e + 0x14);
			p.key = NONE;
			p.path = NONE;
			if (blocks[b].kind == 0)
			{
				p.stand_in = h3_spawn_game_type(path);
				if (p.stand_in != NONE && ng->start_count < NETGAME_MAXIMUM_STARTS)
					ng->starts[ng->start_count++] = p;
				continue;
			}
			p.stand_in = (short)h3_stand_in(path);
			p.path = netgame_path(ng, path);
			/* (a stand-in of the other kind - an item's for a vehicle - is none) */
			if (p.stand_in != NONE && (blocks[b].kind == 2) != (h3_stand_ins[p.stand_in].group == 'vehi'))
				p.stand_in = NONE;
			if (blocks[b].kind == 2)
			{
				if (ng->vehicle_count < NETGAME_MAXIMUM_ITEMS)
					ng->vehicles[ng->vehicle_count++] = p;
			}
			else if (ng->item_count < NETGAME_MAXIMUM_ITEMS)
				ng->items[ng->item_count++] = p;
		}
	}
	ng->multiplayer = ng->start_count > 0;
}

/* the collection a placement places: its key (made if new), NONE if the
level has nothing for it (no Halo 3 one made, no stand-in in the kit) */
H3_ALIGN static short netgame_key(struct netgame *ng, struct h3_sandbox const *sandbox, struct netgame_placement const *p, boolean vehicle)
{
	char const *path = p->path != NONE ? ng->paths[p->path] : NULL;
	long sandbox_tag = vehicle ? h3_sandbox_vehicle(sandbox, path) : h3_sandbox_weapon(sandbox, path);
	long kit = p->stand_in != NONE ? ng->kit[p->stand_in] : NONE;
	long i;

	if (sandbox_tag == NONE && kit == NONE)
		return NONE;
	for (i = 0; i < ng->key_count; i++)
	{
		if (ng->keys[i].vehicle == vehicle && ng->keys[i].sandbox == sandbox_tag && (sandbox_tag != NONE || ng->keys[i].kit == kit))
			return (short)i;
	}
	if (ng->key_count >= NETGAME_MAXIMUM_KEYS)
		return NONE;
	ng->keys[ng->key_count].sandbox = sandbox_tag;
	ng->keys[ng->key_count].kit = sandbox_tag != NONE ? NONE : kit;
	ng->keys[ng->key_count].group = sandbox_tag != NONE ? (vehicle ? 'vehi' : 'weap') : h3_stand_ins[p->stand_in].group;
	ng->keys[ng->key_count].vehicle = vehicle;
	ng->keys[ng->key_count].path = p->path;
	if (vehicle)
	{
		ng->itmc[ng->key_count] = NONE;
		ng->vehicle_slot[ng->key_count] = ng->vehicle_kinds++;
	}
	else
	{
		ng->itmc[ng->key_count] = ng->itmc_count++;
		ng->vehicle_slot[ng->key_count] = NONE;
	}
	return (short)ng->key_count++;
}

/* every placement given its collection: the sandbox's Halo 3 weapon or
vehicle where one is made (h3_sandbox.c), else the kit's STAND-IN (logged),
else left out (logged) */
H3_ALIGN static void netgame_resolve(struct netgame *ng, struct h3_sandbox const *sandbox)
{
	/* (the players' start: Halo 3's default, the assault rifle in hand and
	the magnum - the game engine's starting equipment, the first held) */
	static char const *const start[2] =
	{
		"objects\\weapons\\rifle\\assault_rifle\\assault_rifle", "objects\\weapons\\pistol\\magnum\\magnum"
	};
	long i, kept, kind;

	for (i = 0; i < 2; i++)
	{
		struct netgame_placement p;

		memset(&p, 0, sizeof(p));
		p.stand_in = (short)h3_stand_in(start[i]);
		p.path = netgame_path(ng, start[i]);
		ng->start_key[i] = netgame_key(ng, sandbox, &p, FALSE);
		platform_log("h3 level: netgame: the players start with %s: %s", i ? "the magnum" : "the assault rifle in hand",
			ng->start_key[i] == NONE ? "none (not made, no stand-in)" : ng->keys[ng->start_key[i]].sandbox != NONE ? "Halo 3's" :
			"the kit's STAND-IN");
	}
	for (kind = 0; kind < 2; kind++)
	{
		struct netgame_placement *list = kind ? ng->vehicles : ng->items;
		long *count = kind ? &ng->vehicle_count : &ng->item_count;

		for (i = kept = 0; i < *count; i++)
		{
			struct netgame_placement *p = &list[i];
			char const *path = p->path != NONE ? ng->paths[p->path] : "?";

			p->key = netgame_key(ng, sandbox, p, kind != 0);
			if (p->key == NONE)
			{
				ng->left_out++;
				platform_log("h3 level: netgame: %s at %.1f %.1f %.1f left out (%s)", path, p->position[0], p->position[1], p->position[2],
					p->stand_in == NONE ? "no Halo 3 one made, no stand-in" : "its stand-in not in the kit");
				continue;
			}
			if (ng->keys[p->key].sandbox != NONE)
				platform_log("h3 level: netgame: Halo 3's %s at %.1f %.1f %.1f", path, p->position[0], p->position[1], p->position[2]);
			else
				platform_log("h3 level: netgame: STAND-IN %s -> %s at %.1f %.1f %.1f", path, h3_stand_ins[p->stand_in].ce, p->position[0],
					p->position[1], p->position[2]);
			list[kept++] = *p;
		}
		*count = kept;
	}
	platform_log("h3 level: netgame: %ld spawns, %ld items (%ld collections), %ld vehicles (%ld kinds), %ld placements left out",
		ng->start_count, ng->item_count, ng->itmc_count, ng->vehicle_count, ng->vehicle_kinds, ng->left_out);
}

/* a key's tag id: the sandbox's tag's (from sandbox_first) or the kit's
instance's */
H3_ALIGN static unsigned long netgame_key_id(struct region *tags, struct netgame const *ng, long key, unsigned long instances, long sandbox_first)
{
	if (ng->keys[key].sandbox != NONE)
		return tag_id(sandbox_first + ng->keys[key].sandbox);
	return read32(region_at(tags, instances + XBOX_TAG_INSTANCE_SIZE * ng->keys[key].kit + 12));
}

/* the scenario's multiplayer parts written (scenario_root: this engine's
scenario; instances the tag table; itmc_first the item collections' first
tag; sandbox_first the sandbox's): the starting locations, netgame
equipment, vehicle palette and placements, the item collections' tags, the
scenario multiplayer */
H3_ALIGN static void netgame_write(struct region *tags, struct netgame const *ng, unsigned long scenario_root,
	unsigned long instances, long itmc_first, long sandbox_first)
{
	unsigned long starts, equipment, palette, vehicles;
	long i;

	if (!ng->multiplayer)
	{
		/* a campaign level's weapons and equipment: scenario placements of
		their own kind (0x270, 0x5C each, palette 0x27C; 0x258, 0x28 each,
		palette 0x264), there when the level starts */
		int kind;

		for (kind = 0; kind < 2 && !tags->b.failed; kind++)
		{
			unsigned long group = kind ? 'eqip' : 'weap', size = kind ? 0x28 : 0x5C;
			long slot[NETGAME_MAXIMUM_KEYS], slots = 0, placed = 0, done = 0;
			unsigned long out;

			for (i = 0; i < ng->key_count; i++)
				slot[i] = NONE;
			for (i = 0; i < ng->item_count; i++)
			{
				long key = ng->items[i].key;

				if (ng->keys[key].group != group)
					continue;
				if (slot[key] == NONE)
					slot[key] = slots++;
				placed++;
			}
			if (!placed)
				continue;
			palette = region_block(tags, scenario_root + (kind ? 0x264 : 0x27C), slots, 48);
			for (i = 0; i < ng->key_count && !tags->b.failed; i++)
			{
				if (slot[i] != NONE)
					region_reference(tags, palette + 48 * slot[i], group, netgame_key_id(tags, ng, i, instances, sandbox_first));
			}
			out = region_block(tags, scenario_root + (kind ? 0x258 : 0x270), placed, size);
			for (i = 0; i < ng->item_count && !tags->b.failed; i++)
			{
				unsigned char *o;

				if (ng->keys[ng->items[i].key].group != group)
					continue;
				o = region_at(tags, out + size * done++);
				write16(o, (short)slot[ng->items[i].key]);
				write16(o + 2, -1);
				write16(o + 6, -1);
				memcpy(o + 8, ng->items[i].position, 12);
				writef(o + 0x14, ng->items[i].yaw);
				o[0x24] = 0xFF;
			}
		}
	}
	else
	{
	write16(region_at(tags, scenario_root + 0x3C), 1);
	starts = region_block(tags, scenario_root + 0x354, ng->start_count, 52);
	for (i = 0; i < ng->start_count && !tags->b.failed; i++)
	{
		unsigned char *o = region_at(tags, starts + 52 * i);

		memcpy(o, ng->starts[i].position, 12);
		writef(o + 12, ng->starts[i].yaw);
		write16(o + 0x14, ng->starts[i].stand_in);
	}
	equipment = region_block(tags, scenario_root + 0x384, ng->item_count, 144);
	for (i = 0; i < ng->item_count && !tags->b.failed; i++)
	{
		unsigned char *o = region_at(tags, equipment + 144 * i);

		write16(o + 4, 12);   /* every game */
		write16(o + 0xC, -1);
		write32(o + 0x10, 0xFFFFFFFFUL);
		memcpy(o + 0x40, ng->items[i].position, 12);
		writef(o + 0x4C, ng->items[i].yaw);
		region_reference(tags, equipment + 144 * i + 0x50, 'itmc', tag_id(itmc_first + ng->itmc[ng->items[i].key]));
	}
	/* the starting equipment (every game type): the start's collections */
	if (ng->start_key[0] != NONE || ng->start_key[1] != NONE)
	{
		unsigned long starting = region_block(tags, scenario_root + 0x390, 1, 204);
		long k, n = 0;

		if (!tags->b.failed)
		{
			write16(region_at(tags, starting + 4), 12);   /* every game */
			for (k = 0; k < 2; k++)
			{
				if (ng->start_key[k] != NONE)
					region_reference(tags, starting + 0x3C + 16 * n++, 'itmc', tag_id(itmc_first + ng->itmc[ng->start_key[k]]));
			}
		}
	}
	}
	palette = region_block(tags, scenario_root + 0x24C, ng->vehicle_kinds, 48);
	for (i = 0; i < ng->key_count && !tags->b.failed; i++)
	{
		if (ng->vehicle_slot[i] != NONE)
			region_reference(tags, palette + 48 * ng->vehicle_slot[i], 'vehi', netgame_key_id(tags, ng, i, instances, sandbox_first));
	}
	vehicles = region_block(tags, scenario_root + 0x240, ng->vehicle_count, 120);
	for (i = 0; i < ng->vehicle_count && !tags->b.failed; i++)
	{
		unsigned char *o = region_at(tags, vehicles + 120 * i);

		write16(o, (short)ng->vehicle_slot[ng->vehicles[i].key]);
		write16(o + 2, -1);
		write16(o + 6, -1);
		memcpy(o + 8, ng->vehicles[i].position, 12);
		writef(o + 0x14, ng->vehicles[i].yaw);
		o[0x24] = 0xFF;
		writef(o + 0x48, 1.0f);
		/* (its multiplayer spawn flags: every game type, placed by default) */
		write16(o + 0x5A, 0x3F3F);
	}
	/* the item collections: one item each, Halo 3's or the stand-in */
	for (i = 0; i < ng->key_count && !tags->b.failed; i++)
	{
		unsigned long root, permutations;

		if (ng->itmc[i] == NONE)
			continue;
		root = region_alloc(tags, 92);
		write32(region_at(tags, instances + XBOX_TAG_INSTANCE_SIZE * (itmc_first + ng->itmc[i]) + 20), tags->link + root);
		permutations = region_block(tags, root, 1, 84);
		if (tags->b.failed)
			break;
		writef(region_at(tags, permutations + 0x20), 1.0f);
		region_reference(tags, permutations + 0x24, ng->keys[i].group, netgame_key_id(tags, ng, i, instances, sandbox_first));
	}
}

/* ---------- private code: the Spartan's Halo 3 animation graph

The level's player is the kit's multiplayer cyborg (characters\cyborg_mp:
Halo CE's biped, whose bones Halo 3's Spartan has - h3_spartan.h - and whose
look the player's Halo 3 Spartan is drawn over, player_looks.c). Its graph
is made Halo 3's own (h3_animation.c: the map's masterchief jmad on the
cyborg's skeleton - names matched loosely, "bip01 l thigh" = "l_thigh" -)
and written as an antr tag of the level the biped points at.
HALO_H3_LEVEL_CE_ANIMATION=1 keeps Halo CE's graph. */

enum { SPARTAN_GRAPH_MAXIMUM_NODES = 64 };

/* the graph made from the kit's cyborg model (the pack as read, not yet
relinked) and the level's own map; NULL (why logged) if not */
H3_ALIGN static struct h2_graph *spartan_graph_make(struct build *build, unsigned char *kit)
{
	long count = 0, model, node_count, n;
	unsigned char *kit_instances = scene_pack_instances(kit, &count);
	unsigned long pack_link, nodes;
	unsigned char const *root;
	char names[SPARTAN_GRAPH_MAXIMUM_NODES][32];
	struct h2_animation_pose defaults[SPARTAN_GRAPH_MAXIMUM_NODES];
	short parents[SPARTAN_GRAPH_MAXIMUM_NODES];
	struct h2_graph *graph;
	char why[160];

	if (getenv("HALO_H3_LEVEL_CE_ANIMATION") || !kit_instances)
		return NULL;
	model = scene_pack_find(kit, 'mode', "characters\\cyborg\\cyborg");
	if (model == NONE)
	{
		platform_log("h3 level: the Spartan's graph: the kit has no cyborg model: Halo CE's graph stays");
		return NULL;
	}
	/* (the pack's addresses: its table's address, read from its header, is
	where its table lies) */
	pack_link = read32(kit) - (unsigned long)(kit_instances - kit);
	root = kit + (read32(kit_instances + XBOX_TAG_INSTANCE_SIZE * model + 20) - pack_link);
	node_count = (long)read32(root + 0xB8);
	nodes = read32(root + 0xBC) - pack_link;
	if (node_count < 1 || node_count > SPARTAN_GRAPH_MAXIMUM_NODES)
	{
		platform_log("h3 level: the Spartan's graph: the cyborg model has %ld nodes: Halo CE's graph stays", node_count);
		return NULL;
	}
	for (n = 0; n < node_count; n++)
	{
		unsigned char const *node = kit + nodes + 0x9C * n;
		int c;

		memcpy(names[n], node, 32);
		names[n][31] = 0;
		parents[n] = (short)read16(node + 0x24);
		for (c = 0; c < 3; c++)
			defaults[n].translation[c] = readf(node + 0x28 + 4 * c);
		for (c = 0; c < 4; c++)
			defaults[n].rotation[c] = readf(node + 0x34 + 4 * c);
		defaults[n].scale = 1.0f;
	}
	why[0] = 0;
	graph = h3_animation_graph_make_from(build->cache, "spartan", (char const (*)[32])names, defaults, parents, (short)node_count, why,
		sizeof(why));
	if (!graph)
		platform_log("h3 level: the Spartan's graph not made (%s): Halo CE's graph stays", why[0] ? why : "?");
	return graph;
}

/* (h2_graph_write) the level's tag data as the graph writer's host */
H3_ALIGN static unsigned long graph_host_alloc(void *context, unsigned long size)
{
	return region_alloc(context, size);
}

H3_ALIGN static unsigned char *graph_host_at(void *context, unsigned long offset)
{
	return region_at(context, offset);
}

H3_ALIGN static unsigned long graph_host_block(void *context, unsigned long field, long count, unsigned long element_size)
{
	return region_block(context, field, count, element_size);
}

H3_ALIGN static void graph_host_data(void *context, unsigned long field, void const *bytes, unsigned long size)
{
	region_data(context, field, bytes, size);
}

H3_ALIGN static boolean graph_host_failed(void *context)
{
	return ((struct region *)context)->b.failed;
}

/* the graph written as the level's antr tag index, the kit biped (its
tag index) given it; FALSE if it could not be */
H3_ALIGN static boolean spartan_graph_write(struct region *tags, unsigned long instances, struct h2_graph *graph, long index, long biped)
{
	struct h2_biped_host host;
	unsigned long root;

	memset(&host, 0, sizeof(host));
	host.context = tags;
	host.alloc = graph_host_alloc;
	host.at = graph_host_at;
	host.block = graph_host_block;
	host.data = graph_host_data;
	host.failed = graph_host_failed;
	host.link = tags->link;
	root = h2_graph_write(graph, &host);
	if (!root || tags->b.failed)
		return FALSE;
	write32(region_at(tags, instances + XBOX_TAG_INSTANCE_SIZE * index + 20), tags->link + root);
	if (biped != NONE)
	{
		unsigned long biped_root = read32(region_at(tags, instances + XBOX_TAG_INSTANCE_SIZE * biped + 20)) - tags->link;

		/* (object_definition.animation_graph) */
		region_reference(tags, biped_root + 0x38, 'antr', tag_id(index));
	}
	return !tags->b.failed;
}

/* (h3vita, HALO_H3_HEAP_LOG) the tag data's bytes by tag group - each tag's
the gap to the next tag's address, the last's to the data's end -, the
largest groups first: what fills the tag cache (the Vita's fixed one holds
22 MB; a level over it takes a large one, a block of user RAM of its own
there) */
struct tags_census_entry { unsigned long address, group; };
struct tags_census_group { unsigned long group, bytes; long count; };

H3_ALIGN static int tags_census_address_compare(void const *a, void const *b)
{
	unsigned long x = ((struct tags_census_entry const *)a)->address, y = ((struct tags_census_entry const *)b)->address;

	return x < y ? -1 : x > y ? 1 : 0;
}

H3_ALIGN static int tags_census_bytes_compare(void const *a, void const *b)
{
	unsigned long x = ((struct tags_census_group const *)a)->bytes, y = ((struct tags_census_group const *)b)->bytes;

	return x > y ? -1 : x < y ? 1 : 0;
}

H3_ALIGN static void tags_census_log(struct region *tags, unsigned long instances, long tag_count)
{
	enum { GROUPS = 96 };
	struct tags_census_entry *list = malloc((size_t)(tag_count + 1) * sizeof(*list));
	struct tags_census_group groups[GROUPS];
	char text[640];
	long n = 0, i, k, group_count = 0, at = 0;
	unsigned long total = 0;

	if (!list)
		return;
	for (i = 0; i < tag_count; i++)
	{
		unsigned char const *e = region_at(tags, instances + XBOX_TAG_INSTANCE_SIZE * i);
		unsigned long address = read32(e + 20);

		if (address < tags->link || address >= tags->link + tags->b.size)
			continue;
		list[n].address = address;
		list[n].group = read32(e);
		n++;
	}
	qsort(list, (size_t)n, sizeof(*list), tags_census_address_compare);
	for (i = 0; i < n; i++)
	{
		unsigned long next = i + 1 < n ? list[i + 1].address : tags->link + tags->b.size;
		unsigned long bytes = next - list[i].address;

		for (k = 0; k < group_count && groups[k].group != list[i].group; k++)
			;
		if (k == group_count)
		{
			if (group_count >= GROUPS)
				k = GROUPS - 1;
			else
			{
				groups[group_count].group = list[i].group;
				groups[group_count].bytes = 0;
				groups[group_count].count = 0;
				group_count++;
			}
		}
		groups[k].bytes += bytes;
		groups[k].count++;
		total += bytes;
	}
	free(list);
	qsort(groups, (size_t)group_count, sizeof(*groups), tags_census_bytes_compare);
	text[0] = 0;
	for (k = 0; k < group_count && k < 16 && at < (long)sizeof(text) - 40; k++)
	{
		unsigned long g = groups[k].group;

		at += snprintf(text + at, sizeof(text) - (size_t)at, " %c%c%c%c %lu KB (%ld)", (int)((g >> 24) & 255), (int)((g >> 16) & 255),
			(int)((g >> 8) & 255), (int)(g & 255), groups[k].bytes / 1024, groups[k].count);
	}
	platform_log("h3 level: tags by group (%lu KB in %ld tags):%s", total / 1024, n, text);
}

/* (h3_sandbox_write) the sandbox's bitmaps' pixels at the level's file's
end: their offset */
H3_ALIGN static unsigned long sandbox_pixels_put(void *context, void const *pixels, unsigned long size)
{
	struct stream *s = context;
	unsigned long at;

	stream_pad(s, 0x80);
	at = stream_offset(s);
	stream_put(s, pixels, size);
	return at;
}

/* the level maps_directory/name.map made: its tags into the tag cache at
tags_base (room bytes, linked where they lie), the virtual file's ranges
(the direct state's: nothing reads them until it is made), the header's
fields into header */
H3_ALIGN static boolean level_make(
	char const *maps_directory,
	char const *name,
	unsigned char *tags_base,
	unsigned long room,
	unsigned char *header,
	struct h3_level_build_result *result)
{
	struct build *build = calloc(1, sizeof(*build));
	unsigned long long started = vita_host_time_us();
	struct scene_pack_info kit_info;
	FILE *kit_file = NULL;
	unsigned char *kit = NULL;
	long kit_count = 0;
	struct stream *stream = NULL;
	struct region tags = { { 0 }, 0 };
	struct region bsp = { { 0 }, 0 };
	struct netgame *ng = NULL;
	struct h3_sandbox *sandbox = NULL;
	struct h2_graph *spartan_graph = NULL;
	long itmc_first = NONE, sandbox_first = NONE, spartan_graph_index = NONE, cyborg_mp = NONE;
	unsigned long kit_data_offset, bsp_file_offset, instances_offset = 0;
	long h3_first, scenario_index, sbsp_index, lightmap_index, shader_first, bitmap_first, tag_count;
	long collision_first, collision_count = 0;
	struct tag_layout layout;
	long sky_index = NONE, dobc_first = NONE;
	short *soso_shader = NULL;
	long soso_count = 0;
	unsigned long lightmap_file_offset;
	unsigned long scenario_root, sbsp_instance;
	boolean ok = FALSE;
	long i;
	char why[H3_CACHE_WHY_SIZE];

	memset(result, 0, sizeof(*result));
	memset(&layout, 0, sizeof(layout));
	heap_base = heap_peak = heap_in_use();
	if (!build)
		return FALSE;
	/* (the tags made where they lie: the tag cache, linked at its address) */
	tags.b.data = tags_base;
	tags.b.capacity = room;
	tags.b.fixed = TRUE;
	tags.link = (unsigned long)tags_base;
	build->result = result;
	build->lightprobe.atlas = NONE;
	h3_water_count = 0;
	memset(h3_water_fit, 0, sizeof(h3_water_fit));
	h3_water_light_count = 0;
	build->cache = h3_cache_open(maps_directory, name, why, sizeof(why));
	if (!build->cache)
	{
		build_fail(build, why);
		goto done;
	}
	h3_odst = FALSE;
#if !defined(HALO_VITA) && !defined(HALO_GXM_NULL)
	h3_odst = (h3_cache_find_tag(build->cache, 'achi', NULL) != NONE || h3_cache_find_tag(build->cache, 'aigl', NULL) != NONE) &&
		!(getenv("HALO_H3_ODST") && !atoi(getenv("HALO_H3_ODST")));
	if (h3_odst)
		platform_log("h3 level: %s is a Halo 3: ODST map: read by ODST's layouts", name);
#endif
	{
		long sbsp = h3_cache_find_tag(build->cache, 'sbsp', NULL);
		long lbsp = h3_cache_find_tag(build->cache, 'Lbsp', NULL);
		long scenario = h3_cache_find_tag(build->cache, 'scnr', NULL);
		struct h3_cache_tag t;

		if (sbsp == NONE || lbsp == NONE || scenario == NONE)
		{
			build_fail(build, "it has no structure BSP, lightmap or scenario");
			goto done;
		}
		h3_cache_tag_get(build->cache, scenario, &t);
		build->scenario = t.address;
		/* a campaign level's structure BSP: the one the Chief's first
		starting location is in (a cluster of its holds it; the smallest
		such cluster's), with the lightmap BSP of its place in the map's order - the
		map's first when none holds it, as a multiplayer level's one is.
		HALO_H3_BSP=n: the map's nth */
		{
			long start_count = 0, i, n = 0, best = NONE, wanted = level_segment_for(name) ? level_segment.bsp :
				getenv("HALO_H3_BSP") ? atol(getenv("HALO_H3_BSP")) : NONE;
			unsigned long starts = 0;
			float p[3], best_volume = 0.0f;
			boolean have = FALSE;

			if (h3_cache_block(build->cache, build->scenario + H3_SCNR(H3_SCNR_PLAYER_STARTS), H3_PLAYER_START_SIZE, &start_count, &starts))
			{
				for (i = 0; i < start_count && !have; i++)
				{
					unsigned char e[0x18];

					/* (ODST: the first insertion point's first start) */
					if (h3_cache_read(build->cache, starts + H3_PLAYER_START_SIZE * i, sizeof(e), e) && read16(e + (h3_odst ? 0x14 : 0x16)) == 0)
					{
						memcpy(p, e, 12);
						have = TRUE;
					}
				}
			}
			for (i = 0; (have || wanted != NONE) && i < h3_cache_tag_count(build->cache); i++)
			{
				struct h3_cache_tag s;
				unsigned char const *b;

				if (!h3_cache_tag_get(build->cache, i, &s) || s.group_tag != 'sbsp')
					continue;
				b = h3_cache_at(build->cache, s.address + H3_SBSP_WORLD_BOUNDS, 24);
				if (n == wanted)
				{
					best = n;
					sbsp = i;
					break;
				}
				if (b && wanted == NONE && p[0] >= readf(b) && p[0] <= readf(b + 4) && p[1] >= readf(b + 8) && p[1] <= readf(b + 12) &&
					p[2] >= readf(b + 16) && p[2] <= readf(b + 20))
				{
					/* (its clusters' bounds: the smallest cluster that holds the
					start, of any BSP - a BSP's world bounds hold much that is
					another's: The Covenant's and Floodgate's first, whose
					clusters do not reach it, the first whose bounds hold it
					when no cluster does) */
					long cluster_count = 0, c;
					unsigned long clusters = 0;
					float volume = 3.0e38f;

					if (h3_cache_block(build->cache, s.address + H3_SBSP_CLUSTERS, H3_CLUSTER_SIZE, &cluster_count, &clusters))
					{
						for (c = 0; c < cluster_count; c++)
						{
							unsigned char const *q = h3_cache_at(build->cache, clusters + H3_CLUSTER_SIZE * c, 24);

							if (q && p[0] >= readf(q) && p[0] <= readf(q + 4) && p[1] >= readf(q + 8) && p[1] <= readf(q + 12) &&
								p[2] >= readf(q + 16) && p[2] <= readf(q + 20))
							{
								float v = (readf(q + 4) - readf(q)) * (readf(q + 12) - readf(q + 8)) * (readf(q + 20) - readf(q + 16));

								if (v < volume)
									volume = v;
							}
						}
					}
					if (best == NONE || volume < best_volume)
					{
						best = n;
						best_volume = volume;
						sbsp = i;
					}
				}
				n++;
			}
			if (best > 0)
			{
				for (i = n = 0; i < h3_cache_tag_count(build->cache); i++)
				{
					struct h3_cache_tag s;

					if (!h3_cache_tag_get(build->cache, i, &s) || s.group_tag != 'Lbsp')
						continue;
					if (n++ == best)
					{
						lbsp = i;
						break;
					}
				}
				platform_log("h3 level: structure BSP %ld (%s; lightmap %s): the player's start %.1f %.1f %.1f is in it", best,
					h3_cache_tag_name(build->cache, sbsp), h3_cache_tag_name(build->cache, lbsp), p[0], p[1], p[2]);
			}
		}
		h3_cache_tag_get(build->cache, sbsp, &t);
		build->sbsp = t.address;
		h3_cache_tag_get(build->cache, lbsp, &t);
		build->lbsp = t.address;
		/* (every BSP's clusters' bounds: which BSP a point is in) */
		level_clusters_read(build);
	}

	/* (HALO_H3_REF_DEBUG=text: the tags whose names hold it, and the tags that refer to each) */
	if (getenv("HALO_H3_REF_DEBUG"))
	{
		struct h3_cache *cache = build->cache;
		long n = h3_cache_tag_count(cache), i, j, k;

		for (i = 0; i < n; i++)
		{
			struct h3_cache_tag ti;

			if (!h3_cache_tag_get(cache, i, &ti) || !strstr(h3_cache_tag_name(cache, i), getenv("HALO_H3_REF_DEBUG")))
				continue;
			platform_log("h3 refs: %c%c%c%c %s (tag %ld)", (char)(ti.group_tag >> 24), (char)(ti.group_tag >> 16), (char)(ti.group_tag >> 8),
				(char)ti.group_tag, h3_cache_tag_name(cache, i), i);
			for (j = 0; j < n; j++)
			{
				struct h3_cache_tag tj, tk;
				unsigned long end = 0xFFFFFFFFUL, at;

				if (j == i || !h3_cache_tag_get(cache, j, &tj) || !tj.address)
					continue;
				for (k = 0; k < n; k++)
				{
					if (h3_cache_tag_get(cache, k, &tk) && tk.address > tj.address && tk.address < end)
						end = tk.address;
				}
				if (end == 0xFFFFFFFFUL || end - tj.address > 0x40000)
					end = tj.address + 0x40000;
				for (at = tj.address; at + 16 <= end; at += 4)
				{
					unsigned char const *e = h3_cache_at(cache, at, 16);

					if (!e)
						break;
					if (read32(e) == ti.group_tag && !read32(e + 4) && !read32(e + 8) && (read32(e + 12) & 0xFFFF) == (unsigned long)i &&
						read32(e + 12) != 0xFFFFFFFFUL)
					{
						platform_log("h3 refs:   from %c%c%c%c %s +0x%lX", (char)(tj.group_tag >> 24), (char)(tj.group_tag >> 16),
							(char)(tj.group_tag >> 8), (char)tj.group_tag, h3_cache_tag_name(cache, j), at - tj.address);
						break;
					}
				}
			}
		}
	}
	/* the player's things */
	kit = kit_load(build, &kit_info, &kit_file);
	if (!kit)
		goto done;
	scene_pack_instances(kit, &kit_count);
	result->kit_tag_count = kit_count;
	/* (a multiplayer level's spawns, items and vehicles) */
	ng = malloc(sizeof(*ng));
	if (!ng)
	{
		build_fail(build, "out of memory");
		goto done;
	}
	netgame_gather(build, kit, ng);
	/* (the sandbox - the Halo 3 weapons, grenades and vehicles the scenario
	can place - is made after the lighting, below: h3vita) */
	/* the Spartan's Halo 3 animation graph on the kit's cyborg */
	spartan_graph = spartan_graph_make(build, kit);
	cyborg_mp = scene_pack_find(kit, 'bipd', "characters\\cyborg_mp\\cyborg_mp");
	/* (h3vita: the kit's 6 MB of tags are the writes' - and the sandbox's -:
	let go through the level's read, gather and lighting, read again from the
	pack before the sandbox; HALO_H3_KIT_RELOAD=0 keeps them) */
	if (!getenv("HALO_H3_KIT_RELOAD") || atoi(getenv("HALO_H3_KIT_RELOAD")))
	{
		free(kit);
		kit = NULL;
	}

	/* the virtual file: its header, then as they are made the kit's pixels
	and samples (h2kit.pack's data, read from it when asked), the bitmaps'
	(as they are read), the lightmaps', the BSP */
	stream = calloc(1, sizeof(*stream));
	if (!stream)
	{
		build_fail(build, "out of memory");
		goto done;
	}
	build->stream = stream;
	kit_data_offset = stream_offset(stream);
	stream_put_pack(stream, kit_info.data_offset, kit_info.data_size);
	if (!level_read(build))
		goto done;
	HEAP_SAMPLE();
	for (i = 0; i < build->bitmap_count; i++)
	{
		/* (the made and hazed sky's maps come after the lighting: sky_haze_write) */
		if (!build->bitmaps[i].written && !build->bitmaps[i].generated && !(build->bitmaps[i].hazed && build->bitmaps[i].held_rgba))
		{
			platform_log("h3 level: bitmap %ld of %ld not written: %s, format %d, %dx%d, mode %d, page %ld, composite %d, generated %d",
				i, build->bitmap_count, build->bitmaps[i].path, (int)build->bitmaps[i].format, (int)build->bitmaps[i].width,
				(int)build->bitmaps[i].height, (int)build->bitmaps[i].mode, build->bitmaps[i].page,
				(int)build->bitmaps[i].composite, (int)build->bitmaps[i].generated);
			build_fail(build, "a bitmap's pixels cannot be read");
			goto done;
		}
	}
	if (!level_gather(build))
		goto done;
	HEAP_SAMPLE();
	geometry_close(&build->bsp_geometry);
	geometry_close(&build->sky_geometry);
	/* (h3vita: the props' and the decorators' buffers as read are the
	gather's alone - the models made of them are the writes' -: let go here,
	before the lighting, not at the end) */
	for (i = 0; i < build->prop_count; i++)
	{
		if (build->props[i].geometry_open)
			geometry_close(&build->props[i].geometry);
		build->props[i].geometry_open = FALSE;
	}
	for (i = 0; i < build->decorator_count; i++)
		geometry_close(&build->decorators[i].geometry);
	if (build->decorator_buffers_open)
		geometry_close(&build->decorator_buffers);
	build->decorator_buffers_open = FALSE;
	HEAP_SAMPLE();
	lightprobe_bake(build);
#if !defined(HALO_VITA) && !defined(HALO_GXM_NULL)
	object_light_gather(build);
#endif
	sky_haze_write(build);
	/* (h3/light-1) the frame pass's: the atmospheres and the exposure the
	light was made at */
	{
		struct h3_frame_level *f = h3_frame_level();

		frame_atmospheres_write(build, f);
		f->bake_exposure = build->exposure > 0.0f ? build->exposure : 1.0f;
		f->sky_bake_exposure = sky_exposure_of(build);
	}
	free(build->sky_dome_image);
	build->sky_dome_image = NULL;
	for (i = 0; i < build->bitmap_count; i++)
	{
		if (!build->bitmaps[i].written)
		{
			platform_log("h3 level: sky bitmap %ld not written: %s", i, build->bitmaps[i].path);
			build_fail(build, "a sky bitmap could not be made");
			goto done;
		}
	}
	stream_pad(stream, 0x80);
	lightmap_file_offset = stream_offset(stream);
	{
		unsigned char *p = malloc((unsigned long)LIGHTMAP_ATLAS * LIGHTMAP_ATLAS * 2);

		for (i = 0; p && i < build->atlas_count; i++)
		{
			long x, y;

			for (y = 0; y < LIGHTMAP_ATLAS; y++)
			{
				for (x = 0; x < LIGHTMAP_ATLAS; x++)
				{
					unsigned char const *c = build->atlases[i].rgb + 3 * (y * LIGHTMAP_ATLAS + x);
					/* (ordered dither, 4 x 4: the light's steps - 32 a channel,
					its half the albedo's overbright - spread, not banded) */
					static unsigned char const bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
					unsigned long d = bayer[(y & 3) * 4 + (x & 3)];
					unsigned long r = c[0] + (d >> 1), g = c[1] + (d >> 2), b = c[2] + (d >> 1);
					unsigned long v = ((unsigned long)((r > 255 ? 255 : r) >> 3) << 11) | ((unsigned long)((g > 255 ? 255 : g) >> 2) << 5) |
						((b > 255 ? 255 : b) >> 3);

					write16(p + 2 * morton(x, y), (long)v);
				}
			}
			stream_put(stream, p, (unsigned long)LIGHTMAP_ATLAS * LIGHTMAP_ATLAS * 2);
			free(build->atlases[i].rgb);
			build->atlases[i].rgb = NULL;
			result->lightmap_bytes += LIGHTMAP_ATLAS * LIGHTMAP_ATLAS * 2;
		}
		if (!p)
			stream->failed = TRUE;
		free(p);
	}
	if (stream->failed)
	{
		build_fail(build, "out of memory for the level's pixels and lightmaps");
		goto done;
	}

	/* the tag data: header, table, names, the kit, the level's tags */
	h3_first = kit_count;
	scenario_index = h3_first;
	sbsp_index = h3_first + 1;
	lightmap_index = h3_first + 2;
	shader_first = h3_first + 3;
	/* (the models' environment shaders as model shaders: one each) */
	layout.shader_soso = malloc((build->shader_count + 1) * sizeof(short));
	soso_shader = malloc((build->shader_count + 1) * sizeof(short));
	layout.slot_collision = malloc((build->slot_count + 1) * sizeof(long));
	if (!layout.shader_soso || !soso_shader || !layout.slot_collision)
	{
		build_fail(build, "out of memory");
		goto done;
	}
	for (i = 0; i < build->shader_count; i++)
		layout.shader_soso[i] = NONE;
	for (i = 0; i < build->model_count; i++)
	{
		long k;

		for (k = 0; k < build->models[i].part_count; k++)
		{
			short shader = build->models[i].parts[k].shader;

			if (build->shaders[shader].group == 'senv' && layout.shader_soso[shader] == NONE)
			{
				layout.shader_soso[shader] = (short)soso_count;
				soso_shader[soso_count++] = shader;
			}
		}
	}
	layout.shader_first = shader_first;
	layout.soso_first = shader_first + build->shader_count;
	bitmap_first = layout.soso_first + soso_count;
	collision_first = bitmap_first + build->bitmap_count;
	collision_count = 0;
	for (i = 0; i < build->slot_count; i++)
	{
		struct object_key const *key = &build->keys[build->slot_key[i]];
		boolean collides = key_collision(build, key) != NULL;

		layout.slot_collision[i] = collides ? collision_first + collision_count++ : NONE;
	}
	layout.model_first = collision_first + collision_count;
	layout.object_first = layout.model_first + build->model_count;
	tag_count = layout.object_first + build->slot_count;
	sky_index = build->sky_model_index != NONE ? tag_count++ : NONE;
	/* (the grass's detail object collections) */
	dobc_first = tag_count;
	if (build->grass_object_count)
		tag_count += build->grass_set_count;
	layout.gear_object = layout.gear_graph = NONE;
	if (build->gear.found && build->gear.model_index != NONE)
	{
		layout.gear_object = tag_count++;
		layout.gear_graph = tag_count++;
	}
	/* the Halo 3 weapons, grenades and vehicles the scenario can place, made
	from this map (h3_sandbox.c); then each placement's collection. (h3vita)
	Made here, after the level's read, gather and lighting, not before them:
	its working set - the models, their maps, the weapons' animations, some
	20 MB on Guardian - no longer lies under the lighting's buffers at the
	make's peak; its made maps' pixels go to the virtual file at once, so
	they are not held by the heap until the writes */
	if (!kit && !(kit = kit_reload(build, &kit_info, kit_file)))
		goto done;
	{
		struct h3_sandbox_palettes palettes;

		palettes.weapons = ng->path_pointers;
		palettes.weapon_count = ng->weapon_path_count;
		palettes.vehicles = ng->path_pointers + ng->weapon_path_count;
		palettes.vehicle_count = ng->path_count - ng->weapon_path_count;
		/* (a mission's squads and characters: h3_characters.c) */
		palettes.scenario = ng->multiplayer ? 0 : build->scenario;
		palettes.in_level = level_holds_point;
		palettes.context = build;
		sandbox = h3_sandbox_gather(maps_directory, name, build->cache, &palettes, kit);
		if (sandbox)
			h3_sandbox_pixels_flush(sandbox, sandbox_pixels_put, stream);
	}
	netgame_resolve(ng, sandbox);
	HEAP_SAMPLE();
	/* (the netgame items' collections) */
	itmc_first = tag_count;
	tag_count += ng->itmc_count;
	/* (the sandbox's: h2_weapons.c's tags, shaders, bitmaps, models) */
	sandbox_first = tag_count;
	tag_count += h3_sandbox_tag_count(sandbox);
	build->sandbox_buffer_count = h3_sandbox_buffer_count(sandbox);
	/* (the Spartan's Halo 3 graph) */
	spartan_graph_index = spartan_graph ? tag_count++ : NONE;
	result->tag_count = tag_count;
	{
		unsigned long header = region_alloc(&tags, XBOX_TAG_HEADER_SIZE);
		unsigned long instances = region_alloc(&tags, XBOX_TAG_INSTANCE_SIZE * (unsigned long)tag_count);

		instances_offset = instances;
		unsigned long kit_offset;
		unsigned char *kit_instances;
		long count, ar, pistol;

		ar = scene_pack_find(kit, 'weap', "weapons\\assault rifle\\assault rifle");
		pistol = scene_pack_find(kit, 'weap', "weapons\\pistol\\pistol");
		scene_pack_rebase(kit, 0, 0xE174, (long)(kit_data_offset - SCENE_PACK_FILE_BASE));
		kit_offset = region_alloc(&tags, kit_info.tags_size);
		if (tags.b.failed || !kit_relink(kit, kit_info.tags_size, tags.link + kit_offset))
		{
			build_fail(build, "the globals' pack cannot be placed");
			goto done;
		}
		memcpy(region_at(&tags, kit_offset), kit, kit_info.tags_size);
		kit_instances = region_at(&tags, read32(kit) - tags.link);
		count = (long)read32(kit + 12);
		memcpy(region_at(&tags, instances), kit_instances, XBOX_TAG_INSTANCE_SIZE * (unsigned long)count);
		write32(region_at(&tags, header), tags.link + instances);
		write32(region_at(&tags, header + 4), tag_id(scenario_index));
		write32(region_at(&tags, header + 8), 0);
		write32(region_at(&tags, header + 12), tag_count);
		memcpy(region_at(&tags, header + 16), kit + 16, 16);
		write32(region_at(&tags, header + 32), 'tags');

		free(reference_names);
		reference_names = calloc(tag_count + 1, sizeof(unsigned long));
		reference_name_count = reference_names ? tag_count : 0;
		for (i = 0; i < count && reference_names; i++)
			reference_names[i] = read32(region_at(&tags, instances + XBOX_TAG_INSTANCE_SIZE * i + 16));

		for (i = h3_first; i < tag_count; i++)
		{
			char text[300];
			unsigned long group = 0, name_offset;
			long size;

			if (i == scenario_index)
				group = 'scnr', snprintf(text, sizeof(text), "h3\\%s", h3_cache_name(build->cache));
			else if (i == sbsp_index)
				group = 'sbsp', snprintf(text, sizeof(text), "h3\\%s_bsp", h3_cache_name(build->cache));
			else if (i == lightmap_index)
				group = 'bitm', snprintf(text, sizeof(text), "h3\\%s_lightmaps", h3_cache_name(build->cache));
			else if (i < layout.soso_first)
				group = build->shaders[i - shader_first].group, snprintf(text, sizeof(text), "%s", build->shaders[i - shader_first].path);
			else if (i < bitmap_first)
				group = 'soso', snprintf(text, sizeof(text), "%s model", build->shaders[soso_shader[i - layout.soso_first]].path);
			else if (i < collision_first)
				group = 'bitm', snprintf(text, sizeof(text), "%s", build->bitmaps[i - bitmap_first].path);
			else if (i < layout.model_first)
				group = 'coll', snprintf(text, sizeof(text), "h3\\instances\\collision %ld", i - collision_first);
			else if (i < layout.object_first)
				group = 'mode', snprintf(text, sizeof(text), "h3\\instances\\model %ld", i - layout.model_first);
			else if (i == sky_index)
				group = 'sky ', snprintf(text, sizeof(text), "h3\\%s_sky", h3_cache_name(build->cache));
			else if (build->grass_object_count && i >= dobc_first && i < dobc_first + build->grass_set_count)
				group = 'dobc', snprintf(text, sizeof(text), "h3\\decorators\\%s", build->grass_sets[i - dobc_first].name);
			else if (i == layout.gear_object)
				group = 'mach', snprintf(text, sizeof(text), "h3\\machines\\%s", build->gear.name);
			else if (i == layout.gear_graph)
				group = 'antr', snprintf(text, sizeof(text), "h3\\machines\\%s", build->gear.name);
			else if (i >= itmc_first && i < itmc_first + ng->itmc_count)
			{
				long n;
				char const *path = "item", *slash;

				for (n = 0; n < ng->key_count && ng->itmc[n] != i - itmc_first; n++)
					;
				if (n < ng->key_count && ng->keys[n].path != NONE)
					path = ng->paths[ng->keys[n].path];
				slash = strrchr(path, '\\');
				group = 'itmc', snprintf(text, sizeof(text), "h3\\items\\%s", slash ? slash + 1 : path);
			}
			else if (sandbox && i >= sandbox_first && i < sandbox_first + h3_sandbox_tag_count(sandbox))
				h3_sandbox_tag_name(sandbox, i - sandbox_first, &group, text, sizeof(text));
			else if (i == spartan_graph_index)
				group = 'antr', snprintf(text, sizeof(text), "h3\\characters\\spartan");
			else
				group = slot_group(i - layout.object_first), snprintf(text, sizeof(text), "h3\\instances\\object %ld",
					i - layout.object_first);
			size = (long)strlen(text) + 1;
			name_offset = region_alloc(&tags, (unsigned long)size);
			if (tags.b.failed)
				break;
			memcpy(region_at(&tags, name_offset), text, size);
			{
				unsigned char *instance = region_at(&tags, instances + XBOX_TAG_INSTANCE_SIZE * i);

				unsigned long parent, grandparent;

				h3_sandbox_tag_parents(group, &parent, &grandparent);
				write32(instance, group);
				write32(instance + 4, parent);
				write32(instance + 8, grandparent);
				write32(instance + 12, tag_id(i));
				write32(instance + 16, tags.link + name_offset);
				if (reference_names)
					reference_names[i] = tags.link + name_offset;
			}
		}

		/* the scenario: its BSP (patched below), the player's starts and weapons */
		scenario_root = region_alloc(&tags, 1456);
		write32(region_at(&tags, instances + XBOX_TAG_INSTANCE_SIZE * scenario_index + 20), tags.link + scenario_root);
		{
			unsigned long starts, profiles;
			long start_count = 0;
			unsigned long h3_starts = 0;
			static unsigned long const null_references[] = { 0x0, 0x10, 0x20, 0x574, 0x584, 0x594 };
			int r;

			for (r = 0; r < (int)(sizeof(null_references) / sizeof(null_references[0])); r++)
				region_reference(&tags, scenario_root + null_references[r], 0, 0xFFFFFFFFUL);
			{
				unsigned long nodes = 19001, size = 0x38 + nodes * 20;
				unsigned char *syntax = calloc(size, 1);
				unsigned char *strings = calloc(0x400, 1);

				if (syntax && strings)
				{
					memcpy(syntax, "script node", 12);
					write16(syntax + 0x20, (long)nodes);
					write16(syntax + 0x22, 20);
					syntax[0x24] = 1;
					write32(syntax + 0x28, 0x64407440UL);
					write16(syntax + 0x32, 0xE373);
					region_data(&tags, scenario_root + 0x474, syntax, size);
					region_data(&tags, scenario_root + 0x488, strings, 0x400);
				}
				free(syntax);
				free(strings);
			}
			{
				unsigned long bsps = region_block(&tags, scenario_root + 0x5A4, 1, 32);

				region_reference(&tags, bsps + 0x10, 'sbsp', tag_id(sbsp_index));
			}
			/* Halo 3's player starting locations (0x24C; 0x18 each: position,
			facing, pitch, and at 0x16 the campaign player it is for - 0 the
			Chief, the rest the cooperative players'): a campaign level's are
			the Chief's (all of them if none says so); a multiplayer level's
			are its spawn markers (netgame_write) */
			h3_cache_block(build->cache, build->scenario + H3_SCNR(H3_SCNR_PLAYER_STARTS), H3_PLAYER_START_SIZE, &start_count, &h3_starts);
			if (start_count > 64)
				start_count = 64;
			{
				unsigned char h[64][0x18];
				long kept = 0, chiefs = 0;

				for (count = 0; count < start_count; count++)
				{
					if (h3_cache_read(build->cache, h3_starts + H3_PLAYER_START_SIZE * count, 0x18, h[kept]))
					{
						/* (ODST: the starts of the first insertion point the level's) */
						if (h3_odst)
							write16(h[kept] + 0x16, read16(h[kept] + 0x14));
						if (read16(h[kept] + 0x16) == 0)
							chiefs++;
						kept++;
					}
				}
				if (chiefs && !getenv("HALO_H3_ALL_STARTS"))
				{
					long n = 0;

					for (count = 0; count < kept; count++)
					{
						if (read16(h[count] + 0x16) == 0)
							memmove(h[n++], h[count], 0x18);
					}
					kept = n;
				}
				/* (a mission's next structure BSP: the player where he stood;
				a BSP none of the starts is in - HALO_H3_BSP's -: beside the
				first of its squads that is the player's side, h3_characters.c) */
				if (!ng->multiplayer)
				{
					float fallback[4];
					boolean inside = FALSE;
					long k;

					for (k = 0; k < kept && !inside; k++)
					{
						float p[3];

						memcpy(p, h[k], 12);
						inside = level_holds_point(build, p);
					}
					if (level_segment_for(name))
					{
						memcpy(h[0], level_segment.position, 12);
						memcpy(h[0] + 12, &level_segment.facing, 4);
						kept = 1;
						platform_log("h3 level: the player's start: where he stood as the BSP changed");
					}
					else if (!inside && h3_sandbox_friendly_start(sandbox, fallback))
					{
						memset(h[0], 0, 0x18);
						memcpy(h[0], fallback, 16);
						kept = 1;
						platform_log("h3 level: no player start in this BSP: beside its first friendly squad");
					}
				}
				start_count = kept;
				starts = region_block(&tags, scenario_root + 0x354, start_count ? start_count : 1, 52);
				for (count = 0; count < (start_count ? start_count : 1) && !tags.b.failed; count++)
				{
					unsigned char *o = region_at(&tags, starts + 52 * count);

					if (start_count)
					{
						memcpy(o, h[count], 16);
						if (!ng->multiplayer)
							platform_log("h3 level: player start %ld at %.2f %.2f %.2f facing %.1f", count, readf(o), readf(o + 4),
								readf(o + 8), readf(o + 12) * 57.29578f);
					}
					else
						writef(o + 8, 1.0f);
				}
			}
			/* a campaign level's starting profiles: Halo 3's (0x240, 0x58
			each, this engine's own layout through the grenades: name,
			starting health and shield damage at 0x20, primary weapon at 0x28
			and its rounds loaded and total at 0x38, the secondary's at 0x3C
			and 0x4C, frag and plasma grenades at 0x50), in its order - the
			player spawns with the first. Their weapons are the sandbox's
			Halo 3 ones (the kit's stand-in where one is not made) */
			{
				long profile_count = 0, done = 0;
				unsigned long h3_profiles = 0;

				if (!ng->multiplayer && !getenv("HALO_H3_NO_PROFILES"))
					h3_cache_block(build->cache, build->scenario + H3_SCNR(0x240), 0x58, &profile_count, &h3_profiles);
				if (profile_count > 32)
					profile_count = 32;
				profiles = region_block(&tags, scenario_root + 0x348, profile_count > 0 ? profile_count : 1, 104);
				for (count = 0; count < profile_count && !tags.b.failed; count++)
				{
					unsigned char h[0x58];
					unsigned long weapon[2];
					int k;

					if (!h3_cache_read(build->cache, h3_profiles + 0x58 * count, sizeof(h), h))
						break;
					for (k = 0; k < 2; k++)
					{
						char const *path = h3_cache_tag_name(build->cache, h3_cache_reference(build->cache,
							h3_profiles + 0x58 * count + (k ? 0x3C : 0x28)));
						long made = path ? h3_sandbox_weapon(sandbox, path) : NONE;
						long stand_in = path ? h3_stand_in(path) : NONE;
						unsigned long field = profiles + 104 * count + (k ? 0x3C : 0x28);
						short loaded = (short)read16(h + (k ? 0x4C : 0x38)), total = (short)read16(h + (k ? 0x4E : 0x3A));

						weapon[k] = 0xFFFFFFFFUL;
						if (made != NONE)
							weapon[k] = tag_id(sandbox_first + made);
						else if (stand_in != NONE && h3_stand_ins[stand_in].group == 'weap' && ng->kit[stand_in] != NONE)
							weapon[k] = read32(region_at(&tags, instances + XBOX_TAG_INSTANCE_SIZE * ng->kit[stand_in] + 12));
						region_reference(&tags, field, 'weap', weapon[k]);
						/* (Halo 3's rounds total counts the loaded ones too, as
						Halo 2's does; this engine's profile's is the rest) */
						write16(region_at(&tags, field + 0x10), weapon[k] != 0xFFFFFFFFUL ? loaded : 0);
						write16(region_at(&tags, field + 0x12), weapon[k] != 0xFFFFFFFFUL && total > loaded ? total - loaded : 0);
						if (count == 0)
							platform_log("h3 level: starting profile '%.31s' weapon %d: %s (%s), %d loaded of %d", (char const *)h, k,
								path ? path : "none", made != NONE ? "Halo 3's" : weapon[k] != 0xFFFFFFFFUL ? "the kit's STAND-IN" : "none",
								loaded, total);
					}
					{
						unsigned char *o = region_at(&tags, profiles + 104 * count);

						memcpy(o, h, 31);
						o[31] = 0;
						/* (this engine's modifiers are added to a unit's emptied
						vitality: full less Halo 3's damage) */
						writef(o + 0x20, 1.0f - readf(h + 0x20));
						writef(o + 0x24, 1.0f - readf(h + 0x24));
						o[0x50] = h[0x50];
						o[0x51] = h[0x51];
					}
					done++;
				}
				if (!tags.b.failed && !done)
				{
					unsigned char *o = region_at(&tags, profiles);

					memcpy(o, "h3_start", 9);
					writef(o + 0x20, 1.0f);
					writef(o + 0x24, 1.0f);
					region_reference(&tags, profiles + 0x28, 'weap', ar != NONE ? read32(region_at(&tags, instances + 32 * ar + 12)) : 0xFFFFFFFFUL);
					o = region_at(&tags, profiles);
					write16(o + 0x38, 60);
					write16(o + 0x3A, 240);
					region_reference(&tags, profiles + 0x3C, 'weap', pistol != NONE ? read32(region_at(&tags, instances + 32 * pistol + 12)) : 0xFFFFFFFFUL);
					o = region_at(&tags, profiles);
					write16(o + 0x4C, 12);
					write16(o + 0x4E, 48);
					o[0x50] = 2;
				}
			}
		}

		/* the multiplayer level's spawns, items and vehicles */
		netgame_write(&tags, ng, scenario_root, instances, itmc_first, sandbox_first);

		/* the sky: its model, in the scenario's skies */
		if (sky_index != NONE)
		{
			unsigned long root = region_alloc(&tags, 0xD0);
			unsigned long skies = region_block(&tags, scenario_root + 0x30, 1, 16);

			boolean sky_model = TRUE;

			write32(region_at(&tags, instances + XBOX_TAG_INSTANCE_SIZE * sky_index + 20), tags.link + root);
#if !defined(HALO_VITA) && !defined(HALO_GXM_NULL)
			/* (h3/pc-acc: the level's atmosphere as its outdoor fog,
			atmosphere_fog - HALO_H3_FOG=0 none, as before -; a sky Halo 3
			draws as the haze alone, no model: atmosphere_sky_hidden) */
			{
				float fog[6];

				if (!tags.b.failed && atmosphere_fog(build, fog))
				{
					unsigned char *o = region_at(&tags, root);
					int c;

					/* (h3/light-1: the frame pass draws Halo 3's atmosphere a pixel,
					h3_frame.c - then no fog of this engine's on the level, and a
					sky with no model is the pass's inscatter) */
					if (!h3_frame_atmosphere_enabled())
					{
						for (c = 0; c < 3; c++)
							writef(o + 0x58 + 4 * c, fog[c]);
						writef(o + 0x6C, fog[3]);
						writef(o + 0x70, fog[4]);
						writef(o + 0x74, fog[5]);
					}
					if (fog[3] >= 0.99f && atmosphere_sky_hidden(build))
					{
						sky_model = FALSE;
						h3_frame_level()->sky_inscatter = TRUE;
						platform_log("h3 level: the sky drawn as its atmosphere's haze alone (its model's colours under it): no sky model");
					}
				}
			}
#endif
			region_reference(&tags, root, 'mode', sky_model ? tag_id(layout.model_first + build->sky_model_index) : 0xFFFFFFFFUL);
			region_reference(&tags, root + 0x10, 'antr', 0xFFFFFFFFUL);
			region_reference(&tags, root + 0x98, 'fog ', 0xFFFFFFFFUL);
			if (!tags.b.failed)
				region_reference(&tags, skies, 'sky ', tag_id(sky_index));
		}

		/* the grass's detail object collections (screen facing, a type each:
		its sprite, HALO_H3_GRASS_SIZE over the texture's width its size_min and
		size_max (3: about knee high as drawn; under 2 the sprites are not drawn here), fading out from 6 to 10 units)
		and the scenario's palette of
		them, a layer each */
		if (build->grass_object_count && !tags.b.failed)
		{
			unsigned long palette = region_block(&tags, scenario_root + 0x3C0, build->grass_set_count, 0x30);
			float size = env_float("HALO_H3_GRASS_SIZE", 3.0f);

			for (i = 0; i < build->grass_set_count && !tags.b.failed; i++)
			{
				unsigned long root = region_alloc(&tags, 0x80);
				unsigned long types;
				struct bitmap const *b = &build->bitmaps[build->grass_sets[i].bitmap];
				unsigned char *o;

				region_reference(&tags, palette + 0x30 * i, 'dobc', tag_id(dobc_first + i));
				write32(region_at(&tags, instances + XBOX_TAG_INSTANCE_SIZE * (dobc_first + i) + 20), tags.link + root);
				region_reference(&tags, root + 0x34, 'bitm', tag_id(bitmap_first + build->grass_sets[i].bitmap));
				/* (lifted HALO_H3_GRASS_LIFT - none by default - should a sprite be drawn about
				its point, a decorator's is at its foot) */
				writef(region_at(&tags, root + 0x04), env_float("HALO_H3_GRASS_LIFT", 0.0f));
				types = region_block(&tags, root + 0x44, 1, 0x60);
				if (tags.b.failed)
					break;
				o = region_at(&tags, types);
				snprintf((char *)o, 32, "%s", build->grass_sets[i].name);
				o[0x20] = 0;
				o[0x23] = 1;
				writef(o + 0x24, 1.0f);
				writef(o + 0x30, env_float("HALO_H3_GRASS_NEAR", 6.0f));
				writef(o + 0x34, env_float("HALO_H3_GRASS_FAR", 10.0f));
				writef(o + 0x38, size / (b->width ? b->width : 256));
				writef(o + 0x3C, size / (b->width ? b->width : 256));
			}
		}

		/* the gear: its machine (this engine's gear - machine type 2 - turns
		its device position on, a turn its animation's loop) and animation
		graph (one node, one overlay animation of 64 frames turning a whole
		turn about its axis: its device position animation) */
		if (layout.gear_object != NONE)
		{
			enum { GEAR_FRAMES = 64 };
			unsigned long root = region_alloc(&tags, 0x324);
			unsigned long graph = region_alloc(&tags, 0x80);
			unsigned long devices, nodes, animations, indices;
			unsigned char frames[8 * GEAR_FRAMES], defaults[16];
			float radius = build->models[build->gear.model_index].radius;
			static unsigned long const device_nulls[] = { 0x28, 0x38, 0x70, 0x80, 0x90, 0xA0, 0x1A0, 0x1B0, 0x1C0, 0x1D0,
				0x1E0, 0x1F0, 0x20C };
			static unsigned long const ones[] = { 0x180, 0x184, 0x188, 0x18C, 0x190, 0x194, 0x274, 0x278, 0x27C, 0x284 };
			float tick_velocity = build->gear.velocity / 30.0f;
			int k;

			write32(region_at(&tags, instances + XBOX_TAG_INSTANCE_SIZE * layout.gear_object + 20), tags.link + root);
			write32(region_at(&tags, instances + XBOX_TAG_INSTANCE_SIZE * layout.gear_graph + 20), tags.link + graph);
			for (k = 0; k < (int)(sizeof(device_nulls) / sizeof(device_nulls[0])); k++)
				region_reference(&tags, root + device_nulls[k], 0, 0xFFFFFFFFUL);
			region_reference(&tags, root + 0x28, 'mode', tag_id(layout.model_first + build->gear.model_index));
			region_reference(&tags, root + 0x38, 'antr', tag_id(layout.gear_graph));
			write16(region_at(&tags, root), 7);
			writef(region_at(&tags, root + 4), radius + 0.5f);
			writef(region_at(&tags, root + 0x20), 1.0f);
			writef(region_at(&tags, root + 0x104), radius + 0.5f);
			write16(region_at(&tags, root + 0x13C), -1);
			write16(region_at(&tags, root + 0x13E), -1);
			for (k = 0; k < (int)(sizeof(ones) / sizeof(ones[0])); k++)
				writef(region_at(&tags, root + ones[k]), 1.0f);
			write32(region_at(&tags, root + 0x17C), 1);   /* its position loops */
			writef(region_at(&tags, root + 0x280), tick_velocity);
			writef(region_at(&tags, root + 0x288), tick_velocity);
			write16(region_at(&tags, root + 0x290), 2);   /* gear */
			write16(region_at(&tags, root + 0x2EA), -1);

			/* the graph */
			devices = region_block(&tags, graph + 0x30, 1, 0x60);
			nodes = region_block(&tags, graph + 0x68, 1, 0x40);
			animations = region_block(&tags, graph + 0x74, 1, 0xB4);
			if (tags.b.failed)
			{
				build_fail(build, "out of memory");
				goto done;
			}
			indices = region_block(&tags, devices + 0x54, 2, 2);
			write16(region_at(&tags, indices), 0);
			write16(region_at(&tags, indices + 2), -1);
			memcpy(region_at(&tags, nodes), "frame", 6);
			write16(region_at(&tags, nodes + 0x20), -1);
			write16(region_at(&tags, nodes + 0x22), -1);
			write16(region_at(&tags, nodes + 0x24), -1);
			for (k = 0; k < GEAR_FRAMES; k++)
			{
				float half = 3.14159265f * (float)k / (float)GEAR_FRAMES;
				float q[4];
				int c;

				for (c = 0; c < 3; c++)
					q[c] = build->gear.axis[c] * sinf(half);
				q[3] = cosf(half);
				for (c = 0; c < 4; c++)
					write16(frames + 8 * k + 2 * c, (long)floorf(q[c] * 32767.0f + 0.5f));
			}
			memset(defaults, 0, sizeof(defaults));
			writef(defaults + 12, 1.0f);
			{
				unsigned char *o = region_at(&tags, animations);

				memcpy(o, "device position", 16);
				write16(o + 0x20, 1);              /* overlay */
				write16(o + 0x22, GEAR_FRAMES);
				write16(o + 0x24, 8);
				write16(o + 0x2C, 1);
				writef(o + 0x30, 1.0f);
				write16(o + 0x38, -1);
				write16(o + 0x3C, -1);
				o[0x40] = o[0x41] = 0xFF;
				writef(o + 0x44, 1.0f);
				write32(o + 0x6C, 1);              /* its node's rotation */
			}
			region_data(&tags, animations + 0x8C, defaults, sizeof(defaults));
			region_data(&tags, animations + 0xA0, frames, sizeof(frames));
		}

		/* the instanced geometry's objects */
		layout.kit_offset = kit_offset;
		layout.kit_size = kit_info.tags_size;
		objects_write(build, &tags, instances, header, scenario_root, &layout);
		if (result->why[0])
			goto done;
		/* the sandbox's tags: the Halo 3 weapons, grenades and vehicles on
		their kit stand-ins, their models, shaders and bitmaps (h3_sandbox.c) */
		if (sandbox && !h3_sandbox_write(sandbox, &tags, instances, tag_count, sandbox_first, scenario_root, build->vertex_table,
			build->index_table, build->buffer_next, sandbox_pixels_put, stream))
		{
			build_fail(build, "the Halo 3 weapons and vehicles could not be written");
			goto done;
		}
		/* the Spartan's Halo 3 graph: the kit's multiplayer cyborg plays it */
		if (spartan_graph)
		{
			if (!spartan_graph_write(&tags, instances, spartan_graph, spartan_graph_index, cyborg_mp))
			{
				build_fail(build, "the Spartan's Halo 3 animation graph could not be written");
				goto done;
			}
			platform_log("h3 level: the Spartan's Halo 3 graph written: tag %ld, the kit's %s plays it", spartan_graph_index,
				cyborg_mp != NONE ? "cyborg_mp" : "(no cyborg_mp biped: nothing)");
		}

		/* the models' shaders (shader_model: the environment shader's base and
		detail maps; alpha tested as it was) */
		for (i = 0; i < soso_count && !tags.b.failed; i++)
		{
			struct shader const *sh = &build->shaders[soso_shader[i]];
			unsigned long root = region_alloc(&tags, 0x1B8);
			unsigned char *o;

			write32(region_at(&tags, instances + XBOX_TAG_INSTANCE_SIZE * (layout.soso_first + i) + 20), tags.link + root);
			region_reference(&tags, root + 0xA4, 'bitm', sh->bitmap != NONE ? tag_id(bitmap_first + sh->bitmap) : 0xFFFFFFFFUL);
			region_reference(&tags, root + 0xBC, 'bitm', 0xFFFFFFFFUL);
			region_reference(&tags, root + 0xDC, 'bitm', sh->detail != NONE ? tag_id(bitmap_first + sh->detail) : 0xFFFFFFFFUL);
			region_reference(&tags, root + 0x164, 'bitm', 0xFFFFFFFFUL);
			o = region_at(&tags, root);
			write16(o + 0x22, 7);
			write16(o + 0x24, 4);
			write16(o + 0x28, sh->alpha_tested ? 0 : 1 << 2);
			writef(o + 0x74, 1.0f);
			writef(o + 0x9C, 1.0f);
			writef(o + 0xA0, 1.0f);
			write16(o + 0xD4, 0);
			writef(o + 0xD8, sh->detail != NONE && sh->detail_scale > 0.0f ? sh->detail_scale : 1.0f);
			writef(o + 0xEC, 1.0f);
		}

		/* shaders and bitmaps */
		for (i = 0; i < build->shader_count && !tags.b.failed; i++)
		{
			struct shader const *s = &build->shaders[i];
			unsigned long root = region_alloc(&tags, s->group == 'schi' ? 108 : s->group == 'swat' ? 0x140 : 836);
			unsigned long bitmap = s->bitmap != NONE && !getenv("HALO_H3_NO_BASE_MAPS") ? tag_id(bitmap_first + s->bitmap) : 0xFFFFFFFFUL;

			write32(region_at(&tags, instances + XBOX_TAG_INSTANCE_SIZE * (shader_first + i) + 20), tags.link + root);
			if (s->group == 'swat')
			{
				water_shader_write(&tags, root, tag_id(bitmap_first + s->ripple), tag_id(bitmap_first + s->reflection),
					s->h3_water && s->bitmap != NONE ? tag_id(bitmap_first + s->bitmap) : 0xFFFFFFFFUL);
				/* (Halo 3's water: its tags' places, the level's exposure and
				its maps' coordinates' fit - least squares of the water
				vertices' own, used when it holds to a hundredth of the map) */
				if (s->h3_water)
				{
					struct h3_level_water *w = &h3_waters[s->h3_water - 1];
					double const *f = h3_water_fit;
					double det = f[0] * (f[3] * f[5] - f[4] * f[4]) - f[1] * (f[1] * f[5] - f[4] * f[2]) + f[2] * (f[1] * f[4] - f[3] * f[2]);
					int k;

					w->shader_tag = shader_first + i;
					w->foam_tag = w->foam_tag != NONE ? (long)tag_id(bitmap_first + w->foam_tag) : NONE;
					w->shape_tag = w->shape_tag != NONE ? (long)tag_id(bitmap_first + w->shape_tag) : NONE;
					/* (this engine's water's flags: Halo 3's pass, bit 9; which water, bits 12 to 14) */
					write16(region_at(&tags, root + 0x28), (1 << 2) | (1 << 9) | ((s->h3_water - 1) << 12));
					w->exposure = build->exposure > 0.0f ? build->exposure : 1.0f;
					/* (the lightmap's light on the water: water_shading.fx reads
					the lightprobe's first coefficient at each pixel, saturated;
					here its mean over the water's vertices) */
					{
						double sum[3] = { 0.0, 0.0, 0.0 };
						long n, got = 0;

						for (n = 0; n < h3_water_light_count; n++)
						{
							float sh[4][3], intensity[3];

							if (lightprobe_sample(build, h3_water_light_at[n][0], h3_water_light_at[n][1], sh, intensity))
							{
								for (k = 0; k < 3; k++)
									sum[k] += sh[0][k] < 0.0f ? 0.0f : sh[0][k] > 1.0f ? 1.0f : sh[0][k];
								got++;
							}
						}
						for (k = 0; k < 3; k++)
							w->light[k] = got ? (float)(sum[k] / got) : 1.0f;
						platform_log("h3 level: water: the lightmap's light on it %.3f %.3f %.3f (%ld vertices); shadow mark %.2f", w->light[0],
							w->light[1], w->light[2], got, w->shadow_intensity_mark);
					}
					w->base_map_fitted = FALSE;
					if (f[5] >= 9.0 && fabs(det) > 1e-9)
					{
						double error = 0.0;

						for (k = 0; k < 2; k++)
						{
							double const *r = f + 6 + 3 * k;
							double a = (r[0] * (f[3] * f[5] - f[4] * f[4]) - f[1] * (r[1] * f[5] - f[4] * r[2]) + f[2] * (r[1] * f[4] - f[3] * r[2])) / det;
							double b = (f[0] * (r[1] * f[5] - r[2] * f[4]) - r[0] * (f[1] * f[5] - f[4] * f[2]) + f[2] * (f[1] * r[2] - r[1] * f[2])) / det;
							double c = (f[0] * (f[3] * r[2] - f[4] * r[1]) - f[1] * (f[1] * r[2] - f[2] * r[1]) + r[0] * (f[1] * f[4] - f[3] * f[2])) / det;

							w->base_map[3 * k] = (float)a;
							w->base_map[3 * k + 1] = (float)b;
							w->base_map[3 * k + 2] = (float)c;
							/* (the squared error summed: uu - a ux - b uy - c u) */
							error += f[12 + k] - a * r[0] - b * r[1] - c * r[2];
						}
						error = sqrt(fabs(error) / (2.0 * f[5]));
						w->base_map_fitted = error < 0.01;
						platform_log("h3 level: water: its maps' coordinates u = %.5f x + %.5f y + %.4f, v = %.5f x + %.5f y + %.4f (%.0f water corners, error %.4f: %s)",
							w->base_map[0], w->base_map[1], w->base_map[2], w->base_map[3], w->base_map[4], w->base_map[5], f[5], error,
							w->base_map_fitted ? "used" : "not used - the colour texture's and the shape map's middle read");
					}
					else
						platform_log("h3 level: water: no water corners to fit its maps' coordinates (%.0f)", f[5]);
				}
				continue;
			}
			if (s->group == 'schi')
			{
				transparent_shader_write(&tags, root, bitmap, s->blend, s->water, s->water ? s->base_scale[0] : 1.0f);
				continue;
			}
			shader_write(&tags, root, bitmap, s->detail != NONE ? tag_id(bitmap_first + s->detail) : 0xFFFFFFFFUL, s->detail_scale,
				s->detail2 != NONE ? tag_id(bitmap_first + s->detail2) : 0xFFFFFFFFUL, s->detail2_scale);
			/* (Halo 3's material on it: its bump map, and the Halo 2 extension -
			the lightmap's relief by Halo 3's dominant light, the detail's own
			v scale, its cook-torrance highlight: h2_materials.h) */
			if (!tags.b.failed && (s->bump != NONE || s->detail_aspect != 1.0f || s->specular_color) && !s->water && !s->dome &&
				!getenv("HALO_H3_NO_EXTENSION"))
			{
				struct h2_material material;

				memset(&material, 0, sizeof(material));
				material.known = TRUE;
				material.blended = s->blended;
				material.bump_scale[0] = s->bump_scale[0];
				material.bump_scale[1] = s->bump_scale[1];
				material.detail_scale[0] = material.secondary_scale[0] = 1.0f;
				material.detail_scale[1] = s->blended ? 1.0f : s->detail_aspect;
				material.secondary_scale[1] = s->blended ? s->detail_aspect : 1.0f;
				material.specular_color = s->specular_color;
				material.specular_glancing_color = s->specular_color;
				material.lightmap_type = s->specular_color ? s->lightmap_type : _h2_lightmap_type_none;
				material.shader_lightmap_type = -1;
				/* (1.2.0's own lightmap specular brightness: not read - Halo 3's
				highlight as before the merge) */
				material.lightmap_specular_brightness = -1.0f;
				material.shader_specular_type = -1;
				if (s->bump != NONE)
					region_reference(&tags, root + 0x128, 'bitm', tag_id(bitmap_first + s->bump));
				if (!tags.b.failed)
				{
					unsigned char *extension = region_at(&tags, root) + H2_MATERIAL_EXTENSION_OFFSET;

					h2_material_shader_write(region_at(&tags, root), &material, s->bump != NONE);
					/* (no bump map: no relief - the pass would read none) */
					if (s->bump == NONE)
						extension[6] &= (unsigned char)~_h2_material_relative_bump_bit;
					extension[7] |= (unsigned char)(_h2_material_relief_once_bit >> 8);
				}
			}
			if (s->scrolled)
			{
				write16(region_at(&tags, root + 0x150), 6);
				writef(region_at(&tags, root + 0x154), env_float("HALO_H3_WATER_U_PERIOD", 40.0f));
				write16(region_at(&tags, root + 0x15C), 6);
				writef(region_at(&tags, root + 0x160), env_float("HALO_H3_WATER_V_PERIOD", 55.0f));
			}
			if (s->alpha_tested)
				write16(region_at(&tags, root + 0x28), 1);
			if (s->blended)
				write16(region_at(&tags, root + 0x2A), 1);
		}
		for (i = 0; i < build->bitmap_count && !tags.b.failed; i++)
		{
			struct bitmap *b = &build->bitmaps[i];
			unsigned long root = region_alloc(&tags, 108);
			unsigned short flags = b->format >= 14 && b->format <= 16 ? 0x83 : b->format == 17 ? 0x8D : 0x89;

			write32(region_at(&tags, instances + XBOX_TAG_INSTANCE_SIZE * (bitmap_first + i) + 20), tags.link + root);
			/* (a bump map: a vector map, P8 bump palettized) */
			bitmap_write(&tags, root, tag_id(bitmap_first + i), b->width, b->height, b->format, flags,
				b->mipmaps, b->file_offset, b->file_size, b->format == 17 ? 5 : 0, b->type);
			/* (a detail object collection's: its sequence one sprite, all of
			it, its registration point its bottom's middle) */
			if (b->sprite && !tags.b.failed)
			{
				unsigned long sequence = read32(region_at(&tags, root + 0x54 + 4)) - tags.link;
				unsigned long sprites = region_block(&tags, sequence + 0x34, 1, 0x20);

				if (!tags.b.failed)
				{
					unsigned char *o = region_at(&tags, sprites);

					write16(o, 0);
					writef(o + 0x08, 0.0f);
					writef(o + 0x0C, 1.0f);
					writef(o + 0x10, 0.0f);
					writef(o + 0x14, 1.0f);
					writef(o + 0x18, 0.5f);
					writef(o + 0x1C, 1.0f);
				}
			}
		}
		{
			unsigned long root = region_alloc(&tags, 108);
			unsigned long sequences, data;
			short a;

			write32(region_at(&tags, instances + XBOX_TAG_INSTANCE_SIZE * lightmap_index + 20), tags.link + root);
			write16(region_at(&tags, root + 0x04), 4);
			sequences = region_block(&tags, root + 0x54, build->atlas_count, 64);
			data = region_block(&tags, root + 0x60, build->atlas_count, 48);
			for (a = 0; a < build->atlas_count && !tags.b.failed; a++)
			{
				unsigned char *o;

				write16(region_at(&tags, sequences + 64 * a + 0x20), a);
				write16(region_at(&tags, sequences + 64 * a + 0x22), 1);
				o = region_at(&tags, data + 48 * a);
				write32(o, 'bitm');
				write16(o + 4, LIGHTMAP_ATLAS);
				write16(o + 6, LIGHTMAP_ATLAS);
				write16(o + 8, 1);
				write16(o + 0xC, 6);
				write16(o + 0xE, 0x89);
				write16(o + 0x10, LIGHTMAP_ATLAS / 2);
				write16(o + 0x12, LIGHTMAP_ATLAS / 2);
				write32(o + 0x18, lightmap_file_offset + (unsigned long)a * LIGHTMAP_ATLAS * LIGHTMAP_ATLAS * 2);
				write32(o + 0x1C, LIGHTMAP_ATLAS * LIGHTMAP_ATLAS * 2);
				write32(o + 0x20, tag_id(lightmap_index));
				write32(o + 0x24, 0xFFFFFFFFUL);
			}
		}
		sbsp_instance = instances + XBOX_TAG_INSTANCE_SIZE * sbsp_index;
	}
	if (tags.b.failed || result->why[0])
	{
		build_fail(build, "out of memory");
		goto done;
	}

	if (getenv("HALO_H3_HEAP_LOG") || getenv("HALO_H3_DEBUG"))
		tags_census_log(&tags, instances_offset, tag_count);
	/* the structure BSP, linked after the tag data (where it loads) */
	{
		struct tag_ids ids;

		while (tags.b.size & 0xFFF)
			bytes_grow(&tags.b, 1), tags.b.data[tags.b.size - 1] = 0;
		bsp.link = tags.link + tags.b.size;
		ids.lightmap_bitmap = tag_id(lightmap_index);
		ids.first_shader = tag_id(shader_first);
		HEAP_SAMPLE();
		if (!bsp_write(build, &bsp, &ids))
			goto done;
		HEAP_SAMPLE();
		while (bsp.b.size & 0xFFF)
			bytes_grow(&bsp.b, 1), bsp.b.data[bsp.b.size - 1] = 0;
		result->bsp_bytes = (long)bsp.b.size;
		result->tag_bytes = (long)tags.b.size;
		if (tags.b.size + bsp.b.size > room)
		{
			char text[H3_LEVEL_WHY_SIZE];

			snprintf(text, sizeof(text), "the level is too big for the tag cache (%lu KB of tags and %lu KB of BSP, %lu KB room)",
				tags.b.size / 1024, bsp.b.size / 1024, room / 1024);
			build_fail(build, text);
			result->too_big = TRUE;
			goto done;
		}
		write32(region_at(&tags, sbsp_instance + 20), 0);
	}
	/* the BSP: (h3vita) its bytes copied to where the loader reads it to -
	the tag cache, right after the tags (bsp.link: the room was checked above)
	- and that range of the virtual file served from there: nothing of it
	kept in memory twice, nothing of it in the builder's heap past here */
	stream_pad(stream, 0x800);
	HEAP_SAMPLE();
	bsp_file_offset = stream_offset(stream);
	if (stream->failed)
	{
		build_fail(build, "out of memory for the level's BSP");
		goto done;
	}
	if (!getenv("HALO_H3_BSP_IN_PLACE") || atoi(getenv("HALO_H3_BSP_IN_PLACE")))
	{
		unsigned char *home = tags_base + (bsp.link - tags.link);

		memcpy(home, bsp.b.data, bsp.b.size);
		if (getenv("HALO_H3_HEAP_LOG") || getenv("HALO_H3_DEBUG"))
			platform_log("h3 level: the BSP in place at %p (%lu bytes, file offset %lu): first word %08lx", (void *)home, bsp.b.size,
				bsp_file_offset, read32(home));
		bytes_free(&bsp.b);
		if (!direct_range_add(bsp_file_offset, (unsigned long)result->bsp_bytes, _direct_range_in_place, home, 0))
		{
			build_fail(build, "out of memory for the level's BSP");
			goto done;
		}
	}
	else if (!direct_range_keep(bsp_file_offset, bsp.b.data, bsp.b.size))
	{
		/* (HALO_H3_BSP_IN_PLACE=0: kept in memory, read from there) */
		build_fail(build, "out of memory for the level's BSP");
		goto done;
	}
	stream->total += (unsigned long)result->bsp_bytes;
	memset(&bsp.b, 0, sizeof(bsp.b));
	{
		unsigned long bsps = read32(region_at(&tags, scenario_root + 0x5A4 + 4)) - tags.link;

		write32(region_at(&tags, bsps), bsp_file_offset);
		write32(region_at(&tags, bsps + 4), (unsigned long)result->bsp_bytes);
		write32(region_at(&tags, bsps + 8), bsp.link);
	}
	HEAP_SAMPLE();
	/* the header (no file: what the loader reads of one) */
	memset(header, 0, CACHE_HEADER_SIZE);
	write32(header, 'head');
	write32(header + 4, 5);
	write32(header + 8, stream_offset(stream));
	write32(header + 0x10, CACHE_HEADER_SIZE);
	write32(header + 0x14, tags.b.size);
	strncpy((char *)header + 0x20, name, 31);
	strncpy((char *)header + 0x40, "01.10.12.2276", 31);
	write16(header + 0x60, ng->multiplayer ? 1 : 0);
	write32(header + 0x64, 0xFFFFFFFFUL);
	write32(header + 0x700, 'h3lv');
	write32(header + 0x704, H3_LEVEL_MAKE_VERSION);
	write32(header + 0x7FC, 'foot');
	ok = TRUE;

done:
	result->microseconds = vita_host_time_us() - started;
	/* (the kit's pack kept open for its data's reads) */
	if (ok)
		direct.pack = kit_file;
	else if (kit_file)
		fclose(kit_file);
	free(kit);
	free(ng);
	h3_sandbox_free(sandbox);
	h2_graph_free(spartan_graph);
	free(stream);
	bytes_free(&tags.b);
	bytes_free(&bsp.b);
	geometry_close(&build->bsp_geometry);
	geometry_close(&build->sky_geometry);
	free(build->sky_dome_image);
	build->sky_dome_image = NULL;
	collision_free(&build->collision);
	for (i = 0; i < build->definition_count && build->definition_collision; i++)
		collision_free(&build->definition_collision[i]);
	free(build->definition_collision);
	free(build->definition_radius);
	free(build->definition_centre);
	for (i = 0; i < build->prop_count; i++)
	{
		if (build->props[i].geometry_open)
			geometry_close(&build->props[i].geometry);
		collision_free(&build->props[i].collision);
		free(build->props[i].placements);
	}
	free(build->props);
	for (i = 0; i < build->decorator_count; i++)
	{
		if (build->decorators[i].geometry_open)
			geometry_close(&build->decorators[i].geometry);
	}
	free(build->decorators);
	if (build->decorator_buffers_open)
		geometry_close(&build->decorator_buffers);
	free(build->decorator_wanted_buffers);
	free(build->grass_cells);
	free(build->grass_objects);
	free(build->grass_counts);
	h3_resource_free(&build->collision_resource);
	if (build->cache)
		h3_cache_close(build->cache);
	for (i = 0; i < build->ce_material_count; i++)
	{
		bytes_free(&build->materials[i].vertices);
		bytes_free(&build->materials[i].lightmap_vertices);
		bytes_free(&build->materials[i].triangles);
		bytes_free(&build->materials[i].clusters);
		bytes_free(&build->materials[i].bounds);
	}
	free(build->materials);
	for (i = 0; i < build->atlas_count; i++)
	{
		free(build->atlases[i].rgb);
		free(build->atlases[i].pending);
	}
	free(build->lightprobe_slices);
	free(build->intensity_slices);
	free(build->instance_probes);
	free(build->lightprobe_normals);
	for (i = 0; i < build->bitmap_count; i++)
	{
		free(build->bitmaps[i].kept);
		free(build->bitmaps[i].held_rgba);
	}
	free(build->bitmaps);
	free(build->material_shader);
	free(build->leaf_cluster);
	free(build->instances);
	for (i = 0; i < build->model_count; i++)
		model_free(&build->models[i]);
	free(build->models);
	model_free(&build->sky_model);
	model_free(&build->gear.model);
	geometry_close(&build->gear.geometry);
	free(build->gear.animation);
	free(build->keys);
	free(layout.shader_soso);
	free(layout.slot_collision);
	free(soso_shader);
	free(build);
	result->heap_peak = heap_peak - heap_base;
	platform_log("h3 level: memory: the build's heap peak about %lu KB (the samples' most less the %lu KB before)",
		(heap_peak - heap_base) / 1024, heap_base / 1024);
	platform_log("h3 level: %s %s in %.1f s: %ld tags (%ld the globals' pack's), BSP %ld KB, tags %ld KB, "
		"bitmaps %ld KB, lightmaps %ld KB, %ld surfaces, %ld vertices, %ld materials, %ld clusters, %ld instances "
		"(%ld left out), %ld placed as objects of %ld models (%ld vertices), %ld object definitions%s%s",
		name, ok ? "made" : "not made", result->microseconds / 1e6, result->tag_count, result->kit_tag_count,
		result->bsp_bytes / 1024, result->tag_bytes / 1024, result->bitmap_bytes / 1024, result->lightmap_bytes / 1024,
		result->surfaces, result->vertices, result->materials, result->clusters, result->instances,
		result->instances_left_out, result->object_instances, result->models, result->model_vertices, result->sceneries,
		ok ? "" : ": ", ok ? "" : result->why);

	return ok;
}

/* ---------- public code: a level with no file (h2_level.c's direct level) */

boolean h3_level_direct_enabled(void)
{
	char const *setting = getenv("HALO_H3_LEVELS");

	return setting && setting[0] && strcmp(setting, "0");
}

enum { H3_DIRECT_ROOMS = 32 };
static struct { char name[32]; unsigned long room; } h3_direct_rooms[H3_DIRECT_ROOMS];
static int h3_direct_room_next;

static unsigned long h3_direct_room_hint(char const *name)
{
	int k;

	for (k = 0; k < H3_DIRECT_ROOMS; k++)
		if (h3_direct_rooms[k].name[0] && !strncasecmp(h3_direct_rooms[k].name, name, sizeof(h3_direct_rooms[k].name) - 1))
			return h3_direct_rooms[k].room;
	return 0;
}

void h3_level_direct_room_remember(char const *name, unsigned long room)
{
	int k;

	if (!name || !name[0] || !room)
		return;
	for (k = 0; k < H3_DIRECT_ROOMS; k++)
	{
		if (h3_direct_rooms[k].name[0] && !strncasecmp(h3_direct_rooms[k].name, name, sizeof(h3_direct_rooms[k].name) - 1))
		{
			h3_direct_rooms[k].room = room;
			return;
		}
	}
	k = h3_direct_room_next;
	h3_direct_room_next = (h3_direct_room_next + 1) % H3_DIRECT_ROOMS;
	snprintf(h3_direct_rooms[k].name, sizeof(h3_direct_rooms[k].name), "%s", name);
	h3_direct_rooms[k].room = room;
}

char const *h3_maps_directory_of(char const *name)
{
	char const *cache_files_map_directory(void);
	char path[512];
	FILE *file;

	if (name && name[0] && snprintf(path, sizeof(path), "%s%s.map", H3_MAPS_FOLDER, name) < (int)sizeof(path) &&
		(file = fopen(path, "rb")) != NULL)
	{
		fclose(file);
		return H3_MAPS_FOLDER;
	}
	return cache_files_map_directory();
}

H3_ALIGN static boolean direct_file_exists(char const *path)
{
	FILE *file = fopen(path, "rb");

	if (file)
		fclose(file);
	return file != NULL;
}

H3_ALIGN boolean h3_level_direct_prepare(char const *maps_directory, char const *name, char *path, long path_size, char *why,
	long why_size)
{
	char source[512], shared[512];
	unsigned char *h = direct.header;

	if (!h2_level_name_plain(name) || strlen(name) >= sizeof(direct.name))
	{
		snprintf(why, (size_t)why_size, "its name is not a plain file name");
		return FALSE;
	}
	snprintf(source, sizeof(source), "%s%s.map", maps_directory, name);
	snprintf(shared, sizeof(shared), "%sshared.map", maps_directory);
	if (!direct_file_exists(source))
	{
		snprintf(why, (size_t)why_size, "there is no %s", source);
		return FALSE;
	}
	if (!direct_file_exists(shared))
	{
		snprintf(why, (size_t)why_size, "Halo 3's shared.map is not beside %s.map", name);
		return FALSE;
	}
	pthread_mutex_lock(&direct.lock);
	direct_ranges_free();
	direct.made = FALSE;
	memset(h, 0, CACHE_HEADER_SIZE);
	write32(h, 'head');
	write32(h + 4, 5);
	write32(h + 8, CACHE_HEADER_SIZE + XBOX_TAG_HEADER_SIZE);
	write32(h + 0x10, CACHE_HEADER_SIZE);
	write32(h + 0x14, XBOX_TAG_HEADER_SIZE);
	strncpy((char *)h + 0x20, name, 31);
	strncpy((char *)h + 0x40, "01.10.12.2276", 31);
	write16(h + 0x60, 0);
	write32(h + 0x64, 0xFFFFFFFFUL);
	write32(h + 0x700, 'h3lv');
	write32(h + 0x704, H3_LEVEL_MAKE_VERSION);
	/* (the room this level's last make came to, if made before in this run:
	cache_files.c makes it in that tag cache at once - h3_level_direct_room_remember;
	else the large one first, as Halo 2's missions: a Halo 3 level's made tags,
	the kit's 6.3 MB with them, came to 22.3-27.6 MB on Valhalla, Guardian and
	Last Resort, over the fixed 22 MB - made in the fixed one first, each was
	made twice, the first failing late after a 168 MB build peak) */
	{
		unsigned long hint = h3_direct_room_hint(name);

		write32(h + 0x730, hint ? hint : (unsigned long)TAG_CACHE_LARGE_MAXIMUM_SIZE);
	}
	write32(h + 0x7FC, 'foot');
	snprintf(direct.name, sizeof(direct.name), "%s", name);
	snprintf(direct.maps_directory, sizeof(direct.maps_directory), "%s", maps_directory);
	direct.prepared = TRUE;
	pthread_mutex_unlock(&direct.lock);
	snprintf(path, (size_t)path_size, "%s", source);
	platform_log("h3 level: %s: a direct level - no level file: made as it loads from %s", name, source);
	return TRUE;
}

H3_ALIGN boolean h3_level_direct_header(char const *name, void *header)
{
	boolean got = FALSE;

	pthread_mutex_lock(&direct.lock);
	if (direct.prepared && name && !strcasecmp(direct.name, name))
	{
		memcpy(header, direct.header, CACHE_HEADER_SIZE);
		got = TRUE;
	}
	pthread_mutex_unlock(&direct.lock);
	return got;
}

H3_ALIGN static int direct_range_compare(void const *a, void const *b)
{
	struct direct_range const *x = a, *y = b;

	return x->offset < y->offset ? -1 : x->offset > y->offset ? 1 : 0;
}

/* the make on a thread of its own while the caller - the game thread -
keeps the loading screen drawn (h2_level.c's direct_make_run: made on the
game thread the screen stood still for the whole make, 3.6-6.5 s on PC);
HALO_H3_DIRECT_THREAD=0, or no thread to be had: on the caller */
enum
{
	DIRECT_MAKE_STACK_BYTES = 8 * 1024 * 1024
};

struct direct_make_job
{
	char const *maps_directory, *name;
	unsigned char *tags;
	unsigned long room;
	unsigned char *header;
	struct h3_level_build_result *result;
	boolean ok;
	int done;
};

void game_loading_screen_frame(float progress);
void Sleep(unsigned long milliseconds);

/* a level made, and made again with its BSP's instances cut to fit when
the tags and BSP are more than the tag cache holds (Edge: 32 MB of tags and
39 MB of BSP with all of its instanced geometry, 64 MB room): its surface
and vertex budgets scaled down by the room over the BSP's bytes, at most
three times. The instances left out are logged ("left out") */
H3_ALIGN static boolean level_make_fitted(char const *maps_directory, char const *name, unsigned char *tags, unsigned long room,
	unsigned char *header, struct h3_level_build_result *result)
{
	boolean ok;
	int attempt;

	fit_vertex_budget = fit_surface_budget = 0;
	fit_shared_vertex_light = FALSE;
	ok = level_make(maps_directory, name, tags, room, header, result);
	if (!ok && result->too_big && !getenv("HALO_H3_FIT_SCALE_FIRST"))
	{
		platform_log("h3 level: %s: %ld KB of tags and %ld KB of BSP over %lu KB room: made again, its instances' per-vertex light averaged (STAND-IN)",
			name, result->tag_bytes / 1024, result->bsp_bytes / 1024, room / 1024);
		fit_shared_vertex_light = TRUE;
		pthread_mutex_lock(&direct.lock);
		direct_ranges_free();
		pthread_mutex_unlock(&direct.lock);
		ok = level_make(maps_directory, name, tags, room, header, result);
	}
	for (attempt = 0; !ok && result->too_big && attempt < 3; attempt++)
	{
		long margin = 256 * 1024;
		double scale = (double)((long)room - result->tag_bytes - margin) / (double)(result->bsp_bytes > 0 ? result->bsp_bytes : 1);

		if (scale <= 0.05 || result->surfaces <= 0 || result->vertices <= 0)
			break;
		scale *= 0.97;
		fit_vertex_budget = (long)(result->vertices * scale);
		fit_surface_budget = (long)(result->surfaces * scale);
		platform_log("h3 level: %s: %ld KB of tags and %ld KB of BSP over %lu KB room: made again, the BSP's budgets %ld surfaces, %ld vertices",
			name, result->tag_bytes / 1024, result->bsp_bytes / 1024, room / 1024, fit_surface_budget, fit_vertex_budget);
		/* (the failed make's ranges of the virtual file let go) */
		pthread_mutex_lock(&direct.lock);
		direct_ranges_free();
		pthread_mutex_unlock(&direct.lock);
		ok = level_make(maps_directory, name, tags, room, header, result);
	}
	fit_vertex_budget = fit_surface_budget = 0;
	fit_shared_vertex_light = FALSE;
	return ok;
}

H3_ALIGN static void *direct_make_thread(void *argument)
{
	struct direct_make_job *job = argument;

	job->ok = level_make_fitted(job->maps_directory, job->name, job->tags, job->room, job->header, job->result);
	__atomic_store_n(&job->done, 1, __ATOMIC_RELEASE);
	return NULL;
}

H3_ALIGN static boolean direct_make_run(char const *maps_directory, char const *name, unsigned char *tags, unsigned long room,
	unsigned char *header, struct h3_level_build_result *result)
{
	struct direct_make_job job;
	pthread_attr_t attributes;
	pthread_t thread;
	unsigned long long started = vita_host_time_us(), last, longest = 0;
	char const *setting = getenv("HALO_H3_DIRECT_THREAD");
	int made;

	if (setting && !strcmp(setting, "0"))
		return level_make_fitted(maps_directory, name, tags, room, header, result);
	memset(&job, 0, sizeof(job));
	job.maps_directory = maps_directory;
	job.name = name;
	job.tags = tags;
	job.room = room;
	job.header = header;
	job.result = result;
	pthread_attr_init(&attributes);
	pthread_attr_setstacksize(&attributes, DIRECT_MAKE_STACK_BYTES);
	made = pthread_create(&thread, &attributes, direct_make_thread, &job);
	pthread_attr_destroy(&attributes);
	if (made != 0)
	{
		platform_log("h3 level: %s: no thread for the make: made on the game thread", name);
		return level_make_fitted(maps_directory, name, tags, room, header, result);
	}
	last = started;
	while (!__atomic_load_n(&job.done, __ATOMIC_ACQUIRE))
	{
		unsigned long long now;
		/* (the progress a curve toward 90% - the make has no count to go by) */
		float progress = 0.9f - 0.8f * expf(-(float)((vita_host_time_us() - started) / 1000) / 4000.0f);

		game_loading_screen_frame(progress);
		now = vita_host_time_us();
		if (now - last > longest)
			longest = now - last;
		last = now;
		Sleep(2);
	}
	pthread_join(thread, NULL);
	platform_log("h3 level: %s: made on its own thread in %.0f ms, the game thread's longest wait %.0f ms", name,
		(vita_host_time_us() - started) / 1000.0, longest / 1000.0);
	return job.ok;
}

H3_ALIGN boolean h3_level_direct_make(char const *name, unsigned char *tags, unsigned long room, void *header, char *why,
	long why_size)
{
	struct h3_level_build_result result;
	unsigned char made_header[CACHE_HEADER_SIZE];
	boolean ok;

	if (!direct.prepared || !name || strcasecmp(direct.name, name))
	{
		snprintf(why, (size_t)why_size, "no direct Halo 3 level was prepared for %s", name ? name : "?");
		return FALSE;
	}
	pthread_mutex_lock(&direct.lock);
	direct_ranges_free();
	direct.made = FALSE;
	pthread_mutex_unlock(&direct.lock);
	memset(&result, 0, sizeof(result));
	/* (h3vita: the make may take video memory once user RAM is at its floor
	- its peak is past the Vita's user RAM beside the game -; the Halo 2
	makes as before) */
	h2_build_heap_video_memory(1);
	h2_build_heap_begin();
	h2_build_heap_unit("the Halo 3 level");
	ok = direct_make_run(direct.maps_directory, name, tags, room, made_header, &result);
	h2_build_heap_end();
	h2_build_heap_video_memory(0);
	pthread_mutex_lock(&direct.lock);
	if (!ok)
	{
		direct_ranges_free();
		pthread_mutex_unlock(&direct.lock);
		snprintf(why, (size_t)why_size, "%s", result.why[0] ? result.why : "the level could not be made");
		return FALSE;
	}
	qsort(direct.ranges, (size_t)direct.range_count, sizeof(*direct.ranges), direct_range_compare);
	memcpy(direct.header, made_header, CACHE_HEADER_SIZE);
	memcpy(header, made_header, CACHE_HEADER_SIZE);
	direct.made = TRUE;
	pthread_mutex_unlock(&direct.lock);
	platform_log("h3 level: %s: made directly in %.1f s: %ld tags (%ld the kit's), tags %ld KB, BSP %ld KB, bitmaps %ld KB, "
		"lightmaps %ld KB; the virtual file %lu KB in %ld ranges, %lu KB of it kept in memory (%lu KB in %ld blocks of its own)", name,
		result.microseconds / 1e6, result.tag_count, result.kit_tag_count, result.tag_bytes / 1024, result.bsp_bytes / 1024,
		result.bitmap_bytes / 1024, result.lightmap_bytes / 1024, direct.end / 1024, direct.range_count, direct.kept_bytes / 1024,
		kept.bytes / 1024, kept.count);
	return TRUE;
}

H3_ALIGN boolean h3_level_direct_read(unsigned long offset, unsigned long size, void *buffer)
{
	unsigned char *out = buffer;
	long lo, hi, k;
	boolean ok = TRUE;
	unsigned long zeroed = offset;      /* (the gaps between ranges are zeros: zeroed up to here) */

	pthread_mutex_lock(&direct.lock);
	if (!direct.made)
	{
		memset(out, 0, size);
		pthread_mutex_unlock(&direct.lock);
		return FALSE;
	}
	/* (the first range that ends past offset: sorted, none overlap) */
	lo = 0;
	hi = direct.range_count;
	while (lo < hi)
	{
		long mid = (lo + hi) / 2;

		if (direct.ranges[mid].offset + direct.ranges[mid].size <= offset)
			lo = mid + 1;
		else
			hi = mid;
	}
	for (k = lo; k < direct.range_count && direct.ranges[k].offset < offset + size && ok; k++)
	{
		struct direct_range const *r = &direct.ranges[k];
		unsigned long start = r->offset > offset ? r->offset : offset;
		unsigned long end = r->offset + r->size < offset + size ? r->offset + r->size : offset + size;

		if (end <= start)
			continue;
		if (start > zeroed)
			memset(out + (zeroed - offset), 0, start - zeroed);
		zeroed = end;
		if (r->kind == _direct_range_in_place && r->memory + (start - r->offset) == out + (start - offset))
		{
			/* (h3vita: the BSP made where it loads - the bytes are there) */
			if (getenv("HALO_H3_HEAP_LOG") || getenv("HALO_H3_DEBUG"))
				platform_log("h3 level: in-place read of %lu at %lu (range %lu+%lu at %p): first word %08lx", end - start, start, r->offset,
					r->size, (void *)r->memory, read32(r->memory));
		}
		else if (r->kind == _direct_range_in_place && (getenv("HALO_H3_HEAP_LOG") || getenv("HALO_H3_DEBUG")))
		{
			platform_log("h3 level: in-place range read ELSEWHERE: %lu at %lu to %p (the range at %p): first word %08lx", end - start, start,
				(void *)(out + (start - offset)), (void *)r->memory, read32(r->memory));
			memcpy(out + (start - offset), r->memory + (start - r->offset), end - start);
		}
		else if (r->kind == _direct_range_memory || r->kind == _direct_range_kept || r->kind == _direct_range_in_place)
			memcpy(out + (start - offset), r->memory + (start - r->offset), end - start);
		else
		{
#if !defined(HALO_VITA) && !defined(HALO_GXM_NULL)
			/* (the desktop: the bytes read through a buffer of this function's
			own and copied. The destination may be pages the memory watch has
			write-protected - a texture cache block: a write there faults and is
			let through, but read() into them fails with EFAULT, the file's
			stream then in error; the kit's pixels - the stand-in weapons' -
			were left as they lay, noise. HALO_H3_KIT_READ_DIRECT=1: as before) */
			if (!getenv("HALO_H3_KIT_READ_DIRECT"))
			{
				static unsigned char bounce[0x10000];
				unsigned long done = 0;

				ok = direct.pack && fseek(direct.pack, (long)(r->pack_offset + (start - r->offset)), SEEK_SET) == 0;
				while (ok && done < end - start)
				{
					unsigned long n = end - start - done < sizeof(bounce) ? end - start - done : sizeof(bounce);

					ok = fread(bounce, 1, n, direct.pack) == n;
					if (ok)
						memcpy(out + (start - offset) + done, bounce, n);
					done += n;
				}
			}
			else
#endif
			{
				ok = direct.pack && fseek(direct.pack, (long)(r->pack_offset + (start - r->offset)), SEEK_SET) == 0 &&
					fread(out + (start - offset), 1, end - start, direct.pack) == end - start;
			}
			if (!ok)
			{
				if (direct.pack)
					clearerr(direct.pack);
				platform_log("h3 level: the direct level's read of %lu bytes at %lu from the kit pack failed", end - start, start);
			}
		}
	}
	if (offset + size > zeroed)
		memset(out + (zeroed - offset), 0, offset + size - zeroed);
	pthread_mutex_unlock(&direct.lock);
	return ok;
}

H3_ALIGN void h3_level_direct_unload_named(char const *name)
{
	pthread_mutex_lock(&direct.lock);
	if ((direct.made || direct.prepared) && (!name || !name[0] || !strcasecmp(direct.name, name)))
	{
		platform_log("h3 level: %s: the direct level let go (%lu KB of memory)", direct.name, direct.kept_bytes / 1024);
		direct_ranges_free();
		direct.made = FALSE;
		direct.prepared = FALSE;
	}
	pthread_mutex_unlock(&direct.lock);
}

boolean h3_level_direct_loaded(void)
{
	return direct.made;
}

/* whether the direct level loaded is a Halo 3: ODST map's (its view is
wider than Halo 3's: field_of_view.c) */
boolean h3_level_direct_odst(void)
{
	return h3_odst && direct.made;
}

/* whether the map being made (or made last) is a Halo 3: ODST one: the
sandbox's readers (h3_spartan.c) read its render methods by ODST's layout */
boolean h3_level_odst(void)
{
	return h3_odst;
}

boolean h3_level_direct_in_place_range(unsigned char **first, unsigned char **end)
{
	long i;
	boolean found = FALSE;

	*first = *end = NULL;
	pthread_mutex_lock(&direct.lock);
	for (i = 0; direct.made && i < direct.range_count && !found; i++)
	{
		if (direct.ranges[i].kind == _direct_range_in_place)
		{
			*first = direct.ranges[i].memory;
			*end = direct.ranges[i].memory + direct.ranges[i].size;
			found = TRUE;
		}
	}
	pthread_mutex_unlock(&direct.lock);
	return found;
}

boolean h3_level_image_encode(unsigned char *rgba, unsigned long w, unsigned long h, unsigned char **out,
	unsigned long *out_size, unsigned short *format, unsigned short *mipmaps)
{
	return image_encode(rgba, w, h, out, out_size, format, mipmaps);
}

unsigned char *h3_level_bump_p8_make(unsigned char const *rgba, unsigned long w, unsigned long h, unsigned long *size,
	unsigned short *mipmaps)
{
	return bump_p8_make(rgba, w, h, size, mipmaps);
}


