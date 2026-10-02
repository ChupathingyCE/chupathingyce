/*
UI_MAP_LIST.C

The menus' list of multiplayer maps, filled by the port rather than fixed in
the game (ui_widget_event_handler_functions.c's multiplayer level list): the
Xbox's thirteen maps first, as they were, then the Custom Edition maps in
maps\ce (Halo PC's, played as <name>@ce), each named with [CE].

A row past the Xbox's has no string or bitmap frame of its own in ui.map:
its text comes from here, by a string list index of
UI_MAP_LIST_STRING_BASE and up (ui_widget.c asks ui_map_list_text when a
text box has one), and its picture is for now the frame of an Xbox map. Its
name is two lines in the map list's narrow boxes (the name, then [CE]), and
one in the lobby's.
*/

#ifdef HALO_64BIT

#include "cseries.h"
#include "cseries_windows.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "halo_ui_map_list.h"

/* ---------- constants */

enum
{
	XBOX_MAP_COUNT = 13,
	MAXIMUM_MAP_LIST = 64,
	MAP_NAME_LENGTH = 64,
	DISPLAY_NAME_LENGTH = 48,
	/* text boxes' string list indices from here are this list's: the row
	times four, plus its string's kind */
	UI_MAP_LIST_STRING_BASE = 0x4000,
	UI_MAP_LIST_STRINGS_PER_ROW = 4,
};

/* ---------- structures */

struct ui_map_entry
{
	char map_name[MAP_NAME_LENGTH];
	wchar_t display_name[DISPLAY_NAME_LENGTH];
	wchar_t lobby_name[DISPLAY_NAME_LENGTH];
	/* its row among the Xbox's (their strings and bitmap frames), else the
	Xbox map whose picture stands in for it */
	short xbox_index;
	short picture_index;
};

/* the Custom Edition maps' names as the game shows them, and an Xbox map of
the same kind whose picture stands in until their own are drawn */
static struct
{
	char const *file;
	wchar_t const *name;
	short picture_index;
} const ce_map_names[] =
{
	{ "beavercreek", L"Battle Creek", 0 },
	{ "sidewinder", L"Sidewinder", 1 },
	{ "damnation", L"Damnation", 2 },
	{ "ratrace", L"Rat Race", 3 },
	{ "prisoner", L"Prisoner", 4 },
	{ "hangemhigh", L"Hang 'Em High", 5 },
	{ "chillout", L"Chill Out", 6 },
	{ "carousel", L"Derelict", 7 },
	{ "boardingaction", L"Boarding Action", 8 },
	{ "bloodgulch", L"Blood Gulch", 9 },
	{ "wizard", L"Wizard", 10 },
	{ "putput", L"Chiron TL-34", 11 },
	{ "longest", L"Longest", 12 },
	{ "dangercanyon", L"Danger Canyon", 9 },
	{ "deathisland", L"Death Island", 1 },
	{ "gephyrophobia", L"Gephyrophobia", 2 },
	{ "icefields", L"Ice Fields", 1 },
	{ "infinity", L"Infinity", 9 },
	{ "timberland", L"Timberland", 9 },
};

/* ---------- prototypes */

char const *cache_files_map_directory(void);

/* ---------- globals */

static struct ui_map_entry ui_map_list[MAXIMUM_MAP_LIST];
static char *ui_map_list_names_array[MAXIMUM_MAP_LIST];
static long ui_map_list_count_value;

/* ---------- private code */

/* (the game's wide characters are 16 bits: not the C library's) */
static void wide_copy(
	wchar_t *destination,
	long size,
	wchar_t const *source)
{
	long index;

	for (index = 0; index < size - 1 && source[index]; index++)
		destination[index] = source[index];
	destination[index] = 0;
}

static void wide_append(
	wchar_t *destination,
	long size,
	wchar_t const *source)
{
	long length = 0;

	while (length < size - 1 && destination[length])
		length++;
	wide_copy(destination + length, size - length, source);
}

/* whether the file is a Custom Edition multiplayer map: a cache file of
version 609 whose type is multiplayer */
static boolean ce_map_is_multiplayer(
	char const *path)
{
	unsigned long header[0x19];
	unsigned long bytes_read = 0;
	HANDLE file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	boolean result = FALSE;

	if (file == INVALID_HANDLE_VALUE)
		return FALSE;
	if (ReadFile(file, header, sizeof(header), &bytes_read, NULL) && bytes_read == sizeof(header))
	{
		/* ('head', version, ..., the type a short at 0x60: 1 multiplayer) */
		result = header[0] == 'head' && header[1] == 609 && (header[0x60 / 4] & 0xffff) == 1;
	}
	CloseHandle(file);
	return result;
}

static void add_entry(
	char const *map_name,
	wchar_t const *display_name,
	wchar_t const *lobby_name,
	short xbox_index,
	short picture_index)
{
	struct ui_map_entry *entry;

	if (ui_map_list_count_value >= MAXIMUM_MAP_LIST)
		return;
	entry = &ui_map_list[ui_map_list_count_value];
	snprintf(entry->map_name, sizeof(entry->map_name), "%s", map_name);
	wide_copy(entry->display_name, DISPLAY_NAME_LENGTH, display_name);
	wide_copy(entry->lobby_name, DISPLAY_NAME_LENGTH, lobby_name);
	entry->xbox_index = xbox_index;
	entry->picture_index = picture_index;
	ui_map_list_names_array[ui_map_list_count_value] = entry->map_name;
	ui_map_list_count_value++;
}

/* ---------- public code */

/* the list anew: the Xbox's maps (the game's thirteen names, in its order),
then the Custom Edition maps found */
void ui_map_list_refresh(
	char *const *xbox_maps)
{
	short index;

	ui_map_list_count_value = 0;
	for (index = 0; index < XBOX_MAP_COUNT; index++)
		add_entry(xbox_maps[index], L"", L"", index, index);
	{
		char pattern[256];
		WIN32_FIND_DATAA data;
		HANDLE find;

		snprintf(pattern, sizeof(pattern), "%sce\\*.map", cache_files_map_directory());
		find = FindFirstFileA(pattern, &data);
		if (find != INVALID_HANDLE_VALUE)
		{
			char files[MAXIMUM_MAP_LIST][MAP_NAME_LENGTH];
			long file_count = 0, file;

			do
			{
				char path[512];
				size_t length = strlen(data.cFileName);

				if (length < 5 || _stricmp(data.cFileName + length - 4, ".map") || length - 4 >= MAP_NAME_LENGTH - 3)
					continue;
				snprintf(path, sizeof(path), "%sce\\%s", cache_files_map_directory(), data.cFileName);
				if (!ce_map_is_multiplayer(path) || file_count >= MAXIMUM_MAP_LIST)
					continue;
				snprintf(files[file_count], MAP_NAME_LENGTH, "%.*s", (int)(length - 4), data.cFileName);
				file_count++;
			}
			while (FindNextFileA(find, &data));
			CloseHandle(find);
			/* (in the order of their names, as the list shows them) */
			qsort(files, file_count, MAP_NAME_LENGTH, (int (*)(void const *, void const *))_stricmp);
			for (file = 0; file < file_count; file++)
			{
				char map_name[MAP_NAME_LENGTH];
				wchar_t display_name[DISPLAY_NAME_LENGTH];
				wchar_t lobby_name[DISPLAY_NAME_LENGTH];
				wchar_t const *name = NULL;
				short picture_index = 9;
				short known;

				for (known = 0; known < (short)NUMBEROF(ce_map_names); known++)
				{
					if (!_stricmp(files[file], ce_map_names[known].file))
					{
						name = ce_map_names[known].name;
						picture_index = ce_map_names[known].picture_index;
						break;
					}
				}
				if (name)
				{
					wide_copy(display_name, DISPLAY_NAME_LENGTH, name);
				}
				else
				{
					/* (a map of another's making: its file's name) */
					long character;

					for (character = 0; files[file][character] && character < DISPLAY_NAME_LENGTH - 7; character++)
						display_name[character] = (wchar_t)(unsigned char)files[file][character];
					display_name[character] = 0;
				}
				wide_copy(lobby_name, DISPLAY_NAME_LENGTH, display_name);
				wide_append(lobby_name, DISPLAY_NAME_LENGTH, L" [CE]");
				wide_append(display_name, DISPLAY_NAME_LENGTH, L"\r\n[CE]");
				snprintf(map_name, sizeof(map_name), "%s@ce", files[file]);
				add_entry(map_name, display_name, lobby_name, NONE, picture_index);
			}
		}
	}
}

long ui_map_list_count(
	void)
{
	return ui_map_list_count_value;
}

/* the rows' map names, for the list widget's items */
char **ui_map_list_names(
	void)
{
	return ui_map_list_names_array;
}

/* the row of a map's name (as the list or a game has it), or NONE */
long ui_map_list_find(
	char const *map_name)
{
	long row;

	if (!map_name)
		return NONE;
	for (row = 0; row < ui_map_list_count_value; row++)
	{
		if (!_stricmp(map_name, ui_map_list[row].map_name))
			return row;
	}
	return NONE;
}

/* a row's string list index for one of its strings (_ui_map_list_string_*):
the Xbox's own, or this list's */
short ui_map_list_string_index(
	long row,
	short kind)
{
	if (row < 0 || row >= ui_map_list_count_value)
		return 0;
	if (ui_map_list[row].xbox_index != NONE)
		return ui_map_list[row].xbox_index;
	return (short)(UI_MAP_LIST_STRING_BASE + row * UI_MAP_LIST_STRINGS_PER_ROW + kind);
}

/* a row's bitmap frame: the Xbox map's whose picture it shows */
short ui_map_list_picture_index(
	long row)
{
	if (row < 0 || row >= ui_map_list_count_value)
		return 0;
	return ui_map_list[row].picture_index;
}

/* the text of a string list index of this list's (UI_MAP_LIST_STRING_BASE
and up), or NULL for any other */
wchar_t const *ui_map_list_text(
	short string_list_index)
{
	long row;

	if (string_list_index < UI_MAP_LIST_STRING_BASE)
		return NULL;
	row = (string_list_index - UI_MAP_LIST_STRING_BASE) / UI_MAP_LIST_STRINGS_PER_ROW;
	if (row >= ui_map_list_count_value)
		return NULL;
	switch ((string_list_index - UI_MAP_LIST_STRING_BASE) % UI_MAP_LIST_STRINGS_PER_ROW)
	{
	case _ui_map_list_string_description:
		return L"A Halo Custom\r\nEdition map";
	case _ui_map_list_string_lobby_name:
		return ui_map_list[row].lobby_name;
	default:
		return ui_map_list[row].display_name;
	}
}

#endif
