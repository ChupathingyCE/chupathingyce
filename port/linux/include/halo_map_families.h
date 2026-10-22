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
    played as <name>@pc: maps_pc\<name>.map;
  - community packs of Xbox maps (Halo CE+ and the like: Xbox
    cache files, version 5, often saved under the Xbox's own maps' file
    names), each pack in a folder of its own, maps_<pack>, its maps played
    as <name>@<pack>: maps_cex\bloodgulch.map is bloodgulch@cex, CE+'s Abyss
    (halo_map_packs.h). They are Xbox maps, read by the Xbox's loader and
    copied into its cache slots as those are, but never taken for the
    retail map, or another pack's, of the same file name: the slot, the
    game's settings, the menus and the last map played all name them
    <name>@<pack>. A pack's name is 1 to 8 letters and digits, none of the
    other families' suffixes (ce, md, pc) nor the Xbox's language folders'
    (de, fr, es, it).

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
and Halo PC retail's maps maps_md\<name>.md and maps_pc\<name>.pc, and a
pack's Xbox map maps_<pack>\<name>.<pack>, which no OpenCE client (nor a
ChupathingyCE build before map packs) has (map_family_wire_name); Delta Peer carries each map's
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
	/* (Delta Peer's MAP carries the number: a new family goes last) */
	_map_family_pack,
	NUMBER_OF_MAP_FAMILIES
};

/* a map's family, by its name (a scenario's name or path, or <file>@ce,
<file>@md), and its file's name (the path's last part, without the suffix),
in file (size bytes) if file is not NULL */
short map_family_parse(char const *map, char *file, long size);

/* map_family_parse for a map as the game list names it: a name with no folder
or suffix that is not an Xbox map (deathisland, hugeass: how OpenCE's hosts
announce every Halo PC map) is a Custom Edition map's, as in the game's
protocol (custom_maps\<name>) */
short map_family_parse_listed(char const *map, char *file, long size);

/* (a pack's name, at most, without its terminator) */
#define MAP_PACK_ID_LENGTH 8

/* whether a family's maps are Xbox cache files (version 5), which the Xbox's
loader reads and its cache slots hold: the Xbox's own and a pack's */
boolean map_family_is_xbox_format(short family);

/* whether a name can be a map pack's (1 to 8 letters and digits, any case,
none of the reserved names) */
boolean map_pack_id_valid(char const *id, long length);

/* a map pack's map's pack (<file>@<pack>: its name, in lowercase), into
pack (MAP_PACK_ID_LENGTH + 1 bytes or more): FALSE for another family's */
boolean map_family_pack_id(char const *map, char *pack, long size);

/* the file an Xbox-format map's name (as the loader has it: <file>, or
<file>@<pack>) is read from: maps_<pack>\<file>.map for a pack's, else
the maps folder's <name>.map, as the game always read it (with a pack's own
menus, game.pack_menus, ui is the pack's ui.map). FALSE (path empty) if it
does not fit */
boolean map_family_xbox_path(char const *map_name, char *path, long size);

/* a family's suffix ("@ce", "@md"; "" for the Xbox's, and for a pack's,
whose suffix is its pack's: @cex) */
char const *map_family_suffix(short family);

/* a family's name as the menus and the server browser show it on a game
("HALO PC", "HALOMD", "HALO PC RETAIL", "MAP PACK"; "" for the Xbox's) */
char const *map_family_badge(short family);

/* a family's short tag on a listed game's map, the same in every menu set
and on the game list's pages: XBX (the Xbox's, maps\), HCE (Custom
Edition's, maps_ce\, @ce), HMD (HaloMD's, maps_md\, @md), GBX (Halo PC
retail's, Gearbox's, maps_pc\, @pc), PAK (a map pack's Xbox map: each
pack's own tag where it is known, map_pack_tag); what it stands for
("Custom Edition map"), and its color (0xRRGGBB) */
char const *map_family_tag(short family);
char const *map_family_title(short family);
unsigned long map_family_color(short family);

/* (reserved for MCC's editing kit's maps, not played yet: no family) */
#define MAP_FAMILY_TAG_MCC "MCC"
#define MAP_FAMILY_TITLE_MCC "MCC map"
#define MAP_FAMILY_COLOR_MCC 0x5FD3C8

/* the folder a player puts a family's maps in, as the menus tell them
("maps_ce", "maps_md", "maps_pc"; "maps" for the Xbox's; a pack's is
maps_<pack>, map_pack_folder) */
char const *map_family_folder(short family);

/* a family's name in logs and messages ("Custom Edition", "HaloMD",
"Halo PC", "map pack", "Xbox") */
char const *map_family_kind(short family);

/* a map's name (a scenario's name or path, or <file>@ce, @md, @pc) as the
game's protocol has it (the game's settings, its advertisement): an Xbox
map's as it is, custom_maps\<file>, maps_md\<file>.md, maps_pc\<file>.pc,
maps_<pack>\<file>.<pack> */
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
/* ... and kept so in maps_ce/downloaded_maps.txt, for the next runs (the
map downloader's maps: port/linux/src/map_download.c) */
void map_downloaded_remember(char const *map_name);

#ifdef HALO_CUSTOM_EDITION
/* a family's cache version: 609 for Custom Edition's, 7 for HaloMD's and
Halo PC retail's, 5 for the Xbox's and a pack's */
long map_family_cache_version(short family);

/* the path of a Custom Edition resource map (bitmaps, sounds, loc; or
Halo PC's ui), in maps_ce or its older places: FALSE if there is none (path
then the place it goes) */
boolean map_family_resource(char const *name, char *path, long size);

/* the file of a map of a family past the Xbox's (its file's name without
the suffix), as cache_files_windows.c opens it: the path (for CreateFileA)
of the first of the family's places that is a cache file of the family's
version (the network game's version of its map first:
map_family_set_game_version); FALSE if there is none */
boolean map_family_find(short family, char const *file, char *path, long size);

/* ... the file of a map of the host's version (a game record's map version:
since network version 29 a Custom Edition map's whole-file hash's first 32
bits, before then its header's checksum; 0 for any): one of its name in its
places, then for a Custom Edition map <file>_<8 hex digits>.map of that
version (a download beside another version: map_download.c), and a hashed
name's base whose hash is the name's (an older host's level name) */
boolean map_family_find_version(short family, char const *file, unsigned long version, char *path, long size);

/* what a player lacking one of Halo PC's own maps (deathisland, ...; file) is
told for a game whose host plays the map of version (0 if not known): the
edition their files say the host plays, _map_family_custom_edition or
_map_family_halo_pc (the other edition's file is held, and is not the
host's), else _map_family_xbox (not known). first is the heading, second the
instruction (two lines, split by a newline), blocker the one line the game
list shows. A Halo PC retail file and a Custom Edition file both play a game
of either, when map_stock_pairs (map_stock_pairs.h) has checked them the
same */
long map_family_stock_need(char const *file, unsigned long version, char *first, long first_size, char *second,
	long second_size, char *blocker, long blocker_size);

/* the map a network game is on and the version of it the host plays (0 for
any, which forgets it): map_family_find gives the file of that version, if
there is one, so that the map loaded is the host's */
void map_family_set_game_version(char const *map, unsigned long version);

/* every multiplayer map of a family past the Xbox's, in its folders: each
one's file's name (without the suffix, each once), passed to found */
void map_family_list(short family, void (*found)(char const *file, void *context), void *context);
#endif

#endif
