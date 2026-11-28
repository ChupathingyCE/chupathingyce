/*
HALO_PORT_FEATURES.H

The desktop builds' extras that a console's build leaves out, in one place.
Included by halo_port_limits.h, which every build's prefix header reads.

A desktop build (Linux, macOS, Windows, Android) defines none of these and
compiles every extra, as before. A console's build defines HALO_CONSOLE
(its prefix header) to leave them all out, or any of the
HALO_CONSOLE_NO_* switches below on its own to leave out only that one.
The original Xbox's build doesn't define HALO_CONSOLE (its own switches are
in warthog-xbox's halo_xbox_features.h), so HALO_XBOX_CONSOLE leaves them
all out too: a switch added here reaches it without a change there.
The code stays in the tree either way: each switch only compiles it out.

Two extras already have switches of their own, which the desktop build
scripts set and a console's leaves unset:
HALO_CUSTOM_EDITION: Halo PC's Custom Edition and HaloMD maps (maps_ce,
maps_pc, maps_md), their loaders and conversions
HALO_GAME_BROWSER: the game list, text chat and the dedicated server
(configure.py --no-game-browser)

and ones a console's build sets to have an extra the desktop's have:
HALO_CONSOLE_GAME_REPORTS: the reports of the games it joins, sent to the
site (port/console/reports), as the desktop builds' game list sends them
HALO_CONSOLE_GAME_CLAIMS (with HALO_CONSOLE_GAME_REPORTS): the claim of its
own line in each online game, hosted or joined, for the Delta profile it is
linked to (port/console/reports/console_claims.h); the port builds
console_claims.c and gives its hooks
HALO_CONSOLE_CE_BITMAPS (with HALO_CUSTOM_EDITION): a Custom Edition map's
bitmaps made the console's as they are read, by the port's
platform_ce_bitmap_loaded (the desktop builds' renderer draws them as they are);
a HUD meter's bitmap it leaves in Halo PC's channels is said by the port's
platform_ce_meter_in_pc_channels, and the meter shader reads them there
(rasterizer_xbox_dynavobgeom.c)
HALO_CONSOLE_CE_SHARED_TAG_CACHE (with HALO_CUSTOM_EDITION): a console with no
room for a second tag cache lends its own to a Custom Edition map: the map
checked as it loads, the load's memory borrowed and given back, by the port's
platform_ce_* hooks (platform_ce_alias says whether it is in use); its
large bitmaps also start at their next level, the HUD's and the menus'
excepted (ce_bitmap_reduce.h, ce_repairs.c; console.ce_reduce_bitmaps)
HALO_CONSOLE_CE_MODEL_DATA_WINDOWS (with HALO_CUSTOM_EDITION): a Custom
Edition map's model data (a Halo 2 map's is 14 MB) read through two small
windows as its models are checked and made, not whole: a console's memory
may have no room for it on top of the map (port/linux/game/ce_map_checks.h)
HALO_CONSOLE_MAP_BROWSER: a console's own map browser (MAP BROWSER under
ONLINE GAMES, and the keyboard's search launch), its screen the port's
maps_screen_open
*/

#ifndef __HALO_PORT_FEATURES_H
#define __HALO_PORT_FEATURES_H

#if defined(HALO_CONSOLE) || defined(HALO_XBOX_CONSOLE)

/* the telnet script console (source/networking/telnet_console.c): a
console has no local machine to reach it from */
#ifndef HALO_CONSOLE_NO_TELNET
#define HALO_CONSOLE_NO_TELNET
#endif

/* the automated network tests' bots (port/linux/game/network_test.c):
run from a desktop's debug settings only */
#ifndef HALO_CONSOLE_NO_NETWORK_TEST
#define HALO_CONSOLE_NO_NETWORK_TEST
#endif

/* the tests' virtual gamepads (debug.test_gamepads, port/linux/src/
xinput_sdl.c): SDL's virtual joysticks, run from a desktop's debug settings
only */
#ifndef HALO_CONSOLE_NO_TEST_GAMEPADS
#define HALO_CONSOLE_NO_TEST_GAMEPADS
#endif

/* the high-res text and HUD (port/linux/src/text_hires.c,
port/linux/game/hud_hires_tags.c): a console draws the maps' own fonts and
bitmaps, as retail, without the atlas's texture memory */
#ifndef HALO_CONSOLE_NO_HIRES
#define HALO_CONSOLE_NO_HIRES
#endif

/* the high-res text's glyphs as distance fields (text_hires.c), which the
desktop and Android pixel shaders decode (nv2a_psh.c): a console's own
renderer has no such shader, so its atlas keeps the rasterized glyphs */
#ifndef HALO_CONSOLE_NO_TEXT_FIELDS
#define HALO_CONSOLE_NO_TEXT_FIELDS
#endif

/* the players' names above their heads (interface/hud.c): retail names
only the player under the reticle */
#ifndef HALO_CONSOLE_NO_PLAYER_NAMES
#define HALO_CONSOLE_NO_PLAYER_NAMES
#endif

/* the tag schema and validator (port/linux/game/tag_validate.c): about
170 KB of code and tables checking every map's tags as they load; a
console plays its own retail maps, unchecked, as retail did */
#ifndef HALO_CONSOLE_NO_TAG_VALIDATE
#define HALO_CONSOLE_NO_TAG_VALIDATE
#endif

/* Discord presence (port/linux/src/p2p_discord.c,
port/linux/game/discord_game.c): there is no Discord client on a console;
the switch also leaves out the discord.* settings and their help text */
#ifndef HALO_CONSOLE_NO_DISCORD
#define HALO_CONSOLE_NO_DISCORD
#endif

/* the updater (port/linux/src/updater.c): a console's builds are installed
by hand; the switch leaves out update.auto and debug.update_answer, so one
define brings them back when a console gets an updater */
#ifndef HALO_CONSOLE_NO_UPDATER
#define HALO_CONSOLE_NO_UPDATER
#endif

/* the object functions, change colors and camouflage drawn between ticks
(port/linux/game/render_interpolation.c): about 220 bytes for each object
slot, about 1.7 MB; a console draws them as sampled each tick, as retail */
#ifndef HALO_CONSOLE_NO_SMOOTH_FUNCTIONS
#define HALO_CONSOLE_NO_SMOOTH_FUNCTIONS
#endif

/* a stream of particles from an object made through the tick, on the
emitter as drawn (port/linux/game/render_interpolation.c): about 64 KB held
back; a console makes them all at the tick, as retail */
#ifndef HALO_CONSOLE_NO_PARTICLE_STREAMS
#define HALO_CONSOLE_NO_PARTICLE_STREAMS
#endif

/* the camera shake drawn between ticks (source/effects/player_effects.c):
a console shakes once a tick, as retail */
#ifndef HALO_CONSOLE_NO_SMOOTH_SHAKE
#define HALO_CONSOLE_NO_SMOOTH_SHAKE
#endif

/* the Controls screen (port/linux/game/controls_screen.c), the pause menu's
CONTROLS and the controller's custom layout (port/linux/src/controller_layout.c):
a console keeps its profile's layouts; the switch also leaves out
controls.controller_layout and display.pause_controls */
#ifndef HALO_CONSOLE_NO_CONTROLS
#define HALO_CONSOLE_NO_CONTROLS
#endif

/* Delta Profile in the game (port/linux/game/profile_screen.c): its screen,
its ways in from the menus, and players shown by their profile's name on
the scoreboard and in the lobby; a console that gets the game list keeps
Online Games' Link Profile panel, as before */
#ifndef HALO_CONSOLE_NO_DELTA_PROFILE
#define HALO_CONSOLE_NO_DELTA_PROFILE
#endif

/* the audio probe and the recording of real play's sound
(port/linux/game/audio_probe.c, debug.audio_probe and
debug.audio_capture_seconds): a developer's tool, in a console's build with
its developer tools (HALO_CONSOLE_DEBUG_TOOLS) only; the switch also leaves
out the two settings and their help text */
#if !defined(HALO_CONSOLE_DEBUG_TOOLS) && !defined(HALO_CONSOLE_NO_AUDIO_PROBE)
#define HALO_CONSOLE_NO_AUDIO_PROBE
#endif

/* the renderer's trace of a frame's draws (port/linux/src/d3d8_gl.c,
debug.gpu_trace_frame): a developer's tool, in a console's build with its
developer tools (HALO_CONSOLE_DEBUG_TOOLS) only */
#if !defined(HALO_CONSOLE_DEBUG_TOOLS) && !defined(HALO_CONSOLE_NO_GPU_TRACE)
#define HALO_CONSOLE_NO_GPU_TRACE
#endif

/* the debug camera flown by mouse and keyboard (source/camera/director.c,
halo_linux_debug_camera_input in port/linux/src/xinput_sdl.c): a console
has no mouse, and its controller flies the camera as before */
#ifndef HALO_CONSOLE_NO_DEBUG_CAMERA_KEYS
#define HALO_CONSOLE_NO_DEBUG_CAMERA_KEYS
#endif

#endif /* HALO_CONSOLE || HALO_XBOX_CONSOLE */

/* Forge, the layout editor (port/linux/game/forge.c, game.forge): its
hooks in the game's sources (the monitor's camera, the HUD, the layout's
spawn points and flags). Every console, the Switch and the Wii U too,
which set their own switch and not HALO_CONSOLE: none builds forge.c yet */
#if (defined(HALO_CONSOLE) || defined(HALO_SWITCH_CONSOLE) || defined(HALO_WIIU_CONSOLE) || \
	defined(HALO_XBOX_CONSOLE) || defined(HALO_XBOX360_CONSOLE)) && !defined(HALO_CONSOLE_NO_FORGE)
#define HALO_CONSOLE_NO_FORGE
#endif

/* voice chat (port/linux/src/voice.c, port/linux/game/voice_game.c): a
console has no microphone path or Opus yet; until it does, its player never
says it hears voices, so no host sends it any. Every console, the Switch
and the Wii U too, which set their own switch and not HALO_CONSOLE */
#if (defined(HALO_CONSOLE) || defined(HALO_SWITCH_CONSOLE) || defined(HALO_WIIU_CONSOLE) || \
	defined(HALO_XBOX_CONSOLE) || defined(HALO_XBOX360_CONSOLE)) && !defined(HALO_CONSOLE_NO_VOICE)
#define HALO_CONSOLE_NO_VOICE
#endif

/* voice chat over Delta Peer (docs/delta/peer.md, Voice): the desktop builds'
and Android's (HALO_GAME_BROWSER), as host and client, and the dedicated
server's as a relay alone; HALO_PORT_VOICE_AUDIO is the microphone, Opus
and playback, which a dedicated server (HALO_SERVER) leaves out: it relays
frames without decoding them */
#if defined(HALO_GAME_BROWSER) && !defined(HALO_CONSOLE_NO_VOICE)
#define HALO_PORT_VOICE
#if !defined(HALO_SERVER)
#define HALO_PORT_VOICE_AUDIO
#endif
#endif

/* gyro aiming (port/linux/src/gyro_aim.c, xinput_sdl.c; input.gyro_*): the
desktop builds' and Android's, and the Switch's, whose host reads its
controllers' sensors itself; no other console's, and not the dedicated
server's, which has no player at the machine. HALO_PORT_GYRO_SDL reads the
sensors through SDL 3 (SDL_SetGamepadSensorEnabled and its sensor events):
not the Switch (its host has SDL 2 and its own reading) nor Android's
32-bit guest build, whose SDL is host imports without the sensors */
#if !defined(HALO_SERVER) && (defined(HALO_SWITCH_CONSOLE) || \
	!(defined(HALO_CONSOLE) || defined(HALO_WIIU_CONSOLE) || defined(HALO_XBOX_CONSOLE) || \
	defined(HALO_XBOX360_CONSOLE) || defined(HALO_CONSOLE_GL_LAYER)))
#define HALO_PORT_GYRO_AIM
#if !defined(HALO_SWITCH_CONSOLE) && (!defined(HALO_ANDROID) || defined(__ANDROID__) || defined(HALO_IOS))
#define HALO_PORT_GYRO_SDL
#endif
#endif

/* text chat (port/linux/game/chat.c): the desktop builds' (HALO_GAME_BROWSER),
and a console's with Delta Peer as a client (HALO_CONSOLE_DELTA), the
client's part only, unless it sets HALO_CONSOLE_NO_CHAT */
#if defined(HALO_GAME_BROWSER) || (defined(HALO_CONSOLE_DELTA) && !defined(HALO_CONSOLE_NO_CHAT))
#define HALO_PORT_CHAT
#endif

/* the statistics recorder and the report of a joined game
(port/linux/game/game_stats.c): the desktop builds' (HALO_GAME_BROWSER,
whose game list sends it), and a console's that sends its own reports
(HALO_CONSOLE_GAME_REPORTS: port/console/reports/console_reports.h) */
#if defined(HALO_GAME_BROWSER) || defined(HALO_CONSOLE_GAME_REPORTS)
#define HALO_PORT_GAME_REPORTS
#endif

/* the game's busiest units read its most called accessors inline, with the
checks of the functions they call elsewhere: a tag block's element
(TAG_BLOCK_GET_ELEMENT, source/tag_files/tag_groups.h: the index in the
block, the block's address set, a Custom Edition map's tag cache bounds), a
tag (tag_get, the same header: the index a loaded tag's, its salt, its
group, its data loaded), a datum (datum_get, source/memory/data.h: the array
valid, the index in it, its salt) and an object (object_get_and_verify_type,
source/objects/objects.h: the datum's checks and the object's type). Only
what fails a check calls the function, which checks again and asserts,
logs or gives what it always gave. The debug decomp's accessors are calls
at each use, tens of thousands a tick; the retail build had them inline.
The units are tools/console_build.py's INLINE_ACCESSOR_SOURCES, which it
builds with HALO_CONSOLE_HOT_UNIT: they make 72 to 92% of each accessor's
calls counted on Beaver Creek (warthog-xbox #109), for 51 KB of code; the
inline forms in every unit would cost the original Xbox's 64 MB about
380 KB. Any console's release-style build (--console-no-debug-asserts: the
asserts the inline forms skip are compiled out of the functions there too,
and the memory checks datum_get keeps are in its inline form); the desktop
builds call the functions, as before. HALO_CONSOLE_ACCESSOR_STATE is such a
build's every unit: what the inline forms read (cache_files.c's count of
loaded tags) */
#if (defined(HALO_CONSOLE) || defined(HALO_SWITCH_CONSOLE) || defined(HALO_WIIU_CONSOLE) || \
	defined(HALO_XBOX_CONSOLE) || defined(HALO_XBOX360_CONSOLE) || defined(HALO_CONSOLE_GL_LAYER)) && \
	defined(HALO_CONSOLE_NO_DEBUG_ASSERTS) && !defined(HALO_CONSOLE_ACCESSOR_STATE)
#define HALO_CONSOLE_ACCESSOR_STATE
#endif
#if defined(HALO_CONSOLE_ACCESSOR_STATE) && defined(HALO_CONSOLE_HOT_UNIT) && !defined(HALO_CONSOLE_INLINE_ACCESSORS)
#define HALO_CONSOLE_INLINE_ACCESSORS
#endif

/* a filtered HUD meter's darkening faded over its edge, in two more combiner
stages (source/rasterizer/xbox/rasterizer_xbox_dynavobgeom.c): a console
draws its meters with retail's four stages, scaled or not. Every console's
build, whichever switches it sets on its own: the original Xbox doesn't set
HALO_CONSOLE, and its widescreen HUD drew the six stages */
#if (defined(HALO_CONSOLE) || defined(HALO_SWITCH_CONSOLE) || defined(HALO_WIIU_CONSOLE) || \
	defined(HALO_XBOX_CONSOLE) || defined(HALO_XBOX360_CONSOLE) || defined(HALO_CONSOLE_GL_LAYER)) && \
	!defined(HALO_CONSOLE_NO_SMOOTH_METER_EDGES)
#define HALO_CONSOLE_NO_SMOOTH_METER_EDGES
#endif

/* the settings the game reads each frame or each scoreboard row (the
particle limit, particles.c; the dynamic and specular light limits,
object_lights.c; doors blocking light, door_portals.c; the scoreboard's
icons and quit players, game_engine.c) read again only when the settings
change (config_changes), as the HUD's are. Each read takes a lock and
finds the setting by its name; one the build lacks (the original Xbox has
no particle limit and no doors blocking light) is compared with every
setting's name each time, then with the names it said it lacks. The same
values; any console's build (warthog-xbox #109). The desktop builds read
them each time, as before */
#if (defined(HALO_CONSOLE) || defined(HALO_SWITCH_CONSOLE) || defined(HALO_WIIU_CONSOLE) || \
	defined(HALO_XBOX_CONSOLE) || defined(HALO_XBOX360_CONSOLE) || defined(HALO_CONSOLE_GL_LAYER)) && \
	!defined(HALO_CONSOLE_SETTINGS_ON_CHANGE)
#define HALO_CONSOLE_SETTINGS_ON_CHANGE
#endif

/* the Light effects (display.light_effects, port/linux/game/plasma_lighting.c):
a desktop's lighting extra, on no console (any console's build, whichever
switches it sets on its own): its headers give empty hooks, and its sources
build to nothing (plasma_occlusion.c's fields alone are 130 KB) */
#if (defined(HALO_CONSOLE) || defined(HALO_SWITCH_CONSOLE) || defined(HALO_WIIU_CONSOLE) || \
	defined(HALO_XBOX_CONSOLE) || defined(HALO_XBOX360_CONSOLE) || defined(HALO_CONSOLE_GL_LAYER)) && \
	!defined(HALO_CONSOLE_NO_LIGHT_EFFECTS)
#define HALO_CONSOLE_NO_LIGHT_EFFECTS
#endif

/* an object's dark lights left out of the map's clusters (object_lights.c,
light_port_connect_lit): an attached light whose function is at 0 and whose
color there is black lights nothing and is not drawn, so it does not join
its clusters each tick until it lights again. The same picture for less of
the tick on a console's processor (warthog-xbox #109); the desktop builds
connect every light, as retail did (their Light effects read them). Any
console's build, whichever switches it sets on its own */
#if (defined(HALO_CONSOLE) || defined(HALO_SWITCH_CONSOLE) || defined(HALO_WIIU_CONSOLE) || \
	defined(HALO_XBOX_CONSOLE) || defined(HALO_XBOX360_CONSOLE) || defined(HALO_CONSOLE_GL_LAYER)) && \
	!defined(HALO_CONSOLE_SKIP_DARK_LIGHTS)
#define HALO_CONSOLE_SKIP_DARK_LIGHTS
#endif

/* each cluster's collideable objects that are not bipeds kept as a list of
their own (port/linux/game/object_bounds_cache.c), for the vectors that test
for no bipeds, with the changes to each cluster's list counted
(cluster_partitions.c): the desktop builds and Android, on no console. From
GulchCE by chestahh (granted 2026-11-26) */
#if !(defined(HALO_CONSOLE) || defined(HALO_SWITCH_CONSOLE) || defined(HALO_WIIU_CONSOLE) || \
	defined(HALO_XBOX_CONSOLE) || defined(HALO_XBOX360_CONSOLE) || defined(HALO_CONSOLE_GL_LAYER)) && \
	!defined(HALO_PORT_CLUSTER_OBJECT_LISTS)
#define HALO_PORT_CLUSTER_OBJECT_LISTS 1
#endif

/* a map's navigation for computer-controlled characters (port/linux/game/
ai_nav.c, port/linux/include/ai_nav.h): the desktop builds' and Android's,
on no console (its walkable copy and search memory). HALO_AI_NAV is 1 or 0;
at 0 its source builds to nothing */
#ifndef HALO_AI_NAV
#if defined(HALO_CONSOLE) || defined(HALO_SWITCH_CONSOLE) || defined(HALO_WIIU_CONSOLE) || \
	defined(HALO_XBOX_CONSOLE) || defined(HALO_XBOX360_CONSOLE) || defined(HALO_CONSOLE_GL_LAYER)
#define HALO_AI_NAV 0
#else
#define HALO_AI_NAV 1
#endif
#endif

/* Halo MCC maps (port/linux/game/mcc_*.c, docs/mcc_maps.md): the desktop
builds' and Android's, by a list of their systems, and on no console (any
console's build, whichever switches it sets on its own: the original Xbox
and the Switch don't set HALO_CONSOLE), nor in a Windows app package (UWP,
warthog-uwp). HALO_MCC is 1 or 0; at 0 the MCC headers turn the hooks the
shared code calls into no-ops, and the MCC sources build to nothing */
#ifndef HALO_MCC
#if (defined(__linux__) || defined(__APPLE__) || defined(_WIN32) || defined(__FreeBSD__) || \
	defined(HALO_ANDROID)) && \
	!(defined(HALO_CONSOLE) || defined(HALO_SWITCH_CONSOLE) || defined(HALO_WIIU_CONSOLE) || \
	defined(HALO_XBOX_CONSOLE) || defined(HALO_XBOX360_CONSOLE) || defined(HALO_CONSOLE_GL_LAYER)) && \
	!(defined(WINAPI_FAMILY) && WINAPI_FAMILY != 100 /* WINAPI_FAMILY_DESKTOP_APP */)
#define HALO_MCC 1
#else
#define HALO_MCC 0
#endif
#endif

/* ragdolls (port/linux/game/ragdolls.c, display.ragdolls): a dead body's
pose drawn from a rigid body ragdoll on Jolt Physics. The builds that
compile Jolt (HALO_JOLT: the desktop build scripts set it), and on no
console (any console's build, whichever switches it sets on its own) nor a
dedicated server, which draws nothing. A console's build may also set
HALO_CONSOLE_NO_RAGDOLLS */
#if defined(HALO_JOLT) && !defined(HALO_SERVER) && !defined(HALO_CONSOLE_NO_RAGDOLLS) && \
	!(defined(HALO_CONSOLE) || defined(HALO_SWITCH_CONSOLE) || defined(HALO_WIIU_CONSOLE) || \
	defined(HALO_XBOX_CONSOLE) || defined(HALO_XBOX360_CONSOLE) || defined(HALO_CONSOLE_GL_LAYER))
#define HALO_PORT_RAGDOLLS
#endif

/* Ray tracing and glow
(port/linux/src/modern_lighting.c), their MODERN LIGHTING pages in Video
Setup and the Guide's SETTINGS: the 64-bit desktop
builds, where modern_lighting.c is (a machine without ray queries, or not
on OpenGL, shows NOT SUPPORTED); not the Mac's or iOS's, whose OpenGL has
none of it (modern_lighting.c's stubs) */
#if defined(HALO_64BIT) && !defined(HALO_SERVER) && !defined(HALO_ANDROID) && !defined(__APPLE__) && \
	!(defined(HALO_CONSOLE) || defined(HALO_SWITCH_CONSOLE) || defined(HALO_WIIU_CONSOLE) || \
	defined(HALO_XBOX_CONSOLE) || defined(HALO_XBOX360_CONSOLE) || defined(HALO_CONSOLE_GL_LAYER))
#define HALO_PORT_MODERN_LIGHTING
#endif

/* a profile (blam.sav) written whole or not at all: to blam.sav.tmp, flushed
to the disk, read back, and renamed over the old one in one step, with the
last good one kept as blam.sav.bak and taken back when blam.sav is found
damaged (source/saved games/player_profile.c, saved_game_files.c;
port/linux/src/xbox_files.c's halo_file_write_whole). The desktop builds'
and Android's; a console's file layer is its own, and keeps the Xbox's
write in place until it has been checked there */
#if !(defined(HALO_CONSOLE) || defined(HALO_SWITCH_CONSOLE) || defined(HALO_WIIU_CONSOLE) || \
	defined(HALO_XBOX_CONSOLE) || defined(HALO_XBOX360_CONSOLE) || defined(HALO_CONSOLE_GL_LAYER))
#define HALO_PORT_SAFE_SAVES
#endif

#endif /* __HALO_PORT_FEATURES_H */
