/*
H2_TEXTURE_SOURCE.H

(port) A Halo 2 level's bitmaps the level file does not hold: served to
the cache file's bitmap reads (cache_files_windows.c: the texture cache's
whole reads, the streamer's base and detail reads) from where they are, by
this one dispatch (h2_texture_source.c). The level's record names each
such bitmap tag, its kind and what its kind needs:

- a textures.dat copy (HALO_H2_TEXTURES_FROM_DAT=1 at the build): 2D, DXT,
  as MCC's textures.dat has it, the levels over the level's size limit
  passed over. Its bytes are inflated from the chunks of textures.dat they
  lie in, no others, when asked for.
- a derived bitmap the builder makes from the map's (a shader's
  multipurpose, plasma, multichannel, bump or tint map;
  HALO_H2_DERIVED_AT_LOAD=1 at the build): made again on demand at the
  texture cache's miss by the builder's own code over an open of the map of
  this source's own (h2_level_derived_pixels), exact bytes, kept in a small
  LRU (HALO_H2_DERIVED_KEPT_KB, 1024 by default).

A recorded bitmap's pixels offset is in a range of its own above any level
file's length (H2_TEXTURE_SOURCE_OFFSET_BASE), which the tag check allows a
Halo 2 level (tag_validate.c) and by which the reads name it.
*/

#ifndef H2_TEXTURE_SOURCE_H
#define H2_TEXTURE_SOURCE_H

/* the level's record read (its file's header says whether it has one),
its sources opened; and let go */
void h2_texture_source_map_loaded(char const *map_name);
void h2_texture_source_unloaded(void);

/* whether a bitmap read is a source's: the level has a record and the
offset is in its range (a read names its bitmap by offset alone - a tag of
several bitmaps, an effect's frames, has an entry for each) */
int h2_texture_source_owns(long tag_index, long offset);

/* size bytes of the tag's pixels from offset (the record's offset space:
what its bitmap_data says) into buffer, those past the pixels' end zero;
FALSE if they cannot be read */
int h2_texture_source_read(long tag_index, long offset, long size, void *buffer);

/* (the cache file thread) a derived bitmap's read parked for the game
thread, whose open of the map is the load's: TRUE if parked, request
finished later by h2_texture_source_pump (each frame, main.c, and the cache
file's waits) through cache_file_request_finish */
int h2_texture_source_park(long tag_index, long offset, long size, void *buffer, void *request);

/* (piece J, a direct level's make) a derived bitmap's smallest levels -
64 px and under, what the texture streamer keeps resident - held for its
entry (its pixels offset), the bytes taken: the streamer's base read served
from them at load, the whole not made for it; dropped at a make's start */
void h2_texture_source_tail_hold(unsigned long entry_offset, unsigned char *pixels, unsigned long bytes);
void h2_texture_source_tails_drop_pending(void);
void h2_texture_source_pump(void);
void h2_texture_source_pump_tick(void);

/* (the load's log) */
void h2_texture_source_report(void);
/* (h2_loading.c's reveal) the reads of derived bitmaps parked for the
worker and not made yet - a texture the frame asked for, drawn from a
stand-in meanwhile; *made: the derived bitmaps made so far */
long h2_texture_source_reads_pending(unsigned long *made);

/* the level file's header: the record's words */
enum
{
	/* (after the kit's record, 0x7C8-0x7E7; the part table's slots run to
	0x780, the load-tags ranges to 0x7C7) */
	H2_TEXTURE_SOURCE_HEADER = 0x7E8,          /* 'h2bd' */
	H2_TEXTURE_SOURCE_HEADER_OFFSET = 0x7EC,   /* the table's offset in the file */
	H2_TEXTURE_SOURCE_HEADER_COUNT = 0x7F0,    /* its entries */
	H2_TEXTURE_SOURCE_HEADER_BYTES = 0x7F4,    /* the table's bytes: the entries and the parameters after them */
	H2_TEXTURE_SOURCE_SIGNATURE = 0x68326264UL, /* 'h2bd' */
	/* an entry, eight words: tag index, kind, the pixels offset the
	bitmap's record carries, the pixels' bytes, their FNV-1a sum (what
	HALO_H2_DAT_VERIFY=1 checks the reads against at load), then three the
	kind reads: a textures.dat copy's entry offset in textures.dat, the
	entry's bytes, and the bytes passed over (the levels above the level's
	size limit); a derived bitmap's parameters' offset in the table, their
	bytes, and 0 */
	H2_TEXTURE_SOURCE_ENTRY_SIZE = 32,
	/* the pixels offsets' range: above any level file's length */
	H2_TEXTURE_SOURCE_OFFSET_BASE = 0x40000000UL,
	H2_TEXTURE_SOURCE_OFFSET_RANGE = 0x30000000UL,
	/* a derived entry's parameters (its words 5 and 6: their offset in the
	table and bytes): the flags (the recipe's kind at bits 8-15), the
	recipe's first argument (a shader's tag in the map), the map's name (32
	bytes), its other 9 arguments, then the bitmap's path, NUL-terminated,
	padded to 4 */
	H2_TEXTURE_SOURCE_DERIVED_MAP = 8,
	H2_TEXTURE_SOURCE_DERIVED_ARGS = 40,   /* (the recipe's arguments after its first: 9 words) */
	H2_TEXTURE_SOURCE_DERIVED_PATH = 76,
	/* (a plain one's flags' top half: the bytes of its smallest levels,
	those of this many pixels a side and under - the texture streamer's
	base - made alone at that size, the whole's end; 0 none) */
	H2_TEXTURE_SOURCE_TAIL_PIXELS = 64,
	/* (textures.dat's chunks inflate to this each, the last to the rest) */
	H2_TEXTURE_SOURCE_CHUNK_BYTES = 0x40000
};

/* the kinds */
enum
{
	_h2_texture_source_dat,       /* a textures.dat copy */
	_h2_texture_source_derived,   /* made on demand from the map's bitmaps */
	NUMBER_OF_H2_TEXTURE_SOURCE_KINDS
};

#endif
