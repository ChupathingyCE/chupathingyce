/*
OPENSAUCE_MAPS.H

How the game reads an OpenSauce map (port/linux/game/opensauce_maps.c), any
map with OpenSauce's header, SPV3's among them: which of its header's asks
it can meet, OpenSauce's larger tag window, and the tag cache's size for the
map being checked or loaded. SPV3 mode (spv3_mode.c) adds only SPV3's own
things on top.

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

#else

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

#endif
