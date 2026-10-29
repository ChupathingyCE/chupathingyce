/*
HALO_PERF_LOG.H

The frame timing log (console.perf_log): once a second, one line in
debug.txt with the frame time (average and worst), the time each part of the
main loop took (game ticks, the render's parts, sound, network, present and
the wait for the vertical blank), the draw calls and objects drawn, and a
line when the player zooms in or out. For finding what a slow scene spends
its time on, on hardware with no profiler attached.

A port that has it defines HALO_PERF_LOG (the original Xbox's prefix,
halo_xbox_prefix.h) and gives the functions below (port/xbox/game/
xbox_perf_log.c). The consoles on the shared GL layer (HALO_CONSOLE_GL_LAYER:
the Xbox 360, the Wii U) have it from here, with the functions in
port/console/game/console_perf_log.c. Elsewhere, and while the setting is
off, each hook is a test of one flag. Counters and timers only: nothing is
formatted until the second's line.
*/

#ifndef __HALO_PERF_LOG_H
#define __HALO_PERF_LOG_H

#if defined(HALO_CONSOLE_GL_LAYER) && !defined(HALO_PERF_LOG)
#define HALO_PERF_LOG
#endif

/* the parts of a frame that are timed. The render's own parts (world to
HUD, and setup to end) are inside render; sound is partly inside the game
tick (game_frame's game_sound_update) and partly inside render
(sound_render). The waits (occlusion, texture) are inside whichever part
they happen in, and are logged on their own as well */
enum halo_perf_phase
{
	_halo_perf_tick,
	_halo_perf_render,
	_halo_perf_render_world,
	_halo_perf_render_objects,
	_halo_perf_render_transparent,
	_halo_perf_render_effects,
	_halo_perf_render_hud,
	_halo_perf_sound,
	_halo_perf_network,
	_halo_perf_present,
	_halo_perf_vsync_wait,
	/* the render's finer parts, on the second line: the frame's start
	(rasterizer_frame_begin), a player view's setup before its window (fog,
	visibility, a mirror's view), the lens flares (their visibility tests
	and their drawing, split from world and effects), the
	HUD's screen effect (the scope's mask, blur and night vision, split from
	hud) and the end (the console window, EndScene, rasterizer_frame_end) */
	_halo_perf_render_begin,
	_halo_perf_render_setup,
	_halo_perf_render_lens_flares,
	_halo_perf_render_screen_effect,
	_halo_perf_render_end,
	/* (the sound's share that is inside render: sound_render) */
	_halo_perf_render_sound,
	/* the processor waiting on the graphics chip for the lens flares'
	occlusion results, and on the disc for a texture the cache had to load
	before drawing */
	_halo_perf_occlusion_wait,
	_halo_perf_texture_wait,
	NUMBER_OF_HALO_PERF_PHASES
};

/* what is counted each frame */
enum halo_perf_counter
{
	_halo_perf_ticks,
	_halo_perf_draws,
	_halo_perf_objects,
	_halo_perf_models,
	_halo_perf_models_high_lod,
	/* textures the cache started loading, and the screen effect's passes */
	_halo_perf_texture_loads,
	_halo_perf_screen_effect_passes,
	/* the lens flares' visibility (occlusion) tests drawn */
	_halo_perf_visibility_tests,
	NUMBER_OF_HALO_PERF_COUNTERS
};

#ifdef HALO_PERF_LOG
extern int halo_perf_log_on;
extern unsigned long halo_perf_counters[NUMBER_OF_HALO_PERF_COUNTERS];

void halo_perf_begin(int phase);
void halo_perf_end(int phase);
/* the render's parts follow each other: the one now running (NONE: none) */
void halo_perf_render_part(int phase);
/* once each pass of the main loop, at its end */
void halo_perf_frame(void);

#define HALO_PERF_BEGIN(phase) do { if (halo_perf_log_on) halo_perf_begin(phase); } while (0)
#define HALO_PERF_END(phase) do { if (halo_perf_log_on) halo_perf_end(phase); } while (0)
#define HALO_PERF_RENDER_PART(phase) do { if (halo_perf_log_on) halo_perf_render_part(phase); } while (0)
#define HALO_PERF_COUNT(counter, count) do { if (halo_perf_log_on) halo_perf_counters[counter] += (count); } while (0)
#define HALO_PERF_FRAME() halo_perf_frame()
#else
#define HALO_PERF_BEGIN(phase) ((void)0)
#define HALO_PERF_END(phase) ((void)0)
#define HALO_PERF_RENDER_PART(phase) ((void)0)
#define HALO_PERF_COUNT(counter, count) ((void)0)
#define HALO_PERF_FRAME() ((void)0)
#endif

#endif
