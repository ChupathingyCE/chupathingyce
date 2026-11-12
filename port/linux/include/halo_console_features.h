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
HALO_CONSOLE_LIGHT_LIMITS
	the dynamic light limits on by default: of the dynamic lights on the
	screen, the 8 largest light the level and the 2 largest add their
	specular highlights (display.dynamic_light_limit and
	display.specular_light_limit, source/objects/object_lights.c). The
	desktop builds default both to 0, every light lit as before, and opt in
HALO_CONSOLE_MATCH_LIGHT_LIMITS
	the Xbox's rule for what a multiplayer match draws, as retail: no
	dynamic lights with 13 or more players (or 9 or more with two or more
	vehicles), no flashlight beams with 5 or more players or 4 or more
	vehicles, decided at the match's start (port/linux/game/
	match_light_limits.h, game_engine_build_lighting). Drawing only: the
	flashlight's toggle, battery and effect run as on every other machine
	in the game. For a GPU of the original Xbox's size: the desktop builds
	draw every light
HALO_CONSOLE_WIDGET_POOL_SIZE
	the menus' widget memory, in bytes, for a console that shows only the
	Xbox's menus: the desktop's 256 KB is for the PC menus' screens
	(port/assets/menus), which have many more widgets (source/interface/
	ui_widget.c; console.widget_pool_log logs what the menus use)
HALO_CONSOLE_NO_GUIDE
	no Guide (port/linux/game/guide.c, over any screen) and no presence
	sent to the site (browser.c, network.share_presence), their settings
	left out of config.toml: every console for now, until the Guide is
	sized to each one's memory and input
HALO_CONSOLE_NO_CAPTURE
	no screenshots or clips (port/linux/src/capture.c and capture_clips.c,
	controls.screenshot and controls.clip): every console; the desktop
	builds have them
HALO_CONSOLE_NO_MEDAL_ICONS
	no medal icons (port/assets/medals, ui_overlay_medal): the HUD's medals
	are lines of its messages. The original Xbox sets it (its 64 MB); every
	console is without them anyway for now, as the icons need the game list
	(HALO_GAME_BROWSER), which no console build has
HALO_CONSOLE_NO_DELTA_IDENTITY
	no device key, device certificate or site token (port/linux/src/
	identity.c, browser.c; docs/delta.md, "Device identity"), and no
	verified names in games (Delta Peer's identity capability, never
	offered: "Verified names"): every console for now, until their own
	endpoints come; the player key alone, and game names, as before
HALO_CONSOLE_NO_HOST_MAP_DOWNLOADS
	no Custom Edition map downloaded from a game's host, peer to peer
	(network.map_downloads_from_host, port/linux/src/map_download.c "From
	the host"): every console, which has no game list (HALO_GAME_BROWSER)
	anyway; the desktops and Android have it, off until the player turns
	it on
HALO_CONSOLE_NO_STUBBS
	no Stubbs mode (port/linux/game/stubbs_mode.c: game.pack_menus =
	"stubbs" with a maps_stubbs folder): every console, until the mode is
	finished on the desktop and sized to each one's memory
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
HALO_CONSOLE_HTTPS
	HTTPS through port/console/https (Mbed TLS over the port's sockets,
	struct console_https_port, the site's key and GitHub's roots pinned:
	docs/console-https.md), on its compile line: the signed legacy table
	fetched by the console itself (delta_legacy_refresh, port/linux/src/
	delta.c, called on a thread of the port's) and cached beside
	config.toml
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
HALO_CONSOLE_PARTICLE_LIMIT
	display.particle_limit's default on a console, as a string ("1024": the
	original Xbox's and the retail game's cap), where the desktop's 4096
	costs a console too many draws (port/linux/src/port_config.c)
HALO_CONSOLE_NATIVE_RENDERER
	a console with a renderer of its own under the Xbox's Direct3D 8 device:
	display.renderer = "native" draws through console_renderer.h, defined
	by the port, instead of the OpenGL layer (port/linux/src/d3d8_console.inl),
	and "gl" as before; the setting's default is "gl", or "native" with
	HALO_CONSOLE_NATIVE_RENDERER_FIRST on the port's compile line
HALO_CONSOLE_RENDER_TIMING
	a console that logs where a frame's time goes: the renderer tells it
	when the game enters and leaves the device's draws, clears and present
	(console_render_time, defined by the port: port/linux/src/d3d8_gl.c)
HALO_CONSOLE_XBOX_RENDER_SIZES
	the Xbox's texture cache and the Xbox's structure surfaces and dynamic
	triangles in a frame (halo_port_capacity.h), on a console whose build
	otherwise has the desktop's
HALO_CONSOLE_GLES2
	a console with OpenGL ES 2 only: the renderer's ES path (HALO_GL_ES,
	halo_linux_prefix.h) on an ES 2 context, which console_gles2_layer,
	defined by the port, fills out to the ES 3 it uses (gl_functions.c)
HALO_CONSOLE_AUDIO_OUTPUT
	a console whose sound output SDL does not have: where SDL opens no
	device, console_audio_start, defined by the port, plays the mixer's
	output (dsound_sdl.c)
HALO_CONSOLE_LIGHT_AUDIO
	the cheaper sound by default: linear resampling and no reverb
	(audio.resampling, audio.reverb), on a console with one slow core (the
	Steam Link's, where the sinc resampler and the reverb took two fifths of
	the processor)
HALO_CONSOLE_HUD_ON_DEMAND
	the high-res HUD's pictures decoded each when first drawn, as Android's
	are, rather than a map's all at its load (hud_hires.c): about 40 MB less
	at once, on a console with little memory (the Steam Link's 256 MB)
HALO_CONSOLE_PERFORMANCE_MODE
	a console too slow for the full picture: display.performance_mode, and
	console_performance_update, defined by the port, called each pass of
	the event pump (sdl_platform.c), which leaves passes out while it is on
	and switches it on a button of the port's; console_performance_lines,
	the lines the picture is drawn with while it is on (0 for the game's
	480: screen_mode_choose, d3d8_gl.c), and
	console_performance_view_distance, how far the camera sees (0 for as
	far as the level does: render.c fogs and clips the view at it), and
	console_performance_model_detail, what a model's size on the screen is
	multiplied by when its detail is chosen (1 for the game's own: models.c),
	and console_performance_sky, console_performance_object_transparents,
	console_performance_decoration_distance and
	console_performance_hud_frames, what else it leaves out (render_sky.c,
	render_objects.c, hud_draw.c)
HALO_CONSOLE_CACHE_SCREEN
	a console whose cache files are slow to make (a FAT32 stick, which writes
	them full of zeros): they are made long a little at a time, and
	console_cache_screen, defined by the port, shows how far they have got
	(source/cache/cache_files_windows.c), and console_cache_screen_done when
	they are made
HALO_CONSOLE_SETUP_SCREENS
	a console with no dialogs or file picker, on a television: the start-up's
	screens are the port's. console_setup_start, right after SDL starts
	(sdl_platform.c), and console_setup_after_data, once the maps are
	checked; platform_offer_game_data is the port's too, with
	platform_import_start and platform_import_poll for the copy. The
	updater asks nothing on its own (updater_start, updater_poll): the
	port calls updater_console_check, which asks with
	console_setup_update_question and shows the download with
	console_setup_screen_open, console_setup_screen_progress and
	console_setup_screen_close, and a failure with console_setup_notice
	(updater.c)
HALO_CONSOLE_APP_CERTIFICATES
	a console whose own certificate authorities are too old for today's
	servers: the app's bundle, which its launch script names in
	SSL_CERT_FILE, trusted ahead of the system's for updates
	(posix_update.c)
HALO_CONSOLE_CONFIG_IN_DATA_ROOT
	config.toml in the data folder the launch script names (HALO_DATA_ROOT),
	as on Android, not beside the executable (port_config.c)
HALO_PORT_UPDATE_PLATFORM
	a console port's own download on the game's releases, on its compile
	line: chupathingyce-<platform>-release.zip, listed in the release's
	updates.json and signed with the release key as the desktop builds'
	are. A release is offered only once its assets have that zip
	(updater.c)
HALO_PORT_UPDATE_API, HALO_PORT_UPDATE_REPOSITORY
	a console port with releases of its own instead, on its compile line:
	the address of its release list, laid out as GitHub's API (and its
	downloads under /releases/download), and its name. Its releases are
	signed with its own key, update_public_keys in the port's
	halo_port_update_keys.h (update_key.h); the game's release key signs
	nothing the port's server sends
HALO_PORT_UPDATE_SCRIPT, HALO_PORT_UPDATE_RESTART_EXIT_CODE
	the launch script an update replaces with the game, and the code the
	game exits with after an update for that script to start the new
	version (updater.c)
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
HALO_CONSOLE_MENU_PLATFORM
	a console that shows the Chupathingy menus without the desktop's
	extras: the menus' elements marked platform="console", or with this
	console's own name ("x360", "wiiu"), in place of the desktop's
	(port/linux/src/menu_files.c; tools/port_settings.py packs the settings
	rows the consoles show), and the menus' button prompts drawn with
	ui.map's own pictures where no overlay draws them
	(chupathingy/prompts.xml)
HALO_CONSOLE_PARTICLE_TICKS
	the particles not attached to an object moved once a 30 Hz tick, as on
	the Xbox, and drawn between their ticks, by default
	(display.particle_ticks, source/effects/particles.c): two in three of
	their collision tests spared at 60 frames a second. The desktop builds
	default to moving them at every frame drawn, smooth at high refresh
	rates, and opt in
HALO_CONSOLE_XBOX_MAP_SIZES
	a console that plays the Xbox's own maps only (no Custom Edition or
	Halo PC maps): tables the desktop builds size for those maps are sized
	for the Xbox's. The lens flares' occlusion results hold 8,192 rows,
	not 32,768 (source/rasterizer/rasterizer_lights.c; the most markers in
	a retail or Halo CE+ map's BSP is d40's 4,616): 96 KB less. The
	vehicles that respawn where the map put them are 256, not 1,024
	(source/game/game_engine.c; the most a retail or Halo CE+ map places
	is b40's 74): 39 KB less. A map past either limit loses only what is
	past it: the lens flares past it (said once in debug.txt), or those
	vehicles' respawn
HALO_CONSOLE_NO_HALOMD_MAPS
	a console that plays Custom Edition maps but not HaloMD's, named in
	the message a HaloMD map gets ("the Xbox"): the menus' map list leaves
	maps_md out (port/linux/game/ui_map_list.c), and a game on a HaloMD
	map says it can't be played here rather than that the map is missing
	(source/cache/cache_files.c)
HALO_CONSOLE_NO_LOADING_PICTURE
	the disc's loading.tga on the loading screen, as the Xbox showed it,
	instead of Chupathingy's picture (port/linux/game/loading_picture.c,
	display.xbox_loading_picture): every console, whose packaging has no
	step for the picture
HALO_CONSOLE_NO_TITLE_CHOICE
	only the classic titles (port/assets/titles, Chupathingy Titles), not
	the second set display.title_font = "cyborg" draws (port/assets/
	titles_cyborg and the menus' ce/port_cyborg, about 2.5 MB of pictures):
	every console, for its memory; the desktop builds and the phones have
	both (HALO_TITLE_CHOICE)
HALO_CONSOLE_ANAMORPHIC_SCREEN
	a console that draws 640x480 and can show it on a 16:9 television,
	which stretches the picture to fit (display.widescreen). The port says
	how wide the interface is in the stretched picture (halo_screen_width:
	640, or 852 for 16:9), the present flags that ask for the mode
	(halo_screen_present_flags) and how its units and the 640 pixels
	convert (halo_screen_interface_x, halo_screen_physical_x). The 3D view
	is drawn at the wider aspect in the same 640 pixels (render_cameras.c),
	and the HUD, the menus and their text are laid out as the desktop's
	wide screen lays them out, in the wider units, between
	rasterizer_interface_begin and rasterizer_interface_end (render.c,
	rasterizer_xbox.c), so that they look right once stretched
HALO_PORT_WIDE_INTERFACE
	(not set by a port) a build whose interface can be wider than 640
	columns: every native build, and a console with
	HALO_CONSOLE_ANAMORPHIC_SCREEN. The HUD's zoomed view and the menus'
	full-screen fades have code for it (hud_draw.c, ui_widget.c)
HALO_CONSOLE_HOST_PLAYERS
	the most players a game this console hosts takes, whatever its network
	tables hold (HALO_CONSOLE_PLAYERS) and whatever Delta says: the original
	Xbox hosts 16, on LAN or online, and joins games of up to 32 in its
	builds of 32 (network_server_manager.c's hosted maximum; docs/delta.md,
	"Platform policy"). Without it a host takes as many as its tables hold
HALO_CONSOLE_SERVER_CLOSE_GRACE
	the original game's one-second wait when a hosted game ends, before the
	host's transport closes (network_game_server_dispose, source/
	networking/network_server_manager.c): for a console on the XDK's XNet,
	whose sockets need it. The native transports flush before they close
	and need no wait
HALO_CONSOLE_FRAME_RATE
	a console that draws one frame a tick, as the Xbox did, and can draw
	60 frames a second over its 30 Hz ticks instead (display.frame_rate,
	off by default). The port says the refresh rate the device is made for
	(halo_console_frame_rate: 30 or 60, read once, before
	rasterizer_initialize makes it) and whether frames are drawn between
	the ticks (halo_interpolation_enabled, the same answer for the whole
	run). At 60 the frame pacer steps down to 30 while a scene can't hold
	60 and back up when it can (main_update_time's stabilization)
HALO_CONSOLE_VIEW_INTERPOLATION
	the frames between ticks move the view only, with no memory of their
	own (port/linux/game/render_interpolation.c): on foot in first person
	the camera turns with the stick every frame and eases into each tick's
	place, and the first-person weapon eases with it; the objects are drawn
	where the tick left them, as the H1 Performance Build drew them. The
	native builds' blend of every object between ticks (about 0.5 MB more
	on a busy map) is not made
HALO_CONSOLE_FLICKER_FILTER
	a console on a television whose flicker filter is a setting
	(display.flicker_filter): the port gives the level, 0 to 5
	(halo_console_flicker_filter), set once the device is made. The Xbox
	game asked for 5
HALO_CONSOLE_WAITED_VISIBILITY_TESTS
	a console whose Direct3D waits for a visibility test's count, as the
	Xbox's does: a lens flare reads its test from the slot of its index in
	the frame's list, as the Xbox game did. Every other build's counts come
	back frames later, so each flare keeps a slot of its own and reads only
	counts of its own tests (rasterizer_lights.c)
HALO_PORT_FRAMES_BETWEEN_TICKS
	(not set by a port) a build that can draw several frames a tick: every
	native build, and a console with HALO_CONSOLE_FRAME_RATE. What the Xbox
	counted in frames, a frame being a tick there, is counted in ticks
	(the HUD's timers, effects, particles, contrails, lighting refreshes);
	where that would change play or sound at one frame a tick (a held
	button's count, the listener's velocity, the pause lock), the build
	asks halo_port_frames_between_ticks() and keeps the Xbox's counting
	when the answer is no

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
#define HALO_CONSOLE_PARTICLE_TICKS 1
#define HALO_CONSOLE_GPU_RESOURCE_HOOKS 1
#define HALO_CONSOLE_NO_WRITE_WATCH 1
#define HALO_CONSOLE_HOST_ART 1
#define HALO_CONSOLE_MAP_LOAD_HOOKS 1
#define HALO_CONSOLE_STATUS_LINE 1
#define HALO_CONSOLE_LOG_THROTTLE 1
#define HALO_CONSOLE_LIGHT_LIMITS 1
#endif

#ifdef HALO_WIIU_CONSOLE
#define HALO_CONSOLE_PARTICLE_TICKS 1
#define HALO_CONSOLE_NO_WRITE_WATCH 1
#define HALO_CONSOLE_LOG_THROTTLE 1
#define HALO_CONSOLE_WINDOW_MAXIMUM 0x10000000UL
#define HALO_CONSOLE_NO_THREAD_LOCAL 1
#define HALO_CONSOLE_WIDE_TIME_T 1
#define HALO_CONSOLE_LOG_ECHO 1
#define HALO_CONSOLE_ONLINE_GAMES 1
#define HALO_CONSOLE_WAIT_POLL 1
#define HALO_CONSOLE_GL_LAYER 1
#define HALO_CONSOLE_LIGHT_LIMITS 1
#define HALO_CONSOLE_OVERSIZED_MAPS 1
#define HALO_CONSOLE_MENU_PLATFORM "wiiu"
#endif

#ifdef HALO_XBOX_CONSOLE
#define HALO_CONSOLE_PARTICLE_TICKS 1
#define HALO_CONSOLE_LOG_BUFFER 1
/* (the H1 Performance Build's 64 KB; the retail game had 16 KB) */
#define HALO_CONSOLE_WIDGET_POOL_SIZE 0x10000
#define HALO_CONSOLE_ONLINE_GAMES 1
#define HALO_CONSOLE_OVERSIZED_MAPS 1
#define HALO_CONSOLE_LIGHT_LIMITS 1
#define HALO_CONSOLE_XBOX_MAP_SIZES 1
#define HALO_CONSOLE_MATCH_LIGHT_LIMITS 1
#define HALO_CONSOLE_NO_HALOMD_MAPS "the Xbox"
#define HALO_CONSOLE_SERVER_CLOSE_GRACE 1
#define HALO_CONSOLE_HOST_PLAYERS 16
#define HALO_CONSOLE_ANAMORPHIC_SCREEN 1
#define HALO_CONSOLE_FRAME_RATE 1
#define HALO_CONSOLE_VIEW_INTERPOLATION 1
#define HALO_CONSOLE_FLICKER_FILTER 1
#define HALO_CONSOLE_WAITED_VISIBILITY_TESTS 1
#endif

#if !defined(HALO_XBOX_CONSOLE) || defined(HALO_CONSOLE_ANAMORPHIC_SCREEN)
#define HALO_PORT_WIDE_INTERFACE 1
#endif

#if !defined(HALO_XBOX_CONSOLE) || defined(HALO_CONSOLE_FRAME_RATE)
#define HALO_PORT_FRAMES_BETWEEN_TICKS 1
#endif
#ifdef HALO_CONSOLE_FRAME_RATE
/* (defined by the port) */
short halo_console_frame_rate(void);
int halo_interpolation_enabled(void);
/* (a console draws its ticks only, unless its setting asks for more) */
#define halo_port_frames_between_ticks() (halo_interpolation_enabled() != 0)
#else
#define halo_port_frames_between_ticks() 1
#endif
#ifdef HALO_CONSOLE_FLICKER_FILTER
/* (defined by the port) */
long halo_console_flicker_filter(void);
#endif

/* (the desktop builds copy maps to the Xbox's cache files too: a map pack's
larger maps take a campaign map's there as well) */
#if defined(HALO_MAP_LIST) && !defined(HALO_CONSOLE_PLAYERS) && !defined(HALO_CONSOLE_OVERSIZED_MAPS)
#define HALO_CONSOLE_OVERSIZED_MAPS 1
#endif

#ifdef HALO_XBOX360_CONSOLE
#define HALO_CONSOLE_PARTICLE_TICKS 1
#define HALO_CONSOLE_NO_WRITE_WATCH 1
#define HALO_CONSOLE_DEBUG_CHANNEL 1
#define HALO_CONSOLE_HALT_TO_LAUNCHER 1
#define HALO_CONSOLE_GL_LAYER 1
#define HALO_CONSOLE_XBOX_RENDER_SIZES 1
#define HALO_CONSOLE_RENDER_TIMING 1
#define HALO_CONSOLE_NATIVE_RENDERER 1
#define HALO_CONSOLE_PARTICLE_LIMIT "1024"
#define HALO_CONSOLE_ONLINE_GAMES 1
#define HALO_CONSOLE_LIGHT_LIMITS 1
#define HALO_CONSOLE_OVERSIZED_MAPS 1
#define HALO_CONSOLE_MENU_PLATFORM "x360"
#ifdef HALO_CONSOLE_DEBUG_TOOLS
#define HALO_CONSOLE_BRINGUP_LOG 1
#endif
#endif

/* the Guide (port/linux/game/guide.c, port/linux/src/social.c) and the
presence it tells the site (browser.c): desktop and Android first; each
console gets them sized to its limits later */
#if (defined(HALO_CONSOLE) || defined(HALO_SWITCH_CONSOLE) || defined(HALO_WIIU_CONSOLE) || \
	defined(HALO_XBOX_CONSOLE) || defined(HALO_XBOX360_CONSOLE)) && !defined(HALO_CONSOLE_NO_GUIDE)
#define HALO_CONSOLE_NO_GUIDE 1
#endif
/* (the Guide's builds: the game list's, not the consoles' nor the dedicated
server's, which has no player) */
#if defined(HALO_GAME_BROWSER) && !defined(HALO_CONSOLE_NO_GUIDE) && !defined(HALO_SERVER)
#define HALO_GUIDE 1
#endif

/* a missing Custom Edition map downloaded from the game's host, peer to
peer, when the player allows it (network.map_downloads_from_host,
port/linux/src/map_download.c "From the host", map_share_client.c): the
game list's builds with a player (the desktops and Android), not the
consoles nor the dedicated server, which downloads nothing */
#if (defined(HALO_CONSOLE) || defined(HALO_SWITCH_CONSOLE) || defined(HALO_WIIU_CONSOLE) || \
	defined(HALO_XBOX_CONSOLE) || defined(HALO_XBOX360_CONSOLE)) && !defined(HALO_CONSOLE_NO_HOST_MAP_DOWNLOADS)
#define HALO_CONSOLE_NO_HOST_MAP_DOWNLOADS 1
#endif
#if defined(HALO_GAME_BROWSER) && !defined(HALO_CONSOLE_NO_HOST_MAP_DOWNLOADS) && !defined(HALO_SERVER)
#define HALO_HOST_MAP_DOWNLOADS 1
#endif

/* Stubbs mode (port/linux/game/stubbs_mode.c), an early look: the desktop
builds alone for now, not the consoles */
#if (defined(HALO_CONSOLE) || defined(HALO_SWITCH_CONSOLE) || defined(HALO_WIIU_CONSOLE) || \
	defined(HALO_XBOX_CONSOLE) || defined(HALO_XBOX360_CONSOLE)) && !defined(HALO_CONSOLE_NO_STUBBS)
#define HALO_CONSOLE_NO_STUBBS 1
#endif

/* the medals' icons (port/assets/medals, tools/medal_assets.py: the site's
medals), in the kill feed (display.medals) and DELTA's SERVICE RECORD: the
game list's builds with a player (HALO_GAME_BROWSER: the desktops and the
phones), so no console build and not the dedicated server. Those keep the
HUD's text lines. (The 32-pixel atlas is there for a console that draws
them one day; the builds embed the 64-pixel one) */
#if defined(HALO_XBOX_CONSOLE) && !defined(HALO_CONSOLE_NO_MEDAL_ICONS)
#define HALO_CONSOLE_NO_MEDAL_ICONS 1
#endif
#if defined(HALO_GAME_BROWSER) && !defined(HALO_CONSOLE_NO_MEDAL_ICONS) && !defined(HALO_SERVER)
#define HALO_MEDAL_ICONS 1
#endif

/* screenshots and clips (port/linux/src/capture.c): the desktop builds'
alone, with DELTA's toasts; not the consoles, the phones or the
dedicated server */
#if (defined(HALO_CONSOLE) || defined(HALO_SWITCH_CONSOLE) || defined(HALO_WIIU_CONSOLE) || \
	defined(HALO_XBOX_CONSOLE) || defined(HALO_XBOX360_CONSOLE)) && !defined(HALO_CONSOLE_NO_CAPTURE)
#define HALO_CONSOLE_NO_CAPTURE 1
#endif
#if defined(HALO_GUIDE) && !defined(HALO_CONSOLE_NO_CAPTURE) && !defined(HALO_ANDROID) && !defined(HALO_IOS)
#define HALO_CAPTURE 1
#endif

/* Delta's device identity (port/linux/src/identity.c, browser.c): desktop and
Android; consoles get it with their own endpoints later */
#if (defined(HALO_CONSOLE) || defined(HALO_SWITCH_CONSOLE) || defined(HALO_WIIU_CONSOLE) || \
	defined(HALO_XBOX_CONSOLE) || defined(HALO_XBOX360_CONSOLE)) && !defined(HALO_CONSOLE_NO_DELTA_IDENTITY)
#define HALO_CONSOLE_NO_DELTA_IDENTITY 1
#endif
/* (its builds: the game list's, not the consoles' nor the dedicated
server's, which has no player) */
#if defined(HALO_GAME_BROWSER) && !defined(HALO_CONSOLE_NO_DELTA_IDENTITY) && !defined(HALO_SERVER)
#define HALO_DELTA_IDENTITY 1
#endif
/* verified names in games (Delta Peer's identity capability: delta_peer.c,
delta_peer_game.c): the game list's builds, and the dedicated server, which
checks its players' proofs and keeps the revocation list; the consoles
leave it out with the rest of HALO_CONSOLE_NO_DELTA_IDENTITY */
#if defined(HALO_GAME_BROWSER) && !defined(HALO_CONSOLE_NO_DELTA_IDENTITY)
#define HALO_DELTA_PEER_IDENTITY 1
#endif

#if (defined(HALO_CONSOLE) || defined(HALO_SWITCH_CONSOLE) || defined(HALO_WIIU_CONSOLE) || \
	defined(HALO_XBOX_CONSOLE) || defined(HALO_XBOX360_CONSOLE)) && !defined(HALO_CONSOLE_NO_LOADING_PICTURE)
#define HALO_CONSOLE_NO_LOADING_PICTURE 1
#endif

/* the titles' second set, display.title_font = "cyborg" (the generated
hud_hires_assets.c's titles and menus' files, hud_hires.c, menu_files.c):
not on the consoles, HALO_CONSOLE_NO_TITLE_CHOICE */
#if (defined(HALO_CONSOLE) || defined(HALO_SWITCH_CONSOLE) || defined(HALO_WIIU_CONSOLE) || \
	defined(HALO_XBOX_CONSOLE) || defined(HALO_XBOX360_CONSOLE)) && !defined(HALO_CONSOLE_NO_TITLE_CHOICE)
#define HALO_CONSOLE_NO_TITLE_CHOICE 1
#endif
#if !defined(HALO_CONSOLE_NO_TITLE_CHOICE) && !defined(HALO_SERVER)
#define HALO_TITLE_CHOICE 1
#endif

/* a map's shaders made while its loading screen is up, from its shader key
list (port/linux/src/d3d8_shader_prep.inl, xgpu_shader_keys.c;
display.prepare_shaders): the OpenGL and bgfx renderers of the desktop
builds and Android. A console's renderer can take it later (the Switch's
has its own, HALO_CONSOLE_MAP_LOAD_HOOKS); HALO_CONSOLE_NO_SHADER_PREP
leaves it out */
#if (defined(HALO_CONSOLE) || defined(HALO_SWITCH_CONSOLE) || defined(HALO_WIIU_CONSOLE) || \
	defined(HALO_XBOX_CONSOLE) || defined(HALO_XBOX360_CONSOLE)) && !defined(HALO_CONSOLE_NO_SHADER_PREP)
#define HALO_CONSOLE_NO_SHADER_PREP 1
#endif
#if !defined(HALO_CONSOLE_NO_SHADER_PREP) && !defined(HALO_SERVER)
#define HALO_SHADER_PREP 1
#endif
/* (the game's calls into a renderer as a map loads: d3d8_gl_map_loading,
d3d8_gl_map_progress, d3d8_gl_map_waiting, d3d8_gl_map_loaded) */
#if defined(HALO_SHADER_PREP) || defined(HALO_CONSOLE_MAP_LOAD_HOOKS)
#define HALO_MAP_LOAD_HOOKS 1
#endif

#ifdef HALO_CONSOLE_WAIT_POLL
/* (defined by the port) */
void console_wait_poll(void);
void console_trace(const char *format, ...);
#endif

#endif
