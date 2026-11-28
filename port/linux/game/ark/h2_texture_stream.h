/*
H2_TEXTURE_STREAM.H
Texture streaming for a Halo 2 level, the Xbox's way: each large 2D texture's
small levels (its base, 64 px and under) resident from the level's load, the
larger levels read from the level file when a surface is drawn with them, by
the Vita's texture decoder in the background (port/vita/platform/vita_textures.c).
On by default for Halo 2 levels (HALO_H2_TEXTURE_STREAMING=0 turns it off);
Halo CE's maps are untouched. h2_texture_stream.c.
*/

#ifndef __H2_TEXTURE_STREAM_H
#define __H2_TEXTURE_STREAM_H

struct bitmap_data;

/* ---------- constants */

enum
{
	/* the most textures a level streams */
	H2_TEXTURE_STREAM_MAXIMUM_SOURCES = 4096,
	/* the resident bases' memory, by default (HALO_H2_TEXTURE_BASE_MB) */
	H2_TEXTURE_STREAM_BASE_DEFAULT_MB = 8
};

/* ---------- structures */

/* a streamed texture's detail: the bytes of the levels above its base in the
level file, for the decoder's read */
struct h2_texture_stream_detail
{
	long tag_index;
	/* the file's offset and size of the levels wanted, the largest first,
	down to the level above the base: one contiguous run */
	long file_offset, bytes;
	/* the levels above the base the texture has in the file, and those the
	run holds (1..dropped) */
	unsigned long dropped, levels;
};

/* ---------- prototypes/H2_TEXTURE_STREAM.C */

/* whether the switch is on (the environment) */
int h2_texture_stream_enabled(void);
/* a map loaded (cache_files.c, after its tags): a Halo 2 level's large
textures made base-resident, their bases read; any other map's cleared */
void h2_texture_stream_map_loaded(char const *map_name);
/* (cache_files.c) the bases' blocks given back as the map unloads */
void h2_texture_stream_map_unloaded(void);
/* the resident base's hardware format of a bitmap the stream holds, NULL for
any other (xbox_texture_cache.c, before its cache) */
void *h2_texture_stream_resident(struct bitmap_data *bitmap);
/* the base at the address is a streamed texture's: its detail run for levels
above the base (1..dropped); 0 when the address is no base's. The serial
changes with each map (an address's bytes are another texture's then) */
int h2_texture_stream_detail(unsigned long base_address, unsigned long levels, struct h2_texture_stream_detail *detail);
/* the detail run read from the level file into the buffer, whose size is
the run's rounded up to 512 bytes; waited for (the decoder's thread) */
int h2_texture_stream_read(struct h2_texture_stream_detail const *detail, void *buffer);
unsigned long h2_texture_stream_serial(void);
/* the count of textures streamed and their bases' bytes (halo.log) */
void h2_texture_stream_report(void);

#endif
