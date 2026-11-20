/*
H3_SPARTAN.H

Halo 3's multiplayer Spartan read from the player's own MCC files
(h3_spartan.c): objects\characters\masterchief\mp_masterchief\mp_masterchief,
whose tags every multiplayer map of halo3\maps holds and whose vertices and
largest textures are pages of shared.map. Nothing of it is shipped: it is
read on the player's machine, a look at a time (a permutation of each region),
and made what this engine's model tags hold: triangles by material, skinned to
at most two nodes of the skeleton it is given, its colour change masks made a
multipurpose map.

Halo 3's Spartan has Halo CE's cyborg's and Halo 2's Spartan's biped (the same
bones, the same lengths: only its default pose, arms forward, and its fingers
and toes differ), so it is rebound by node name: each vertex moved by its
nodes' default pose to the skeleton's default pose, and a node the skeleton
lacks (a finger, a toe) skinned to its nearest ancestor it has. It then plays
that skeleton's animations as they are.

PC only for now (h3_spartan_offered): the read inflates shared.map's page of
535 MB up to the model.
*/

#ifndef __H3_SPARTAN_H
#define __H3_SPARTAN_H

#include "cseries.h"
#include "h2_animation.h"

struct h3_cache_read_control;
struct h3_cache;

/* ---------- constants */

enum
{
	H3_SPARTAN_MAXIMUM_NODES = 64,
	H3_SPARTAN_MAXIMUM_MATERIALS = 64,
	H3_SPARTAN_MAXIMUM_PARTS = 256,
	/* (this engine's parts' strips: 65535 indices, six a triangle at worst) */
	H3_SPARTAN_MAXIMUM_PART_TRIANGLES = 13000,
	H3_SPARTAN_MAXIMUM_BITMAPS = 160,
	/* an armoury's (below) slots: an object's regions */
	H3_SPARTAN_MAXIMUM_SLOTS = 8,
	H3_SPARTAN_NAME_SIZE = 64,
	/* this engine's model node's bytes (a skeleton's nodes) */
	H3_SPARTAN_MODEL_NODE_SIZE = 0x9C,
	H3_SPARTAN_WHY_SIZE = 160,

	/* this engine's bitmap formats of the maps made here: A8R8G8B8, swizzled,
	with its mip levels (a multipurpose map, a bump map's normals) */
	H3_SPARTAN_FORMAT_A8R8G8B8 = 11,
	/* (h3vita) this engine's P8 bump: a bump map made for the sandbox on the
	Vita tier (h3_level.c's bump_p8_make, as the level's bump maps) */
	H3_SPARTAN_FORMAT_P8_BUMP = 17,
};

/* the map files tried for its tags, the first found (any multiplayer map) */
#define H3_SPARTAN_MAPS { "guardian", "zanzibar", "riverworld", "construct", "isolation", "chill" }

/* ---------- structures */

/* the skeleton it is rebound to: each node's name (Halo CE's "bip01 l thigh"
and Halo 2's "l thigh" are both taken) and its default inverse matrix as a
model tag holds it: scale, forward, left, up, position */
struct h3_spartan_skeleton
{
	short node_count;
	char names[H3_SPARTAN_MAXIMUM_NODES][32];
	short parents[H3_SPARTAN_MAXIMUM_NODES];
	float inverse[H3_SPARTAN_MAXIMUM_NODES][13];
	/* (a skeleton of a loaded level's biped, not the source map's: its nodes as
	a model tag holds them, node_size bytes each, for the model made) */
	unsigned long node_size;
	unsigned char *nodes;
};

struct h3_spartan_vertex
{
	float position[3];
	float normal[3];
	float binormal[3];
	float tangent[3];
	float uv[2];
	unsigned char nodes[2];
	float weight;               /* the first node's */
};

struct h3_spartan_part
{
	short material;
	/* (an armoury's) which slot and which of its choices it is drawn for */
	short slot;
	short choice;
	long vertex_count;
	struct h3_spartan_vertex *vertices;
	long triangle_count;
	unsigned short *triangles;  /* three each */
};

/* a bitmap as this engine's file holds it: DXT levels one after another (14,
15, 16 as this engine's formats), or a swizzled A4R4G4B4 of one level */
struct h3_spartan_bitmap
{
	char name[H3_SPARTAN_NAME_SIZE];
	unsigned short width;
	unsigned short height;
	unsigned short format;
	unsigned short mipmaps;
	unsigned char *pixels;
	unsigned long size;
};

/* a material as this engine's model shaders and Halo 2's per-pixel model pass
(h2_materials.h) draw it: base map, detail map, bump map, multipurpose map
(red the specular mask, green the self-illumination, blue the primary colour's
mask, alpha the secondary's) */
struct h3_spartan_material
{
	char name[H3_SPARTAN_NAME_SIZE];
	short base_bitmap;
	short multipurpose_bitmap;   /* NONE: none */
	short detail_bitmap;         /* NONE: none */
	short bump_bitmap;           /* NONE: none */
	boolean change_colour;
	boolean visor;               /* the player's visor colour tints its reflection */
	boolean illuminated;         /* green of the multipurpose map glows */
	boolean additive;            /* Halo 3 adds it: lights, drawn only by their glow */
	float detail_scale[2];
	float bump_scale[2];
	/* the highlight: its colour facing and glancing (0xRRGGBB), its curve
	(Halo 2's _h2_specular_type_*), its strength 0..1 */
	unsigned long specular_color, specular_glancing_color;
	unsigned char specular_type;
	float specular_strength;
	/* the reflection: its strength and colour facing and glancing */
	float reflection, reflection_glancing;
	float reflection_tint[3];
	float illum_color[3];
	/* (a material of its self-illumination alone - an assault rifle's display:
	no base map, a black albedo, added - its base bitmap the illumination map
	times its colour and intensity; digit: 1 the tens' and 2 the ones' digit of
	a weapon's counter, the model's digit_bitmaps drawn by its rounds) */
	boolean illum_only;
	short digit;
};

struct h3_spartan
{
	short part_count;
	struct h3_spartan_part parts[H3_SPARTAN_MAXIMUM_PARTS];
	short material_count;
	struct h3_spartan_material materials[H3_SPARTAN_MAXIMUM_MATERIALS];
	short bitmap_count;
	struct h3_spartan_bitmap bitmaps[H3_SPARTAN_MAXIMUM_BITMAPS];
	float radius;
	long triangles;
	long vertices;
	unsigned long source_bytes;  /* what was read of shared.map, inflated */
	/* the slots and each one's choices: one and one but for an armoury */
	short slot_count;
	short slot_choices[H3_SPARTAN_MAXIMUM_SLOTS];
	/* its own idle (combat:rifle:idle) on the skeleton it was given, a frame's
	nodes after another's (NULL: no idle was read), this engine's rotations; which
	nodes it moves */
	short idle_frames;
	struct h2_animation_pose *idle_poses;
	boolean idle_node_set[H3_SPARTAN_MAXIMUM_NODES];
	/* the preview's weapon's hand: NONE none */
	short hand_node;
	float hand_translation[3];
	float hand_rotation[4];
	/* (a weapon's counter: its numbers bitmap's ten images, tinted; NONE each
	when it has none) */
	short digit_bitmaps[10];
};

/* the armour a model offers (h3_spartan_catalog): each region's permutations
that have a mesh and are classic Halo 3 armour (h3_spartan.c's list, and where
it came from), in the model's order, and an identity of them all: the same list
on every machine with the same files. A region with none has no row.

An armoury is a model's whole catalog in one pack, a look's armour a choice of
what to draw of it, so that a change of armour builds nothing: the look text
"model=spartan,armoury=1". Its regions are its slots: slot 0 all that has no
choice, then a slot for each region of the catalog with choices, in its order,
choice 0 of a slot the model's own and each other its catalog's, in order
(player_looks.c's look_slots) */
enum
{
	H3_MODEL_SPARTAN = 0,
	H3_MODEL_ELITE,
	NUMBER_OF_H3_MODELS,

	H3_CATALOG_MAXIMUM_REGIONS = 16,
	H3_CATALOG_MAXIMUM_PERMUTATIONS = 80,
	H3_CATALOG_NAME_SIZE = 32,
};

struct h3_model_catalog
{
	boolean read;
	short region_count;
	struct
	{
		char name[H3_CATALOG_NAME_SIZE];
		short permutation_count;
		char permutations[H3_CATALOG_MAXIMUM_PERMUTATIONS][H3_CATALOG_NAME_SIZE];
	} regions[H3_CATALOG_MAXIMUM_REGIONS];
	unsigned long identity;
};

/* ---------- prototypes/H3_SPARTAN.C */

/* whether the row is offered: not on the Vita, not with HALO_H3_MODELS=0, and
the player's content folder holds shared.map and a multiplayer map */
boolean h3_spartan_offered(
	void);

/* halo3\maps' folder (ending in a separator): FALSE if there is none */
boolean h3_spartan_maps_directory(
	char *out,
	unsigned long out_size);

/* a model's catalog read from the player's files: FALSE if it cannot be */
boolean h3_model_catalog_read(
	short model,
	struct h3_model_catalog *catalog);

/* (a look with own=1: Halo 3's Elite in the menus' preview, whose bones are no
other model's) the model's own skeleton, its nodes made as a model tag holds them;
FALSE if it cannot be read */
boolean h3_spartan_own_skeleton(
	char const *look,
	struct h3_spartan_skeleton *skeleton);

/* (its nodes: this file's allocator) */
void h3_spartan_nodes_free(
	unsigned char *nodes);

/* the Spartan read and rebound to skeleton; look: the model (no "model=": the
Spartan; "model=elite") and the permutations wanted,
"model=elite,head=mp_scythe,chest=mp_blades" (NULL or a region not named:
base, or its first with a mesh); NULL, and why, if it cannot be, or control's
word stopped it (control may be NULL: h3_cache.h) */
struct h3_spartan *h3_spartan_read(
	struct h3_spartan_skeleton const *skeleton,
	char const *look,
	struct h3_cache_read_control const *control,
	char *why,
	long why_size);

void h3_spartan_free(
	struct h3_spartan *spartan);

/* any Halo 3 render model (the 'mode' tag at mode_tag of an open cache) read
on its own skeleton - its nodes as the model holds them, in its order, so a
part's node indices are the model's own -: a weapon's, the first-person
arms' (h3_weapons.c). look names a region's permutation ("region=permutation",
NULL: each region's "base", else its first with a mesh); no idle, no hands.
NULL and why if it cannot be. The cache stays the caller's */
struct h3_spartan *h3_model_read(
	struct h3_cache *cache,
	long mode_tag,
	char const *look,
	struct h3_cache_read_control const *control,
	char *why,
	long why_size);

/* every region's permutations, written to the log (h3_spartan_command) */
void h3_spartan_log_permutations(
	void);

/* (h3_hud.c) bitmap tags' first images' top levels as RGBA, all their pages
read in one pass: pixels[i] NULL where a tag could not be read (DXT1/3/5 and
A8R8G8B8 are); each freed with h3_bitmap_free */
void h3_bitmaps_read_rgba(
	struct h3_cache *cache,
	long const *tags,
	long count,
	unsigned char **pixels,
	unsigned long *widths,
	unsigned long *heights);
void h3_bitmap_free(
	unsigned char *pixels);

#endif
