/*
MAP_QUICK_CHECK.H

The quick checks a Custom Edition map from a game's host gets before it
leaves the quarantine folder (map_download.c, "From the host"): a few
milliseconds of reading, never a load. A file that fails one is deleted;
nothing in it is changed or "fixed". What passes is still held to the
game's own checks when it is played (ce_map_checks.c), as every map is.

- the name: a map file name the repositories would take
  (map_archive_name_valid), not one of Halo PC's own maps or files;
- the size: no larger than the player's cap
  (network.map_downloads_from_host_limit);
- the header: a Custom Edition (cache version 609) multiplayer map named
  for the map asked for, its tag data inside the file
  (map_archive_check_header);
- the tag data: no larger than the game's tag cache, its header signed
  'tags', 1 to 65535 tags whose instances lie in it, each one's handle its
  own index, its name in it and ended within 256 bytes, its data in it (a
  structure BSP's none; only bitmaps, sounds, fonts and strings held by the
  resource maps); the scenario one of them; the model data inside the file.

Plain C and plain types: the fuzz target (tools/tests/host_fuzz/
map_quick_check) and the tests (tools/test_map_host_download.py) build it
alone, with map_archive.c.
*/

#ifndef MAP_QUICK_CHECK_H
#define MAP_QUICK_CHECK_H

#include "map_archive.h"

enum
{
	/* network.map_downloads_from_host_limit, in MB: its default, and the
	most and least it takes */
	MAP_QUICK_CHECK_DEFAULT_LIMIT_MB = 512,
	MAP_QUICK_CHECK_MINIMUM_LIMIT_MB = 16,
	/* (the transfer's own limit, map_share_client.c's MAP_LIMIT: 768 MiB) */
	MAP_QUICK_CHECK_MAXIMUM_LIMIT_MB = 768,
	/* the game's tag cache (ce_map_checks.h's CE_IMAGE_TAG_CACHE_SIZE):
	no map's tag data is larger */
	MAP_QUICK_CHECK_TAG_CACHE_SIZE = 0x01700000,
	/* where the tag data is loaded (ce_map_checks.h's
	CE_IMAGE_TAG_CACHE_BASE): its pointers are addresses past it */
	MAP_QUICK_CHECK_TAG_CACHE_BASE = 0x40440000,
};

/* whether name is a file name a map from a host may be kept by: one the
repositories would take, and not one of Halo PC's own maps or files */
int map_quick_check_name(const char *name);

/* the cap, in bytes, for a setting of megabytes (out of range: the
nearest bound; 0 or less: the default) */
unsigned int map_quick_check_limit(long megabytes);

/* the map of file_size bytes that read reads (context its own) checked as
above, for the map named name (its file's name, without .map; the name
inside it, any case) and a cap of limit bytes: 1, else 0 with why in error
(a plain sentence's end, for the log and the player) */
int map_quick_check(map_archive_read_proc read, void *context, unsigned int file_size, const char *name,
	unsigned int limit, char *error, int error_size);

#endif
