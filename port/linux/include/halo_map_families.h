/*
HALO_MAP_FAMILIES.H

The families of maps the native builds play (port/linux/game/map_families.c),
each named on the network and in playlists by a suffix to its file's name:

  - the Xbox's own maps, in maps\ (a scenario's name or path, no suffix);
  - Halo PC's Custom Edition maps (cache version 609), played as <name>@ce:
    maps\ce\<name>.map, or a file named <name>@ce.map there or in maps\;
  - HaloMD's maps (Halo PC retail's cache version 7, the maps of HaloMD's
    mod list), played as <name>@md: md_maps\<name>.map, or a file named
    <name>@md.map there or in maps\, else (where they were first played)
    maps\ce\<name>.map or maps\ce\<name>@md.map.

A map past the Xbox's is found by its family's folders and checked to be a
cache file of its family's version, so a Custom Edition map and a HaloMD map
of the same file name (in maps\ce) are told apart. Their resource maps
(bitmaps.map, sounds.map, loc.map) are Custom Edition's, in maps\ce, for
both. The suffixes are this port's: the game's protocol carries the name as
it carries any map's, and a build that plays no such maps does not have it.
*/

#ifndef HALO_MAP_FAMILIES_H
#define HALO_MAP_FAMILIES_H

enum
{
	_map_family_xbox,
	_map_family_custom_edition,
	_map_family_halomd,
	NUMBER_OF_MAP_FAMILIES
};

/* a map's family, by its name (a scenario's name or path, or <file>@ce,
<file>@md), and its file's name (the path's last part, without the suffix),
in file (size bytes) if file is not NULL */
short map_family_parse(char const *map, char *file, long size);

/* a family's suffix ("@ce", "@md"; "" for the Xbox's) */
char const *map_family_suffix(short family);

/* a family's name as the menus and the server browser show it on a game
("HALO PC", "HALOMD"; "" for the Xbox's) */
char const *map_family_badge(short family);

/* the folder a player puts a family's maps in, as the menus tell them
("maps/ce", "md_maps") */
char const *map_family_folder(short family);

#ifdef HALO_CUSTOM_EDITION
/* the file of a map of a family past the Xbox's (its file's name without
the suffix), as cache_files_windows.c opens it: the path (for CreateFileA)
of the first of the family's places that is a cache file of the family's
version; FALSE if there is none */
boolean map_family_find(short family, char const *file, char *path, long size);

/* every multiplayer map of a family past the Xbox's, in its folders: each
one's file's name (without the suffix, each once), passed to found */
void map_family_list(short family, void (*found)(char const *file, void *context), void *context);
#endif

#endif
