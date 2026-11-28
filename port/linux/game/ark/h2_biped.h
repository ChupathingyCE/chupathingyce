/*
H2_BIPED.H

Halo 2's multiplayer player models made this engine's bipeds (h2_biped.c):
the Spartan (objects\characters\masterchief\masterchief_mp) and the Elite
(objects\characters\elite\elite_mp), read from the player's own MCC Halo 2
maps (shared.map holds them) while a Halo 2 level is built (h2_level.c), and
written into that level's cache file as tags of this engine's: each one's
render model as a model of its own skeleton (its nodes, markers, regions and
permutations, its five levels of detail, skinned vertices of up to two nodes
each), its shaders as model shaders (the base map, and a multipurpose map
made from its change colour map: the primary colour's mask where this
engine's model shaders read colour change, the secondary's kept beside it),
its collision model, its animation graph (h2_animation.c) and a biped made
from the stock multiplayer biped with these in it. The game's multiplayer
unit is then the Spartan: every player and bot on the level.

h2_level.c calls this module through a host (struct h2_biped_host): the
bitmaps it streams into the file, the tag data it lays out, the tag table.
Nothing of h2_level.c's own structures is seen here, so the two files merge
apart.

The customization side (h2_player_models_*: the models and their
permutations by name, a player's choice and colours, and the identity every
machine of a game agrees on) is at the end.
*/

#ifndef __H2_BIPED_H
#define __H2_BIPED_H

/* (h2_biped_host.bitmap_described) not described: make it */
#define H2_BIPED_NOT_DESCRIBED (-2L)

#include "cseries.h"

struct h2_cache;

/* ---------- constants */

enum
{
	/* the bipeds made, in this order */
	H2_BIPED_SPARTAN = 0,
	H2_BIPED_ELITE,
	/* (a campaign level's: its characters' bipeds, for the AI - h2_ai.c;
	made only on a level of Halo 2's campaign, from the level's own map) */
	H2_BIPED_FIRST_CAMPAIGN,
	H2_BIPED_CAMPAIGN_ELITE = H2_BIPED_FIRST_CAMPAIGN,
	H2_BIPED_GRUNT,
	H2_BIPED_JACKAL,
	H2_BIPED_MARINE,
	H2_BIPED_HUNTER,
	H2_BIPED_BUGGER,
	H2_BIPED_BRUTE,
	/* (the cutscenes' characters: Tartarus and the Prophets) */
	H2_BIPED_TARTARUS,
	H2_BIPED_PROPHET_MERCY,
	H2_BIPED_PROPHET_TRUTH,
	H2_BIPED_PROPHET_MINOR,
	H2_BIPED_PROPHET_REGRET,
	H2_BIPED_REGRET_HOLOGRAM,
	/* (the cutscenes' people, each its own model: Miranda, Lord Hood,
	Cortana, the female Marine, the crewmen and officers, the Elite Ranger) */
	H2_BIPED_MIRANDA,
	H2_BIPED_HOOD,
	H2_BIPED_CORTANA,
	H2_BIPED_MARINE_FEMALE,
	H2_BIPED_CREWMAN,
	H2_BIPED_ELITE_RANGER,
	/* (the Flood: combat forms, the carrier, the infection form - a
	creature of Halo 2's, made a biped here -, the juggernaut) */
	H2_BIPED_FLOOD_HUMAN,
	H2_BIPED_FLOOD_ELITE,
	H2_BIPED_FLOOD_CARRIER,
	H2_BIPED_FLOOD_INFECTION,
	H2_BIPED_FLOOD_JUGGERNAUT,
	/* (the Sentinels - the constructor a creature - and 343 Guilty Spark) */
	H2_BIPED_SENTINEL,
	H2_BIPED_SENTINEL_CONSTRUCTOR,
	H2_BIPED_SENTINEL_ENFORCER,     /* (a vehicle of Halo 2's its AI drives: a flying biped here) */
	H2_BIPED_MONITOR,
	/* (the Heretics: the Elites, their leader's hologram, the Grunts) */
	H2_BIPED_HERETIC,
	H2_BIPED_HERETIC_HOLOGRAM,
	H2_BIPED_HERETIC_GRUNT,
	/* (High Charity's: the Gravemind, Regret and the Monitor taken) */
	H2_BIPED_GRAVEMIND,
	H2_BIPED_FLOOD_REGRET,
	H2_BIPED_MONITOR_INFECTED,
	/* (Halo 2's bipeds of another's model - their own biped tag, the
	other's model, graph and collision model: their default variant theirs) */
	H2_BIPED_FIRST_SHARED,
	H2_BIPED_HERETIC_LEADER = H2_BIPED_FIRST_SHARED,
	H2_BIPED_HERETIC_LEADER_CINEMATIC,
	H2_BIPED_SENTINEL_HALO1,
	H2_BIPED_MARINE_MASSIVE,
	NUMBER_OF_H2_BIPEDS,

	/* this engine's levels of detail (super low .. super high) */
	H2_BIPED_DETAIL_LEVELS = 5,
	H2_BIPED_MAXIMUM_PERMUTATIONS = 16,
	H2_BIPED_MAXIMUM_REGIONS = 12,
	H2_BIPED_NAME_SIZE = 32,
};

/* ---------- structures */

/* what h2_level.c lends: its cache, its bitmaps (gathered), and while the
tags are written its tag data (a region of it: offsets, linked at link) and
its table */
struct h2_biped_host
{
	void *context;
	struct h2_cache *cache;

	/* (gathering) a Halo 2 bitmap tag's first image as a bitmap of the
	level (its index), or NONE; a bitmap made here (its pixels as this
	engine's file holds them, given to the level), or NONE */
	long (*bitmap_for)(void *context, long h2_tag);
	long (*bitmap_made)(void *context, char const *path, unsigned short width, unsigned short height,
		unsigned short format, unsigned short mipmaps, unsigned char *pixels, unsigned long size);
	/* a Halo 2 base map with the cut-out of its bump map's alpha in its own
	alpha, as this engine's model shader tests it, or NONE */
	long (*alpha_base_for)(void *context, long base_tag, long bump_tag);
	/* (h2_materials.h) a Halo 2 bump map as this engine's (P8), within the
	level's bump budget, or NONE */
	long (*bump_for)(void *context, long h2_tag);
	/* (optional) a made bitmap's full-size pixels (the same format) for the
	PC tier's sidecar (h2_texture_sidecar.h): given to the level */
	void (*sidecar_made)(void *context, long bitmap, unsigned short width, unsigned short height, unsigned char *pixels,
		unsigned long size);
	/* (optional) a Halo 2 bitmap's mean alpha, 0..1, < 0 unknown (a
	material's detail map: a part of the specular mask) */
	float (*mean_alpha)(void *context, long h2_tag);

	/* (optional, the Vita tier) the build's spill file: a model's vertices
	and strips put there once made, read back straight into the tag data as
	they are written (put: FALSE, no room on the card - they stay in
	memory) */
	boolean (*spill_put)(void *context, void const *bytes, unsigned long size, unsigned long *offset);
	boolean (*spill_get)(void *context, void *bytes, unsigned long size, unsigned long offset);
	/* (optional, piece J) what the next bitmap_made's bitmap is made of -
	a character's multipurpose map: its tags and flag - for the level to
	make it again alone at load (h2_biped_multipurpose_again) */
	void (*recipe)(void *context, unsigned long const *args, int count);
	/* (optional: a direct level made without its checks) the next bitmap -
	its recipe given - described, not made: its size, no pixels, made again
	at load (h2_level.c bitmap_described); H2_BIPED_NOT_DESCRIBED if the
	level would not leave it out (make it, bitmap_made) */
	long (*bitmap_described)(void *context, char const *path, unsigned short width, unsigned short height,
		unsigned short format, unsigned short mipmaps, unsigned long size);

	/* (writing) the tag data: size bytes zeroed (their offset); where an
	offset is now; a block at a field (its elements' offset); a data field;
	a reference (group and tag id; id 0xFFFFFFFF none) */
	unsigned long (*alloc)(void *context, unsigned long size);
	unsigned char *(*at)(void *context, unsigned long offset);
	unsigned long (*block)(void *context, unsigned long field, long count, unsigned long element_size);
	void (*data)(void *context, unsigned long field, void const *bytes, unsigned long size);
	void (*reference)(void *context, unsigned long field, unsigned long group, unsigned long id);
	boolean (*failed)(void *context);
	unsigned long link;

	/* (writing) the tag table: a tag's id, its root's offset (0: none),
	its root set; a bitmap of the level's tag id; a tag of the globals'
	pack by group and name (its index, NONE) */
	unsigned long (*tag_id)(void *context, long index);
	unsigned long (*root_of)(void *context, long index);
	void (*set_root)(void *context, long index, unsigned long offset);
	unsigned long (*bitmap_tag_id)(void *context, long bitmap);
	long (*kit_find)(void *context, unsigned long group, char const *name);
	/* the level's own converted shader of a Halo 2 shader (gathering: NONE
	if it has none), and that shader's tag id (writing) */
	short (*level_shader)(void *context, long h2_shader);
	unsigned long (*level_shader_id)(void *context, short level_shader);

	/* (writing) this engine's collision BSP at field from Halo 2's at h2
	(its eight blocks), its surfaces' material material (NONE: theirs) */
	boolean (*collision_convert)(void *context, unsigned long field, unsigned long h2, short material);

	/* (writing) the scenario's root (its offset; 0 none): tests may place
	the bipeds in it (HALO_H2_BIPED_SHOWCASE) */
	unsigned long scenario_root;

	/* (writing) the vertex and index buffers' tables (offsets: 12 bytes an
	entry) and the first entry free in each for these models' parts */
	unsigned long vertex_table;
	unsigned long index_table;
	long first_buffer;
	long first_index_buffer;
	/* (models at load, feat/h2-models-at-load: h2_level.c's
	h2_models_load.inc) models_only: only the bipeds' render models,
	animation graphs and collision models written - no globals, shaders or
	biped tags - as a level's models are made again from its MCC map at
	load. 0 (FALSE) as every host is made */
	boolean models_only;
	/* (models at load: the level's other bipeds' tags written, these not -
	after a models_only pass of the same bipeds, whose collision models'
	outcome the biped tags take) */
	boolean no_models;
	/* (with no_models) each collision model written as a stub - its root,
	materials and damage regions, no nodes, BSPs or pathfinding spheres: what
	the effects pass (h2_effects.c) reads and writes into it after -, made
	whole from the map as the level loads; a biped whose collision model the
	models_only pass could not make keeps the stock one's copy */
	boolean collision_stub;
	/* (optional, gathering) called as each biped's make begins: a host that
	gathers on a cache whose inflated data is kept (the makers at load,
	h2_models_load.inc) trims it there - nothing of an earlier biped's is
	read again */
	void (*trim)(void *context);
};

/* the models gathered */
struct h2_bipeds;

/* what was made, for the log and the reports */
struct h2_biped_report
{
	char name[H2_BIPED_NAME_SIZE];
	long nodes;
	long markers;
	long regions;
	long permutations;
	long triangles[H2_BIPED_DETAIL_LEVELS];
	long vertices[H2_BIPED_DETAIL_LEVELS];
	long animations;
	long animation_bytes;
	long model_bytes;
	long bitmap_bytes;
	boolean made;
};

/* ---------- prototypes/H2_BIPED.C */

/* the player models of the cache's shared map read (models, shaders,
bitmaps given to the host, animations decoded); NULL if none could be or
HALO_H2_NO_BIPEDS */
/* (the make, piece H) the bipeds' animation graphs made ahead on a thread
of their own over a view of cache, for h2_bipeds_gather to take where made
from its very inputs; end: the thread joined, what was not taken let go */
void h2_bipeds_graphs_ahead_begin(
	struct h2_cache *cache);
void h2_bipeds_graphs_ahead_end(
	void);

struct h2_bipeds *h2_bipeds_gather(
	struct h2_biped_host const *host);

void h2_bipeds_free(
	struct h2_bipeds *bipeds);

/* the tags they are (after the level's), and each one's group and name */
long h2_bipeds_tag_count(
	struct h2_bipeds const *bipeds);
boolean h2_bipeds_tag_describe(
	struct h2_bipeds const *bipeds,
	long index,
	unsigned long *group,
	char *name,
	long name_size);

/* the vertex and index buffer entries their models' parts take */
long h2_bipeds_buffer_count(
	struct h2_bipeds const *bipeds);

/* their tags written, the first of them first_tag in the table; the
globals' multiplayer unit made the Spartan */
boolean h2_bipeds_write(
	struct h2_bipeds *bipeds,
	struct h2_biped_host const *host,
	long first_tag);

/* (after h2_bipeds_write) a biped's tag index in the level's table, NONE
if it was not made (the AI's units, h2_ai.c) */
long h2_bipeds_biped_tag(
	struct h2_bipeds const *bipeds,
	short biped);

/* (before h2_bipeds_write: the scenario's placements) a biped's tag index
were its tags written from first_tag, NONE if it was not made */
long h2_bipeds_biped_tag_planned(
	struct h2_bipeds const *bipeds,
	short biped,
	long first_tag);

/* (runtime) a biped's name (its tags' "h2\\characters\\<name>_mp\\...") and
the Halo 2 biped it is made of, NULL past the last */
char const *h2_bipeds_source_name(
	short biped);
char const *h2_bipeds_source_path(
	short biped);
/* the Halo 2 biped (or creature) a biped is made of on the cache's level
(a campaign level's Spartan and Elite: the Master Chief and the Arbiter),
NONE */
/* a biped definition's (its tag's id) Halo 2 overshield shader's tag id,
NONE; its colour by the overshield's amount (0 to 1) */
long h2_bipeds_overshield_shader(
	long definition_index);
void h2_bipeds_overshield_colour(
	float amount,
	float colour[3]);
/* (cache_files.c, as a map's tags load) the loaded level's overshield
shaders found by name (without it, HALO_H2_OVERSHIELD_READER=0, only a
level made in this session has them) */
void h2_bipeds_overshields_map_loaded(void);
/* (cache_files.c) the level's globals' player values Halo 2's again from
its MCC map (HALO_H2_BIPED_VALUES_READER=0: the level's own) */
void h2_bipeds_globals_map_loaded(char const *map_name);
/* Halo 2's variant child objects (the Enforcer's frontal shields): made
and attached as a unit is created; one goes when the region that takes it
is destroyed (h2_biped.c) */
void h2_bipeds_children_create(long object_index);
void h2_bipeds_children_region_destroyed(long object_index, char const *region_name);

long h2_bipeds_made_from(
	struct h2_cache *cache,
	short biped);
/* (runtime) the Halo 2 group of the tag it is made of ('bipd', 'crea', 'vehi') */
unsigned long h2_bipeds_source_group(
	short biped);
/* the Halo 2 biped's (or creature's) default model variant (its object's,
a string id: Halo 2's heretic_leader "leader_her"), 0 none */
unsigned long h2_bipeds_default_variant(
	struct h2_cache *cache,
	long h2_object_tag);
/* (any Halo 2 unit or creature's name) the biped made for it, NONE */
short h2_bipeds_for_unit_name(
	char const *name);
/* a Halo 2 biped or creature flies (its character physics' flying flag) */
boolean h2_bipeds_tag_flies(
	struct h2_cache *cache,
	long h2_object_tag);

/* (a placement of one, the scenario's bipeds') its Halo 2 variant (a string
id; none: the placed biped's default model variant) as the placement's
variant number (h2_ai_object_variant: the variant's index + 1), NONE if
none or the placed biped is not of the model it is made from */
short h2_bipeds_variant_number(
	struct h2_cache *cache,
	short biped,
	long h2_biped_tag,
	unsigned long variant_name);

/* Halo 2's hologram pass (H2EK shaders\shader_passes\hologram\
hologram_simple) of a shaders\shader_templates\hologram\hologram shader
- Regret's regret_holo -: its pixel constants (C1.a background_invert, C2.a
background_value, C3 background_color, C4.a alpha_value) and its alpha,
2 alpha_value (base_map.b mult_map.b + add_map.b) - the scanline and noise
maps -, here their mean over the maps as the pass's transforms tile them */
struct h2_hologram_pass
{
	float invert, value, colour[3];
	float alpha;
};

/* (FALSE: not of that template) */
boolean h2_hologram_pass_read(
	struct h2_cache *cache,
	long h2_shader,
	struct h2_hologram_pass *pass);

/* the pass's colour of a texel x (0..1 each): R0 = lerp(x, 1 - x, invert),
then times 2 value and times 2 colour, each stage clamped as the combiners */
void h2_hologram_pass_texel(
	struct h2_hologram_pass const *pass,
	float const x[3],
	float out[3]);

/* what was made of one (FALSE: not made) */
boolean h2_bipeds_report(
	struct h2_bipeds const *bipeds,
	short biped,
	struct h2_biped_report *report);

/* (piece J) a character's multipurpose map made again alone: the level's
recipe's arguments (change colour, base, illumination tags, glow_full), its
name as its path has it; the bitmap made through host->bitmap_made */
long h2_biped_multipurpose_again(struct h2_biped_host const *host, char const *name, unsigned long const *args);

#endif
