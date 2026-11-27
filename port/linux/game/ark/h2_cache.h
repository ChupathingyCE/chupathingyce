/*
H2_CACHE.H

A retail Xbox Halo 2 cache file (version 8), read for the game: its header,
its tag index and names, its tags' data and its structure BSP's, and the
resources it keeps in itself or in the shared maps (shared.map,
single_player_shared.map, mainmenu.map) beside it. Read only, every count,
offset and address checked against what was read: the file is anyone's.

Layouts: the halo2-decomp project (a local checkout,
include/halo2/cache_layout.h, docs/CACHE-MAPS.md and docs/TAGS-OBJECTS.md:
the header, the index, the tag entries, the structure descriptors and the
resource selectors), and for the structure BSP's blocks the layouts checked
against the owner's 01a_tutorial.map (h2_level.c says which).
*/

#ifndef __H2_CACHE_H
#define __H2_CACHE_H

#include "cseries.h"

/* ---------- constants */

enum
{
	H2_CACHE_HEADER_SIZE = 0x800,
	H2_CACHE_VERSION = 8,
	/* the address the tag index is read to (every retail map's) */
	H2_CACHE_BASE_ADDRESS = 0x80061000UL,
	H2_CACHE_NAME_SIZE = 256,
	H2_CACHE_WHY_SIZE = 160,
};

/* ---------- structures */

struct h2_cache;

struct h2_cache_tag
{
	unsigned long group_tag;
	unsigned long handle;
	unsigned long address;
	unsigned long size;
};

/* ---------- prototypes/H2_CACHE.C */

/* whether the file at path begins as a Halo 2 Xbox cache does */
boolean h2_cache_file_is_halo2(
	char const *path);

/* whether the file at path is an MCC Halo 2 map (version 13), whose
bitmaps' pixels are in textures.dat, not in the map */
boolean h2_cache_file_is_mcc(
	char const *path);

/* maps_directory/name.map opened, its index and tag data read; NULL and
why if not */
struct h2_cache *h2_cache_open(
	char const *maps_directory,
	char const *name,
	char *why,
	long why_size);

/* (port) an open of its own, never the load's kept one (a user holding it
on another thread) */
struct h2_cache *h2_cache_open_own(
	char const *maps_directory,
	char const *name,
	char *why,
	long why_size);

/* (port) a view of an open MCC map for another thread: its names and
tables shared read only, its files, scratch and segments its own (to
budget bytes; 0 none); base must stay open while it is */
struct h2_cache *h2_cache_open_view(
	struct h2_cache *base,
	unsigned long budget);

void h2_cache_close(
	struct h2_cache *cache);

/* (port, a level's make) every MCC chunk inflated whole kept until the end,
in the build's heap within a budget (HALO_H2_CHUNKS_KEPT_MB, 16 MB), and
copied rather than inflated again: the make's gathers let their segments go
between them (h2_cache_trim) and read many of the same chunks again */
/* (HALO_INFLATE_BENCH=n) zlib and libdeflate timed on shared.map's first n chunks, logged */
void h2_cache_inflate_bench(
	char const *maps_directory,
	long count);

void h2_cache_chunks_begin(
	void);
void h2_cache_chunks_end(
	void);

/* (port) between these, a level's load: the first open of a map is kept
and shared with the game thread's later opens of it (h2_cache.c), closed
at the end (or at an open of another map) */
void h2_cache_keep_begin(void);
void h2_cache_keep_end(void);
/* (port) a user holding the kept one through the play keeps it alive, trimmed
at the window's end: the game thread's later opens of the map are served it
until the next load's begin or another map's open (the world's reads at a
BSP switch during play, h2_world_load.inc) */

/* its name and its scenario's path, as its header says */
char const *h2_cache_name(
	struct h2_cache const *cache);
char const *h2_cache_scenario_path(
	struct h2_cache const *cache);

long h2_cache_tag_count(
	struct h2_cache const *cache);
boolean h2_cache_tag_get(
	struct h2_cache const *cache,
	long index,
	struct h2_cache_tag *tag);
/* a tag's name, or "" */
char const *h2_cache_tag_name(
	struct h2_cache const *cache,
	long index);
/* the tag a salted handle names (NONE: none) */
long h2_cache_tag_from_handle(
	struct h2_cache const *cache,
	unsigned long handle);
/* the scenario's tag */
long h2_cache_scenario_tag(
	struct h2_cache const *cache);

/* size bytes at an address of the tag data (or of the structure BSP
loaded by h2_cache_structure_load), or NULL if they are not all there */
void const *h2_cache_at(
	struct h2_cache const *cache,
	unsigned long address,
	unsigned long size);
/* a tag block at address: its count and elements, every element of
element_size bytes there (FALSE: not) */
boolean h2_cache_block(
	struct h2_cache const *cache,
	unsigned long address,
	unsigned long element_size,
	long *count,
	unsigned long *elements);
/* a tag reference at address: the tag it names (NONE: none or not one) */
long h2_cache_reference(
	struct h2_cache const *cache,
	unsigned long address);
unsigned long h2_cache_read32(
	struct h2_cache const *cache,
	unsigned long address);

/* the scenario's structure BSPs: how many; one loaded (its block read),
its structure BSP tag's address and its lightmap tag's (0: none) */
long h2_cache_structure_count(
	struct h2_cache const *cache);
/* (a campaign level's) structure BSP index's tag, NONE if none */
long h2_cache_structure_tag(
	struct h2_cache const *cache,
	long index);
boolean h2_cache_structure_load(
	struct h2_cache *cache,
	long index,
	unsigned long *structure_bsp,
	unsigned long *lightmap,
	char *why,
	long why_size);

/* an encoded resource reference's size bytes (the top two bits pick the
map: this one, mainmenu, shared, single player shared) read to out */
boolean h2_cache_resource_read(
	struct h2_cache *cache,
	unsigned long reference,
	void *out,
	unsigned long size);

/* (MCC) what h2_cache_resource_read has done so far (every map's reads
in this process): resources read, chunks inflated for them and the bytes
those chunks held - the makers' cost of reading geometry at load
(h2_models_load.inc logs the difference round its gathers) */
void h2_cache_resource_stats(
	unsigned long *reads,
	unsigned long *chunks,
	unsigned long long *bytes);

/* (MCC) which file the data at an address is in: 0 the map, 2 shared.map,
3 single_player_shared.map - a resource reference of its with no file
(selector 0) is that file's */
short h2_cache_address_source(
	struct h2_cache const *cache,
	unsigned long address);

/* (MCC) the inflated data dropped: what h2_cache_at gave before is no
longer valid; later reads inflate again what they want */
/* h2_cache_trim when the segments hold more than bytes (where nothing
h2_cache_at gave is held): TRUE if they were let go */
boolean h2_cache_trim_past(
	struct h2_cache *cache,
	unsigned long bytes);

void h2_cache_trim(
	struct h2_cache *cache);

/* (MCC) size bytes of the decompressed file at a file offset (the
globals' language packs: their string tables), NULL if not there or not
MCC's; valid as h2_cache_at's */
void const *h2_cache_file_bytes(
	struct h2_cache *cache,
	unsigned long offset,
	unsigned long size);

/* whether it is MCC's (PC) Halo 2 map rather than the Xbox's */
boolean h2_cache_is_mcc(
	struct h2_cache const *cache);

/* a bitmap's pixels at an image's reference (size its stored bytes):
the Xbox's resource as it is, MCC's textures.dat entry decompressed (the
top mip); the caller frees; NULL if not */
void *h2_cache_texture_read(
	struct h2_cache *cache,
	unsigned long reference,
	unsigned long size,
	unsigned long *out_size);
/* (the level builder) as h2_cache_texture_read, its buffers where the
segments come from (h2_cache_segment_memory: the memory window); let go
with h2_cache_texture_free. options (NULL: none): its first skip
decompressed bytes not kept (the levels too big to be kept: inflated and
dropped); reduce_log2 > 0 (a single level, 4 bytes a texel, width x
height): averaged 2^k x 2^k as it is inflated, never whole in memory;
expected (0: unknown): the bytes it will be, its buffer made once */
struct h2_cache_texture_options
{
	unsigned long skip;
	int reduce_log2;
	unsigned long width, height;
	unsigned long expected;
};
/* (as h2_cache_texture_read) its first most bytes alone: the chunks past
them not inflated, its room theirs (water_maps: a ripple sheet's top
image of its 16 MB entry) */
void *h2_cache_texture_read_most(
	struct h2_cache *cache,
	unsigned long reference,
	unsigned long size,
	unsigned long most,
	unsigned long *out_size);
/* (as h2_cache_texture_read, the C heap's - free() -, with the options:
the lightmap pages, decoded at the atlas's size) */
void *h2_cache_texture_read_options(
	struct h2_cache *cache,
	unsigned long reference,
	unsigned long size,
	struct h2_cache_texture_options const *options,
	unsigned long *out_size);
void *h2_cache_texture_read_span(
	struct h2_cache *cache,
	unsigned long reference,
	unsigned long size,
	unsigned long skip,
	unsigned long most,
	unsigned long *out_size);
void *h2_cache_texture_read_window(
	struct h2_cache *cache,
	unsigned long reference,
	unsigned long size,
	struct h2_cache_texture_options const *options,
	unsigned long *out_size);
/* (a direct level made without its checks) a textures.dat entry's bytes
once inflated, its last chunk alone inflated to count them */
boolean h2_cache_texture_inflated_size(
	struct h2_cache *cache,
	unsigned long reference,
	unsigned long size,
	unsigned long *inflated);
/* (h2_cache_texture_read_window's pixels let go) */
void h2_cache_texture_free(
	struct h2_cache *cache,
	void *pixels);

/* (the menus) the index-th string of the globals' language pack (0
English), UTF-8, as a unicode string list's strings are kept in MCC's maps,
and its string id (string_id: NULL not wanted) (FALSE and "" for the
Xbox's, or none) */
boolean h2_cache_localized_string(
	struct h2_cache *cache,
	long language,
	long index,
	unsigned long *string_id,
	char *out,
	long out_size);

/* (player models, h2_biped.c) a string id's text (MCC's maps: FALSE and ""
for the Xbox's), and a tag by group and name (NONE: none) */
boolean h2_cache_string(
	struct h2_cache *cache,
	unsigned long id,
	char *out,
	long out_size);
long h2_cache_tag_find(
	struct h2_cache const *cache,
	unsigned long group_tag,
	char const *name);

/* (the level builder) where an MCC cache's inflated segments come from
(and its shared maps', now and when opened): segment_alloc may give NULL
(then the C heap); segment_free gets back what it gave, with its size.
Set before the first read; the segments are freed where they came from. */
void h2_cache_segment_memory(
	struct h2_cache *cache,
	void *(*segment_alloc)(unsigned long size),
	void (*segment_free)(void *data, unsigned long size));

/* (the sound loader, h2_sound.c) an MCC cache's inflated segments kept to
about `bytes` (and its shared maps'): past it the oldest are let go and
inflated again when read again; the newest few always stay, so a pointer
h2_cache_at gave holds over the next few reads only (0: no budget, the
default - every segment kept for the cache's life) */
void h2_cache_segment_budget(
	struct h2_cache *cache,
	unsigned long bytes);
/* (logs) the chunks inflated and the segments let go, its shared maps' added */
/* whether a read of the tags gave nothing for want of memory since last
asked (a segment not made): the reads give NULL alike for a damaged map */
boolean h2_cache_memory_failed(
	struct h2_cache *cache);
/* (port) the segments every open has not made for want of memory, all told
(h2_world.c: a BSP's world refused for memory, said so) */
unsigned long h2_cache_memory_failures_count(void);

/* (port) the bytes its inflated segments hold, its shared maps' too */
unsigned long h2_cache_segment_bytes(
	struct h2_cache const *cache);

void h2_cache_segment_counts(
	struct h2_cache const *cache,
	unsigned long *inflated,
	unsigned long *evicted);

/* (port, piece H) the maps' and textures.dat's file reads (their bytes and
microseconds) and chunk inflates (their microseconds) so far, every
thread's and every open's: a load's steps and the make log the change */
void h2_cache_io_counts(
	unsigned long *reads,
	unsigned long *read_bytes,
	unsigned long *read_us,
	unsigned long *inflates,
	unsigned long *inflate_us);
/* a read of textures.dat made outside h2_cache (h2_texture_source.c's own
file): counted with the rest */
/* (a map's or a .dat's file, read at offsets) its reads unbuffered: one
read of the card each (h2_cache.c) */
void h2_file_unbuffered(
	FILE *file);

void h2_cache_io_read_noted(
	unsigned long bytes,
	unsigned long microseconds);

#endif
