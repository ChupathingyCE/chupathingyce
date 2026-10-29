/*
STUBBS_MODE.H

Stubbs the Zombie's maps as a development-only mode (port/linux/game/stubbs_mode.c).
*/

#ifndef STUBBS_MODE_H
#define STUBBS_MODE_H

/* the pack id: maps_stubbs, game.pack_menus = "stubbs" */
#define STUBBS_MODE_PACK_ID "stubbs"

/* where Stubbs's Xbox maps' tags are linked (Halo's: 0x803A6000) */
#define STUBBS_XBOX_TAG_CACHE_BASE 0x8038B000UL

/* whether Stubbs mode is on: a development build, with the stubbs pack's menus */
boolean stubbs_mode_active(void);

/* the tag cache's address: Stubbs's in Stubbs mode, else halo_base */
unsigned long stubbs_mode_tag_cache_base(unsigned long halo_base);

/* a line in debug.txt, in Stubbs mode only */
void stubbs_mode_log(char const *format, ...);

/* Stubbs's tags just read (Xbox maps) rewritten where Halo's differ, before
the schema check: nothing outside Stubbs mode */
void stubbs_mode_convert_tags(void *tag_cache, unsigned long tag_data_size);

/* how stubbs_mode_convert_tags reads the open map's file (cache_files.c):
TRUE once size bytes at offset are in buffer */
void stubbs_mode_set_file_reader(boolean (*read)(long offset, long size, void *buffer));

/* whether size bytes at address are in the animation frame data Stubbs mode
read from the map file (tag_validate.c) */
boolean stubbs_mode_owns_data(void const *address, long size);

/* a Stubbs structure BSP just read (its header at bsp_base, size bytes)
rewritten where Halo's differs, before its schema check */
void stubbs_mode_convert_structure_bsp(void *bsp_base, unsigned long size);

/* Stubbs's script functions (stubbs_functions.c): how many there are (none
outside Stubbs mode), one by its index among them, and a name's index (NONE) */
struct hs_function_definition;
long stubbs_function_count(void);
struct hs_function_definition *stubbs_function_get(long index);
long stubbs_function_find(char const *name);

#endif
