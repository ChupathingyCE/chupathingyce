/*
HALO_CONSOLE_FEATURES.H

What a console build of the shared platform layer and game code asks of
them, as capabilities any console can turn on (included by
halo_port_limits.h, which every build's prefix header reads). The shared
code tests these, never a console's name; a console turns a capability on
by defining it here from its own build's define (the Switch's
HALO_SWITCH_CONSOLE, the original Xbox's HALO_XBOX_CONSOLE, the Xbox 360's
HALO_XBOX360_CONSOLE, the Wii U's HALO_WIIU_CONSOLE), or on its
compile lines (HALO_CONSOLE_PLAYERS, HALO_CONSOLE_DELTA,
HALO_CONSOLE_MEMORY_CLASS, the HALO_CONSOLE_NO_* of a port's prefix header:
docs/building.md, "Console ports").

A console's name stays in the shared code only for what is that console's
alone: the original Xbox's own target (HALO_XBOX_CONSOLE: the game's code
as the Xbox ran it, one frame a tick on its Direct3D and XDK, where the
native builds changed it, and its calls into port/xbox), the Switch's
settings and renderer calls.

HALO_CONSOLE_GPU_RESOURCE_HOOKS
	a renderer whose GPU reads the game's memory after the draw: resources
	report busy and locks wait for the GPU (halo_resource_busy and
	halo_resource_wait, weak in port/linux/src/d3d8_resources.c, defined
	for real by the console's renderer), and a busy dynamic vertex buffer
	gets other storage instead of waiting
HALO_CONSOLE_NO_WRITE_WATCH
	a console that will not write-protect guest memory, so the renderer's
	write watch cannot see the game write a resource: locks and file reads
	into the window say so themselves (memory_watch_prepare_write)
HALO_CONSOLE_HOST_ART
	a host renderer of its own: read-only accessors for the menus' art
	(menu_files.h), the high-res HUD's PNGs and the text atlas (hud_hires.h,
	text_hires.h), and a map's HUD sheets handed to the host to decode
	(dk_hud_prepare, dk_hud_forget)
HALO_CONSOLE_MAP_LOAD_HOOKS
	a renderer told when a map loads (d3d8_gl_map_loaded, from
	scenario_load) and that compiles its shaders under the loading screen,
	which waits for them (d3d8_gl_map_loading, d3d8_gl_map_progress,
	d3d8_gl_map_waiting, from game_precache_new_map)
HALO_CONSOLE_STATUS_LINE
	terminal_status_printf: one console line kept on screen and rewritten
	(source/interface/terminal.c)
HALO_CONSOLE_LOG_THROTTLE
	a console whose log writes are slow (debug.txt on a card): the host's
	notices are logged without their blank lines, at most three a second
	(port/linux/game/network_distributed.c)
HALO_CONSOLE_LOG_BUFFER
	debug.txt's lines kept in memory and written together, a second after
	the first waits, and at once for an exception, an exit or a crash
	(error_log_flush and error_log_update, source/cseries/errors.c;
	console.log_flush)
HALO_CONSOLE_WINDOW_MAXIMUM
	a console that puts the Xbox's memory window where it finds room
	(HALO_WINDOW_REBASE, source/cseries/xbox_address.h) and tells the
	platform layer its size before any constructor runs
	(platform_contiguous_base and platform_contiguous_size): the window is
	from a development kit's 128 MB up to this many bytes
	(port/linux/src/platform.h), and the texture cache half of it
	(halo_port_capacity.h)
HALO_CONSOLE_NO_THREAD_LOCAL
	a console whose executables have no thread-local storage (the Wii U's
	RPX: its loader takes no TLS relocations): the platform layer's
	per-thread variables are in blocks the port keeps for each thread
	(platform_thread_local, port/linux/src/platform.h), and a release
	build's skipped-assert flag is shared, as on the original Xbox
	(source/cseries/cseries.h)
HALO_CONSOLE_WIDE_TIME_T
	a 32-bit console whose C library's time_t is 64 bits (newlib's): the
	error log's time() is given a time_t (source/cseries/errors.c)
HALO_CONSOLE_LOG_ECHO
	debug.txt's lines also in the platform's log (platform_log), for a
	console that shows its log on the screen while it has no other way to
	say why it stopped (source/cseries/errors.c)
HALO_CONSOLE_ONLINE_GAMES
	the Multiplayer menu's ONLINE GAMES on a build without the desktop's
	game browser (HALO_GAME_BROWSER): the console's own screen of the game
	list (browser_screen_* and the invite join, defined by the port), with
	the menu's fonts (ui_widget_online_games_font, source/interface/
	ui_widget.c; the original Xbox's port/xbox/game/xbox_browser_screen.c,
	the Xbox 360's port/xbox360/game/x360_browser_screen.c, the Wii U's
	port/wiiu/game/wiiu_browser_screen.c)
HALO_CONSOLE_DEBUG_CHANNEL
	a console without a standard error: the platform layer's log lines and
	OutputDebugStringA go to its debug channel (console_debug_print,
	defined by the port), and the game's log lines too (errors.c)
HALO_CONSOLE_HALT_TO_LAUNCHER
	a halt (halt_and_catch_fire, source/main/main.c) logs where it was
	(console_log_backtrace) and returns to the console's launcher
	(console_exit_to_launcher), both defined by the port
HALO_CONSOLE_GL_LAYER
	a console whose OpenGL is a layer of the port's own over another
	graphics API: the stream and index buffers stay small (an orphaned
	buffer is new memory until the GPU is done with it), mip levels are
	copied by blit (no glCopyImageSubData), and the vertex mirror's
	segments are the window's own memory (console_gl_buffer_alias, defined
	by the port: port/linux/src/d3d8_gl.c)
HALO_CONSOLE_XBOX_RENDER_SIZES
	the Xbox's texture cache and the Xbox's structure surfaces and dynamic
	triangles in a frame (halo_port_capacity.h), on a console whose build
	otherwise has the desktop's
HALO_CONSOLE_WAIT_POLL
	a console whose system must be served by the thread that runs the game
	(the Wii U's HOME button): console_wait_poll, defined by the port, is
	called by every wait that thread makes in the platform layer and the
	game's cache files, and console_trace puts a line in the port's log for
	the steps of a cache file's making (cache_files_windows.c)
HALO_CONSOLE_BRINGUP_LOG
	a console's developer tools (HALO_CONSOLE_DEBUG_TOOLS) for its
	bring-up: the main loop's stages in its profile log
	(console_profile_stage: cseries/frame_profile.h), the loop's pulse
	(console_main_loop_tick) and the structure bsp switches in the log
HALO_CONSOLE_OVERSIZED_MAPS
	a console that copies maps to the Xbox's fixed-size cache files: a map
	larger than its type's (a map pack's multiplayer maps, past retail's
	47 MB) is copied to a campaign map's instead, and a map whose tags do
	not fit the tag cache says so (source/cache/cache_files_windows.c,
	cache_files.c). The retail maps fit their own and are cached as before.
	The desktop builds with map packs (HALO_MAP_LIST) have it too

A big-endian console (HALO_BIG_ENDIAN, its prefix header) reads the maps,
which are the Xbox's, little-endian, through port/console/game's byte swap
of their headers and tags. A 32-bit console whose memory cannot have the
Xbox's addresses builds with the 64-bit builds' pointers over a window
placed wherever it has room (HALO_64BIT with HALO_WINDOW_TRANSLATED, on its
compile lines: source/cseries/xbox_address.h).
*/

#ifndef __HALO_CONSOLE_FEATURES_H
#define __HALO_CONSOLE_FEATURES_H

#ifdef HALO_SWITCH_CONSOLE
#define HALO_CONSOLE_GPU_RESOURCE_HOOKS 1
#define HALO_CONSOLE_NO_WRITE_WATCH 1
#define HALO_CONSOLE_HOST_ART 1
#define HALO_CONSOLE_MAP_LOAD_HOOKS 1
#define HALO_CONSOLE_STATUS_LINE 1
#define HALO_CONSOLE_LOG_THROTTLE 1
#endif

#ifdef HALO_WIIU_CONSOLE
#define HALO_CONSOLE_NO_WRITE_WATCH 1
#define HALO_CONSOLE_LOG_THROTTLE 1
#define HALO_CONSOLE_WINDOW_MAXIMUM 0x10000000UL
#define HALO_CONSOLE_NO_THREAD_LOCAL 1
#define HALO_CONSOLE_WIDE_TIME_T 1
#define HALO_CONSOLE_LOG_ECHO 1
#define HALO_CONSOLE_ONLINE_GAMES 1
#define HALO_CONSOLE_WAIT_POLL 1
#define HALO_CONSOLE_GL_LAYER 1
#endif

#ifdef HALO_XBOX_CONSOLE
#define HALO_CONSOLE_LOG_BUFFER 1
#define HALO_CONSOLE_ONLINE_GAMES 1
#define HALO_CONSOLE_OVERSIZED_MAPS 1
#endif

/* (the desktop builds copy maps to the Xbox's cache files too: a map pack's
larger maps take a campaign map's there as well) */
#if defined(HALO_MAP_LIST) && !defined(HALO_CONSOLE_PLAYERS) && !defined(HALO_CONSOLE_OVERSIZED_MAPS)
#define HALO_CONSOLE_OVERSIZED_MAPS 1
#endif

#ifdef HALO_XBOX360_CONSOLE
#define HALO_CONSOLE_NO_WRITE_WATCH 1
#define HALO_CONSOLE_DEBUG_CHANNEL 1
#define HALO_CONSOLE_HALT_TO_LAUNCHER 1
#define HALO_CONSOLE_GL_LAYER 1
#define HALO_CONSOLE_XBOX_RENDER_SIZES 1
#define HALO_CONSOLE_ONLINE_GAMES 1
#ifdef HALO_CONSOLE_DEBUG_TOOLS
#define HALO_CONSOLE_BRINGUP_LOG 1
#endif
#endif

#ifdef HALO_CONSOLE_WAIT_POLL
/* (defined by the port) */
void console_wait_poll(void);
void console_trace(const char *format, ...);
#endif

#endif
