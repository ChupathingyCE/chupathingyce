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

/* the object functions drawn between ticks
(port/linux/game/render_interpolation.c): 48 bytes for each object slot,
about 384 KB; a console draws them as sampled each tick, as retail */
#ifndef HALO_CONSOLE_NO_SMOOTH_FUNCTIONS
#define HALO_CONSOLE_NO_SMOOTH_FUNCTIONS
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

#endif /* HALO_CONSOLE */

/* text chat (port/linux/game/chat.c): the desktop builds' (HALO_GAME_BROWSER),
and a console's with Delta Peer as a client (HALO_CONSOLE_DELTA), the
client's part only, unless it sets HALO_CONSOLE_NO_CHAT */
#if defined(HALO_GAME_BROWSER) || (defined(HALO_CONSOLE_DELTA) && !defined(HALO_CONSOLE_NO_CHAT))
#define HALO_PORT_CHAT
#endif

#endif /* __HALO_PORT_FEATURES_H */
