/*
MAP_COMPRESSED.H

Halo PC maps compressed with Chimera's chimera-compress (map_compressed.c):
the map's own 2048-byte header, its cache version marked 0x861A0000 (Custom
Edition's 609 as 0x861A0261, Halo PC retail's 7 as 0x861A0007), then the
rest of the map as Zstandard data. Expanded, they are ordinary cache files.

The format is Chimera's, by Snowy Mouse and its contributors (GPL-3.0,
github.com/SnowyMouse/chimera: compressor/main.cpp and
src/chimera/map_loading/compression.cpp). This reader is written from it.

No file, network or game dependency (zstd only): the tests build it by
itself (tools/test_compressed_maps.py). The desktop builds alone have it
(HALO_COMPRESSED_MAPS); the consoles play no Halo PC maps.

Plain types only: the game's units (64-bit builds rewrite their long) and
the tests both call it.

SPDX-License-Identifier: GPL-3.0-only
*/

#ifndef MAP_COMPRESSED_H
#define MAP_COMPRESSED_H

enum
{
	/* a cache file's header, which a compressed map keeps as it is */
	MAP_COMPRESSED_HEADER_SIZE = 0x800,
};

/* the compressed map's bytes [offset, offset + size) into buffer: 1 if all
were read */
typedef int (*map_compressed_read_proc)(void *context, unsigned int offset, void *buffer, unsigned int size);
/* the expanded map's next size bytes written: 1 on success */
typedef int (*map_compressed_write_proc)(void *context, const void *buffer, unsigned int size);
/* whether to stop: nonzero stops */
typedef int (*map_compressed_stop_proc)(void *context);

struct map_compressed_io
{
	void *context;
	/* the compressed file's size in bytes */
	unsigned int compressed_size;
	map_compressed_read_proc read;
	map_compressed_write_proc write;
	/* (NULL: never stops) */
	map_compressed_stop_proc stop;
};

/* the cache version a compressed map's header (its first 8 bytes at least)
has once expanded: 609 (Custom Edition) or 7 (Halo PC retail, HaloMD's
maps); 0 for any other header, an uncompressed map's among them */
unsigned int map_compressed_version(const unsigned char *header);

/* the compressed map io reads expanded through io's write: its header, its
version restored, then the rest. 1 with the expanded size in
*expanded_size, else 0 with why in error. Refused: a file that is not a
compressed map (map_compressed_version), damaged or cut-off data, data past
its end, more than maximum_size bytes expanded, or an expanded size other
than the one its header gives (when it gives one) */
int map_compressed_expand(const struct map_compressed_io *io, unsigned int maximum_size, unsigned int *expanded_size,
	char *error, int error_size);

/* ---------- the game's files (map_compressed_files.c)

Game paths (d:\maps_ce\..., z:\...), any thread. */

/* the version the map file at path has once expanded
(map_compressed_version), or 0: not a compressed map, or not read */
unsigned int map_compressed_file_version(const char *path);

/* the compressed map at source expanded into destination (made, or
replaced): 1, or 0 with why in error and no destination left */
int map_compressed_file_expand(const char *source, const char *destination, char *error, int error_size);

/* path, when it is a compressed map's file: replaced with the path of its
expanded copy in the save root's cache (z:\cache_compressed_<name>_<its
checksum>_<its size>.map), made the first time and kept for the next
runs. 1 if path now names an uncompressed file (left as it was when it was
not compressed), 0 if the map could not be expanded */
int map_compressed_file_resolve(char *path, int size);

#endif
