/*
HALO_MAP_FAMILIES.H

The families of maps the native builds play (port/linux/game/map_families.c),
each named in playlists, the menus, the game list and Delta by a suffix to
its file's name, and each with a folder of its own beside maps\:

  - the Xbox's own maps, in maps\ (a scenario's name or path, no suffix);
  - Halo PC's Custom Edition maps (cache version 609), played as <name>@ce:
    maps_ce\<name>.map, with Custom Edition's bitmaps.map, sounds.map and
    loc.map beside them;
  - HaloMD's maps (Halo PC retail's cache version 7, the maps of HaloMD's
    mod list), played as <name>@md: maps_md\<name>.map;
  - Halo PC retail's own maps (version 7, from the player's Halo PC disc),
    played as <name>@pc: maps_pc\<name>.map.

A file named for its family (<name>@ce.map) is played from its family's
folder or from maps\ itself. The older places are read too: maps\ce
(ChupathingyCE 0.6 and 0.7.0b: Custom Edition's and HaloMD's maps), md_maps
(HaloMD's) and OpenCE's custom_maps (Custom Edition's). A map past the
Xbox's is found by its family's folders and checked to be a cache file of
its family's version, so a Custom Edition map and a HaloMD map of the same
file name are told apart. Their resource maps (bitmaps.map, sounds.map,
loc.map) are Custom Edition's, for every family past the Xbox's.

The suffixes are this port's. In the game's protocol a Custom Edition map
is named as OpenCE's build-145 names it, custom_maps\<name>, and HaloMD's
and Halo PC retail's maps maps_md\<name>.md and maps_pc\<name>.pc, which no
OpenCE client has (map_family_wire_name); Delta Peer carries each map's
identity between ChupathingyCE machines (docs/delta.md).
*/

#ifndef HALO_MAP_FAMILIES_H
#define HALO_MAP_FAMILIES_H

enum
{
	_map_family_xbox,
	_map_family_custom_edition,
	_map_family_halomd,
	_map_family_halo_pc,
	NUMBER_OF_MAP_FAMILIES
};

/* a map's family, by its name (a scenario's name or path, or <file>@ce,
<file>@md), and its file's name (the path's last part, without the suffix),
in file (size bytes) if file is not NULL */
short map_family_parse(char const *map, char *file, long size);

/* a family's suffix ("@ce", "@md"; "" for the Xbox's) */
char const *map_family_suffix(short family);

/* a family's name as the menus and the server browser show it on a game
("HALO PC", "HALOMD", "HALO PC RETAIL"; "" for the Xbox's) */
char const *map_family_badge(short family);

/* the folder a player puts a family's maps in, as the menus tell them
("maps_ce", "maps_md", "maps_pc"; "maps" for the Xbox's) */
char const *map_family_folder(short family);

/* a family's name in logs and messages ("Custom Edition", "HaloMD",
"Halo PC", "Xbox") */
char const *map_family_kind(short family);

/* a map's name (a scenario's name or path, or <file>@ce, @md, @pc) as the
game's protocol has it (the game's settings, its advertisement): an Xbox
map's as it is, custom_maps\<file>, maps_md\<file>.md, maps_pc\<file>.pc */
void map_family_wire_name(char const *map, char *wire, long size);

/* the map a name in the game's protocol names, as this port names it
(<file>@ce for custom_maps\<file>, and so on; any other name as it is,
<file>@ce of an older ChupathingyCE host among them), into map; its family */
short map_family_from_wire_name(char const *wire, char *map, long size);

/* whether a map (as scenario_tags_load names it: <file>, <file>@ce, ...)
came from a download, whose scripts are held to tighter rules (hs.c):
marked by the downloader, or named by game.downloaded_maps */
boolean map_is_downloaded(char const *map_name);
void map_downloaded_mark(char const *map_name);

#ifdef HALO_CUSTOM_EDITION
/* a family's cache version: 609 for Custom Edition's, 7 for HaloMD's and
Halo PC retail's */
long map_family_cache_version(short family);

/* the path of a Custom Edition resource map (bitmaps, sounds, loc; or
Halo PC's ui), in maps_ce or its older places: FALSE if there is none (path
then the place it goes) */
boolean map_family_resource(char const *name, char *path, long size);

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
