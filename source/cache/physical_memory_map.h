/*
PHYSICAL_MEMORY_MAP.H

header included in hcex build.
*/

#ifndef __PHYSICAL_MEMORY_MAP_H
#define __PHYSICAL_MEMORY_MAP_H
#pragma once

/* ---------- constants */

/* port: the tag cache's size, which the loader checks a map's tag data and
structure bsps against (cache_files.c) */
#define TAG_CACHE_SIZE 0x1600000

/* port: the sound cache's size (the Xbox's 4 MB); a platform may define its
own, and the cache's pages follow it (xbox_sound_cache.c). A console that
answers for its memory class (halo_console_memory.h) has it at run time:
the Xbox's 4 MB, more on a console above its baseline */
#ifdef HALO_CONSOLE_MEMORY_CLASS
unsigned long physical_memory_get_sound_cache_size(void);

#define SOUND_CACHE_SIZE physical_memory_get_sound_cache_size()
#endif
#ifndef SOUND_CACHE_SIZE
#define SOUND_CACHE_SIZE 0x400000
#endif

/* ---------- macros */

/* ---------- structures */

/* ---------- prototypes/PHYSICAL_MEMORY_MAP.C */

void physical_memory_allocate(void);
void physical_memory_verify(void);
void physical_memory_free(void);

void *physical_memory_get_game_state_base_address(void);
void *physical_memory_get_tag_cache_base_address(void);
void *physical_memory_get_texture_cache_base_address(void);
void *physical_memory_get_sound_cache_base_address(void);

/* ---------- globals */

/* ---------- public code */

#endif // __PHYSICAL_MEMORY_MAP_H
