/* RENDER_EPOCH.H (Halo 2 mode)
	(port) What Halo 2 mode's units used from Project Ark's render_epoch.h, on
	this engine, which renders and ticks on one thread: no epochs, every
	thread is the tick's. HALO_ENV_ONCE reads a switch once a run. */
#ifndef H2_RENDER_EPOCH_COMPAT_H
#define H2_RENDER_EPOCH_COMPAT_H

#include <stdlib.h>

/* changes with every map loaded (halo2_mode.c: halo2_mode_map_loaded) */
extern unsigned long halo_map_generation;
/* changes with every settings change (never, here) */
extern volatile unsigned long halo_settings_generation;

#define halo_epoch_on_mutator() 1
#define halo_epoch_on_mutator_inline() 1
#define halo_epoch_mutator_alone_inline() 1
int halo_thread_index(void);
#define halo_epoch_tick_view 0
#define halo_epoch_tick_view_set(on) ((void)(on))


#endif
