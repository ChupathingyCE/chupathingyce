/*
H2_BIPED.C

Halo 2's multiplayer player models made this engine's bipeds (h2_biped.h
says what is made and how h2_level.c calls this).

What a Halo 2 render model holds and how it is read (MCC's, checked against
the owner's shared.map; Assembly's Halo 2 plugins as a guide):
- its tag: regions at 0x1C (0x10 each: name, permutations at 8 - 0x10
  each: name, the section of each level of detail L1..L6 at 4..0xE),
  sections at 0x24 (0x5C each), compression bounds at 0x14 (0x38 each:
  position x y z ranges, texture coordinate u v ranges), nodes at 0x48
  (0x60 each: name, parent, first child, next sibling, default translation
  0x0C, rotation 0x18, inverse scale 0x28, inverse forward, left, up and
  position 0x2C..0x5C, distance from parent 0x5C), marker groups at 0x58
  (0x0C each: name, markers at 4 - 0x24 each: region, permutation, node,
  translation 4, rotation 0x10, scale 0x20), materials at 0x60 (0x20
  each: the shader at 8);
- a section: its vertex and part counts at 4, compression flags at 0x1A
  (bit 0 positions, bit 1 texture coordinates within the bounds), its rigid
  node at 0x2C (rigid sections: every vertex that node's), its own
  compression bounds at 0x1C, its resource at 0x38 ('blkh', the section,
  then the resources the block at 0x48 lists: parts (type 0, 0x00), strip
  indices (0x20), vertex buffers' descriptors (0x38), the node map (0x68:
  a byte a section node, the model's node), the buffers (type 2, by
  index));
- vertex buffers: positions 0x01 (3 floats), 0x02 (3 floats and a node
  byte: rigid boned), 0x04 and 0x06 (3 floats, 4 node bytes, 4 weight bytes:
  skinned), texture coordinates 0x17 (2 floats), normal, binormal and
  tangent 0x19 (9 floats) - positions in the model's space (its bind pose),
  nodes the section's node map's.
- quaternions are Halo 2's (a standard rotation), this engine's
  conjugated: checked by composing the nodes' defaults with this engine's
  matrix4x3_from_orientation and getting Halo 2's inverse matrices back.
*/

/* ---------- headers */

#include "cseries.h"
#include "h2_biped.h"
#include "h2_animation.h"
#include "h2_cache.h"
#include "h3_animation.h"
#include "h2_damage.h"
#include "h2_emblem.h"
#include "h2_materials.h"
#include "h2_plasma_shield.h"
#include "tag_relocate.h"

#include "game/game_globals.h"
#include "h2_player_physics.h"
#include "objects/object_definitions.h"
#include "objects/damage_effect_definitions.h"
#include "units/unit_definitions.h"
#include "units/biped_definitions.h"
#include "physics/collision_model_definitions.h"
#include "scenario/scenario_definitions.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef malloc
#undef free
#undef realloc

/* (the Vita tier: what a level's build allocates is the builder's own
heap's, not the C heap's - h2_build_heap.h) */
#define H2_BUILD_HEAP_WRAP
#include "h2_build_heap.h"
#ifdef HALO_LINUX
#include "render_epoch.h" /* HALO_ENV_ONCE */
#endif

#define H2_ALIGN __attribute__((force_align_arg_pointer))

void platform_log(char const *format, ...);

/* ---------- constants */

enum
{
	H2_MODE_REGIONS = 0x1C,
	H2_MODE_SECTIONS = 0x24,
	H2_MODE_SECTION_SIZE = 0x5C,
	H2_MODE_COMPRESSION = 0x14,
	H2_MODE_NODES = 0x48,
	H2_MODE_NODE_SIZE = 0x60,
	H2_MODE_MARKER_GROUPS = 0x58,
	H2_MODE_MATERIALS = 0x60,
	H2_PART_SIZE = 0x48,
	H2_SECTION_MAXIMUM_BYTES = 8 * 1024 * 1024,

	MAXIMUM_NODES = 96,
	/* (Halo 2's Marine: 75 - Johnson's head the 75th) */
	MAXIMUM_SECTIONS = 128,
	MAXIMUM_MARKERS = 64,
	MAXIMUM_MATERIALS = 32,
	MAXIMUM_PARTS = 32,
	/* this engine's renderer skins this many nodes at once (rasterizer.h's
	RASTERIZER_MAXIMUM_NODES_PER_MODEL less one) */
	RENDERER_SKINNED_NODES = 43,
	/* (a model of more: its parts drawn their own nodes at a time - the
	part palettes, h2_bipeds_part_palettes; this engine's models' limit) */
	MODEL_NODES = 64,

	CE_MODEL_SIZE = 0xE8,
	CE_MODEL_NODE_SIZE = 0x9C,
	CE_MODEL_MARKER_SIZE = 0x40,
	CE_MODEL_MARKER_INSTANCE_SIZE = 0x20,
	CE_MODEL_REGION_SIZE = 0x4C,
	CE_MODEL_PERMUTATION_SIZE = 0x58,
	CE_MODEL_GEOMETRY_SIZE = 0x30,
	CE_MODEL_PART_SIZE = 0x68,
	CE_MODEL_SHADER_SIZE = 0x20,
	CE_SHADER_MODEL_SIZE = 0x1B8,
	CE_SHADER_CHICAGO_SIZE = 108,
	CE_SHADER_PLASMA_SIZE = 0x14C,
	CE_COLLISION_SIZE = 0x298,
	CE_COLLISION_NODE_SIZE = 0x40,
	CE_DAMAGE_REGION_SIZE = 0x54,
	CE_DAMAGE_REGIONS = OBJECT_REGIONS_WITH_STATE, /* (the regions an object keeps state of: the Enforcer's 10) */
	CE_COLLISION_BSP_SIZE = 0x60,
	CE_PATHFINDING_SPHERE_SIZE = 0x20,
	VERTEX_SIZE = 32,

	/* multipurpose maps made: this side, A4R4G4B4 */
	MULTIPURPOSE_SIZE = 128,
};

/* the most nodes a made model keeps: this engine's whole model
(MODEL_NODES), its parts skinned their own nodes at a time (the part
palettes, which the Vita's renderer draws too - rasterizer_xbox.c's
rasterizer_model_part_skinning is both's: the Vita kept 43, and the
Prophets lost their faces, wattles and jaws' leaves - their cutscene faces
still); HALO_H2_BIPED_COLLAPSE=1 what the renderer skins at once (the
tests) */
#define RENDERER_NODES ((short)(getenv("HALO_H2_BIPED_COLLAPSE") ? RENDERER_SKINNED_NODES : MODEL_NODES))

/* ---------- structures */

struct made_part
{
	short shader;           /* the model's shader block index */
	long vertex_count;
	unsigned char *vertices;    /* VERTEX_SIZE each, this engine's compressed */
	long strip_count;
	unsigned short *strip;
	float centroid[3];
	short centroid_node;
	/* (the host's spill file: vertices_spill) its vertices there, its
	strip right after them */
	boolean spilled;
	unsigned long spill;
};

struct made_geometry
{
	short section;          /* Halo 2's */
	long part_count;
	struct made_part parts[MAXIMUM_PARTS];
	long triangles;
	long vertices;
};

struct made_shader
{
	long h2_shader;
	long base_bitmap;           /* the level's bitmap, NONE */
	long multipurpose_bitmap;   /* made, NONE */
	boolean change_colour;
	/* (its self-illumination - the Elite armour's lights: the glow map in
	the multipurpose map's green, its colour's two ends, its period (0: the
	upper end, steady)) */
	boolean illum;
	float illum_lower[3], illum_upper[3], illum_period;
	boolean illum_shield;
	boolean illum_full;         /* (an illum template - the inset lights: the whole of its base map glowing) */       /* (its glow by the shields: Halo 2's shield_vitality input - colour A, darkened by them) */
	boolean alpha_tested;       /* (lit alpha blended hair - Tartarus' mane: cut out by its base map's alpha) */
	boolean skipped;            /* (not drawn) */
	boolean emblem;             /* (an emblem's overlay: its map clamped, the pass's border address) */
	/* (Halo 2's transparent and hologram templates - a Jackal's and a
	Sentinel's shield, Cortana, the holograms, eyes' glow -: an added
	transparent chicago shader of its first map, two-sided) */
	boolean transparent;
	long h2_diffuse;            /* (its diffuse map, Halo 2's tag: NONE) */
	boolean alpha_blended;      /* (Halo 2's alpha ones - eyes, lashes, wings: their map alpha blended as it is) */
	/* (Halo 2's plasma_shield - a Sentinel's shield -: a copy of the
	kit's plasma, the cyborg's shield hit, its colour Halo 2's tint) */
	boolean plasma;
	float plasma_colour[3];
	/* (Halo 2's plasma_shield pass itself, h2_plasma_shield.h: written in
	the plasma shader's padding for the desktop's program) */
	boolean shield_pass_read;
	struct h2_plasma_shield_pass shield_pass;
	short level_shader;         /* (plasma_mask_offset - a Jackal's shield -: the level's own shader of it, h2_level.c draws the pass; NONE) */
	/* (Halo 2's overshield - the model's damage info's overshield shader:
	a plasma over the whole biped while its shields are overcharged, its
	colour by the overshield's amount, drawn as the object's modifier shader
	(render_objects.c, h2_bipeds_overshield_*); no part draws it) */
	boolean overshield;
	char name[64];
	/* (Halo 2's material, h2_materials.c: its detail map and environment
	cube, the level's bitmaps, NONE) */
	struct h2_material material;
	long detail_bitmap, cube_bitmap, bump_bitmap;
};

struct made_marker
{
	char name[32];
	long instance_count;
	struct { unsigned char region, permutation, node; float translation[3], rotation[4]; } instances[8];
};

struct made_region
{
	char name[32];
	/* (its destroyed state's permutation - a variant's state 4 -: written
	"~damaged", the one this engine shows a region its damage destroyed;
	none: no destroyed state) */
	boolean destroyable;
	char destroyed[32];
	long permutation_count;
	struct { char name[32]; short geometry[H2_BIPED_DETAIL_LEVELS]; } permutations[H2_BIPED_MAXIMUM_PERMUTATIONS];
};

struct made_biped
{
	boolean made;
	/* (its unit camera's track - Halo 2's biped's, at 0xE8: the Spartan's and
	Elite's camera\\biped_follow_camera, the follow and death camera -: a
	'trak' tag of its own, the last of its tags (camera_local, NONE none:
	the stand-in's, camera\\biped_small_tight), its control points) */
	long camera_local;
	long camera_point_count;
	float camera_points[16][7];
	/* (h2_bipeds_write: its own collision model made, not the stock one's) */
	boolean collision_made;
	/* (a biped of another's model: that one - its model, graph and
	collision model these - else NONE) */
	short shares;
	unsigned long h2_group;     /* its Halo 2 tag's: 'bipd', 'crea' */
	char name[H2_BIPED_NAME_SIZE];
	char h2_biped[128];
	short overshield_shader;    /* (its overshield's shader, NONE) */
	short shield_hit_shader;    /* (its shield's hit shader - Halo 2's shield damaged shader -, the object's modifier shader, NONE) */
	long h2_biped_tag;
	long h2_model_tag;
	long h2_coll_tag;
	long h2_jmad_tag;

	/* the model */
	short node_count;
	char node_names[MAXIMUM_NODES][32];
	short node_parent[MAXIMUM_NODES], node_child[MAXIMUM_NODES], node_sibling[MAXIMUM_NODES];
	/* Halo 2's nodes: the model's each is (a node left out: its nearest
	kept ancestor's - node_collapse) */
	short h2_node_count;
	short node_remap[MAXIMUM_NODES];
	char h2_node_names[MAXIMUM_NODES][32];
	long h2_hlmt_tag;
	struct h2_animation_pose node_default[MAXIMUM_NODES];    /* this engine's convention */
	float node_distance[MAXIMUM_NODES];
	float node_inverse[MAXIMUM_NODES][13];      /* scale, forward, left, up, position */
	long marker_count;
	struct made_marker markers[MAXIMUM_MARKERS];
	long region_count;
	struct made_region regions[H2_BIPED_MAXIMUM_REGIONS];
	long geometry_count;
	struct made_geometry *geometries;
	long shader_count;
	struct made_shader shaders[MAXIMUM_MATERIALS];
	short material_shader[MAXIMUM_MATERIALS];   /* Halo 2's materials: this model's shaders */
	float radius;
	float uv_scale;

	struct h2_graph *graph;
	/* (whose graph it plays: its own, or an earlier biped's of the same
	Halo 2 graph and skeleton - Halo 2's Elite's, the Heretics' -, made once) */
	short graph_of;
	/* (Halo 2's biped flies - a Drone: its graph's standing seat Halo 2's
	flight mode, h2_animation.c) */
	boolean flies;

	/* where its tags are, from the first (written); and from the bipeds'
	first, as gathered */
	long first_tag;
	long local_first_tag;
	struct h2_biped_report report;
};

struct h2_bipeds
{
	struct h2_cache *cache;
	/* (each the biped's own, made as the gather comes to it and kept if made;
	one not made - another level's, a mission's on a multiplayer level -
	biped_absent: 130 KB each, 2.2 MB all of them, no longer held for
	nothing at a mission's memory peak, piece H) */
	struct made_biped *bipeds[NUMBER_OF_H2_BIPEDS];
	long tag_count;
};

/* (a biped not made: none of its fields read but made, FALSE) */
static struct made_biped biped_absent;

/* which tags of a biped (its first's offset) */
enum
{
	_tag_biped = 0,
	_tag_model,
	_tag_collision,
	_tag_shaders,       /* and one a shader after it, then its graph (one of its own: graph_tag) */
};

/* a biped's graph tag (its own: after its shaders) */
#define GRAPH_TAG(biped) ((biped)->first_tag + _tag_shaders + (biped)->shader_count)

static struct
{
	char const *name;
	char const *biped;
	unsigned long group;        /* its Halo 2 tag's: a biped's, a creature's */
	short shares;               /* (a biped of another's model: that one, made first) NONE */
} const biped_sources[NUMBER_OF_H2_BIPEDS] =
{
	{ "spartan", "objects\\characters\\masterchief\\masterchief_mp", 'bipd', NONE },
	{ "elite", "objects\\characters\\elite\\elite_mp", 'bipd', NONE },
	{ "elite_sp", "objects\\characters\\elite\\elite", 'bipd', NONE },
	{ "grunt", "objects\\characters\\grunt\\grunt", 'bipd', NONE },
	{ "jackal", "objects\\characters\\jackal\\jackal", 'bipd', NONE },
	{ "marine", "objects\\characters\\marine\\marine", 'bipd', NONE },
	{ "hunter", "objects\\characters\\hunter\\hunter", 'bipd', NONE },
	{ "bugger", "objects\\characters\\bugger\\bugger", 'bipd', NONE },
	{ "brute", "objects\\characters\\brute\\brute", 'bipd', NONE },
	{ "tartarus", "objects\\characters\\brute\\tartarus\\brute_tartarus", 'bipd', NONE },
	{ "prophet_mercy", "objects\\characters\\prophet\\mercy\\prophet_mercy", 'bipd', NONE },
	{ "prophet_truth", "objects\\characters\\prophet\\truth\\prophet_truth", 'bipd', NONE },
	{ "prophet_minor", "objects\\characters\\prophet\\prophet_minor", 'bipd', NONE },
	{ "prophet_regret", "objects\\characters\\prophet\\regret\\prophet_regret", 'bipd', NONE },
	{ "regret_holo", "objects\\characters\\prophet\\regret_holo\\regret_holo", 'bipd', NONE },
	{ "miranda", "objects\\characters\\miranda\\miranda", 'bipd', NONE },
	{ "lord_hood", "objects\\characters\\lord_hood\\lord_hood", 'bipd', NONE },
	{ "cortana", "objects\\characters\\cortana\\cortana", 'bipd', NONE },
	{ "marine_female", "objects\\characters\\marine\\marine_female\\marine_female", 'bipd', NONE },
	{ "crewman", "objects\\characters\\marine\\crewman\\crewman", 'bipd', NONE },
	{ "elite_ranger", "objects\\characters\\elite\\elite_ranger\\elite_ranger", 'bipd', NONE },
	{ "flood_human", "objects\\characters\\floodcombat_human\\floodcombat_human", 'bipd', NONE },
	{ "flood_elite", "objects\\characters\\floodcombat_elite\\floodcombat_elite", 'bipd', NONE },
	{ "flood_carrier", "objects\\characters\\floodcarrier\\floodcarrier", 'bipd', NONE },
	{ "flood_infection", "objects\\characters\\flood_infection\\flood_infection", 'crea', NONE },
	{ "flood_juggernaut", "objects\\characters\\flood_juggernaut\\flood_juggernaut", 'bipd', NONE },
	{ "sentinel", "objects\\characters\\sentinel_aggressor\\sentinel_aggressor", 'bipd', NONE },
	{ "sentinel_constructor", "objects\\characters\\sentinel_constructor\\sentinel_constructor", 'crea', NONE },
	{ "sentinel_enforcer", "objects\\characters\\sentinel_enforcer\\sentinel_enforcer", 'vehi', NONE },
	{ "monitor", "objects\\characters\\monitor\\monitor", 'bipd', NONE },
	{ "heretic", "objects\\characters\\heretic\\heretic", 'bipd', NONE },
	{ "heretic_hologram", "objects\\characters\\heretic\\heretic_leader\\heretic_leader_hologram", 'bipd', NONE },
	{ "heretic_grunt", "objects\\characters\\heretic_grunt\\heretic_grunt", 'bipd', NONE },
	{ "gravemind", "objects\\characters\\gravemind\\gravemind", 'bipd', NONE },
	{ "flood_regret", "objects\\characters\\gravemind\\floodregret\\floodregret", 'bipd', NONE },
	{ "monitor_infected", "objects\\characters\\gravemind\\monitor_flood_infected\\monitor_flood_infected", 'bipd', NONE },
	{ "heretic_leader", "objects\\characters\\heretic\\heretic_leader\\heretic_leader", 'bipd', H2_BIPED_HERETIC },
	{ "heretic_leader_cine", "objects\\characters\\heretic\\heretic_leader\\heretic_leader_hologram_cinematic", 'bipd',
		H2_BIPED_HERETIC_HOLOGRAM },
	{ "sentinel_halo1", "objects\\characters\\sentinel_aggressor\\sentinel_aggressor_halo1", 'bipd', H2_BIPED_SENTINEL },
	{ "marine_massive", "objects\\characters\\marine\\marine_massive", 'bipd', H2_BIPED_MARINE },
};

/* ---------- private code: bytes */

H2_ALIGN static unsigned long read32(void const *p)
{
	unsigned char const *b = p;

	return (unsigned long)b[0] | ((unsigned long)b[1] << 8) | ((unsigned long)b[2] << 16) | ((unsigned long)b[3] << 24);
}

H2_ALIGN static short read16(void const *p)
{
	unsigned char const *b = p;

	return (short)(b[0] | (b[1] << 8));
}

H2_ALIGN static float readf(void const *p)
{
	float f;

	memcpy(&f, p, 4);
	return f;
}

H2_ALIGN static void write32(void *p, unsigned long v)
{
	unsigned char *b = p;

	b[0] = (unsigned char)v;
	b[1] = (unsigned char)(v >> 8);
	b[2] = (unsigned char)(v >> 16);
	b[3] = (unsigned char)(v >> 24);
}

H2_ALIGN static void write16(void *p, long v)
{
	unsigned char *b = p;

	b[0] = (unsigned char)v;
	b[1] = (unsigned char)(v >> 8);
}

H2_ALIGN static void writef(void *p, float f)
{
	memcpy(p, &f, 4);
}

/* rasterizer_geometry.c's compress_real_vector3d_to_int32_clamp */
H2_ALIGN static unsigned long compress_vector(float const *vector)
{
	float i = vector[0] < -1.f ? -1.f : vector[0] > 1.f ? 1.f : vector[0];
	float j = vector[1] < -1.f ? -1.f : vector[1] > 1.f ? 1.f : vector[1];
	float k = vector[2] < -1.f ? -1.f : vector[2] > 1.f ? 1.f : vector[2];
	long ci = (long)floor((double)i * 1023.5f) & 0x7FF;
	long cj = (long)floor((double)j * 1023.5f) & 0x7FF;
	long ck = (long)floor((double)k * 511.5f) & 0x3FF;

	return (unsigned long)((((ck << 11) | cj) << 11) | ci);
}

H2_ALIGN static short compress_real(float value)
{
	float clamped = value < -1.f ? -1.f : value > 1.f ? 1.f : value;

	return (short)(long)floor((double)clamped * 32767.5f);
}

H2_ALIGN static void name_of(struct h2_cache *cache, unsigned long id, char *out, long size)
{
	if (!h2_cache_string(cache, id, out, size))
		snprintf(out, (size_t)size, "%s", "");
}

H2_ALIGN static char const *short_name(char const *path)
{
	char const *slash = strrchr(path, '\\');

	return slash ? slash + 1 : path;
}

/* ---------- private code: pixels */

H2_ALIGN static unsigned long morton(unsigned long x, unsigned long y)
{
	/* (each coordinate's low 16 bits spread to the even bits - x - and the
	odd bits - y -: the bit loop's result in five steps; it was the
	builder's costliest function, once a texel of every lightmap atlas and
	swizzled bitmap) */
	unsigned long sx = x & 0xFFFFUL, sy = y & 0xFFFFUL;

	sx = (sx | (sx << 8)) & 0x00FF00FFUL;
	sx = (sx | (sx << 4)) & 0x0F0F0F0FUL;
	sx = (sx | (sx << 2)) & 0x33333333UL;
	sx = (sx | (sx << 1)) & 0x55555555UL;
	sy = (sy | (sy << 8)) & 0x00FF00FFUL;
	sy = (sy | (sy << 4)) & 0x0F0F0F0FUL;
	sy = (sy | (sy << 2)) & 0x33333333UL;
	sy = (sy | (sy << 1)) & 0x55555555UL;
	return (sx | sy << 1) & 0xFFFFFFFFUL;
}

/* a DXT1/3/5 image (Halo 2's formats 14, 15, 16) decoded: w x h, R G B A
bytes */
H2_ALIGN static boolean dxt_decode(unsigned short format, unsigned char const *in, unsigned long in_size,
	unsigned long w, unsigned long h, unsigned char *out)
{
	unsigned long bx, by, block_size = format == 14 ? 8 : 16;
	unsigned long blocks_wide = (w + 3) / 4;

	if (in_size < blocks_wide * ((h + 3) / 4) * block_size)
		return FALSE;
	for (by = 0; by < (h + 3) / 4; by++)
	{
		for (bx = 0; bx < blocks_wide; bx++)
		{
			unsigned char const *block = in + block_size * (by * blocks_wide + bx);
			unsigned char const *colour = format == 14 ? block : block + 8;
			unsigned int c0 = colour[0] | (colour[1] << 8), c1 = colour[2] | (colour[3] << 8);
			unsigned long bits = read32(colour + 4);
			unsigned char palette[4][3], alpha[16];
			int k, i;

			palette[0][0] = (unsigned char)((c0 >> 11) * 255 / 31);
			palette[0][1] = (unsigned char)(((c0 >> 5) & 63) * 255 / 63);
			palette[0][2] = (unsigned char)((c0 & 31) * 255 / 31);
			palette[1][0] = (unsigned char)((c1 >> 11) * 255 / 31);
			palette[1][1] = (unsigned char)(((c1 >> 5) & 63) * 255 / 63);
			palette[1][2] = (unsigned char)((c1 & 31) * 255 / 31);
			for (k = 0; k < 3; k++)
			{
				if (c0 > c1 || format != 14)
				{
					palette[2][k] = (unsigned char)((2 * palette[0][k] + palette[1][k]) / 3);
					palette[3][k] = (unsigned char)((palette[0][k] + 2 * palette[1][k]) / 3);
				}
				else
				{
					palette[2][k] = (unsigned char)((palette[0][k] + palette[1][k]) / 2);
					palette[3][k] = 0;
				}
			}
			for (i = 0; i < 16; i++)
				alpha[i] = 255;
			if (format == 15)
			{
				for (i = 0; i < 16; i++)
					alpha[i] = (unsigned char)(((block[i / 2] >> (4 * (i & 1))) & 15) * 17);
			}
			else if (format == 16)
			{
				unsigned char a[8];
				unsigned long long ab = 0;

				a[0] = block[0];
				a[1] = block[1];
				for (k = 0; k < 6; k++)
					ab |= (unsigned long long)block[2 + k] << (8 * k);
				if (a[0] > a[1])
				{
					for (k = 1; k < 7; k++)
						a[k + 1] = (unsigned char)((a[0] * (7 - k) + a[1] * k) / 7);
				}
				else
				{
					for (k = 1; k < 5; k++)
						a[k + 1] = (unsigned char)((a[0] * (5 - k) + a[1] * k) / 5);
					a[6] = 0;
					a[7] = 255;
				}
				for (i = 0; i < 16; i++)
					alpha[i] = a[(ab >> (3 * i)) & 7];
			}
			for (i = 0; i < 16; i++)
			{
				unsigned long x = 4 * bx + (i & 3), y = 4 * by + (i >> 2);
				int index = (int)((bits >> (2 * i)) & 3);

				if (x >= w || y >= h)
					continue;
				for (k = 0; k < 3; k++)
					out[4 * (y * w + x) + k] = palette[index][k];
				out[4 * (y * w + x) + 3] = alpha[i];
			}
		}
	}
	return TRUE;
}

/* a bitmap tag's first image (its top level) as R G B A bytes, its size;
NULL if it cannot be read or is not DXT or 32-bit */
H2_ALIGN static unsigned char *bitmap_rgba(struct h2_cache *cache, long tag, unsigned long *w, unsigned long *h)
{
	struct h2_cache_tag t;
	long count;
	unsigned long images, got = 0;
	unsigned char const *image;
	unsigned char *pixels, *out;
	unsigned short format;

	if (tag == NONE || !h2_cache_tag_get(cache, tag, &t) || !h2_cache_is_mcc(cache) ||
		!h2_cache_block(cache, t.address + 0x44, 0xA8, &count, &images) || count < 1 ||
		!(image = h2_cache_at(cache, images, 0xA8)))
	{
		return NULL;
	}
	*w = (unsigned short)read16(image + 4);
	*h = (unsigned short)read16(image + 6);
	format = (unsigned short)read16(image + 0xC);
	if (!*w || !*h || *w > 2048 || *h > 2048 || read16(image + 0xA) != 0)
		return NULL;
	pixels = h2_cache_texture_read(cache, read32(image + 0x38), read32(image + 0x50), &got);
	if (!pixels)
		return NULL;
	out = malloc(*w * *h * 4);
	if (out && (format == 14 || format == 15 || format == 16))
	{
		if (!dxt_decode(format, pixels, got, *w, *h, out))
		{
			free(out);
			out = NULL;
		}
	}
	else if (out && (format == 10 || format == 11) && got >= *w * *h * 4)
	{
		unsigned long i;

		/* (B G R A) */
		for (i = 0; i < *w * *h; i++)
		{
			out[4 * i] = pixels[4 * i + 2];
			out[4 * i + 1] = pixels[4 * i + 1];
			out[4 * i + 2] = pixels[4 * i];
			out[4 * i + 3] = format == 11 ? pixels[4 * i + 3] : 255;
		}
	}
	else
	{
		free(out);
		out = NULL;
	}
	free(pixels);
	return out;
}

/* a model shader's multipurpose map (this engine's order: red specular,
green self-illumination, blue colour change, alpha the auxiliary mask) made
from Halo 2's change colour map (red the primary colour's mask, green the
secondary's) and the base map's alpha (its specular): MULTIPURPOSE_SIZE a
side, A4R4G4B4, swizzled - given to the level as a bitmap (its index) */
/* its pixels at side a side (the change colour map's texels under each
averaged) */
H2_ALIGN static unsigned char *multipurpose_pixels(unsigned char const *cc, unsigned long cw, unsigned long ch,
	unsigned char const *base, unsigned long bw, unsigned long bh, unsigned char const *illum, unsigned long iw,
	unsigned long ih, boolean glow_full, unsigned long side)
{
	unsigned char *out = malloc(side * side * 2);
	unsigned long x, y;

	if (!out)
		return NULL;
	for (y = 0; y < side; y++)
	{
		for (x = 0; x < side; x++)
		{
			/* (the texels of the source under this one, averaged) */
			unsigned long sum[4] = { 0, 0, 0, 0 }, n = 0, sx, sy;
			unsigned long x0 = x * cw / side, x1 = (x + 1) * cw / side, y0 = y * ch / side, y1 = (y + 1) * ch / side;
			unsigned long specular = 0, a, r, g, b;

			if (x1 <= x0)
				x1 = x0 + 1;
			if (y1 <= y0)
				y1 = y0 + 1;
			for (sy = y0; sy < y1 && sy < ch; sy++)
			{
				for (sx = x0; sx < x1 && sx < cw; sx++)
				{
					sum[0] += cc[4 * (sy * cw + sx)];
					sum[1] += cc[4 * (sy * cw + sx) + 1];
					n++;
				}
			}
			if (base)
				specular = base[4 * ((y * bh / side) * bw + x * bw / side) + 3];
			n = n ? n : 1;
			r = specular >> 4;
			g = 0;
			b = (sum[0] / n) >> 4;
			a = (sum[1] / n) >> 4;
			/* (the self-illumination map's light, the glow's mask) */
			if (illum)
			{
				unsigned char const *q = illum + 4 * ((y * ih / side) * iw + x * iw / side);
				unsigned long l = q[0] > q[1] ? q[0] : q[1];

				l = l > q[2] ? l : q[2];
				g = l >> 4;
				/* (this engine's self-illumination lights the base map, the
				colour change over it: where it glows, untinted - Halo 2's
				glow is added over the tint, the Elite's orange lights not
				turned the armour's blue) */
				b = b * (255 - l) / 255;
			}
			/* (a glow throughout - Halo 2's illum templates, the Elite's inset
			lights: its map the base, all of it lit) */
			if (glow_full)
				g = 15;
			write16(out + 2 * morton(x, y), (long)((a << 12) | (r << 8) | (g << 4) | b));
		}
	}
	return out;
}

/* a self-illuminated shader's glow colour (its postprocess definition's
first overlay - 0x3C, 0x14 each: input name, range name, time period at 8,
its function's bytes at 0xC - Halo 2's self_illum_color animated: a colour
function, flag 0x20 in its second byte, its two colours at 4 and 16, B G R;
a periodic one (type 3) cycling them in its period, else the input's - the
shields', full - upper end); white if it has none */
H2_ALIGN static boolean illum_colours(struct h2_cache *cache, unsigned long shader, long map, struct made_shader *s)
{
	unsigned long iw = 0, ih = 0;
	unsigned char *pixels = bitmap_rgba(cache, map, &iw, &ih);
	long count, overlay_count, function_size;
	unsigned long definitions, overlays, function;
	unsigned char const *f;
	int c;

	for (c = 0; c < 3; c++)
		s->illum_lower[c] = s->illum_upper[c] = 1.0f;
	s->illum_period = 0.0f;
	if (h2_cache_block(cache, shader + 0x20, 0x7C, &count, &definitions) && count > 0 &&
		h2_cache_block(cache, definitions + 0x3C, 0x14, &overlay_count, &overlays) && overlay_count > 0 &&
		h2_cache_block(cache, overlays + 0xC, 1, &function_size, &function) && function_size >= 20 &&
		(f = h2_cache_at(cache, function, 20)) != NULL && (f[1] & 0x20))
	{
		unsigned char const *f0 = h2_cache_at(cache, overlays, 0x14);

		for (c = 0; c < 3; c++)
		{
			s->illum_lower[c] = f[4 + 2 - c] / 255.0f;
			s->illum_upper[c] = f[16 + 2 - c] / 255.0f;
		}
		/* (an input - the Elite's arm and leg lights' shield_vitality -:
		its glow by the shields, black without them) */
		s->illum_shield = f[0] != 3 && f0 && read32(f0) != 0 && !getenv("HALO_H2_BIPED_GLOW_STEADY");
		if (f[0] == 3 && f0)
		{
			union { unsigned long u; float f; } period;

			period.u = read32(f0 + 8);
			s->illum_period = period.f > 0.0f && period.f < 60.0f ? period.f : 1.0f;
		}
		else
		{
			for (c = 0; c < 3; c++)
				s->illum_lower[c] = s->illum_upper[c];
		}
	}
	/* (times the map's own colour - the Elite's arm and leg lights are
	orange in their map, its torso's grey: its lit texels' mean, its
	brightest channel 1) */
	if (pixels)
	{
		unsigned long k, sum[3] = { 0, 0, 0 }, lit = 0, top;

		for (k = 0; k < iw * ih; k++)
		{
			if (pixels[4 * k] + pixels[4 * k + 1] + pixels[4 * k + 2] > 96)
			{
				for (c = 0; c < 3; c++)
					sum[c] += pixels[4 * k + c];
				lit++;
			}
		}
		top = sum[0] > sum[1] ? sum[0] : sum[1];
		top = sum[2] > top ? sum[2] : top;
		for (c = 0; c < 3 && top; c++)
		{
			s->illum_lower[c] *= (float)sum[c] / (float)top;
			s->illum_upper[c] *= (float)sum[c] / (float)top;
		}
		free(pixels);
	}
	return TRUE;
}

/* (a direct level made without its checks) whether bitmap_rgba would give
the tag's first image - its description and its entry's inflated bytes, not
inflated - and its width and height */
H2_ALIGN static long multipurpose_make_made(struct h2_biped_host const *host, char const *name, long change_colour_tag,
	long base_tag, long illum_tag, boolean glow_full, boolean predicted_made);

H2_ALIGN static boolean bitmap_rgba_ok(struct h2_cache *cache, long tag, unsigned long *w, unsigned long *h)
{
	struct h2_cache_tag t;
	long count;
	unsigned long images, got = 0;
	unsigned char const *image;
	unsigned short format;

	*w = *h = 0;
	if (tag == NONE || !h2_cache_tag_get(cache, tag, &t) || !h2_cache_is_mcc(cache) ||
		!h2_cache_block(cache, t.address + 0x44, 0xA8, &count, &images) || count < 1 ||
		!(image = h2_cache_at(cache, images, 0xA8)))
	{
		return FALSE;
	}
	*w = (unsigned short)read16(image + 4);
	*h = (unsigned short)read16(image + 6);
	format = (unsigned short)read16(image + 0xC);
	if (!*w || !*h || *w > 2048 || *h > 2048 || read16(image + 0xA) != 0 ||
		!h2_cache_texture_inflated_size(cache, read32(image + 0x38), read32(image + 0x50), &got))
	{
		return FALSE;
	}
	if (format == 14 || format == 15 || format == 16)
		return got >= ((*w + 3) / 4) * ((*h + 3) / 4) * (format == 14 ? 8UL : 16UL);
	return (format == 10 || format == 11) && got >= *w * *h * 4;
}

H2_ALIGN static long multipurpose_make(struct h2_biped_host const *host, char const *name, long change_colour_tag,
	long base_tag, long illum_tag, boolean glow_full)
{
	unsigned long side = MULTIPURPOSE_SIZE;
	char path[256];
	/* (described where the level leaves it out - a direct level made
	without its checks, no sidecar: its size the side's, made or not by its
	sources' descriptions alone) */
	unsigned long dw, dh;
	boolean cc_ok = change_colour_tag != NONE && bitmap_rgba_ok(host->cache, change_colour_tag, &dw, &dh);
	boolean base_ok = base_tag != NONE && bitmap_rgba_ok(host->cache, base_tag, &dw, &dh);
	boolean predicted_made = !((change_colour_tag != NONE && !cc_ok) || (!cc_ok && !base_ok));

	snprintf(path, sizeof(path), "h2\\characters\\bitmaps\\%s multipurpose", name);
	if (host->bitmap_described && !host->sidecar_made)
	{
		long described;

		if (!predicted_made)
			return NONE;
		if (host->recipe)
		{
			unsigned long args[4];

			args[0] = (unsigned long)change_colour_tag;
			args[1] = (unsigned long)base_tag;
			args[2] = (unsigned long)illum_tag;
			args[3] = glow_full ? 1 : 0;
			host->recipe(host->context, args, 4);
		}
		described = host->bitmap_described(host->context, path, (unsigned short)side, (unsigned short)side, 9, 0, side * side * 2);
		if (described != H2_BIPED_NOT_DESCRIBED)
			return described;
	}
	return multipurpose_make_made(host, name, change_colour_tag, base_tag, illum_tag, glow_full, predicted_made);
}

H2_ALIGN static long multipurpose_make_made(struct h2_biped_host const *host, char const *name, long change_colour_tag,
	long base_tag, long illum_tag, boolean glow_full, boolean predicted_made)
{
	unsigned long cw = 0, ch = 0, bw = 0, bh = 0, iw = 0, ih = 0;
	unsigned char *illum = illum_tag != NONE ? bitmap_rgba(host->cache, illum_tag, &iw, &ih) : NULL;
	unsigned char *cc = change_colour_tag != NONE ? bitmap_rgba(host->cache, change_colour_tag, &cw, &ch) : NULL;
	unsigned char *base = base_tag != NONE ? bitmap_rgba(host->cache, base_tag, &bw, &bh) : NULL;
	unsigned long side = MULTIPURPOSE_SIZE, full = 1;
	unsigned char *out;
	char path[256];
	long made;
	boolean made_so = !((change_colour_tag != NONE && !cc) || (!cc && !base));

	/* (its description - whether it is made - checked against the make) */
	if (made_so != predicted_made)
		platform_log("h2 bipeds: %s multipurpose described %s, %s - MISDESCRIBED", name, predicted_made ? "made" : "not made",
			made_so ? "made" : "not made");
	/* (without a change colour map - an environment mapped shader's, the
	visor's: its specular mask alone, the base map's alpha) */
	if (!made_so)
	{
		free(cc);
		free(base);
		free(illum);
		return NONE;
	}
	if (!cc)
	{
		cw = bw;
		ch = bh;
	}
	out = multipurpose_pixels(cc, cc ? cw : 0, cc ? ch : 0, base, bw, bh, illum, iw, ih, glow_full, side);
	if (!out)
	{
		free(cc);
		free(base);
		free(illum);
		return NONE;
	}
	snprintf(path, sizeof(path), "h2\\characters\\bitmaps\\%s multipurpose", name);
	/* (the level makes it again alone at load from these: piece J) */
	if (host->recipe)
	{
		unsigned long args[4];

		args[0] = (unsigned long)change_colour_tag;
		args[1] = (unsigned long)base_tag;
		args[2] = (unsigned long)illum_tag;
		args[3] = glow_full ? 1 : 0;
		host->recipe(host->context, args, 4);
	}
	made = host->bitmap_made(host->context, path, (unsigned short)side, (unsigned short)side, 9, 0, out, side * side * 2);
	/* (the PC tier's: the change colour map's own size, up to 1024, in the
	level's sidecar - h2_texture_sidecar.h) */
	while (full * 2 <= (cw > ch ? cw : ch) && full < 1024)
		full *= 2;
	if (made != NONE && host->sidecar_made && full > side)
	{
		unsigned char *large = multipurpose_pixels(cc, cc ? cw : 0, cc ? ch : 0, base, bw, bh, illum, iw, ih, glow_full, full);

		if (large)
			host->sidecar_made(host->context, made, (unsigned short)full, (unsigned short)full, large, full * full * 2);
	}
	free(cc);
	free(base);
	free(illum);
	return made;
}

/* a transparent shader's map (h2_biped's added chicago shaders): its
bitmap's brightness times the tint, TINTED_SIZE a side, A4R4G4B4 swizzled,
given to the level; NONE */
H2_ALIGN static long tinted_make(struct h2_biped_host const *host, char const *owner, char const *name, long bitmap_tag, float const tint[3])
{
	unsigned long w = 0, h = 0, x, y, side = 64;
	unsigned char *rgba = bitmap_rgba(host->cache, bitmap_tag, &w, &h);
	unsigned char *out;
	char path[256];
	long made;

	if (!rgba)
		return NONE;
	out = malloc(side * side * 2);
	if (!out)
	{
		free(rgba);
		return NONE;
	}
	for (y = 0; y < side; y++)
	{
		for (x = 0; x < side; x++)
		{
			unsigned char const *t = rgba + 4 * ((y * h / side) * w + x * w / side);
			/* (its brightness: the colour's, else - a mask's alpha alone - its alpha's) */
			float level = (t[0] + t[1] + t[2]) / (3.0f * 255.0f);
			unsigned long c[3];
			int k;

			if (level < 0.02f)
				level = t[3] / 255.0f;
			for (k = 0; k < 3; k++)
			{
				float v = level * tint[k];

				c[k] = (unsigned long)(v <= 0.0f ? 0 : v >= 1.0f ? 15 : v * 15.0f + 0.5f);
			}
			write16(out + 2 * morton(x, y), (long)((15UL << 12) | (c[0] << 8) | (c[1] << 4) | c[2]));
		}
	}
	free(rgba);
	snprintf(path, sizeof(path), "h2\\characters\\bitmaps\\%s %s tinted", owner, name);
	made = host->bitmap_made(host->context, path, (unsigned short)side, (unsigned short)side, 9, 0, out, side * side * 2);
	return made;
}

/* a map drawn through Halo 2's hologram pass (h2_hologram_pass_texel): its
colour - the pass's T0, the hologram's own look behind it; here the map's
colour, unlit - inverted by part, times the value and the colour, alpha
blended at the pass's alpha (h2_hologram_pass_read: its scanline and noise
maps' mean) - 64 square, A4R4G4B4 */
H2_ALIGN static long hologram_pass_make(struct h2_biped_host const *host, char const *owner, char const *name, long bitmap_tag,
	struct h2_hologram_pass const *pass)
{
	unsigned long w = 0, h = 0, x, y, side = 64, alpha;
	unsigned char *rgba = bitmap_rgba(host->cache, bitmap_tag, &w, &h);
	unsigned char *out;
	char path[256];

	if (!rgba || !w || !h)
	{
		free(rgba);
		return NONE;
	}
	if (!(out = malloc(side * side * 2)))
	{
		free(rgba);
		return NONE;
	}
	alpha = (unsigned long)(pass->alpha * 15.0f + 0.5f);
	alpha = alpha > 15 ? 15 : alpha;
	for (y = 0; y < side; y++)
	{
		for (x = 0; x < side; x++)
		{
			unsigned char const *t = rgba + 4 * ((y * h / side) * w + x * w / side);
			float in[3], c[3];
			unsigned long q[3];
			int k;

			for (k = 0; k < 3; k++)
				in[k] = t[k] / 255.0f;
			h2_hologram_pass_texel(pass, in, c);
			for (k = 0; k < 3; k++)
				q[k] = (unsigned long)(c[k] * 15.0f + 0.5f);
			write16(out + 2 * morton(x, y), (long)((alpha << 12) | (q[0] << 8) | (q[1] << 4) | q[2]));
		}
	}
	free(rgba);
	snprintf(path, sizeof(path), "h2\\characters\\bitmaps\\%s %s hologram", owner, name);
	return host->bitmap_made(host->context, path, (unsigned short)side, (unsigned short)side, 9, 0, out, side * side * 2);
}

/* (this level's: h2_bipeds_gather begins it again) */
static long emblem_stand_in_bitmap = NONE;

/* the emblem shaders' stand-in map (h2_emblem.h): nothing to see, one tag of
the level however many shaders name it */
H2_ALIGN static long emblem_stand_in(struct h2_biped_host const *host)
{
	unsigned char *pixels;

	if (emblem_stand_in_bitmap != NONE)
		return emblem_stand_in_bitmap;
	if (!(pixels = calloc(1, 4 * 4 * 2)))
		return NONE;
	emblem_stand_in_bitmap = host->bitmap_made(host->context, H2_EMBLEM_STAND_IN, 4, 4, 9, 0, pixels, 4 * 4 * 2);
	return emblem_stand_in_bitmap;
}

/* Cortana's two passes baked (shader_make): the map's colour times the
alpha blend colour, plus the traces' sharp colour by the map's brightness
(her glowing lines), its alpha the map's times the opacity - 128 square,
A4R4G4B4 */
H2_ALIGN static long cortana_make(struct h2_biped_host const *host, char const *owner, char const *name, long bitmap_tag,
	unsigned long colour, unsigned long opacity, unsigned long sharp)
{
	unsigned long w = 0, h = 0, x, y, side = 128;
	unsigned char *rgba = bitmap_rgba(host->cache, bitmap_tag, &w, &h);
	unsigned char *out;
	char path[256];
	float gain = getenv("HALO_H2_CORTANA_TRACES") ? (float)atof(getenv("HALO_H2_CORTANA_TRACES")) : 2.0f;

	if (!rgba || !w || !h)
	{
		free(rgba);
		return NONE;
	}
	out = malloc(side * side * 2);
	if (!out)
	{
		free(rgba);
		return NONE;
	}
	for (y = 0; y < side; y++)
	{
		for (x = 0; x < side; x++)
		{
			unsigned char const *t = rgba + 4 * ((y * h / side) * w + x * w / side);
			float level = (t[0] + t[1] + t[2]) / (3.0f * 255.0f);
			unsigned long c[3], a;
			int k;

			for (k = 0; k < 3; k++)
			{
				float base = t[k] / 255.0f * ((colour >> (16 - 8 * k)) & 255) / 255.0f;
				float trace = gain * level * level * ((sharp >> (16 - 8 * k)) & 255) / 255.0f;
				float v = base + trace;

				c[k] = (unsigned long)(v <= 0.0f ? 0 : v >= 1.0f ? 15 : v * 15.0f + 0.5f);
			}
			a = (unsigned long)(t[3] * opacity / 255UL) >> 4;
			write16(out + 2 * morton(x, y), (long)((a << 12) | (c[0] << 8) | (c[1] << 4) | c[2]));
		}
	}
	free(rgba);
	snprintf(path, sizeof(path), "h2\\characters\\bitmaps\\%s %s cortana", owner, name);
	return host->bitmap_made(host->context, path, (unsigned short)side, (unsigned short)side, 9, 0, out, side * side * 2);
}

/* ---------- private code: the render model */

/* a section's raw resource read: its parts, strips, descriptors, buffers
and node map (pointers into raw, which the caller frees) */
struct section_raw
{
	unsigned char *raw;
	unsigned char const *parts, *strips, *descriptors, *node_map;
	unsigned long parts_size, strips_size, descriptors_size, node_map_size;
	unsigned char const *buffers[8];
	unsigned long buffer_sizes[8];
};

H2_ALIGN static boolean section_raw_read(struct h2_cache *cache, unsigned long section, struct section_raw *s)
{
	unsigned char const *g = h2_cache_at(cache, section, H2_MODE_SECTION_SIZE);
	unsigned long reference, size, section_size, resources, data;
	long resource_count, i;

	memset(s, 0, sizeof(*s));
	if (!g)
		return FALSE;
	reference = read32(g + 0x38);
	size = read32(g + 0x3C);
	section_size = read32(g + 0x40);
	if (h2_cache_is_mcc(cache) && reference != 0xFFFFFFFFUL && !(reference >> 30))
		reference |= (unsigned long)h2_cache_address_source(cache, section) << 30;
	if (!size || reference == 0xFFFFFFFFUL || size > H2_SECTION_MAXIMUM_BYTES || section_size > size ||
		!h2_cache_block(cache, section + 0x48, 16, &resource_count, &resources))
	{
		return FALSE;
	}
	s->raw = malloc(size);
	if (!s->raw || !h2_cache_resource_read(cache, reference, s->raw, size) || memcmp(s->raw, "hklb", 4) != 0 ||
		8 + section_size > size)
	{
		free(s->raw);
		s->raw = NULL;
		return FALSE;
	}
	data = 8 + section_size;
	for (i = 0; i < resource_count; i++)
	{
		unsigned char const *r = h2_cache_at(cache, resources + 16 * i, 16);
		unsigned char type = r ? r[0] : 0xFF;
		unsigned short primary = r ? (unsigned short)read16(r + 2) : 0;
		short element = r ? read16(r + 6) : -1;
		unsigned long rsize = r ? read32(r + 8) : 0, roffset = r ? read32(r + 12) : 0;
		unsigned char const *p = s->raw + data + roffset;

		if (!r || roffset > size - data || rsize > size - data - roffset)
			continue;
		if (type == 0 && primary == 0)
			s->parts = p, s->parts_size = rsize;
		else if (type == 0 && primary == 0x20)
			s->strips = p, s->strips_size = rsize;
		else if (type == 0 && primary == 0x38)
			s->descriptors = p, s->descriptors_size = rsize;
		else if (type == 0 && primary == 0x68)
			s->node_map = p, s->node_map_size = rsize;
		else if (type == 2 && element >= 0 && element < 8)
			s->buffers[element] = p, s->buffer_sizes[element] = rsize;
	}
	return s->parts && s->strips && s->descriptors;
}

/* a vertex decoded */
struct vertex
{
	float position[3];
	float uv[2];
	float normal[3], binormal[3], tangent[3];
	unsigned char nodes[2];
	float weight;
};

/* (the strip of a part: Halo 2's counter-clockwise triangles given
clockwise, joined by repeated indices - h2_level.c's strip_triangle) */
H2_ALIGN static void strip_triangle(unsigned short *strip, long *count, unsigned short a, unsigned short b, unsigned short c)
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

/* a section made a geometry of this engine's (its parts by the model's
shaders, vertices skinned to two nodes at most) */
H2_ALIGN static boolean geometry_make(struct h2_cache *cache, struct made_biped *biped, unsigned long model,
	short section_index, struct made_geometry *geometry)
{
	unsigned long section = 0;
	long section_count;
	unsigned long sections;
	unsigned char const *header;
	struct section_raw s;
	struct vertex *vertices = NULL;
	long vertex_count, i, k;
	unsigned short flags;
	short rigid;
	float position_bounds[6] = { -1, 1, -1, 1, -1, 1 }, uv_bounds[4] = { -1, 1, -1, 1 };
	boolean ok = FALSE;

	memset(geometry, 0, sizeof(*geometry));
	geometry->section = section_index;
	if (!h2_cache_block(cache, model + H2_MODE_SECTIONS, H2_MODE_SECTION_SIZE, &section_count, &sections) ||
		section_index < 0 || section_index >= section_count)
	{
		return FALSE;
	}
	section = sections + H2_MODE_SECTION_SIZE * section_index;
	header = h2_cache_at(cache, section, H2_MODE_SECTION_SIZE);
	if (!header || !section_raw_read(cache, section, &s))
		return FALSE;
	vertex_count = (unsigned short)read16(header + 4);
	flags = (unsigned short)read16(header + 0x1A);
	rigid = read16(header + 0x2C);
	{
		long count;
		unsigned long compression;
		unsigned char const *c;

		if (((h2_cache_block(cache, section + 0x1C, 0x38, &count, &compression) && count > 0) ||
				(h2_cache_block(cache, model + H2_MODE_COMPRESSION, 0x38, &count, &compression) && count > 0)) &&
			(c = h2_cache_at(cache, compression, 0x38)) != NULL)
		{
			for (k = 0; k < 6; k++)
				position_bounds[k] = readf(c + 4 * k);
			for (k = 0; k < 4; k++)
				uv_bounds[k] = readf(c + 0x18 + 4 * k);
		}
	}
	vertices = calloc((unsigned long)vertex_count + 1, sizeof(*vertices));
	if (!vertices || vertex_count < 1)
		goto done;
	/* the buffers */
	{
		boolean have_position = FALSE, have_uv = FALSE, have_frame = FALSE;

		for (i = 0; i < (long)(s.descriptors_size / 32) && i < 8; i++)
		{
			unsigned char type = s.descriptors[32 * i];
			unsigned char const *b = s.buffers[i];
			unsigned long bs = s.buffer_sizes[i], stride;

			if (!b)
				continue;
			stride = bs / (unsigned long)vertex_count;
			/* (0x08: the Jackal's body's, Johnson's head's, as 0x04 - a position
			and four nodes and weights) */
			if (type == 0x01 || type == 0x02 || type == 0x04 || type == 0x06 || type == 0x08)
			{
				if (type == 0x08)
					type = 0x04;
				if (stride < 12 || (type != 0x01 && stride < 16) || ((type == 0x04 || type == 0x06) && stride < 20))
					goto done;
				for (k = 0; k < vertex_count; k++)
				{
					unsigned char const *v = b + stride * k;
					struct vertex *o = &vertices[k];
					int c;

					for (c = 0; c < 3; c++)
					{
						o->position[c] = readf(v + 4 * c);
						if (flags & 1)
							o->position[c] = position_bounds[2 * c] + 0.5f * (o->position[c] + 1.0f) * (position_bounds[2 * c + 1] - position_bounds[2 * c]);
					}
					/* its nodes: the section's node map's (the model's) */
					if (type == 0x01)
					{
						o->nodes[0] = o->nodes[1] = (unsigned char)(rigid >= 0 && rigid < biped->h2_node_count ? biped->node_remap[rigid] : 0);
						o->weight = 1.0f;
					}
					else
					{
						int count = type == 0x02 ? 1 : 4, best = 0, second = -1;
						unsigned char local[4];
						float weights[4];

						for (c = 0; c < count; c++)
						{
							local[c] = v[12 + c];
							weights[c] = type == 0x02 ? 1.0f : v[16 + c] / 255.0f;
						}
						for (c = 1; c < count; c++)
						{
							if (weights[c] > weights[best])
								best = c;
						}
						for (c = 0; c < count; c++)
						{
							if (c != best && weights[c] > 0.0f && (second < 0 || weights[c] > weights[second]))
								second = c;
						}
						for (c = 0; c < 2; c++)
						{
							int which = c == 0 ? best : second;
							unsigned char node = which >= 0 ? local[which] : local[best];

							if (s.node_map && node < s.node_map_size)
								node = s.node_map[node];
							o->nodes[c] = (unsigned char)(node < biped->h2_node_count ? biped->node_remap[node] : 0);
						}
						o->weight = second >= 0 && weights[best] + weights[second] > 0.0f ?
							weights[best] / (weights[best] + weights[second]) : 1.0f;
					}
				}
				have_position = TRUE;
			}
			else if (type == 0x17 && stride >= 8)
			{
				for (k = 0; k < vertex_count; k++)
				{
					int c;

					for (c = 0; c < 2; c++)
					{
						float u = readf(b + stride * k + 4 * c);

						if (flags & 2)
							u = uv_bounds[2 * c] + 0.5f * (u + 1.0f) * (uv_bounds[2 * c + 1] - uv_bounds[2 * c]);
						vertices[k].uv[c] = u;
					}
				}
				have_uv = TRUE;
			}
			else if (type == 0x19 && stride >= 36)
			{
				for (k = 0; k < vertex_count; k++)
				{
					memcpy(vertices[k].normal, b + stride * k, 12);
					memcpy(vertices[k].binormal, b + stride * k + 12, 12);
					memcpy(vertices[k].tangent, b + stride * k + 24, 12);
				}
				have_frame = TRUE;
			}
		}
		if (!have_position || !have_uv || !have_frame)
		{
			if (getenv("HALO_H2_DEBUG"))
			{
				char types[64];

				types[0] = 0;
				for (i = 0; i < (long)(s.descriptors_size / 32) && i < 8; i++)
					snprintf(types + strlen(types), sizeof(types) - strlen(types), " %02x/%lu", s.descriptors[32 * i],
						s.buffers[i] ? s.buffer_sizes[i] / (unsigned long)vertex_count : 0UL);
				platform_log("h2 bipeds: %s: section %d: buffers%s (position %d uv %d frame %d)", biped->name, (int)section_index,
					types, have_position, have_uv, have_frame);
			}
			goto done;
		}
	}
	/* the parts: strips (or lists, flag bit 3) made this engine's strips,
	a part each material */
	for (i = 0; i < (long)(s.parts_size / H2_PART_SIZE) && geometry->part_count < MAXIMUM_PARTS; i++)
	{
		unsigned char const *p = s.parts + H2_PART_SIZE * i;
		short part_flags = read16(p + 2), material = read16(p + 4);
		long first = (unsigned short)read16(p + 6), count = (unsigned short)read16(p + 8);
		unsigned short const *indices = (unsigned short const *)s.strips;
		long index_count = (long)(s.strips_size / 2), n, used = 0, strip_count = 0;
		long *remap;
		unsigned short *strip;
		struct made_part *part;
		short shader;
		long node_votes[MAXIMUM_NODES];
		double centroid[3] = { 0, 0, 0 };

		if (material < 0 || material >= MAXIMUM_MATERIALS || (shader = biped->material_shader[material]) == NONE ||
			biped->shaders[shader].skipped || first > index_count || count > index_count - first || count < 3)
		{
			continue;
		}
		remap = malloc((unsigned long)vertex_count * sizeof(long));
		strip = malloc((unsigned long)count * 6 * sizeof(unsigned short) + 16);
		if (!remap || !strip)
		{
			free(remap);
			free(strip);
			goto done;
		}
		for (n = 0; n < vertex_count; n++)
			remap[n] = -1;
		memset(node_votes, 0, sizeof(node_votes));
		part = &geometry->parts[geometry->part_count];
		memset(part, 0, sizeof(*part));
		part->vertices = malloc((unsigned long)vertex_count * VERTEX_SIZE);
		if (!part->vertices)
		{
			free(remap);
			free(strip);
			goto done;
		}
		for (n = 0; n + 2 < count; n += (part_flags & 8) ? 3 : 1)
		{
			unsigned short a = indices[first + n], b = indices[first + n + 1], c = indices[first + n + 2];
			boolean odd = !(part_flags & 8) && (n & 1);
			unsigned short corners[3];
			int q;

			if (a == b || b == c || a == c || a >= vertex_count || b >= vertex_count || c >= vertex_count)
				continue;
			corners[0] = a;
			corners[1] = odd ? c : b;
			corners[2] = odd ? b : c;
			for (q = 0; q < 3; q++)
			{
				long v = corners[q];

				if (remap[v] < 0)
				{
					struct vertex const *in = &vertices[v];
					unsigned char *o = part->vertices + VERTEX_SIZE * used;
					int c2;

					remap[v] = used++;
					for (c2 = 0; c2 < 3; c2++)
					{
						writef(o + 4 * c2, in->position[c2]);
						centroid[c2] += in->position[c2];
					}
					write32(o + 12, compress_vector(in->normal));
					write32(o + 16, compress_vector(in->binormal));
					write32(o + 20, compress_vector(in->tangent));
					writef(o + 24, in->uv[0]);      /* (floats until the scale is known) */
					writef(o + 28, in->uv[1]);
					node_votes[in->nodes[0]]++;
					{
						float d = sqrtf(in->position[0] * in->position[0] + in->position[1] * in->position[1] +
							in->position[2] * in->position[2]);

						if (d > biped->radius)
							biped->radius = d;
					}
				}
				corners[q] = (unsigned short)remap[v];
			}
			if (HALO_ENV_ONCE("HALO_H2_BIPED_FLIP"))
				strip_triangle(strip, &strip_count, corners[0], corners[2], corners[1]);
			else
				strip_triangle(strip, &strip_count, corners[0], corners[1], corners[2]);
			geometry->triangles++;
		}
		if (!used || strip_count < 3)
		{
			free(part->vertices);
			free(remap);
			free(strip);
			continue;
		}
		/* the nodes and weights, in a table after the vertices (moved in by
		vertices_finish) - a copy as long as they are */
		{
			unsigned char *grown = malloc((unsigned long)used * VERTEX_SIZE + (unsigned long)used * 8);

			if (!grown)
			{
				free(part->vertices);
				free(remap);
				free(strip);
				goto done;
			}
			memcpy(grown, part->vertices, (unsigned long)used * VERTEX_SIZE);
			free(part->vertices);
			part->vertices = grown;
			for (n = 0; n < vertex_count; n++)
			{
				if (remap[n] >= 0)
				{
					unsigned char *side = part->vertices + (unsigned long)used * VERTEX_SIZE + 8 * remap[n];

					side[0] = vertices[n].nodes[0];
					side[1] = vertices[n].nodes[1];
					writef(side + 4, vertices[n].weight);
				}
			}
		}
		part->shader = shader;
		part->vertex_count = used;
		/* (the strip as long as it is: it was made room for the worst) */
		{
			unsigned short *fitted = malloc((unsigned long)strip_count * sizeof(unsigned short) + 2);

			if (fitted)
			{
				memcpy(fitted, strip, (unsigned long)strip_count * sizeof(unsigned short));
				free(strip);
				strip = fitted;
			}
		}
		part->strip = strip;
		part->strip_count = strip_count;
		for (k = 0; k < 3; k++)
			part->centroid[k] = (float)(centroid[k] / used);
		part->centroid_node = 0;
		for (k = 1; k < biped->node_count; k++)
		{
			if (node_votes[k] > node_votes[part->centroid_node])
				part->centroid_node = (short)k;
		}
		geometry->vertices += used;
		geometry->part_count++;
		free(remap);
	}
	ok = geometry->part_count > 0;

done:
	free(vertices);
	free(s.raw);
	return ok;
}

/* the texture coordinates compressed (the model's scale: a power of two
over the largest) and the nodes put in */
H2_ALIGN static void vertices_finish(struct made_biped *biped)
{
	long g, p, n;
	float largest = 0.0f;

	for (g = 0; g < biped->geometry_count; g++)
	{
		for (p = 0; p < biped->geometries[g].part_count; p++)
		{
			struct made_part *part = &biped->geometries[g].parts[p];

			for (n = 0; n < part->vertex_count; n++)
			{
				unsigned char const *v = part->vertices + VERTEX_SIZE * n;
				float u = fabsf(readf(v + 24)), w = fabsf(readf(v + 28));

				largest = u > largest ? u : largest;
				largest = w > largest ? w : largest;
			}
		}
	}
	for (biped->uv_scale = 1.0f; biped->uv_scale < largest && biped->uv_scale < 65536.0f; biped->uv_scale *= 2.0f)
		;
	for (g = 0; g < biped->geometry_count; g++)
	{
		for (p = 0; p < biped->geometries[g].part_count; p++)
		{
			struct made_part *part = &biped->geometries[g].parts[p];
			unsigned char *side = part->vertices + (unsigned long)part->vertex_count * VERTEX_SIZE;

			for (n = 0; n < part->vertex_count; n++)
			{
				unsigned char *v = part->vertices + VERTEX_SIZE * n;
				float u = readf(v + 24), w = readf(v + 28), weight = readf(side + 8 * n + 4);

				write16(v + 24, compress_real(u / biped->uv_scale));
				write16(v + 26, compress_real(w / biped->uv_scale));
				/* (the Xbox's: node indices times three, the first's weight) */
				{
					/* (HALO_H2_BIPED_NODE_LIMIT=n, tests: nodes past n skinned to
					their nearest ancestor below it) */
					long limit = HALO_ENV_ONCE("HALO_H2_BIPED_NODE_LIMIT") ? atol(HALO_ENV_ONCE("HALO_H2_BIPED_NODE_LIMIT")) : 0;
					int k;

					for (k = 0; k < 2 && limit > 0; k++)
					{
						short node = side[8 * n + k];

						while (node >= limit && node >= 0 && node < biped->node_count)
							node = biped->node_parent[node];
						side[8 * n + k] = (unsigned char)(node >= 0 ? node : 0);
					}
				}
				v[28] = (unsigned char)(3 * side[8 * n]);
				v[29] = (unsigned char)(3 * side[8 * n + 1]);
				write16(v + 30, compress_real(weight));
			}
		}
	}
}

/* the model's shader for a Halo 2 material's shader (made once) */
/* a Halo 2 shader's tint: its brightest pixel constant (its postprocess
definition's, 0x20: at 0xC, 4 bytes each, a r g b - a Jackal's shield's
95 FF CA, the mint green its plasma glows), its hue at full brightness and
a third white (Halo 2's plasma glows brighter than its constant); white if
it has none */
H2_ALIGN boolean h2_hologram_pass_read(
	struct h2_cache *cache,
	long h2_shader,
	struct h2_hologram_pass *pass)
{
	struct h2_cache_tag t;
	long count, pixel_count, bitmap_count, vertex_count, template_tag, m;
	unsigned long definitions, pixels, bitmaps, vertices;
	unsigned long c[4];
	unsigned char *maps[3] = { NULL, NULL, NULL };
	unsigned long w[3] = { 0, 0, 0 }, h[3] = { 0, 0, 0 };
	float scale[3][4] = { { 1, 1, 0, 0 }, { 1, 1, 0, 0 }, { 1, 1, 0, 0 } };
	double sum = 0.0;
	int x, y, k;

	memset(pass, 0, sizeof(*pass));
	/* (opt-in, HALO_H2_HOLOGRAM_PASS=1: with T0 the hologram's map unlit -
this engine has no framebuffer read or view-space coordinates in its
transparent path - Regret comes out a flat translucent blue where the
Xbox (00a footage 133.5 s) shows a pale, lit, detailed figure: T0 there
is lit, likely the object's own render. Off, the tinted additive look) */
	if (!getenv("HALO_H2_HOLOGRAM_PASS") || !h2_cache_tag_get(cache, h2_shader, &t) ||
		(template_tag = h2_cache_reference(cache, t.address)) == NONE ||
		!strstr(h2_cache_tag_name(cache, template_tag), "\\hologram\\hologram") ||
		!h2_cache_block(cache, t.address + 0x20, 0x7C, &count, &definitions) || count < 1 ||
		!h2_cache_block(cache, definitions + 0xC, 4, &pixel_count, &pixels) || pixel_count < 4)
	{
		return FALSE;
	}
	for (k = 0; k < 4; k++)
		c[k] = h2_cache_read32(cache, pixels + 4 * k);
	pass->invert = (float)((c[0] >> 24) & 255) / 255.0f;
	pass->value = (float)((c[1] >> 24) & 255) / 255.0f;
	pass->colour[0] = (float)((c[2] >> 16) & 255) / 255.0f;
	pass->colour[1] = (float)((c[2] >> 8) & 255) / 255.0f;
	pass->colour[2] = (float)(c[2] & 255) / 255.0f;
	/* (the alpha: its maps - base, mult, add - through their transforms,
	the vertex constants' scale u v and offset u v; a map missing the
	template's default: base and add black, mult white) */
	if (h2_cache_block(cache, definitions + 4, 0xC, &bitmap_count, &bitmaps))
	{
		for (m = 0; m < 3 && m < bitmap_count; m++)
		{
			long b = h2_cache_tag_from_handle(cache, h2_cache_read32(cache, bitmaps + 0xC * m));

			maps[m] = b != NONE ? bitmap_rgba(cache, b, &w[m], &h[m]) : NULL;
		}
	}
	if (h2_cache_block(cache, definitions + 0x14, 16, &vertex_count, &vertices))
	{
		for (m = 0; m < 3 && m < vertex_count; m++)
		{
			for (k = 0; k < 4; k++)
			{
				unsigned long bits = h2_cache_read32(cache, vertices + 16 * m + 4 * k);

				memcpy(&scale[m][k], &bits, 4);
			}
		}
	}
	for (y = 0; y < 64; y++)
	{
		for (x = 0; x < 64; x++)
		{
			float b[3], r1;

			for (m = 0; m < 3; m++)
			{
				if (maps[m] && w[m] && h[m])
				{
					float u = (x + 0.5f) / 64.0f * scale[m][0] + scale[m][2], v = (y + 0.5f) / 64.0f * scale[m][1] + scale[m][3];
					unsigned long tx = (unsigned long)((u - (float)floor(u)) * w[m]) % w[m], ty = (unsigned long)((v - (float)floor(v)) * h[m]) % h[m];

					b[m] = maps[m][4 * (ty * w[m] + tx) + 2] / 255.0f;
				}
				else
					b[m] = m == 1 ? 1.0f : 0.0f;
			}
			r1 = b[0] * b[1] + b[2];
			r1 = r1 > 1.0f ? 1.0f : r1;
			r1 = 2.0f * r1 * ((float)((c[3] >> 24) & 255) / 255.0f);
			sum += r1 > 1.0f ? 1.0f : r1;
		}
	}
	for (m = 0; m < 3; m++)
		free(maps[m]);
	pass->alpha = (float)(sum / (64.0 * 64.0));
	return TRUE;
}

H2_ALIGN void h2_hologram_pass_texel(
	struct h2_hologram_pass const *pass,
	float const x[3],
	float out[3])
{
	int k;

	for (k = 0; k < 3; k++)
	{
		float r = (1.0f - pass->invert) * x[k] + pass->invert * (1.0f - x[k]);

		r = 2.0f * r * pass->value;
		r = r > 1.0f ? 1.0f : r;
		r = 2.0f * r * pass->colour[k];
		out[k] = r > 1.0f ? 1.0f : r < 0.0f ? 0.0f : r;
	}
}

H2_ALIGN static void shader_tint(struct h2_cache *cache, long h2_shader, float tint[3])
{
	struct h2_cache_tag t;
	long count, pixel_count, i;
	unsigned long definitions, pixels, best = 0, best_sum = 0;

	tint[0] = tint[1] = tint[2] = 1.0f;
	if (!h2_cache_tag_get(cache, h2_shader, &t) || !h2_cache_block(cache, t.address + 0x20, 0x7C, &count, &definitions) || count < 1 ||
		!h2_cache_block(cache, definitions + 0xC, 4, &pixel_count, &pixels))
	{
		return;
	}
	for (i = 0; i < pixel_count; i++)
	{
		unsigned long colour = h2_cache_read32(cache, pixels + 4 * i);
		unsigned long sum = ((colour >> 16) & 255) + ((colour >> 8) & 255) + (colour & 255);

		if (sum > best_sum)
			best = colour, best_sum = sum;
	}
	if (best_sum)
	{
		float r = ((best >> 16) & 255) / 255.0f, g = ((best >> 8) & 255) / 255.0f, b = (best & 255) / 255.0f;
		float top = r > g ? (r > b ? r : b) : (g > b ? g : b);

		tint[0] = 0.3f + 0.7f * r / top;
		tint[1] = 0.3f + 0.7f * g / top;
		tint[2] = 0.3f + 0.7f * b / top;
	}
}

H2_ALIGN static short shader_make(struct h2_biped_host const *host, struct made_biped *biped, long h2_shader)
{
	struct h2_cache *cache = host->cache;
	struct h2_cache_tag t;
	struct made_shader *s;
	long i, count;
	unsigned long properties, definitions;
	long diffuse = NONE, change_colour = NONE, illum = NONE, template_tag, bump_tag = NONE;
	char const *template_name;

	for (i = 0; i < biped->shader_count; i++)
	{
		if (biped->shaders[i].h2_shader == h2_shader)
			return (short)i;
	}
	if (biped->shader_count >= MAXIMUM_MATERIALS || h2_shader == NONE || !h2_cache_tag_get(cache, h2_shader, &t))
		return NONE;
	s = &biped->shaders[biped->shader_count];
	memset(s, 0, sizeof(*s));
	s->level_shader = NONE;
	s->h2_shader = h2_shader;
	s->base_bitmap = s->multipurpose_bitmap = NONE;
	s->detail_bitmap = s->cube_bitmap = s->bump_bitmap = NONE;
	snprintf(s->name, sizeof(s->name), "%s", short_name(h2_cache_tag_name(cache, h2_shader)));
	template_tag = h2_cache_reference(cache, t.address);
	template_name = template_tag != NONE ? h2_cache_tag_name(cache, template_tag) : "";
	/* (an emblem - opaque\\emblem_overlay_simple, mc_emblem and elite_emblem:
	on a multiplayer level an alpha blended overlay of a stand-in that shows
	nothing, the player's own emblem bound as it draws (h2_emblem.h); Halo 2
	draws none in a campaign - function_bc970's emblem_active -: not made) */
	/* (lit alpha blended hair - transparent\lit\transparent_lit_alpha_blend_*,
	Tartarus' white mane: drawn here, its alpha_blend_map the base map cut
	out by its alpha, its change colour map's mask tinting it) */
	s->alpha_tested = strstr(template_name, "\\transparent\\lit\\") && strstr(template_name, "alpha_blend") &&
		!getenv("HALO_H2_NO_LIT_HAIR");
	if (strstr(template_name, "emblem") && !strstr(h2_cache_scenario_path(cache), "\\solo\\") && !getenv("HALO_H2_NO_EMBLEMS"))
	{
		s->transparent = TRUE;
		s->alpha_blended = TRUE;
		s->emblem = TRUE;
		s->base_bitmap = emblem_stand_in(host);
		s->skipped = s->base_bitmap == NONE;
		return (short)biped->shader_count++;
	}
	if (strstr(template_name, "emblem") || (strstr(template_name, "\\transparent\\") && !s->alpha_tested &&
		getenv("HALO_H2_BIPED_NO_TRANSPARENT")))
	{
		s->skipped = TRUE;
		return (short)biped->shader_count++;
	}
	s->transparent = (strstr(template_name, "\\transparent\\") != NULL && !s->alpha_tested) ||
		strstr(template_name, "\\hologram\\") != NULL;
	s->alpha_blended = s->transparent && strstr(template_name, "_alpha_") != NULL;
	/* its diffuse map (the runtime properties' first reference, 0x0C) and
	its change colour map (a postprocess bitmap - 0x20, 0x7C each, bitmaps
	at 4, 0xC each - named as one) */
	if (h2_cache_block(cache, t.address + 0x0C, 0x50, &count, &properties) && count > 0)
		diffuse = h2_cache_reference(cache, properties);
	/* (one_alpha_env - a Jackal's eyes: its alpha_blend_map, not the specular
	map the first property holds, a white noise over the eye) */
	if (diffuse != NONE && strstr(template_name, "one_alpha_env") && strstr(h2_cache_tag_name(cache, diffuse), "specular"))
		diffuse = NONE;
	if (h2_cache_block(cache, t.address + 0x20, 0x7C, &count, &definitions) && count > 0)
	{
		long bitmap_count;
		unsigned long bitmaps;

		if (h2_cache_block(cache, definitions + 4, 0xC, &bitmap_count, &bitmaps))
		{
			for (i = 0; i < bitmap_count; i++)
			{
				long bitmap = h2_cache_tag_from_handle(cache, h2_cache_read32(cache, bitmaps + 0xC * i));
				char const *name = bitmap != NONE ? h2_cache_tag_name(cache, bitmap) : "";
				size_t length = strlen(name);

				if (bump_tag == NONE && strstr(name, "_bump"))
					bump_tag = bitmap;

				if (bitmap == NONE)
					continue;
				if (illum == NONE && strstr(template_name, "illum") && !strstr(name, "default_") &&
					(strstr(name, "illum") || strstr(name, "glow") || (length > 6 && !strcmp(name + length - 6, "_light"))))
				{
					illum = bitmap;
				}
				if (change_colour == NONE && (strstr(name, "change_color") || strstr(name, "chg_color") ||
					strstr(name, "color_change") || (length > 3 && !strcmp(name + length - 3, "_cc"))))
				{
					change_colour = bitmap;
				}
				if (diffuse == NONE && !strstr(name, "default_") && !strstr(name, "_bump") && !strstr(name, "detail") &&
					!strstr(name, "reflection") && !strstr(name, "rasterizer") && !strstr(name, "specular") &&
					!(s->alpha_tested && bitmap == change_colour))
				{
					diffuse = bitmap;
				}
			}
		}
	}
	/* (a plasma one: its mask - a map named so -, else its first,
	tinted by its brightest pixel constant (the others - Cortana, the
	holograms, glows - their map's own colour, added) (the postprocess
	definition's at 0xC, 4 bytes each, a r g b): a Jackal's shield its
	plasma's blue) */
	if (s->transparent && !s->alpha_blended && (strstr(template_name, "\\plasma") || strstr(template_name, "one_add_illum")) &&
		h2_cache_block(cache, t.address + 0x20, 0x7C, &count, &definitions) && count > 0)
	{
		long bitmap_count, mask = NONE;
		unsigned long bitmaps;
		float tint[3];

		if (h2_cache_block(cache, definitions + 4, 0xC, &bitmap_count, &bitmaps))
		{
			for (i = 0; i < bitmap_count && mask == NONE; i++)
			{
				long bitmap = h2_cache_tag_from_handle(cache, h2_cache_read32(cache, bitmaps + 0xC * i));

				if (bitmap != NONE && strstr(h2_cache_tag_name(cache, bitmap), "mask"))
					mask = bitmap;
			}
		}
		shader_tint(cache, h2_shader, tint);
		if (mask != NONE)
			diffuse = mask;
		/* (plasma_mask_offset - a Jackal's shield, its noises through its
		mask -: the level's shader of it, whose pass the PC draws as Halo 2's;
		the Vita's has none, and keeps this tinted mask) */
		if (strstr(template_name, "\\plasma_mask_offset") && host->level_shader)
			s->level_shader = host->level_shader(host->context, h2_shader);
		if (strstr(template_name, "\\plasma_shield"))
		{
			s->plasma = TRUE;
			memcpy(s->plasma_colour, tint, sizeof(tint));
			diffuse = NONE;
			/* (the pass itself for the desktop: its colours and the functions
			of the shield_hit input that drive them) */
			s->shield_pass_read = h2_plasma_shield_read(cache, h2_shader, template_name, &s->shield_pass);
			if (s->shield_pass_read && getenv("HALO_H2_DEBUG"))
			{
				struct h2_plasma_shield_pass const *p = &s->shield_pass;

				platform_log("h2 bipeds: %s: plasma_shield pass %s: mode %d, wide %06lx..%06lx%s sharp %06lx..%06lx%s center %06lx..%06lx%s "
					"color %06lx..%06lx%s glancing %06lx..%06lx%s, scales %.0f %.0f, noise 0 x %s%.2f y %s%.2f z %s%.2f, noise 1 x %s%.2f y %s%.2f z %s%.2f, "
					"offset %s%.3f, input '%s'", biped->name, s->name, (int)p->mode,
					p->colour_low[0], p->colour_high[0], p->colour_by_input[0] ? "*" : "", p->colour_low[1], p->colour_high[1], p->colour_by_input[1] ? "*" : "",
					p->colour_low[2], p->colour_high[2], p->colour_by_input[2] ? "*" : "", p->colour_low[3], p->colour_high[3], p->colour_by_input[3] ? "*" : "",
					p->colour_low[4], p->colour_high[4], p->colour_by_input[4] ? "*" : "", p->scale[0], p->scale[1],
					p->translation_by_input[0][0] ? "*" : "T", p->translation_by_input[0][0] ? p->translation_high[0][0] : p->translation_period[0][0],
					p->translation_by_input[0][1] ? "*" : "T", p->translation_by_input[0][1] ? p->translation_high[0][1] : p->translation_period[0][1],
					p->translation_by_input[0][2] ? "*" : "T", p->translation_by_input[0][2] ? p->translation_high[0][2] : p->translation_period[0][2],
					p->translation_by_input[1][0] ? "*" : "T", p->translation_by_input[1][0] ? p->translation_high[1][0] : p->translation_period[1][0],
					p->translation_by_input[1][1] ? "*" : "T", p->translation_by_input[1][1] ? p->translation_high[1][1] : p->translation_period[1][1],
					p->translation_by_input[1][2] ? "*" : "T", p->translation_by_input[1][2] ? p->translation_high[1][2] : p->translation_period[1][2],
					p->offset_by_input ? "*" : "", p->offset_high, p->input_name);
			}
		}
		if (diffuse != NONE)
			s->base_bitmap = tinted_make(host, biped->name, s->name, diffuse, tint);
		if (getenv("HALO_H2_DEBUG"))
			platform_log("h2 bipeds: %s: shader %s (%s): added, %s tinted %.2f %.2f %.2f", biped->name, s->name, template_name,
				diffuse != NONE ? h2_cache_tag_name(cache, diffuse) : "(none)", tint[0], tint[1], tint[2]);
	}
	else if (diffuse != NONE)
		s->base_bitmap = host->bitmap_for(host->context, diffuse);
	/* (opaque ..._alpha_test: tex_bump_env_alpha_test, a Jackal's crest and
	teeth over his head's map: cut out by its bump map's alpha - the head's
	base alpha is its specular mask -, drawn whole they were solid cards) */
	if (diffuse != NONE && !s->transparent && strstr(template_name, "\\opaque\\") && strstr(template_name, "alpha_test") &&
		bump_tag != NONE && host->alpha_base_for && !getenv("HALO_H2_OBJECT_CUTOUTS_OLD"))
	{
		long cut = host->alpha_base_for(host->context, diffuse, bump_tag);

		if (cut != NONE)
		{
			s->base_bitmap = cut;
			s->alpha_tested = TRUE;
		}
	}
	/* (Cortana - transparent\\cortana, its pass
	cortana_default_transparent_one_sided: alpha_blend_map (the third map,
	cortana_alpha) times alpha_blend_color (the second pixel constant) over
	what is behind by its alpha times alpha_blend_opacity (the third's
	alpha, a half) - premultiplied, an alpha blend - and the trace plasma
	(trace_a, trace_b by color_wide, _medium, _sharp, the sixth, seventh
	and ninth constants) over it, added, masked by the alpha map: here its
	two baked together - the map, tinted, its alpha halved; where the map
	glows, the traces' sharp colour added. Added whole she was a saturated
	blue figure; Halo 2's is pale lavender, half see-through.
	HALO_H2_CORTANA_OLD: as before) */
	if (s->transparent && strstr(template_name, "\\transparent\\cortana") && !getenv("HALO_H2_CORTANA_OLD") &&
		h2_cache_block(cache, t.address + 0x20, 0x7C, &count, &definitions) && count > 0)
	{
		long bitmap_count, constant_count;
		unsigned long bitmaps, constants;
		long map = NONE;
		unsigned long colour = 0xFFFFFFUL, opacity = 0x80, sharp = 0x6C74D0UL;

		if (h2_cache_block(cache, definitions + 4, 0xC, &bitmap_count, &bitmaps) && bitmap_count > 2)
			map = h2_cache_tag_from_handle(cache, h2_cache_read32(cache, bitmaps + 0xC * 2));
		if (h2_cache_block(cache, definitions + 0xC, 4, &constant_count, &constants))
		{
			if (constant_count > 2)
			{
				colour = h2_cache_read32(cache, constants + 4) & 0xFFFFFFUL;
				opacity = h2_cache_read32(cache, constants + 8) >> 24;
			}
			if (constant_count > 8)
				sharp = h2_cache_read32(cache, constants + 32) & 0xFFFFFFUL;
		}
		if (map != NONE)
		{
			long made = cortana_make(host, biped->name, s->name, map, colour, opacity, sharp);

			if (made != NONE)
			{
				s->base_bitmap = made;
				s->alpha_blended = TRUE;
				diffuse = map;
			}
		}
	}
	s->h2_diffuse = diffuse;
	if (s->transparent)
	{
		if (s->base_bitmap == NONE && !s->plasma)
			s->skipped = TRUE;
		return (short)biped->shader_count++;
	}
	/* (Halo 2's illum templates - opaque\\illum, illum_clamped...: the Elite's
	inset lights, the honor guards' orange ones - their map (the base) all
	glowing in their first pixel constant's colour) */
	if (illum == NONE && diffuse != NONE && strstr(template_name, "\\opaque\\illum") && !getenv("HALO_H2_BIPED_NO_ILLUM"))
	{
		long constant_count;
		unsigned long constants;
		int c;

		illum = diffuse;
		s->illum = TRUE;
		s->illum_full = TRUE;
		s->illum_period = 0.0f;
		for (c = 0; c < 3; c++)
			s->illum_lower[c] = s->illum_upper[c] = 1.0f;
		if (h2_cache_block(cache, t.address + 0x20, 0x7C, &count, &definitions) && count > 0 &&
			h2_cache_block(cache, definitions + 0xC, 4, &constant_count, &constants) && constant_count > 0)
		{
			unsigned long colour = h2_cache_read32(cache, constants) & 0xFFFFFFUL;

			for (c = 0; c < 3; c++)
				s->illum_lower[c] = s->illum_upper[c] = ((colour >> (16 - 8 * c)) & 255) / 255.0f;
		}
	}
	else if (illum != NONE && !getenv("HALO_H2_BIPED_NO_ILLUM"))
		s->illum = illum_colours(cache, t.address, illum, s);
	if (change_colour != NONE && strstr(template_name, "change_color"))
	{
		s->multipurpose_bitmap = multipurpose_make(host, s->name, change_colour, diffuse, s->illum ? illum : NONE, FALSE);
		s->change_colour = s->multipurpose_bitmap != NONE;
	}
	/* (a glow without a change colour map - the inset lights, a Brute's
	gear: its multipurpose map the glow alone) */
	if (s->illum && s->multipurpose_bitmap == NONE && diffuse != NONE)
		s->multipurpose_bitmap = multipurpose_make(host, s->name, NONE, diffuse, s->illum_full ? NONE : illum, s->illum_full);
	if (s->base_bitmap == NONE)
		s->skipped = TRUE;
	/* (its detail map and environment cube - the armour's metal - as
	h2_materials.c reads its template; HALO_H2_MATERIALS_OLD: neither) */
	s->detail_bitmap = s->cube_bitmap = s->bump_bitmap = NONE;
	if (!s->skipped && !getenv("HALO_H2_MATERIALS_OLD") && h2_material_read(cache, t.address, template_name, &s->material))
	{
		if (s->material.detail_tag != NONE && !getenv("HALO_H2_NO_DETAIL"))
			s->detail_bitmap = host->bitmap_for(host->context, s->material.detail_tag);
		if (host->bump_for)
			s->bump_bitmap = host->bump_for(host->context, s->material.bump_tag);
		/* (an environment mapped one without a change colour map - the
		Spartan's visor, tex_bump_env: gold glass, its cube's reflection
		tinted, its base map near black - its multipurpose map its specular
		mask alone; without it no cube, the visor drawn black) */
		if (s->material.has_env && s->multipurpose_bitmap == NONE && diffuse != NONE && !getenv("HALO_H2_NO_ENV") &&
			!getenv("HALO_H2_NO_VISOR_ENV"))
		{
			s->multipurpose_bitmap = multipurpose_make(host, s->name, NONE, diffuse, s->illum && !s->illum_full ? illum : NONE, s->illum_full);
		}
		if (s->material.has_env && s->multipurpose_bitmap != NONE && !getenv("HALO_H2_NO_ENV"))
			s->cube_bitmap = host->bitmap_for(host->context, s->material.cube_tag);
	}
	return (short)biped->shader_count++;
}

H2_ALIGN static int marker_compare(void const *a, void const *b)
{
	char const *x = ((struct made_marker const *)a)->name, *y = ((struct made_marker const *)b)->name;

	/* (models.c's model_find_marker: a binary search, _stricmp) */
	for (;; x++, y++)
	{
		int cx = *x >= 'A' && *x <= 'Z' ? *x - 'A' + 'a' : *x, cy = *y >= 'A' && *y <= 'Z' ? *y - 'A' + 'a' : *y;

		if (cx != cy || !cx)
			return cx - cy;
	}
}

/* the nodes' first children and next siblings from their parents (in the
nodes' order) */
H2_ALIGN static void node_links(short const *parents, short count, short *children, short *siblings)
{
	short n, m;

	for (n = 0; n < count; n++)
		children[n] = siblings[n] = NONE;
	for (n = 0; n < count; n++)
	{
		for (m = n + 1; m < count && parents[n] != NONE; m++)
		{
			if (parents[m] == parents[n])
			{
				siblings[n] = m;
				break;
			}
		}
		for (m = 0; m < count; m++)
		{
			if (parents[m] == n)
			{
				children[n] = m;
				break;
			}
		}
	}
}

/* a skeleton of more nodes than this engine's renderer skins at once
(RENDERER_NODES) made one it does: leaf nodes of the face (eyes, brows,
lips, throat: the Elite's, animated in cinematics, not multiplayer) and
physics helpers left out, their vertices their parents' (stand_ins.md) */
H2_ALIGN static void node_collapse(struct made_biped *biped)
{
	short n, kept = 0, remap[MAXIMUM_NODES];
	short count = biped->node_count;

	for (n = 0; n < count; n++)
		remap[n] = n;
	if (count > RENDERER_NODES)
	{
		for (n = 0; n < count; n++)
		{
			char const *name = biped->node_names[n];
			boolean leaf = biped->node_child[n] == NONE;

			remap[n] = (leaf && n > 0 && (!strncmp(name, "f_", 2) || !strncmp(name, "eye_", 4) || !strcmp(name, "physics_control"))) ?
				NONE : kept++;
		}
		/* (room left: the face's leaves the cutscenes' graphs move most -
		lips, mouth corners, upper eyelids, inner brows, eyes - kept back,
		a pair at a time while both fit: a Marine's, Miranda's, Hood's lips
		move as they speak; HALO_H2_BIPED_NO_FACE leaves them out) */
		if (kept < RENDERER_NODES && !getenv("HALO_H2_BIPED_NO_FACE"))
		{
			static char const *const face[][2] =
			{
				{ "f_l_lip_upper", "f_r_lip_upper" }, { "f_l_lip_lower", "f_r_lip_lower" }, { "f_l_mouth", "f_r_mouth" },
				{ "f_l_eyelid_upper", "f_r_eyelid_upper" }, { "f_l_eyelid_up_in", "f_r_eyelid_up_in" },
				{ "f_l_eyelid_up_out", "f_r_eyelid_up_out" }, { "f_l_brow_inner", "f_r_brow_inner" }, { "eye_left", "eye_right" },
			};
			short f, back = 0;

			for (f = 0; f < (short)(sizeof(face) / sizeof(face[0])); f++)
			{
				short pair[2] = { NONE, NONE }, k;

				for (n = 0; n < count; n++)
				{
					for (k = 0; k < 2; k++)
					{
						if (remap[n] == NONE && biped->node_child[n] == NONE && !strcmp(biped->node_names[n], face[f][k]))
							pair[k] = n;
					}
				}
				if (pair[0] == NONE || pair[1] == NONE || kept + back + 2 > RENDERER_NODES)
					continue;
				remap[pair[0]] = remap[pair[1]] = 0;    /* (kept: numbered below) */
				back += 2;
			}
			/* (and the face's other leaves, while there is room - the PC's
			64: Cortana's whole face but one -, a left one with its right
			together: a Prophet's 68 left its right eyelids out and kept the
			left ones, one eye blinking) */
			for (n = 0; n < count && kept + back < RENDERER_NODES; n++)
			{
				if (remap[n] == NONE && biped->node_child[n] == NONE &&
					(!strncmp(biped->node_names[n], "f_", 2) || !strncmp(biped->node_names[n], "eye_", 4)))
				{
					char mirror[32];
					char const *name = biped->node_names[n];
					short m = NONE;

					/* (its other side's: f_l_ <-> f_r_) */
					if (!strncmp(name, "f_l_", 4) || !strncmp(name, "f_r_", 4))
					{
						snprintf(mirror, sizeof(mirror), "f_%c_%s", name[2] == 'l' ? 'r' : 'l', name + 4);
						for (m = 0; m < count && strcmp(biped->node_names[m], mirror); m++)
							;
						if (m == count || remap[m] != NONE || biped->node_child[m] != NONE)
							m = NONE;
					}
					if (m != NONE)
					{
						if (kept + back + 2 > RENDERER_NODES)
							continue;
						remap[m] = 0;
						back++;
					}
					remap[n] = 0;
					back++;
				}
			}
			if (back)
			{
				for (n = 0, kept = 0; n < count; n++)
				{
					if (remap[n] != NONE)
						remap[n] = kept++;
				}
			}
		}
		/* (still too many - a Prophet's 68: its wattles, the head's and
		jaw's other leaves, too) */
		if (kept > RENDERER_NODES)
		{
			for (n = 0, kept = 0; n < count; n++)
			{
				short p = biped->node_parent[n];
				boolean of_head = FALSE;

				while (p != NONE && !of_head)
				{
					of_head = !strcmp(biped->node_names[p], "head");
					p = biped->node_parent[p];
				}
				if (remap[n] != NONE && biped->node_child[n] == NONE && n > 0 && of_head)
					remap[n] = NONE;
				else if (remap[n] != NONE)
					remap[n] = kept++;
			}
		}
	}
	/* (still too many - the Gravemind's tentacles, flood Regret's: the
	deepest kept leaf left out, one at a time, until the renderer skins
	them all; a leaf's vertices its parent's) */
	if (kept > RENDERER_NODES)
	{
		short depth[MAXIMUM_NODES];
		boolean out[MAXIMUM_NODES];

		for (n = 0; n < count; n++)
		{
			short p = biped->node_parent[n];

			out[n] = remap[n] == NONE;
			for (depth[n] = 0; p != NONE && depth[n] < MAXIMUM_NODES; p = biped->node_parent[p])
				depth[n]++;
		}
		while (kept > RENDERER_NODES)
		{
			short best = NONE, c;

			for (n = 1; n < count; n++)
			{
				boolean leaf = TRUE;

				if (out[n])
					continue;
				for (c = 0; c < count && leaf; c++)
					leaf = out[c] || biped->node_parent[c] != n;
				if (leaf && (best == NONE || depth[n] >= depth[best]))
					best = n;
			}
			if (best == NONE)
				break;
			out[best] = TRUE;
			kept--;
		}
		for (n = 0, kept = 0; n < count; n++)
			remap[n] = out[n] ? NONE : kept++;
	}
	if (kept == 0 || kept == count || kept > RENDERER_NODES)
	{
		for (n = 0; n < count; n++)
			biped->node_remap[n] = n;
		return;
	}
	/* (each left out: its nearest kept ancestor) */
	if (getenv("HALO_H2_NODE_LOG"))
	{
		for (n = 0; n < count; n++)
		{
			if (remap[n] == NONE)
				platform_log("h2 bipeds: %s: node %d %s (parent %s) left out", biped->name, (int)n, biped->node_names[n],
					biped->node_parent[n] != NONE ? biped->node_names[biped->node_parent[n]] : "none");
		}
	}
	for (n = 0; n < count; n++)
	{
		short a = n;

		while (a != NONE && remap[a] == NONE)
			a = biped->node_parent[a];
		biped->node_remap[n] = a != NONE ? remap[a] : 0;
	}
	for (n = 0; n < count; n++)
	{
		short to = remap[n];

		if (to == NONE)
			continue;
		memmove(biped->node_names[to], biped->node_names[n], 32);
		biped->node_parent[to] = biped->node_parent[n] != NONE ? biped->node_remap[biped->node_parent[n]] : NONE;
		biped->node_default[to] = biped->node_default[n];
		memmove(biped->node_inverse[to], biped->node_inverse[n], sizeof(biped->node_inverse[n]));
		biped->node_distance[to] = biped->node_distance[n];
	}
	biped->node_count = kept;
	node_links(biped->node_parent, biped->node_count, biped->node_child, biped->node_sibling);
	platform_log("h2 bipeds: %s: %d nodes of Halo 2's %d (the face's and physics' leaves left out)", biped->name,
		(int)kept, (int)count);
}

/* (the model's variant "default" - else its first: hlmt 0x50, 0x38 each:
its regions at 0x14, 0x14 each, their permutations at 8, 0x20 each) the
permutation a region shows: its name (an empty one: none), FALSE if the
variant does not name the region */
H2_ALIGN static boolean variant_permutation(struct h2_cache *cache, long hlmt_tag, char const *region, char *permutation,
	long size)
{
	struct h2_cache_tag t;
	long variant_count, region_count, i, chosen = 0;
	unsigned long variants, regions;

	if (hlmt_tag == NONE || !h2_cache_tag_get(cache, hlmt_tag, &t) ||
		!h2_cache_block(cache, t.address + 0x50, 0x38, &variant_count, &variants) || variant_count < 1)
	{
		return FALSE;
	}
	/* (none named "default": the first a default's - the Marine's first,
	"johnson", is Johnson's) */
	chosen = NONE;
	for (i = 0; i < variant_count; i++)
	{
		char name[64];

		name_of(cache, h2_cache_read32(cache, variants + 0x38 * i), name, sizeof(name));
		if (!strcmp(name, "default"))
			chosen = i;
	}
	for (i = 0; i < variant_count && chosen == NONE; i++)
	{
		char name[64];

		name_of(cache, h2_cache_read32(cache, variants + 0x38 * i), name, sizeof(name));
		if (!strncmp(name, "default", 7))
			chosen = i;
	}
	if (chosen == NONE)
		chosen = 0;
	if (!h2_cache_block(cache, variants + 0x38 * chosen + 0x14, 0x14, &region_count, &regions))
		return FALSE;
	for (i = 0; i < region_count; i++)
	{
		char name[64];
		long permutation_count;
		unsigned long permutations;

		name_of(cache, h2_cache_read32(cache, regions + 0x14 * i), name, sizeof(name));
		if (strcmp(name, region))
			continue;
		permutation[0] = 0;
		if (h2_cache_block(cache, regions + 0x14 * i + 8, 0x20, &permutation_count, &permutations) && permutation_count > 0)
			name_of(cache, h2_cache_read32(cache, permutations), permutation, size);
		return TRUE;
	}
	return FALSE;
}

/* some variant of the model (hlmt 0x50, 0x38 each; regions at 0x14, 0x14
each: name, permutations at 8, 0x20 each: name) shows nothing of a region:
an empty permutation, or none the region has */
H2_ALIGN static boolean variants_hide(struct h2_cache *cache, long hlmt_tag, struct made_region const *region)
{
	struct h2_cache_tag t;
	long variant_count, i;
	unsigned long variants;

	if (hlmt_tag == NONE || !h2_cache_tag_get(cache, hlmt_tag, &t) ||
		!h2_cache_block(cache, t.address + 0x50, 0x38, &variant_count, &variants))
	{
		return FALSE;
	}
	for (i = 0; i < variant_count; i++)
	{
		long region_count, r;
		unsigned long regions;

		if (!h2_cache_block(cache, variants + 0x38 * i + 0x14, 0x14, &region_count, &regions))
			continue;
		for (r = 0; r < region_count; r++)
		{
			char name[64];
			long permutation_count, k, j;
			unsigned long permutations;

			name_of(cache, h2_cache_read32(cache, regions + 0x14 * r), name, sizeof(name));
			if (strcmp(name, region->name) || !h2_cache_block(cache, regions + 0x14 * r + 8, 0x20, &permutation_count, &permutations))
				continue;
			for (k = 0; k < permutation_count; k++)
			{
				name_of(cache, h2_cache_read32(cache, permutations + 0x20 * k), name, sizeof(name));
				if (!name[0])
					return TRUE;
				for (j = 0; j < region->permutation_count && strcmp(region->permutations[j].name, name); j++)
					;
				if (j == region->permutation_count)
					return TRUE;
			}
		}
	}
	return FALSE;
}

/* a render model read and made (its nodes, markers, regions, shaders and
geometries) */
/* (the Vita tier) a model's vertices and strips put in the host's spill
file once finished - the player models', 4-6 MB on a campaign level, else
held through the build's peak as the level's tag data is made - and read
back into the tag data as written (model_write) */
H2_ALIGN static void vertices_spill(struct h2_biped_host const *host, struct made_biped *biped)
{
	long g, p;

	if (!host->spill_put || !host->spill_get)
		return;
	for (g = 0; g < biped->geometry_count; g++)
	{
		for (p = 0; p < biped->geometries[g].part_count; p++)
		{
			struct made_part *part = &biped->geometries[g].parts[p];
			unsigned long vertex_bytes = (unsigned long)part->vertex_count * VERTEX_SIZE, strip;

			if (part->spilled || !part->vertices || !part->strip)
				continue;
			if (!host->spill_put(host->context, part->vertices, vertex_bytes, &part->spill) ||
				!host->spill_put(host->context, part->strip, (unsigned long)part->strip_count * 2, &strip))
			{
				return;
			}
			free(part->vertices);
			free(part->strip);
			part->vertices = NULL;
			part->strip = NULL;
			part->spilled = TRUE;
		}
	}
}

/* a render model's nodes as the made biped has them (collapsed to the
renderer's): model_make's first step, and the graphs made ahead's
(graphs_ahead) - the one reading for both; FALSE if not a model of nodes */
H2_ALIGN static boolean model_nodes_read(struct h2_cache *cache, struct made_biped *biped, long model_tag, struct h2_cache_tag *model)
{
	struct h2_cache_tag t;
	long count, i;
	unsigned long nodes;

	if (!h2_cache_tag_get(cache, model_tag, &t) || t.group_tag != 'mode' ||
		!h2_cache_block(cache, t.address + H2_MODE_NODES, H2_MODE_NODE_SIZE, &count, &nodes) || count < 1 || count > MAXIMUM_NODES)
	{
		return FALSE;
	}
	*model = t;
	biped->node_count = (short)count;
	for (i = 0; i < count; i++)
	{
		unsigned char const *n = h2_cache_at(cache, nodes + H2_MODE_NODE_SIZE * i, H2_MODE_NODE_SIZE);
		struct h2_animation_pose *d = &biped->node_default[i];

		if (!n)
			return FALSE;
		name_of(cache, read32(n), biped->node_names[i], 32);
		biped->node_parent[i] = read16(n + 4);
		biped->node_child[i] = read16(n + 6);
		biped->node_sibling[i] = read16(n + 8);
		memcpy(d->translation, n + 0x0C, 12);
		memcpy(d->rotation, n + 0x18, 16);
		/* (this engine's convention: conjugated) */
		d->rotation[0] = -d->rotation[0];
		d->rotation[1] = -d->rotation[1];
		d->rotation[2] = -d->rotation[2];
		d->scale = 1.0f;
		memcpy(biped->node_inverse[i], n + 0x28, 52);
		biped->node_distance[i] = readf(n + 0x5C);
		memcpy(biped->h2_node_names[i], biped->node_names[i], 32);
	}
	biped->h2_node_count = biped->node_count;
	node_links(biped->node_parent, biped->node_count, biped->node_child, biped->node_sibling);
	node_collapse(biped);
	return TRUE;
}

H2_ALIGN static boolean model_make(struct h2_biped_host const *host, struct made_biped *biped, long model_tag)
{
	struct h2_cache *cache = host->cache;
	struct h2_cache_tag t;
	long count, i, j;
	unsigned long regions, groups, materials;
	short section_geometry[MAXIMUM_SECTIONS];

	/* nodes */
	if (!model_nodes_read(cache, biped, model_tag, &t))
		return FALSE;
	/* shaders, by material */
	for (i = 0; i < MAXIMUM_MATERIALS; i++)
		biped->material_shader[i] = NONE;
	if (h2_cache_block(cache, t.address + H2_MODE_MATERIALS, 0x20, &count, &materials))
	{
		for (i = 0; i < count && i < MAXIMUM_MATERIALS; i++)
		{
			long shader = h2_cache_reference(cache, materials + 0x20 * i + 8);

			if (shader == NONE)
				shader = h2_cache_reference(cache, materials + 0x20 * i);
			biped->material_shader[i] = shader_make(host, biped, shader);
		}
	}
	/* regions, their permutations and their sections' geometries */
	for (i = 0; i < MAXIMUM_SECTIONS; i++)
		section_geometry[i] = NONE;
	biped->geometries = calloc(MAXIMUM_SECTIONS, sizeof(*biped->geometries));
	if (!biped->geometries || !h2_cache_block(cache, t.address + H2_MODE_REGIONS, 0x10, &count, &regions))
		return FALSE;
	for (i = 0; i < count && biped->region_count < H2_BIPED_MAXIMUM_REGIONS; i++)
	{
		struct made_region *region = &biped->regions[biped->region_count];
		long permutation_count;
		unsigned long permutations;

		memset(region, 0, sizeof(*region));
		name_of(cache, h2_cache_read32(cache, regions + 0x10 * i), region->name, sizeof(region->name));
		if (!h2_cache_block(cache, regions + 0x10 * i + 8, 0x10, &permutation_count, &permutations))
			continue;
		for (j = 0; j < permutation_count && region->permutation_count < H2_BIPED_MAXIMUM_PERMUTATIONS; j++)
		{
			unsigned char const *p = h2_cache_at(cache, permutations + 0x10 * j, 0x10);
			int level;

			if (!p)
				continue;
			name_of(cache, read32(p), region->permutations[region->permutation_count].name, 32);
			/* (this engine's five levels: Halo 2's L1..L5; L6 is
			cinematics') */
			for (level = 0; level < H2_BIPED_DETAIL_LEVELS; level++)
			{
				short section = read16(p + 4 + 2 * level);
				short geometry = NONE;

				if (section >= 0 && section < MAXIMUM_SECTIONS)
				{
					if (section_geometry[section] == NONE &&
						geometry_make(cache, biped, t.address, section, &biped->geometries[biped->geometry_count]))
					{
						section_geometry[section] = (short)biped->geometry_count++;
					}
					geometry = section_geometry[section];
				}
				region->permutations[region->permutation_count].geometry[level] = geometry;
			}
			/* (a level missing: the next finer one's, else coarser) */
			for (level = H2_BIPED_DETAIL_LEVELS - 2; level >= 0; level--)
			{
				if (region->permutations[region->permutation_count].geometry[level] == NONE)
					region->permutations[region->permutation_count].geometry[level] =
						region->permutations[region->permutation_count].geometry[level + 1];
			}
			for (level = 1; level < H2_BIPED_DETAIL_LEVELS; level++)
			{
				if (region->permutations[region->permutation_count].geometry[level] == NONE)
					region->permutations[region->permutation_count].geometry[level] =
						region->permutations[region->permutation_count].geometry[level - 1];
			}
			if (region->permutations[region->permutation_count].geometry[H2_BIPED_DETAIL_LEVELS - 1] != NONE)
				region->permutation_count++;
			else if (getenv("HALO_H2_DEBUG"))
				platform_log("h2 bipeds: %s: region %s permutation %s: no geometry made (sections %d %d %d %d %d)", biped->name,
					region->name, region->permutations[region->permutation_count].name, (int)read16(p + 4), (int)read16(p + 6),
					(int)read16(p + 8), (int)read16(p + 10), (int)read16(p + 12));
		}
		/* (the model's default variant's permutation first - the one an
		object shows unless chosen; none named: an empty permutation, the
		region hidden, as Halo 2's multiplayer Elite's honour guard armour) */
		{
			char wanted[32];

			if (region->permutation_count && variant_permutation(cache, biped->h2_hlmt_tag, region->name, wanted, sizeof(wanted)))
			{
				long k, found = NONE;

				for (k = 0; k < region->permutation_count; k++)
				{
					if (!strcmp(region->permutations[k].name, wanted))
						found = k;
				}
				if (found > 0)
				{
					struct made_region copy = *region;

					region->permutations[0] = copy.permutations[found];
					for (k = 0, j = 1; k < copy.permutation_count; k++)
					{
						if (k != found)
							region->permutations[j++] = copy.permutations[k];
					}
				}
				else if (found == NONE && region->permutation_count < H2_BIPED_MAXIMUM_PERMUTATIONS)
				{
					int level;

					memmove(&region->permutations[1], &region->permutations[0],
						(unsigned long)region->permutation_count * sizeof(region->permutations[0]));
					snprintf(region->permutations[0].name, sizeof(region->permutations[0].name), "%s", "none");
					for (level = 0; level < H2_BIPED_DETAIL_LEVELS; level++)
						region->permutations[0].geometry[level] = NONE;
					region->permutation_count++;
				}
			}
		}
		/* (a variant that hides the region - an empty permutation, a
		Marine's dress uniform without its helmet or armour, Hood's officers
		- or a script's object_set_permutation to an empty one needs "none"
		to choose: one made, no geometry. Every region gets it: 01b's
		cinematic takes Johnson's dress cap off his head - (object_set_permutation
		johnson_x02_01 "helmet" "") - for the cap he holds, a scenery of its
		own (johnson_hat), and no variant hides the Marine's helmet, so the
		script found no "none" and he wore two caps) */
		if (region->permutation_count && region->permutation_count < H2_BIPED_MAXIMUM_PERMUTATIONS &&
			strcmp(region->permutations[0].name, "none") &&
			(variants_hide(cache, biped->h2_hlmt_tag, region) || !getenv("HALO_H2_NONE_BY_VARIANT_ONLY")))
		{
			int level;

			snprintf(region->permutations[region->permutation_count].name, sizeof(region->permutations[0].name), "%s", "none");
			for (level = 0; level < H2_BIPED_DETAIL_LEVELS; level++)
				region->permutations[region->permutation_count].geometry[level] = NONE;
			region->permutation_count++;
		}
		/* (its destroyed state: the permutation the model's variants' state
		4 names most for this region, an empty name - the region hidden -
		first among equals (Halo 2's hlmt variants 0x50, 0x38 each, regions
		0x14, 0x14 each: name, permutations 8, 0x20 each, their states 0xC,
		0x18 each: permutation name, state 6): a Marine's helmet gone, a
		Flood form's arm its stump. Made a permutation of its own,
		"~damaged" - the one this engine shows a region its damage
		destroyed -, a copy of that one's geometry (none: nothing drawn),
		the permutations the variants show kept as they are named) */
		if (region->permutation_count && region->permutation_count < H2_BIPED_MAXIMUM_PERMUTATIONS &&
			!getenv("HALO_H2_NO_DAMAGE_REGIONS"))
		{
			struct h2_cache_tag h;
			long variant_count, v;
			unsigned long variants;
			char names[8][32];
			long votes[8], name_count = 0, best = NONE;

			if (h2_cache_tag_get(cache, biped->h2_hlmt_tag, &h) && h2_cache_block(cache, h.address + 0x50, 0x38, &variant_count, &variants))
			{
				for (v = 0; v < variant_count; v++)
				{
					long rc, r, pc, k, sc, q;
					unsigned long rs, ps, ss;

					if (!h2_cache_block(cache, variants + 0x38 * (unsigned long)v + 0x14, 0x14, &rc, &rs))
						continue;
					for (r = 0; r < rc; r++)
					{
						char name[64];

						name_of(cache, h2_cache_read32(cache, rs + 0x14 * (unsigned long)r), name, sizeof(name));
						if (strcmp(name, region->name) || !h2_cache_block(cache, rs + 0x14 * (unsigned long)r + 8, 0x20, &pc, &ps))
							continue;
						for (k = 0; k < pc; k++)
						{
							if (!h2_cache_block(cache, ps + 0x20 * (unsigned long)k + 0xC, 0x18, &sc, &ss))
								continue;
							for (q = 0; q < sc; q++)
							{
								unsigned char const *st = h2_cache_at(cache, ss + 0x18 * (unsigned long)q, 0x18);
								char destroyed[32];
								long n;

								if (!st || read16(st + 6) != 4)
									continue;
								name_of(cache, read32(st), destroyed, sizeof(destroyed));
								for (n = 0; n < name_count && strcmp(names[n], destroyed); n++)
									;
								if (n == name_count && name_count < 8)
								{
									snprintf(names[name_count], sizeof(names[0]), "%s", destroyed);
									votes[name_count++] = 0;
								}
								if (n < name_count)
									votes[n]++;
							}
						}
					}
				}
			}
			for (v = 0; v < name_count; v++)
			{
				if (best == NONE || votes[v] > votes[best] || (votes[v] == votes[best] && !names[v][0]))
					best = v;
			}
			if (best != NONE)
			{
				long k, made = region->permutation_count++;
				int level;

				region->destroyable = TRUE;
				snprintf(region->destroyed, sizeof(region->destroyed), "%s", names[best]);
				for (k = 0; k < made && (!names[best][0] || strcmp(region->permutations[k].name, names[best])); k++)
					;
				for (level = 0; level < H2_BIPED_DETAIL_LEVELS; level++)
					region->permutations[made].geometry[level] = k < made ? region->permutations[k].geometry[level] : NONE;
				snprintf(region->permutations[made].name, sizeof(region->permutations[made].name), "%s", "~damaged");
			}
		}
		if (region->permutation_count)
			biped->region_count++;
	}
	if (!biped->region_count || !biped->geometry_count)
		return FALSE;
	vertices_finish(biped);
	vertices_spill(host, biped);
	/* markers: Halo 2's names with spaces for underscores (this engine's
	"right hand"), sorted as model_find_marker searches them; one on every
	region (-1) put on the first region's permutations */
	if (h2_cache_block(cache, t.address + H2_MODE_MARKER_GROUPS, 0xC, &count, &groups))
	{
		for (i = 0; i < count && biped->marker_count < MAXIMUM_MARKERS; i++)
		{
			struct made_marker *marker = &biped->markers[biped->marker_count];
			long marker_count;
			unsigned long markers;
			char *c;

			memset(marker, 0, sizeof(*marker));
			name_of(cache, h2_cache_read32(cache, groups + 0xC * i), marker->name, sizeof(marker->name));
			for (c = marker->name; *c; c++)
			{
				if (*c == '_')
					*c = ' ';
			}
			if (!marker->name[0] || !h2_cache_block(cache, groups + 0xC * i + 4, 0x24, &marker_count, &markers))
				continue;
			for (j = 0; j < marker_count; j++)
			{
				unsigned char const *m = h2_cache_at(cache, markers + 0x24 * j, 0x24);
				signed char region = m ? (signed char)m[0] : -1, permutation = m ? (signed char)m[1] : -1;
				long r, p;

				if (!m || m[2] >= biped->h2_node_count)
					continue;
				for (r = region < 0 ? 0 : region; r <= (region < 0 ? 0 : region) && r < biped->region_count; r++)
				{
					for (p = permutation < 0 ? 0 : permutation;
						p < (permutation < 0 ? biped->regions[r].permutation_count : permutation + 1) && marker->instance_count < 8; p++)
					{
						int k;

						marker->instances[marker->instance_count].region = (unsigned char)r;
						marker->instances[marker->instance_count].permutation = (unsigned char)p;
						marker->instances[marker->instance_count].node = (unsigned char)biped->node_remap[m[2]];
						memcpy(marker->instances[marker->instance_count].translation, m + 4, 12);
						memcpy(marker->instances[marker->instance_count].rotation, m + 0x10, 16);
						for (k = 0; k < 3; k++)
							marker->instances[marker->instance_count].rotation[k] = -marker->instances[marker->instance_count].rotation[k];
						marker->instance_count++;
					}
				}
			}
			if (marker->instance_count)
				biped->marker_count++;
		}
		/* (the Elite's hands are "right_hand_elite" and "left_hand_elite",
		where its weapons go: this engine's units hold theirs at "right
		hand" - an alias of each where the model has no marker so named) */
		{
			/* (the Enforcer's guns' triggers - its needler's and rockets' -
			this engine's weapons' "primary trigger" and "secondary trigger",
			where its built-in guns fire from: h2_weapons.c) */
			static char const *const aliases[][2] = { { "right hand elite", "right hand" }, { "left hand elite", "left hand" },
				{ "needler trigger", "primary trigger" }, { "rocket trigger", "secondary trigger" } };
			long a, k;

			for (a = 0; a < (long)(sizeof(aliases) / sizeof(aliases[0])) && biped->marker_count < MAXIMUM_MARKERS; a++)
			{
				boolean have = FALSE;
				long found = NONE;

				for (k = 0; k < biped->marker_count; k++)
				{
					if (!strcmp(biped->markers[k].name, aliases[a][1]))
						have = TRUE;
					if (!strcmp(biped->markers[k].name, aliases[a][0]))
						found = k;
				}
				if (!have && found != NONE)
				{
					biped->markers[biped->marker_count] = biped->markers[found];
					snprintf(biped->markers[biped->marker_count].name, sizeof(biped->markers[0].name), "%s", aliases[a][1]);
					biped->marker_count++;
				}
			}
		}
		/* (a biped's gun marker its tag's own - right_hand_node, preferred_gun_node:
		the Jackal's "left_hand_jackal", the Brute's "right_hand_brute". On the model's
		plain hand a Jackal's rifle lay along its back) */
		{
			static char const *const guns[][2] = { { "jackal", "left hand jackal" }, { "brute", "right hand brute" } };
			long a, k, found;

			for (a = 0; a < (long)(sizeof(guns) / sizeof(guns[0])); a++)
			{
				if (strcmp(biped->name, guns[a][0]))
					continue;
				found = NONE;
				for (k = 0; k < biped->marker_count; k++)
				{
					if (!strcmp(biped->markers[k].name, guns[a][1]))
						found = k;
				}
				if (found != NONE)
				{
					long at = NONE;

					for (k = 0; k < biped->marker_count; k++)
					{
						if (!strcmp(biped->markers[k].name, "right hand"))
							at = k;
					}
					if (at == NONE && biped->marker_count < MAXIMUM_MARKERS)
						at = biped->marker_count++;
					if (at != NONE)
					{
						biped->markers[at] = biped->markers[found];
						snprintf(biped->markers[at].name, sizeof(biped->markers[0].name), "right hand");
					}
				}
				platform_log("h2 bipeds: %s: gun marker %s %s", biped->name, guns[a][1], found != NONE ? "used" : "not in the model");
			}
			if (getenv("HALO_H2_AI_DEBUG"))
			{
				for (k = 0; k < biped->marker_count; k++)
					platform_log("h2 bipeds: %s: marker %s", biped->name, biped->markers[k].name);
			}
		}
		qsort(biped->markers, (size_t)biped->marker_count, sizeof(biped->markers[0]), marker_compare);
	}
	return TRUE;
}

H2_ALIGN static void biped_free(struct made_biped *biped)
{
	long g, p;

	for (g = 0; g < biped->geometry_count; g++)
	{
		for (p = 0; p < biped->geometries[g].part_count; p++)
		{
			free(biped->geometries[g].parts[p].vertices);
			free(biped->geometries[g].parts[p].strip);
		}
	}
	free(biped->geometries);
	h2_graph_free(biped->graph);
	memset(biped, 0, sizeof(*biped));
}

/* ---------- private code: writing */

/* a tag's struct at at (an offset of the tag data) given blocks and data
of its own: each block and data field it points to copied (and theirs, by
the tags' layouts - tag_relocate.h), so that nothing is shared with the
tag it was copied from (the tag check refuses blocks that overlap) */
H2_ALIGN static void deep_copy(struct h2_biped_host const *host, unsigned short layout, unsigned long at, int depth)
{
	void *c = host->context;
	long count = halo_tag_layout_field_count(layout), i;

	if (layout == HALO_TAG_LAYOUT_NONE || layout == HALO_TAG_LAYOUT_UNKNOWN || depth > 16)
		return;
	for (i = 0; i < count && !host->failed(c); i++)
	{
		struct halo_tag_layout_field field;
		unsigned long f;

		if (!halo_tag_layout_field(layout, i, &field))
			continue;
		f = at + field.offset;
		if (field.kind == HALO_TAG_LAYOUT_FIELD_BLOCK)
		{
			long n = (long)read32(host->at(c, f));
			unsigned long address = read32(host->at(c, f + 4)), copy, bytes;
			long e;

			if (n <= 0 || !field.child_size || address < host->link)
				continue;
			bytes = (unsigned long)n * field.child_size;
			copy = host->alloc(c, bytes);
			if (host->failed(c))
				return;
			memmove(host->at(c, copy), host->at(c, address - host->link), bytes);
			write32(host->at(c, f + 4), host->link + copy);
			for (e = 0; e < n && field.child != HALO_TAG_LAYOUT_NONE; e++)
				deep_copy(host, field.child, copy + field.child_size * e, depth + 1);
		}
		else if (field.kind == HALO_TAG_LAYOUT_FIELD_DATA)
		{
			unsigned long size = read32(host->at(c, f)), address = read32(host->at(c, f + 12)), copy;

			if (!size || address < host->link)
				continue;
			copy = host->alloc(c, size);
			if (host->failed(c))
				return;
			memmove(host->at(c, copy), host->at(c, address - host->link), size);
			write32(host->at(c, f + 12), host->link + copy);
		}
		else if (field.kind == HALO_TAG_LAYOUT_FIELD_STRUCT)
			deep_copy(host, field.child, f, depth + 1);
	}
}

H2_ALIGN static void name_copy(unsigned char *field, char const *name)
{
	memset(field, 0, 32);
	memcpy(field, name, strlen(name) < 31 ? strlen(name) : 31);
}

/* the model shaders (shader_model: base map, multipurpose map, colour
change from the object's change colour C - the player's) */
H2_ALIGN static void shaders_write(struct h2_biped_host const *host, struct made_biped *biped)
{
	void *c = host->context;
	long i;

	for (i = 0; i < biped->shader_count && !host->failed(c); i++)
	{
		struct made_shader const *s = &biped->shaders[i];
		unsigned long root, maps;
		unsigned char *o;

		if (s->plasma)
		{
			long kit = host->kit_find(c, 'spla', "characters\\cyborg\\shaders\\shield hit");
			unsigned long from = kit != NONE ? host->root_of(c, kit) : 0;
			int k;

			/* (no kit plasma: an empty one, nothing drawn) */
			root = host->alloc(c, CE_SHADER_PLASMA_SIZE);
			if (host->failed(c))
				return;
			if (from)
			{
				memcpy(host->at(c, root), host->at(c, from), CE_SHADER_PLASMA_SIZE);
				deep_copy(host, halo_tag_layout_root('spla'), root, 0);
			}
			host->set_root(c, biped->first_tag + _tag_shaders + i, root);
			o = host->at(c, root);
			/* (its colour facing and glancing Halo 2's tint, a half's alpha
			facing; no colour source) */
			writef(o + 0x60, 0.5f);
			writef(o + 0x70, 1.0f);
			for (k = 0; k < 3; k++)
			{
				writef(o + 0x64 + 4 * k, 0.5f * s->plasma_colour[k]);
				writef(o + 0x74 + 4 * k, s->plasma_colour[k]);
			}
			write16(o + 0x80, 0);
			/* (Halo 2's pass beside the CE fields - h2_plasma_shield.h: the
			desktop's program; the CE noises' scales Halo 2's; the Chief's
			change-colour pass reads the object's primary colour: CE's colour
			source A) */
			if (s->shield_pass_read && !s->overshield && !getenv("HALO_H2_PLASMA_SHIELD_OLD"))
			{
				int n;

				h2_plasma_shield_write(o, &s->shield_pass, (short)read16(o + 0x2C));
				for (n = 0; n < 2; n++)
					writef(o + (n ? 0x118 : 0xD0), s->shield_pass.scale[n]);
				if (s->shield_pass.mode == _h2_plasma_shield_mode_change_color)
					write16(o + 0x80, 1);
				if (getenv("HALO_H2_DEBUG"))
					platform_log("h2 bipeds: %s: %s: plasma_shield pass written (kit intensity source %d, exponent %.2f; offset source %d amount %.3f)",
						biped->name, s->name, (int)read16(o + 0x2C), readf(o + 0x30), (int)read16(o + 0x34), readf(o + 0x38));
			}
			if (s->overshield)
			{
				/* (Halo 2's overshield pass: noise coloured facing by its
				colour - the overshield's amount's, given as the modifier's
				colour A - and glancing by its glancing colour (0, 0.25, 0.25),
				added; its brightness the modifier's value A: on whenever it
				is drawn) */
				write16(o + 0x2C, 1);
				writef(o + 0x30, 1.0f);
				write16(o + 0x34, 0);
				writef(o + 0x60, 1.0f);
				writef(o + 0x70, 1.0f);
				writef(o + 0x64, 1.0f);
				writef(o + 0x68, 1.0f);
				writef(o + 0x6C, 1.0f);
				writef(o + 0x74, 0.0f);
				writef(o + 0x78, 0.5f);
				writef(o + 0x7C, 0.5f);
				write16(o + 0x80, 1);
			}
			continue;
		}
		if (s->transparent)
		{
			/* (an added transparent chicago shader, two-sided: its one map) */
			root = host->alloc(c, CE_SHADER_CHICAGO_SIZE);
			if (host->failed(c))
				return;
			host->set_root(c, biped->first_tag + _tag_shaders + i, root);
			o = host->at(c, root);
			write16(o + 0x02, 2);
			write16(o + 0x24, 6);
			write16(o + 0x28, 1 << 2);
			write16(o + 0x2C, s->alpha_blended ? 0 : 3);
			host->reference(c, root + 0x38, 'lens', 0xFFFFFFFFUL);
			maps = host->block(c, root + 0x54, 1, 220);
			if (host->failed(c))
				return;
			o = host->at(c, maps);
			/* (u and v clamped: _shader_transparent_chicago_map_flag_*_clamped_bit) */
			if (s->emblem)
				write16(o, (1 << 2) | (1 << 3));
			writef(o + 0x54, 1.0f);
			writef(o + 0x58, 1.0f);
			writef(o + 0xA8, 1.0f);
			writef(o + 0xB0, 1.0f);
			writef(o + 0xB8, 1.0f);
			writef(o + 0xC0, 1.0f);
			writef(o + 0xC8, 1.0f);
			writef(o + 0xD0, 360.0f);
			host->reference(c, maps + 0x6C, 'bitm', s->base_bitmap != NONE ? host->bitmap_tag_id(c, s->base_bitmap) : 0xFFFFFFFFUL);
			continue;
		}
		root = host->alloc(c, CE_SHADER_MODEL_SIZE);
		host->set_root(c, biped->first_tag + _tag_shaders + i, root);
		host->reference(c, root + 0xA4, 'bitm', s->base_bitmap != NONE ? host->bitmap_tag_id(c, s->base_bitmap) : 0xFFFFFFFFUL);
		host->reference(c, root + 0xBC, 'bitm',
			s->multipurpose_bitmap != NONE ? host->bitmap_tag_id(c, s->multipurpose_bitmap) : 0xFFFFFFFFUL);
		host->reference(c, root + 0xDC, 'bitm', 0xFFFFFFFFUL);
		host->reference(c, root + 0x164, 'bitm', 0xFFFFFFFFUL);
		o = host->at(c, root);
		write16(o + 0x22, 7);
		write16(o + 0x24, 4);
		write16(o + 0x28, s->alpha_tested ? 0 : 1 << 2);         /* not alpha tested (but lit hair) */
		/* (colour change: the object's third change colour, C - the
		player's colour as players.c gives it every one, which the stock
		biped's change colours leave as it is: A it darkens with the
		shields, B it scales - as the stock armor's shader does) */
		if (s->change_colour && !getenv("HALO_H2_BIPED_NO_COLOUR"))
			write16(o + 0x4C, 3);
		writef(o + 0x74, 1.0f);
		/* (self-illumination: its colour's two ends, cycled by a cosine in
		its period, or steady) */
		if (s->illum && s->multipurpose_bitmap != NONE)
		{
			int k;

			write16(o + 0x70, s->illum_shield ? 1 : 0);             /* colour A (the shields'), else none */
			write16(o + 0x72, s->illum_period > 0.0f ? 2 : 0);       /* cosine, else one */
			writef(o + 0x74, s->illum_period > 0.0f ? s->illum_period : 1.0f);
			for (k = 0; k < 3; k++)
			{
				writef(o + 0x78 + 4 * k, s->illum_lower[k]);
				writef(o + 0x84 + 4 * k, s->illum_upper[k]);
			}
			if (getenv("HALO_H2_DEBUG"))
				fprintf(stderr, "h2 bipeds: shader %s: glow %.2f %.2f %.2f - %.2f %.2f %.2f period %.1f\n", s->name,
					s->illum_lower[0], s->illum_lower[1], s->illum_lower[2], s->illum_upper[0], s->illum_upper[1],
					s->illum_upper[2], s->illum_period);
		}
		writef(o + 0x9C, 1.0f);
		writef(o + 0xA0, 1.0f);
		writef(o + 0xD8, 1.0f);
		writef(o + 0xEC, 1.0f);
		/* (Halo 2's material: the detail map, biased multiply everywhere;
		the environment cube, its tints and brightnesses, masked by the
		multipurpose map's red - the base map's alpha, Halo 2's mask) */
		if (getenv("HALO_H2_DEBUG"))
			fprintf(stderr, "h2 bipeds: shader %s: base %ld detail %ld (%08lx) cube %ld multipurpose %ld\n", s->name, s->base_bitmap,
				s->detail_bitmap, s->detail_bitmap != NONE ? host->bitmap_tag_id(c, s->detail_bitmap) : 0UL, s->cube_bitmap,
				s->multipurpose_bitmap);
		if (s->detail_bitmap != NONE)
		{
			host->reference(c, root + 0xDC, 'bitm', host->bitmap_tag_id(c, s->detail_bitmap));
			o = host->at(c, root);
			write16(o + 0xD4, 0);   /* biased multiply */
			write16(o + 0xD6, 0);   /* everywhere */
			writef(o + 0xD8, s->material.detail_scale[0]);
			writef(o + 0xEC, s->material.detail_scale[0] > 0.0f ? s->material.detail_scale[1] / s->material.detail_scale[0] : 1.0f);
		}
		if (s->material.known && !s->skipped)
		{
			h2_material_model_shader_write(host->at(c, root), &s->material,
				s->bump_bitmap != NONE ? host->bitmap_tag_id(c, s->bump_bitmap) : 0xFFFFFFFFUL);
			/* (a multiplayer armour's secondary colour: h2_materials.h) */
			if (s->change_colour && s->multipurpose_bitmap != NONE && !strstr(h2_cache_scenario_path(host->cache), "\\solo\\"))
				host->at(c, root)[H2_MODEL_MATERIAL_EXTENSION_OFFSET + offsetof(struct h2_model_material_extension, flags)] |=
					_h2_model_material_secondary_bit;
			if (host->mean_alpha && s->material.detail_tag != NONE && !getenv("HALO_H2_NO_DETAIL_MASK"))
			{
				float alpha = host->mean_alpha(host->context, s->material.detail_tag);

				if (alpha >= 0.0f)
					h2_material_model_detail_alpha_write(host->at(c, root), alpha);
			}
		}
		if (s->cube_bitmap != NONE)
		{
			int k;

			host->reference(c, root + 0x164, 'bitm', host->bitmap_tag_id(c, s->cube_bitmap));
			o = host->at(c, root);
			writef(o + 0x144, s->material.env_brightness);
			writef(o + 0x154, s->material.env_glancing_brightness);
			for (k = 0; k < 3; k++)
			{
				writef(o + 0x148 + 4 * k, s->material.env_tint[k]);
				writef(o + 0x158 + 4 * k, s->material.env_glancing_tint[k]);
			}
		}
	}
}

/* Halo 2's own levels of detail for a biped, as this engine's cutoffs.
Halo 2 picks a model's level by distance, its model tag's (hlmt 0x34..0x44,
world units: "reduce to" L1 super low .. L5 super high - the Spartan's and
the Elite's 30 20 15 8 0, so L5 nearer than 8 and L6 never in play); this
engine's models.c by the pixels the object's bounding sphere spans (the
biped's 0x4: 0.44 both). A level drawn nearer than its distance is drawn
above the pixels that sphere spans there in this engine's stock view - 457
pixels a world unit at a unit's depth: render_cameras.c's 480-line window at
70 degrees, 240 / (0.75 tan 35) -: the Spartan's L2 from 13.4 pixels, L3
20.1, L4 26.8, L5 50.3; its least (L1) drawn from 2 (the PC's before, kept:
Halo 2's disappear distance not taken - open). FALSE: no such distances
(another's) */
#define BIPED_LOD_PIXELS_PER_UNIT 457.0f
H2_ALIGN static boolean model_cutoffs_halo2(struct h2_cache *cache, struct made_biped const *biped, float cutoffs[5])
{
	struct h2_cache_tag t;
	unsigned char const *distances, *object;
	float bounding, previous = 1000.0f;
	int k;

	if (!cache || biped->h2_hlmt_tag == NONE || biped->h2_biped_tag == NONE ||
		!h2_cache_tag_get(cache, biped->h2_hlmt_tag, &t) || t.size < 0x48 || !(distances = h2_cache_at(cache, t.address + 0x34, 16)) ||
		!h2_cache_tag_get(cache, biped->h2_biped_tag, &t) || t.size < 8 || !(object = h2_cache_at(cache, t.address + 4, 4)))
	{
		return FALSE;
	}
	bounding = readf(object);
	if (!(bounding > 0.1f && bounding < 8.0f))
		return FALSE;
	cutoffs[0] = 2.0f;
	for (k = 1; k < 5; k++)
	{
		float distance = readf(distances + 4 * (k - 1));

		if (!(distance > 0.0f && distance <= previous))
			return FALSE;
		previous = distance;
		cutoffs[k] = MAX(BIPED_LOD_PIXELS_PER_UNIT * 2.0f * bounding / distance, cutoffs[k - 1]);
	}
	return TRUE;
}

H2_ALIGN static boolean model_write(struct h2_biped_host const *host, struct made_biped *biped)
{
	void *c = host->context;
	unsigned long root = host->alloc(c, CE_MODEL_SIZE), nodes, markers, regions, geometries, shaders;
	long i, j, used_shaders = 0, next_buffer = 0;
	short shader_slot[MAXIMUM_MATERIALS];

	if (host->failed(c))
		return FALSE;
	host->set_root(c, biped->first_tag + _tag_model, root);
	{
		unsigned char *o = host->at(c, root);
		/* (the cutoffs, pixels, as models.c reads them: by level, super
		low first - the least a model is drawn at, then the least each finer
		level is: Halo 2's own on both tiers, model_cutoffs_halo2 - the
		Vita had the stock cyborg's 5 50 120 250 400, a player L3 (738
		triangles of 2814 to 3086) below 250 pixels, until
		feat/h2-vita-full-model-detail; the PC 2 12 25 45 80, its L5 from 5
		world units not Halo 2's 8, its L4 from 9 not 15. A biped with none
		of its own, these last) */
		static float const fallback_cutoffs[5] = { 2.0f, 12.0f, 25.0f, 45.0f, 80.0f };
		/* (HALO_H2_BIPED_STOCK_LOD: the stock cyborg's, a test's) */
		static float const stock_cutoffs[5] = { 5.0f, 50.0f, 120.0f, 250.0f, 400.0f };
		float cutoffs[5];

		if (getenv("HALO_H2_BIPED_STOCK_LOD"))
			memcpy(cutoffs, stock_cutoffs, sizeof(cutoffs));
		else if (!model_cutoffs_halo2(host->cache, biped, cutoffs))
			memcpy(cutoffs, fallback_cutoffs, sizeof(cutoffs));
		if (getenv("HALO_H2_DEBUG"))
			platform_log("h2 bipeds: %s: level cutoffs %.1f %.1f %.1f %.1f %.1f pixels", biped->name,
				cutoffs[0], cutoffs[1], cutoffs[2], cutoffs[3], cutoffs[4]);

		for (i = 0; i < 5; i++)
		{
			writef(o + 0x08 + 4 * i, cutoffs[i]);
			write16(o + 0x1C + 2 * i, biped->node_count);
		}
		writef(o + 0x30, biped->uv_scale);
		writef(o + 0x34, biped->uv_scale);
	}
	/* nodes */
	nodes = host->block(c, root + 0xB8, biped->node_count, CE_MODEL_NODE_SIZE);
	for (i = 0; i < biped->node_count && !host->failed(c); i++)
	{
		unsigned char *o = host->at(c, nodes + CE_MODEL_NODE_SIZE * i);

		name_copy(o, biped->node_names[i]);
		write16(o + 0x20, biped->node_sibling[i]);
		write16(o + 0x22, biped->node_child[i]);
		write16(o + 0x24, biped->node_parent[i]);
		memcpy(o + 0x28, biped->node_default[i].translation, 12);
		memcpy(o + 0x34, biped->node_default[i].rotation, 16);
		writef(o + 0x44, biped->node_distance[i]);
		memcpy(o + 0x68, biped->node_inverse[i], 52);
	}
	/* markers */
	markers = host->block(c, root + 0xAC, biped->marker_count, CE_MODEL_MARKER_SIZE);
	for (i = 0; i < biped->marker_count && !host->failed(c); i++)
	{
		struct made_marker const *m = &biped->markers[i];
		unsigned long instances;

		name_copy(host->at(c, markers + CE_MODEL_MARKER_SIZE * i), m->name);
		instances = host->block(c, markers + CE_MODEL_MARKER_SIZE * i + 0x34, m->instance_count, CE_MODEL_MARKER_INSTANCE_SIZE);
		for (j = 0; j < m->instance_count && !host->failed(c); j++)
		{
			unsigned char *o = host->at(c, instances + CE_MODEL_MARKER_INSTANCE_SIZE * j);

			o[0] = m->instances[j].region;
			o[1] = m->instances[j].permutation;
			o[2] = m->instances[j].node;
			memcpy(o + 4, m->instances[j].translation, 12);
			memcpy(o + 0x10, m->instances[j].rotation, 16);
		}
	}
	/* regions and permutations (all but the first "cannot be chosen
	randomly": a player's are chosen - h2_player_models) */
	regions = host->block(c, root + 0xC4, biped->region_count, CE_MODEL_REGION_SIZE);
	for (i = 0; i < biped->region_count && !host->failed(c); i++)
	{
		struct made_region const *r = &biped->regions[i];
		unsigned long permutations;

		name_copy(host->at(c, regions + CE_MODEL_REGION_SIZE * i), r->name);
		permutations = host->block(c, regions + CE_MODEL_REGION_SIZE * i + 0x40, r->permutation_count, CE_MODEL_PERMUTATION_SIZE);
		for (j = 0; j < r->permutation_count && !host->failed(c); j++)
		{
			unsigned char *o = host->at(c, permutations + CE_MODEL_PERMUTATION_SIZE * j);
			int level;

			name_copy(o, r->permutations[j].name);
			if (j > 0)
				write32(o + 0x20, 1);
			for (level = 0; level < H2_BIPED_DETAIL_LEVELS; level++)
				write16(o + 0x40 + 2 * level, getenv("HALO_H2_BIPED_ONE_LOD") ?
					r->permutations[j].geometry[H2_BIPED_DETAIL_LEVELS - 1] : r->permutations[j].geometry[level]);
			host->block(c, permutations + CE_MODEL_PERMUTATION_SIZE * j + 0x4C, 0, 0x50);
		}
	}
	/* the shader block: each shader a part draws with */
	for (i = 0; i < MAXIMUM_MATERIALS; i++)
		shader_slot[i] = NONE;
	for (i = 0; i < biped->geometry_count; i++)
	{
		for (j = 0; j < biped->geometries[i].part_count; j++)
		{
			short s = biped->geometries[i].parts[j].shader;

			if (shader_slot[s] == NONE)
				shader_slot[s] = (short)used_shaders++;
		}
	}
	shaders = host->block(c, root + 0xDC, used_shaders, CE_MODEL_SHADER_SIZE);
	for (i = 0; i < biped->shader_count && !host->failed(c); i++)
	{
		if (shader_slot[i] != NONE)
			host->reference(c, shaders + CE_MODEL_SHADER_SIZE * shader_slot[i],
				biped->shaders[i].level_shader != NONE ? 'schi' : biped->shaders[i].plasma ? 'spla' : biped->shaders[i].transparent ? 'schi' : 'soso',
				biped->shaders[i].level_shader != NONE ? host->level_shader_id(host->context, biped->shaders[i].level_shader) :
				host->tag_id(c, biped->first_tag + _tag_shaders + i));
	}
	/* geometries: parts, their vertices and strips in the tag data, a
	buffer table entry each */
	geometries = host->block(c, root + 0xD0, biped->geometry_count, CE_MODEL_GEOMETRY_SIZE);
	for (i = 0; i < biped->geometry_count && !host->failed(c); i++)
	{
		struct made_geometry const *g = &biped->geometries[i];
		unsigned long parts = host->block(c, geometries + CE_MODEL_GEOMETRY_SIZE * i + 0x24, g->part_count, CE_MODEL_PART_SIZE);

		for (j = 0; j < g->part_count && !host->failed(c); j++)
		{
			struct made_part const *part = &g->parts[j];
			unsigned long vertices = host->alloc(c, (unsigned long)part->vertex_count * VERTEX_SIZE);
			unsigned long strip = host->alloc(c, (unsigned long)part->strip_count * 2);
			long vb = host->first_buffer + next_buffer, ib = host->first_index_buffer + next_buffer;
			unsigned char *o;
			int k;

			if (host->failed(c))
				break;
			next_buffer++;
			if (part->spilled)
			{
				unsigned long vertex_bytes = (unsigned long)part->vertex_count * VERTEX_SIZE;

				if (!host->spill_get(c, host->at(c, vertices), vertex_bytes, part->spill) ||
					!host->spill_get(c, host->at(c, strip), (unsigned long)part->strip_count * 2, part->spill + vertex_bytes))
				{
					platform_log("h2 bipeds: %s: a part's vertices not read back from the spill file (%lu bytes at %lu)", biped->name,
						vertex_bytes, (unsigned long)part->spill);
					return FALSE;
				}
			}
			else
			{
				memcpy(host->at(c, vertices), part->vertices, (unsigned long)part->vertex_count * VERTEX_SIZE);
				memcpy(host->at(c, strip), part->strip, (unsigned long)part->strip_count * 2);
			}
			write32(host->at(c, host->vertex_table + 12 * vb + 4), host->link + vertices);
			write32(host->at(c, host->index_table + 12 * ib + 4), host->link + strip);
			o = host->at(c, parts + CE_MODEL_PART_SIZE * j);
			write16(o + 0x04, shader_slot[part->shader]);
			o[6] = 0xFF;
			o[7] = 0xFF;
			write16(o + 0x08, part->centroid_node);
			write16(o + 0x0A, part->centroid_node);
			writef(o + 0x0C, 1.0f);
			for (k = 0; k < 3; k++)
				writef(o + 0x14 + 4 * k, part->centroid[k]);
			write16(o + 0x44, 1);       /* a strip */
			write32(o + 0x48, (unsigned long)(part->strip_count - 2));
			write32(o + 0x4C, host->link + strip);
			write32(o + 0x50, host->link + host->index_table + 12 * ib);
			write16(o + 0x54, 5);       /* compressed model vertices */
			write32(o + 0x58, (unsigned long)part->vertex_count);
			write32(o + 0x60, host->link + vertices);
			write32(o + 0x64, host->link + host->vertex_table + 12 * vb);
		}
	}
	biped->report.model_bytes = 0;
	for (i = 0; i < biped->geometry_count; i++)
	{
		for (j = 0; j < biped->geometries[i].part_count; j++)
			biped->report.model_bytes += biped->geometries[i].parts[j].vertex_count * VERTEX_SIZE +
				biped->geometries[i].parts[j].strip_count * 2 + CE_MODEL_PART_SIZE;
	}
	return !host->failed(c);
}

/* (h2_damage.c: a float written into a tag) */
H2_ALIGN static void damage_write_real(void *at, float value)
{
	writef(at, value);
}

/* the collision model's damage Halo 2's (h2_damage.h): the hlmt's body and
shield vitality, stun and recharge, the body's regeneration; its materials
the species' material types (the damage effects made from Halo 2's have
Halo 2's damage table's multipliers for them), none leaking or scaling
damage (Halo 2's materials do not: the table does) */
H2_ALIGN static void damage_write(struct h2_biped_host const *host, struct made_biped *biped, unsigned long root)
{
	void *c = host->context;
	struct h2_damage_info info;
	short species = h2_damage_species_named(biped->name);
	unsigned long resistance = root + offsetof(struct collision_model, resistance);
	long material_count, i;
	unsigned long materials;

	if (getenv("HALO_H2_DAMAGE_AS_IS") || !h2_damage_info_read(host->cache, biped->h2_biped_tag, &info))
	{
		platform_log("h2 bipeds: %s: its damage the stock biped's", biped->name);
		return;
	}
	/* (a character's - not a player's species -: its hlmt's body and
	shield vitality, stun and recharge alone, its materials the stock
	ones (an AI's actor variant's vitality, Halo 2's character's, wins,
	h2_ai.c); a model of no shield none) */
	if (species == NONE)
	{
		if (info.body_vitality > 0.0f)
			writef(host->at(c, resistance + offsetof(struct damage_resistance, maximum_body_vitality)), info.body_vitality);
		writef(host->at(c, resistance + offsetof(struct damage_resistance, maximum_shield_vitality)), info.shield_vitality);
		writef(host->at(c, resistance + offsetof(struct damage_resistance, minimum_shield_stun_damage)), info.shield_minimum_stun_damage);
		writef(host->at(c, resistance + offsetof(struct damage_resistance, shield_stun_time)), info.shield_stun_time);
		writef(host->at(c, resistance + offsetof(struct damage_resistance, shield_recharge_time)), info.shield_recharge_time);
		writef(host->at(c, resistance + offsetof(struct damage_resistance, runtime_shield_recharge_velocity)),
			info.shield_recharge_time > 0.0f ? 1.0f / (info.shield_recharge_time * 30.0f) : 0.0f);
		writef(host->at(c, resistance + offsetof(struct damage_resistance, shield_failure_threshold)), 0.0f);
		writef(host->at(c, resistance + offsetof(struct damage_resistance, maximum_shield_failure)), 0.0f);
		platform_log("h2 bipeds: %s: body %.0f, shield %.0f (Halo 2's model's)", biped->name, info.body_vitality, info.shield_vitality);
		return;
	}
	writef(host->at(c, resistance + offsetof(struct damage_resistance, maximum_body_vitality)), info.body_vitality);
	writef(host->at(c, resistance + offsetof(struct damage_resistance, maximum_shield_vitality)), info.shield_vitality);
	writef(host->at(c, resistance + offsetof(struct damage_resistance, minimum_shield_stun_damage)), info.shield_minimum_stun_damage);
	writef(host->at(c, resistance + offsetof(struct damage_resistance, shield_stun_time)), info.shield_stun_time);
	writef(host->at(c, resistance + offsetof(struct damage_resistance, shield_recharge_time)), info.shield_recharge_time);
	/* (the tool's: the shield's recharge, of the whole, a tick) */
	writef(host->at(c, resistance + offsetof(struct damage_resistance, runtime_shield_recharge_velocity)),
		info.shield_recharge_time > 0.0f ? 1.0f / (info.shield_recharge_time * 30.0f) : 0.0f);
	/* (Halo 2's shields do not fail early) */
	writef(host->at(c, resistance + offsetof(struct damage_resistance, shield_failure_threshold)), 0.0f);
	writef(host->at(c, resistance + offsetof(struct damage_resistance, maximum_shield_failure)), 0.0f);
	writef(host->at(c, resistance + offsetof(struct damage_resistance, port_body_minimum_stun_damage)), info.body_minimum_stun_damage);
	writef(host->at(c, resistance + offsetof(struct damage_resistance, port_body_stun_time)), info.body_stun_time);
	writef(host->at(c, resistance + offsetof(struct damage_resistance, port_body_recharge_velocity)),
		info.body_recharge_time > 0.0f ? 1.0f / (info.body_recharge_time * 30.0f) : 0.0f);
	writef(host->at(c, resistance + offsetof(struct damage_resistance, port_body_recharge_fraction)), info.body_recharge_fraction);
	write16(host->at(c, resistance + offsetof(struct damage_resistance, shield_material_type)),
		h2_damage_player_material_type(species, _h2_damage_shield));
	material_count = (long)read32(host->at(c, resistance + offsetof(struct damage_resistance, materials)));
	materials = read32(host->at(c, resistance + offsetof(struct damage_resistance, materials) + 4));
	for (i = 0; i < material_count && materials >= host->link; i++)
	{
		unsigned long at = materials - host->link + sizeof(struct damage_resistance_material) * i;
		unsigned char *m = host->at(c, at);

		if (getenv("HALO_H2_DEBUG"))
			platform_log("h2 bipeds: %s: material %ld %s type %d leak %.2f shield x%.2f body x%.2f flags %lx", biped->name, i,
				(char const *)m, (int)(short)read16(m + offsetof(struct damage_resistance_material, material_type)),
				readf(m + offsetof(struct damage_resistance_material, shield_leak_fraction)),
				readf(m + offsetof(struct damage_resistance_material, shield_damage_multiplier)),
				readf(m + offsetof(struct damage_resistance_material, body_damage_multiplier)),
				read32(m + offsetof(struct damage_resistance_material, flags)));
		write16(m + offsetof(struct damage_resistance_material, material_type), h2_damage_player_material_type(species, _h2_damage_body));
		writef(m + offsetof(struct damage_resistance_material, shield_leak_fraction), 0.0f);
		writef(m + offsetof(struct damage_resistance_material, shield_damage_multiplier), 1.0f);
		writef(m + offsetof(struct damage_resistance_material, body_damage_multiplier), 1.0f);
	}
	platform_log("h2 bipeds: %s: body %.0f (stun %.0f s, recharge %.0f s to %.0f%%), shield %.0f (stun %.0f s, recharge %.0f s)",
		biped->name, info.body_vitality, info.body_stun_time, info.body_recharge_time, 100.0f * info.body_recharge_fraction,
		info.shield_vitality, info.shield_stun_time, info.shield_recharge_time);
}

/* a material of the collision model's (its resistance) of one more: a copy of
the stock one `base`, named `name`, of Halo 2's armor class `armor_class`
(damage_resistance_material.pad, class + 1; h2_damage.h), no leak, no scale
(Halo 2's damage table does that), its hit effects' material type `type`
(NONE: the stock one's); its index, NONE: failed */
H2_ALIGN static short material_append(struct h2_biped_host const *host, unsigned long root, short base, short armor_class, short type,
	char const *name)
{
	void *c = host->context;
	unsigned long field = root + offsetof(struct collision_model, resistance.materials);
	long count = (long)read32(host->at(c, field));
	unsigned long old = read32(host->at(c, field + 4));
	unsigned long copy;
	struct damage_resistance_material *m;

	if (count < 2 || base >= count || old < host->link)
		return NONE;
	copy = host->block(c, field, count + 1, sizeof(struct damage_resistance_material));
	if (host->failed(c))
		return NONE;
	memmove(host->at(c, copy), host->at(c, old - host->link), sizeof(struct damage_resistance_material) * (size_t)count);
	m = (struct damage_resistance_material *)host->at(c, copy + sizeof(struct damage_resistance_material) * (unsigned long)count);
	*m = *(struct damage_resistance_material *)host->at(c, copy + sizeof(struct damage_resistance_material) * (unsigned long)base);
	snprintf(m->name, sizeof(m->name), "%s", name);
	if (type != NONE)
		m->material_type = type;
	m->pad = (word)(armor_class + 1);
	m->shield_leak_fraction = 0.0f;
	m->shield_damage_multiplier = 1.0f;
	m->body_damage_multiplier = 1.0f;
	return (short)count;
}

/* the collision model: the stock biped's (its vitality, shields, materials
and region), its nodes Halo 2's with Halo 2's collision BSPs on them (their
surfaces' materials the stock one's of the same name: head, else body) */
H2_ALIGN static boolean collision_write(struct h2_biped_host const *host, struct made_biped *biped, long kit_collision)
{
	void *c = host->context;
	struct h2_cache *cache = host->cache;
	struct h2_cache_tag t;
	unsigned long kit_root = kit_collision != NONE ? host->root_of(c, kit_collision) : 0;
	unsigned long root, nodes, spheres;
	long region_count, i, j, k, material_count;
	unsigned long regions, h2_materials;
	short material_map[MAXIMUM_MATERIALS];
	short material_region[MAXIMUM_MATERIALS];
	short species = h2_damage_species_named(biped->name);
	float region_threshold[CE_DAMAGE_REGIONS];
	boolean damage_regions = FALSE;
	long node_bsps[MAXIMUM_NODES];
	short node_region[MAXIMUM_NODES];
	boolean node_multi[MAXIMUM_NODES];
	short coll_node[MAXIMUM_NODES];

	if (!kit_root || biped->h2_coll_tag == NONE || !h2_cache_tag_get(cache, biped->h2_coll_tag, &t) ||
		!h2_cache_block(cache, t.address + 0x1C, 0xC, &region_count, &regions))
	{
		return FALSE;
	}
	root = host->alloc(c, CE_COLLISION_SIZE);
	if (host->failed(c))
		return FALSE;
	memcpy(host->at(c, root), host->at(c, kit_root), CE_COLLISION_SIZE);
	/* (its own copies of the stock one's blocks; not its nodes and
	pathfinding spheres, made below) */
	memset(host->at(c, root + 0x280), 0, 12);
	memset(host->at(c, root + 0x28C), 0, 12);
	deep_copy(host, halo_tag_layout_root('coll'), root, 0);
	host->set_root(c, biped->first_tag + _tag_collision, root);
	/* the Elite (the Heretics, the Ranger too) bleeds Halo 2's way (blue), not the stock cyborg's red: its
	body's material the Elite's (its hits' effects, h2_effects.c, and
	decals), its shield the Elite's energy shield, the area damage effect
	(the blood that pools where it falls) the Elite's own blood */
	if (!strncmp(biped->name, "elite", 5) || (!strncmp(biped->name, "heretic", 7) && !strstr(biped->name, "grunt")))
	{
		long blood = host->kit_find(c, 'effe', "effects\\blood collision elite large");
		long material_count = (long)read32(host->at(c, root + offsetof(struct collision_model, resistance.materials)));
		unsigned long materials = read32(host->at(c, root + offsetof(struct collision_model, resistance.materials) + 4));

		for (i = 0; i < material_count && materials >= host->link; i++)
			write16(host->at(c, materials - host->link + 0x48 * i + 0x24), 14);
		write16(host->at(c, root + offsetof(struct collision_model, resistance.shield_material_type)), 30);
		if (blood != NONE)
			host->reference(c, root + offsetof(struct collision_model, resistance.area_damage_effect), 'effe', host->tag_id(c, blood));
	}
	damage_write(host, biped, root);
	/* Halo 2's materials: the stock model's by name; a "shield" (the
	Jackal's arm shield: Halo 2's damage section "shield", absorbs body
	damage: it draws on the object's shield vitality, the character's 40,
	and the body takes nothing) one more material of this engine's, its
	impacts an energy shield's. The sections that ignore shields - the
	Jackal's body and head - go to the body at once: below */
	for (i = 0; i < MAXIMUM_MATERIALS; i++)
		material_map[i] = 1;
	if (h2_cache_block(cache, t.address + 0x14, 4, &material_count, &h2_materials))
	{
		short shield_material = NONE;

		for (i = 0; i < material_count && i < MAXIMUM_MATERIALS; i++)
		{
			char name[64];

			name_of(cache, h2_cache_read32(cache, h2_materials + 4 * i), name, sizeof(name));
			/* (the stock model's head material - its hits headshots, damage.c
			_damage_material_head_bit - where the model's material is of a
			damage section flagged headshotable, 0x80: Halo 2's head test,
			damage.cpp function_d8cb0, by the struck material's section; not
			by its name. A material the model does not name has no section,
			no headshot) */
			{
				short section;
				unsigned long section_flags;
				boolean found = h2_damage_material_section_flags(cache, biped->h2_biped_tag, h2_cache_read32(cache, h2_materials + 4 * i),
					&section, &section_flags);

				material_map[i] = found && (section_flags & 0x80) ? 0 : strstr(name, "leg") ? 2 : 1;
				if (getenv("HALO_LOG_HEADSHOTS"))
					platform_log("h2 bipeds: %s: material %s: section %d flags %04lx: %s (by its name: %s)", biped->name, name, (int)section,
						section_flags, material_map[i] == 0 ? "headshotable" : "not headshotable", strstr(name, "head") ? "head" : "not head");
			}
			/* (a character's armor classes: the model's materials by name -> global
			material -> its class, a material of its own for each (base, class), whose
			hits take the damage table's multiplier for the class, h2_damage.h) */
			if (species == NONE && !getenv("HALO_H2_DAMAGE_AS_IS"))
			{
				struct h2_cache_tag h;
				long hm_count;
				unsigned long hm_block;

				if (h2_cache_tag_get(cache, biped->h2_hlmt_tag, &h) && h2_cache_block(cache, h.address + 0x58, 0x14, &hm_count, &hm_block))
				{
					long m2;

					for (m2 = 0; m2 < hm_count; m2++)
					{
						char other[64];

						name_of(cache, h2_cache_read32(cache, hm_block + 0x14 * (unsigned long)m2), other, sizeof(other));
						if (!strcmp(name, other))
						{
							short armor_class = h2_damage_armor_class(cache, h2_cache_read32(cache, hm_block + 0x14 * (unsigned long)m2 + 0xC));
							short base = material_map[i];

							if (getenv("HALO_H2_DEBUG"))
								platform_log("h2 bipeds: %s: material %s: class %d", biped->name, name, (int)armor_class);
							if (armor_class != NONE && base >= 0 && base < 3)
							{
								short made = material_append(host, root, base, armor_class, h2_damage_class_effect_type(armor_class), name);

								platform_log("h2 bipeds: %s: its %s a material of its own (%d), armor class %d", biped->name, name, (int)made,
									(int)armor_class);
								if (made != NONE)
									material_map[i] = made;
							}
							break;
						}
					}
				}
			}
			if (!strcmp(name, "shield"))
			{
				if (shield_material == NONE)
				{
					unsigned long field = root + offsetof(struct collision_model, resistance.materials);
					long count = (long)read32(host->at(c, field));
					unsigned long old = read32(host->at(c, field + 4));

					if (count > 1 && old >= host->link)
					{
						unsigned long copy = host->block(c, field, count + 1, sizeof(struct damage_resistance_material));
						struct damage_resistance_material *m;

						if (host->failed(c))
							return FALSE;
						memmove(host->at(c, copy), host->at(c, old - host->link), sizeof(struct damage_resistance_material) * (size_t)count);
						m = (struct damage_resistance_material *)host->at(c, copy + sizeof(struct damage_resistance_material) * (unsigned long)count);
						*m = *(struct damage_resistance_material *)host->at(c, copy + sizeof(struct damage_resistance_material));
						snprintf(m->name, sizeof(m->name), "shield");
						m->material_type = 30;      /* (the Elite's energy shield's impacts) */
						m->shield_leak_fraction = 0.0f;
						m->shield_damage_multiplier = 1.0f;
						m->body_damage_multiplier = 0.0f;
						shield_material = (short)count;
						platform_log("h2 bipeds: %s: its shield a material of its own (%d): hits on it absorbed", biped->name, shield_material);
					}
				}
				if (shield_material != NONE)
					material_map[i] = shield_material;
			}
		}
		/* (a model whose sections ignore shields - model_damage_info's flag:
		the Jackal's body and head -: every material but its arm shield's lets
		all the damage through to the body, so the shield is hit only there,
		and shows only there. Its shield comes back as its character says, the
		model's own times 0: vitality's shield recharge delay 0x48 and time 0x4C) */
		if (shield_material != NONE && material_count > 0 &&
			h2_damage_material_ignores_shields(cache, biped->h2_biped_tag, h2_cache_read32(cache, h2_materials)))
		{
			long count = (long)read32(host->at(c, root + offsetof(struct collision_model, resistance.materials)));
			unsigned long block = read32(host->at(c, root + offsetof(struct collision_model, resistance.materials) + 4));
			long character = h2_cache_tag_find(cache, 'char', "objects\\characters\\jackal\\ai\\jackal");
			struct h2_cache_tag t2;

			for (i = 0; i < count && block >= host->link; i++)
			{
				if (i != shield_material)
					((struct damage_resistance_material *)host->at(c, block - host->link + sizeof(struct damage_resistance_material) * i))->shield_leak_fraction = 1.0f;
			}
			if (character != NONE && h2_cache_tag_get(cache, character, &t2))
			{
				long vitality_count;
				unsigned long vitality;

				if (h2_cache_block(cache, t2.address + 0x3C, 0x70, &vitality_count, &vitality) && vitality_count > 0)
				{
					unsigned long delay_bits = h2_cache_read32(cache, vitality + 0x48), time_bits = h2_cache_read32(cache, vitality + 0x4C);
					float delay, time;

					unsigned long resistance = root + offsetof(struct collision_model, resistance);

					memcpy(&delay, &delay_bits, 4);
					memcpy(&time, &time_bits, 4);
					writef(host->at(c, resistance + offsetof(struct damage_resistance, shield_stun_time)), delay);
					writef(host->at(c, resistance + offsetof(struct damage_resistance, shield_recharge_time)), time);
					writef(host->at(c, resistance + offsetof(struct damage_resistance, runtime_shield_recharge_velocity)),
						time > 0.0f ? 1.0f / (time * 30.0f) : 0.0f);
				}
			}
			platform_log("h2 bipeds: %s: its body and head ignore its shield", biped->name);
		}
	}
	/* a splash's (an explosion's) hits: the damage info's indirect material's class
	(damage.c: the model's indirect damage material), the body's of it; a
	shield: its shield material's class */
	if (species == NONE && !getenv("HALO_H2_DAMAGE_AS_IS"))
	{
		struct h2_damage_info indirect;

		if (h2_damage_info_read(cache, biped->h2_biped_tag, &indirect))
		{
			short armor_class = h2_damage_armor_class(cache, indirect.body_material);
			short shield_class = h2_damage_armor_class(cache, indirect.shield_material);

			if (armor_class != NONE)
			{
				short index = material_append(host, root, 1, armor_class, h2_damage_class_effect_type(armor_class), "indirect");

				if (index != NONE)
				{
					write16(host->at(c, root + offsetof(struct collision_model, resistance.indirect_damage_material_index)), index);
					platform_log("h2 bipeds: %s: its indirect damage on its material %d (armor class %d)", biped->name, (int)index, (int)armor_class);
				}
			}
			if (shield_class != NONE)
			{
				write16(host->at(c, root + offsetof(struct collision_model, resistance.pad0)), shield_class + 1);
				platform_log("h2 bipeds: %s: its shield armor class %d", biped->name, (int)shield_class);
			}
		}
	}
	/* (the collision model's nodes: the model's of the same names - a node
	the model left out, its ancestor's) */
	{
		long coll_count, k2;
		unsigned long coll_nodes;

		for (i = 0; i < MAXIMUM_NODES; i++)
			coll_node[i] = NONE;
		if (h2_cache_block(cache, t.address + 0x2C, 0xC, &coll_count, &coll_nodes))
		{
			for (i = 0; i < coll_count && i < MAXIMUM_NODES; i++)
			{
				char name[64];

				name_of(cache, h2_cache_read32(cache, coll_nodes + 0xC * i), name, sizeof(name));
				for (k2 = 0; k2 < biped->h2_node_count; k2++)
				{
					if (!strcmp(biped->h2_node_names[k2], name))
						coll_node[i] = biped->node_remap[k2];
				}
			}
		}
	}
#define COLL_NODE(index) ((index) >= 0 && (index) < MAXIMUM_NODES ? coll_node[(index)] : NONE)
	/* (how many collision BSPs each node has: its region's base
	permutation's. A region of the model with several permutations - the
	Jackal's shield: active, major, giant, inactive, none - has Halo 2's
	collision for each (collision_model regions 0x1C: permutations 4, bsps 4,
	0x44 each, node 0): its nodes get one BSP slot per permutation of the
	model's region, in the model's order, the slot of a permutation without
	collision (none) empty; the collision test picks the slot by the object's
	permutation of that region (collision_models.c, region_permutation_indices),
	as Halo 2's does. Before this the first permutation's - the giant shield -
	stood for every Jackal: the shieldless sniper's shots hit it (body x0) and
	a minor's or major's kept it with its shield down) */
	memset(node_bsps, 0, sizeof(node_bsps));
	for (i = 0; i < MAXIMUM_NODES; i++)
		node_region[i] = 0, node_multi[i] = FALSE;
	for (i = 0; i < region_count; i++)
	{
		long permutation_count, bsp_count, p;
		unsigned long permutations, bsps;
		short model_region = NONE, mr;
		char region_name[64];

		if (!h2_cache_block(cache, regions + 0xC * i + 4, 0x14, &permutation_count, &permutations) || permutation_count < 1)
			continue;
		name_of(cache, h2_cache_read32(cache, regions + 0xC * i), region_name, sizeof(region_name));
		for (mr = 0; mr < biped->region_count && model_region == NONE; mr++)
		{
			if (!strcmp(biped->regions[mr].name, region_name))
				model_region = mr;
		}
		if (model_region != NONE && biped->regions[model_region].permutation_count > 1 && permutation_count > 1)
		{
			for (p = 0; p < permutation_count; p++)
			{
				if (!h2_cache_block(cache, permutations + 0x14 * p + 4, 0x44, &bsp_count, &bsps))
					continue;
				for (j = 0; j < bsp_count; j++)
				{
					short node = COLL_NODE(read16(h2_cache_at(cache, bsps + 0x44 * j, 2)));

					if (node >= 0 && node < biped->node_count && !node_multi[node])
					{
						node_multi[node] = TRUE;
						node_region[node] = model_region;
						node_bsps[node] = biped->regions[model_region].permutation_count;
					}
				}
			}
			platform_log("h2 bipeds: %s: region %s: its collision by permutation (%ld of the model's %ld)", biped->name, region_name,
				permutation_count, biped->regions[model_region].permutation_count);
			continue;
		}
		if (!h2_cache_block(cache, permutations + 4, 0x44, &bsp_count, &bsps))
			continue;
		for (j = 0; j < bsp_count; j++)
		{
			short node = COLL_NODE(read16(h2_cache_at(cache, bsps + 0x44 * j, 2)));

			if (node >= 0 && node < biped->node_count && !node_multi[node])
				node_bsps[node]++;
		}
	}
	/* Halo 2's damage sections that destroy a region of the model - a
Flood form's arms, head, torso -: each collision material's (by name, the
hlmt's materials 0x58, 0x14 each: damage section 6) section (the damage
info's 0x60, sections 0xBC, 0x38 each: vitality 8, instant responses 0xC,
0x50 each: region 0x1C, new state 0x20 - 4 destroyed) as this engine's
damage region - the model's region of that name, its damage threshold the
section's vitality: its "~damaged" permutation shown when it is reached
(damage.c) -, a collision node's region the one most of its surfaces'
materials name */
	{
		struct h2_cache_tag h;
		long hlmt_materials = 0, info_count = 0, section_count = 0;
		unsigned long hlmt_material_block = 0, info = 0, sections = 0;

		for (i = 0; i < MAXIMUM_MATERIALS; i++)
			material_region[i] = NONE;
		for (i = 0; i < CE_DAMAGE_REGIONS; i++)
			region_threshold[i] = 0.0f;
		damage_regions = FALSE;
		if (!getenv("HALO_H2_NO_DAMAGE_REGIONS") && h2_cache_tag_get(cache, biped->h2_hlmt_tag, &h) &&
			h2_cache_block(cache, h.address + 0x58, 0x14, &hlmt_materials, &hlmt_material_block) &&
			h2_cache_block(cache, h.address + 0x60, 0xF8, &info_count, &info) && info_count > 0 &&
			h2_cache_block(cache, info + 0xBC, 0x38, &section_count, &sections))
		{
			long material_count;
			unsigned long h2_materials;

			if (h2_cache_block(cache, t.address + 0x14, 4, &material_count, &h2_materials))
			{
				for (i = 0; i < material_count && i < MAXIMUM_MATERIALS; i++)
				{
					char name[64], other[64];
					long m, response_count, k2;
					short section = NONE;
					unsigned long responses;

					name_of(cache, h2_cache_read32(cache, h2_materials + 4 * i), name, sizeof(name));
					for (m = 0; m < hlmt_materials && section == NONE; m++)
					{
						name_of(cache, h2_cache_read32(cache, hlmt_material_block + 0x14 * (unsigned long)m), other, sizeof(other));
						if (!strcmp(name, other))
							section = read16(h2_cache_at(cache, hlmt_material_block + 0x14 * (unsigned long)m + 6, 2));
					}
					if (section < 0 || section >= section_count ||
						!h2_cache_block(cache, sections + 0x38 * (unsigned long)section + 0xC, 0x50, &response_count, &responses))
					{
						continue;
					}
					for (k2 = 0; k2 < response_count && material_region[i] == NONE; k2++)
					{
						unsigned long response = responses + 0x50 * (unsigned long)k2;
						short r;

						if (read16(h2_cache_at(cache, response + 0x20, 2)) != 4)
							continue;
						name_of(cache, h2_cache_read32(cache, response + 0x1C), other, sizeof(other));
						for (r = 0; r < biped->region_count && r < CE_DAMAGE_REGIONS && other[0]; r++)
						{
							if (biped->regions[r].destroyable && !strcmp(biped->regions[r].name, other))
							{
								float vitality = readf(h2_cache_at(cache, sections + 0x38 * (unsigned long)section + 8, 4));

								material_region[i] = r;
								region_threshold[r] = vitality > 0.0f && vitality < 1.0f ? vitality : 1.0f;
								damage_regions = TRUE;
								break;
							}
						}
					}
				}
			}
		}
		if (damage_regions)
		{
			/* (HALO_H2_EXTRA_REGIONS_OFF=1: only the object's datum's 8, as
			before the side table - the Enforcer's right shield generator and
			thruster, regions 8 and 9, never collide) */
			short r, region_total = (short)MIN(biped->region_count,
				getenv("HALO_H2_EXTRA_REGIONS_OFF") ? MAXIMUM_REGIONS_PER_OBJECT : CE_DAMAGE_REGIONS);
			unsigned long out = host->block(c, root + offsetof(struct collision_model, resistance.regions), region_total, CE_DAMAGE_REGION_SIZE);

			for (r = 0; r < region_total && !host->failed(c); r++)
			{
				unsigned char *o = host->at(c, out + CE_DAMAGE_REGION_SIZE * r);

				name_copy(o, biped->regions[r].name);
				writef(o + 0x28, region_threshold[r]);
				host->reference(c, out + CE_DAMAGE_REGION_SIZE * r + 0x38, 'effe', 0xFFFFFFFFUL);
				host->block(c, out + CE_DAMAGE_REGION_SIZE * r + 0x48, 0, 0x34);
			}
			platform_log("h2 bipeds: %s: damage regions: %d of its model's regions, their damage sections' %s", biped->name,
				(int)region_total, "thresholds");
		}
		else if (!getenv("HALO_H2_SHIELD_REGION_OFF"))
		{
			/* (a node of a region past the stock model's regions - the
			Jackal's shield, region 1, its collision by permutation above -
			needs that region in the resistance, or the tag's check takes its
			index to none and no shot ever meets the shield (all 27 Marine
			rounds that struck a crouched, facing Jackal hit its body): the
			model's regions, the stock first one kept, the others without a
			damage threshold (never destroyed: Halo 2's shield section only
			absorbs, damage.c) */
			short r, needed = 0, region_total;
			long stock = (long)read32(host->at(c, root + offsetof(struct collision_model, resistance.regions)));

			for (i = 0; i < MAXIMUM_NODES; i++)
			{
				if (node_bsps[i] && node_multi[i] && node_region[i] + 1 > needed)
					needed = (short)(node_region[i] + 1);
			}
			region_total = (short)MIN(MIN(biped->region_count, getenv("HALO_H2_EXTRA_REGIONS_OFF") ? MAXIMUM_REGIONS_PER_OBJECT : CE_DAMAGE_REGIONS), MAX(needed, 0));
			if (region_total > stock && stock >= 1)
			{
				unsigned long field = root + offsetof(struct collision_model, resistance.regions);
				unsigned long old = read32(host->at(c, field + 4));
				unsigned char first[CE_DAMAGE_REGION_SIZE];
				unsigned long out;

				out = 0;
				if (old >= host->link)
				{
					memcpy(first, host->at(c, old - host->link), CE_DAMAGE_REGION_SIZE);
					out = host->block(c, field, region_total, CE_DAMAGE_REGION_SIZE);
				}
				for (r = 0; out && r < region_total && !host->failed(c); r++)
				{
					unsigned char *o = host->at(c, out + CE_DAMAGE_REGION_SIZE * r);

					if (r == 0)
					{
						memcpy(o, first, CE_DAMAGE_REGION_SIZE);
						continue;
					}
					name_copy(o, biped->regions[r].name);
					writef(o + 0x28, 0.0f);
					host->reference(c, out + CE_DAMAGE_REGION_SIZE * r + 0x38, 'effe', 0xFFFFFFFFUL);
					host->block(c, out + CE_DAMAGE_REGION_SIZE * r + 0x48, 0, 0x34);
				}
				platform_log("h2 bipeds: %s: resistance regions: %d (the stock model's %ld): its collision's regions", biped->name,
					(int)region_total, stock);
			}
		}
	}
	/* (a stub: nothing of its nodes, BSPs and spheres - h2_biped.h) */
	if (host->collision_stub)
		return !host->failed(c);
	nodes = host->block(c, root + 0x28C, biped->node_count, CE_COLLISION_NODE_SIZE);
	for (i = 0; i < biped->node_count && !host->failed(c); i++)
	{
		unsigned char *o = host->at(c, nodes + CE_COLLISION_NODE_SIZE * i);

		name_copy(o, biped->node_names[i]);
		write16(o + 0x20, node_bsps[i] ? node_region[i] : -1);
		write16(o + 0x22, biped->node_parent[i]);
		write16(o + 0x24, biped->node_sibling[i]);
		write16(o + 0x26, biped->node_child[i]);
		host->block(c, nodes + CE_COLLISION_NODE_SIZE * i + 0x34, 0, CE_COLLISION_BSP_SIZE);
	}
	for (i = 0; i < biped->node_count && !host->failed(c); i++)
	{
		unsigned long bsps_out;
		long written = 0, r;

		long votes[CE_DAMAGE_REGIONS];

		if (!node_bsps[i])
			continue;
		memset(votes, 0, sizeof(votes));
		bsps_out = host->block(c, nodes + CE_COLLISION_NODE_SIZE * i + 0x34, node_bsps[i], CE_COLLISION_BSP_SIZE);
		for (r = 0; r < region_count && !host->failed(c); r++)
		{
			long permutation_count, bsp_count, p;
			unsigned long permutations, bsps;
			char region_name[64];

			if (!h2_cache_block(cache, regions + 0xC * r + 4, 0x14, &permutation_count, &permutations) || permutation_count < 1)
				continue;
			name_of(cache, h2_cache_read32(cache, regions + 0xC * r), region_name, sizeof(region_name));
			/* (a node of a region with several permutations: only that
			region's, each permutation's BSP in the slot of the model's
			permutation of its name) */
			if (node_multi[i] && strcmp(biped->regions[node_region[i]].name, region_name))
				continue;
			for (p = 0; p < (node_multi[i] ? permutation_count : 1) && !host->failed(c); p++)
			{
			long slot = NONE;

			if (node_multi[i])
			{
				char permutation_name[64];
				long q;

				name_of(cache, h2_cache_read32(cache, permutations + 0x14 * p), permutation_name, sizeof(permutation_name));
				for (q = 0; q < biped->regions[node_region[i]].permutation_count && slot == NONE; q++)
				{
					if (!strcmp(biped->regions[node_region[i]].permutations[q].name, permutation_name))
						slot = q;
				}
				if (slot == NONE || slot >= node_bsps[i])
				{
					platform_log("h2 bipeds: %s: node %s: no permutation %s of region %s in the model", biped->name, biped->node_names[i],
						permutation_name, region_name);
					continue;
				}
			}
			if (!h2_cache_block(cache, permutations + 0x14 * p + 4, 0x44, &bsp_count, &bsps))
				continue;
			for (j = 0; j < bsp_count && (node_multi[i] || written < node_bsps[i]); j++)
			{
				long at = node_multi[i] ? slot : written;

				if (COLL_NODE(read16(h2_cache_at(cache, bsps + 0x44 * j, 2))) != i)
					continue;
				if (!host->collision_convert(c, bsps_out + CE_COLLISION_BSP_SIZE * at, bsps + 0x44 * j + 4, NONE))
					return FALSE;
				/* (the surfaces' materials: the stock model's) */
				{
					unsigned char *b = host->at(c, bsps_out + CE_COLLISION_BSP_SIZE * at);
					long surface_count = (long)read32(b + 12 * 5);
					unsigned long surfaces = read32(b + 12 * 5 + 4) - host->link;

					for (k = 0; k < surface_count; k++)
					{
						unsigned char *s = host->at(c, surfaces + 12 * k);
						short material = read16(s + 10);

						if (material >= 0 && material < MAXIMUM_MATERIALS && material_region[material] != NONE)
							votes[material_region[material]]++;
						write16(s + 10, material >= 0 && material < MAXIMUM_MATERIALS ? material_map[material] : 1);
					}
				}
				written++;
			}
			}
		}
		/* (its damage region: its surfaces' most) */
		if (damage_regions)
		{
			short r, best = NONE;

			for (r = 0; r < CE_DAMAGE_REGIONS; r++)
			{
				if (votes[r] && (best == NONE || votes[r] > votes[best]))
					best = r;
			}
			write16(host->at(c, nodes + CE_COLLISION_NODE_SIZE * i + 0x20), best != NONE && !node_multi[i] ? best : node_region[i]);
		}
	}
	/* pathfinding spheres: Halo 2's (0x24, 0x14 each: node, flags,
	centre, radius) */
	{
		long sphere_count;
		unsigned long h2_spheres;

		if (h2_cache_block(cache, t.address + 0x24, 0x14, &sphere_count, &h2_spheres))
		{
			spheres = host->block(c, root + 0x280, sphere_count, CE_PATHFINDING_SPHERE_SIZE);
			for (i = 0; i < sphere_count && !host->failed(c); i++)
			{
				unsigned char const *h = h2_cache_at(cache, h2_spheres + 0x14 * i, 0x14);
				unsigned char *o = host->at(c, spheres + CE_PATHFINDING_SPHERE_SIZE * i);

				if (!h)
					continue;
				write16(o, COLL_NODE(read16(h)) != NONE ? COLL_NODE(read16(h)) : 0);
				memcpy(o + 0x10, h + 4, 16);
			}
		}
	}
#undef COLL_NODE
	return !host->failed(c);
}

/* the biped: the stock multiplayer biped's root, its model, graph and
collision model these, its nodes' places in them */
/* the overshield shaders made: a biped tag's id and its overshield's
shader's (h2_bipeds_overshield_shader) */
static struct { unsigned long biped, shader; } overshields[NUMBER_OF_H2_BIPEDS];
static short overshield_count = 0;

/* whether Halo 2's biped flies, as biped_write makes it fly: a vehicle its
AI flies (the Enforcer), a biped whose physics say so (a Sentinel's
flying flag, 0x10) or the Drone */
H2_ALIGN static boolean biped_flies_in(struct h2_cache *cache, struct made_biped const *biped)
{
	struct h2_cache_tag t;
	unsigned char const *h;
	unsigned long physics = biped->h2_group == 'crea' ? 0xD4 : 0x264;

	if (!strcmp(biped->name, "bugger"))
		return TRUE;
	if (biped->h2_biped_tag == NONE || !h2_cache_tag_get(cache, biped->h2_biped_tag, &t))
		return FALSE;
	if (biped->h2_group == 'vehi')
		return t.size >= 0x204 && h2_cache_at(cache, t.address, 0x204) != NULL;
	if (t.size < physics + 0x94 || !(h = h2_cache_at(cache, t.address, physics + 0x94)))
		return FALSE;
	return (read32(h + physics) & 0x10) != 0;
}

H2_ALIGN static boolean biped_flies(struct h2_biped_host const *host, struct made_biped const *biped)
{
	return biped_flies_in(host->cache, biped);
}

/* a made biped's own values Halo 2's, on its root o: its shape (collision
pill, camera heights, bounding radius), flight, jump, grenades, landings,
slopes, turning, autoaim... - as made (biped_write) and as loaded
(h2_bipeds_values_map_loaded), the same writes */
H2_ALIGN static void biped_values_write(struct h2_cache *cache, struct made_biped const *biped, unsigned char *o)
{
	/* Halo 2's own shape and flight - its biped's or creature's character
	physics (a biped's at 0x264, a creature's at 0xD4: flags - bit 4
	flying -, the collision pill's heights and radius at 4..0xC, the slopes
	at 0x38.., the flight at 0x68: bank angle, its apply and decay times,
	pitch ratio, speed, sidestep speed, acceleration, deceleration, turn
	rate and acceleration, the crouched speed) -, the camera's heights (a
	biped's, 0x218 and 0x21C) and the object's bounding radius (4); the rest
	- speeds, jumps, landing, turning, autoaim - this engine's, as the stock
	multiplayer biped's (stand_ins.md). A flier (a Drone, a Sentinel, the
	Monitor) flies as Halo 2's does */
	{
		struct h2_cache_tag t;
		unsigned char const *h;
		unsigned long physics = biped->h2_group == 'crea' ? 0xD4 : 0x264;

		/* (a vehicle its AI flies - the Enforcer -: a sphere its bounding
		radius's half, flying at its vehicle's top speed - forward 0x1F4,
		acceleration and deceleration 0x1FC and 0x200 - turning a quarter
		turn a second, unbanked) */
		if (biped->h2_group == 'vehi' && biped->h2_biped_tag != NONE && h2_cache_tag_get(cache, biped->h2_biped_tag, &t) &&
			t.size >= 0x204 && (h = h2_cache_at(cache, t.address, 0x204)) != NULL)
		{
			struct biped_definition *d = (struct biped_definition *)o;
			float bounding = readf(h + 4), speed = readf(h + 0x1F4), acceleration = readf(h + 0x1FC), deceleration = readf(h + 0x200);

			if (bounding > 0.1f && bounding < 8.0f)
			{
				writef(o + offsetof(struct biped_definition, object.bounding_radius), bounding);
				writef(o + offsetof(struct biped_definition, biped.collision_radius), 0.5f * bounding);
				writef(o + offsetof(struct biped_definition, biped.collision_height_standing), bounding);
				writef(o + offsetof(struct biped_definition, biped.collision_height_crouching), bounding);
			}
			SET_FLAG(d->biped.flags, _biped_flying_bit, TRUE);
			d->biped.flying_bank_amount = 0.0f;
			d->biped.flying_bank_apply_time = 0.3f;
			d->biped.flying_bank_decay_time = 0.3f;
			d->biped.flying_pitch_amount = 0.0f;
			d->biped.flying_velocity = speed > 0.0f && speed < 20.0f ? speed : 1.0f;
			d->biped.flying_sidestep_velocity = d->biped.flying_velocity;
			d->biped.flying_acceleration = acceleration > 0.0f && acceleration < 50.0f ? acceleration : 4.0f;
			d->biped.flying_deceleration = deceleration > 0.0f && deceleration < 50.0f ? deceleration : 4.0f;
			d->biped.flying_angular_velocity = 1.5708f;
			d->biped.flying_angular_acceleration = 3.1416f;
			d->biped.flying_crouch_velocity_modifier = 1.0f;
			platform_log("h2 bipeds: %s: a vehicle of Halo 2's, flies (%.2f a second, acceleration %.1f/%.1f), radius %.2f", biped->name,
				d->biped.flying_velocity, d->biped.flying_acceleration, d->biped.flying_deceleration, 0.5f * bounding);
		}
		else if (biped->h2_biped_tag != NONE && h2_cache_tag_get(cache, biped->h2_biped_tag, &t) && t.size >= physics + 0x94 &&
			(h = h2_cache_at(cache, t.address, physics + 0x94)) != NULL)
		{
			float standing = readf(h + physics + 4), crouching = readf(h + physics + 8), radius = readf(h + physics + 0xC);
			float camera_standing = physics == 0x264 ? readf(h + 0x218) : 0.0f, camera_crouching = physics == 0x264 ? readf(h + 0x21C) : 0.0f;
			float bounding = readf(h + 4);
			unsigned long physics_flags = read32(h + physics);

			if (standing > 0.2f && standing < 2.0f && crouching > 0.1f && crouching <= standing && radius > 0.05f && radius < 1.0f)
			{
				writef(o + offsetof(struct biped_definition, biped.collision_height_standing), standing);
				writef(o + offsetof(struct biped_definition, biped.collision_height_crouching), crouching);
				writef(o + offsetof(struct biped_definition, biped.collision_radius), radius);
			}
			else if (radius > 0.05f && radius < 2.0f && standing == 0.0f)
			{
				/* (a sphere - Halo 2's "shape spherical", a Sentinel, the
				Monitor -: its pill as tall as it is wide) */
				writef(o + offsetof(struct biped_definition, biped.collision_height_standing), 2.0f * radius);
				writef(o + offsetof(struct biped_definition, biped.collision_height_crouching), 2.0f * radius);
				writef(o + offsetof(struct biped_definition, biped.collision_radius), radius);
			}
			if (camera_standing > 0.2f && camera_standing < 2.0f && camera_crouching > 0.1f && camera_crouching <= camera_standing)
			{
				writef(o + offsetof(struct biped_definition, biped.standing_camera_height), camera_standing);
				writef(o + offsetof(struct biped_definition, biped.crouching_camera_height), camera_crouching);
			}
			if (bounding > 0.1f && bounding < 8.0f)
			{
				int k;

				writef(o + offsetof(struct biped_definition, object.bounding_radius), bounding);
				/* (and its bounding offset, 8: Halo 2's sphere centre is its root
				node - the pelvis - times this offset, 0xbdef0, as this engine's
				objects.c does with node 0; this engine's stock biped's offset
				put the centre 0.4 up) */
				for (k = 0; k < 3; k++)
				{
					float v = readf(h + 8 + 4 * k);

					writef(o + offsetof(struct biped_definition, object.bounding_offset) + 4 * k, v > -20.0f && v < 20.0f ? v : 0.0f);
				}
			}
			/* (a creature's own flags, the word at its creature fields' start 0xBC: bit 2 "immune to falling damage" - the
			infection forms, the Sentinel constructor -: this engine's biped flag; 04b's swarms had died of the drops they
			leap down, the real engine's taking none) */
			if (biped->h2_group == 'crea')
			{
				SET_FLAG(((struct biped_definition *)o)->biped.flags, _biped_immune_to_falling_damage_bit, (h[0xBC] & 4) != 0);
				platform_log("h2 bipeds: %s: creature flags %02x%s", biped->name, (unsigned)h[0xBC], (h[0xBC] & 4) ? ", immune to falling damage" : "");
			}
			if ((physics_flags & 0x10) || !strcmp(biped->name, "bugger"))
			{
				struct biped_definition *d = (struct biped_definition *)o;
				float const *f = (float const *)(h + physics + 0x68);
				float flight[11];
				int k;

				memcpy(flight, f, sizeof(flight));
				for (k = 0; k < 11; k++)
				{
					if (!(flight[k] == flight[k]) || flight[k] < 0.0f || flight[k] > 100.0f)
						flight[k] = 0.0f;
				}
				SET_FLAG(d->biped.flags, _biped_flying_bit, TRUE);
				d->biped.flying_bank_amount = flight[0];
				d->biped.flying_bank_apply_time = flight[1];
				d->biped.flying_bank_decay_time = flight[2];
				d->biped.flying_pitch_amount = flight[3];
				d->biped.flying_velocity = flight[4];
				d->biped.flying_sidestep_velocity = flight[5];
				d->biped.flying_acceleration = flight[6];
				d->biped.flying_deceleration = flight[7];
				d->biped.flying_angular_velocity = flight[8];
				d->biped.flying_angular_acceleration = flight[9];
				d->biped.flying_crouch_velocity_modifier = flight[10];
				platform_log("h2 bipeds: %s: flies (%.2f a second, sidestep %.2f, acceleration %.1f/%.1f, turns %.0f degrees a second)",
					biped->name, flight[4], flight[5], flight[6], flight[7], flight[8] * 57.29578f);
			}
			if (getenv("HALO_H2_DEBUG"))
				platform_log("h2 bipeds: %s: collision %.3f/%.3f radius %.3f, camera %.3f/%.3f, bounding %.3f, physics flags %lx", biped->name,
					standing, crouching, radius, camera_standing, camera_crouching, bounding, physics_flags);
		}
	}
	/* and its movement, from the same tag: the jump (3.08 world units a
	second, a tick's share here: gravity is Halo 2's on its levels,
	h2_player_physics.c, so it rises 1.14), the landings (the soft one's
	time and speeds, the hard one's time and speeds a second as this engine
	reads them), the steepest slope walked (50 degrees) and the slopes'
	speed (downhill 1.5, uphill 0.5, from 45 degrees to the cutoff at 50:
	their cosines and sines at 0x2B8..0x2C8, the engine's runtime values),
	and how wide the autoaim is (the Spartan 0, the Elite 0.05) */
	{
		struct h2_cache_tag t;
		unsigned char const *h;

		if (!getenv("HALO_H2_STOCK_PLAYER_PHYSICS") && biped->h2_biped_tag != NONE && biped->h2_group == 'bipd' &&
			h2_cache_tag_get(cache, biped->h2_biped_tag, &t) && t.size >= 0x2CC &&
			(h = h2_cache_at(cache, t.address, 0x2CC)) != NULL)
		{
			float jump = readf(h + 0x1F8), slope = readf(h + 0x29C), downhill = readf(h + 0x2B0), uphill = readf(h + 0x2B4);
			float soft_time = readf(h + 0x1FC), hard_time = readf(h + 0x200), soft_velocity = readf(h + 0x204);
			float hard_velocity = readf(h + 0x208), hard_maximum = readf(h + 0x20C), death_velocity = readf(h + 0x210);
			float autoaim = readf(h + 0x23C);
			struct biped_definition *d = (struct biped_definition *)o;

			if (getenv("HALO_H2_DEBUG"))
				platform_log("h2 bipeds: %s: stock jump %.4f landing %.2f/%.2f %.2f/%.2f/%.2f slope %.3f %.2f/%.2f k %.4f %.4f %.4f %.4f %.4f autoaim %.3f",
					biped->name, d->biped.jump_velocity, d->biped.maximum_soft_landing_time, d->biped.maximum_hard_landing_time,
					d->biped.minimum_soft_landing_velocity, d->biped.minimum_hard_landing_velocity, d->biped.maximum_hard_landing_velocity,
					d->biped.maximum_slope_angle, d->biped.downhill_velocity_scale, d->biped.uphill_velocity_scale,
					d->biped.runtime_minimum_normal_k, d->biped.runtime_downhill_k0, d->biped.runtime_downhill_k1,
					d->biped.runtime_uphill_k0, d->biped.runtime_uphill_k1, d->biped.autoaim_width);
			if (jump > 0.5f && jump < 10.0f)
				writef(o + offsetof(struct biped_definition, biped.jump_velocity), jump / 30.0f);
			/* the grenades: the throw's speed, the type (the Elite's plasma), the count */
			{
				float throw_speed = readf(h + 0x1B0);
				short grenade_type = (short)read16(h + 0x1B4), grenade_count = (short)read16(h + 0x1B6);

				if (throw_speed > 0.5f && throw_speed < 100.0f)
					writef(o + offsetof(struct biped_definition, unit.grenade_velocity), throw_speed);
				if (grenade_type >= 0 && grenade_type < 2 && grenade_count >= 0)
				{
					write16(o + offsetof(struct biped_definition, unit.grenade_type), (unsigned short)grenade_type);
					write16(o + offsetof(struct biped_definition, unit.grenade_count), (unsigned short)grenade_count);
				}
			}
			if (soft_time >= 0.0f && soft_time < 10.0f && hard_time >= 0.0f && hard_time < 10.0f && soft_velocity > 0.0f &&
				hard_velocity > soft_velocity && hard_maximum >= hard_velocity && hard_maximum < 100.0f && death_velocity >= 0.0f)
			{
				writef(o + offsetof(struct biped_definition, biped.maximum_soft_landing_time), soft_time);
				writef(o + offsetof(struct biped_definition, biped.maximum_hard_landing_time), hard_time);
				writef(o + offsetof(struct biped_definition, biped.minimum_soft_landing_velocity), soft_velocity);
				writef(o + offsetof(struct biped_definition, biped.minimum_hard_landing_velocity), hard_velocity);
				writef(o + offsetof(struct biped_definition, biped.maximum_hard_landing_velocity), hard_maximum);
				writef(o + offsetof(struct biped_definition, biped.death_hard_landing_velocity), death_velocity);
			}
			if (slope > 0.3f && slope < 1.5f && downhill > 0.0f && downhill < 4.0f && uphill > 0.0f && uphill < 4.0f)
			{
				writef(o + offsetof(struct biped_definition, biped.maximum_slope_angle), slope);
				writef(o + offsetof(struct biped_definition, biped.downhill_falloff_angle), readf(h + 0x2A0));
				writef(o + offsetof(struct biped_definition, biped.downhill_cutoff_angle), readf(h + 0x2A4));
				writef(o + offsetof(struct biped_definition, biped.uphill_falloff_angle), readf(h + 0x2A8));
				writef(o + offsetof(struct biped_definition, biped.uphill_cutoff_angle), readf(h + 0x2AC));
				writef(o + offsetof(struct biped_definition, biped.downhill_velocity_scale), downhill);
				writef(o + offsetof(struct biped_definition, biped.uphill_velocity_scale), uphill);
				writef(o + offsetof(struct biped_definition, biped.runtime_minimum_normal_k), readf(h + 0x2B8));
				writef(o + offsetof(struct biped_definition, biped.runtime_downhill_k0), readf(h + 0x2BC));
				writef(o + offsetof(struct biped_definition, biped.runtime_downhill_k1), readf(h + 0x2C0));
				writef(o + offsetof(struct biped_definition, biped.runtime_uphill_k0), readf(h + 0x2C4));
				writef(o + offsetof(struct biped_definition, biped.runtime_uphill_k1), readf(h + 0x2C8));
			}
			if (autoaim >= 0.0f && autoaim < 1.0f)
				writef(o + offsetof(struct biped_definition, biped.autoaim_width), autoaim);
			/* its turning (the moving turning speed at 0x1EC, radians a
			second; the stationary turning threshold at 0x1F4, the angle
			past which a standing biped turns its body - this engine keeps
			its cosine) and its crouch transition time (0x220; this engine
			keeps its velocity a tick), as the stock values' runtime fields
			were computed by the kit's tool (bipeds.c reads the runtime
			ones alone) */
			{
				float turning_speed = readf(h + 0x1EC), turning_threshold = readf(h + 0x1F4), crouch_time = readf(h + 0x220);

				if (getenv("HALO_H2_DEBUG"))
					platform_log("h2 bipeds: %s: stock turning %.4f rad/s threshold cos %.4f crouch transition %.3f s (%.4f a tick)",
						biped->name, d->biped.moving_turning_speed, d->biped.runtime_cosine_stationary_turning_threshold,
						d->biped.crouch_transition_time, d->biped.runtime_crouch_transition_velocity);
				if (turning_speed > 0.1f && turning_speed < 100.0f)
					writef(o + offsetof(struct biped_definition, biped.moving_turning_speed), turning_speed);
				if (turning_threshold > 0.0f && turning_threshold < 3.1416f)
				{
					writef(o + offsetof(struct biped_definition, biped.stationary_turning_threshold), turning_threshold);
					writef(o + offsetof(struct biped_definition, biped.runtime_cosine_stationary_turning_threshold), (float)cos(turning_threshold));
				}
				if (crouch_time > 0.01f && crouch_time < 5.0f)
				{
					writef(o + offsetof(struct biped_definition, biped.crouch_transition_time), crouch_time);
					writef(o + offsetof(struct biped_definition, biped.runtime_crouch_transition_velocity), 1.0f / (crouch_time * 30.0f));
				}
				if (getenv("HALO_H2_DEBUG"))
					platform_log("h2 bipeds: %s: now turning %.4f rad/s threshold cos %.4f crouch transition %.3f s (%.4f a tick)",
						biped->name, d->biped.moving_turning_speed, d->biped.runtime_cosine_stationary_turning_threshold,
						d->biped.crouch_transition_time, d->biped.runtime_crouch_transition_velocity);
			}
			/* (the object's acceleration scale - what a damage's
			instantaneous acceleration is multiplied by, damage.c: the
			Spartan 1.2, the Elite 1.3) */
			{
				float acceleration_scale = readf(h + 0x14);

				if (acceleration_scale >= 0.0f && acceleration_scale < 10.0f)
					writef(o + offsetof(struct biped_definition, object.acceleration_scale), acceleration_scale);
			}
			if (getenv("HALO_H2_DEBUG"))
				platform_log("h2 bipeds: %s: now jump %.4f (%.2f a second) landing %.2f/%.2f %.2f/%.2f/%.2f slope %.3f %.2f/%.2f k %.4f %.4f %.4f %.4f %.4f autoaim %.3f",
					biped->name, d->biped.jump_velocity, d->biped.jump_velocity * 30.0f, d->biped.maximum_soft_landing_time,
					d->biped.maximum_hard_landing_time, d->biped.minimum_soft_landing_velocity, d->biped.minimum_hard_landing_velocity,
					d->biped.maximum_hard_landing_velocity, d->biped.maximum_slope_angle, d->biped.downhill_velocity_scale,
					d->biped.uphill_velocity_scale, d->biped.runtime_minimum_normal_k, d->biped.runtime_downhill_k0,
					d->biped.runtime_downhill_k1, d->biped.runtime_uphill_k0, d->biped.runtime_uphill_k1, d->biped.autoaim_width);
		}
	}
}

/* its Halo 2 biped's (unit's) first camera track (its tracks at 0xE8, 8
bytes each, a 'trak' reference; the track's control points at 4, 0x1C
each: a position and an orientation): its control points kept, their count */
H2_ALIGN static long biped_camera_track_read(struct h2_cache *cache, struct made_biped *biped)
{
	struct h2_cache_tag t, track_tag;
	long tracks, track, point_count, n;
	unsigned long track_block, point_block;

	biped->camera_point_count = 0;
	if (biped->h2_biped_tag == NONE || !h2_cache_tag_get(cache, biped->h2_biped_tag, &t) ||
		!h2_cache_block(cache, t.address + 0xE8, 8, &tracks, &track_block) || tracks < 1 ||
		(track = h2_cache_reference(cache, track_block)) == NONE || !h2_cache_tag_get(cache, track, &track_tag) ||
		!h2_cache_block(cache, track_tag.address + 4, 0x1C, &point_count, &point_block))
	{
		return 0;
	}
	for (n = 0; n < point_count && n < 16; n++)
	{
		unsigned char const *p = h2_cache_at(cache, point_block + 0x1C * (unsigned long)n, 0x1C);
		int q;

		if (!p)
			return 0;
		for (q = 0; q < 7; q++)
			memcpy(&biped->camera_points[n][q], p + 4 * q, 4);
	}
	biped->camera_point_count = n;
	platform_log("h2 bipeds: %s: its camera track %s, %ld points (level %.2f back, %.2f up)", biped->name, h2_cache_tag_name(cache, track), n,
		n ? biped->camera_points[n / 2][0] : 0.0f, n ? biped->camera_points[n / 2][2] : 0.0f);
	return n;
}

/* (h2_bipeds_write) its camera track's tag written */
H2_ALIGN static void biped_camera_track_write(struct h2_biped_host const *host, struct made_biped const *biped)
{
	void *c = host->context;
	unsigned long root, points;
	long n;

	if (biped->camera_local == NONE)
		return;
	root = host->alloc(c, 0x30);
	if (host->failed(c))
		return;
	points = host->block(c, root + 4, biped->camera_point_count, 0x3C);
	for (n = 0; n < biped->camera_point_count && !host->failed(c); n++)
	{
		int q;

		for (q = 0; q < 7; q++)
			memcpy(host->at(c, points + 0x3C * (unsigned long)n + 4 * (unsigned long)q), &biped->camera_points[n][q], 4);
	}
	host->set_root(c, biped->first_tag + biped->camera_local, root);
}

H2_ALIGN static boolean biped_write(struct h2_biped_host const *host, struct made_biped *biped, struct made_biped const *model,
	long graph_tag, long kit_biped, boolean collision_made)
{
	void *c = host->context;
	unsigned long kit_root = kit_biped != NONE ? host->root_of(c, kit_biped) : 0;
	unsigned long root;
	unsigned char *o;
	short n;

	if (!kit_root)
		return FALSE;
	root = host->alloc(c, sizeof(struct biped_definition));
	if (host->failed(c))
		return FALSE;
	memcpy(host->at(c, root), host->at(c, kit_root), sizeof(struct biped_definition));
	deep_copy(host, halo_tag_layout_root('bipd'), root, 0);
	host->set_root(c, biped->first_tag + _tag_biped, root);
	if (model->overshield_shader != NONE && overshield_count < NUMBER_OF_H2_BIPEDS)
	{
		overshields[overshield_count].biped = host->tag_id(c, biped->first_tag + _tag_biped);
		overshields[overshield_count].shader = host->tag_id(c, model->first_tag + _tag_shaders + model->overshield_shader);
		overshield_count++;
	}
	host->reference(c, root + offsetof(struct biped_definition, object.model), 'mode', host->tag_id(c, model->first_tag + _tag_model));
	/* (its shield when hit: Halo 2's shield damaged shader as the modifier
	shader, in place of the kit cyborg's shield hit) */
	if (model->shield_hit_shader != NONE)
		host->reference(c, root + offsetof(struct biped_definition, object.modifier_shader), 'spla',
			host->tag_id(c, model->first_tag + _tag_shaders + model->shield_hit_shader));
	host->reference(c, root + offsetof(struct biped_definition, object.animation_graph), 'antr',
		host->tag_id(c, graph_tag));
	if (collision_made)
		host->reference(c, root + offsetof(struct biped_definition, object.collision_model), 'coll',
			host->tag_id(c, model->first_tag + _tag_collision));
	/* (its unit camera's track Halo 2's - the follow and death camera) */
	if (biped->shares == NONE && biped->camera_local != NONE)
	{
		unsigned long tracks = host->block(c, root + offsetof(struct biped_definition, unit.camera.unit_camera_tracks), 1, 28);

		if (!host->failed(c))
			host->reference(c, tracks, 'trak', host->tag_id(c, biped->first_tag + biped->camera_local));
	}
	o = host->at(c, root);
	biped_values_write(host->cache, biped, o);
	/* (its change colours: A and B white; C and D Halo 2's primary and
secondary as its tag gives them for each variant - the Chief's olive, the
Brutes' browns by rank, the Elites' blue, red, white... (h2_materials.h;
objects.c picks the object's variant's) - else C Halo 2's default armour,
olive (the stock multiplayer biped's left C black: a black Spartan) -
change colour block (0x2C each), permutations (0x1C: weight, lower, upper)) */
	/* (a campaign character carries nothing of its own: an actor's weapon is
	its variant's, a scene's its script's, the player's its starting profile's
	(players.c, player_add_equipment). The stand-in's rifle stood in 03a's
	pilots' and spotter's hands, and - the Master Chief's biped too - a battle
	rifle in every cutscene Chief's beside the weapon the scene gives him:
	05a's chief_intro climbing out of his pod with it and the SMG) */
	if (strstr(h2_cache_scenario_path(host->cache), "\\solo\\"))
		host->block(c, root + offsetof(struct biped_definition, unit.initial_weapons), 0, 36);
	if (!getenv("HALO_H2_BIPED_STOCK_COLOURS"))
	{
		static float const defaults[3][3] = { { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 0.36f, 0.40f, 0.22f } };
		struct h2_change_colours *h2 = malloc(sizeof(*h2));
		boolean from_tag = h2 && !getenv("HALO_H2_BIPED_OLD_COLOURS") && h2_change_colours_read(host->cache, biped->h2_biped_tag, h2);
		int slots = from_tag && h2->count[3] ? 4 : 3;
		unsigned long colours = host->block(c, root + offsetof(struct biped_definition, object.change_colors), slots, 0x2C);
		short shield_function = 0;
		int k, j;

		/* (a glow by the shields - the Elite's lights: change colour A, white,
		darkened by an incoming function of the shields' vitality - its own,
		else a free one made so) */
		for (k = 0; k < model->shader_count && !shield_function; k++)
		{
			if (model->shaders[k].illum_shield && model->shaders[k].illum)
			{
				unsigned char *d = host->at(c, root);
				short *modes = (short *)(d + offsetof(struct biped_definition, object.function_modes));

				for (j = 0; j < 4 && !shield_function; j++)
				{
					if (read16((unsigned char *)&modes[j]) == 2)   /* (objects.h: _object_function_shield_vitality) */
						shield_function = (short)(j + 1);
				}
				for (j = 0; j < 4 && !shield_function; j++)
				{
					if (read16((unsigned char *)&modes[j]) == 0)
					{
						write16((unsigned char *)&modes[j], 2);   /* shield vitality */
						shield_function = (short)(j + 1);
					}
				}
			}
		}
		for (k = 0; k < slots && !host->failed(c); k++)
		{
			long count = from_tag && k >= 2 ? h2->count[k] : 1, p;
			unsigned long permutations = count ? host->block(c, colours + 0x2C * k + 0x20, count, 0x1C) : 0;

			if (host->failed(c))
				break;
			o = host->at(c, colours + 0x2C * k);
			if (k == 0 && shield_function)
				write16(o, shield_function);   /* darken by */
			for (j = 0; j < 3; j++)
			{
				writef(o + 8 + 4 * j, defaults[k < 3 ? k : 0][j]);
				writef(o + 0x14 + 4 * j, defaults[k < 3 ? k : 0][j]);
			}
			for (p = 0; p < count; p++)
			{
				o = host->at(c, permutations + 0x1C * p);
				if (from_tag && k >= 2)
				{
					struct h2_change_colour_permutation const *h = &h2->permutations[k][p];

					writef(o, h->weight);
					for (j = 0; j < 3; j++)
					{
						writef(o + 4 + 4 * j, h->lower[j]);
						writef(o + 0x10 + 4 * j, h->upper[j]);
					}
				}
				else
				{
					writef(o, 1.0f);
					for (j = 0; j < 3; j++)
					{
						writef(o + 4 + 4 * j, defaults[k][j]);
						writef(o + 0x10 + 4 * j, defaults[k][j]);
					}
				}
			}
		}
		if (from_tag && getenv("HALO_H2_DEBUG"))
			platform_log("h2 bipeds: %s: change colours C %d, D %d permutations (by variant)", biped->name, (int)h2->count[2],
				(int)h2->count[3]);
		free(h2);
		o = host->at(c, root);
	}
	/* (its pelvis and head among its nodes) */
	write16(o + offsetof(struct biped_definition, biped.runtime_pelvis_node_index), 0);
	write16(o + offsetof(struct biped_definition, biped.runtime_head_node_index), 0);
	for (n = 0; n < model->node_count; n++)
	{
		if (!strcmp(model->node_names[n], "pelvis"))
			write16(o + offsetof(struct biped_definition, biped.runtime_pelvis_node_index), n);
		if (!strcmp(model->node_names[n], "head"))
			write16(o + offsetof(struct biped_definition, biped.runtime_head_node_index), n);
	}
	return !host->failed(c);
}

/* (HALO_H2_DEBUG, Linux) the C library heap's bytes in use: what the
gathering costs */
#if defined(__GLIBC__) && !defined(HALO_VITA)
struct h2_biped_mallinfo2 { size_t arena, ordblks, smblks, hblks, hblkhd, usmblks, fsmblks, uordblks, fordblks, keepcost; };
extern struct h2_biped_mallinfo2 h2_biped_mallinfo2(void) __asm__("mallinfo2");
static unsigned long heap_in_use(void)
{
	struct h2_biped_mallinfo2 info = h2_biped_mallinfo2();

	return (unsigned long)(info.uordblks + info.hblkhd);
}
#else
static unsigned long heap_in_use(void)
{
	return 0;
}
#endif

/* ---------- public code */

/* ---------- the bipeds' graphs made ahead (piece H: the make's load time)

A biped's animation graph is the most of its gather (h2_graph_make: Halo 2's
animations made this engine's, a second of Lockout's make on Vita3K), and
depends on nothing but the map, the biped's Halo 2 graph and its model's
nodes. h2_bipeds_graphs_ahead_begin, as the make begins, makes each biped's
graph on a thread of its own over a view of the map (h2_cache_open_view)
while the make's thread gathers the rest; the gather takes one only if it
was made from the very graph, nodes (names, parents, poses, bit for bit)
and flight the gather's own are - else it makes it as before. Graphs not
taken are let go at h2_bipeds_graphs_ahead_end. HALO_H2_GRAPHS_AHEAD=0:
none; on by default. */
enum
{
	GRAPHS_AHEAD_VIEW_BUDGET = 4 * 1024 * 1024,
	GRAPHS_AHEAD_STACK_BYTES = 1024 * 1024
};

struct graph_ahead
{
	int wanted, done;               /* (done: atomic, the thread's) */
	long jmad_tag;
	short node_count;
	int flies;
	char names[MAXIMUM_NODES][32];
	struct h2_animation_pose defaults[MAXIMUM_NODES];
	short parents[MAXIMUM_NODES];
	struct h2_graph *graph;
};

#include <pthread.h>

boolean h2_level_switch(char const *name, boolean default_on);
unsigned long long vita_host_time_us(void);

static pthread_mutex_t graphs_ahead_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t graphs_ahead_made = PTHREAD_COND_INITIALIZER;

static struct
{
	boolean running;
	pthread_t thread;
	struct h2_cache *view;
	struct graph_ahead *items;      /* NUMBER_OF_H2_BIPEDS */
	struct made_biped *scratch;
	unsigned long long started, finished;
	long made, taken, dropped;
	int stop;                       /* (atomic: the build's heap had no room - none made more, none kept) */
} graphs_ahead;

H2_ALIGN static void graph_ahead_done(struct graph_ahead *g)
{
	pthread_mutex_lock(&graphs_ahead_lock);
	__atomic_store_n(&g->done, 1, __ATOMIC_RELEASE);
	pthread_cond_broadcast(&graphs_ahead_made);
	pthread_mutex_unlock(&graphs_ahead_lock);
}

/* (the thread) the graph of each biped the gather would make, as it would */
H2_ALIGN static void *graphs_ahead_thread(void *argument)
{
	struct h2_cache *cache = graphs_ahead.view;
	struct made_biped *biped = graphs_ahead.scratch;
	short b, e;

	(void)argument;
	for (b = 0; b < NUMBER_OF_H2_BIPEDS; b++)
	{
		struct graph_ahead *g = &graphs_ahead.items[b];
		struct h2_cache_tag t, hlmt, model;
		long hlmt_tag;
		char why[160];

		if (!g->wanted || __atomic_load_n(&graphs_ahead.stop, __ATOMIC_ACQUIRE))
		{
			g->wanted = FALSE;
			graph_ahead_done(g);
			continue;
		}
		memset(biped, 0, sizeof(*biped));
		snprintf(biped->name, sizeof(biped->name), "%s", biped_sources[b].name);
		biped->h2_group = biped_sources[b].group;
		biped->h2_biped_tag = h2_bipeds_made_from(cache, b);
		/* (what an allocation that gets no memory was for: h2_build_heap_unit) */
		{
			char unit[96];

			snprintf(unit, sizeof(unit), "the %s biped's animation graph (made ahead)", biped->name);
			h2_build_heap_unit(unit);
		}
		if (biped->h2_biped_tag == NONE || !h2_cache_tag_get(cache, biped->h2_biped_tag, &t) ||
			(hlmt_tag = h2_cache_reference(cache, t.address + 0x34)) == NONE || !h2_cache_tag_get(cache, hlmt_tag, &hlmt) ||
			!model_nodes_read(cache, biped, h2_cache_reference(cache, hlmt.address), &model) || biped->node_count > RENDERER_NODES)
		{
			g->wanted = FALSE;
			graph_ahead_done(g);
			continue;
		}
		g->jmad_tag = h2_cache_reference(cache, hlmt.address + 0x10);
		g->node_count = biped->node_count;
		g->flies = biped_flies_in(cache, biped);
		memcpy(g->names, biped->node_names, sizeof(g->names[0]) * (size_t)g->node_count);
		memcpy(g->defaults, biped->node_default, sizeof(g->defaults[0]) * (size_t)g->node_count);
		memcpy(g->parents, biped->node_parent, sizeof(g->parents[0]) * (size_t)g->node_count);
		/* (one an earlier biped's graph will stand for - the same graph and
		skeleton -: not made twice) */
		for (e = 0; e < b; e++)
		{
			struct graph_ahead const *f = &graphs_ahead.items[e];

			if (f->wanted && f->jmad_tag == g->jmad_tag && f->node_count == g->node_count && f->flies == g->flies &&
				!memcmp(f->names, g->names, sizeof(g->names[0]) * (size_t)g->node_count) &&
				!memcmp(f->parents, g->parents, sizeof(g->parents[0]) * (size_t)g->node_count))
			{
				break;
			}
		}
		if (e == b)
		{
			struct h2_graph *made = h2_graph_make(cache, g->jmad_tag, (char const (*)[32])g->names, g->defaults, g->parents, g->node_count,
				g->flies, why, sizeof(why));

			/* (the heap short meanwhile: not kept - the gather makes it) */
			pthread_mutex_lock(&graphs_ahead_lock);
			if (made && __atomic_load_n(&graphs_ahead.stop, __ATOMIC_ACQUIRE))
			{
				h2_graph_free(made);
				made = NULL;
				g->wanted = FALSE;
				graphs_ahead.dropped++;
			}
			g->graph = made;
			pthread_mutex_unlock(&graphs_ahead_lock);
			if (g->graph)
				graphs_ahead.made++;
		}
		graph_ahead_done(g);
	}
	graphs_ahead.finished = vita_host_time_us();
	/* (its view let go as soon as the graphs are made: the chunks it
	inflated - 4 MB at most - not held into the make's peak; the graphs read
	nothing through it again, the gather's open given them as they are taken) */
	pthread_mutex_lock(&graphs_ahead_lock);
	h2_cache_close(graphs_ahead.view);
	graphs_ahead.view = NULL;
	pthread_mutex_unlock(&graphs_ahead_lock);
	return NULL;
}

/* (the build's heap had no room: h2_build_heap_pressure_add) the graphs made
and not taken let go, and none made more - the gather makes them as before:
what the make needs comes first. Nothing if the gather is at the list */
H2_ALIGN static void graphs_ahead_release(void)
{
	short b;

	__atomic_store_n(&graphs_ahead.stop, 1, __ATOMIC_RELEASE);
	if (pthread_mutex_trylock(&graphs_ahead_lock) != 0)
		return;
	for (b = 0; graphs_ahead.items && b < NUMBER_OF_H2_BIPEDS; b++)
	{
		struct graph_ahead *g = &graphs_ahead.items[b];

		if (__atomic_load_n(&g->done, __ATOMIC_ACQUIRE) && g->graph)
		{
			h2_graph_free(g->graph);
			g->graph = NULL;
			g->wanted = FALSE;
			graphs_ahead.dropped++;
		}
	}
	pthread_mutex_unlock(&graphs_ahead_lock);
}

H2_ALIGN void h2_bipeds_graphs_ahead_begin(
	struct h2_cache *cache)
{
	pthread_attr_t attributes;
	short b;
	int made;

	h2_bipeds_graphs_ahead_end();
	if (!cache || !h2_cache_is_mcc(cache) || getenv("HALO_H2_NO_BIPEDS") || !h2_level_switch("HALO_H2_GRAPHS_AHEAD", TRUE))
		return;
	/* (only with room for them besides the make's: the graphs a mission's
	bipeds have are 10 MB and more - made ahead into a heap with no room they
	only raised the make's peak, Old Mombasa's +5 MB with the harness's 45 MB;
	HALO_H2_GRAPHS_AHEAD_MB, 16) */
	{
		char const *setting = getenv("HALO_H2_GRAPHS_AHEAD_MB");
		unsigned long wanted = (setting ? (unsigned long)atol(setting) : 16UL) * 1024UL * 1024UL, left = h2_build_heap_room_left();

		if (left < wanted)
		{
			platform_log("h2 bipeds: the graphs not made ahead: the build's heap has %lu KB left (%lu KB wanted)", left / 1024,
				wanted / 1024);
			return;
		}
	}
	graphs_ahead.items = calloc(NUMBER_OF_H2_BIPEDS, sizeof(*graphs_ahead.items));
	graphs_ahead.scratch = calloc(1, sizeof(*graphs_ahead.scratch));
	graphs_ahead.view = graphs_ahead.items && graphs_ahead.scratch ? h2_cache_open_view(cache, GRAPHS_AHEAD_VIEW_BUDGET) : NULL;
	if (!graphs_ahead.view)
	{
		h2_bipeds_graphs_ahead_end();
		return;
	}
	/* (the bipeds the gather makes: h2_bipeds_gather's choice, a biped of
	another's model left to it) */
	for (b = 0; b < NUMBER_OF_H2_BIPEDS; b++)
	{
		graphs_ahead.items[b].wanted = biped_sources[b].shares == NONE &&
			!(getenv("HALO_H2_BIPEDS") && !strstr(getenv("HALO_H2_BIPEDS"), biped_sources[b].name)) &&
			!(b >= H2_BIPED_FIRST_CAMPAIGN && (!strstr(h2_cache_scenario_path(cache), "\\solo\\") || getenv("HALO_H2_NO_AI_BIPEDS")));
	}
	graphs_ahead.started = vita_host_time_us();
	pthread_attr_init(&attributes);
	pthread_attr_setstacksize(&attributes, GRAPHS_AHEAD_STACK_BYTES);
	made = pthread_create(&graphs_ahead.thread, &attributes, graphs_ahead_thread, NULL);
	pthread_attr_destroy(&attributes);
	if (made != 0)
	{
		h2_bipeds_graphs_ahead_end();
		return;
	}
	graphs_ahead.running = TRUE;
	h2_build_heap_pressure_add(graphs_ahead_release);
}

/* (the gather) biped b's graph made ahead, if made from these very inputs
(then the gather's: read through its open); NULL: make it */
H2_ALIGN static struct h2_graph *graphs_ahead_take(short b, struct h2_cache *cache, struct made_biped const *biped)
{
	struct graph_ahead *g;
	struct h2_graph *graph;

	if (!graphs_ahead.running || b < 0 || b >= NUMBER_OF_H2_BIPEDS)
		return NULL;
	g = &graphs_ahead.items[b];
	pthread_mutex_lock(&graphs_ahead_lock);
	while (!__atomic_load_n(&g->done, __ATOMIC_ACQUIRE))
		pthread_cond_wait(&graphs_ahead_made, &graphs_ahead_lock);
	if (!g->wanted || !g->graph || g->jmad_tag != biped->h2_jmad_tag || g->node_count != biped->node_count ||
		g->flies != biped->flies || memcmp(g->names, biped->node_names, sizeof(g->names[0]) * (size_t)g->node_count) ||
		memcmp(g->defaults, biped->node_default, sizeof(g->defaults[0]) * (size_t)g->node_count) ||
		memcmp(g->parents, biped->node_parent, sizeof(g->parents[0]) * (size_t)g->node_count))
	{
		pthread_mutex_unlock(&graphs_ahead_lock);
		return NULL;
	}
	graph = g->graph;
	g->graph = NULL;
	pthread_mutex_unlock(&graphs_ahead_lock);
	h2_graph_cache_set(graph, cache);
	graphs_ahead.taken++;
	return graph;
}

H2_ALIGN void h2_bipeds_graphs_ahead_end(
	void)
{
	short b;

	if (graphs_ahead.running)
	{
		h2_build_heap_pressure_remove(graphs_ahead_release);
		pthread_join(graphs_ahead.thread, NULL);
		platform_log("h2 bipeds: the graphs made ahead: %ld made, %ld taken, %ld let go for the build's heap, in %.0f ms on their own "
			"thread", graphs_ahead.made, graphs_ahead.taken, graphs_ahead.dropped,
			(graphs_ahead.finished - graphs_ahead.started) / 1000.0);
	}
	/* (under the lock: a release under way on another thread finished first) */
	pthread_mutex_lock(&graphs_ahead_lock);
	for (b = 0; graphs_ahead.items && b < NUMBER_OF_H2_BIPEDS; b++)
		h2_graph_free(graphs_ahead.items[b].graph);
	free(graphs_ahead.items);
	graphs_ahead.items = NULL;
	pthread_mutex_unlock(&graphs_ahead_lock);
	free(graphs_ahead.scratch);
	if (graphs_ahead.view)
		h2_cache_close(graphs_ahead.view);
	memset(&graphs_ahead, 0, sizeof(graphs_ahead));
}

/* (the gather) biped b, not made: let go, the absent one in its place */
H2_ALIGN static void biped_slot_settle(struct h2_bipeds *bipeds, short b)
{
	struct made_biped *biped = bipeds->bipeds[b];

	if (biped == &biped_absent || biped->made)
		return;
	biped_free(biped);
	free(biped);
	bipeds->bipeds[b] = &biped_absent;
}

H2_ALIGN struct h2_bipeds *h2_bipeds_gather(
	struct h2_biped_host const *host)
{
	struct h2_bipeds *bipeds;
	short b;
	long tags = 0;
	unsigned long heap_before = heap_in_use();

	if (getenv("HALO_H2_NO_BIPEDS") || !h2_cache_is_mcc(host->cache))
		return NULL;
	bipeds = calloc(1, sizeof(*bipeds));
	if (!bipeds)
		return NULL;
	bipeds->cache = host->cache;
	for (b = 0; b < NUMBER_OF_H2_BIPEDS; b++)
		bipeds->bipeds[b] = &biped_absent;
	for (b = 0; b < NUMBER_OF_H2_BIPEDS; b++)
	{
		struct made_biped *biped;

		/* (the one before, not made: let go - only a made one's fields are read) */
		if (b > 0)
			biped_slot_settle(bipeds, (short)(b - 1));
		biped = calloc(1, sizeof(*biped));
		if (!biped)
		{
			h2_bipeds_free(bipeds);
			return NULL;
		}
		bipeds->bipeds[b] = biped;
		struct h2_cache_tag t, hlmt;
		long hlmt_tag;
		char why[160];

		if (b == 0)
			emblem_stand_in_bitmap = NONE;
		if (host->trim)
			host->trim(host->context);
		snprintf(biped->name, sizeof(biped->name), "%s", biped_sources[b].name);
		snprintf(biped->h2_biped, sizeof(biped->h2_biped), "%s", biped_sources[b].biped);
		biped->shares = NONE;
		biped->overshield_shader = NONE;
		biped->shield_hit_shader = NONE;
		biped->h2_group = biped_sources[b].group;
		if (getenv("HALO_H2_BIPEDS") && !strstr(getenv("HALO_H2_BIPEDS"), biped->name))
			continue;
		/* (the campaign's characters: on a campaign level alone) */
		if (b >= H2_BIPED_FIRST_CAMPAIGN && (!strstr(h2_cache_scenario_path(host->cache), "\\solo\\") ||
			getenv("HALO_H2_NO_AI_BIPEDS")))
		{
			continue;
		}
		/* (a campaign level's player is Halo 2's campaign one - the Master
		Chief, the Arbiter -, not the multiplayer Spartan and Elite its
		shared map also holds: the cutscenes' Chief and Arbiter are these
		too, h2_bipeds_for_unit_name; HALO_H2_MP_PLAYER_ON_CAMPAIGN keeps
		the multiplayer ones) */
		biped->h2_biped_tag = h2_bipeds_made_from(host->cache, b);
		if (biped->h2_biped_tag != NONE)
			snprintf(biped->h2_biped, sizeof(biped->h2_biped), "%s", h2_cache_tag_name(host->cache, biped->h2_biped_tag));
		if (biped->h2_biped_tag == NONE || !h2_cache_tag_get(host->cache, biped->h2_biped_tag, &t) ||
			(hlmt_tag = h2_cache_reference(host->cache, t.address + 0x34)) == NONE ||
			!h2_cache_tag_get(host->cache, hlmt_tag, &hlmt))
		{
			if (getenv("HALO_H2_DEBUG") || b < H2_BIPED_MIRANDA)
				platform_log("h2 bipeds: %s: no %s", biped->name, biped_sources[b].biped);
			continue;
		}
		/* (a biped of another's model, that one made: its tag alone, the
		other's model, graph and collision model in it) */
		if (biped_sources[b].shares != NONE && (*bipeds->bipeds[biped_sources[b].shares]).made &&
			(*bipeds->bipeds[biped_sources[b].shares]).h2_hlmt_tag == hlmt_tag)
		{
			struct made_biped const *base = &(*bipeds->bipeds[biped_sources[b].shares]);

			biped->shares = biped_sources[b].shares;
			biped->camera_local = NONE;
			biped->h2_hlmt_tag = hlmt_tag;
			biped->h2_model_tag = base->h2_model_tag;
			biped->h2_coll_tag = base->h2_coll_tag;
			biped->h2_jmad_tag = base->h2_jmad_tag;
			biped->made = TRUE;
			biped->first_tag = tags;
			biped->local_first_tag = tags;
			tags += 1;
			biped->report = base->report;
			snprintf(biped->report.name, sizeof(biped->report.name), "%s", biped->name);
			biped->report.animation_bytes = 0;
			biped->report.model_bytes = 0;
			biped->report.bitmap_bytes = 0;
			platform_log("h2 bipeds: %s: %s's model, graph and collision model", biped->name, base->name);
			continue;
		}
		biped->h2_hlmt_tag = hlmt_tag;
		/* (what an allocation that gets no memory was for: h2_build_heap_unit) */
		{
			char unit[96];

			snprintf(unit, sizeof(unit), "the %s biped (its model, graph and collision model)", biped->name);
			h2_build_heap_unit(unit);
		}
		biped->h2_model_tag = h2_cache_reference(host->cache, hlmt.address);
		biped->h2_coll_tag = h2_cache_reference(host->cache, hlmt.address + 0x08);
		biped->h2_jmad_tag = h2_cache_reference(host->cache, hlmt.address + 0x10);
		if (!model_make(host, biped, biped->h2_model_tag))
		{
			platform_log("h2 bipeds: %s: its render model %s could not be made", biped->name,
				biped->h2_model_tag != NONE ? h2_cache_tag_name(host->cache, biped->h2_model_tag) : "(none)");
			biped_free(biped);
			continue;
		}
		/* (a hologram - Halo 2's hlmt's hologram shader at 0xF0: the Heretic
		leader's, Regret's -: each of its shaders drawn as it, its own map's
		brightness in the hologram's tint, added) */
		{
			long hologram = h2_cache_reference(host->cache, hlmt.address + 0xF0);
			struct h2_cache_tag shader;

			char control[64];

			/* (its control function, at 0xF8: drawn whole as the hologram when
			it is always on - "one", Regret's; "hologram_control", the
			Heretic leader's -; Tartarus's, "invincibility", is his
			overshield's while he cannot be hurt: not his look) */
			name_of(host->cache, h2_cache_read32(host->cache, hlmt.address + 0xF8), control, sizeof(control));
			if (hologram != NONE && h2_cache_tag_get(host->cache, hologram, &shader) && shader.group_tag == 'shad' &&
				(!strcmp(control, "one") || strstr(control, "hologram")) && !getenv("HALO_H2_NO_HOLOGRAMS"))
			{
				float tint[3];
				long k;
				struct h2_hologram_pass pass;
				boolean real_pass = h2_hologram_pass_read(host->cache, hologram, &pass);

				shader_tint(host->cache, hologram, tint);
				for (k = 0; k < biped->shader_count; k++)
				{
					struct made_shader *s = &biped->shaders[k];

					if (s->skipped || s->plasma || s->h2_diffuse == NONE)
						continue;
					/* (Halo 2's hologram template: its pass, alpha blended) */
					s->base_bitmap = real_pass ? hologram_pass_make(host, biped->name, s->name, s->h2_diffuse, &pass) :
						tinted_make(host, biped->name, s->name, s->h2_diffuse, tint);
					s->transparent = TRUE;
					s->alpha_blended = real_pass;
					s->change_colour = FALSE;
					s->skipped = s->base_bitmap == NONE;
				}
				platform_log("h2 bipeds: %s: a hologram (%s, tint %.2f %.2f %.2f)", biped->name, h2_cache_tag_name(host->cache, hologram),
					tint[0], tint[1], tint[2]);
				if (real_pass)
					platform_log("h2 bipeds: %s: its hologram pass: invert %.3f value %.3f colour %.3f %.3f %.3f alpha %.3f", biped->name,
						pass.invert, pass.value, pass.colour[0], pass.colour[1], pass.colour[2], pass.alpha);
			}
		}
		/* (its overshield: the model's damage info (0x60, 0xF8 each) its
		overshield shader at 0xF0 (third person; 0xE8 the first person's,
		the same but for its brightness under camouflage) - a shader of its
		own, a plasma, if there is room) */
		{
			long info_count;
			unsigned long info;

			if (h2_cache_block(host->cache, hlmt.address + 0x60, 0xF8, &info_count, &info) && info_count > 0 &&
				h2_cache_reference(host->cache, info + 0xF0) != NONE && biped->shader_count < MAXIMUM_MATERIALS &&
				!getenv("HALO_H2_NO_OVERSHIELD"))
			{
				struct made_shader *s = &biped->shaders[biped->shader_count];

				memset(s, 0, sizeof(*s));
				s->plasma = TRUE;
				s->overshield = TRUE;
				s->base_bitmap = s->multipurpose_bitmap = s->detail_bitmap = s->cube_bitmap = s->bump_bitmap = NONE;
				s->h2_diffuse = NONE;
				snprintf(s->name, sizeof(s->name), "overshield");
				biped->overshield_shader = (short)biped->shader_count++;
			}
			/* (its shield when hit: the damage info's shield damaged shader at
			0x84 (third person; 0x7C the first person's) - Halo 2's
			plasma_shield over the whole model while the shield_hit function
			is up: this engine's object modifier shader (biped_write), a plasma
			of the kit's shield hit on the Vita, Halo 2's pass on the desktop
			(h2_plasma_shield.h); before, the kit cyborg's own shield hit -
			yellow-white lines on every character) */
			if (h2_cache_block(host->cache, hlmt.address + 0x60, 0xF8, &info_count, &info) && info_count > 0 &&
				biped->shader_count < MAXIMUM_MATERIALS && !getenv("HALO_H2_PLASMA_SHIELD_OLD"))
			{
				long shield = h2_cache_reference(host->cache, info + 0x84);
				struct h2_cache_tag st;
				long template_tag;
				char const *template_name;

				if (shield != NONE && h2_cache_tag_get(host->cache, shield, &st) &&
					(template_tag = h2_cache_reference(host->cache, st.address)) != NONE &&
					(template_name = h2_cache_tag_name(host->cache, template_tag)) != NULL && strstr(template_name, "\\plasma_shield"))
				{
					struct made_shader *s = &biped->shaders[biped->shader_count];

					memset(s, 0, sizeof(*s));
					s->h2_shader = shield;
					s->plasma = TRUE;
					s->base_bitmap = s->multipurpose_bitmap = s->detail_bitmap = s->cube_bitmap = s->bump_bitmap = NONE;
					s->h2_diffuse = NONE;
					shader_tint(host->cache, shield, s->plasma_colour);
					s->shield_pass_read = h2_plasma_shield_read(host->cache, shield, template_name, &s->shield_pass);
					snprintf(s->name, sizeof(s->name), "shield hit");
					biped->shield_hit_shader = (short)biped->shader_count++;
					if (getenv("HALO_H2_DEBUG"))
					{
						struct h2_plasma_shield_pass const *p = &s->shield_pass;

						platform_log("h2 bipeds: %s: shield hit %s (%s): pass %s, mode %d, wide %06lx..%06lx%s sharp %06lx..%06lx%s center %06lx..%06lx%s "
							"color %06lx..%06lx%s glancing %06lx..%06lx%s, shapes %d %d %d %d %d, scales %.0f %.0f, noise 0 x %s%.2f y %s%.2f z %s%.2f, "
							"noise 1 x %s%.2f y %s%.2f z %s%.2f, offset %s%.3f, input '%s'", biped->name, h2_cache_tag_name(host->cache, shield),
							template_name, s->shield_pass_read ? "read" : "NOT read", (int)p->mode,
							p->colour_low[0], p->colour_high[0], p->colour_by_input[0] ? "*" : "", p->colour_low[1], p->colour_high[1], p->colour_by_input[1] ? "*" : "",
							p->colour_low[2], p->colour_high[2], p->colour_by_input[2] ? "*" : "", p->colour_low[3], p->colour_high[3], p->colour_by_input[3] ? "*" : "",
							p->colour_low[4], p->colour_high[4], p->colour_by_input[4] ? "*" : "", p->colour_shape[0], p->colour_shape[1], p->colour_shape[2],
							p->colour_shape[3], p->colour_shape[4], p->scale[0], p->scale[1],
							p->translation_by_input[0][0] ? "*" : "T", p->translation_by_input[0][0] ? p->translation_high[0][0] : p->translation_period[0][0],
							p->translation_by_input[0][1] ? "*" : "T", p->translation_by_input[0][1] ? p->translation_high[0][1] : p->translation_period[0][1],
							p->translation_by_input[0][2] ? "*" : "T", p->translation_by_input[0][2] ? p->translation_high[0][2] : p->translation_period[0][2],
							p->translation_by_input[1][0] ? "*" : "T", p->translation_by_input[1][0] ? p->translation_high[1][0] : p->translation_period[1][0],
							p->translation_by_input[1][1] ? "*" : "T", p->translation_by_input[1][1] ? p->translation_high[1][1] : p->translation_period[1][1],
							p->translation_by_input[1][2] ? "*" : "T", p->translation_by_input[1][2] ? p->translation_high[1][2] : p->translation_period[1][2],
							p->offset_by_input ? "*" : "", p->offset_high, p->input_name);
					}
				}
			}
		}
		if (getenv("HALO_H2_DEBUG"))
			platform_log("h2 bipeds: %s: its model made: the heap %ld KB more than before", biped->name,
				((long)heap_in_use() - (long)heap_before) / 1024);
		if (biped->node_count > RENDERER_NODES)
		{
			/* (stand_ins.md: a model of more nodes than the renderer skins at
			once needs part palettes for this engine's models first) */
			platform_log("h2 bipeds: %s: %d nodes, more than the renderer skins (%d): not yet", biped->name,
				(int)biped->node_count, RENDERER_NODES);
			biped_free(biped);
			continue;
		}
		/* (a graph an earlier biped made of the same Halo 2 graph, for the
		same skeleton - names and defaults -, played: one copy of it) */
		biped->graph_of = b;
		biped->flies = biped_flies(host, biped);
		if (biped->flies)
			platform_log("h2 bipeds: %s flies (its standing seat Halo 2's flight mode, if its graph has one)", biped->name);
		{
			short e;

			for (e = 0; e < b && biped->graph_of == b && !getenv("HALO_H2_BIPED_OWN_GRAPHS"); e++)
			{
				struct made_biped const *earlier = &(*bipeds->bipeds[e]);
				short n;
				boolean same;

				if (!earlier->made || earlier->shares != NONE || earlier->graph_of != e || earlier->h2_jmad_tag != biped->h2_jmad_tag ||
					earlier->node_count != biped->node_count || earlier->flies != biped->flies)
				{
					continue;
				}
				for (n = 0, same = TRUE; n < biped->node_count && same; n++)
				{
					int k;

					same = !strcmp(earlier->node_names[n], biped->node_names[n]) && earlier->node_parent[n] == biped->node_parent[n];
					for (k = 0; k < 3 && same; k++)
						same = fabsf(earlier->node_default[n].translation[k] - biped->node_default[n].translation[k]) < 0.002f;
					for (k = 0; k < 4 && same; k++)
						same = fabsf(earlier->node_default[n].rotation[k] - biped->node_default[n].rotation[k]) < 0.002f;
				}
				if (same)
					biped->graph_of = e;
			}
		}
		if (biped->graph_of != b)
			platform_log("h2 bipeds: %s: plays %s's graph", biped->name, (*bipeds->bipeds[biped->graph_of]).name);
		else
		{
			/* (Halo 3's graph on this biped, HALO_H3_ANIMATION: h3_animation.c;
			Halo 2's own if it cannot be made) */
			if (h3_animation_wanted(biped->name))
			{
				biped->graph = h3_animation_graph_make(biped->name, (char const (*)[32])biped->node_names, biped->node_default,
					biped->node_parent, biped->node_count, why, sizeof(why));
				if (!biped->graph)
					platform_log("h2 bipeds: %s: its Halo 3 graph not made (%s): Halo 2's", biped->name, why);
			}
			/* (made ahead on another thread, from these very nodes: taken) */
			if (!biped->graph)
				biped->graph = graphs_ahead_take(b, host->cache, biped);
			if (!biped->graph)
				biped->graph = h2_graph_make(host->cache, biped->h2_jmad_tag, (char const (*)[32])biped->node_names, biped->node_default,
					biped->node_parent, biped->node_count, biped->flies, why, sizeof(why));
		}
		/* (a graph none of whose animations this engine plays - Lord Hood's:
		his face's alone, his cutscenes' their own graphs, h2_cinematics.c -:
		an earlier biped's of the same skeleton by its nodes' names, the
		Marine's, stands for it) */
		if (!biped->graph && biped->graph_of == b)
		{
			short e;

			for (e = 0; e < b && biped->graph_of == b; e++)
			{
				struct made_biped const *earlier = &(*bipeds->bipeds[e]);
				short n;

				if (!earlier->made || earlier->shares != NONE || earlier->graph_of != e || earlier->node_count != biped->node_count)
					continue;
				for (n = 0; n < biped->node_count && !strcmp(earlier->node_names[n], biped->node_names[n]) &&
					earlier->node_parent[n] == biped->node_parent[n]; n++)
					;
				if (n == biped->node_count)
					biped->graph_of = e;
			}
			if (biped->graph_of != b)
				platform_log("h2 bipeds: %s: its animation graph: %s; plays %s's", biped->name, why, (*bipeds->bipeds[biped->graph_of]).name);
		}
		if (!biped->graph && biped->graph_of == b)
		{
			platform_log("h2 bipeds: %s: its animation graph: %s", biped->name, why);
			biped_free(biped);
			continue;
		}
		if (getenv("HALO_H2_DEBUG"))
			platform_log("h2 bipeds: %s: its graph made: the heap %ld KB more than before", biped->name,
				((long)heap_in_use() - (long)heap_before) / 1024);
		biped->made = TRUE;
		biped->first_tag = tags;
		biped->local_first_tag = tags;
		tags += _tag_shaders + biped->shader_count + (biped->graph_of == b ? 1 : 0);
		/* (its camera track Halo 2's: HALO_H2_BIPED_STOCK_CAMERA=1 the stand-in's) */
		biped->camera_local = NONE;
		biped->camera_point_count = 0;
		if (biped->h2_group == 'bipd' && !getenv("HALO_H2_BIPED_STOCK_CAMERA") && biped_camera_track_read(host->cache, biped) >= 4)
		{
			biped->camera_local = _tag_shaders + biped->shader_count + (biped->graph_of == b ? 1 : 0);
			tags++;
		}
		/* the report */
		{
			struct h2_biped_report *r = &biped->report;
			struct h2_graph_report graph;
			long i, level;

			snprintf(r->name, sizeof(r->name), "%s", biped->name);
			r->nodes = biped->node_count;
			r->markers = biped->marker_count;
			r->regions = biped->region_count;
			for (i = 0; i < biped->region_count; i++)
			{
				r->permutations += biped->regions[i].permutation_count;
				for (level = 0; level < H2_BIPED_DETAIL_LEVELS; level++)
				{
					short g = biped->regions[i].permutations[0].geometry[level];

					if (g != NONE)
					{
						r->triangles[level] += biped->geometries[g].triangles;
						r->vertices[level] += biped->geometries[g].vertices;
					}
				}
			}
			h2_graph_report((*bipeds->bipeds[biped->graph_of]).graph, &graph);
			if (biped->graph_of != b)
				graph.bytes = 0;
			r->animations = graph.animations;
			r->animation_bytes = graph.bytes;
			r->made = TRUE;
			platform_log("h2 bipeds: %s (%s): %ld nodes, %ld markers, %ld regions (%ld permutations), triangles by level %ld %ld %ld %ld %ld, "
				"%ld animations (%ld KB), %ld shaders",
				biped->name, biped->h2_biped, r->nodes, r->markers, r->regions, r->permutations, r->triangles[0], r->triangles[1], r->triangles[2],
				r->triangles[3], r->triangles[4], r->animations, r->animation_bytes / 1024, biped->shader_count);
		}
	}
	biped_slot_settle(bipeds, NUMBER_OF_H2_BIPEDS - 1);
	bipeds->tag_count = tags;
	if (getenv("HALO_H2_DEBUG"))
		platform_log("h2 bipeds: gathered: the heap %ld KB more than before", ((long)heap_in_use() - (long)heap_before) / 1024);
	if (!tags)
	{
		h2_bipeds_free(bipeds);
		return NULL;
	}
	return bipeds;
}

H2_ALIGN void h2_bipeds_free(
	struct h2_bipeds *bipeds)
{
	short b;

	if (!bipeds)
		return;
	for (b = 0; b < NUMBER_OF_H2_BIPEDS; b++)
	{
		if (!bipeds->bipeds[b] || bipeds->bipeds[b] == &biped_absent)
			continue;
		biped_free(bipeds->bipeds[b]);
		free(bipeds->bipeds[b]);
	}
	free(bipeds);
}

/* the overshield shaders made: a biped tag's id and its overshield's
shader's (render_objects.c) */

/* the loaded level's (h2_bipeds_overshields_map_loaded): found by name as
its tags load - a level loaded from its file draws Halo 2's overshield too,
not only one made in this session (Nov 3 2026: the table was filled only as
a level was made, as the weapons' fields were). NONE count: not read (the
reader off, HALO_H2_OVERSHIELD_READER=0): the made level's table, as before */
static struct { unsigned long biped, shader; } overshields_loaded[NUMBER_OF_H2_BIPEDS];
static short overshields_loaded_count = NONE;

H2_ALIGN long h2_bipeds_overshield_shader(
	long definition_index)
{
	short count = overshields_loaded_count != NONE ? overshields_loaded_count : overshield_count;
	short i;

	for (i = 0; i < count; i++)
	{
		unsigned long biped = overshields_loaded_count != NONE ? overshields_loaded[i].biped : overshields[i].biped;

		if (biped == (unsigned long)definition_index)
			return (long)(overshields_loaded_count != NONE ? overshields_loaded[i].shader : overshields[i].shader);
	}
	return NONE;
}

/* an overshield's colour by its amount (0 to 1: the overcharge, of two
more shields): Halo 2's colour function's four colours - black, (86, 41,
41) twice, (127, 127, 127) - in turn */
H2_ALIGN void h2_bipeds_overshield_colour(
	float amount,
	float colour[3])
{
	static float const stops[4][3] = { { 0.0f, 0.0f, 0.0f }, { 86 / 255.0f, 41 / 255.0f, 41 / 255.0f },
		{ 86 / 255.0f, 41 / 255.0f, 41 / 255.0f }, { 127 / 255.0f, 127 / 255.0f, 127 / 255.0f } };
	static float gain = -1.0f;
	float x;
	int k, c;

	if (gain < 0.0f)
		gain = getenv("HALO_H2_OVERSHIELD_GAIN") ? (float)atof(getenv("HALO_H2_OVERSHIELD_GAIN")) : 2.0f;
	x = (amount < 0.0f ? 0.0f : amount > 1.0f ? 1.0f : amount) * 3.0f;
	k = x >= 3.0f ? 2 : (int)x;
	x -= (float)k;
	for (c = 0; c < 3; c++)
	{
		colour[c] = gain * (stops[k][c] + (stops[k + 1][c] - stops[k][c]) * x);
		if (colour[c] > 1.0f)
			colour[c] = 1.0f;
	}
}

H2_ALIGN long h2_bipeds_tag_count(
	struct h2_bipeds const *bipeds)
{
	return bipeds ? bipeds->tag_count : 0;
}

H2_ALIGN boolean h2_bipeds_tag_describe(
	struct h2_bipeds const *bipeds,
	long index,
	unsigned long *group,
	char *name,
	long name_size)
{
	short b;

	for (b = 0; bipeds && b < NUMBER_OF_H2_BIPEDS; b++)
	{
		struct made_biped const *biped = &(*bipeds->bipeds[b]);
		long local = index - biped->first_tag;

		if (!biped->made || local < 0 || local >= (biped->shares != NONE ? 1 : _tag_shaders + biped->shader_count +
			(biped->graph_of == b ? 1 : 0) + (biped->camera_local != NONE ? 1 : 0)))
		{
			continue;
		}
		if (biped->shares == NONE && biped->camera_local != NONE && local == biped->camera_local)
		{
			*group = 'trak';
			snprintf(name, (size_t)name_size, "h2\\characters\\%s_mp\\%s camera", biped->name, biped->name);
			return TRUE;
		}
		if (biped->shares == NONE && local == _tag_shaders + biped->shader_count)
		{
			*group = 'antr';
			snprintf(name, (size_t)name_size, "h2\\characters\\%s_mp\\%s", biped->name, biped->name);
			return TRUE;
		}
		switch (local)
		{
		case _tag_biped:
			*group = 'bipd';
			snprintf(name, (size_t)name_size, "h2\\characters\\%s_mp\\%s_mp", biped->name, biped->name);
			break;
		case _tag_model:
			*group = 'mode';
			snprintf(name, (size_t)name_size, "h2\\characters\\%s_mp\\%s", biped->name, biped->name);
			break;
		case _tag_collision:
			*group = 'coll';
			snprintf(name, (size_t)name_size, "h2\\characters\\%s_mp\\%s", biped->name, biped->name);
			break;
		default:
			*group = biped->shaders[local - _tag_shaders].plasma ? 'spla' : biped->shaders[local - _tag_shaders].transparent ? 'schi' : 'soso';
			snprintf(name, (size_t)name_size, "h2\\characters\\%s_mp\\shaders\\%s", biped->name, biped->shaders[local - _tag_shaders].name);
			break;
		}
		return TRUE;
	}
	return FALSE;
}

H2_ALIGN long h2_bipeds_buffer_count(
	struct h2_bipeds const *bipeds)
{
	long count = 0, g;
	short b;

	for (b = 0; bipeds && b < NUMBER_OF_H2_BIPEDS; b++)
	{
		if (!(*bipeds->bipeds[b]).made)
			continue;
		for (g = 0; g < (*bipeds->bipeds[b]).geometry_count; g++)
			count += (*bipeds->bipeds[b]).geometries[g].part_count;
	}
	return count;
}

/* this engine's falling damage block (physics.c's and bipeds.c's own
copies are private to them): the distances and the speeds baked from them */
struct h2_ce_falling_damage
{
	long unused0[2];
	real falling_distance_lower_bound;
	real falling_distance_upper_bound;
	struct tag_reference falling_damage;
	long terminal_velocity_unused[2];
	real maximum_distance;
	struct tag_reference references[5];
	long unused7c[4];
	real runtime_maximum_falling_velocity;
	real runtime_minimum_damage_velocity;
	real runtime_maximum_damage_velocity;
};

typedef char verify_h2_ce_falling_damage_size[sizeof(struct h2_ce_falling_damage) == 0x98 ? 1 : -1];

/* the game's player control and falling damage, Halo 2's: the look (the
acceleration time 0.8 s and scale 2.5 held at the stick's edge, the edge
0.85 of it, the curve of six points from the stick's push to the turn, the
rates, how far the level comes back on its own) and the crosshair's place
(0, 0.165 - kept at 0x18/0x1C of the block, the Halo 2 layout's, where this
engine's has unused words), and the falls: harmful from 6 world units to
the worst at 10, deadly at 14 (their speeds as this engine keeps them, a
tick's, gravity being Halo 2's on its levels). The damage tags stay the
stock ones (the damage groups') */
H2_ALIGN static void globals_write(struct h2_biped_host const *host, long kit_globals)
{
	void *c = host->context;
	struct h2_cache *cache = host->cache;
	struct h2_cache_tag t;
	long h2_globals;
	unsigned long globals;
	unsigned char const *g;
	long count, i;
	unsigned long address;

	if (kit_globals == NONE || getenv("HALO_H2_STOCK_PLAYER_PHYSICS") || !(globals = host->root_of(c, kit_globals)))
		return;
	h2_globals = h2_cache_tag_find(cache, 'matg', "globals\\globals");
	if (h2_globals == NONE || !h2_cache_tag_get(cache, h2_globals, &t))
		return;
	/* player control (H2: block at 0xF0) */
	if (h2_cache_block(cache, t.address + 0xF0, 0x80, &count, &address) && count > 0 &&
		(g = h2_cache_at(cache, address, 0x80)) != NULL)
	{
		unsigned long field = globals + offsetof(struct game_globals, player_control);
		long own = (long)read32(host->at(c, field));
		unsigned long block = read32(host->at(c, field + 4));

		if (own > 0 && block >= host->link && readf(g + 0x4C) > 0.0f && readf(g + 0x50) > 0.0f)
		{
			unsigned char *o = host->at(c, block - host->link);
			struct game_globals_player_control *p = (struct game_globals_player_control *)o;
			long points;
			unsigned long curve;

			p->look_acceleration_time = readf(g + 0x4C);
			p->look_acceleration_scale = readf(g + 0x50);
			p->look_pegging_threshold = readf(g + 0x48);
			p->look_default_pitch_rate = readf(g + 0x40);
			p->look_default_yaw_rate = readf(g + 0x44);
			p->look_autolevel_scale = readf(g + 0x5C);
			p->minimum_autolevel_enabled_ticks = (short)read16(g + 0x6E);
			writef(o + 0x18, readf(g + 0x18));
			writef(o + 0x1C, readf(g + 0x1C));
			/* the aim assist's magnetism (friction 0.6, adhesion 0.7, the
			inconsequential target scale 0.5: player_control.c's magnetism
			and aim_assist.c's targets read them) and the action button's
			hold (Halo 2's "minimum action hold time", 0x7C, 0.2333 s in
			MCC - the kit's own is 0 -: how long the action button is held
			before it swaps a weapon for one on the ground, or takes a
			second one in the left hand, player_control.c's minimum weapon
			swap ticks; a press short of it picks a weapon up into a free
			hand, enters a vehicle, uses a switch) */
			{
				float friction = readf(g + 0x00), adhesion = readf(g + 0x04), inconsequential = readf(g + 0x08);
				float hold = readf(g + 0x7C);

				platform_log("h2 player control: stock magnetism friction %.2f adhesion %.2f inconsequential %.2f, action hold %d ticks",
					p->magnetism_friction, p->magnetism_adhesion, p->magnetism_inconsequential_target_scale,
					(int)p->minimum_weapon_swap_ticks);
				if (friction >= 0.0f && friction <= 1.0f && adhesion >= 0.0f && adhesion <= 1.0f && inconsequential >= 0.0f &&
					inconsequential <= 1.0f)
				{
					p->magnetism_friction = friction;
					p->magnetism_adhesion = adhesion;
					p->magnetism_inconsequential_target_scale = inconsequential;
				}
				if (hold >= 0.0f && hold < 5.0f)
					p->minimum_weapon_swap_ticks = (short)(hold * 30.0f + 0.5f);
				platform_log("h2 player control: Halo 2's magnetism friction %.2f adhesion %.2f inconsequential %.2f, action hold %d ticks (%.4f s)",
					p->magnetism_friction, p->magnetism_adhesion, p->magnetism_inconsequential_target_scale,
					(int)p->minimum_weapon_swap_ticks, hold);
			}
			if (h2_cache_block(cache, address + 0x74, 4, &points, &curve) && points >= 2 && points <= 32)
			{
				unsigned long out = host->block(c, block - host->link + offsetof(struct game_globals_player_control, look_function), points, 4);

				for (i = 0; i < points && !host->failed(c); i++)
					{
					unsigned char const *v = h2_cache_at(cache, curve + 4 * i, 4);

					writef(host->at(c, out + 4 * i), v ? readf(v) : 0.0f);
				}
			}
			if (getenv("HALO_H2_DEBUG"))
				platform_log("h2 player control: look %.2f s x%.2f edge %.2f rates %.0f/%.0f autolevel %.2f crosshair %.3f", p->look_acceleration_time,
					p->look_acceleration_scale, p->look_pegging_threshold, p->look_default_yaw_rate, p->look_default_pitch_rate,
					p->look_autolevel_scale, readf(o + 0x1C));
		}
	}
	/* the player information (H2: block at 0x130, one element laid out as
	this engine's game_globals_player_information from 0x24: walking speed,
	the double-speed multiplier, run forward / backward / sideways, run
	acceleration, sneak forward / backward / sideways, sneak acceleration,
	airborne acceleration, the grenade origin at 0x60 and the stun penalties
	at 0x78 - movement, turning, jumping, the stun times): Halo 2's player
	runs 2.25 / 2.0 / 2.0 a second, accelerates at 9.6, crouches at 0.9 /
	0.65 / 0.6 accelerating at 4.8, steers in the air at 1.05, and a stun
	slows and turns him 0.8 for up to 1.25 s (MCC's globals; the 2004 kit's
	the same). Checked against the kit's stock globals (logged below): Halo
	CE's are the same numbers, so a Halo 2 level's player already ran as
	Halo 2's - these are written from the player's own map so MCC's values
	rule. This engine's cache keeps the three accelerations a tick (the
	kit's tool divided them by 30: 9.6 is stored 0.32, as bipeds.c's own
	fallback has it), so Halo 2's a second are divided here. bipeds.c reads
	these for a player's biped every tick (biped_update_control). The two
	speed multipliers (MCC's are 0) are left this engine's: the double-speed
	cheat and the multiplayer-only scale this engine applies to every player */
	if (h2_cache_block(cache, t.address + 0x130, 0xA0, &count, &address) && count > 0 &&
		(g = h2_cache_at(cache, address, 0xA0)) != NULL)
	{
		unsigned long field = globals + offsetof(struct game_globals, player_information);
		long own = (long)read32(host->at(c, field));
		unsigned long block = read32(host->at(c, field + 4));
		float run_forward = readf(g + 0x2C), run_backward = readf(g + 0x30), run_sideways = readf(g + 0x34);
		float run_acceleration = readf(g + 0x38), sneak_forward = readf(g + 0x3C), sneak_backward = readf(g + 0x40);
		float sneak_sideways = readf(g + 0x44), sneak_acceleration = readf(g + 0x48), airborne = readf(g + 0x4C);

		if (own > 0 && block >= host->link && run_forward > 0.5f && run_forward < 10.0f && run_backward > 0.0f &&
			run_backward <= run_forward && run_sideways > 0.0f && run_sideways <= run_forward && run_acceleration > 0.0f &&
			run_acceleration < 100.0f && sneak_forward > 0.0f && sneak_forward <= run_forward && sneak_backward > 0.0f &&
			sneak_backward <= run_forward && sneak_sideways > 0.0f && sneak_sideways <= run_forward && sneak_acceleration > 0.0f &&
			sneak_acceleration < 100.0f && airborne >= 0.0f && airborne < 100.0f)
		{
			struct game_globals_player_information *p = (struct game_globals_player_information *)host->at(c, block - host->link);
			float stun_movement = readf(g + 0x78), stun_turning = readf(g + 0x7C), stun_jumping = readf(g + 0x80);
			float minimum_stun = readf(g + 0x84), maximum_stun = readf(g + 0x88);

			platform_log("h2 player information: stock walk %.3f run %.2f/%.2f/%.2f accel %.2f sneak %.2f/%.2f/%.2f accel %.2f "
				"airborne %.2f stun %.2f/%.2f/%.2f %.2f..%.2f s grenade origin %.2f %.2f %.2f",
				p->walking_speed, p->run_forward_speed, p->run_backward_speed, p->run_sideways_speed, p->run_acceleration,
				p->sneak_forward_speed, p->sneak_backward_speed, p->sneak_sideways_speed, p->sneak_acceleration,
				p->airborne_acceleration, p->stun_movement_penalty, p->stun_turning_penalty, p->stun_jumping_penalty,
				p->minimum_stun_time, p->maximum_stun_time, p->grenade_origin.i, p->grenade_origin.j, p->grenade_origin.k);
			p->walking_speed = readf(g + 0x24);
			p->run_forward_speed = run_forward;
			p->run_backward_speed = run_backward;
			p->run_sideways_speed = run_sideways;
			p->run_acceleration = run_acceleration / 30.0f;
			p->sneak_forward_speed = sneak_forward;
			p->sneak_backward_speed = sneak_backward;
			p->sneak_sideways_speed = sneak_sideways;
			p->sneak_acceleration = sneak_acceleration / 30.0f;
			p->airborne_acceleration = airborne / 30.0f;
			p->grenade_origin.i = readf(g + 0x60);
			p->grenade_origin.j = readf(g + 0x64);
			p->grenade_origin.k = readf(g + 0x68);
			if (stun_movement >= 0.0f && stun_movement <= 1.0f && stun_turning >= 0.0f && stun_turning <= 1.0f &&
				stun_jumping >= 0.0f && stun_jumping <= 1.0f && minimum_stun >= 0.0f && maximum_stun >= minimum_stun && maximum_stun < 60.0f)
			{
				p->stun_movement_penalty = stun_movement;
				p->stun_turning_penalty = stun_turning;
				p->stun_jumping_penalty = stun_jumping;
				p->minimum_stun_time = minimum_stun;
				p->maximum_stun_time = maximum_stun;
			}
			platform_log("h2 player information: Halo 2's walk %.3f run %.2f/%.2f/%.2f accel %.2f sneak %.2f/%.2f/%.2f accel %.2f "
				"airborne %.2f stun %.2f/%.2f/%.2f %.2f..%.2f s grenade origin %.2f %.2f %.2f",
				p->walking_speed, p->run_forward_speed, p->run_backward_speed, p->run_sideways_speed, p->run_acceleration,
				p->sneak_forward_speed, p->sneak_backward_speed, p->sneak_sideways_speed, p->sneak_acceleration,
				p->airborne_acceleration, p->stun_movement_penalty, p->stun_turning_penalty, p->stun_jumping_penalty,
				p->minimum_stun_time, p->maximum_stun_time, p->grenade_origin.i, p->grenade_origin.j, p->grenade_origin.k);
		}
		else
			platform_log("h2 player information: Halo 2's block not taken (count %ld, run %.2f/%.2f/%.2f)", count, run_forward,
				run_backward, run_sideways);
	}
	/* the difficulty's scales (H2: block at 0xF8, laid out as this engine's:
	easy, normal, heroic, legendary of each value - enemy damage, vitality,
	shield, recharge, the friends' the same, the AI's rate of fire, errors,
	delays, grenades, upgrades -, game_globals.c): a campaign level's, so
	the enemies hit the player and recharge as Halo 2's do on each
	difficulty (a multiplayer level keeps the stock ones). Taken whole when
	its first value is enemy damage's - rising from easy to legendary */
	if (strstr(h2_cache_scenario_path(cache), "\\solo\\") &&
		h2_cache_block(cache, t.address + 0xF8, sizeof(struct game_globals_difficulty_information), &count, &address) &&
		count > 0 && (g = h2_cache_at(cache, address, sizeof(struct game_globals_difficulty_information))) != NULL)
	{
		unsigned long field = globals + offsetof(struct game_globals, difficulty_information);
		long own = (long)read32(host->at(c, field));
		unsigned long block = read32(host->at(c, field + 4));
		float easy = readf(g), legendary = readf(g + 12);

		if (own > 0 && block >= host->link && easy > 0.0f && easy <= legendary && legendary < 10.0f)
		{
			struct game_globals_difficulty_information *d =
				(struct game_globals_difficulty_information *)host->at(c, block - host->link);
			short v, k;

			for (v = 0; v < NUMBER_OF_GAME_DIFFICULTY_VALUES; v++)
				for (k = 0; k < 4; k++)
					d->values[v][k] = readf(g + 16 * v + 4 * k);
			platform_log("h2 difficulty: Halo 2's scales (enemy damage %.2f/%.2f/%.2f/%.2f, enemy recharge %.2f/%.2f/%.2f/%.2f, "
				"rate of fire %.2f/%.2f/%.2f/%.2f)", d->values[_game_difficulty_value_enemy_damage][0],
				d->values[_game_difficulty_value_enemy_damage][1], d->values[_game_difficulty_value_enemy_damage][2],
				d->values[_game_difficulty_value_enemy_damage][3], d->values[_game_difficulty_value_enemy_recharge][0],
				d->values[_game_difficulty_value_enemy_recharge][1], d->values[_game_difficulty_value_enemy_recharge][2],
				d->values[_game_difficulty_value_enemy_recharge][3], d->values[_game_difficulty_value_rate_of_fire][0],
				d->values[_game_difficulty_value_rate_of_fire][1], d->values[_game_difficulty_value_rate_of_fire][2],
				d->values[_game_difficulty_value_rate_of_fire][3]);
		}
		else
			platform_log("h2 difficulty: Halo 2's block not taken (count %ld, first %.2f..%.2f)", count, easy, legendary);
	}
	/* falling damage (H2: block at 0x140: harmful from 0x8, to 0xC, deadly at 0x20) */
	if (h2_cache_block(cache, t.address + 0x140, 0x4C, &count, &address) && count > 0 && (g = h2_cache_at(cache, address, 0x24)) != NULL)
	{
		unsigned long field = globals + offsetof(struct game_globals, falling_damage);
		long own = (long)read32(host->at(c, field));
		unsigned long block = read32(host->at(c, field + 4));
		float lower = readf(g + 0x8), upper = readf(g + 0xC), deadly = readf(g + 0x20);

		if (own > 0 && block >= host->link && lower > 0.0f && upper > lower && deadly > upper)
		{
			struct h2_ce_falling_damage *f = (struct h2_ce_falling_damage *)host->at(c, block - host->link);
			float gravity = H2_GRAVITY_PER_SECOND / (30.0f * 30.0f);

			if (getenv("HALO_H2_DEBUG"))
				platform_log("h2 falling damage: stock %.1f/%.1f/%.1f speeds %.4f/%.4f/%.4f", f->falling_distance_lower_bound,
					f->falling_distance_upper_bound, f->maximum_distance, f->runtime_minimum_damage_velocity,
					f->runtime_maximum_damage_velocity, f->runtime_maximum_falling_velocity);
			f->falling_distance_lower_bound = lower;
			f->falling_distance_upper_bound = upper;
			f->maximum_distance = deadly;
			f->runtime_minimum_damage_velocity = (float)sqrt(2.0 * gravity * lower);
			f->runtime_maximum_damage_velocity = (float)sqrt(2.0 * gravity * upper);
			f->runtime_maximum_falling_velocity = (float)sqrt(2.0 * gravity * deadly);
		}
		/* (its damage effects - the fall's and the deadly fall's - Halo
		2's: globals\falling and globals\distance, h2_damage.c) */
		{
			static char const *const kit_names[2] = { "globals\\falling", "globals\\distance" };
			static unsigned long const h2_fields[2] = { 0x10, 0x24 };
			long k;

			for (k = 0; k < 2; k++)
			{
				long kit = host->kit_find(c, 'jpt!', kit_names[k]);
				unsigned long root = kit != NONE ? host->root_of(c, kit) : 0;

				if (root)
					h2_damage_effect_write(cache, h2_cache_reference(cache, address + h2_fields[k]), host->at(c, root), damage_write_real);
				else
					platform_log("h2 falling damage: no %s", kit_names[k]);
			}
		}
		/* (group H, vehicles: the splatter - a vehicle running a player
		down, physics.c - deals Halo 2's collision damage effect, the
		globals' collision damage (0xB8, its reference at 0), scaled by
		Halo 2's force rule; a vehicle killing a unit takes Halo 2's
		vehicle_killed_unit (none)) */
		{
			static char const *const kit_names[2] = { "globals\\vehicle_collision", "globals\\vehicle_killed_unit" };
			long h2_effects[2] = { NONE, NONE }, k, collision_count;
			unsigned long collision;

			if (h2_cache_block(cache, t.address + 0xB8, 0x8, &collision_count, &collision) && collision_count > 0)
				h2_effects[0] = h2_cache_reference(cache, collision);
			h2_effects[1] = h2_cache_reference(cache, address + 0x34);
			for (k = 0; k < 2; k++)
			{
				long kit = host->kit_find(c, 'jpt!', kit_names[k]);
				unsigned long root = kit != NONE ? host->root_of(c, kit) : 0;

				if (root && h2_effects[k] != NONE)
					h2_damage_effect_write(cache, h2_effects[k], host->at(c, root), damage_write_real);
				else
					platform_log("h2 vehicle collision damage: no %s (kit %ld, Halo 2's %ld)", kit_names[k], kit, h2_effects[k]);
			}
		}
	}
}

H2_ALIGN boolean h2_bipeds_write(
	struct h2_bipeds *bipeds,
	struct h2_biped_host const *host,
	long first_tag)
{
	void *c = host->context;
	struct h2_biped_host local = *host;
	long kit_globals = host->kit_find(c, 'matg', "globals\\globals");
	long kit_biped = host->kit_find(c, 'bipd', "characters\\cyborg_mp\\cyborg_mp");
	long kit_collision = NONE;
	short b;
	boolean spartan = FALSE;

	/* (models at load: the models alone - the rest of the level's bipeds'
	tags, the globals' values, are the level's own) */
	if (!host->models_only)
	{
		overshield_count = 0;
		globals_write(host, kit_globals);
	}
	if (!bipeds)
		return TRUE;
	/* (the stock biped's collision model: its reference's tag id) */
	if (kit_biped != NONE && host->root_of(c, kit_biped))
	{
		unsigned long id = read32(host->at(c, host->root_of(c, kit_biped) + offsetof(struct biped_definition, object.collision_model) + 12));

		kit_collision = id != 0xFFFFFFFFUL ? (long)(id & 0xFFFF) : NONE;
	}
	for (b = 0; b < NUMBER_OF_H2_BIPEDS; b++)
	{
		struct made_biped *biped = &(*bipeds->bipeds[b]);
		unsigned long graph;
		boolean collision;

		if (!biped->made)
			continue;
		biped->first_tag += first_tag;
		/* (a biped of another's model: its biped tag alone) */
		if (biped->shares != NONE)
		{
			struct made_biped const *model = &(*bipeds->bipeds[biped->shares]);

			if (host->models_only)
				continue;
			if (!biped_write(&local, biped, model, GRAPH_TAG(&(*bipeds->bipeds[model->graph_of])), kit_biped, TRUE))
				return FALSE;
			continue;
		}
		if (!host->models_only)
		{
			shaders_write(&local, biped);
			biped_camera_track_write(&local, biped);
		}
		/* (models at load, h2_models_load.inc: the level made without its
		bipeds' models, graphs and collision models - made as it loads; the
		collision model's outcome as a models_only pass found it) */
		if (host->no_models)
		{
			long g;

			for (g = 0; g < biped->geometry_count; g++)
			{
				local.first_buffer += biped->geometries[g].part_count;
				local.first_index_buffer += biped->geometries[g].part_count;
			}
			/* (its collision model saved all the same - or, collision_stub, a
			stub of it, made whole at load: the effects h2_effects.c writes
			into it after - its shield's, its regions' - are recorded by the
			level's builder and written again at load; whether the whole one
			can be made the models_only pass found) */
			collision = (!host->collision_stub || biped->collision_made) && collision_write(&local, biped, kit_collision);
			if (!collision)
			{
				unsigned long kit_root = kit_collision != NONE ? host->root_of(c, kit_collision) : 0;
				unsigned long root = kit_root ? host->alloc(c, CE_COLLISION_SIZE) : 0;

				if (!root || host->failed(c))
					return FALSE;
				memcpy(host->at(c, root), host->at(c, kit_root), CE_COLLISION_SIZE);
				deep_copy(host, halo_tag_layout_root('coll'), root, 0);
				host->set_root(c, biped->first_tag + _tag_collision, root);
			}
			if (!biped_write(&local, biped, biped, GRAPH_TAG(&(*bipeds->bipeds[biped->graph_of])), kit_biped, collision))
			{
				platform_log("h2 bipeds: %s: its biped tag not written", biped->name);
				return FALSE;
			}
			if (b == H2_BIPED_SPARTAN)
				spartan = TRUE;
			continue;
		}
		if (!model_write(&local, biped))
		{
			platform_log("h2 bipeds: %s: its model not written", biped->name);
			return FALSE;
		}
		/* (the model's parts took this many buffer entries) */
		{
			long g;

			for (g = 0; g < biped->geometry_count; g++)
			{
				local.first_buffer += biped->geometries[g].part_count;
				local.first_index_buffer += biped->geometries[g].part_count;
			}
		}
		if (biped->graph_of == b)
		{
			graph = h2_graph_write(biped->graph, &local);
			if (!graph)
			{
				platform_log("h2 bipeds: %s: its animation graph not written", biped->name);
				return FALSE;
			}
			host->set_root(c, GRAPH_TAG(biped), graph);
		}
		collision = collision_write(&local, biped, kit_collision);
		biped->collision_made = collision;
		if (!collision)
		{
			/* (stand_ins.md: the stock biped's collision model) */
			/* (its tag a copy of the stock one's, unused - the tag check refuses
			a tag whose data is another's: a Prophet's, whose model has no
			collision model of Halo 2's) */
			unsigned long kit_root = kit_collision != NONE ? host->root_of(c, kit_collision) : 0;
			unsigned long root = kit_root ? host->alloc(c, CE_COLLISION_SIZE) : 0;

			platform_log("h2 bipeds: %s: its collision model not made: the stock one's", biped->name);
			if (!root || host->failed(c))
			{
				platform_log("h2 bipeds: %s: no stock collision model either (kit tag %ld, kit root %lu, tag data %s)", biped->name,
					kit_collision, kit_root, host->failed(c) ? "out of room" : "fine");
				return FALSE;
			}
			memcpy(host->at(c, root), host->at(c, kit_root), CE_COLLISION_SIZE);
			deep_copy(host, halo_tag_layout_root('coll'), root, 0);
			host->set_root(c, biped->first_tag + _tag_collision, root);
		}
		if (host->models_only)
			continue;
		if (!biped_write(&local, biped, biped, GRAPH_TAG(&(*bipeds->bipeds[biped->graph_of])), kit_biped, collision))
		{
			platform_log("h2 bipeds: %s: its biped tag not written", biped->name);
			return FALSE;
		}
		if (b == H2_BIPED_SPARTAN)
			spartan = TRUE;
	}
	if (host->models_only)
		return !host->failed(c);
	/* (tests: HALO_H2_BIPED_SHOWCASE="x,y,z,yaw": each biped made placed in
	the scenario there, side by side, coloured - a still model to look at,
	where players come and go) */
	if (getenv("HALO_H2_BIPED_SHOWCASE") && host->scenario_root)
	{
		float x, y, z, yaw;

		if (sscanf(getenv("HALO_H2_BIPED_SHOWCASE"), "%f,%f,%f,%f", &x, &y, &z, &yaw) == 4)
		{
			long count = 0, slot = 0;
			unsigned long palette, placements;
			static unsigned long const colours[4] = { 0xFFC02020UL, 0xFF2040C0UL, 0xFFE0E0E0UL, 0xFF20A040UL };
			float radians = yaw * 3.14159265f / 180.0f;

			for (b = 0; b < NUMBER_OF_H2_BIPEDS; b++)
				count += (*bipeds->bipeds[b]).made;
			palette = host->block(c, host->scenario_root + offsetof(struct scenario, biped_palette), count, 0x30);
			placements = host->block(c, host->scenario_root + offsetof(struct scenario, bipeds), 2 * count, 0x78);
			for (b = 0; b < NUMBER_OF_H2_BIPEDS && !host->failed(c); b++)
			{
				int k;

				if (!(*bipeds->bipeds[b]).made)
					continue;
				host->reference(c, palette + 0x30 * slot, 'bipd', host->tag_id(c, (*bipeds->bipeds[b]).first_tag + _tag_biped));
				for (k = 0; k < 2; k++)
				{
					unsigned char *o = host->at(c, placements + 0x78 * (2 * slot + k));
					float side = (float)(2 * slot + k) * 0.9f;

					write16(o, slot);
					write16(o + 2, -1);
					writef(o + 8, x - side * sinf(radians));
					writef(o + 12, y + side * cosf(radians));
					writef(o + 16, z);
					writef(o + 0x14, radians);
					o[0x24] = 0xFF;
					write32(o + 0x28 + 8, colours[(2 * slot + k) & 3]);      /* change colour C */
					writef(o + 0x48, 1.0f);
				}
				slot++;
			}
			platform_log("h2 bipeds: showcase: %ld bipeds placed at %.2f %.2f %.2f facing %.0f", 2 * count, x, y, z, yaw);
		}
	}
	/* the game's multiplayer unit: the Spartan */
	if (spartan && kit_globals != NONE && !getenv("HALO_H2_STOCK_PLAYER"))
	{
		unsigned long globals = host->root_of(c, kit_globals);
		unsigned long field = globals + offsetof(struct game_globals, multiplayer_information);
		long count = (long)read32(host->at(c, field));
		unsigned long information = read32(host->at(c, field + 4));

		if (count > 0 && information >= host->link)
		{
			host->reference(c, information - host->link + offsetof(struct game_globals_multiplayer_information, unit), 'bipd',
				host->tag_id(c, (*bipeds->bipeds[H2_BIPED_SPARTAN]).first_tag + _tag_biped));
			platform_log("h2 bipeds: the multiplayer unit is Halo 2's Spartan");
		}
	}
	/* a campaign level's player: Halo 2's, as its first starting location's
	campaign player type says (0x2C of the scenario's starting locations,
	0x100: 0 the Master Chief, 1 the Arbiter) - the Spartan or the Elite made
	here, which carry Halo 2's weapons (this engine's campaign cyborg, the
	stock player, cannot: none of its labels) */
	if (spartan && kit_globals != NONE && strstr(h2_cache_scenario_path(host->cache), "\\solo\\") && !getenv("HALO_H2_STOCK_PLAYER"))
	{
		unsigned long globals = host->root_of(c, kit_globals);
		unsigned long field = globals + offsetof(struct game_globals, player_information);
		long count = (long)read32(host->at(c, field));
		unsigned long information = read32(host->at(c, field + 4));
		struct h2_cache_tag scenario;
		long starts = 0;
		unsigned long start_elements;
		short type = 0, biped = H2_BIPED_SPARTAN;

		if (h2_cache_tag_get(host->cache, h2_cache_scenario_tag(host->cache), &scenario) &&
			h2_cache_block(host->cache, scenario.address + 0x100, 0x34, &starts, &start_elements) && starts > 0)
		{
			unsigned char const *start = h2_cache_at(host->cache, start_elements, 0x34);

			type = start ? (short)read16(start + 0x2C) : 0;
		}
		if (type == 1 && (*bipeds->bipeds[H2_BIPED_ELITE]).made)
			biped = H2_BIPED_ELITE;
		if (count > 0 && information >= host->link)
		{
			host->reference(c, information - host->link + offsetof(struct game_globals_player_information, player_unit), 'bipd',
				host->tag_id(c, (*bipeds->bipeds[biped]).first_tag + _tag_biped));
			platform_log("h2 bipeds: the campaign's player is Halo 2's %s (campaign player type %d)",
				biped == H2_BIPED_ELITE ? "Elite" : "Spartan", (int)type);
		}
	}
	return !host->failed(c);
}

H2_ALIGN boolean h2_bipeds_report(
	struct h2_bipeds const *bipeds,
	short biped,
	struct h2_biped_report *report)
{
	if (!bipeds || biped < 0 || biped >= NUMBER_OF_H2_BIPEDS || !(*bipeds->bipeds[biped]).made)
		return FALSE;
	*report = (*bipeds->bipeds[biped]).report;
	return TRUE;
}

H2_ALIGN long h2_bipeds_biped_tag(
	struct h2_bipeds const *bipeds,
	short biped)
{
	if (!bipeds || biped < 0 || biped >= NUMBER_OF_H2_BIPEDS || !(*bipeds->bipeds[biped]).made)
		return NONE;
	return (*bipeds->bipeds[biped]).first_tag + _tag_biped;
}

H2_ALIGN unsigned long h2_bipeds_default_variant(
	struct h2_cache *cache,
	long h2_object_tag)
{
	struct h2_cache_tag t;
	unsigned long name;

	if (h2_object_tag == NONE || !h2_cache_tag_get(cache, h2_object_tag, &t) || t.size < 0x34)
		return 0;
	name = h2_cache_read32(cache, t.address + 0x30);
	return name == 0xFFFFFFFFUL ? 0 : name;
}

H2_ALIGN boolean h2_bipeds_tag_flies(
	struct h2_cache *cache,
	long h2_object_tag)
{
	struct h2_cache_tag t;

	if (h2_object_tag == NONE || !h2_cache_tag_get(cache, h2_object_tag, &t))
		return FALSE;
	/* (a Drone: its physics say ground, its character "has flying mode", "perch";
	its biped carries the flight's numbers all the same) */
	if (t.group_tag == 'bipd' && strstr(h2_cache_tag_name(cache, h2_object_tag), "\\bugger\\"))
		return TRUE;
	if (t.group_tag == 'bipd' && t.size >= 0x268)
		return (h2_cache_read32(cache, t.address + 0x264) & 0x10) != 0;
	if (t.group_tag == 'crea' && t.size >= 0xD8)
		return (h2_cache_read32(cache, t.address + 0xD4) & 0x10) != 0;
	/* (the Enforcer: a vehicle its AI flies, a flying biped here) */
	if (t.group_tag == 'vehi')
		return h2_bipeds_for_unit_name(h2_cache_tag_name(cache, h2_object_tag)) == H2_BIPED_SENTINEL_ENFORCER;
	return FALSE;
}

H2_ALIGN short h2_bipeds_for_unit_name(
	char const *name)
{
	short b;

	if (!name)
		return NONE;
	/* (the campaign's Master Chief and Arbiter - the cutscenes' "chief",
	the player on a campaign level -: the Spartan's and Elite's, made of
	them on a campaign level, h2_bipeds_gather) */
	if (!strcmp(name, "objects\\characters\\masterchief\\masterchief"))
		return H2_BIPED_SPARTAN;
	if (!strcmp(name, "objects\\characters\\dervish\\dervish"))
		return H2_BIPED_ELITE;
	for (b = 0; b < NUMBER_OF_H2_BIPEDS; b++)
	{
		if (!strcmp(name, biped_sources[b].biped))
			return b;
	}
	return NONE;
}

H2_ALIGN long h2_bipeds_made_from(
	struct h2_cache *cache,
	short biped)
{
	long tag = NONE;

	if (biped < 0 || biped >= NUMBER_OF_H2_BIPEDS)
		return NONE;
	if ((biped == H2_BIPED_SPARTAN || biped == H2_BIPED_ELITE) && strstr(h2_cache_scenario_path(cache), "\\solo\\") &&
		!getenv("HALO_H2_MP_PLAYER_ON_CAMPAIGN"))
	{
		tag = h2_cache_tag_find(cache, 'bipd', biped == H2_BIPED_SPARTAN ? "objects\\characters\\masterchief\\masterchief" :
			"objects\\characters\\dervish\\dervish");
	}
	return tag != NONE ? tag : h2_cache_tag_find(cache, biped_sources[biped].group, biped_sources[biped].biped);
}

H2_ALIGN short h2_bipeds_variant_number(
	struct h2_cache *cache,
	short biped,
	long h2_biped_tag,
	unsigned long variant_name)
{
	struct h2_cache_tag t, h;
	long hlmt, made_hlmt, made, count, i;
	unsigned long variants;

	if (biped < 0 || biped >= NUMBER_OF_H2_BIPEDS || h2_biped_tag == NONE)
		return NONE;
	if (!variant_name || variant_name == 0xFFFFFFFFUL)
		variant_name = h2_bipeds_default_variant(cache, h2_biped_tag);
	if (!variant_name)
		return NONE;
	/* (the placed biped's model the made one's: the same Halo 2 biped, or
	another of its model - marine_massive a Marine) */
	made = h2_bipeds_made_from(cache, biped);
	if (made == NONE || !h2_cache_tag_get(cache, made, &t) || (made_hlmt = h2_cache_reference(cache, t.address + 0x34)) == NONE ||
		!h2_cache_tag_get(cache, h2_biped_tag, &t) || (hlmt = h2_cache_reference(cache, t.address + 0x34)) == NONE ||
		hlmt != made_hlmt || !h2_cache_tag_get(cache, hlmt, &h) || !h2_cache_block(cache, h.address + 0x50, 0x38, &count, &variants))
	{
		return NONE;
	}
	for (i = 0; i < count && i < 32; i++)
	{
		if (h2_cache_read32(cache, variants + 0x38 * i) == variant_name)
			return (short)(i + 1);
	}
	return NONE;
}

H2_ALIGN char const *h2_bipeds_source_name(
	short biped)
{
	return biped >= 0 && biped < NUMBER_OF_H2_BIPEDS ? biped_sources[biped].name : NULL;
}

H2_ALIGN char const *h2_bipeds_source_path(
	short biped)
{
	return biped >= 0 && biped < NUMBER_OF_H2_BIPEDS ? biped_sources[biped].biped : NULL;
}

H2_ALIGN unsigned long h2_bipeds_source_group(
	short biped)
{
	return biped >= 0 && biped < NUMBER_OF_H2_BIPEDS ? biped_sources[biped].group : 0;
}

H2_ALIGN long h2_bipeds_biped_tag_planned(
	struct h2_bipeds const *bipeds,
	short biped,
	long first_tag)
{
	if (!bipeds || biped < 0 || biped >= NUMBER_OF_H2_BIPEDS || !(*bipeds->bipeds[biped]).made)
		return NONE;
	return first_tag + (*bipeds->bipeds[biped]).local_first_tag + _tag_biped;
}

/* ---------- runtime: the part palettes (PC) */

#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"
#include "cache/cache_files.h"
#include "cache/physical_memory_map.h"
#include "models/model_definitions.h"
#include "custom_edition_cache.h"
#include "h2_level.h"

/* (the cache's model part: models.c's, 0x68 bytes - its flags at 0, its
vertex buffer at 0x54: the vertex count at 4, the vertices at 0xC) */
enum
{
	PART_BYTES = 0x68,
	GEOMETRY_PARTS = 0x24,
	PART_VERTEX_BUFFER = 0x54,
	PART_PALETTE_MADE_BIT = 31,      /* (its vertices already name its palette) */
};

H2_ALIGN void h2_bipeds_part_palettes(
	void)
{
	struct tag_iterator iterator;
	long tag_index, models = 0, parts = 0, failed = 0;

	/* (the same tags again - a revert, no load -: their parts already
	renumbered, the palettes made then kept) */
	tag_iterator_new(&iterator, 'mode');
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		struct model *model = (struct model *)tag_get('mode', tag_index);
		struct tag_block const *geometry_parts;

		if (strncmp(tag_get_name(tag_index), "h2\\characters\\", 14) || model->nodes.count < RENDERER_SKINNED_NODES + 1 ||
			model->geometries.count < 1)
		{
			continue;
		}
		geometry_parts = (struct tag_block const *)((unsigned char *)model->geometries.address + GEOMETRY_PARTS);
		if (geometry_parts->count > 0 && TEST_FLAG(*(unsigned long *)geometry_parts->address, PART_PALETTE_MADE_BIT))
			return;
	}
	custom_edition_part_palette_add(NULL, NULL, 0);
	tag_iterator_new(&iterator, 'mode');
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		struct model *model = (struct model *)tag_get('mode', tag_index);
		long g;

		if (strncmp(tag_get_name(tag_index), "h2\\characters\\", 14) || model->nodes.count < RENDERER_SKINNED_NODES + 1)
			continue;
		models++;
		for (g = 0; g < model->geometries.count; g++)
		{
			struct tag_block const *geometry_parts =
				(struct tag_block const *)((unsigned char *)model->geometries.address + CE_MODEL_GEOMETRY_SIZE * g + GEOMETRY_PARTS);
			long p;

			for (p = 0; p < geometry_parts->count; p++)
			{
				unsigned char *part = (unsigned char *)geometry_parts->address + PART_BYTES * p;
				unsigned long *flags = (unsigned long *)part;
				struct vertex_buffer *buffer = (struct vertex_buffer *)(part + PART_VERTEX_BUFFER);
				long vertex_count = *(long *)(part + PART_VERTEX_BUFFER + 4);
				unsigned char *vertices = *(unsigned char **)(part + PART_VERTEX_BUFFER + 0xC);
				unsigned char palette[RENDERER_SKINNED_NODES], local[MODEL_NODES];
				short count = 0;
				long v;
				boolean fits = TRUE;

				if (!vertices || vertex_count <= 0)
					continue;
				memset(local, 0xFF, sizeof(local));
				/* (made once a load: a second pass reads its palette from the
				vertices it has already renumbered - unknowable -, so the
				first's is kept by the flag, and its nodes rebuilt here) */
				for (v = 0; v < vertex_count && fits; v++)
				{
					int k;

					for (k = 0; k < 2; k++)
					{
						unsigned char node = (unsigned char)(vertices[VERTEX_SIZE * v + 28 + k] / 3);

						if (node >= MODEL_NODES)
							node = 0;
						if (local[node] == 0xFF)
						{
							if (count == RENDERER_SKINNED_NODES)
							{
								fits = FALSE;
								break;
							}
							local[node] = (unsigned char)count;
							palette[count++] = node;
						}
					}
				}
				if (!fits)
				{
					failed++;
					continue;
				}
				if (!TEST_FLAG(*flags, PART_PALETTE_MADE_BIT))
				{
					for (v = 0; v < vertex_count; v++)
					{
						vertices[VERTEX_SIZE * v + 28] = (unsigned char)(3 * local[vertices[VERTEX_SIZE * v + 28] / 3 < MODEL_NODES ?
							vertices[VERTEX_SIZE * v + 28] / 3 : 0]);
						vertices[VERTEX_SIZE * v + 29] = (unsigned char)(3 * local[vertices[VERTEX_SIZE * v + 29] / 3 < MODEL_NODES ?
							vertices[VERTEX_SIZE * v + 29] / 3 : 0]);
					}
					SET_FLAG(*flags, PART_PALETTE_MADE_BIT, TRUE);
					custom_edition_part_palette_add(buffer, palette, count);
					parts++;
				}
			}
		}
	}
	if (models)
		platform_log("h2 bipeds: %ld models of more nodes than the renderer skins at once: %ld parts drawn their own nodes at a time%s",
			models, parts, failed ? " (some parts name too many: drawn with the model's first nodes)" : "");
}

/* ---------- runtime: the overshields */

/* (cache_files.c, as a map's tags load) each made Halo 2 biped's overshield
shader, as h2_bipeds_write pairs them: a biped h2\\characters\\..., its
model's folder's shaders\\overshield (a plasma shader, 'spla'). Nothing for
a level with none. HALO_H2_OVERSHIELD_CHECK=1: in a session that made the
level, compared with the made table */
void h2_bipeds_overshields_map_loaded(void)
{
	char const *off = getenv("HALO_H2_OVERSHIELD_READER");
	struct tag_iterator iterator;
	long tag_index, differ = 0;
	short count = 0;

	overshields_loaded_count = NONE;
	if (off && !strcmp(off, "0"))
		return;
	tag_iterator_new(&iterator, 'bipd');
	while ((tag_index = tag_iterator_next(&iterator)) != NONE && count < NUMBER_OF_H2_BIPEDS)
	{
		struct biped_definition const *biped = (struct biped_definition const *)tag_get('bipd', tag_index);
		char const *name = tag_get_name(tag_index);
		char shader[256];
		char const *model, *slash;
		long shader_index;

		if (!name || strncmp(name, "h2\\characters\\", 14) || biped->object.model.index == NONE ||
			!(model = tag_get_name(biped->object.model.index)) || !(slash = strrchr(model, '\\')))
		{
			continue;
		}
		snprintf(shader, sizeof(shader), "%.*sshaders\\overshield", (int)(slash + 1 - model), model);
		if ((shader_index = tag_loaded('spla', shader)) == NONE)
			continue;
		overshields_loaded[count].biped = (unsigned long)tag_index;
		overshields_loaded[count].shader = (unsigned long)shader_index;
		count++;
	}
	overshields_loaded_count = count;
	if (getenv("HALO_H2_OVERSHIELD_CHECK"))
	{
		short i, j;

		for (i = 0; i < overshield_count; i++)
		{
			for (j = 0; j < count && overshields_loaded[j].biped != overshields[i].biped; j++)
				;
			if (j == count || overshields_loaded[j].shader != overshields[i].shader)
				differ++;
		}
		if (count != overshield_count)
			differ++;
		platform_log("h2 bipeds: overshields check: %d made, %d found, %ld differ", overshield_count, count, differ);
	}
	if (count)
		platform_log("h2 bipeds: %d overshield shaders found by name", count);
}

/* ---------- runtime: the globals' player values */

/* (h2_bipeds_globals_map_loaded) globals_write's host on the loaded tags:
offsets from the tag cache's base (pointers there are the tags' own); a
block it asks for is the one there, of as many elements, or it fails */
struct loaded_host
{
	unsigned char *base;
	boolean failed;
	long found_count;
	struct { long index; unsigned long group; } found[16];
};

static unsigned char *loaded_at(void *context, unsigned long offset)
{
	return ((struct loaded_host *)context)->base + offset;
}

static unsigned long loaded_block(void *context, unsigned long field, long count, unsigned long element_size)
{
	struct loaded_host *h = context;
	struct tag_block const *block = (struct tag_block const *)(h->base + field);

	(void)element_size;
	if (block->count != count || !block->address)
	{
		h->failed = TRUE;
		return 0;
	}
	return (unsigned long)((unsigned char *)block->address - h->base);
}

static boolean loaded_failed(void *context)
{
	return ((struct loaded_host *)context)->failed;
}

static long loaded_kit_find(void *context, unsigned long group, char const *name)
{
	struct loaded_host *h = context;
	struct tag_iterator iterator;
	long index;

	tag_iterator_new(&iterator, (long)group);
	while ((index = tag_iterator_next(&iterator)) != NONE)
	{
		if (tag_get_name(index) && !strcmp(tag_get_name(index), name))
		{
			if (h->found_count < (long)(sizeof(h->found) / sizeof(h->found[0])))
			{
				h->found[h->found_count].index = index;
				h->found[h->found_count++].group = group;
			}
			return index;
		}
	}
	return NONE;
}

static unsigned long loaded_root_of(void *context, long index)
{
	struct loaded_host *h = context;
	long k;

	for (k = 0; k < h->found_count; k++)
	{
		if (h->found[k].index == index)
			return (unsigned long)((unsigned char *)tag_get((long)h->found[k].group, index) - h->base);
	}
	return 0;
}

/* (cache_files.c, as a map's tags load) the level's globals' player values
Halo 2's again from its MCC map, as h2_bipeds_write's globals_write writes
them as it is made: the look, magnetism and action hold, the player
information's speeds and accelerations, the falling damage... - a fix to
them needs no new level. HALO_H2_BIPED_VALUES_READER=0: the level's own.
HALO_H2_WEAPON_VALUES_CHECK=1: the globals' bytes changed (none on a level
this builder made); HALO_H2_WEAPON_VALUES_PERTURB=1 (a test's): a few
spoiled first */
static void h2_bipeds_children_reset(void);

void h2_bipeds_globals_map_loaded(char const *map_name)
{
	char const *off = getenv("HALO_H2_BIPED_VALUES_READER");
	char const *directory = map_name ? h2_maps_directory_of(map_name) : NULL;
	struct h2_biped_host host;
	struct loaded_host loaded;
	struct h2_cache *cache;
	char why[160];
	long globals;
	unsigned char *root, *before = NULL;

	/* (the last map's units' children forgotten) */
	h2_bipeds_children_reset();
	if ((off && !strcmp(off, "0")) || !directory || !h2_level_is_halo2_map(directory, map_name))
		return;
	memset(&loaded, 0, sizeof(loaded));
	loaded.base = (unsigned char *)physical_memory_get_tag_cache_base_address();
	if ((globals = loaded_kit_find(&loaded, 'matg', "globals\\globals")) == NONE)
		return;
	if (!(cache = h2_cache_open(directory, map_name, why, sizeof(why))))
		return;
	memset(&host, 0, sizeof(host));
	host.context = &loaded;
	host.cache = cache;
	host.at = loaded_at;
	host.block = loaded_block;
	host.failed = loaded_failed;
	host.kit_find = loaded_kit_find;
	host.root_of = loaded_root_of;
	host.link = (unsigned long)loaded.base;
	root = (unsigned char *)tag_get('matg', globals);
	if (getenv("HALO_H2_WEAPON_VALUES_PERTURB"))
	{
		struct game_globals *g = (struct game_globals *)root;

		if (g->player_control.count > 0)
			((struct game_globals_player_control *)g->player_control.address)->look_acceleration_time = 9.0f;
		if (g->player_information.count > 0)
			((struct game_globals_player_information *)g->player_information.address)->run_forward_speed = 0.1f;
	}
	{
		struct game_globals *g = (struct game_globals *)root;
		struct { unsigned char *at; unsigned long size; char const *what; } parts[7];
		static char const *const damage_names[4] = { "globals\\falling", "globals\\distance", "globals\\vehicle_collision",
			"globals\\vehicle_killed_unit" };
		long part_count = 0, k;

		parts[part_count].at = root, parts[part_count].size = sizeof(struct game_globals), parts[part_count++].what = "root";
		if (g->player_control.count > 0)
			parts[part_count].at = g->player_control.address, parts[part_count].size = sizeof(struct game_globals_player_control),
				parts[part_count++].what = "player control";
		if (g->player_information.count > 0)
			parts[part_count].at = g->player_information.address, parts[part_count].size = sizeof(struct game_globals_player_information),
				parts[part_count++].what = "player information";
		for (k = 0; k < 4; k++)
		{
			long damage = loaded_kit_find(&loaded, 'jpt!', damage_names[k]);

			if (damage != NONE)
				parts[part_count].at = (unsigned char *)tag_get('jpt!', damage), parts[part_count].size = sizeof(struct damage_effect_definition),
					parts[part_count++].what = damage_names[k];
		}
		if (getenv("HALO_H2_WEAPON_VALUES_CHECK") && (before = malloc(sizeof(struct game_globals) +
			sizeof(struct game_globals_player_control) + sizeof(struct game_globals_player_information) +
			4 * sizeof(struct damage_effect_definition))) != NULL)
		{
			unsigned long at = 0;

			for (k = 0; k < part_count; at += parts[k].size, k++)
				memcpy(before + at, parts[k].at, parts[k].size);
		}
		globals_write(&host, globals);
		if (before)
		{
			unsigned long at = 0;

			for (k = 0; k < part_count; at += parts[k].size, k++)
			{
				unsigned long changed = 0, b;

				for (b = 0; b < parts[k].size; b++)
					changed += before[at + b] != parts[k].at[b];
				platform_log("h2 weapon values: check: globals\\globals %s: %lu bytes changed%s", parts[k].what, changed,
					loaded.failed ? " (a block of another count: not all written)" : "");
			}
			free(before);
		}
	}
	/* each made biped's own values (biped_values_write), on its tag
	h2\\characters\\<name>_mp\\<name>_mp */
	if (!(off = getenv("HALO_H2_BIPED_VALUES_READER")) || strcmp(off, "0"))
	{
		struct made_biped *biped = malloc(sizeof(*biped));
		short b;

		for (b = 0; biped && b < NUMBER_OF_H2_BIPEDS; b++)
		{
			char name[160];
			long index;
			unsigned char *o, *biped_before = NULL;

			snprintf(name, sizeof(name), "h2\\characters\\%s_mp\\%s_mp", biped_sources[b].name, biped_sources[b].name);
			if ((index = loaded_kit_find(&loaded, 'bipd', name)) == NONE)
				continue;
			memset(biped, 0, sizeof(*biped));
			snprintf(biped->name, sizeof(biped->name), "%s", biped_sources[b].name);
			biped->h2_group = biped_sources[b].group;
			biped->h2_biped_tag = h2_bipeds_made_from(cache, b);
			if (biped->h2_biped_tag == NONE)
				continue;
			o = (unsigned char *)tag_get('bipd', index);
			if (getenv("HALO_H2_WEAPON_VALUES_PERTURB"))
				((struct biped_definition *)o)->biped.jump_velocity = 0.01f;
			if (getenv("HALO_H2_WEAPON_VALUES_CHECK") && (biped_before = malloc(sizeof(struct biped_definition))) != NULL)
				memcpy(biped_before, o, sizeof(struct biped_definition));
			biped_values_write(cache, biped, o);
			if (biped_before)
			{
				unsigned long changed = 0, k;

				for (k = 0; k < sizeof(struct biped_definition); k++)
					changed += biped_before[k] != o[k];
				platform_log("h2 weapon values: check: %s: %lu bytes changed", name, changed);
				free(biped_before);
			}
		}
		free(biped);
	}
	h2_cache_close(cache);
}

/* (piece J) a character's multipurpose map made again alone (h2_biped.h) */
long h2_biped_multipurpose_again(struct h2_biped_host const *host, char const *name, unsigned long const *args)
{
	return multipurpose_make(host, name, (long)args[0], (long)args[1], (long)args[2], args[3] != 0);
}

/* ---- Halo 2's variant child objects on a unit (runtime) ----
A Halo 2 model's variant names child objects ("objects", the variant's
+0x1C, 0x10 each: parent marker, child marker, child object): the Enforcer's
frontal shields - scenery right_shield at its marker right_shield_gen,
left_shield at left_shield_gen (variant "enforcer"; "no_shields" has none).
h2_level.c makes those sceneries (h2\objects\<name>); here each unit made of
a Halo 2 biped gets its variant's children as it is created, attached at
their markers, and a child goes when the region whose damage section's
"destroyed" response (state 4) names its marker as the "destroyed child
object marker" (response +0x48) is destroyed. Read from the scripts' cache
once a kind and variant per map. HALO_H2_NO_UNIT_CHILDREN=1: none. */
#include "objects/objects.h"
#include "game/game.h"
#include "h2_scripts.h"
#include "h2_rules.h"

enum { H2_UNIT_CHILDREN = 4, H2_UNIT_CHILDREN_KINDS = 16 };

struct h2_unit_children
{
	short biped;
	short variant_number;   /* (the object's: its key) */
	short variant;      /* (Halo 2's variant index) */
	short count;
	struct
	{
		char parent_marker[32];
		char child_marker[32];
		char destroyed_by_region[32];   /* (a parent's region whose destruction takes it; "" none) */
		long definition_index;
	} child[H2_UNIT_CHILDREN];
};

static struct
{
	short count;
	struct h2_unit_children kinds[H2_UNIT_CHILDREN_KINDS];
} unit_children;

static void h2_bipeds_children_reset(void)
{
	unit_children.count = 0;
}

/* the Halo 2 biped a unit's definition was made of (h2\characters\NAME_mp\NAME_mp) */
static short unit_children_biped_of(long definition_index)
{
	char const *name = definition_index != NONE ? tag_get_name(definition_index) : NULL;
	char kind[64];
	char const *end;
	short b;

	if (!name || strncmp(name, "h2\\characters\\", 14))
		return NONE;
	name += 14;
	end = strstr(name, "_mp\\");
	if (!end || end - name >= (long)sizeof(kind))
		return NONE;
	memcpy(kind, name, (size_t)(end - name));
	kind[end - name] = 0;
	for (b = 0; b < NUMBER_OF_H2_BIPEDS; b++)
	{
		if (!strcmp(kind, biped_sources[b].name))
			return b;
	}
	return NONE;
}

/* the kind's children for Halo 2's variant index variant (NONE: its default), read once a map */
static struct h2_unit_children const *unit_children_get(short biped, short variant_number)
{
	struct h2_cache *cache = h2_scripts_cache();
	struct h2_unit_children *k;
	struct h2_cache_tag t, model;
	long unit, variant_count, child_count, info_count, section_count, c, s, r;
	unsigned long variants, children, info = 0, sections = 0;
	short variant = NONE, i;

	if (biped == NONE)
		return NULL;
	for (i = 0; i < unit_children.count; i++)
	{
		if (unit_children.kinds[i].biped == biped && unit_children.kinds[i].variant_number == variant_number)
			return &unit_children.kinds[i];
	}
	if (!cache || (unit = h2_bipeds_made_from(cache, biped)) == NONE || !h2_cache_tag_get(cache, unit, &t) ||
		!h2_cache_tag_get(cache, h2_cache_reference(cache, t.address + 0x34), &model) ||
		!h2_cache_block(cache, model.address + 0x50, 0x38, &variant_count, &variants) || variant_count < 1)
	{
		return NULL;
	}
	/* (the object's variant: its number Halo 2's index + 1, h2_bipeds_variant_number;
	none, the unit's default variant, else the one named "default", else the first -
	the variant the biped's regions were made of) */
	if (variant_number > 0 && variant_number <= variant_count)
		variant = (short)(variant_number - 1);
	else
	{
		unsigned long default_name = h2_bipeds_default_variant(cache, unit);
		long v;

		for (v = 0; v < variant_count && variant == NONE; v++)
		{
			char name[64];

			if (default_name && h2_cache_read32(cache, variants + 0x38 * (unsigned long)v) == default_name)
				variant = (short)v;
			else if (!default_name)
			{
				name_of(cache, h2_cache_read32(cache, variants + 0x38 * (unsigned long)v), name, sizeof(name));
				if (!strcmp(name, "default"))
					variant = (short)v;
			}
		}
		if (variant == NONE)
			variant = 0;
	}
	if (unit_children.count >= H2_UNIT_CHILDREN_KINDS)
		return NULL;
	k = &unit_children.kinds[unit_children.count++];
	memset(k, 0, sizeof(*k));
	k->biped = biped;
	k->variant_number = variant_number;
	k->variant = variant;
	if (!h2_cache_block(cache, variants + 0x38 * (unsigned long)variant + 0x1C, 0x10, &child_count, &children))
		return k;
	/* (the model's damage info, 0x60 (0xF8 each): its sections at 0xBC (0x38), their responses at 0xC (0x50)) */
	if (h2_cache_block(cache, model.address + 0x60, 0xF8, &info_count, &info) && info_count > 0)
		h2_cache_block(cache, info + 0xBC, 0x38, &section_count, &sections);
	else
		section_count = 0;
	for (c = 0; c < child_count && k->count < H2_UNIT_CHILDREN; c++)
	{
		unsigned long element = children + 0x10 * (unsigned long)c;
		long child = h2_cache_reference(cache, element + 8);
		char path[256];
		struct h2_cache_tag ct;
		char const *short_name;

		if (child == NONE || !h2_cache_tag_get(cache, child, &ct) || ct.group_tag != 'scen' || !h2_cache_tag_name(cache, child))
			continue;
		short_name = strrchr(h2_cache_tag_name(cache, child), '\\');
		short_name = short_name ? short_name + 1 : h2_cache_tag_name(cache, child);
		snprintf(path, sizeof(path), "h2\\objects\\%s", short_name);
		k->child[k->count].definition_index = tag_loaded('scen', path);
		if (k->child[k->count].definition_index == NONE)
		{
			platform_log("h2 bipeds: %s's child object %s: no scenery %s in the level", biped_sources[biped].name,
				h2_cache_tag_name(cache, child), path);
			continue;
		}
		name_of(cache, h2_cache_read32(cache, element), k->child[k->count].parent_marker, 32);
		name_of(cache, h2_cache_read32(cache, element + 4), k->child[k->count].child_marker, 32);
		/* (the region whose destroyed response names this marker) */
		for (s = 0; s < section_count && !k->child[k->count].destroyed_by_region[0]; s++)
		{
			long response_count;
			unsigned long responses;

			if (!h2_cache_block(cache, sections + 0x38 * (unsigned long)s + 0xC, 0x50, &response_count, &responses))
				continue;
			for (r = 0; r < response_count; r++)
			{
				unsigned long response = responses + 0x50 * (unsigned long)r;
				char marker[32];

				unsigned char const *state = h2_cache_at(cache, response + 0x20, 2);

				if (!state || (short)read16(state) != 4)
					continue;
				name_of(cache, h2_cache_read32(cache, response + 0x48), marker, sizeof(marker));
				if (marker[0] && !strcmp(marker, k->child[k->count].parent_marker))
				{
					name_of(cache, h2_cache_read32(cache, response + 0x1C), k->child[k->count].destroyed_by_region, 32);
					break;
				}
			}
		}
		platform_log("h2 bipeds: %s variant %d: child object %s at '%s', taken by its region '%s'", biped_sources[biped].name,
			(int)variant, path, k->child[k->count].parent_marker, k->child[k->count].destroyed_by_region);
		k->count++;
	}
	return k;
}

static boolean unit_children_on(void)
{
	static int off = -1;

	if (off < 0)
		off = getenv("HALO_H2_NO_UNIT_CHILDREN") && atoi(getenv("HALO_H2_NO_UNIT_CHILDREN"));
	return !off && h2_rules_level() && game_connection() != _game_connection_network_client;
}

/* (objects.c, object_new: the unit complete) */
void h2_bipeds_children_create(long object_index)
{
	struct object_datum *object;
	struct h2_unit_children const *k;
	short i;

	if (!unit_children_on() || !(object = object_try_and_get(object_index)) ||
		!TEST_FLAG(_object_mask_unit, object->object.type))
	{
		return;
	}
	k = unit_children_get(unit_children_biped_of(object->definition_index), object->object.variant_number);
	for (i = 0; k && i < k->count; i++)
	{
		struct object_placement_data data;
		long child;

		object_placement_data_new(&data, k->child[i].definition_index, NONE);
		data.position = object->object.position;
		child = object_new(&data);
		if (child == NONE)
			continue;
		object_attach_to_marker(object_index, k->child[i].parent_marker, child, k->child[i].child_marker);
	}
}

/* (damage.c, object_destroy_region: the region named destroyed) */
void h2_bipeds_children_region_destroyed(long object_index, char const *region_name)
{
	struct object_datum *object;
	struct h2_unit_children const *k;
	short i;

	if (!unit_children_on() || !region_name || !(object = object_try_and_get(object_index)) ||
		!TEST_FLAG(_object_mask_unit, object->object.type))
	{
		return;
	}
	k = unit_children_get(unit_children_biped_of(object->definition_index), object->object.variant_number);
	for (i = 0; k && i < k->count; i++)
	{
		long child;

		if (!k->child[i].destroyed_by_region[0] || strcmp(k->child[i].destroyed_by_region, region_name))
			continue;
		for (child = object->object.first_child_object_index; child != NONE; )
		{
			struct object_datum *c = object_get(child);
			long next = c->object.next_object_index;

			if (c->definition_index == k->child[i].definition_index)
			{
				if (getenv("HALO_H2_UNIT_CHILDREN_LOG"))
					platform_log("h2 bipeds: %08lx's region %s destroyed: its child %08lx at '%s' goes", (unsigned long)object_index,
						region_name, (unsigned long)child, k->child[i].parent_marker);
				object_delete(child);
			}
			child = next;
		}
	}
}
