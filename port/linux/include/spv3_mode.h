/*
SPV3_MODE.H

SPV3 (a campaign mod for Halo Custom Edition on OpenSauce) as an early mode
(port/linux/game/spv3_mode.c): whether it is on, where its maps are, what of
OpenSauce it lets a map ask for, and its log. Bring your own copy: none of
its files ship.
*/

#ifndef SPV3_MODE_H
#define SPV3_MODE_H

/* the pack id: maps_spv3, game.pack_menus = "spv3" */
#define SPV3_MODE_PACK_ID "spv3"
#define SPV3_MODE_FOLDER "maps_spv3"

/* OpenSauce's memory upgrades (blam_memory_upgrades.hpp: 1.5 times Custom
Edition's): its tag cache, which its maps' structure BSPs are linked to the
top of, and its tag count */
#define SPV3_OPENSAUCE_TAG_CACHE_SIZE 0x02280000UL
#define SPV3_OPENSAUCE_TAG_COUNT 7680

/* whether SPV3 mode is on: with the spv3 pack's menus (maps_spv3\ui.yelo) */
boolean spv3_mode_active(void);

/* whether the folder of a pack is SPV3's and has its ui.yelo */
boolean spv3_mode_pack_present(char const *pack);

/* nonzero once the larger tag window OpenSauce's maps need is mapped: on a
64-bit desktop build, by its heap at the first allocation when an SPV3
install (maps_spv3) is in the data folder (port/linux/src/xbox_heap.c) */
extern int spv3_opensauce_window;

/* the Custom Edition tag cache's size: OpenSauce's in SPV3 mode with the
window mapped, else Custom Edition's */
unsigned long spv3_mode_tag_cache_size(unsigned long custom_edition_size);

/* a line in debug.txt, "spv3: ...", in SPV3 mode only */
void spv3_mode_log(char const *format, ...);

/* the path of maps_spv3\<file>.yelo (or .map) into path: FALSE if SPV3 mode
is off or it is not there */
boolean spv3_mode_map_path(char const *file, char *path, long size);

/* whether a map named so is SPV3's menus' (ui), played from maps_spv3 as a
Custom Edition map, in SPV3 mode */
boolean spv3_mode_is_ui(char const *map_name);

/* whether a Custom Edition cache whose OpenSauce header asks for flags may
be read: in SPV3 mode, with the window mapped, memory upgrades alone (each
other flag logged and refused) */
boolean spv3_mode_accepts_opensauce(char const *map_name, unsigned short flags);

/* the survey of a map just read, before it is checked (ce_map_checks.c):
its header, OpenSauce's header, its tags' groups, its BSPs against the
window and its scripts' functions and globals the game lacks; once per map,
in SPV3 mode. tags is the tag data as read, at the tag cache's base */
void spv3_mode_survey(char const *map_name, void const *cache_header, void const *tags, unsigned long tag_data_size,
	unsigned long file_size);

/* a script's function or global the game lacks, named as the scripts were
compiled (hs_compile.c), counted for the map's summary */
void spv3_mode_note_missing_function(char const *name);
void spv3_mode_note_missing_global(char const *name);
/* the map's scripts compiled: the counts logged and cleared */
void spv3_mode_scripts_summary(long dropped_scripts, long scripts, long dropped_globals, long globals);

/* a resource (a bitmap's pixels or a sound's samples) a tag asked a resource
map for that it does not have */
void spv3_mode_note_resource_miss(char const *kind, char const *tag_name);

/* a change the loader made to a map's tags (ce_repairs.c): counted and its
first ones logged */
void spv3_mode_note_correction(char const *what);

#endif
