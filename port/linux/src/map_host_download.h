/*
MAP_HOST_DOWNLOAD.H

The rules of a Custom Edition map downloaded from a game's host, peer to
peer (map_download.c, "From the host"), apart from the game so the tests
build them alone (tools/test_map_host_download.py):

- network.map_downloads_from_host's words ("off", the default, "ask",
  "always");
- what follows the repositories' download (map_host_step): the host comes
  last, after the community repositories and Chupathingy maps, only when
  they failed without the blacklist refusing the map, and only as the
  setting says;
- the quarantine folder's files (maps_quarantine, beside maps_ce), and the
  install from it (map_host_install): the quick checks
  (map_quick_check.h), the map hash the host offered, the host's version,
  the blacklist by both hashes, and only then the move into maps_ce, never
  over a file that is there. Anything refused is deleted, never changed.

Its files come through a struct of calls (the game's d:\ files, or a
test's). Plain C and plain types.
*/

#ifndef MAP_HOST_DOWNLOAD_H
#define MAP_HOST_DOWNLOAD_H

#include "map_archive.h"

/* the quarantine folder, and the maps' (game paths) */
#define MAP_HOST_QUARANTINE_FOLDER "d:\\maps_quarantine"
#define MAP_HOST_MAPS_FOLDER "d:\\maps_ce"

enum
{
	MAP_HOST_PATH_SIZE = 256,
	MAP_HOST_NAME_SIZE = 48,
};

/* network.map_downloads_from_host (map_download.h's MAP_HOST_DOWNLOADS_*:
off 0, ask 1, always 2) from its words, any case; anything else is off */
int map_host_setting_from(const char *text);
const char *map_host_setting_word(int setting);

/* what follows a download that ended (map_download.h's MAP_HOST_STEP_*:
none 0, ask 1, start 2), for a setting, the download's state and source
(map_download.h's MAP_DOWNLOAD_*) and whether the blacklist refused it */
int map_host_step(int setting, int state, int source, int blocked);

/* name's files in the quarantine folder: being written (part) and whole,
each MAP_HOST_PATH_SIZE bytes */
void map_host_quarantine_paths(const char *name, char *part, char *whole);

/* what the install reads and changes */
struct map_host_files
{
	void *context;
	/* the size of the file at path, or -1 if there is none (or it is too
	large to be a map) */
	long long (*size)(void *context, const char *path);
	/* size bytes at offset of the file at path into buffer: 1 if all were */
	int (*read)(void *context, const char *path, unsigned int offset, void *buffer, unsigned int size);
	/* the file at path moved to target, never over a file there: 1 if it
	was */
	int (*move)(void *context, const char *path, const char *target);
	void (*remove)(void *context, const char *path);
	/* the file's SHA-256 and map hash (map_archive_map_hash), 32 bytes each:
	1, else 0 */
	int (*hashes)(void *context, const char *path, unsigned char *sha256, unsigned char *map_hash);
	/* whether the blacklist refuses a file of these hashes: 1 with why in
	reason (NULL: no blacklist held) */
	int (*blocked)(void *context, const unsigned char *sha256, const unsigned char *map_hash, char *reason,
		int reason_size);
};

/* the map name (its file's name, as the host's game names it) whole in the
quarantine folder, checked and moved into maps_ce: the quick checks within
limit bytes, its map hash the one the host offered (offered), the host's
version when not 0 (its hash's or its header checksum's:
map_archive_version_matches) and a hashed name's digits its hash's, and
neither of its hashes on the blacklist. It goes in <name>.map (a hashed
name's base's), or beside another version there in <name>_<8 hex
digits>.map (a hashed name's: its own). 1 with the file's name (without
.map) in installed and its hashes in sha256 and map_hash; else 0 with why
in error, *blocked 1 when the blacklist refused it, and the file deleted */
int map_host_install(const struct map_host_files *files, const char *name, unsigned int version,
	const unsigned char *offered, unsigned int limit, char *installed, unsigned char *sha256, unsigned char *map_hash,
	int *blocked, char *error, int error_size);

#endif
