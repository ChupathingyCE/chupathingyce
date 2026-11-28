/*
ARK_MODE.H

Project Ark as an early mode (port/linux/game/ark_mode.c): whether it is on,
and where the player's own files for it are. The mode is picked as the packs'
menus are (game.pack_menus = "ark", the setup screen's Play list, --mode ark),
but it has no ui.map: the menus stay the game's own.

A build has the mode only with HALO_ARK (configure.py --ark, on for the
32-bit Linux desktop build); without it every function here is constant and
the engine's calls into the mode compile away.
*/

#ifndef ARK_MODE_H
#define ARK_MODE_H

/* the mode's id: game.pack_menus = "ark", --mode ark */
#define ARK_MODE_PACK_ID "ark"

/* the folder beside maps for a copy of the player's own files */
#define ARK_MODE_FOLDER "maps_ark"

/* the folder's Xbox path, with its separator: the mode's map files are read
from here */
#define ARK_MODE_MAPS_PATH "d:\\" ARK_MODE_FOLDER "\\"

/* whether the mode is on: picked, in this build, with the files here */
boolean ark_mode_active(void);

/* a line in debug.txt, in the mode only */
void ark_mode_log(char const *format, ...);

/* whether a game on another machine is not joined: every one in the mode,
which plays offline and split screen for now (the player is told) */
boolean ark_mode_join_refused(char const *map_name);

/* whether a machine from elsewhere is kept out of this one's game: every
one in the mode */
boolean ark_mode_remote_machine_refused(void);

#endif
