/*
HALO_MAP_PACKS.H

Community packs of Xbox maps (Halo CE+ and the like), the map
pack family of halo_map_families.h (port/linux/game/map_packs.c). A pack's
maps are Xbox cache files, often saved under the Xbox's own maps' file
names (CE+'s Abyss is its bloodgulch.map), each pack in a folder of its own
beside maps: maps_<pack>, its maps played as <file>@<pack> (maps_cex's
bloodgulch.map is bloodgulch@cex). A pack may have a maplist.txt naming
them, a line a map:

	bloodgulch - Abyss

A player copies the pack's maps folder (its maplist.txt with it) into the
pack's folder, or one of the names the library knows it by (maps_ceplus is
CE+'s too). A pack is not its folder's name: a map whose fingerprint is in
the library below belongs to the library's pack whatever folder it is in, so
it is hosted, announced and joined as <file>@cex by players whose folders are
called otherwise. Packs live beside the retail maps and beside each other,
whatever file names they share. The pack's own ui.map is not read unless
the player asks for the pack's menus (game.pack_menus): the game's menus
stay the game's.

Each file has a fingerprint (map_pack_fingerprint): a host sends it as the
map's version in the game record, and a joining machine compares its own
file's, so a joiner with another version of the pack is told so before the
game loads. The fingerprint also finds the map in the library of known Xbox
mod maps (port/assets/maps/xbox_mod_maps.json, built into
xbox_mod_maps.h), which names it, and its pack, for a player without the
pack's maplist.txt or the pack itself. The library also knows the packs'
folders and their aliases (cex: Halo CE+, also maps_ceplus and maps_ce+); a
maps_<name> folder it doesn't know is a pack too, named by its folder.
*/

#ifndef HALO_MAP_PACKS_H
#define HALO_MAP_PACKS_H

/* a known pack (xbox_mod_maps.h): its folder's name (maps_<id>), the other
folder names it is known by (aliases, a comma-separated list: "ceplus,ce+"),
its name, its short tag in the menus, its team and credits, and a public page
about it ("" where not known) */
struct xbox_mod_pack
{
	char const *id;
	char const *aliases;
	char const *name;
	char const *tag;
	char const *team;
	char const *credits;
	char const *source;
};

/* a known Xbox mod map (xbox_mod_maps.h): its file's fingerprint, its pack's
folder name, the file's name in its pack (without .map), its name, the
pack's version, its author and a public page about it ("" where not known) */
struct xbox_mod_map
{
	unsigned long fingerprint;
	char const *pack;
	char const *file;
	char const *name;
	char const *pack_version;
	char const *author;
	char const *source;
};

/* a fingerprint's reading of a file: size bytes at offset into buffer,
nonzero if they were read */
typedef int (*map_pack_read_proc)(void *context, unsigned long offset, void *buffer, unsigned long size);

/* the fingerprint of a file of size bytes: FNV-1a (32 bits) over its size (4
bytes, little endian) and 16 samples of at most 64 KB at size / 16 * i, 1
in place of 0 (a game record's "any version"); 0 if it could not be read.
tools/xbox_mod_maps.py computes the same */
unsigned long map_pack_fingerprint_read(unsigned long size, map_pack_read_proc read, void *context);

/* the fingerprint of maps_<pack>\<file>.map (kept for the run, by the file's
size and time), or 0 if there is none */
unsigned long map_pack_fingerprint(char const *pack, char const *file);

/* whether maps_<pack> has <file>.map */
boolean map_pack_present(char const *pack, char const *file);

/* a pack's map's name as the game names it, <file>@<pack>, into map: FALSE
if it does not fit a cache slot's name (31 characters) */
boolean map_pack_map_name(char const *pack, char const *file, char *map, long size);

/* the path of a pack's <file>.map, into path: in the folder that has it (the
pack's, else an alias's, else for a pack in the library any maps_* folder
whose file of that name is one the library knows as the pack's), else in the
pack's own; FALSE if it does not fit */
boolean map_pack_file_path(char const *pack, char const *file, char *path, long size);

/* a pack's folder (maps_<pack>), its name (the library's, else "the
maps_<pack> pack"), and its tag in the menus (the library's, else its name
in capitals) */
void map_pack_folder(char const *pack, char *folder, long size);
void map_pack_title(char const *pack, char *title, long size);
void map_pack_tag(char const *pack, char *tag, long size);

/* the library's pack of a folder's name, its map of a fingerprint; NULL for
none */
struct xbox_mod_pack const *map_pack_library_pack(char const *pack);
struct xbox_mod_map const *map_pack_library_find(unsigned long fingerprint);

/* a file's name in a maplist.txt's text ("file - Name" a line; the first
'-' splits them, spaces around each part dropped; any case): FALSE if it has
none */
boolean map_pack_maplist_find(char const *text, char const *file, char *name, long size);

/* a pack's map's name and its pack's name: the library's, by the fingerprint
(0: this machine's file's) when its map is that pack's and file; else the
pack's maplist.txt's name, and the pack's title; else the file's name made
readable. The source of the name: */
enum
{
	_map_pack_name_file,
	_map_pack_name_maplist,
	_map_pack_name_library,
};
short map_pack_names(char const *pack, char const *file, unsigned long fingerprint, char *name, long name_size,
	char *pack_name, long pack_size);

/* every multiplayer map of every pack (each maps_<pack> folder's Xbox cache
files, version 5, of multiplayer maps; not a pack's ui.map, nor its campaign
levels): each one's pack and file's name (without .map), passed to found */
void map_pack_list(void (*found)(char const *pack, char const *file, void *context), void *context);

/* the pack whose own menus are played (game.pack_menus names it, and its
folder has a ui.map; read once, at the first ask), or NULL; whether that
ui.map is the library's (its scripts may then switch levels) */
char const *map_pack_menus(void);
boolean map_pack_menus_known(void);

/* a map's name as the pack's menus mean it, into name: a level named alone
(cex_museum) that only the pack has is the pack's (cex_museum@cex); any
other name as it is */
void map_pack_menus_map_name(char const *map_name, char *name, long size);

/* a listed map's name, its pack's name and the pack's tag (map_name as the
game names it, <file>@<pack>), for a host's announcement: 0 (all empty) for
another family's map. Plain types: the platform layer calls it */
int map_pack_listing(char const *map, char *title, int title_size, char *pack, int pack_size, char *tag,
	int tag_size);

#endif
