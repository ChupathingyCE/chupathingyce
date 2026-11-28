/* H2_PORT.H
	(port) Force-included ahead of each of Halo 2 mode's units
	(port/linux/game/ark: tools/linux_build.py, HALO2_PORT_HEADER).

	These units come from Project Ark, which built them on its own engine: a
	Halo CE port with a parallel tick, a larger tag cache and fields of its own
	in a few engine structures. What they took from that engine and this one
	does not have (or names otherwise) is given here, so the units move as
	they are. Each entry says what it stands for; PLAN-next lists them. */

#ifndef H2_PORT_H
#define H2_PORT_H

/* (Project Ark's units test HALO_LINUX for the native desktop code paths) */
#ifndef HALO_LINUX
#define HALO_LINUX 1
#endif

#include <sys/mman.h>
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS 0x20
#endif

/* (Ark's render_epoch.h) a switch's value, read from the environment once a run */
#include <stdlib.h>
#define HALO_ENV_ONCE(name) (__extension__ ({ \
	static int halo_env_read_; \
	static char const *halo_env_value_; \
	if (!halo_env_read_) \
	{ \
		halo_env_value_ = getenv(name); \
		halo_env_read_ = 1; \
	} \
	halo_env_value_; }))

/* changes with every map loaded (halo2_mode.c) */
extern unsigned long halo_map_generation;

/* (Ark's halo_port_capacity.h, the desktop tier) the surfaces of one
structure BSP Ark's renderer has room for; this engine's MAXIMUM_SURFACES_PER_STRUCTURE */
#define HALO_PORT_MAXIMUM_SURFACES_PER_STRUCTURE 0x20000

/* (Ark's physical_memory_map.h) the largest tag cache a level may ask for:
this engine's fixed one */
#define TAG_CACHE_LARGE_MAXIMUM_SIZE TAG_CACHE_SIZE

/* (Ark's bitmap_group.h) a streamed texture's dropped levels, which Ark
keeps in the record's padding: this engine's padding field. Its renderer
does not read them yet (PLAN-next: texture streaming) */
#define levels_dropped mipmap_pad
#define BITMAP_LEVELS_DROPPED_STREAM 0x100

/* (Ark's devices.h) a machine that never closes by itself: Ark's flag after
this engine's last machine flag */
#define _machine_does_not_close_automatically_bit 31

/* (Ark's game_engine.h: Halo 2's Territories, held back: h2_port_stubs.c) */
#define MAXIMUM_TERRITORIES 8
unsigned char territories_active(void); /* (boolean) */
#endif
