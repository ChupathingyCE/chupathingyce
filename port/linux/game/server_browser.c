/*
SERVER_BROWSER.C

The PC menus' Server Browser's games on Halo PC maps (halo_server_browser.h):
a game whose map is announced as <file>@ce (Custom Edition) or <file>@md
(HaloMD) is named as the menus' map list names the map (ui_map_list.c), and
joined only with the map in its family's folders (halo_map_families.h), on
a build with Halo PC map support (HALO_CUSTOM_EDITION), as the game list's
Online Games screen joins one (browser_screen.c's words).
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "main/main.h"
#include "text/unicode.h"
#include "halo_map_families.h"
#include "halo_server_browser.h"
#include "halo_ui_map_list.h"

#include <stdio.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* a map's file name, at most */
	MAP_FILE_LENGTH = 64,
	/* whether a family's folders have a game's map, asked again this often */
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
/* the last map asked of its family's folders, and the answer */
static struct
{
	char map[MAP_FILE_LENGTH + 4];
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

/* whether a game's map can be played here: an Xbox map, or a Halo PC map
in its family's folders, missing, or on a build without them (the folders
asked again for another map, or now and then) */
static short ce_map_state(char const *map)
{
	char file[MAP_FILE_LENGTH];
	short family = map_family_parse(map, file, sizeof(file));

	if (family == _map_family_xbox)
		return _ce_map_none;
#ifdef HALO_CUSTOM_EDITION
	if (strcmp(ce_map_check.map, map) || system_milliseconds() - ce_map_check.time > CE_MAP_CHECK_INTERVAL)
	{
		csstrncpy(ce_map_check.map, map, sizeof(ce_map_check.map) - 1);
		ce_map_check.map[sizeof(ce_map_check.map) - 1] = 0;
		ce_map_check.answer = ui_map_list_family_present(family, file) ? _ce_map_present : _ce_map_missing;
		ce_map_check.time = system_milliseconds();
	}
	return ce_map_check.answer;
#else
	return _ce_map_unsupported;
#endif
}

/* ---------- public code */

short server_browser_map_family(char const *map, wchar_t *text, short length)
{
	char file[MAP_FILE_LENGTH];
	short family = map_family_parse(map, file, sizeof(file));

	if (family == _map_family_xbox)
		return family;
	ui_map_list_family_name(family, file, text, length);
	if (!text[0])
		text_from_ascii("Unknown map", text, length);
	return family;
}

boolean server_browser_map_blocked(char const *map, wchar_t *message, short length)
{
	char file[MAP_FILE_LENGTH], text[MAP_FILE_LENGTH + 64];
	short family = map_family_parse(map, file, sizeof(file));

	switch (ce_map_state(map))
	{
	case _ce_map_missing:
		snprintf(text, sizeof(text), "Needs %s/%s.map to join", map_family_folder(family), file);
		break;
	case _ce_map_unsupported:
		snprintf(text, sizeof(text), "%s maps need ChupathingyCE with Halo PC map support.",
			family == _map_family_halomd ? "HaloMD" : "Halo PC");
		break;
	default:
		return FALSE;
	}
	text_from_ascii(text, message, length);
	return TRUE;
}
