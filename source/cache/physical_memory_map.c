/*
PHYSICAL_MEMORY_MAP.C

symbols in this file:
001AD660 00f0:
	_physical_memory_allocate (0000)
001AD750 00a0:
	_physical_memory_verify (0000)
001AD7F0 0040:
	_physical_memory_free (0000)
001AD830 0010:
	_physical_memory_get_game_state_base_address (0000)
001AD840 0010:
	_physical_memory_get_tag_cache_base_address (0000)
001AD850 0010:
	_physical_memory_get_texture_cache_base_address (0000)
001AD860 0010:
	_physical_memory_get_sound_cache_base_address (0000)
002A76A0 0035:
	??_C@_0DF@JIMKNOMI@physical_memory_map_globals?4soun@ (0000)
002A76D8 0037:
	??_C@_0DH@HFDEEEDC@physical_memory_map_globals?4text@ (0000)
002A7710 005a:
	??_C@_0FK@BLCIEIND@?$CIunsigned?5long?$CJphysical_memory_m@ (0000)
002A7770 005c:
	??_C@_0FM@EHFFGLDK@?$CIunsigned?5long?$CJphysical_memory_m@ (0000)
002A77CC 002b:
	??_C@_0CL@BKHLOEEB@c?3?2halo?2SOURCE?2cache?2physical_me@ (0000)
002A77F8 001e:
	??_C@_0BO@DKDCAKMK@page_status?5?$DN?$DN?5PAGE_READWRITE?$AA@ (0000)
004D1074 0010:
	_physical_memory_map_globals (0000)
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries_windows.h"
#include "cache/physical_memory_map.h"

/* ---------- constants */

#ifndef HALO_XBOX_CONSOLE
/* the native builds' larger game state, placed above the tag cache
(halo_port_capacity.h); the verified part is the CPU part, as on the Xbox */
#define GAME_STATE_BASE_ADDRESS HALO_PORT_GAME_STATE_BASE_ADDRESS
#define GAME_STATE_SIZE HALO_PORT_GAME_STATE_SIZE
#define GAME_STATE_VERIFY_SIZE HALO_PORT_GAME_STATE_CPU_SIZE
#else
/* (the Xbox's, unless a test build holds more than 16 players: halo_port_capacity.h) */
#define GAME_STATE_BASE_ADDRESS HALO_PORT_GAME_STATE_BASE_ADDRESS
#define GAME_STATE_SIZE HALO_PORT_GAME_STATE_SIZE
#define GAME_STATE_VERIFY_SIZE HALO_PORT_GAME_STATE_CPU_SIZE
#endif
#define TAG_CACHE_BASE_ADDRESS 0x803A6000
#ifndef HALO_XBOX_CONSOLE
/* (the native builds': halo_port_capacity.h) */
#define TEXTURE_CACHE_SIZE HALO_PORT_TEXTURE_CACHE_SIZE
#define TEXTURE_CACHE_MAXIMUM_ADDRESS -1
#else
/* port: the console's, by its memory class (port/xbox/src/xbox_memory.c);
a raised cache is kept in the low 128 MB */
unsigned long xbox_memory_texture_cache_size(void);
unsigned long xbox_memory_texture_cache_maximum_address(void);

#define TEXTURE_CACHE_SIZE xbox_memory_texture_cache_size()
#define TEXTURE_CACHE_MAXIMUM_ADDRESS xbox_memory_texture_cache_maximum_address()
#endif
#ifndef HALO_XBOX_CONSOLE
#define SOUND_CACHE_MAXIMUM_ADDRESS -1
#else
/* port: the console's, by its memory class too (the Xbox's 4 MB, 8 MB on a
console above the 64 MB baseline, kept in the low 128 MB) */
unsigned long xbox_memory_sound_cache_size(void);
unsigned long xbox_memory_sound_cache_maximum_address(void);

#undef SOUND_CACHE_SIZE
#define SOUND_CACHE_SIZE xbox_memory_sound_cache_size()
#define SOUND_CACHE_MAXIMUM_ADDRESS xbox_memory_sound_cache_maximum_address()
#endif

#ifdef HALO_XBOX_CONSOLE
/* port: each cache logged as it is made, before the game's own log is up
(port/xbox/src/xbox_memory.c): a console without the memory for one says
which */
void xbox_memory_allocation(char const *name, unsigned long size, void const *address);

#define PHYSICAL_MEMORY_LOG(name, size, address) xbox_memory_allocation(name, size, address)
#else
#define PHYSICAL_MEMORY_LOG(name, size, address)
#endif

/* ---------- macros */

/* ---------- structures */

struct physical_memory_map_globals
{
	void *game_state_base_address;
	void *tag_cache_base_address;
	void *texture_cache_base_address;
	void *sound_cache_base_address;
};

/* ---------- prototypes */

/* ---------- globals */

static struct physical_memory_map_globals physical_memory_map_globals;

/* ---------- public code */

void physical_memory_allocate(
	void)
{
	physical_memory_map_globals.game_state_base_address = XPhysicalAlloc(GAME_STATE_SIZE, GAME_STATE_BASE_ADDRESS & 0x7FFFFFFF, 0, PAGE_READWRITE);
	PHYSICAL_MEMORY_LOG("game state", GAME_STATE_SIZE, physical_memory_map_globals.game_state_base_address);
#ifdef HALO_64BIT
#line 46 "c:\\halo\\SOURCE\\cache\\physical_memory_map.c"
	match_assert(__FILE__, __LINE__, XBOX_ADDRESS(physical_memory_map_globals.game_state_base_address)==GAME_STATE_BASE_ADDRESS);
#elif defined(HALO_WINDOW_REBASE)
#line 46 "c:\\halo\\SOURCE\\cache\\physical_memory_map.c"
	match_assert(__FILE__, __LINE__, (unsigned long)XBOX_ADDRESS(physical_memory_map_globals.game_state_base_address)==GAME_STATE_BASE_ADDRESS);
#else
#line 46 "c:\\halo\\SOURCE\\cache\\physical_memory_map.c"
	match_assert(__FILE__, __LINE__, (unsigned long)physical_memory_map_globals.game_state_base_address==GAME_STATE_BASE_ADDRESS);
#endif

	physical_memory_map_globals.tag_cache_base_address = XPhysicalAlloc(TAG_CACHE_SIZE, TAG_CACHE_BASE_ADDRESS & 0x7FFFFFFF, 0, PAGE_READWRITE);
	PHYSICAL_MEMORY_LOG("tag cache", TAG_CACHE_SIZE, physical_memory_map_globals.tag_cache_base_address);
#ifdef HALO_64BIT
#line 50 "c:\\halo\\SOURCE\\cache\\physical_memory_map.c"
	match_assert(__FILE__, __LINE__, XBOX_ADDRESS(physical_memory_map_globals.tag_cache_base_address)==TAG_CACHE_BASE_ADDRESS);
#elif defined(HALO_WINDOW_REBASE)
#line 50 "c:\\halo\\SOURCE\\cache\\physical_memory_map.c"
	match_assert(__FILE__, __LINE__, (unsigned long)XBOX_ADDRESS(physical_memory_map_globals.tag_cache_base_address)==TAG_CACHE_BASE_ADDRESS);
#else
#line 50 "c:\\halo\\SOURCE\\cache\\physical_memory_map.c"
	match_assert(__FILE__, __LINE__, (unsigned long)physical_memory_map_globals.tag_cache_base_address==TAG_CACHE_BASE_ADDRESS);
#endif

	physical_memory_map_globals.texture_cache_base_address = XPhysicalAlloc(TEXTURE_CACHE_SIZE, TEXTURE_CACHE_MAXIMUM_ADDRESS, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
	PHYSICAL_MEMORY_LOG("texture cache", TEXTURE_CACHE_SIZE, physical_memory_map_globals.texture_cache_base_address);
#line 55 "c:\\halo\\SOURCE\\cache\\physical_memory_map.c"
	match_assert(__FILE__, __LINE__, physical_memory_map_globals.texture_cache_base_address);

	physical_memory_map_globals.sound_cache_base_address = XPhysicalAlloc(SOUND_CACHE_SIZE, SOUND_CACHE_MAXIMUM_ADDRESS, 0, PAGE_READWRITE);
	PHYSICAL_MEMORY_LOG("sound cache", SOUND_CACHE_SIZE, physical_memory_map_globals.sound_cache_base_address);
#line 58 "c:\\halo\\SOURCE\\cache\\physical_memory_map.c"
	match_assert(__FILE__, __LINE__, physical_memory_map_globals.sound_cache_base_address);

	return;
}

void physical_memory_verify(
	void)
{
	byte *address;
	unsigned long page_status;

	for (address = physical_memory_map_globals.tag_cache_base_address;
		address < (byte *)physical_memory_map_globals.tag_cache_base_address + TAG_CACHE_SIZE;
		address += 0x1000)
	{
		page_status = XQueryMemoryProtect(address);
#line 77 "c:\\halo\\SOURCE\\cache\\physical_memory_map.c"
		match_assert(__FILE__, __LINE__, page_status == PAGE_READWRITE);
	}

	for (address = physical_memory_map_globals.game_state_base_address;
		address < (byte *)physical_memory_map_globals.game_state_base_address + GAME_STATE_VERIFY_SIZE;
		address += 0x1000)
	{
		page_status = XQueryMemoryProtect(address);
#line 86 "c:\\halo\\SOURCE\\cache\\physical_memory_map.c"
		match_assert(__FILE__, __LINE__, page_status == PAGE_READWRITE);
	}

	return;
}

void physical_memory_free(
	void)
{
	if (physical_memory_map_globals.game_state_base_address)
		XPhysicalFree(physical_memory_map_globals.game_state_base_address);
	if (physical_memory_map_globals.tag_cache_base_address)
		XPhysicalFree(physical_memory_map_globals.tag_cache_base_address);
	if (physical_memory_map_globals.texture_cache_base_address)
		XPhysicalFree(physical_memory_map_globals.texture_cache_base_address);
	if (physical_memory_map_globals.sound_cache_base_address)
		XPhysicalFree(physical_memory_map_globals.sound_cache_base_address);

	return;
}

void *physical_memory_get_game_state_base_address(
	void)
{
	return physical_memory_map_globals.game_state_base_address;
}

void *physical_memory_get_tag_cache_base_address(
	void)
{
	return physical_memory_map_globals.tag_cache_base_address;
}

void *physical_memory_get_texture_cache_base_address(
	void)
{
	return physical_memory_map_globals.texture_cache_base_address;
}

void *physical_memory_get_sound_cache_base_address(
	void)
{
	return physical_memory_map_globals.sound_cache_base_address;
}

#ifdef HALO_CONSOLE_MEMORY_CLASS
/* port: the sound cache's size in bytes (the Xbox's 0x400000, or more on a
console above its memory baseline), for the sound cache's pages and the
sound channels' checks of a sound's place in it (physical_memory_map.h's
SOUND_CACHE_SIZE) */
unsigned long physical_memory_get_sound_cache_size(
	void)
{
	return SOUND_CACHE_SIZE;
}
#endif

/* ---------- private code */
