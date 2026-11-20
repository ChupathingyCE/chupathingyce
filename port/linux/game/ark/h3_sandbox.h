/*
H3_SANDBOX.H

A Halo 3 level's sandbox (h3_sandbox.c, compiled as a part of h2_level.c):
the Halo 3 weapons, grenades and vehicles its scenario can place, made from
the level's own MCC map by h3_weapons.c and h3_vehicles.c - the readers the
h3/animation lane wrote on h2_weapons.c's and h2_vehicles.c's writers - and
written into the Halo 3 level's tags (h3_level.c) as they are written into a
Halo 2 level's: each weapon on its kit stand-in with Halo 3's values, models,
first-person arms and graphs; each vehicle on its own rig. h3_level.c then
places the real Halo 3 weapon or vehicle where the scenario places it, and
starts the players with Halo 3's.

A Halo 2 level's weapons are made on h2_level.c's own build (its Halo 2
cache, shaders, bitmaps, models); here a build of their own with no Halo 2
map: what the writers read of a Halo 2 cache is guarded (the stand-in's values
stay where Halo 2's would have been read). The tags are laid out from a first
index the Halo 3 level gives: the weapons' and vehicles' (h2_weapons_tag_count:
H2W_TAGS_PER_WEAPON each, the vehicles', grenades', sounds'), then their
shaders, their model shaders, their bitmaps and their models.

PC only (as h3_level.c; the Vita's Halo 3 is later milestones).
*/

#ifndef H3_SANDBOX_H
#define H3_SANDBOX_H

#include "cseries.h"

struct h3_sandbox;
struct h3_cache;
struct region;

/* what the scenario can place: its weapon and vehicle palettes' tag paths
(objects\weapons\rifle\assault_rifle\assault_rifle; strings the caller keeps
until h3_sandbox_free) */
struct h3_sandbox_palettes
{
	char const *const *weapons;
	long weapon_count;
	char const *const *vehicles;
	long vehicle_count;
	/* (a mission's characters, h3_characters.c) the scenario's tag data's
	address in the map (0: none - no characters made) and whether a point is
	in the level made (its structure BSP; NULL: every point is) */
	unsigned long scenario;
	boolean (*in_level)(void *context, float const *point);
	void *context;
};

/* the sandbox's bitmaps' pixels given to the level's file: their offset in it */
typedef unsigned long (*h3_sandbox_pixels_put)(void *context, void const *pixels, unsigned long size);

/* (h3vita) the sandbox's made bitmaps' pixels put into the level's file now
(each's offset remembered for h3_sandbox_write, the pixels freed): what the
builder's heap held until the writes goes to the file's kept memory at once */
void h3_sandbox_pixels_flush(struct h3_sandbox *sandbox, h3_sandbox_pixels_put put, void *context);

/* ---------- prototypes/H3_SANDBOX.C */

/* the sandbox gathered from the level's map (maps_directory/name.map, open
as cache - the level's, borrowed; NULL: opened here): the
weapons the palettes name and the players' start, the grenades, the vehicles
the palette names; their kit stand-ins found in kit (the globals' pack, as
read). NULL if nothing could be made (why logged) or HALO_H3_SANDBOX=0 */
struct h3_sandbox *h3_sandbox_gather(
	char const *maps_directory,
	char const *name,
	struct h3_cache *cache,
	struct h3_sandbox_palettes const *palettes,
	unsigned char *kit);

/* its tags (from the first index the level gives it) and its models' parts
(the vertex and index buffer entries they take) */
long h3_sandbox_tag_count(
	struct h3_sandbox const *sandbox);

long h3_sandbox_buffer_count(
	struct h3_sandbox const *sandbox);

/* a tag's group and name (index from its first), and the groups its tag
instance says it is (parents: 0xFFFFFFFF none) */
void h3_sandbox_tag_name(
	struct h3_sandbox const *sandbox,
	long index,
	unsigned long *group,
	char *text,
	long size);

void h3_sandbox_tag_parents(
	unsigned long group,
	unsigned long *parent,
	unsigned long *grandparent);

/* the tags written into the level's tag data (region: the level's struct
region, its table at instances - tag_count of them, named -, the sandbox's
first tag first) - the kit
already placed in it, the scenario (its root) with its starting profile -,
their models' buffers at the tables' entries from buffer_first, their pixels
through put; FALSE (why logged) if they could not be */
boolean h3_sandbox_write(
	struct h3_sandbox *sandbox,
	struct region *region,
	unsigned long instances,
	long tag_count,
	long first,
	unsigned long scenario_root,
	unsigned long vertex_table,
	unsigned long index_table,
	long buffer_first,
	h3_sandbox_pixels_put put,
	void *context);

/* a made weapon's or vehicle's tag (index from the sandbox's first) by its
Halo 3 tag path (or its last part), NONE if none is made - good after the
gather */
long h3_sandbox_weapon(
	struct h3_sandbox const *sandbox,
	char const *h3_path);

long h3_sandbox_vehicle(
	struct h3_sandbox const *sandbox,
	char const *h3_path);

/* (a mission's level whose BSP holds no player start) a place beside the
first squad of the player's side in the level: position (3) and facing into
start; FALSE if there is none */
boolean h3_sandbox_friendly_start(
	struct h3_sandbox const *sandbox,
	float *start);

/* whether the kit's frag and plasma grenades carry Halo 3's models and values */
boolean h3_sandbox_grenades_made(
	struct h3_sandbox const *sandbox);

void h3_sandbox_free(
	struct h3_sandbox *sandbox);

#endif
