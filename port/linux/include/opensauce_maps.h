/*
OPENSAUCE_MAPS.H

How the game reads an OpenSauce map (port/linux/game/opensauce_maps.c), any
map with OpenSauce's header, SPV3's among them: which of its header's asks
it can meet, OpenSauce's larger tag window, the tag cache's size for the map
being checked or loaded, and where its maps are: maps_os, for any OpenSauce
map, and an OpenSauce mod's own folder with its menus (maps_<pack>\ui.yelo,
played as its own mode). SPV3 mode (spv3_mode.c) adds only SPV3's own things
on top.

Only the 64-bit desktop builds map the larger window (port/linux/src/
xbox_heap.c), as only they offer SPV3 mode: elsewhere a map asking for
OpenSauce's memory upgrades is refused, as before. A console's build reads
no OpenSauce map: there the calls are fixed.
*/

#ifndef OPENSAUCE_MAPS_H
#define OPENSAUCE_MAPS_H

/* the builds that can map OpenSauce's larger tag window: the 64-bit
desktop's (spv3_mode_active, launcher.c and xbox_heap.c's list) */
#if defined(HALO_CUSTOM_EDITION) && defined(HALO_MAP_LIST) && defined(HALO_64BIT) && \
	!defined(HALO_WINDOW_TRANSLATED) && !defined(HALO_XBOX_CONSOLE) && !defined(HALO_CONSOLE)
#define OPENSAUCE_MAPS_MEMORY_UPGRADES
#endif

/* nonzero once the larger tag window OpenSauce's memory upgrades need is
mapped: by the heap at its first allocation (port/linux/src/xbox_heap.c) */
extern int opensauce_tag_window;

/* the folder of OpenSauce maps beside maps_ce: <name>.yelo (or .map) is
<name>@ce, found after Custom Edition's own places */
#define OPENSAUCE_MAPS_FOLDER "maps_os"

#if defined(HALO_CUSTOM_EDITION) && !defined(HALO_CONSOLE) && !defined(HALO_XBOX_CONSOLE)

#define OPENSAUCE_MAPS

/* whether the larger tag window is mapped, on a build that reads maps with
it */
boolean opensauce_maps_window_mapped(void);

/* whether a Custom Edition cache whose OpenSauce header asks for flags may
be read: memory upgrades with the window mapped, and nothing else (each
other flag logged and refused). Its answer is the map being checked's
(opensauce_maps_check_started) */
boolean opensauce_maps_accepts_header(char const *map_name, unsigned short flags);

/* a Custom Edition map's checks start, and end (ce_map_checks.c): while
they run, the tag cache's size is the map being checked's */
void opensauce_maps_check_started(void);
void opensauce_maps_check_done(void);

/* whether the map being checked, or else the map loaded, is read with
OpenSauce's memory upgrades: one whose header asks for them, with the window
mapped; any map in SPV3 mode with the window mapped, as SPV3 mode reads them */
boolean opensauce_maps_memory_upgrades(void);

/* the Custom Edition tag cache's size: OpenSauce's when the map is read
with its memory upgrades, else custom_edition_size */
unsigned long opensauce_maps_tag_cache_size(unsigned long custom_edition_size);

/* the same for a cache's header in hand (its first 0x800 bytes), before the
map is opened */
unsigned long opensauce_maps_header_tag_cache_size(void const *cache_header, unsigned long custom_edition_size);

/* the path of maps_os\<file>.yelo (or .map) into path: FALSE if it is not
there. Never the menus (ui): a ui in maps_os is no one's menus */
boolean opensauce_maps_folder_map_path(char const *file, char *path, long size);

/* whether a pack's folder is an OpenSauce mod's, played as its own mode:
maps_<pack>\ui.yelo and no ui.map, on a build that reads memory upgrades.
SPV3's is SPV3 mode's (spv3_mode.c), not this */
boolean opensauce_maps_pack_present(char const *pack);

/* the OpenSauce mod whose menus are played (game.pack_menus), NULL if none */
char const *opensauce_maps_pack(void);

/* whether a map named so is the OpenSauce mod's menus (ui), played from its
folder as a Custom Edition map */
boolean opensauce_maps_pack_is_ui(char const *map_name);

/* the path of maps_<pack>\<file>.yelo (or .map) of the OpenSauce mod
played, its ui among them: FALSE if none is played or it is not there */
boolean opensauce_maps_pack_map_path(char const *file, char *path, long size);

/* the path of the OpenSauce mod's own resource map (bitmaps, sounds, loc)
into path: FALSE if none is played or it has none of that name */
boolean opensauce_maps_pack_resource(char const *name, char *path, long size);

/* the network session a map loads in: hosted (and whether the internet may
join it), or joined (and whether the host is on this machine's LAN, not an
internet peer reached through the tunnel) */
struct opensauce_maps_session
{
	boolean hosting;
	boolean hosting_internet;
	boolean joined;
	boolean host_on_lan;
};

/* whether a session is an online one: a game hosted for the internet
(Online Games, online co-op, a dedicated server), or one joined from the
internet. Offline play and system link are not */
boolean opensauce_maps_session_is_online(struct opensauce_maps_session const *session);

/* whether a Custom Edition map whose OpenSauce header asks for flags may
load in the session played now: never in an online session, for now,
outside SPV3 mode (which keeps its own rules). FALSE said in the log */
boolean opensauce_maps_online_allows(char const *map_name, unsigned short flags,
	struct opensauce_maps_session const *session);

/* the session played now (opensauce_session.c: the network game's) */
void opensauce_maps_session_now(struct opensauce_maps_session *session);

/* opensauce_maps_online_allows for a cache's header in hand (its first 0x78
bytes at least) and the session played now: for a map already open, which
is not checked again (cache_files_windows.c) */
boolean opensauce_maps_header_online_allows(char const *map_name, void const *cache_header);

/* whether a cache's header (its first 0x78 bytes at least) is a map for
local play only, for now: OpenSauce's header asking for any of its flags
(memory upgrades, first of all), which only this game reads. Never in the
lists games are hosted from (map_families.c); Load Level lists its
campaigns */
boolean opensauce_maps_header_local_only(void const *cache_header);

#else

struct opensauce_maps_session
{
	boolean hosting;
	boolean hosting_internet;
	boolean joined;
	boolean host_on_lan;
};
static __inline boolean opensauce_maps_online_allows(char const *map_name, unsigned short flags,
	struct opensauce_maps_session const *session)
{
	(void)map_name;
	(void)flags;
	(void)session;
	return TRUE;
}
static __inline boolean opensauce_maps_header_online_allows(char const *map_name, void const *cache_header)
{
	(void)map_name;
	(void)cache_header;
	return TRUE;
}
static __inline void opensauce_maps_session_now(struct opensauce_maps_session *session)
{
	session->hosting = session->hosting_internet = session->joined = session->host_on_lan = FALSE;
}
static __inline boolean opensauce_maps_header_local_only(void const *cache_header)
{
	(void)cache_header;
	return FALSE;
}
static __inline boolean opensauce_maps_folder_map_path(char const *file, char *path, long size)
{
	(void)file;
	(void)path;
	(void)size;
	return FALSE;
}
static __inline boolean opensauce_maps_pack_present(char const *pack)
{
	(void)pack;
	return FALSE;
}
static __inline char const *opensauce_maps_pack(void) { return NULL; }
static __inline boolean opensauce_maps_pack_is_ui(char const *map_name)
{
	(void)map_name;
	return FALSE;
}
static __inline boolean opensauce_maps_pack_map_path(char const *file, char *path, long size)
{
	(void)file;
	(void)path;
	(void)size;
	return FALSE;
}
static __inline boolean opensauce_maps_pack_resource(char const *name, char *path, long size)
{
	(void)name;
	(void)path;
	(void)size;
	return FALSE;
}
static __inline boolean opensauce_maps_window_mapped(void) { return FALSE; }
static __inline boolean opensauce_maps_accepts_header(char const *map_name, unsigned short flags)
{
	(void)map_name;
	(void)flags;
	return FALSE;
}
static __inline void opensauce_maps_check_started(void) { }
static __inline void opensauce_maps_check_done(void) { }
static __inline boolean opensauce_maps_memory_upgrades(void) { return FALSE; }
static __inline unsigned long opensauce_maps_tag_cache_size(unsigned long custom_edition_size)
{
	return custom_edition_size;
}
static __inline unsigned long opensauce_maps_header_tag_cache_size(void const *cache_header,
	unsigned long custom_edition_size)
{
	(void)cache_header;
	return custom_edition_size;
}

#endif

/* whose menus' scripts may call map_name (hs.c): a known map pack's own
ui.map (map_pack_menus_known), never an OpenSauce mod's ui.yelo whatever
ui.map of a known pack sits in another of its folders; or SPV3's own
ui.yelo (spv3_mode_own_ui_open), for its levels alone. Any other map's,
none */
enum
{
	_opensauce_menus_may_not_change_level,
	_opensauce_menus_known_pack,
	_opensauce_menus_spv3,
};
short opensauce_maps_menus_level_rule(char const *map_name);

#endif
