/*
SERVER_BROWSER.C

The PC menus' Server Browser's games on Halo PC (Custom Edition) maps
(halo_server_browser.h): a game whose map is announced as <file>@ce is named
as the menus' map list names the map (ui_map_list.c), and joined only with
the map in maps\ce, on a build with Halo PC map support
(HALO_CUSTOM_EDITION), as the game list's Online Games screen joins one
(browser_screen.c's words).
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "main/main.h"
#include "text/unicode.h"
#include "halo_server_browser.h"
#include "halo_ui_map_list.h"

#include <stdio.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* a map's file name, at most */
	MAP_FILE_LENGTH = 64,
	/* whether maps\ce has a game's map, asked again this often */
	CE_MAP_CHECK_INTERVAL = 1000,
};

/* whether a game's map can be played here */
enum
{
	_ce_map_none,
	_ce_map_present,
	_ce_map_missing,
	_ce_map_unsupported,
};

/* ---------- globals */

#ifdef HALO_CUSTOM_EDITION
/* the last map asked of maps\ce, and the answer */
static struct
{
	char file[MAP_FILE_LENGTH];
	short answer;
	unsigned long time;
} ce_map_check;
#endif

/* ---------- private code */

static void text_from_ascii(char const *ascii, wchar_t *text, short length)
{
	short index;

	for (index = 0; ascii[index] && index < length - 1; index++)
		text[index] = (wchar_t)(unsigned char)ascii[index];
	text[index] = 0;
}

/* a game's map: its file's name (the path's last part, without a Halo PC
map's @ce), and whether it is a Halo PC map */
static boolean map_file(char const *path, char *file, long size)
{
	char const *base = path;
	char const *cursor;
	long length;
	boolean ce;

	for (cursor = path; *cursor; cursor++)
	{
		if (*cursor == '\\' || *cursor == '/')
			base = cursor + 1;
	}
	length = (long)strlen(base);
	ce = length >= 3 && base[length - 3] == '@' && (base[length - 2] | 0x20) == 'c' && (base[length - 1] | 0x20) == 'e';
	if (ce)
		length -= 3;
	snprintf(file, (size_t)size, "%.*s", (int)length, base);
	return ce;
}

/* whether a game's map can be played here: an Xbox map, or a Halo PC map
in maps\ce, missing, or on a build without them (maps\ce asked again for
another map, or now and then) */
static short ce_map_state(char const *map)
{
	char file[MAP_FILE_LENGTH];

	if (!map_file(map, file, sizeof(file)))
		return _ce_map_none;
#ifdef HALO_CUSTOM_EDITION
	if (strcmp(ce_map_check.file, file) || system_milliseconds() - ce_map_check.time > CE_MAP_CHECK_INTERVAL)
	{
		csstrncpy(ce_map_check.file, file, sizeof(ce_map_check.file) - 1);
		ce_map_check.file[sizeof(ce_map_check.file) - 1] = 0;
		ce_map_check.answer = ui_map_list_ce_present(file) ? _ce_map_present : _ce_map_missing;
		ce_map_check.time = system_milliseconds();
	}
	return ce_map_check.answer;
#else
	return _ce_map_unsupported;
#endif
}

/* ---------- public code */

boolean server_browser_map_halo_pc(char const *map, wchar_t *text, short length)
{
	char file[MAP_FILE_LENGTH];

	if (!map_file(map, file, sizeof(file)))
		return FALSE;
	ui_map_list_ce_name(file, text, length);
	if (!text[0])
		text_from_ascii("Unknown map", text, length);
	return TRUE;
}

boolean server_browser_map_blocked(char const *map, wchar_t *message, short length)
{
	char file[MAP_FILE_LENGTH], text[MAP_FILE_LENGTH + 32];

	switch (ce_map_state(map))
	{
	case _ce_map_missing:
		map_file(map, file, sizeof(file));
		snprintf(text, sizeof(text), "Needs maps/ce/%s.map to join", file);
		break;
	case _ce_map_unsupported:
		snprintf(text, sizeof(text), "Halo PC maps need ChupathingyCE with Halo PC map support.");
		break;
	default:
		return FALSE;
	}
	text_from_ascii(text, message, length);
	return TRUE;
}
