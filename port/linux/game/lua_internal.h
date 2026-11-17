/*
LUA_INTERNAL.H

What the Lua units share (lua_scripting.c, lua_chimera_api.c): a loaded
script and its callbacks, and the log.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef LUA_INTERNAL_H
#define LUA_INTERNAL_H

#ifdef HALO_LUA

#include "lua.h"
#include "lauxlib.h"

/* the Chimera Lua API version these scripts are run as (Chimera's
CHIMERA_LUA_VERSION): a script asking for a newer one is warned */
#define LUA_CHIMERA_API_VERSION 2.056

#define LUA_MAXIMUM_SCRIPTS 32
#define LUA_SCRIPT_NAME_LENGTH 64
#define LUA_FUNCTION_NAME_LENGTH 64
#define LUA_PATH_LENGTH 256

enum lua_callback
{
	_lua_callback_command,
	_lua_callback_frame,
	_lua_callback_preframe,
	_lua_callback_map_load,
	_lua_callback_map_preload,
	_lua_callback_precamera,
	_lua_callback_rcon_message,
	_lua_callback_spawn,
	_lua_callback_prespawn,
	_lua_callback_tick,
	_lua_callback_pretick,
	_lua_callback_unload,
	number_of_lua_callbacks
};

/* Chimera's priorities, called in this order */
enum lua_callback_priority
{
	_lua_priority_before,
	_lua_priority_default,
	_lua_priority_after,
	_lua_priority_final,
	number_of_lua_priorities
};

struct lua_script_callback
{
	char function[LUA_FUNCTION_NAME_LENGTH];
	enum lua_callback_priority priority;
};

enum lua_script_kind
{
	/* lua/scripts/global, all the time */
	_lua_script_global,
	/* lua/<pack>/scripts/global, while that pack's menus are in use */
	_lua_script_pack,
	/* lua/scripts/map/<map>.lua (or the pack's), while that map is loaded */
	_lua_script_map
};

struct lua_script
{
	lua_State *state;
	char name[LUA_SCRIPT_NAME_LENGTH];
	enum lua_script_kind kind;
	/* Chimera's map sandbox (an embedded script's) */
	boolean sandbox;
	boolean loaded;
	/* the clua_version the script asked for, else the API's */
	double version;
	/* d:\lua or d:\lua\<pack>: its modules and data folders are under it */
	char root[LUA_PATH_LENGTH];
	struct lua_script_callback callbacks[number_of_lua_callbacks];
	/* bytes its Lua state holds, against LUA_SCRIPT_MEMORY_LIMIT */
	unsigned int memory;
};

/* ---------- lua_scripting.c */

/* the script of a Lua state (every state is one of ours) */
struct lua_script *lua_script_from_state(lua_State *state);
/* a line in debug.txt, "lua: ..." */
void lua_log(char const *format, ...) __attribute__((format(printf, 1, 2)));
/* the same, only the first time this key is given (a function name, an
address as text): what a script asked for that the game does not do */
void lua_log_once(char const *key, char const *format, ...) __attribute__((format(printf, 2, 3)));
/* a Lua error on the top of the state's stack, logged once per distinct
message, and popped */
void lua_log_error(lua_State *state, char const *where);
/* a line in the console (and debug.txt), white unless given a color */
void lua_console_line(real_argb_color const *color, char const *text);

/* ---------- lua_chimera_api.c */

/* registers the API's functions and variables in a new script's state */
void lua_chimera_api_register(lua_State *state);
/* the variables that change (map, local_player_index, server_type,
gametype), refreshed before map load and each pretick */
void lua_chimera_api_refresh(lua_State *state);
/* runs a script's chunk: the API's version check; FALSE (logged) when it
failed */
boolean lua_chimera_api_run(struct lua_script *script, char const *chunk, unsigned int size);
/* calls every script's callback of this kind, in Chimera's priority order */
void lua_chimera_api_call(enum lua_callback callback);
/* the "command" and "rcon message" callbacks: FALSE when one blocked it */
boolean lua_chimera_api_call_allow(enum lua_callback callback, char const *text);
/* the loaded scripts (lua_scripting.c) */
extern struct lua_script lua_scripts[LUA_MAXIMUM_SCRIPTS];
extern int lua_script_count;

#endif

#endif
