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

#endif
