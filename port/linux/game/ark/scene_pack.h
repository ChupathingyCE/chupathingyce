/*
SCENE_PACK.H

Scene packs (scene_pack.c): a few objects of another map - their tags, and
their bitmaps' pixels and sounds' samples - built on the player's machine
from the player's own maps into one file, then loaded beside whatever map
is loaded. Option B of the Forge design ("Palette packs, built on the Vita,
loaded with the map": docs/superpowers/specs/2026-10-18-forge-design.md).
Nothing of a map is shipped: a pack is made from a recipe (our own text: a
pack's name, the maps it may be made from, the tags it holds, the groups and
tags it leaves out), on the machine that uses it.

This is the pack itself, with no game state: its recipe, its builder, and
the checks and moves a pack's tags go through before anything reads them.
scene_packs.c puts a pack into the running game (the tag cache, the tag
table, the reads of its pixels and samples); the tools test this alone
(tools/scene_pack_test.c).

A pack file (little-endian):
  the header (SCENE_PACK_HEADER_SIZE bytes): signature, version, sizes and
    offsets, the recipe's hash and the source map's, the source map's name,
    a CRC of the header and one of each section;
  the tags (header.tags_size bytes at header.tags_offset): laid out as a
    map's tag data - a tag header (its scenario none), the tag table, the
    tags' names, the models' vertex and index buffers' headers, the tags
    (each one's root, blocks and data together), the buffers' vertices and
    indices - every pointer linked to SCENE_PACK_LINK_BASE, every tag index
    the pack's own (its first header.root_count tags are the recipe's, in
    its order), every bitmap's pixels and sound's samples at a file offset
    from SCENE_PACK_FILE_BASE;
  the data (header.data_size bytes at header.data_offset): those pixels and
    samples.

A pack is anyone's file: it is checked before anything reads it, as
strictly as a map is (scene_pack_open, scene_pack_load): bounded counts and
sizes, every section in the file, the file's size and CRCs; then its tags
moved with every pointer followed only into the pack and every reference
checked against its own table (tag_relocate.c), then checked by the tag
validator against their groups' schemas with the pack as the only region
and its data as the only file (tag_validate.c), which a pack must pass with
no correction at all. A pack that fails anything is refused (and made
again by the game: scene_packs.c), never trusted in part.
*/

#ifndef __SCENE_PACK_H
#define __SCENE_PACK_H

#include "cseries.h"

#include <stdio.h>

/* ---------- constants */

enum
{
	SCENE_PACK_VERSION = 1,
	/* (the builder's own: a pack of another builder's is made again) */
	SCENE_PACK_BUILDER_VERSION = 2,
	SCENE_PACK_HEADER_SIZE = 128,
	/* (a backdrop's: its structure bsp and scenario taken as this version
	of the builder takes them; another is a pack made again) */
	SCENE_PACK_BACKDROP_VERSION = 2,

	/* a recipe's most lines' worth */
	SCENE_PACK_MAXIMUM_RECIPE_BYTES = 8 * 1024,
	SCENE_PACK_MAXIMUM_MAPS = 16,
	SCENE_PACK_MAXIMUM_ROOTS = 32,
	SCENE_PACK_MAXIMUM_STRIP_GROUPS = 32,
	SCENE_PACK_MAXIMUM_STRIP_TAGS = 48,
	SCENE_PACK_MAXIMUM_TUNES = 4,
	SCENE_PACK_MAXIMUM_KEEPS = 96,
	SCENE_PACK_ANIMATION_NAME_SIZE = 32,
	/* a pack's (and its name's, a map's name's, a tag path's) most */
	SCENE_PACK_NAME_SIZE = 32,
	SCENE_PACK_TAG_PATH_SIZE = 256,
	SCENE_PACK_MAXIMUM_TAGS = 2048,
	/* (a menu backdrop's pack holds a structure bsp: 16 MB of tags, the
	bsp's textures and lightmaps in its data) */
	SCENE_PACK_MAXIMUM_TAG_BYTES = 16 * 1024 * 1024,
	SCENE_PACK_MAXIMUM_DATA_BYTES = 128 * 1024 * 1024,
	SCENE_PACK_MAXIMUM_BUFFERS = 8192,

	/* a reason's room (scene_pack_build, scene_pack_open...) */
	SCENE_PACK_WHY_SIZE = 160,
};

/* where a pack's tags are linked (they are moved to wherever they are
read), and where its pixels' and samples' file offsets count from: past any
map's file (and Custom Edition's combined offsets, 0x40000000 and
0x60000000: custom_edition_cache.c) */
#define SCENE_PACK_LINK_BASE 0x5C000000UL
#define SCENE_PACK_FILE_BASE 0x70000000UL

/* ---------- structures */

struct scene_pack_recipe_tag
{
	unsigned long group_tag;
	char path[SCENE_PACK_TAG_PATH_SIZE];
};

/* a Custom Edition map a recipe may be made from (cemap): its file's name
in the maps folder, without ".map", and its identity - the file's size and
its header's checksum field - which must be the file's */
struct scene_pack_recipe_custom_edition_map
{
	char name[SCENE_PACK_NAME_SIZE];
	unsigned long file_size;
	unsigned long checksum;
};

/* (a recipe's tune line) a tuning of the builder's and the recipe's tag it
changes */
struct scene_pack_recipe_tune
{
	char tuning[SCENE_PACK_NAME_SIZE];
	struct scene_pack_recipe_tag tag;
};

/* a recipe (scene_pack_recipe_parse): its text is

	# a comment
	pack <name>                  the pack's file name (letters, digits, _ -)
	map <name> [<name>...]       the maps it may be made from, the first one
	                             of them in the maps folder used
	cemap <name> <size> <checksum>
	                             (instead of map lines) a Halo Custom Edition
	                             map it may be made from, <name>.map in the
	                             maps folder, of that size in bytes (decimal)
	                             and header checksum (8 hex digits): the
	                             first of them there, of its identity, is
	                             used, with the maps folder's bitmaps.map,
	                             sounds.map and loc.map
	tag <group> <path>           a tag it holds, with all it needs (in order:
	                             the pack's first tags)
	unit multiplayer             a tag it holds: the source map's
	                             multiplayer unit (its globals' player
	                             biped), whatever its name (a protected
	                             map's have none)
	strip <group>                a group left out: references to its tags
	                             name none
	strip <group> <path>         a tag left out, the same way
	tune <tuning> <group> <path> one of the recipe's tags (a tag line's)
	                             changed by one of the builder's own
	                             tunings (scene_pack.c: "flyable_pelican",
	                             the campaign's Pelican made one players
	                             fly) once the pack is built; a tag that is
	                             not what the tuning expects is left as it
	                             is (said in the log)
	backdrop                     (the menus' live backdrop: menu_backdrop.c)
	                             its tag lines may name the source's
	                             structure bsp (sbsp: read from the map's
	                             bsp section, its vertices with it) and its
	                             scenario (scnr: a copy of it with its skies,
	                             scenery and their palette, its detail
	                             objects' palette and nothing else)
	h3spartan [<look>]           Halo 3's Spartan or Elite, made by the game
	                             (scene_pack_blocks_make_h3_spartan)
	blocks                       Forge's building blocks, made by the
	                             game (scene_pack_blocks.c: their
	                             scenery, models, collision models and
	                             shaders, textured with the source map's
	                             bitmaps), the pack's objects after its
	                             tag lines' (a recipe may have them alone;
	                             an Xbox map's only)
	keep <graph>|<animation>     (an animation graph's, antr) an animation
	                             it keeps: a graph with keep lines keeps
	                             those whole, and each other animation of it
	                             takes a kept one's frames (the first kept of
	                             its type: no bytes of its own), so a scene
	                             that holds a few poses carries those alone

groups as four characters ("bipd", "snd!"), paths as the tags' own names
("characters\cyborg_mp\cyborg_mp"). A root of the multiplayer unit has the
group 'unit' and an empty path. */
struct scene_pack_recipe
{
	char name[SCENE_PACK_NAME_SIZE];
	short map_count;
	short root_count;
	short strip_group_count;
	short strip_tag_count;
	char maps[SCENE_PACK_MAXIMUM_MAPS][SCENE_PACK_NAME_SIZE];
	struct scene_pack_recipe_tag roots[SCENE_PACK_MAXIMUM_ROOTS];
	unsigned long strip_groups[SCENE_PACK_MAXIMUM_STRIP_GROUPS];
	struct scene_pack_recipe_tag strip_tags[SCENE_PACK_MAXIMUM_STRIP_TAGS];
	/* of what the recipe says, as parsed (not its comments or spacing),
	and the builder's version: two machines with the same recipe have the
	same hash */
	unsigned long long hash;
	/* (after the hash: tools/scene_pack_test.c knows the fields before it)
	the Custom Edition maps it may be made from (cemap lines, which a
	recipe has instead of map lines) */
	short custom_edition_map_count;
	struct scene_pack_recipe_custom_edition_map custom_edition_maps[SCENE_PACK_MAXIMUM_MAPS];
	/* (tune lines) the tunings of its tags */
	short tune_count;
	struct scene_pack_recipe_tune tunes[SCENE_PACK_MAXIMUM_TUNES];
	/* (keep lines) the animations kept, each its graph's path and its name */
	short keep_count;
	struct scene_pack_recipe_keep
	{
		char graph[SCENE_PACK_TAG_PATH_SIZE];
		char animation[SCENE_PACK_ANIMATION_NAME_SIZE];
	} keeps[SCENE_PACK_MAXIMUM_KEEPS];
	/* (a blocks line) Forge's building blocks made into it */
	boolean blocks;
	/* (a globals line: a Halo 2 level's pack, h2_level.c) the game's globals
	may be one of its tags */
	boolean globals;
	/* (a backdrop line: the menus' backdrop, menu_backdrop.c) a structure
	bsp and a scenario's copy may be its tags */
	boolean backdrop;
	/* (an h3spartan line: Halo 3's Spartan on the source's cyborg's skeleton,
	from the player's MCC files, h3_spartan.c, its biped a root of the recipe
	as SCENE_PACK_H3_SPARTAN_BIPED; a Vita or a machine without them: the
	build fails, nothing else changes) */
	boolean h3_spartan;
	char h3_look[200];
	/* (an h2levels line: its maps are Halo 2 levels this machine built, in
	t:\h2: the look of a Halo 2 model on a Halo CE level, player_looks.c) */
	boolean h2_levels;
	/* (scene_pack_recipe_mcc_fallback) its map lines' Xbox maps are not in
	the maps folder: its maps are MCC's own Halo CE maps of the same names
	(the Halo content folder's halo1\maps, version 13), made its cemap
	lines with no size or checksum (whatever MCC's version of them), read as
	Custom Edition maps are (scene_pack_ce.c) */
	boolean mcc_maps;
};

/* what a pack file's header says, once checked (scene_pack_open) */
struct scene_pack_info
{
	unsigned long file_size;
	unsigned long tags_offset;
	unsigned long tags_size;
	unsigned long data_offset;
	unsigned long data_size;
	long tag_count;
	long root_count;
	unsigned long tags_crc;
	unsigned long data_crc;
	unsigned long builder_version;
	unsigned long long recipe_hash;
	unsigned long long source_hash;
	char source_map[SCENE_PACK_NAME_SIZE];
};

/* what a build did (scene_pack_build) */
struct scene_pack_build_result
{
	char map[SCENE_PACK_NAME_SIZE];
	long tag_count;
	long stripped_count;
	long buffer_count;
	unsigned long tag_bytes;
	unsigned long data_bytes;
	unsigned long file_bytes;
	/* the source map's bytes inflated (or read) for it */
	unsigned long long source_bytes;
	/* how far it is, 0 to 100 (read by another thread while it builds:
	the source map read twice, each read half of it) */
	long volatile progress;
	unsigned long long microseconds;
	char why[SCENE_PACK_WHY_SIZE];
	/* (a pack built in memory: its path SCENE_PACK_MEMORY_PREFIX's) the
	whole pack, as its file would be, from malloc (the caller's to free);
	NULL otherwise */
	unsigned char *memory;
	unsigned long memory_size;
	/* (a pack built in memory whose pixels and samples stay in its source:
	SCENE_PACK_SOURCE_PREFIX) memory holds its header, its tags and its first
	held_bytes of data (the pixels the build made itself); the rest of its
	data, piece by piece, is the source map's decompressed copy's bytes
	(pieces, from malloc, the caller's to free) */
	unsigned long held_bytes;
	struct scene_pack_source_piece *pieces;
	long piece_count;
};

/* (scene_pack_build_many) a pack path that begins so is a pack built in
memory, not a file: nothing written (no .tmp, no rename, no source index
saved), the pack's bytes given in its result (memory, memory_size). Command
mode's build-out (port/linux/game/command.c): the owner's rule, nothing
made written to the card */
#define SCENE_PACK_MEMORY_PREFIX "memory:"

/* (scene_pack_build_many) a pack path that begins so is a pack built in
memory as SCENE_PACK_MEMORY_PREFIX's, but its pixels and samples are not
copied: each stays where it is in the source map's decompressed copy (the
game's cache partition's, scene_pack_copy_source), and the result says
where (pieces). The Vita's Command build-out: ~96 MB the Vita's memory
cannot hold. A build whose map has no such copy fails (its why says so) */
#define SCENE_PACK_SOURCE_PREFIX "source:"

/* a piece of a SCENE_PACK_SOURCE_PREFIX pack's data: where in the pack's
data (from its data's start), where in the source map's copy, how long */
struct scene_pack_source_piece
{
	unsigned long offset;
	unsigned long source;
	unsigned long size;
};

/* the decompressed copy of an Xbox map the game keeps (its cache
partition's, scene_packs.c), which a build reads in place of inflating the
compressed map again: open gives that copy of the map so named (NULL:
none), and a token unchanged says is still the same copy. The builder takes
it only when its header is the maps folder's map's, byte for byte, with a
checksum (scene_pack_source_seekable), and checks the header and the
token again after each of its reads: a copy changed meanwhile is a build
made again from the compressed map */
struct scene_pack_copy_source
{
	FILE *(*open)(char const *map, unsigned long *token);
	boolean (*unchanged)(unsigned long token);
};

/* ---------- globals */

/* (scene_packs.c) called by a build and a load after each of their reads
and writes, with its bytes (source: a read of the source map, else of a
pack; write: a write of a pack): the build's thread pauses and paces itself
there. NULL (the tools): nothing */
extern void (*scene_pack_io_hook)(unsigned long bytes, boolean source, boolean write);
/* (tools/scene_pack_test.c, NULL in the game) a recipe with blocks: the
source's tag data as the build has read it, before the blocks are made
from it (and the rest of the pack taken from it): the tool damages it */
extern void (*scene_pack_blocks_source_hook)(unsigned char *tags, unsigned long size, unsigned long address,
	unsigned long instances, long count);
/* (scene_packs.c) the maps' copies a build may read, NULL for none */
extern struct scene_pack_copy_source const *scene_pack_copy_source;

/* ---------- prototypes/SCENE_PACK.C */

/* a recipe's text (length bytes, at most SCENE_PACK_MAXIMUM_RECIPE_BYTES):
FALSE, and why, if it is not one */
boolean scene_pack_recipe_parse(
	char const *text,
	long length,
	struct scene_pack_recipe *recipe,
	char *why,
	long why_size);

/* (the tools) the size of a recipe, as the game's objects have it */
unsigned long scene_pack_recipe_size(
	void);

/* (port) a recipe of Xbox maps (map lines) none of which is in
maps_directory made one of MCC's own Halo CE maps of the same names, those
of them in mcc_directory (the Halo content folder's halo1\maps): its map
lines cemap lines of any size and checksum, which only MCC's own maps
(version 13) answer, its hash told so - a backdrop's too (the menus' live
background from MCC's a30: its structure bsp read and made this build's as
the game's loader makes it, scene_pack_ce.c). A recipe with blocks, a
globals line, a Halo 2 or Halo 3 line, or an Xbox map there is left as it
is. TRUE if it was made so */
boolean scene_pack_recipe_mcc_fallback(
	struct scene_pack_recipe *recipe,
	char const *maps_directory,
	char const *mcc_directory);

/* (the tools) a recipe's Custom Edition map's name, by index, or NULL */
char const *scene_pack_recipe_custom_edition_map(
	struct scene_pack_recipe const *recipe,
	short index);

/* whether a recipe's root is the source map's multiplayer unit (unit
multiplayer) rather than a tag of a path */
boolean scene_pack_recipe_root_is_multiplayer_unit(
	struct scene_pack_recipe_tag const *tag);

/* the source map's identity (its header, its file's size) as packs record
it, and its name in maps_directory (the first of the recipe's maps there,
which an Xbox map must be; or of its Custom Edition maps, of the identity
the recipe gives): FALSE if none of them is there, and why (why may be
NULL: "extinction.map is not in the maps folder") */
boolean scene_pack_source_check(
	struct scene_pack_recipe const *recipe,
	char const *maps_directory,
	char *map_name,
	long map_name_size,
	unsigned long long *source_hash,
	char *why,
	long why_size);

/* a map of the recipe's (one of its map lines' names) in maps_directory,
an Xbox map, and its identity (source_hash, as packs record it): FALSE if
the recipe does not name it or it is not there */
boolean scene_pack_source_identity(
	struct scene_pack_recipe const *recipe,
	char const *maps_directory,
	char const *map,
	unsigned long long *source_hash);

/* whether a build reads the map in place, seeking to what it wants, with
no inflating: the maps folder's map is not compressed, or its decompressed
copy (scene_pack_copy_source) is there and is that map's - its header the
maps folder's map's byte for byte, with a checksum (a map without one
cannot be told from another version of it) */
boolean scene_pack_source_seekable(
	char const *maps_directory,
	char const *map);

/* the same, without why */
boolean scene_pack_source_find(
	struct scene_pack_recipe const *recipe,
	char const *maps_directory,
	char *map_name,
	long map_name_size,
	unsigned long long *source_hash);

/* the pack the recipe makes, from its source map in maps_directory,
written to pack_path (by way of pack_path.tmp, renamed when whole). The
source map's tag data is read into scratch (scratch_size bytes) when it fits
there, otherwise into the heap. *cancel (if not NULL) becoming non-zero
stops it. FALSE, and why (result->why), if it was not made */
boolean scene_pack_build(
	struct scene_pack_recipe const *recipe,
	char const *maps_directory,
	char const *pack_path,
	void *scratch,
	unsigned long scratch_size,
	int volatile const *cancel,
	struct scene_pack_build_result *result);

/* (a Custom Edition source's) the packs of `count` recipes of Custom Edition
maps (or MCC's own), each written to its pack_paths[i] with its results[i]
as scene_pack_build's, the map they are made of read and converted once:
the recipes are expected to come to the same map (scene_packs.c groups those
whose maps are the same list), and one that does not is made of its own,
read for it. TRUE if every one was made */
boolean scene_pack_build_custom_edition_shared(
	struct scene_pack_recipe const *const *recipes,
	short count,
	char const *maps_directory,
	char const *const *pack_paths,
	void *scratch,
	unsigned long scratch_size,
	int volatile const *cancel,
	struct scene_pack_build_result *results);

/* (count) packs, each of its recipe, made in one read of one source map,
map (each recipe must name it: scene_pack_source_identity), Xbox maps only:
the source's tag data read once (into scratch when it fits, otherwise the
heap) and its pixels and samples once, for all of them, each pack written
to its pack_paths' (by way of .tmp, renamed when whole), results[i] its
recipe's. A recipe whose pack fails takes none of the others with it.
TRUE if every one was made */
boolean scene_pack_build_many(
	struct scene_pack_recipe const *const *recipes,
	short count,
	char const *map,
	char const *maps_directory,
	char const *const *pack_paths,
	void *scratch,
	unsigned long scratch_size,
	int volatile const *cancel,
	struct scene_pack_build_result *results);

/* (port: one kit for every Halo 2 level, h2_level.c) the pack recipe makes,
made in memory from a pack already made (source_tags: its tags section as
its file holds them; source_info its header's) whose recipe holds every tag
this one may - the builder's walk over the source's tags, its strips left
out - its pixels and samples left at their offsets in the source pack's
data. *tags (*tags_size bytes, from allocate, or malloc if NULL) as a pack
file's tags are: scene_pack_place_tags next. FALSE, and why */
boolean scene_pack_filter(
	struct scene_pack_recipe const *recipe,
	unsigned char *source_tags,
	struct scene_pack_info const *source_info,
	void *(*allocate)(unsigned long size),
	unsigned char **tags,
	unsigned long *tags_size,
	long *tag_count,
	char *why,
	long why_size);

/* a pack's tags section read (info's) to destination as its file holds them
(linked at SCENE_PACK_LINK_BASE, not moved), its CRC checked; not its data
(scene_pack_data_check). FALSE, and why */
boolean scene_pack_read_tags(
	FILE *file,
	struct scene_pack_info const *info,
	void *destination,
	char *why,
	long why_size);

/* a pack's tags (info: its tags_size, tag_count, data_size and source map),
read or made at destination as its file holds them: moved there, every
pointer followed only into them and every reference checked against their
own table, then checked by the tag validator (scene_pack_load's checks past
its reads and CRCs). FALSE, and why, if they cannot be used */
boolean scene_pack_place_tags(
	void *destination,
	struct scene_pack_info const *info,
	char *why,
	long why_size);

/* the pack file at path: its header checked (bounds, sizes, the file's
size, the header's CRC) and what it says (info). FALSE, and why, if it is
not a pack this build reads */
boolean scene_pack_open(
	char const *path,
	struct scene_pack_info *info,
	char *why,
	long why_size);

/* the pack's tags (info->tags_size bytes) read to destination (4-aligned),
both sections' CRCs checked, the tags moved there and checked (above): FALSE,
and why, if the pack cannot be used, and nothing at destination may be */
boolean scene_pack_load(
	char const *path,
	struct scene_pack_info const *info,
	void *destination,
	char *why,
	long why_size);

/* the same checks of the pack open as file, from its start, by the same
handle: scene_pack_load_file reads the tags it checks once, into
destination, and the data's CRC through that handle (whose later reads are
then of the file checked, not of another put at its path meanwhile) */
/* (a SCENE_PACK_SOURCE_PREFIX pack's bytes: its header, its tags and the
first held_bytes of its data) as scene_pack_open_file */
boolean scene_pack_open_file_held(
	FILE *file,
	unsigned long held_bytes,
	struct scene_pack_info *info,
	char *why,
	long why_size);
boolean scene_pack_open_file(
	FILE *file,
	struct scene_pack_info *info,
	char *why,
	long why_size);

boolean scene_pack_load_file(
	FILE *file,
	struct scene_pack_info const *info,
	void *destination,
	char *why,
	long why_size);

/* the same, its data's CRC not read again when data_checked (the caller
knows that file's data good: scene_packs.c's record of the file checked,
its size and time unchanged since) - its tags read, checked by their CRC
and moved and checked as ever */
boolean scene_pack_load_file_checked(
	FILE *file,
	struct scene_pack_info const *info,
	void *destination,
	boolean data_checked,
	char *why,
	long why_size);

/* a pack's data (its pixels and samples) checked by its CRC, through file:
FALSE, and why, if it is not what the header says */
boolean scene_pack_data_check(
	FILE *file,
	struct scene_pack_info const *info,
	char *why,
	long why_size);

/* a pack loaded at tags (scene_pack_load) given its tag indices in the
game's table: its tag i is (first_salt + i) << 16 | (first_index + i), as
are its references, tag indices and sounds' own indices; and its pixels'
and samples' file offsets moved by file_delta (several packs loaded at once
read from files of their own: scene_packs.c) */
void scene_pack_rebase(
	void *tags,
	long first_index,
	long first_salt,
	long file_delta);

/* (a loaded pack, before scene_pack_rebase) each place its tags point into
its data (its pixels and samples): every file data field of the schema's
(a sound's samples...) and every bitmap's pixels, given to visit with the
tag's index, its group, the field's file offset (the pack's own:
SCENE_PACK_FILE_BASE + the place in its data) to read or change, its size,
and the field (pixels FALSE: a struct tag_data; TRUE: a bitmap's struct
bitmap_data) */
typedef void (*scene_pack_file_data_proc)(void *context, long tag_index, unsigned long group_tag,
	long *file_offset, long size, void *field, boolean pixels);
void scene_pack_file_data_each(
	void *tags,
	scene_pack_file_data_proc visit,
	void *context);

/* (the packs' worker, scene_packs.c, while it builds the menus' backdrop;
the H2 level builder, while it builds its globals' pack)
where a pack build's big blob (its tags, up to 16 MB) comes from: NULL the C heap; the
alloc may give NULL (then the C heap). Zeroed memory is not assumed.
Thread-local: only that thread's builds use them. */
extern __thread void *(*scene_pack_big_alloc)(unsigned long size);
extern __thread void (*scene_pack_big_free)(void *data, unsigned long size);
/* (the same) a big block of scene_pack_big_alloc's grown in place to
new_size bytes, its first size bytes kept (moved to the start it gives):
NULL if it cannot be (then a new block, the bytes copied to it). NULL: none */
extern __thread void *(*scene_pack_big_grow)(void *data, unsigned long size, unsigned long new_size);
/* (the same) TRUE: a big buffer - the blob, a backdrop's structure bsp, the
source's tag data when the scratch is too small - the hooks' alone, never
the C heap's: none from them is the build's failure ("no room in the memory
window"), not a C heap the system's libraries share run dry (the Vita) */
extern __thread boolean scene_pack_big_only;
extern __thread boolean (*scene_pack_heap_room)(unsigned long size);
/* (the packs' worker, scene_packs.c) where a build's copy of its source
map's tag data comes from when the scratch it was given is too small for it
(the menus' backdrop loaded beside the menus takes 8.7 MB of the tag cache's
free room, which a campaign map's 15 MB of tag data then no longer fits):
NULL the C heap; the alloc may give NULL (then the C heap). Thread-local */
extern __thread void *(*scene_pack_scratch_alloc)(unsigned long size);
extern __thread void (*scene_pack_scratch_free)(void *data, unsigned long size);

/* a pack given its tag indices once (scene_pack_rebase, its first tag at
old_first_index) given others: first_index, first_salt, and its file
offsets moved by file_delta more */
void scene_pack_rebase_again(
	void *tags,
	long old_first_index,
	long first_index,
	long first_salt,
	long file_delta);

/* a loaded pack's tag table (32 bytes a tag, cache_files.c's instances)
and count, and its vertex and index buffers' header (cache_files.c's struct
cache_file_tag_header) */
void *scene_pack_instances(
	void *tags,
	long *count);

/* the pack's tag of group and path (case aside), as an index into its
table, or NONE */
long scene_pack_find(
	void *tags,
	unsigned long group_tag,
	char const *path);

/* a group's four characters, terminated */
char *scene_pack_group_text(
	unsigned long group_tag,
	char text[5]);

#endif
