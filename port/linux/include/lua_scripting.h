/*
LUA_SCRIPTING.H

Mods' Lua scripts written for Chimera's Lua API, run by the game
(port/linux/game/lua_scripting.c): the game's calls into them. A build
without Lua (HALO_LUA: the desktop builds define it) compiles each call to
nothing. Nothing runs unless lua.scripts is on.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef LUA_SCRIPTING_H
#define LUA_SCRIPTING_H

#ifdef HALO_LUA

/* a map's game started (game.c's game_initialize_for_new_map, at its end):
the map's scripts load, the pack's when its pack changed, then each
script's "map load" */
void lua_scripting_map_load(void);
/* the map's game ends (game_dispose_from_old_map): the map's scripts'
"unload", and they are closed */
void lua_scripting_map_unload(void);
/* a tick of the game (game_tick): "pretick" at its start, "tick" at its end */
void lua_scripting_pretick(void);
void lua_scripting_tick(void);
/* a frame of the game (game_frame): "preframe" at its start, "frame" at its
end */
void lua_scripting_preframe(void);
void lua_scripting_frame(void);
/* a console command (console.c, before it runs): FALSE when a script's
"command" blocked it */
boolean lua_scripting_command(char const *command);

#else

static __inline void lua_scripting_map_load(void) { }
static __inline void lua_scripting_map_unload(void) { }
static __inline void lua_scripting_pretick(void) { }
static __inline void lua_scripting_tick(void) { }
static __inline void lua_scripting_preframe(void) { }
static __inline void lua_scripting_frame(void) { }
static __inline boolean lua_scripting_command(char const *command) { (void)command; return TRUE; }

#endif

#endif
