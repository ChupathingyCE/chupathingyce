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

/* the tag schema and validator (port/linux/game/tag_validate.c): a large
table, for maps from elsewhere; a console loads its own maps */
#ifndef HALO_CONSOLE_NO_TAG_VALIDATE
#define HALO_CONSOLE_NO_TAG_VALIDATE
#endif

#endif /* HALO_CONSOLE */

#endif /* __HALO_PORT_FEATURES_H */
