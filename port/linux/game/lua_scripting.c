/*
LUA_SCRIPTING.C

Mods' Lua scripts written for Chimera's Lua API (lua_scripting.h), so that
what modders wrote for Chimera carries over: SPV3's HUD, legs and skulls,
and anyone's own. Off unless lua.scripts is on, but for SPV3 mode's own.

Where scripts are, as Chimera lays out its lua folder, in the data folder
(the one holding maps/):
  lua\scripts\global\*.lua      loaded at the first map, kept for the run
  lua\scripts\map\<map>.lua     loaded with that map, closed after it
  lua\modules\                  what require and loadfile find
  lua\data\                     what io.open reads and writes
and a map pack's own (game.pack_menus, as "spv3"), loaded while its menus
are in use, the same folders under lua\<pack>\. The ones above run only
with lua.scripts on; SPV3 mode's own (lua\spv3\, which the setup screen's
installer fills) run in that mode without it, sandboxed the same, since
SPV3 is made to play with them. Each script has a Lua state
of its own (Lua 5.4, port/third_party/lua) and Chimera's API
(lua_chimera_api.c); its memory reads and writes go through Twiddlings
(lua_twiddlings.c).

What a script may do is less than Chimera gives its global scripts, which
may do anything a program may:
  - no C modules, no bytecode, no debug library, no os.execute, os.exit,
    os.remove, os.rename or os.tmpname, no io.popen;
  - require, loadfile and dofile find .lua files under its modules folder
    alone, a name's case ignored (Windows' file names are: SPV3 asks for
    "cmt" and has "CMT"), a leading lua\ dropped (as the game folder was
    theirs);
  - io.open opens files under its data folder alone; os.getenv gives
    USERPROFILE and APPDATA as Windows paths, which io.open finds in the
    data folder's profile\ (SPV3 keeps its resume file and settings there);
  - its Lua memory is held to 64 MB.
A map's own script (not loaded yet) gets Chimera's map sandbox on top.
What a script asks for that the game does not do is logged once in
debug.txt, "lua: ...", and each distinct error once.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifdef HALO_LUA

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "errors.h"
#include "cache/cache_files.h"
#include "interface/terminal.h"

#include "lua_internal.h"
#include "lua_opensauce.h"
#include "lua_scripting.h"
#include "lua_twiddlings.h"
#include "spv3_mode.h"
#include "../src/spv3_installer.h"
#include "../../third_party/lua/lualib.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (port/linux/src/port_config.c) */
int config_boolean(char const *name);
char const *config_string(char const *name);

void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size);

/* ---------- constants */

#define LUA_SCRIPT_MEMORY_LIMIT (64u * 1024u * 1024u)
#define LUA_SCRIPT_FILE_LIMIT (4u * 1024u * 1024u)
#define LUA_LOG_ONCE_SLOTS 2048
#define LUA_ROOT "d:\\lua"
/* the Windows folders a script is told it has (os.getenv) */
#define LUA_VIRTUAL_PROFILE "C:\\Users\\Player"
#define LUA_VIRTUAL_APPDATA "C:\\Users\\Player\\AppData\\Roaming"
/* the objects a script may pose between two ticks (lua_scripting_object_posed) */
#define LUA_MAXIMUM_POSED_OBJECTS 16
/* execute_script's functions: Halo PC's, then OpenSauce's */
#define LUA_HS_HALO_PC_FIRST LUA_HS_FUNCTION_FIRST
#define LUA_HS_OPENSAUCE_FIRST (LUA_HS_FUNCTION_FIRST + 0x40)
#define LUA_HS_FUNCTION_END (LUA_HS_FUNCTION_FIRST + 0x100)

/* ---------- globals */

struct lua_script lua_scripts[LUA_MAXIMUM_SCRIPTS];
int lua_script_count = 0;
boolean lua_execute_script_running = FALSE;

/* the objects scripts posed since the last tick */
static long lua_posed_objects[LUA_MAXIMUM_POSED_OBJECTS];
static short lua_posed_object_count = 0;

static struct
{
	boolean announced;
	boolean globals_loaded;
	char pack[32];
	/* SPV3 mode's own scripts running without lua.scripts: only the files
	the installer writes, unchanged, are read (lua_read_file) */
	boolean known_files_only;
	/* hashes of what lua_log_once has logged */
	unsigned long long logged[LUA_LOG_ONCE_SLOTS];
	int logged_count;
	boolean logged_full;
} lua_globals;

/* ---------- logging */

static unsigned long long lua_hash(
	char const *text)
{
	unsigned long long hash = 1469598103934665603ULL;

	while (*text)
	{
		hash ^= (unsigned char)*text++;
		hash *= 1099511628211ULL;
	}
	return hash ? hash : 1;
}

/* TRUE the first time key is seen */
static boolean lua_first_time(
	char const *key)
{
	unsigned long long hash = lua_hash(key);
	int slot = (int)(hash % LUA_LOG_ONCE_SLOTS);
	int probe;

	for (probe = 0; probe < LUA_LOG_ONCE_SLOTS; probe++)
	{
		unsigned long long *entry = &lua_globals.logged[(slot + probe) % LUA_LOG_ONCE_SLOTS];

		if (*entry == hash)
			return FALSE;
		if (!*entry)
		{
			if (lua_globals.logged_count + 1 >= LUA_LOG_ONCE_SLOTS)
				break;
			*entry = hash;
			lua_globals.logged_count++;
			return TRUE;
		}
	}
	if (!lua_globals.logged_full)
	{
		lua_globals.logged_full = TRUE;
		error(_error_silent, "lua: too many different things to log: no more are");
	}
	return FALSE;
}

void lua_log(
	char const *format,
	...)
{
	char text[1024];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	error(_error_silent, "lua: %s", text);
}

void lua_log_once(
	char const *key,
	char const *format,
	...)
{
	char text[1024];
	va_list arguments;

	if (!lua_first_time(key))
		return;
	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	error(_error_silent, "lua: %s", text);
}

void lua_log_error(
	lua_State *state,
	char const *where)
{
	char const *message = lua_tostring(state, -1);
	char key[1200];

	snprintf(key, sizeof(key), "error %s %s", where, message ? message : "(no message)");
	lua_log_once(key, "%s: error: %s", where, message ? message : "(no message)");
	lua_pop(state, 1);
}

void lua_console_line(
	real_argb_color const *color,
	char const *text)
{
	terminal_printf(color, "%s", text);
	error(_error_silent, "lua: console: %s", text);
}

/* ---------- scripts */

struct lua_script *lua_script_from_state(
	lua_State *state)
{
	int index;
	void *user = NULL;

	lua_getallocf(state, &user);
	for (index = 0; index < lua_script_count; index++)
	{
		if (&lua_scripts[index] == user)
			return &lua_scripts[index];
	}
	/* (every state is one of ours) */
	return &lua_scripts[0];
}

/* Lua's own allocator (lauxlib.c's, with the host's C library, not the
game's memory: this unit's malloc is the game's) */
static lua_Alloc lua_host_allocate = NULL;

/* the state's allocator: Lua's own, held to LUA_SCRIPT_MEMORY_LIMIT */
static void *lua_allocate(
	void *user,
	void *block,
	size_t old_size,
	size_t new_size)
{
	struct lua_script *script = (struct lua_script *)user;
	void *result;

	if (!block)
		old_size = 0;
	if (!new_size)
	{
		lua_host_allocate(NULL, block, old_size, 0);
		script->memory -= (unsigned int)old_size;
		return NULL;
	}
	if (new_size > old_size && new_size - old_size > LUA_SCRIPT_MEMORY_LIMIT - script->memory)
	{
		lua_log_once(script->name, "%s: is over its %u MB of memory", script->name, LUA_SCRIPT_MEMORY_LIMIT >> 20);
		return NULL;
	}
	result = lua_host_allocate(NULL, block, old_size, new_size);
	if (result)
		script->memory = script->memory - (unsigned int)old_size + (unsigned int)new_size;
	return result;
}

/* a whole file at an Xbox path (d:\...), NUL terminated, or NULL; free() it */
static char *lua_read_file(
	char const *xbox_path,
	unsigned int *size)
{
	FILE *file;
	char *buffer;
	long length;

	/* (the game's fopen takes an Xbox path: port/linux/include/stdio.h) */
	file = fopen(xbox_path, "rb");
	if (!file)
		return NULL;
	if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
		(unsigned long)length > LUA_SCRIPT_FILE_LIMIT || fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		return NULL;
	}
	buffer = (char *)malloc((size_t)length + 1);
	if (buffer && fread(buffer, 1, (size_t)length, file) != (size_t)length)
	{
		free(buffer);
		buffer = NULL;
	}
	fclose(file);
	if (buffer)
	{
		buffer[length] = 0;
		*size = (unsigned int)length;
		/* (without lua.scripts, SPV3 mode reads only SPV3's own files as
		the installer wrote them: d:\lua\spv3\... must be one of its list,
		at its size and CRC-32) */
		if (lua_globals.known_files_only)
		{
			char const *relative = xbox_path;

			if (!_strnicmp(relative, "d:\\", 3) || !_strnicmp(relative, "d:/", 3))
				relative += 3;
			if (!spv3_installer_known_lua(relative, buffer, (unsigned long long)length))
			{
				char key[LUA_PATH_LENGTH * 2 + 16];

				snprintf(key, sizeof(key), "unknown %s", relative);
				lua_log_once(key, "%s: not one of SPV3's own files as installed (lua.scripts is off): not run",
					relative);
				free(buffer);
				return NULL;
			}
		}
	}
	return buffer;
}

/* a relative path a script gave (a module's, a file's), checked and made
an Xbox path under folder: FALSE for one that would leave it */
static boolean lua_jail_path(
	char const *folder,
	char const *relative,
	char *path,
	size_t size)
{
	size_t length = strlen(folder);
	char const *cursor;
	char previous = 0;

	if (!relative[0] || relative[0] == '/' || relative[0] == '\\' || strchr(relative, ':'))
		return FALSE;
	if (length + 1 + strlen(relative) + 1 > size)
		return FALSE;
	memcpy(path, folder, length);
	path[length++] = '\\';
	for (cursor = relative; *cursor; cursor++)
	{
		char character = *cursor == '/' ? '\\' : *cursor;

		/* (no .. anywhere) */
		if (character == '.' && previous == '.')
			return FALSE;
		previous = character;
		path[length++] = character;
	}
	path[length] = 0;
	return TRUE;
}

/* the leading lua\ of a path relative to the game folder dropped */
static char const *lua_strip_game_folder(
	char const *path)
{
	if (!_strnicmp(path, "lua/", 4) || !_strnicmp(path, "lua\\", 4))
		return path + 4;
	if (!_strnicmp(path, "./lua/", 6) || !_strnicmp(path, ".\\lua\\", 6))
		return path + 6;
	return path;
}

static int lua_load_jailed_file(
	lua_State *state,
	char const *relative)
{
	struct lua_script *script = lua_script_from_state(state);
	char folder[LUA_PATH_LENGTH + 16], path[LUA_PATH_LENGTH * 2], chunk_name[LUA_PATH_LENGTH];
	unsigned int size = 0;
	char *text;
	int result;

	snprintf(folder, sizeof(folder), "%s\\modules", script->root);
	if (!lua_jail_path(folder, lua_strip_game_folder(relative), path, sizeof(path)))
	{
		lua_pushnil(state);
		lua_pushfstring(state, "%s: not in the script's modules folder", relative);
		return 2;
	}
	text = lua_read_file(path, &size);
	if (!text)
	{
		lua_pushnil(state);
		lua_pushfstring(state, "%s: not found", relative);
		return 2;
	}
	snprintf(chunk_name, sizeof(chunk_name), "@%s", relative);
	result = luaL_loadbufferx(state, text, size, chunk_name, "t");
	free(text);
	if (result != LUA_OK)
	{
		lua_pushnil(state);
		lua_insert(state, -2);
		return 2;
	}
	return 1;
}

/* loadfile(path): a .lua file under the modules folder, as text */
static int lua_jailed_loadfile(
	lua_State *state)
{
	return lua_load_jailed_file(state, luaL_checkstring(state, 1));
}

/* dofile(path): the same, run */
static int lua_jailed_dofile(
	lua_State *state)
{
	if (lua_load_jailed_file(state, luaL_checkstring(state, 1)) != 1)
		return luaL_error(state, "%s", lua_tostring(state, -1));
	lua_call(state, 0, LUA_MULTRET);
	return lua_gettop(state) - 1;
}

/* load(chunk, [name], [mode], [env]): Lua's, text alone */
static int lua_text_load(
	lua_State *state)
{
	int arguments = lua_gettop(state);

	lua_pushvalue(state, lua_upvalueindex(1));
	lua_pushvalue(state, 1);
	if (arguments >= 2)
		lua_pushvalue(state, 2);
	else
		lua_pushnil(state);
	lua_pushstring(state, "t");
	if (arguments >= 4)
		lua_pushvalue(state, 4);
	lua_call(state, arguments >= 4 ? 4 : 3, 2);
	return 2;
}

/* require's searcher: name.lua or name\init.lua under the modules folder
(a dot is a folder) */
static int lua_module_searcher(
	lua_State *state)
{
	char const *name = luaL_checkstring(state, 1);
	char relative[LUA_PATH_LENGTH];
	size_t index, length = strlen(name);
	int result;

	if (length + 10 >= sizeof(relative))
		return 0;
	for (index = 0; index <= length; index++)
		relative[index] = name[index] == '.' ? '\\' : name[index];
	strcat(relative, ".lua");
	result = lua_load_jailed_file(state, relative);
	if (result == 1)
	{
		lua_pushstring(state, relative);
		return 2;
	}
	lua_pop(state, 2);
	relative[length] = 0;
	strcat(relative, "\\init.lua");
	result = lua_load_jailed_file(state, relative);
	if (result == 1)
	{
		lua_pushstring(state, relative);
		return 2;
	}
	lua_pop(state, 2);
	lua_pushfstring(state, "\n\tno module %s in the script's modules folder", name);
	return 1;
}

/* os.getenv(name): the Windows folders a script is told it has, nothing else */
static int lua_getenv(
	lua_State *state)
{
	char const *name = luaL_checkstring(state, 1);

	if (!_stricmp(name, "USERPROFILE"))
		lua_pushstring(state, LUA_VIRTUAL_PROFILE);
	else if (!_stricmp(name, "APPDATA"))
		lua_pushstring(state, LUA_VIRTUAL_APPDATA);
	else
		lua_pushnil(state);
	return 1;
}

static int lua_refused(
	lua_State *state)
{
	char const *name = lua_tostring(state, lua_upvalueindex(1));
	char key[96];

	snprintf(key, sizeof(key), "refused %s", name);
	lua_log_once(key, "%s: %s is not allowed: it does nothing", lua_script_from_state(state)->name, name);
	lua_pushnil(state);
	return 1;
}

/* makes each folder of an Xbox path's (not its last part) */
static void lua_make_folders(
	char const *xbox_path)
{
	char partial[LUA_PATH_LENGTH * 2];
	size_t index;

	for (index = 0; xbox_path[index] && index + 1 < sizeof(partial); index++)
	{
		if (xbox_path[index] == '\\' && index > 3)
		{
			memcpy(partial, xbox_path, index);
			partial[index] = 0;
			CreateDirectoryA(partial, NULL);
		}
	}
}

/* io.open(path, [mode]): a file under the script's data folder; a path
under the Windows folders os.getenv gives is under its profile\ */
static int lua_jailed_open(
	lua_State *state)
{
	struct lua_script *script = lua_script_from_state(state);
	char const *given = luaL_checkstring(state, 1);
	char const *mode = luaL_optstring(state, 2, "r");
	char folder[LUA_PATH_LENGTH + 32], path[LUA_PATH_LENGTH * 2], host_path[1024];
	char const *relative = given;
	size_t profile = strlen(LUA_VIRTUAL_PROFILE);
	boolean writes = strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+');

	snprintf(folder, sizeof(folder), "%s\\data", script->root);
	if (!_strnicmp(given, LUA_VIRTUAL_PROFILE, profile) && (given[profile] == '\\' || given[profile] == '/'))
	{
		snprintf(folder, sizeof(folder), "%s\\data\\profile", script->root);
		relative = given + profile + 1;
	}
	if (!lua_jail_path(folder, relative, path, sizeof(path)))
	{
		char key[LUA_PATH_LENGTH + 16];

		snprintf(key, sizeof(key), "open %s", given);
		lua_log_once(key, "%s: io.open: %s is outside the script's data folder: refused", script->name, given);
		lua_pushnil(state);
		lua_pushfstring(state, "%s: outside the script's data folder", given);
		return 2;
	}
	if (writes)
		lua_make_folders(path);
	platform_translate_path(path, host_path, sizeof(host_path));
	lua_pushvalue(state, lua_upvalueindex(1));
	lua_pushstring(state, host_path);
	lua_pushstring(state, mode);
	lua_call(state, 2, 3);
	return 3;
}

/* print(...): into debug.txt */
static int lua_print(
	lua_State *state)
{
	int arguments = lua_gettop(state), index;
	luaL_Buffer buffer;

	luaL_buffinit(state, &buffer);
	for (index = 1; index <= arguments; index++)
	{
		if (index > 1)
			luaL_addchar(&buffer, '\t');
		luaL_tolstring(state, index, NULL);
		luaL_addvalue(&buffer);
	}
	luaL_pushresult(&buffer);
	lua_log("%s: %s", lua_script_from_state(state)->name, lua_tostring(state, -1));
	return 0;
}

static void lua_set_field_nil(
	lua_State *state,
	char const *table,
	char const *field)
{
	lua_getglobal(state, table);
	if (lua_istable(state, -1))
	{
		lua_pushnil(state);
		lua_setfield(state, -2, field);
	}
	lua_pop(state, 1);
}

static void lua_set_field_refused(
	lua_State *state,
	char const *table,
	char const *field)
{
	char name[64];

	snprintf(name, sizeof(name), "%s.%s", table, field);
	lua_getglobal(state, table);
	lua_pushstring(state, name);
	lua_pushcclosure(state, lua_refused, 1);
	lua_setfield(state, -2, field);
	lua_pop(state, 1);
}

/* the libraries, as the header says */
static void lua_open_sandboxed(
	lua_State *state,
	boolean sandbox)
{
	static luaL_Reg const libraries[] =
	{
		{ LUA_GNAME, luaopen_base },
		{ LUA_LOADLIBNAME, luaopen_package },
		{ LUA_COLIBNAME, luaopen_coroutine },
		{ LUA_TABLIBNAME, luaopen_table },
		{ LUA_IOLIBNAME, luaopen_io },
		{ LUA_OSLIBNAME, luaopen_os },
		{ LUA_STRLIBNAME, luaopen_string },
		{ LUA_MATHLIBNAME, luaopen_math },
		{ LUA_UTF8LIBNAME, luaopen_utf8 },
	};
	static char const *const io_removed[] = { "popen", "lines", "input", "output", "read", "write", "tmpfile", "close" };
	static char const *const os_removed[] = { "exit", "remove", "rename", "tmpname", "setlocale" };
	short index;

	for (index = 0; index < (short)NUMBEROF(libraries); index++)
	{
		luaL_requiref(state, libraries[index].name, libraries[index].func, 1);
		lua_pop(state, 1);
	}

	/* base: no files but the modules folder's, text alone */
	lua_register(state, "loadfile", lua_jailed_loadfile);
	lua_register(state, "dofile", lua_jailed_dofile);
	lua_register(state, "print", lua_print);
	lua_getglobal(state, "load");
	lua_pushcclosure(state, lua_text_load, 1);
	lua_setglobal(state, "load");

	/* package: the modules folder's searcher after the preloaded */
	lua_getglobal(state, LUA_LOADLIBNAME);
	lua_pushstring(state, "");
	lua_setfield(state, -2, "path");
	lua_pushstring(state, "");
	lua_setfield(state, -2, "cpath");
	lua_pushnil(state);
	lua_setfield(state, -2, "loadlib");
	lua_getfield(state, -1, "searchers");
	lua_rawgeti(state, -1, 1);
	lua_newtable(state);
	lua_insert(state, -2);
	lua_rawseti(state, -2, 1);
	lua_pushcfunction(state, lua_module_searcher);
	lua_rawseti(state, -2, 2);
	lua_setfield(state, -3, "searchers");
	lua_pop(state, 2);

	/* io: open alone, in the data folder */
	lua_getglobal(state, LUA_IOLIBNAME);
	lua_getfield(state, -1, "open");
	lua_pushcclosure(state, lua_jailed_open, 1);
	lua_setfield(state, -2, "open");
	lua_pop(state, 1);
	for (index = 0; index < (short)NUMBEROF(io_removed); index++)
		lua_set_field_nil(state, LUA_IOLIBNAME, io_removed[index]);

	/* os: the time, and the Windows folders */
	lua_getglobal(state, LUA_OSLIBNAME);
	lua_pushcfunction(state, lua_getenv);
	lua_setfield(state, -2, "getenv");
	lua_pop(state, 1);
	lua_set_field_refused(state, LUA_OSLIBNAME, "execute");
	for (index = 0; index < (short)NUMBEROF(os_removed); index++)
		lua_set_field_nil(state, LUA_OSLIBNAME, os_removed[index]);

	/* a map's own script: Chimera's map sandbox (lua_script.cpp) */
	if (sandbox)
	{
		static char const *const removed[] = { "io", "dofile", "getfenv", "load", "loadfile", "loadstring", "require" };

		for (index = 0; index < (short)NUMBEROF(removed); index++)
		{
			lua_pushnil(state);
			lua_setglobal(state, removed[index]);
		}
		lua_set_field_nil(state, LUA_OSLIBNAME, "execute");
	}
}

static void lua_script_close(
	struct lua_script *script)
{
	if (script->state)
	{
		if (script->loaded && script->callbacks[_lua_callback_unload].function[0])
		{
			lua_getglobal(script->state, script->callbacks[_lua_callback_unload].function);
			if (lua_pcall(script->state, 0, 0, 0) != LUA_OK)
				lua_log_error(script->state, script->name);
		}
		lua_close(script->state);
	}
	memset(script, 0, sizeof(*script));
}

/* closes the scripts of a kind, keeping the others in order */
static void lua_close_kind(
	enum lua_script_kind kind)
{
	int index, kept = 0;

	for (index = 0; index < lua_script_count; index++)
	{
		if (lua_scripts[index].kind == kind)
		{
			lua_log("%s: closed", lua_scripts[index].name);
			lua_script_close(&lua_scripts[index]);
			continue;
		}
		if (kept != index)
		{
			/* (the state's allocator names its script: moved with it) */
			lua_scripts[kept] = lua_scripts[index];
			lua_setallocf(lua_scripts[kept].state, lua_allocate, &lua_scripts[kept]);
			memset(&lua_scripts[index], 0, sizeof(lua_scripts[index]));
		}
		kept++;
	}
	lua_script_count = kept;
}

static void lua_script_load(
	char const *root,
	char const *file_path,
	char const *name,
	enum lua_script_kind kind)
{
	struct lua_script *script;
	unsigned int size = 0;
	char *text;

	if (lua_script_count >= LUA_MAXIMUM_SCRIPTS)
	{
		lua_log("%s: not loaded: %d scripts already are", name, LUA_MAXIMUM_SCRIPTS);
		return;
	}
	text = lua_read_file(file_path, &size);
	if (!text)
	{
		lua_log("%s: could not be read", name);
		return;
	}
	script = &lua_scripts[lua_script_count++];
	memset(script, 0, sizeof(*script));
	csstrncpy(script->name, name, LUA_SCRIPT_NAME_LENGTH - 1);
	csstrncpy(script->root, root, LUA_PATH_LENGTH - 1);
	script->kind = kind;
	script->sandbox = FALSE;
	script->version = LUA_CHIMERA_API_VERSION;
	if (!lua_host_allocate)
	{
		lua_State *probe = luaL_newstate();

		if (probe)
		{
			lua_host_allocate = lua_getallocf(probe, NULL);
			lua_close(probe);
		}
	}
	script->state = lua_host_allocate ? lua_newstate(lua_allocate, script) : NULL;
	if (!script->state)
	{
		lua_log("%s: no memory for it", name);
		lua_script_count--;
		free(text);
		return;
	}
	lua_open_sandboxed(script->state, script->sandbox);
	lua_chimera_api_register(script->state);
	lua_log("%s: loading (%u bytes)", name, size);
	if (!lua_chimera_api_run(script, text, size))
	{
		lua_close(script->state);
		memset(script, 0, sizeof(*script));
		lua_script_count--;
	}
	else
	{
		lua_log("%s: loaded", name);
	}
	free(text);
}

/* every .lua file in root\scripts\global */
static void lua_load_global_scripts(
	char const *root,
	enum lua_script_kind kind)
{
	WIN32_FIND_DATAA data;
	char pattern[LUA_PATH_LENGTH + 32];
	HANDLE find;

	snprintf(pattern, sizeof(pattern), "%s\\scripts\\global\\*.lua", root);
	find = FindFirstFileA(pattern, &data);
	if (find == INVALID_HANDLE_VALUE)
		return;
	do
	{
		char path[LUA_PATH_LENGTH * 2];
		size_t length = strlen(data.cFileName);

		if (length <= 4 || _stricmp(data.cFileName + length - 4, ".lua"))
			continue;
		snprintf(path, sizeof(path), "%s\\scripts\\global\\%s", root, data.cFileName);
		lua_script_load(root, path, data.cFileName, kind);
	} while (FindNextFileA(find, &data));
	CloseHandle(find);
}

static void lua_load_map_script(
	char const *root,
	char const *map)
{
	char path[LUA_PATH_LENGTH * 2], name[LUA_SCRIPT_NAME_LENGTH];
	unsigned int size;
	char *text;

	snprintf(path, sizeof(path), "%s\\scripts\\map\\%s.lua", root, map);
	text = lua_read_file(path, &size);
	if (!text)
		return;
	free(text);
	snprintf(name, sizeof(name), "%s.lua", map);
	lua_script_load(root, path, name, _lua_script_map);
}

static boolean lua_enabled(
	void)
{
	return config_boolean("lua.scripts");
}

/* the pack whose menus are in use (game.pack_menus), "" for none */
static void lua_current_pack(
	char *pack,
	size_t size)
{
	char const *value = config_string("game.pack_menus");
	size_t index;

	snprintf(pack, size, "%s", value);
	for (index = 0; pack[index]; index++)
	{
		char character = pack[index];

		if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
			(character >= '0' && character <= '9') || character == '_' || character == '-'))
		{
			pack[0] = 0;
			return;
		}
	}
}

/* ---------- public code */

void lua_scripting_map_load(
	void)
{
	char pack[sizeof(lua_globals.pack)];
	char map[0x20 + 1];
	int index;
	boolean all = lua_enabled();

	/* (SPV3 mode's own scripts run in that mode without lua.scripts: the
	pack's alone, not the player's global or map ones) */
	lua_current_pack(pack, sizeof(pack));
	if (!all && !(spv3_mode_active() && !strcmp(pack, "spv3")))
		pack[0] = 0;
	lua_globals.known_files_only = !all;
	if (!all && !pack[0])
	{
		if (lua_script_count)
		{
			lua_close_kind(_lua_script_map);
			lua_close_kind(_lua_script_pack);
			lua_close_kind(_lua_script_global);
			lua_globals.globals_loaded = FALSE;
			lua_globals.pack[0] = 0;
		}
		return;
	}
	if (!lua_globals.announced)
	{
		lua_globals.announced = TRUE;
		lua_log("on: %s, Chimera's Lua API %.3f, scripts from the data folder's lua\\%s", LUA_RELEASE,
			LUA_CHIMERA_API_VERSION, all ? "" : "spv3 (SPV3 mode's own; lua.scripts is off)");
	}
	if (all && !lua_globals.globals_loaded)
	{
		lua_globals.globals_loaded = TRUE;
		lua_load_global_scripts(LUA_ROOT, _lua_script_global);
	}
	else if (!all && lua_globals.globals_loaded)
	{
		lua_close_kind(_lua_script_global);
		lua_globals.globals_loaded = FALSE;
	}
	if (strcmp(pack, lua_globals.pack))
	{
		lua_close_kind(_lua_script_pack);
		snprintf(lua_globals.pack, sizeof(lua_globals.pack), "%s", pack);
		if (pack[0])
		{
			char root[LUA_PATH_LENGTH];

			snprintf(root, sizeof(root), "%s\\%s", LUA_ROOT, pack);
			lua_load_global_scripts(root, _lua_script_pack);
		}
	}

	/* the map's own, by the name its scripts see */
	lua_close_kind(_lua_script_map);
	map[0] = 0;
	{
		/* (the cache header's name, as Chimera's: the scenario's) */
		void const *header = cache_file_loaded_header();

		if (header)
		{
			memcpy(map, (char const *)header + 0x20, 0x20);
			map[0x20] = 0;
		}
	}
	if (map[0] && !strpbrk(map, "\\/:."))
	{
		if (all)
			lua_load_map_script(LUA_ROOT, map);
		if (lua_globals.pack[0])
		{
			char root[LUA_PATH_LENGTH];

			snprintf(root, sizeof(root), "%s\\%s", LUA_ROOT, lua_globals.pack);
			lua_load_map_script(root, map);
		}
	}

	lua_opensauce_map_load();
	for (index = 0; index < lua_script_count; index++)
		lua_chimera_api_refresh(lua_scripts[index].state);
	if (lua_script_count)
		lua_log("map %s: %d script(s), calling map load", map, lua_script_count);
	lua_chimera_api_call(_lua_callback_map_load);
}

void lua_scripting_map_unload(
	void)
{
	if (lua_script_count)
		lua_close_kind(_lua_script_map);
}

void lua_scripting_pretick(
	void)
{
	int index;

	/* (a tick poses every object anew) */
	lua_posed_object_count = 0;
	if (!lua_script_count)
		return;
	for (index = 0; index < lua_script_count; index++)
		lua_chimera_api_refresh(lua_scripts[index].state);
	lua_chimera_api_call(_lua_callback_pretick);
}

void lua_scripting_tick(
	void)
{
	if (!lua_script_count)
		return;
	lua_opensauce_tick();
	lua_chimera_api_timers();
	lua_chimera_api_call(_lua_callback_tick);
}

void lua_scripting_preframe(
	void)
{
	if (lua_script_count)
		lua_chimera_api_call(_lua_callback_preframe);
}

void lua_scripting_frame(
	void)
{
	if (!lua_script_count)
		return;
	lua_chimera_api_timers();
	lua_chimera_api_call(_lua_callback_frame);
}

boolean lua_scripting_command(
	char const *command)
{
	if (!lua_script_count)
		return TRUE;
	return lua_chimera_api_call_allow(_lua_callback_command, command);
}

struct hs_function_definition *lua_scripting_hs_function(
	short index)
{
	if (index < LUA_HS_FUNCTION_FIRST || index >= LUA_HS_FUNCTION_END)
		return NULL;
	if (index < LUA_HS_OPENSAUCE_FIRST)
		return twiddlings_hs_function((short)(index - LUA_HS_HALO_PC_FIRST));
	return lua_opensauce_hs_function((short)(index - LUA_HS_OPENSAUCE_FIRST));
}

short lua_scripting_hs_function_find(
	char const *name)
{
	short index;

	if (!lua_execute_script_running || !name)
		return NONE;
	/* (OpenSauce's only on its maps: lua_opensauce.c) */
	index = lua_opensauce_hs_function_find(name);
	if (index != NONE && index < LUA_HS_FUNCTION_END - LUA_HS_OPENSAUCE_FIRST)
		return (short)(LUA_HS_OPENSAUCE_FIRST + index);
	index = twiddlings_hs_function_find(name);
	if (index != NONE && index < LUA_HS_OPENSAUCE_FIRST - LUA_HS_HALO_PC_FIRST)
		return (short)(LUA_HS_HALO_PC_FIRST + index);
	return NONE;
}

void lua_scripting_note_posed(
	long object_index)
{
	short index;

	for (index = 0; index < lua_posed_object_count; index++)
	{
		if (lua_posed_objects[index] == object_index)
			return;
	}
	if (lua_posed_object_count < LUA_MAXIMUM_POSED_OBJECTS)
	{
		lua_posed_objects[lua_posed_object_count++] = object_index;
		return;
	}
	lua_log_once("posed full", "more than %d objects posed by scripts in a tick: the rest are drawn as the ticks left "
		"them", LUA_MAXIMUM_POSED_OBJECTS);
}

boolean lua_scripting_object_posed(
	long object_index)
{
	short index;

	for (index = 0; index < lua_posed_object_count; index++)
	{
		if (lua_posed_objects[index] == object_index)
			return TRUE;
	}
	return FALSE;
}

#endif
