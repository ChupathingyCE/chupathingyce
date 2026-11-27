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

/* whether SPV3 mode is on: with the spv3 pack's menus (maps_spv3\ui.yelo) */
boolean spv3_mode_active(void);

/* whether the folder of a pack is SPV3's and has its ui.yelo */
boolean spv3_mode_pack_present(char const *pack);

/* a line in debug.txt, "spv3: ...", in SPV3 mode only */
void spv3_mode_log(char const *format, ...);

/* the path of maps_spv3\<file>.yelo (or .map) into path: FALSE if SPV3 mode
is off or it is not there */
boolean spv3_mode_map_path(char const *file, char *path, long size);

/* whether a map named so is SPV3's menus' (ui), played from maps_spv3 as a
Custom Edition map, in SPV3 mode */
boolean spv3_mode_is_ui(char const *map_name);

/* a Custom Edition map opened from path (cache_files_windows.c): for the
ui, whether it is SPV3's own maps_spv3\ui.yelo */
void spv3_mode_note_map_opened(char const *map_name, char const *path);

/* whether the ui open is SPV3's own file (not one the loader fell back to):
only its scripts may change the level (hs.c) */
boolean spv3_mode_own_ui_open(char const *map_name);

/* a level SPV3's ui names with map_name (spv3a10) as this game names it
(spv3a10@ce) into name: FALSE, the name said once in the log, unless it is
a plain name of maps_spv3's .yelo levels */
boolean spv3_mode_level_name(char const *file, char *name, long size);

/* a change SPV3 mode made to what a map asks for (a camera point narrowed,
camera_scripting.c; a grenade type passed over, units.c): counted and its
first ones logged, in SPV3 mode, by OpenSauce's map log (opensauce_survey.h) */
void spv3_mode_note_correction(char const *what);

#endif
