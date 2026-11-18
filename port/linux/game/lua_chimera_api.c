/*
LUA_CHIMERA_API.C

Chimera's Lua API, for mods' scripts written for it (lua_scripting.c loads
them): the functions a script calls, the variables it reads and the
callbacks it sets, with Chimera's names, arguments, results and order, so
that a script behaves as it does under Chimera.

Adapted from Chimera (github.com/SnowyMouse/chimera, by Snowy Mouse, its
current developers Aerocatia and Cambidd, and its contributors), its
src/chimera/lua: lua_callback.cpp (set_callback, the callbacks and their
priorities, the command callback's block), lua_game.cpp (console_out,
execute_script, get_tag, get_object, get_player, get_dynamic_player,
spawn_object, delete_object, ticks, tick_rate, get_global, set_global,
set_timer, stop_timer, and the timers' checks from lua_callback.cpp and
lua_script.cpp), lua_io.cpp (read_* and write_*,
read_bit, write_bit, read_string8, write_string8), lua_script.cpp (running
a script and its version check) and lua_variables.cpp (the script's
variables), each GPL-3.0-only. Rewritten in C for this game: memory goes
through Twiddlings (lua_twiddlings.c), never to an address as given, and
what the game does not do yet is logged once and answered with nil.

SPDX-FileCopyrightText: Snowy Mouse and the Chimera contributors
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-only
*/

#ifdef HALO_LUA

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "cache/cache_files.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "hs/hs.h"
#include "main/console.h"
#include "memory/data.h"
#include "objects/objects.h"

#include "lua_internal.h"
#include "lua_opensauce.h"
#include "lua_twiddlings.h"

#include <stdio.h>
#include <string.h>

/* ---------- constants */

/* (no Chimera build: scripts that compare it see none) */
#define LUA_CHIMERA_BUILD 0
#define LUA_CHIMERA_FULL_BUILD "Chupathingy"

/* get_tag gave 0 for a tag that is not there until Chimera's API 2.050
(the API it took from its -5xx builds, which scripts asking for 2.042, as
SPV3's do, were written for); nil from then on */
#define LUA_CHIMERA_GET_TAG_NIL_VERSION 2.050

/* Chimera's minimum timer interval (milliseconds) and tick rate
(lua_game.cpp) */
#define LUA_MINIMUM_TIMER_INTERVAL 0.1
#define LUA_MINIMUM_TICK_RATE 0.01
/* a timer's calls in one check at most: a pause (a map loading) owes a
fast timer thousands, which are let go */
#define LUA_MAXIMUM_TIMER_CALLS 100
/* Chimera's timers' arguments as they were before its API 2.03: text */
#define LUA_TIMER_TYPED_ARGUMENTS_VERSION 2.03

/* ---------- globals */

extern struct game_variant game_variant_global;

/* the callbacks' names, as set_callback takes them (an underscore is a
space: "map_load" is "map load") */
static char const *const lua_callback_names[number_of_lua_callbacks] =
{
	"command",
	"frame",
	"preframe",
	"map load",
	"map preload",
	"precamera",
	"rcon message",
	"spawn",
	"prespawn",
	"tick",
	"pretick",
	"unload",
};

/* the ones the game calls; the rest are taken and logged once */
static boolean const lua_callback_called[number_of_lua_callbacks] =
{
	TRUE, TRUE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, TRUE,
};

/* ---------- private code */

static int lua_pcall_logged(
	lua_State *state,
	int arguments,
	int results,
	char const *where)
{
	int result = lua_pcall(state, arguments, results, 0);

	if (result != LUA_OK)
		lua_log_error(state, where);
	return result;
}

static int lua_wrong_arguments(
	lua_State *state,
	char const *function)
{
	return luaL_error(state, "wrong number of arguments in %s", function);
}

/* a function of Chimera's API the game does not have yet: logged once, nil */
static int lua_unsupported(
	lua_State *state)
{
	char const *name = lua_tostring(state, lua_upvalueindex(1));
	char key[96];

	snprintf(key, sizeof(key), "function %s", name);
	lua_log_once(key, "%s: %s is not supported yet: it does nothing", lua_script_from_state(state)->name, name);
	lua_pushnil(state);
	return 1;
}

static void lua_register_unsupported(
	lua_State *state,
	char const *name)
{
	lua_pushstring(state, name);
	lua_pushcclosure(state, lua_unsupported, 1);
	lua_setglobal(state, name);
}

/* set_callback's name, with an underscore taken as a space */
static enum lua_callback lua_callback_from_name(
	char const *name)
{
	short callback;

	for (callback = 0; callback < number_of_lua_callbacks; callback++)
	{
		char const *expected = lua_callback_names[callback];
		short index;

		for (index = 0; expected[index] && name[index]; index++)
		{
			char character = name[index] == '_' ? ' ' : name[index];

			if (character != expected[index])
				break;
		}
		if (!expected[index] && !name[index])
			return (enum lua_callback)callback;
	}
	return number_of_lua_callbacks;
}

/* set_callback(callback, [function], [priority]): the function's name, ""
or none to clear it (lua_callback.cpp) */
static int lua_set_callback(
	lua_State *state)
{
	int arguments = lua_gettop(state);
	char const *callback_name;
	char const *function_name = "";
	enum lua_callback callback;
	enum lua_callback_priority priority = _lua_priority_default;
	struct lua_script *script = lua_script_from_state(state);

	if (arguments < 1 || arguments > 3)
		return lua_wrong_arguments(state, "set_callback");
	callback_name = luaL_checkstring(state, 1);
	if (arguments >= 2 && !lua_isnil(state, 2))
		function_name = luaL_checkstring(state, 2);
	if (arguments == 3)
	{
		char const *text = luaL_checkstring(state, 3);

		if (!strcmp(text, "before"))
			priority = _lua_priority_before;
		else if (!strcmp(text, "after"))
			priority = _lua_priority_after;
		else if (!strcmp(text, "final"))
			priority = _lua_priority_final;
	}
	callback = lua_callback_from_name(callback_name);
	if (callback == number_of_lua_callbacks)
		return luaL_error(state, "invalid callback %s", callback_name);
	if (function_name[0] && !lua_callback_called[callback])
	{
		char key[96];

		snprintf(key, sizeof(key), "callback %s", lua_callback_names[callback]);
		lua_log_once(key, "%s: the \"%s\" callback is not called yet", script->name, lua_callback_names[callback]);
	}
	csstrncpy(script->callbacks[callback].function, function_name, LUA_FUNCTION_NAME_LENGTH - 1);
	script->callbacks[callback].function[LUA_FUNCTION_NAME_LENGTH - 1] = 0;
	script->callbacks[callback].priority = priority;
	return 0;
}

/* a value as console_out and hud_message print it */
static char const *lua_message_argument(
	lua_State *state,
	int argument)
{
	if (lua_isboolean(state, argument))
		return lua_toboolean(state, argument) ? "true" : "false";
	if (lua_isnil(state, argument))
		return "(nil)";
	return luaL_checkstring(state, argument);
}

/* console_out(message, [[alpha,] red, green, blue]) */
static int lua_console_out(
	lua_State *state)
{
	int arguments = lua_gettop(state);
	real_argb_color color = { 1.f, 1.f, 1.f, 1.f };
	char const *message;
	int offset = 0;

	if (arguments != 1 && arguments != 4 && arguments != 5)
		return lua_wrong_arguments(state, "console_out");
	message = lua_message_argument(state, 1);
	if (arguments >= 4)
	{
		if (arguments == 5)
		{
			color.alpha = (real)luaL_checknumber(state, 2);
			offset = 1;
		}
		color.red = (real)luaL_checknumber(state, offset + 2);
		color.green = (real)luaL_checknumber(state, offset + 3);
		color.blue = (real)luaL_checknumber(state, offset + 4);
	}
	lua_console_line(&color, message);
	return 0;
}

static int lua_console_is_open(
	lua_State *state)
{
	lua_pushboolean(state, console_is_active());
	return 1;
}

/* execute_script(expression): one of the game's script functions or
globals, as the console runs it; a function the game does not have is
logged once and skipped (OpenSauce's pp_* among them) */
static int lua_execute_script(
	lua_State *state)
{
	int arguments = lua_gettop(state);
	char const *expression;
	char word[128];
	char const *cursor;
	short length = 0;
	boolean was_running;

	if (arguments != 1 && arguments != 4 && arguments != 5)
		return lua_wrong_arguments(state, "execute_script");
	expression = luaL_checkstring(state, 1);
	for (cursor = expression; *cursor == ' ' || *cursor == '\t' || *cursor == '('; cursor++)
		;
	while (cursor[length] && cursor[length] != ' ' && cursor[length] != '\t' && cursor[length] != ')' &&
		length + 1 < (short)sizeof(word))
	{
		word[length] = cursor[length];
		length++;
	}
	word[length] = 0;
	if (!word[0])
		return 0;
	/* (the functions only execute_script may name, Halo PC's and on its maps
	OpenSauce's, are found while it runs: lua_scripting.h) */
	was_running = lua_execute_script_running;
	lua_execute_script_running = TRUE;
	if (hs_find_function_by_name(word) == NONE && hs_find_global_by_name(word) == NONE)
	{
		char key[160];

		lua_execute_script_running = was_running;
		/* (OpenSauce's, on an OpenSauce map: its globals kept, the rest
		logged as OpenSauce's) */
		if (lua_opensauce_script_word(lua_script_from_state(state)->name, word, expression))
			return 0;

		snprintf(key, sizeof(key), "script %s", word);
		lua_log_once(key, "%s: execute_script: the game has no %s: skipped", lua_script_from_state(state)->name,
			word);
		return 0;
	}
	hs_compile_and_evaluate(expression);
	lua_execute_script_running = was_running;
	return 0;
}

/* get_global(name): a map's script global (lua_game.cpp, globals.cpp's
read_global): a boolean, a number, or an error for none or one of a type it
does not give (text, a script) */
static int lua_get_global(
	lua_State *state)
{
	long *value;
	short type;

	if (lua_gettop(state) != 1)
		return lua_wrong_arguments(state, "get_global");
	type = hs_runtime_scenario_global(luaL_checkstring(state, 1), &value);
	switch (type)
	{
	case _hs_unparsed:
	case _hs_special_form:
	case _hs_function_name:
	case _hs_passthrough:
	case _hs_type_void:
	case _hs_type_string:
	case _hs_type_script:
		return luaL_error(state, "global not found");
	case _hs_type_boolean:
		lua_pushboolean(state, (byte)*value != 0);
		break;
	case _hs_type_real:
	{
		real number;

		memcpy(&number, value, sizeof(number));
		lua_pushnumber(state, (lua_Number)number);
		break;
	}
	case _hs_type_short_integer:
		lua_pushinteger(state, (short)*value);
		break;
	default:
		/* (a long, or a handle: an object, a unit, an AI, as Chimera gives
		them) */
		lua_pushinteger(state, (int)*value);
		break;
	}
	return 1;
}

/* set_global(name, value) (lua_game.cpp): a boolean takes a boolean or a
number; the rest a number */
static int lua_set_global(
	lua_State *state)
{
	char const *name;
	long *value;
	short type;

	if (lua_gettop(state) != 2)
		return lua_wrong_arguments(state, "set_global");
	name = luaL_checkstring(state, 1);
	type = hs_runtime_scenario_global(name, &value);
	switch (type)
	{
	case _hs_unparsed:
	case _hs_special_form:
	case _hs_function_name:
	case _hs_passthrough:
	case _hs_type_void:
	case _hs_type_string:
	case _hs_type_script:
		return luaL_error(state, "global not found");
	case _hs_type_boolean:
		if (lua_isboolean(state, 2))
			*value = lua_toboolean(state, 2) ? TRUE : FALSE;
		else if (lua_isnumber(state, 2))
			*value = lua_tointeger(state, 2) != 0 ? TRUE : FALSE;
		else
			return luaL_error(state, "expected number or boolean for argument #%d", 2);
		break;
	case _hs_type_real:
	{
		real number;

		if (!lua_isnumber(state, 2))
			return luaL_error(state, "expected number for argument #%d", 2);
		number = (real)lua_tonumber(state, 2);
		memcpy(value, &number, sizeof(number));
		break;
	}
	case _hs_type_short_integer:
		if (!lua_isnumber(state, 2))
			return luaL_error(state, "expected number for argument #%d", 2);
		*value = (short)lua_tointeger(state, 2);
		break;
	case _hs_type_long_integer:
		if (!lua_isnumber(state, 2))
			return luaL_error(state, "expected number for argument #%d", 2);
		*value = (long)(int)lua_tointeger(state, 2);
		break;
	default:
	{
		/* (a handle, which the game's own lists count: an object list's
		references would go wrong, so a script reads them alone) */
		char key[160];

		snprintf(key, sizeof(key), "set_global %.100s", name);
		lua_log_once(key, "%s: set_global: %.100s holds a handle, which scripts may read here but not set",
			lua_script_from_state(state)->name, name);
		break;
	}
	}
	return 0;
}

/* a timer by its id, or NULL */
static struct lua_timer *lua_timer_find(
	struct lua_script *script,
	long id)
{
	short index;

	for (index = 0; index < script->timer_count; index++)
	{
		if (script->timers[index].id == id)
			return &script->timers[index];
	}
	return NULL;
}

static void lua_timer_remove(
	struct lua_script *script,
	long id)
{
	short index;

	for (index = 0; index < script->timer_count; index++)
	{
		if (script->timers[index].id == id)
		{
			luaL_unref(script->state, LUA_REGISTRYINDEX, script->timers[index].arguments);
			script->timer_count--;
			memmove(&script->timers[index], &script->timers[index + 1],
				(size_t)(script->timer_count - index) * sizeof(script->timers[0]));
			return;
		}
	}
}

/* set_timer(interval, function, ...): its id; the function is called every
interval milliseconds with the arguments given, until it returns false or
stop_timer stops it (lua_game.cpp, lua_script.cpp's check_argument) */
static int lua_set_timer(
	lua_State *state)
{
	int arguments = lua_gettop(state);
	struct lua_script *script = lua_script_from_state(state);
	struct lua_timer *timer;
	double interval;
	char const *function;
	int argument;

	if (arguments < 2)
		return lua_wrong_arguments(state, "set_timer");
	interval = luaL_checknumber(state, 1);
	if (interval < LUA_MINIMUM_TIMER_INTERVAL)
		return luaL_error(state, "interval must be at least 0.1 ms");
	function = luaL_checkstring(state, 2);
	if (script->timer_count >= LUA_MAXIMUM_TIMERS)
		return luaL_error(state, "too many timers (%d)", LUA_MAXIMUM_TIMERS);
	/* (its arguments in a table: booleans, text and nil, and numbers as text
	as Chimera keeps them, or all as text for a script older than 2.03) */
	lua_createtable(state, arguments - 2, 1);
	for (argument = 3; argument <= arguments; argument++)
	{
		if (script->version >= LUA_TIMER_TYPED_ARGUMENTS_VERSION && lua_isboolean(state, argument))
		{
			lua_pushboolean(state, lua_toboolean(state, argument));
		}
		else if (lua_isstring(state, argument))
		{
			lua_pushvalue(state, argument);
			lua_tostring(state, -1);
		}
		else if (script->version >= LUA_TIMER_TYPED_ARGUMENTS_VERSION && lua_isnil(state, argument))
		{
			lua_pushnil(state);
		}
		else
		{
			return luaL_error(state, "invalid timer argument");
		}
		lua_rawseti(state, -2, argument - 2);
	}
	lua_pushinteger(state, arguments - 2);
	lua_setfield(state, -2, "n");
	timer = &script->timers[script->timer_count++];
	memset(timer, 0, sizeof(*timer));
	timer->arguments = luaL_ref(state, LUA_REGISTRYINDEX);
	timer->id = script->next_timer_id++;
	timer->interval = interval;
	csstrncpy(timer->function, function, LUA_FUNCTION_NAME_LENGTH - 1);
	timer->function[LUA_FUNCTION_NAME_LENGTH - 1] = 0;
	lua_pushinteger(state, timer->id);
	return 1;
}

/* stop_timer(id) */
static int lua_stop_timer(
	lua_State *state)
{
	struct lua_script *script = lua_script_from_state(state);
	long id;

	if (lua_gettop(state) != 1)
		return lua_wrong_arguments(state, "stop_timer");
	id = (long)luaL_checkinteger(state, 1);
	if (!lua_timer_find(script, id))
		return luaL_error(state, "timer does not exist");
	lua_timer_remove(script, id);
	return 0;
}

/* the API version a script asks for: its clua_version as it stands (a
script that runs its own code as it loads has set it by then; Chimera reads
it once the script has run), else the API's */
static double lua_api_version(
	lua_State *state)
{
	double version = lua_script_from_state(state)->version;

	if (lua_getglobal(state, "clua_version") == LUA_TNUMBER)
		version = lua_tonumber(state, -1);
	lua_pop(state, 1);
	return version;
}

/* get_tag(id) or get_tag(group, path): the tag's instance in the tag
index, as an address, or nil (0 for a script asking for an API before
2.050) */
static int lua_get_tag(
	lua_State *state)
{
	int arguments = lua_gettop(state);
	unsigned int address = 0;

	if (arguments == 1)
	{
		lua_Integer tag_index = luaL_checkinteger(state, 1);

		if ((unsigned int)tag_index != 0xFFFFFFFFu)
			address = twiddlings_tag_instance_address((long)(unsigned int)tag_index);
	}
	else if (arguments == 2)
	{
		char const *group = luaL_checkstring(state, 1);
		char const *path = luaL_checkstring(state, 2);
		unsigned long group_tag = 0;
		short index;

		/* (a group by its four characters; Chimera also takes its names
		for the groups, not yet here) */
		if (strlen(group) > 4)
		{
			char key[96];

			snprintf(key, sizeof(key), "group name %s", group);
			lua_log_once(key, "%s: get_tag: groups by name (%s) are not supported yet",
				lua_script_from_state(state)->name, group);
		}
		else
		{
			for (index = 0; index < 4; index++)
				group_tag = (group_tag << 8) | (unsigned char)(group[index] ? group[index] : ' ');
			address = twiddlings_tag_instance_address_by_name(group_tag, path);
		}
	}
	else
	{
		return lua_wrong_arguments(state, "get_tag");
	}
	if (address)
		lua_pushinteger(state, address);
	else if (lua_api_version(state) < LUA_CHIMERA_GET_TAG_NIL_VERSION)
		lua_pushinteger(state, 0);
	else
		lua_pushnil(state);
	return 1;
}

static void lua_push_address(
	lua_State *state,
	unsigned int address)
{
	if (address)
		lua_pushinteger(state, address);
	else
		lua_pushnil(state);
}

/* the local player (Chimera's client player), as a handle */
static long lua_local_player(
	void)
{
	if (!game_time_initialized() || !local_player_exists(0))
		return NONE;
	return local_player_get_player_index(0);
}

/* get_object(id): the object, by its handle or its index alone, as an
address (Twiddlings' stand-in for it), or nil */
static int lua_get_object(
	lua_State *state)
{
	lua_Integer id;

	if (lua_gettop(state) != 1)
		return lua_wrong_arguments(state, "get_object");
	id = luaL_checkinteger(state, 1);
	lua_push_address(state, twiddlings_object_address((long)(unsigned int)id));
	return 1;
}

/* get_player([index]): the local player, or the one at that place in the
player list, as an address, or nil. A nil index is the local player, as the
Chimera SPV3 was written for took it (its players.lua passes
local_player_index, nil at the menus) */
static int lua_get_player(
	lua_State *state)
{
	int arguments = lua_gettop(state);
	long player_index;

	if (arguments > 1)
		return lua_wrong_arguments(state, "get_player");
	if (arguments == 0 || lua_isnil(state, 1))
	{
		player_index = lua_local_player();
		lua_push_address(state, player_index == NONE ? 0 : twiddlings_player_address(player_index));
	}
	else
	{
		lua_push_address(state, twiddlings_player_slot_address((long)luaL_checkinteger(state, 1)));
	}
	return 1;
}

/* get_dynamic_player([index]): that player's object (its unit), or nil */
static int lua_get_dynamic_player(
	lua_State *state)
{
	int arguments = lua_gettop(state);
	long player_index;
	unsigned int address = 0;

	if (arguments > 1)
		return lua_wrong_arguments(state, "get_dynamic_player");
	if (arguments == 0 || lua_isnil(state, 1))
		player_index = lua_local_player();
	else
		player_index = twiddlings_player_by_list_index((long)luaL_checkinteger(state, 1));
	if (player_index != NONE)
	{
		struct player_datum *player = (struct player_datum *)datum_try_and_get(player_data, player_index);

		if (player && player->unit_index != NONE)
			address = twiddlings_object_address(player->unit_index);
	}
	lua_push_address(state, address);
	return 1;
}

/* the object groups spawn_object takes */
static unsigned long const lua_object_groups[] =
{
	'bipd', 'vehi', 'weap', 'eqip', 'garb', 'proj', 'scen', 'mach', 'ctrl', 'lifi', 'plac', 'ssce',
};

/* spawn_object(group, path, x, y, z) or spawn_object(tag id, x, y, z): a
new object of that tag there, its handle (lua_game.cpp). A tag that is none
or not an object's is an error, as in Chimera */
static int lua_spawn_object(
	lua_State *state)
{
	int arguments = lua_gettop(state);
	int first;
	unsigned int instance = 0, group = 0, tag_index = 0xFFFFFFFFu;
	struct object_placement_data data;
	long object_index;
	short index;

	if (arguments != 4 && arguments != 5)
		return lua_wrong_arguments(state, "spawn_object");
	if (lua_isnumber(state, 1))
	{
		instance = twiddlings_tag_instance_address((long)(unsigned int)luaL_checkinteger(state, 1));
		first = 2;
	}
	else
	{
		char const *group_name = luaL_checkstring(state, 1);
		char const *path = luaL_checkstring(state, 2);
		unsigned long group_tag = 0;

		for (index = 0; index < 4; index++)
			group_tag = (group_tag << 8) | (unsigned char)(index < (short)strlen(group_name) ? group_name[index] : ' ');
		instance = twiddlings_tag_instance_address_by_name(group_tag, path);
		first = 3;
	}
	if (instance)
	{
		twiddlings_read(instance, &group, sizeof(group));
		twiddlings_read(instance + 0xC, &tag_index, sizeof(tag_index));
	}
	for (index = 0; index < (short)NUMBEROF(lua_object_groups); index++)
	{
		if (group == (unsigned int)lua_object_groups[index])
			break;
	}
	if (!instance || tag_index == 0xFFFFFFFFu || index == (short)NUMBEROF(lua_object_groups))
		return luaL_error(state, "invalid tag in spawn_object");
	if (!game_time_initialized())
	{
		lua_pushinteger(state, 0xFFFFFFFFu);
		return 1;
	}
	object_placement_data_new(&data, (long)tag_index, NONE);
	data.position.x = (real)luaL_checknumber(state, first);
	data.position.y = (real)luaL_checknumber(state, first + 1);
	data.position.z = (real)luaL_checknumber(state, first + 2);
	object_index = object_new(&data);
	lua_pushinteger(state, (lua_Integer)(unsigned int)object_index);
	return 1;
}

/* delete_object(id) */
static int lua_delete_object(
	lua_State *state)
{
	long object_index;

	if (lua_gettop(state) != 1)
		return lua_wrong_arguments(state, "delete_object");
	object_index = (long)(unsigned int)luaL_checkinteger(state, 1);
	if (object_index == NONE)
		return luaL_error(state, "invalid object id in delete_object");
	if (game_time_initialized() && object_try_and_get(object_index))
		object_delete(object_index);
	return 0;
}

/* ticks(): the game's ticks, and how far into the next this frame is */
static int lua_ticks(
	lua_State *state)
{
	lua_pushnumber(state, game_time_initialized() ? (lua_Number)game_time_get() : 0.0);
	return 1;
}

/* tick_rate([rate]): the ticks a second; setting it is not supported */
static int lua_tick_rate(
	lua_State *state)
{
	if (lua_gettop(state) == 1)
	{
		if (luaL_checknumber(state, 1) < LUA_MINIMUM_TICK_RATE)
			return luaL_error(state, "tick rate too small");
		lua_log_once("tick_rate set", "%s: tick_rate: changing the tick rate is not supported",
			lua_script_from_state(state)->name);
	}
	lua_pushnumber(state, (lua_Number)TICKS_PER_SECOND);
	return 1;
}

/* ---------- memory (lua_io.cpp), through Twiddlings */

enum lua_memory_type
{
	_lua_memory_i8,
	_lua_memory_i16,
	_lua_memory_i32,
	_lua_memory_u8,
	_lua_memory_u16,
	_lua_memory_u32,
	_lua_memory_f32,
	_lua_memory_f64
};

struct lua_memory_name
{
	char const *name;
	enum lua_memory_type type;
};

/* Chimera's names for them, each a read_ and a write_ */
static struct lua_memory_name const lua_memory_names[] =
{
	{ "i8", _lua_memory_i8 }, { "i16", _lua_memory_i16 }, { "i32", _lua_memory_i32 },
	{ "u8", _lua_memory_u8 }, { "u16", _lua_memory_u16 }, { "u32", _lua_memory_u32 },
	{ "byte", _lua_memory_u8 }, { "word", _lua_memory_u16 }, { "dword", _lua_memory_u32 },
	{ "char", _lua_memory_i8 }, { "short", _lua_memory_i16 }, { "int", _lua_memory_i32 },
	{ "long", _lua_memory_i32 },
	{ "f32", _lua_memory_f32 }, { "f64", _lua_memory_f64 }, { "float", _lua_memory_f32 },
	{ "double", _lua_memory_f64 },
};

static unsigned int lua_memory_size(
	enum lua_memory_type type)
{
	switch (type)
	{
	case _lua_memory_i8:
	case _lua_memory_u8:
		return 1;
	case _lua_memory_i16:
	case _lua_memory_u16:
		return 2;
	case _lua_memory_f64:
		return 8;
	default:
		return 4;
	}
}

static unsigned int lua_check_address(
	lua_State *state,
	int argument)
{
	return (unsigned int)luaL_checkinteger(state, argument);
}

static int lua_read_memory(
	lua_State *state)
{
	enum lua_memory_type type = (enum lua_memory_type)lua_tointeger(state, lua_upvalueindex(1));
	union { signed char i8; short i16; int i32; unsigned char u8; unsigned short u16; unsigned int u32; float f32;
		double f64; } value;
	unsigned int address;

	if (lua_gettop(state) != 1)
		return lua_wrong_arguments(state, "read");
	address = lua_check_address(state, 1);
	twiddlings_read(address, &value, lua_memory_size(type));
	switch (type)
	{
	case _lua_memory_i8: lua_pushinteger(state, value.i8); break;
	case _lua_memory_i16: lua_pushinteger(state, value.i16); break;
	case _lua_memory_i32: lua_pushinteger(state, value.i32); break;
	case _lua_memory_u8: lua_pushinteger(state, value.u8); break;
	case _lua_memory_u16: lua_pushinteger(state, value.u16); break;
	case _lua_memory_u32: lua_pushinteger(state, value.u32); break;
	case _lua_memory_f32: lua_pushnumber(state, value.f32); break;
	case _lua_memory_f64: lua_pushnumber(state, value.f64); break;
	}
	return 1;
}

static int lua_write_memory(
	lua_State *state)
{
	enum lua_memory_type type = (enum lua_memory_type)lua_tointeger(state, lua_upvalueindex(1));
	union { signed char i8; short i16; int i32; unsigned char u8; unsigned short u16; unsigned int u32; float f32;
		double f64; } value;
	unsigned int address;

	if (lua_gettop(state) != 2)
		return lua_wrong_arguments(state, "write");
	address = lua_check_address(state, 1);
	switch (type)
	{
	case _lua_memory_i8: value.i8 = (signed char)luaL_checkinteger(state, 2); break;
	case _lua_memory_i16: value.i16 = (short)luaL_checkinteger(state, 2); break;
	case _lua_memory_i32: value.i32 = (int)luaL_checkinteger(state, 2); break;
	case _lua_memory_u8: value.u8 = (unsigned char)luaL_checkinteger(state, 2); break;
	case _lua_memory_u16: value.u16 = (unsigned short)luaL_checkinteger(state, 2); break;
	case _lua_memory_u32: value.u32 = (unsigned int)luaL_checkinteger(state, 2); break;
	case _lua_memory_f32: value.f32 = (float)luaL_checknumber(state, 2); break;
	case _lua_memory_f64: value.f64 = (double)luaL_checknumber(state, 2); break;
	}
	twiddlings_write(address, &value, lua_memory_size(type));
	return 0;
}

/* read_bit(address, bit): a bit of the 32-bit value there */
static int lua_read_bit(
	lua_State *state)
{
	unsigned int address, value = 0;
	lua_Integer bit;

	if (lua_gettop(state) != 2)
		return lua_wrong_arguments(state, "read_bit");
	address = lua_check_address(state, 1);
	bit = luaL_checkinteger(state, 2);
	if (bit < 0 || bit >= 32)
		return luaL_error(state, "invalid argument 2 in read_bit");
	twiddlings_read(address, &value, sizeof(value));
	lua_pushinteger(state, (value >> bit) & 1);
	return 1;
}

/* write_bit(address, bit, 0 or 1 or a boolean) */
static int lua_write_bit(
	lua_State *state)
{
	unsigned int address, value = 0;
	lua_Integer bit;
	int set;

	if (lua_gettop(state) != 3)
		return lua_wrong_arguments(state, "write_bit");
	address = lua_check_address(state, 1);
	bit = luaL_checkinteger(state, 2);
	if (bit < 0 || bit >= 32)
		return luaL_error(state, "invalid argument 2 in write_bit");
	if (lua_isboolean(state, 3))
	{
		set = lua_toboolean(state, 3);
	}
	else
	{
		lua_Integer given = luaL_checkinteger(state, 3);

		if (given > 1 || given < 0)
			return luaL_error(state, "invalid argument 3 in write_bit");
		set = (int)given;
	}
	if (!twiddlings_read(address, &value, sizeof(value)))
	{
		/* (refused: what is not read is not written) */
		twiddlings_write(address, &value, sizeof(value));
		return 0;
	}
	if (set)
		value |= 1u << bit;
	else
		value &= ~(1u << bit);
	twiddlings_write(address, &value, sizeof(value));
	return 0;
}

static int lua_read_string8(
	lua_State *state)
{
	char text[1024];

	if (lua_gettop(state) != 1)
		return lua_wrong_arguments(state, "read_string");
	twiddlings_read_string(lua_check_address(state, 1), text, sizeof(text));
	lua_pushstring(state, text);
	return 1;
}

static int lua_write_string8(
	lua_State *state)
{
	if (lua_gettop(state) != 2)
		return lua_wrong_arguments(state, "write_string");
	twiddlings_write_string(lua_check_address(state, 1), luaL_checkstring(state, 2));
	return 0;
}

static void lua_register_memory(
	lua_State *state)
{
	short index;

	for (index = 0; index < (short)NUMBEROF(lua_memory_names); index++)
	{
		char name[32];

		snprintf(name, sizeof(name), "read_%s", lua_memory_names[index].name);
		lua_pushinteger(state, lua_memory_names[index].type);
		lua_pushcclosure(state, lua_read_memory, 1);
		lua_setglobal(state, name);
		snprintf(name, sizeof(name), "write_%s", lua_memory_names[index].name);
		lua_pushinteger(state, lua_memory_names[index].type);
		lua_pushcclosure(state, lua_write_memory, 1);
		lua_setglobal(state, name);
	}
	lua_register(state, "read_bit", lua_read_bit);
	lua_register(state, "write_bit", lua_write_bit);
	lua_register(state, "read_string8", lua_read_string8);
	lua_register(state, "write_string8", lua_write_string8);
	lua_register(state, "read_string", lua_read_string8);
	lua_register(state, "write_string", lua_write_string8);
}

/* ---------- public code */

void lua_chimera_api_register(
	lua_State *state)
{
	static char const *const unsupported[] =
	{
		"draw_text", "hud_message", "load_ui_widget", "create_font_override", "execute_chimera_command",
		"create_directory", "remove_directory", "list_directory", "directory_exists",
		"write_file", "read_file", "delete_file", "file_exists",
	};
	short index;

	lua_register(state, "set_callback", lua_set_callback);
	lua_register(state, "console_out", lua_console_out);
	lua_register(state, "console_is_open", lua_console_is_open);
	lua_register(state, "execute_script", lua_execute_script);
	lua_register(state, "get_tag", lua_get_tag);
	lua_register(state, "get_object", lua_get_object);
	lua_register(state, "get_player", lua_get_player);
	lua_register(state, "get_dynamic_player", lua_get_dynamic_player);
	lua_register(state, "spawn_object", lua_spawn_object);
	lua_register(state, "delete_object", lua_delete_object);
	lua_register(state, "ticks", lua_ticks);
	lua_register(state, "tick_rate", lua_tick_rate);
	lua_register(state, "get_global", lua_get_global);
	lua_register(state, "set_global", lua_set_global);
	lua_register(state, "set_timer", lua_set_timer);
	lua_register(state, "stop_timer", lua_stop_timer);
	lua_register_memory(state);
	for (index = 0; index < (short)NUMBEROF(unsupported); index++)
		lua_register_unsupported(state, unsupported[index]);
	lua_pushinteger(state, LUA_CHIMERA_BUILD);
	lua_setglobal(state, "build");
	lua_pushstring(state, LUA_CHIMERA_FULL_BUILD);
	lua_setglobal(state, "full_build");
	lua_chimera_api_refresh(state);
}

/* (lua_variables.cpp) */
void lua_chimera_api_refresh(
	lua_State *state)
{
	char const *map = "";
	void const *header = cache_file_loaded_header();
	boolean engine = game_time_initialized() && game_engine_running();
	char name[0x20 + 1];

	if (game_time_initialized() && local_player_exists(0) && local_player_get_player_index(0) != NONE)
		lua_pushinteger(state, local_player_get_player_index(0) & 0xFFFF);
	else
		lua_pushnil(state);
	lua_setglobal(state, "local_player_index");

	/* (the cache header's name, as Chimera's: the scenario's) */
	if (header)
	{
		memcpy(name, (char const *)header + 0x20, 0x20);
		name[0x20] = 0;
		map = name;
	}
	else if (cache_file_loaded_map_name())
	{
		map = cache_file_loaded_map_name();
	}
	lua_pushstring(state, map);
	lua_setglobal(state, "map");

	lua_pushboolean(state, FALSE);
	lua_setglobal(state, "map_is_protected");

	/* (a game type's game is the player's own here: "local", whose game
	type Chimera leaves nil; a campaign is "none") */
	lua_pushstring(state, engine ? "local" : "none");
	lua_setglobal(state, "server_type");
	lua_pushnil(state);
	lua_setglobal(state, "gametype");
}

/* (lua_script.cpp's load_lua_script, after the state is made) */
boolean lua_chimera_api_run(
	struct lua_script *script,
	char const *chunk,
	unsigned int size)
{
	lua_State *state = script->state;
	char chunk_name[LUA_SCRIPT_NAME_LENGTH + 1];

	lua_pushstring(state, script->name);
	lua_setglobal(state, "script_name");
	lua_pushstring(state, script->kind == _lua_script_map ? "map" : "global");
	lua_setglobal(state, "script_type");
	lua_pushboolean(state, script->sandbox);
	lua_setglobal(state, "sandboxed");

	snprintf(chunk_name, sizeof(chunk_name), "=%s", script->name);
	if (luaL_loadbufferx(state, chunk, size, chunk_name, "t") != LUA_OK)
	{
		lua_log_error(state, script->name);
		lua_log("%s: did not load", script->name);
		return FALSE;
	}
	if (lua_pcall_logged(state, 0, 0, script->name) != LUA_OK)
	{
		lua_log("%s: did not load", script->name);
		return FALSE;
	}

	lua_getglobal(state, "clua_version");
	if (lua_isnumber(state, -1))
	{
		double version = lua_tonumber(state, -1);

		if (version > LUA_CHIMERA_API_VERSION)
			lua_log("%s: asks for Chimera's Lua API %.3f, newer than this game's %.3f: it may not work", script->name,
				version, LUA_CHIMERA_API_VERSION);
		else if ((int)version < (int)LUA_CHIMERA_API_VERSION)
			lua_log("%s: asks for Chimera's Lua API %.3f, too old for this game's %.3f: it may not work",
				script->name, version, LUA_CHIMERA_API_VERSION);
		else
			script->version = version;
	}
	else
	{
		lua_log("%s: names no Chimera Lua API version (clua_version): it may not work", script->name);
	}
	lua_pop(state, 1);
	script->loaded = TRUE;
	return TRUE;
}

/* (lua_callback.cpp's basic_callback, at each priority) */
void lua_chimera_api_call(
	enum lua_callback callback)
{
	short priority;

	for (priority = 0; priority < number_of_lua_priorities; priority++)
	{
		int index;

		for (index = 0; index < lua_script_count; index++)
		{
			struct lua_script *script = &lua_scripts[index];
			struct lua_script_callback const *entry = &script->callbacks[callback];
			char where[LUA_SCRIPT_NAME_LENGTH + 32];

			if (!script->loaded || !entry->function[0] || entry->priority != priority)
				continue;
			snprintf(where, sizeof(where), "%s (%s)", script->name, lua_callback_names[callback]);
			lua_getglobal(script->state, entry->function);
			lua_pcall_logged(script->state, 0, 0, where);
		}
	}
}

/* (lua_callback.cpp's allow_string_callback: a false result blocks, or
true for a script older than 2.02; the final priority cannot block) */
boolean lua_chimera_api_call_allow(
	enum lua_callback callback,
	char const *text)
{
	boolean allow = TRUE;
	short priority;

	for (priority = 0; priority < number_of_lua_priorities; priority++)
	{
		int index;

		for (index = 0; index < lua_script_count && allow; index++)
		{
			struct lua_script *script = &lua_scripts[index];
			struct lua_script_callback const *entry = &script->callbacks[callback];
			char where[LUA_SCRIPT_NAME_LENGTH + 32];

			if (!script->loaded || !entry->function[0] || entry->priority != priority)
				continue;
			snprintf(where, sizeof(where), "%s (%s)", script->name, lua_callback_names[callback]);
			lua_getglobal(script->state, entry->function);
			lua_pushstring(script->state, text);
			if (lua_pcall_logged(script->state, 1, 1, where) == LUA_OK)
			{
				if (!lua_isnil(script->state, -1) && priority != _lua_priority_final)
				{
					allow = lua_toboolean(script->state, -1) ? TRUE : FALSE;
					if (script->version < 2.02)
						allow = !allow;
				}
				lua_pop(script->state, 1);
			}
		}
	}
	return allow;
}

/* (lua_callback.cpp's check_timers: real time since the last check, at
each tick and each frame) */
void lua_chimera_api_timers(
	void)
{
	static unsigned long last = 0;
	static boolean started = FALSE;
	unsigned long now = system_milliseconds();
	double elapsed = started ? (double)(unsigned long)(now - last) : 0.0;
	int index;

	last = now;
	started = TRUE;
	for (index = 0; index < lua_script_count; index++)
	{
		struct lua_script *script = &lua_scripts[index];
		long ids[LUA_MAXIMUM_TIMERS];
		short count = script->timer_count, timer_index;

		if (!script->loaded)
			continue;
		/* (the timers as they are now: one a timer sets waits for the next
		check) */
		for (timer_index = 0; timer_index < count; timer_index++)
			ids[timer_index] = script->timers[timer_index].id;
		for (timer_index = 0; timer_index < count; timer_index++)
		{
			struct lua_timer *timer = lua_timer_find(script, ids[timer_index]);
			short calls = 0;

			if (!timer)
				continue;
			timer->passed += elapsed;
			while (timer && timer->passed >= timer->interval)
			{
				lua_State *state = script->state;
				char where[LUA_SCRIPT_NAME_LENGTH + LUA_FUNCTION_NAME_LENGTH + 16];
				long id = timer->id;
				int argument, arguments;
				boolean stop = FALSE;

				if (calls++ >= LUA_MAXIMUM_TIMER_CALLS)
				{
					lua_log_once("timer calls", "%s: timer %s owed more than %d calls at once: the rest let go",
						script->name, timer->function, LUA_MAXIMUM_TIMER_CALLS);
					timer->passed = 0.0;
					break;
				}
				timer->passed -= timer->interval;
				snprintf(where, sizeof(where), "%s (timer %s)", script->name, timer->function);
				lua_getglobal(state, timer->function);
				lua_rawgeti(state, LUA_REGISTRYINDEX, timer->arguments);
				lua_getfield(state, -1, "n");
				arguments = (int)lua_tointeger(state, -1);
				lua_pop(state, 1);
				for (argument = 1; argument <= arguments; argument++)
				{
					lua_rawgeti(state, -1 - (argument - 1), argument);
				}
				/* (the table out from under the function's arguments) */
				lua_remove(state, -1 - arguments);
				if (lua_pcall_logged(state, arguments, 1, where) == LUA_OK)
				{
					if (lua_isboolean(state, -1) && !lua_toboolean(state, -1))
						stop = TRUE;
					lua_pop(state, 1);
				}
				/* (the call may have stopped it, or set others) */
				timer = lua_timer_find(script, id);
				if (timer && stop)
				{
					lua_timer_remove(script, id);
					timer = NULL;
				}
			}
		}
	}
}

#endif
