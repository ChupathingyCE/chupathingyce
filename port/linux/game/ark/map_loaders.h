/* MAP_LOADERS.H
	(port) one way to load every map: a map file's format told from its
	header alone, and the loader for that format, behind one small table -
	no per-game setting. A loader says what a file is (its name, its
	scenario) and gives the path of the cache file this engine plays: the
	file itself for a Halo 1 map, one made from it (once, then kept) for the
	others. */

#ifndef __MAP_LOADERS_H
#define __MAP_LOADERS_H

/* ---------- constants */

enum map_format
{
	_map_format_unknown = 0,
	_map_format_halo1,          /* Halo 1: Xbox (5), PC/CE (7, 609), MCC (13): header 0x800 */
	_map_format_halo2_xbox,     /* Halo 2 Xbox (8) */
	_map_format_halo2_mcc,      /* Halo 2 MCC (13): header 0x380, chunk-compressed */
	_map_format_halo3_mcc,      /* Halo 3 MCC (13): header 0x4000 */
	NUMBER_OF_MAP_FORMATS
};

enum
{
	MAP_LOADER_NAME_SIZE = 64,
	MAP_LOADER_SCENARIO_SIZE = 256,
	MAP_LOADER_WHY_SIZE = 256,
};

/* ---------- structures */

struct map_loader_entry
{
	short format;                               /* enum map_format */
	char name[MAP_LOADER_NAME_SIZE];            /* as its header names it */
	char scenario[MAP_LOADER_SCENARIO_SIZE];    /* its scenario's tag path */
};

struct map_loader
{
	short format;
	char const *label;
	/* what the file at path is; FALSE if not one of this loader's */
	boolean (*describe)(char const *path, struct map_loader_entry *entry);
	/* the playable cache file for maps_directory/name.map in path (path_size
	bytes): made if need be; FALSE and why if it cannot be */
	boolean (*resolve)(char const *maps_directory, char const *name, char *path, long path_size,
		char *why, long why_size);
};

/* ---------- prototypes/MAP_LOADERS.C */

/* the file's format, from its first 0x4000 bytes at most */
short map_format_of_file(
	char const *path);

/* the loader of a format; NULL if none */
struct map_loader const *map_loader_get(
	short format);

/* the loader for the file at path, entry (if not NULL) what it is; NULL
if no loader takes it */
struct map_loader const *map_loader_for_file(
	char const *path,
	struct map_loader_entry *entry);

/* every map in directory each loader takes, given to found (in the
directory's order); how many */
long map_loaders_list(
	char const *directory,
	void (*found)(struct map_loader_entry const *entry, char const *path, void *context),
	void *context);

/* the playable cache file for maps_directory/name.map, its loader's (a
Halo 1 map: the file); FALSE and why if not */
boolean map_loaders_resolve(
	char const *maps_directory,
	char const *name,
	char *path,
	long path_size,
	char *why,
	long why_size);

#endif
