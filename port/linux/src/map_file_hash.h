/*
MAP_FILE_HASH.H

A map file's whole-file hash prefix (map_archive_map_hash), as a hashed
name ends in and a host of network version 29 sends as the map's version
(map_archive.h), read from the file once and kept for the run
(map_file_hash.c): what finds a map of the host's version, or a hashed
name's by its base name (port/linux/game/map_families.c), gives a host's
map its version (cache_files.c) and checks a downloaded one
(map_download.c).

Plain types only: the game's units (64-bit builds rewrite their long) and
the platform's both call it.
*/

#ifndef MAP_FILE_HASH_H
#define MAP_FILE_HASH_H

/* the hash prefix of the file at path (a game path, d:\...): 1 with it in
*prefix, else 0 (no such file, or not read whole). A file is read once for
its path, size and modification time; any thread may ask */
int map_file_hash_prefix(const char *path, unsigned int *prefix);
/* ... and the version of the map in it, as a host of network version 29
sends it (map_archive_hash_version: the prefix, or 1 for 0) */
int map_file_hash_version(const char *path, unsigned int *version);

#endif
