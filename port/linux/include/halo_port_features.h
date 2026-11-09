/*
HALO_PORT_FEATURES.H

The desktop builds' extras that a console's build leaves out, in one place.
Included by halo_port_limits.h, which every build's prefix header reads.

A desktop build (Linux, macOS, Windows, Android) defines none of these and
compiles every extra, as before. A console's build defines HALO_CONSOLE
(its prefix header) to leave them all out, or any of the
HALO_CONSOLE_NO_* switches below on its own to leave out only that one.
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
platform_ce_bitmap_loaded (the desktop builds' renderer draws them as they are)
HALO_CONSOLE_CE_SHARED_TAG_CACHE (with HALO_CUSTOM_EDITION): a console with no
room for a second tag cache lends its own to a Custom Edition map: the map
checked as it loads, the load's memory borrowed and given back, by the port's
platform_ce_* hooks (platform_ce_alias says whether it is in use)
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

#ifdef HALO_CONSOLE

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

#endif /* HALO_CONSOLE */

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

#endif /* __HALO_PORT_FEATURES_H */
