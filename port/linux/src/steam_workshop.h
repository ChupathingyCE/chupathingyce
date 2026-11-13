/*
STEAM_WORKSHOP.H

The MCC maps of the player's subscribed Steam Workshop items, found in place.
A Halo CE Workshop item is a folder, <library>/steamapps/workshop/content/976730/<item id>/, with a ModInfo.json
(its title, engine and multiplayer maps' json) and its .map files in a maps folder. The catalog lists them for
mcc_maps.c as the level mcc_maps\<item id>\<map name>, and platform_translate_path (xbox_files.c) opens that
package folder as the item's maps folder, so nothing is copied into the data root. A package folder of that name
in the data root's mcc_maps folder wins over the item. Resource requests also find the item's halo1/maps
files and MCC's installed shared sets, while explicit local resources retain precedence.

The json files are the item makers' text: too large, deep or odd ones refuse their item or map, never crash.
*/

#ifndef __HALO_LINUX_STEAM_WORKSHOP_H
#define __HALO_LINUX_STEAM_WORKSHOP_H

enum
{
	/* the item folders' names are decimal ids of at most this many digits */
	STEAM_WORKSHOP_ID_SIZE = 24,
	STEAM_WORKSHOP_TITLE_SIZE = 64,
	STEAM_WORKSHOP_PATH_SIZE = 512,
	STEAM_WORKSHOP_MAXIMUM_MAPS = 1024,
	/* the largest ModInfo.json or map json read */
	STEAM_WORKSHOP_MAXIMUM_JSON = 256 * 1024,
	/* One multiplayer collection can occupy the whole catalog. */
	STEAM_WORKSHOP_MAXIMUM_ITEM_MAPS = STEAM_WORKSHOP_MAXIMUM_MAPS,
	STEAM_WORKSHOP_MAXIMUM_LIBRARIES = 16,
	/* the maps a scan keeps that couldn't be listed (steam_workshop_unlisted), and their reasons' size */
	STEAM_WORKSHOP_MAXIMUM_UNLISTED = 256,
	STEAM_WORKSHOP_REASON_SIZE = 32,
	STEAM_WORKSHOP_DETAIL_SIZE = 128,
};

struct steam_workshop_map
{
	char item_id[STEAM_WORKSHOP_ID_SIZE];
	/* the item's Title.Neutral; "" if it has none */
	char item_title[STEAM_WORKSHOP_TITLE_SIZE];
	/* the .map file's name, without ".map", as it is spelled on disk */
	char name[64];
	/* the map's own Title.Neutral; the name if it has none */
	char title[STEAM_WORKSHOP_TITLE_SIZE];
	/* the .map file, host path */
	char path[STEAM_WORKSHOP_PATH_SIZE];
	/* the map's Images.Thumbnail file, host path; "" if it names none that exists */
	char preview[STEAM_WORKSHOP_PATH_SIZE];
};

/* ---------- parsing (text in, no files) */

struct steam_workshop_item_text
{
	char title[STEAM_WORKSHOP_TITLE_SIZE];
	/* Engine is "Halo1" */
	int halo1;
	int has_campaign;
	int map_count;
	/* MultiplayerMaps: item-relative json paths, '/' separators */
	char map_json[STEAM_WORKSHOP_MAXIMUM_ITEM_MAPS][128];
};

struct steam_workshop_map_text
{
	/* ScenarioFile's last part */
	char name[64];
	char title[STEAM_WORKSHOP_TITLE_SIZE];
	/* Images.Thumbnail, item-relative, '/' separators; "" if absent or not a plain relative path */
	char thumbnail[128];
	/* Images.Large, with the same relative-path contract. */
	char large[128];
};

/* a ModInfo.json's text: 1 if it is an object whose lists are sound, else 0. A MultiplayerMaps path that is
absolute, holds ".." or a control character refuses the whole item */
int steam_workshop_read_item(const char *text, int length, struct steam_workshop_item_text *item);
/* a map json's text: 1 if it has a usable ScenarioFile */
int steam_workshop_read_map(const char *text, int length, struct steam_workshop_map_text *map);
/* the "path" values of a libraryfolders.vdf: how many were written to paths (each STEAM_WORKSHOP_PATH_SIZE) */
int steam_workshop_read_libraries(const char *text, int length, char (*paths)[STEAM_WORKSHOP_PATH_SIZE], int maximum);

/* ---------- the catalog */

#ifdef _WIN32
/* Windows-only registry reader; keeps Windows SDK headers out of Xbox-facing units. */
int steam_workshop_windows_roots(char (*paths)[STEAM_WORKSHOP_PATH_SIZE], int maximum);
#endif

/* the game's own Steam library (steamcmd's downloads with
game.workshop_steam: workshop_browser.c), looked at after paths.steam;
"" none */
void steam_workshop_set_own_library(const char *path);
/* the Steam library folders (paths.steam first, then the Steam folders found and the libraries their
libraryfolders.vdf names), each once, whether or not game.workshop_maps is on: how many were written */
int steam_workshop_libraries(char (*paths)[STEAM_WORKSHOP_PATH_SIZE], int maximum);
/* looks in the Steam libraries again (paths.steam first, Windows registry or Linux environment hints, usual folders,
and the libraries their libraryfolders.vdf names). Returns how many maps were found. Each item once, by its id */
int steam_workshop_scan(void);
/* the same, the first time only; then the last scan's count */
/* the Workshop items' maps folders, each once, with their item ids (item_ids
may be NULL): their count, at most maximum; 0 with game.workshop_maps off */
int steam_workshop_map_folders(char (*folders)[STEAM_WORKSHOP_PATH_SIZE],
	char (*item_ids)[STEAM_WORKSHOP_ID_SIZE], int maximum);
int steam_workshop_count(void);
const struct steam_workshop_map *steam_workshop_map(int index);
/* the folder holding the .map files of an item (the package folder of mcc_maps\<item id>); 1 and the path, or 0
if no scanned map belongs to that id */
int steam_workshop_package_folder(const char *item_id, char *folder, unsigned long size);
/* Resolve an MCC-relative package/shared resource in place; zero keeps the existing path.
 * Explicit data-root packages/files and beside-level resources retain precedence. */
int steam_workshop_resource_path(const char *request, const char *data_root, char *path, unsigned long size);
/* whether a map has needed MCC's own shared files (halo1/maps) and they
weren't found this run: the Workshop tab then offers them */
int steam_workshop_shared_missing(void);
/* a map of an item (map "": the whole item) that can't be listed: reason is a word or two for the item's
Workshop page ("Map missing"), detail the log's own words, file names and all. Logged once a scan and kept
(the scan's own, and mcc_maps.c's for a map file it can't use) */
void steam_workshop_unlisted(const char *item_id, const char *map, const char *reason, const char *detail);
/* how many of the item's maps (item_id NULL: any item's) couldn't be listed; the which-th's item id
(STEAM_WORKSHOP_ID_SIZE), map (64), reason (STEAM_WORKSHOP_REASON_SIZE) and detail
(STEAM_WORKSHOP_DETAIL_SIZE) when there is one and they aren't NULL */
int steam_workshop_item_unlisted(const char *item_id, int which, char *found_item, char *map, char *reason,
	char *detail);
/* the item's title; 1 if some scanned map belongs to the id (the title may still be "") */
int steam_workshop_item_title(const char *item_id, char *title, unsigned long size);

#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define steam_workshop_package_folder(a0, a1, a2) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), 0)
#define steam_workshop_resource_path(a0, a1, a2, a3) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), 0)
#endif

#endif
