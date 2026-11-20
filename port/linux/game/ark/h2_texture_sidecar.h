/*
H2_TEXTURE_SIDECAR.H

The PC quality tier's full-size textures of a Halo 2 level (h2_level.c's
cache file): the level's file is the same on every machine - its bitmaps no
more than 256 a side on a multiplayer level (the multiplayer cache slot's
49,283,072 bytes, which the network and map sharing know) - and beside it,
in t:\h2\<map>.texcache, the same bitmaps as MCC's textures.dat holds them,
up to 2048 a side, made with the level's file from the player's own
textures.dat. As the level loads on a PC (HALO_PC_TEXTURE_TIER,
physical_memory_map.h; never the Vita), each bitmap the sidecar has is
given its full size in memory (width, height, levels, format) and its
pixels are read from the sidecar instead of the level's file
(cache_files_windows.c); the texture cache is 96 MB there. The tags are
changed in memory only, so nothing the network, saves or a shared map see
is different, and a missing, stale or damaged sidecar (CRC-32 checked, tied
to its level's file by its size and Adler-32) leaves the level as it is.

The sidecar also carries bitmaps the level's file has only as placeholders
(a multiplayer level's bump maps: h2_level.c), given to their shaders on
the PC as the sidecar's entry names them.

HALO_H2_SIDECAR=0: no sidecar made or used. HALO_H2_SIDECAR_MAXIMUM: its
largest side (2048).
*/

#ifndef __H2_TEXTURE_SIDECAR_H
#define __H2_TEXTURE_SIDECAR_H

/* ---------- constants */

enum
{
	H2_TEXTURE_SIDECAR_SIGNATURE = 0x68327478,   /* 'h2tx' */
	H2_TEXTURE_SIDECAR_VERSION = 1,
	H2_TEXTURE_SIDECAR_HEADER_SIZE = 64,
	H2_TEXTURE_SIDECAR_NAME_SIZE = 200,
	H2_TEXTURE_SIDECAR_ENTRY_SIZE = 224,
	H2_TEXTURE_SIDECAR_DEFAULT_MAXIMUM = 2048,
	/* an entry's kind */
	H2_TEXTURE_SIDECAR_FULL_SIZE = 0,    /* the level's bitmap of that name, full size */
	H2_TEXTURE_SIDECAR_BUMP = 1,         /* a placeholder bump map's pixels (its shaders' bump on the PC) */
	/* the files beside a level this reads: the sidecar (the PC tier's), and
	the effects' pixel cache - the effect sprite sheets' pixels, which a
	level made with its effects at load (HALO_H2_EFFECTS_AT_LOAD) does not
	carry: made by its builder or at load from textures.dat (h2_effects.c),
	the same format, <level>.fxcache, read on every platform */
	H2_TEXTURE_SIDECAR_FILE_SIDECAR = 0,
	H2_TEXTURE_SIDECAR_FILE_EFFECTS = 1,
	H2_TEXTURE_SIDECAR_FILES = 2
};

/*
The file, little endian:
	header (64 bytes): signature, version, entry count, the entries' offset,
		the level file's size, the level file's last 4 bytes (its zlib
		stream's Adler-32), the CRC-32 of every byte after the header, the
		pixels' bytes
	the pixels, each bitmap's 128-byte aligned, as the level's file would
		hold them (DXT as MCC has them; the others swizzled; levels largest
		first)
	the entries (224 bytes each): the bitmap tag's name (200 bytes), width,
		height, levels past the first, format, flags, kind (2 bytes each),
		pixels' offset, pixels' size (4 each), 4 spare
*/

/* ---------- prototypes/H2_TEXTURE_SIDECAR.C */

/* whether the PC quality tier's sidecar is made and used here */
int h2_texture_sidecar_enabled(void);
/* the sidecar's path for a level's cache file path (its .map made .texcache) */
void h2_texture_sidecar_path(char const *level_path, char *path, unsigned long path_size);
/* the effects' pixel cache's (its .map made .fxcache) */
void h2_texture_sidecar_fx_path(char const *level_path, char *path, unsigned long path_size);
/* whether the sidecar beside the level's file is its own (header, size and
Adler-32; not the CRC) - h2_level_cache_path makes both again if not */
int h2_texture_sidecar_matches(char const *level_path);

/* the level loaded (cache_files.c): its bitmaps the sidecar has made full
size, their reads the sidecar's; nothing for another map or without one */
void h2_texture_sidecar_map_loaded(char const *map_name);
/* the level goes (after the texture cache closes) */
void h2_texture_sidecar_unloaded(void);

/* whether the bitmap tag's pixels are the sidecar's (cache_files_windows.c) */
int h2_texture_sidecar_owns(long tag_index);
/* whether the bitmap tag's reads are the effects' pixel cache's, which
serves them at their offsets (part of an image too, as a level's file
does: the texture streaming's bases and detail) */
int h2_texture_sidecar_offsets(long tag_index);
/* its pixels read into buffer (size bytes at most); FALSE if not. offset:
the record's pixels offset - for the effects' pixel cache the key of the
image (a sprite sheet's images all carry the sheet's tag index); the PC
tier's sidecar has one entry a tag and does not read it */
int h2_texture_sidecar_read(long tag_index, long offset, long size, void *buffer);

#endif
